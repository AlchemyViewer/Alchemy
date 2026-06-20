/**
 * @file llfontgpu_test.cpp
 * @brief Phase 0 de-risk spike for a HarfBuzz-GPU (libharfbuzz-gpu)
 *        analytic glyph rendering path.
 *
 * This is not a feature test — nothing in the viewer consumes hb-gpu yet.
 * It exists to answer the two questions that gate the whole effort before
 * any production plumbing is written:
 *
 *   1. Does libharfbuzz-gpu actually link, and does its CPU-side encode
 *      path (create -> draw_glyph -> encode -> blob) produce a non-empty
 *      blob with sane extents for a real outline glyph?
 *   2. Does the GLSL that HarfBuzz emits via hb_gpu_shader_source() +
 *      hb_gpu_draw_shader_source() actually compile in a GL 3.3 core
 *      context?
 *
 * The non-GL tests (1-3) run anywhere. The GLSL-compile test (4) is gated
 * on LL_MESA_HEADLESS and uses the shared OSMesa fixture. A clean OSMesa
 * compile is a syntax/version sanity gate only — authoritative validation
 * against the real desktop GL driver happens when the shader is wired into
 * the viewer (Phase 3). An OSMesa-specific failure (e.g. no texture-buffer
 * support for the isamplerBuffer atlas) is a false negative, not a blocker.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include <hb.h>
#include <hb-gpu.h>

#if LL_MESA_HEADLESS
#  include "llheadlessgl_fixture.h"
#endif

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif
    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    // A plain outline TrueType face that ships in the source tree. .ttf (not
    // .woff2) so HarfBuzz can open it directly via hb_face_create without
    // depending on a brotli/woff2-enabled build.
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

#if LL_MESA_HEADLESS
    // Assemble the full GLSL for one stage: the shared helper source first,
    // then the draw-renderer source. HarfBuzz emits version-agnostic GLSL so
    // the caller can target desktop GL 3.3 or GLES 3.0 / WebGL2 — prepend a
    // #version directive when it didn't carry one itself.
    std::string assembleDrawGlsl(hb_gpu_shader_stage_t stage)
    {
        const char* common = hb_gpu_shader_source(stage, HB_GPU_SHADER_LANG_GLSL);
        const char* draw   = hb_gpu_draw_shader_source(stage, HB_GPU_SHADER_LANG_GLSL);

        const bool has_version =
            (common && std::strstr(common, "#version")) ||
            (draw   && std::strstr(draw,   "#version"));

        std::string src;
        if (!has_version)
        {
            src += "#version 330\n";
        }
        if (common) { src += common; src += '\n'; }
        if (draw)   { src += draw; }
        return src;
    }
#endif // LL_MESA_HEADLESS
}

namespace tut
{
    struct llfontgpu_data
    {
    };

    typedef test_group<llfontgpu_data> llfontgpu_test;
    typedef llfontgpu_test::object     llfontgpu_object;
    tut::llfontgpu_test llfontgpu_testcase("LLFontGpu");

    // (1) Linkage + lifecycle. If libharfbuzz-gpu isn't linked this fails to
    // build; if it's linked but broken, create_or_fail returns null.
    template<> template<>
    void llfontgpu_object::test<1>()
    {
        hb_gpu_draw_t* draw = hb_gpu_draw_create_or_fail();
        ensure("hb_gpu_draw_create_or_fail returned an encoder", draw != nullptr);
        hb_gpu_draw_destroy(draw);
    }

    // (2) Shader-source API contract. The shared helper source
    // (hb_gpu_shader_source) carries the per-stage helpers — hb_gpu_dilate for
    // the vertex stage, the _hb_gpu_slug rasterizer + isamplerBuffer atlas for
    // the fragment stage — so it is non-empty for BOTH stages. The draw-renderer
    // source (hb_gpu_draw_shader_source) only adds the fragment-stage
    // hb_gpu_draw() wrapper; there is no draw-vertex source, so it is empty for
    // the vertex stage by design. The app supplies main() and prepends the
    // shared source to the draw source for the fragment program (Phase 3).
    template<> template<>
    void llfontgpu_object::test<2>()
    {
        const hb_gpu_shader_stage_t stages[] = {
            HB_GPU_SHADER_STAGE_VERTEX, HB_GPU_SHADER_STAGE_FRAGMENT };

        for (hb_gpu_shader_stage_t stage : stages)
        {
            const char* common = hb_gpu_shader_source(stage, HB_GPU_SHADER_LANG_GLSL);
            const char* draw   = hb_gpu_draw_shader_source(stage, HB_GPU_SHADER_LANG_GLSL);

            ensure("hb_gpu_shader_source(GLSL) non-null",  common != nullptr);
            ensure("hb_gpu_shader_source(GLSL) non-empty", common && common[0] != '\0');
            // The draw source is always a valid string, but may be empty.
            ensure("hb_gpu_draw_shader_source(GLSL) non-null", draw != nullptr);

            LL_INFOS("FontGpuSpike")
                << "stage=" << (stage == HB_GPU_SHADER_STAGE_VERTEX ? "VERTEX" : "FRAGMENT")
                << " common_len=" << (common ? std::strlen(common) : 0)
                << " draw_len="   << (draw   ? std::strlen(draw)   : 0)
                << "\n---- common ----\n" << (common ? common : "(null)")
                << "\n---- draw ----\n"   << (draw   ? draw   : "(null)")
                << LL_ENDL;
        }

        // Draw-specific code is fragment-only: the rasterizer wrapper exists for
        // FRAGMENT and there is intentionally no draw-vertex source.
        const char* draw_frag = hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_FRAGMENT, HB_GPU_SHADER_LANG_GLSL);
        const char* draw_vert = hb_gpu_draw_shader_source(HB_GPU_SHADER_STAGE_VERTEX,   HB_GPU_SHADER_LANG_GLSL);
        ensure("draw fragment source provides the rasterizer", draw_frag && draw_frag[0] != '\0');
        ensure("draw vertex source is empty (fragment-only by design)", draw_vert && draw_vert[0] == '\0');
    }

    // (3) CPU-side encode of a real outline glyph -> non-empty blob with
    // plausible extents. Self-contained HarfBuzz (no FreeType / LLFontManager):
    // open the face, scale to upem so outline coords land in font units (the
    // blob format's preferred range, see hb-gpu.h), encode glyph 'A'.
    template<> template<>
    void llfontgpu_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + kOutlineFont;
        if (!fileExists(path))
        {
            skip("outline test font not present in test data dir");
        }

        hb_blob_t* blob = hb_blob_create_from_file(path.c_str());
        ensure("font blob loaded", hb_blob_get_length(blob) > 0);

        hb_face_t* face = hb_face_create(blob, 0);
        const unsigned upem = hb_face_get_upem(face);
        ensure("face has a sane upem", upem > 0);

        hb_font_t* font = hb_font_create(face);
        // Coordinates in font design units: smallest feature >= 1-2 units and
        // bbox well within +/-8000, which the blob's 16-bit/4-units-per-step
        // quantization wants.
        hb_font_set_scale(font, (int)upem, (int)upem);

        hb_codepoint_t gid = 0;
        ensure("cmap maps 'A' to a glyph",
               hb_font_get_nominal_glyph(font, (hb_codepoint_t)'A', &gid) && gid != 0);

        hb_gpu_draw_t* draw = hb_gpu_draw_create_or_fail();
        ensure("encoder created", draw != nullptr);

        hb_gpu_draw_glyph(draw, font, gid);

        hb_glyph_extents_t extents = {0, 0, 0, 0};
        hb_blob_t* encoded = hb_gpu_draw_encode(draw, &extents);

        ensure("encode produced a blob", encoded != nullptr);
        ensure("encoded blob is non-empty", encoded && hb_blob_get_length(encoded) > 0);
        // 'A' has a real outline, so the encoder must report non-zero extent.
        ensure("glyph has non-zero width",  extents.width  != 0);
        ensure("glyph has non-zero height", extents.height != 0);

        LL_INFOS("FontGpuSpike")
            << "encoded 'A' gid=" << gid
            << " blob_bytes=" << (encoded ? hb_blob_get_length(encoded) : 0)
            << " extents=(" << extents.x_bearing << "," << extents.y_bearing
            << " " << extents.width << "x" << extents.height << ")"
            << " upem=" << upem << LL_ENDL;

        if (encoded)
        {
            hb_gpu_draw_recycle_blob(draw, encoded);
        }
        hb_gpu_draw_destroy(draw);
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }

#if LL_MESA_HEADLESS
    // Bring the OSMesa GL 3.3 core context up once per binary, shared across
    // the GL test(s) below — same pattern as the other llrender GL tests.
    inline ll_test::HeadlessGL& getSharedHeadlessGL()
    {
        static ll_test::HeadlessGL gl(/*needs_vbos=*/false, /*needs_imagegl=*/false,
                                      /*needs_llrender=*/false, /*needs_render=*/false);
        return gl;
    }

    // (4) The make-or-break syntax gate: HarfBuzz's emitted GLSL compiles in a
    // GL 3.3 core context. Logs the assembled source + compiler diagnostic via
    // the fixture's compileTestShader so a failure is actionable.
    template<> template<>
    void llfontgpu_object::test<4>()
    {
        getSharedHeadlessGL();

        const std::string vsrc = assembleDrawGlsl(HB_GPU_SHADER_STAGE_VERTEX);
        const std::string fsrc = assembleDrawGlsl(HB_GPU_SHADER_STAGE_FRAGMENT);

        LL_INFOS("FontGpuSpike") << "assembled VERTEX GLSL:\n" << vsrc << LL_ENDL;
        LL_INFOS("FontGpuSpike") << "assembled FRAGMENT GLSL:\n" << fsrc << LL_ENDL;

        GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, vsrc.c_str());
        ensure("hb-gpu vertex GLSL compiled under GL 3.3 core", vs != 0);
        if (vs) glDeleteShader(vs);

        GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, fsrc.c_str());
        ensure("hb-gpu fragment GLSL compiled under GL 3.3 core", fs != 0);
        if (fs) glDeleteShader(fs);
    }
#endif // LL_MESA_HEADLESS
}
