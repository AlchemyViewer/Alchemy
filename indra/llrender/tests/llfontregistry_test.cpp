/**
 * @file llfontregistry_test.cpp
 * @brief Unit tests for LLFontDescriptor + LLFontRegistry's parser,
 *        resolver, override path, and lookup helpers.
 *
 * Exercises the pure-CPU surface of llfontregistry — descriptor
 * normalization, init_from_xml, resolveFontReferences, applyFamilyOverrides,
 * nameToSize/getMatchingFontDesc/getClosestFontTemplate, getAvailableFamilies.
 * FreeType-backed integration is covered by alfontcolrv1_test.cpp.
 *
 * When the binary is built against llrenderheadless (BUILD_HEADLESS=ON,
 * which sets LL_MESA_HEADLESS=1), the trailing block at the bottom of
 * this file adds GL-requiring tests: createFont allocating an atlas,
 * getFont caching the LLFontGL pointer, destroyGL releasing textures.
 * Without LL_MESA_HEADLESS those tests compile out and the binary stays
 * pure-CPU.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontregistry.h"
#include "../llfontgl.h"

#include "llxmlnode.h"
#include "llsd.h"

#include "../test/lltut.h"

#include <cstring>

#if LL_MESA_HEADLESS
#  include "../llfontfreetype.h"
#  include "../llfontbitmapcache.h"
#  include "../llimagegl.h"
#  include "llheadlessgl_fixture.h"
#  include <cstdio>
#endif

// init_from_xml is declared inside LLFontRegistry as a public friend; the
// header doesn't otherwise expose its prototype. Forward-declare here so
// the parser tests can call it directly without having to set up
// gDirUtilp + a temp skin tree just to reach parseFontInfo.
bool init_from_xml(LLFontRegistry* registry, LLPointer<LLXMLNode> node);

namespace tut
{
    // No FreeType/GL needed for any test in this file; the registry's only
    // construction-time side-effect is caching LLWindow's dynamic fallback
    // list, which is a static query that's safe to run anywhere llwindow is
    // linked. We skip LLFontManager::initClass() entirely.
    struct llfontregistry_data
    {
        // create_gl_textures=false matches the llui_libtest path: createFont
        // would still try to allocate atlases, but we never call createFont
        // in this file.
        LLFontRegistry reg{ /*create_gl_textures=*/false };

        // Parse an XML fragment into a <fonts>-rooted tree and feed it to
        // init_from_xml. Mirrors what parseFontInfo does per skin layer
        // (minus the disk + dir lookup).
        bool loadXml(const char* xml)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(xml, std::strlen(xml), root, nullptr))
                return false;
            if (root.isNull() || !root->hasName("fonts"))
                return false;
            return init_from_xml(&reg, root);
        }

        // Friend-access wrappers so test methods (which live on the derived
        // tut::object class, not on this fixture) can drive the private
        // resolveFontReferences / applyFamilyOverrides via inherited members.
        void resolve(const LLSD& overrides = LLSD::emptyMap())
        {
            reg.resolveFontReferences(overrides);
        }

        void applyOverrides(const LLSD& overrides)
        {
            reg.applyFamilyOverrides(overrides);
        }

        // Convenience: fetch the post-resolve template descriptor for
        // (family, style). Returns nullptr if absent.
        const LLFontDescriptor* templateFor(const std::string& family, U8 style = LLFontGL::NORMAL) const
        {
            for (const auto& kv : reg.mFontMap)
            {
                if (kv.first.isTemplate()
                    && kv.first.getName() == family
                    && kv.first.getStyle() == style)
                {
                    return &kv.first;
                }
            }
            return nullptr;
        }

        // File-name list at (family, style); empty vector on miss.
        std::vector<std::string> fileNames(const std::string& family, U8 style = LLFontGL::NORMAL) const
        {
            std::vector<std::string> out;
            const LLFontDescriptor* d = templateFor(family, style);
            if (!d) return out;
            for (const auto& f : d->getFontFiles())
                out.push_back(f.FileName);
            return out;
        }

        // Counts file_name occurrences across the chain — used by cycle/
        // diamond tests where we want to assert "appears exactly once."
        S32 countFile(const std::string& family, const std::string& file_name, U8 style = LLFontGL::NORMAL) const
        {
            S32 n = 0;
            for (const std::string& f : fileNames(family, style))
                if (f == file_name) ++n;
            return n;
        }

        // Look up the per-family <size> point size. Returns false when
        // either the family or the size_name isn't registered. Test bodies
        // aren't friends of LLFontRegistry so they can't reach mFamilySizes
        // directly — this fixture method is the friend-mediated hop.
        bool familySizePt(const std::string& family,
                          const std::string& size_name,
                          F32& out) const
        {
            auto fam_it = reg.mFamilySizes.find(family);
            if (fam_it == reg.mFamilySizes.end()) return false;
            auto sz_it = fam_it->second.find(size_name);
            if (sz_it == fam_it->second.end()) return false;
            out = sz_it->second;
            return true;
        }

        // Mirror the parse-time state wipe that LLFontRegistry::reload()
        // performs before re-parsing fonts.xml. The CPU-only test fixture
        // can't call reload() (it needs disk I/O), so this simulates the
        // teardown so a follow-up loadXml + resolve cycle exercises the
        // same code paths a runtime override change would. Skips the
        // shape-cache clear and fallback-instance bookkeeping that only
        // matter when the registry actually owns LLFontGL instances.
        void simulateReload()
        {
            reg.mFontSizes.clear();
            reg.mFamilySizes.clear();
            reg.mFamilyUses.clear();
            reg.mInheritFlags.clear();
            reg.mFamilyMeta.clear();
            reg.mFontMap.clear();
        }
    };

    // 128 slots: TUT defaults to 50; we already have 50+ tests and want
    // headroom before the silent-drop threshold bites again.
    typedef test_group<llfontregistry_data, 128> llfontregistry_test;
    typedef llfontregistry_test::object     llfontregistry_object;
    tut::llfontregistry_test llfontregistry_testcase("LLFontRegistry");

    // ===================================================================
    // Group 1: LLFontDescriptor (pure)
    // ===================================================================

    // normalize() pulls an embedded "Huge"/"Large"/etc. out of the name and
    // moves it into the size slot, leaving the family stem behind.
    template<> template<>
    void llfontregistry_object::test<1>()
    {
        LLFontDescriptor desc("SansSerifHuge", "", 0);
        LLFontDescriptor n = desc.normalize();
        ensure_equals("name keeps stem", n.getName(), std::string("SansSerif"));
        ensure_equals("size becomes Huge", n.getSize(), std::string("Huge"));
        ensure_equals("style stays NORMAL", (S32)n.getStyle(), 0);
    }

    // "Big" is the legacy alias for "Large" — verify the rewrite happens.
    template<> template<>
    void llfontregistry_object::test<2>()
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerifBig", "", 0).normalize();
        ensure_equals("Big maps to Large", n.getSize(), std::string("Large"));
        ensure_equals("name stripped", n.getName(), std::string("SansSerif"));
    }

    // "Bold"/"Italic" suffixes set the style flags AND get stripped from
    // the name (the leading inline comment in normalize() claims the name
    // is preserved, but the code definitively removes the substring).
    template<> template<>
    void llfontregistry_object::test<3>()
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerifBold", "", 0).normalize();
        ensure_equals("Bold stripped from name",
                      n.getName(), std::string("SansSerif"));
        ensure_equals("size defaults to Small (no size suffix in input)",
                      n.getSize(), std::string("Small"));
        ensure_equals("BOLD flag set", (S32)n.getStyle(), (S32)LLFontGL::BOLD);
    }

    // Bold + Italic combine; other style bits (UNDERLINE) get filtered.
    template<> template<>
    void llfontregistry_object::test<4>()
    {
        LLFontDescriptor desc("SansSerifBoldItalic", "",
                              static_cast<U8>(LLFontGL::UNDERLINE));
        LLFontDescriptor n = desc.normalize();
        ensure_equals("BOLD|ITALIC flags",
                      (S32)n.getStyle(),
                      (S32)(LLFontGL::BOLD | LLFontGL::ITALIC));
    }

    // "Monospace" is preserved as both name and size — the special-case
    // branch in normalize() exists because stripping it would leave "".
    template<> template<>
    void llfontregistry_object::test<5>()
    {
        LLFontDescriptor n = LLFontDescriptor("Monospace", "", 0).normalize();
        ensure_equals("name preserved", n.getName(), std::string("Monospace"));
        ensure_equals("size becomes Monospace", n.getSize(), std::string("Monospace"));
    }

    // Empty size + non-Monospace name → "Small" default.
    template<> template<>
    void llfontregistry_object::test<6>()
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerif", "", 0).normalize();
        ensure_equals("default size is Small", n.getSize(), std::string("Small"));
    }

    // Files attached to a descriptor survive the normalize() round-trip.
    template<> template<>
    void llfontregistry_object::test<7>()
    {
        LLFontDescriptor desc("SansSerif", "", 0);
        desc.addFontFile("Foo.ttf", EFontHinting::DEFAULT, 0, 0.f);
        LLFontDescriptor n = desc.normalize();
        ensure_equals("file count preserved", (S32)n.getFontFiles().size(), 1);
        ensure_equals("file name preserved",
                      n.getFontFiles()[0].FileName, std::string("Foo.ttf"));
    }

    // operator== compares (name, style, size). Different files but same
    // identity-tuple still compare equal — that's intentional, the file
    // list is treated as data the descriptor carries, not part of identity.
    template<> template<>
    void llfontregistry_object::test<8>()
    {
        LLFontDescriptor a("SansSerif", "Medium", 0);
        LLFontDescriptor b("SansSerif", "Medium", 0);
        a.addFontFile("a.ttf", EFontHinting::DEFAULT, 0, 0.f);
        b.addFontFile("b.ttf", EFontHinting::DEFAULT, 0, 0.f);
        ensure("equal regardless of files", a == b);
        ensure_equals("hash matches", hash_value(a), hash_value(b));
    }

    // operator< orders lexicographically by (name, style, size). Style
    // comparisons happen ahead of size — verify by holding name fixed.
    template<> template<>
    void llfontregistry_object::test<9>()
    {
        LLFontDescriptor a("SansSerif", "Medium", 0);
        LLFontDescriptor b("SansSerif", "Medium", LLFontGL::BOLD);
        ensure("style breaks tie before size", a < b);
        ensure("strict weak ordering", !(b < a));
    }

    // The "TEMPLATE" sentinel marks descriptors that exist only as the
    // post-XML template (no concrete point size yet).
    template<> template<>
    void llfontregistry_object::test<10>()
    {
        LLFontDescriptor t("SansSerif", "TEMPLATE", 0);
        LLFontDescriptor n("SansSerif", "Medium", 0);
        ensure("template sentinel detected", t.isTemplate());
        ensure("non-template not flagged", !n.isTemplate());
    }

    // ===================================================================
    // Group 2: Parser (init_from_xml)
    // ===================================================================

    // <font_size> populates the global size table; nameToSize hits it
    // when no per-family override applies.
    template<> template<>
    void llfontregistry_object::test<11>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font_size name='Large'  size='14.0'/>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        F32 sz = 0.f;
        ensure("Medium found", reg.nameToSize("Medium", sz));
        ensure_equals("Medium = 12", sz, 12.0f);
        ensure("Large found", reg.nameToSize("Large", sz));
        ensure_equals("Large = 14", sz, 14.0f);
        ensure("Huge missing", !reg.nameToSize("Huge", sz));
    }

    // New-format <style> blocks produce one descriptor per (family, style).
    // file_hinting+font_weight inherited from the family-level attributes
    // unless the file overrides — verify both routes.
    template<> template<>
    void llfontregistry_object::test<12>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inter' label='Inter' font_hinting='light' font_weight='400'>"
            "    <style name='NORMAL'>"
            "      <file>Inter-Regular.woff2</file>"
            "    </style>"
            "    <style name='BOLD'>"
            "      <file font_weight='700'>Inter-Regular.woff2</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const LLFontDescriptor* normal = templateFor("Inter", LLFontGL::NORMAL);
        const LLFontDescriptor* bold   = templateFor("Inter", LLFontGL::BOLD);
        ensure("NORMAL template present", normal != nullptr);
        ensure("BOLD template present",   bold != nullptr);
        ensure("NORMAL inherits family wght set",
               normal->getFontFiles()[0].mVarAxes.wght_set);
        ensure_equals("NORMAL inherits family weight 400",
                      normal->getFontFiles()[0].mVarAxes.wght, 400.f);
        ensure_equals("NORMAL inherits family hinting light",
                      (S32)normal->getFontFiles()[0].mHinting,
                      (S32)EFontHinting::LIGHT);
        ensure("BOLD has wght set after override",
               bold->getFontFiles()[0].mVarAxes.wght_set);
        ensure_equals("BOLD overrides weight to 700",
                      bold->getFontFiles()[0].mVarAxes.wght, 700.f);
        ensure_equals("BOLD inherits family hinting",
                      (S32)bold->getFontFiles()[0].mHinting,
                      (S32)EFontHinting::LIGHT);
    }

    // <size> children inside <font> populate the per-family override table
    // that nameToSize(family, size) consults before the global table.
    template<> template<>
    void llfontregistry_object::test<13>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font name='DejaVu' label='DejaVu Sans'>"
            "    <size name='Medium' size='8.6'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        F32 sz = 0.f;
        ensure("DejaVu Medium hits family override",
               reg.nameToSize("DejaVu", "Medium", sz));
        ensure_equals("DejaVu Medium = 8.6", sz, 8.6f);
        F32 sz2 = 0.f;
        ensure("Other family Medium falls back to global",
               reg.nameToSize("SansSerif", "Medium", sz2));
        ensure_equals("Global Medium unchanged", sz2, 12.0f);
    }

    // Family-level ligatures="on" propagates to every file unless the
    // file overrides. The flag lives on LLFontFileInfo (it controls the
    // freetype-level setAllowMonospaceLigatures call later on).
    template<> template<>
    void llfontregistry_object::test<14>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Mono' monospace='true' ligatures='on'>"
            "    <style name='NORMAL'><file>FiraCode.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const LLFontDescriptor* d = templateFor("Mono", LLFontGL::NORMAL);
        ensure("template present", d != nullptr);
        ensure("ligatures inherited",
               d->getFontFiles()[0].mMonospaceLigatures);
    }

    // load_collection on the family is inherited by every file; file-level
    // load_collection overrides per-file. Verify both paths.
    template<> template<>
    void llfontregistry_object::test<15>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='CJK' load_collection='true'>"
            "    <style name='NORMAL'><file>SourceHanSans.ttc</file></style>"
            "  </font>"
            "  <font name='Mono'>"
            "    <style name='NORMAL'><file load_collection='true'>OnlyOne.ttc</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        ensure("family-level load_collection inherited",
               templateFor("CJK")->getFontFiles()[0].mLoadCollection);
        ensure("file-level load_collection works alone",
               templateFor("Mono")->getFontFiles()[0].mLoadCollection);
    }

    // flags="bold" forces LLFontGL::BOLD into the file's flags so that
    // a single .woff2 carrying multiple weights still lights up callers'
    // BOLD checks.
    template<> template<>
    void llfontregistry_object::test<16>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inter'>"
            "    <style name='BOLD'>"
            "      <file flags='bold'>InterVariable.woff2</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const LLFontDescriptor* d = templateFor("Inter", LLFontGL::BOLD);
        ensure("template present", d != nullptr);
        ensure_equals("BOLD bit set on file flags",
                      d->getFontFiles()[0].mFlags & LLFontGL::BOLD,
                      (S32)LLFontGL::BOLD);
    }

    // unicode_ranges parses comma-separated U+lo-U+hi tokens into a
    // CharFunctor that returns true for matching codepoints.
    template<> template<>
    void llfontregistry_object::test<17>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Emoji' user_selectable='false'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='U+2600-U+27BF, U+1F525, 0x200D'>Noto-COLRv1.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const auto& cf = templateFor("Emoji")->getFontFiles()[0].CharFunctor;
        ensure("functor present", static_cast<bool>(cf));
        ensure("U+2600 inside range",   cf(0x2600));
        ensure("U+27BF inside range",   cf(0x27BF));
        ensure("U+27C0 outside range", !cf(0x27C0));
        ensure("U+1F525 single CP",     cf(0x1F525));
        ensure("0x200D via 0x prefix",  cf(0x200D));
        ensure("U+0000 nowhere covered", !cf(0));
    }

    // Malformed unicode_ranges tokens get skipped with a log, but the
    // remaining valid tokens still build a functor.
    template<> template<>
    void llfontregistry_object::test<18>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Frag'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='garbage, U+2600-U+27BF, U+ZZZ'>F.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const auto& cf = templateFor("Frag")->getFontFiles()[0].CharFunctor;
        ensure("functor still built", static_cast<bool>(cf));
        ensure("valid range survives", cf(0x2700));
    }

    // size_delta carries to the file unchanged. createFont adds it to
    // the resolved point size when loading a face.
    template<> template<>
    void llfontregistry_object::test<19>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='X'>"
            "    <style name='NORMAL'>"
            "      <file size_delta='-1.5'>X.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        ensure_equals("size_delta preserved",
                      templateFor("X")->getFontFiles()[0].mSizeDelta, -1.5f);
    }

    // Family metadata: label / user_selectable / monospace flow into
    // mFamilyMeta where getAvailableFamilies reads them.
    template<> template<>
    void llfontregistry_object::test<20>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inter' label='Inter Display'>"
            "    <style name='NORMAL'><file>I.woff2</file></style>"
            "  </font>"
            "  <font name='Mono' label='Mono' monospace='true'>"
            "    <style name='NORMAL'><file>M.woff2</file></style>"
            "  </font>"
            "  <font name='Hidden' user_selectable='false'>"
            "    <style name='NORMAL'><file>H.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        // Hidden filtered out; Inter + Mono remain. Stable sort by label.
        ensure_equals("Hidden filtered + 2 visible families", (S32)fams.size(), 2);
        bool sawInter = false, sawMono = false;
        for (const auto& f : fams)
        {
            if (f.name == "Inter") { sawInter = true; ensure_equals("Inter label", f.label, std::string("Inter Display")); }
            if (f.name == "Mono")  { sawMono  = true; }
        }
        ensure("Inter present", sawInter);
        ensure("Mono present",  sawMono);
    }

    // Two consecutive init_from_xml calls (mimicking cross-skin layering)
    // PREPEND the second layer's files onto an existing same-key entry.
    template<> template<>
    void llfontregistry_object::test<21>()
    {
        const char* base =
            "<fonts>"
            "  <font name='SansSerif'>"
            "    <style name='NORMAL'><file>Base.woff2</file></style>"
            "  </font>"
            "</fonts>";
        const char* overlay =
            "<fonts>"
            "  <font name='SansSerif'>"
            "    <style name='NORMAL'><file>Overlay.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("base parse",    loadXml(base));
        ensure("overlay parse", loadXml(overlay));
        auto names = fileNames("SansSerif");
        ensure_equals("two files merged", (S32)names.size(), 2);
        ensure_equals("overlay prepended", names[0], std::string("Overlay.woff2"));
        ensure_equals("base kept as fallback", names[1], std::string("Base.woff2"));
    }

    // ===================================================================
    // Group 3: Resolver (resolveFontReferences)
    // ===================================================================

    // Simple <use family="B"/> appends B's files onto A's chain at the
    // matching style.
    template<> template<>
    void llfontregistry_object::test<22>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("A");
        ensure_equals("A then B", (S32)names.size(), 2);
        ensure_equals("A own first", names[0], std::string("A.woff2"));
        ensure_equals("B appended",  names[1], std::string("B.woff2"));
    }

    // Style fallback: A wants B at BOLD, B only declares NORMAL.
    // Resolver should pull B's NORMAL files into A's BOLD chain.
    template<> template<>
    void llfontregistry_object::test<23>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='BOLD'><file>A-Bold.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B-Regular.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("A", LLFontGL::BOLD);
        ensure_equals("A-Bold then B-Regular fallback", (S32)names.size(), 2);
        ensure_equals("A's bold first", names[0], std::string("A-Bold.woff2"));
        ensure_equals("B NORMAL fallback", names[1], std::string("B-Regular.woff2"));
    }

    // Cycle A→B + B→A: each family's chain ends with the other's files
    // exactly once. Without the visited-set guard this would recurse
    // infinitely or duplicate.
    template<> template<>
    void llfontregistry_object::test<24>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <use family='A'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure_equals("A.woff2 once on A", countFile("A", "A.woff2"), 1);
        ensure_equals("B.woff2 once on A", countFile("A", "B.woff2"), 1);
        ensure_equals("B.woff2 once on B", countFile("B", "B.woff2"), 1);
        ensure_equals("A.woff2 once on B", countFile("B", "A.woff2"), 1);
    }

    // Diamond A→B, A→C, B→D, C→D: D must appear exactly once on A's
    // chain — the visited set is shared across the whole walk.
    template<> template<>
    void llfontregistry_object::test<25>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <use family='C'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <use family='D'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "  <font name='C'>"
            "    <use family='D'/>"
            "    <style name='NORMAL'><file>C.woff2</file></style>"
            "  </font>"
            "  <font name='D'>"
            "    <style name='NORMAL'><file>D.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure_equals("D.woff2 appears exactly once on A",
                      countFile("A", "D.woff2"), 1);
        ensure_equals("B.woff2 once on A", countFile("A", "B.woff2"), 1);
        ensure_equals("C.woff2 once on A", countFile("A", "C.woff2"), 1);
    }

    // Self-reference <use family="self"/> logs and is skipped.
    template<> template<>
    void llfontregistry_object::test<26>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='A'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure_equals("only A's own file remains",
                      countFile("A", "A.woff2"), 1);
        ensure_equals("nothing else added",
                      (S32)fileNames("A").size(), 1);
    }

    // <use family="ghost"/> targeting an undeclared family is skipped.
    template<> template<>
    void llfontregistry_object::test<27>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='ghost'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure_equals("undeclared use skipped",
                      (S32)fileNames("A").size(), 1);
    }

    // <use>-only family (no <style> blocks declared): style descriptors
    // are synthesized from whichever styles the referenced family covers.
    template<> template<>
    void llfontregistry_object::test<28>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Wrapper'>"
            "    <use family='Inner'/>"
            "  </font>"
            "  <font name='Inner'>"
            "    <style name='NORMAL'><file>Inner-R.woff2</file></style>"
            "    <style name='BOLD'><file>Inner-B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure("Wrapper NORMAL synthesized",
               templateFor("Wrapper", LLFontGL::NORMAL) != nullptr);
        ensure("Wrapper BOLD synthesized",
               templateFor("Wrapper", LLFontGL::BOLD) != nullptr);
        auto bold = fileNames("Wrapper", LLFontGL::BOLD);
        ensure_equals("Wrapper BOLD pulls Inner-B", (S32)bold.size(), 1);
        ensure_equals("...and the file is Inner-B",
                      bold[0], std::string("Inner-B.woff2"));
    }

    // inherit="true" on BOLD appends the family's NORMAL files.
    // Verify: BOLD's chain ends with NORMAL's contents.
    template<> template<>
    void llfontregistry_object::test<29>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='NORMAL'><file>F-R.woff2</file></style>"
            "    <style name='BOLD' inherit='true'><file>F-B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("F", LLFontGL::BOLD);
        ensure_equals("BOLD then NORMAL via inherit", (S32)names.size(), 2);
        ensure_equals("BOLD first", names[0], std::string("F-B.woff2"));
        ensure_equals("NORMAL appended", names[1], std::string("F-R.woff2"));
    }

    // <use> applies per-style before inherit; then inherit appends NORMAL's
    // post-<use> chain. F BOLD's pre-dedup chain would be [F-B, B, F-R, B]
    // — the same B file from both F BOLD's own <use> and from NORMAL's
    // post-<use> list inherited in. The dedup pass at the end of
    // resolveFontReferences drops the second B because chain[0..2] already
    // covers everything the duplicate would; it just wastes the renderer's
    // per-codepoint chain walk to look up a face it'll handle the same way.
    template<> template<>
    void llfontregistry_object::test<30>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>F-R.woff2</file></style>"
            "    <style name='BOLD' inherit='true'><file>F-B.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto bold = fileNames("F", LLFontGL::BOLD);
        ensure_equals("3 files post-dedup: own bold + B from <use> + NORMAL inherit",
                      (S32)bold.size(), 3);
        ensure_equals("F-B first",        bold[0], std::string("F-B.woff2"));
        ensure_equals("B from <use>",     bold[1], std::string("B.woff2"));
        ensure_equals("F-R inherited",    bold[2], std::string("F-R.woff2"));
        ensure_equals("duplicate B from NORMAL's <use> deduped out",
                      countFile("F", "B.woff2", LLFontGL::BOLD), 1);
    }

    // Calling resolve() a second time is a no-op — mFamilyUses /
    // mInheritFlags are consumed at the end of the first call. Per-
    // family sizes (mFamilySizes) and per-file source-family tags
    // survive — those are runtime data, not parse state.
    template<> template<>
    void llfontregistry_object::test<31>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto first = fileNames("A");
        resolve(); // second call must not re-append
        auto second = fileNames("A");
        ensure_equals("size unchanged across re-resolve",
                      (S32)first.size(), (S32)second.size());
    }

    // ===================================================================
    // Group 4: Overrides (applyFamilyOverrides)
    // ===================================================================

    // Family→family override: target's chain becomes [source files...,
    // target's own files] so source wins for the head face but target
    // fallbacks remain available behind it.
    template<> template<>
    void llfontregistry_object::test<32>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <style name='NORMAL'><file>Alt.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";
        applyOverrides(overrides);
        auto names = fileNames("UI");
        ensure_equals("source prepended + target preserved", (S32)names.size(), 2);
        ensure_equals("Alt prepended", names[0], std::string("Alt.woff2"));
        ensure_equals("UI fallback kept", names[1], std::string("UI.woff2"));
    }

    // Family→file override: the override value isn't a known family, so
    // it's treated as a literal file path. The synthesized LLFontFileInfo
    // inherits hinting/weight/flags from the target's first file so the
    // swap behaves like a drop-in replacement.
    template<> template<>
    void llfontregistry_object::test<33>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI' font_hinting='light' font_weight='400' ligatures='on'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "user-supplied.ttf";
        applyOverrides(overrides);
        const auto& files = templateFor("UI")->getFontFiles();
        ensure_equals("file added in front", files[0].FileName,
                      std::string("user-supplied.ttf"));
        ensure_equals("hinting inherited from target's first file",
                      (S32)files[0].mHinting, (S32)EFontHinting::LIGHT);
        ensure_equals("weight inherited", files[0].mVarAxes.wght, 400.f);
        ensure("ligatures inherited",      files[0].mMonospaceLigatures);
    }

    // Self-override (target == source string) is rejected — the override
    // would otherwise prepend the target's own files onto itself.
    template<> template<>
    void llfontregistry_object::test<34>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "UI";
        applyOverrides(overrides);
        auto names = fileNames("UI");
        ensure_equals("self-override skipped", (S32)names.size(), 1);
    }

    // Unknown target family: skipped, no phantom entries created.
    template<> template<>
    void llfontregistry_object::test<35>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Real'>"
            "    <style name='NORMAL'><file>R.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["Phantom"] = "Real";
        applyOverrides(overrides);
        ensure("no template materialized for Phantom",
               templateFor("Phantom") == nullptr);
        ensure_equals("Real untouched",
                      (S32)fileNames("Real").size(), 1);
    }

    // overridesEqual: snapshot of last-applied overrides updates with
    // each call to resolveFontReferences (which calls applyFamilyOverrides
    // and stashes the LLSD).
    template<> template<>
    void llfontregistry_object::test<36>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "  <font name='Alt'><style name='NORMAL'><file>A.woff2</file></style></font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        LLSD ovr1;
        ovr1["UI"] = "Alt";
        resolve(ovr1);
        ensure("identical maps compare equal", reg.overridesEqual(ovr1));
        LLSD ovr2;
        ovr2["UI"] = "Different";
        ensure("different value differs", !reg.overridesEqual(ovr2));
    }

    // applyOverrides prepends source files onto the target's chain. The
    // prepended source files retain their mSourceFamily so createFont's
    // per-file size pin lookup picks up the source family's <size> at
    // render time. nameToSize itself is NOT routed — the override target's
    // own <size> (or global) is what nameToSize returns. This pins the
    // post-redesign behavior: per-family size is absolute for THAT family
    // only, never propagated cross-family.
    template<> template<>
    void llfontregistry_object::test<37>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <size name='Custom' size='9.5'/>"
            "    <style name='NORMAL'><file>U.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <size name='Custom' size='11.5'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";
        applyOverrides(overrides);
        // nameToSize for UI returns UI's own pin; the override does NOT
        // route this lookup through Alt.
        F32 sz = 0.f;
        ensure(reg.nameToSize("UI", "Custom", sz));
        ensure_equals("UI Custom = own 9.5 (override does NOT route nameToSize)",
                      sz, 9.5f);
        // The override DID prepend Alt's file onto UI's chain, with
        // mSourceFamily=Alt — so per-file pin lookup at render time
        // would use Alt's <size> for that prepended file.
        const auto& files = templateFor("UI")->getFontFiles();
        ensure("UI chain has at least one file", !files.empty());
        ensure_equals("first file = Alt's prepended file",
                      files[0].FileName, std::string("A.woff2"));
        ensure_equals("prepended file tagged with source family Alt",
                      files[0].mSourceFamily, std::string("Alt"));
        // Alt's pin is intact for per-file lookup.
        F32 alt_pin = 0.f;
        ensure(familySizePt("Alt", "Custom", alt_pin));
        ensure_equals("Alt Custom pin = 11.5 (createFont uses this for Alt's file)",
                      alt_pin, 11.5f);

        // Clear the override; UI's own pin still applies.
        applyOverrides(LLSD());
        F32 sz2 = 0.f;
        ensure(reg.nameToSize("UI", "Custom", sz2));
        ensure_equals("UI Custom = own 9.5 (override cleared)",
                      sz2, 9.5f);
    }

    // ===================================================================
    // Group 5: Lookups
    // ===================================================================

    // getMatchingFontDesc applies normalize() to its argument, so passing
    // an unnormalized "SansSerifHuge" finds the {SansSerif, Huge} template.
    template<> template<>
    void llfontregistry_object::test<38>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='SansSerif'>"
            "    <style name='NORMAL'><file>S.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLFontDescriptor query("SansSerif", "TEMPLATE", 0);
        ensure("template lookup hits", reg.getMatchingFontDesc(query) != nullptr);
        LLFontDescriptor miss("Nope", "TEMPLATE", 0);
        ensure("absent name misses", reg.getMatchingFontDesc(miss) == nullptr);
    }

    // getClosestFontTemplate prefers the entry whose style bits overlap
    // most with the requested style, with BOLD as a tiebreaker.
    template<> template<>
    void llfontregistry_object::test<39>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='NORMAL'><file>F-N.woff2</file></style>"
            "    <style name='BOLD'><file>F-B.woff2</file></style>"
            "    <style name='ITALIC'><file>F-I.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // Asking for BOLD: exact match wins.
        LLFontDescriptor want_bold("F", "TEMPLATE", LLFontGL::BOLD);
        const LLFontDescriptor* best = reg.getClosestFontTemplate(want_bold);
        ensure("bold exact",  best != nullptr);
        ensure_equals("bold style", (S32)best->getStyle(), (S32)LLFontGL::BOLD);

        // Asking for BOLD|ITALIC with no BOLD|ITALIC entry: the BOLD
        // tiebreaker should prefer BOLD over ITALIC at equal bit-count.
        LLFontDescriptor want_bi("F", "TEMPLATE",
                                 (U8)(LLFontGL::BOLD | LLFontGL::ITALIC));
        best = reg.getClosestFontTemplate(want_bi);
        ensure("BI fallback hit", best != nullptr);
        ensure_equals("BOLD wins tiebreak",
                      (S32)best->getStyle(), (S32)LLFontGL::BOLD);
    }

    // getClosestFontTemplate REJECTS entries that have style bits the
    // request didn't ask for. Asking for NORMAL must not return a BOLD
    // template even if the family has only BOLD.
    template<> template<>
    void llfontregistry_object::test<40>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='BOLD'><file>F-B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLFontDescriptor want_normal("F", "TEMPLATE", 0);
        ensure("no NORMAL match returned",
               reg.getClosestFontTemplate(want_normal) == nullptr);
    }

    // Empty-family overload of nameToSize skips the per-family path
    // entirely and only consults the global table.
    template<> template<>
    void llfontregistry_object::test<41>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font name='F'>"
            "    <size name='Medium' size='9.0'/>"
            "    <style name='NORMAL'><file>F.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        F32 sz = 0.f;
        ensure("empty-family overload finds Medium",
               reg.nameToSize("Medium", sz));
        ensure_equals("...and gets the global value", sz, 12.0f);
        F32 sz2 = 0.f;
        reg.nameToSize("F", "Medium", sz2);
        ensure_equals("family overload still gets per-family value", sz2, 9.0f);
    }

    // ===================================================================
    // Group 6: getAvailableFamilies
    // ===================================================================

    // ANY filter returns both monospace and proportional, "default" and
    // user_selectable=false are excluded, and result is sorted by label.
    template<> template<>
    void llfontregistry_object::test<42>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Zebra' label='Zebra'>"
            "    <style name='NORMAL'><file>Z.woff2</file></style>"
            "  </font>"
            "  <font name='Apple' label='Apple'>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='Hidden' user_selectable='false'>"
            "    <style name='NORMAL'><file>H.woff2</file></style>"
            "  </font>"
            "  <font name='default'>"
            "    <style name='NORMAL'><file>D.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        ensure_equals("only 2 selectable families", (S32)fams.size(), 2);
        ensure_equals("sorted by label, Apple first",
                      fams[0].name, std::string("Apple"));
        ensure_equals("Zebra second",
                      fams[1].name, std::string("Zebra"));
    }

    // MONOSPACE filter returns only families with monospace="true";
    // PROPORTIONAL is the inverse.
    template<> template<>
    void llfontregistry_object::test<43>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Sans'>"
            "    <style name='NORMAL'><file>S.woff2</file></style>"
            "  </font>"
            "  <font name='Mono' monospace='true'>"
            "    <style name='NORMAL'><file>M.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        ensure_equals("MONOSPACE: 1 family", (S32)mono.size(), 1);
        ensure_equals("MONOSPACE returns Mono",
                      mono[0].name, std::string("Mono"));
        auto prop = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::PROPORTIONAL);
        ensure_equals("PROPORTIONAL: 1 family", (S32)prop.size(), 1);
        ensure_equals("PROPORTIONAL returns Sans",
                      prop[0].name, std::string("Sans"));
    }

    // Family without a label attribute defaults its UI label to the
    // family name.
    template<> template<>
    void llfontregistry_object::test<44>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Plain'>"
            "    <style name='NORMAL'><file>P.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        ensure_equals("one family", (S32)fams.size(), 1);
        ensure_equals("label defaults to name",
                      fams[0].label, std::string("Plain"));
    }

    // <use family='A'/> inside <font name='A'> — literal self-reference
    // by name, distinct from the 'self' alias path (test 26). The cycle
    // detector must skip both. The post-resolve chain should contain
    // only A's own files, with no duplicate insertion.
    template<> template<>
    void llfontregistry_object::test<45>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Mono'>"
            "    <use family='Mono'/>"
            "    <style name='NORMAL'><file>M.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("Mono");
        ensure_equals("self-reference by literal name doesn't duplicate files",
                      (S32)names.size(), 1);
        ensure_equals("only the family's own file in chain",
                      names[0], std::string("M.woff2"));
    }

    // Empty <fonts/> root with no children is a degenerate but valid
    // input — init_from_xml + resolve must complete without crashing
    // and leave the registry's font map empty. Pins safe handling of
    // an empty skin layer (e.g., when a skin overrides only the metric
    // table and leaves families to inherit from a lower layer).
    template<> template<>
    void llfontregistry_object::test<46>()
    {
        const char* xml = "<fonts></fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        ensure_equals("empty XML produces empty available-families list",
                      (S32)fams.size(), 0);
    }

    // applyOverrides called twice with different LLSD maps: the second
    // overrides the first wholesale rather than merging. Pins that
    // override application is replacement, not accumulation — a
    // regression that merged would compound stale routings. Verified
    // via the per-file source-family on the prepended head file (the
    // override target's chain head should reflect ONLY the most recent
    // override's source).
    template<> template<>
    void llfontregistry_object::test<47>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <size name='Custom' size='9.5'/>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='AltA'>"
            "    <size name='Custom' size='11.5'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='AltB'>"
            "    <size name='Custom' size='13.5'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        // First override: UI → AltA. UI's chain head becomes AltA's file.
        LLSD ovr1;
        ovr1["UI"] = "AltA";
        applyOverrides(ovr1);
        const auto& files_a = templateFor("UI")->getFontFiles();
        ensure("UI chain non-empty after first override", !files_a.empty());
        ensure_equals("UI head = AltA's file",
                      files_a[0].FileName, std::string("A.woff2"));
        ensure_equals("head's mSourceFamily = AltA",
                      files_a[0].mSourceFamily, std::string("AltA"));

        // Second override: UI → AltB. The earlier UI→AltA mapping must
        // be replaced, not merged — UI's head is now AltB.
        LLSD ovr2;
        ovr2["UI"] = "AltB";
        applyOverrides(ovr2);
        const auto& files_b = templateFor("UI")->getFontFiles();
        ensure("UI chain non-empty after second override", !files_b.empty());
        ensure_equals("UI head = AltB's file (replaced AltA)",
                      files_b[0].FileName, std::string("B.woff2"));
        ensure_equals("head's mSourceFamily = AltB",
                      files_b[0].mSourceFamily, std::string("AltB"));
    }

    // Override on a base family must propagate through <use> chains to
    // every dependent family. Regression for: applying an override to
    // SansSerifBase only modified SansSerifBase's own template, while
    // SansSerif and SansSerifLimitedEmoji had already baked in the
    // pre-override SansSerifBase files via collect_chain — so menus
    // (which use SansSerifLimitedEmoji) silently kept the old font even
    // after the user picked a new one in the font picker.
    //
    // Mirrors the production family graph:
    //   Base  -- direct file: Source.woff2
    //   Plain -- <use Base/> + <use Latin/>
    //   Menu  -- <use Base/> + <use Latin/> + <use Mark/>
    // Override Base → Custom must end up in BOTH Plain and Menu.
    template<> template<>
    void llfontregistry_object::test<48>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Custom'>"
            "    <style name='NORMAL'><file>Custom.woff2</file></style>"
            "  </font>"
            "  <font name='Source'>"
            "    <style name='NORMAL'><file>Source.woff2</file></style>"
            "  </font>"
            "  <font name='Latin'>"
            "    <style name='NORMAL'><file>Latin.woff2</file></style>"
            "  </font>"
            "  <font name='Mark'>"
            "    <style name='NORMAL'><file>Mark.woff2</file></style>"
            "  </font>"
            "  <font name='Base'>"
            "    <use family='Source'/>"
            "  </font>"
            "  <font name='Plain'>"
            "    <use family='Base'/>"
            "    <use family='Latin'/>"
            "  </font>"
            "  <font name='Menu'>"
            "    <use family='Base'/>"
            "    <use family='Latin'/>"
            "    <use family='Mark'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "Custom";
        resolve(overrides);

        // Direct target picks up the override.
        ensure_equals("Base[0] is the override file",
                      fileNames("Base").size() > 0u
                          ? fileNames("Base")[0]
                          : std::string(),
                      std::string("Custom.woff2"));
        // Both dependents see Custom.woff2 in their chain — that's the
        // load-bearing assertion. A regression to the old order
        // (overrides applied after <use> resolution) would leave them
        // without Custom.woff2 entirely.
        ensure("Plain chain includes Custom.woff2 via Base",
               countFile("Plain", "Custom.woff2") >= 1);
        ensure("Menu chain includes Custom.woff2 via Base",
               countFile("Menu", "Custom.woff2") >= 1);
        // The chain after the override-driven Base files must still
        // carry Source (Base's own <use>) and the sibling <use>s — the
        // override prepends, doesn't replace.
        ensure("Plain chain still has Source.woff2",
               countFile("Plain", "Source.woff2") >= 1);
        ensure("Plain chain still has Latin.woff2",
               countFile("Plain", "Latin.woff2") >= 1);
        ensure("Menu chain still has Mark.woff2",
               countFile("Menu", "Mark.woff2") >= 1);

        // Position pin: the override file MUST be at index 0 of every
        // dependent chain. The renderer treats position 0 as the head
        // face — anything later is a fallback that only fires when the
        // head's cmap returns notdef. If the override files end up
        // somewhere later than 0, the original Source.woff2 still wins
        // for every glyph it covers and the user observes "no change."
        const auto plain_files = fileNames("Plain");
        ensure("Plain chain non-empty", !plain_files.empty());
        ensure_equals("Plain[0] must be the override file",
                      plain_files.empty() ? std::string() : plain_files[0],
                      std::string("Custom.woff2"));
        const auto menu_files = fileNames("Menu");
        ensure("Menu chain non-empty", !menu_files.empty());
        ensure_equals("Menu[0] must be the override file",
                      menu_files.empty() ? std::string() : menu_files[0],
                      std::string("Custom.woff2"));
    }

    // Override propagation through <use> chains for BOLD style. Real
    // fonts.xml declares `inherit="true"` on BOLD variants, which
    // appends the parent NORMAL chain after the variant's own files.
    // Verify the override (which prepends to the BOLD template) still
    // ends up at position 0 of dependent chains for the BOLD style.
    template<> template<>
    void llfontregistry_object::test<49>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Custom'>"
            "    <style name='NORMAL'><file>Custom-Regular.woff2</file></style>"
            "    <style name='BOLD' inherit='true'><file flags='bold'>Custom-Bold.woff2</file></style>"
            "  </font>"
            "  <font name='Source'>"
            "    <style name='NORMAL'><file>Source-Regular.woff2</file></style>"
            "    <style name='BOLD' inherit='true'><file flags='bold'>Source-Bold.woff2</file></style>"
            "  </font>"
            "  <font name='Latin'>"
            "    <style name='NORMAL'><file>Latin.woff2</file></style>"
            "  </font>"
            "  <font name='Base'>"
            "    <use family='Source'/>"
            "  </font>"
            "  <font name='Menu'>"
            "    <use family='Base'/>"
            "    <use family='Latin'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "Custom";
        resolve(overrides);

        // BOLD variant of Menu must lead with the override's BOLD file.
        const auto menu_bold = fileNames("Menu", LLFontGL::BOLD);
        ensure("Menu BOLD chain non-empty", !menu_bold.empty());
        ensure_equals("Menu BOLD[0] must be override BOLD file",
                      menu_bold.empty() ? std::string() : menu_bold[0],
                      std::string("Custom-Bold.woff2"));
        // NORMAL variant unchanged invariant.
        const auto menu_normal = fileNames("Menu", LLFontGL::NORMAL);
        ensure_equals("Menu NORMAL[0] must be override NORMAL file",
                      menu_normal.empty() ? std::string() : menu_normal[0],
                      std::string("Custom-Regular.woff2"));
    }

    // Reload with a different override: first resolve applies override A,
    // simulated reload wipes parse-time state, second resolve applies
    // override B. Dependent chains must reflect override B and must NOT
    // carry residue from override A. Mirrors the runtime path where the
    // user picks one font in the picker, then picks a different one —
    // each setting change drives LLFontRegistry::reload(font_overrides)
    // which clears mFontMap before re-parsing.
    template<> template<>
    void llfontregistry_object::test<50>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='AltA'>"
            "    <style name='NORMAL'><file>AltA.woff2</file></style>"
            "  </font>"
            "  <font name='AltB'>"
            "    <style name='NORMAL'><file>AltB.woff2</file></style>"
            "  </font>"
            "  <font name='Source'>"
            "    <style name='NORMAL'><file>Source.woff2</file></style>"
            "  </font>"
            "  <font name='Base'>"
            "    <use family='Source'/>"
            "  </font>"
            "  <font name='Menu'>"
            "    <use family='Base'/>"
            "  </font>"
            "</fonts>";

        // First load: override Base -> AltA.
        ensure("first parse ok", loadXml(xml));
        LLSD ovr_a;
        ovr_a["Base"] = "AltA";
        resolve(ovr_a);

        const auto menu_after_a = fileNames("Menu");
        ensure("Menu chain non-empty after override A",
               !menu_after_a.empty());
        ensure_equals("Menu[0] is AltA after first override",
                      menu_after_a.empty() ? std::string() : menu_after_a[0],
                      std::string("AltA.woff2"));
        ensure("Menu chain has Source after first override",
               countFile("Menu", "Source.woff2") >= 1);

        // Simulate reload: wipe parse-time state, re-parse, apply
        // override B. AltA must be GONE from the chain — a regression
        // would leak the prior override into the new resolution.
        simulateReload();
        ensure("re-parse ok after simulated reload", loadXml(xml));
        LLSD ovr_b;
        ovr_b["Base"] = "AltB";
        resolve(ovr_b);

        const auto menu_after_b = fileNames("Menu");
        ensure("Menu chain non-empty after override B",
               !menu_after_b.empty());
        ensure_equals("Menu[0] is AltB after second override",
                      menu_after_b.empty() ? std::string() : menu_after_b[0],
                      std::string("AltB.woff2"));
        ensure_equals("AltA must NOT appear in Menu chain after replace",
                      countFile("Menu", "AltA.woff2"), 0);
        ensure("Menu chain still has Source after replace",
               countFile("Menu", "Source.woff2") >= 1);
        // Same checks for the direct target — Base itself must show
        // only the new override at chain[0], no residue from AltA.
        const auto base_after_b = fileNames("Base");
        ensure_equals("Base[0] is AltB after second override",
                      base_after_b.empty() ? std::string() : base_after_b[0],
                      std::string("AltB.woff2"));
        ensure_equals("AltA must NOT appear in Base chain after replace",
                      countFile("Base", "AltA.woff2"), 0);
    }

    // Reload-and-clear: existing registry has override A, then user picks
    // "(default)" in the picker which writes an empty overrides map.
    // Reload with empty overrides must restore the original chain — no
    // override file lingers from the prior resolution.
    template<> template<>
    void llfontregistry_object::test<51>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='AltA'>"
            "    <style name='NORMAL'><file>AltA.woff2</file></style>"
            "  </font>"
            "  <font name='Source'>"
            "    <style name='NORMAL'><file>Source.woff2</file></style>"
            "  </font>"
            "  <font name='Base'>"
            "    <use family='Source'/>"
            "  </font>"
            "  <font name='Menu'>"
            "    <use family='Base'/>"
            "  </font>"
            "</fonts>";

        ensure("first parse ok", loadXml(xml));
        LLSD ovr_a;
        ovr_a["Base"] = "AltA";
        resolve(ovr_a);
        ensure_equals("Menu[0] is AltA before clear",
                      fileNames("Menu").empty()
                          ? std::string()
                          : fileNames("Menu")[0],
                      std::string("AltA.woff2"));

        // Reload with empty overrides — picker's "(default)" sentinel.
        simulateReload();
        ensure("re-parse ok", loadXml(xml));
        resolve(LLSD::emptyMap());

        const auto menu = fileNames("Menu");
        ensure("Menu non-empty after clear", !menu.empty());
        ensure_equals("Menu[0] returns to Source after override clear",
                      menu[0], std::string("Source.woff2"));
        ensure_equals("AltA gone from Menu after clear",
                      countFile("Menu", "AltA.woff2"), 0);
    }

    // Dedup pin: when the override source is also reachable through the
    // target's own <use> chain, the resolved chain should NOT carry the
    // file twice. Reproduces the user-reported case where overriding
    // "SansSerifBase" → "DejaVu" produced [DejaVu(override), SourceSans,
    // DejaVu(via <use family="DejaVu"/>), Noto, CJK] — the second DejaVu
    // is functionally redundant since chain[0] already covers everything
    // the duplicate would, and the duplicated entry just wastes the
    // renderer's per-codepoint walk.
    template<> template<>
    void llfontregistry_object::test<52>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Source'>"
            "    <style name='NORMAL'><file>Source.woff2</file></style>"
            "  </font>"
            "  <font name='DejaVu'>"
            "    <style name='NORMAL'><file>DejaVu.woff2</file></style>"
            "  </font>"
            "  <font name='Base'>"
            "    <use family='Source'/>"
            "  </font>"
            "  <font name='Menu'>"
            "    <use family='Base'/>"
            "    <use family='DejaVu'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "DejaVu";
        resolve(overrides);

        // Without dedup, Menu would be [DejaVu(override), Source,
        // DejaVu(<use>), …]. With dedup, the second DejaVu is dropped.
        ensure_equals("Menu chain has DejaVu exactly once",
                      countFile("Menu", "DejaVu.woff2"), 1);
        ensure("Menu chain still has Source",
               countFile("Menu", "Source.woff2") >= 1);
        const auto menu = fileNames("Menu");
        ensure("Menu non-empty", !menu.empty());
        ensure_equals("Menu[0] is the override file",
                      menu[0], std::string("DejaVu.woff2"));
    }

    // Pure <use> diamond, no override. A reaches D twice — once through
    // B and once through C — and the dedup pass at the end of
    // resolveFontReferences must drop the second copy. Test 52 covers
    // the override + <use> shape; this one pins that the dedup logic
    // also operates on plain chain composition with no override
    // involved, so a future regression that gates dedup on "did
    // applyFamilyOverrides run?" would fail here.
    template<> template<>
    void llfontregistry_object::test<53>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='D'>"
            "    <style name='NORMAL'><file>D.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <use family='D'/>"
            "  </font>"
            "  <font name='C'>"
            "    <use family='D'/>"
            "  </font>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <use family='C'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        // A reaches D twice (via B and via C). Pre-dedup the chain
        // would be [D, D]; post-dedup [D] — first hit wins, second
        // copy dropped.
        ensure_equals("A chain has D exactly once",
                      countFile("A", "D.woff2"), 1);
        const auto a_files = fileNames("A");
        ensure_equals("A chain length post-dedup",
                      (S32)a_files.size(), 1);
    }

    // Two functored entries with the same filename must NOT collapse.
    // std::function targets can't be reliably compared for equality, so
    // the dedup predicate guards against false positives via
    // `if (a.CharFunctor || b.CharFunctor) return false;` — when either
    // side carries a functor, treat them as distinct. A regression that
    // weakened this to "compare via target_type or address" would
    // silently collapse two distinct unicode_ranges gates pointing at
    // the same multi-script TTF, losing the per-codepoint routing
    // gate for one of them.
    //
    // Two-gate scenario (both entries functored): the outer
    // `static_cast<bool>(a.CharFunctor) != static_cast<bool>(...)`
    // check passes (both are non-null), so the inner guard is the only
    // line keeping them apart.
    template<> template<>
    void llfontregistry_object::test<54>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='LatinGated'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='U+0000-U+00FF'>Shared.ttf</file>"
            "    </style>"
            "  </font>"
            "  <font name='EmojiGated'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='U+1F000-U+1F0FF'>Shared.ttf</file>"
            "    </style>"
            "  </font>"
            "  <font name='Combined'>"
            "    <use family='LatinGated'/>"
            "    <use family='EmojiGated'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        // Both entries reference the same filename and carry a non-null
        // CharFunctor. Dedup must keep both — collapsing would route
        // every codepoint through whichever functor survived, breaking
        // coverage for the dropped range.
        ensure_equals("Combined keeps both functored entries",
                      countFile("Combined", "Shared.ttf"), 2);
    }

    // simulateReload + re-parse with new sizes must update nameToSize for
    // both the global <font_size> table and per-family <size> children.
    // Live-fonts.xml-edit reload depends on this — without it, the rest
    // of reload() would swap freetypes at the OLD point sizes.
    template<> template<>
    void llfontregistry_object::test<55>()
    {
        const char* xml_v1 =
            "<fonts>"
            "  <font_size name='Foo' size='10.0'/>"
            "  <font name='Inter'>"
            "    <size name='Bar' size='8.0'/>"
            "    <style name='NORMAL'><file>Inter.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse v1 ok", loadXml(xml_v1));
        resolve();
        F32 sz = 0.f;
        ensure(reg.nameToSize("Inter", "Foo", sz));
        ensure_equals("v1: Foo (global) = 10.0", sz, 10.0f);
        ensure(reg.nameToSize("Inter", "Bar", sz));
        ensure_equals("v1: Bar (per-family) = 8.0", sz, 8.0f);

        // Now simulate a runtime fonts.xml edit: wipe state, re-parse.
        simulateReload();
        const char* xml_v2 =
            "<fonts>"
            "  <font_size name='Foo' size='20.0'/>"
            "  <font name='Inter'>"
            "    <size name='Bar' size='14.5'/>"
            "    <style name='NORMAL'><file>Inter.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse v2 ok", loadXml(xml_v2));
        resolve();
        ensure(reg.nameToSize("Inter", "Foo", sz));
        ensure_equals("v2: Foo (global) updated to 20.0", sz, 20.0f);
        ensure(reg.nameToSize("Inter", "Bar", sz));
        ensure_equals("v2: Bar (per-family) updated to 14.5", sz, 14.5f);
    }

    // ===================================================================
    // Group 6: Per-family <size> as absolute pin — no cross-family flow
    // ===================================================================

    // Use-only family's nameToSize falls through directly to the global
    // table. No head-face chain walk, no override-source routing. Pin
    // assertions stay on per-file rendering (createFont path), not on
    // chain-head pt resolution.
    template<> template<>
    void llfontregistry_object::test<56>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='HeadFace'>"
            "    <size name='Large' size='12.0'/>"  // own pin, would have been picked up by old walker
            "    <style name='NORMAL'><file>HeadFace.woff2</file></style>"
            "  </font>"
            "  <font name='UseOnly'>"
            "    <use family='HeadFace'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        F32 sz = 0.f;
        ensure(reg.nameToSize("UseOnly", "Large", sz));
        ensure_equals("UseOnly Large = global 11.0 (no chain walk)",
                      sz, 11.0f);
        // HeadFace's own <size> still applies for direct queries.
        ensure(reg.nameToSize("HeadFace", "Large", sz));
        ensure_equals("HeadFace Large = own 12.0", sz, 12.0f);
    }

    // AlchemyUIFontOverrides changes the file list but does NOT route
    // the override target's nameToSize through the override source.
    // Picking OpenDyslexic via override on SansSerifBase: nameToSize
    // for SansSerifBase still returns its own table or global —
    // OpenDyslexic's <size> applies only to OD's own files (verified
    // by the per-file pin tests below).
    template<> template<>
    void llfontregistry_object::test<57>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='OpenDyslexic'>"
            "    <size name='Large' size='12.0'/>"
            "    <style name='NORMAL'><file>OD.otf</file></style>"
            "  </font>"
            "  <font name='SansSerifBase'>"
            "    <style name='NORMAL'><file>SS.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // Baseline: SansSerifBase has no <size>, falls to global.
        F32 sz = 0.f;
        ensure(reg.nameToSize("SansSerifBase", "Large", sz));
        ensure_equals("baseline SansSerifBase Large = global 11.0",
                      sz, 11.0f);
        // Apply override; nameToSize result must NOT change.
        LLSD overrides;
        overrides["SansSerifBase"] = "OpenDyslexic";
        applyOverrides(overrides);
        F32 sz2 = 0.f;
        ensure(reg.nameToSize("SansSerifBase", "Large", sz2));
        ensure_equals("override DOES NOT route nameToSize through OD",
                      sz2, 11.0f);
    }

    // Per-family <size> on multiple distinct families: each pins its own
    // family's files. The data is keyed correctly per family.
    template<> template<>
    void llfontregistry_object::test<58>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='A'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <size name='Large' size='13.0'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        F32 a_sz = 0.f, b_sz = 0.f;
        ensure(familySizePt("A", "Large", a_sz));
        ensure(familySizePt("B", "Large", b_sz));
        ensure_equals("A Large pin = 8.0",  a_sz, 8.0f);
        ensure_equals("B Large pin = 13.0", b_sz, 13.0f);
    }

    // Per-family <size> from a <use>'d family is preserved on its files
    // when those files flow into the using family's chain. A <use>s B; B
    // has <size>; A's resolved chain has B's file with mSourceFamily=B,
    // and mFamilySizes[B] still has B's pin. createFont's per-file
    // lookup will use B's pin for B's file, regardless of A's chain pt.
    template<> template<>
    void llfontregistry_object::test<59>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='B'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const auto& a_files = templateFor("A")->getFontFiles();
        ensure_equals("A chain has 2 files", (S32)a_files.size(), 2);
        ensure_equals("A's own file tagged 'A'",
                      a_files[0].mSourceFamily, std::string("A"));
        ensure_equals("B's file tagged 'B' through <use>",
                      a_files[1].mSourceFamily, std::string("B"));
        F32 b_sz = 0.f;
        ensure("B's pin still in mFamilySizes",
               familySizePt("B", "Large", b_sz));
        ensure_equals("B's pin = 8.0", b_sz, 8.0f);
    }

    // Hot-reload of a per-family <size> pin: edit the pin on a fallback
    // family, simulateReload, re-parse, verify the new pin is reflected
    // in mFamilySizes. createFont's per-file lookup will then pick up
    // the new pin on the next load. Pins the runtime-edit path that
    // matters for fonts.xml live editing.
    template<> template<>
    void llfontregistry_object::test<62>()
    {
        const char* xml_v1 =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='B'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse v1 ok", loadXml(xml_v1));
        resolve();
        F32 v1 = 0.f;
        ensure(familySizePt("B", "Large", v1));
        ensure_equals("v1: B Large pin = 8.0", v1, 8.0f);

        // Edit and reload — the new pin should land in mFamilySizes.
        simulateReload();
        const char* xml_v2 =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='B'>"
            "    <size name='Large' size='13.5'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse v2 ok", loadXml(xml_v2));
        resolve();
        F32 v2 = 0.f;
        ensure(familySizePt("B", "Large", v2));
        ensure_equals("v2: B Large pin = 13.5 (hot-reloaded)", v2, 13.5f);
    }

    // ===================================================================
    // Group 8: Parser robustness + use-chain edge cases
    // ===================================================================

    // <font> without a `name` attribute (new format): processNewFormatFont
    // logs a warning and bails before registering anything. No template
    // entry is created; sibling well-formed fonts still parse cleanly.
    template<> template<>
    void llfontregistry_object::test<64>()
    {
        const char* xml =
            "<fonts>"
            "  <font label='nameless'>"
            "    <style name='NORMAL'><file>nameless.woff2</file></style>"
            "  </font>"
            "  <font name='Good'>"
            "    <style name='NORMAL'><file>Good.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        // Empty-name template not created. The well-formed sibling is.
        ensure("nameless not registered",
               templateFor("", LLFontGL::NORMAL) == nullptr);
        ensure("well-formed sibling parsed",
               templateFor("Good", LLFontGL::NORMAL) != nullptr);
    }

    // <font_size> missing either attribute is silently dropped (parser
    // checks both before insert). The well-formed entry parses normally.
    template<> template<>
    void llfontregistry_object::test<65>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='OnlyName'/>"
            "  <font_size size='10.0'/>"
            "  <font_size name='Both' size='13.0'/>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        F32 sz = 0.f;
        ensure("OnlyName not added",   !reg.nameToSize("OnlyName", sz));
        ensure("Both registered",       reg.nameToSize("Both", sz));
        ensure_equals("Both = 13.0",   sz, 13.0f);
    }

    // <use> without a `family` attribute logs a warning and is skipped.
    // The chain reflects only well-formed entries.
    template<> template<>
    void llfontregistry_object::test<66>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Target'>"
            "    <style name='NORMAL'><file>Target.woff2</file></style>"
            "  </font>"
            "  <font name='A'>"
            "    <use/>"                  // no family attribute -> dropped
            "    <use family='Target'/>"  // well-formed
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("A");
        ensure_equals("A own + Target via well-formed <use>",
                      (S32)names.size(), 2);
        ensure_equals("A own first",  names[0], std::string("A.woff2"));
        ensure_equals("Target second", names[1], std::string("Target.woff2"));
    }

    // <size> missing name or size is dropped from the per-family table
    // with a warning. Sibling well-formed <size> still applies.
    template<> template<>
    void llfontregistry_object::test<67>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Foo' size='10.0'/>"
            "  <font name='F'>"
            "    <size name='OnlyName'/>"
            "    <size size='5.0'/>"
            "    <size name='Foo' size='8.0'/>"
            "    <style name='NORMAL'><file>F.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        F32 sz = 0.f;
        ensure(reg.nameToSize("F", "Foo", sz));
        ensure_equals("F Foo = own 8.0 (well-formed entry takes effect)",
                      sz, 8.0f);
        ensure("Malformed name 'OnlyName' did not register on F",
               !reg.nameToSize("F", "OnlyName", sz));
    }

    // <style> with no <file> children produces an empty descriptor.
    // The empty template still registers as a host so the resolver can
    // hang chain references on it. The use-chain provides files; the
    // composite's nameToSize falls through to global because per-family
    // <size> never propagates across families.
    template<> template<>
    void llfontregistry_object::test<68>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='X' size='10.0'/>"
            "  <font name='Pickup'>"
            "    <size name='X' size='6.0'/>"
            "    <style name='NORMAL'><file>Pickup.woff2</file></style>"
            "  </font>"
            "  <font name='Empty'>"
            "    <use family='Pickup'/>"
            "    <style name='NORMAL'/>"  // no <file>: empty descriptor
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // Empty <style> -> the use-chain is the only file source.
        auto names = fileNames("Empty");
        ensure_equals("Empty pulled Pickup via <use>",
                      (S32)names.size(), 1);
        ensure_equals("Pickup file present", names[0], std::string("Pickup.woff2"));
        // No cross-family propagation: Empty's nameToSize falls through
        // to global. Pickup's <size X=6> would still pin the Pickup
        // file at 6pt at createFont time (per-file pin), but the chain
        // head's pt is global.
        F32 sz = 0.f;
        ensure(reg.nameToSize("Empty", "X", sz));
        ensure_equals("Empty's X = global 10.0 (no cross-family flow)",
                      sz, 10.0f);
        // Pickup's <size> survives — per-file pin at render time.
        F32 pickup_pin = 0.f;
        ensure(familySizePt("Pickup", "X", pickup_pin));
        ensure_equals("Pickup's X pin still 6.0 (createFont uses for Pickup file)",
                      pickup_pin, 6.0f);
        // The Pickup file in Empty's chain carries mSourceFamily=Pickup.
        const auto& files = templateFor("Empty")->getFontFiles();
        ensure_equals("Pickup file tagged with source family",
                      files[0].mSourceFamily, std::string("Pickup"));
    }

    // <style name="..."> with an unrecognized style string maps to NORMAL
    // (LLFontGL::getStyleFromString returns 0 for unknown names). The
    // resulting descriptor is keyed at NORMAL.
    template<> template<>
    void llfontregistry_object::test<69>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Quirky'>"
            "    <style name='WEIRD'><file>Q.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        ensure("'WEIRD' style mapped to NORMAL",
               templateFor("Quirky", LLFontGL::NORMAL) != nullptr);
        ensure("no BOLD template synthesized for unrecognized style",
               templateFor("Quirky", LLFontGL::BOLD) == nullptr);
    }

    // Three-level <use> cycle (A -> B -> C -> A). Visited set must
    // terminate the walk; each family contributes its own files exactly
    // once on every other family's chain.
    template<> template<>
    void llfontregistry_object::test<70>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <use family='C'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "  <font name='C'>"
            "    <use family='A'/>"
            "    <style name='NORMAL'><file>C.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // Each chain should contain exactly one of each file, in the
        // walk order starting from the queried family.
        ensure_equals("A: A.woff2 once", countFile("A", "A.woff2"), 1);
        ensure_equals("A: B.woff2 once", countFile("A", "B.woff2"), 1);
        ensure_equals("A: C.woff2 once", countFile("A", "C.woff2"), 1);
        ensure_equals("A chain length 3", (S32)fileNames("A").size(), 3);
    }

    // Deep linear chain (5 levels). Walker descends without depth
    // restriction; final chain has each family's file exactly once.
    template<> template<>
    void llfontregistry_object::test<71>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='F1'>"
            "    <use family='F2'/>"
            "    <style name='NORMAL'><file>F1.woff2</file></style>"
            "  </font>"
            "  <font name='F2'>"
            "    <use family='F3'/>"
            "    <style name='NORMAL'><file>F2.woff2</file></style>"
            "  </font>"
            "  <font name='F3'>"
            "    <use family='F4'/>"
            "    <style name='NORMAL'><file>F3.woff2</file></style>"
            "  </font>"
            "  <font name='F4'>"
            "    <use family='F5'/>"
            "    <style name='NORMAL'><file>F4.woff2</file></style>"
            "  </font>"
            "  <font name='F5'>"
            "    <style name='NORMAL'><file>F5.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto names = fileNames("F1");
        ensure_equals("F1 chain has all 5 files", (S32)names.size(), 5);
        ensure_equals("F1 own first",  names[0], std::string("F1.woff2"));
        ensure_equals("F2 next",       names[1], std::string("F2.woff2"));
        ensure_equals("F3 next",       names[2], std::string("F3.woff2"));
        ensure_equals("F4 next",       names[3], std::string("F4.woff2"));
        ensure_equals("F5 last",       names[4], std::string("F5.woff2"));
    }

    // inherit="true" on a non-NORMAL style with no NORMAL parent declared
    // logs a warning and is a no-op for that variant. The variant's chain
    // contains only its own files, not anything synthesized.
    template<> template<>
    void llfontregistry_object::test<72>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Lonely'>"
            "    <style name='BOLD' inherit='true'>"
            "      <file>Lonely-Bold.woff2</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto bold = fileNames("Lonely", LLFontGL::BOLD);
        ensure_equals("BOLD has only its own file (no NORMAL to inherit)",
                      (S32)bold.size(), 1);
        ensure_equals("Lonely-Bold present",
                      bold[0], std::string("Lonely-Bold.woff2"));
    }

    // Synthesis fires only for styles explicitly authored somewhere in
    // the <use> chain. A use-only with B/C declaring only NORMAL gets
    // ONLY a NORMAL host — BOLD / ITALIC / BOLD|ITALIC are not minted.
    // Lookups at unsynthesized styles fall through getClosestFontTemplate
    // (which accepts NORMAL as the closest match for any style request),
    // so request-time fallback handles the missing-style case rather
    // than synthesis aliasing every style to NORMAL.
    template<> template<>
    void llfontregistry_object::test<73>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <use family='C'/>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B-Reg.woff2</file></style>"
            "  </font>"
            "  <font name='C'>"
            "    <style name='NORMAL'><file>C-Reg.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        ensure("A NORMAL synthesized (NORMAL declared in chain)",
               templateFor("A", LLFontGL::NORMAL) != nullptr);
        ensure("A BOLD NOT synthesized (no BOLD in chain)",
               templateFor("A", LLFontGL::BOLD) == nullptr);
        ensure("A ITALIC NOT synthesized (no ITALIC in chain)",
               templateFor("A", LLFontGL::ITALIC) == nullptr);
        ensure("A BOLD|ITALIC NOT synthesized",
               templateFor("A",
                           static_cast<U8>(LLFontGL::BOLD | LLFontGL::ITALIC))
                   == nullptr);
        // NORMAL synthesizes from B and C:
        auto normal = fileNames("A", LLFontGL::NORMAL);
        ensure_equals("A NORMAL pulls both fallbacks",
                      (S32)normal.size(), 2);
    }

    // <size> entries round-trip through the parser into mFamilySizes,
    // and files contributed by this family carry mSourceFamily set to
    // the family name so createFont can later look up the per-family
    // pin at render time. Every <size> is an absolute pin — no force
    // attribute needed.
    template<> template<>
    void llfontregistry_object::test<75>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Pinned'>"
            "    <size name='Large' size='8.0'/>"
            "    <size name='Medium' size='7.0'/>"
            "    <style name='NORMAL'><file>Pinned.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        // <size> entries land in mFamilySizes.
        F32 large = 0.f, medium = 0.f;
        ensure(familySizePt("Pinned", "Large", large));
        ensure_equals("Large = 8.0",  large, 8.0f);
        ensure(familySizePt("Pinned", "Medium", medium));
        ensure_equals("Medium = 7.0", medium, 7.0f);

        // Files contributed by Pinned's <style> block carry mSourceFamily.
        auto names = fileNames("Pinned");
        ensure_equals("one file", (S32)names.size(), 1);
        const auto& files = templateFor("Pinned")->getFontFiles();
        ensure_equals("file's mSourceFamily = Pinned",
                      files[0].mSourceFamily, std::string("Pinned"));
    }

    // mSourceFamily is preserved when files flow through a <use> chain.
    // A <use>s B; B's file appears in A's chain with mSourceFamily=B.
    // This is the plumbing that lets createFont look up B's per-family
    // pin when B-contributed files render under A's head.
    template<> template<>
    void llfontregistry_object::test<76>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='B'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "  <font name='A'>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const auto& files = templateFor("A")->getFontFiles();
        ensure_equals("A chain length 2", (S32)files.size(), 2);
        ensure_equals("A's own file tagged 'A'",
                      files[0].mSourceFamily, std::string("A"));
        ensure_equals("B's file tagged 'B' (preserved through <use>)",
                      files[1].mSourceFamily, std::string("B"));
    }

    // Override SWAP between two distinct pinned sources, mirroring the
    // picker's reload-driven workflow. Boot with override Target ->
    // SourceA (pin Large=10), simulateReload + re-parse with override
    // Target -> SourceB (pin Large=14). The reload wipes mFontMap before
    // re-resolve, so the chain rebuilds cleanly with no SourceA leakage.
    // Verifies:
    //   - Target's chain head is SourceB's file post-swap.
    //   - Head's mSourceFamily is SourceB (per-file pin will hit B's 14).
    //   - SourceA's file is wholly absent from the post-swap chain.
    //   - Both source families' pins remain queryable in mFamilySizes
    //     after the reload (parse-time data, persists).
    //   - Target's own nameToSize is unaffected by either override —
    //     no cross-family routing.
    template<> template<>
    void llfontregistry_object::test<77>()
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='SourceA'>"
            "    <size name='Large' size='10.0'/>"
            "    <style name='NORMAL'><file>SourceA.woff2</file></style>"
            "  </font>"
            "  <font name='SourceB'>"
            "    <size name='Large' size='14.0'/>"
            "    <style name='NORMAL'><file>SourceB.woff2</file></style>"
            "  </font>"
            "  <font name='Target'>"
            "    <style name='NORMAL'><file>Target.woff2</file></style>"
            "  </font>"
            "</fonts>";

        // Boot state: override Target -> SourceA via reload-equivalent.
        ensure("parse v1 ok", loadXml(xml));
        LLSD ovr_a;
        ovr_a["Target"] = "SourceA";
        resolve(ovr_a);
        const auto& a_files = templateFor("Target")->getFontFiles();
        ensure("Target chain non-empty after first override",
               !a_files.empty());
        ensure_equals("Target head = SourceA's file",
                      a_files[0].FileName, std::string("SourceA.woff2"));
        ensure_equals("head's mSourceFamily = SourceA",
                      a_files[0].mSourceFamily, std::string("SourceA"));

        // Picker swap: production calls reload() which wipes mFontMap +
        // re-parses + re-resolves with the new override. simulateReload
        // mirrors the wipe; loadXml + resolve mirrors the re-parse +
        // re-resolve.
        simulateReload();
        ensure("parse v2 ok", loadXml(xml));
        LLSD ovr_b;
        ovr_b["Target"] = "SourceB";
        resolve(ovr_b);
        const auto& b_files = templateFor("Target")->getFontFiles();
        ensure("Target chain non-empty after swap",
               !b_files.empty());
        ensure_equals("Target head = SourceB's file (clean swap)",
                      b_files[0].FileName, std::string("SourceB.woff2"));
        ensure_equals("head's mSourceFamily = SourceB",
                      b_files[0].mSourceFamily, std::string("SourceB"));
        // After the reload-driven swap, SourceA's file is gone — no
        // leftover state from the prior override.
        ensure_equals("SourceA's file is not in the post-swap chain",
                      countFile("Target", "SourceA.woff2"), 0);

        // Both source families' pins remain queryable in mFamilySizes
        // post-reload. createFont uses the head's mSourceFamily to
        // decide its render pt: under ovr_a that hits SourceA's 10,
        // under ovr_b that hits SourceB's 14.
        F32 a_pin = 0.f, b_pin = 0.f;
        ensure(familySizePt("SourceA", "Large", a_pin));
        ensure_equals("SourceA pin = 10.0", a_pin, 10.0f);
        ensure(familySizePt("SourceB", "Large", b_pin));
        ensure_equals("SourceB pin = 14.0", b_pin, 14.0f);

        // Target's own nameToSize is unaffected by either override —
        // it falls through to global because Target has no own <size>.
        // Pins the "no cross-family routing" rule.
        F32 sz = 0.f;
        ensure(reg.nameToSize("Target", "Large", sz));
        ensure_equals("Target Large = global 11.0 (override does not route)",
                      sz, 11.0f);
    }

    // Override that points to a use-only family: applyFamilyOverrides
    // recognizes the source as a known family (via mFontMap template
    // presence) and prepends its resolved file list. Pins the case
    // where the override source is itself a composite use-only family
    // — the override should still propagate the source's chain head.
    template<> template<>
    void llfontregistry_object::test<74>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inner'>"
            "    <style name='NORMAL'><file>Inner.woff2</file></style>"
            "  </font>"
            "  <font name='Composite'>"  // use-only, source of override
            "    <use family='Inner'/>"
            "  </font>"
            "  <font name='Target'>"
            "    <style name='NORMAL'><file>Target.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["Target"] = "Composite";
        applyOverrides(overrides);
        auto names = fileNames("Target");
        // Override prepends Composite's resolved chain (= Inner's files).
        ensure("Target chain after override is non-empty", !names.empty());
        ensure_equals("Inner.woff2 prepended via Composite override",
                      names[0], std::string("Inner.woff2"));
        ensure("Target's own file kept as fallback behind override",
               countFile("Target", "Target.woff2") == 1);
    }

    // LLFontDescriptor equality and hash are keyed on (name, style, size)
    // only — file lists are template *content*, not identity. Pinning this
    // lets resolveFontReferences/applyFamilyOverrides mutate file lists by
    // erase + reinsert without rehashing collisions, and lets a request
    // descriptor (which carries no files) find the corresponding registry
    // template entry. If anyone changes operator== or hash_value to fold
    // file lists into the key, every chain mutation in the resolver becomes
    // a hash-bucket walk and lookups by request desc start missing. This
    // test fails noisily in that scenario.
    template<> template<>
    void llfontregistry_object::test<78>()
    {
        font_file_info_vec_t files_a;
        files_a.emplace_back("A.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);
        font_file_info_vec_t files_b;
        files_b.emplace_back("B.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);
        files_b.emplace_back("C.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);

        LLFontDescriptor a("SansSerif", "Small", 0, files_a);
        LLFontDescriptor b("SansSerif", "Small", 0, files_b);

        ensure("same (name,style,size) compares equal regardless of files",
               a == b);
        ensure_equals("same (name,style,size) hashes the same regardless of files",
                      hash_value(a), hash_value(b));

        // Sanity: vary each component, equality breaks.
        ensure("different name is not equal",
               !(a == LLFontDescriptor("Other", "Small", 0, files_a)));
        ensure("different style is not equal",
               !(a == LLFontDescriptor("SansSerif", "Small", LLFontGL::BOLD, files_a)));
        ensure("different size is not equal",
               !(a == LLFontDescriptor("SansSerif", "Large", 0, files_a)));

        // Lookup by no-files descriptor must hit a stored with-files entry.
        // This is what getMatchingFontDesc relies on at runtime.
        boost::unordered_map<LLFontDescriptor, int> m;
        m[a] = 1;
        LLFontDescriptor lookup("SansSerif", "Small", 0); // no files
        auto it = m.find(lookup);
        ensure("lookup by no-files descriptor finds a stored with-files entry",
               it != m.end());
    }

    // overridesEqual: extra coverage beyond test<36>'s identical/different
    // value pair. Pin the LLSD-comparison semantics initClass relies on
    // when deciding fast-DPI vs full-reload: empty-map and undefined-LLSD
    // are distinct values, and map equality is order-independent.
    template<> template<>
    void llfontregistry_object::test<79>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        // Resolve with empty-map; mLastFontOverrides becomes empty-map.
        LLSD empty_map = LLSD::emptyMap();
        resolve(empty_map);
        ensure("empty map equals empty map", reg.overridesEqual(empty_map));

        // Undefined LLSD is distinct from empty-map per llsd_equals.
        LLSD undef;
        ensure("empty map differs from undefined LLSD",
               !reg.overridesEqual(undef));

        // Map equality is keyed by (key,value) pairs regardless of LLSD's
        // internal iteration order. Pin so a re-ordered override map
        // (e.g. picker rebuilds the map in different order) doesn't trip
        // the fast-path and force a redundant full reload.
        simulateReload();
        ensure("re-parse ok", loadXml(xml));
        LLSD ovr1;
        ovr1["UI"] = "Alt";
        ovr1["Other"] = "Pick";
        LLSD ovr2;
        ovr2["Other"] = "Pick";
        ovr2["UI"] = "Alt";
        resolve(ovr1);
        ensure("LLSD map equality is order-independent",
               reg.overridesEqual(ovr2));
    }

    // sweepGlyphCaches must not dereference NULL-valued template entries.
    // mFontMap holds NULL-valued templates (size="TEMPLATE") between parse
    // and first getFont, and the production initClass path may tick the
    // sweep during early frames before any head exists. The NULL guard at
    // llfontregistry.cpp:1595 covers heads; this test pins the empty-
    // fallback-cache path too. Crash here = regression.
    template<> template<>
    void llfontregistry_object::test<80>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // No heads instantiated, no fallback-cache entries.
        reg.sweepGlyphCaches();
        ensure("registry survives sweep with templates only", true);
    }

    // OpenType variation axes plumbed through XML parse. Family-level
    // font_italic / font_width / font_slant cascade to child <file>
    // entries that don't override; per-file overrides win when present.
    template<> template<>
    void llfontregistry_object::test<81>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Demo' font_italic='1.0' font_width='87.5' font_slant='-12.0'>"
            "    <style name='NORMAL'>"
            "      <file>Demo-Regular.ttf</file>"
            "    </style>"
            "    <style name='BOLD'>"
            "      <file font_width='125.0'>Demo-Regular.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        const LLFontDescriptor* normal = templateFor("Demo", LLFontGL::NORMAL);
        ensure("NORMAL template present", normal != nullptr);
        const auto& nfiles = normal->getFontFiles();
        ensure_equals("NORMAL has one file", nfiles.size(), 1u);
        ensure("NORMAL inherits family ital",
               nfiles[0].mVarAxes.ital_set);
        ensure_equals("NORMAL ital == 1.0", nfiles[0].mVarAxes.ital, 1.0f);
        ensure("NORMAL inherits family wdth",
               nfiles[0].mVarAxes.wdth_set);
        ensure_equals("NORMAL wdth == 87.5", nfiles[0].mVarAxes.wdth, 87.5f);
        ensure("NORMAL inherits family slnt",
               nfiles[0].mVarAxes.slnt_set);
        ensure_equals("NORMAL slnt == -12.0", nfiles[0].mVarAxes.slnt, -12.0f);

        // Per-file override on wdth wins; ital/slnt still inherit.
        const LLFontDescriptor* bold = templateFor("Demo", LLFontGL::BOLD);
        ensure("BOLD template present", bold != nullptr);
        const auto& wfiles = bold->getFontFiles();
        ensure_equals("BOLD has one file", wfiles.size(), 1u);
        ensure_equals("BOLD overrides wdth to 125.0",
                      wfiles[0].mVarAxes.wdth, 125.0f);
        ensure("BOLD keeps inherited ital",
               wfiles[0].mVarAxes.ital_set);
        ensure_equals("BOLD inherited ital still 1.0",
                      wfiles[0].mVarAxes.ital, 1.0f);
        ensure("BOLD keeps inherited slnt",
               wfiles[0].mVarAxes.slnt_set);
        ensure_equals("BOLD inherited slnt still -12.0",
                      wfiles[0].mVarAxes.slnt, -12.0f);
    }

    // Files without any axis attribute leave ALFontVarAxes at its
    // default (no *_set flags). Pins the negative path that the cache
    // key + face-load skip rely on.
    template<> template<>
    void llfontregistry_object::test<82>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Plain'>"
            "    <style name='NORMAL'><file>Plain.ttf</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        const LLFontDescriptor* d = templateFor("Plain", LLFontGL::NORMAL);
        ensure("template present", d != nullptr);
        const auto& f = d->getFontFiles()[0];
        ensure("no wght", !f.mVarAxes.wght_set);
        ensure("no opsz", !f.mVarAxes.opsz_set);
        ensure("no ital", !f.mVarAxes.ital_set);
        ensure("no wdth", !f.mVarAxes.wdth_set);
        ensure("no slnt", !f.mVarAxes.slnt_set);
    }

    // font_optical_size cascades family -> file the same way
    // font_weight does. Per-file override wins; absence at file level
    // inherits the family value. Pins the new opsz axis plumbing.
    template<> template<>
    void llfontregistry_object::test<83>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Demo' font_optical_size='14.0'>"
            "    <style name='NORMAL'>"
            "      <file>Demo-Regular.ttf</file>"
            "    </style>"
            "    <style name='BOLD'>"
            "      <file font_optical_size='28.0'>Demo-Regular.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        const LLFontDescriptor* normal = templateFor("Demo", LLFontGL::NORMAL);
        ensure("NORMAL template present", normal != nullptr);
        const auto& nfiles = normal->getFontFiles();
        ensure_equals("NORMAL has one file", nfiles.size(), 1u);
        ensure("NORMAL inherits family opsz",
               nfiles[0].mVarAxes.opsz_set);
        ensure_equals("NORMAL opsz == 14.0",
                      nfiles[0].mVarAxes.opsz, 14.0f);

        const LLFontDescriptor* bold = templateFor("Demo", LLFontGL::BOLD);
        ensure("BOLD template present", bold != nullptr);
        const auto& bfiles = bold->getFontFiles();
        ensure_equals("BOLD has one file", bfiles.size(), 1u);
        ensure("BOLD has opsz set after override",
               bfiles[0].mVarAxes.opsz_set);
        ensure_equals("BOLD overrides opsz to 28.0",
                      bfiles[0].mVarAxes.opsz, 28.0f);
    }

    // replace_first=true on a family→family override drops the target's
    // head file before prepending the source. Mirror of test<32> with
    // the new map shape.
    template<> template<>
    void llfontregistry_object::test<84>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <style name='NORMAL'><file>Alt.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);
        auto names = fileNames("UI");
        ensure_equals("head replaced -> only one file remains",
                      (S32)names.size(), 1);
        ensure_equals("Alt is the head", names[0], std::string("Alt.woff2"));
    }

    // replace_first=true preserves orig_files[1..]. Verifies the
    // tail-merge contract: drop only the head, keep DejaVu/Emoji/CJK.
    template<> template<>
    void llfontregistry_object::test<85>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='DejaVu'>"
            "    <style name='NORMAL'><file>DejaVu.woff2</file></style>"
            "  </font>"
            "  <font name='UI'>"
            "    <use family='DejaVu'/>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <style name='NORMAL'><file>Alt.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        // Sanity: post-resolve UI's chain is [UI.woff2, DejaVu.woff2].
        auto pre_names = fileNames("UI");
        ensure_equals("UI starts with 2 files", (S32)pre_names.size(), 2);
        ensure_equals("UI head is UI.woff2",
                      pre_names[0], std::string("UI.woff2"));

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        ensure_equals("[Alt, DejaVu] -> 2 files",
                      (S32)names.size(), 2);
        ensure_equals("Alt is the head", names[0], std::string("Alt.woff2"));
        ensure_equals("DejaVu fallback preserved",
                      names[1], std::string("DejaVu.woff2"));
    }

    // replace_first=true with family→file override: the target's head
    // is dropped and the synthesized override file takes its slot;
    // tail still preserved.
    template<> template<>
    void llfontregistry_object::test<86>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='DejaVu'>"
            "    <style name='NORMAL'><file>DejaVu.woff2</file></style>"
            "  </font>"
            "  <font name='UI'>"
            "    <use family='DejaVu'/>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "user.ttf";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        ensure_equals("[user.ttf, DejaVu] -> 2 files",
                      (S32)names.size(), 2);
        ensure_equals("user.ttf head", names[0], std::string("user.ttf"));
        ensure_equals("DejaVu fallback preserved",
                      names[1], std::string("DejaVu.woff2"));
    }

    // replace_first=false in the map shape behaves identically to the
    // bare string shape: the override is prepended onto the chain and
    // the target's head stays as a fallback. Pin the legacy semantics
    // on the new map shape so it can't accidentally diverge.
    template<> template<>
    void llfontregistry_object::test<87>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <style name='NORMAL'><file>Alt.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = false;  // explicit false
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        ensure_equals("map(replace_first=false) -> [Alt, UI]",
                      (S32)names.size(), 2);
        ensure_equals("Alt prepended", names[0], std::string("Alt.woff2"));
        ensure_equals("UI head preserved as fallback (no replace)",
                      names[1], std::string("UI.woff2"));
    }

    // String shape continues to prepend (no replace). Regression guard
    // for the most common user-facing override path.
    template<> template<>
    void llfontregistry_object::test<88>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "  <font name='Alt'>"
            "    <style name='NORMAL'><file>Alt.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";  // bare string
        applyOverrides(overrides);
        auto names = fileNames("UI");
        ensure_equals("string shape -> [Alt, UI]", (S32)names.size(), 2);
        ensure_equals("Alt prepended", names[0], std::string("Alt.woff2"));
        ensure_equals("UI head preserved as fallback",
                      names[1], std::string("UI.woff2"));
    }

    // <use unicode_ranges="..."/> attaches a CharFunctor to every file
    // contributed by the named family. Pin: a consumer that does
    // `<use family="EmojiBase" unicode_ranges="U+1F000-U+1FFFF"/>`
    // ends up with EmojiBase's file gated by that range — accepts an
    // astral codepoint, rejects ASCII.
    template<> template<>
    void llfontregistry_object::test<89>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='EmojiBase'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='Consumer'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Consumer");
        ensure("template present", d != nullptr);
        const auto& files = d->getFontFiles();
        ensure_equals("Consumer has 2 files", (S32)files.size(), 2);
        ensure_equals("UI head", files[0].FileName, std::string("UI.woff2"));
        ensure_equals("Noto from EmojiBase", files[1].FileName, std::string("Noto.ttf"));
        ensure("Noto has a use-level functor",
               static_cast<bool>(files[1].CharFunctor));
        ensure("functor accepts U+1F600",
               files[1].CharFunctor((llwchar)0x1F600));
        ensure("functor rejects ASCII",
               !files[1].CharFunctor((llwchar)0x0041));
    }

    // Composition: file's own unicode_ranges intersects with the
    // use-level filter. Both gates must accept a codepoint for it to
    // pass.
    template<> template<>
    void llfontregistry_object::test<90>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Source'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='U+1F000-U+1FFFF'>F.ttf</file>"
            "    </style>"
            "  </font>"
            "  <font name='Consumer'>"
            "    <use family='Source' unicode_ranges='U+1F500-U+1F5FF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Consumer");
        ensure("template present", d != nullptr);
        const auto& files = d->getFontFiles();
        ensure_equals("Consumer has 1 file", (S32)files.size(), 1);
        ensure("composed functor accepts U+1F500 (in both)",
               files[0].CharFunctor((llwchar)0x1F500));
        ensure("composed functor rejects U+1F100 (in file, NOT in use)",
               !files[0].CharFunctor((llwchar)0x1F100));
        ensure("composed functor rejects U+1F900 (in use, NOT in file)",
               !files[0].CharFunctor((llwchar)0x1F900));
        ensure("composed functor rejects U+0041 (in neither)",
               !files[0].CharFunctor((llwchar)0x0041));
    }

    // Two consumers share one source with different filters. Each
    // resolved chain has its own functor — no cross-talk.
    template<> template<>
    void llfontregistry_object::test<91>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='EmojiBase'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='Broad'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "  <font name='Narrow'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F600-U+1F6FF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        const auto& bf = templateFor("Broad")->getFontFiles();
        const auto& nf = templateFor("Narrow")->getFontFiles();
        ensure_equals("Broad has 1 file", (S32)bf.size(), 1);
        ensure_equals("Narrow has 1 file", (S32)nf.size(), 1);
        // Broad accepts U+1F100; Narrow doesn't.
        ensure("Broad accepts U+1F100", bf[0].CharFunctor((llwchar)0x1F100));
        ensure("Narrow rejects U+1F100", !nf[0].CharFunctor((llwchar)0x1F100));
        // Both accept U+1F600 (in their own range).
        ensure("Broad accepts U+1F600", bf[0].CharFunctor((llwchar)0x1F600));
        ensure("Narrow accepts U+1F600", nf[0].CharFunctor((llwchar)0x1F600));
    }

    // Single override on the source family propagates to every consumer
    // with each consumer's filter intact. This is the load-bearing
    // motivation for use-level filters: one override entry, two chains
    // updated, each with its own glyph gate.
    template<> template<>
    void llfontregistry_object::test<92>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='EmojiBase'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='Broad'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "  <font name='Narrow'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F600-U+1F6FF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        // Override at the SOURCE — EmojiBase gets prepended with Twemoji.
        // Apply BEFORE resolveFontReferences so the consumers' use chains
        // pick up the new EmojiBase head when collected. (The fixture's
        // `resolve()` calls resolveFontReferences which itself runs
        // applyFamilyOverrides first.)
        LLSD overrides;
        overrides["EmojiBase"] = "Twemoji.ttf";
        resolve(overrides);

        const auto& bf = templateFor("Broad")->getFontFiles();
        const auto& nf = templateFor("Narrow")->getFontFiles();
        // Each consumer sees BOTH files (Twemoji prepended onto the
        // EmojiBase chain by the override; Noto preserved behind it),
        // each with the consumer's filter applied.
        ensure_equals("Broad has 2 files (Twemoji + Noto)",
                      (S32)bf.size(), 2);
        ensure_equals("Broad head is Twemoji",
                      bf[0].FileName, std::string("Twemoji.ttf"));
        ensure_equals("Narrow has 2 files",
                      (S32)nf.size(), 2);
        ensure_equals("Narrow head is Twemoji",
                      nf[0].FileName, std::string("Twemoji.ttf"));
        // Filter still gates each consumer independently.
        ensure("Broad's Twemoji accepts U+1F100",
               bf[0].CharFunctor((llwchar)0x1F100));
        ensure("Narrow's Twemoji rejects U+1F100",
               !nf[0].CharFunctor((llwchar)0x1F100));
    }

    // Combined replace_first + EmojiBase: { value: "Twemoji.ttf",
    // replace_first: true } on EmojiBase fully displaces Noto rather
    // than prepending in front of it. Each consumer's chain ends up
    // with just the override file gated by the consumer's filter.
    template<> template<>
    void llfontregistry_object::test<93>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='EmojiBase'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='Broad'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "  <font name='Narrow'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F600-U+1F6FF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Twemoji.ttf";
        entry["replace_first"] = true;
        overrides["EmojiBase"] = entry;
        resolve(overrides);

        const auto& bf = templateFor("Broad")->getFontFiles();
        const auto& nf = templateFor("Narrow")->getFontFiles();
        // replace_first dropped Noto from EmojiBase's chain; consumers
        // pull just Twemoji through their respective use-level filters.
        ensure_equals("Broad has 1 file (Noto displaced)",
                      (S32)bf.size(), 1);
        ensure_equals("Broad head is Twemoji",
                      bf[0].FileName, std::string("Twemoji.ttf"));
        ensure_equals("Narrow has 1 file (Noto displaced)",
                      (S32)nf.size(), 1);
        ensure_equals("Narrow head is Twemoji",
                      nf[0].FileName, std::string("Twemoji.ttf"));
        ensure("Broad's Twemoji accepts U+1F100",
               bf[0].CharFunctor((llwchar)0x1F100));
        ensure("Narrow's Twemoji rejects U+1F100",
               !nf[0].CharFunctor((llwchar)0x1F100));
        ensure("Both accept U+1F600",
               bf[0].CharFunctor((llwchar)0x1F600)
            && nf[0].CharFunctor((llwchar)0x1F600));
    }

    // No-cascade regression: a use-level filter applies ONLY to the
    // named family's own files. When that family <use>s another, the
    // filter does NOT propagate to the inner use's files. This is the
    // explicit design rule (filter is local to the use site) — pin it
    // so a future "fix" that propagates the filter through the chain
    // doesn't silently change semantics for existing chains.
    template<> template<>
    void llfontregistry_object::test<94>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inner'>"
            "    <style name='NORMAL'><file>Inner.ttf</file></style>"
            "  </font>"
            "  <font name='Mid'>"
            "    <use family='Inner'/>"
            "    <style name='NORMAL'><file>Mid.ttf</file></style>"
            "  </font>"
            "  <font name='Outer'>"
            "    <use family='Mid' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Outer");
        ensure("Outer template present", d != nullptr);
        const auto& files = d->getFontFiles();
        // Outer pulls Mid's own files (Mid.ttf) PLUS Mid's <use> chain
        // (Inner.ttf). The filter applies only to Mid's direct files.
        ensure_equals("Outer has 2 files", (S32)files.size(), 2);

        // Locate the entries by filename rather than positional index —
        // collect_chain orders direct files before use-chain files
        // today, but the rule under test is functor presence, not
        // ordering. Find each by name and check its functor.
        const LLFontFileInfo* mid = nullptr;
        const LLFontFileInfo* inner = nullptr;
        for (const auto& f : files)
        {
            if (f.FileName == "Mid.ttf")   mid = &f;
            if (f.FileName == "Inner.ttf") inner = &f;
        }
        ensure("Mid.ttf contributed",   mid   != nullptr);
        ensure("Inner.ttf contributed", inner != nullptr);

        // Mid is filtered (use-level filter applied to its own files).
        ensure("Mid has functor",            static_cast<bool>(mid->CharFunctor));
        ensure("Mid functor accepts U+1F600", mid->CharFunctor((llwchar)0x1F600));
        ensure("Mid functor rejects U+0041", !mid->CharFunctor((llwchar)0x0041));

        // Inner is NOT filtered — the use-level filter doesn't cascade.
        ensure("Inner has no functor (no cascade)",
               !static_cast<bool>(inner->CharFunctor));
    }

    // Baseline: a use-only EmojiBase that <use>s NotoEmoji, consumed
    // by Emoji with a unicode_ranges filter. Emoji's resolved chain
    // contains exactly one Noto file (contributed via the deepest <use>
    // step, with no filter — the use-level range is dropped on
    // recursion per the explicit design rule, see test<94>).
    template<> template<>
    void llfontregistry_object::test<98>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='NotoEmoji' emoji='true'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='EmojiBase'>"
            "    <use family='NotoEmoji'/>"
            "  </font>"
            "  <font name='Emoji'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        const auto& files = templateFor("Emoji")->getFontFiles();
        ensure_equals("Emoji has exactly 1 file (baseline)",
                      (S32)files.size(), 1);
        ensure_equals("the file is Noto.ttf",
                      files[0].FileName, std::string("Noto.ttf"));
        ensure("Noto fallback has NO functor (recursion dropped use-level filter)",
               !static_cast<bool>(files[0].CharFunctor));
    }

    // Override target already <use>s the override source: a use-only
    // EmojiBase that <use>s NotoEmoji, with override EmojiBase=NotoEmoji
    // (replace_first=true, matching the production picker write for
    // EmojiBase). Mirrors the production workflow Preferences > Themes
    // > Emoji Font: pick "Noto Emoji" while EmojiBase already <use>s
    // NotoEmoji underneath.
    //
    // Expected (no-op): Emoji's resolved chain stays at 1 file —
    // identical to the baseline above — because the override source is
    // what EmojiBase already pulls through <use>.
    //
    // Observed pre-fix: Emoji's chain ends up with TWO copies of the
    // file — one filtered (the override-prepended file gated by the
    // use-level range), one unfiltered (the <use NotoEmoji> recursion
    // drops the use-level filter). Step-4 dedup keeps both because
    // their CharFunctors differ. The duplicate filtered head perturbs
    // the shape-itemizer priority (priority-1 functored hit before the
    // priority-3 no-functor hit) and is observable as differing emoji
    // metrics through the codepoint vs shape paths.
    template<> template<>
    void llfontregistry_object::test<99>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='NotoEmoji' emoji='true'>"
            "    <style name='NORMAL'><file>Noto.ttf</file></style>"
            "  </font>"
            "  <font name='EmojiBase'>"
            "    <use family='NotoEmoji'/>"
            "  </font>"
            "  <font name='Emoji'>"
            "    <use family='EmojiBase' unicode_ranges='U+1F000-U+1FFFF'/>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "NotoEmoji";
        entry["replace_first"] = true;
        overrides["EmojiBase"] = entry;
        resolve(overrides);

        const auto& files = templateFor("Emoji")->getFontFiles();
        ensure_equals("Emoji chain length unchanged by no-op override "
                      "(EmojiBase already <use>s NotoEmoji)",
                      (S32)files.size(), 1);
    }

    // emoji="true" attribute parses; getAvailableFamilies(EMOJI) returns
    // only emoji-flagged families. PROPORTIONAL and MONOSPACE filter
    // emoji families OUT so they don't surface in regular text-font
    // pickers (a color-emoji file shouldn't appear as a UI choice).
    template<> template<>
    void llfontregistry_object::test<95>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Sans'>"
            "    <style name='NORMAL'><file>S.woff2</file></style>"
            "  </font>"
            "  <font name='Mono' monospace='true'>"
            "    <style name='NORMAL'><file>M.woff2</file></style>"
            "  </font>"
            "  <font name='NotoEmoji' emoji='true'>"
            "    <style name='NORMAL'><file>N.ttf</file></style>"
            "  </font>"
            "  <font name='Twemoji' emoji='true'>"
            "    <style name='NORMAL'><file>T.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();

        // EMOJI returns the two emoji families, sorted by label.
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        ensure_equals("EMOJI: 2 families", (S32)em.size(), 2);
        std::set<std::string> em_names;
        for (const auto& f : em) em_names.insert(f.name);
        ensure("EMOJI returns NotoEmoji", em_names.count("NotoEmoji") == 1);
        ensure("EMOJI returns Twemoji",   em_names.count("Twemoji")   == 1);

        // PROPORTIONAL excludes both monospace AND emoji.
        auto prop = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::PROPORTIONAL);
        ensure_equals("PROPORTIONAL: 1 family (just Sans)",
                      (S32)prop.size(), 1);
        ensure_equals("PROPORTIONAL returns Sans",
                      prop[0].name, std::string("Sans"));

        // MONOSPACE excludes emoji families even if they were also
        // marked monospace=true (emoji wins for filter routing).
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        ensure_equals("MONOSPACE: 1 family (just Mono)",
                      (S32)mono.size(), 1);
        ensure_equals("MONOSPACE returns Mono",
                      mono[0].name, std::string("Mono"));
    }

    // emoji="true" + monospace="true" on the same family routes to
    // EMOJI only, not MONOSPACE. Locks the "emoji wins" rule from the
    // FamilyFilter docs.
    template<> template<>
    void llfontregistry_object::test<96>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='WeirdMonoEmoji' monospace='true' emoji='true'>"
            "    <style name='NORMAL'><file>W.ttf</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        ensure_equals("EMOJI returns the dual-flagged family",
                      (S32)em.size(), 1);
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        ensure_equals("MONOSPACE excludes the dual-flagged family",
                      (S32)mono.size(), 0);
    }

    // user_selectable="false" still hides a family from EMOJI just like
    // it hides from PROPORTIONAL/MONOSPACE. Pin so internal aliases
    // (EmojiBase, Emoji, LimitedEmoji) don't surface even if some
    // future change marks them emoji=true.
    template<> template<>
    void llfontregistry_object::test<97>()
    {
        const char* xml =
            "<fonts>"
            "  <font name='Public' emoji='true'>"
            "    <style name='NORMAL'><file>P.ttf</file></style>"
            "  </font>"
            "  <font name='Internal' emoji='true' user_selectable='false'>"
            "    <style name='NORMAL'><file>I.ttf</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(xml));
        resolve();
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        ensure_equals("EMOJI: only the user-selectable family",
                      (S32)em.size(), 1);
        ensure_equals("EMOJI returns Public",
                      em[0].name, std::string("Public"));
    }

