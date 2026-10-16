/**
 * @file llfontgl_test.cpp
 * @brief Smoke test for the headless GL harness.
 *
 * Brings up a real GL context on LLWindowManager's hidden window, runs
 * LLFontGL::initClass through the explicit-fonts.xml-path overload,
 * and verifies the GL-backed atlas pipeline produces a live texture.
 *
 * The context is the binary's shared one (see llheadlessgl_fixture.h),
 * and so is LLFontGL's registry: the first case to call initClass makes
 * it, and it stays for every case after, llfonttextcache's too, since
 * LLFontGL's getters cache the fonts it holds. A case leaves the fonts,
 * their DPI and their scale as it found them.
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

#include "../llfontgl.h"
#include "../llfontfreetype.h"
#include "../llfontbitmapcache.h"
#include "../llimagegl.h"
#include "../alfontshaping.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>

#include <set>

namespace
{
#ifndef LLFONT_TEST_APP_DIR
#  define LLFONT_TEST_APP_DIR ""
#endif
#ifndef LLFONT_TEST_FONTS_XML
#  define LLFONT_TEST_FONTS_XML ""
#endif

    constexpr const char* kAppDir   = LLFONT_TEST_APP_DIR;
    constexpr const char* kFontsXml = LLFONT_TEST_FONTS_XML;

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

namespace
{
    // The shared GL context. Catch2 makes a fixture per test case, but
    // per-fixture HeadlessGL recreation poisons LLFontGL's per-getter
    // static fontp caches after ~10 cycles (the per-test new GL
    // context invalidates LLImageGL textures the fontps reference).
    // Sharing the GL context for the binary's lifetime keeps font
    // state coherent across all tests.
    //
    // It binds gUIProgram, so the render group can drive gGL.beginList /
    // flush against a real shader. Layout-only tests (which
    // never issue a draw) still work fine — the bound shader is harmless
    // when no draw call is in flight.
    struct llfontgl_data
    {
        ll_test::HeadlessGL& gl = ll_test::sharedHeadlessGL();
        llfontgl_data() = default;
        ~llfontgl_data() = default;
    };


    // initClass through the explicit-path overload should bring up a
    // working font registry under a real GL context. loadDefaultFonts
    // (called from initClass) returns true only if every required
    // family resolved through to a usable LLFontFreetype.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: initClass from an explicit fonts.xml brings up a working registry", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
        {
            SKIP("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        LLFontGL::initClass(/*screen_dpi=*/96.f,
                            /*x_scale=*/1.f, /*y_scale=*/1.f,
                            /*app_dir=*/kAppDir,
                            /*fonts_xml_path=*/kFontsXml,
                            /*font_overrides=*/LLSD(),
                            /*create_gl_textures=*/true);

        CHECK(LLFontGL::loadDefaultFonts());
        CHECK(LLFontGL::getFontSansSerif() != nullptr);
    }

    // After ASCII rasterization, the SansSerif font's bitmap cache
    // should have at least one greyscale page with a live GL texture.
    // Catches the silent-no-op failure mode where the GL context is
    // healthy but createGLTexture skipped the upload.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: ASCII glyphs land on a grayscale atlas page with a live GL texture", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
        {
            SKIP("fonts.xml not found at " + std::string(kFontsXml));
            return;
        }

        // initClass is single-shot; if test<1> ran first the registry is
        // already up. The call below either initializes (test<2> ran
        // standalone) or warns + bails (test<1> ran first). Either way
        // the assertions below operate against a working registry.
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);

        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // Force the ASCII range to rasterize so the atlas isn't empty.
        // (createFont's pre-warm in getFont already does this on first
        // resolve; calling again is a fast no-op via head cache.)
        font->generateASCIIglyphs();

        const LLFontFreetype* ft = font->getFontFreetype();
        REQUIRE(ft != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        REQUIRE(page != nullptr);
        CHECK(page->getTexName() != 0);
    }

    // getFontMonospace resolves and reports fixed-width through its
    // underlying freetype. Pins the registry's monospace family
    // routing — a regression that resolved Monospace to a
    // proportional fallback would surface here.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getFontMonospace resolves to a fixed-width face", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* mono = LLFontGL::getFontMonospace();
        REQUIRE(mono != nullptr);
        const LLFontFreetype* ft = mono->getFontFreetype();
        REQUIRE(ft != nullptr);
        INFO("Monospace resolved to " << ft->getName());
        CHECK(ft->isFixedWidth());
    }

    // getFontSansSerifBig resolves with positive line-height.
    // Captures the registry's size-suffix routing — Big size must
    // produce a freetype whose metrics scale up from the base.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getFontSansSerifBig has a taller line than getFontSansSerifSmall", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* small = LLFontGL::getFontSansSerifSmall();
        LLFontGL* big   = LLFontGL::getFontSansSerifBig();
        REQUIRE(small != nullptr);
        REQUIRE(big   != nullptr);
        const LLFontFreetype* fs = small->getFontFreetype();
        const LLFontFreetype* fb = big->getFontFreetype();
        REQUIRE(fs != nullptr);
        REQUIRE(fb != nullptr);
        CHECK(fs->getLineHeight() > 0.f);
        CHECK(fb->getLineHeight() > 0.f);
        CHECK(fb->getLineHeight() > fs->getLineHeight());
    }

    // Repeat-call getter caches: getFontSansSerif() returns the same
    // pointer on second call. Pins the per-getter static cache —
    // a regression that re-resolved on every call would change the
    // pointer (and waste a registry lookup per UI label render).
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getFontSansSerif returns the same font on every call", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* a = LLFontGL::getFontSansSerif();
        LLFontGL* b = LLFontGL::getFontSansSerif();
        REQUIRE(a != nullptr);
        CHECK(a == b);
    }

    // generateASCIIglyphs is idempotent — repeat calls don't allocate
    // additional atlas sheets. Pins the head-cache lookup that makes
    // the second call a fast no-op.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a second generateASCIIglyphs adds no atlas sheets", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        font->generateASCIIglyphs();
        const LLFontFreetype* ft    = font->getFontFreetype();
        REQUIRE(ft != nullptr);
        const LLFontBitmapCache* bc = ft->getFontBitmapCache();
        REQUIRE(bc != nullptr);
        const U32 sheets_after_first = bc->getNumBitmaps(EFontGlyphType::Grayscale);
        CHECK(sheets_after_first >= 1u);

        font->generateASCIIglyphs();
        const U32 sheets_after_second = bc->getNumBitmaps(EFontGlyphType::Grayscale);
        CHECK(sheets_after_second == sheets_after_first);
    }

    // getFontByName returns the same instance for the same legacy
    // name string. Pins the legacy-name routing in the registry.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getFontByName returns the same instance for the same legacy name", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* a = LLFontGL::getFontByName("SANSSERIF");
        LLFontGL* b = LLFontGL::getFontByName("SANSSERIF");
        REQUIRE(a != nullptr);
        CHECK(a == b);
    }

    // collectGarbage on the font face does not crash and (since
    // we just rendered the ASCII set) is a fast no-op — sheets
    // are recently used. Pins 169390b593 (cache-sweep moved off
    // hot path) and 7c05b2de82 (collectGarbage safe at any frame
    // boundary).
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: collectGarbage leaves glyphs in use and their atlas page alone", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const LLFontFreetype* ft = font->getFontFreetype();
        REQUIRE(ft != nullptr);

        // What a sweep must not do is disturb glyphs still in use. Measure
        // first, sweep twice -- the second is inside the throttle interval and
        // should do nothing at all -- and measure again: same width, and the
        // atlas page the glyphs came from still carries a live texture. The
        // assertion this replaces was the literal `true`, which held whether
        // the sweep ran, was throttled, or evicted the whole atlas.
        const std::string sample = "The quick brown fox";
        font->generateASCIIglyphs();
        const F32 before = font->getWidthF32Bytes(sample, 0, (S32)sample.size(), false);
        UNSCOPED_INFO("sample has a width to begin with");
        CHECK(before > 0.f);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        const U32 pages_before = cache->getNumBitmaps(EFontGlyphType::Grayscale);

        ft->collectGarbage();
        ft->collectGarbage();

        const F32 after = font->getWidthF32Bytes(sample, 0, (S32)sample.size(), false);
        UNSCOPED_INFO("a sweep does not change what live text measures");
        CHECK(before == after);
        UNSCOPED_INFO("a sweep does not drop a page holding live glyphs");
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) == pages_before);
        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        UNSCOPED_INFO("page 0 still present after a sweep");
        REQUIRE(page != nullptr);
        UNSCOPED_INFO("page 0 still has a live GL texture");
        CHECK(page->getTexName() != 0);
    }

    // Re-calling initClass after the registry is up is a no-op
    // (warns + bails). The registry pointer must remain stable so
    // LLFontGL's static fontp caches don't dangle.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a second initClass leaves the fonts already resolved in place", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* before = LLFontGL::getFontSansSerif();
        REQUIRE(before != nullptr);

        // Re-call initClass with different DPI. The single-shot
        // guard short-circuits, so the registry stays the same and
        // getter caches keep returning the same pointer.
        LLFontGL::initClass(72.f, 0.5f, 0.5f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* after = LLFontGL::getFontSansSerif();
        CHECK(after == before);
    }

    // A second initClass through the explicit-path overload is ignored
    // whole: the fonts already made keep the DPI and scale they were made
    // at, which every later layout and measurement reads.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a second initClass leaves the DPI and scale the fonts were made at", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        const F32 horiz_dpi = LLFontGL::sHorizDPI;
        const F32 vert_dpi  = LLFontGL::sVertDPI;
        const F32 scale_x   = LLFontGL::sScaleX;
        const F32 scale_y   = LLFontGL::sScaleY;

        LLFontGL::initClass(72.f, 0.5f, 0.5f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        CHECK(LLFontGL::sHorizDPI == horiz_dpi);
        CHECK(LLFontGL::sVertDPI == vert_dpi);
        CHECK(LLFontGL::sScaleX == scale_x);
        CHECK(LLFontGL::sScaleY == scale_y);
    }

    // Static getter family — every named getter must resolve to a
    // non-null LLFontGL after a successful initClass. Smoke check
    // that the default fonts.xml covers all the families exposed.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: every named font getter resolves after initClass", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        CHECK(LLFontGL::getFontSansSerif()           != nullptr);
        CHECK(LLFontGL::getFontSansSerifSmall()      != nullptr);
        CHECK(LLFontGL::getFontSansSerifSmallBold()  != nullptr);
        CHECK(LLFontGL::getFontSansSerifBig()        != nullptr);
        CHECK(LLFontGL::getFontSansSerifHuge()       != nullptr);
        CHECK(LLFontGL::getFontSansSerifBold()       != nullptr);
        CHECK(LLFontGL::getFontMonospace()           != nullptr);
        CHECK(LLFontGL::getFontEmojiSmall()          != nullptr);
        CHECK(LLFontGL::getFontEmojiMedium()         != nullptr);
        CHECK(LLFontGL::getFontEmojiLarge()          != nullptr);
        CHECK(LLFontGL::getFontDefault()             != nullptr);
    }

    // getWidthF32: the whole-string overload defaults to the text's length;
    // a getWidthF32Bytes call with an explicit budget matches it.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getWidthF32 of a whole string equals getWidthF32Bytes of all of it", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";
        const F32 wfull = font->getWidthF32(s);
        const F32 wexp  = font->getWidthF32Bytes(s, 0,
                                                 (S32)s.size(), false);
        UNSCOPED_INFO("getWidthF32 with default max == explicit max");
        CHECK(wfull == wexp);
        CHECK(wfull > 0.f);
    }

    // Width monotonicity: width(AAA) > width(AA) > width(A). Used by
    // render()'s halign math at llfontgl.cpp:354-372 — the alignment
    // calc relies on accumulated width strictly increasing with
    // character count for the same string.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a string grows wider with each character added", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const F32 w1 = font->getWidthF32(std::string("A"));
        const F32 w2 = font->getWidthF32(std::string("AA"));
        const F32 w3 = font->getWidthF32(std::string("AAA"));
        CHECK(w1 > 0.f);
        CHECK(w2 > w1);
        CHECK(w3 > w2);
    }

    // Ellipsis budget: getWidthF32("....") is positive — render() pre-
    // computes this for the truncation path (llfontgl.cpp:459 references
    // the dots constant). A regression where '.' rasterized to a 0-width
    // glyph would silently break ellipsis sizing.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getWidthF32 gives the ellipsis dots a positive width", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const F32 w = font->getWidthF32(std::string("...."));
        CHECK(w > 0.f);
    }

    // maxDrawableBytes boundary: zero pixel budget fits nothing,
    // F32_MAX fits all, mid-budget fits a partial prefix.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: maxDrawableBytes fits none or all or the prefix the budget holds", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        CHECK(font->maxDrawableBytes(s, 0.f, (S32)s.size()) ==
                      (S32)0);
        CHECK(font->maxDrawableBytes(s, F32_MAX,
                                             (S32)s.size()) ==
                      (S32)s.size());
        // Find a budget that should fit "Hel" but not "Hell".
        const F32 w_hel  = font->getWidthF32Bytes(s, 0, 3, false);
        const F32 w_hell = font->getWidthF32Bytes(s, 0, 4, false);
        // Budget halfway between produces partial fit.
        const F32 mid_budget = (w_hel + w_hell) * 0.5f;
        const S32 fit = font->maxDrawableBytes(s, mid_budget,
                                               (S32)s.size());
        UNSCOPED_INFO("a budget halfway between Hel and Hell fits Hel");
        CHECK(fit == 3);
    }

    // byteFromPixelOffset round-trip: for each prefix length i, the
    // pixel offset corresponding to the prefix's width must invert to
    // i. Width accumulation and pixel->offset lookup must agree.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: byteFromPixelOffset inverts getWidthF32Bytes at every character start", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";
        // ASCII, so one byte per character: prefix lengths 0..5.
        for (S32 i = 0; i <= (S32)s.size(); ++i)
        {
            const F32 w = font->getWidthF32Bytes(s, 0, i, false);
            const S32 cp = font->byteFromPixelOffset(s, 0,
                                                     w,
                                                     F32_MAX,
                                                     (S32)s.size(),
                                                     /*round=*/true);
            // Exactly, not within one. A tolerance of ±1 over ASCII is a whole
            // character wide -- it accepts precisely the mistake this is here
            // to catch -- and the target is a boundary, which rounding takes
            // to the boundary itself.
            UNSCOPED_INFO("round-trip lands on the offset it was measured from");
            REQUIRE(cp == i);
        }

        // The same round trip where a character is not a byte. The expected
        // offsets are the character starts, recorded rather than counted.
        const std::string m = "A\xE6\x97\xA5" "B";
        const S32 starts[] = { 0, 1, 4, 5 };
        for (S32 start : starts)
        {
            const F32 w = font->getWidthF32Bytes(m, 0, start, false);
            const S32 got = font->byteFromPixelOffset(m, 0, w, F32_MAX,
                                                      (S32)m.size(), /*round=*/true);
            UNSCOPED_INFO("round-trip through a multi-byte character");
            REQUIRE(got == start);
        }
    }

    // Mixed-script width consistency: width("a你") ≈ width("a") +
    // width("你"). Pins shape-range itemization (e671bde0d1) at the
    // width measurement boundary — a regression that shaped "a你"
    // against DejaVu only (replacing 你 with notdef) would diverge.
    // Skip if the default fallback chain doesn't cover U+4F60.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a mixed-script string is as wide as its parts", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string a_s  = "a";
        const std::string c_s  = utf8str_from_cp(0x4F60);
        const std::string ac_s = a_s + c_s;

        const F32 wa = font->getWidthF32(a_s);
        const F32 wc = font->getWidthF32(c_s);
        const F32 wac = font->getWidthF32(ac_s);
        if (wc == 0.f)
            SKIP("font's fallback chain doesn't cover U+4F60");
        // Tolerance: ~0.5 px accounts for sub-pixel positioning across
        // shape-range boundaries.
        CHECK(std::abs(wac - (wa + wc)) <= 0.5f);
    }

    // Style string parser round-trip: every output of getStringFromStyle
    // must round-trip through getStyleFromString. The legacy parser used
    // substring search, so a poisoned token like "FAUX-BOLD" set BOLD;
    // the new tokenizer-based parser splits on '|' and matches whole
    // tokens. NORMAL has its own dedicated string ("NORMAL", no leading
    // pipe), so the round-trip pins both edge cases at once.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: getStringFromStyle round-trips and the parser matches whole tokens", "[LLFontGL]")
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
            CAPTURE(s);
            REQUIRE_FALSE(s.empty());
            REQUIRE(s.front() != '|');
            REQUIRE(s.back() != '|');
            REQUIRE(LLFontGL::getStyleFromString(s) == style);
        }

        // Substring-match regression guard: legacy parser matched
        // "BOLD" inside "FAUX-BOLD"; new parser tokenizes on '|'.
        UNSCOPED_INFO("FAUX-BOLD does not set BOLD");
        CHECK(LLFontGL::getStyleFromString("FAUX-BOLD") == 0);
        UNSCOPED_INFO("NOTBOLD does not set BOLD");
        CHECK(LLFontGL::getStyleFromString("NOTBOLD") == 0);
        CHECK(LLFontGL::getStyleFromString("") ==
                      LLFontGL::NORMAL);
        CHECK(LLFontGL::getStyleFromString("BOLD|ITALIC") ==
                      (LLFontGL::BOLD | LLFontGL::ITALIC));
    }

    // byteFromPixelOffset must snap to cluster boundaries on a
    // multi-codepoint emoji during mouse-driven cursor placement.
    // The trans-flag ZWJ sequence (1F3F3 FE0F 200D 26A7 FE0F) shapes
    // to a single glyph; as target_x walks across that glyph's pixel
    // span, the returned cursor position must only ever be the
    // cluster's start or its end. Mid-cluster returns oscillate the
    // selection rect's right edge across the cluster's interior —
    // observed as drag-select highlight flicker on ZWJ flags / family
    // emoji where 🐶 (1 codepoint) is unaffected.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: byteFromPixelOffset never lands inside a ZWJ emoji cluster", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // "X" + trans flag (5 codepoints) + "Y". The cluster's byte bounds
        // are recorded as the string is built rather than counted by hand:
        // each of those codepoints is a different width in UTF-8, and a
        // hand-written offset here would be a silent lie the moment it drifts.
        std::string s = "X";
        const S32 cluster_start = (S32)s.size();
        for (llwchar cp : { (llwchar)0x1F3F3, (llwchar)0xFE0F, (llwchar)0x200D,
                            (llwchar)0x26A7, (llwchar)0xFE0F })
        {
            utf8str_append_cp(s, cp);
        }
        const S32 cluster_end = (S32)s.size();
        s += "Y";
        const S32 total = (S32)s.size();

        const F32 wfull = font->getWidthF32Bytes(s, 0, total, false);
        if (wfull <= 0.f)
            SKIP("emoji fallback not available");

        // Sanity: width up to the cluster's end must exceed width up to its
        // start by at least a few pixels — otherwise the emoji didn't
        // actually shape and the cluster-snap test below would trivially
        // pass on an unrendered glyph.
        const F32 w_pre  = font->getWidthF32Bytes(s, 0, cluster_start, false);
        const F32 w_post = font->getWidthF32Bytes(s, 0, cluster_end, false);
        if (w_post - w_pre < 2.f)
            SKIP("emoji cluster did not shape to a measurable glyph");

        // Walk target_x across the rendered cluster's pixel span at
        // sub-pixel granularity. Every returned cursor pos must land on a
        // cluster boundary — never inside it.
        const F32 cluster_left  = w_pre;
        const F32 cluster_right = w_post;
        for (F32 x = cluster_left - 1.f; x <= cluster_right + 1.f;
             x += 0.25f)
        {
            const S32 pos = font->byteFromPixelOffset(s, 0,
                                                      x, F32_MAX,
                                                      total, /*round=*/true);
            CAPTURE(x, pos);
            const bool inside_cluster = (pos > cluster_start && pos < cluster_end);
            UNSCOPED_INFO("byteFromPixelOffset round=true returns mid-cluster offset");
            REQUIRE_FALSE(inside_cluster);
        }
    }

    // firstDrawableByte is the only one of the three layout walks that goes
    // backward, and a backward walk is where a cluster gets split: the
    // trailing codepoints of one carry no advance of their own, so a naive
    // step always finds room for them and then stops on the one holding the
    // width. Sweep the budget across a string with a ZWJ cluster in it and
    // hold every answer to the promise llfontgl.h makes -- a character start,
    // and where the text shapes, a cluster start.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: firstDrawableByte never lands inside a character or a ZWJ cluster", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // Byte bounds recorded as the string is built: the five codepoints of
        // a trans flag are 4/3/3/3/3 bytes, so a hand-written offset here
        // would be a lie the moment anything about it drifts.
        std::string s = "X";
        const S32 cluster_start = (S32)s.size();
        for (llwchar cp : { (llwchar)0x1F3F3, (llwchar)0xFE0F, (llwchar)0x200D,
                            (llwchar)0x26A7, (llwchar)0xFE0F })
        {
            utf8str_append_cp(s, cp);
        }
        const S32 cluster_end = (S32)s.size();
        s += "Y";
        const S32 total = (S32)s.size();

        const F32 wfull = font->getWidthF32Bytes(s, 0, total, false);
        if (wfull <= 0.f)
            SKIP("emoji fallback not available");
        const F32 w_pre  = font->getWidthF32Bytes(s, 0, cluster_start, false);
        const F32 w_post = font->getWidthF32Bytes(s, 0, cluster_end, false);
        if (w_post - w_pre < 2.f)
            SKIP("emoji cluster did not shape to a measurable glyph");

        for (F32 px = 0.f; px <= wfull + 2.f; px += 0.5f)
        {
            const S32 first = font->firstDrawableByte(s, px);
            CAPTURE(px, first);
            {
                INFO("firstDrawableByte names a position inside the text");
                REQUIRE(first >= 0);
                REQUIRE(first < total);
            }
            UNSCOPED_INFO("firstDrawableByte returns a continuation byte");
            REQUIRE(((unsigned char)s[first] & 0xC0) != 0x80);
            const bool inside_cluster = (first > cluster_start && first < cluster_end);
            UNSCOPED_INFO("firstDrawableByte returns a position inside a cluster");
            REQUIRE_FALSE(inside_cluster);
        }
    }

    // What the budget buys, and that it buys it one character at a time. The
    // set of positions a full sweep can return has to be exactly the set of
    // character starts: anything missing is a character the backward walk
    // stepped over, and anything extra is an offset inside one.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: firstDrawableByte sweeps through every character start and no other", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // "a", e-acute (two bytes), "b", "c" -- so byte 2 is the only offset
        // in the string that is not somewhere drawing may begin.
        const std::string s = "a\xC3\xA9" "bc";
        const S32 total = (S32)s.size();
        UNSCOPED_INFO("the fixture string is five bytes");
        CHECK(total == (S32)5);

        const F32 wfull = font->getWidthF32Bytes(s, 0, total, false);
        if (wfull <= 0.f)
            SKIP("font produced no width");

        std::set<S32> seen;
        S32 previous = total;
        for (F32 px = 0.f; px <= wfull + 2.f; px += 0.25f)
        {
            const S32 first = font->firstDrawableByte(s, px);
            CAPTURE(px);
            UNSCOPED_INFO("a larger budget starts drawing no later");
            REQUIRE(first <= previous);
            previous = first;
            seen.insert(first);
        }

        const std::set<S32> starts{ 0, 1, 3, 4 };
        UNSCOPED_INFO("the sweep reaches every character start and nothing else");
        CHECK(seen == starts);
    }

    // The edges, and the one case with no good answer. A budget that cannot
    // hold even the last character still has to name a place to draw from --
    // that character, clipped -- because returning nothing would leave a
    // caller scrolling a field it can never make progress in.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: firstDrawableByte on empty text and at zero and unlimited budgets", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string s = "Hello";
        const S32 total = (S32)s.size();
        const F32 wfull = font->getWidthF32Bytes(s, 0, total, false);
        if (wfull <= 0.f)
            SKIP("font produced no width");

        UNSCOPED_INFO("empty text draws from the beginning");
        CHECK(font->firstDrawableByte(std::string_view(), 100.f) == (S32)0);
        UNSCOPED_INFO("room for everything draws from the beginning");
        CHECK(font->firstDrawableByte(s, F32_MAX) == (S32)0);
        UNSCOPED_INFO("no room at all still draws the last character");
        CHECK(font->firstDrawableByte(s, 0.f) == total - 1);
        UNSCOPED_INFO("a byte budget of nothing draws from the beginning");
        CHECK(font->firstDrawableByte(s, F32_MAX, S32_MAX, 0) == (S32)0);

        // start_pos chooses which character has to be the last one drawn, so
        // with room to spare the answer is the beginning whichever it is.
        for (S32 start = 0; start < total; ++start)
        {
            CAPTURE(start);
            UNSCOPED_INFO("room for everything draws from the beginning");
            REQUIRE(font->firstDrawableByte(s, F32_MAX, start) == (S32)0);
            UNSCOPED_INFO("no room draws the character asked for");
            REQUIRE(font->firstDrawableByte(s, 0.f, start) == start);
        }
    }

    // maxDrawableBytes over text where a character is not a byte. The ASCII
    // test beside this one cannot tell a byte count from a character count, a
    // glyph count or a cluster count -- they are the same number there.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: maxDrawableBytes over text where a character is not a byte", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // Three characters, three bytes each.
        const std::string s = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
        const S32 total = (S32)s.size();
        UNSCOPED_INFO("the fixture string is nine bytes");
        CHECK(total == (S32)9);
        if (font->getWidthF32Bytes(s, 0, total, false) <= 0.f)
            SKIP("font produced no width for the sample");

        UNSCOPED_INFO("all the room in the world fits every byte");
        CHECK(font->maxDrawableBytes(s, F32_MAX, total) == total);
        UNSCOPED_INFO("no room fits nothing");
        CHECK(font->maxDrawableBytes(s, 0.f, total) == (S32)0);

        // A budget that stops inside a character has to come back to where
        // that character started. Answering 4 would split it.
        UNSCOPED_INFO("a byte budget inside a character backs off");
        CHECK(font->maxDrawableBytes(s, F32_MAX, 4) == (S32)3);
        UNSCOPED_INFO("and again one byte further in");
        CHECK(font->maxDrawableBytes(s, F32_MAX, 5) == (S32)3);
        UNSCOPED_INFO("a budget on a boundary is kept");
        CHECK(font->maxDrawableBytes(s, F32_MAX, 6) == (S32)6);

        // By pixels rather than bytes: room for two characters and not three.
        const F32 w2 = font->getWidthF32Bytes(s, 0, 6, false);
        const F32 w3 = font->getWidthF32Bytes(s, 0, 9, false);
        UNSCOPED_INFO("the third character has a width");
        CHECK(w3 > w2);
        UNSCOPED_INFO("a pixel budget also lands on a character");
        CHECK(font->maxDrawableBytes(s, (w2 + w3) * 0.5f, total) == (S32)6);
    }

    // begin_offset, and what the answer is counted from. Every one of these
    // takes an offset to start at, and byteFromPixelOffset reports back
    // relative to it -- LLLineEditor::calcCursorPos adds its scroll position
    // to the result, so an absolute answer would double it.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: measuring and hit-testing from begin_offset answer relative to it", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // "A", a three-byte character, "B" -- and the tail on its own.
        const std::string s    = "A\xE6\x97\xA5" "B";
        const std::string tail = "\xE6\x97\xA5" "B";
        if (font->getWidthF32Bytes(s, 0, (S32)s.size(), false) <= 0.f)
            SKIP("font produced no width for the sample");

        UNSCOPED_INFO("measuring from an offset measures the rest");
        CHECK(font->getWidthF32Bytes(s, 1, S32_MAX, false) ==
                      font->getWidthF32Bytes(tail, 0, S32_MAX, false));
        UNSCOPED_INFO("S32_MAX from a non-zero offset does not overflow");
        CHECK(font->getWidthF32Bytes(s, 1, S32_MAX, false) ==
                      font->getWidthF32Bytes(s, 1, (S32)s.size() - 1, false));
        UNSCOPED_INFO("an offset at the end measures nothing");
        CHECK(font->getWidthF32Bytes(s, (S32)s.size(), S32_MAX, false) == 0.f);

        // Hit-testing from an offset answers in the same frame of reference.
        const F32 w_tail_first = font->getWidthF32Bytes(tail, 0, 3, false);
        UNSCOPED_INFO("hit-testing from an offset is relative to it");
        CHECK(font->byteFromPixelOffset(s, 1, w_tail_first, F32_MAX,
                                                S32_MAX, /*round=*/true) ==
                      (S32)3);

        // round=false asks which character the pixel is inside, rather than
        // which boundary it is nearest, so a point just past a character's
        // start still belongs to that character.
        const F32 just_inside = font->getWidthF32Bytes(s, 0, 1, false) + 0.5f;
        UNSCOPED_INFO("round=false stays in the character the pixel is in");
        CHECK(font->byteFromPixelOffset(s, 0, just_inside, F32_MAX,
                                                S32_MAX, /*round=*/false) ==
                      (S32)1);
    }

    // The two wrap styles that are not ANYWHERE. maxDrawableBytes was rewritten
    // onto UAX #14 in this work and only its default was ever exercised.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: the word-boundary wrap styles retreat to a break", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string s = "alpha beta";
        const S32 total = (S32)s.size();
        const S32 after_space = 6;   // "alpha " -- where "beta" begins

        // A budget reaching into "beta" but not through it. ANYWHERE cuts
        // wherever it runs out; the word-boundary styles retreat to the break.
        const F32 w_partial = font->getWidthF32Bytes(s, 0, 8, false);
        if (w_partial <= 0.f)
            SKIP("font produced no width for the sample");

        const S32 anywhere = font->maxDrawableBytes(s, w_partial, total,
                                                    LLFontGL::ANYWHERE);
        UNSCOPED_INFO("ANYWHERE cuts inside the last word");
        CHECK(anywhere > after_space);

        UNSCOPED_INFO("a word boundary is preferred when there is one");
        CHECK(font->maxDrawableBytes(s, w_partial, total,
                                             LLFontGL::WORD_BOUNDARY_IF_POSSIBLE) ==
                      after_space);
        UNSCOPED_INFO("and required when the style says only");
        CHECK(font->maxDrawableBytes(s, w_partial, total,
                                             LLFontGL::ONLY_WORD_BOUNDARIES) ==
                      after_space);

        // One long word offers no break at all. IF_POSSIBLE has to give the
        // caller the clip position anyway, or a line it can never make
        // progress past. ONLY_WORD_BOUNDARIES is entitled to refuse.
        const std::string one_word = "unbreakablesequence";
        const F32 w_some = font->getWidthF32Bytes(one_word, 0, 6, false);
        const S32 no_break = font->maxDrawableBytes(one_word, w_some,
                                                    (S32)one_word.size(),
                                                    LLFontGL::WORD_BOUNDARY_IF_POSSIBLE);
        UNSCOPED_INFO("with no legal break the caller still gets progress");
        CHECK(no_break > 0);
    }

    // maxDrawableBytes shapes a window sized from the pixel budget rather than
    // the whole remaining text, and widens it when the walk runs out of text
    // before it runs out of pixels. Every answer has to be the one the
    // unwindowed walk gave, or a wrapped paragraph silently breaks in
    // different places than it used to.
    //
    // The bound is checked by construction: a budget of F32_MAX takes the
    // whole text as its window, so the two are the same walk and comparing
    // them proves nothing. What proves it is that a small budget over a long
    // string -- where the window is a fraction of the text -- agrees with the
    // same question asked over a string short enough that no window applies.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: maxDrawableBytes on long text fits what getWidthF32Bytes says fits", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // Long enough that a small pixel budget windows well inside it, in a
        // mix of scripts so the shaping is not trivially uniform.
        std::string s;
        for (int i = 0; i < 40; ++i)
        {
            s += "the quick brown fox ";
            s += "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E ";
            s += "caf\xC3\xA9 na\xC3\xAFve ";
        }
        const S32 total = (S32)s.size();
        UNSCOPED_INFO("the fixture string is long");
        CHECK(total > 1500);
        if (font->getWidthF32Bytes(s, 0, 64, false) <= 0.f)
            SKIP("font produced no width for the sample");

        // Held against getWidthF32Bytes rather than against maxDrawableBytes
        // itself. Asking the same function a second question only proves it is
        // consistent with its own window; both readings move together when the
        // window is wrong. getWidthF32Bytes walks independently, and its
        // return is exactly the quantity the clip compares -- advance plus the
        // extent overhang of the last glyph -- so the two are directly
        // comparable.
        for (F32 px = 8.f; px < 900.f; px += 13.f)
        {
            const S32 got = font->maxDrawableBytes(s, px, total, LLFontGL::ANYWHERE);
            CAPTURE(px, got);
            {
                INFO("the answer stays inside the text");
                REQUIRE(got >= 0);
                REQUIRE(got <= total);
            }

            if (got > 0)
            {
                UNSCOPED_INFO("what fits is within the budget");
                REQUIRE(font->getWidthF32Bytes(s, 0, got, false) <= px);
            }
            if (got < total)
            {
                // And it is the most that fits: one character more overruns.
                const S32 more = (S32)utf8str_step_grapheme_forward(s, (size_t)got);
                UNSCOPED_INFO("and one character more does not");
                REQUIRE(font->getWidthF32Bytes(s, 0, more, false) > px);
            }
        }

        // The word-boundary styles retreat to a break, so they answer with no
        // more than ANYWHERE does and never with something that overruns.
        for (const LLFontGL::EWordWrapStyle style :
             { LLFontGL::WORD_BOUNDARY_IF_POSSIBLE, LLFontGL::ONLY_WORD_BOUNDARIES })
        {
            for (F32 px = 24.f; px < 900.f; px += 37.f)
            {
                const S32 got = font->maxDrawableBytes(s, px, total, style);
                CAPTURE(style, px, got);
                {
                    INFO("a word-boundary answer stays inside the text");
                    REQUIRE(got >= 0);
                    REQUIRE(got <= total);
                }
                UNSCOPED_INFO("a word-boundary answer never overruns");
                REQUIRE((got == 0 || font->getWidthF32Bytes(s, 0, got, false) <= px));
                UNSCOPED_INFO("and never exceeds what ANYWHERE would take");
                REQUIRE(got <= font->maxDrawableBytes(s, px, total, LLFontGL::ANYWHERE));
            }
        }

        // A budget nothing fits in, and a budget everything fits in.
        UNSCOPED_INFO("no room fits nothing");
        CHECK(font->maxDrawableBytes(s, 0.f, total) == (S32)0);
        UNSCOPED_INFO("all the room takes everything");
        CHECK(font->maxDrawableBytes(s, F32_MAX, total) == total);
    }


    // ===================================================================
    // Render-output group: fixture brings up gUIProgram (needs_render=true)
    // so LLFontGL::render() completes end-to-end against the
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
        ll_test::HeadlessGL& gl = ll_test::sharedHeadlessGL();
        llfontgl_render_data() { ensureRenderLLFontGL(); }
        ~llfontgl_render_data() = default;
    };


    // What a reflow costs the shaper. LLTextBase wraps by asking
    // maxDrawableBytes how much of the remaining text fits, then asking
    // getWidthF32Bytes how wide that answer is -- two questions about the same
    // run, per line, per segment, and every one of them can be a HarfBuzz
    // pass. The shape cache turns the repeats into lookups only when the two
    // calls agree on the slice they are asking about.
    //
    // Counted in cache mutations, which rise once per insert, so the number is
    // exactly "how many runs had to be shaped". Timing would not survive a
    // build machine; this does.
    TEST_CASE_METHOD(llfontgl_data, "LLFontGL: a reflow costs no shapes at the same width and few in a drag", "[LLFontGL]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // A chat line long enough to wrap several times at a readable width.
        const std::string paragraph =
            "The quick brown fox jumps over the lazy dog while the "
            "second sentence carries on for long enough to wrap a few "
            "times at any sensible chat width at all.";

        // One pass of LLTextBase's wrap loop over one segment, counting the
        // shapes it costs and the lines it makes. Mirrors
        // LLNormalTextSegment::getNumBytes followed by getDimensionsF32.
        struct Wrap
        {
            size_t shapes = 0;
            size_t lines  = 0;
        };
        auto wrap_once = [&](F32 width) -> Wrap
        {
            Wrap wrap;
            const size_t before = ALFontShaping::cacheMutationCount();
            S32 at = 0;
            while (at < (S32)paragraph.size())
            {
                const std::string_view rest = std::string_view(paragraph).substr((size_t)at);
                const S32 fits = font->maxDrawableBytes(rest, width,
                                                        (S32)rest.size(),
                                                        LLFontGL::WORD_BOUNDARY_IF_POSSIBLE);
                if (fits <= 0)
                {
                    break;
                }
                // The width of exactly what fit, which is what the wrap loop
                // asks for next to place the line.
                (void)font->getWidthF32Bytes(paragraph, at, fits, true);
                at += fits;
                ++wrap.lines;
            }
            wrap.shapes = ALFontShaping::cacheMutationCount() - before;
            return wrap;
        };

        // Cold, so every distinct slice counts.
        ALFontShaping::clearCache();
        const size_t first = wrap_once(220.f).shapes;
        UNSCOPED_INFO("wrapping shapes something");
        CHECK(first > 0);

        // The same width again: every slice is already cached, so a repeated
        // reflow at an unchanged width must cost nothing.
        const size_t again = wrap_once(220.f).shapes;
        UNSCOPED_INFO("re-wrapping at the same width re-shapes nothing");
        CHECK(again == size_t(0));

        // A drag-resize. Each frame moves the width a pixel or two, and the
        // wrap points move with it, so the slices differ from the ones the
        // last frame cached. This is what a resize costs per frame per
        // segment, and it is the number worth watching.
        size_t dragging = 0;
        size_t drag_lines = 0;
        for (S32 px = 219; px >= 200; --px)
        {
            const Wrap wrap = wrap_once((F32)px);
            dragging += wrap.shapes;
            drag_lines += wrap.lines;
        }

        // Not an assertion about a good number -- an assertion that the number
        // is bounded by the work there is to do. Twenty widths over a
        // paragraph that wraps into a handful of lines: if this ever exceeds
        // two shapes per line per width, the two callers have stopped agreeing
        // on their slices again. The lines are each width's own, counted as
        // the wrap makes them.
        std::printf("[reflow shaping] cold=%zu same-width=%zu drag-20px=%zu over %zu lines\n",
                    first, again, dragging, drag_lines);
        UNSCOPED_INFO("the drag stays within two shapes per line per width");
        CHECK(dragging <= 2 * drag_lines);
    }

    // First end-to-end render() call with LEFT/BASELINE places glyph
    // pixels into the framebuffer at the requested x. Pins the full
    // chain: shape → atlas → vertex buffer → drawArrays → fragment
    // output → glReadPixels.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes of one character emits one textured quad", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        gl.clearFramebuffer();
        const std::string s = "A";

        // Held against the quads the draw emits rather than against pixels.
        // Until the fixture's UI shader read its matrices from the Matrices
        // block, nothing this harness drew reached the framebuffer, and a
        // test that asked whether a glyph appeared there answered yes by
        // accident: clearFramebuffer clears to opaque black, the check was
        // for a non-zero alpha, and alpha is 255 over the whole buffer before
        // anything is drawn at all.
        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        const S32 n = font->renderBytes(s, 0, /*x=*/64.f, /*y=*/64.f,
                                   LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                   1);
        // Force any pending verts to draw.
        gGL.flush();
        gGL.endList();
        CHECK(n == 1);

        U32 verts = 0;
        for (const LLVertexBufferData& entry : capture)
        {
            verts += entry.mCount;
        }
        UNSCOPED_INFO("one glyph is one quad");
        CHECK(verts == (U32)6);
        {
            INFO("the quad carries an atlas texture");
            REQUIRE_FALSE(capture.empty());
            CHECK(capture.front().mTexName != 0);
        }
    }

    // HCENTER and RIGHT alignment branches: render() must complete
    // through both branches without crashing and return the same
    // character count as the LEFT branch. Pixel-level position
    // verification is brittle in this test setup (stub shader / state
    // accumulation); this pins crash-free traversal of all three
    // halign branches at llfontgl.cpp:354-372.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes draws every byte whatever the horizontal alignment", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "ABC";

        const S32 n_left = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        const S32 n_cen  = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::HCENTER, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        const S32 n_right = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                         LLFontGL::RIGHT, LLFontGL::BASELINE,
                                         LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 3);
        gGL.flush();
        CHECK(n_left == 3);
        CHECK(n_cen == 3);
        CHECK(n_right == 3);
    }

    // VAlign branches: render() must complete through TOP, VCENTER,
    // BASELINE, and BOTTOM without crashing; return value matches
    // requested char count for each. Pins the valign switch in
    // LLFontGL::renderBytes.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes draws every byte whatever the vertical alignment", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        const S32 n_top = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::TOP,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_vcen = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::VCENTER,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_bs  = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BASELINE,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_btm = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                       LLFontGL::LEFT, LLFontGL::BOTTOM,
                                       LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        gGL.flush();
        CHECK(n_top == 2);
        CHECK(n_vcen == 2);
        CHECK(n_bs == 2);
        CHECK(n_btm == 2);
    }

    // Style flip across renders: render with NORMAL style, then BOLD.
    // Both must succeed. Pins style propagation through render() —
    // 00f3a4e93d's regression invalidation gate.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes draws every byte whatever the style", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        const S32 n_norm = font->renderBytes(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 n_bold = font->renderBytes(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::BOLD, LLFontGL::NO_SHADOW, 5);
        const S32 n_und  = font->renderBytes(s, 0, 50.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::UNDERLINE, LLFontGL::NO_SHADOW, 5);
        gGL.flush();
        CHECK(n_norm == 5);
        CHECK(n_bold == 5);
        CHECK(n_und == 5);
    }

    // Shadow rendering paths: render with NO_SHADOW, DROP_SHADOW, and
    // DROP_SHADOW_SOFT. All three branches must complete without
    // crashing. The two-pass shadow capture (genBuffers' beginList
    // dance at llfonttextcache.cpp:281-290) needs sCurBoundShader
    // to be gUIProgram, which the fixture ensures. Pins shadow
    // rendering doesn't crash; visible-shadow geometry verification is
    // out of scope for this fixture (no production shadow shader).
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes draws every byte whatever the shadow", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        const S32 n_no = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                      LLFontGL::LEFT, LLFontGL::BASELINE,
                                      LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_drop = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::DROP_SHADOW, 2);
        const S32 n_soft = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::DROP_SHADOW_SOFT, 2);
        gGL.flush();
        CHECK(n_no == 2);
        CHECK(n_drop == 2);
        CHECK(n_soft == 2);
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
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: mid-render glyph rasterization keeps the pending batch's texture", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const LLFontFreetype* head = font->getFontFreetype();
        REQUIRE(head != nullptr);

        // U+1F995 SAUROPOD — obscure enough that no other test in this
        // binary rasterizes it. The mid-render upload only fires on a
        // cache miss, so the emoji must be cold going in.
        const llwchar emoji_cp = 0x1F995;
        U32 emoji_idx = 0;
        const LLFontFreetype* emoji_face = head->selectShapingFace(emoji_cp, emoji_idx);
        if (!emoji_face || emoji_face == head || emoji_idx == 0)
            SKIP("no emoji fallback covers U+1F995 in this harness");
        REQUIRE(emoji_face->getFontFace() != nullptr);
        if (emoji_face->getFontFace()->findGlyphInfo(emoji_idx, EFontGlyphType::Color))
            SKIP("U+1F995 already rasterized; mid-render upload won't fire");

        const std::string s = "stomp \xF0\x9F\xA6\x95 check";

        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        const S32 n = font->renderBytes(s, 0, 50.f, 100.f, LLColor4::white,
                                   LLFontGL::LEFT, LLFontGL::BASELINE,
                                   LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                   (S32)s.size());
        gGL.endList();
        UNSCOPED_INFO("render consumed the whole string");
        REQUIRE(n == (S32)s.size());

        // Expected textures, resolved through the glyph entries the render
        // itself produced. use_color defaulted true, so the render looked
        // glyphs up as Color; querying the same way returns the same
        // entries (and therefore the same atlas pages) it drew from.
        const LLFontGlyphInfo* latin_gi = head->getGlyphInfo((llwchar)'s', EFontGlyphType::Color);
        {
            INFO("latin glyph cached");
            REQUIRE(latin_gi);
            REQUIRE(latin_gi->mSourceFace);
        }
        const auto& latin_entry = latin_gi->mPhaseSlots[0].mBitmapEntry;
        const LLFontBitmapCache* latin_cache = latin_gi->mSourceFace->getBitmapCache();
        REQUIRE(latin_cache != nullptr);
        LLImageGL* latin_img = latin_cache->getImageGL(latin_entry.first, (U32)latin_entry.second);
        UNSCOPED_INFO("latin atlas page live");
        REQUIRE(latin_img != nullptr);
        const U32 latin_tex = latin_img->getTexName();

        const LLFontGlyphInfo* emoji_gi = head->getGlyphInfo(emoji_cp, EFontGlyphType::Color);
        {
            INFO("emoji glyph cached");
            REQUIRE(emoji_gi);
            REQUIRE(emoji_gi->mSourceFace);
        }
        const auto& emoji_entry = emoji_gi->mPhaseSlots[0].mBitmapEntry;
        const LLFontBitmapCache* emoji_cache = emoji_gi->mSourceFace->getBitmapCache();
        REQUIRE(emoji_cache != nullptr);
        LLImageGL* emoji_img = emoji_cache->getImageGL(emoji_entry.first, (U32)emoji_entry.second);
        UNSCOPED_INFO("emoji atlas page live");
        REQUIRE(emoji_img != nullptr);
        const U32 emoji_tex = emoji_img->getTexName();

        UNSCOPED_INFO("string spans two distinct atlas textures");
        CHECK(latin_tex != emoji_tex);
        REQUIRE_FALSE(capture.empty());

        // The first captured batch is the Latin prefix, flushed when the
        // glyph stream switches to the emoji's atlas — exactly the batch
        // the mid-render upload used to misdirect.
        UNSCOPED_INFO("prefix batch carries its own atlas texture");
        CHECK(capture.front().mTexName == latin_tex);
        // And every batch in the capture must reference one of the two
        // atlas pages this string actually draws from.
        for (const LLVertexBufferData& entry : capture)
        {
            UNSCOPED_INFO("batch texture is one of the string's atlas pages");
            REQUIRE((entry.mTexName == latin_tex || entry.mTexName == emoji_tex));
        }
    }

    // What renderBytes counts. Over ASCII its return is indistinguishable from
    // a codepoint count, a glyph count or a cluster count, and every existing
    // assertion about it is over ASCII. Three characters of three bytes each
    // tell the four apart: nine, and nothing else.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes counts bytes rather than characters", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string s = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
        const S32 total = (S32)s.size();
        UNSCOPED_INFO("the fixture string is nine bytes");
        CHECK(total == (S32)9);
        if (font->getWidthF32Bytes(s, 0, total, false) <= 0.f)
            SKIP("font produced no width for the sample");

        const S32 drawn = font->renderBytes(s, 0, 100.f, 100.f, LLColor4::white,
                                            LLFontGL::LEFT, LLFontGL::BASELINE,
                                            LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                            total);
        gGL.flush();
        UNSCOPED_INFO("renderBytes returns bytes, not characters");
        CHECK(drawn == total);

        // A budget of one character is three bytes, and drawing from an offset
        // reports what it drew rather than where it stopped.
        const S32 one = font->renderBytes(s, 0, 100.f, 120.f, LLColor4::white,
                                          LLFontGL::LEFT, LLFontGL::BASELINE,
                                          LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                          3);
        gGL.flush();
        UNSCOPED_INFO("a one-character budget is three bytes");
        CHECK(one == (S32)3);

        const S32 tail = font->renderBytes(s, 3, 100.f, 140.f, LLColor4::white,
                                           LLFontGL::LEFT, LLFontGL::BASELINE,
                                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                           S32_MAX);
        gGL.flush();
        UNSCOPED_INFO("drawing from an offset returns the bytes it drew");
        CHECK(tail == total - 3);
    }

    // Clipping is per cluster, and the count says so. Several glyphs can carry
    // one cluster -- a Devanagari conjunct, a Thai vowel sign, a mark stack,
    // an emoji and its variation selector -- and the pen moves inside one, so
    // a per-glyph clip can paint a base and drop the mark that belongs to it.
    // That is not a clipped syllable, it is a different syllable, and the
    // count reported back calls the whole cluster undrawn either way.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes clips whole clusters and counts what it drew", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const LLFontFreetype* ft = font->getFontFreetype();
        REQUIRE(ft != nullptr);

        // Devanagari, which shapes two glyphs to a cluster with an advance on
        // each -- the shape that a per-glyph clip splits.
        const std::string s =
            "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87";
        const S32 total = (S32)s.size();
        const F32 full = font->getWidthF32Bytes(s, 0, total, false);
        if (full <= 0.f)
            SKIP("no Devanagari coverage in the test font set");

        // Cluster boundaries from the shaper itself rather than by hand: which
        // bytes are boundaries is the font's business, not this test's.
        const auto& glyphs = ALFontShaping::shapeLine(ft, s, 0, (size_t)total);
        if (glyphs.empty())
            SKIP("Devanagari did not shape");
        std::set<S32> boundaries;
        for (const ALShapedGlyph& g : glyphs)
        {
            boundaries.insert(g.cluster);
        }
        boundaries.insert(total);

        // Counted through the vertex capture rather than the framebuffer: the
        // quads a draw emits are what the count has to agree with, and they are
        // observable here whether or not the harness rasterizes anything.
        S32 checked = 0;
        for (F32 px = 0.f; px <= full + 2.f; px += 0.5f)
        {
            std::list<LLVertexBufferData> capture;
            gGL.beginList(&capture);
            const S32 n = font->renderBytes(s, 0, 20.f, 64.f, LLColor4::white,
                                            LLFontGL::LEFT, LLFontGL::BASELINE,
                                            LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                            S32_MAX, (S32)px);
            gGL.flush();
            gGL.endList();
            CAPTURE(px, n);

            U32 verts = 0;
            for (const LLVertexBufferData& entry : capture)
            {
                verts += entry.mCount;
            }

            UNSCOPED_INFO("a clipped draw stops on a cluster boundary");
            REQUIRE(boundaries.count(n) == 1);

            // Exactly the glyphs of the clusters the count claims. A per-glyph
            // clip emits the glyphs of the overflowing cluster that came before
            // the one that did not fit, and then reports that whole cluster as
            // undrawn -- so the quads outnumber what the count accounts for,
            // which is the same as saying a base was painted without its mark.
            U32 expected = 0;
            for (const ALShapedGlyph& g : glyphs)
            {
                if (g.cluster < n) expected += 6;
            }
            // What this does NOT cover: a clip that tests one glyph of a
            // cluster and then emits the rest of it unconditionally keeps the
            // quads and the count agreeing while painting past the edge. The
            // obvious check -- getWidthF32Bytes(s, 0, n) against the budget --
            // does not work, because the clip compares scaled pixels against
            // its own start_x while getWidthF32Bytes divides the scale back
            // out. Testing the drawn extent wants the vertex positions out of
            // the capture, not a re-measurement.
            UNSCOPED_INFO("quads drawn match the bytes reported");
            REQUIRE(verts == expected);
            if (n > 0 && n < total) ++checked;
        }
        UNSCOPED_INFO("the sweep reached a partially-clipped draw");
        CHECK(checked > 0);
    }

    // renderBytes places a right-aligned or centred string by measuring it, and
    // it takes that measurement from the glyphs it is about to draw rather than
    // shaping the text a second time. The two have to agree exactly: the number
    // that positions the text and the number getWidthF32Bytes reports are the
    // same quantity, and a string measured by one and drawn by the other sits
    // in the wrong place.
    //
    // right_x is the observable end of the drawn run, so LEFT-aligned it is the
    // origin plus the width. That is what pins the shared sum against the
    // independent walk.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderBytes ends a run exactly where getWidthF32Bytes measures it", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string samples[] = {
            std::string("Hello world"),
            std::string("caf\xC3\xA9 na\xC3\xAFve"),
            std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"),
            std::string("A\xE6\x97\xA5" "B"),
            std::string("Wa"),          // a kerning pair
            std::string("."),
        };

        S32 measured = 0;
        for (const std::string& s : samples)
        {
            CAPTURE(s);
            const S32 total = (S32)s.size();
            // no_padding: right_x is where the pen ended, so it carries the
            // advances and not the extent overhang of the last glyph, which is
            // what the padded form adds on top.
            const F32 expect = font->getWidthF32Bytes(s, 0, total, true);
            if (expect <= 0.f)
                continue;   // a script the test's fonts do not cover
            ++measured;

            F32 right_x = 0.f;
            font->renderBytes(s, 0, 40.f, 60.f, LLColor4::white,
                              LLFontGL::LEFT, LLFontGL::BASELINE,
                              LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                              total, S32_MAX, &right_x);
            gGL.flush();

            // right_x is in the same unscaled space the origin was given in.
            UNSCOPED_INFO("the drawn run ends where the measurement says");
            REQUIRE(right_x == 40.f + expect);
        }
        UNSCOPED_INFO("some sample had a width to hold the drawn run against");
        CHECK(measured > 0);
    }

    // An ellipsized draw buys itself padding by measuring "....", and that
    // measurement shapes: on a cold cache it misses, inserts, and moves the
    // shape cache's mutation count -- after renderBytes has already shaped the
    // string it is about to walk. That count is the tripwire for the walk
    // holding a pointer into an entry that went away underneath it, so it has
    // to be read after the dots are measured. Read before, the first
    // ellipsized string of a session trips a tripwire for a walk that has not
    // started yet, which on the login screen is every session.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: an ellipsized draw on a cold cache shapes the dots mid-draw", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        const std::string s = "a string rather wider than the budget it is given";

        // Warm the string itself, so the only thing left to shape inside the
        // draw below is the ellipsis measurement.
        ALFontShaping::clearCache();
        const F32 full = font->getWidthF32(s);
        UNSCOPED_INFO("the sample string has width");
        CHECK(full > 0.f);

        const size_t before = ALFontShaping::cacheMutationCount();

        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        const S32 n = font->renderBytes(s, 0, /*x=*/20.f, /*y=*/64.f,
                                        LLColor4::white,
                                        LLFontGL::LEFT, LLFontGL::BASELINE,
                                        LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                        (S32)s.size(), (S32)(full / 2.f),
                                        nullptr, /*use_ellipses=*/true);
        gGL.flush();
        gGL.endList();

        UNSCOPED_INFO("an ellipsized draw stops short of the whole string");
        REQUIRE(n < (S32)s.size());
        // Without this the test proves nothing: it is the dots landing in the
        // cache mid-draw that puts the snapshot and the count out of step.
        UNSCOPED_INFO("the ellipsis measurement shaped inside the draw");
        CHECK(ALFontShaping::cacheMutationCount() > before);
    }

    // A string placed by placeGlyphs and drawn by renderGlyphs lands on the
    // pixels renderUTF8 puts it on, whichever way it is aligned; and runs
    // drawn in one call by renderGlyphRuns are the runs drawn apiece.
    TEST_CASE_METHOD(llfontgl_render_data, "LLFontGLRender: renderGlyphs matches renderUTF8 and renderGlyphRuns matches renderGlyphs", "[LLFontGLRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not found");
        const S32  w     = ll_test::HeadlessGL::WIDTH;
        const S32  h     = ll_test::HeadlessGL::HEIGHT;
        const auto frame = [&](const std::function<void()>& draw) {
            gl.clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(w, h);
        };
        const auto lit = [](const std::vector<U8>& px) {
            for (size_t i = 0; i < px.size(); i += 4)
            {
                if (px[i] > 0)
                    return true;
            }
            return false;
        };
        // Where two frames differ: how many pixels, and the first of them, so
        // a failure names a place rather than printing both frames whole.
        struct FrameDiff
        {
            size_t pixels  = 0;
            S32    first_x = -1;
            S32    first_y = -1;
        };
        const auto differ = [&](const std::vector<U8>& a, const std::vector<U8>& b) {
            FrameDiff d;
            const size_t common = std::min(a.size(), b.size());
            for (size_t i = 0; i + 3 < common; i += 4)
            {
                if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2] || a[i + 3] != b[i + 3])
                {
                    if (d.pixels++ == 0)
                    {
                        d.first_x = (S32)((i / 4) % (size_t)w);
                        d.first_y = (S32)((i / 4) / (size_t)w);
                    }
                }
            }
            // Frames of different sizes are different frames.
            d.pixels += (std::max(a.size(), b.size()) - common + 3) / 4;
            return d;
        };
        const LLFontGL::HAlign aligns[] = { LLFontGL::LEFT, LLFontGL::RIGHT, LLFontGL::HCENTER };
        for (LLFontGL* font : { LLFontGL::getFontMonospace(), LLFontGL::getFontSansSerif() })
        {
            REQUIRE(font != nullptr);
            for (const std::string s : { "7", "42", "12345", "Hello, world" })
            {
                for (LLFontGL::HAlign halign : aligns)
                {
                    CAPTURE(s, halign);
                    const std::vector<U8> said = frame([&] {
                        font->renderUTF8(s, 0, 128.f, 100.f, LLColor4::white, halign, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
                    });
                    std::vector<LLFontGL::Placed> placed;
                    const F32                     shift = font->placeGlyphs(s, halign, placed);
                    const std::vector<LLColor4U>  colors(placed.size(), LLColor4U(255, 255, 255, 255));
                    const std::vector<U8>         drawn = frame([&] { font->renderGlyphs(placed.data(), colors.data(), placed.size(), 128.f + shift, 100.f); });
                    UNSCOPED_INFO("drawn at all");
                    REQUIRE(lit(said));
                    const FrameDiff d = differ(said, drawn);
                    CAPTURE(d.first_x, d.first_y);
                    UNSCOPED_INFO("the same pixels");
                    REQUIRE(d.pixels == 0u);
                }
            }
        }

        LLFontGL*                     font = LLFontGL::getFontSansSerif();
        std::vector<LLFontGL::Placed> first, second;
        font->placeGlyphs("abc", LLFontGL::LEFT, first);
        font->placeGlyphs("xyz", LLFontGL::LEFT, second);
        const std::vector<LLColor4U> red(first.size(), LLColor4U(255, 0, 0, 255));
        const std::vector<LLColor4U> green(second.size(), LLColor4U(0, 255, 0, 255));
        const std::vector<U8> apiece = frame([&] {
            font->renderGlyphs(first.data(), red.data(), first.size(), 20.f, 50.f);
            font->renderGlyphs(second.data(), green.data(), second.size(), 100.f, 150.f);
        });
        const LLFontGL::GlyphRun runs[] = { { first.data(), red.data(), first.size(), 20.f, 50.f },
                                            { second.data(), green.data(), second.size(), 100.f, 150.f } };
        const std::vector<U8> together = frame([&] { font->renderGlyphRuns(runs, 2); });
        UNSCOPED_INFO("runs drawn at all");
        CHECK(lit(apiece));
        const FrameDiff d = differ(apiece, together);
        CAPTURE(d.first_x, d.first_y);
        UNSCOPED_INFO("runs in one call are the runs apiece");
        CHECK(d.pixels == 0u);
    }
}
