/**
 * @file llfontbitmapcache_test.cpp
 * @brief Unit tests for LLFontBitmapCache — atlas allocator, row/sheet
 *        packing, generation counter, sheet release/eviction.
 *
 * Pure-CPU tests cover the bookkeeping surface (construction, generation
 * counter, init/reset). nextOpenPos and releaseSheet, which call into gGL
 * and LLImageGL::destroyGLTexture and so need a real GL context, are in
 * llfontbitmapcache_gl_test.cpp.
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
}
