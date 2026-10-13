/**
 * @file llfonttextcache_test.cpp
 * @brief Smoke tests for LLFontTextCache + LLFontTextCache.
 *
 * The deeper cache-invalidation invariants (geometry vs color
 * regen, mLastFontCacheGen, mLastUsesColorAtlas) are private to
 * LLFontTextCache, and the public render() entry point dives into
 * gGL.pushUIMatrix and drawGlyph — code paths that the
 * headless harness doesn't have populated (no UI matrix stack,
 * no shader bindings to draw with). A test for the recolor fast
 * path proper would need a full viewer-scale GL bring-up that the
 * llrender harness can't supply.
 *
 * What this file does instead is smoke the API surface that doesn't
 * touch the render-time path: construct/destruct, reset(),
 * enable*() static toggles. That at least catches link-time
 * regressions and obvious lifecycle bugs (double-free in dtor,
 * etc.). LLFontTextCache's measurement path is also covered
 * lightly — it's invoked via fontp->getWidthF32, which uses the
 * shape layout but doesn't hit gGL drawing primitives.
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

#include "../llfonttextcache.h"
#include "../llfontgl.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

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
    // Shared GL context + LLFontGL bring-up. One per binary. TUT
    // creates a fresh fixture per test method; per-fixture HeadlessGL
    // creation poisons LLFontGL's process-static fontp cache, since
    // each test's GL context destroys the LLImageGL textures the
    // fontps reference (LLFontManager::cleanupClass + LLImageGL::
    // cleanupClass in HeadlessGL dtor). Sharing the GL context across
    // tests keeps all that state coherent for the binary's lifetime.
    // Both groups take it with gUIProgram bound (the smoke tests don't
    // drive rendering, but having it bound is harmless for them).
    inline void ensureLLFontGLLoaded()
    {
        static bool initialized = false;
        if (initialized)
            return;
        if (!fileExists(kFontsXml))
            return;
        LLFontGL::initClass(96.f, 1.f, 1.f, kAppDir, kFontsXml,
                            LLSD(), /*create_gl_textures=*/true);
        LLFontGL::loadDefaultFonts();
        initialized = true;
    }

    // Fixture: minimal — just gates the static toggles and ensures the
    // shared GL fixture is up. The actual HeadlessGL instance lives in
    // ll_test::sharedHeadlessGL() and stays alive for the whole
    // binary, so font/atlas state stays coherent across tests.
    struct llfonttextcache_data
    {
        ll_test::HeadlessGL& gl = ll_test::sharedHeadlessGL();

        llfonttextcache_data()
        {
            LLFontTextCache::enableBufferCollection(true);
            LLFontTextCache::enableColorOnlyRegen(true);
            ensureLLFontGLLoaded();
        }
        ~llfonttextcache_data()
        {
            LLFontTextCache::enableBufferCollection(true);
            LLFontTextCache::enableColorOnlyRegen(true);
        }
    };


    // Default construction must not crash and must leave the buffer
    // in a destructible state. Catches uninitialized-member regressions
    // that surface as a crash in ~LLFontTextCache trying to free
    // a garbage list.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: a default-constructed cache destroys cleanly", "[LLFontTextCache]")
    {
        LLFontTextCache vb;
        CHECK(true);
        // Implicit destruct at scope exit; assertion above catches any
        // crash during dtor by being unreached.
    }

    // reset() on a freshly-constructed buffer must be a safe no-op.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: reset on a fresh cache is safe to call twice", "[LLFontTextCache]")
    {
        LLFontTextCache vb;
        vb.reset();
        vb.reset();  // idempotent
        CHECK(true);
    }

    // Static toggles round-trip without affecting later test setup.
    // The assertions confirm the calls compile and link; observable
    // round-trip would require accessing the private statics.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: enableBufferCollection and enableColorOnlyRegen toggle off and on", "[LLFontTextCache]")
    {
        LLFontTextCache::enableBufferCollection(false);
        LLFontTextCache::enableBufferCollection(true);
        LLFontTextCache::enableColorOnlyRegen(false);
        LLFontTextCache::enableColorOnlyRegen(true);
        CHECK(true);
    }

    // LLFontTextCache parallel: construct + reset.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: a cache resets twice and outlives a buffer collection toggle", "[LLFontTextCache]")
    {
        LLFontTextCache wb;
        wb.reset();
        wb.reset();
        CHECK(true);
        LLFontTextCache::enableBufferCollection(false);
        LLFontTextCache::enableBufferCollection(true);
    }

    // LLFontTextCache constructed alongside LLFontTextCache:
    // multiple coexisting instances don't share static state in a way
    // that breaks lifecycle. Pin two buffers in flight at once.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: four caches in flight at once each reset cleanly", "[LLFontTextCache]")
    {
        LLFontTextCache a;
        LLFontTextCache b;
        LLFontTextCache  w1;
        LLFontTextCache  w2;
        a.reset();
        b.reset();
        w1.reset();
        w2.reset();
        CHECK(true);
    }

    // ===================================================================
    // Width-path tests — exercise LLFontTextCache's measurement path.
    // No render-time shader binding needed; getWidthF32 walks the shape
    // layout but doesn't issue draw calls.
    // ===================================================================

    // First call computes; second call with identical params is a cache
    // hit and returns the same value. Both must equal fontp->getWidthF32.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes matches the font and repeats its answer", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache wb;
        const F32 first  = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 second = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 ref    = font->getWidthF32Bytes(s, 0, 5, false);
        UNSCOPED_INFO("first width matches getWidthF32Bytes");
        CHECK(first == ref);
        UNSCOPED_INFO("second call same as first (cache hit)");
        CHECK(second == first);
        CHECK(first > 0.f);
    }

    // Cache miss when geometry differs: changing max_chars between
    // calls forces recompute. The cache key includes max_chars at
    // llfonttextcache.cpp:517.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes recomputes for a new max_chars and repeats for the same", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache wb;
        const F32 w_5 = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 w_3 = wb.getWidthBytes(font, s, 0, 3, false);
        CHECK(w_5 > w_3);
        const F32 w_3_again = wb.getWidthBytes(font, s, 0, 3, false);
        UNSCOPED_INFO("identical max_chars hits cache");
        CHECK(w_3_again == w_3);
    }

    // Cache miss when LLFontGL::sScaleX changes between calls. Pins
    // mLastScaleX in the cache key (llfonttextcache.cpp:519).
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes recomputes when LLFontGL::sScaleX changes", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        const F32 saved_scale = LLFontGL::sScaleX;
        LLFontTextCache wb;
        LLFontGL::sScaleX = 1.0f;
        const F32 w_1 = wb.getWidthBytes(font, s, 0, 2, false);
        LLFontGL::sScaleX = 0.5f;
        const F32 w_half = wb.getWidthBytes(font, s, 0, 2, false);
        LLFontGL::sScaleX = saved_scale; // restore

        // Width should scale roughly with sScaleX (within FP rounding
        // noise). The cache key invalidation is the load-bearing pin.
        UNSCOPED_INFO("scale=0.5 produced different width than scale=1.0");
        CHECK(std::abs(w_1 - w_half) > 0.01f);
    }

    // Cache miss when the font's cache generation advances between
    // calls (a fallback face's atlas mutation, for example). Pins
    // mLastFontCacheGen at llfonttextcache.cpp:524.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes agrees with the font across an atlas generation tick", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache wb;
        const F32 first = wb.getWidthBytes(font, s, 0, 2, false);
        // Force a generation tick by rasterizing a fresh glyph through
        // the font (atlas allocation bumps the global counter).
        font->getFontFreetype()->getGlyphInfo(L'É', // 'É'
                                              EFontGlyphType::Grayscale);
        const F32 second = wb.getWidthBytes(font, s, 0, 2, false);
        // Held against the font rather than against the buffer's own earlier
        // answer. Comparing a cache to itself passes whether it invalidated,
        // recomputed, or returned a stale value -- the only reading that can
        // fail is one where the buffer and the font disagree.
        UNSCOPED_INFO("width survives an atlas generation tick");
        CHECK(first == second);
        UNSCOPED_INFO("and still agrees with the font it came from");
        CHECK(second == font->getWidthF32Bytes(s, 0, 2, false));
    }

    // reset() clears the cache — next call recomputes. Pins
    // llfonttextcache.cpp:480-493.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes after reset() still agrees with the font", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache wb;
        const F32 first = wb.getWidthBytes(font, s, 0, 2, false);
        wb.reset();
        const F32 after = wb.getWidthBytes(font, s, 0, 2, false);
        UNSCOPED_INFO("width survives reset()");
        CHECK(first == after);
        // Again, against the font: a reset that failed to clear and a reset
        // that recomputed correctly both return the earlier value, and only
        // one of those also matches an independent measurement.
        UNSCOPED_INFO("and still agrees with the font it came from");
        CHECK(after == font->getWidthF32Bytes(s, 0, 2, false));
    }

    // Multiple LLFontTextCache instances stay independent: cache
    // state in instance A doesn't affect instance B.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: one cache's reset leaves another's width unchanged", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache a, b;
        const F32 wa = a.getWidthBytes(font, s, 0, 2, false);
        a.reset();
        const F32 wb_val = b.getWidthBytes(font, s, 0, 2, false);
        UNSCOPED_INFO("instance B unaffected by A's reset");
        CHECK(wa == wb_val);
    }

    // The bound the cache is keyed on, over text where a byte is not a
    // character. Every other test in this group uses ASCII, where a byte
    // count, a character count and a glyph count are the same number, so a
    // cache keyed in the wrong one of them hits and misses identically on all
    // of them and every assertion still passes.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes keys its cache on bytes over multi-byte text", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        // "A", a three-byte character, "B": five bytes, three characters.
        const std::string s = "A\xE6\x97\xA5" "B";
        if (font->getWidthF32Bytes(s, 0, (S32)s.size(), false) <= 0.f)
            SKIP("font produced no width for the sample");

        // Every bound, boundary or not, has to agree with the font. A bound of
        // 1 and a bound of 4 are one character apart and three bytes apart;
        // a cache counting the wrong one answers one of these with the other's
        // width.
        LLFontTextCache wb;
        for (S32 bound : { 0, 1, 4, 5 })
        {
            CAPTURE(bound);
            const F32 cached = wb.getWidthBytes(font, s, 0, bound, false);
            UNSCOPED_INFO("cached width agrees with the font");
            REQUIRE(cached == font->getWidthF32Bytes(s, 0, bound, false));
            // And again, now that it is cached.
            UNSCOPED_INFO("and still agrees on the second reading");
            REQUIRE(wb.getWidthBytes(font, s, 0, bound, false) == cached);
        }

        // Distinct bounds must not collapse onto one entry.
        const F32 w1 = wb.getWidthBytes(font, s, 0, 1, false);
        const F32 w4 = wb.getWidthBytes(font, s, 0, 4, false);
        UNSCOPED_INFO("one character in is narrower than two");
        CHECK(w1 < w4);
    }

    // The guard that says a cache is being asked about the text it was named
    // for. It reads a bounded sample rather than the whole string, because a
    // text widget names its entire document here and then asks about spans of
    // it -- hashing all of what it is handed made every width a reflow asked
    // for cost the length of the document, in every build that keeps asserts.
    // The strings it exists to separate are still separated: a field's bullets
    // are a different length from the text behind them, and two labels differ
    // at one end or the other. Two long strings differing only in the middle
    // read as the same text, deliberately, and this is the whole of what that
    // bound gives up.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: ALFontCacheKey tells recorded text apart by length and both ends", "[LLFontTextCache]")
    {
        ALFontCacheKey key;
        UNSCOPED_INFO("the first text asked about is the one recorded");
        REQUIRE(key.sameTextAsRecorded("Inventory"));
        UNSCOPED_INFO("and reads as the same text when asked again");
        CHECK(key.sameTextAsRecorded("Inventory"));

        // What a password field does: one bullet per character, three bytes
        // apiece, over text that is one byte per character.
        UNSCOPED_INFO("a substituted string is not the text behind it");
        CHECK_FALSE(key.sameTextAsRecorded("\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"));

        // Same length, differing at the front and at the back -- the second is
        // what a sample taken only from the front would miss.
        ALFontCacheKey front;
        REQUIRE(front.sameTextAsRecorded("Received Items"));
        UNSCOPED_INFO("a different string of the same length is caught");
        CHECK_FALSE(front.sameTextAsRecorded("Xeceived Items"));

        ALFontCacheKey back;
        const std::string common(64, 'a');
        REQUIRE(back.sameTextAsRecorded(common + "tail"));
        UNSCOPED_INFO("a difference past the sampled front is caught");
        CHECK_FALSE(back.sameTextAsRecorded(common + "tai1"));

        // Naming a new source is what clears the recorded text, so the next
        // string asked about becomes the one this is holding.
        key.forgetSource();
        UNSCOPED_INFO("a forgotten source records whatever is asked next");
        CHECK(key.sameTextAsRecorded("\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"));
    }

    // ===================================================================
    // Render-path tests — exercise LLFontTextCache::render() through
    // a real compiled gUIProgram. The fixture ctor binds the stub UI
    // shader so beginList → flush → drawArrays completes against the
    // framebuffer.
    // ===================================================================

    struct llfonttextcache_render_data
    {
        ll_test::HeadlessGL& gl = ll_test::sharedHeadlessGL();

        llfonttextcache_render_data()
        {
            LLFontTextCache::enableBufferCollection(true);
            LLFontTextCache::enableColorOnlyRegen(true);
            ensureLLFontGLLoaded();
        }
        ~llfonttextcache_render_data()
        {
            LLFontTextCache::enableBufferCollection(true);
            LLFontTextCache::enableColorOnlyRegen(true);
        }
    };


    // First render() populates the buffer cache and returns the
    // character count rendered. Pins genBuffers + drawBuffer end-to-end
    // through the stub UI shader.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: renderBytes on a fresh cache returns the bytes it drew", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        gl.clearFramebuffer();
        LLFontTextCache vb;
        const S32 n = vb.renderBytes(font, s, 0,
                                /*x=*/100.f, /*y=*/100.f,
                                LLColor4::white,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                /*max_chars=*/5);
        CHECK(n == 5);
    }

    // Color-only fast path (b317778477): same render twice with same
    // geometry but different color. Second call goes through
    // recolorBuffers — no genBuffers, no atlas alloc. The global
    // generation counter must NOT advance between the first and second
    // calls (atlas already populated; recolor doesn't allocate).
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a repeated identical render allocates nothing in the atlas", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache vb;
        // Pre-rasterize all glyphs of "Hello" so neither call grows
        // the atlas (otherwise both calls would bump the gen counter
        // and the test would be a no-op).
        font->generateASCIIglyphs();
        vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);

        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        // Second call: same geometry, same color. Should be a no-regen
        // replay — neither genBuffers nor recolorBuffers fires.
        vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_after = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("no atlas alloc on repeat render (gen counter stable)");
        CHECK(gen_after == gen_before);
    }

    // Color change with same geometry: takes the recolor fast path —
    // recolorBuffers walks the captured streams and rewrites the color
    // attribute, no genBuffers. Pins llfonttextcache.cpp:203-210.
    // Observable: global generation counter doesn't advance (no atlas
    // mutation), and the call returns the same character count.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a color change alone keeps the count and allocates nothing", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        const S32 n_white = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                      LLFontGL::LEFT, LLFontGL::BASELINE,
                                      LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        const S32 n_red = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::red,
                                    LLFontGL::LEFT, LLFontGL::BASELINE,
                                    LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_after = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("color change preserves char count");
        CHECK(n_red == n_white);
        UNSCOPED_INFO("recolor fast path: no atlas mutation");
        CHECK(gen_after == gen_before);
    }

    // Geometry change forces full regen: render at x=100, then at
    // x=200. Both calls succeed; the global generation counter still
    // shouldn't advance (nothing about geometry change touches the
    // atlas), but the buffer cache had to rebuild. The post-render
    // render-target reflects the second position.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a render moved to another x returns the same count", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "AB";

        LLFontTextCache vb;
        font->generateASCIIglyphs();

        const S32 n1 = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n2 = vb.renderBytes(font, s, 0, 200.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        UNSCOPED_INFO("char count stable after geometry change");
        CHECK(n1 == n2);
        CHECK(n1 > 0);
    }

    // enableColorOnlyRegen(false) disables the fast path entirely:
    // a same-geometry color-change call falls through to genBuffers
    // (full regen). The static toggle pins the flag-controlled branch
    // at llfonttextcache.cpp:210. Restore the flag in dtor.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: with enableColorOnlyRegen off a color change still draws every byte", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);

        LLFontTextCache::enableColorOnlyRegen(false);
        // Same geometry, different color — with fast path off, this
        // should still complete cleanly through genBuffers.
        const S32 n = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::red,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        LLFontTextCache::enableColorOnlyRegen(true);
        UNSCOPED_INFO("render with fast path disabled still returns 2");
        CHECK(n == 2);
    }

    // Style flip NORMAL → BOLD forces full regen — different glyph
    // index stream. Pins 00f3a4e93d (style invalidation in geometry-
    // invalid check at llfonttextcache.cpp:175).
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a style flip from NORMAL to BOLD keeps the count", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        const S32 n_norm = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_bold = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::BOLD, LLFontGL::NO_SHADOW, 2);
        UNSCOPED_INFO("char count stable across style flip");
        CHECK(n_norm == n_bold);
        CHECK(n_norm > 0);
    }

    // Truncated DROP_SHADOW_SOFT render with use_ellipses=true: the
    // appended ellipsis must NOT contribute shadow geometry to either
    // capture list. Pre-fix, the inner ellipsis render dropped the
    // outer's on_pass_boundary callback and routed its shadow batch
    // into mForegroundBufferList — visible as off-color shadow tinting
    // in the recolor fast path. Fix renders the ellipsis with NO_SHADOW.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: an ellipsized DROP_SHADOW_SOFT render adds no shadow quads for the dots", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        ll_test::FontStateScope scope;

        // Long ASCII run + narrow max_pixels forces truncation. The
        // exact visible count depends on font metrics; the test reads
        // it back from render's return and computes expected quad
        // counts off that.
        const std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        font->generateASCIIglyphs();

        LLFontTextCache vb;
        const S32 n = vb.renderBytes(font, s, 0,
                                /*x=*/0.f, /*y=*/100.f,
                                LLColor4::white,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL,
                                LLFontGL::DROP_SHADOW_SOFT,
                                /*max_chars=*/S32_MAX,
                                /*max_pixels=*/40,
                                /*right_x=*/nullptr,
                                /*use_ellipses=*/true,
                                /*use_color=*/true);
        {
            INFO("truncation actually happened (n < full string)");
            CHECK(n > 0);
            REQUIRE(n < (S32)s.length());
        }

        const auto counts = ll_test::VertexBufferProbe::count(vb);

        // Pass-A SOFT shadow: 5 quads per visible char. Ellipsis must
        // NOT add to this list — the ellipsis render is single-pass
        // (NO_SHADOW) post-fix.
        const size_t expected_shadow = static_cast<size_t>(n) * 5u;
        UNSCOPED_INFO("shadow list = visible chars * 5 (ellipsis adds 0)");
        CHECK(counts.shadow_quads == expected_shadow);

        // Foreground: 1 quad per visible char + 3 ellipsis chars.
        const size_t expected_fg = static_cast<size_t>(n) + 3u;
        UNSCOPED_INFO("foreground list = visible + 3 ellipsis foreground");
        CHECK(counts.foreground_quads == expected_fg);
    }

    // Sanity check on chars_drawn: when use_ellipses=true triggers
    // truncation, render's return value reports only the visible
    // pre-ellipsis character count, not including the appended "...".
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: an ellipsized render counts the visible bytes and not the dots", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        ll_test::FontStateScope scope;

        const std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        font->generateASCIIglyphs();

        LLFontTextCache vb;
        const S32 n_truncated = vb.renderBytes(font, s, 0,
                                          0.f, 100.f,
                                          LLColor4::white,
                                          LLFontGL::LEFT, LLFontGL::BASELINE,
                                          LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                          S32_MAX,
                                          /*max_pixels=*/40,
                                          nullptr,
                                          /*use_ellipses=*/true,
                                          true);
        {
            INFO("truncated count is positive but less than full string");
            CHECK(n_truncated > 0);
            REQUIRE(n_truncated < (S32)s.length());
        }

        // Same render with max_pixels wide enough to fit the whole
        // string. No truncation, no ellipsis path; full count returned.
        LLFontTextCache vb2;
        const S32 n_full = vb2.renderBytes(font, s, 0,
                                      0.f, 100.f,
                                      LLColor4::white,
                                      LLFontGL::LEFT, LLFontGL::BASELINE,
                                      LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                      S32_MAX,
                                      /*max_pixels=*/2000,
                                      nullptr,
                                      /*use_ellipses=*/true,
                                      true);
        UNSCOPED_INFO("full-fit render returns the entire string length");
        CHECK(n_full == (S32)s.length());
    }

    // Moving text does not rebuild it. Glyph geometry is built relative to the
    // text's own origin rather than in screen coordinates, so a scroll, a
    // floater drag or a resize replays the capture under a new transform. That
    // was not true while the origin was added into the vertices: everything
    // visible in a moving container was reshaped every frame.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: moving text does not rebuild it", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        ll_test::FontStateScope scope;

        const std::string s = "Moved, not rebuilt";
        font->generateASCIIglyphs();

        const LLFontGL::UIOrigin saved_origin = LLFontGL::sCurOrigin;
        LLFontGL::sCurOrigin.set(0, 0);

        LLFontTextCache vb;

        // Same text, at whatever place in its own coordinates it is asked for.
        // Named before every draw, which is how a widget uses this -- it holds
        // no copy of the text and cannot see a change for itself.
        auto draw_at = [&](F32 x)
        {
            vb.setSource(&s, 0);
            vb.renderBytes(font, s, 0, x, 34.f, LLColor4::white,
                           LLFontGL::LEFT, LLFontGL::BASELINE,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        };
        auto draw = [&]() { draw_at(12.f); };

        const U64 before = LLFontTextCache::regenCount();

        // Draw where it is until it stops rebuilding. This takes more than one
        // pass on purpose: a draw can rasterize glyph phases, which bumps the
        // font's cache generation, and geometry captured while the atlas was
        // growing under it is deliberately not trusted for a second frame.
        bool settled = false;
        for (int i = 0; i < 8 && !settled; ++i)
        {
            const U64 before_draw = LLFontTextCache::regenCount();
            draw();
            settled = (LLFontTextCache::regenCount() == before_draw);
        }
        const U64 after_settle = LLFontTextCache::regenCount();

        LLFontGL::sCurOrigin.set(137, 42);
        draw();
        LLFontGL::sCurOrigin.set(-71, 900);
        draw();
        const U64 after_moves = LLFontTextCache::regenCount();

        // Moving the text within its own coordinates is a different draw and
        // must still rebuild. Without this the assertion above would hold just
        // as well for a cache that had stopped noticing anything.
        draw_at(200.f);
        const U64 after_real_change = LLFontTextCache::regenCount();

        LLFontGL::sCurOrigin = saved_origin;

        UNSCOPED_INFO("the first draw builds");
        CHECK(after_settle > before);
        // And without this it would hold for a cache that never replays at all.
        UNSCOPED_INFO("an unmoved redraw eventually replays");
        CHECK(settled);
        UNSCOPED_INFO("moving the text replays rather than rebuilds");
        CHECK(after_moves == after_settle);
        UNSCOPED_INFO("a draw somewhere else still rebuilds");
        CHECK(after_real_change > after_moves);
    }

    // Text that shapes to no geometry settles too. A cell with nothing in it
    // and a string clipped to no width both leave the capture empty, which
    // reads exactly like a cache that has never been asked to build one --
    // and a scroll list draws a cell per column whether or not it holds
    // anything. Such a piece of text reshaped itself on every frame it stayed
    // on screen, and the count is the only place it showed.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: text that shapes to no geometry stops rebuilding too", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        ll_test::FontStateScope scope;
        font->generateASCIIglyphs();

        // Settles a cache the same way the viewer does: draw until the count
        // stops moving, since a first draw can rasterize glyphs and bump the
        // font's generation under the geometry it just captured.
        auto settle = [](auto&& draw) -> bool
        {
            for (int i = 0; i < 8; ++i)
            {
                const U64 before_draw = LLFontTextCache::regenCount();
                draw();
                if (LLFontTextCache::regenCount() == before_draw)
                {
                    return true;
                }
            }
            return false;
        };

        const std::string nothing;
        LLFontTextCache blank;
        UNSCOPED_INFO("an empty cell stops rebuilding");
        CHECK(settle([&]()
        {
            blank.setSource(&nothing, 0);
            blank.renderBytes(font, nothing, 0, 12.f, 34.f, LLColor4::white,
                              LLFontGL::LEFT, LLFontGL::BASELINE,
                              LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        }));

        const std::string clipped = "Nothing of this fits";
        LLFontTextCache narrow;
        UNSCOPED_INFO("text with no room to draw stops rebuilding");
        CHECK(settle([&]()
        {
            narrow.setSource(&clipped, 0);
            narrow.renderBytes(font, clipped, 0, 12.f, 34.f, LLColor4::white,
                               LLFontGL::LEFT, LLFontGL::BASELINE,
                               LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                               S32_MAX, /*max_pixels=*/0);
        }));

        // Still a cache, not a switch that was left off: give it something to
        // draw and it builds again.
        const U64 before_visible = LLFontTextCache::regenCount();
        narrow.setSource(&clipped, 0);
        narrow.renderBytes(font, clipped, 0, 12.f, 34.f, LLColor4::white,
                           LLFontGL::LEFT, LLFontGL::BASELINE,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                           S32_MAX, /*max_pixels=*/2000);
        UNSCOPED_INFO("room to draw rebuilds");
        CHECK(LLFontTextCache::regenCount() > before_visible);
    }

    // A width and a draw come off the same glyphs, so the state the font
    // rasterizes at invalidates both. Asking whether it moved also records
    // that it has, which means only the first of the two to ask is told --
    // and a measurement taken before the draw is very common: the highlight
    // box, the ellipsis fit, the auto-resize. Without the other half being
    // dropped alongside, the draw was told nothing had moved and replayed a
    // capture whose glyphs had been rebuilt underneath it.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a resolution change a measurement saw still rebuilds the draw", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);

        ll_test::FontStateScope scope;
        font->generateASCIIglyphs();

        const std::string s = "Measured, then drawn";
        LLFontTextCache vb;

        auto draw = [&]()
        {
            vb.setSource(&s, 0);
            vb.renderBytes(font, s, 0, 12.f, 34.f, LLColor4::white,
                           LLFontGL::LEFT, LLFontGL::BASELINE,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        };
        auto measure = [&]()
        {
            vb.setSource(&s, 0);
            return vb.getWidthBytes(font, s, 0, S32_MAX, false);
        };

        bool settled = false;
        for (int i = 0; i < 8 && !settled; ++i)
        {
            const U64 before_draw = LLFontTextCache::regenCount();
            measure();
            draw();
            settled = (LLFontTextCache::regenCount() == before_draw);
        }
        UNSCOPED_INFO("measure then draw eventually replays");
        CHECK(settled);

        const S32 saved_res_gen = LLFontGL::sResolutionGeneration;
        ++LLFontGL::sResolutionGeneration;

        // The measurement asks first and is the one told.
        measure();
        const U64 before_stale_draw = LLFontTextCache::regenCount();
        draw();
        const U64 after_stale_draw = LLFontTextCache::regenCount();

        LLFontGL::sResolutionGeneration = saved_res_gen;

        UNSCOPED_INFO("a draw after a measurement saw the same change rebuilds");
        CHECK(after_stale_draw > before_stale_draw);
    }
}
