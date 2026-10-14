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
 * The GL-requiring paths -- createFont allocating an atlas, getFont
 * caching the LLFontGL pointer, destroyGL releasing textures -- are in
 * llfontregistry_gl_test.cpp.
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

#include <catch2/catch_test_macros.hpp>

#include <cstring>

// init_from_xml is declared inside LLFontRegistry as a public friend; the
// header doesn't otherwise expose its prototype. Forward-declare here so
// the parser tests can call it directly without having to set up
// gDirUtilp + a temp skin tree just to reach parseFontInfo.
bool init_from_xml(LLFontRegistry* registry, LLPointer<LLXMLNode> node);

namespace ll_test
{
    // No FreeType/GL needed for any test in this file; the registry's only
    // construction-time side-effect is caching LLWindow's dynamic fallback
    // list, which is a static query that's safe to run anywhere llwindow is
    // linked. We skip LLFontManager::initClass() entirely.
    //
    // The fixture keeps its name and namespace, outside the anonymous one:
    // llfontregistry.h friends it by name.
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

        // Friend-access wrappers so test cases (which live on a class
        // TEST_CASE_METHOD derives from this fixture, not on the fixture
        // itself) can drive the private resolveFontReferences /
        // applyFamilyOverrides via inherited members.
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

    // ===================================================================
    // Group 1: LLFontDescriptor (pure)
    // ===================================================================

    // normalize() pulls an embedded "Huge"/"Large"/etc. out of the name and
    // moves it into the size slot, leaving the family stem behind.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize moves a size suffix like Huge from the name to the size", "[LLFontRegistry]")
    {
        LLFontDescriptor desc("SansSerifHuge", "", 0);
        LLFontDescriptor n = desc.normalize();
        CHECK(n.getName() == std::string("SansSerif"));
        CHECK(n.getSize() == std::string("Huge"));
        UNSCOPED_INFO("style stays NORMAL");
        CHECK((S32)n.getStyle() == 0);
    }

