/**
 * @file llfontgpushader_test.cpp
 * @brief Unit tests for LLFontGpuShader — the analytic (hb-gpu) UI text program
 *        assembler/builder: source splicing and (headless) compile+link with
 *        the uniforms the render path feeds.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../llfontgpushader.h"

#if LL_HAS_HB_GPU

#include <string>

#if LL_MESA_HEADLESS
#  include "../llglslshader.h"
#  include "llheadlessgl_fixture.h"
#endif

namespace
{
    bool contains(const std::string& haystack, const char* needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

namespace tut
{
    struct llfontgpushader_data
    {
    };

    typedef test_group<llfontgpushader_data> llfontgpushader_test;
    typedef llfontgpushader_test::object     llfontgpushader_object;
    tut::llfontgpushader_test llfontgpushader_testcase("LLFontGpuShader");

    // (1) Source assembly (no GL): #version first, HarfBuzz helpers spliced in,
    // and the app IO/main present for each stage.
    template<> template<>
    void llfontgpushader_object::test<1>()
    {
        const std::string vs = LLFontGpuShader::vertexSource();
        const std::string fs = LLFontGpuShader::fragmentSource();

        ensure("vertex starts with #version", vs.rfind("#version", 0) == 0);
        ensure("fragment starts with #version", fs.rfind("#version", 0) == 0);

        // Vertex: HarfBuzz dilate helper + app main wiring with reserved attrib
        // names (so LLVertexBuffer's mapAttributes binds them).
        ensure("vertex has hb_gpu_dilate", contains(vs, "hb_gpu_dilate"));
        ensure("vertex has main", contains(vs, "void main"));
        ensure("vertex declares MVP uniform", contains(vs, "modelview_projection_matrix"));
        ensure("vertex declares glyph_loc attribute", contains(vs, "in uint glyph_loc"));
        ensure("vertex declares reserved position", contains(vs, "in vec3 position"));
        ensure("vertex declares reserved texcoord0", contains(vs, "in vec2 texcoord0"));

        // Fragment: shared rasterizer + atlas + draw wrapper + app main.
        ensure("fragment has _hb_gpu_slug (shared)", contains(fs, "_hb_gpu_slug"));
        ensure("fragment has hb_gpu_atlas (shared)", contains(fs, "hb_gpu_atlas"));
        ensure("fragment has hb_gpu_draw (draw)", contains(fs, "hb_gpu_draw"));
        ensure("fragment applies stem darkening", contains(fs, "hb_gpu_stem_darken"));
        ensure("fragment has main", contains(fs, "void main"));

        // Shared-fragment must precede the draw wrapper that calls into it.
        ensure("shared source precedes draw wrapper",
               fs.find("_hb_gpu_slug") < fs.find("float hb_gpu_draw"));
    }

#if LL_MESA_HEADLESS
    inline ll_test::HeadlessGL& getSharedHeadlessGL()
    {
        // needs_render brings up a TestShaderMgr, which mapUniforms() consults
        // for the reserved uniform name table (modelview_projection_matrix).
        static ll_test::HeadlessGL gl(/*needs_vbos=*/true, /*needs_imagegl=*/true,
                                      /*needs_llrender=*/true, /*needs_render=*/true);
        return gl;
    }

    // (2) Full compile + LINK in a GL 3.3 core context, and the uniforms the
    // render path feeds all resolve. A complete program with main() linking is a
    // stronger result than the Phase-0 helper-snippet compile check.
    template<> template<>
    void llfontgpushader_object::test<2>()
    {
        getSharedHeadlessGL();

        LLGLSLShader program;
        const bool built = LLFontGpuShader::buildProgram(program);
        ensure("hb-gpu text program built", built);
        ensure("program is complete (linked)", program.isComplete());

        ensure("modelview_projection_matrix resolves",
               program.getUniformLocation(LLStaticHashedString("modelview_projection_matrix")) >= 0);
        ensure("viewport resolves",
               program.getUniformLocation(LLStaticHashedString("viewport")) >= 0);
        ensure("jac resolves",
               program.getUniformLocation(LLStaticHashedString("jac")) >= 0);
        ensure("hb_gpu_atlas resolves",
               program.getUniformLocation(LLStaticHashedString("hb_gpu_atlas")) >= 0);

        program.unload();
    }
#endif // LL_MESA_HEADLESS
}

#endif // LL_HAS_HB_GPU
