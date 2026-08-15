/**
 * @file llfontbitmapcache_test.cpp
 * @brief Unit tests for LLFontBitmapCache — atlas allocator, row/sheet
 *        packing, generation counter, sheet release/eviction.
 *
 * Pure-CPU tests cover the bookkeeping surface (construction, generation
 * counter, init/reset). The trailing block under #if LL_MESA_HEADLESS
 * exercises nextOpenPos and releaseSheet, which call into gGL and
 * LLImageGL::destroyGLTexture and so need a real GL context. CMake
 * supplies the headless library + LL_MESA_HEADLESS=1 when BUILD_HEADLESS=ON;
 * without it those test groups compile out and the binary stays pure-CPU.
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

#include "../test/lltut.h"

#if LL_MESA_HEADLESS
#  include "../llimagegl.h"
#  include "llimage.h"
#  include "llheadlessgl_fixture.h"
#endif

namespace tut
{
    // Pure-CPU fixture: no GL context required. The tests in this group
    // touch only the bookkeeping state that LLFontBitmapCache maintains
    // independently of any GL allocation.
    struct llfontbitmapcache_data
    {
    };

    typedef test_group<llfontbitmapcache_data> llfontbitmapcache_test;
    typedef llfontbitmapcache_test::object     llfontbitmapcache_object;
    tut::llfontbitmapcache_test llfontbitmapcache_testcase("LLFontBitmapCache");

    // Construction takes a fresh value from sNextGeneration. Two
    // back-to-back instances must therefore land on distinct
    // generations — without per-instance uniqueness, vertex buffers
    // comparing their stored mLastFontCacheGen across an in-place
    // font reload would false-match (see llfontbitmapcache.h:139-147).
    template<> template<>
    void llfontbitmapcache_object::test<1>()
    {
        LLFontBitmapCache a;
        LLFontBitmapCache b;
        ensure_not_equals("two instances get distinct generations",
                          a.getCacheGeneration(), b.getCacheGeneration());
        ensure("first generation > 0",  a.getCacheGeneration() > 0);
        ensure("second generation > first",
               b.getCacheGeneration() > a.getCacheGeneration());
    }

    // Static getGlobalGeneration must reflect the last sNextGeneration
    // bump. Constructing a new cache bumps the counter, so the global
    // tick must equal the new instance's mGeneration immediately after.
    template<> template<>
    void llfontbitmapcache_object::test<2>()
    {
        const S32 before = LLFontBitmapCache::getGlobalGeneration();
        LLFontBitmapCache c;
        const S32 after  = LLFontBitmapCache::getGlobalGeneration();
        ensure("global generation strictly increased on construction",
               after > before);
        ensure_equals("global generation matches new instance's gen",
                      after, c.getCacheGeneration());
    }

    // init() seeds mBitmapWidth/Height; reset() clears them back to 0
    // and bumps the generation counter. Verify both ends.
    template<> template<>
    void llfontbitmapcache_object::test<3>()
    {
        LLFontBitmapCache c;
        const S32 gen_init0 = c.getCacheGeneration();

        c.init(/*max_char_width=*/2, /*max_char_height=*/2);
        // 2*40=80 → next pow2 ≥ 80 → 128, capped at 1024.
        ensure_equals("bitmap width set by init",  c.getBitmapWidth(),  128);
        ensure_equals("bitmap height set by init", c.getBitmapHeight(), 128);

        c.reset();
        ensure_equals("bitmap width cleared by reset",  c.getBitmapWidth(),  0);
        ensure_equals("bitmap height cleared by reset", c.getBitmapHeight(), 0);
        ensure("reset bumped generation",
               c.getCacheGeneration() > gen_init0);
    }

    // Out-of-range queries on a freshly-constructed (or reset) cache
    // must return safe defaults, not crash. EFontGlyphType::Count is
    // a sentinel — every accessor must reject it.
    template<> template<>
    void llfontbitmapcache_object::test<4>()
    {
        LLFontBitmapCache c;
        ensure_equals("getNumBitmaps(Count) returns 0",
                      c.getNumBitmaps(EFontGlyphType::Count), 0u);
        ensure("getImageRaw(Count, 0) returns null",
               c.getImageRaw(EFontGlyphType::Count, 0) == nullptr);
        ensure("getImageGL(Count, 0) returns null",
               c.getImageGL(EFontGlyphType::Count, 0) == nullptr);
        ensure("getImageRaw on empty Grayscale returns null",
               c.getImageRaw(EFontGlyphType::Grayscale, 0) == nullptr);
        ensure_equals("getSheetLastUsedTime out-of-range == 0",
                      c.getSheetLastUsedTime(EFontGlyphType::Grayscale, 0), 0.0);
        ensure("isSheetReleased on empty cache reports false (slot doesn't exist)",
               !c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

#if LL_MESA_HEADLESS
    // GL-backed fixture: shares the binary's HeadlessGL singleton so
    // every test below has a live OSMesa context available for the
    // gGL.bind call inside nextOpenPos and the destroyGLTexture call
    // inside releaseSheet.
    struct llfontbitmapcache_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
    };

    typedef test_group<llfontbitmapcache_gl_data> llfontbitmapcache_gl_test;
    typedef llfontbitmapcache_gl_test::object     llfontbitmapcache_gl_object;
    tut::llfontbitmapcache_gl_test llfontbitmapcache_gl_testcase("LLFontBitmapCacheGL");

    // After a Grayscale allocation, the underlying LLImageRaw must
    // carry 2 components (R+G with R=255 stem, G=alpha) — pins
    // c99dcaa7d3 which restored the greyscale atlas to 2 components
    // after a regression had it at 1.
    template<> template<>
    void llfontbitmapcache_gl_object::test<1>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        ensure("nextOpenPos succeeded",
               c.nextOpenPos(/*width=*/8, /*height=*/8, px, py, EFontGlyphType::Grayscale, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Grayscale, sheet);
        ensure("raw image present", raw != nullptr);
        ensure_equals("Grayscale atlas has 2 components",
                      (S32)raw->getComponents(), 2);
    }

    // Color allocation must produce a 4-component (RGBA) raw — pins
    // the same array-init bug from c99dcaa7d3 in its Color form.
    template<> template<>
    void llfontbitmapcache_gl_object::test<2>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        ensure("nextOpenPos succeeded",
               c.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet));

        LLImageRaw* raw = c.getImageRaw(EFontGlyphType::Color, sheet);
        ensure("raw image present", raw != nullptr);
        ensure_equals("Color atlas has 4 components",
                      (S32)raw->getComponents(), 4);
    }

    // The very first nextOpenPos on a fresh cache must yield (4, 4) —
    // pins the mCurrentOffsetX/Y = {4, 4} array-init in the header
    // (an earlier `= { 4 }` form was zero-initing index [1], which
    // started Color atlases at offset 0).
    template<> template<>
    void llfontbitmapcache_gl_object::test<3>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = -1, py = -1; U32 sheet = 99;
        ensure("first Grayscale alloc succeeded",
               c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet));
        ensure_equals("first Grayscale posX == 4", px, 4);
        ensure_equals("first Grayscale posY == 4", py, 4);
        ensure_equals("first Grayscale sheet index == 0", (S32)sheet, 0);

        // Repeat for Color — a fresh cache, fresh allocation. The bug
        // would have started Color at posX=0; the fix shows posX=4.
        LLFontBitmapCache c2;
        c2.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        ensure("first Color alloc succeeded",
               c2.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Color, sheet2));
        ensure_equals("first Color posX == 4", px2, 4);
        ensure_equals("first Color posY == 4", py2, 4);
    }

    // Sequential allocations within the same row advance posX by
    // width + 4 (4-pixel border). Tests the row-packing pen advance
    // at llfontbitmapcache.cpp:201.
    template<> template<>
    void llfontbitmapcache_gl_object::test<4>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(10, 8, px, py, EFontGlyphType::Grayscale, sheet);
        ensure_equals("first posX",  px, 4);
        ensure_equals("first posY",  py, 4);

        c.nextOpenPos(10, 8, px, py, EFontGlyphType::Grayscale, sheet);
        // Expected: 4 (initial border) + 10 (first width) + 4 (border)
        ensure_equals("second posX advances by width+4", px, 18);
        ensure_equals("second posY stays in same row", py, 4);
    }

    // When the next allocation would exceed mBitmapWidth, the row
    // wraps. posX resets to 4; posY advances by the tallest glyph
    // placed in the prior row (mCurrentRowMaxHeight, not mMaxCharHeight).
    template<> template<>
    void llfontbitmapcache_gl_object::test<5>()
    {
        // 128x128 atlas. Two 50px-wide glyphs fit in row 0
        // (4 + 50 + 4 + 50 + 4 = 112 ≤ 128); third forces wrap.
        LLFontBitmapCache c;
        c.init(2, 2);
        ensure_equals("sheet width is 128", c.getBitmapWidth(), 128);

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 0
        c.nextOpenPos(50, 20, px, py, EFontGlyphType::Grayscale, sheet); // row 0, col 1
        ensure_equals("second alloc still in row 0", py, 4);

        c.nextOpenPos(50, 8, px, py, EFontGlyphType::Grayscale, sheet);  // wraps to row 1
        ensure_equals("third alloc wraps posX to 4", px, 4);
        // Tallest in row 0 was 30, so row 1 starts at 4 + 30 + 4 = 38.
        ensure_equals("third alloc posY uses tallest glyph in prior row",
                      py, 38);
        ensure_equals("still on sheet 0 (row wrap, not sheet rollover)",
                      (S32)sheet, 0);
    }

    // When a row wrap would push posY past mBitmapHeight, the cache
    // must allocate a fresh sheet rather than write past the atlas.
    // Verify by repeatedly forcing wraps until the next would overflow.
    template<> template<>
    void llfontbitmapcache_gl_object::test<6>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        ensure_equals("sheet width is 128",  c.getBitmapWidth(),  128);
        ensure_equals("sheet height is 128", c.getBitmapHeight(), 128);

        // Glyphs 50 wide, 30 tall. 2 per row (positions 4 and 58),
        // 3 rows (Y = 4, 38, 72) before next would land at Y=106
        // with bottom 106+30+4=140 > 128 — that triggers sheet rollover.
        S32 px = 0, py = 0; U32 sheet = 0;
        for (int i = 0; i < 6; ++i)
        {
            c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        }
        ensure_equals("six allocs of 50x30 still on sheet 0",
                      (S32)sheet, 0);
        ensure_equals("one sheet so far",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);

        // 7th glyph: Y advance to 4+30+4+30+4+30+4=106, bottom would
        // be 106+30+4=140 > 128 → new sheet. Either the row-wrap path
        // (line 125) or the vertical-fit recheck (line 144) fires.
        c.nextOpenPos(50, 30, px, py, EFontGlyphType::Grayscale, sheet);
        ensure_equals("7th alloc moved to new sheet",
                      (S32)sheet, 1);
        ensure_equals("two sheets allocated",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 2u);
        ensure_equals("new sheet starts at posX=4", px, 4);
        ensure_equals("new sheet starts at posY=4", py, 4);
    }

    // Per-type isolation: a Grayscale allocation must not move the
    // Color pen, and vice versa. Each type owns its own X/Y/row-max
    // state at index [bitmap_idx].
    template<> template<>
    void llfontbitmapcache_gl_object::test<7>()
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
        ensure_equals("Color pen unaffected by Grayscale activity (x)", cpx, 4);
        ensure_equals("Color pen unaffected by Grayscale activity (y)", cpy, 4);
        ensure_equals("Color sheet index starts at 0",  (S32)csheet, 0);
        ensure_equals("Grayscale + Color each have one sheet",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);
        ensure_equals("Color has its own sheet vector",
                      c.getNumBitmaps(EFontGlyphType::Color),     1u);
    }

    // Generation counter must strictly increase on every allocation
    // and on releaseSheet. Vertex buffers compare against a captured
    // generation to invalidate; equal-or-decreasing generations would
    // mask the invalidation.
    template<> template<>
    void llfontbitmapcache_gl_object::test<8>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        const S32 g0 = c.getCacheGeneration();

        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        const S32 g1 = c.getCacheGeneration();
        ensure("generation bumped by nextOpenPos", g1 > g0);

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);
        const S32 g2 = c.getCacheGeneration();
        ensure("generation bumped by releaseSheet", g2 > g1);

        c.reset();
        const S32 g3 = c.getCacheGeneration();
        ensure("generation bumped by reset", g3 > g2);
    }

    // releaseSheet drops the LLImageRaw + LLImageGL but keeps the
    // slot index alive (as nullptr) so existing index references stay
    // valid. isSheetReleased flips true; getSheetLastUsedTime returns 0.
    template<> template<>
    void llfontbitmapcache_gl_object::test<9>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);

        ensure("sheet not yet released",
               !c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        // Don't assert getSheetLastUsedTime is > 0 here — it samples
        // LLFrameTimer::getTotalSeconds() and the test environment may
        // not have ticked a frame yet, so the wall-clock can legitimately
        // be 0.0. The release-time clearing below is the load-bearing
        // contract; the timestamp tracking itself is exercised
        // implicitly via touchSheet on every nextOpenPos call.

        c.releaseSheet(EFontGlyphType::Grayscale, sheet);

        ensure("isSheetReleased flips true after release",
               c.isSheetReleased(EFontGlyphType::Grayscale, sheet));
        ensure_equals("released sheet getImageRaw returns null",
                      c.getImageRaw(EFontGlyphType::Grayscale, sheet),
                      (LLImageRaw*)nullptr);
        ensure_equals("released sheet last-used time cleared to 0",
                      c.getSheetLastUsedTime(EFontGlyphType::Grayscale, sheet), 0.0);
        ensure_equals("getNumBitmaps preserves slot count after release",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);
    }

    // After releasing the active sheet, the next nextOpenPos must build a
    // fresh image rather than write into the freed one — and it recycles
    // the released slot instead of growing the sheet vector. The purge-
    // before-release contract guarantees nothing references the released
    // index, so reuse is safe and bounds slot growth across eviction
    // cycles. Pins the active_sheet_released check + slot recycling in
    // nextOpenPos.
    template<> template<>
    void llfontbitmapcache_gl_object::test<10>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        c.releaseSheet(EFontGlyphType::Grayscale, sheet);

        S32 px2 = 0, py2 = 0; U32 sheet2 = 99;
        ensure("nextOpenPos succeeds after active sheet release",
               c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2));
        ensure_equals("released slot 0 is recycled for the new sheet",
                      (S32)sheet2, 0);
        ensure_equals("new sheet starts at posX=4", px2, 4);
        ensure_equals("new sheet starts at posY=4", py2, 4);
        ensure("recycled slot 0 is live again",
               !c.isSheetReleased(EFontGlyphType::Grayscale, 0));
        ensure_equals("slot vector did not grow",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);
    }

    // Multi-cycle release+re-alloc: alloc, release, alloc, release, alloc
    // keeps recycling slot 0 — the slot vector never grows across eviction
    // cycles, and the generation counter still advances every cycle so
    // captured vertex buffers rebuild. Pins bounded slot growth under
    // repeat cycles.
    template<> template<>
    void llfontbitmapcache_gl_object::test<12>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 s0 = 0, s1 = 99, s2 = 99;
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s0);
        ensure_equals("first alloc lands on sheet 0", (S32)s0, 0);
        const S32 gen0 = c.getCacheGeneration();
        c.releaseSheet(EFontGlyphType::Grayscale, s0);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s1);
        ensure_equals("second alloc recycles slot 0", (S32)s1, 0);
        const S32 gen1 = c.getCacheGeneration();
        ensure("generation advanced across release+recycle", gen1 > gen0);
        c.releaseSheet(EFontGlyphType::Grayscale, s1);

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s2);
        ensure_equals("third alloc recycles slot 0 again", (S32)s2, 0);
        ensure("generation advanced again", c.getCacheGeneration() > gen1);

        ensure_equals("getNumBitmaps == 1 (slots recycled, not reserved)",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);
        ensure("slot 0 live after the final alloc",
               !c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

    // A live active sheet with room keeps absorbing allocations — a
    // release+recycle cycle doesn't perturb the pen, and no spurious
    // sheets are created while the active sheet has space. Pins the
    // active-sheet tracking (mCurrentSheet) staying put across allocs.
    template<> template<>
    void llfontbitmapcache_gl_object::test<13>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 s0 = 0, s1 = 99;

        // Alloc sheet 0, release it, alloc → recycled slot 0 (live).
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s0);
        c.releaseSheet(EFontGlyphType::Grayscale, s0);
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, s1);
        ensure_equals("after release+alloc, recycled onto slot 0", (S32)s1, 0);

        // Further allocs must keep landing on the recycled sheet (it has
        // plenty of room — only one 8x8 glyph placed since the rebuild).
        for (int i = 0; i < 4; ++i)
        {
            U32 si = 99;
            c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, si);
            ensure_equals("subsequent alloc stays on the recycled sheet",
                          (S32)si, 0);
        }
        ensure_equals("still 1 sheet slot (no extra allocation)",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);
        ensure("recycled sheet stays live",
               !c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }

    // Cross-instance global generation: two LLFontBitmapCache instances
    // mutating in interleaved order each bump the global counter. Pins
    // that vertex buffers checking `mLastFontCacheGen` against
    // getGlobalGeneration() can detect changes that fired on any cache,
    // not just the one they captured against.
    template<> template<>
    void llfontbitmapcache_gl_object::test<14>()
    {
        LLFontBitmapCache c1; c1.init(2, 2);
        LLFontBitmapCache c2; c2.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;

        const S32 g0 = LLFontBitmapCache::getGlobalGeneration();
        c1.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, sheet);
        const S32 g1 = LLFontBitmapCache::getGlobalGeneration();
        ensure("c1 alloc bumped global gen", g1 > g0);

        c2.nextOpenPos(8, 8, px, py, EFontGlyphType::Color, sheet);
        const S32 g2 = LLFontBitmapCache::getGlobalGeneration();
        ensure("c2 alloc bumped global gen", g2 > g1);

        c1.releaseSheet(EFontGlyphType::Grayscale, 0);
        const S32 g3 = LLFontBitmapCache::getGlobalGeneration();
        ensure("c1 release bumped global gen", g3 > g2);

        c2.releaseSheet(EFontGlyphType::Color, 0);
        const S32 g4 = LLFontBitmapCache::getGlobalGeneration();
        ensure("c2 release bumped global gen", g4 > g3);
    }

    // Per-type independence under release: releasing all Color sheets
    // doesn't disturb the Grayscale pen, and the per-type isSheetReleased
    // queries don't cross-bleed. Pins the per-type vector array layout
    // — each EFontGlyphType has its own mImageRawVec / pen / released
    // tracking.
    template<> template<>
    void llfontbitmapcache_gl_object::test<15>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0;
        U32 gs_sheet = 0, col_sheet = 0;

        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Grayscale, gs_sheet);
        c.nextOpenPos(8, 8, px, py, EFontGlyphType::Color,     col_sheet);
        ensure_equals("Grayscale on its own sheet 0", (S32)gs_sheet, 0);
        ensure_equals("Color on its own sheet 0",     (S32)col_sheet, 0);

        c.releaseSheet(EFontGlyphType::Color, col_sheet);
        ensure("Color sheet 0 released",
               c.isSheetReleased(EFontGlyphType::Color, 0));
        ensure("Grayscale sheet 0 unaffected by Color release",
               !c.isSheetReleased(EFontGlyphType::Grayscale, 0));

        // Allocate again on Grayscale: still lands on sheet 0 (its
        // trailing slot is live, unaffected by Color side).
        S32 px2 = 0, py2 = 0; U32 gs_sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, gs_sheet2);
        ensure_equals("Grayscale alloc stays on sheet 0 (Color release didn't touch it)",
                      (S32)gs_sheet2, 0);
    }

    // reset() after allocations clears all state — no live sheets,
    // pens back at 4, and a subsequent nextOpenPos starts from (4, 4)
    // on a fresh sheet.
    template<> template<>
    void llfontbitmapcache_gl_object::test<11>()
    {
        LLFontBitmapCache c;
        c.init(2, 2);
        S32 px = 0, py = 0; U32 sheet = 0;
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        c.nextOpenPos(40, 20, px, py, EFontGlyphType::Grayscale, sheet);
        ensure_equals("two sheets pending before reset",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 1u);

        c.reset();
        ensure_equals("no Grayscale sheets after reset",
                      c.getNumBitmaps(EFontGlyphType::Grayscale), 0u);
        ensure_equals("no Color sheets after reset",
                      c.getNumBitmaps(EFontGlyphType::Color), 0u);

        // Re-init and verify pens really did reset back to (4,4).
        c.init(2, 2);
        S32 px2 = -1, py2 = -1; U32 sheet2 = 99;
        c.nextOpenPos(8, 8, px2, py2, EFontGlyphType::Grayscale, sheet2);
        ensure_equals("post-reset first posX",  px2, 4);
        ensure_equals("post-reset first posY",  py2, 4);
    }
#endif // LL_MESA_HEADLESS
}