    // "Big" is the legacy alias for "Large" — verify the rewrite happens.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize rewrites the legacy size suffix Big to Large", "[LLFontRegistry]")
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerifBig", "", 0).normalize();
        CHECK(n.getSize() == std::string("Large"));
        CHECK(n.getName() == std::string("SansSerif"));
    }

    // "Bold"/"Italic" suffixes set the style flags AND get stripped from
    // the name (the leading inline comment in normalize() claims the name
    // is preserved, but the code definitively removes the substring).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize strips a Bold suffix from the name and sets the BOLD flag", "[LLFontRegistry]")
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerifBold", "", 0).normalize();
        CHECK(n.getName() == std::string("SansSerif"));
        UNSCOPED_INFO("size defaults to Small (no size suffix in input)");
        CHECK(n.getSize() == std::string("Small"));
        CHECK((S32)n.getStyle() == (S32)LLFontGL::BOLD);
    }

    // Bold + Italic combine; other style bits (UNDERLINE) get filtered.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize combines Bold and Italic and drops the UNDERLINE bit", "[LLFontRegistry]")
    {
        LLFontDescriptor desc("SansSerifBoldItalic", "",
                              static_cast<U8>(LLFontGL::UNDERLINE));
        LLFontDescriptor n = desc.normalize();
        CHECK((S32)n.getStyle() == (S32)(LLFontGL::BOLD | LLFontGL::ITALIC));
    }

    // "Monospace" is preserved as both name and size — the special-case
    // branch in normalize() exists because stripping it would leave "".
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize keeps Monospace as both the name and the size", "[LLFontRegistry]")
    {
        LLFontDescriptor n = LLFontDescriptor("Monospace", "", 0).normalize();
        CHECK(n.getName() == std::string("Monospace"));
        CHECK(n.getSize() == std::string("Monospace"));
    }

    // Empty size + non-Monospace name → "Small" default.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize defaults an empty size to Small", "[LLFontRegistry]")
    {
        LLFontDescriptor n = LLFontDescriptor("SansSerif", "", 0).normalize();
        CHECK(n.getSize() == std::string("Small"));
    }

    // Files attached to a descriptor survive the normalize() round-trip.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: normalize keeps the descriptor's font files", "[LLFontRegistry]")
    {
        LLFontDescriptor desc("SansSerif", "", 0);
        desc.addFontFile("Foo.ttf", EFontHinting::DEFAULT, 0, 0.f);
        LLFontDescriptor n = desc.normalize();
        REQUIRE((S32)n.getFontFiles().size() == 1);
        CHECK(n.getFontFiles()[0].FileName == std::string("Foo.ttf"));
    }

    // operator== compares (name, style, size). Different files but same
    // identity-tuple still compare equal — that's intentional, the file
    // list is treated as data the descriptor carries, not part of identity.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: operator== and hash_value ignore the font file list", "[LLFontRegistry]")
    {
        LLFontDescriptor a("SansSerif", "Medium", 0);
        LLFontDescriptor b("SansSerif", "Medium", 0);
        a.addFontFile("a.ttf", EFontHinting::DEFAULT, 0, 0.f);
        b.addFontFile("b.ttf", EFontHinting::DEFAULT, 0, 0.f);
        CHECK(a == b);
        CHECK(hash_value(a) == hash_value(b));
    }

    // operator< orders lexicographically by (name, style, size). Style
    // comparisons happen ahead of size — verify by holding name fixed and
    // giving the lower style the later size: "Small" sorts after "Huge",
    // so comparing size first would put b ahead of a.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: operator< compares style before size", "[LLFontRegistry]")
    {
        LLFontDescriptor a("SansSerif", "Small", 0);
        LLFontDescriptor b("SansSerif", "Huge", LLFontGL::BOLD);
        CHECK(a < b);
        UNSCOPED_INFO("strict weak ordering");
        CHECK_FALSE(b < a);
    }

    // The "TEMPLATE" sentinel marks descriptors that exist only as the
    // post-XML template (no concrete point size yet).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: isTemplate is true only for the TEMPLATE size sentinel", "[LLFontRegistry]")
    {
        LLFontDescriptor t("SansSerif", "TEMPLATE", 0);
        LLFontDescriptor n("SansSerif", "Medium", 0);
        CHECK(t.isTemplate());
        CHECK_FALSE(n.isTemplate());
    }

    // ===================================================================
    // Group 2: Parser (init_from_xml)
    // ===================================================================

    // <font_size> populates the global size table; nameToSize hits it
    // when no per-family override applies.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <font_size> fills the global table that nameToSize reads", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font_size name='Large'  size='14.0'/>"
            "</fonts>";
        CHECK(loadXml(xml));
        F32 sz = 0.f;
        CHECK(reg.nameToSize("Medium", sz));
        CHECK(sz == 12.0f);
        CHECK(reg.nameToSize("Large", sz));
        CHECK(sz == 14.0f);
        CHECK_FALSE(reg.nameToSize("Huge", sz));
    }

    // New-format <style> blocks produce one descriptor per (family, style).
    // file_hinting+font_weight inherited from the family-level attributes
    // unless the file overrides — verify both routes.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <style> files take family weight and hinting unless they override", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        const LLFontDescriptor* normal = templateFor("Inter", LLFontGL::NORMAL);
        const LLFontDescriptor* bold   = templateFor("Inter", LLFontGL::BOLD);
        REQUIRE(normal != nullptr);
        REQUIRE(bold != nullptr);
        REQUIRE_FALSE(normal->getFontFiles().empty());
        REQUIRE_FALSE(bold->getFontFiles().empty());
        CHECK(normal->getFontFiles()[0].mVarAxes.wght_set);
        CHECK(normal->getFontFiles()[0].mVarAxes.wght == 400.f);
        CHECK((S32)normal->getFontFiles()[0].mHinting == (S32)EFontHinting::LIGHT);
        CHECK(bold->getFontFiles()[0].mVarAxes.wght_set);
        CHECK(bold->getFontFiles()[0].mVarAxes.wght == 700.f);
        CHECK((S32)bold->getFontFiles()[0].mHinting == (S32)EFontHinting::LIGHT);
    }

    // <size> children inside <font> populate the per-family override table
    // that nameToSize(family, size) consults before the global table.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a family's <size> overrides the global size for that family only", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font name='DejaVu' label='DejaVu Sans'>"
            "    <size name='Medium' size='8.6'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        F32 sz = 0.f;
        CHECK(reg.nameToSize("DejaVu", "Medium", sz));
        CHECK(sz == 8.6f);
        F32 sz2 = 0.f;
        UNSCOPED_INFO("Other family Medium falls back to global");
        CHECK(reg.nameToSize("SansSerif", "Medium", sz2));
        CHECK(sz2 == 12.0f);
    }

    // Family-level ligatures="on" propagates to every file unless the
    // file overrides. The flag lives on LLFontFileInfo (it controls the
    // freetype-level setAllowMonospaceLigatures call later on).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a family's ligatures=on carries to its files", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Mono' monospace='true' ligatures='on'>"
            "    <style name='NORMAL'><file>FiraCode.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("Mono", LLFontGL::NORMAL);
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        CHECK(d->getFontFiles()[0].mMonospaceLigatures);
    }

    // load_collection on the family is inherited by every file; file-level
    // load_collection overrides per-file. Verify both paths.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: load_collection works on the family and on a single file", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        const LLFontDescriptor* cjk = templateFor("CJK");
        const LLFontDescriptor* mono = templateFor("Mono");
        REQUIRE(cjk != nullptr);
        REQUIRE(mono != nullptr);
        REQUIRE_FALSE(cjk->getFontFiles().empty());
        REQUIRE_FALSE(mono->getFontFiles().empty());
        UNSCOPED_INFO("family-level load_collection inherited");
        CHECK(cjk->getFontFiles()[0].mLoadCollection);
        UNSCOPED_INFO("file-level load_collection works alone");
        CHECK(mono->getFontFiles()[0].mLoadCollection);
    }

    // flags="bold" forces LLFontGL::BOLD into the file's flags so that
    // a single .woff2 carrying multiple weights still lights up callers'
    // BOLD checks.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a file's flags=bold sets BOLD in its flags", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Inter'>"
            "    <style name='BOLD'>"
            "      <file flags='bold'>InterVariable.woff2</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("Inter", LLFontGL::BOLD);
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        CHECK((d->getFontFiles()[0].mFlags & LLFontGL::BOLD) == (S32)LLFontGL::BOLD);
    }

    // unicode_ranges parses comma-separated U+lo-U+hi tokens into a
    // CharFunctor that returns true for matching codepoints.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: unicode_ranges builds a CharFunctor from U+ ranges and code points", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Emoji' user_selectable='false'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='U+2600-U+27BF, U+1F525, 0x200D'>Noto-COLRv1.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("Emoji");
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        const auto& cf = d->getFontFiles()[0].CharFunctor;
        REQUIRE(static_cast<bool>(cf));
        UNSCOPED_INFO("U+2600 inside range");
        CHECK(cf(0x2600));
        UNSCOPED_INFO("U+27BF inside range");
        CHECK(cf(0x27BF));
        UNSCOPED_INFO("U+27C0 outside range");
        CHECK_FALSE(cf(0x27C0));
        UNSCOPED_INFO("U+1F525 single CP");
        CHECK(cf(0x1F525));
        UNSCOPED_INFO("0x200D via 0x prefix");
        CHECK(cf(0x200D));
        UNSCOPED_INFO("U+0000 nowhere covered");
        CHECK_FALSE(cf(0));
    }

    // Malformed unicode_ranges tokens get skipped with a log, but the
    // remaining valid tokens still build a functor.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: unicode_ranges skips malformed tokens and keeps the valid ones", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Frag'>"
            "    <style name='NORMAL'>"
            "      <file unicode_ranges='garbage, U+2600-U+27BF, U+ZZZ'>F.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("Frag");
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        const auto& cf = d->getFontFiles()[0].CharFunctor;
        REQUIRE(static_cast<bool>(cf));
        CHECK(cf(0x2700));
    }

    // size_delta carries to the file unchanged. createFont adds it to
    // the resolved point size when loading a face.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: size_delta carries to the file unchanged", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='X'>"
            "    <style name='NORMAL'>"
            "      <file size_delta='-1.5'>X.ttf</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("X");
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        CHECK(d->getFontFiles()[0].mSizeDelta == -1.5f);
    }

    // Family metadata: label / user_selectable / monospace flow into
    // mFamilyMeta where getAvailableFamilies reads them.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getAvailableFamilies reports labels and hides user_selectable=false", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        // Hidden filtered out; Inter + Mono remain. Stable sort by label.
        UNSCOPED_INFO("Hidden is filtered out: 2 visible families");
        CHECK((S32)fams.size() == 2);
        bool sawInter = false, sawMono = false;
        for (const auto& f : fams)
        {
            if (f.name == "Inter") { sawInter = true; REQUIRE(f.label == std::string("Inter Display")); }
            if (f.name == "Mono")  { sawMono  = true; }
        }
        CHECK(sawInter);
        CHECK(sawMono);
    }

    // Two consecutive init_from_xml calls (mimicking cross-skin layering)
    // PREPEND the second layer's files onto an existing same-key entry.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a second init_from_xml layer prepends its files to the family", "[LLFontRegistry]")
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
        CHECK(loadXml(base));
        CHECK(loadXml(overlay));
        auto names = fileNames("SansSerif");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("Overlay.woff2"));
        CHECK(names[1] == std::string("Base.woff2"));
    }

    // ===================================================================
    // Group 3: Resolver (resolveFontReferences)
    // ===================================================================

    // Simple <use family="B"/> appends B's files onto A's chain at the
    // matching style.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <use> appends the used family's files after the family's own", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto names = fileNames("A");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("A.woff2"));
        CHECK(names[1] == std::string("B.woff2"));
    }

    // Style fallback: A wants B at BOLD, B only declares NORMAL.
    // Resolver should pull B's NORMAL files into A's BOLD chain.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <use> of a family without BOLD fills the BOLD chain from its NORMAL", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto names = fileNames("A", LLFontGL::BOLD);
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("A-Bold.woff2"));
        CHECK(names[1] == std::string("B-Regular.woff2"));
    }

    // Cycle A→B + B→A: each family's chain ends with the other's files
    // exactly once. Without the visited-set guard this would recurse
    // infinitely or duplicate.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a two-family <use> cycle puts each file on each chain once", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        CHECK(countFile("A", "A.woff2") == 1);
        CHECK(countFile("A", "B.woff2") == 1);
        CHECK(countFile("B", "B.woff2") == 1);
        CHECK(countFile("B", "A.woff2") == 1);
    }

    // Diamond A→B, A→C, B→D, C→D: D must appear exactly once on A's
    // chain — the visited set is shared across the whole walk.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> diamond puts the shared family on the chain once", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        CHECK(countFile("A", "D.woff2") == 1);
        CHECK(countFile("A", "B.woff2") == 1);
        CHECK(countFile("A", "C.woff2") == 1);
    }

    // A <use> naming the family itself (<use family='A'/> inside A) logs
    // and is skipped: the chain holds only A's own file, once.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> of the family itself is skipped", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='A'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        CHECK(countFile("A", "A.woff2") == 1);
        CHECK((S32)fileNames("A").size() == 1);
    }

    // <use family="ghost"/> targeting an undeclared family is skipped.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> of an undeclared family is skipped", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <use family='ghost'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        CHECK((S32)fileNames("A").size() == 1);
    }

    // <use>-only family (no <style> blocks declared): style descriptors
    // are synthesized from whichever styles the referenced family covers.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a use-only family gets a template for each style its <use> declares", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        CHECK(templateFor("Wrapper", LLFontGL::NORMAL) != nullptr);
        CHECK(templateFor("Wrapper", LLFontGL::BOLD) != nullptr);
        auto bold = fileNames("Wrapper", LLFontGL::BOLD);
        REQUIRE((S32)bold.size() == 1);
        CHECK(bold[0] == std::string("Inner-B.woff2"));
    }

    // inherit="true" on BOLD appends the family's NORMAL files.
    // Verify: BOLD's chain ends with NORMAL's contents.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: inherit=true on BOLD appends the family's NORMAL files", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='NORMAL'><file>F-R.woff2</file></style>"
            "    <style name='BOLD' inherit='true'><file>F-B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto names = fileNames("F", LLFontGL::BOLD);
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("F-B.woff2"));
        CHECK(names[1] == std::string("F-R.woff2"));
    }

    // <use> applies per-style before inherit; then inherit appends NORMAL's
    // post-<use> chain. F BOLD's pre-dedup chain would be [F-B, B, F-R, B]
    // — the same B file from both F BOLD's own <use> and from NORMAL's
    // post-<use> list inherited in. The dedup pass at the end of
    // resolveFontReferences drops the second B because chain[0..2] already
    // covers everything the duplicate would; it just wastes the renderer's
    // per-codepoint chain walk to look up a face it'll handle the same way.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: BOLD inherit after <use> keeps one copy of the used family's file", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto bold = fileNames("F", LLFontGL::BOLD);
        UNSCOPED_INFO("3 files post-dedup: own bold + B from <use> + NORMAL inherit");
        REQUIRE((S32)bold.size() == 3);
        CHECK(bold[0] == std::string("F-B.woff2"));
        CHECK(bold[1] == std::string("B.woff2"));
        CHECK(bold[2] == std::string("F-R.woff2"));
        UNSCOPED_INFO("duplicate B from NORMAL's <use> deduped out");
        CHECK(countFile("F", "B.woff2", LLFontGL::BOLD) == 1);
    }

    // Calling resolve() a second time is a no-op — mFamilyUses /
    // mInheritFlags are consumed at the end of the first call. Per-
    // family sizes (mFamilySizes) and per-file source-family tags
    // survive — those are runtime data, not parse state.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a second resolveFontReferences adds nothing and keeps size and source", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='A'>"
            "    <size name='Large' size='9.0'/>"
            "    <use family='B'/>"
            "    <style name='NORMAL'><file>A.woff2</file></style>"
            "  </font>"
            "  <font name='B'>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto first = fileNames("A");
        resolve(); // second call must not re-append
        auto second = fileNames("A");
        CHECK((S32)first.size() == (S32)second.size());

        F32 pin = 0.f;
        CHECK(familySizePt("A", "Large", pin));
        CHECK(pin == 9.0f);
        const LLFontDescriptor* a = templateFor("A");
        REQUIRE(a != nullptr);
        const auto& files = a->getFontFiles();
        REQUIRE((S32)files.size() == 2);
        CHECK(files[0].mSourceFamily == std::string("A"));
        CHECK(files[1].mSourceFamily == std::string("B"));
    }

    // ===================================================================
    // Group 4: Overrides (applyFamilyOverrides)
    // ===================================================================

    // Family→family override: target's chain becomes [source files...,
    // target's own files] so source wins for the head face but target
    // fallbacks remain available behind it.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a family override puts the source's files ahead of the target's", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";
        applyOverrides(overrides);
        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("Alt.woff2"));
        CHECK(names[1] == std::string("UI.woff2"));
    }

    // Family→file override: the override value isn't a known family, so
    // it's treated as a literal file path. The synthesized LLFontFileInfo
    // inherits hinting/weight/flags from the target's first file so the
    // swap behaves like a drop-in replacement.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a file override copies hinting/weight/ligatures from the target", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI' font_hinting='light' font_weight='400' ligatures='on'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "user-supplied.ttf";
        applyOverrides(overrides);
        const LLFontDescriptor* ui = templateFor("UI");
        REQUIRE(ui != nullptr);
        const auto& files = ui->getFontFiles();
        REQUIRE_FALSE(files.empty());
        CHECK(files[0].FileName == std::string("user-supplied.ttf"));
        CHECK((S32)files[0].mHinting == (S32)EFontHinting::LIGHT);
        CHECK(files[0].mVarAxes.wght == 400.f);
        CHECK(files[0].mMonospaceLigatures);
    }

    // Self-override (target == source string) is rejected — the override
    // would otherwise prepend the target's own files onto itself.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override of a family to itself is ignored", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'>"
            "    <style name='NORMAL'><file>UI.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "UI";
        applyOverrides(overrides);
        auto names = fileNames("UI");
        CHECK((S32)names.size() == 1);
    }

    // Unknown target family: skipped, no phantom entries created.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override of an undeclared family creates no template", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Real'>"
            "    <style name='NORMAL'><file>R.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["Phantom"] = "Real";
        applyOverrides(overrides);
        CHECK(templateFor("Phantom") == nullptr);
        UNSCOPED_INFO("Real untouched");
        CHECK((S32)fileNames("Real").size() == 1);
    }

    // overridesEqual: snapshot of last-applied overrides updates with
    // each call to resolveFontReferences (which calls applyFamilyOverrides
    // and stashes the LLSD).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: overridesEqual compares against the overrides last resolved", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "  <font name='Alt'><style name='NORMAL'><file>A.woff2</file></style></font>"
            "</fonts>";
        CHECK(loadXml(xml));
        LLSD ovr1;
        ovr1["UI"] = "Alt";
        resolve(ovr1);
        CHECK(reg.overridesEqual(ovr1));
        LLSD ovr2;
        ovr2["UI"] = "Different";
        CHECK_FALSE(reg.overridesEqual(ovr2));
    }

    // applyOverrides prepends source files onto the target's chain. The
    // prepended source files retain their mSourceFamily so createFont's
    // per-file size pin lookup picks up the source family's <size> at
    // render time. nameToSize itself is NOT routed — the override target's
    // own <size> (or global) is what nameToSize returns. This pins the
    // post-redesign behavior: per-family size is absolute for THAT family
    // only, never propagated cross-family.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: override files carry the source family while nameToSize stays put", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";
        applyOverrides(overrides);
        // nameToSize for UI returns UI's own pin; the override does NOT
        // route this lookup through Alt.
        F32 sz = 0.f;
        CHECK(reg.nameToSize("UI", "Custom", sz));
        UNSCOPED_INFO("UI Custom = own 9.5 (override does NOT route nameToSize)");
        CHECK(sz == 9.5f);
        // The override DID prepend Alt's file onto UI's chain, with
        // mSourceFamily=Alt — so per-file pin lookup at render time
        // would use Alt's <size> for that prepended file.
        const LLFontDescriptor* ui = templateFor("UI");
        REQUIRE(ui != nullptr);
        const auto& files = ui->getFontFiles();
        REQUIRE_FALSE(files.empty());
        CHECK(files[0].FileName == std::string("A.woff2"));
        CHECK(files[0].mSourceFamily == std::string("Alt"));
        // Alt's pin is intact for per-file lookup.
        F32 alt_pin = 0.f;
        CHECK(familySizePt("Alt", "Custom", alt_pin));
        UNSCOPED_INFO("Alt Custom pin = 11.5 (createFont uses this for Alt's file)");
        CHECK(alt_pin == 11.5f);

        // Clear the override; UI's own pin still applies.
        applyOverrides(LLSD());
        F32 sz2 = 0.f;
        CHECK(reg.nameToSize("UI", "Custom", sz2));
        UNSCOPED_INFO("UI Custom = own 9.5 (override cleared)");
        CHECK(sz2 == 9.5f);
    }

    // ===================================================================
    // Group 5: Lookups
    // ===================================================================

    // getMatchingFontDesc applies normalize() to its argument, so passing
    // an unnormalized "SansSerifBold" finds the {SansSerif, BOLD} template.
    // (A size suffix such as "Huge" would not: normalize moves it into the
    // size, over "TEMPLATE", so the query names a head, not a template.)
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getMatchingFontDesc normalizes its query and misses an absent family", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='SansSerif'>"
            "    <style name='NORMAL'><file>S.woff2</file></style>"
            "    <style name='BOLD'><file>S-Bold.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        LLFontDescriptor query("SansSerif", "TEMPLATE", 0);
        CHECK(reg.getMatchingFontDesc(query) != nullptr);
        const LLFontDescriptor* bold = reg.getMatchingFontDesc(LLFontDescriptor("SansSerifBold", "TEMPLATE", 0));
        REQUIRE(bold != nullptr);
        CHECK(bold->getName() == std::string("SansSerif"));
        CHECK((S32)bold->getStyle() == (S32)LLFontGL::BOLD);
        LLFontDescriptor miss("Nope", "TEMPLATE", 0);
        CHECK(reg.getMatchingFontDesc(miss) == nullptr);
    }

    // getClosestFontTemplate prefers the entry whose style bits overlap
    // most with the requested style, with BOLD as a tiebreaker.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getClosestFontTemplate takes an exact style and breaks ties toward BOLD", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='NORMAL'><file>F-N.woff2</file></style>"
            "    <style name='BOLD'><file>F-B.woff2</file></style>"
            "    <style name='ITALIC'><file>F-I.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        // Asking for BOLD: exact match wins.
        LLFontDescriptor want_bold("F", "TEMPLATE", LLFontGL::BOLD);
        const LLFontDescriptor* best = reg.getClosestFontTemplate(want_bold);
        REQUIRE(best != nullptr);
        CHECK((S32)best->getStyle() == (S32)LLFontGL::BOLD);

        // Asking for BOLD|ITALIC with no BOLD|ITALIC entry: the BOLD
        // tiebreaker should prefer BOLD over ITALIC at equal bit-count.
        LLFontDescriptor want_bi("F", "TEMPLATE",
                                 (U8)(LLFontGL::BOLD | LLFontGL::ITALIC));
        best = reg.getClosestFontTemplate(want_bi);
        UNSCOPED_INFO("BOLD|ITALIC request falls back to a template");
        REQUIRE(best != nullptr);
        UNSCOPED_INFO("BOLD wins tiebreak");
        CHECK((S32)best->getStyle() == (S32)LLFontGL::BOLD);
    }

    // getClosestFontTemplate REJECTS entries that have style bits the
    // request didn't ask for. Asking for NORMAL must not return a BOLD
    // template even if the family has only BOLD.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getClosestFontTemplate never returns a style the request lacks", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='F'>"
            "    <style name='BOLD'><file>F-B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        LLFontDescriptor want_normal("F", "TEMPLATE", 0);
        CHECK(reg.getClosestFontTemplate(want_normal) == nullptr);
    }

    // Empty-family overload of nameToSize skips the per-family path
    // entirely and only consults the global table.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: nameToSize without a family reads only the global table", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='Medium' size='12.0'/>"
            "  <font name='F'>"
            "    <size name='Medium' size='9.0'/>"
            "    <style name='NORMAL'><file>F.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        F32 sz = 0.f;
        CHECK(reg.nameToSize("Medium", sz));
        CHECK(sz == 12.0f);
        F32 sz2 = 0.f;
        reg.nameToSize("F", "Medium", sz2);
        UNSCOPED_INFO("family overload still gets per-family value");
        CHECK(sz2 == 9.0f);
    }

    // ===================================================================
    // Group 6: getAvailableFamilies
    // ===================================================================

    // ANY filter returns both monospace and proportional, "default" and
    // user_selectable=false are excluded, and result is sorted by label.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getAvailableFamilies ANY drops hidden and default and sorts by label", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        REQUIRE((S32)fams.size() == 2);
        UNSCOPED_INFO("sorted by label, Apple first");
        CHECK(fams[0].name == std::string("Apple"));
        CHECK(fams[1].name == std::string("Zebra"));
    }

    // MONOSPACE filter returns only families with monospace="true";
    // PROPORTIONAL is the inverse.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: getAvailableFamilies MONOSPACE and PROPORTIONAL split on monospace", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        REQUIRE((S32)mono.size() == 1);
        CHECK(mono[0].name == std::string("Mono"));
        auto prop = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::PROPORTIONAL);
        REQUIRE((S32)prop.size() == 1);
        CHECK(prop[0].name == std::string("Sans"));
    }

    // Family without a label attribute defaults its UI label to the
    // family name.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a family without a label uses its name as the label", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Plain'>"
            "    <style name='NORMAL'><file>P.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        REQUIRE((S32)fams.size() == 1);
        CHECK(fams[0].label == std::string("Plain"));
    }

    // Empty <fonts/> root with no children is a degenerate but valid
    // input — init_from_xml + resolve must complete without crashing
    // and leave the registry's font map empty. Pins safe handling of
    // an empty skin layer (e.g., when a skin overrides only the metric
    // table and leaves families to inherit from a lower layer).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an empty <fonts> root parses and resolves to no families", "[LLFontRegistry]")
    {
        const char* xml = "<fonts></fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto fams = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::ANY);
        CHECK((S32)fams.size() == 0);
    }

    // applyOverrides called twice with different LLSD maps: the second
    // overrides the first wholesale rather than merging. Pins that
    // override application is replacement, not accumulation — a
    // regression that merged would compound stale routings. Verified
    // via the per-file source-family on the prepended head file (the
    // override target's chain head should reflect ONLY the most recent
    // override's source).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: applyOverrides twice replaces the first override instead of merging", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        // First override: UI → AltA. UI's chain head becomes AltA's file.
        LLSD ovr1;
        ovr1["UI"] = "AltA";
        applyOverrides(ovr1);
        const LLFontDescriptor* ui_a = templateFor("UI");
        REQUIRE(ui_a != nullptr);
        const auto& files_a = ui_a->getFontFiles();
        REQUIRE_FALSE(files_a.empty());
        CHECK(files_a[0].FileName == std::string("A.woff2"));
        CHECK(files_a[0].mSourceFamily == std::string("AltA"));

        // Second override: UI → AltB. The earlier UI→AltA mapping must
        // be replaced, not merged — UI's head is now AltB.
        LLSD ovr2;
        ovr2["UI"] = "AltB";
        applyOverrides(ovr2);
        const LLFontDescriptor* ui_b = templateFor("UI");
        REQUIRE(ui_b != nullptr);
        const auto& files_b = ui_b->getFontFiles();
        REQUIRE_FALSE(files_b.empty());
        CHECK(files_b[0].FileName == std::string("B.woff2"));
        CHECK(files_b[0].mSourceFamily == std::string("AltB"));
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
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override on a base family reaches every family that <use>s it", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "Custom";
        resolve(overrides);

        // Direct target picks up the override.
        CHECK((fileNames("Base").size() > 0u ? fileNames("Base")[0] : std::string())
              == std::string("Custom.woff2"));
        // Both dependents see Custom.woff2 in their chain — that's the
        // load-bearing assertion. A regression to the old order
        // (overrides applied after <use> resolution) would leave them
        // without Custom.woff2 entirely.
        CHECK(countFile("Plain", "Custom.woff2") >= 1);
        CHECK(countFile("Menu", "Custom.woff2") >= 1);
        // The chain after the override-driven Base files must still
        // carry Source (Base's own <use>) and the sibling <use>s — the
        // override prepends, doesn't replace.
        CHECK(countFile("Plain", "Source.woff2") >= 1);
        CHECK(countFile("Plain", "Latin.woff2") >= 1);
        CHECK(countFile("Menu", "Mark.woff2") >= 1);

        // Position pin: the override file MUST be at index 0 of every
        // dependent chain. The renderer treats position 0 as the head
        // face — anything later is a fallback that only fires when the
        // head's cmap returns notdef. If the override files end up
        // somewhere later than 0, the original Source.woff2 still wins
        // for every glyph it covers and the user observes "no change."
        const auto plain_files = fileNames("Plain");
        CHECK_FALSE(plain_files.empty());
        CHECK((plain_files.empty() ? std::string() : plain_files[0]) == std::string("Custom.woff2"));
        const auto menu_files = fileNames("Menu");
        CHECK_FALSE(menu_files.empty());
        CHECK((menu_files.empty() ? std::string() : menu_files[0]) == std::string("Custom.woff2"));
    }

    // Override propagation through <use> chains for BOLD style. Real
    // fonts.xml declares `inherit="true"` on BOLD variants, which
    // appends the parent NORMAL chain after the variant's own files.
    // Verify the override (which prepends to the BOLD template) still
    // ends up at position 0 of dependent chains for the BOLD style.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override on a base family leads the BOLD chains that <use> it", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "Custom";
        resolve(overrides);

        // BOLD variant of Menu must lead with the override's BOLD file.
        const auto menu_bold = fileNames("Menu", LLFontGL::BOLD);
        CHECK_FALSE(menu_bold.empty());
        CHECK((menu_bold.empty() ? std::string() : menu_bold[0]) == std::string("Custom-Bold.woff2"));
        // NORMAL variant unchanged invariant.
        const auto menu_normal = fileNames("Menu", LLFontGL::NORMAL);
        CHECK((menu_normal.empty() ? std::string() : menu_normal[0])
              == std::string("Custom-Regular.woff2"));
    }

    // Reload with a different override: first resolve applies override A,
    // simulated reload wipes parse-time state, second resolve applies
    // override B. Dependent chains must reflect override B and must NOT
    // carry residue from override A. Mirrors the runtime path where the
    // user picks one font in the picker, then picks a different one —
    // each setting change drives LLFontRegistry::reload(font_overrides)
    // which clears mFontMap before re-parsing.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a reload with a new override leaves no trace of the old one", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        LLSD ovr_a;
        ovr_a["Base"] = "AltA";
        resolve(ovr_a);

        const auto menu_after_a = fileNames("Menu");
        CHECK_FALSE(menu_after_a.empty());
        CHECK((menu_after_a.empty() ? std::string() : menu_after_a[0]) == std::string("AltA.woff2"));
        CHECK(countFile("Menu", "Source.woff2") >= 1);

        // Simulate reload: wipe parse-time state, re-parse, apply
        // override B. AltA must be GONE from the chain — a regression
        // would leak the prior override into the new resolution.
        simulateReload();
        CHECK(loadXml(xml));
        LLSD ovr_b;
        ovr_b["Base"] = "AltB";
        resolve(ovr_b);

        const auto menu_after_b = fileNames("Menu");
        CHECK_FALSE(menu_after_b.empty());
        CHECK((menu_after_b.empty() ? std::string() : menu_after_b[0]) == std::string("AltB.woff2"));
        CHECK(countFile("Menu", "AltA.woff2") == 0);
        CHECK(countFile("Menu", "Source.woff2") >= 1);
        // Same checks for the direct target — Base itself must show
        // only the new override at chain[0], no residue from AltA.
        const auto base_after_b = fileNames("Base");
        CHECK((base_after_b.empty() ? std::string() : base_after_b[0]) == std::string("AltB.woff2"));
        CHECK(countFile("Base", "AltA.woff2") == 0);
    }

    // Reload-and-clear: existing registry has override A, then user picks
    // "(default)" in the picker which writes an empty overrides map.
    // Reload with empty overrides must restore the original chain — no
    // override file lingers from the prior resolution.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a reload with no overrides restores the original chain head", "[LLFontRegistry]")
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

        CHECK(loadXml(xml));
        LLSD ovr_a;
        ovr_a["Base"] = "AltA";
        resolve(ovr_a);
        CHECK((fileNames("Menu").empty() ? std::string() : fileNames("Menu")[0])
              == std::string("AltA.woff2"));

        // Reload with empty overrides — picker's "(default)" sentinel.
        simulateReload();
        CHECK(loadXml(xml));
        resolve(LLSD::emptyMap());

        const auto menu = fileNames("Menu");
        REQUIRE_FALSE(menu.empty());
        CHECK(menu[0] == std::string("Source.woff2"));
        CHECK(countFile("Menu", "AltA.woff2") == 0);
    }

    // Dedup pin: when the override source is also reachable through the
    // target's own <use> chain, the resolved chain should NOT carry the
    // file twice. Reproduces the user-reported case where overriding
    // "SansSerifBase" → "DejaVu" produced [DejaVu(override), SourceSans,
    // DejaVu(via <use family="DejaVu"/>), Noto, CJK] — the second DejaVu
    // is functionally redundant since chain[0] already covers everything
    // the duplicate would, and the duplicated entry just wastes the
    // renderer's per-codepoint walk.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override source also reached by <use> appears once in the chain", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        LLSD overrides;
        overrides["Base"] = "DejaVu";
        resolve(overrides);

        // Without dedup, Menu would be [DejaVu(override), Source,
        // DejaVu(<use>), …]. With dedup, the second DejaVu is dropped.
        CHECK(countFile("Menu", "DejaVu.woff2") == 1);
        CHECK(countFile("Menu", "Source.woff2") >= 1);
        const auto menu = fileNames("Menu");
        REQUIRE_FALSE(menu.empty());
        CHECK(menu[0] == std::string("DejaVu.woff2"));
    }

    // Pure <use> diamond, no override. A reaches D twice — once through
    // B and once through C — and the dedup pass at the end of
    // resolveFontReferences must drop the second copy. The dedup pin
    // above covers the override + <use> shape; this one pins that the
    // dedup logic also operates on plain chain composition with no
    // override involved, so a future regression that gates dedup on
    // "did applyFamilyOverrides run?" would fail here.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a plain <use> diamond with no override is deduplicated too", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        // A reaches D twice (via B and via C). Pre-dedup the chain
        // would be [D, D]; post-dedup [D] — first hit wins, second
        // copy dropped.
        CHECK(countFile("A", "D.woff2") == 1);
        const auto a_files = fileNames("A");
        CHECK((S32)a_files.size() == 1);
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
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: two entries of one file with different CharFunctors are both kept", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        // Both entries reference the same filename and carry a non-null
        // CharFunctor. Dedup must keep both — collapsing would route
        // every codepoint through whichever functor survived, breaking
        // coverage for the dropped range.
        CHECK(countFile("Combined", "Shared.ttf") == 2);
    }

    // simulateReload + re-parse with new sizes must update nameToSize for
    // both the global <font_size> table and per-family <size> children.
    // Live-fonts.xml-edit reload depends on this — without it, the rest
    // of reload() would swap freetypes at the OLD point sizes.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a reload with new sizes updates nameToSize for global and family sizes", "[LLFontRegistry]")
    {
        const char* xml_v1 =
            "<fonts>"
            "  <font_size name='Foo' size='10.0'/>"
            "  <font name='Inter'>"
            "    <size name='Bar' size='8.0'/>"
            "    <style name='NORMAL'><file>Inter.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml_v1));
        resolve();
        F32 sz = 0.f;
        CHECK(reg.nameToSize("Inter", "Foo", sz));
        UNSCOPED_INFO("v1: Foo (global) = 10.0");
        CHECK(sz == 10.0f);
        CHECK(reg.nameToSize("Inter", "Bar", sz));
        UNSCOPED_INFO("v1: Bar (per-family) = 8.0");
        CHECK(sz == 8.0f);

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
        CHECK(loadXml(xml_v2));
        resolve();
        CHECK(reg.nameToSize("Inter", "Foo", sz));
        UNSCOPED_INFO("v2: Foo (global) updated to 20.0");
        CHECK(sz == 20.0f);
        CHECK(reg.nameToSize("Inter", "Bar", sz));
        UNSCOPED_INFO("v2: Bar (per-family) updated to 14.5");
        CHECK(sz == 14.5f);
    }

    // ===================================================================
    // Group 6: Per-family <size> as absolute pin — no cross-family flow
    // ===================================================================

    // Use-only family's nameToSize falls through directly to the global
    // table. No head-face chain walk, no override-source routing. Pin
    // assertions stay on per-file rendering (createFont path), not on
    // chain-head pt resolution.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a use-only family's nameToSize reads the global table", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        F32 sz = 0.f;
        CHECK(reg.nameToSize("UseOnly", "Large", sz));
        UNSCOPED_INFO("UseOnly Large = global 11.0 (no chain walk)");
        CHECK(sz == 11.0f);
        // HeadFace's own <size> still applies for direct queries.
        CHECK(reg.nameToSize("HeadFace", "Large", sz));
        UNSCOPED_INFO("HeadFace Large = own 12.0");
        CHECK(sz == 12.0f);
    }

    // AlchemyUIFontOverrides changes the file list but does NOT route
    // the override target's nameToSize through the override source.
    // Picking OpenDyslexic via override on SansSerifBase: nameToSize
    // for SansSerifBase still returns its own table or global —
    // OpenDyslexic's <size> applies only to OD's own files (verified
    // by the per-file pin tests below).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override does not route the target's nameToSize to the source", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        // Baseline: SansSerifBase has no <size>, falls to global.
        F32 sz = 0.f;
        CHECK(reg.nameToSize("SansSerifBase", "Large", sz));
        UNSCOPED_INFO("baseline SansSerifBase Large = global 11.0");
        CHECK(sz == 11.0f);
        // Apply override; nameToSize result must NOT change.
        LLSD overrides;
        overrides["SansSerifBase"] = "OpenDyslexic";
        applyOverrides(overrides);
        F32 sz2 = 0.f;
        CHECK(reg.nameToSize("SansSerifBase", "Large", sz2));
        UNSCOPED_INFO("override DOES NOT route nameToSize through OD");
        CHECK(sz2 == 11.0f);
    }

    // Per-family <size> on multiple distinct families: each pins its own
    // family's files. The data is keyed correctly per family.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: each family's <size> pin is stored under that family", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        F32 a_sz = 0.f, b_sz = 0.f;
        CHECK(familySizePt("A", "Large", a_sz));
        CHECK(familySizePt("B", "Large", b_sz));
        CHECK(a_sz == 8.0f);
        CHECK(b_sz == 13.0f);
    }

    // Per-family <size> from a <use>'d family is preserved on its files
    // when those files flow into the using family's chain. A <use>s B; B
    // has <size>; A's resolved chain has B's file with mSourceFamily=B,
    // and mFamilySizes[B] still has B's pin. createFont's per-file
    // lookup will use B's pin for B's file, regardless of A's chain pt.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use>d family's files keep their source family and its size pin", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* a = templateFor("A");
        REQUIRE(a != nullptr);
        const auto& a_files = a->getFontFiles();
        REQUIRE((S32)a_files.size() == 2);
        CHECK(a_files[0].mSourceFamily == std::string("A"));
        CHECK(a_files[1].mSourceFamily == std::string("B"));
        F32 b_sz = 0.f;
        CHECK(familySizePt("B", "Large", b_sz));
        CHECK(b_sz == 8.0f);
    }

    // Hot-reload of a per-family <size> pin: edit the pin on a fallback
    // family, simulateReload, re-parse, verify the new pin is reflected
    // in mFamilySizes. createFont's per-file lookup will then pick up
    // the new pin on the next load. Pins the runtime-edit path that
    // matters for fonts.xml live editing.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a reload picks up an edited per-family <size> pin", "[LLFontRegistry]")
    {
        const char* xml_v1 =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='B'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>B.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml_v1));
        resolve();
        F32 v1 = 0.f;
        CHECK(familySizePt("B", "Large", v1));
        CHECK(v1 == 8.0f);

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
        CHECK(loadXml(xml_v2));
        resolve();
        F32 v2 = 0.f;
        CHECK(familySizePt("B", "Large", v2));
        CHECK(v2 == 13.5f);
    }

    // ===================================================================
    // Group 8: Parser robustness + use-chain edge cases
    // ===================================================================

    // <font> without a `name` attribute (new format): processNewFormatFont
    // logs a warning and bails before registering anything. No template
    // entry is created; sibling well-formed fonts still parse cleanly.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <font> without a name is dropped and its siblings still parse", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        // Empty-name template not created. The well-formed sibling is.
        CHECK(templateFor("", LLFontGL::NORMAL) == nullptr);
        CHECK(templateFor("Good", LLFontGL::NORMAL) != nullptr);
    }

    // <font_size> missing either attribute is silently dropped (parser
    // checks both before insert). The well-formed entry parses normally.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <font_size> missing its name or size is dropped", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font_size name='OnlyName'/>"
            "  <font_size size='10.0'/>"
            "  <font_size name='Both' size='13.0'/>"
            "</fonts>";
        CHECK(loadXml(xml));
        F32 sz = 0.f;
        CHECK_FALSE(reg.nameToSize("OnlyName", sz));
        CHECK(reg.nameToSize("Both", sz));
        CHECK(sz == 13.0f);
    }

    // <use> without a `family` attribute logs a warning and is skipped.
    // The chain reflects only well-formed entries.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> without a family is skipped and the rest still resolve", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto names = fileNames("A");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("A.woff2"));
        CHECK(names[1] == std::string("Target.woff2"));
    }

    // <size> missing name or size is dropped from the per-family table
    // with a warning. Sibling well-formed <size> still applies.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <size> missing its name or size is dropped from the family table", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        F32 sz = 0.f;
        CHECK(reg.nameToSize("F", "Foo", sz));
        UNSCOPED_INFO("F Foo = own 8.0 (well-formed entry takes effect)");
        CHECK(sz == 8.0f);
        CHECK_FALSE(reg.nameToSize("F", "OnlyName", sz));
    }

    // <style> with no <file> children produces an empty descriptor.
    // The empty template still registers as a host so the resolver can
    // hang chain references on it. The use-chain provides files; the
    // composite's nameToSize falls through to global because per-family
    // <size> never propagates across families.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an empty <style> takes its files from <use> and its size from global", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        // Empty <style> -> the use-chain is the only file source.
        auto names = fileNames("Empty");
        REQUIRE((S32)names.size() == 1);
        CHECK(names[0] == std::string("Pickup.woff2"));
        // No cross-family propagation: Empty's nameToSize falls through
        // to global. Pickup's <size X=6> would still pin the Pickup
        // file at 6pt at createFont time (per-file pin), but the chain
        // head's pt is global.
        F32 sz = 0.f;
        CHECK(reg.nameToSize("Empty", "X", sz));
        UNSCOPED_INFO("Empty's X = global 10.0 (no cross-family flow)");
        CHECK(sz == 10.0f);
        // Pickup's <size> survives — per-file pin at render time.
        F32 pickup_pin = 0.f;
        CHECK(familySizePt("Pickup", "X", pickup_pin));
        UNSCOPED_INFO("Pickup's X pin still 6.0 (createFont uses for Pickup file)");
        CHECK(pickup_pin == 6.0f);
        // The Pickup file in Empty's chain carries mSourceFamily=Pickup.
        const auto& files = templateFor("Empty")->getFontFiles();
        CHECK(files[0].mSourceFamily == std::string("Pickup"));
    }

    // <style name="..."> with an unrecognized style string maps to NORMAL
    // (LLFontGL::getStyleFromString returns 0 for unknown names). The
    // resulting descriptor is keyed at NORMAL.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an unrecognized <style> name maps to NORMAL", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Quirky'>"
            "    <style name='WEIRD'><file>Q.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        CHECK(templateFor("Quirky", LLFontGL::NORMAL) != nullptr);
        CHECK(templateFor("Quirky", LLFontGL::BOLD) == nullptr);
    }

    // Three-level <use> cycle (A -> B -> C -> A). Visited set must
    // terminate the walk; each family contributes its own files exactly
    // once on every other family's chain.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a three-family <use> cycle puts each file on the chain once", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        // Each chain should contain exactly one of each file, in the
        // walk order starting from the queried family.
        CHECK(countFile("A", "A.woff2") == 1);
        CHECK(countFile("A", "B.woff2") == 1);
        CHECK(countFile("A", "C.woff2") == 1);
        CHECK((S32)fileNames("A").size() == 3);
    }

    // Deep linear chain (5 levels). Walker descends without depth
    // restriction; final chain has each family's file exactly once.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a five-level <use> chain collects every file in order", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto names = fileNames("F1");
        REQUIRE((S32)names.size() == 5);
        CHECK(names[0] == std::string("F1.woff2"));
        CHECK(names[1] == std::string("F2.woff2"));
        CHECK(names[2] == std::string("F3.woff2"));
        CHECK(names[3] == std::string("F4.woff2"));
        CHECK(names[4] == std::string("F5.woff2"));
    }

    // inherit="true" on a non-NORMAL style with no NORMAL parent declared
    // logs a warning and is a no-op for that variant. The variant's chain
    // contains only its own files, not anything synthesized.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: inherit=true with no NORMAL style leaves the variant's own files", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Lonely'>"
            "    <style name='BOLD' inherit='true'>"
            "      <file>Lonely-Bold.woff2</file>"
            "    </style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto bold = fileNames("Lonely", LLFontGL::BOLD);
        REQUIRE((S32)bold.size() == 1);
        CHECK(bold[0] == std::string("Lonely-Bold.woff2"));
    }

    // Synthesis fires only for styles explicitly authored somewhere in
    // the <use> chain. A use-only with B/C declaring only NORMAL gets
    // ONLY a NORMAL host — BOLD / ITALIC / BOLD|ITALIC are not minted.
    // Lookups at unsynthesized styles fall through getClosestFontTemplate
    // (which accepts NORMAL as the closest match for any style request),
    // so request-time fallback handles the missing-style case rather
    // than synthesis aliasing every style to NORMAL.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a use-only family gets no template for styles its chain lacks", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        CHECK(templateFor("A", LLFontGL::NORMAL) != nullptr);
        CHECK(templateFor("A", LLFontGL::BOLD) == nullptr);
        CHECK(templateFor("A", LLFontGL::ITALIC) == nullptr);
        CHECK(templateFor("A", static_cast<U8>(LLFontGL::BOLD | LLFontGL::ITALIC)) == nullptr);
        // NORMAL synthesizes from B and C:
        auto normal = fileNames("A", LLFontGL::NORMAL);
        CHECK((S32)normal.size() == 2);
    }

    // Override that points to a use-only family: applyFamilyOverrides
    // recognizes the source as a known family (via mFontMap template
    // presence) and prepends its resolved file list. Pins the case
    // where the override source is itself a composite use-only family
    // — the override should still propagate the source's chain head.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override whose source is a use-only family prepends its chain", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["Target"] = "Composite";
        applyOverrides(overrides);
        auto names = fileNames("Target");
        // Override prepends Composite's resolved chain (= Inner's files).
        REQUIRE_FALSE(names.empty());
        CHECK(names[0] == std::string("Inner.woff2"));
        UNSCOPED_INFO("Target's own file kept as fallback behind override");
        CHECK(countFile("Target", "Target.woff2") == 1);
    }

    // <size> entries round-trip through the parser into mFamilySizes,
    // and files contributed by this family carry mSourceFamily set to
    // the family name so createFont can later look up the per-family
    // pin at render time. Every <size> is an absolute pin — no force
    // attribute needed.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <size> pins reach mFamilySizes and files carry their source family", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Pinned'>"
            "    <size name='Large' size='8.0'/>"
            "    <size name='Medium' size='7.0'/>"
            "    <style name='NORMAL'><file>Pinned.woff2</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();

        // <size> entries land in mFamilySizes.
        F32 large = 0.f, medium = 0.f;
        CHECK(familySizePt("Pinned", "Large", large));
        CHECK(large == 8.0f);
        CHECK(familySizePt("Pinned", "Medium", medium));
        CHECK(medium == 7.0f);

        // Files contributed by Pinned's <style> block carry mSourceFamily.
        auto names = fileNames("Pinned");
        REQUIRE((S32)names.size() == 1);
        const auto& files = templateFor("Pinned")->getFontFiles();
        CHECK(files[0].mSourceFamily == std::string("Pinned"));
    }

    // mSourceFamily is preserved when files flow through a <use> chain.
    // A <use>s B; B's file appears in A's chain with mSourceFamily=B.
    // This is the plumbing that lets createFont look up B's per-family
    // pin when B-contributed files render under A's head.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: mSourceFamily survives a <use> into another family's chain", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* a = templateFor("A");
        REQUIRE(a != nullptr);
        const auto& files = a->getFontFiles();
        REQUIRE((S32)files.size() == 2);
        CHECK(files[0].mSourceFamily == std::string("A"));
        CHECK(files[1].mSourceFamily == std::string("B"));
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
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a reload swapping pinned override sources drops the first source's file", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        LLSD ovr_a;
        ovr_a["Target"] = "SourceA";
        resolve(ovr_a);
        const LLFontDescriptor* target_a = templateFor("Target");
        REQUIRE(target_a != nullptr);
        const auto& a_files = target_a->getFontFiles();
        REQUIRE_FALSE(a_files.empty());
        CHECK(a_files[0].FileName == std::string("SourceA.woff2"));
        CHECK(a_files[0].mSourceFamily == std::string("SourceA"));

        // Picker swap: production calls reload() which wipes mFontMap +
        // re-parses + re-resolves with the new override. simulateReload
        // mirrors the wipe; loadXml + resolve mirrors the re-parse +
        // re-resolve.
        simulateReload();
        CHECK(loadXml(xml));
        LLSD ovr_b;
        ovr_b["Target"] = "SourceB";
        resolve(ovr_b);
        const LLFontDescriptor* target_b = templateFor("Target");
        REQUIRE(target_b != nullptr);
        const auto& b_files = target_b->getFontFiles();
        REQUIRE_FALSE(b_files.empty());
        CHECK(b_files[0].FileName == std::string("SourceB.woff2"));
        CHECK(b_files[0].mSourceFamily == std::string("SourceB"));
        // After the reload-driven swap, SourceA's file is gone — no
        // leftover state from the prior override.
        CHECK(countFile("Target", "SourceA.woff2") == 0);

        // Both source families' pins remain queryable in mFamilySizes
        // post-reload. createFont uses the head's mSourceFamily to
        // decide its render pt: under ovr_a that hits SourceA's 10,
        // under ovr_b that hits SourceB's 14.
        F32 a_pin = 0.f, b_pin = 0.f;
        CHECK(familySizePt("SourceA", "Large", a_pin));
        CHECK(a_pin == 10.0f);
        CHECK(familySizePt("SourceB", "Large", b_pin));
        CHECK(b_pin == 14.0f);

        // Target's own nameToSize is unaffected by either override —
        // it falls through to global because Target has no own <size>.
        // Pins the "no cross-family routing" rule.
        F32 sz = 0.f;
        CHECK(reg.nameToSize("Target", "Large", sz));
        UNSCOPED_INFO("Target Large = global 11.0 (override does not route)");
        CHECK(sz == 11.0f);
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
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: descriptor identity is name/style/size and a no-file lookup hits", "[LLFontRegistry]")
    {
        font_file_info_vec_t files_a;
        files_a.emplace_back("A.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);
        font_file_info_vec_t files_b;
        files_b.emplace_back("B.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);
        files_b.emplace_back("C.woff2", EFontHinting::FORCE_AUTOHINT, 0, 0.f);

        LLFontDescriptor a("SansSerif", "Small", 0, files_a);
        LLFontDescriptor b("SansSerif", "Small", 0, files_b);

        CHECK(a == b);
        CHECK(hash_value(a) == hash_value(b));

        // Sanity: vary each component, equality breaks.
        CHECK_FALSE(a == LLFontDescriptor("Other", "Small", 0, files_a));
        CHECK_FALSE(a == LLFontDescriptor("SansSerif", "Small", LLFontGL::BOLD, files_a));
        CHECK_FALSE(a == LLFontDescriptor("SansSerif", "Large", 0, files_a));

        // Lookup by no-files descriptor must hit a stored with-files entry.
        // This is what getMatchingFontDesc relies on at runtime.
        boost::unordered_map<LLFontDescriptor, int> m;
        m[a] = 1;
        LLFontDescriptor lookup("SansSerif", "Small", 0); // no files
        auto it = m.find(lookup);
        CHECK(it != m.end());
    }

    // overridesEqual: extra coverage beyond the identical/different value
    // pair in Group 4. Pin the LLSD-comparison semantics initClass relies on
    // when deciding fast-DPI vs full-reload: empty-map and undefined-LLSD
    // are distinct values, and map equality is order-independent.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: overridesEqual tells empty from undefined and ignores key order", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "</fonts>";
        CHECK(loadXml(xml));

        // Resolve with empty-map; mLastFontOverrides becomes empty-map.
        LLSD empty_map = LLSD::emptyMap();
        resolve(empty_map);
        CHECK(reg.overridesEqual(empty_map));

        // Undefined LLSD is distinct from empty-map per llsd_equals.
        LLSD undef;
        CHECK_FALSE(reg.overridesEqual(undef));

        // Map equality is keyed by (key,value) pairs regardless of LLSD's
        // internal iteration order. Pin so a re-ordered override map
        // (e.g. picker rebuilds the map in different order) doesn't trip
        // the fast-path and force a redundant full reload.
        simulateReload();
        CHECK(loadXml(xml));
        LLSD ovr1;
        ovr1["UI"] = "Alt";
        ovr1["Other"] = "Pick";
        LLSD ovr2;
        ovr2["Other"] = "Pick";
        ovr2["UI"] = "Alt";
        resolve(ovr1);
        CHECK(reg.overridesEqual(ovr2));
    }

    // sweepGlyphCaches must not dereference NULL-valued template entries.
    // mFontMap holds NULL-valued templates (size="TEMPLATE") between parse
    // and first getFont, and the production initClass path may tick the
    // sweep during early frames before any head exists. The NULL guard at
    // llfontregistry.cpp:1595 covers heads; this test pins the empty-
    // fallback-cache path too. Crash here = regression.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: sweepGlyphCaches survives a registry holding only templates", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='UI'><style name='NORMAL'><file>U.woff2</file></style></font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        // No heads instantiated, no fallback-cache entries: the map holds
        // the UI template alone, with no font behind it.
        CHECK(templateFor("UI") != nullptr);
        // The case passes by returning: the sweep walks that entry without
        // dereferencing its NULL font.
        reg.sweepGlyphCaches();
    }

    // OpenType variation axes plumbed through XML parse. Family-level
    // font_italic / font_width / font_slant cascade to child <file>
    // entries that don't override; per-file overrides win when present.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: font_italic/font_width/font_slant cascade to files unless overridden", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        const LLFontDescriptor* normal = templateFor("Demo", LLFontGL::NORMAL);
        REQUIRE(normal != nullptr);
        const auto& nfiles = normal->getFontFiles();
        REQUIRE(nfiles.size() == 1u);
        CHECK(nfiles[0].mVarAxes.ital_set);
        CHECK(nfiles[0].mVarAxes.ital == 1.0f);
        CHECK(nfiles[0].mVarAxes.wdth_set);
        CHECK(nfiles[0].mVarAxes.wdth == 87.5f);
        CHECK(nfiles[0].mVarAxes.slnt_set);
        CHECK(nfiles[0].mVarAxes.slnt == -12.0f);

        // Per-file override on wdth wins; ital/slnt still inherit.
        const LLFontDescriptor* bold = templateFor("Demo", LLFontGL::BOLD);
        REQUIRE(bold != nullptr);
        const auto& wfiles = bold->getFontFiles();
        REQUIRE(wfiles.size() == 1u);
        CHECK(wfiles[0].mVarAxes.wdth == 125.0f);
        CHECK(wfiles[0].mVarAxes.ital_set);
        CHECK(wfiles[0].mVarAxes.ital == 1.0f);
        CHECK(wfiles[0].mVarAxes.slnt_set);
        CHECK(wfiles[0].mVarAxes.slnt == -12.0f);
    }

    // Files without any axis attribute leave ALFontVarAxes at its
    // default (no *_set flags). Pins the negative path that the cache
    // key + face-load skip rely on.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a file with no axis attributes leaves every ALFontVarAxes flag unset", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='Plain'>"
            "    <style name='NORMAL'><file>Plain.ttf</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        const LLFontDescriptor* d = templateFor("Plain", LLFontGL::NORMAL);
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->getFontFiles().empty());
        const auto& f = d->getFontFiles()[0];
        CHECK_FALSE(f.mVarAxes.wght_set);
        CHECK_FALSE(f.mVarAxes.opsz_set);
        CHECK_FALSE(f.mVarAxes.ital_set);
        CHECK_FALSE(f.mVarAxes.wdth_set);
        CHECK_FALSE(f.mVarAxes.slnt_set);
    }

    // font_optical_size cascades family -> file the same way
    // font_weight does. Per-file override wins; absence at file level
    // inherits the family value. Pins the new opsz axis plumbing.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: font_optical_size cascades to files unless a file overrides it", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        const LLFontDescriptor* normal = templateFor("Demo", LLFontGL::NORMAL);
        REQUIRE(normal != nullptr);
        const auto& nfiles = normal->getFontFiles();
        REQUIRE(nfiles.size() == 1u);
        CHECK(nfiles[0].mVarAxes.opsz_set);
        CHECK(nfiles[0].mVarAxes.opsz == 14.0f);

        const LLFontDescriptor* bold = templateFor("Demo", LLFontGL::BOLD);
        REQUIRE(bold != nullptr);
        const auto& bfiles = bold->getFontFiles();
        REQUIRE(bfiles.size() == 1u);
        CHECK(bfiles[0].mVarAxes.opsz_set);
        CHECK(bfiles[0].mVarAxes.opsz == 28.0f);
    }

    // replace_first=true on a family→family override drops the target's
    // head file before prepending the source. Mirror of Group 4's
    // family→family override case with the new map shape.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: replace_first drops the target's head before prepending the source", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);
        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 1);
        CHECK(names[0] == std::string("Alt.woff2"));
    }

    // replace_first=true preserves orig_files[1..]. Verifies the
    // tail-merge contract: drop only the head, keep DejaVu/Emoji/CJK.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: replace_first keeps the target's fallbacks after its head", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        // Sanity: post-resolve UI's chain is [UI.woff2, DejaVu.woff2].
        auto pre_names = fileNames("UI");
        REQUIRE((S32)pre_names.size() == 2);
        CHECK(pre_names[0] == std::string("UI.woff2"));

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("Alt.woff2"));
        CHECK(names[1] == std::string("DejaVu.woff2"));
    }

    // replace_first=true with family→file override: the target's head
    // is dropped and the synthesized override file takes its slot;
    // tail still preserved.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: replace_first with a file override puts the file in the head's slot", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "user.ttf";
        entry["replace_first"] = true;
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("user.ttf"));
        CHECK(names[1] == std::string("DejaVu.woff2"));
    }

    // replace_first=false in the map shape behaves identically to the
    // bare string shape: the override is prepended onto the chain and
    // the target's head stays as a fallback. Pin the legacy semantics
    // on the new map shape so it can't accidentally diverge.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: replace_first=false prepends like the bare string shape", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Alt";
        entry["replace_first"] = false;  // explicit false
        overrides["UI"] = entry;
        applyOverrides(overrides);

        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("Alt.woff2"));
        CHECK(names[1] == std::string("UI.woff2"));
    }

    // String shape continues to prepend (no replace). Regression guard
    // for the most common user-facing override path.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a bare string override still prepends without replacing", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        LLSD overrides;
        overrides["UI"] = "Alt";  // bare string
        applyOverrides(overrides);
        auto names = fileNames("UI");
        REQUIRE((S32)names.size() == 2);
        CHECK(names[0] == std::string("Alt.woff2"));
        CHECK(names[1] == std::string("UI.woff2"));
    }

    // <use unicode_ranges="..."/> attaches a CharFunctor to every file
    // contributed by the named family. Pin: a consumer that does
    // `<use family="EmojiBase" unicode_ranges="U+1F000-U+1FFFF"/>`
    // ends up with EmojiBase's file gated by that range — accepts an
    // astral codepoint, rejects ASCII.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: <use unicode_ranges> gates the used family's files with a CharFunctor", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Consumer");
        REQUIRE(d != nullptr);
        const auto& files = d->getFontFiles();
        REQUIRE((S32)files.size() == 2);
        CHECK(files[0].FileName == std::string("UI.woff2"));
        CHECK(files[1].FileName == std::string("Noto.ttf"));
        REQUIRE(static_cast<bool>(files[1].CharFunctor));
        CHECK(files[1].CharFunctor((llwchar)0x1F600));
        CHECK_FALSE(files[1].CharFunctor((llwchar)0x0041));
    }

    // Composition: file's own unicode_ranges intersects with the
    // use-level filter. Both gates must accept a codepoint for it to
    // pass.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a file's unicode_ranges and the <use> filter must both accept", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Consumer");
        REQUIRE(d != nullptr);
        const auto& files = d->getFontFiles();
        REQUIRE((S32)files.size() == 1);
        UNSCOPED_INFO("composed functor accepts U+1F500 (in both)");
        CHECK(files[0].CharFunctor((llwchar)0x1F500));
        UNSCOPED_INFO("composed functor rejects U+1F100 (in file, NOT in use)");
        CHECK_FALSE(files[0].CharFunctor((llwchar)0x1F100));
        UNSCOPED_INFO("composed functor rejects U+1F900 (in use, NOT in file)");
        CHECK_FALSE(files[0].CharFunctor((llwchar)0x1F900));
        UNSCOPED_INFO("composed functor rejects U+0041 (in neither)");
        CHECK_FALSE(files[0].CharFunctor((llwchar)0x0041));
    }

    // Two consumers share one source with different filters. Each
    // resolved chain has its own functor — no cross-talk.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: two <use>rs of one family keep their own filters", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        const LLFontDescriptor* broad = templateFor("Broad");
        const LLFontDescriptor* narrow = templateFor("Narrow");
        REQUIRE(broad != nullptr);
        REQUIRE(narrow != nullptr);
        const auto& bf = broad->getFontFiles();
        const auto& nf = narrow->getFontFiles();
        REQUIRE((S32)bf.size() == 1);
        REQUIRE((S32)nf.size() == 1);
        // Broad accepts U+1F100; Narrow doesn't.
        CHECK(bf[0].CharFunctor((llwchar)0x1F100));
        CHECK_FALSE(nf[0].CharFunctor((llwchar)0x1F100));
        // Both accept U+1F600 (in their own range).
        CHECK(bf[0].CharFunctor((llwchar)0x1F600));
        CHECK(nf[0].CharFunctor((llwchar)0x1F600));
    }

    // Single override on the source family propagates to every consumer
    // with each consumer's filter intact. This is the load-bearing
    // motivation for use-level filters: one override entry, two chains
    // updated, each with its own glyph gate.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override on a used family reaches each <use>r with its filter", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        // Override at the SOURCE — EmojiBase gets prepended with Twemoji.
        // Apply BEFORE resolveFontReferences so the consumers' use chains
        // pick up the new EmojiBase head when collected. (The fixture's
        // `resolve()` calls resolveFontReferences which itself runs
        // applyFamilyOverrides first.)
        LLSD overrides;
        overrides["EmojiBase"] = "Twemoji.ttf";
        resolve(overrides);

        const LLFontDescriptor* broad = templateFor("Broad");
        const LLFontDescriptor* narrow = templateFor("Narrow");
        REQUIRE(broad != nullptr);
        REQUIRE(narrow != nullptr);
        const auto& bf = broad->getFontFiles();
        const auto& nf = narrow->getFontFiles();
        // Each consumer sees BOTH files (Twemoji prepended onto the
        // EmojiBase chain by the override; Noto preserved behind it),
        // each with the consumer's filter applied.
        REQUIRE((S32)bf.size() == 2);
        CHECK(bf[0].FileName == std::string("Twemoji.ttf"));
        REQUIRE((S32)nf.size() == 2);
        CHECK(nf[0].FileName == std::string("Twemoji.ttf"));
        // Filter still gates each consumer independently.
        CHECK(bf[0].CharFunctor((llwchar)0x1F100));
        CHECK_FALSE(nf[0].CharFunctor((llwchar)0x1F100));
    }

    // Combined replace_first + EmojiBase: { value: "Twemoji.ttf",
    // replace_first: true } on EmojiBase fully displaces Noto rather
    // than prepending in front of it. Each consumer's chain ends up
    // with just the override file gated by the consumer's filter.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: replace_first on a used family displaces its file in every <use>r", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));

        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "Twemoji.ttf";
        entry["replace_first"] = true;
        overrides["EmojiBase"] = entry;
        resolve(overrides);

        const LLFontDescriptor* broad = templateFor("Broad");
        const LLFontDescriptor* narrow = templateFor("Narrow");
        REQUIRE(broad != nullptr);
        REQUIRE(narrow != nullptr);
        const auto& bf = broad->getFontFiles();
        const auto& nf = narrow->getFontFiles();
        // replace_first dropped Noto from EmojiBase's chain; consumers
        // pull just Twemoji through their respective use-level filters.
        REQUIRE((S32)bf.size() == 1);
        CHECK(bf[0].FileName == std::string("Twemoji.ttf"));
        REQUIRE((S32)nf.size() == 1);
        CHECK(nf[0].FileName == std::string("Twemoji.ttf"));
        CHECK(bf[0].CharFunctor((llwchar)0x1F100));
        CHECK_FALSE(nf[0].CharFunctor((llwchar)0x1F100));
        CHECK(bf[0].CharFunctor((llwchar)0x1F600));
        CHECK(nf[0].CharFunctor((llwchar)0x1F600));
    }

    // No-cascade regression: a use-level filter applies ONLY to the
    // named family's own files. When that family <use>s another, the
    // filter does NOT propagate to the inner use's files. This is the
    // explicit design rule (filter is local to the use site) — pin it
    // so a future "fix" that propagates the filter through the chain
    // doesn't silently change semantics for existing chains.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> filter does not cascade into the used family's own <use>s", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* d = templateFor("Outer");
        REQUIRE(d != nullptr);
        const auto& files = d->getFontFiles();
        // Outer pulls Mid's own files (Mid.ttf) PLUS Mid's <use> chain
        // (Inner.ttf). The filter applies only to Mid's direct files.
        CHECK((S32)files.size() == 2);

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
        REQUIRE(mid   != nullptr);
        REQUIRE(inner != nullptr);

        // Mid is filtered (use-level filter applied to its own files).
        REQUIRE(static_cast<bool>(mid->CharFunctor));
        CHECK(mid->CharFunctor((llwchar)0x1F600));
        CHECK_FALSE(mid->CharFunctor((llwchar)0x0041));

        // Inner is NOT filtered — the use-level filter doesn't cascade.
        CHECK_FALSE(static_cast<bool>(inner->CharFunctor));
    }

    // emoji="true" attribute parses; getAvailableFamilies(EMOJI) returns
    // only emoji-flagged families. PROPORTIONAL and MONOSPACE filter
    // emoji families OUT so they don't surface in regular text-font
    // pickers (a color-emoji file shouldn't appear as a UI choice).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: emoji=true families list under EMOJI and drop out of the others", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();

        // EMOJI returns the two emoji families, sorted by label.
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        CHECK((S32)em.size() == 2);
        std::set<std::string> em_names;
        for (const auto& f : em) em_names.insert(f.name);
        CHECK(em_names.count("NotoEmoji") == 1);
        CHECK(em_names.count("Twemoji")   == 1);

        // PROPORTIONAL excludes both monospace AND emoji.
        auto prop = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::PROPORTIONAL);
        REQUIRE((S32)prop.size() == 1);
        CHECK(prop[0].name == std::string("Sans"));

        // MONOSPACE excludes emoji families even if they were also
        // marked monospace=true (emoji wins for filter routing).
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        REQUIRE((S32)mono.size() == 1);
        CHECK(mono[0].name == std::string("Mono"));
    }

    // emoji="true" + monospace="true" on the same family routes to
    // EMOJI only, not MONOSPACE. Locks the "emoji wins" rule from the
    // FamilyFilter docs.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a family both emoji and monospace lists under EMOJI only", "[LLFontRegistry]")
    {
        const char* xml =
            "<fonts>"
            "  <font name='WeirdMonoEmoji' monospace='true' emoji='true'>"
            "    <style name='NORMAL'><file>W.ttf</file></style>"
            "  </font>"
            "</fonts>";
        CHECK(loadXml(xml));
        resolve();
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        CHECK((S32)em.size() == 1);
        auto mono = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::MONOSPACE);
        CHECK((S32)mono.size() == 0);
    }

    // user_selectable="false" still hides a family from EMOJI just like
    // it hides from PROPORTIONAL/MONOSPACE. Pin so internal aliases
    // (EmojiBase, Emoji, LimitedEmoji) don't surface even if some
    // future change marks them emoji=true.
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: user_selectable=false hides a family from EMOJI too", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        auto em = reg.getAvailableFamilies(LLFontRegistry::FamilyFilter::EMOJI);
        REQUIRE((S32)em.size() == 1);
        CHECK(em[0].name == std::string("Public"));
    }

    // Baseline: a use-only EmojiBase that <use>s NotoEmoji, consumed
    // by Emoji with a unicode_ranges filter. Emoji's resolved chain
    // contains exactly one Noto file (contributed via the deepest <use>
    // step, with no filter — the use-level range is dropped on
    // recursion per the explicit design rule, see the no-cascade case above).
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: a <use> filter on a use-only family leaves the deeper file unfiltered", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        resolve();
        const LLFontDescriptor* emoji = templateFor("Emoji");
        REQUIRE(emoji != nullptr);
        const auto& files = emoji->getFontFiles();
        REQUIRE((S32)files.size() == 1);
        CHECK(files[0].FileName == std::string("Noto.ttf"));
        CHECK_FALSE(static_cast<bool>(files[0].CharFunctor));
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
    TEST_CASE_METHOD(llfontregistry_data, "LLFontRegistry: an override to a family the target already <use>s adds no file", "[LLFontRegistry]")
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
        CHECK(loadXml(xml));
        LLSD overrides;
        LLSD entry = LLSD::emptyMap();
        entry["value"] = "NotoEmoji";
        entry["replace_first"] = true;
        overrides["EmojiBase"] = entry;
        resolve(overrides);

        const LLFontDescriptor* emoji = templateFor("Emoji");
        REQUIRE(emoji != nullptr);
        CHECK((S32)emoji->getFontFiles().size() == 1);
    }
}
