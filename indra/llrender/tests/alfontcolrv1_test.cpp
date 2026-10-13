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

#include "llfonttest_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <cstring>
#include <memory>
#include <set>
#include <vector>

namespace
{
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

namespace
{
    using namespace ll_test;

    // Each case gets a fresh fixture, so init/cleanup the font manager
    // once per test rather than once per process. Cheaper than tracking
    // init state and keeps each case isolated from the others.
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


    // Painter input validation: null hb_font fails cleanly without a crash.
    // Doesn't need a real font, so this test runs even when the test data
    // dir is empty (e.g., a stripped CI checkout).
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: paintGlyph with a null hb_font returns false and an empty result", "[ALFontColrV1]")
    {
        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        CHECK_FALSE(painter.paintGlyph(nullptr, /*glyph_index=*/0,
                                   /*point_size=*/14.f, fg,
                                   /*palette_index=*/0, result));
        CHECK(result.mBitmap == nullptr);
        CHECK(result.mWidth == 0);
    }

    // hasColrV1 is the in-tree probe for "FreeType's FT_LOAD_COLOR can't
    // rasterise this — route through the paint walker." Verify it fires
    // for Noto-COLRv1.ttf, which ships a COLRv1 paint table.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: hasColrV1 and hasColor are true for Noto-COLRv1", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        CHECK(face->hasColrV1());
        CHECK(face->hasColor());
    }

    // Negative: InterVariable.woff2 is a plain text font with no COLR
    // table, so hasColrV1 must stay false. Guards against the probe
    // false-positiving on any FT_HAS_COLOR-y face.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: hasColrV1 is false for a plain text face with no COLR table", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
        {
            SKIP("InterVariable.woff2 not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        CHECK_FALSE(face->hasColrV1());
    }

    // Painter end-to-end: feed a known emoji glyph (fire, U+1F525) through
    // hb_font_paint_glyph and verify the resulting BGRA buffer has at least
    // one non-zero-alpha pixel. Also exercises the FT_Get_Color_Glyph_ClipBox
    // path (Noto-COLRv1 ships clip boxes for its glyphs) and the BGRA atlas
    // packing logic in paintGlyph.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: paintGlyph paints the fire emoji into a BGRA bitmap with some alpha", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        CHECK(face->hasColrV1());

        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);

