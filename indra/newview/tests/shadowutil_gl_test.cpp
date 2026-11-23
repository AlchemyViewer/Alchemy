/**
 * @file shadowutil_gl_test.cpp
 * @brief shadowUtil.glsl's samplers on the hidden window, with a map on every shadow unit that shadows every receiver:
 *        a map the pass has none of (unbuilt_shadow_maps) reads lit, at its cascade's weight, and the rest as their
 *        maps say, under the compare and the PCSS samplers alike. And a receiver lying on the plane its map holds,
 *        magnified so that a texel spans many pixels, reads lit under both, however steep the plane: the fixed kernels
 *        push it out along its normal far enough, and PCSS's receiver-plane bias follows the plane. One behind an
 *        occluder over that plane reads shadowed under both. A curved surface of flat facets, smooth-shaded, seen up
 *        close under the sun's perspective fit, reads lit on every facet the sun faces, and an occluder's shadow on it
 *        stays dark.
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

#include "aluniformbuffer.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    // A receiver a column; two rows, so each pixel's derivatives come from a whole quad.
    constexpr S32 W = 4;
    constexpr S32 H = 2;

    // The shadow maps: four sun cascades, then the spot pair.
    constexpr U32 MAPS = 6;

    // The sun's clip planes, and the weight each cascade has at each column's receiver (sampleDirectionalShadow's
    // splits at 0.75 and 1.25 of them): cascades 0, 1 and 3 read alone, and 2, which never is, with a tenth each of 1
    // and 3. The last plane lies far enough out that cascade 3 does not fade to lit at its receiver.
    constexpr F32 SHADOW_CLIP[4] = { 10.f, 20.f, 30.f, 100.f };
    constexpr F32 WEIGHTS[W][4] = {
        { 1.f, 0.f, 0.f, 0.f },
        { 0.f, 1.f, 0.f, 0.f },
        { 0.f, 0.1f, 0.8f, 0.1f },
        { 0.f, 0.f, 0.f, 1.f },
    };

    const char* kVertex =
        "#version 400\n"
        "in vec3 position;\n"
        "void main() { gl_Position = vec4(position, 1.0); }\n";

    // The sun's shadow and each spot's, at the eye-space receiver its column names.
    const char* kFragment =
        "#version 400\n"
        "out vec4 frag_color;\n"
        "float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);\n"
        "float sampleSpotShadow(vec3 pos, vec3 norm, int index, vec2 pos_screen);\n"
        "const float kDepth[4] = float[4](-1.0, -14.0, -24.0, -40.0);\n"
        "void main()\n"
        "{\n"
        "    vec3 pos = vec3(0.0, 0.0, kDepth[int(gl_FragCoord.x)]);\n"
        "    vec3 norm = vec3(0.0, 0.0, 1.0);\n"
        "    vec2 screen = gl_FragCoord.xy / vec2(4.0, 2.0);\n"
        "    frag_color = vec4(sampleDirectionalShadow(pos, norm, screen), sampleSpotShadow(pos, norm, 0, screen),\n"
        "                      sampleSpotShadow(pos, norm, 1, screen), 1.0);\n"
        "}\n";

    // The Deferred block as LLPipeline::DeferredUBOData packs it (std140).
    struct alignas(16) DeferredBlock
    {
        F32 shadow_matrix[6][16];
        F32 ssao_effect_mat[12];
        F32 shadow_clip[4];
        F32 shadow_res[2];
        F32 proj_shadow_res[2];
        F32 shadow_bias;
        F32 shadow_offset;
        F32 spot_shadow_bias;
        F32 spot_shadow_offset;
        F32 ssao_radius;
        F32 ssao_max_radius;
        F32 ssao_factor;
        F32 ssao_factor_inv;
        F32 shadow_pcss_scale;
        F32 specular_exponent;
    };
    static_assert(sizeof(DeferredBlock) == 512);

    struct shadowutil_data
    {
        shadowutil_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~shadowutil_data()
        {
            for (S32 channel : mChannels)
            {
                if (channel > -1)
                {
                    gGL.getTextureSlot(channel)->unbind();
                    gGL.getTextureSlot(channel)->bindSampler(0);
                }
            }
            LLGLSLShader::unbind();
            mProgram.unload();
            mShaders.clearShaderObjects();
            mBlock.release();
            mMap.release();
            mOut.release();
            mTriangle = nullptr;
        }

        // The samplers built as the viewer builds them with sun and spot shadows, at the compare tier or PCSS's, over a
        // 4x4 map whose every texel lies nearer the light than any receiver, on all six units under the sampler the
        // tier declares them for, as bindShadowMaps binds them.
        void setUp(bool pcss)
        {
            std::map<std::string, std::string> defines = { { "SUN_SHADOW", "1" },
                                                            { "SPOT_SHADOW", "1" },
                                                            { "SHADOW_PCSS", pcss ? "1" : "0" } };
            S32 level = 1;
            const GLuint util = mShaders.loadShaderFile("deferred/shadowUtil.glsl", level, GL_FRAGMENT_SHADER, &defines);
            {
                INFO("shadowUtil compiles");
                REQUIRE(util != 0);
            }
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("test shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "shadowUtil (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            mProgram.attachObject(util);
            UNSCOPED_INFO("program links");
            REQUIRE(mProgram.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO("program maps");
            REQUIRE(mProgram.mapUniforms());

            // Every receiver to the map's centre at half depth, which the map's texels lie nearer the light than. The map
            // looks down the line the receivers lie on, its u and v running square to it as a projection's do: the
            // fixed kernels' push takes the map's ray from their gradients, and the receivers' normal lies along it.
            DeferredBlock block{};
            for (auto& matrix : block.shadow_matrix)
            {
                matrix[0] = 1.f;
                matrix[5] = 1.f;
                matrix[12] = 0.5f;
                matrix[13] = 0.5f;
                matrix[14] = 0.5f;
                matrix[15] = 1.f;
            }
            std::copy(std::begin(SHADOW_CLIP), std::end(SHADOW_CLIP), block.shadow_clip);
            block.shadow_res[0] = block.shadow_res[1] = 4.f;
            block.proj_shadow_res[0] = block.proj_shadow_res[1] = 4.f;
            mBlock.update(&block, sizeof(block));

            REQUIRE(mMap.allocate(4, 4, 0, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                  LLRender::sReverseZ ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24));
            mMap.bindTarget();
            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                glClearDepth(LLRender::sReverseZ ? 1.0 : 0.0);
                glClear(GL_DEPTH_BUFFER_BIT);
            }
            mMap.flush();

            REQUIRE(mOut.allocate(W, H, GL_RGBA16F));

            mProgram.bind();
            const U32 sampler = gGL.getSampler(pcss ? ALSamplers::PointClamp : ALSamplers::ShadowCompare);
            for (U32 i = 0; i < MAPS; ++i)
            {
                mChannels[i] = mProgram.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
                CAPTURE(i);
                REQUIRE(mChannels[i] > -1);
                gGL.getTextureSlot(mChannels[i])->bind(&mMap, true, sampler);
            }
            mProgram.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.f, 1.f);
            mProgram.uniform3f(LLShaderMgr::DEFERRED_MOON_DIR, 0.f, 0.f, 1.f);
            mProgram.uniform1i(LLShaderMgr::SUN_UP_FACTOR, 1);
            mProgram.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, 0);
            mProgram.uniform1i(LLShaderMgr::SUN_SHADOW_CASCADES, 4);
            mProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)W, (F32)H);
            mProgram.unbind();

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();
        }

        // Every receiver's shadows, with the maps `unbuilt` names as the pass's missing.
        void run(U32 unbuilt)
        {
            mOut.bindTarget();
            mProgram.bind();
            mBlock.bind(LLGLSLShader::UB_DEFERRED);
            mProgram.uniform1i(LLShaderMgr::UNBUILT_SHADOW_MAPS, (S32)unbuilt);
            {
                LLGLDepthTest depth_test(GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mProgram.unbind();

            mPixels.assign(size_t(W) * H * 4, 0.f);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, mPixels.data());
            mOut.flush();
        }

        const F32* at(S32 x, S32 y) const { return &mPixels[(size_t(y) * W + x) * 4]; }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mProgram;
        ALUniformBuffer mBlock;
        LLRenderTarget mMap;
        LLRenderTarget mOut;
        LLPointer<LLVertexBuffer> mTriangle;
        S32 mChannels[MAPS] = { -1, -1, -1, -1, -1, -1 };
        std::vector<F32> mPixels;
    };

    // A map PLANE_RES texels square, and an output PLANE_OUT pixels square showing PLANE_SPAN of its texels about its
    // centre: a texel spans 32 pixels, and a pixel steps 1/16384 across the map, which squared is under the 1e-8 a
    // fixed floor on the receiver-plane solve's determinant would hold it to.
    constexpr S32 PLANE_RES  = 512;
    constexpr S32 PLANE_OUT  = 128;
    constexpr F32 PLANE_SPAN = 4.f;

    // The constant bias directionalShadow takes from shadow_bias, twice this.
    constexpr F32 PLANE_BIAS = 0.002f;

    // The plane's depth steps a texel along the map's v.
    // - Shallow, under the constant bias: a texel compared against a ref taken within a texel of its centre reads lit,
    //   and over two thirds of it, so a receiver whose bias does not follow its plane is shadowed by the compare tier's
    //   taps a texel and a half out.
    // - Steep, over the constant bias in half a texel: a texel compared against a ref taken anywhere but its own centre,
    //   as a 2x2 gather's one ref is taken half a texel off each of its texels, is shadowed by its own plane.
    constexpr F32 PLANE_STEP_SHALLOW = 0.003f;
    constexpr F32 PLANE_STEP_STEEP   = 0.009f;

    // How far nearer the light than the plane an occluder over it lies, in normalized depth, under PCSS: past the
    // constant bias, so every receiver on the plane is in its shadow, and within it and a texel of either plane's slope,
    // which a compare taken a texel off its plane lets through.
    constexpr F32 OCCLUDER_GAP = 0.006f;

    // And under the fixed kernels, along the plane's normal in texels. Their receiver is pushed out along it by up to
    // their reach, two and a half texels at the default kernel, and compared at one depth across the kernel, over
    // which an occluder parallel to the plane falls away from the light by as much again at its far side: past twice
    // the reach, the occluder shadows every texel of it however steep the plane.
    constexpr F32 OCCLUDER_TEXELS = 6.f;

    // The defines the plane's programs read, at a depth step of `step` a texel, the map holding the plane `lift` nearer
    // the light than the receivers on it. LIGHTWARD is the way depth runs toward the light: down forward, up under
    // reverse-Z.
    std::string planeDefines(F32 step, F32 lift)
    {
        return "#version 400\n"
               "#define PLANE_RES " + std::to_string(PLANE_RES) + ".0\n"
               "#define PLANE_OUT " + std::to_string(PLANE_OUT) + ".0\n"
               "#define PLANE_SPAN " + std::to_string(PLANE_SPAN) + "\n"
               "#define PLANE_STEP " + std::to_string(step) + "\n"
               "#define PLANE_LIFT " + std::to_string(lift) + "\n"
               "#define LIGHTWARD " + (LLRender::sReverseZ ? "1.0" : "-1.0") + "\n";
    }

    // The plane's depth at each texel's centre: 0.5 at the map's middle row, PLANE_STEP more each row up, moved by
    // PLANE_LIFT.
    const char* kPlaneDepth =
        "void main() { gl_FragDepth = 0.5 + PLANE_LIFT + PLANE_STEP * (gl_FragCoord.y - 0.5 * PLANE_RES); }\n";

    // The receiver on that plane under each pixel, in an eye space the shadow matrix takes to the map as it is: x and y
    // about its centre, and the depth about 0.5. Its normal is the plane's, facing the light.
    const char* kPlaneReceiver =
        "out vec4 frag_color;\n"
        "float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);\n"
        "void main()\n"
        "{\n"
        "    vec2 uv = (0.5 * PLANE_RES + (gl_FragCoord.xy / PLANE_OUT - 0.5) * PLANE_SPAN) / PLANE_RES;\n"
        "    vec3 pos = vec3(uv - 0.5, PLANE_STEP * PLANE_RES * (uv.y - 0.5));\n"
        "    vec3 n = normalize(vec3(0.0, -PLANE_STEP * PLANE_RES, 1.0)) * LIGHTWARD;\n"
        "    frag_color = vec4(sampleDirectionalShadow(pos, n, gl_FragCoord.xy / PLANE_OUT), 0.0, 0.0, 1.0);\n"
        "}\n";

    // A silhouette across the plane, as the sun pass sees one: the receivers from SILHOUETTE_COLUMN on lie 0.2 behind
    // it, so the quad holding columns 64 and 65 spans the step and column 64's derivatives reach across it.
    constexpr S32 SILHOUETTE_COLUMN = 65;
    const char* kSilhouetteReceiver =
        "out vec4 frag_color;\n"
        "float sampleDirectionalShadowFacing(vec3 pos, vec3 norm, vec3 geo_norm, vec2 pos_screen);\n"
        "void main()\n"
        "{\n"
        "    vec2 uv = (0.5 * PLANE_RES + (gl_FragCoord.xy / PLANE_OUT - 0.5) * PLANE_SPAN) / PLANE_RES;\n"
        "    float behind = gl_FragCoord.x > 65.0 ? 0.2 : 0.0;\n"
        "    vec3 pos = vec3(uv - 0.5, PLANE_STEP * PLANE_RES * (uv.y - 0.5) + behind);\n"
        "    vec3 n = vec3(0.0, 0.0, 1.0);\n"
        "    frag_color = vec4(sampleDirectionalShadowFacing(pos, n, n, gl_FragCoord.xy / PLANE_OUT), 0.0, 0.0, 1.0);\n"
        "}\n";

    struct shadowutil_plane_data
    {
        shadowutil_plane_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~shadowutil_plane_data()
        {
            for (S32 channel : mChannels)
            {
                if (channel > -1)
                {
                    gGL.getTextureSlot(channel)->unbind();
                    gGL.getTextureSlot(channel)->bindSampler(0);
                }
            }
            LLGLSLShader::unbind();
            mDepth.unload();
            mProgram.unload();
            mShaders.clearShaderObjects();
            mBlock.release();
            mMap.release();
            mOut.release();
            mTriangle = nullptr;
        }

        // A program of the full-screen vertex stage and `fragment`, with `util` beside it where it is not 0.
        void link(LLGLSLShader& program, const char* name, const std::string& fragment, GLuint util)
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, fragment.c_str());
            {
                INFO(name << " compiles");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            program.mProgramObject = glCreateProgram();
            program.mName = name;
            program.attachObject(vs);
            program.attachObject(fs);
            if (util)
            {
                program.attachObject(util);
            }
            UNSCOPED_INFO(name << " links");
            REQUIRE(program.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO(name << " maps");
            REQUIRE(program.mapUniforms());
        }

        // The sun's samplers at the compare tier or PCSS's, its cascades all one map holding the plane of depth step
        // `step` a texel, `gap` nearer the light than the receivers on it, read through the sampler the tier declares
        // them for by `receiver`.
        void setUp(bool pcss, F32 step, const char* receiver = kPlaneReceiver, F32 gap = 0.f)
        {
            std::map<std::string, std::string> defines = { { "SUN_SHADOW", "1" }, { "SHADOW_PCSS", pcss ? "1" : "0" } };
            S32 level = 1;
            const GLuint util = mShaders.loadShaderFile("deferred/shadowUtil.glsl", level, GL_FRAGMENT_SHADER, &defines);
            {
                INFO("shadowUtil compiles");
                REQUIRE(util != 0);
            }
            // Nearer the light is smaller depth forward, larger under reverse-Z.
            const F32 lift = LLRender::sReverseZ ? gap : -gap;
            link(mDepth, "plane depth (test)", planeDefines(step, lift) + kPlaneDepth, 0);
            link(mProgram, "plane receiver (test)", planeDefines(step, lift) + receiver, util);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            REQUIRE(mMap.allocate(PLANE_RES, PLANE_RES, 0, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                  LLRender::sReverseZ ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24));
            mMap.bindTarget();
            mDepth.bind();
            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mDepth.unbind();
            mMap.flush();

            // Eye space to the map's, as it is but for the offset to its middle. The bias toward lit under either
            // depth convention, as LLPipeline::packDeferredUBO signs it.
            DeferredBlock block{};
            for (U32 i = 0; i < 4; ++i)
            {
                F32* matrix = block.shadow_matrix[i];
                matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.f;
                matrix[12] = matrix[13] = matrix[14] = 0.5f;
            }
            std::copy(std::begin(SHADOW_CLIP), std::end(SHADOW_CLIP), block.shadow_clip);
            block.shadow_res[0] = block.shadow_res[1] = (F32)PLANE_RES;
            block.shadow_bias = LLRender::sReverseZ ? PLANE_BIAS : -PLANE_BIAS;
            block.shadow_pcss_scale = 200.f;
            mBlock.update(&block, sizeof(block));

            REQUIRE(mOut.allocate(PLANE_OUT, PLANE_OUT, GL_RGBA16F));

            mProgram.bind();
            const U32 sampler = gGL.getSampler(pcss ? ALSamplers::PointClamp : ALSamplers::ShadowCompare);
            for (U32 i = 0; i < 4; ++i)
            {
                mChannels[i] = mProgram.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
                CAPTURE(i);
                REQUIRE(mChannels[i] > -1);
                gGL.getTextureSlot(mChannels[i])->bind(&mMap, true, sampler);
            }
            mProgram.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, 0.f, 0.f, 1.f);
            mProgram.uniform3f(LLShaderMgr::DEFERRED_MOON_DIR, 0.f, 0.f, 1.f);
            mProgram.uniform1i(LLShaderMgr::SUN_UP_FACTOR, 1);
            mProgram.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, 0);
            mProgram.uniform1i(LLShaderMgr::SUN_SHADOW_CASCADES, 4);
            mProgram.uniform1i(LLShaderMgr::UNBUILT_SHADOW_MAPS, 0);
            mProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)PLANE_OUT, (F32)PLANE_OUT);
            mProgram.unbind();
        }

        // Every receiver's sun shadow.
        void run()
        {
            mOut.bindTarget();
            mProgram.bind();
            mBlock.bind(LLGLSLShader::UB_DEFERRED);
            {
                LLGLDepthTest depth_test(GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mProgram.unbind();

            mPixels.assign(size_t(PLANE_OUT) * PLANE_OUT * 4, 0.f);
            glReadPixels(0, 0, PLANE_OUT, PLANE_OUT, GL_RGBA, GL_FLOAT, mPixels.data());
            mOut.flush();
        }

        // The darkest sun shadow among the output's first `columns` columns, and where it is.
        F32 darkest(S32 columns, S32& at_x, S32& at_y) const
        {
            F32 sun = 1.f;
            for (S32 y = 0; y < PLANE_OUT; ++y)
            {
                for (S32 x = 0; x < columns; ++x)
                {
                    const F32 value = mPixels[(size_t(y) * PLANE_OUT + x) * 4];
                    if (value < sun)
                    {
                        sun = value;
                        at_x = x;
                        at_y = y;
                    }
                }
            }
            return sun;
        }

        // The brightest sun shadow in the output, and where it is.
        F32 brightest(S32& at_x, S32& at_y) const
        {
            F32 sun = 0.f;
            for (S32 y = 0; y < PLANE_OUT; ++y)
            {
                for (S32 x = 0; x < PLANE_OUT; ++x)
                {
                    const F32 value = mPixels[(size_t(y) * PLANE_OUT + x) * 4];
                    if (value > sun)
                    {
                        sun = value;
                        at_x = x;
                        at_y = y;
                    }
                }
            }
            return sun;
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mDepth;
        LLGLSLShader mProgram;
        ALUniformBuffer mBlock;
        LLRenderTarget mMap;
        LLRenderTarget mOut;
        LLPointer<LLVertexBuffer> mTriangle;
        S32 mChannels[4] = { -1, -1, -1, -1 };
        std::vector<F32> mPixels;
    };
}

TEST_CASE_METHOD(shadowutil_data, "shadowutil_gl: a map the pass has none of reads lit and the rest as their maps say", "[shadowutil_gl]")
{
    const bool pcss = GENERATE(false, true);
    CAPTURE(pcss);
    setUp(pcss);

    // Every map shadows every receiver, so a receiver is lit by exactly the weight its missing maps carry.
    for (U32 unbuilt = 0; unbuilt < (1u << MAPS); ++unbuilt)
    {
        CAPTURE(unbuilt);
        run(unbuilt);
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                CAPTURE(x, y);
                F32 sun = 0.f;
                for (U32 cascade = 0; cascade < 4; ++cascade)
                {
                    sun += (unbuilt & (1u << cascade)) ? WEIGHTS[x][cascade] : 0.f;
                }
                const F32* px = at(x, y);
                CHECK(std::fabs(px[0] - sun) < 0.002f);
                CHECK(px[1] == ((unbuilt & (1u << 4)) ? 1.f : 0.f));
                CHECK(px[2] == ((unbuilt & (1u << 5)) ? 1.f : 0.f));
            }
        }
    }
}

TEST_CASE_METHOD(shadowutil_plane_data, "shadowutil_gl: a receiver on its map's plane reads lit however magnified or steep the plane",
                 "[shadowutil_gl]")
{
    const bool pcss = GENERATE(false, true);
    const F32 step = GENERATE(PLANE_STEP_SHALLOW, PLANE_STEP_STEEP);
    CAPTURE(pcss, step);
    setUp(pcss, step);
    run();

    // A receiver compared against texels of its own plane nearer the light than it, unpushed and unfollowed, reads its
    // plane's acne, in steps a texel tall, and a texel compared off its own plane shadows a part of every gather.
    S32 at_x = -1;
    S32 at_y = -1;
    const F32 sun = darkest(PLANE_OUT, at_x, at_y);
    CAPTURE(at_x, at_y);
    CHECK(sun > 0.999f);
}

TEST_CASE_METHOD(shadowutil_plane_data, "shadowutil_gl: a receiver behind an occluder reads shadowed however steep its plane",
                 "[shadowutil_gl]")
{
    const bool pcss = GENERATE(false, true);
    const F32 step = GENERATE(0.f, PLANE_STEP_SHALLOW, PLANE_STEP_STEEP);
    CAPTURE(pcss, step);
    // The occluder's lift in depth: OCCLUDER_GAP, or OCCLUDER_TEXELS along the plane's normal, which the depth
    // takes 1 / cos of, cos the plane's normal's against the light.
    const F32 slope = step * PLANE_RES;
    const F32 gap = pcss ? OCCLUDER_GAP : OCCLUDER_TEXELS / PLANE_RES * sqrtf(1.f + slope * slope);
    CAPTURE(gap);
    setUp(pcss, step, kPlaneReceiver, gap);
    run();

    // A compare taken off its plane toward lit, or a receiver pushed out past the occluder, lets its shadow through, and
    // the more on a steeper plane: a curved surface under a near occluder, each facet at its own slope, reads lit facet
    // by facet.
    S32 at_x = -1;
    S32 at_y = -1;
    const F32 sun = brightest(at_x, at_y);
    CAPTURE(at_x, at_y);
    CHECK(sun < 0.001f);
}

TEST_CASE_METHOD(shadowutil_plane_data, "shadowutil_gl: the sun pass takes no slope across a silhouette", "[shadowutil_gl]")
{
    const bool pcss = GENERATE(false, true);
    CAPTURE(pcss);
    setUp(pcss, 0.f, kSilhouetteReceiver);
    run();

    // The plane in front, on a map holding it flat, reads lit up to the silhouette: column 64 too, whose derivatives
    // reach the receiver behind it, and whose taps a slope solved across the step would tilt off the plane.
    S32 at_x = -1;
    S32 at_y = -1;
    const F32 sun = darkest(SILHOUETTE_COLUMN, at_x, at_y);
    CAPTURE(at_x, at_y);
    CHECK(sun > 0.999f);
}

namespace
{
    // A low-poly sphere with smooth normals half a metre from the camera, lit from its side, as a body seen up close is:
    // twelve facets around and eight down, each a plane its shading normals disagree with.
    constexpr S32 SPHERE_OUT    = 512;
    constexpr S32 SPHERE_MAP    = 1024;
    constexpr S32 SPHERE_STACKS = 8;
    constexpr S32 SPHERE_SLICES = 12;
    constexpr F32 SPHERE_RADIUS = 0.12f;
    constexpr F32 SPHERE_DEPTH  = 0.5f;

    // The sun's map of it as generateSunShadow's perspective fit lays one out: w is the distance along the fit's axis,
    // square to the light, from an origin the sphere lies `ys` out from; the axis maps [0.1, 10] m to the map, x spans
    // 4 m at the sphere, and depth is fz z / w for receivers 3 m either side of the light's axis at the far end, narrowed
    // by the near plane's clearance. A texel along the axis lengthens with w squared: 4 cm at 2 m out.
    constexpr F32 SPHERE_FIT_NEAR  = 0.1f;
    constexpr F32 SPHERE_FIT_FAR   = 10.f;
    constexpr F32 SPHERE_FIT_WIDTH = 4.f;
    constexpr F32 SPHERE_FIT_SPAN  = 3.f;
    constexpr F32 SPHERE_FIT_CLEAR = 0.028f;

    // RenderShadowBias and RenderShadowOffset as the viewer ships them, the bias's size.
    constexpr F32 STOCK_SHADOW_BIAS   = 0.0005f;
    constexpr F32 STOCK_SHADOW_OFFSET = 0.01f;

    const char* kSphereMapV =
        "in vec3 position;\n"
        "uniform mat4 to_map;\n"
        "noperspective out float vary_depth;\n"
        "void main()\n"
        "{\n"
        "    vec4 t = to_map * vec4(position, 1.0);\n"
        "    vary_depth = t.z / t.w;\n"
        "    gl_Position = vec4(t.xy * 2.0 - t.w, 0.0, t.w);\n"
        "}\n";
    const char* kSphereMapF =
        "noperspective in float vary_depth;\n"
        "void main() { gl_FragDepth = vary_depth; }\n";

    // The camera's G-buffer, one attachment a pass: the eye-space position, or the shading normal with the n.l of the
    // facet's own plane.
    const char* kSphereGBufferV =
        "in vec3 position;\n"
        "in vec3 normal;\n"
        "uniform mat4 cam_proj;\n"
        "out vec3 vary_pos;\n"
        "out vec3 vary_norm;\n"
        "void main() { vary_pos = position; vary_norm = normal; gl_Position = cam_proj * vec4(position, 1.0); }\n";
    const char* kSphereGBufferF =
        "in vec3 vary_pos;\n"
        "in vec3 vary_norm;\n"
        "uniform vec3 light;\n"
        "uniform int attachment;\n"
        "uniform float tag;\n"
        "out vec4 frag_color;\n"
        "void main()\n"
        "{\n"
        "    vec3 facet = normalize(cross(dFdx(vary_pos), dFdy(vary_pos)));\n"
        "    facet = dot(facet, vary_pos) > 0.0 ? -facet : facet;\n"
        "    float d = (-vary_pos.z - 0.05) / 20.0;\n"
        "    gl_FragDepth = REVERSED ? 1.0 - d : d;\n"
        "    frag_color = attachment == 0 ? vec4(vary_pos, tag) : vec4(normalize(vary_norm), dot(facet, light));\n"
        "}\n";

    // The sun pass on it: its shadow, the shading n.l, the facet's, and whether the sphere covers the pixel: 1 where
    // the ray to the sun clears the occluder (`occ`, a centre and radius; none at radius 0), 2 where it passes well
    // inside it, 3 near its edge.
    const char* kSphereReceiverF =
        "uniform sampler2D gpos;\n"
        "uniform sampler2D gnorm;\n"
        "uniform vec3 light;\n"
        "uniform vec4 occ;\n"
        "out vec4 frag_color;\n"
        "float sampleDirectionalShadowFacing(vec3 pos, vec3 norm, vec3 geo_norm, vec2 pos_screen);\n"
        "void main()\n"
        "{\n"
        "    vec4 p = texelFetch(gpos, ivec2(gl_FragCoord.xy), 0);\n"
        "    vec4 n = texelFetch(gnorm, ivec2(gl_FragCoord.xy), 0);\n"
        "    float sun = sampleDirectionalShadowFacing(p.xyz, n.xyz, n.xyz, gl_FragCoord.xy / 512.0);\n"
        "    float cls = 0.0;\n"
        "    if (p.w > 0.5 && p.w < 1.5)\n"
        "    {\n"
        "        vec3 to = occ.xyz - p.xyz;\n"
        "        float along = dot(to, light);\n"
        "        float miss = length(to - light * along);\n"
        "        cls = (occ.w <= 0.0 || along <= 0.0 || miss > 1.15 * occ.w) ? 1.0 : (miss < 0.85 * occ.w ? 2.0 : 3.0);\n"
        "    }\n"
        "    frag_color = vec4(sun, dot(n.xyz, light), n.w, cls);\n"
        "}\n";

    struct shadowutil_sphere_data
    {
        shadowutil_sphere_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~shadowutil_sphere_data()
        {
            for (S32 channel : mChannels)
            {
                if (channel > -1)
                {
                    gGL.getTextureSlot(channel)->unbind();
                    gGL.getTextureSlot(channel)->bindSampler(0);
                }
            }
            gGL.getTextureSlot(GPOS_UNIT)->unbind();
            gGL.getTextureSlot(GNORM_UNIT)->unbind();
            LLGLSLShader::unbind();
            mMapProgram.unload();
            mGBufferProgram.unload();
            mProgram.unload();
            mShaders.clearShaderObjects();
            mBlock.release();
            mMap.release();
            mPos.release();
            mNorm.release();
            mOut.release();
            mSphere = nullptr;
            mOccluder = nullptr;
            mTriangle = nullptr;
        }

        static constexpr U32 GPOS_UNIT  = 10;
        static constexpr U32 GNORM_UNIT = 11;

        static std::string header()
        {
            return std::string("#version 400\n#define REVERSED ") + (LLRender::sReverseZ ? "true" : "false") + "\n";
        }

        static void link(LLGLSLShader& program, const char* name, const std::string& vertex, const std::string& fragment,
                         GLuint util)
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, vertex.c_str());
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, fragment.c_str());
            {
                INFO(name << " compiles");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            program.mProgramObject = glCreateProgram();
            program.mName = name;
            program.attachObject(vs);
            program.attachObject(fs);
            if (util)
            {
                program.attachObject(util);
            }
            UNSCOPED_INFO(name << " links");
            REQUIRE(program.mapAttributes());
            glDeleteShader(vs);
            glDeleteShader(fs);
            UNSCOPED_INFO(name << " maps");
            REQUIRE(program.mapUniforms());
        }

        static GLint location(const LLGLSLShader& program, const char* name)
        {
            return glGetUniformLocation(program.mProgramObject, name);
        }

        static constexpr U32 SPHERE_INDICES = SPHERE_STACKS * SPHERE_SLICES * 6;

        static LLPointer<LLVertexBuffer> makeSphere(const LLVector3& centre, F32 radius)
        {
            const U32 vertices = (SPHERE_STACKS + 1) * (SPHERE_SLICES + 1);
            LLPointer<LLVertexBuffer> sphere = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL);
            REQUIRE(sphere->allocateBuffer(vertices, SPHERE_INDICES));
            LLStrider<LLVector3> pos;
            LLStrider<LLVector3> norm;
            LLStrider<U16> index;
            sphere->getVertexStrider(pos);
            sphere->getNormalStrider(norm);
            sphere->getIndexStrider(index);
            for (S32 i = 0; i <= SPHERE_STACKS; ++i)
            {
                const F32 polar = F_PI * (F32)i / SPHERE_STACKS;
                for (S32 j = 0; j <= SPHERE_SLICES; ++j)
                {
                    const F32 around = 2.f * F_PI * (F32)j / SPHERE_SLICES;
                    const LLVector3 dir(sinf(polar) * cosf(around), cosf(polar), sinf(polar) * sinf(around));
                    *pos++ = centre + dir * radius;
                    *norm++ = dir;
                }
            }
            for (S32 i = 0; i < SPHERE_STACKS; ++i)
            {
                for (S32 j = 0; j < SPHERE_SLICES; ++j)
                {
                    const U16 a = (U16)(i * (SPHERE_SLICES + 1) + j);
                    const U16 b = (U16)(a + SPHERE_SLICES + 1);
                    *index++ = a;
                    *index++ = b;
                    *index++ = (U16)(a + 1);
                    *index++ = (U16)(a + 1);
                    *index++ = b;
                    *index++ = (U16)(b + 1);
                }
            }
            sphere->unmapBuffer();
            return sphere;
        }

        // The sphere, the occluder where `occ` has a radius, and the full-screen triangle.
        void buildScene(const LLVector4& occ)
        {
            mSphere = makeSphere(LLVector3(0.f, 0.f, -SPHERE_DEPTH), SPHERE_RADIUS);
            if (occ.mV[3] > 0.f)
            {
                mOccluder = makeSphere(LLVector3(occ.mV[0], occ.mV[1], occ.mV[2]), occ.mV[3]);
            }

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();
        }

        // Eye space to the map's texture space under the perspective fit, column-major, the sphere `ys` out from its
        // origin along the fit's axis.
        static void fitMatrix(const LLVector3& light, F32 ys, F32 out[16])
        {
            LLVector3 axis = LLVector3(0.f, 0.f, -1.f);
            axis -= light * (axis * light);
            axis.normVec();
            LLVector3 across = axis % light;
            across.normVec();
            const LLVector3 origin = LLVector3(0.f, 0.f, -SPHERE_DEPTH) - axis * ys;

            const F32 fx = 2.f * ys / SPHERE_FIT_WIDTH;
            const F32 fz = (1.f - 2.f * SPHERE_FIT_CLEAR) * SPHERE_FIT_FAR / SPHERE_FIT_SPAN;
            const F32 a = (SPHERE_FIT_FAR + SPHERE_FIT_NEAR) / (SPHERE_FIT_FAR - SPHERE_FIT_NEAR);
            const F32 b = -2.f * SPHERE_FIT_FAR * SPHERE_FIT_NEAR / (SPHERE_FIT_FAR - SPHERE_FIT_NEAR);
            // Nearer the light is smaller depth forward, larger under reverse-Z.
            const F32 dz = LLRender::sReverseZ ? 0.5f * fz : -0.5f * fz;

            // Each row is half the clip coordinate plus half of w, over w = axis . (p - origin).
            const auto row = [out](S32 r, const LLVector3& v, F32 c)
            {
                out[r] = v.mV[0];
                out[4 + r] = v.mV[1];
                out[8 + r] = v.mV[2];
                out[12 + r] = c;
            };
            const F32 w0 = axis * origin;
            row(0, across * (0.5f * fx) + axis * 0.5f, -0.5f * fx * (across * origin) - 0.5f * w0);
            row(1, axis * (0.5f * a + 0.5f), -(0.5f * a + 0.5f) * w0 + 0.5f * b);
            row(2, light * dz + axis * 0.5f, -dz * (light * origin) - 0.5f * w0);
            row(3, axis, -w0);
        }

        // Every mesh of the scene, the G-buffer's `tag` uniform naming each (1 the sphere, 2 the occluder) where it is
        // bound.
        void drawScene(GLint tag)
        {
            if (tag > -1)
            {
                glUniform1f(tag, 1.f);
            }
            mSphere->setBuffer();
            mSphere->draw(LLRender::TRIANGLES, SPHERE_INDICES, 0);
            if (mOccluder)
            {
                if (tag > -1)
                {
                    glUniform1f(tag, 2.f);
                }
                mOccluder->setBuffer();
                mOccluder->draw(LLRender::TRIANGLES, SPHERE_INDICES, 0);
            }
        }

        // The sun pass over the sphere at the filter tier `pcss` names (the gather tier at High's kernel otherwise), the
        // sphere `ys` out from the fit's origin, with an occluder where `occ` (centre, radius) has a radius.
        void run(bool pcss, F32 ys, const LLVector4& occ = LLVector4(0.f, 0.f, 0.f, 0.f))
        {
            std::map<std::string, std::string> defines = { { "SUN_SHADOW", "1" },
                                                            { "SHADOW_PCSS", pcss ? "1" : "0" },
                                                            { "SHADOW_PCF_KERNEL", "6" } };
            S32 level = 1;
            const GLuint util = mShaders.loadShaderFile("deferred/shadowUtil.glsl", level, GL_FRAGMENT_SHADER, &defines);
            {
                INFO("shadowUtil compiles");
                REQUIRE(util != 0);
            }
            link(mMapProgram, "sphere map (test)", header() + kSphereMapV, header() + kSphereMapF, 0);
            link(mGBufferProgram, "sphere gbuffer (test)", header() + kSphereGBufferV, header() + kSphereGBufferF, 0);
            link(mProgram, "sphere receiver (test)", kVertex, header() + kSphereReceiverF, util);
            buildScene(occ);

            LLVector3 light(1.f, 0.6f, 0.2f);
            light.normVec();
            F32 to_map[16];
            fitMatrix(light, ys, to_map);

            const LLRenderTarget::eDepthFormat depth_fmt =
                LLRender::sReverseZ ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
            const F32 clear_depth = LLRender::sReverseZ ? 0.f : 1.f;

            REQUIRE(mMap.allocate(SPHERE_MAP, SPHERE_MAP, 0, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                  depth_fmt));
            mMap.bindTarget();
            mMapProgram.bind();
            glUniformMatrix4fv(location(mMapProgram, "to_map"), 1, GL_FALSE, to_map);
            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                glClearDepth(clear_depth);
                glClear(GL_DEPTH_BUFFER_BIT);
            }
            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_LESS);
                drawScene(-1);
            }
            mMapProgram.unbind();
            mMap.flush();

            const F32 f = 1.f / tanf(20.f * DEG_TO_RAD);
            const F32 cam_proj[16] = { f, 0.f, 0.f, 0.f, 0.f, f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 0.f };
            for (S32 attachment = 0; attachment < 2; ++attachment)
            {
                LLRenderTarget& target = attachment == 0 ? mPos : mNorm;
                REQUIRE(target.allocate(SPHERE_OUT, SPHERE_OUT, GL_RGBA32F, true, false, ALTextureSlot::TT_TEXTURE,
                                        LLRenderTarget::MIPS_NONE, depth_fmt));
                target.bindTarget();
                mGBufferProgram.bind();
                glUniformMatrix4fv(location(mGBufferProgram, "cam_proj"), 1, GL_FALSE, cam_proj);
                glUniform3f(location(mGBufferProgram, "light"), light.mV[0], light.mV[1], light.mV[2]);
                glUniform1i(location(mGBufferProgram, "attachment"), attachment);
                {
                    LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                    glClearColor(0.f, 0.f, 0.f, 0.f);
                    glClearDepth(clear_depth);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
                {
                    LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_LESS);
                    drawScene(location(mGBufferProgram, "tag"));
                }
                mGBufferProgram.unbind();
                target.flush();
            }

            // The fit's matrix on every cascade, at the stock bias and offset as LLPipeline::packDeferredUBO signs them.
            DeferredBlock block{};
            for (U32 i = 0; i < 4; ++i)
            {
                std::copy(std::begin(to_map), std::end(to_map), block.shadow_matrix[i]);
            }
            std::copy(std::begin(SHADOW_CLIP), std::end(SHADOW_CLIP), block.shadow_clip);
            block.shadow_res[0] = block.shadow_res[1] = (F32)SPHERE_MAP;
            block.shadow_bias = LLRender::sReverseZ ? STOCK_SHADOW_BIAS : -STOCK_SHADOW_BIAS;
            block.shadow_offset = STOCK_SHADOW_OFFSET;
            block.shadow_pcss_scale = 200.f;
            mBlock.update(&block, sizeof(block));

            REQUIRE(mOut.allocate(SPHERE_OUT, SPHERE_OUT, GL_RGBA32F));
            mOut.bindTarget();
            mProgram.bind();
            mBlock.bind(LLGLSLShader::UB_DEFERRED);
            const U32 sampler = gGL.getSampler(pcss ? ALSamplers::PointClamp : ALSamplers::ShadowCompare);
            for (U32 i = 0; i < 4; ++i)
            {
                mChannels[i] = mProgram.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
                CAPTURE(i);
                REQUIRE(mChannels[i] > -1);
                gGL.getTextureSlot(mChannels[i])->bind(&mMap, true, sampler);
            }
            gGL.getTextureSlot(GPOS_UNIT)->bind(&mPos);
            gGL.getTextureSlot(GNORM_UNIT)->bind(&mNorm);
            glUniform1i(location(mProgram, "gpos"), GPOS_UNIT);
            glUniform1i(location(mProgram, "gnorm"), GNORM_UNIT);
            glUniform3f(location(mProgram, "light"), light.mV[0], light.mV[1], light.mV[2]);
            glUniform4f(location(mProgram, "occ"), occ.mV[0], occ.mV[1], occ.mV[2], occ.mV[3]);
            mProgram.uniform3f(LLShaderMgr::DEFERRED_SUN_DIR, light.mV[0], light.mV[1], light.mV[2]);
            mProgram.uniform3f(LLShaderMgr::DEFERRED_MOON_DIR, light.mV[0], light.mV[1], light.mV[2]);
            mProgram.uniform1i(LLShaderMgr::SUN_UP_FACTOR, 1);
            mProgram.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, 0);
            mProgram.uniform1i(LLShaderMgr::SUN_SHADOW_CASCADES, 4);
            mProgram.uniform1i(LLShaderMgr::UNBUILT_SHADOW_MAPS, 0);
            mProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)SPHERE_OUT, (F32)SPHERE_OUT);
            {
                LLGLDepthTest depth_test(GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mProgram.unbind();

            mPixels.assign(size_t(SPHERE_OUT) * SPHERE_OUT * 4, 0.f);
            glReadPixels(0, 0, SPHERE_OUT, SPHERE_OUT, GL_RGBA, GL_FLOAT, mPixels.data());
            mOut.flush();
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mMapProgram;
        LLGLSLShader mGBufferProgram;
        LLGLSLShader mProgram;
        ALUniformBuffer mBlock;
        LLRenderTarget mMap;
        LLRenderTarget mPos;
        LLRenderTarget mNorm;
        LLRenderTarget mOut;
        LLPointer<LLVertexBuffer> mSphere;
        LLPointer<LLVertexBuffer> mOccluder;
        LLPointer<LLVertexBuffer> mTriangle;
        S32 mChannels[4] = { -1, -1, -1, -1 };
        std::vector<F32> mPixels;
    };
}

TEST_CASE_METHOD(shadowutil_sphere_data, "shadowutil_gl: a faceted sphere far out along a perspective fit reads lit on every facet the sun faces",
                 "[shadowutil_gl]")
{
    const bool pcss = GENERATE(false, true);
    const F32 ys = GENERATE(2.f, 5.f);
    CAPTURE(pcss, ys);
    run(pcss, ys);

    // A facet the sun faces, seen where its shading normals face it too, holds nothing between it and the sun but its own
    // plane. A receiver the fixed kernels push out too little, or one whose receiver-plane bias carries the taps short of
    // a slope that steep, shadows its facet by itself, in steps a texel tall, a facet at a time. Only the pixels at the
    // sphere's silhouette and its facets' edges, whose quads take a slope from two planes, may read dark.
    S32 facing = 0;
    S32 dark = 0;
    for (size_t i = 0; i < mPixels.size(); i += 4)
    {
        const F32 sun = mPixels[i];
        const F32 shading_nl = mPixels[i + 1];
        const F32 facet_nl = mPixels[i + 2];
        const bool open = mPixels[i + 3] == 1.f;
        if (open && shading_nl > 0.2f && facet_nl > 0.05f)
        {
            ++facing;
            dark += sun < 0.95f;
        }
    }
    CAPTURE(facing, dark);
    CHECK(facing > 30000);
    CHECK(dark * 200 < facing);
}

TEST_CASE_METHOD(shadowutil_sphere_data, "shadowutil_gl: an occluder's shadow on a faceted sphere stays dark and the rest lit",
                 "[shadowutil_gl]")
{
    // A smaller sphere 2 cm off the first toward the sun, its shadow on the facets the sun faces most squarely, half a
    // metre out along the fit, where a texel is a few millimetres. The fixed kernels alone: PCSS widens a shadow cast
    // from that far by design.
    LLVector3 light(1.f, 0.6f, 0.2f);
    light.normVec();
    const F32 radius = 0.06f;
    const LLVector3 centre = LLVector3(0.f, 0.f, -SPHERE_DEPTH) + light * (SPHERE_RADIUS + radius + 0.02f);
    run(false, 0.5f, LLVector4(centre.mV[0], centre.mV[1], centre.mV[2], radius));

    // A receiver pushed out farther than its kernel needs lies past the occluder near the edge of its shadow, and reads
    // lit there; one pushed too little shadows its own facets.
    S32 facing = 0;
    S32 dark = 0;
    S32 occluded = 0;
    S32 lit = 0;
    F32 occluded_sun = 0.f;
    for (size_t i = 0; i < mPixels.size(); i += 4)
    {
        const F32 sun = mPixels[i];
        const F32 shading_nl = mPixels[i + 1];
        const F32 facet_nl = mPixels[i + 2];
        const F32 cls = mPixels[i + 3];
        if (cls == 1.f && shading_nl > 0.2f && facet_nl > 0.05f)
        {
            ++facing;
            dark += sun < 0.95f;
        }
        else if (cls == 2.f && shading_nl > 0.05f)
        {
            ++occluded;
            lit += sun > 0.25f;
            occluded_sun += sun;
        }
    }
    CAPTURE(facing, dark, occluded, lit, occluded_sun);
    CHECK(facing > 30000);
    CHECK(dark * 200 < facing);
    CHECK(occluded > 1000);
    CHECK(lit == 0);
    CHECK(occluded_sun < 0.02f * occluded);
}

