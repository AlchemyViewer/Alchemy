/**
 * @file alcompute_gl_bench.cpp
 * @brief The GPU time of each pass the session computes against the fragment passes it replaces, at 2560x1440, by GL
 *        timer queries on the hidden window.
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

#include "altestbenchmark.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr S32 W = 2560;
    constexpr S32 H = 1440;

    struct GPUTime
    {
        F64 mMedian = 0.0; ///< milliseconds
        F64 mLeast  = 0.0;
    };

    // The GPU time of `run`, over `samples` timed runs after a warm-up, in milliseconds: the median and the least.
    template <typename Run> GPUTime gpuTime(Run&& run, U32 samples = 64)
    {
        GLuint query = 0;
        glGenQueries(1, &query);
        for (U32 i = 0; i < 8; ++i)
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
        return GPUTime{ times[times.size() / 2], times.front() };
    }
}

TEST_CASE_METHOD(allightmapblur_gl::Scene, "alcompute_gl_bench: SSAO's blur, two fragment passes against one dispatch",
                 "[alcompute_gl_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    if (!gGLManager.mHasCompute)
    {
        SKIP("no compute");
    }
    setUp(gGLManager.mHasClipControl, W, H);

    // RenderShadowGaussian and RenderShadowBlurSize at their defaults.
    const ALLightMapBlur::Kernel kernel = ALLightMapBlur::kernel(LLVector3(3.f, 2.f, 0.f), 1.4f, 0.f);
    const GPUTime fragment = gpuTime([&] { blurFragment(kernel); });
    const GPUTime compute  = gpuTime([&] { REQUIRE(blurCompute(kernel)); });
    WARN("SSAO blur at " << W << "x" << H << ", RenderShadowBlurSize 1.4: fragment passes " << fragment.mMedian
                         << " ms (least " << fragment.mLeast << "), one dispatch " << compute.mMedian << " ms (least "
                         << compute.mLeast << ")");
    CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
}
