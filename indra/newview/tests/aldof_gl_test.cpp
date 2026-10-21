/**
 * @file aldof_gl_test.cpp
 * @brief Depth of field with its circle of confusion worked from the scene's depth where the gather and the combine
 *        read it, held against passes carrying it in a sharp copy's alpha: in all four gathers and both
 *        combines, with the scene's glow tag carried through.
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

#include "aldof_gl_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr U32 W = 160;
    constexpr U32 H = 96;
}

TEST_CASE_METHOD(ll_test::DoFPasses, "aldof_gl: the circle of confusion from depth draws what the sharp copy's drew",
                 "[aldof_gl]")
{
    const bool near_blur = GENERATE(false, true);
    const S32  blades    = GENERATE(0, 6);
    CAPTURE(near_blur, blades);

    // A wide aperture focused a few metres out, so every cell is in focus, in front of it or behind it, and many at
    // the largest circle. The cells are flat, so most taps lie on their centre's own depth.
    Lens lens;
    lens.focus     = 3.f;
    lens.fnumber   = 1.4f;
    lens.near_blur = near_blur;
    lens.blades    = blades;
    setUp(W, H, lens);

    drawFromSharpCopy();
    const std::vector<F32> before = ll_test::PostChainGL::read(mRefScene);
    drawFromDepth();
    const std::vector<F32> after = ll_test::PostChainGL::read(mOut);
    const std::vector<F32> scene = ll_test::PostChainGL::read(mScene);

    // The two read the same circle at every texel, so they take the same taps; they differ by the order of float
    // operations alone.
    F32 worst     = 0.f;
    U32 defocused = 0;
    for (size_t i = 0; i < after.size(); i += 4)
    {
        for (size_t c = 0; c < 3; ++c)
        {
            worst = std::max(worst, std::fabs(after[i + c] - before[i + c]) / std::max(1.f, std::fabs(before[i + c])));
        }
        defocused += after[i] != scene[i] ? 1 : 0;
        // The scene's alpha, the legacy glow's tag, as the combine found it.
        CAPTURE(i);
        REQUIRE(after[i + 3] == scene[i + 3]);
    }
    CHECK(worst <= 1e-4f);
    INFO("most of the frame is out of focus, so the blur is what is compared");
    CHECK(defocused > W * H / 2);
}

TEST_CASE_METHOD(ll_test::DoFPasses, "aldof_gl: without the near blur, what lies before the focal plane comes through untouched",
                 "[aldof_gl]")
{
    // Focused past everything in the scene, so every pixel lies in front of the focal plane.
    Lens lens;
    lens.focus     = 1000.f;
    lens.fnumber   = 1.4f;
    lens.near_blur = false;
    setUp(W, H, lens);

    drawFromDepth();
    CHECK(ll_test::PostChainGL::read(mOut) == ll_test::PostChainGL::read(mScene));
}
