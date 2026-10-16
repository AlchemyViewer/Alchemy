/**
 * @file llfonttextcache_test.cpp
 * @brief Tests for LLFontTextCache, which keeps what one piece of text
 *        costs to draw and to measure.
 *
 * Its lifecycle (construct, reset, destroy) and its static toggles; its
 * width slots, held against the font's own measurement; and its draws,
 * through the shared context's UI program. Which way a draw went -- a
 * replay, a recolor in place, or a rebuild -- is private to the cache,
 * and is read off LLFontTextCache::regenCount(), which only a rebuild
 * moves, and off the pixels the draw left.
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
    // Shared GL context + LLFontGL bring-up. One per binary. Catch2
    // creates a fresh fixture per test case; per-fixture HeadlessGL
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

    // Draws until a draw replays rather than rebuilds, as the viewer's frames
    // do, at most eight times. A first draw can rasterize a glyph that shaping
    // substituted, which moves the font's cache generation under the geometry
    // it just captured, and that geometry is deliberately not trusted for a
    // second frame. A cache that never replays is left for the checks after
    // this to report.
    template <typename DRAW>
    void settleDraws(DRAW&& draw)
    {
        for (int i = 0; i < 8; ++i)
        {
            const U64 before = LLFontTextCache::regenCount();
            draw();
            if (LLFontTextCache::regenCount() == before)
            {
                return;
            }
        }
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
    // a garbage list. There is nothing to ask of it: the case passes by
    // returning, and a crash in the destructor is its failure.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: a default-constructed cache destroys cleanly", "[LLFontTextCache]")
    {
        LLFontTextCache vb;
    }

    // reset() on a freshly-constructed buffer must be a safe no-op. The case
    // passes by returning.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: reset on a fresh cache is safe to call twice", "[LLFontTextCache]")
    {
        LLFontTextCache vb;
        vb.reset();
        vb.reset();  // idempotent
    }

    // The static toggles take effect, and turning one back on restores what
    // it turned off, which every later case relies on. Their statics are
    // private, so each is read off what a draw does: with buffer collection
    // off a draw goes straight to the font and captures nothing, so nothing
    // is rebuilt; with the colour-only fast path on, a colour change is
    // recolored in place rather than rebuilt. What the fast path does when it
    // is off is the render group's to check.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: enableBufferCollection and enableColorOnlyRegen toggle off and on", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        font->generateASCIIglyphs();
        const std::string s = "Hi";

        auto draw = [&](LLFontTextCache& cache, const LLColor4& color)
        {
            cache.renderBytes(font, s, 0, 100.f, 100.f, color,
                              LLFontGL::LEFT, LLFontGL::BASELINE,
                              LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
            // An uncollected draw leaves its quads in gGL's batch.
            gGL.flush();
        };

        LLFontTextCache::enableBufferCollection(false);
        LLFontTextCache off;
        const U64 uncollected = LLFontTextCache::regenCount();
        draw(off, LLColor4::white);
        UNSCOPED_INFO("with buffer collection off a draw captures nothing");
        CHECK(LLFontTextCache::regenCount() == uncollected);

        // A fresh cache, whose first draw is a capture whenever collection is on.
        LLFontTextCache::enableBufferCollection(true);
        LLFontTextCache on;
        const U64 collected = LLFontTextCache::regenCount();
        draw(on, LLColor4::white);
        UNSCOPED_INFO("turned back on, a draw is captured");
        CHECK(LLFontTextCache::regenCount() > collected);

        LLFontTextCache::enableColorOnlyRegen(false);
        LLFontTextCache::enableColorOnlyRegen(true);
        settleDraws([&] { draw(on, LLColor4::white); });
        const U64 before_recolor = LLFontTextCache::regenCount();
        draw(on, LLColor4::red);
        UNSCOPED_INFO("turned back on, the colour-only fast path recolors without a rebuild");
        CHECK(LLFontTextCache::regenCount() == before_recolor);
    }

    // Several caches in flight at once: none shares state with another in a
    // way that breaks construction, reset or destruction. The case passes by
    // returning; that one cache's reset leaves another's answers alone is
    // checked in the width group.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: four caches in flight at once each reset cleanly", "[LLFontTextCache]")
    {
        LLFontTextCache a;
        LLFontTextCache b;
        LLFontTextCache c;
        LLFontTextCache d;
        a.reset();
        b.reset();
        c.reset();
        d.reset();
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
    // calls (a fallback face's atlas mutation, for example): the width the
    // cache holds is measured again. Whether it was is read off the shaper,
    // which a measurement asks and an answer from the cache's own slot does
    // not -- so with the shape cache emptied, a measurement shows as a shape.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: getWidthBytes measures again after a generation tick and agrees", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const LLFontFreetype* ft = font->getFontFreetype();
        REQUIRE(ft != nullptr);
        REQUIRE(ft->getFontFace() != nullptr);
        const std::string s = "Hi";

        // A glyph of the font's own face that nothing has rasterized yet, so
        // that asking for it has to take an atlas slot. A fixed one could
        // already have been drawn by any case before this.
        llwchar fresh = 0;
        for (llwchar cp = 0x100; cp < 0x180 && fresh == 0; ++cp)
        {
            const U32 index = ft->getCharGlyphIndex(cp);
            if (index != 0 && !ft->getFontFace()->findGlyphInfo(index, EFontGlyphType::Grayscale))
            {
                fresh = cp;
            }
        }
        if (fresh == 0)
            SKIP("every Latin Extended-A glyph is already rasterized");

        LLFontTextCache wb;
        const F32 first = wb.getWidthBytes(font, s, 0, 2, false);

        // Nothing moved, so a repeat is answered from the cache's slot.
        ALFontShaping::clearCache();
        const size_t shaped_before_repeat = ALFontShaping::cacheMutationCount();
        (void)wb.getWidthBytes(font, s, 0, 2, false);
        UNSCOPED_INFO("with nothing changed the cache answers without measuring");
        CHECK(ALFontShaping::cacheMutationCount() == shaped_before_repeat);

        // Force a generation tick by rasterizing the fresh glyph through the
        // font: its atlas allocation moves the font's cache generation.
        const U64 gen_before = font->getCacheGeneration();
        ft->getGlyphInfo(fresh, EFontGlyphType::Grayscale);
        UNSCOPED_INFO("rasterizing a new glyph moves the font's cache generation");
        CHECK(font->getCacheGeneration() > gen_before);

        const size_t shaped_before_tick = ALFontShaping::cacheMutationCount();
        const F32 second = wb.getWidthBytes(font, s, 0, 2, false);
        UNSCOPED_INFO("after the tick the cache measures again");
        CHECK(ALFontShaping::cacheMutationCount() > shaped_before_tick);
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
    // state in instance A doesn't affect instance B. The two hold different
    // texts over the same span, and width slots are keyed on the span, so a
    // slot the two shared would hand one of them the other's width.
    TEST_CASE_METHOD(llfonttextcache_data, "LLFontTextCache: one cache's reset leaves another's width unchanged", "[LLFontTextCache]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string sa = "Hi";
        const std::string sb = "WW";

        LLFontTextCache a, b;
        const F32 wa = a.getWidthBytes(font, sa, 0, 2, false);
        const F32 wb_val = b.getWidthBytes(font, sb, 0, 2, false);
        UNSCOPED_INFO("the two texts differ in width, so a shared slot would show");
        CHECK(wa != wb_val);
        UNSCOPED_INFO("instance B answers for its own text");
        CHECK(wb_val == font->getWidthF32Bytes(sb, 0, 2, false));

        // Whether B kept its answer is read off the shaper: with the shape
        // cache emptied, B measuring again would shape, and B answering from
        // its own slot does not.
        a.reset();
        ALFontShaping::clearCache();
        const size_t shaped = ALFontShaping::cacheMutationCount();
        const F32 wb_again = b.getWidthBytes(font, sb, 0, 2, false);
        UNSCOPED_INFO("instance B unaffected by A's reset: it answers from its own slot");
        CHECK(ALFontShaping::cacheMutationCount() == shaped);
        CHECK(wb_again == wb_val);
        UNSCOPED_INFO("and A answers for its own text again");
        CHECK(a.getWidthBytes(font, sa, 0, 2, false) == wa);
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

    // An identical render replays: same geometry, same colour, so the second
    // call has nothing to rebuild and nothing to recolor, and the atlas is
    // not touched. The global generation counter must NOT advance across the
    // repeat, and the rebuild count must not move.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a repeated identical render replays and allocates nothing", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache vb;
        // Pre-rasterize all glyphs of "Hello" so the repeat cannot grow
        // the atlas (otherwise it would bump the gen counter and the
        // test would be a no-op).
        font->generateASCIIglyphs();
        settleDraws([&] {
            vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                           LLFontGL::LEFT, LLFontGL::BASELINE,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        });

        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        const U64 regen_before = LLFontTextCache::regenCount();
        // Same geometry, same color. Should be a no-regen replay —
        // neither genBuffers nor recolorBuffers fires.
        vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_after = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("the repeat replays: nothing is rebuilt");
        CHECK(LLFontTextCache::regenCount() == regen_before);
        UNSCOPED_INFO("no atlas alloc on repeat render (gen counter stable)");
        CHECK(gen_after == gen_before);
    }

    // Color change with same geometry: takes the recolor fast path —
    // recolorBuffers walks the captured streams and rewrites the color
    // attribute, no genBuffers. Observable: nothing is rebuilt, the global
    // generation counter doesn't advance (no atlas mutation), the call
    // returns the same character count, and the text comes out in the new
    // colour -- which a replay of the white capture would not.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a color change alone recolors in place without rebuilding or allocating", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        S32 n_white = 0;
        settleDraws([&] {
            n_white = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        });

        gl.clearFramebuffer();
        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        const U64 regen_before = LLFontTextCache::regenCount();
        const S32 n_red = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::red,
                                    LLFontGL::LEFT, LLFontGL::BASELINE,
                                    LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_after = LLFontBitmapCache::getGlobalGeneration();
        const U64 regen_after = LLFontTextCache::regenCount();

        // Over a black frame, red text leaves only red; anything with green or
        // blue in it is the white the geometry was captured in.
        glFinish();
        const std::vector<U8> px = ll_test::readFramebufferRGBA(ll_test::HeadlessGL::WIDTH,
                                                                ll_test::HeadlessGL::HEIGHT);
        size_t red = 0;
        size_t not_red = 0;
        for (size_t i = 0; i + 3 < px.size(); i += 4)
        {
            if (px[i + 1] != 0 || px[i + 2] != 0)
            {
                ++not_red;
            }
            else if (px[i] != 0)
            {
                ++red;
            }
        }

        UNSCOPED_INFO("color change preserves char count");
        CHECK(n_red == n_white);
        UNSCOPED_INFO("recolor fast path: nothing rebuilt");
        CHECK(regen_after == regen_before);
        UNSCOPED_INFO("recolor fast path: no atlas mutation");
        CHECK(gen_after == gen_before);
        UNSCOPED_INFO("the text is drawn in the new colour");
        CHECK(red > 0u);
        UNSCOPED_INFO("and nothing is left of the colour it was captured in");
        CHECK(not_red == 0u);
    }

    // Geometry change forces full regen: render at x=100, then at
    // x=200. The global generation counter still shouldn't advance
    // (nothing about geometry change touches the atlas), but the buffer
    // cache has to rebuild, and the run it draws ends where the second
    // position puts it (right_x) rather than where the first capture did.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a render moved to another x rebuilds there and allocates nothing", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "AB";

        LLFontTextCache vb;
        font->generateASCIIglyphs();

        S32 n1 = 0;
        F32 right1 = 0.f;
        settleDraws([&] {
            n1 = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2, S32_MAX, &right1);
        });
        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        const U64 regen_before = LLFontTextCache::regenCount();
        F32 right2 = 0.f;
        const S32 n2 = vb.renderBytes(font, s, 0, 200.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2, S32_MAX, &right2);
        UNSCOPED_INFO("char count stable after geometry change");
        CHECK(n1 == n2);
        CHECK(n1 > 0);
        UNSCOPED_INFO("moving the text rebuilds it");
        CHECK(LLFontTextCache::regenCount() > regen_before);
        UNSCOPED_INFO("a move allocates nothing in the atlas");
        CHECK(LLFontBitmapCache::getGlobalGeneration() == gen_before);
        UNSCOPED_INFO("the moved run ends 100 px further right");
        CHECK(right2 == right1 + 100.f);
    }

    // enableColorOnlyRegen(false) disables the fast path entirely:
    // a same-geometry color-change call falls through to genBuffers
    // (full regen), where with it on the change is recolored in place.
    // The fixture puts the flag back whatever happens here.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: with enableColorOnlyRegen off a color change rebuilds every byte", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        settleDraws([&] {
            vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                           LLFontGL::LEFT, LLFontGL::BASELINE,
                           LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        });

        LLFontTextCache::enableColorOnlyRegen(false);
        // Same geometry, different color — with fast path off, this
        // goes through genBuffers.
        const U64 regen_before = LLFontTextCache::regenCount();
        const S32 n = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::red,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const U64 regen_after = LLFontTextCache::regenCount();
        LLFontTextCache::enableColorOnlyRegen(true);
        UNSCOPED_INFO("with the fast path off a colour change rebuilds");
        CHECK(regen_after > regen_before);
        UNSCOPED_INFO("render with fast path disabled still returns 2");
        CHECK(n == 2);
    }

    // Style flip NORMAL → BOLD forces full regen — different glyph
    // index stream. Pins 00f3a4e93d (style invalidation in the cache's
    // geometry check); where the registry has a bold face the flip also
    // changes the font drawing it, which rebuilds as well.
    TEST_CASE_METHOD(llfonttextcache_render_data, "LLFontTextCacheRender: a style flip from NORMAL to BOLD rebuilds and keeps the count", "[LLFontTextCacheRender]")
    {
        if (!fileExists(kFontsXml))
            SKIP("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        REQUIRE(font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        S32 n_norm = 0;
        settleDraws([&] {
            n_norm = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                    LLFontGL::LEFT, LLFontGL::BASELINE,
                                    LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        });
        const U64 regen_before = LLFontTextCache::regenCount();
        const S32 n_bold = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::BOLD, LLFontGL::NO_SHADOW, 2);
        UNSCOPED_INFO("the style flip rebuilds");
        CHECK(LLFontTextCache::regenCount() > regen_before);
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
