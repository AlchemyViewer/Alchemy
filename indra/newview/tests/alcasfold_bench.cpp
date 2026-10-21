/**
 * @file alcasfold_bench.cpp
 * @brief What sharpening costs the GPU at 1440p: CAS's own pass then the final blit, against the blit sharpening as it
 *        samples.
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

// Each row is the GPU time, by GL_TIME_ELAPSED, of the draws one frame's sharpening and blit issue, the median of 101
// runs after 10 unmeasured: into an RGBA8 target the window's size, from the post chain's RGB10_A2 at the render
// size. The output is a table, read against the same rows from another build on the same quiet machine.

#include "linden_common.h"

#include "alcasfold_gl_fixture.h"

#include "altestbenchmark.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

namespace
{
    struct Row
    {
        const char* name;
        U32 src_width;
        U32 src_height;
        ll_test::CASFold::Lens lens;
    };
}

TEST_CASE_METHOD(ll_test::CASFold, "alcasfold_bench: sharpening at 1440p, as a pass of its own and as the blit samples",
                 "[!benchmark][alcasfold_bench]")
{
    ll_test::skipUnlessOptimised();

    constexpr U32 W = 2560;
    constexpr U32 H = 1440;
    constexpr F32 SHARPNESS = 0.4f;
    const Row rows[] = {
        { "one to one", W, H, {} },
        { "one to one, barrel distortion", W, H, { -0.2f, 0.f, 0.85f } },
        { "render size halved (divisor 2)", W / 2, H / 2, {} },
    };

    std::printf("\n%-34s %12s %12s %12s\n", "1440p", "CAS + blit", "folded", "blit alone");
    for (const Row& row : rows)
    {
        setUp(row.src_width, row.src_height, W, H, GL_RGBA8);
        const U64 two_pass = ll_test::gpuMedianNs([&] {
            cas(SHARPNESS);
            blit(mSharpened, false, 0.f, row.lens);
        });
        const U64 folded = ll_test::gpuMedianNs([&] { blit(mSource, true, SHARPNESS, row.lens); });
        const U64 plain  = ll_test::gpuMedianNs([&] { blit(mSource, false, 0.f, row.lens); });
        std::printf("%-34s %9.3f ms %9.3f ms %9.3f ms\n", row.name, two_pass / 1e6, folded / 1e6, plain / 1e6);
    }
    SUCCEED();
}
