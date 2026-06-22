/**
 * @file llfontgl_test.cpp
 * @brief Smoke test for the OSMesa-backed headless GL harness.
 *
 * Brings up a real GL context via LLWindowMesaHeadless, runs
 * LLFontGL::initClass through the explicit-fonts.xml-path overload,
 * and verifies the GL-backed atlas pipeline produces a live texture.
 *
 * The single fixture instance shared across test methods (see
 * llheadlessgl_fixture.h) keeps the OSMesa context creation cost
 * out of every test — but each test method still gets a fresh
 * sFontRegistry, so font-state mutations don't leak.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontgl.h"
#include "../llfontfreetype.h"
#include "../llfontface.h"        // LLFontFace (linear-metric regime test)
#include "../llfontregistry.h"    // EFontHinting
#include "../llfontbitmapcache.h"
#include "../llhbgpu.h"   // LL_HAS_HB_GPU (for the hb-gpu render-path test)
#include "../llimagegl.h"

#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <cstdio>

namespace
{
#ifndef LLFONT_TEST_APP_DIR
#  define LLFONT_TEST_APP_DIR ""
#endif
#ifndef LLFONT_TEST_FONTS_XML
#  define LLFONT_TEST_FONTS_XML ""
#endif
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

    constexpr const char* kAppDir   = LLFONT_TEST_APP_DIR;
    constexpr const char* kFontsXml = LLFONT_TEST_FONTS_XML;
    // Raw font files (newview/fonts/) for direct LLFontFreetype::loadFace tests.
    constexpr const char* kFontDir  = LLFONT_TEST_DATA_DIR;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }
}

namespace tut
{
    // Shared OSMesa fixture. TUT spins fixtures per-test method, but
    // per-fixture HeadlessGL recreation poisons LLFontGL's per-getter
    // static fontp caches after ~10 cycles (the per-test new GL
    // context invalidates LLImageGL textures the fontps reference).
    // Sharing the GL context for the binary's lifetime keeps font
    // state coherent across all tests.
    //
    // needs_render=true so the render group can drive gGL.beginList /
    // flush against a real bound gUIProgram. Layout-only tests (which
    // never issue a draw) still work fine — the bound shader is harmless
    // when no draw call is in flight.
    inline ll_test::HeadlessGL& getSharedFontGL()
    {
        static ll_test::HeadlessGL gl(/*needs_vbos=*/true,
                                      /*needs_imagegl=*/true,
                                      /*needs_llrender=*/true,
                                      /*needs_render=*/true);
        return gl;
    }

    struct llfontgl_data
    {
        ll_test::HeadlessGL& gl = getSharedFontGL();
        llfontgl_data() = default;
        ~llfontgl_data() = default;
    };

    typedef test_group<llfontgl_data> llfontgl_test;
    typedef llfontgl_test::object     llfontgl_object;
    tut::llfontgl_test llfontgl_testcase("LLFontGL");

    // initClass through the explicit-path overload should bring up a
    // working font registry under a real GL context. loadDefaultFonts
    // (called from initClass) returns true only if every required
    // family resolved through to a usable LLFontFreetype.
    template<> template<>
    void llfontgl_object::test<1>()
    {
        if (!fileExists(kFontsXml))
        {
            skip("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        LLFontGL::initClass(/*screen_dpi=*/96.f,
                            /*x_scale=*/1.f, /*y_scale=*/1.f,
                            /*app_dir=*/kAppDir,
                            /*fonts_xml_path=*/kFontsXml,
                            /*font_overrides=*/LLSD(),
                            /*create_gl_textures=*/true);

        ensure("default fonts loaded",
               LLFontGL::loadDefaultFonts());
        ensure("SansSerif resolves",
               LLFontGL::getFontSansSerif() != nullptr);
    }

    // After ASCII rasterization, the SansSerif font's bitmap cache
    // should have at least one greyscale page with a live GL texture.
    // Catches the silent-no-op failure mode where the GL context is
    // healthy but createGLTexture skipped the upload.
    template<> template<>
    void llfontgl_object::test<2>()
    {
        if (!fileExists(kFontsXml))
        {
            skip("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        // initClass is single-shot; if test<1> ran first the registry is
        // already up. The call below either initializes (test<2> ran
        // standalone) or warns + bails (test<1> ran first). Either way
        // the assertions below operate against a working registry.
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);

        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("SansSerif resolves", font != nullptr);

        // Force the ASCII range to rasterize so the atlas isn't empty.
        // (createFont's pre-warm in getFont already does this on first
        // resolve; calling again is a fast no-op via head cache.)
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

    // getFontMonospace resolves and reports fixed-width through its
    // underlying freetype. Pins the registry's monospace family
    // routing — a regression that resolved Monospace to a
    // proportional fallback would surface here.
    template<> template<>
    void llfontgl_object::test<3>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* mono = LLFontGL::getFontMonospace();
        ensure("Monospace resolves", mono != nullptr);
        const LLFontFreetype* ft = mono->getFontFreetype();
        ensure("Monospace has a freetype", ft != nullptr);
        ensure("Monospace freetype is fixed-width", ft->isFixedWidth());
    }

    // getFontSansSerifBig resolves with positive line-height.
    // Captures the registry's size-suffix routing — Big size must
    // produce a freetype whose metrics scale up from the base.
    template<> template<>
    void llfontgl_object::test<4>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* small = LLFontGL::getFontSansSerifSmall();
        LLFontGL* big   = LLFontGL::getFontSansSerifBig();
        ensure("Small SansSerif resolves", small != nullptr);
        ensure("Big   SansSerif resolves", big   != nullptr);
        const LLFontFreetype* fs = small->getFontFreetype();
        const LLFontFreetype* fb = big->getFontFreetype();
        ensure("Small line-height > 0",  fs->getLineHeight() > 0.f);
        ensure("Big   line-height > 0",  fb->getLineHeight() > 0.f);
        ensure("Big line-height > Small line-height",
               fb->getLineHeight() > fs->getLineHeight());
    }

    // Repeat-call getter caches: getFontSansSerif() returns the same
    // pointer on second call. Pins the per-getter static cache —
    // a regression that re-resolved on every call would change the
    // pointer (and waste a registry lookup per UI label render).
    template<> template<>
    void llfontgl_object::test<5>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* a = LLFontGL::getFontSansSerif();
        LLFontGL* b = LLFontGL::getFontSansSerif();
        ensure("first resolve non-null",  a != nullptr);
        ensure_equals("second resolve returns the same instance", a, b);
    }

    // generateASCIIglyphs is idempotent — repeat calls don't allocate
    // additional atlas sheets. Pins the head-cache lookup that makes
    // the second call a fast no-op.
    template<> template<>
    void llfontgl_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        font->generateASCIIglyphs();
        const LLFontFreetype* ft    = font->getFontFreetype();
        const LLFontBitmapCache* bc = ft->getFontBitmapCache();
        const U32 sheets_after_first = bc->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure("at least one sheet after first generate",
               sheets_after_first >= 1u);

        font->generateASCIIglyphs();
        const U32 sheets_after_second = bc->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure_equals("second generate is idempotent (no extra sheets)",
                      sheets_after_second, sheets_after_first);
    }

    // getFontByName returns the same instance for the same legacy
    // name string. Pins the legacy-name routing in the registry.
    template<> template<>
    void llfontgl_object::test<7>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* a = LLFontGL::getFontByName("SANSSERIF");
        LLFontGL* b = LLFontGL::getFontByName("SANSSERIF");
        ensure("legacy name resolves to non-null", a != nullptr);
        ensure_equals("repeat lookup returns same instance", a, b);
    }

    // collectGarbage on the font face does not crash and (since
    // we just rendered the ASCII set) is a fast no-op — sheets
    // are recently used. Pins 169390b593 (cache-sweep moved off
    // hot path) and 7c05b2de82 (collectGarbage safe at any frame
    // boundary).
    template<> template<>
    void llfontgl_object::test<8>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        const LLFontFreetype* ft = font->getFontFreetype();
        ensure("freetype present", ft != nullptr);

        ft->collectGarbage();
        ft->collectGarbage();
        ensure("repeat collectGarbage is throttled and safe", true);
    }

    // Re-calling initClass after the registry is up is a no-op
    // (warns + bails). The registry pointer must remain stable so
    // LLFontGL's static fontp caches don't dangle.
    template<> template<>
    void llfontgl_object::test<9>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* before = LLFontGL::getFontSansSerif();
        ensure("first resolve non-null", before != nullptr);

        // Re-call initClass with different DPI. The single-shot
        // guard short-circuits, so the registry stays the same and
        // getter caches keep returning the same pointer.
        LLFontGL::initClass(72.f, 0.5f, 0.5f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* after = LLFontGL::getFontSansSerif();
        ensure_equals("second resolve returns same instance after re-init",
                      after, before);
    }

    // getWidthF32: legacy 2-arg overload defaults max_chars=text length;
    // a 4-arg call with explicit max_chars matches it. Pins legacy
    // entry-point's max_chars defaulting (llfontgl.cpp:1063).
    template<> template<>
    void llfontgl_object::test<11>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");
        const F32 wfull = font->getWidthF32(s.c_str());
        const F32 wexp  = font->getWidthF32(s.c_str(), 0,
                                            (S32)s.size(), false);
        ensure("getWidthF32 with default max == explicit max",
               wfull == wexp);
        ensure("width is positive", wfull > 0.f);
    }

    // Width monotonicity: width(AAA) > width(AA) > width(A). Used by
    // render()'s halign math at llfontgl.cpp:354-372 — the alignment
    // calc relies on accumulated width strictly increasing with
    // character count for the same string.
    template<> template<>
    void llfontgl_object::test<12>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        LLWString a   = utf8str_to_wstring("A");
        LLWString aa  = utf8str_to_wstring("AA");
        LLWString aaa = utf8str_to_wstring("AAA");
        const F32 w1 = font->getWidthF32(a.c_str());
        const F32 w2 = font->getWidthF32(aa.c_str());
        const F32 w3 = font->getWidthF32(aaa.c_str());
        ensure("width(A) > 0",      w1 > 0.f);
        ensure("width(AA) > width(A)",   w2 > w1);
        ensure("width(AAA) > width(AA)", w3 > w2);
    }

    // Ellipsis budget: getWidthF32(L"....") is positive — render() pre-
    // computes this for the truncation path (llfontgl.cpp:459 references
    // the dots constant). A regression where '.' rasterized to a 0-width
    // glyph would silently break ellipsis sizing.
    template<> template<>
    void llfontgl_object::test<13>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        LLWString dots = utf8str_to_wstring("....");
        const F32 w = font->getWidthF32(dots.c_str());
        ensure("ellipsis width is positive", w > 0.f);
    }

    // maxDrawableChars boundary: zero pixel budget fits 0 chars,
    // F32_MAX fits all, mid-budget fits a partial prefix. Pins the
    // measurement loop at llfontgl.cpp:1210.
    template<> template<>
    void llfontgl_object::test<14>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        ensure_equals("budget 0 fits 0 chars",
                      font->maxDrawableChars(s.c_str(), 0.f, (S32)s.size()),
                      (S32)0);
        ensure_equals("budget F32_MAX fits all chars",
                      font->maxDrawableChars(s.c_str(), F32_MAX,
                                             (S32)s.size()),
                      (S32)s.size());
        // Find a budget that should fit "Hel" but not "Hell".
        const F32 w_hel  = font->getWidthF32(s.c_str(), 0, 3, false);
        const F32 w_hell = font->getWidthF32(s.c_str(), 0, 4, false);
        // Budget halfway between produces partial fit.
        const F32 mid_budget = (w_hel + w_hell) * 0.5f;
        const S32 fit = font->maxDrawableChars(s.c_str(), mid_budget,
                                               (S32)s.size());
        ensure("partial-budget fit is in [3, 3] (Hel exactly)",
               fit == 3);
    }

    // charFromPixelOffset round-trip: for each prefix length i, the
    // pixel offset corresponding to the prefix's width must invert to
    // i. Pins the inverse path at llfontgl.cpp:1518 — width
    // accumulation and pixel→char lookup must agree.
    template<> template<>
    void llfontgl_object::test<15>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");
        // Compare pixel→char inverse for each prefix length 0..5.
        for (S32 i = 0; i <= (S32)s.size(); ++i)
        {
            const F32 w = font->getWidthF32(s.c_str(), 0, i, false);
            const S32 cp = font->charFromPixelOffset(s.c_str(), 0,
                                                     w,
                                                     F32_MAX,
                                                     (S32)s.size(),
                                                     /*round=*/true);
            // round=true rounds to the nearest character boundary, so
            // hitting exactly w should map back to i (or off-by-one
            // due to rounding at the half-glyph mark — accept ±1).
            ensure("round-trip char index is within ±1",
                   std::abs(cp - i) <= 1);
        }
    }

    // Mixed-script width consistency: width("a你") ≈ width("a") +
    // width("你"). Pins shape-range itemization (e671bde0d1) at the
    // width measurement boundary — a regression that shaped "a你"
    // against DejaVu only (replacing 你 with notdef) would diverge.
    // Skip if the default fallback chain doesn't cover U+4F60.
    template<> template<>
    void llfontgl_object::test<16>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const llwchar a_arr[] = { L'a', 0 };
        const llwchar c_arr[] = { 0x4F60, 0 };
        const llwchar ac_arr[] = { L'a', 0x4F60, 0 };
        LLWString a_s(a_arr, 1);
        LLWString c_s(c_arr, 1);
        LLWString ac_s(ac_arr, 2);

        const F32 wa = font->getWidthF32(a_s.c_str());
        const F32 wc = font->getWidthF32(c_s.c_str());
        const F32 wac = font->getWidthF32(ac_s.c_str());
        if (wc == 0.f)
            skip("font's fallback chain doesn't cover U+4F60");
        // Tolerance: ~0.5 px accounts for sub-pixel positioning across
        // shape-range boundaries.
        ensure("width(a你) ≈ width(a) + width(你) within 0.5px",
               std::abs(wac - (wa + wc)) <= 0.5f);
    }

    // charFromPixelOffset must snap to cluster boundaries on a
    // multi-codepoint emoji during mouse-driven cursor placement.
    // The trans-flag ZWJ sequence (1F3F3 FE0F 200D 26A7 FE0F) shapes
    // to a single glyph; as target_x walks across that glyph's pixel
    // span, the returned cursor position must only ever be the
    // cluster's start or its end. Mid-cluster returns oscillate the
    // selection rect's right edge across the cluster's interior —
    // observed as drag-select highlight flicker on ZWJ flags / family
    // emoji where 🐶 (1 codepoint) is unaffected.
    template<> template<>
    void llfontgl_object::test<18>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        // "X" + trans flag (5 cps) + "Y": cluster occupies indices 1..6.
        const llwchar arr[] = { L'X', 0x1F3F3, 0xFE0F, 0x200D, 0x26A7,
                                0xFE0F, L'Y', 0 };
        LLWString s(arr, 7);

        const F32 wfull = font->getWidthF32(s.c_str(), 0, 7, false);
        if (wfull <= 0.f)
            skip("emoji fallback not available");

        // Sanity: width up to cluster end (6) must exceed width up to
        // cluster start (1) by at least a few pixels — otherwise the
        // emoji didn't actually shape and the cluster-snap test below
        // would trivially pass on an unrendered glyph.
        const F32 w_pre  = font->getWidthF32(s.c_str(), 0, 1, false);
        const F32 w_post = font->getWidthF32(s.c_str(), 0, 6, false);
        if (w_post - w_pre < 2.f)
            skip("emoji cluster did not shape to a measurable glyph");

        // Walk target_x across the rendered cluster's pixel span at
        // sub-pixel granularity. Every returned cursor pos must land
        // on a cluster boundary — never inside the cluster (2..5).
        const F32 cluster_left  = w_pre;
        const F32 cluster_right = w_post;
        for (F32 x = cluster_left - 1.f; x <= cluster_right + 1.f;
             x += 0.25f)
        {
            const S32 cp = font->charFromPixelOffset(s.c_str(), 0,
                                                     x, F32_MAX,
                                                     7, /*round=*/true);
            const bool inside_cluster = (cp >= 2 && cp <= 5);
            ensure("charFromPixelOffset round=true returns mid-cluster index",
                   !inside_cluster);
        }
    }

    // Linear (hb-gpu) metric regime. An outline face built with
    // sEnableFontGpu reports UNHINTED advances + global metrics and forces the
    // subpixel pen, so layout matches the unhinted outlines the analytic path
    // draws. The regime is part of LLFontFaceKey, so it caches distinctly from
    // the hinted face and the AlchemyFontRenderGPU toggle's reload re-resolves
    // to the right one instead of reusing a stale wrapper.
    template<> template<>
    void llfontgl_object::test<19>()
    {
        const std::string path = std::string(kFontDir) + "IBMPlexSansVar-Roman.ttf";
        if (!fileExists(path))
            skip("test font not found: " + path);

        // Bring up the font manager (gFontManagerp) + FT library. initClass is
        // single-shot; if a prior test already ran it this warns + no-ops, but
        // gFontManagerp is up either way.
        if (fileExists(kFontsXml))
            LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                                LLSD(), /*create_gl_textures=*/true);
        if (!gFontManagerp)
            skip("font manager not initialized (fonts.xml missing?)");

        // Save/restore the process-global so sibling tests aren't perturbed.
        const bool saved_gpu = LLFontGL::sEnableFontGpu;

        // Small size so DEFAULT (native) hinting visibly grid-fits the advance
        // and vertical metrics away from their linear (unrounded) values.
        constexpr F32 PT = 9.f, DPI = 96.f;
        auto load_head = [&](bool gpu) -> LLPointer<LLFontFreetype>
        {
            LLFontGL::sEnableFontGpu = gpu;
            LLPointer<LLFontFreetype> head = new LLFontFreetype();
            if (!head->loadFace(path, PT, DPI, DPI, /*is_fallback=*/false,
                                /*face_n=*/0, EFontHinting::DEFAULT, 0, {}))
                head = nullptr;
            return head;
        };

        LLPointer<LLFontFreetype> hinted  = load_head(false);
        LLPointer<LLFontFreetype> linear  = load_head(true);
        LLPointer<LLFontFreetype> hinted2 = load_head(false);  // cache-hit probe
        LLFontGL::sEnableFontGpu = saved_gpu;

        ensure("hinted face loaded",  hinted.notNull());
        ensure("linear face loaded",  linear.notNull());
        ensure("hinted2 face loaded", hinted2.notNull());

        // The regime is in the cache key: hinted vs linear resolve to DISTINCT
        // face wrappers, while two same-regime loads share one. This is what
        // makes the GPU toggle's fast-path reload actually swap metric regimes
        // rather than hand back the stale cached face.
        ensure("linear regime is a distinct cached face",
               linear->getFontFace() != hinted->getFontFace());
        ensure("same regime reuses the cached face",
               hinted2->getFontFace() == hinted->getFontFace());

        // Subpixel pen forced on in the linear regime even under DEFAULT
        // hinting (so per-glyph ll_round doesn't crush the linear advances);
        // off in the hinted regime.
        ensure("hinted DEFAULT face does not use subpixel pen",
               !hinted->useSubpixelPen());
        ensure("linear face uses subpixel pen", linear->useSubpixelPen());

        // Per-glyph advance (codepoint path → mXAdvance): linear sources the
        // glyph's unhinted linearHoriAdvance, which differs from the grid-fit
        // hinted advance at this size.
        const F32 ah = hinted->getXAdvance((llwchar)'m');
        const F32 al = linear->getXAdvance((llwchar)'m');
        ensure("advances positive", ah > 0.f && al > 0.f);
        ensure("linear advance differs from hinted advance",
               std::fabs(al - ah) > 0.01f);

        // Global vertical metrics: the linear (unrounded) ascender / line
        // height differ from FreeType's grid-fit size metrics, while staying
        // within a pixel of them (sanity that it's the same face + size, not a
        // different rasterization).
        const F32 asc_h = hinted->getAscenderHeight();
        const F32 asc_l = linear->getAscenderHeight();
        const F32 lh_h  = hinted->getLineHeight();
        const F32 lh_l  = linear->getLineHeight();
        ensure("ascenders agree within a pixel",   std::fabs(asc_h - asc_l) <= 1.5f);
        ensure("line heights agree within a pixel", std::fabs(lh_h - lh_l) <= 1.5f);
        ensure("a linear vertical metric is unrounded (differs from grid-fit)",
               std::fabs(asc_h - asc_l) > 1e-4f || std::fabs(lh_h - lh_l) > 1e-4f);
    }

    // Scalable (COLRv1) color faces get the subpixel pen in the GPU regime:
    // they render through the analytic paint encoder (resolution-independent
    // vectors at the exact pen), so fractional placement is correct and keeps
    // emoji aligned to the surrounding text's sub-pixel pen. With GPU off they
    // rasterize to the atlas, where a subpixel pen would blur, so the pen stays
    // on the integer grid. sbix/CBDT/SVG color faces stay excluded either way.
    template<> template<>
    void llfontgl_object::test<20>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("COLRv1 test font not found: " + path);

        if (fileExists(kFontsXml))
            LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                                LLSD(), /*create_gl_textures=*/true);
        if (!gFontManagerp)
            skip("font manager not initialized (fonts.xml missing?)");

        const bool saved_gpu = LLFontGL::sEnableFontGpu;
        auto load_head = [&](bool gpu) -> LLPointer<LLFontFreetype>
        {
            LLFontGL::sEnableFontGpu = gpu;
            LLPointer<LLFontFreetype> head = new LLFontFreetype();
            // is_fallback=true skips the head's notdef pre-warm — we only read
            // the load-time subpixel-pen flag, no rasterization needed.
            if (!head->loadFace(path, 12.f, 96.f, 96.f, /*is_fallback=*/true,
                                /*face_n=*/0, EFontHinting::DEFAULT, 0, {}))
                head = nullptr;
            return head;
        };

        LLPointer<LLFontFreetype> off = load_head(false);
        LLPointer<LLFontFreetype> on  = load_head(true);
        LLFontGL::sEnableFontGpu = saved_gpu;

        ensure("COLRv1 face loaded (gpu off)", off.notNull());
        ensure("COLRv1 face loaded (gpu on)",  on.notNull());
        // Sanity: this really is a scalable (COLRv1) color face.
        ensure("face is COLRv1",
               on->getFontFace() && on->getFontFace()->hasColrV1());
        ensure("COLRv1 atlas regime keeps integer pen", !off->useSubpixelPen());
        ensure("COLRv1 GPU regime uses subpixel pen",    on->useSubpixelPen());
    }

    // Static getter family — every named getter must resolve to a
    // non-null LLFontGL after a successful initClass. Smoke check
    // that the default fonts.xml covers all the families exposed.
    template<> template<>
    void llfontgl_object::test<10>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        ensure("SansSerif",            LLFontGL::getFontSansSerif()           != nullptr);
        ensure("SansSerifSmall",       LLFontGL::getFontSansSerifSmall()      != nullptr);
        ensure("SansSerifSmallBold",   LLFontGL::getFontSansSerifSmallBold()  != nullptr);
        ensure("SansSerifBig",         LLFontGL::getFontSansSerifBig()        != nullptr);
        ensure("SansSerifHuge",        LLFontGL::getFontSansSerifHuge()       != nullptr);
        ensure("SansSerifBold",        LLFontGL::getFontSansSerifBold()       != nullptr);
        ensure("Monospace",            LLFontGL::getFontMonospace()           != nullptr);
        ensure("EmojiSmall",           LLFontGL::getFontEmojiSmall()          != nullptr);
        ensure("EmojiMedium",          LLFontGL::getFontEmojiMedium()         != nullptr);
        ensure("EmojiLarge",           LLFontGL::getFontEmojiLarge()          != nullptr);
        ensure("FontDefault",          LLFontGL::getFontDefault()             != nullptr);
    }

    // Style string parser round-trip: every output of getStringFromStyle
    // must round-trip through getStyleFromString. The legacy parser used
    // substring search, so a poisoned token like "FAUX-BOLD" set BOLD;
    // the new tokenizer-based parser splits on '|' and matches whole
    // tokens. NORMAL has its own dedicated string ("NORMAL", no leading
    // pipe), so the round-trip pins both edge cases at once.
    template<> template<>
    void llfontgl_object::test<17>()
    {
        const U8 cases[] = {
            LLFontGL::NORMAL,
            LLFontGL::BOLD,
            LLFontGL::ITALIC,
            LLFontGL::UNDERLINE,
            LLFontGL::BOLD | LLFontGL::ITALIC,
            LLFontGL::BOLD | LLFontGL::UNDERLINE,
            LLFontGL::ITALIC | LLFontGL::UNDERLINE,
            LLFontGL::BOLD | LLFontGL::ITALIC | LLFontGL::UNDERLINE,
        };
        for (U8 style : cases)
        {
            const std::string s = LLFontGL::getStringFromStyle(style);
            ensure("string output is non-empty", !s.empty());
            ensure("no leading pipe", s.front() != '|');
            ensure("no trailing pipe", s.back() != '|');
            ensure_equals("round-trip preserves style bits",
                          LLFontGL::getStyleFromString(s), style);
        }

        // Substring-match regression guard: legacy parser matched
        // "BOLD" inside "FAUX-BOLD"; new parser tokenizes on '|'.
        ensure_equals("FAUX-BOLD does not set BOLD",
                      LLFontGL::getStyleFromString("FAUX-BOLD"), 0);
        ensure_equals("NOTBOLD does not set BOLD",
                      LLFontGL::getStyleFromString("NOTBOLD"), 0);
        ensure_equals("empty string is NORMAL",
                      LLFontGL::getStyleFromString(""),
                      LLFontGL::NORMAL);
        ensure_equals("BOLD|ITALIC tokenizes correctly",
                      LLFontGL::getStyleFromString("BOLD|ITALIC"),
                      LLFontGL::BOLD | LLFontGL::ITALIC);
    }

    // ===================================================================
    // Render-output group: fixture brings up gUIProgram (needs_render=true)
    // so LLFontGL::render() completes end-to-end against the OSMesa
    // framebuffer; tests verify pixel-level invariants via glReadPixels.
    // Shared static HeadlessGL — per-test recreation poisons LLFontGL's
    // static fontp cache after a few cycles (see vertexbuffer fixture).
    // ===================================================================

    inline void ensureRenderLLFontGL()
    {
        static bool initialized = false;
        if (initialized || !fileExists(kFontsXml))
            return;
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL::loadDefaultFonts();
        initialized = true;
    }

    struct llfontgl_render_data
    {
        ll_test::HeadlessGL& gl = getSharedFontGL();
        llfontgl_render_data() { ensureRenderLLFontGL(); }
        ~llfontgl_render_data() = default;
    };

    typedef test_group<llfontgl_render_data> llfontgl_render_test;
    typedef llfontgl_render_test::object     llfontgl_render_object;
    tut::llfontgl_render_test llfontgl_render_testcase("LLFontGLRender");

    // Helper: scan a region of a readback buffer and return true iff
    // any pixel has non-zero alpha. Used to detect "did the glyph
    // actually rasterize at this position?".
    inline bool hasAnyAlpha(const std::vector<U8>& px, S32 fb_w, S32 fb_h,
                            S32 x0, S32 y0, S32 x1, S32 y1)
    {
        x0 = llmax(0, x0); y0 = llmax(0, y0);
        x1 = llmin(fb_w, x1); y1 = llmin(fb_h, y1);
        for (S32 y = y0; y < y1; ++y)
        {
            for (S32 x = x0; x < x1; ++x)
            {
                const U8 a = px[(y * fb_w + x) * 4 + 3];
                if (a > 0) return true;
            }
        }
        return false;
    }

    // First end-to-end render() call with LEFT/BASELINE places glyph
    // pixels into the framebuffer at the requested x. Pins the full
    // chain: shape → atlas → vertex buffer → drawArrays → fragment
    // output → glReadPixels.
    template<> template<>
    void llfontgl_render_object::test<1>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        gl.clearFramebuffer();
        LLWString s = utf8str_to_wstring("A");
        const S32 n = font->render(s, 0, /*x=*/64.f, /*y=*/64.f,
                                   LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                   1);
        // Force any pending verts to draw.
        gGL.flush();
        ensure_equals("render returned 1 char", n, 1);

        // Read back the framebuffer; expect non-zero alpha somewhere
        // in the rendered glyph's bounding box (rough ±32px around 64).
        auto px = ll_test::readFramebufferRGBA(ll_test::HeadlessGL::WIDTH,
                                               ll_test::HeadlessGL::HEIGHT);
        ensure("rendered glyph appears in framebuffer",
               hasAnyAlpha(px, ll_test::HeadlessGL::WIDTH,
                           ll_test::HeadlessGL::HEIGHT,
                           48, 32, 96, 96));
    }

    // HCENTER and RIGHT alignment branches: render() must complete
    // through both branches without crashing and return the same
    // character count as the LEFT branch. Pixel-level position
    // verification is brittle in this test setup (stub shader / state
    // accumulation); this pins crash-free traversal of all three
    // halign branches at llfontgl.cpp:354-372.
    template<> template<>
    void llfontgl_render_object::test<2>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        LLWString s = utf8str_to_wstring("ABC");

        const S32 n_left = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        const S32 n_cen  = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::HCENTER, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        const S32 n_right = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                         LLFontGL::RIGHT, LLFontGL::BASELINE,
                                         LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        gGL.flush();
        ensure_equals("LEFT render returns 3 chars",     n_left,  3);
        ensure_equals("HCENTER render returns 3 chars",  n_cen,   3);
        ensure_equals("RIGHT render returns 3 chars",    n_right, 3);
    }

    // VAlign branches: render() must complete through TOP, VCENTER,
    // BASELINE, and BOTTOM without crashing; return value matches
    // requested char count for each. Pins the valign switch at
    // llfontvertexbuffer.cpp:123-138.
    template<> template<>
    void llfontgl_render_object::test<3>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        LLWString s = utf8str_to_wstring("Hi");

        const S32 n_top = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::TOP,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_bs  = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_btm = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BOTTOM,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        gGL.flush();
        ensure_equals("TOP render returns 2 chars",       n_top, 2);
        ensure_equals("BASELINE render returns 2 chars",  n_bs,  2);
        ensure_equals("BOTTOM render returns 2 chars",    n_btm, 2);
    }

    // Style flip across renders: render with NORMAL style, then BOLD.
    // Both must succeed. Pins style propagation through render() —
    // 00f3a4e93d's regression invalidation gate.
    template<> template<>
    void llfontgl_render_object::test<4>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        LLWString s = utf8str_to_wstring("Hello");

        const S32 n_norm = font->render(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 n_bold = font->render(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::BOLD, LLFontGL::NO_SHADOW, 5);
        const S32 n_und  = font->render(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::UNDERLINE, LLFontGL::NO_SHADOW, 5);
        gGL.flush();
        ensure_equals("NORMAL render returns 5 chars",    n_norm, 5);
        ensure_equals("BOLD render returns 5 chars",      n_bold, 5);
        ensure_equals("UNDERLINE render returns 5 chars", n_und,  5);
    }

    // Shadow rendering paths: render with NO_SHADOW, DROP_SHADOW, and
    // DROP_SHADOW_SOFT. All three branches must complete without
    // crashing. The two-pass shadow capture (genBuffers' beginList
    // dance at llfontvertexbuffer.cpp:281-290) needs sCurBoundShader
    // to be gUIProgram, which the fixture ensures. Pins shadow
    // rendering doesn't crash; visible-shadow geometry verification is
    // out of scope for this fixture (no production shadow shader).
    template<> template<>
    void llfontgl_render_object::test<5>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        LLWString s = utf8str_to_wstring("Hi");

        const S32 n_no = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                      LLFontGL::LEFT, LLFontGL::BASELINE,
                                      LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_drop = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::DROP_SHADOW, 2);
        const S32 n_soft = font->render(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::DROP_SHADOW_SOFT, 2);
        gGL.flush();
        ensure_equals("NO_SHADOW returns 2",        n_no,   2);
        ensure_equals("DROP_SHADOW returns 2",      n_drop, 2);
        ensure_equals("DROP_SHADOW_SOFT returns 2", n_soft, 2);
    }

    // Mid-render glyph rasterization must not misdirect the pending
    // batch's texture. Rasterizing a cache-missed glyph uploads into its
    // atlas via LLImageGL::setSubImage, which binds the upload target on
    // unit 0 and leaves it bound; if the quads queued before it then
    // flush under that binding, their UVs sample a different atlas page
    // and the run renders fragments of other glyphs ("text shows other
    // text"). The legacy per-codepoint `last_char != wch` flush kept the
    // queue to ~1 glyph and hid this; with real batching every flush must
    // re-assert the batch's own texture (flush_batch in LLFontGL::render).
    //
    // Captured display lists record the texture bound at flush time
    // (LLVertexBufferData::mTexName), which makes the misdirection
    // directly observable: render a Latin prefix plus a never-yet-
    // rasterized emoji and require the prefix batch to carry the
    // grayscale atlas texture its UVs were built against.
    template<> template<>
    void llfontgl_render_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);
        const LLFontFreetype* head = font->getFontFreetype();
        ensure("head freetype resolves", head != nullptr);

        // U+1F995 SAUROPOD — obscure enough that no other test in this
        // binary rasterizes it. The mid-render upload only fires on a
        // cache miss, so the emoji must be cold going in.
        const llwchar emoji_cp = 0x1F995;
        U32 emoji_idx = 0;
        const LLFontFreetype* emoji_face = head->selectShapingFace(emoji_cp, emoji_idx);
        if (!emoji_face || emoji_face == head || emoji_idx == 0)
            skip("no emoji fallback covers U+1F995 in this harness");
        ensure("emoji fallback has a face wrapper",
               emoji_face->getFontFace() != nullptr);
        if (emoji_face->getFontFace()->findGlyphInfo(emoji_idx, EFontGlyphType::Color))
            skip("U+1F995 already rasterized; mid-render upload won't fire");

        LLWString s = utf8str_to_wstring("stomp \xF0\x9F\xA6\x95 check");

        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        const S32 n = font->render(s, 0, 50.f, 100.f, LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                   (S32)s.size());
        gGL.endList();
        ensure_equals("render consumed the whole string", n, (S32)s.size());

        // Expected textures, resolved through the glyph entries the render
        // itself produced. use_color defaulted true, so the render looked
        // glyphs up as Color; querying the same way returns the same
        // entries (and therefore the same atlas pages) it drew from.
        const LLFontGlyphInfo* latin_gi = head->getGlyphInfo((llwchar)'s', EFontGlyphType::Color);
        ensure("latin glyph cached", latin_gi && latin_gi->mSourceFace);
        const auto& latin_entry = latin_gi->mPhaseSlots[0].mBitmapEntry;
        LLImageGL* latin_img = latin_gi->mSourceFace->getBitmapCache()->getImageGL(
            latin_entry.first, (U32)latin_entry.second);
        ensure("latin atlas page live", latin_img != nullptr);
        const U32 latin_tex = latin_img->getTexName();

        const LLFontGlyphInfo* emoji_gi = head->getGlyphInfo(emoji_cp, EFontGlyphType::Color);
        ensure("emoji glyph cached", emoji_gi && emoji_gi->mSourceFace);
        const auto& emoji_entry = emoji_gi->mPhaseSlots[0].mBitmapEntry;
        LLImageGL* emoji_img = emoji_gi->mSourceFace->getBitmapCache()->getImageGL(
            emoji_entry.first, (U32)emoji_entry.second);
        ensure("emoji atlas page live", emoji_img != nullptr);
        const U32 emoji_tex = emoji_img->getTexName();

        ensure("string spans two distinct atlas textures", latin_tex != emoji_tex);
        ensure("capture produced batches", !capture.empty());

        // The first captured batch is the Latin prefix, flushed when the
        // glyph stream switches to the emoji's atlas — exactly the batch
        // the mid-render upload used to misdirect.
        ensure_equals("prefix batch carries its own atlas texture",
                      capture.front().mTexName, latin_tex);
        // And every batch in the capture must reference one of the two
        // atlas pages this string actually draws from.
        for (const LLVertexBufferData& entry : capture)
        {
            ensure("batch texture is one of the string's atlas pages",
                   entry.mTexName == latin_tex || entry.mTexName == emoji_tex);
        }
    }

    // Analytic (hb-gpu) render path: with sEnableFontGpu on, render() routes an
    // eligible run (SansSerif, normal style, no shadow) through the hb-gpu
    // branch instead of the atlas. Verify it draws glyph ink in the same region
    // as the atlas path and reports the same character count. Pins the whole
    // wiring: shape -> per-face GPU cache -> LLFontGpuBatch -> drawArrays ->
    // analytic raster -> glReadPixels.
    template<> template<>
    void llfontgl_render_object::test<7>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        const LLWString s = utf8str_to_wstring("A");

        // Count pixels with meaningful luminance (white text on the black
        // clear) inside a rough bbox around the glyph.
        auto count_ink = [](const std::vector<U8>& px, S32 w, S32 h) -> S32
        {
            S32 c = 0;
            for (S32 y = 32; y < 100 && y < h; ++y)
                for (S32 x = 40; x < 100 && x < w; ++x)
                {
                    const U8* p = &px[((std::size_t)y * w + x) * 4];
                    if (((S32)p[0] + p[1] + p[2]) / 3 > 40) ++c;
                }
            return c;
        };

        // Atlas baseline.
        LLFontGL::enableFontGpu(false);
        gl.clearFramebuffer();
        const S32 n_atlas = font->render(s, 0, 64.f, 64.f, LLColor4::white,
                                         LLFontGL::LEFT, LLFontGL::BASELINE,
                                         LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 1);
        gGL.flush();
        const S32 ink_atlas = count_ink(ll_test::readFramebufferRGBA(W, H), W, H);

        // hb-gpu path.
        LLFontGL::enableFontGpu(true);
        gl.clearFramebuffer();
        const S32 n_gpu = font->render(s, 0, 64.f, 64.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 1);
        gGL.flush();
        const S32 ink_gpu = count_ink(ll_test::readFramebufferRGBA(W, H), W, H);
        LLFontGL::enableFontGpu(false);

        ensure("atlas path drew ink", ink_atlas > 0);
        ensure("hb-gpu path drew ink", ink_gpu > 0);
        ensure_equals("same character count", n_gpu, n_atlas);
