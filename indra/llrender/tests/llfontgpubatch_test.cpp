/**
 * @file llfontgpubatch_test.cpp
 * @brief Unit tests for LLFontGpuBatch::buildGlyphQuad — the CPU glyph-quad
 *        geometry helper for the analytic (hb-gpu) font path. End-to-end render
 *        (cache -> geometry -> shader -> pixels) is covered by llfontgl_test.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../llfontgpubatch.h"
#include "../llfontgpuglyphcache.h"
#include "../llvertexbuffer.h"   // LLVector4a / LLVector2 / LLColor4U + GLYPH_LOC_*

#if LL_HAS_HB_GPU

#include <hb.h>

#include <cmath>
#include <cstdio>
#include <string>

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
        hb_font_t* font = nullptr;   // default instance; LLFontGpuGlyphCache::init takes a font
        TestFace()
        {
            const std::string path = std::string(kFontDir) + kOutlineFont;
            if (!fileExists(path)) return;
            blob = hb_blob_create_from_file(path.c_str());
            if (hb_blob_get_length(blob) > 0)
            {
                face = hb_face_create(blob, 0);
                font = hb_font_create(face);
            }
        }
        ~TestFace()
        {
            if (font) hb_font_destroy(font);
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

    bool approx(F32 a, F32 b, F32 eps = 0.05f) { return std::fabs(a - b) <= eps; }
}

namespace tut
{
    struct llfontgpubatch_data
    {
    };

    typedef test_group<llfontgpubatch_data> llfontgpubatch_test;
    typedef llfontgpubatch_test::object      llfontgpubatch_object;
    tut::llfontgpubatch_test llfontgpubatch_testcase("LLFontGpuBatch");

    // (1) CPU geometry: one glyph -> 6 vertices obeying the core invariant
    // position == pen + texcoord * scale (which holds even with the half-pixel
    // pre-dilation folded in), carrying the right glyph_loc and color, and
    // spanning at least the glyph's design-unit extents (the dilation makes the
    // quad slightly larger than the tight bbox).
    template<> template<>
    void llfontgpubatch_object::test<1>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);

        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_A);
        ensure("'A' drawable", loc.drawable());

        const F32 scale = 0.05f;
        const F32 pen_x = 100.f, pen_y = 80.f;
        const LLColor4U color(10, 20, 30, 255);
        const U32 glyph_loc = loc.mTexelOffset;   // monochrome (no COLOR bit)

        LLVector4a pos[6]; LLVector2 uv[6]; LLColor4U col[6]; U32 gl[6];
        LLFontGpuBatch::buildGlyphQuad(pos, uv, col, gl, loc,
                                       pen_x, pen_y, scale, /*slant=*/0.f, color, glyph_loc);

        F32 min_u = 1e9f, max_u = -1e9f, min_v = 1e9f, max_v = -1e9f;
        for (S32 i = 0; i < 6; ++i)
        {
            const F32* p = pos[i].getF32ptr();
            // The invariant tying the screen quad to the em-space coords. The
            // pre-dilation expands position AND texcoord in lock-step, so it still
            // holds exactly (slant == 0).
            ensure("pos.x == pen_x + tc.x*scale", approx(p[0], pen_x + uv[i].mV[0] * scale));
            ensure("pos.y == pen_y + tc.y*scale", approx(p[1], pen_y + uv[i].mV[1] * scale));
            ensure_equals("glyph_loc matches", gl[i], glyph_loc);
            ensure("color preserved",
                   col[i].mV[0] == 10 && col[i].mV[1] == 20 && col[i].mV[2] == 30 && col[i].mV[3] == 255);

            min_u = std::min(min_u, uv[i].mV[0]); max_u = std::max(max_u, uv[i].mV[0]);
            min_v = std::min(min_v, uv[i].mV[1]); max_v = std::max(max_v, uv[i].mV[1]);
        }
        // The quad covers at least the glyph's design-unit extents (plus the
        // half-pixel dilation margin on each side).
        ensure("tc x-span >= width",     (max_u - min_u) >= (F32)std::abs(loc.mWidth));
        ensure("tc y-span >= |height|",  (max_v - min_v) >= (F32)std::abs(loc.mHeight));
    }

    // (2) The COLOR bit is preserved verbatim in every vertex's glyph_loc (the UI
    // shader keys paint vs. coverage off it). buildGlyphQuad is mode-agnostic — it
    // just forwards the value.
    template<> template<>
    void llfontgpubatch_object::test<2>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);
        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gidFor(tf.face, (hb_codepoint_t)'A'));
        ensure("'A' drawable", loc.drawable());

        const U32 glyph_loc = (loc.mTexelOffset & LLVertexBuffer::GLYPH_LOC_OFFSET_MASK)
                            | LLVertexBuffer::GLYPH_LOC_COLOR;
        LLVector4a pos[6]; LLVector2 uv[6]; LLColor4U col[6]; U32 gl[6];
        LLFontGpuBatch::buildGlyphQuad(pos, uv, col, gl, loc, 0.f, 0.f, 0.05f, 0.f,
                                       LLColor4U::white, glyph_loc);
        for (S32 i = 0; i < 6; ++i)
        {
            ensure("COLOR bit set", (gl[i] & LLVertexBuffer::GLYPH_LOC_COLOR) != 0u);
            ensure_equals("offset recoverable", gl[i] & LLVertexBuffer::GLYPH_LOC_OFFSET_MASK,
                          loc.mTexelOffset & LLVertexBuffer::GLYPH_LOC_OFFSET_MASK);
        }
    }
}

#endif // LL_HAS_HB_GPU
