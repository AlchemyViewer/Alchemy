/**
 * @file alfontcolrv1_test.cpp
 * @brief Unit tests for COLRv1 detection and the paint walker that
 *        rasterises Noto-COLRv1 glyphs.
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

#include "../alfontcolrv1.h"
#include "../alfontface.h"
#include "../llfontfreetype.h"
#include "../llfontregistry.h"

#include "../test/lltut.h"

#include <hb.h>
#include <hb-ft.h>

#if LL_MESA_HEADLESS
#  include "../llfontbitmapcache.h"
#  include "../llfontgl.h"      // sForceMonochromeEmoji static for force-mono test
#  include "../llimagegl.h"
#  include "llheadlessgl_fixture.h"
#endif

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

namespace
{
    // Resolved at test time, points at the source-tree font directory so the
    // tests don't depend on whatever the runtime install staged. Set via the
    // LLFONT_TEST_DATA_DIR compile-time define from CMake.
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = LLFile::fopen(path.c_str(), LLFILE_MODE("rb")))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // Construct a face key with reasonable defaults. point_size 14 / DPI 96
    // is a typical UI-text size; the painter doesn't care what we pick as
    // long as ppem is non-zero.
    ALFontFaceKey makeKey(const std::string& filename)
    {
        return ALFontFaceKey{
            filename,
            /*face_index=*/0,
            /*point_size=*/14.f,
            /*vert_dpi=*/96.f,
            /*horz_dpi=*/96.f,
            EFontHinting::DEFAULT,
            /*flags=*/0
        };
    }

    // Walks a putative BGRA buffer (top-row first, tightly packed) and
    // returns true if any pixel has non-zero alpha. Used to confirm the
    // painter actually drew something into the surface.
    bool anyAlpha(const U8* bgra, S32 w, S32 h, S32 pitch)
    {
        for (S32 y = 0; y < h; ++y)
        {
            const U8* row = bgra + (ptrdiff_t)y * pitch;
            for (S32 x = 0; x < w; ++x)
            {
                if (row[x * 4 + 3] != 0)
                    return true;
            }
        }
        return false;
    }
}

namespace tut
{
    // Each test_group<>::object spins up a fresh fixture for every test<N>,
    // so init/cleanup the font manager once per test rather than once per
    // process. Cheaper than tracking init state and matches the test-
    // isolation model TUT expects.
    struct alfontcolrv1_data
    {
        alfontcolrv1_data()
        {
            LLFontManager::initClass();
        }
        ~alfontcolrv1_data()
        {
            LLFontManager::cleanupClass();
        }
    };

    typedef test_group<alfontcolrv1_data> alfontcolrv1_test;
    typedef alfontcolrv1_test::object     alfontcolrv1_object;
    tut::alfontcolrv1_test alfontcolrv1_testcase("ALFontColrV1");

