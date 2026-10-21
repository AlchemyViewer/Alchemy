/**
 * @file aldof_bench.cpp
 * @brief What depth of field costs the GPU at 1440p: its circle of confusion worked from the scene's depth in the
 *        gather and the combine, against a pass writing it into a full-resolution sharp copy first.
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

// Each row is the GPU time, by GL_TIME_ELAPSED, of the passes one frame's depth of field issues, the median of 101
// runs after 10 unmeasured, over a 2560x1440 scene whose depth steps every 16 pixels between half a metre and two
// hundred: the sharp copy in the screen's RGBA16F and the blur in bloom's R11F_G11F_B10F, as the pipeline has them.
// The output is a table, read against the same rows from another build on the same quiet machine.

#include "linden_common.h"

#include "aldof_gl_fixture.h"

#include "altestbenchmark.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

TEST_CASE_METHOD(ll_test::DoFPasses, "aldof_bench: depth of field at 1440p, from depth and through a sharp copy",
                 "[!benchmark][aldof_bench]")
{
    ll_test::skipUnlessOptimised();

    struct Row
    {
        const char* name;
        Lens lens;
    };
    Lens defaults;
    Lens wide = defaults;
    wide.focus   = 3.f;
    wide.fnumber = 1.4f;
    Lens wide_near = wide;
    wide_near.near_blur = true;
    Lens wide_near_shaped = wide_near;
    wide_near_shaped.blades = 6;
    const Row rows[] = {
        { "defaults (f/9, 16 m, no near blur)", defaults },
        { "f/1.4 at 3 m", wide },
        { "f/1.4 at 3 m, near blur", wide_near },
        { "f/1.4 at 3 m, near blur, shaped", wide_near_shaped },
    };

    std::printf("\n%-38s %14s %12s\n", "1440p", "sharp copy", "from depth");
    for (const Row& row : rows)
    {
        setUp(2560, 1440, row.lens, 16, GL_RGBA16F, GL_R11F_G11F_B10F);
        const U64 before = ll_test::gpuMedianNs([&] { drawFromSharpCopy(); });
        const U64 after  = ll_test::gpuMedianNs([&] { drawFromDepth(); });
        std::printf("%-38s %11.3f ms %9.3f ms\n", row.name, before / 1e6, after / 1e6);
    }
    SUCCEED();
}
