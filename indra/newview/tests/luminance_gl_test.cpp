/**
 * @file luminance_gl_test.cpp
 * @brief The exposure meter's grid through the viewer's own program (luminanceF.glsl), over a G-buffer as the deferred
 *        pass and the water leave it: the sky, the world, and the water over each, with the water's map made as
 *        LLPipeline::generateLuminance makes it.
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

#include "alpostchain_gl_fixture.h"

#include "alwaterstencil.h"
#include "llmath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

namespace
{
    // The scene: the world over the left half and nothing over the right, and the water over the lower half of both.
    // The meter's grid is four texels by two, each a 16 x 16 patch lying wholly in one of the four.
    constexpr U32 W      = 64;
    constexpr U32 H      = 32;
    constexpr U32 GRID_W = 4;
    constexpr U32 GRID_H = 2;

    // Each part's luminance in the HDR scene.
    constexpr F32 WORLD_LUM = 2.f;
    constexpr F32 SKY_LUM   = 4.f;
    constexpr F32 WATER_LUM = 1.f;

    // The world's flag in the normals' alpha (GBUFFER_FLAG_HAS_ATMOS), and the depths the world and the water lie at.
    constexpr F32 WORLD_FLAG  = 0.34f;
    constexpr F64 WORLD_DEPTH = 0.5;
    constexpr F64 WATER_DEPTH = 0.25;

    // A patch's texel of the grid: the mean luminance over its world, over its sky, and the share of it that is sky.
    struct Patch
    {
        F32 mGround = 0.f;
        F32 mSky    = 0.f;
        F32 mShare  = 0.f;
    };

    class LuminanceMeter
    {
    public:
        LuminanceMeter()
        {
            const auto deferred = [](LLShaderFeatures& features) { features.isDeferred = true; };
            mPost.build(mMeter, "Exposure Meter Grid",
                        { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER },
                          { "deferred/luminanceF.glsl", GL_FRAGMENT_SHADER } },
                        {}, deferred);
            mPost.build(mCopy, "Copy Shader",
                        { { "interface/copyV.glsl", GL_VERTEX_SHADER }, { "interface/copyF.glsl", GL_FRAGMENT_SHADER } });
        }

        ~LuminanceMeter()
        {
            mPost.unloadPrograms();
            // The targets sharing the G-buffer's depth go before it.
            for (LLRenderTarget* target : { &mWater, &mScreen, &mGrid, &mGBuffer })
            {
                target->release();
            }
            glClearDepth(1.0);
            glClearStencil(0);
        }

        // The G-buffer, its normal in `normal_format`, cleared as the frame clears it, magenta with the far plane's
        // depth, and the world laid over the left half; the water's depth over the lower half and its own pixels
        // marked in the stencil, as the water's draws leave them; and the screen, sharing the G-buffer's depth and
        // stencil, holding each part's luminance.
        void setUp(U32 normal_format)
        {
            const ALRenderTargetDesc gbuffer{ .width   = W,
                                              .height  = H,
                                              .color   = { GL_SRGB8_ALPHA8, GL_RGBA8, normal_format },
                                              .depth   = ALRenderTargetDesc::Depth::D24,
                                              .stencil = true };
            REQUIRE(mGBuffer.ensure(gbuffer) == LLRenderTarget::EnsureResult::Built);
            REQUIRE(mScreen.ensure({ .width = W, .height = H, .color = { GL_RGBA16F } }, mGBuffer) ==
                    LLRenderTarget::EnsureResult::Built);
            // The light map's description, as the pipeline takes the map in.
            REQUIRE(mWater.ensure({ .width = W, .height = H, .color = { GL_RGBA8 } }, mGBuffer) ==
                    LLRenderTarget::EnsureResult::Built);
            REQUIRE(mGrid.ensure({ .width = GRID_W, .height = GRID_H, .color = { GL_RGBA16F } }) ==
                    LLRenderTarget::EnsureResult::Built);

            mGBuffer.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE);
                gGL.setClearColor(LLColor4::magenta);
                glClearDepth(1.0);
                glClearStencil(0);
                mGBuffer.clear();
                {
                    LLGLSScissor world(0, 0, W / 2, H);
                    gGL.setClearColor(LLColor4(0.5f, 0.5f, 0.5f, WORLD_FLAG));
                    glClearDepth(WORLD_DEPTH);
                    mGBuffer.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
                {
                    LLGLSScissor water(0, 0, W, H / 2);
                    glClearDepth(WATER_DEPTH);
                    glClearStencil((GLint)ALWaterStencil::SURFACE_BIT);
                    mGBuffer.clear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                }
                glClearDepth(1.0);
                glClearStencil(0);
                gGL.setClearColor(LLColor4::transparent);
            }
            mGBuffer.flush();

            std::vector<F32> scene(size_t(W) * H * 4, 0.f);
            for (U32 y = 0; y < H; ++y)
            {
                for (U32 x = 0; x < W; ++x)
                {
                    const F32 lum = y < H / 2 ? WATER_LUM : (x < W / 2 ? WORLD_LUM : SKY_LUM);
                    F32* texel    = &scene[(size_t(y) * W + x) * 4];
                    texel[0] = texel[1] = texel[2] = lum;
                }
            }
            ll_test::PostChainGL::write(mScreen, scene);
        }

        // LLPipeline::generateLuminance's map of the water: cleared, then the G-buffer's normals copied into it under the
        // water's stencil.
        void drawWaterMap()
        {
            mWater.bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mWater.clear(GL_COLOR_BUFFER_BIT);
            {
                LLGLDepthTest           no_depth(GL_FALSE, GL_FALSE);
                LLGLDisable             no_blend(GL_BLEND);
                ALWaterStencil::OnWater on_water;
                mCopy.bind();
                mCopy.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mGBuffer, ALSamplers::PointClamp, 2);
                mPost.drawTriangle();
                mCopy.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                mCopy.unbind();
            }
            mWater.flush();
        }

        // The grid as generateLuminance draws it, the water's map bound where `with_water` and the unit left holding
        // nothing otherwise, each patch's texel in rows from the bottom.
        std::vector<Patch> meter(bool with_water)
        {
            mGrid.bindTarget();
            {
                LLGLDepthTest depth(GL_FALSE, GL_FALSE);
                LLGLDisable   no_blend(GL_BLEND);
                mMeter.bind();
                const S32 scene = mMeter.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mScreen, ALSamplers::BilinearClamp);
                const S32 depth_channel = mMeter.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mGBuffer);
                const S32 water = mMeter.enableTexture(LLShaderMgr::METER_WATER_MAP);
                if (water > -1)
                {
                    if (with_water)
                    {
                        mWater.bindTexture(0, water, ALSamplers::PointClamp);
                    }
                    else
                    {
                        gGL.getTextureSlot(water)->unbind();
                    }
                }
                const F32 patch_w = (F32)W / (F32)GRID_W;
                const F32 patch_h = (F32)H / (F32)GRID_H;
                mMeter.uniform4f(LLShaderMgr::METER_PARAMS, patch_w, patch_h, (F32)llmax(1, llceil(patch_w * 0.5f)),
                                 (F32)llmax(1, llceil(patch_h * 0.5f)));
                mPost.drawTriangle();
                for (S32 channel : { scene, depth_channel, water })
                {
                    if (channel > -1)
                    {
                        gGL.getTextureSlot(channel)->unbind();
                    }
                }
                mMeter.unbind();
            }
            mGrid.flush();

            const std::vector<F32> texels = ll_test::PostChainGL::read(mGrid);
            std::vector<Patch> patches(size_t(GRID_W) * GRID_H);
            for (size_t i = 0; i < patches.size(); ++i)
            {
                patches[i] = { texels[i * 4], texels[i * 4 + 1], texels[i * 4 + 2] };
            }
            return patches;
        }

        ll_test::SharedGLScope mGL;
        ll_test::PostChainGL   mPost;
        LLGLSLShader           mMeter;
        LLGLSLShader           mCopy;
        LLRenderTarget         mGBuffer;
        LLRenderTarget         mScreen;
        LLRenderTarget         mWater;
        LLRenderTarget         mGrid;
    };

    // Within the grid's half floats of what a patch should hold.
    bool closeTo(F32 value, F32 expected)
    {
        return std::fabs(value - expected) <= 1e-3f * std::fmax(1.f, std::fabs(expected));
    }
}

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: water over the sky meters as sky and water over the world as the world",
                 "[luminance_gl]")
{
    // The normal's two layouts (GBUFFER_NORM_HDR), whose alpha holds the flags and the clear's 1 either way.
    const U32 normal_format = GENERATE((U32)GL_RGB10_A2, (U32)GL_RGBA16);
    CAPTURE(normal_format);
    setUp(normal_format);
    drawWaterMap();
    const std::vector<Patch> grid = meter(true);

    for (U32 gy = 0; gy < GRID_H; ++gy)
    {
        for (U32 gx = 0; gx < GRID_W; ++gx)
        {
            CAPTURE(gx, gy);
            const Patch& patch = grid[size_t(gy) * GRID_W + gx];
            CAPTURE(patch.mGround, patch.mSky, patch.mShare);
            const bool water = gy == 0;
            const bool sky   = gx >= GRID_W / 2;
            if (sky)
            {
                UNSCOPED_INFO((water ? "the water over the sky is sky" : "the sky is sky"));
                CHECK(closeTo(patch.mShare, 1.f));
                CHECK(closeTo(patch.mSky, water ? WATER_LUM : SKY_LUM));
                CHECK(closeTo(patch.mGround, 0.f));
            }
            else
            {
                UNSCOPED_INFO((water ? "the water over the world is the world" : "the world is the world"));
                CHECK(closeTo(patch.mShare, 0.f));
                CHECK(closeTo(patch.mGround, water ? WATER_LUM : WORLD_LUM));
                CHECK(closeTo(patch.mSky, 0.f));
            }
        }
    }
}

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: without the water's map no water meters as sky", "[luminance_gl]")
{
    setUp(GL_RGB10_A2);
    drawWaterMap();
    const std::vector<Patch> grid = meter(false);

    for (U32 gy = 0; gy < GRID_H; ++gy)
    {
        for (U32 gx = 0; gx < GRID_W; ++gx)
        {
            CAPTURE(gx, gy);
            const Patch& patch = grid[size_t(gy) * GRID_W + gx];
            CAPTURE(patch.mGround, patch.mSky, patch.mShare);
            const bool sky = gy == 1 && gx >= GRID_W / 2;
            CHECK(closeTo(patch.mShare, sky ? 1.f : 0.f));
        }
    }
}