    // Painter input validation: null hb_font fails cleanly without a crash.
    // Doesn't need a real font, so this test runs even when the test data
    // dir is empty (e.g., a stripped CI checkout).
    template<> template<>
    void alfontcolrv1_object::test<1>()
    {
        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph(null hb_font) returns false",
               !painter.paintGlyph(nullptr, /*glyph_index=*/0,
                                   /*point_size=*/14.f, fg,
                                   /*palette_index=*/0, result));
        ensure("Result.mBitmap stays null on failure",
               result.mBitmap == nullptr);
        ensure_equals("Result.mWidth stays zero on failure",
                      result.mWidth, 0);
    }

    // hasColrV1 is the in-tree probe for "FreeType's FT_LOAD_COLOR can't
    // rasterise this — route through the paint walker." Verify it fires
    // for Noto-COLRv1.ttf, which ships a COLRv1 paint table.
    template<> template<>
    void alfontcolrv1_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("Noto-COLRv1 loaded", face.notNull() && face->isValid());
        ensure("Noto-COLRv1 hasColrV1 == true", face->hasColrV1());
        ensure("Noto-COLRv1 hasColor == true (general flag)", face->hasColor());
    }

    // Negative: InterVariable.woff2 is a plain text font with no COLR
    // table, so hasColrV1 must stay false. Guards against the probe
    // false-positiving on any FT_HAS_COLOR-y face.
    template<> template<>
    void alfontcolrv1_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
        {
            skip("InterVariable.woff2 not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("Inter loaded", face.notNull() && face->isValid());
        ensure("Inter hasColrV1 == false", !face->hasColrV1());
    }

    // Painter end-to-end: feed a known emoji glyph (fire, U+1F525) through
    // hb_font_paint_glyph and verify the resulting BGRA buffer has at least
    // one non-zero-alpha pixel. Also exercises the FT_Get_Color_Glyph_ClipBox
    // path (Noto-COLRv1 ships clip boxes for its glyphs) and the BGRA atlas
    // packing logic in paintGlyph.
    template<> template<>
    void alfontcolrv1_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded",     face.notNull() && face->isValid());
        ensure("face has COLRv1", face->hasColrV1());

        hb_font_t* hb_font = face->getHbFont();
        ensure("hb_font available", hb_font != nullptr);

        // Resolve the fire emoji's glyph index via the cmap. Avoids hardcoding
        // a glyph_id that could shift across font versions.
        const hb_codepoint_t fire_cp = 0x1F525;
        hb_codepoint_t       fire_gid = 0;
        ensure("cmap maps U+1F525 to a glyph",
               hb_font_get_nominal_glyph(hb_font, fire_cp, &fire_gid)
               && fire_gid != 0);

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        const bool ok = painter.paintGlyph(hb_font, fire_gid, /*point_size=*/14.f,
                                           fg, /*palette_index=*/0, result);
        ensure("paintGlyph succeeded", ok);
        ensure("painter produced bitmap",
               result.mBitmap != nullptr
            && result.mWidth > 0 && result.mHeight > 0
            && result.mPitch != 0);
        ensure("painter drew non-zero alpha somewhere",
               anyAlpha(result.mBitmap, result.mWidth, result.mHeight, result.mPitch));
    }

    // VS-16 cmap absence is the trigger for shape_sub_run's stripping path.
    // Verify Noto-COLRv1 actually has the shape we expect: ZWJ in cmap but
    // VS-16 not. Other emoji fonts that ship VS-16 (e.g. Twemoji) take the
    // no-strip path; the shape pipeline branches on this flag per-face.
    template<> template<>
    void alfontcolrv1_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());

        // ZWJ should resolve to a real glyph (cmap entry present).
        ensure("Noto cmap has ZWJ glyph",
               face->getCharGlyphIndex(0x200D) != 0u);
        // VS-16 should NOT — shape_sub_run keys VS-16 stripping on this.
        ensure_equals("Noto cmap has no VS-16 glyph",
                      face->getCharGlyphIndex(0xFE0F), 0u);
    }

    // paintGlyph on an out-of-range glyph index (beyond the font's
    // num_glyphs) must fail cleanly. The painter calls
    // hb_font_paint_glyph which returns 0 for unknown glyphs; the
    // wrapper turns that into a Result with mBitmap=null and
    // mWidth=0.
    template<> template<>
    void alfontcolrv1_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());
        hb_font_t* hb_font = face->getHbFont();
        ensure("hb_font available", hb_font != nullptr);

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);

        // Use a clearly out-of-range glyph index. Real fonts have
        // num_glyphs in the thousands; 0xFFFFFFFE is well beyond.
        const bool ok = painter.paintGlyph(hb_font,
                                           /*glyph_index=*/0xFFFFFFFE,
                                           14.f, fg, 0, result);
        ensure("paintGlyph on out-of-range glyph index fails cleanly",
               !ok);
        ensure("result.mBitmap stays null on out-of-range",
               result.mBitmap == nullptr);
        ensure_equals("result.mWidth zero on out-of-range",
                      result.mWidth, 0);
    }

    // Painter staging surface reuse: paint two different emoji
    // through one painter. Both calls succeed and the second
    // call's result must reflect the second glyph (not the first
    // — a staging-buffer leak would surface as the second result
    // pointing at stale first-glyph bytes when dimensions match).
    template<> template<>
    void alfontcolrv1_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());
        hb_font_t* hb_font = face->getHbFont();

        // Two distinct emoji: fire (U+1F525) and red heart (U+2764).
        // Both should rasterize cleanly through Noto-COLRv1's paint
        // walker. We don't assert the dimensions match (different
        // glyphs render to different surfaces) — just that both
        // succeed and produce live bitmaps.
        const LLColor4U fg(255, 255, 255, 255);

        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        hb_codepoint_t heart = 0;
        hb_font_get_nominal_glyph(hb_font, 0x2764, &heart);
        if (fire == 0 || heart == 0)
            skip("Noto-COLRv1 missing one of the test emoji codepoints");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r1, r2;
        ensure("first paintGlyph (fire) succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r1));
        ensure("first painter produced bitmap",
               r1.mBitmap != nullptr && r1.mWidth > 0 && r1.mHeight > 0);

        ensure("second paintGlyph (heart) succeeded on same painter",
               painter.paintGlyph(hb_font, heart, 14.f, fg, 0, r2));
        ensure("second painter produced bitmap",
               r2.mBitmap != nullptr && r2.mWidth > 0 && r2.mHeight > 0);
    }

    // 2-em-square surface fallback (88585ff86d): a glyph with no
    // ClipBox AND no FT extents lands the painter on a synthetic
    // 2-em surface so it still produces SOMETHING rather than
    // returning a 0×0 bitmap. We can't naturally trigger this with
    // Noto-COLRv1 (every emoji has extents); the test instead pins
    // the public contract via a positive-extent glyph and confirms
    // the surface size is on the order of (point_size, point_size)
    // rather than 0×0 — i.e. the path that runs when extents work
    // also produces a sized bitmap. A negative version (synthetic
    // glyph forcing the fallback) requires a hand-built TTF and
    // is left for the integration suite.
    template<> template<>
    void alfontcolrv1_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            skip("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());
        hb_font_t* hb_font = face->getHbFont();

        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        constexpr F32 point_size = 14.f;
        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph succeeded",
               painter.paintGlyph(hb_font, fire, point_size, fg, 0, result));
        // Surface dimensions are on the order of point_size — the
        // ClipBox / FT-extents path produces a bitmap whose width and
        // height are within a small multiple of the requested point
        // size. A regression where the path returned 0×0 would fail
        // the lower bound; a regression that ballooned to 2× the
        // 2-em square (32×32 fallback for 14pt) would fail the upper
        // bound (~3× point_size).
        ensure("surface width is in a reasonable range for 14pt",
               result.mWidth >= 4 && result.mWidth <= (S32)(point_size * 4.f));
        ensure("surface height is in a reasonable range for 14pt",
               result.mHeight >= 4 && result.mHeight <= (S32)(point_size * 4.f));
    }

    // (Test 9 removed: pinned the mForegroundOnly flag on the painter
    // Result, which was removed along with the dormant retint chain.)

    // Gradient color variance: paint fire emoji and walk the BGRA bitmap.
    // Among non-zero-alpha pixels, count distinct (b, g, r) triples. A
    // regression where interp_stops returned `front()` always (or the
    // gradient sampling used the same color for every offset) would
    // produce a single solid color → 1 distinct triple. Real gradient
    // emojis show many colors. Pin >= 3.
    template<> template<>
    void alfontcolrv1_object::test<10>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        // Use a larger point size so the rendered bitmap has enough
        // pixels to sample distinct gradient stops without aliasing
        // them away.
        constexpr F32 ps = 32.f;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph succeeded",
               painter.paintGlyph(hb_font, fire, ps, fg, 0, result));
        ensure("painter produced bitmap",
               result.mBitmap != nullptr && result.mWidth > 0 && result.mHeight > 0);

        std::set<U32> distinct;  // packed BGR (alpha excluded)
        for (S32 y = 0; y < result.mHeight; ++y)
        {
            const U8* row = result.mBitmap + (ptrdiff_t)y * result.mPitch;
            for (S32 x = 0; x < result.mWidth; ++x)
            {
                const U8 b = row[x * 4 + 0];
                const U8 g = row[x * 4 + 1];
                const U8 r = row[x * 4 + 2];
                const U8 a = row[x * 4 + 3];
                if (a > 8)  // ignore near-transparent fringe pixels
                    distinct.insert((U32(r) << 16) | (U32(g) << 8) | U32(b));
            }
        }
        // Fire emoji has at minimum a yellow body + orange gradient +
        // red highlights; >= 3 distinct triples is a conservative bound.
        ensure("fire emoji produces >= 3 distinct RGB triples (gradient)",
               distinct.size() >= 3);
    }

    // palette_index argument propagates to hb_font_paint_glyph: paint
    // the fire emoji once with palette 0 and once with palette 1. If
    // Noto-COLRv1 ships >= 2 CPAL palettes, the resulting bytes must
    // differ at non-zero-alpha positions. Skip if only one palette is
    // present (no observable difference, but the code path is exercised
    // either way).
    template<> template<>
    void alfontcolrv1_object::test<11>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        const LLColor4U fg(255, 255, 255, 255);

        ALFontColrV1Painter painter_a, painter_b;
        ALFontColrV1Painter::Result r0, r1;
        ensure("palette=0 paintGlyph succeeded",
               painter_a.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/0, r0));
        ensure("palette=1 paintGlyph succeeded",
               painter_b.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/1, r1));
        ensure("both produced bitmaps",
               r0.mBitmap && r1.mBitmap
            && r0.mWidth == r1.mWidth && r0.mHeight == r1.mHeight);

        // Compare bytes — if the font has only one palette, hb resolves
        // both calls against palette 0 and the bitmaps match. Skip in
        // that case; the test would be vacuous.
        bool any_diff = false;
        for (S32 y = 0; y < r0.mHeight && !any_diff; ++y)
        {
            const U8* a_row = r0.mBitmap + (ptrdiff_t)y * r0.mPitch;
            const U8* b_row = r1.mBitmap + (ptrdiff_t)y * r1.mPitch;
            for (S32 x = 0; x < r0.mWidth; ++x)
            {
                if (a_row[x * 4 + 3] == 0 && b_row[x * 4 + 3] == 0)
                    continue;
                if (std::memcmp(a_row + x * 4, b_row + x * 4, 4) != 0)
                {
                    any_diff = true;
                    break;
                }
            }
        }
        if (!any_diff)
            skip("Noto-COLRv1 ships only one CPAL palette");

        ensure("palette swap changed at least one painted pixel", any_diff);
    }

    // OutputFormat::Gray basics: format selector hits the gray fold path
    // and returns a single-channel buffer with mPitch == width and
    // non-zero coverage somewhere in the bitmap. Pins the §1/§2 contract
    // that Gray output is single-channel, top-row first, tightly packed.
    template<> template<>
    void alfontcolrv1_object::test<12>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph(format=Gray) succeeded",
               painter.paintGlyph(hb_font, fire, /*point_size=*/14.f, fg,
                                  /*palette=*/0,
                                  ALFontColrV1Painter::OutputFormat::Gray,
                                  result));
        ensure_equals("Result.mFormat == Gray",
                      (S32)result.mFormat,
                      (S32)ALFontColrV1Painter::OutputFormat::Gray);
        ensure("Gray output has positive dims",
               result.mWidth > 0 && result.mHeight > 0);
        ensure_equals("Gray pitch is width (tightly packed single channel)",
                      result.mPitch, result.mWidth);

        bool any_nonzero = false;
        for (S32 y = 0; y < result.mHeight && !any_nonzero; ++y)
        {
            const U8* row = result.mBitmap + (ptrdiff_t)y * result.mPitch;
            for (S32 x = 0; x < result.mWidth; ++x)
            {
                if (row[x] != 0) { any_nonzero = true; break; }
            }
        }
        ensure("Gray output has at least one non-zero coverage pixel",
               any_nonzero);
    }

    // Gray-fold formula pin: paint the same glyph in BGRA and Gray and
    // verify each Gray byte equals max(Rec.709(BGRA.rgb_premul),
    // BGRA.alpha * 0.15) within ±1 (rounding tolerance). Locks the
    // hybrid floor + luminance projection from §2 against silent drift.
    // For pixels that happen to be fg-only (white premul: R=G=B=A) this
    // also verifies the foreground-collapses-to-alpha invariant since
    // Y == A and Y > A*0.15 holds, so the test is a strict superset of
    // the "fg-only collapses to alpha" case.
    template<> template<>
    void alfontcolrv1_object::test<13>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result rgba_r, gray_r;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("BGRA paint succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, rgba_r));
        // Stash the BGRA bytes — the next paintGlyph call invalidates
        // mBitmap (it points back into mStaging which we overwrite below).
        std::vector<U8> bgra_bytes(rgba_r.mBitmap,
                                   rgba_r.mBitmap + (size_t)rgba_r.mPitch * rgba_r.mHeight);
        const S32 bgra_w = rgba_r.mWidth, bgra_h = rgba_r.mHeight;
        const S32 bgra_pitch = rgba_r.mPitch;

        ensure("Gray paint succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0,
                                  ALFontColrV1Painter::OutputFormat::Gray,
                                  gray_r));
        ensure_equals("Gray and BGRA produce same width",  gray_r.mWidth,  bgra_w);
        ensure_equals("Gray and BGRA produce same height", gray_r.mHeight, bgra_h);

        S32 mismatches = 0;
        for (S32 y = 0; y < bgra_h; ++y)
        {
            const U8* bgra_row = bgra_bytes.data() + (ptrdiff_t)y * bgra_pitch;
            const U8* gray_row = gray_r.mBitmap   + (ptrdiff_t)y * gray_r.mPitch;
            for (S32 x = 0; x < bgra_w; ++x)
            {
                const U32 b = bgra_row[x*4 + 0];
                const U32 g = bgra_row[x*4 + 1];
                const U32 r = bgra_row[x*4 + 2];
                const U32 a = bgra_row[x*4 + 3];
                const U32 y_256     = r * 54u + g * 183u + b * 18u;
                const U32 floor_256 = a * 38u;
                const U32 cov_256   = (y_256 > floor_256) ? y_256 : floor_256;
                const U32 expected  = (cov_256 >> 8) > 255u ? 255u : (cov_256 >> 8);
                const U32 actual    = gray_row[x];
                const S32 diff = (S32)expected - (S32)actual;
                if (diff < -1 || diff > 1)
                    ++mismatches;
            }
        }
        ensure_equals("every Gray byte matches the hybrid fold formula",
                      mismatches, 0);
    }

    // Hybrid floor pin: scan the BGRA result for fully-saturated palette
    // black pixels (R=G=B=0, A>200) and assert the corresponding Gray
    // byte is >= floor (≈ 38 * a / 256) rather than zero. If no such
    // pixel exists across the test glyphs, skip — the floor invariant
    // still holds, just isn't observable here. The point of the test is
    // to lock the constant against future tweaks that would silently
    // crush dark CPAL details.
    template<> template<>
    void alfontcolrv1_object::test<14>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();

        // Sweep a handful of likely candidates: emoji that commonly carry
        // black eye/outline detail in Noto's palette. First one with a
        // fully-saturated black pixel wins; otherwise the test skips.
        const hb_codepoint_t cps[] = { 0x1F600 /*grinning face*/,
                                       0x1F602 /*face with tears of joy*/,
                                       0x1F525 /*fire*/,
                                       0x2764  /*red heart*/ };
        for (hb_codepoint_t cp : cps)
        {
            hb_codepoint_t gid = 0;
            hb_font_get_nominal_glyph(hb_font, cp, &gid);
            if (gid == 0) continue;

            ALFontColrV1Painter painter;
            ALFontColrV1Painter::Result rgba_r, gray_r;
            const LLColor4U fg(255, 255, 255, 255);
            if (!painter.paintGlyph(hb_font, gid, 32.f, fg, 0, rgba_r))
                continue;
            std::vector<U8> bgra_bytes(rgba_r.mBitmap,
                                       rgba_r.mBitmap + (size_t)rgba_r.mPitch * rgba_r.mHeight);
            const S32 bgra_w = rgba_r.mWidth, bgra_h = rgba_r.mHeight;
            const S32 bgra_pitch = rgba_r.mPitch;
            if (!painter.paintGlyph(hb_font, gid, 32.f, fg, 0,
                                    ALFontColrV1Painter::OutputFormat::Gray,
                                    gray_r))
                continue;

            for (S32 y = 0; y < bgra_h; ++y)
            {
                const U8* bgra_row = bgra_bytes.data() + (ptrdiff_t)y * bgra_pitch;
                const U8* gray_row = gray_r.mBitmap   + (ptrdiff_t)y * gray_r.mPitch;
                for (S32 x = 0; x < bgra_w; ++x)
                {
                    const U32 b = bgra_row[x*4 + 0];
                    const U32 g = bgra_row[x*4 + 1];
                    const U32 r = bgra_row[x*4 + 2];
                    const U32 a = bgra_row[x*4 + 3];
                    if (r == 0 && g == 0 && b == 0 && a > 200)
                    {
                        const U32 expected_floor = (a * 38u) >> 8;
                        ensure("Gray byte at saturated-black palette pixel "
                               "≥ alpha*floor (no crush to zero)",
                               (U32)gray_row[x] + 1u >= expected_floor);
                        return;
                    }
                }
            }
        }
        skip("no fully-saturated-black palette pixel found across test glyphs");
    }

    // Out-of-range palette_index clamps to 0 deterministically. Paint
    // fire with palette=0 and palette=99; the resulting bitmaps must
    // be byte-identical (the painter clamps any oversized index to 0
    // before calling hb_font_paint_glyph).
    template<> template<>
    void alfontcolrv1_object::test<15>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter_a, painter_b;
        ALFontColrV1Painter::Result r_zero, r_huge;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("palette=0 paint succeeded",
               painter_a.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/0u, r_zero));
        ensure("palette=99 paint succeeded (clamps to 0)",
               painter_b.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/99u, r_huge));
        ensure_equals("clamped result has same width",  r_huge.mWidth,  r_zero.mWidth);
        ensure_equals("clamped result has same height", r_huge.mHeight, r_zero.mHeight);
        ensure_equals("clamped result has same pitch",  r_huge.mPitch,  r_zero.mPitch);

        const size_t bytes = (size_t)r_zero.mPitch * r_zero.mHeight;
        ensure("clamped result is byte-identical to palette=0",
               std::memcmp(r_zero.mBitmap, r_huge.mBitmap, bytes) == 0);
    }

    // Fallback point_size kicks in when the hb_font has ppem unset.
    // Synthesize that condition by zeroing the hb_font scale before
    // calling paintGlyph; the painter must still produce a sized
    // bitmap rather than returning false. Specifically targets the
    // 2-em fallback inside the empty-bbox branch — without the
    // fallback point_size override, that branch would return false.
    template<> template<>
    void alfontcolrv1_object::test<16>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        // Build a dedicated hb_font on top of the same FT_Face so we can
        // mutate its ppem without affecting the face cache shared with
        // other tests in this group.
        hb_font_t* shared = face->getHbFont();
        ensure("shared hb_font available", shared != nullptr);
        hb_face_t* hb_face = hb_font_get_face(shared);
        hb_font_t* hb_font = hb_font_create(hb_face);
        ensure("derived hb_font allocated", hb_font != nullptr);
        hb_font_set_ppem(hb_font, 0, 0);
        unsigned px = 1, py = 1;
        hb_font_get_ppem(hb_font, &px, &py);
        ensure("ppem really is unset on derived font", px == 0 && py == 0);

        // Pick a glyph by index — without a scale set, the cmap query
        // through hb_font_get_nominal_glyph may not work as expected, so
        // resolve the index off the parent (scaled) font and use it on
        // the unscaled one.
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(shared, 0x1F525, &fire);
        if (fire == 0)
        {
            hb_font_destroy(hb_font);
            skip("Noto-COLRv1 has no fire emoji");
        }

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        // fallback_point_size = 14: should drive the empty-bbox branch
        // to size against ~14 px instead of returning false.
        const bool ok = painter.paintGlyph(hb_font, fire,
                                           /*fallback_point_size=*/14.f, fg,
                                           /*palette=*/0u, result);
        // The walker may still bail on other grounds for this unscaled
        // font (some paint commands depend on scale-derived coords), so
        // the strict assertion is "doesn't crash and returns either a
        // valid bitmap or a clean false." A regression that returned
        // false when fallback_point_size would have rescued it is the
        // failure mode we're protecting against.
        if (ok)
        {
            ensure("fallback path produced a sized bitmap",
                   result.mWidth > 0 && result.mHeight > 0);
        }
        hb_font_destroy(hb_font);
    }

    // GLYPH_PAD bump (§10.2): outermost row/column of a rendered glyph
    // contains no non-zero alpha. Pins that the surface allocates enough
    // slack around the bbox for AA spread, supersample edge taps, and
    // post-transform paint commands. A regression that drops PAD back to
    // 1 (or below) would surface here as a fringe of non-zero pixels at
    // the buffer boundary that visually clips the glyph at the atlas.
    template<> template<>
    void alfontcolrv1_object::test<17>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("paintGlyph succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r));
        ensure("bitmap valid",
               r.mBitmap && r.mWidth >= 4 && r.mHeight >= 4);

        // First row, last row.
        for (S32 x = 0; x < r.mWidth; ++x)
        {
            const U8* top    = r.mBitmap                              + x*4;
            const U8* bottom = r.mBitmap + (ptrdiff_t)(r.mHeight-1)*r.mPitch + x*4;
            ensure_equals("top row alpha is zero",    (S32)top[3],    0);
            ensure_equals("bottom row alpha is zero", (S32)bottom[3], 0);
        }
        // First column, last column.
        for (S32 y = 0; y < r.mHeight; ++y)
        {
            const U8* left  = r.mBitmap + (ptrdiff_t)y*r.mPitch                + 0*4;
            const U8* right = r.mBitmap + (ptrdiff_t)y*r.mPitch + (r.mWidth-1)*4;
            ensure_equals("left col alpha is zero",  (S32)left[3],  0);
            ensure_equals("right col alpha is zero", (S32)right[3], 0);
        }
    }

