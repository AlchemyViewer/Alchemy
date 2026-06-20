/**
 * @file llfontgpubatch_test.cpp
 * @brief Unit tests for LLFontGpuBatch — analytic (hb-gpu) glyph geometry and
 *        end-to-end draw. test<1> checks the CPU quad layout; test<2> renders a
 *        glyph into an OSMesa framebuffer and reads back the coverage.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../llfontgpubatch.h"
#include "../llfontgpuglyphcache.h"

#if LL_HAS_HB_GPU

#include <hb.h>

#include <cmath>
#include <cstdio>
#include <string>

#if LL_MESA_HEADLESS
#  include "../llfontgpushader.h"
#  include "../llglslshader.h"
#  include "llheadlessgl_fixture.h"
#endif

namespace
{
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif
    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;
    constexpr const char* kOutlineFont = "IBMPlexMono-Regular.ttf";

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    struct TestFace
    {
        hb_blob_t* blob = nullptr;
        hb_face_t* face = nullptr;
        TestFace()
        {
            const std::string path = std::string(kFontDir) + kOutlineFont;
            if (!fileExists(path)) return;
            blob = hb_blob_create_from_file(path.c_str());
            if (hb_blob_get_length(blob) > 0) face = hb_face_create(blob, 0);
        }
        ~TestFace()
        {
            if (face) hb_face_destroy(face);
            if (blob) hb_blob_destroy(blob);
        }
        bool valid() const { return face != nullptr; }
    };

    U32 gidFor(hb_face_t* face, hb_codepoint_t cp)
    {
        hb_font_t* f = hb_font_create(face);
        hb_codepoint_t g = 0;
        hb_font_get_nominal_glyph(f, cp, &g);
        hb_font_destroy(f);
        return (U32)g;
    }

    bool approx(F32 a, F32 b, F32 eps = 0.01f) { return std::fabs(a - b) <= eps; }
}

namespace tut
{
    struct llfontgpubatch_data
    {
    };

    typedef test_group<llfontgpubatch_data> llfontgpubatch_test;
    typedef llfontgpubatch_test::object      llfontgpubatch_object;
    tut::llfontgpubatch_test llfontgpubatch_testcase("LLFontGpuBatch");

    // (1) CPU geometry: one drawable glyph -> 6 vertices obeying the
    // position == pen + texcoord * scale invariant, with the right glyphLoc,
    // jacobian, color, and em-space span. A non-drawable glyph yields nothing.
    template<> template<>
    void llfontgpubatch_object::test<1>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        LLFontGpuGlyphCache cache;
        cache.init(tf.face);

        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_A);
        ensure("'A' drawable", loc.drawable());

        LLFontGpuBatch batch;
        const F32 scale = 0.05f;

        LLFontGpuBatch::Placement p;
        p.glyph_id = gid_A;
        p.pen_x = 100.f;
        p.pen_y = 80.f;
        p.color[0] = 10; p.color[1] = 20; p.color[2] = 30; p.color[3] = 255;

        const U32 n = batch.buildVertices(cache, { p }, scale);
        ensure_equals("one glyph -> 6 vertices", n, 6U);

        const auto& v = batch.vertices();
        F32 min_u = 1e9f, max_u = -1e9f, min_w = 1e9f, max_w = -1e9f;
        for (const auto& vert : v)
        {
            // The core invariant tying the screen quad to the em-space coords.
            ensure("pos.x == pen_x + tc.x*scale", approx(vert.pos[0], p.pen_x + vert.tc[0] * scale));
            ensure("pos.y == pen_y + tc.y*scale", approx(vert.pos[1], p.pen_y + vert.tc[1] * scale));
            ensure_equals("glyphLoc matches cache", vert.glyphLoc, loc.mTexelOffset);
            ensure("color preserved", vert.col[0] == 10 && vert.col[1] == 20 && vert.col[2] == 30 && vert.col[3] == 255);

            min_u = std::min(min_u, vert.tc[0]); max_u = std::max(max_u, vert.tc[0]);
            min_w = std::min(min_w, vert.tc[1]); max_w = std::max(max_w, vert.tc[1]);
        }
        // The quad spans exactly the glyph's design-unit extents.
        ensure("tc x-span == width", approx(max_u - min_u, (F32)std::abs(loc.mWidth), 0.5f));
        ensure("tc y-span == |height|", approx(max_w - min_w, (F32)std::abs(loc.mHeight), 0.5f));

        // Non-drawable glyph (space) emits no geometry.
        const U32 gid_sp = gidFor(tf.face, (hb_codepoint_t)' ');
        LLFontGpuBatch::Placement sp;
        sp.glyph_id = gid_sp;
        ensure_equals("space -> 0 vertices", batch.buildVertices(cache, { sp }, scale), 0U);
    }

#if LL_MESA_HEADLESS
    inline ll_test::HeadlessGL& getSharedHeadlessGL()
    {
        static ll_test::HeadlessGL gl(/*needs_vbos=*/true, /*needs_imagegl=*/true,
                                      /*needs_llrender=*/true, /*needs_render=*/true);
        return gl;
    }

    // (2) End-to-end: render 'A' into the 256x256 OSMesa framebuffer and read
    // back the coverage. Proves the whole stack composes — cache -> geometry ->
    // shader -> analytic rasterization -> pixels. Asserts the glyph drew real
    // shape (both covered and uncovered pixels inside its box) and didn't bleed.
    template<> template<>
    void llfontgpubatch_object::test<2>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }
        getSharedHeadlessGL();

        LLGLSLShader program;
        ensure("text program built", LLFontGpuShader::buildProgram(program));

        LLFontGpuGlyphCache cache;
        cache.init(tf.face);
        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_A);
        ensure("'A' drawable", loc.drawable());

        // Scale to ~90px tall so AA edges don't dominate the readback.
        const F32 scale = 90.f / (F32)std::max(1, std::abs(loc.mHeight));

        LLFontGpuBatch::Placement p;
        p.glyph_id = gid_A;
        p.pen_x = 70.f;
        p.pen_y = 70.f;
        p.color[0] = p.color[1] = p.color[2] = p.color[3] = 255;  // white

        const S32 W = ll_test::HeadlessGL::WIDTH;
        const S32 H = ll_test::HeadlessGL::HEIGHT;

        glViewport(0, 0, W, H);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        LLFontGpuBatch batch;
        ensure("render succeeded", batch.render(program, cache, { p }, scale, W, H));

        glFinish();
        std::vector<U8> px = ll_test::readFramebufferRGBA(W, H);

        auto lum = [&](S32 x, S32 y) -> int
        {
            const U8* c = &px[((std::size_t)y * W + x) * 4];
            return (c[0] + c[1] + c[2]) / 3;
        };

        // Glyph screen bbox (y-up; height is negative so bottom = yb+h).
        S32 x0 = (S32)std::floor(p.pen_x + loc.mXBearing * scale);
        S32 x1 = (S32)std::ceil(p.pen_x + (loc.mXBearing + loc.mWidth) * scale);
        S32 y0 = (S32)std::floor(p.pen_y + (loc.mYBearing + loc.mHeight) * scale);
        S32 y1 = (S32)std::ceil(p.pen_y + loc.mYBearing * scale);
        x0 = std::max(0, x0); y0 = std::max(0, y0);
        x1 = std::min(W - 1, x1); y1 = std::min(H - 1, y1);
        ensure("glyph bbox is on-screen", x1 > x0 && y1 > y0);

        int max_lum = 0, min_lum = 255, bright = 0;
        for (S32 y = y0; y <= y1; ++y)
        {
            for (S32 x = x0; x <= x1; ++x)
            {
                int l = lum(x, y);
                max_lum = std::max(max_lum, l);
                min_lum = std::min(min_lum, l);
                if (l > 200) ++bright;
            }
        }

        ensure("glyph drew strong coverage somewhere", max_lum > 180);
        ensure("glyph is shaped (uncovered pixels inside bbox)", min_lum < 60);
        ensure("a meaningful number of pixels are covered", bright > 30);
        ensure_equals("no bleed into the far corner", lum(4, 4), 0);

        LLGLSLShader::unbind();
        batch.destroyGL();
        program.unload();
    }
#endif // LL_MESA_HEADLESS
}

#endif // LL_HAS_HB_GPU
