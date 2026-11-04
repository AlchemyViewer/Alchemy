/**
 * @file luminance_gl_test.cpp
 * @brief The exposure meter's grid through the viewer's own program (luminanceF.glsl), over a G-buffer as the deferred
 *        pass and the water leave it: the sky, the world, and the water over each, with the water's map made as
 *        LLPipeline::generateLuminance makes it. And incident metering over it: the world's luminance divided by its
 *        albedo, and the pixels of blended surfaces, which the albedo cannot speak for, marked in the stencil and
 *        read through a mask made as generateLuminance makes that.
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

#include "alblendstencil.h"
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
    // The grid's alpha too: the share of the patch's exposure that its albedo has earned.
    struct Patch
    {
        F32 mGround = 0.f;
        F32 mSky    = 0.f;
        F32 mShare  = 0.f;
        F32 mEarned = 1.f;
    };

    // What luminanceF.glsl is set to by default (AlchemyExposureIncidentTuning), and the key it reads a pixel the G-buffer
    // cannot speak for as reflecting (NOMINAL_ALBEDO). The test is the spec: a change to either is a change here.
    constexpr F32 ALBEDO_FLOOR   = 0.1f;
    constexpr F32 TRUSTED_FROM   = 0.02f;
    constexpr F32 TRUSTED_TO     = 0.12f;
    constexpr F32 NOMINAL_ALBEDO = 0.18f;

    F32 srgbEncode(F32 linear)
    {
        return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
    }

    F32 srgbDecode(F32 encoded)
    {
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    // The albedo the G-buffer's sRGB8 attachment holds for `linear`, which is what the meter reads back: stored in eight
    // bits, so a little off what was asked for.
    F32 storedAlbedo(F32 linear)
    {
        return srgbDecode(std::round(srgbEncode(linear) * 255.f) / 255.f);
    }

    // A patch of mean luminance `lum` and mean albedo `albedo` as luminanceF.glsl writes it at `strength`: the luminance
    // divided by the albedo to that power, and the share of the exposure the albedo has earned.
    Patch incident(F32 lum, F32 albedo, F32 strength)
    {
        Patch patch;
        patch.mGround = lum;
        if (strength > 0.f)
        {
            const F32 t     = llclamp((albedo - TRUSTED_FROM) / (TRUSTED_TO - TRUSTED_FROM), 0.f, 1.f);
            const F32 trust = t * t * (3.f - 2.f * t);
            patch.mGround   = lum / std::pow(std::fmax(albedo, ALBEDO_FLOOR), strength);
            patch.mEarned   = (1.f - strength) + strength * trust;
        }
        return patch;
    }

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
            for (LLRenderTarget* target : { &mBlend, &mWater, &mScreen, &mGrid, &mWhite, &mGBuffer })
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
            // The blended surfaces' mask, as the pipeline takes it in, and the white it copies where they were drawn.
            REQUIRE(mBlend.ensure({ .width = W, .height = H, .color = { GL_R8 } }, mGBuffer) ==
                    LLRenderTarget::EnsureResult::Built);
            REQUIRE(mWhite.ensure({ .width = 1, .height = 1, .color = { GL_RGBA8 } }) == LLRenderTarget::EnsureResult::Built);
            ll_test::PostChainGL::write(mWhite, { 1.f, 1.f, 1.f, 1.f });

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

        // The G-buffer's albedo, its first attachment: the linear albedo `top` over the upper half and `bottom` over the
        // lower. It is sRGB8, which the meter reads decoded, so what goes in is the encoded value.
        void setAlbedo(F32 top, F32 bottom)
        {
            std::vector<F32> albedo(size_t(W) * H * 4, 1.f);
            for (U32 y = 0; y < H; ++y)
            {
                for (U32 x = 0; x < W; ++x)
                {
                    F32* texel = &albedo[(size_t(y) * W + x) * 4];
                    texel[0] = texel[1] = texel[2] = srgbEncode(y < H / 2 ? bottom : top);
                }
            }
            ll_test::PostChainGL::write(mGBuffer, albedo);
        }

        // What LLDrawPoolAlpha::forwardRender leaves in the stencil where a blended surface's fragments pass: the blended
        // bit over the rectangle, with no depth test and no colour written, so that nothing else changes.
        void markBlended(GLint x, GLint y, GLsizei width, GLsizei height)
        {
            mScreen.bindTarget();
            {
                LLGLDepthTest no_depth(GL_FALSE, GL_FALSE);
                LLGLDisable   no_blend(GL_BLEND);
                LLGLSScissor  region(x, y, width, height);
                gGL.setColorMask(false, false);
                ALBlendStencil::Mark blended;
                mCopy.bind();
                mCopy.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mWhite, ALSamplers::PointClamp);
                mPost.drawTriangle();
                mCopy.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                mCopy.unbind();
                gGL.setColorMask(true, true);
            }
            mScreen.flush();
        }

        // LLPipeline::generateLuminance's mask of the blended surfaces: cleared, then white copied into it where the
        // stencil holds the blended bit. (The pipeline's white is the viewer's white texture, here a target of one texel.)
        void drawBlendMap()
        {
            mBlend.bindTarget();
            gGL.setClearColor(LLColor4::transparent);
            mBlend.clear(GL_COLOR_BUFFER_BIT);
            {
                LLGLDepthTest             no_depth(GL_FALSE, GL_FALSE);
                LLGLDisable               no_blend(GL_BLEND);
                ALBlendStencil::OnBlended on_blended;
                mCopy.bind();
                mCopy.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mWhite, ALSamplers::PointClamp);
                mPost.drawTriangle();
                mCopy.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                mCopy.unbind();
            }
            mBlend.flush();
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
        // nothing otherwise, each patch's texel in rows from the bottom. Above strength 0 the albedo and the blended mask
        // are bound, as generateLuminance binds them: it meters reflectively, at 0, where it could not make the mask.
        std::vector<Patch> meter(bool with_water, F32 strength = 0.f)
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
                const S32 albedo = strength > 0.f ? mMeter.bindTexture(LLShaderMgr::METER_ALBEDO_MAP, &mGBuffer,
                                                                       ALSamplers::PointClamp | ALSampler::SRGBDecode, 0)
                                                  : -1;
                const S32 blend  = strength > 0.f ? mMeter.enableTexture(LLShaderMgr::METER_BLEND_MAP) : -1;
                if (blend > -1)
                {
                    mBlend.bindTexture(0, blend, ALSamplers::PointClamp);
                }
                mMeter.uniform4f(LLShaderMgr::METER_ALBEDO_PARAMS, strength, ALBEDO_FLOOR, TRUSTED_FROM, TRUSTED_TO);
                const F32 patch_w = (F32)W / (F32)GRID_W;
                const F32 patch_h = (F32)H / (F32)GRID_H;
                mMeter.uniform4f(LLShaderMgr::METER_PARAMS, patch_w, patch_h, (F32)llmax(1, llceil(patch_w * 0.5f)),
                                 (F32)llmax(1, llceil(patch_h * 0.5f)));
                mPost.drawTriangle();
                for (S32 channel : { scene, depth_channel, water, albedo, blend })
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
                patches[i] = { texels[i * 4], texels[i * 4 + 1], texels[i * 4 + 2], texels[i * 4 + 3] };
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
        LLRenderTarget         mBlend;
        LLRenderTarget         mWhite;
        LLRenderTarget         mGrid;
    };

    // Within the grid's half floats of what a patch should hold.
    bool closeTo(F32 value, F32 expected)
    {
        return std::fabs(value - expected) <= 1e-3f * std::fmax(1.f, std::fabs(expected));
    }

    // The same through the albedo, which the GPU reads decoded from sRGB8. How closely the decode follows the curve is the
    // driver's: one run's came out a fifth of a percent off, in either direction. A wrong floor, ramp, exponent or mask
    // moves a patch by far more than half a percent.
    bool closeToDecoded(F32 value, F32 expected)
    {
        return std::fabs(value - expected) <= 5e-3f * std::fmax(1.f, std::fabs(expected));
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

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: incident metering divides the world's luminance by its albedo, as far as the strength says",
                 "[luminance_gl]")
{
    const F32 strength = GENERATE(0.f, 0.5f, 1.f);
    CAPTURE(strength);
    setUp(GL_RGB10_A2);
    // The world over the upper half reflects a good deal and the world under the water less, both above the floor.
    constexpr F32 TOP    = 0.6f;
    constexpr F32 BOTTOM = 0.25f;
    setAlbedo(TOP, BOTTOM);
    drawWaterMap();
    drawBlendMap();
    const std::vector<Patch> grid = meter(true, strength);

    for (U32 gy = 0; gy < GRID_H; ++gy)
    {
        for (U32 gx = 0; gx < GRID_W; ++gx)
        {
            CAPTURE(gx, gy);
            const Patch& patch = grid[size_t(gy) * GRID_W + gx];
            CAPTURE(patch.mGround, patch.mSky, patch.mShare, patch.mEarned);
            const bool water = gy == 0;
            if (gx >= GRID_W / 2)
            {
                // The sky has no albedo to divide by: the sky and the water over it are metered as they were.
                CHECK(closeTo(patch.mSky, water ? WATER_LUM : SKY_LUM));
                CHECK(closeTo(patch.mShare, 1.f));
                CHECK(closeTo(patch.mEarned, 1.f));
            }
            else
            {
                const Patch want = incident(water ? WATER_LUM : WORLD_LUM, storedAlbedo(water ? BOTTOM : TOP), strength);
                CHECK(closeToDecoded(patch.mGround, want.mGround));
                CHECK(closeToDecoded(patch.mEarned, want.mEarned));
                CHECK(closeTo(patch.mShare, 0.f));
            }
        }
    }
}

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: a surface that reflects little earns little of the exposure", "[luminance_gl]")
{
    // Well above the floor and the ramp, at the ramp's top, inside the ramp, below the floor, and next to black.
    const F32 albedo = GENERATE(0.9f, 0.3f, 0.12f, 0.05f, 0.01f);
    CAPTURE(albedo);
    setUp(GL_RGB10_A2);
    setAlbedo(albedo, albedo);
    drawWaterMap();
    drawBlendMap();
    const std::vector<Patch> grid = meter(true, 1.f);

    // The world's upper left patch.
    const Patch& patch = grid[size_t(1) * GRID_W + 0];
    const Patch  want  = incident(WORLD_LUM, storedAlbedo(albedo), 1.f);
    CAPTURE(patch.mGround, patch.mEarned, want.mGround, want.mEarned);
    CHECK(closeToDecoded(patch.mGround, want.mGround));
    CHECK(closeToDecoded(patch.mEarned, want.mEarned));
}

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: blended pixels read as the key whatever lies behind them", "[luminance_gl]")
{
    setUp(GL_RGB10_A2);
    // Dark enough behind that, unmarked, a patch earns nothing of the exposure.
    constexpr F32 BEHIND = 0.02f;
    setAlbedo(BEHIND, BEHIND);
    // A blended surface over the whole of the world's upper patch (1, 1), and over the left half of its upper patch (0, 1),
    // whose taps then divide evenly: four of eight across.
    markBlended(W / 4, H / 2, W / 4, H / 2);
    markBlended(0, H / 2, W / 8, H / 2);
    drawWaterMap();
    drawBlendMap();
    const std::vector<Patch> grid = meter(true, 1.f);

    const F32 behind = storedAlbedo(BEHIND);
    for (U32 gy = 0; gy < GRID_H; ++gy)
    {
        for (U32 gx = 0; gx < GRID_W / 2; ++gx)
        {
            CAPTURE(gx, gy);
            const Patch& patch = grid[size_t(gy) * GRID_W + gx];
            CAPTURE(patch.mGround, patch.mEarned);

            F32 albedo = behind;
            if (gy == 1)
            {
                // The whole patch (1, 1) reads as the key, and half of (0, 1) does.
                albedo = gx == 1 ? NOMINAL_ALBEDO : 0.5f * NOMINAL_ALBEDO + 0.5f * behind;
            }
            const Patch want = incident(gy == 0 ? WATER_LUM : WORLD_LUM, albedo, 1.f);
            CAPTURE(want.mGround, want.mEarned);
            CHECK(closeToDecoded(patch.mGround, want.mGround));
            CHECK(closeToDecoded(patch.mEarned, want.mEarned));
        }
    }

    // The whole patch is the reflective reading: the luminance over the key, and all of its exposure earned.
    const Patch& whole = grid[size_t(1) * GRID_W + 1];
    CHECK(closeTo(whole.mGround, WORLD_LUM / NOMINAL_ALBEDO));
    CHECK(closeTo(whole.mEarned, 1.f));
}

TEST_CASE_METHOD(LuminanceMeter, "luminance_gl: the blended mask is where blended surfaces were drawn and nothing else", "[luminance_gl]")
{
    setUp(GL_RGB10_A2);
    drawWaterMap();
    const std::vector<F32> water_before = ll_test::PostChainGL::read(mWater);

    // Straddling the water's upper edge, so that the mask is read across texels holding both bits and one.
    constexpr U32 X0 = W / 4;
    constexpr U32 X1 = W / 2;
    constexpr U32 Y0 = H / 4;
    constexpr U32 Y1 = H * 3 / 4;
    markBlended(X0, Y0, X1 - X0, Y1 - Y0);
    drawBlendMap();

    const std::vector<F32> mask = ll_test::PostChainGL::read(mBlend);
    U32 wrong = 0;
    for (U32 y = 0; y < H; ++y)
    {
        for (U32 x = 0; x < W; ++x)
        {
            const bool marked = x >= X0 && x < X1 && y >= Y0 && y < Y1;
            if (!closeTo(mask[(size_t(y) * W + x) * 4], marked ? 1.f : 0.f))
            {
                ++wrong;
            }
        }
    }
    CHECK(wrong == 0);

    // The mark touches only its own bit: the water's map, drawn again, is what it was.
    drawWaterMap();
    CHECK(ll_test::PostChainGL::read(mWater) == water_before);
}
