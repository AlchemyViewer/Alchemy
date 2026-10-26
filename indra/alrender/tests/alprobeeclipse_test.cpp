/**
 * @file alprobeeclipse_test.cpp
 * @brief The eclipse pass's test of every automatic probe against every placed one, against the pairwise test it
 *        replaces.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "../alprobeeclipse.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
    constexpr F32 HYSTERESIS = 0.5f;
    // A probe within this many metres of a placed probe's edge is one the two tests may round apart.
    constexpr F32 EDGE = 1e-3f;

    // A placed probe as LLReflectionMap::eclipses() read one, pair by pair.
    struct Placed
    {
        LLVector3    mCentre;
        LLQuaternion mRotation;
        LLVector3    mHalf;
        F32          mRadius = 0.f;
        bool         mBox = false;
    };

    // That test, as it was: the box in its own frame, the offset turned back by the probe's rotation; the sphere's
    // inner half.
    bool pairwise(const Placed& placed, const LLVector3& centre, F32 radius, F32 margin)
    {
        const LLVector3 delta = centre - placed.mCentre;
        if (placed.mBox)
        {
            LLVector3 local = delta;
            local.rotVec(~placed.mRotation);
            return std::fabs(local.mV[0]) + radius <= placed.mHalf.mV[0] + margin
                && std::fabs(local.mV[1]) + radius <= placed.mHalf.mV[1] + margin
                && std::fabs(local.mV[2]) + radius <= placed.mHalf.mV[2] + margin;
        }
        return delta.length() + radius <= placed.mRadius * ALProbeEclipse::SPHERE_FULL_WEIGHT_FRACTION + margin;
    }

    // How near `centre` lies to the edge of the pairwise test, in metres: within a rounding of it, that test's arithmetic
    // and ALProbeEclipse's answer it as the compiler's contractions round each, and neither answer is wrong.
    F32 edge_distance(const Placed& placed, const LLVector3& centre, F32 radius, F32 margin)
    {
        const LLVector3 delta = centre - placed.mCentre;
        if (placed.mBox)
        {
            LLVector3 local = delta;
            local.rotVec(~placed.mRotation);
            F32 nearest = F32_MAX;
            for (U32 i = 0; i < 3; ++i)
            {
                nearest = llmin(nearest, std::fabs(placed.mHalf.mV[i] + margin - std::fabs(local.mV[i]) - radius));
            }
            return nearest;
        }
        return std::fabs(placed.mRadius * ALProbeEclipse::SPHERE_FULL_WEIGHT_FRACTION + margin - delta.length() - radius);
    }

    void add(ALProbeEclipse& eclipse, const Placed& placed)
    {
        if (placed.mBox)
        {
            eclipse.addBox(placed.mCentre, placed.mRotation, placed.mHalf);
        }
        else
        {
            eclipse.addSphere(placed.mCentre, placed.mRadius);
        }
    }

    LLQuaternion turn(F32 angle, const LLVector3& axis)
    {
        LLVector3 unit = axis;
        unit.normVec();
        return LLQuaternion(angle, unit);
    }
}

// A probe inside a box turned by a quarter about z is swallowed along the box's own axes, not the world's.
TEST_CASE("alprobeeclipse: a turned box swallows along its own axes", "[alprobeeclipse]")
{
    ALProbeEclipse eclipse;
    eclipse.clear(HYSTERESIS);
    // 20 m along its own x, 2 m along its own y; turned a quarter about z, so 20 m along the world's y.
    eclipse.addBox(LLVector3(100.f, 100.f, 20.f), turn(F_PI_BY_TWO, LLVector3(0.f, 0.f, 1.f)), LLVector3(10.f, 1.f, 5.f));
    {
        // Narrower than the box's short axis, so that its sides do not sit on that axis's faces, where the turned axes'
        // rounding decides.
        INFO("8 m along the world's y, inside the box's long axis");
        CHECK(eclipse.eclipses(LLVector3(100.f, 108.f, 20.f), 0.5f, 0.f));
    }
    {
        INFO("8 m along the world's x, past the box's short axis");
        CHECK_FALSE(eclipse.eclipses(LLVector3(108.f, 100.f, 20.f), 1.f, 0.f));
    }
    {
        INFO("the margin decides a probe on the edge either way");
        CHECK(eclipse.eclipses(LLVector3(100.f, 109.2f, 20.f), 1.f, HYSTERESIS));
        CHECK_FALSE(eclipse.eclipses(LLVector3(100.f, 109.2f, 20.f), 1.f, -HYSTERESIS));
    }
}

// A sphere swallows only what lies within the half of its radius it weighs fully against the automatic probes.
TEST_CASE("alprobeeclipse: a sphere swallows within its fully weighted half", "[alprobeeclipse]")
{
    ALProbeEclipse eclipse;
    eclipse.clear(HYSTERESIS);
    eclipse.addSphere(LLVector3(0.f, 0.f, 0.f), 20.f);
    CHECK(eclipse.eclipses(LLVector3(5.f, 0.f, 0.f), 4.f, 0.f));
    {
        INFO("inside the sphere, but not inside its half");
        CHECK_FALSE(eclipse.eclipses(LLVector3(9.f, 0.f, 0.f), 4.f, 0.f));
    }
    {
        INFO("nothing placed swallows nothing");
        eclipse.clear(HYSTERESIS);
        CHECK(eclipse.size() == 0);
        CHECK_FALSE(eclipse.eclipses(LLVector3(5.f, 0.f, 0.f), 4.f, 0.f));
    }
}

// Placed and automatic probes thrown at random over a few regions, turned every way and near one another's edges:
// every automatic probe at both margins is swallowed exactly where the pairwise test says one of the placed probes
// swallows it.
TEST_CASE("alprobeeclipse: every pair as the pairwise test answers it", "[alprobeeclipse]")
{
    std::mt19937 rng(20261024u);
    std::uniform_real_distribution<F32> unit(0.f, 1.f);
    const auto in = [&](F32 lo, F32 hi) { return lo + (hi - lo) * unit(rng); };

    for (U32 round = 0; round < 20; ++round)
    {
        std::vector<Placed> placed(60);
        for (Placed& probe : placed)
        {
            probe.mCentre   = LLVector3(in(0.f, 512.f), in(0.f, 512.f), in(0.f, 64.f));
            probe.mBox      = unit(rng) < 0.6f;
            probe.mRotation = turn(in(0.f, F_TWO_PI), LLVector3(in(-1.f, 1.f), in(-1.f, 1.f), in(-1.f, 1.f) + 0.01f));
            probe.mHalf     = LLVector3(in(0.5f, 40.f), in(0.5f, 40.f), in(0.5f, 20.f));
            probe.mRadius   = in(1.f, 64.f);
        }
        ALProbeEclipse eclipse;
        eclipse.clear(HYSTERESIS);
        for (const Placed& probe : placed)
        {
            add(eclipse, probe);
        }

        U32 swallowed = 0;
        U32 edges     = 0;
        for (U32 i = 0; i < 2000; ++i)
        {
            // Most near a placed probe, where the answer turns.
            const Placed& beside = placed[i % placed.size()];
            const LLVector3 centre = unit(rng) < 0.8f
                ? beside.mCentre + LLVector3(in(-30.f, 30.f), in(-30.f, 30.f), in(-15.f, 15.f))
                : LLVector3(in(0.f, 512.f), in(0.f, 512.f), in(0.f, 64.f));
            const F32 radius = in(0.f, 12.f);
            for (const F32 margin : { HYSTERESIS, -HYSTERESIS })
            {
                bool expected = false;
                bool on_edge  = false;
                for (const Placed& probe : placed)
                {
                    expected = expected || pairwise(probe, centre, radius, margin);
                    on_edge  = on_edge || edge_distance(probe, centre, radius, margin) < EDGE;
                }
                if (on_edge)
                {
                    ++edges;
                    continue;
                }
                INFO("round " << round << ", probe " << i << ", margin " << margin);
                REQUIRE(eclipse.eclipses(centre, radius, margin) == expected);
                swallowed += expected;
            }
        }
        INFO("the rounds swallow some probes and leave others");
        CHECK(swallowed > 0);
        CHECK(swallowed < 4000);
        INFO("and few lie on an edge");
        CHECK(edges < 40);
    }
}