#else
        skip("hb-gpu not available");
#endif
    }

    // hb-gpu bold + drop-shadow passes. Bold double-strikes (offset +1px) so it
    // must produce at least as much ink as the normal render; shadow is a smoke
    // test (the black shadow over the black clear isn't visible in luminance,
    // but the path must render the glyph and report the right char count).
    template<> template<>
    void llfontgl_render_object::test<8>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        const LLWString s = utf8str_to_wstring("E");

        auto ink = [&](U8 style, LLFontGL::ShadowType shadow) -> std::pair<S32,S32>
        {
            gl.clearFramebuffer();
            const S32 n = font->render(s, 0, 64.f, 64.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       style, shadow, 1);
            gGL.flush();
            auto px = ll_test::readFramebufferRGBA(W, H);
            S32 c = 0;
            for (S32 y = 32; y < 100 && y < H; ++y)
                for (S32 x = 40; x < 110 && x < W; ++x)
                {
                    const U8* p = &px[((std::size_t)y * W + x) * 4];
                    if (((S32)p[0] + p[1] + p[2]) / 3 > 40) ++c;
                }
            return { n, c };
        };

        LLFontGL::enableFontGpu(true);
        const auto normal = ink(LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        const auto bold   = ink(LLFontGL::BOLD,   LLFontGL::NO_SHADOW);
        const auto italic = ink(LLFontGL::ITALIC, LLFontGL::NO_SHADOW);
        const auto shadow = ink(LLFontGL::NORMAL, LLFontGL::DROP_SHADOW);
        LLFontGL::enableFontGpu(false);

        ensure_equals("normal char count", normal.first, 1);
        ensure("normal drew ink", normal.second > 0);
        ensure_equals("bold char count", bold.first, 1);
        ensure("bold is at least as heavy as normal", bold.second >= normal.second);
        ensure_equals("italic char count", italic.first, 1);
        ensure("italic drew ink", italic.second > 0);
        ensure_equals("shadow char count", shadow.first, 1);
        ensure("shadowed glyph still drew foreground ink", shadow.second > 0);
#else
        skip("hb-gpu not available");
#endif
    }

    // Monospace coverage: fixed-width faces are shaped end-to-end like any
    // other (build_shape_layout makes one range; shape_sub_run disables
    // kern/liga for column alignment), so the GPU shaped path covers them with
    // no codepoint detour. The GPU path never touches the bitmap atlas, so this
    // renders mono purely through hb-gpu (no warm-up needed) and checks for ink
    // and the right char count.
    template<> template<>
    void llfontgl_render_object::test<9>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* mono = LLFontGL::getFontMonospace();
        if (!mono)
            skip("monospace font unavailable in this harness");

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        const LLWString s = utf8str_to_wstring("Ab");

        LLFontGL::enableFontGpu(true);
        gl.clearFramebuffer();
        const S32 n = mono->render(s, 0, 40.f, 64.f, LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                   (S32)s.size());
        gGL.flush();
        auto px = ll_test::readFramebufferRGBA(W, H);
        LLFontGL::enableFontGpu(false);

        S32 ink = 0;
        for (S32 y = 32; y < 100 && y < H; ++y)
            for (S32 x = 20; x < 130 && x < W; ++x)
            {
                const U8* p = &px[((std::size_t)y * W + x) * 4];
                if (((S32)p[0] + p[1] + p[2]) / 3 > 40) ++ink;
            }

        ensure_equals("mono gpu rendered both chars", n, (S32)s.size());
        ensure("mono gpu path drew ink", ink > 0);
