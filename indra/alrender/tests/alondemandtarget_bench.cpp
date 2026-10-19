/**
 * @file alondemandtarget_bench.cpp
 * @brief What a preview or a bake pays when its target was trimmed from the pool: the target built again, its
 *        framebuffer made and its first clear, against the same run finding the target in the pool.
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

// One sample is one run of updateAllInstances' over the range's target with nothing drawn into it: acquired, bound,
// cleared, flushed and given back. A cold sample follows the pool freeing every target, as a trim does; a warm one
// finds the target the last left. Three times a sample: the CPU's time in the calls, the GPU's from a GL_TIME_ELAPSED
// query around them, and the wall time to the GPU's finishing them (glFinish), which is what a frame would wait.
// Each row is the median of 31 samples after a warm-up, with the samples' spread; the GPU idle otherwise.

#include "linden_common.h"

#include "../alondemandtarget.h"
#include "../alrenderresourceplan.h"
#include "../altransienttargetpool.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"
#include "llrendertarget.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"
#include "altestbenchmark.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
    using Plan  = ALRenderResourcePlan;
    using Id    = Plan::Id;
    using Clock = std::chrono::steady_clock;

    constexpr S32 SAMPLES = 31;
    constexpr S32 WARMUP  = 5;

    struct Times
    {
        std::vector<F64> mCpu;
        std::vector<F64> mGpu;
        std::vector<F64> mWall;
    };

    F64 ms_since(Clock::time_point start)
    {
        return std::chrono::duration<F64, std::milli>(Clock::now() - start).count();
    }

    ALRenderResourceInputs defaults()
    {
        ALRenderResourceInputs in;
        in.mWidth  = 2560;
        in.mHeight = 1440;
        return in;
    }

    struct alondemandtarget_bench_data
    {
        alondemandtarget_bench_data() : mPlan(Plan::make(defaults()))
        {
            glGenQueries(1, &mQuery);
        }

        ~alondemandtarget_bench_data()
        {
            mPool.clear();
            glDeleteQueries(1, &mQuery);
        }

        // One run over each of `ids`' targets in turn, as updateAllInstances runs its two ranges.
        void run(std::initializer_list<Id> ids)
        {
            for (Id id : ids)
            {
                ALOnDemandTarget target(mPool, mPlan.desc(id), Plan::name(id));
                REQUIRE(target.bind());
            }
        }

        Times sample(std::initializer_list<Id> ids, bool cold)
        {
            Times times;
            for (S32 sample = -WARMUP; sample < SAMPLES; ++sample)
            {
                if (cold)
                {
                    mPool.clear();
                }
                glFinish();

                const Clock::time_point start = Clock::now();
                glBeginQuery(GL_TIME_ELAPSED, mQuery);
                run(ids);
                glEndQuery(GL_TIME_ELAPSED);
                const F64 cpu = ms_since(start);
                glFinish();
                const F64 wall = ms_since(start);

                GLuint64 ns = 0;
                glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT, &ns);
                if (sample >= 0)
                {
                    times.mCpu.push_back(cpu);
                    times.mGpu.push_back((F64)ns / 1.0e6);
                    times.mWall.push_back(wall);
                }
            }
            CHECK(mPool.stats().mFailures == 0u);
            return times;
        }

        // The median and the samples' spread, in microseconds.
        static std::string summary(std::vector<F64> samples)
        {
            std::sort(samples.begin(), samples.end());
            const auto us = [](F64 ms) { return std::to_string(ll_round((F32)(ms * 1000.0))); };
            return us(samples[samples.size() / 2]) + " us (" + us(samples.front()) + "-" + us(samples.back()) + ")";
        }

        void report(const std::string& row, std::initializer_list<Id> ids)
        {
            const Times cold = sample(ids, true);
            const Times warm = sample(ids, false);
            WARN(row << "\n  cold: cpu " << summary(cold.mCpu) << ", gpu " << summary(cold.mGpu) << ", to finish "
                     << summary(cold.mWall) << "\n  warm: cpu " << summary(warm.mCpu) << ", gpu " << summary(warm.mGpu)
                     << ", to finish " << summary(warm.mWall));
        }

        ll_test::SharedGLScope mGL;
        Plan                   mPlan;
        ALTransientTargetPool  mPool;
        GLuint                 mQuery = 0;
    };

    TEST_CASE_METHOD(alondemandtarget_bench_data, "alondemandtarget_bench: a trimmed preview or bake target taken again",
                     "[alondemandtarget_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        report("the previews' target, 512x512 SRGB8_ALPHA8 with its own depth", { Id::Preview });
        report("the bakes' target, 2048x2048 RGBA8", { Id::Bake });
        report("both, as a slider drag in the appearance editor takes them", { Id::Preview, Id::Bake });
    }
}
