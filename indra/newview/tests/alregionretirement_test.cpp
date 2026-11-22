/**
 * @file alregionretirement_test.cpp
 * @brief Unit tests for which regions a settled teleport arrival keeps, and when an arrival has settled
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

#include "../alregionretirement.h"

namespace
{
    constexpr F64 WIDTH = 256.0;
    constexpr F64 DRAW = 128.0;
    // the agent stands in the middle of the region whose origin is (2560, 2560)
    const LLVector3d AGENT(2688.0, 2688.0, 30.0);

    // A region the agent is not in and is not connected to, with its origin at x, y.
    bool keeps(F64 x, F64 y, F64 width = WIDTH, F64 draw = DRAW)
    {
        return ALRegionRetirement::keeps(false, false, LLVector3d(x, y, 0.0), width, AGENT, draw);
    }

    TEST_CASE("ALRegionRetirement: the agent's region and its connected neighbours stay however far", "[ALRegionRetirement]")
    {
        const LLVector3d far_away(100000.0, 100000.0, 0.0);
        CHECK(ALRegionRetirement::keeps(true, false, far_away, WIDTH, AGENT, DRAW));
        CHECK(ALRegionRetirement::keeps(false, true, far_away, WIDTH, AGENT, DRAW));
        CHECK_FALSE(ALRegionRetirement::keeps(false, false, far_away, WIDTH, AGENT, DRAW));
    }

    // The reach is draw distance plus one region width, measured from the agent to the nearest point of the
    // region's square: 384 m here.
    TEST_CASE("ALRegionRetirement: a region stays while its square is within draw distance and a region width", "[ALRegionRetirement]")
    {
        const F64 reach = DRAW + WIDTH;
        // east: the square's west edge at the reach, then a metre past it
        CHECK(keeps(AGENT.mdV[VX] + reach, 2560.0));
        CHECK_FALSE(keeps(AGENT.mdV[VX] + reach + 1.0, 2560.0));
        // west: the square's east edge is what counts, not its origin
        CHECK(keeps(AGENT.mdV[VX] - reach - WIDTH, 2560.0));
        CHECK_FALSE(keeps(AGENT.mdV[VX] - reach - WIDTH - 1.0, 2560.0));
        // north and south likewise
        CHECK(keeps(2560.0, AGENT.mdV[VY] + reach));
        CHECK_FALSE(keeps(2560.0, AGENT.mdV[VY] + reach + 1.0));
        CHECK(keeps(2560.0, AGENT.mdV[VY] - reach - WIDTH));
        CHECK_FALSE(keeps(2560.0, AGENT.mdV[VY] - reach - WIDTH - 1.0));
        // a square the agent stands over without being in it
        CHECK(keeps(2560.0, 2560.0));
    }

    // Diagonally the reach is a circle: a corner within the reach on each axis alone can be out of it.
    TEST_CASE("ALRegionRetirement: the reach is a distance and not a box", "[ALRegionRetirement]")
    {
        const F64 reach = DRAW + WIDTH;
        const F64 on_circle = reach / std::sqrt(2.0);
        CHECK(keeps(AGENT.mdV[VX] + on_circle - 1.0, AGENT.mdV[VY] + on_circle - 1.0));
        CHECK_FALSE(keeps(AGENT.mdV[VX] + on_circle + 1.0, AGENT.mdV[VY] + on_circle + 1.0));
        CHECK_FALSE(keeps(AGENT.mdV[VX] + reach - 1.0, AGENT.mdV[VY] + reach - 1.0));
    }

    TEST_CASE("ALRegionRetirement: a wider region reaches further and height does not count", "[ALRegionRetirement]")
    {
        const F64 big = 512.0;
        CHECK_FALSE(keeps(AGENT.mdV[VX] + DRAW + WIDTH + 100.0, 2560.0));
        CHECK(keeps(AGENT.mdV[VX] + DRAW + WIDTH + 100.0, 2560.0, big));
        CHECK(ALRegionRetirement::keeps(false, false, LLVector3d(AGENT.mdV[VX] + DRAW + WIDTH, 2560.0, 4000.0), WIDTH,
                                        AGENT, DRAW));
        UNSCOPED_INFO("a draw distance below zero reaches one region width");
        CHECK(keeps(AGENT.mdV[VX] + WIDTH, 2560.0, WIDTH, -50.0));
        CHECK_FALSE(keeps(AGENT.mdV[VX] + WIDTH + 1.0, 2560.0, WIDTH, -50.0));
    }

    TEST_CASE("ALRegionRetirement: an arrival settles once it is old enough and the queues are calm", "[ALRegionRetirement]")
    {
        const F64 min_s = ALRegionRetirement::MIN_SETTLE_SECONDS;
        const F64 max_s = ALRegionRetirement::MAX_SETTLE_SECONDS;
        const S32 calm_fetch = ALRegionRetirement::CALM_FETCH_REQUESTS;
        const S32 calm_create = ALRegionRetirement::CALM_CREATE_QUEUE;

        UNSCOPED_INFO("never before the minimum, however calm");
        CHECK_FALSE(ALRegionRetirement::settled(0.0, 0, 0));
        CHECK_FALSE(ALRegionRetirement::settled(min_s - 0.01, 0, 0));

        CHECK(ALRegionRetirement::settled(min_s, calm_fetch, calm_create));
        CHECK_FALSE(ALRegionRetirement::settled(min_s, calm_fetch + 1, 0));
        CHECK_FALSE(ALRegionRetirement::settled(min_s, 0, calm_create + 1));
        CHECK_FALSE(ALRegionRetirement::settled(max_s - 0.01, 1000, 1000));

        UNSCOPED_INFO("at the maximum, however busy");
        CHECK(ALRegionRetirement::settled(max_s, 1000, 1000));
    }

    // The simulator still holds the agent in a retiring region it arrives at, and sends nothing it sent before: a new
    // region in its place would stay empty.
    TEST_CASE("ALRegionRetirement: a simulator arriving at a retiring region it still holds revives it", "[ALRegionRetirement]")
    {
        using EArrival = ALRegionRetirement::EArrival;
        CHECK(ALRegionRetirement::arrivalAt(true, true, true) == EArrival::REVIVE);
        CHECK(ALRegionRetirement::arrivalAt(true, true, false) == EArrival::REUSE);

        UNSCOPED_INFO("another host, or a region whose circuit is gone, is replaced whether it retires or not");
        CHECK(ALRegionRetirement::arrivalAt(false, true, true) == EArrival::REPLACE);
        CHECK(ALRegionRetirement::arrivalAt(false, true, false) == EArrival::REPLACE);
        CHECK(ALRegionRetirement::arrivalAt(true, false, true) == EArrival::REPLACE);
        CHECK(ALRegionRetirement::arrivalAt(true, false, false) == EArrival::REPLACE);
    }

    TEST_CASE("ALRetiredObjects: a revival asks once for each object lost and for none its simulator killed", "[ALRegionRetirement]")
    {
        ALRetiredObjects lost;
        CHECK(lost.empty());
        lost.lost(30);
        lost.lost(7);
        // killed by the retirement, then dropped as its simulator sent an update of it
        lost.lost(30);
        lost.lost(12);
        lost.killed(12);
        UNSCOPED_INFO("a kill of an object never lost changes nothing");
        lost.killed(99);
        CHECK(lost.size() == 2);
        CHECK(lost.take() == std::vector<U32>{ 7, 30 });
        CHECK(lost.empty());
        CHECK(lost.take().empty());
    }

    TEST_CASE("ALRetiredObjects: an object made under the local ID of one its simulator killed is asked for", "[ALRegionRetirement]")
    {
        ALRetiredObjects lost;
        lost.lost(5);
        lost.killed(5);
        lost.lost(5);
        CHECK(lost.take() == std::vector<U32>{ 5 });
    }
}