#else
        skip("hb-gpu not available");
#endif
    }

    // Color (COLRv1) glyphs via the hb-gpu paint path. SansSerif's chain pulls
    // in the Emoji family (Noto-COLRv1), so an emoji codepoint routes to a
    // COLRv1 face -> the paint encoder + paint program + premultiplied blend.
    // Render purely through hb-gpu (the paint path never touches the atlas) and
    // verify it drew SATURATED ink: white text is R==G==B, so a colored pixel
    // proves the paint interpreter ran rather than the monochrome draw path.
    template<> template<>
    void llfontgl_render_object::test<10>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        // U+1F600 GRINNING FACE: emoji-presentation only (no text variant), so
        // it routes to the color emoji face rather than a monochrome fallback.
        const llwchar emoji_arr[] = { 0x1F600, 0 };
        const LLWString s(emoji_arr, 1);

        LLFontGL::enableFontGpu(true);
        gl.clearFramebuffer();
        const S32 n = font->render(s, 0, 64.f, 64.f, LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 1);
        gGL.flush();
        auto px = ll_test::readFramebufferRGBA(W, H);
        LLFontGL::enableFontGpu(false);

        // Count opaque pixels and, among them, ones with channel spread (the
        // hallmark of a painted color glyph vs. white coverage).
        S32 ink = 0, colored = 0;
        for (S32 y = 20; y < 110 && y < H; ++y)
            for (S32 x = 40; x < 110 && x < W; ++x)
            {
                const U8* p = &px[((std::size_t)y * W + x) * 4];
                if (p[3] == 0) continue;
                ++ink;
                const S32 hi = llmax(p[0], llmax(p[1], p[2]));
                const S32 lo = llmin(p[0], llmin(p[1], p[2]));
                if (hi - lo > 30) ++colored;
            }

        // If the emoji didn't route to a color face in this harness, nothing
        // colored renders — skip rather than fail, so the suite stays robust to
        // font-coverage differences.
        if (ink == 0)
            skip("emoji did not route to a renderable face in this harness");
        ensure_equals("emoji rendered one char", n, 1);
        ensure("paint path produced saturated color", colored > 0);
