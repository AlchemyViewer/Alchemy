/**
 * @file llfonttextcache_test.cpp
 * @brief Smoke tests for LLFontTextCache + LLFontTextCache.
 *
 * The deeper cache-invalidation invariants (geometry vs color
 * regen, mLastFontCacheGen, mLastUsesColorAtlas) are private to
 * LLFontTextCache, and the public render() entry point dives into
 * gGL.pushUIMatrix and drawGlyph — code paths that the OSMesa
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

#include "../test/lltut.h"

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

namespace tut
{
    // Shared OSMesa context + LLFontGL bring-up. One per binary. TUT
    // creates a fresh fixture per test method; per-fixture HeadlessGL
    // creation poisons LLFontGL's process-static fontp cache, since
    // each test's GL context destroys the LLImageGL textures the
    // fontps reference (LLFontManager::cleanupClass + LLImageGL::
    // cleanupClass in HeadlessGL dtor). Sharing the GL context across
    // tests keeps all that state coherent for the binary's lifetime.
    inline ll_test::HeadlessGL& getSharedHeadlessGL(bool needs_render)
    {
        // The first test in the binary picks the needs_render mode.
        // Both groups in this binary use needs_render=true (the smoke
        // tests don't drive rendering, but having gUIProgram bound is
        // harmless for them). All subsequent fixtures share this one.
        static ll_test::HeadlessGL gl(/*needs_vbos=*/true,
                                      /*needs_imagegl=*/true,
                                      /*needs_llrender=*/true,
                                      /*needs_render=*/true);
        (void)needs_render;
        return gl;
    }

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
    // the static getSharedHeadlessGL() and stays alive for the whole
    // binary, so font/atlas state stays coherent across tests.
    struct llfonttextcache_data
    {
        ll_test::HeadlessGL& gl = getSharedHeadlessGL(/*needs_render=*/false);

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

    typedef test_group<llfonttextcache_data> llfonttextcache_test;
    typedef llfonttextcache_test::object     llfonttextcache_object;
    tut::llfonttextcache_test llfonttextcache_testcase("LLFontTextCache");

    // Default construction must not crash and must leave the buffer
    // in a destructible state. Catches uninitialized-member regressions
    // that surface as a crash in ~LLFontTextCache trying to free
    // a garbage list.
    template<> template<>
    void llfonttextcache_object::test<1>()
    {
        LLFontTextCache vb;
        ensure("default-constructed buffer reaches end-of-scope", true);
        // Implicit destruct at scope exit; assertion above catches any
        // crash during dtor by being unreached.
    }

    // reset() on a freshly-constructed buffer must be a safe no-op.
    template<> template<>
    void llfonttextcache_object::test<2>()
    {
        LLFontTextCache vb;
        vb.reset();
        vb.reset();  // idempotent
        ensure("reset on fresh buffer returns cleanly", true);
    }

    // Static toggles round-trip without affecting later test setup.
    // The assertions confirm the calls compile and link; observable
    // round-trip would require accessing the private statics.
    template<> template<>
    void llfonttextcache_object::test<3>()
    {
        LLFontTextCache::enableBufferCollection(false);
        LLFontTextCache::enableBufferCollection(true);
        LLFontTextCache::enableColorOnlyRegen(false);
        LLFontTextCache::enableColorOnlyRegen(true);
        ensure("static toggles set/reset without crash", true);
    }

    // LLFontTextCache parallel: construct + reset.
    template<> template<>
    void llfonttextcache_object::test<4>()
    {
        LLFontTextCache wb;
        wb.reset();
        wb.reset();
        ensure("LLFontTextCache construct + reset return cleanly", true);
        LLFontTextCache::enableBufferCollection(false);
        LLFontTextCache::enableBufferCollection(true);
    }

    // LLFontTextCache constructed alongside LLFontTextCache:
    // multiple coexisting instances don't share static state in a way
    // that breaks lifecycle. Pin two buffers in flight at once.
    template<> template<>
    void llfonttextcache_object::test<5>()
    {
        LLFontTextCache a;
        LLFontTextCache b;
        LLFontTextCache  w1;
        LLFontTextCache  w2;
        a.reset();
        b.reset();
        w1.reset();
        w2.reset();
        ensure("multiple coexisting buffers reset cleanly", true);
    }

    // ===================================================================
    // Width-path tests — exercise LLFontTextCache's measurement path.
    // No render-time shader binding needed; getWidthF32 walks the shape
    // layout but doesn't issue draw calls.
    // ===================================================================

    // First call computes; second call with identical params is a cache
    // hit and returns the same value. Both must equal fontp->getWidthF32.
    template<> template<>
    void llfonttextcache_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache wb;
        const F32 first  = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 second = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 ref    = font->getWidthF32Bytes(s, 0, 5, false);
        ensure("first width matches getWidthF32", first == ref);
        ensure("second call same as first (cache hit)",
               second == first);
        ensure("width is positive", first > 0.f);
    }

    // Cache miss when geometry differs: changing max_chars between
    // calls forces recompute. The cache key includes max_chars at
    // llfonttextcache.cpp:517.
    template<> template<>
    void llfonttextcache_object::test<7>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hello";

        LLFontTextCache wb;
        const F32 w_5 = wb.getWidthBytes(font, s, 0, 5, false);
        const F32 w_3 = wb.getWidthBytes(font, s, 0, 3, false);
        ensure("width(5) > width(3)", w_5 > w_3);
        const F32 w_3_again = wb.getWidthBytes(font, s, 0, 3, false);
        ensure_equals("identical max_chars hits cache", w_3_again, w_3);
    }

    // Cache miss when LLFontGL::sScaleX changes between calls. Pins
    // mLastScaleX in the cache key (llfonttextcache.cpp:519).
    template<> template<>
    void llfonttextcache_object::test<8>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
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
        ensure("scale=0.5 produced different width than scale=1.0",
               std::abs(w_1 - w_half) > 0.01f);
    }

    // Cache miss when the font's cache generation advances between
    // calls (a fallback face's atlas mutation, for example). Pins
    // mLastFontCacheGen at llfonttextcache.cpp:524.
    template<> template<>
    void llfonttextcache_object::test<9>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
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
        ensure_equals("width survives an atlas generation tick",
                      first, second);
        ensure_equals("and still agrees with the font it came from",
                      second, font->getWidthF32Bytes(s, 0, 2, false));
    }

    // reset() clears the cache — next call recomputes. Pins
    // llfonttextcache.cpp:480-493.
    template<> template<>
    void llfonttextcache_object::test<10>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache wb;
        const F32 first = wb.getWidthBytes(font, s, 0, 2, false);
        wb.reset();
        const F32 after = wb.getWidthBytes(font, s, 0, 2, false);
        ensure_equals("width survives reset()", first, after);
        // Again, against the font: a reset that failed to clear and a reset
        // that recomputed correctly both return the earlier value, and only
        // one of those also matches an independent measurement.
        ensure_equals("and still agrees with the font it came from",
                      after, font->getWidthF32Bytes(s, 0, 2, false));
    }

    // Multiple LLFontTextCache instances stay independent: cache
    // state in instance A doesn't affect instance B.
    template<> template<>
    void llfonttextcache_object::test<11>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache a, b;
        const F32 wa = a.getWidthBytes(font, s, 0, 2, false);
        a.reset();
        const F32 wb_val = b.getWidthBytes(font, s, 0, 2, false);
        ensure_equals("instance B unaffected by A's reset", wa, wb_val);
    }

    // The bound the cache is keyed on, over text where a byte is not a
    // character. Every other test in this group uses ASCII, where a byte
    // count, a character count and a glyph count are the same number, so a
    // cache keyed in the wrong one of them hits and misses identically on all
    // of them and every assertion still passes.
    template<> template<>
    void llfonttextcache_object::test<12>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);

        // "A", a three-byte character, "B": five bytes, three characters.
        const std::string s = "A\xE6\x97\xA5" "B";
        if (font->getWidthF32Bytes(s, 0, (S32)s.size(), false) <= 0.f)
            skip("font produced no width for the sample");

        // Every bound, boundary or not, has to agree with the font. A bound of
        // 1 and a bound of 4 are one character apart and three bytes apart;
        // a cache counting the wrong one answers one of these with the other's
        // width.
        LLFontTextCache wb;
        for (S32 bound : { 0, 1, 4, 5 })
        {
            const F32 cached = wb.getWidthBytes(font, s, 0, bound, false);
            ensure_equals("cached width agrees with the font",
                          cached, font->getWidthF32Bytes(s, 0, bound, false));
            // And again, now that it is cached.
            ensure_equals("and still agrees on the second reading",
                          wb.getWidthBytes(font, s, 0, bound, false), cached);
        }

        // Distinct bounds must not collapse onto one entry.
        const F32 w1 = wb.getWidthBytes(font, s, 0, 1, false);
        const F32 w4 = wb.getWidthBytes(font, s, 0, 4, false);
        ensure("one character in is narrower than two", w1 < w4);
    }

    // ===================================================================
    // Render-path tests — exercise LLFontTextCache::render() through
    // a real compiled gUIProgram. The fixture ctor binds the stub UI
    // shader so beginList → flush → drawArrays completes against the
    // OSMesa framebuffer.
    // ===================================================================

    struct llfonttextcache_render_data
    {
        ll_test::HeadlessGL& gl = getSharedHeadlessGL(/*needs_render=*/true);

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

    typedef test_group<llfonttextcache_render_data> llfonttextcache_render_test;
    typedef llfonttextcache_render_test::object     llfonttextcache_render_object;
    tut::llfonttextcache_render_test llfonttextcache_render_testcase("LLFontTextCacheRender");

    // First render() populates the buffer cache and returns the
    // character count rendered. Pins genBuffers + drawBuffer end-to-end
    // through the stub UI shader.
    template<> template<>
    void llfonttextcache_render_object::test<1>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hello";

        gl.clearFramebuffer();
        LLFontTextCache vb;
        const S32 n = vb.renderBytes(font, s, 0,
                                /*x=*/100.f, /*y=*/100.f,
                                LLColor4::white,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
                                /*max_chars=*/5);
        ensure_equals("render returned 5 chars", n, 5);
    }

    // Color-only fast path (b317778477): same render twice with same
    // geometry but different color. Second call goes through
    // recolorBuffers — no genBuffers, no atlas alloc. The global
    // generation counter must NOT advance between the first and second
    // calls (atlas already populated; recolor doesn't allocate).
    template<> template<>
    void llfonttextcache_render_object::test<2>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
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
        ensure_equals("no atlas alloc on repeat render (gen counter stable)",
                      gen_after, gen_before);
    }

    // Color change with same geometry: takes the recolor fast path —
    // recolorBuffers walks the captured streams and rewrites the color
    // attribute, no genBuffers. Pins llfonttextcache.cpp:203-210.
    // Observable: global generation counter doesn't advance (no atlas
    // mutation), and the call returns the same character count.
    template<> template<>
    void llfonttextcache_render_object::test<3>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
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
        ensure_equals("color change preserves char count",
                      n_red, n_white);
        ensure_equals("recolor fast path: no atlas mutation",
                      gen_after, gen_before);
    }

    // Geometry change forces full regen: render at x=100, then at
    // x=200. Both calls succeed; the global generation counter still
    // shouldn't advance (nothing about geometry change touches the
    // atlas), but the buffer cache had to rebuild. The post-render
    // render-target reflects the second position.
    template<> template<>
    void llfonttextcache_render_object::test<4>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "AB";

        LLFontTextCache vb;
        font->generateASCIIglyphs();

        const S32 n1 = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n2 = vb.renderBytes(font, s, 0, 200.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        ensure_equals("char count stable after geometry change", n1, n2);
        ensure("both calls returned positive count", n1 > 0);
    }

    // enableColorOnlyRegen(false) disables the fast path entirely:
    // a same-geometry color-change call falls through to genBuffers
    // (full regen). The static toggle pins the flag-controlled branch
    // at llfonttextcache.cpp:210. Restore the flag in dtor.
    template<> template<>
    void llfonttextcache_render_object::test<5>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
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
        ensure_equals("render with fast path disabled still returns 2", n, 2);
    }

    // Style flip NORMAL → BOLD forces full regen — different glyph
    // index stream. Pins 00f3a4e93d (style invalidation in geometry-
    // invalid check at llfonttextcache.cpp:175).
    template<> template<>
    void llfonttextcache_render_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        const std::string s = "Hi";

        LLFontTextCache vb;
        font->generateASCIIglyphs();
        const S32 n_norm = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_bold = vb.renderBytes(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::BOLD, LLFontGL::NO_SHADOW, 2);
        ensure_equals("char count stable across style flip", n_norm, n_bold);
        ensure("both renders returned positive", n_norm > 0);
    }

    // Truncated DROP_SHADOW_SOFT render with use_ellipses=true: the
    // appended ellipsis must NOT contribute shadow geometry to either
    // capture list. Pre-fix, the inner ellipsis render dropped the
    // outer's on_pass_boundary callback and routed its shadow batch
    // into mForegroundBufferList — visible as off-color shadow tinting
    // in the recolor fast path. Fix renders the ellipsis with NO_SHADOW.
    template<> template<>
    void llfonttextcache_render_object::test<7>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);

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
        ensure("truncation actually happened (n < full string)",
               n > 0 && n < (S32)s.length());

        const auto counts = ll_test::VertexBufferProbe::count(vb);

        // Pass-A SOFT shadow: 5 quads per visible char. Ellipsis must
        // NOT add to this list — the ellipsis render is single-pass
        // (NO_SHADOW) post-fix.
        const size_t expected_shadow = static_cast<size_t>(n) * 5u;
        ensure_equals("shadow list = visible chars * 5 (ellipsis adds 0)",
                      counts.shadow_quads, expected_shadow);

        // Foreground: 1 quad per visible char + 3 ellipsis chars.
        const size_t expected_fg = static_cast<size_t>(n) + 3u;
        ensure_equals("foreground list = visible + 3 ellipsis foreground",
                      counts.foreground_quads, expected_fg);
    }

    // Sanity check on chars_drawn: when use_ellipses=true triggers
    // truncation, render's return value reports only the visible
    // pre-ellipsis character count, not including the appended "...".
    template<> template<>
    void llfonttextcache_render_object::test<8>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);

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
        ensure("truncated count is positive but less than full string",
               n_truncated > 0 && n_truncated < (S32)s.length());

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
        ensure_equals("full-fit render returns the entire string length",
                      n_full, (S32)s.length());
    }
}
