/**
 * @file alcasfold_gl_test.cpp
 * @brief The final blit sharpening as it samples, held against contrast adaptive sharpening's own pass followed by the
 *        blit: one to one, through the lens distortion's warp, and scaled either way.
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

#include "alcasfold_gl_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    // How the frame reaches the window: its size against the source's, and the lens.
    struct Case
    {
        const char* name;
        U32 src_width;
        U32 src_height;
        U32 out_width;
        U32 out_height;
        ll_test::CASFold::Lens lens;
    };

    const Case CASES[] = {
        { "one to one", 48, 32, 48, 32, {} },
        { "barrel distortion", 48, 32, 48, 32, { -0.2f, 0.f, 1.f } },
        { "pincushion distortion, black corners", 48, 32, 48, 32, { 0.3f, 0.05f, 1.f } },
        { "fit-scaled moustache", 48, 32, 48, 32, { -0.3f, 0.2f, 0.9f } },
        { "drawn at twice the size (a resolution divisor)", 24, 16, 48, 32, {} },
        { "drawn at half the size (a resolution multiplier)", 96, 64, 48, 32, {} },
        { "drawn at an uneven scale, distorted", 40, 27, 48, 32, { -0.15f, 0.f, 1.f } },
    };
}

TEST_CASE_METHOD(ll_test::CASFold, "alcasfold_gl: the blit sharpening as it samples draws what CAS then the blit drew",
                 "[alcasfold_gl]")
{
    const Case& c = CASES[GENERATE(range(0, (int)(sizeof(CASES) / sizeof(CASES[0]))))];
    CAPTURE(c.name);
    setUp(c.src_width, c.src_height, c.out_width, c.out_height);

    const F32 sharpness = GENERATE(0.4f, 1.f);
    CAPTURE(sharpness);
    const std::vector<F32> two_pass = drawTwoPass(sharpness, c.lens);
    const std::vector<F32> folded   = drawFolded(sharpness, c.lens);
    const std::vector<F32> plain    = drawPlain(c.lens);

    // Two passes carry CAS's output through the post chain's RGB10_A2 between them, a 10-bit step at most as GL
    // rounds into it; the filter's weights are another 1/256 step at most, over the largest difference two
    // neighbouring texels have.
    const bool one_to_one = c.src_width == c.out_width && c.src_height == c.out_height && c.lens.k1 == 0.f && c.lens.k2 == 0.f;
    const F32 tolerance = one_to_one ? 1.f / 1023.f + 1e-5f : 1.f / 1023.f + 1.f / 256.f + 1e-5f;

    F32 worst = 0.f;
    F32 sharpened = 0.f;
    for (size_t i = 0; i < two_pass.size(); ++i)
    {
        worst = std::max(worst, std::fabs(two_pass[i] - folded[i]));
        // The alpha is CAS's in both, which writes none; the colour is sharpened in both.
        if (i % 4 != 3)
        {
            sharpened = std::max(sharpened, std::fabs(two_pass[i] - plain[i]));
        }
    }
    CAPTURE(worst, tolerance);
    CHECK(worst <= tolerance);
    INFO("the case sharpens at all, so a blit that does not would differ");
    CHECK(sharpened > 4.f * tolerance);
}

TEST_CASE_METHOD(ll_test::CASFold, "alcasfold_gl: on a texel's centre the blit reads that texel's sharpening alone",
                 "[alcasfold_gl]")
{
    // One to one, every output pixel is CAS's own texel: the filter's weights select it alone, the folded blit's as
    // the hardware's do, so the two agree to the intermediate's rounding, and the two passes return what CAS wrote to
    // the filter's own precision, a tenth of a 10-bit step.
    setUp(64, 40, 64, 40);
    const std::vector<F32> two_pass = drawTwoPass(0.6f, {});
    const std::vector<F32> cas      = drawCAS(0.6f);
    const std::vector<F32> folded   = drawFolded(0.6f, {});
    for (size_t i = 0; i < folded.size(); ++i)
    {
        if (i % 4 == 3)
        {
            continue;
        }
        CAPTURE(i);
        REQUIRE(std::fabs(two_pass[i] - cas[i]) <= 1e-4f);
        REQUIRE(std::fabs(folded[i] - cas[i]) <= 1.f / 1023.f + 1e-5f);
    }
}