#if LL_MESA_HEADLESS
    // ===================================================================
    // Group 7: GL-requiring paths (LL_MESA_HEADLESS only)
    //
    // Companion fixture that owns a local LLFontRegistry with
    // create_gl_textures=true and shares the OSMesa GL context across
    // all tests via llheadlessgl_fixture.h. Pure-CPU tests above keep
    // their own create_gl_textures=false fixture so they don't pay for
    // GL setup when building under BUILD_HEADLESS.
    // ===================================================================

    namespace
    {
#ifndef LLFONT_TEST_APP_DIR
#  define LLFONT_TEST_APP_DIR ""
#endif
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

        constexpr const char* kAppDir   = LLFONT_TEST_APP_DIR;
        constexpr const char* kFontsDir = LLFONT_TEST_DATA_DIR;

        bool fileExists(const std::string& path)
        {
            if (FILE* f = LLFile::fopen(path.c_str(), LLFILE_MODE("rb")))
            {
                std::fclose(f);
                return true;
            }
            return false;
        }

        // Smallest fonts.xml fragment that resolves an "Inter" descriptor
        // at size "Small" to InterVariable.woff2 — enough to reach
        // createFont's GL-allocating path. No <use> / inherit / overrides
        // so the GL fixture's loadXml can skip resolveFontReferences (a
        // private method that the existing pure-CPU friend covers but
        // the GL fixture doesn't).
        constexpr const char* kInterXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
    }

    struct llfontregistry_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();

        LLFontRegistry reg{ /*create_gl_textures=*/true };

        llfontregistry_gl_data()
        {
            // sVertDPI / sHorizDPI need plausible values for createFont
            // to compute pixel sizes — the fixture isn't going through
            // LLFontGL::initClass which would set them. 96 DPI matches
            // the rest of the test suite.
            LLFontGL::sVertDPI  = 96.f;
            LLFontGL::sHorizDPI = 96.f;
            LLFontGL::sScaleX   = 1.f;
            LLFontGL::sScaleY   = 1.f;
            LLFontGL::sAppDir   = kAppDir;
        }

        bool loadXml(const char* xml)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(xml, std::strlen(xml), root, nullptr))
                return false;
            if (root.isNull() || !root->hasName("fonts"))
                return false;
            return init_from_xml(&reg, root);
        }

        // Public-from-fixture wrapper for resolveFontReferences. Test
        // bodies inherit fixture members but aren't friends of
        // LLFontRegistry themselves; this gives them an entry point.
        void resolve(const LLSD& overrides = LLSD::emptyMap())
        {
            reg.resolveFontReferences(overrides);
        }

        bool interFontPresent() const
        {
            return fileExists(std::string(kFontsDir) + "InterVariable.woff2");
        }

        // True iff `desc` has any entry (including a NULL slot) in mFontMap.
        // Used by the F-NULL1 regression test to verify that a failed
        // createFont does NOT poison the registry with a stale NULL — the
        // test body is not a friend of LLFontRegistry, this fixture method
        // is the friend-mediated hop.
        bool fontMapContains(const LLFontDescriptor& desc) const
        {
            return reg.mFontMap.find(desc) != reg.mFontMap.end();
        }

        // Mirror of llfontregistry_data::simulateReload for the GL fixture.
        // Wipes the parse-time state and mFontMap so a follow-up loadXml
        // re-builds templates from scratch — i.e. the same teardown the
        // production reload() does before re-parsing fonts.xml. Skips the
        // mFallbackInstanceCache pin/restore (the test re-creates everything
        // from scratch) and ALFontShaping::clearCache (the GL bring-up here
        // doesn't go through real shaping pipelines).
        void simulateReload()
        {
            // Drop heads first — they own LLFontGL pointers we own (the
            // registry's destructor will not be running yet).
            for (auto& kv : reg.mFontMap)
            {
                if (!kv.first.isTemplate())
                    delete kv.second;
            }
            reg.mFontSizes.clear();
            reg.mFamilySizes.clear();
            reg.mFamilyUses.clear();
            reg.mInheritFlags.clear();
            reg.mFamilyMeta.clear();
            reg.mFontMap.clear();
            reg.mFallbackInstanceCache.clear();
        }
    };

    typedef test_group<llfontregistry_gl_data> llfontregistry_gl_test;
    typedef llfontregistry_gl_test::object     llfontregistry_gl_object;
    tut::llfontregistry_gl_test llfontregistry_gl_testcase("LLFontRegistry-GL");

    // createFont with mCreateGLTextures=true must produce a head whose
    // bitmap cache, after rasterizing the ASCII range, has at least one
    // grayscale page bound to a live GL texture.
    template<> template<>
    void llfontregistry_gl_object::test<1>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves Inter/Small", font != nullptr);

        font->generateASCIIglyphs();

        const LLFontFreetype* ft = font->getFontFreetype();
        ensure("LLFontFreetype present", ft != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("at least one grayscale atlas page",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        ensure("page 0 LLImageGL present", page != nullptr);
        ensure("page 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // getFont(desc) must return the same LLFontGL pointer on a repeat
    // call — the registry caches by descriptor and a second hit must
    // not re-create or re-rasterize the head.
    template<> template<>
    void llfontregistry_gl_object::test<2>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* first = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("first getFont resolves", first != nullptr);

        LLFontGL* second = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("second getFont resolves", second != nullptr);
        ensure("repeat getFont returns the same LLFontGL pointer",
               first == second);
    }

    // destroyGL must drop GL textures and reset the face's bitmap cache
    // without removing registry entries — the LLFontGL pointer stays
    // valid (so widget caches don't dangle) and getFont still hits.
    // Reset semantics: ALFontFace::destroyGL → resetBitmapCache clears
    // the LLImageGL vector entirely, so post-destroyGL the page count
    // is 0 (not "page count preserved with texname=0"). The next render
    // through the face re-allocates fresh atlas pages.
    template<> template<>
    void llfontregistry_gl_object::test<3>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);

        font->generateASCIIglyphs();

        const LLFontBitmapCache* cache = font->getFontFreetype()->getFontBitmapCache();
        ensure("at least one atlas page before destroyGL",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
        ensure("texname live before destroyGL",
               cache->getImageGL(EFontGlyphType::Grayscale, 0)->getTexName() != 0);

        reg.destroyGL();

        ensure_equals("page vector cleared after destroyGL",
                      (S32)cache->getNumBitmaps(EFontGlyphType::Grayscale), 0);
        ensure("registry entry survived destroyGL — same LLFontGL pointer",
               reg.getFont(LLFontDescriptor("Inter", "Small", 0)) == font);
    }

    // End-to-end runtime size-change reload. Verify that wiping registry
    // state and re-parsing fonts.xml with a different point size for the
    // same descriptor actually produces a freetype rendering at the new
    // size. Indirect because LLFontFreetype::mPointSize is private —
    // ascender height is FreeType metrics * pointSize/units_per_EM, so
    // doubling the point size doubles the ascender (within rounding).
    template<> template<>
    void llfontregistry_gl_object::test<4>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }

        // v1: 12 pt
        constexpr const char* kV1 =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("v1 parse ok", loadXml(kV1));
        LLFontGL* font_v1 = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("v1 getFont resolves", font_v1 != nullptr);
        F32 ascender_v1 = font_v1->getAscenderHeight();
        ensure("v1 ascender > 0", ascender_v1 > 0.f);

        // Wipe + re-parse with 2x size.
        simulateReload();
        constexpr const char* kV2 =
            "<fonts>"
            "  <font_size name='Small' size='24.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("v2 parse ok", loadXml(kV2));
        LLFontGL* font_v2 = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("v2 getFont resolves", font_v2 != nullptr);
        F32 ascender_v2 = font_v2->getAscenderHeight();
        ensure("v2 ascender > 0", ascender_v2 > 0.f);

        // Doubling pt should ~double ascender (allow generous tolerance for
        // FreeType rounding: 1.7x lower bound is comfortably above 1.0x).
        ensure("ascender scales with reloaded point size",
               ascender_v2 > ascender_v1 * 1.7f);
    }

    // End-to-end override -> clear -> override-again with DIFFERENT font
    // files at DIFFERENT point sizes. Repros the user's runtime workflow:
    // start with AlchemyUIFontOverrides[SansSerifBase] = OpenDyslexic
    // (head face is OpenDyslexic.otf at OpenDyslexic's <size Large = 14>),
    // clear override (head face becomes Inter.woff2 at global Large = 11),
    // verify the head's freetype actually changed AND the point size moved.
    // Pins both halves of the loop: file swap on override change, AND
    // size routing through the head-face <use> chain.
    template<> template<>
    void llfontregistry_gl_object::test<5>()
    {
        const std::string od_path = std::string(kFontsDir) + "OpenDyslexic-Regular.otf";
        const std::string inter_path = std::string(kFontsDir) + "InterVariable.woff2";
        if (!fileExists(od_path) || !fileExists(inter_path))
        {
            skip("OpenDyslexic-Regular.otf or InterVariable.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='OpenDyslexic'>"
            "    <size name='Large' size='14.0'/>"
            "    <style name='NORMAL'><file>OpenDyslexic-Regular.otf</file></style>"
            "  </font>"
            "  <font name='SansSerifBase'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "  <font name='SansSerif'>"
            "    <use family='SansSerifBase'/>"
            "  </font>"
            "</fonts>";

        // First load: override SansSerifBase -> OpenDyslexic.
        ensure("parse v1 ok", loadXml(kXml));
        LLSD overrides;
        overrides["SansSerifBase"] = "OpenDyslexic";
        resolve(overrides);

        LLFontGL* font_with = reg.getFont(LLFontDescriptor("SansSerif", "Large", 0));
        ensure("getFont with override resolves", font_with != nullptr);
        F32 ascender_with = font_with->getAscenderHeight();
        ensure("ascender with override > 0", ascender_with > 0.f);

        // Wipe + re-parse with no override.
        simulateReload();
        ensure("parse v2 ok", loadXml(kXml));
        resolve(LLSD());

        LLFontGL* font_no = reg.getFont(LLFontDescriptor("SansSerif", "Large", 0));
        ensure("getFont without override resolves", font_no != nullptr);
        F32 ascender_no = font_no->getAscenderHeight();
        ensure("ascender without override > 0", ascender_no > 0.f);

        // The two states must produce visibly different ascender heights:
        //   With override: OpenDyslexic at 14pt — large intrinsic + larger pt.
        //   No override: Inter at 11pt — smaller intrinsic + smaller pt.
        // A sub-pixel difference would be a noisy test, so demand at least
        // 10% gap. In practice the spread is much wider, but 10% comfortably
        // separates "size changed" from "stuck on stale freetype".
        const F32 diff = (ascender_with > ascender_no)
                             ? (ascender_with - ascender_no)
                             : (ascender_no - ascender_with);
        ensure("override and no-override produce distinguishable ascenders",
               diff > ascender_no * 0.1f);
    }

    // Per-family <size> on a fallback family pins THAT family's files at
    // its declared point size even when the head wants a different chain
    // size. End-to-end: head (Inter) at chain size 16pt + fallback
    // (DejaVu) with <size Large=8>. Walk the head's mFallbackFonts chain
    // and find the DejaVu fallback — its getAscenderHeight() reflects
    // 8pt rendering, not 16pt. Every <size> is an absolute pin (no
    // force keyword needed).
    template<> template<>
    void llfontregistry_gl_object::test<6>()
    {
        const std::string inter_path = std::string(kFontsDir) + "InterVariable.woff2";
        const std::string dejavu_path = std::string(kFontsDir) + "DejaVuSans.woff2";
        if (!fileExists(inter_path) || !fileExists(dejavu_path))
        {
            skip("InterVariable.woff2 or DejaVuSans.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='16.0'/>"
            "  <font name='DejaVu'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "  <font name='Inter'>"
            "    <use family='DejaVu'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";

        ensure("parse ok", loadXml(kXml));
        resolve();
        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Large", 0));
        ensure("getFont resolves", font != nullptr);

        // Head is Inter at chain's 16pt.
        F32 head_ascender = font->getAscenderHeight();
        ensure("head ascender > 0", head_ascender > 0.f);

        // Find the DejaVu fallback in the chain. Its mPointSize should be
        // the per-family pin 8.0, NOT the chain's 16.0.
        const auto& fallbacks = font->getFontFreetype()->getFallbackFonts();
        const LLFontFreetype* dejavu_fallback = nullptr;
        for (const auto& fb : fallbacks)
        {
            if (fb.first && fb.first->getName().find("DejaVuSans") != std::string::npos)
            {
                dejavu_fallback = fb.first.get();
                break;
            }
        }
        ensure("DejaVu fallback present in chain", dejavu_fallback != nullptr);

        F32 fb_ascender = dejavu_fallback->getAscenderHeight();
        ensure("DejaVu fallback ascender > 0", fb_ascender > 0.f);

        // The pinned fallback at 8pt must be substantially smaller than
        // the head's 16pt rendering. Demand >40% smaller — comfortably
        // separates "pin applied" from "fell through to chain pt".
        ensure("pinned fallback ascender is substantially smaller than head",
               fb_ascender < head_ascender * 0.6f);
    }

    // End-to-end override SWAP between two distinct pinned sources, with
    // real fonts. Boot with override Target -> OpenDyslexic (pin Large=14),
    // get ascender. simulateReload + reapply override Target -> Inter
    // (pin Large=8), get ascender. Assert ascender shrinks substantially
    // — proves both per-file pins land on real freetypes through a
    // back-to-back override swap, with no leftover state from the first
    // source. Mirrors the picker workflow of moving between two custom
    // UI fonts that each declare their own preferred size.
    template<> template<>
    void llfontregistry_gl_object::test<7>()
    {
        const std::string od_path = std::string(kFontsDir) + "OpenDyslexic-Regular.otf";
        const std::string inter_path = std::string(kFontsDir) + "InterVariable.woff2";
        if (!fileExists(od_path) || !fileExists(inter_path))
        {
            skip("OpenDyslexic-Regular.otf or InterVariable.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='OpenDyslexic'>"
            "    <size name='Large' size='14.0'/>"
            "    <style name='NORMAL'><file>OpenDyslexic-Regular.otf</file></style>"
            "  </font>"
            "  <font name='Inter'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "  <font name='Target'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";

        // Override A: Target -> OpenDyslexic. Per-file pin lands head at 14pt.
        ensure("v1 parse ok", loadXml(kXml));
        LLSD ovr_a;
        ovr_a["Target"] = "OpenDyslexic";
        resolve(ovr_a);
        LLFontGL* font_a = reg.getFont(LLFontDescriptor("Target", "Large", 0));
        ensure("getFont under OpenDyslexic override", font_a != nullptr);
        F32 asc_a = font_a->getAscenderHeight();
        ensure("OpenDyslexic ascender > 0", asc_a > 0.f);

        // Swap: Target -> Inter (pin Large=8). Head must reload at 8pt
        // with Inter's intrinsics — substantially smaller than 14pt OD.
        simulateReload();
        ensure("v2 parse ok", loadXml(kXml));
        LLSD ovr_b;
        ovr_b["Target"] = "Inter";
        resolve(ovr_b);
        LLFontGL* font_b = reg.getFont(LLFontDescriptor("Target", "Large", 0));
        ensure("getFont under Inter override", font_b != nullptr);
        F32 asc_b = font_b->getAscenderHeight();
        ensure("Inter ascender > 0", asc_b > 0.f);

        // Inter at 8pt is comfortably smaller than OpenDyslexic at 14pt.
        // 60% threshold separates "pin swapped cleanly" from "stuck at
        // OD's 14pt" with margin against FreeType rounding noise.
        ensure("ascender shrinks across override swap (OD 14pt -> Inter 8pt)",
               asc_b < asc_a * 0.6f);
    }

    // F-NULL1 regression: getFont for a descriptor whose font files all
    // fail to load returns NULL but MUST NOT poison mFontMap. Pre-fix
    // behavior unconditionally cached `NULL` at the end of createFont, so
    // the next getFont silently returned the cached NULL and widgets
    // dereferenced it; the fix at llfontregistry.cpp:1555-1567 only
    // inserts on success. This test pins that contract.
    template<> template<>
    void llfontregistry_gl_object::test<8>()
    {
        // No file presence check needed — the whole point is that the
        // file is missing.
        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Phantom'>"
            "    <style name='NORMAL'><file>does-not-exist.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(kXml));
        resolve();

        LLFontDescriptor desc("Phantom", "Small", 0);
        LLFontGL* first = reg.getFont(desc);
        ensure("getFont with missing file returns NULL", first == nullptr);

        ensure("failed createFont must NOT poison mFontMap with a NULL slot",
               !fontMapContains(desc));
    }

    // reloadForDpiChange walks heads + fallback cache and resets each
    // freetype at the new (sVertDPI, sHorizDPI). End-to-end: ascender at
    // 96 DPI → double DPI + reloadForDpiChange → ascender ~doubles
    // (FreeType point→pixel conversion goes through DPI). Pins the
    // fast-DPI path that LLFontGL::initClass takes when fonts.xml +
    // overrides are unchanged but only the scale moved.
    template<> template<>
    void llfontregistry_gl_object::test<9>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);
        F32 ascender_96 = font->getAscenderHeight();
        ensure("ascender at 96 DPI > 0", ascender_96 > 0.f);

        LLFontGL::sVertDPI  = 192.f;
        LLFontGL::sHorizDPI = 192.f;
        reg.reloadForDpiChange();

        F32 ascender_192 = font->getAscenderHeight();
        ensure("ascender at 192 DPI > 0", ascender_192 > 0.f);

        // 192/96 = 2.0x; allow 1.7x lower bound for FreeType rounding.
        ensure("ascender scales with DPI after reloadForDpiChange",
               ascender_192 > ascender_96 * 1.7f);

        // Restore so neighbour tests see the fixture default.
        LLFontGL::sVertDPI  = 96.f;
        LLFontGL::sHorizDPI = 96.f;
        reg.reloadForDpiChange();
    }

    // sweepGlyphCaches walks heads + fallback freetypes calling
    // collectGarbage on each. collectGarbage is internally throttled and
    // must not drop in-use atlas pages on a short-interval invocation.
    // Pin: after generating ASCII glyphs, a sweep doesn't reduce page
    // count.
    template<> template<>
    void llfontregistry_gl_object::test<10>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);
        font->generateASCIIglyphs();

        const LLFontBitmapCache* cache = font->getFontFreetype()->getFontBitmapCache();
        const U32 pages_before = cache->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure("at least one atlas page before sweep", pages_before >= 1u);

        reg.sweepGlyphCaches();

        const U32 pages_after = cache->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure_equals("sweep does not drop in-use atlas pages",
                      pages_after, pages_before);
    }

    // End-to-end shared-fallback eviction: two heads built via the
    // registry resolve to the SAME fallback LLFontFreetype instance via
    // mFallbackInstanceCache (matching file + size + hinting + flags).
    // Both heads rasterize an emoji glyph through that shared fallback,
    // populating the fallback face's atlas. Then we simulate what the
    // fallback's collectGarbage would do after long idle: delete the
    // face-owned glyph entries pointing at the sheet, then release the
    // sheet. Both heads must continue rendering the emoji correctly on
    // the next lookup.
    //
    // Pre-fix: each head's mGlyphInfoMap held a non-owning pointer to the
    // fallback face's glyph entry. erase_glyph_entries deleted those
    // entries, so the heads' fast-path lookup returned freed memory; if
    // the heap was untouched, bitmap_entry still pointed at the released
    // sheet, getImageGL returned null, the bind was skipped, and the
    // emoji silently never rendered again — matching the production
    // "glyphs unload after idle and never reload" symptom.
    //
    // Post-fix: heads have no mGlyphInfoMap; lookup goes through the
    // face's findGlyphInfo, which misses post-eviction and falls through
    // to addShapedGlyphFromFont to re-rasterize on a fresh sheet.
    template<> template<>
    void llfontregistry_gl_object::test<11>()
    {
        const std::string emoji_path  = std::string(kFontsDir) + "Noto-COLRv1.ttf";
        const std::string dejavu_path = std::string(kFontsDir) + "DejaVuSans.woff2";
        const std::string inter_path  = std::string(kFontsDir) + "InterVariable.woff2";
        if (!fileExists(emoji_path) || !fileExists(dejavu_path) || !fileExists(inter_path))
        {
            skip("Noto-COLRv1.ttf + DejaVuSans.woff2 + InterVariable.woff2 required");
            return;
        }

        // Two heads with DIFFERENT primary faces (so they don't share a
        // head ALFontFace) but a COMMON Noto-COLRv1 fallback at the same
        // size — that's what triggers mFallbackInstanceCache deduplication.
        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Emoji' emoji='true'>"
            "    <style name='NORMAL'><file>Noto-COLRv1.ttf</file></style>"
            "  </font>"
            "  <font name='HeadA'>"
            "    <use family='Emoji'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "  <font name='HeadB'>"
            "    <use family='Emoji'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(kXml));
        resolve();

        LLFontGL* head_a = reg.getFont(LLFontDescriptor("HeadA", "Small", 0));
        LLFontGL* head_b = reg.getFont(LLFontDescriptor("HeadB", "Small", 0));
        ensure("HeadA resolves", head_a != nullptr);
        ensure("HeadB resolves", head_b != nullptr);

        // Locate the Noto-COLRv1 fallback inside each head's chain.
        auto find_emoji_fallback = [](LLFontGL* head) -> const LLFontFreetype*
        {
            for (const auto& fb : head->getFontFreetype()->getFallbackFonts())
            {
                if (fb.first && fb.first->getName().find("Noto-COLRv1") != std::string::npos)
                    return fb.first.get();
            }
            return nullptr;
        };
        const LLFontFreetype* emoji_a = find_emoji_fallback(head_a);
        const LLFontFreetype* emoji_b = find_emoji_fallback(head_b);
        ensure("HeadA chain includes Noto-COLRv1", emoji_a != nullptr);
        ensure("HeadB chain includes Noto-COLRv1", emoji_b != nullptr);
        ensure_equals("siblings share the fallback freetype instance "
                      "(mFallbackInstanceCache dedup)",
                      emoji_a, emoji_b);

        // Rasterize an emoji glyph through both heads. Both routes go
        // through getGlyphInfo → cmap miss on the head face → fallback
        // walk picks Noto-COLRv1 → face's findGlyphInfo allocates and
        // caches on the FIRST call; the SECOND head hits the face cache.
        constexpr llwchar kFire = 0x1F525;
        const LLFontGlyphInfo* gi_a = head_a->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        const LLFontGlyphInfo* gi_b = head_b->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        ensure("HeadA produced an emoji glyph entry", gi_a != nullptr);
        ensure("HeadB produced an emoji glyph entry", gi_b != nullptr);
        // Face-level dedup → both heads see the same face-owned entry.
        ensure_equals("face dedup returns the same entry to both heads",
                      gi_a, gi_b);

        const auto target = gi_a->mPhaseSlots[0].mBitmapEntry;
        ensure("emoji glyph references a real Color sheet",
               target.first == EFontGlyphType::Color && target.second >= 0);

        // Simulate the fallback's collectGarbage releasing this sheet.
        // Delete face-owned entries pointing at (Color, target.second),
        // then release the sheet itself. The whole point of this test:
        // pre-fix, both heads' mGlyphInfoMap retained dangling pointers
        // to the deleted entry; post-fix, neither head has such a cache.
        const ALFontFace* emoji_face = emoji_a->getFontFace();
        LLFontBitmapCache* cache = emoji_a->getBitmapCache();
        ensure("fallback freetype exposes its face", emoji_face != nullptr);
        ensure("fallback freetype exposes its atlas", cache != nullptr);

        emoji_face->erase_glyph_entries(
            [target](const LLFontGlyphInfo* gi)
            {
                for (U8 p = 0; p < gi->mPhaseCount; ++p)
                {
                    const auto& e = gi->mPhaseSlots[p].mBitmapEntry;
                    if (e.first == target.first && e.second == target.second)
                        return true;
                }
                return false;
            });
        cache->releaseSheet(target.first, static_cast<U32>(target.second));
        ensure("target sheet reports released",
               cache->isSheetReleased(target.first,
                                      static_cast<U32>(target.second)));

        // Re-request the emoji through both heads. Each lookup must:
        //  1. not crash on a freed face entry,
        //  2. produce a non-null glyph entry,
        //  3. land that entry on a LIVE sheet (not the released one).
        const LLFontGlyphInfo* gi_a2 = head_a->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        const LLFontGlyphInfo* gi_b2 = head_b->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        ensure("HeadA re-rasterized after sibling-driven eviction",
               gi_a2 != nullptr);
        ensure("HeadB re-rasterized after sibling-driven eviction",
               gi_b2 != nullptr);
        const auto post_a = gi_a2->mPhaseSlots[0].mBitmapEntry;
        const auto post_b = gi_b2->mPhaseSlots[0].mBitmapEntry;
        ensure("HeadA's post-eviction entry points at a live sheet",
               !cache->isSheetReleased(post_a.first,
                                       static_cast<U32>(post_a.second)));
        ensure("HeadB's post-eviction entry points at a live sheet",
               !cache->isSheetReleased(post_b.first,
                                       static_cast<U32>(post_b.second)));
    }
#endif // LL_MESA_HEADLESS
}
