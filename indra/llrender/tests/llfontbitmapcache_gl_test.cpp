/**
 * @file llfontbitmapcache_gl_test.cpp
 * @brief LLFontBitmapCache against a GL context: nextOpenPos and
 *        releaseSheet, which allocate and free atlas textures.
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

#include "../llfontbitmapcache.h"
#include "../llimagegl.h"
#include "llimage.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using namespace ll_test;

    // GL-backed fixture: a HeadlessGL of each test's own, so every
    // test below has a live GL context available for the
    // gGL.bind call inside nextOpenPos and the destroyGLTexture call
    // inside releaseSheet.
    struct llfontbitmapcache_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
    };


    // After a Grayscale allocation, the underlying LLImageRaw must
    // carry 2 components (R+G with R=255 stem, G=alpha) — pins
    // c99dcaa7d3 which restored the greyscale atlas to 2 components
    // after a regression had it at 1.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a Grayscale sheet has 2 components", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        CHECK(c.nextOpenPos(/*width=*/8, /*height=*/8, px, py, EFontGlyphType::Grayscale, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Grayscale, sheet);
        REQUIRE(raw != nullptr);
        CHECK((S32)raw->getComponents() == 2);
    }

    // Color allocation must produce a 4-component (RGBA) raw — pins
    // the same array-init bug from c99dcaa7d3 in its Color form.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a Color sheet has 4 components", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        CHECK(c.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Color, sheet);
        REQUIRE(raw != nullptr);
        CHECK((S32)raw->getComponents() == 4);
    }

    // The very first nextOpenPos on a fresh cache must yield (4, 4) —
    // pins the mCurrentOffsetX/Y = {4, 4} array-init in the header
    // (an earlier `= { 4 }` form was zero-initing index [1], which
    // started Color atlases at offset 0).
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: the first Grayscale and Color allocations start past the 4px border", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = -1, py = -1; U32 sheet = 99;
        CHECK(c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet));
        CHECK(px == 4);
        CHECK(py == 4);
        CHECK((S32)sheet == 0);

        // Repeat for Color — a fresh cache, fresh allocation. The bug
        // would have started Color at posX=0; the fix shows posX=4.
        LLFontBitmapCache c2;
        c2.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        CHECK(c2.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Color, sheet2));
        CHECK(px2 == 4);
        CHECK(py2 == 4);
    }

    // Sequential allocations within the same row advance posX by
    // width + 4 (4-pixel border). Tests the row-packing pen advance
    // at llfontbitmapcache.cpp:201.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: allocations in a row advance x by the width and the 4px border", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(10, 8, px, py, EFontGlyphType::Grayscale, sheet);
        CHECK(px == 4);
        CHECK(py == 4);

        c.nextOpenPos(10, 8, px, py, EFontGlyphType::Grayscale, sheet);
        // Expected: 4 (initial border) + 10 (first width) + 4 (border)
        UNSCOPED_INFO("the second allocation advances by width + 4");
        CHECK(px == 18);
        UNSCOPED_INFO("in the same row");
        CHECK(py == 4);
    }

    // When the next allocation would exceed mBitmapWidth, the row
    // wraps. posX resets to 4; posY advances by the tallest glyph
    // placed in the prior row (mCurrentRowMaxHeight, not mMaxCharHeight).
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a full row wraps below the tallest glyph placed in it", "[LLFontBitmapCacheGL]")
    {
        // 128x128 atlas. Two 50px-wide glyphs fit in row 0
        // (4 + 50 + 4 + 50 + 4 = 112 ≤ 128); third forces wrap.
        LLFontBitmapCache c;
        c.init(2, 2);
        CHECK(c.getBitmapWidth() == 128);

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 0
        c.nextOpenPos(50, 20, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 1
        UNSCOPED_INFO("the second allocation is still in row 0");
        CHECK(py == 4);

        c.nextOpenPos(50, 8, px, py, EFontGlyphType::Grayscale, sheet);  // wraps to row 1
        UNSCOPED_INFO("the third allocation wraps");
        CHECK(px == 4);
        // Tallest in row 0 was 30, so row 1 starts at 4 + 30 + 4 = 38.
        UNSCOPED_INFO("below the tallest glyph of row 0");
        CHECK(py == 38);
        UNSCOPED_INFO("a row wrap and not a new sheet");
        CHECK((S32)sheet == 0);
    }

    // When a row wrap would push posY past mBitmapHeight, the cache
    // must allocate a fresh sheet rather than write past the atlas.
    // Verify by repeatedly forcing wraps until the next would overflow.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a row wrap that would pass the bottom starts a new sheet", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        CHECK(c.getBitmapWidth() == 128);
        CHECK(c.getBitmapHeight() == 128);

        // Glyphs 50 wide, 30 tall. 2 per row (positions 4 and 58),
        // 3 rows (Y = 4, 38, 72) before next would land at Y=106
        // with bottom 106+30+4=140 > 128 — that triggers sheet rollover.
        S32 px = 0, py = 0; U32 sheet = 0;
        for (int i = 0; i < 6; ++i)
        {
            c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        }
        UNSCOPED_INFO("six allocations fill three rows of sheet 0");
        CHECK((S32)sheet == 0);
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);

        // 7th glyph: Y advance to 4+30+4+30+4+30+4=106, bottom would
        // be 106+30+4=140 > 128 → new sheet. Either the row-wrap path
        // (line 125) or the vertical-fit recheck (line 144) fires.
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        UNSCOPED_INFO("the seventh allocation goes to a new sheet");
        CHECK((S32)sheet == 1);
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 2u);
        {
            INFO("the new sheet's first position");
            CHECK(px == 4);
            CHECK(py == 4);
        }
    }

    // Per-type isolation: a Grayscale allocation must not move the
    // Color pen, and vice versa. Each type owns its own X/Y/row-max
    // state at index [bitmap_idx].
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: Grayscale and Color pens and sheets are independent", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        // Push the Grayscale pen forward.
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        // Color must still be at the (4, 4) start.
        S32 cpx = 0, cpy = 0; U32 csheet = 99;
        c.nextOpenPos(8, 8, cpx, cpy, EFontGlyphType::Color, csheet);
        {
            INFO("the Color pen after two Grayscale allocations");
            CHECK(cpx == 4);
            CHECK(cpy == 4);
        }
        CHECK((S32)csheet == 0);
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        CHECK(c.getNumBitmaps(EFontGlyphType::Color) == 1u);
    }

    // Generation counter must strictly increase on every allocation
    // and on releaseSheet. Vertex buffers compare against a captured
    // generation to invalidate; equal-or-decreasing generations would
    // mask the invalidation.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: the generation rises on allocation and release and reset", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        const S32 g0 = c.getCacheGeneration();

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        const S32 g1 = c.getCacheGeneration();
        CHECK(g1 > g0);

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);
        const S32 g2 = c.getCacheGeneration();
        CHECK(g2 > g1);

        c.reset();
        const S32 g3 = c.getCacheGeneration();
        CHECK(g3 > g2);
    }

    // releaseSheet drops the LLImageRaw + LLImageGL but keeps the
    // slot index alive (as nullptr) so existing index references stay
    // valid. isSheetReleased flips true; getSheetLastUsedTime returns 0.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: releaseSheet frees the images and keeps the slot", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);

        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        // Don't assert getSheetLastUsedTime is > 0 here — it samples
        // LLFrameTimer::getTotalSeconds() and the test environment may
        // not have ticked a frame yet, so the wall-clock can legitimately
        // be 0.0. The release-time clearing below is the load-bearing
        // contract; the timestamp tracking itself is exercised
        // implicitly via touchSheet on every nextOpenPos call.

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);

        CHECK(c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        CHECK(c.getImageRaw(EFontGlyphType::Grayscale, sheet) == nullptr);
        UNSCOPED_INFO("a released sheet's last-used time is cleared");
        CHECK(c.getSheetLastUsedTime(EFontGlyphType::Grayscale, sheet) == 0.0);
        UNSCOPED_INFO("the slot is kept");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
    }

    // After releasing the active sheet, the next nextOpenPos must build a
    // fresh image rather than write into the freed one — and it recycles
    // the released slot instead of growing the sheet vector. The purge-
    // before-release contract guarantees nothing references the released
    // index, so reuse is safe and bounds slot growth across eviction
    // cycles. Pins the active_sheet_released check + slot recycling in
    // nextOpenPos.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: an allocation after the active sheet's release rebuilds it in that slot", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        c.releaseSheet(EFontGlyphType::Grayscale, sheet);

        S32 px2 = 0, py2 = 0; U32 sheet2 = 99;
        CHECK(c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2));
        UNSCOPED_INFO("the released slot is recycled");
        CHECK((S32)sheet2 == 0);
        {
            INFO("the new sheet's first position");
            CHECK(px2 == 4);
            CHECK(py2 == 4);
        }
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
    }

    // reset() after allocations clears all state — no live sheets,
    // pens back at 4, and a subsequent nextOpenPos starts from (4, 4)
    // on a fresh sheet.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: reset clears every sheet and the pen starts over", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        UNSCOPED_INFO("two glyphs on one sheet before reset");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);

        c.reset();
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 0u);
        CHECK(c.getNumBitmaps(EFontGlyphType::Color) == 0u);

        // Re-init and verify pens really did reset back to (4,4).
        c.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2);
        CHECK(px2 == 4);
        CHECK(py2 == 4);
    }

    // Multi-cycle release+re-alloc: alloc, release, alloc, release, alloc
    // keeps recycling slot 0 — the slot vector never grows across eviction
    // cycles, and the generation counter still advances every cycle so
    // captured vertex buffers rebuild. Pins bounded slot growth under
    // repeat cycles.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: repeated release and allocation keeps recycling slot 0", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 s0 = 0, s1 = 99, s2 = 99;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s0);
        CHECK((S32)s0 == 0);
        const S32 gen0 = c.getCacheGeneration();
        c.releaseSheet(EFontGlyphType::Grayscale, s0);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s1);
        CHECK((S32)s1 == 0);
        const S32 gen1 = c.getCacheGeneration();
        CHECK(gen1 > gen0);
        c.releaseSheet(EFontGlyphType::Grayscale, s1);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s2);
        CHECK((S32)s2 == 0);
        CHECK(c.getCacheGeneration() > gen1);

        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

    // A live active sheet with room keeps absorbing allocations — a
    // release+recycle cycle doesn't perturb the pen, and no spurious
    // sheets are created while the active sheet has space. Pins the
    // active-sheet tracking (mCurrentSheet) staying put across allocs.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a recycled active sheet keeps taking allocations while it has room", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 s0 = 0, s1 = 99;

        // Alloc sheet 0, release it, alloc → recycled slot 0 (live).
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s0);
        c.releaseSheet(EFontGlyphType::Grayscale, s0);
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s1);
        CHECK((S32)s1 == 0);

        // Further allocs must keep landing on the recycled sheet (it has
        // plenty of room — only one 8x8 glyph placed since the rebuild).
        for (int i = 0; i < 4; ++i)
        {
            U32 si = 99;
            c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, si);
            UNSCOPED_INFO("a later allocation on the recycled sheet");
            REQUIRE((S32)si == 0);
        }
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

    // Cross-instance global generation: two LLFontBitmapCache instances
    // mutating in interleaved order each bump the global counter. Every
    // cache draws its generation from it, and LLFontGL::getCacheGeneration
    // keys its memo on it, so a change to any cache of a font's chain has
    // to move it. (Vertex buffers no longer compare against it directly:
    // llfontbitmapcache.h:84-92.)
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: two caches' allocations and releases each bump the global generation", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c1; c1.init(2, 2);
        LLFontBitmapCache c2; c2.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;

        const S32 g0 = LLFontBitmapCache::getGlobalGeneration();
        c1.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        const S32 g1 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after c1 allocated");
        CHECK(g1 > g0);

        c2.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet);
        const S32 g2 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after c2 allocated");
        CHECK(g2 > g1);

        c1.releaseSheet(EFontGlyphType::Grayscale, 0);
        const S32 g3 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after c1 released");
        CHECK(g3 > g2);

        c2.releaseSheet(EFontGlyphType::Color, 0);
        const S32 g4 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after c2 released");
        CHECK(g4 > g3);
    }

    // Per-type independence under release: releasing all Color sheets
    // doesn't disturb the Grayscale pen, and the per-type isSheetReleased
    // queries don't cross-bleed. Pins the per-type vector array layout
    // — each EFontGlyphType has its own mImageRawVec / pen / released
    // tracking.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: releasing the Color sheet leaves Grayscale alone", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 gs_sheet = 0, col_sheet = 0;

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, gs_sheet);
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Color,     col_sheet);
        CHECK((S32)gs_sheet == 0);
        CHECK((S32)col_sheet == 0);

        c.releaseSheet(EFontGlyphType::Color, col_sheet);
        CHECK(c.isSheetReleased(EFontGlyphType::Color, 0));
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));

        // Allocate again on Grayscale: still lands on sheet 0 (its
        // trailing slot is live, unaffected by Color side).
        S32 px2 = 0, py2 = 0; U32 gs_sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, gs_sheet2);
        CHECK((S32)gs_sheet2 == 0);
    }
}
