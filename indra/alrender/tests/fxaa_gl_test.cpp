/**
 * @file fxaa_gl_test.cpp
 * @brief fxaaF.glsl on the hidden window, built as LLViewerShaderMgr builds gFXAAProgram and run straight on an
 *        RGB10_A2 source as applyFXAA runs it: luma taken from the colour though the source's alpha holds none, a
 *        pixel left alone keeping the source's 10-bit colour, and luma in every pixel's alpha.
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

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
        constexpr S32 W = 64;
        constexpr S32 H = 16;

        // Where the source steps from the ramp to the dark block: a vertical edge between columns EDGE - 1 and EDGE.
        constexpr S32 EDGE = 48;

        // The source, in 10-bit units: a ramp one unit per column in each channel, too shallow for FXAA to call an
        // edge, then a dark block. Alpha is 0 throughout, so nothing in it says where the edge is.
        void sourceTexel(S32 x, U32& r, U32& g, U32& b)
        {
            if (x < EDGE)
            {
                r = 300 + (U32)x;
                g = 400 + (U32)x;
                b = 500 + (U32)x;
            }
            else
            {
                r = g = b = 50;
            }
        }

        F32 luma(F32 r, F32 g, F32 b)
        {
            return r * 0.299f + g * 0.587f + b * 0.114f;
        }

    struct fxaa_data
    {
        fxaa_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~fxaa_data()
        {
            LLGLSLShader::unbind();
            mFXAA.unload();
            mShaders.clearShaderObjects();
            mSource.release();
            mDest.release();
            mTriangle = nullptr;
        }

        // gFXAAProgram at the default quality (RenderFSAASamples 0, preset 12) on a GL 4 context, the source an
        // RGB10_A2 target as postPingMap is under HDR, with the depth applyFXAA binds, and a float destination so
        // the alpha reads back at more than RGB10_A2's two bits.
        void setUp()
        {
            // The shared objects LLViewerShaderMgr::loadBasicShaders compiles for every program to attach.
            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                CAPTURE(object);
                S32 level = 1;
                REQUIRE(mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            S32 level = 1;
            REQUIRE(mShaders.loadShaderFile("deferred/globalF.glsl", level, GL_FRAGMENT_SHADER, &defines) != 0);

            mFXAA.mName = "FXAA Shader (Low)";
            mFXAA.mShaderFiles.clear();
            mFXAA.mShaderFiles.push_back(std::make_pair("deferred/postDeferredV.glsl", GL_VERTEX_SHADER));
            mFXAA.mShaderFiles.push_back(std::make_pair("deferred/fxaaF.glsl", GL_FRAGMENT_SHADER));
            mFXAA.clearPermutations();
            mFXAA.addPermutation("FXAA_QUALITY_PRESET", "12");
            mFXAA.addPermutation("FXAA_GLSL_400", "1");
            mFXAA.mShaderLevel = 1;
            REQUIRE(mFXAA.createShader());

            REQUIRE(mSource.allocate(W, H, GL_RGB10_A2, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                     LLRenderTarget::DEPTH_FMT_24));
            REQUIRE(mDest.allocate(W, H, GL_RGBA16F));

            std::vector<U32> texels(size_t(W) * H);
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    U32 r, g, b;
                    sourceTexel(x, r, g, b);
                    texels[size_t(y) * W + x] = r | (g << 10) | (b << 20);
                }
            }
            glBindTexture(GL_TEXTURE_2D, mSource.getTexture());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, texels.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();
        }

        // applyFXAA's pass, with its uniforms for a source the destination's size.
        void run()
        {
            mDest.bindTarget();
            mFXAA.bind();
            const S32 channel = mFXAA.enableTexture(LLShaderMgr::DIFFUSE_MAP);
            REQUIRE(channel > -1);
            mSource.bindTexture(0, channel, ALSamplers::BilinearMirror);
            mFXAA.uniform2f(LLShaderMgr::FXAA_TC_SCALE, 1.f, 1.f);
            mFXAA.uniform2f(LLShaderMgr::FXAA_RCP_SCREEN_RES, 1.f / W, 1.f / H);
            mFXAA.uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT, -0.5f / W, -0.5f / H, 0.5f / W, 0.5f / H);
            mFXAA.uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT2, -2.f / W, -2.f / H, 2.f / W, 2.f / H);
            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                const S32 depth_channel = mFXAA.getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);
                gGL.getTextureSlot(depth_channel)->bind(&mSource, true);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mFXAA.unbind();

            mOut.assign(size_t(W) * H * 4, 0.f);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, mOut.data());
            mDest.flush();
        }

        const F32* at(S32 x, S32 y) const { return &mOut[(size_t(y) * W + x) * 4]; }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mFXAA;
        LLRenderTarget mSource;
        LLRenderTarget mDest;
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<F32> mOut;
    };
}

TEST_CASE_METHOD(fxaa_data, "fxaa_gl: a pixel FXAA leaves alone keeps the source's 10-bit colour", "[fxaa_gl]")
{
    setUp();
    run();

    // Away from the edge and the borders, a ramp one 10-bit unit per column: an 8-bit step anywhere on the way
    // would put some of these more than a quarter unit off.
    for (S32 y = 2; y < H - 2; ++y)
    {
        for (S32 x = 2; x < EDGE - 4; ++x)
        {
            CAPTURE(x, y);
            U32 r, g, b;
            sourceTexel(x, r, g, b);
            const F32* px = at(x, y);
            CHECK(std::fabs(px[0] * 1023.f - (F32)r) < 0.25f);
            CHECK(std::fabs(px[1] * 1023.f - (F32)g) < 0.25f);
            CHECK(std::fabs(px[2] * 1023.f - (F32)b) < 0.25f);
        }
    }
}

TEST_CASE_METHOD(fxaa_data, "fxaa_gl: an edge in the colour is anti-aliased though the source's alpha is flat", "[fxaa_gl]")
{
    setUp();
    run();

    // Luma read from the source's alpha would see nothing here and leave both sides of the edge as they were.
    S32 blended = 0;
    for (S32 y = 2; y < H - 2; ++y)
    {
        for (S32 x : { EDGE - 1, EDGE })
        {
            U32 r, g, b;
            sourceTexel(x, r, g, b);
            if (std::fabs(at(x, y)[1] * 1023.f - (F32)g) > 2.f)
            {
                ++blended;
            }
        }
    }
    CHECK(blended > 0);
}

TEST_CASE_METHOD(fxaa_data, "fxaa_gl: every pixel's alpha is its luma", "[fxaa_gl]")
{
    setUp();
    run();

    // The flat pixels return early, the edge's go through the search; both put the luma of the pixel's own source
    // colour in alpha (an edge pixel's colour is blended, its alpha is not), never the source's 0.
    for (S32 y = 2; y < H - 2; ++y)
    {
        for (S32 x = 2; x < W - 2; ++x)
        {
            CAPTURE(x, y);
            U32 r, g, b;
            sourceTexel(x, r, g, b);
            CHECK(std::fabs(at(x, y)[3] - luma(r / 1023.f, g / 1023.f, b / 1023.f)) < 0.002f);
        }
    }
}
