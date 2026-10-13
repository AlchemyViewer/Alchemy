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

#include <catch2/catch_test_macros.hpp>

namespace
{
    // Construction takes a fresh value from sNextGeneration. Two
    // back-to-back instances must therefore land on distinct
    // generations — without per-instance uniqueness, vertex buffers
    // comparing their stored mLastFontCacheGen across an in-place
    // font reload would false-match (see llfontbitmapcache.h:139-147).
    TEST_CASE("LLFontBitmapCache: each new cache takes a fresh and higher generation", "[LLFontBitmapCache]")
    {
        LLFontBitmapCache a;
        LLFontBitmapCache b;
        UNSCOPED_INFO("two instances get distinct generations");
        CHECK(a.getCacheGeneration() != b.getCacheGeneration());
        UNSCOPED_INFO("first generation > 0");
        CHECK(a.getCacheGeneration() > 0);
        UNSCOPED_INFO("second generation > first");
        CHECK(b.getCacheGeneration() > a.getCacheGeneration());
    }

    // Static getGlobalGeneration must reflect the last sNextGeneration
    // bump. Constructing a new cache bumps the counter, so the global
    // tick must equal the new instance's mGeneration immediately after.
    TEST_CASE("LLFontBitmapCache: getGlobalGeneration is the newest cache's generation", "[LLFontBitmapCache]")
    {
        const S32 before = LLFontBitmapCache::getGlobalGeneration();
        LLFontBitmapCache c;
        const S32 after  = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("global generation strictly increased on construction");
        CHECK(after > before);
        UNSCOPED_INFO("global generation matches new instance's gen");
        CHECK(after == c.getCacheGeneration());
    }

    // init() seeds mBitmapWidth/Height; reset() clears them back to 0
    // and bumps the generation counter. Verify both ends.
    TEST_CASE("LLFontBitmapCache: init sizes the sheets; reset clears them and bumps the generation", "[LLFontBitmapCache]")
    {
        LLFontBitmapCache c;
        const S32 gen_init0 = c.getCacheGeneration();

        c.init(/*max_char_width=*/2, /*max_char_height=*/2);
        // 2*40=80 → next pow2 ≥ 80 → 128, capped at 1024.
        UNSCOPED_INFO("bitmap width set by init");
        CHECK(c.getBitmapWidth() == 128);
        UNSCOPED_INFO("bitmap height set by init");
        CHECK(c.getBitmapHeight() == 128);

        c.reset();
        UNSCOPED_INFO("bitmap width cleared by reset");
        CHECK(c.getBitmapWidth() == 0);
        UNSCOPED_INFO("bitmap height cleared by reset");
        CHECK(c.getBitmapHeight() == 0);
        UNSCOPED_INFO("reset bumped generation");
        CHECK(c.getCacheGeneration() > gen_init0);
    }

    // Out-of-range queries on a freshly-constructed (or reset) cache
    // must return safe defaults, not crash. EFontGlyphType::Count is
    // a sentinel — every accessor must reject it.
    TEST_CASE("LLFontBitmapCache: queries out of range or on an empty cache return safe defaults", "[LLFontBitmapCache]")
    {
        LLFontBitmapCache c;
        UNSCOPED_INFO("getNumBitmaps(Count) returns 0");
        CHECK(c.getNumBitmaps(EFontGlyphType::Count) == 0u);
        UNSCOPED_INFO("getImageRaw(Count, 0) returns null");
        REQUIRE(c.getImageRaw(EFontGlyphType::Count, 0) == nullptr);
        UNSCOPED_INFO("getImageGL(Count, 0) returns null");
        REQUIRE(c.getImageGL(EFontGlyphType::Count, 0) == nullptr);
        UNSCOPED_INFO("getImageRaw on empty Grayscale returns null");
        REQUIRE(c.getImageRaw(EFontGlyphType::Grayscale, 0) == nullptr);
        UNSCOPED_INFO("getSheetLastUsedTime out-of-range == 0");
        CHECK(c.getSheetLastUsedTime(EFontGlyphType::Grayscale, 0) == 0.0);
        UNSCOPED_INFO("isSheetReleased on empty cache reports false (slot doesn't exist)");
        CHECK_FALSE(c.isSheetReleased(EFontGlyphType::Grayscale, 0));
    }
}
