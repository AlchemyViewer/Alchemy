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

#include <algorithm>
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
    // fully weighted radius.
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
        return delta.length() + radius <= ALProbeEclipse::sphereFullWeightRadius(placed.mRadius) + margin;
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
        return std::fabs(ALProbeEclipse::sphereFullWeightRadius(placed.mRadius) + margin - delta.length() - radius);
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

// The fully weighted radius is where sphereWeight's distance weight (class3/deferred/reflectionProbeF.glsl), for a probe
// faded fully in, reaches 1, at every radius: inside it probeMix takes the placed probes alone.
TEST_CASE("alprobeeclipse: a sphere weighs fully exactly within its fully weighted radius", "[alprobeeclipse]")
{
    const auto shader_dw = [](F32 d, F32 r)
    {
        const F32 r1    = r * 0.5f;
        const F32 d2    = std::max(d, 0.001f);
        const F32 atten = 1.f - std::max(d2 - r1, 0.f) / std::max(r - r1, 0.001f);
        return (1.f / d2) * atten * std::max(r, 1.f) * 4.f;
    };

    for (const F32 r : { 0.25f, 0.5f, 1.f, 2.f, 4.f, 20.f, 64.f })
    {
        const F32 full = ALProbeEclipse::sphereFullWeightRadius(r);
        INFO("radius " << r << " fully weighted to " << full);
        CHECK(full >= r * 0.5f);
        CHECK(full < r);
        for (U32 i = 1; i <= 1000; ++i)
        {
            const F32 d = r * (F32)i / 1000.f;
            if (std::fabs(d - full) < r * 1e-4f)
            {
                continue;
            }
            INFO("at " << d);
            CHECK((shader_dw(d, r) >= 1.f) == (d <= full));
        }
    }
    CHECK(ALProbeEclipse::sphereFullWeightRadius(0.f) == 0.f);
}

// A sphere swallows what lies within the radius it weighs fully against the automatic probes, and not what only lies
// within the sphere.
TEST_CASE("alprobeeclipse: a sphere swallows within its fully weighted radius", "[alprobeeclipse]")
{
    ALProbeEclipse eclipse;
    eclipse.clear(HYSTERESIS);
    // Fully weighted to 160/9, about 17.8 m.
    eclipse.addSphere(LLVector3(0.f, 0.f, 0.f), 20.f);
    CHECK(eclipse.eclipses(LLVector3(5.f, 0.f, 0.f), 4.f, 0.f));
    {
        INFO("past half the radius, within the fully weighted one");
        CHECK(eclipse.eclipses(LLVector3(9.f, 0.f, 0.f), 4.f, 0.f));
    }
    {
        INFO("inside the sphere, but reaching past its fully weighted radius");
        CHECK_FALSE(eclipse.eclipses(LLVector3(13.f, 0.f, 0.f), 6.f, 0.f));
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