#else
        skip("hb-gpu not available");
#endif
    }

    // use_color=false is the per-draw "monochrome emoji" choice — it must NOT
    // kick the run to the atlas. With GPU on, a COLR face drawn use_color=false
    // routes through the draw (silhouette) path tinted with text color, so any
    // ink it produces is monochrome (R==G==B), never the saturated paint output.
    // use_color=true on the same glyph is saturated — the two diverge, proving
    // the want_color branch picks draw vs. paint correctly.
    template<> template<>
    void llfontgl_render_object::test<11>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        const llwchar emoji_arr[] = { 0x1F600, 0 };
        const LLWString s(emoji_arr, 1);

        auto render_count = [&](bool use_color) -> std::pair<S32,S32>
        {
            gl.clearFramebuffer();
            font->render(s, 0, 64.f, 64.f, LLColor4::white,
                         LLFontGL::LEFT, LLFontGL::BASELINE,
                         LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 1,
                         S32_MAX, nullptr, /*use_ellipses=*/false, use_color);
            gGL.flush();
            auto px = ll_test::readFramebufferRGBA(W, H);
            S32 ink = 0, colored = 0;
            for (S32 y = 20; y < 110 && y < H; ++y)
                for (S32 x = 40; x < 110 && x < W; ++x)
                {
                    const U8* p = &px[((std::size_t)y * W + x) * 4];
                    if (p[3] == 0) continue;
                    ++ink;
                    const S32 hi = llmax(p[0], llmax(p[1], p[2]));
                    const S32 lo = llmin(p[0], llmin(p[1], p[2]));
                    if (hi - lo > 30) ++colored;
                }
            return { ink, colored };
        };

        LLFontGL::enableFontGpu(true);
        const auto col  = render_count(/*use_color=*/true);
        const auto mono = render_count(/*use_color=*/false);
        LLFontGL::enableFontGpu(false);

        if (col.first == 0)
            skip("emoji did not route to a renderable face in this harness");
        ensure("color draw produced saturation", col.second > 0);
        // The COLR base outline may be empty (all visual is in the paint
        // layers); if so the silhouette is blank, which is fine. But whatever
        // monochrome ink appears must carry no saturation.
        ensure("monochrome draw has no saturated color", mono.second == 0);