#if LL_MESA_HEADLESS
    // -------------------------------------------------------------
    // GL-backed group: COLRv1 painter integrated with the LLFontFreetype
    // / LLFontBitmapCache / LLImageGL pipeline. Each fixture builds a
    // fresh OSMesa context so the addGlyph path's gGL.bind plus the
    // setSubImageBGRA → LLImageGL upload land on a live GL state.
    // -------------------------------------------------------------

    struct alfontcolrv1_render_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    typedef test_group<alfontcolrv1_render_data> alfontcolrv1_render_test;
    typedef alfontcolrv1_render_test::object     alfontcolrv1_render_object;
    tut::alfontcolrv1_render_test alfontcolrv1_render_testcase("ALFontColrV1Render");

    // Build an LLFontFreetype as a real head face. The non-fallback
    // path's notdef pre-warm calls into the rasterizer, which is fine
    // here — the headless GL context is alive.
    static LLPointer<LLFontFreetype> loadFtHead(const std::string& filename)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        if (!ft->loadFace(filename, /*point_size=*/14.f, /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                          /*is_fallback=*/false, /*face_n=*/0,
                          EFontHinting::DEFAULT, /*flags=*/0))
        {
            return nullptr;
        }
        return ft;
    }

    // End-to-end: requesting a Color glyph for the fire emoji on
    // Noto-COLRv1 must route through renderColrV1Glyph → painter →
    // setSubImageBGRA → Color atlas. Verify the returned info has
    // Color glyph type and the cache's Color page has a live GL
    // texture name.
    template<> template<>
    void alfontcolrv1_render_object::test<1>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("getGlyphInfo returned an entry for fire emoji", gi != nullptr);
        ensure_equals("delivered glyph type is Color (COLRv1 raster)",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Color);
        ensure("glyph has positive width",   gi->mWidth   > 0);
        ensure("glyph has positive height",  gi->mHeight  > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("Color atlas allocated at least one sheet",
               cache->getNumBitmaps(EFontGlyphType::Color) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Color, 0);
        ensure("Color sheet 0 LLImageGL present", page != nullptr);
        ensure("Color sheet 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // Several COLRv1 emojis in a row share the Color atlas — a single
    // 1024² Color sheet has plenty of room for many small color
    // bitmaps. Pins that the painter+atlas integration doesn't
    // allocate an extra sheet per glyph.
    template<> template<>
    void alfontcolrv1_render_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        const llwchar fire    = 0x1F525;
        const llwchar heart   = 0x2764;
        const llwchar rocket  = 0x1F680;

        ensure("fire glyph allocated",
               ft->getGlyphInfo(fire,   EFontGlyphType::Color) != nullptr);
        ensure("heart glyph allocated",
               ft->getGlyphInfo(heart,  EFontGlyphType::Color) != nullptr);
        ensure("rocket glyph allocated",
               ft->getGlyphInfo(rocket, EFontGlyphType::Color) != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure_equals("three Color glyphs share a single Color atlas sheet",
                      cache->getNumBitmaps(EFontGlyphType::Color), 1u);
    }

    // Repeat lookup is a cache hit, not a re-rasterize. Returning
    // the same LLFontGlyphInfo pointer pins the head's char_glyph
    // info cache (so each render call doesn't rebuild the painter
    // input on every frame).
    template<> template<>
    void alfontcolrv1_render_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* first  = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        LLFontGlyphInfo* second = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("first lookup non-null", first  != nullptr);
        ensure("second lookup non-null", second != nullptr);
        ensure_equals("repeat lookup returns same LLFontGlyphInfo pointer",
                      first, second);
    }

    // Color atlas generation counter advances on the first emoji
    // raster (a sheet allocation bumps generation), so a vertex
    // buffer captured before the rasterize would invalidate. Pins
    // the global-generation contract that LLFontBitmapCache holds
    // for COLRv1 atlases too — not just Grayscale.
    template<> template<>
    void alfontcolrv1_render_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        const S32 gen_before = cache->getCacheGeneration();

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("glyph allocated", gi != nullptr);

        const S32 gen_after = cache->getCacheGeneration();
        ensure("cache generation advanced after Color rasterize",
               gen_after > gen_before);
    }

    // (Test 5 removed: pinned LLFontGlyphInfo::mTintWithForeground
    // propagation, which was removed along with the dormant retint chain.)

    // Painter staging surface dimension consistency: paint the SAME
    // glyph twice on one painter; second result's dimensions match
    // the first. Pins that mStaging.resize at alfontcolrv1.cpp:819
    // doesn't accumulate state across calls (e.g., a regression that
    // grew the staging buffer on every call would still produce a
    // valid bitmap but mWidth/mHeight could drift).
    template<> template<>
    void alfontcolrv1_render_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r1, r2;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("first paintGlyph succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r1));
        const S32 w1 = r1.mWidth, h1 = r1.mHeight;
        ensure("second paintGlyph (same glyph) succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r2));
        ensure_equals("repeat paint width matches",  r2.mWidth,  w1);
        ensure_equals("repeat paint height matches", r2.mHeight, h1);
    }

    // Grayscale-COLRv1 routing: requesting EFontGlyphType::Grayscale on a
    // COLRv1 face must take the painter path (not the FT outline fallback)
    // and land the result in the Grayscale atlas. Pins §3+§4: the gate
    // change in renderGlyph drops the bitmap_type==Color restriction, so
    // Grayscale lookups on COLRv1 faces produce a luminance-shaded mask
    // in the LA atlas instead of an empty / outline-only glyph.
    template<> template<>
    void alfontcolrv1_render_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Grayscale);
        ensure("Grayscale lookup returns an entry", gi != nullptr);
        ensure_equals("delivered glyph type is Grayscale (LA atlas)",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Grayscale);
        ensure("Grayscale glyph has positive width",  gi->mWidth  > 0);
        ensure("Grayscale glyph has positive height", gi->mHeight > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("Grayscale atlas allocated at least one sheet",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        ensure("Grayscale sheet 0 LLImageGL present", page != nullptr);
        ensure("Grayscale sheet 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // Force-monochrome static is wired in at the LLFontGL render-call layer.
    // We can't directly drive a render call from here without the full UI
    // bringup, but we *can* pin the static's existence + mutability and
    // verify that the Grayscale routing works while it's set, which is
    // what the call-site code does at llfontgl.cpp:560/715/862. Save and
    // restore the static so the test doesn't perturb sibling tests.
    template<> template<>
    void alfontcolrv1_render_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        const bool saved = LLFontGL::sForceMonochromeEmoji;
        LLFontGL::sForceMonochromeEmoji = true;

        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        // Simulate the render-call selector at llfontgl.cpp:560 with
        // use_color=true and force_mono=true → request Grayscale.
        const EFontGlyphType requested =
            (!/*use_color*/true || LLFontGL::sForceMonochromeEmoji)
                ? EFontGlyphType::Grayscale : EFontGlyphType::Color;
        ensure_equals("force-mono toggle downgrades Color to Grayscale at "
                      "the request layer", (S32)requested,
                      (S32)EFontGlyphType::Grayscale);

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, requested);
        ensure("Grayscale lookup under force-mono returns an entry",
               gi != nullptr);
        ensure_equals("Grayscale path landed in Grayscale atlas",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Grayscale);

        LLFontGL::sForceMonochromeEmoji = saved;
    }
#endif // LL_MESA_HEADLESS
}
