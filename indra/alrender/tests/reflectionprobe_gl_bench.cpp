/**
 * @file reflectionprobe_gl_bench.cpp
 * @brief The GPU time of the reflection probes' sampling at 3440x1369, by GL timer queries on the hidden window: the
 *        one pass over the probes against the probes sampled as they were, a list and then a pass for each tap, over
 *        reflectionprobe_gl_scene.h's probes, at the probe level that searches them and at the level that takes the
 *        default probe alone, the most a cheaper search could save.
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

#include "reflectionprobe_gl_scene.h"

#include "altestbenchmark.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr S32 W = 3440;
    constexpr S32 H = 1369;

    // The median GPU time of `run` over `samples` timed runs after a warm-up, in milliseconds.
    template <typename Run> F64 gpuTime(Run&& run, U32 samples = 32)
    {
        GLuint query = 0;
        glGenQueries(1, &query);
        for (U32 i = 0; i < 4; ++i)
        {
            run();
        }
        std::vector<F64> times;
        for (U32 i = 0; i < samples; ++i)
        {
            glBeginQuery(GL_TIME_ELAPSED, query);
            run();
            glEndQuery(GL_TIME_ELAPSED);
            GLuint64 ns = 0;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
            times.push_back((F64)ns * 1e-6);
        }
        glDeleteQueries(1, &query);
        std::sort(times.begin(), times.end());
        return times[times.size() / 2];
    }
}

TEST_CASE_METHOD(reflectionprobe_gl::ProbeScene, "reflectionprobe_gl_bench: the probes' taps in one pass against their list",
                 "[reflectionprobe_gl_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    setUp(W, H);

    // The soften pass's taps, PBR or legacy a pixel (sampleReflectionProbesEither), the PBR and legacy entry points the
    // forward shaders take, and the water's.
    const char* apis[] = { "PBR", "legacy", "water", "soften" };
    // A surface as a view holds one, every pixel's probes its neighbours', and points scattered so a group of pixels
    // holds probes apart.
    for (bool surface : { true, false })
    {
        mSurface = surface;
        for (S32 level : { 3, 0 })
        {
            REQUIRE(build(mFused, "test/probeFusedF.glsl", level, 32, false));
            REQUIRE(build(mListed, "test/probeListedF.glsl", level, 32, false));
            layProbes(mFused);
            for (S32 api : { 3, 0, 1, 2 })
            {
                const F64 fused = gpuTime([&] { draw(mFused, api); });
                const F64 listed = gpuTime([&] { draw(mListed, api); });
                WARN((surface ? "a surface" : "scattered points")
                     << " at " << W << "x" << H << ", REFMAP_LEVEL " << level << ", " << apis[api] << ": one pass "
                     << fused << " ms, list then a pass for each tap " << listed << " ms");
            }
        }
    }
    CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
}