#else
        skip("hb-gpu not available");
#endif
    }

    // Ellipsis on the GPU path: an overflowing run truncates (budget reserved
    // the "...." width) and the dots render recursively through the GPU branch.
    // Verify it truncates, draws ink, and the dots extend ink to the RIGHT of
    // the same truncated prefix drawn without ellipsis — proving the "..." was
    // actually emitted, not silently dropped.
    template<> template<>
    void llfontgl_render_object::test<12>()
    {
#if LL_HAS_HB_GPU
        if (!fileExists(kFontsXml))
            skip("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font resolves", font != nullptr);

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;
        const LLWString s = utf8str_to_wstring("WWWWWWWWWWWWWWWW");

        // Rightmost framebuffer column carrying glyph ink in the text band.
        auto rightmost_ink = [&]() -> S32
        {
            auto px = ll_test::readFramebufferRGBA(W, H);
            S32 rm = -1;
            for (S32 y = 32; y < 100 && y < H; ++y)
                for (S32 x = 0; x < W; ++x)
                {
                    const U8* p = &px[((std::size_t)y * W + x) * 4];
                    if (((S32)p[0] + p[1] + p[2]) / 3 > 40) rm = llmax(rm, x);
                }
            return rm;
        };

        const F32 full_w = font->getWidthF32(s.c_str());
        const S32 budget = (S32)(full_w * 0.5f);   // ~half -> forces truncation

        LLFontGL::enableFontGpu(true);

        // Ellipsis render: truncated prefix + "...".
        gl.clearFramebuffer();
        const S32 n_ell = font->render(s, 0, 20.f, 64.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                       S32_MAX, budget, nullptr,
                                       /*use_ellipses=*/true, /*use_color=*/true);
        gGL.flush();
        const S32 rm_ell = rightmost_ink();

        // The same visible prefix, no ellipsis, same origin.
        gl.clearFramebuffer();
        const S32 n_pre = font->render(s, 0, 20.f, 64.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                       n_ell, S32_MAX, nullptr,
                                       /*use_ellipses=*/false, /*use_color=*/true);
        gGL.flush();
        const S32 rm_pre = rightmost_ink();

        LLFontGL::enableFontGpu(false);

        ensure("ellipsis truncated the run", n_ell > 0 && n_ell < (S32)s.size());
        ensure_equals("prefix render drew the same char count", n_pre, n_ell);
        ensure("ellipsis render drew ink", rm_ell >= 0);
        ensure("ellipsis dots extend ink past the truncated prefix", rm_ell > rm_pre);
#else
        skip("hb-gpu not available");
#endif
    }
}
