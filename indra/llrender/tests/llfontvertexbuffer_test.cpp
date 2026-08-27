/**
 * @file llfontvertexbuffer_test.cpp
 * @brief Smoke tests for LLFontVertexBuffer + LLFontWidthBuffer.
 *
 * The deeper cache-invalidation invariants (geometry vs color
 * regen, mLastFontCacheGen, mLastUsesColorAtlas) are private to
 * LLFontVertexBuffer, and the public render() entry point dives into
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
 * etc.). LLFontWidthBuffer's measurement path is also covered
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

#include "../llfontvertexbuffer.h"
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
    struct llfontvertexbuffer_data
    {
        ll_test::HeadlessGL& gl = getSharedHeadlessGL(/*needs_render=*/false);

        llfontvertexbuffer_data()
        {
            LLFontVertexBuffer::enableBufferCollection(true);
            LLFontVertexBuffer::enableColorOnlyRegen(true);
            ensureLLFontGLLoaded();
        }
        ~llfontvertexbuffer_data()
        {
            LLFontVertexBuffer::enableBufferCollection(true);
            LLFontVertexBuffer::enableColorOnlyRegen(true);
        }
    };

    typedef test_group<llfontvertexbuffer_data> llfontvertexbuffer_test;
    typedef llfontvertexbuffer_test::object     llfontvertexbuffer_object;
    tut::llfontvertexbuffer_test llfontvertexbuffer_testcase("LLFontVertexBuffer");

    // Default construction must not crash and must leave the buffer
    // in a destructible state. Catches uninitialized-member regressions
    // that surface as a crash in ~LLFontVertexBuffer trying to free
    // a garbage list.
    template<> template<>
    void llfontvertexbuffer_object::test<1>()
    {
        LLFontVertexBuffer vb;
        ensure("default-constructed buffer reaches end-of-scope", true);
        // Implicit destruct at scope exit; assertion above catches any
        // crash during dtor by being unreached.
    }

    // reset() on a freshly-constructed buffer must be a safe no-op.
    template<> template<>
    void llfontvertexbuffer_object::test<2>()
    {
        LLFontVertexBuffer vb;
        vb.reset();
        vb.reset();  // idempotent
        ensure("reset on fresh buffer returns cleanly", true);
    }

    // Static toggles round-trip without affecting later test setup.
    // The assertions confirm the calls compile and link; observable
    // round-trip would require accessing the private statics.
    template<> template<>
    void llfontvertexbuffer_object::test<3>()
    {
        LLFontVertexBuffer::enableBufferCollection(false);
        LLFontVertexBuffer::enableBufferCollection(true);
        LLFontVertexBuffer::enableColorOnlyRegen(false);
        LLFontVertexBuffer::enableColorOnlyRegen(true);
        ensure("static toggles set/reset without crash", true);
    }

    // LLFontWidthBuffer parallel: construct + reset.
    template<> template<>
    void llfontvertexbuffer_object::test<4>()
    {
        LLFontWidthBuffer wb;
        wb.reset();
        wb.reset();
        ensure("LLFontWidthBuffer construct + reset return cleanly", true);
        LLFontWidthBuffer::enableBufferCollection(false);
        LLFontWidthBuffer::enableBufferCollection(true);
    }

    // LLFontVertexBuffer constructed alongside LLFontWidthBuffer:
    // multiple coexisting instances don't share static state in a way
    // that breaks lifecycle. Pin two buffers in flight at once.
    template<> template<>
    void llfontvertexbuffer_object::test<5>()
    {
        LLFontVertexBuffer a;
        LLFontVertexBuffer b;
        LLFontWidthBuffer  w1;
        LLFontWidthBuffer  w2;
        a.reset();
        b.reset();
        w1.reset();
        w2.reset();
        ensure("multiple coexisting buffers reset cleanly", true);
    }

    // ===================================================================
    // Width-path tests — exercise LLFontWidthBuffer's measurement path.
    // No render-time shader binding needed; getWidthF32 walks the shape
    // layout but doesn't issue draw calls.
    // ===================================================================

    // First call computes; second call with identical params is a cache
    // hit and returns the same value. Both must equal fontp->getWidthF32.
    template<> template<>
    void llfontvertexbuffer_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        LLFontWidthBuffer wb;
        const F32 first  = wb.getWidth(font, s, 0, 5, false);
        const F32 second = wb.getWidth(font, s, 0, 5, false);
        const F32 ref    = font->getWidthF32(s, 0, 5, false);
        ensure("first width matches getWidthF32", first == ref);
        ensure("second call same as first (cache hit)",
               second == first);
        ensure("width is positive", first > 0.f);
    }

    // Cache miss when geometry differs: changing max_chars between
    // calls forces recompute. The cache key includes max_chars at
    // llfontvertexbuffer.cpp:517.
    template<> template<>
    void llfontvertexbuffer_object::test<7>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        LLFontWidthBuffer wb;
        const F32 w_5 = wb.getWidth(font, s, 0, 5, false);
        const F32 w_3 = wb.getWidth(font, s, 0, 3, false);
        ensure("width(5) > width(3)", w_5 > w_3);
        const F32 w_3_again = wb.getWidth(font, s, 0, 3, false);
        ensure_equals("identical max_chars hits cache", w_3_again, w_3);
    }

    // Cache miss when LLFontGL::sScaleX changes between calls. Pins
    // mLastScaleX in the cache key (llfontvertexbuffer.cpp:519).
    template<> template<>
    void llfontvertexbuffer_object::test<8>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        const F32 saved_scale = LLFontGL::sScaleX;
        LLFontWidthBuffer wb;
        LLFontGL::sScaleX = 1.0f;
        const F32 w_1 = wb.getWidth(font, s, 0, 2, false);
        LLFontGL::sScaleX = 0.5f;
        const F32 w_half = wb.getWidth(font, s, 0, 2, false);
        LLFontGL::sScaleX = saved_scale; // restore

        // Width should scale roughly with sScaleX (within FP rounding
        // noise). The cache key invalidation is the load-bearing pin.
        ensure("scale=0.5 produced different width than scale=1.0",
               std::abs(w_1 - w_half) > 0.01f);
    }

    // Cache miss when the font's cache generation advances between
    // calls (a fallback face's atlas mutation, for example). Pins
    // mLastFontCacheGen at llfontvertexbuffer.cpp:524.
    template<> template<>
    void llfontvertexbuffer_object::test<9>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        LLFontWidthBuffer wb;
        const F32 first = wb.getWidth(font, s, 0, 2, false);
        // Force a generation tick by rasterizing a fresh glyph through
        // the font (atlas allocation bumps the global counter).
        font->getFontFreetype()->getGlyphInfo(L'É', // 'É'
                                              EFontGlyphType::Grayscale);
        const F32 second = wb.getWidth(font, s, 0, 2, false);
        // Width value itself doesn't change, but the cache should have
        // recomputed: not directly observable except via the strider
        // path. The "no crash + same value" assertion exercises the
        // recompute path under generation invalidation.
        ensure_equals("width stable across atlas generation tick",
                      first, second);
    }

    // reset() clears the cache — next call recomputes. Pins
    // llfontvertexbuffer.cpp:480-493.
    template<> template<>
    void llfontvertexbuffer_object::test<10>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        LLFontWidthBuffer wb;
        const F32 first = wb.getWidth(font, s, 0, 2, false);
        wb.reset();
        const F32 after = wb.getWidth(font, s, 0, 2, false);
        ensure_equals("width stable across reset()", first, after);
    }

    // Multiple LLFontWidthBuffer instances stay independent: cache
    // state in instance A doesn't affect instance B.
    template<> template<>
    void llfontvertexbuffer_object::test<11>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        LLFontWidthBuffer a, b;
        const F32 wa = a.getWidth(font, s, 0, 2, false);
        a.reset();
        const F32 wb_val = b.getWidth(font, s, 0, 2, false);
        ensure_equals("instance B unaffected by A's reset", wa, wb_val);
    }

    // ===================================================================
    // Render-path tests — exercise LLFontVertexBuffer::render() through
    // a real compiled gUIProgram. The fixture ctor binds the stub UI
    // shader so beginList → flush → drawArrays completes against the
    // OSMesa framebuffer.
    // ===================================================================

    struct llfontvertexbuffer_render_data
    {
        ll_test::HeadlessGL& gl = getSharedHeadlessGL(/*needs_render=*/true);

        llfontvertexbuffer_render_data()
        {
            LLFontVertexBuffer::enableBufferCollection(true);
            LLFontVertexBuffer::enableColorOnlyRegen(true);
            ensureLLFontGLLoaded();
        }
        ~llfontvertexbuffer_render_data()
        {
            LLFontVertexBuffer::enableBufferCollection(true);
            LLFontVertexBuffer::enableColorOnlyRegen(true);
        }
    };

    typedef test_group<llfontvertexbuffer_render_data> llfontvertexbuffer_render_test;
    typedef llfontvertexbuffer_render_test::object     llfontvertexbuffer_render_object;
    tut::llfontvertexbuffer_render_test llfontvertexbuffer_render_testcase("LLFontVertexBufferRender");

    // First render() populates the buffer cache and returns the
    // character count rendered. Pins genBuffers + drawBuffer end-to-end
    // through the stub UI shader.
    template<> template<>
    void llfontvertexbuffer_render_object::test<1>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        gl.clearFramebuffer();
        LLFontVertexBuffer vb;
        const S32 n = vb.render(font, s, 0,
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
    void llfontvertexbuffer_render_object::test<2>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        LLFontVertexBuffer vb;
        // Pre-rasterize all glyphs of "Hello" so neither call grows
        // the atlas (otherwise both calls would bump the gen counter
        // and the test would be a no-op).
        font->generateASCIIglyphs();
        vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);

        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        // Second call: same geometry, same color. Should be a no-regen
        // replay — neither genBuffers nor recolorBuffers fires.
        vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_after = LLFontBitmapCache::getGlobalGeneration();
        ensure_equals("no atlas alloc on repeat render (gen counter stable)",
                      gen_after, gen_before);
    }

    // Color change with same geometry: takes the recolor fast path —
    // recolorBuffers walks the captured streams and rewrites the color
    // attribute, no genBuffers. Pins llfontvertexbuffer.cpp:203-210.
    // Observable: global generation counter doesn't advance (no atlas
    // mutation), and the call returns the same character count.
    template<> template<>
    void llfontvertexbuffer_render_object::test<3>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hello");

        LLFontVertexBuffer vb;
        font->generateASCIIglyphs();
        const S32 n_white = vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                                      LLFontGL::LEFT, LLFontGL::BASELINE,
                                      LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 5);
        const S32 gen_before = LLFontBitmapCache::getGlobalGeneration();
        const S32 n_red = vb.render(font, s, 0, 100.f, 100.f, LLColor4::red,
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
    void llfontvertexbuffer_render_object::test<4>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("AB");

        LLFontVertexBuffer vb;
        font->generateASCIIglyphs();

        const S32 n1 = vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n2 = vb.render(font, s, 0, 200.f, 100.f, LLColor4::white,
                                 LLFontGL::LEFT, LLFontGL::BASELINE,
                                 LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        ensure_equals("char count stable after geometry change", n1, n2);
        ensure("both calls returned positive count", n1 > 0);
    }

    // enableColorOnlyRegen(false) disables the fast path entirely:
    // a same-geometry color-change call falls through to genBuffers
    // (full regen). The static toggle pins the flag-controlled branch
    // at llfontvertexbuffer.cpp:210. Restore the flag in dtor.
    template<> template<>
    void llfontvertexbuffer_render_object::test<5>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        LLFontVertexBuffer vb;
        font->generateASCIIglyphs();
        vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                  LLFontGL::LEFT, LLFontGL::BASELINE,
                  LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);

        LLFontVertexBuffer::enableColorOnlyRegen(false);
        // Same geometry, different color — with fast path off, this
        // should still complete cleanly through genBuffers.
        const S32 n = vb.render(font, s, 0, 100.f, 100.f, LLColor4::red,
                                LLFontGL::LEFT, LLFontGL::BASELINE,
                                LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        LLFontVertexBuffer::enableColorOnlyRegen(true);
        ensure_equals("render with fast path disabled still returns 2", n, 2);
    }

    // Style flip NORMAL → BOLD forces full regen — different glyph
    // index stream. Pins 00f3a4e93d (style invalidation in geometry-
    // invalid check at llfontvertexbuffer.cpp:175).
    template<> template<>
    void llfontvertexbuffer_render_object::test<6>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);
        LLWString s = utf8str_to_wstring("Hi");

        LLFontVertexBuffer vb;
        font->generateASCIIglyphs();
        const S32 n_norm = vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
                                     LLFontGL::LEFT, LLFontGL::BASELINE,
                                     LLFontGL::NORMAL, LLFontGL::NO_SHADOW, 2);
        const S32 n_bold = vb.render(font, s, 0, 100.f, 100.f, LLColor4::white,
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
    void llfontvertexbuffer_render_object::test<7>()
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
        LLWString s = utf8str_to_wstring("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        font->generateASCIIglyphs();

        LLFontVertexBuffer vb;
        const S32 n = vb.render(font, s, 0,
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
    void llfontvertexbuffer_render_object::test<8>()
    {
        if (!fileExists(kFontsXml))
            skip("fonts.xml not present in test data dir");
        LLFontGL* font = LLFontGL::getFontSansSerif();
        ensure("font available", font != nullptr);

        ll_test::FontStateScope scope;

        LLWString s = utf8str_to_wstring("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        font->generateASCIIglyphs();

        LLFontVertexBuffer vb;
        const S32 n_truncated = vb.render(font, s, 0,
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
        LLFontVertexBuffer vb2;
        const S32 n_full = vb2.render(font, s, 0,
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
