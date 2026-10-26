/**
 * @file allightmapblur_gl_test.cpp
 * @brief The light map's ambient occlusion blur on the hidden window, both ways the viewer runs it: the two fragment
 *        passes through a scratch (blurLightF.glsl), and the one dispatch (blurLightC.glsl), over a G-buffer of planes,
 *        a step between them and sky, against a light map of noisy occlusion.
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

#include "allightmapblur_gl_scene.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace
{
    using allightmapblur_gl::Scene;

    // Neither a whole number of the dispatch's tiles nor of its apron, and more than two tiles tall, so every edge a
    // tile has is somewhere a tile ends short of it, and some tile has tiles above and below.
    constexpr S32 W = 157;
    constexpr S32 H = 601;

    // The kernel of RenderShadowBlurSize `blur_size`, at a distance factor that shortens a lean one.
    ALLightMapBlur::Kernel kernelOf(F32 blur_size)
    {
        return ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), blur_size, blur_size < 1.f ? 0.08f : 0.f);
    }
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: one dispatch blurs as the fragment passes do, through no scratch",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    const bool reverse = GENERATE(false, true);
    // The default blur, the widest the dispatch takes, and a lean kernel.
    const F32 blur_size = GENERATE(1.4f, 1.5f, 0.6f);
    CAPTURE(reverse, blur_size);
    if (reverse && !gGLManager.mHasClipControl)
    {
        SKIP("no clip control");
    }
    setUp(reverse, W, H);

    const ALLightMapBlur::Kernel kernel = kernelOf(blur_size);
    REQUIRE(ALLightMapBlur::apron(kernel) > 0);

    blurFragment(kernel);
    poisonBlurred();
    REQUIRE(blurCompute(kernel));
    CHECK((U32)glGetError() == (U32)GL_NO_ERROR);

    const std::vector<F32> fragment = read(mLight);
    const std::vector<F32> compute = read(mBlurred);

    // The occlusion within one 8-bit step of the fragment passes': theirs passes through the scratch's 8 bits between
    // the axes, half a step from the dispatch's single precision at most, and both round it to 8 bits. The shadow
    // passes through untouched. Somewhere the blur moved the occlusion, or there is nothing to compare.
    F32 worst = 0.f;
    S32 blurred = 0;
    S32 sky = 0;
    for (S32 y = 0; y < H; ++y)
    {
        for (S32 x = 0; x < W; ++x)
        {
            const size_t i = (size_t(y) * W + x) * 4;
            CAPTURE(x, y);
            REQUIRE(compute[i] == fragment[i]);
            const F32 diff = std::fabs(compute[i + 1] - fragment[i + 1]);
            worst = llmax(worst, diff);
            if (diff > 1.001f / 255.f)
            {
                FAIL_CHECK("occlusion " << compute[i + 1] << " against " << fragment[i + 1]);
            }
            blurred += std::fabs(fragment[i + 1] - mSource[i + 1]) > 0.01f ? 1 : 0;
            if (isSky(x, y))
            {
                ++sky;
                UNSCOPED_INFO("the sky keeps the light map's clear");
                CHECK(compute[i] == 1.f);
                CHECK(compute[i + 1] == 1.f);
            }
        }
    }
    UNSCOPED_INFO("worst occlusion difference " << worst);
    CHECK(blurred > W * H / 2);
    CHECK(sky > 0);
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: one dispatch blurs texel for texel as the fragment passes do through a single-precision scratch",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    const bool reverse = GENERATE(false, true);
    const F32 blur_size = GENERATE(1.4f, 1.5f, 0.6f);
    CAPTURE(reverse, blur_size);
    if (reverse && !gGLManager.mHasClipControl)
    {
        SKIP("no clip control");
    }
    setUp(reverse, W, H);

    const ALLightMapBlur::Kernel kernel = kernelOf(blur_size);
    blurFragment(kernel, true);
    poisonBlurred();
    REQUIRE(blurCompute(kernel));

    // The dispatch's store holds the 8 bits nearest the fragment passes' single precision, or at a tie, which their
    // value lies within a rounding of at a few texels, either of the two. Or the step below, where their value lies
    // less than a sixteenth of a step past the half: NVIDIA's store to an 8-bit image rounds there, not at the half,
    // always down, at about one texel in forty.
    const std::vector<F32> fragment = read(mFloatLight);
    const std::vector<F32> compute = read(mBlurred);
    const auto nearest = [](F32 stored, F32 value)
    {
        const F32 above = (value - stored) * 255.f;
        return above >= -(0.5f + 1e-4f) && above <= 0.5f + 1.f / 16.f + 1e-4f;
    };
    S32 differ = 0;
    for (S32 y = 0; y < H; ++y)
    {
        for (S32 x = 0; x < W; ++x)
        {
            const size_t i = (size_t(y) * W + x) * 4;
            if (!nearest(compute[i], fragment[i]) || !nearest(compute[i + 1], fragment[i + 1]))
            {
                if (++differ <= 8)
                {
                    UNSCOPED_INFO("at " << x << ", " << y << ": " << compute[i] << ", " << compute[i + 1] << " against "
                                        << fragment[i] << ", " << fragment[i + 1]);
                }
            }
        }
    }
    CHECK(differ == 0);
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: the dispatch blurs as the taps say, against the blur worked on the CPU",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    const bool reverse = GENERATE(false, true);
    const F32 blur_size = GENERATE(1.4f, 1.5f, 0.6f);
    CAPTURE(reverse, blur_size);
    if (reverse && !gGLManager.mHasClipControl)
    {
        SKIP("no clip control");
    }
    setUp(reverse, W, H);

    const ALLightMapBlur::Kernel kernel = kernelOf(blur_size);
    poisonBlurred();
    REQUIRE(blurCompute(kernel));
    const std::vector<F32> compute = read(mBlurred);
    const std::vector<F32> expected = reference(kernel);

    // The GPU's rounding moves a tap across its plane's tolerance only where it lies on the edge of it, which a few
    // pixels' taps do; everywhere else the two agree to the 8 bits the target rounds to.
    S32 off = 0;
    for (S32 y = 0; y < H; ++y)
    {
        for (S32 x = 0; x < W; ++x)
        {
            const size_t i = size_t(y) * W + x;
            const F32 rounded = std::round(expected[i] * 255.f) / 255.f;
            if (std::fabs(compute[i * 4 + 1] - rounded) > 1.001f / 255.f && ++off <= 8)
            {
                UNSCOPED_INFO("at " << x << ", " << y << ": " << compute[i * 4 + 1] << " against " << expected[i]);
            }
        }
    }
    UNSCOPED_INFO(off << " of " << W * H << " pixels off");
    CHECK(off <= W * H / 10000);
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: the blur keeps to each pixel's plane, so no occlusion crosses a step in depth",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    const bool reverse = GENERATE(false, true);
    CAPTURE(reverse);
    if (reverse && !gGLManager.mHasClipControl)
    {
        SKIP("no clip control");
    }
    setUp(reverse, W, H);

    // The box occluded, the floor and the wall behind it open: whatever the blur takes across the box's edge shows.
    layOcclusion(0.f, 1.f);
    const ALLightMapBlur::Kernel kernel = kernelOf(1.4f);
    blurFragment(kernel);
    poisonBlurred();
    REQUIRE(blurCompute(kernel));

    const std::vector<F32> fragment = read(mLight);
    const std::vector<F32> compute = read(mBlurred);
    S32 crossed = 0;
    S32 box = 0;
    for (S32 y = 0; y < H; ++y)
    {
        for (S32 x = 0; x < W; ++x)
        {
            const size_t i = (size_t(y) * W + x) * 4;
            const F32 open = isBox(x, y) ? 0.f : 1.f;
            box += isBox(x, y) ? 1 : 0;
            for (F32 blurred : { compute[i + 1], fragment[i + 1] })
            {
                if (blurred != open && ++crossed <= 8)
                {
                    UNSCOPED_INFO("at " << x << ", " << y << ": " << blurred << ", not " << open);
                }
            }
        }
    }
    CHECK(box > 0);
    CHECK(crossed == 0);
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: a kernel reaching past the apron, a map smaller than a tile and its aprons, or a target of another format, is left to the fragment passes",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    setUp(false, W, H);

    // RenderShadowBlurSize 1.4's taps reach 13.125 rows down and up: 15 rows of apron, and 1.5's 14.06, 16. Past
    // MAX_APRON less one of reach, the Lightbox's widest among them, nothing is dispatched.
    CHECK(ALLightMapBlur::apron(kernelOf(1.4f)) == 15);
    CHECK(ALLightMapBlur::apron(kernelOf(1.5f)) == (S32)ALLightMapBlur::MAX_APRON);
    for (F32 blur_size : { 1.6f, 3.f })
    {
        CAPTURE(blur_size);
        CHECK(ALLightMapBlur::apron(kernelOf(blur_size)) == -1);
        CHECK_FALSE(blurCompute(kernelOf(blur_size)));
    }

    // The dispatch stores through an rg8 image, which no other format may be bound to.
    const ALLightMapBlur::Kernel kernel = kernelOf(1.4f);
    LLRenderTarget half;
    REQUIRE(half.ensure({ .width = (U32)W, .height = (U32)H, .color = { GL_RG16F } }) == LLRenderTarget::EnsureResult::Built);
    bindDeferred(mCompute, mComputeLight, kernel);
    CHECK_FALSE(ALLightMapBlur::dispatch(mCompute, kernel, half));
    half.release();

    // A tile reads its aprons' texels within one mirror of the map, which a map narrower or shorter than a tile and
    // its aprons does not hold.
    const U32 least = ALLightMapBlur::TILE_WIDTH + 2 * 15;
    for (const auto& [width, height] : { std::pair<U32, U32>{ least - 1, (U32)H }, std::pair<U32, U32>{ (U32)W, least - 1 } })
    {
        CAPTURE(width, height);
        LLRenderTarget small;
        REQUIRE(small.ensure({ .width = width, .height = height, .color = { GL_RG8 } }) == LLRenderTarget::EnsureResult::Built);
        CHECK_FALSE(ALLightMapBlur::dispatch(mCompute, kernel, small));
        small.release();
    }
    // The dispatch blurs over the screen's size, as the viewer gives it the light map's.
    LLRenderTarget fits;
    REQUIRE(fits.ensure({ .width = least, .height = least, .color = { GL_RG8 } }) == LLRenderTarget::EnsureResult::Built);
    mCompute.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)least, (F32)least);
    CHECK(ALLightMapBlur::dispatch(mCompute, kernel, fits));
    fits.release();
    mCompute.unbind();
}
