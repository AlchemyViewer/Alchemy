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
#include <vector>

namespace
{
    using allightmapblur_gl::Scene;

    // Neither a whole number of the dispatch's tiles nor of its apron, so every edge a tile has is somewhere a tile
    // ends short of it.
    constexpr S32 W = 157;
    constexpr S32 H = 211;
}

TEST_CASE_METHOD(Scene, "allightmapblur_gl: one dispatch blurs as the fragment passes do, through no scratch",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    const bool reverse = GENERATE(false, true);
    // The default blur, the Lightbox's widest, and a lean kernel at a distance factor that shortens it.
    const F32 blur_size = GENERATE(1.4f, 3.f, 0.6f);
    CAPTURE(reverse, blur_size);
    if (reverse && !gGLManager.mHasClipControl)
    {
        SKIP("no clip control");
    }
    setUp(reverse, W, H);

    const F32 dist_factor = blur_size < 1.f ? 0.08f : 0.f;
    const ALLightMapBlur::Kernel kernel = ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), blur_size, dist_factor);
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

TEST_CASE_METHOD(Scene, "allightmapblur_gl: a kernel reaching past the apron, or a target of another format, is left to the fragment passes",
                 "[allightmapblur_gl]")
{
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    setUp(false, W, H);

    // RenderShadowBlurSize 1.4's taps reach 13.125 rows down and up: 15 rows of apron. Past MAX_APRON less one of
    // reach, nothing is dispatched.
    CHECK(ALLightMapBlur::apron(ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), 1.4f, 0.f)) == 15);
    const ALLightMapBlur::Kernel wide = ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), 3.4f, 0.f);
    CHECK(ALLightMapBlur::apron(wide) == -1);
    CHECK_FALSE(blurCompute(wide));

    // The dispatch stores through an rg8 image, which no other format may be bound to.
    LLRenderTarget half;
    REQUIRE(half.ensure({ .width = (U32)W, .height = (U32)H, .color = { GL_RG16F } }) == LLRenderTarget::EnsureResult::Built);
    const ALLightMapBlur::Kernel kernel = ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), 1.4f, 0.f);
    bindDeferred(mCompute, mComputeLight, kernel);
    CHECK_FALSE(ALLightMapBlur::dispatch(mCompute, kernel, half));
    mCompute.unbind();
    half.release();
}