        // Resolve the fire emoji's glyph index via the cmap. Avoids hardcoding
        // a glyph_id that could shift across font versions.
        const hb_codepoint_t fire_cp = 0x1F525;
        hb_codepoint_t       fire_gid = 0;
        CHECK(hb_font_get_nominal_glyph(hb_font, fire_cp, &fire_gid));
        CHECK(fire_gid != 0);

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        const bool ok = painter.paintGlyph(hb_font, fire_gid, /*point_size=*/14.f,
                                           fg, /*palette_index=*/0, result);
        CHECK(ok);
        REQUIRE(result.mBitmap != nullptr);
        CHECK(result.mWidth > 0);
        CHECK(result.mHeight > 0);
        CHECK(result.mPitch != 0);
        CHECK(anyAlpha(result.mBitmap, result.mWidth, result.mHeight, result.mPitch));
    }

    // VS-16 cmap absence is the trigger for shape_sub_run's stripping path.
    // Verify Noto-COLRv1 actually has the shape we expect: ZWJ in cmap but
    // VS-16 not. Other emoji fonts that ship VS-16 (e.g. Twemoji) take the
    // no-strip path; the shape pipeline branches on this flag per-face.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: Noto-COLRv1's cmap has ZWJ but no VS-16", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());

        // ZWJ should resolve to a real glyph (cmap entry present).
        CHECK(face->getCharGlyphIndex(0x200D) != 0u);
        // VS-16 should NOT — shape_sub_run keys VS-16 stripping on this.
        CHECK(face->getCharGlyphIndex(0xFE0F) == 0u);
    }

    // paintGlyph on an out-of-range glyph index (beyond the font's
    // num_glyphs) must fail cleanly. The painter calls
    // hb_font_paint_glyph which returns 0 for unknown glyphs; the
    // wrapper turns that into a Result with mBitmap=null and
    // mWidth=0.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: paintGlyph fails cleanly on a glyph index past the font's glyphs", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);

        // Use a clearly out-of-range glyph index. Real fonts have
        // num_glyphs in the thousands; 0xFFFFFFFE is well beyond.
        const bool ok = painter.paintGlyph(hb_font,
                                           /*glyph_index=*/0xFFFFFFFE,
                                           14.f, fg, 0, result);
        CHECK_FALSE(ok);
        CHECK(result.mBitmap == nullptr);
        CHECK(result.mWidth == 0);
    }

    // Painter staging surface reuse: paint two different emoji
    // through one painter. Both calls succeed and the second
    // call's result must reflect the second glyph (not the first
    // — a staging-buffer leak would surface as the second result
    // pointing at stale first-glyph bytes when dimensions match).
    // The reference is the second glyph painted by a painter that
    // never painted the first.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: one painter paints a second emoji exactly as a fresh painter does", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);

        // Two distinct emoji: fire (U+1F525) and red heart (U+2764).
        // Both should rasterize cleanly through Noto-COLRv1's paint
        // walker. Their surfaces differ in size (different glyphs
        // render to different surfaces), so the second result is
        // compared with the heart painted alone, not with the fire.
        const LLColor4U fg(255, 255, 255, 255);

        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        hb_codepoint_t heart = 0;
        hb_font_get_nominal_glyph(hb_font, 0x2764, &heart);
        if (fire == 0 || heart == 0)
            SKIP("Noto-COLRv1 missing one of the test emoji codepoints");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r1, r2;
        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r1));
        REQUIRE(r1.mBitmap != nullptr);
        CHECK(r1.mWidth > 0);
        CHECK(r1.mHeight > 0);

        REQUIRE(painter.paintGlyph(hb_font, heart, 14.f, fg, 0, r2));
        REQUIRE(r2.mBitmap != nullptr);
        CHECK(r2.mWidth > 0);
        CHECK(r2.mHeight > 0);

        ALFontColrV1Painter fresh;
        ALFontColrV1Painter::Result ref;
        REQUIRE(fresh.paintGlyph(hb_font, heart, 14.f, fg, 0, ref));
        REQUIRE(ref.mBitmap != nullptr);
        CHECK(r2.mWidth == ref.mWidth);
        // The memcmp below reads r2 for ref's pitch times height.
        REQUIRE(r2.mHeight == ref.mHeight);
        REQUIRE(r2.mPitch == ref.mPitch);
        UNSCOPED_INFO("the second paint holds the heart, byte for byte");
        CHECK(std::memcmp(r2.mBitmap, ref.mBitmap, (size_t)ref.mPitch * ref.mHeight) == 0);
    }

    // 2-em-square surface fallback (88585ff86d): a glyph with no
    // ClipBox AND no FT extents lands the painter on a synthetic
    // 2-em surface so it still produces SOMETHING rather than
    // returning a 0×0 bitmap. Noto-COLRv1 gives every emoji extents,
    // so this case cannot reach the fallback; the no-scale case below
    // drives it by taking the font's scale away. This one pins the
    // path that runs when extents work: the surface size is on the
    // order of (point_size, point_size) rather than 0×0.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: the fire emoji's surface is sized on the order of the point size", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
        {
            SKIP("Noto-COLRv1.ttf not present in test data dir");
        }

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        CHECK(face->isValid());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);

        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        constexpr F32 point_size = 14.f;
        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, point_size, fg, 0, result));
        // Surface dimensions are on the order of point_size — the
        // ClipBox / FT-extents path produces a bitmap whose width and
        // height are within a small multiple of the requested point
        // size. A regression where the path returned 0×0 would fail
        // the lower bound; a regression that ballooned to 2× the
        // 2-em square (32×32 fallback for 14pt) would fail the upper
        // bound (3× point_size).
        CHECK(result.mWidth >= 4);
        CHECK(result.mWidth <= (S32)(point_size * 3.f));
        CHECK(result.mHeight >= 4);
        CHECK(result.mHeight <= (S32)(point_size * 3.f));
    }

    // (Test 9 removed: pinned the mForegroundOnly flag on the painter
    // Result, which was removed along with the dormant retint chain.)

    // Gradient color variance: paint fire emoji and walk the BGRA bitmap.
    // Among non-zero-alpha pixels, count distinct (b, g, r) triples. A
    // regression where interp_stops returned `front()` always (or the
    // gradient sampling used the same color for every offset) would
    // produce a single solid color → 1 distinct triple. Real gradient
    // emojis show many colors. Pin >= 3.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: the fire emoji's gradients paint at least three distinct colours", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        // Use a larger point size so the rendered bitmap has enough
        // pixels to sample distinct gradient stops without aliasing
        // them away.
        constexpr F32 ps = 32.f;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, ps, fg, 0, result));
        REQUIRE(result.mBitmap != nullptr);
        CHECK(result.mWidth > 0);
        CHECK(result.mHeight > 0);

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
        CHECK(distinct.size() >= 3);
    }

    // palette_index argument propagates to hb_font_paint_glyph: paint
    // the fire emoji once with palette 0 and once with palette 1. If
    // Noto-COLRv1 ships >= 2 CPAL palettes, the resulting bytes must
    // differ at non-zero-alpha positions. Skip if only one palette is
    // present: the painter clamps palette 1 to 0 then, so the two paints
    // match and say nothing.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: palette_index reaches hb_font_paint_glyph: palette 1 paints differently", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");
        if (hb_ot_color_palette_get_count(hb_font_get_face(hb_font)) < 2)
            SKIP("Noto-COLRv1 ships only one CPAL palette");

        const LLColor4U fg(255, 255, 255, 255);

        ALFontColrV1Painter painter_a, painter_b;
        ALFontColrV1Painter::Result r0, r1;
        REQUIRE(painter_a.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/0, r0));
        REQUIRE(painter_b.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/1, r1));
        REQUIRE(r0.mBitmap);
        REQUIRE(r1.mBitmap);
        // The comparison below walks r1 at r0's size.
        REQUIRE(r0.mWidth == r1.mWidth);
        REQUIRE(r0.mHeight == r1.mHeight);

        // Compare bytes — palette 1 exists, so the two paints must differ
        // somewhere either of them covers.
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
        CHECK(any_diff);
    }

    // OutputFormat::Gray basics: format selector hits the gray fold path
    // and returns a single-channel buffer with mPitch == width and
    // non-zero coverage somewhere in the bitmap. Pins the §1/§2 contract
    // that Gray output is single-channel, top-row first, tightly packed.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: OutputFormat::Gray paints a tightly packed one-channel bitmap", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, /*point_size=*/14.f, fg,
                                  /*palette=*/0,
                                  ALFontColrV1Painter::OutputFormat::Gray,
                                  result));
        REQUIRE(result.mBitmap != nullptr);
        CHECK((S32)result.mFormat == (S32)ALFontColrV1Painter::OutputFormat::Gray);
        CHECK(result.mWidth > 0);
        CHECK(result.mHeight > 0);
        CHECK(result.mPitch == result.mWidth);

        bool any_nonzero = false;
        for (S32 y = 0; y < result.mHeight && !any_nonzero; ++y)
        {
            const U8* row = result.mBitmap + (ptrdiff_t)y * result.mPitch;
            for (S32 x = 0; x < result.mWidth; ++x)
            {
                if (row[x] != 0) { any_nonzero = true; break; }
            }
        }
        CHECK(any_nonzero);
    }

    // Gray-fold formula pin: paint the same glyph in BGRA and Gray and
    // verify each Gray byte equals max(Rec.709(BGRA.rgb_premul),
    // BGRA.alpha * 0.15) within ±1 (rounding tolerance). Locks the
    // hybrid floor + luminance projection from §2 against silent drift.
    // For pixels that happen to be fg-only (white premul: R=G=B=A) this
    // also verifies the foreground-collapses-to-alpha invariant since
    // Y == A and Y > A*0.15 holds, so the test is a strict superset of
    // the "fg-only collapses to alpha" case.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: each Gray byte is the hybrid luminance fold of the BGRA paint", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result rgba_r, gray_r;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0, rgba_r));
        REQUIRE(rgba_r.mBitmap != nullptr);
        // Stash the BGRA bytes — the next paintGlyph call invalidates
        // mBitmap (it points back into mStaging which we overwrite below).
        std::vector<U8> bgra_bytes(rgba_r.mBitmap,
                                   rgba_r.mBitmap + (size_t)rgba_r.mPitch * rgba_r.mHeight);
        const S32 bgra_w = rgba_r.mWidth, bgra_h = rgba_r.mHeight;
        const S32 bgra_pitch = rgba_r.mPitch;

        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0,
                                  ALFontColrV1Painter::OutputFormat::Gray,
                                  gray_r));
        REQUIRE(gray_r.mBitmap != nullptr);
        // The comparison below walks the Gray bitmap at the BGRA size.
        REQUIRE(gray_r.mWidth == bgra_w);
        REQUIRE(gray_r.mHeight == bgra_h);

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
        CHECK(mismatches == 0);
    }

    // Hybrid floor pin: scan the BGRA result for fully-saturated palette
    // black pixels (R=G=B=0, A>200) and assert every corresponding Gray
    // byte is >= floor (≈ 38 * a / 256) rather than zero. If no such
    // pixel exists across the test glyphs, skip — the floor invariant
    // still holds, just isn't observable here. The dark sunglasses'
    // lenses are palette black, so Noto-COLRv1 has some. The point of
    // the test is to lock the constant against future tweaks that would
    // silently crush dark CPAL details.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: Gray keeps an alpha floor under saturated black palette pixels", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);

        // Sweep a handful of likely candidates: emoji that commonly carry
        // black eye/outline detail in Noto's palette. Every
        // fully-saturated black pixel of every one is checked; if none
        // has one, the test skips.
        const hb_codepoint_t cps[] = { 0x1F600 /*grinning face*/,
                                       0x1F602 /*face with tears of joy*/,
                                       0x1F525 /*fire*/,
                                       0x2764  /*red heart*/,
                                       0x1F576 /*dark sunglasses*/ };
        S32 black = 0;            // fully-saturated black pixels seen
        S32 under_floor = 0;      // of those, Gray bytes below the floor
        hb_codepoint_t first_cp = 0;
        S32 first_x = -1, first_y = -1;
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
            REQUIRE(rgba_r.mBitmap != nullptr);
            std::vector<U8> bgra_bytes(rgba_r.mBitmap,
                                       rgba_r.mBitmap + (size_t)rgba_r.mPitch * rgba_r.mHeight);
            const S32 bgra_w = rgba_r.mWidth, bgra_h = rgba_r.mHeight;
            const S32 bgra_pitch = rgba_r.mPitch;
            if (!painter.paintGlyph(hb_font, gid, 32.f, fg, 0,
                                    ALFontColrV1Painter::OutputFormat::Gray,
                                    gray_r))
                continue;
            REQUIRE(gray_r.mBitmap != nullptr);
            // The scan below walks the Gray bitmap at the BGRA size.
            CAPTURE(cp);
            REQUIRE(gray_r.mWidth == bgra_w);
            REQUIRE(gray_r.mHeight == bgra_h);

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
                        ++black;
                        const U32 expected_floor = (a * 38u) >> 8;
                        if ((U32)gray_row[x] + 1u < expected_floor && under_floor++ == 0)
                        {
                            first_cp = cp;
                            first_x  = x;
                            first_y  = y;
                        }
                    }
                }
            }
        }
        if (black == 0)
            SKIP("no fully-saturated-black palette pixel found across test glyphs");
        // The first pixel under the floor, if any.
        CAPTURE(black, first_cp, first_x, first_y);
        CHECK(under_floor == 0);
    }

    // Out-of-range palette_index clamps to 0 deterministically. Paint
    // fire with palette=0 and palette=99; the resulting bitmaps must
    // be byte-identical (the painter clamps any oversized index to 0
    // before calling hb_font_paint_glyph).
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: an out-of-range palette_index paints byte for byte as palette 0", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter_a, painter_b;
        ALFontColrV1Painter::Result r_zero, r_huge;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter_a.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/0u, r_zero));
        REQUIRE(painter_b.paintGlyph(hb_font, fire, 14.f, fg, /*palette=*/99u, r_huge));
        REQUIRE(r_zero.mBitmap != nullptr);
        REQUIRE(r_huge.mBitmap != nullptr);
        CHECK(r_huge.mWidth == r_zero.mWidth);
        // The memcmp below reads r_huge for r_zero's pitch times height.
        REQUIRE(r_huge.mHeight == r_zero.mHeight);
        REQUIRE(r_huge.mPitch == r_zero.mPitch);

        const size_t bytes = (size_t)r_zero.mPitch * r_zero.mHeight;
        CHECK(std::memcmp(r_zero.mBitmap, r_huge.mBitmap, bytes) == 0);
    }

    // Fallback point_size kicks in when the hb_font has ppem unset and
    // gives the glyph no extents. Synthesize that condition by zeroing
    // the hb_font scale before calling paintGlyph (a fresh hb_font has
    // no ppem already); the painter must still produce a bitmap sized
    // from the fallback rather than returning false. Specifically
    // targets the 2-em fallback inside the empty-bbox branch — without
    // the fallback point_size override, that branch would return false.
    // Nothing is painted at scale 0; the surface is what is checked.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: with no scale or ppem paintGlyph sizes 2 em of the fallback point size", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        // Build a dedicated hb_font on top of the same hb_face so we can
        // mutate its scale without affecting the face cache shared with
        // other tests in this group. Destroyed however the case ends.
        hb_font_t* shared = face->getHbFont();
        REQUIRE(shared != nullptr);
        hb_face_t* hb_face = hb_font_get_face(shared);
        const std::unique_ptr<hb_font_t, void (*)(hb_font_t*)> owned(hb_font_create(hb_face), hb_font_destroy);
        hb_font_t* hb_font = owned.get();
        REQUIRE(hb_font != nullptr);
        hb_font_set_scale(hb_font, 0, 0);
        unsigned px = 1, py = 1;
        hb_font_get_ppem(hb_font, &px, &py);
        CHECK(px == 0);
        CHECK(py == 0);

        // Pick a glyph by index — without a scale set, the cmap query
        // through hb_font_get_nominal_glyph may not work as expected, so
        // resolve the index off the parent (scaled) font and use it on
        // the unscaled one.
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(shared, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result result;
        const LLColor4U fg(255, 255, 255, 255);
        // fallback_point_size = 14: drives the empty-bbox branch to size
        // against 14 px instead of returning false.
        constexpr F32 fallback_point_size = 14.f;
        REQUIRE(painter.paintGlyph(hb_font, fire, fallback_point_size, fg,
                                   /*palette=*/0u, result));
        // Two ems of the fallback size, and the painter's 2 px pad on
        // every side.
        constexpr S32 expected = 2 * (S32)fallback_point_size + 2 * 2;
        CHECK(result.mWidth == expected);
        CHECK(result.mHeight == expected);
    }

    // GLYPH_PAD bump (§10.2): outermost row/column of a rendered glyph
    // contains no non-zero alpha. Pins that the surface allocates enough
    // slack around the bbox for AA spread, supersample edge taps, and
    // post-transform paint commands. A regression that drops PAD back to
    // 1 (or below) would surface here as a fringe of non-zero pixels at
    // the buffer boundary that visually clips the glyph at the atlas.
    TEST_CASE_METHOD(alfontcolrv1_data, "ALFontColrV1: a painted glyph's outermost rows and columns have zero alpha", "[ALFontColrV1]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r));
        REQUIRE(r.mBitmap);
        // The loops below read row mHeight-1 and column mWidth-1.
        REQUIRE(r.mWidth >= 4);
        REQUIRE(r.mHeight >= 4);

        // First row, last row.
        for (S32 x = 0; x < r.mWidth; ++x)
        {
            const U8* top    = r.mBitmap                              + x*4;
            const U8* bottom = r.mBitmap + (ptrdiff_t)(r.mHeight-1)*r.mPitch + x*4;
            CAPTURE(x);
            REQUIRE((S32)top[3] == 0);
            REQUIRE((S32)bottom[3] == 0);
        }
        // First column, last column.
        for (S32 y = 0; y < r.mHeight; ++y)
        {
            const U8* left  = r.mBitmap + (ptrdiff_t)y*r.mPitch                + 0*4;
            const U8* right = r.mBitmap + (ptrdiff_t)y*r.mPitch + (r.mWidth-1)*4;
            CAPTURE(y);
            REQUIRE((S32)left[3] == 0);
            REQUIRE((S32)right[3] == 0);
        }
    }
}
