/**
 * @file reflectionprobe_gl_test.cpp
 * @brief class3/deferred/reflectionProbeF.glsl's probe sampling, one pass over the probes that taps each as it finds
 *        it, against the probes sampled as they were before: a list of the probes that hold the point, then a pass
 *        over it for each tap. Over a scene of automatic and manual spheres and boxes, neighbour lists and the default
 *        probe (reflectionprobe_gl_scene.h), every entry point, to a rounding at unit magnitude.
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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

namespace
{
    using reflectionprobe_gl::ProbeScene;

    constexpr S32 SIZE = 256;
}

TEST_CASE_METHOD(ProbeScene, "reflectionprobe_gl: one pass over the probes taps them as their list did",
                 "[reflectionprobe_gl]")
{
    // Every probe level's search, the viewer's list length and one the scenes fill, and the classic sky's taps.
    const S32 level = GENERATE(3, 0);
    const S32 count = GENERATE(32, 3);
    const bool classic = GENERATE(false, true);
    CAPTURE(level, count, classic);
    setUp(SIZE, SIZE);
    REQUIRE(build(mFused, "test/probeFusedF.glsl", level, count, classic));
    REQUIRE(build(mListed, "test/probeListedF.glsl", level, count, classic));
    layProbes(mFused);

    const char* apis[] = { "sampleReflectionProbes", "sampleReflectionProbesLegacy", "sampleReflectionProbesWater",
                           "sampleReflectionProbesEither" };
    for (S32 api = 0; api < 4; ++api)
    {
        CAPTURE(apis[api]);
        draw(mFused, api);
        const std::vector<F32> fused = read();
        draw(mListed, api);
        const std::vector<F32> listed = read();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);

        // Each tap as the list's to a rounding at unit magnitude. Where a manual sphere's taps are mixed with the
        // automatic probes', the compiler rounds the pass, which weighs each probe once for all its taps, apart from
        // the list's passes, which weigh it once for each, by up to that: 1.2e-7 on the NVIDIA driver this was written
        // on. Wherever a box shuts the automatic probes out, or one kind of probe alone holds the point, the two agree
        // to the bit. And taken where the probes vary, not every pixel the same.
        S32 differ = 0;
        S32 varied = 0;
        for (size_t i = 0; i < fused.size(); i += 4)
        {
            bool close = true;
            for (size_t c = 0; c < 3; ++c)
            {
                close = close && std::fabs(fused[i + c] - listed[i + c]) <= 4e-7f * llmax(1.f, std::fabs(listed[i + c]));
            }
            if (!close && ++differ <= 8)
            {
                const size_t pixel = (i / 4) % (size_t(SIZE) * SIZE);
                UNSCOPED_INFO("tap " << i / (size_t(SIZE) * SIZE * 4) << " at " << pixel % SIZE << ", " << pixel / SIZE
                                     << ": " << fused[i] << ", " << fused[i + 1] << ", " << fused[i + 2] << " against "
                                     << listed[i] << ", " << listed[i + 1] << ", " << listed[i + 2]);
            }
            varied += fused[i] != fused[0] ? 1 : 0;
        }
        CHECK(differ == 0);
        CHECK(varied > SIZE * SIZE / 2);
    }
}
