/**
 * @file shadowutil_gl_test.cpp
 * @brief shadowUtil.glsl's samplers on the hidden window, with a map on every shadow unit that shadows every receiver:
 *        a map the pass has none of (unbuilt_shadow_maps) reads lit, at its cascade's weight, and the rest as their
 *        maps say, under the compare and the PCSS samplers alike.
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
    };
    static_assert(sizeof(DeferredBlock) == 496);

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

            // Every receiver to the map's centre at half depth, which the map's texels lie nearer the light than.
            DeferredBlock block{};
            for (auto& matrix : block.shadow_matrix)
            {
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
}

TEST_CASE_METHOD(shadowutil_data, "shadowutil_gl: a map the pass has none of reads lit, and the rest as their maps say", "[shadowutil_gl]")
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
