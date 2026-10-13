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
        UNSCOPED_INFO("nextOpenPos succeeded");
        CHECK(c.nextOpenPos(/*width=*/8, /*height=*/8, px, py, EFontGlyphType::Grayscale, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Grayscale, sheet);
        UNSCOPED_INFO("raw image present");
        REQUIRE(raw != nullptr);
        UNSCOPED_INFO("Grayscale atlas has 2 components");
        CHECK((S32)raw->getComponents() == 2);
    }

    // Color allocation must produce a 4-component (RGBA) raw — pins
    // the same array-init bug from c99dcaa7d3 in its Color form.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a Color sheet has 4 components", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        UNSCOPED_INFO("nextOpenPos succeeded");
        CHECK(c.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Color, sheet);
        UNSCOPED_INFO("raw image present");
        REQUIRE(raw != nullptr);
        UNSCOPED_INFO("Color atlas has 4 components");
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
        UNSCOPED_INFO("first Grayscale alloc succeeded");
        CHECK(c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet));
        UNSCOPED_INFO("first Grayscale posX == 4");
        CHECK(px == 4);
        UNSCOPED_INFO("first Grayscale posY == 4");
        CHECK(py == 4);
        UNSCOPED_INFO("first Grayscale sheet index == 0");
        CHECK((S32)sheet == 0);

        // Repeat for Color — a fresh cache, fresh allocation. The bug
        // would have started Color at posX=0; the fix shows posX=4.
        LLFontBitmapCache c2;
        c2.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        UNSCOPED_INFO("first Color alloc succeeded");
        CHECK(c2.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Color, sheet2));
        UNSCOPED_INFO("first Color posX == 4");
        CHECK(px2 == 4);
        UNSCOPED_INFO("first Color posY == 4");
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
        UNSCOPED_INFO("first posX");
        CHECK(px == 4);
        UNSCOPED_INFO("first posY");
        CHECK(py == 4);

        c.nextOpenPos(10, 8, px, py, EFontGlyphType::Grayscale, sheet);
        // Expected: 4 (initial border) + 10 (first width) + 4 (border)
        UNSCOPED_INFO("second posX advances by width+4");
        CHECK(px == 18);
        UNSCOPED_INFO("second posY stays in same row");
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
        UNSCOPED_INFO("sheet width is 128");
        CHECK(c.getBitmapWidth() == 128);

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 0
        c.nextOpenPos(50, 20, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 1
        UNSCOPED_INFO("second alloc still in row 0");
        CHECK(py == 4);

        c.nextOpenPos(50, 8, px, py, EFontGlyphType::Grayscale, sheet);  // wraps to row 1
        UNSCOPED_INFO("third alloc wraps posX to 4");
        CHECK(px == 4);
        // Tallest in row 0 was 30, so row 1 starts at 4 + 30 + 4 = 38.
        UNSCOPED_INFO("third alloc posY uses tallest glyph in prior row");
        CHECK(py == 38);
        UNSCOPED_INFO("still on sheet 0 (row wrap, not sheet rollover)");
        CHECK((S32)sheet == 0);
    }

    // When a row wrap would push posY past mBitmapHeight, the cache
    // must allocate a fresh sheet rather than write past the atlas.
    // Verify by repeatedly forcing wraps until the next would overflow.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: a row wrap that would pass the bottom starts a new sheet", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        UNSCOPED_INFO("sheet width is 128");
        CHECK(c.getBitmapWidth() == 128);
        UNSCOPED_INFO("sheet height is 128");
        CHECK(c.getBitmapHeight() == 128);

        // Glyphs 50 wide, 30 tall. 2 per row (positions 4 and 58),
        // 3 rows (Y = 4, 38, 72) before next would land at Y=106
        // with bottom 106+30+4=140 > 128 — that triggers sheet rollover.
        S32 px = 0, py = 0; U32 sheet = 0;
        for (int i = 0; i < 6; ++i)
        {
            c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        }
        UNSCOPED_INFO("six allocs of 50x30 still on sheet 0");
        CHECK((S32)sheet == 0);
        UNSCOPED_INFO("one sheet so far");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);

        // 7th glyph: Y advance to 4+30+4+30+4+30+4=106, bottom would
        // be 106+30+4=140 > 128 → new sheet. Either the row-wrap path
        // (line 125) or the vertical-fit recheck (line 144) fires.
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        UNSCOPED_INFO("7th alloc moved to new sheet");
        CHECK((S32)sheet == 1);
        UNSCOPED_INFO("two sheets allocated");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 2u);
        UNSCOPED_INFO("new sheet starts at posX=4");
        CHECK(px == 4);
        UNSCOPED_INFO("new sheet starts at posY=4");
        CHECK(py == 4);
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
        UNSCOPED_INFO("Color pen unaffected by Grayscale activity (x)");
        CHECK(cpx == 4);
        UNSCOPED_INFO("Color pen unaffected by Grayscale activity (y)");
        CHECK(cpy == 4);
        UNSCOPED_INFO("Color sheet index starts at 0");
        CHECK((S32)csheet == 0);
        UNSCOPED_INFO("Grayscale + Color each have one sheet");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        UNSCOPED_INFO("Color has its own sheet vector");
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
        UNSCOPED_INFO("generation bumped by nextOpenPos");
        CHECK(g1 > g0);

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);
        const S32 g2 = c.getCacheGeneration();
        UNSCOPED_INFO("generation bumped by releaseSheet");
        CHECK(g2 > g1);

        c.reset();
        const S32 g3 = c.getCacheGeneration();
        UNSCOPED_INFO("generation bumped by reset");
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

        UNSCOPED_INFO("sheet not yet released");
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        // Don't assert getSheetLastUsedTime is > 0 here — it samples
        // LLFrameTimer::getTotalSeconds() and the test environment may
        // not have ticked a frame yet, so the wall-clock can legitimately
        // be 0.0. The release-time clearing below is the load-bearing
        // contract; the timestamp tracking itself is exercised
        // implicitly via touchSheet on every nextOpenPos call.

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);

        UNSCOPED_INFO("isSheetReleased flips true after release");
        CHECK(c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        UNSCOPED_INFO("released sheet getImageRaw returns null");
        REQUIRE(c.getImageRaw(EFontGlyphType::Grayscale, sheet) ==
                      (LLImageRaw*)nullptr);
        UNSCOPED_INFO("released sheet last-used time cleared to 0");
        CHECK(c.getSheetLastUsedTime(EFontGlyphType::Grayscale, sheet) == 0.0);
        UNSCOPED_INFO("getNumBitmaps preserves slot count after release");
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
        UNSCOPED_INFO("nextOpenPos succeeds after active sheet release");
        CHECK(c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2));
        UNSCOPED_INFO("released slot 0 is recycled for the new sheet");
        CHECK((S32)sheet2 == 0);
        UNSCOPED_INFO("new sheet starts at posX=4");
        CHECK(px2 == 4);
        UNSCOPED_INFO("new sheet starts at posY=4");
        CHECK(py2 == 4);
        UNSCOPED_INFO("recycled slot 0 is live again");
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
        UNSCOPED_INFO("slot vector did not grow");
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
        UNSCOPED_INFO("two sheets pending before reset");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);

        c.reset();
        UNSCOPED_INFO("no Grayscale sheets after reset");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 0u);
        UNSCOPED_INFO("no Color sheets after reset");
        CHECK(c.getNumBitmaps(EFontGlyphType::Color) == 0u);

        // Re-init and verify pens really did reset back to (4,4).
        c.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2);
        UNSCOPED_INFO("post-reset first posX");
        CHECK(px2 == 4);
        UNSCOPED_INFO("post-reset first posY");
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
        UNSCOPED_INFO("first alloc lands on sheet 0");
        CHECK((S32)s0 == 0);
        const S32 gen0 = c.getCacheGeneration();
        c.releaseSheet(EFontGlyphType::Grayscale, s0);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s1);
        UNSCOPED_INFO("second alloc recycles slot 0");
        CHECK((S32)s1 == 0);
        const S32 gen1 = c.getCacheGeneration();
        UNSCOPED_INFO("generation advanced across release+recycle");
        CHECK(gen1 > gen0);
        c.releaseSheet(EFontGlyphType::Grayscale, s1);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s2);
        UNSCOPED_INFO("third alloc recycles slot 0 again");
        CHECK((S32)s2 == 0);
        UNSCOPED_INFO("generation advanced again");
        CHECK(c.getCacheGeneration() > gen1);

        UNSCOPED_INFO("getNumBitmaps == 1 (slots recycled, not reserved)");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        UNSCOPED_INFO("slot 0 live after the final alloc");
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
        UNSCOPED_INFO("after release+alloc, recycled onto slot 0");
        CHECK((S32)s1 == 0);

        // Further allocs must keep landing on the recycled sheet (it has
        // plenty of room — only one 8x8 glyph placed since the rebuild).
        for (int i = 0; i < 4; ++i)
        {
            U32 si = 99;
            c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, si);
            UNSCOPED_INFO("subsequent alloc stays on the recycled sheet");
            REQUIRE((S32)si == 0);
        }
        UNSCOPED_INFO("still 1 sheet slot (no extra allocation)");
        CHECK(c.getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        UNSCOPED_INFO("recycled sheet stays live");
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

    // Cross-instance global generation: two LLFontBitmapCache instances
    // mutating in interleaved order each bump the global counter. Pins
    // that vertex buffers checking `mLastFontCacheGen` against
    // getGlobalGeneration() can detect changes that fired on any cache,
    // not just the one they captured against.
    TEST_CASE_METHOD(llfontbitmapcache_gl_data, "LLFontBitmapCacheGL: two caches' allocations and releases each bump the global generation", "[LLFontBitmapCacheGL]")
    {
        LLFontBitmapCache c1; c1.init(2, 2);
        LLFontBitmapCache c2; c2.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;

        const S32 g0 = LLFontBitmapCache::getGlobalGeneration();
        c1.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        const S32 g1 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("c1 alloc bumped global gen");
        CHECK(g1 > g0);

        c2.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet);
        const S32 g2 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("c2 alloc bumped global gen");
        CHECK(g2 > g1);

        c1.releaseSheet(EFontGlyphType::Grayscale, 0);
        const S32 g3 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("c1 release bumped global gen");
        CHECK(g3 > g2);

        c2.releaseSheet(EFontGlyphType::Color, 0);
        const S32 g4 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("c2 release bumped global gen");
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
        UNSCOPED_INFO("Grayscale on its own sheet 0");
        CHECK((S32)gs_sheet == 0);
        UNSCOPED_INFO("Color on its own sheet 0");
        CHECK((S32)col_sheet == 0);

        c.releaseSheet(EFontGlyphType::Color, col_sheet);
        UNSCOPED_INFO("Color sheet 0 released");
        CHECK(c.isSheetReleased(EFontGlyphType::Color, 0));
        UNSCOPED_INFO("Grayscale sheet 0 unaffected by Color release");
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));

        // Allocate again on Grayscale: still lands on sheet 0 (its
        // trailing slot is live, unaffected by Color side).
        S32 px2 = 0, py2 = 0; U32 gs_sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, gs_sheet2);
        UNSCOPED_INFO("Grayscale alloc stays on sheet 0 (Color release didn't touch it)");
        CHECK((S32)gs_sheet2 == 0);
    }
}
