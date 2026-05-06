/**
 * @file llfontregistry_test.cpp
 * @brief Unit tests for LLFontDescriptor + LLFontRegistry's parser,
 *        resolver, override path, and lookup helpers.
 *
 * Exercises the pure-CPU surface of llfontregistry — descriptor
 * normalization, init_from_xml, resolveFontReferences, applyFamilyOverrides,
 * nameToSize/getMatchingFontDesc/getClosestFontTemplate, getAvailableFamilies.
 * FreeType-backed integration is covered by llfontcolrv1_test.cpp.
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
    };

    typedef test_group<llfontregistry_data> llfontregistry_test;
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
        desc.addFontFile("Foo.ttf", EFontHinting::DEFAULT, 0, 0.f, -1);
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
        a.addFontFile("a.ttf", EFontHinting::DEFAULT, 0, 0.f, -1);
        b.addFontFile("b.ttf", EFontHinting::DEFAULT, 0, 0.f, -1);
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
        ensure_equals("NORMAL inherits family weight 400",
                      normal->getFontFiles()[0].mWeight, 400);
        ensure_equals("NORMAL inherits family hinting light",
                      (S32)normal->getFontFiles()[0].mHinting,
                      (S32)EFontHinting::LIGHT);
        ensure_equals("BOLD overrides weight to 700",
                      bold->getFontFiles()[0].mWeight, 700);
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
    // post-<use> chain. F BOLD ends up with [F-B, B] from its own <use>
    // walk, then [F-R, B] appended from NORMAL's post-<use> list — so B
    // intentionally appears twice. The freetype-load fallback cache
    // (mFallbackInstanceCache, keyed by file + face params) dedups at
    // load time, so the doubled descriptor entry doesn't double-load.
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
        ensure_equals("4 files: own bold + B from <use> + NORMAL inherit + B again",
                      (S32)bold.size(), 4);
        ensure_equals("F-B first",        bold[0], std::string("F-B.woff2"));
        ensure_equals("B from <use>",     bold[1], std::string("B.woff2"));
        ensure_equals("F-R inherited",    bold[2], std::string("F-R.woff2"));
        ensure_equals("B carried by NORMAL's <use>",
                      bold[3], std::string("B.woff2"));
    }

    // Calling resolve() a second time is a no-op — mFamilyUses /
    // mInheritFlags are consumed at the end of the first call.
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
        ensure_equals("weight inherited", files[0].mWeight, 400);
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

    // applyOverrides with a non-map LLSD must still clear the prior
    // mFamilyOverrideSources — otherwise nameToSize would keep routing
    // through a now-removed override.
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
        F32 sz = 0.f;
        ensure("UI Custom routes through Alt's size",
               reg.nameToSize("UI", "Custom", sz));
        ensure_equals("UI Custom = Alt's 11.5", sz, 11.5f);
        // Now reset with empty / non-map LLSD.
        applyOverrides(LLSD());
        F32 sz2 = 0.f;
        reg.nameToSize("UI", "Custom", sz2);
        ensure_equals("UI Custom falls back to UI's own value", sz2, 9.5f);
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
    // regression that merged would compound stale routings.
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

        // First override: UI → AltA. UI's "Custom" size routes to 11.5.
        LLSD ovr1;
        ovr1["UI"] = "AltA";
        applyOverrides(ovr1);
        F32 sz = 0.f;
        reg.nameToSize("UI", "Custom", sz);
        ensure_equals("first override routes UI->AltA (11.5)", sz, 11.5f);

        // Second override: UI → AltB. The earlier UI→AltA mapping must
        // be replaced, not merged — UI now routes to AltB's 13.5.
        LLSD ovr2;
        ovr2["UI"] = "AltB";
        applyOverrides(ovr2);
        F32 sz2 = 0.f;
        reg.nameToSize("UI", "Custom", sz2);
        ensure_equals("second override replaces first (UI->AltB = 13.5)",
                      sz2, 13.5f);
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
            if (FILE* f = std::fopen(path.c_str(), "rb"))
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

        bool interFontPresent() const
        {
            return fileExists(std::string(kFontsDir) + "InterVariable.woff2");
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
    // Reset semantics: LLFontFace::destroyGL → resetBitmapCache clears
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
#endif // LL_MESA_HEADLESS
}
