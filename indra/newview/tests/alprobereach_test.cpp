/**
 * @file alprobereach_test.cpp
 * @brief Unit tests for which reflection probes the view reaches, and the cube slots dynamic allocation asks for
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

#include <catch2/catch_test_macros.hpp>

#include "../alprobereach.h"

namespace
{
    const LLVector3 CAMERA(128.f, 128.f, 30.f);

    TEST_CASE("ALProbeReach: a probe is in reach while its sphere comes within draw distance", "[ALProbeReach]")
    {
        const F32 draw = 128.f;
        // a 16 m probe whose centre is 144 m off: its sphere's near side is at the draw distance
        CHECK(ALProbeReach::inReach(CAMERA + LLVector3(144.f, 0.f, 0.f), 16.f, CAMERA, draw));
        CHECK_FALSE(ALProbeReach::inReach(CAMERA + LLVector3(145.f, 0.f, 0.f), 16.f, CAMERA, draw));
        // the distance is to the centre in three dimensions, not along an axis
        CHECK_FALSE(ALProbeReach::inReach(CAMERA + LLVector3(110.f, 110.f, 0.f), 16.f, CAMERA, draw));
        CHECK_FALSE(ALProbeReach::inReach(CAMERA + LLVector3(0.f, 0.f, 200.f), 16.f, CAMERA, draw));
        // a large placed probe is in reach as long as any of it is
        CHECK(ALProbeReach::inReach(CAMERA + LLVector3(300.f, 0.f, 0.f), 180.f, CAMERA, draw));
        // the camera inside a probe
        CHECK(ALProbeReach::inReach(CAMERA + LLVector3(4.f, 0.f, 0.f), 16.f, CAMERA, draw));
        UNSCOPED_INFO("a draw distance below zero reaches only the probes the camera is in");
        CHECK(ALProbeReach::inReach(CAMERA + LLVector3(15.f, 0.f, 0.f), 16.f, CAMERA, -10.f));
        CHECK_FALSE(ALProbeReach::inReach(CAMERA + LLVector3(17.f, 0.f, 0.f), 16.f, CAMERA, -10.f));
    }

    // The probes of a region a teleport left are a region or more away: none of them is in reach of a camera at the
    // arrival, at any draw distance short of that.
    TEST_CASE("ALProbeReach: the terrain probes of a region far behind are out of reach", "[ALProbeReach]")
    {
        const F32 draw = 256.f;
        const LLVector3 far_origin(128.f + 2048.f, 128.f, 0.f);
        const F32 spacing = 32.f;
        for (U32 i = 0; i < 8; ++i)
        {
            for (U32 j = 0; j < 8; ++j)
            {
                const LLVector3 origin = far_origin + LLVector3(i * spacing, j * spacing, 20.f);
                CHECK_FALSE(ALProbeReach::inReach(origin, spacing * 0.75f, CAMERA, draw));
            }
        }
    }

    TEST_CASE("ALProbeReach: the relevant count moves a step at a time", "[ALProbeReach]")
    {
        CHECK(ALProbeReach::stepRelevant(0, 0) == 0u);
        CHECK(ALProbeReach::stepRelevant(1, 0) == 16u);
        CHECK(ALProbeReach::stepRelevant(16, 0) == 16u);
        CHECK(ALProbeReach::stepRelevant(17, 0) == 32u);
        CHECK(ALProbeReach::stepRelevant(250, 0) == 256u);
        UNSCOPED_INFO("up as soon as a step is needed");
        CHECK(ALProbeReach::stepRelevant(33, 32) == 48u);
    }

    // A probe crossing the edge of draw distance moves the count across a step's boundary and back: the count stays.
    TEST_CASE("ALProbeReach: the relevant count does not flap at a step's edge", "[ALProbeReach]")
    {
        U32 current = ALProbeReach::stepRelevant(33, 0);
        REQUIRE(current == 48u);
        for (U32 i = 0; i < 10; ++i)
        {
            current = ALProbeReach::stepRelevant((i % 2) ? 33 : 32, current);
            CHECK(current == 48u);
        }
        UNSCOPED_INFO("down once more than a step fewer");
        CHECK(ALProbeReach::stepRelevant(31, 48) == 48u);
        CHECK(ALProbeReach::stepRelevant(16, 48) == 16u);
        CHECK(ALProbeReach::stepRelevant(0, 48) == 0u);
    }

    // Dynamic allocation sizes the cube array from the probes that matter; off, it takes every slot allowed.
    TEST_CASE("ALProbeReach: dynamic allocation counts the probes that matter", "[ALProbeReach]")
    {
        const U32 slots = 256;

        UNSCOPED_INFO("off");
        CHECK(ALProbeReach::dynamicProbeCount(1, -1, 3, slots) == slots);
        CHECK(ALProbeReach::dynamicProbeCount(3, -1, 3, slots) == slots);

        UNSCOPED_INFO("level 0 takes the default probe only");
        CHECK(ALProbeReach::dynamicProbeCount(0, 0, 200, slots) == 1u);

        UNSCOPED_INFO("level 1 takes the relevant probes and the buffer");
        CHECK(ALProbeReach::dynamicProbeCount(1, 0, 48, slots) == 48u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 1, 48, slots) == 49u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 0, 0, slots) == 1u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 0, 4000, slots) == slots);

        UNSCOPED_INFO("level 2 takes at least 128");
        CHECK(ALProbeReach::dynamicProbeCount(2, 0, 48, slots) == 128u);
        CHECK(ALProbeReach::dynamicProbeCount(2, 0, 160, slots) == 160u);

        UNSCOPED_INFO("level 3 takes 256");
        CHECK(ALProbeReach::dynamicProbeCount(3, 0, 48, slots) == 256u);

        UNSCOPED_INFO("a buffer above 1 rounds to its multiple, in sixteens");
        CHECK(ALProbeReach::dynamicProbeCount(1, 16, 40, slots) == 48u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 16, 56, slots) == 64u);
        UNSCOPED_INFO("at other values the count is scaled by 16 / allocation");
        CHECK(ALProbeReach::dynamicProbeCount(1, 2, 16, slots) == 128u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 2, 40, slots) == slots);
        CHECK(ALProbeReach::dynamicProbeCount(1, 32, 40, slots) == 16u);
        CHECK(ALProbeReach::dynamicProbeCount(1, 32, 8, slots) == 1u);
    }

    // A probe in reach stays in it a margin past draw distance, and one out of it comes back only within it.
    TEST_CASE("ALProbeReach: reach does not flap at the edge of draw distance", "[ALProbeReach]")
    {
        const F32 draw = 128.f;
        const F32 edge = draw + ALProbeReach::REACH_MARGIN;
        CHECK(ALProbeReach::inReach(draw + 1.f, draw, true));
        CHECK(ALProbeReach::inReach(edge, draw, true));
        CHECK_FALSE(ALProbeReach::inReach(edge + 0.5f, draw, true));
        CHECK_FALSE(ALProbeReach::inReach(draw + 1.f, draw, false));
        CHECK(ALProbeReach::inReach(draw, draw, false));

        // a probe wandering either side of the edge holds its state
        bool in = true;
        for (F32 d : { draw - 2.f, draw + 3.f, draw - 1.f, draw + 8.f, draw + 2.f })
        {
            in = ALProbeReach::inReach(d, draw, in);
            CHECK(in);
        }
        in = ALProbeReach::inReach(edge + 4.f, draw, in);
        CHECK_FALSE(in);
        for (F32 d : { draw + 8.f, draw + 3.f, draw + 1.f })
        {
            in = ALProbeReach::inReach(d, draw, in);
            CHECK_FALSE(in);
        }
        CHECK(ALProbeReach::inReach(draw - 1.f, draw, in));
    }
}
