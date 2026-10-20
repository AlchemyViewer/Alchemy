/**
 * @file alskylayers_test.cpp
 * @brief What the sky's composite takes of its layers: the sun's and moon's quads, and where a ray meets one.
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

#include "../alfarplane.h"
#include "../alskylayers.h"

#include "llmath.h"
#include "llquaternion.h"

#include <cmath>

namespace
{
    // LLVOSky's HEAVENLY_BODY_DIST, and the sun's disk radius.
    constexpr F32 BODY_DISTANCE   = 1004.f;
    constexpr F32 SUN_DISK_RADIUS = 0.5f;

    LLQuaternion turnTo(const LLVector3& dir)
    {
        LLQuaternion rotation;
        rotation.shortestArc(LLVector3::x_axis, dir);
        return rotation;
    }

    LLVector3 dome(const LLVector3& agent)
    {
        return ALFarPlane::skyFrame(agent);
    }
}

TEST_CASE("alskylayers: a body's quad is centred along it and square to it and grows toward the horizon", "[alskylayers]")
{
    // Below 81.9 degrees, where the zenith's own axes take over.
    for (const F32 elevation : { 0.f, 3.f, 30.f, 60.f, 80.f })
    {
        CAPTURE(elevation);
        const F32 el = elevation * DEG_TO_RAD;
        const LLVector3 dir(cosf(el) * 0.6f, cosf(el) * 0.8f, sinf(el));
        const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir), BODY_DISTANCE, 1.f, SUN_DISK_RADIUS);

        const LLVector3 centre = (c[0] + c[3]) * 0.5f;
        CHECK(dist_vec(centre, (c[1] + c[2]) * 0.5f) < 1e-3f);
        CHECK(dist_vec(centre, dir * BODY_DISTANCE) < 1e-2f);

        const LLVector3 right = (c[2] - c[0]) * 0.5f;
        const LLVector3 up    = (c[0] - c[1]) * 0.5f;
        CHECK(fabsf(right * up) < 1e-2f);
        CHECK(fabsf(right * dir) < 1e-2f);
        CHECK(fabsf(up * dir) < 1e-2f);
        // Up is up the sky, right along the horizon.
        CHECK(up.mV[VZ] > 0.f);
        CHECK(fabsf(right.mV[VZ]) < 1e-3f);

        // A tenth of the distance times the disk radius, wider by 30% and taller by 20% at the horizon.
        const F32 toward = 1.f - dir.mV[VZ];
        CHECK(fabsf(right.length() - BODY_DISTANCE * 0.1f * SUN_DISK_RADIUS * (1.f + toward * 0.3f)) < 1e-2f);
        CHECK(fabsf(up.length() - BODY_DISTANCE * 0.1f * SUN_DISK_RADIUS * (1.f + toward * 0.2f)) < 1e-2f);
    }
}

TEST_CASE("alskylayers: a body near the zenith takes its own axes", "[alskylayers]")
{
    const LLVector3 dir(0.1f, 0.f, 1.f);
    const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir / dir.length()), BODY_DISTANCE, 1.f, SUN_DISK_RADIUS);
    const LLVector3 right = (c[2] - c[0]) * 0.5f;
    const LLVector3 up    = (c[0] - c[1]) * 0.5f;
    CHECK(right.length() > 10.f);
    CHECK(up.length() > 10.f);
    CHECK(fabsf(right * up) < 1e-2f);
}

TEST_CASE("alskylayers: a ray meets a quad at the texture coordinates of the point it crosses", "[alskylayers]")
{
    const LLVector3 dir = LLVector3(0.3f, -0.5f, 0.6f) / LLVector3(0.3f, -0.5f, 0.6f).length();
    const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir), BODY_DISTANCE, 2.f, SUN_DISK_RADIUS);

    for (const LLVector3& offset : { LLVector3::zero, LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP) })
    {
        CAPTURE(offset);
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(c, offset);

        // The corners take (0, 1), (0, 0), (1, 1) and (1, 0); the centre the middle.
        const F32 expected[5][2] = { { 0.f, 1.f }, { 0.f, 0.f }, { 1.f, 1.f }, { 1.f, 0.f }, { 0.5f, 0.5f } };
        for (S32 i = 0; i < 5; ++i)
        {
            CAPTURE(i);
            const LLVector3 point = (i < 4 ? c[i] : (c[0] + c[3]) * 0.5f) + offset;
            F32 u = -1.f;
            F32 v = -1.f;
            REQUIRE(ALSkyLayers::quadCoord(quad, dome(point / point.length()), u, v));
            CHECK(fabsf(u - expected[i][0]) < 1e-4f);
            CHECK(fabsf(v - expected[i][1]) < 1e-4f);
        }

        // A ray away from the quad, or along its plane, misses it.
        F32 u = 0.f;
        F32 v = 0.f;
        CHECK_FALSE(ALSkyLayers::quadCoord(quad, dome(-dir), u, v));
        const LLVector3 along = (c[2] - c[0]) / (c[2] - c[0]).length();
        CHECK_FALSE(ALSkyLayers::quadCoord(quad, dome(along), u, v));
    }

    // The dome's frame is the agent's (east, north, up) turned to (north, up, east).
    CHECK(dome(LLVector3(1.f, 0.f, 0.f)) == LLVector3(0.f, 0.f, 1.f));
    CHECK(dome(LLVector3(0.f, 1.f, 0.f)) == LLVector3(1.f, 0.f, 0.f));
    CHECK(dome(LLVector3(0.f, 0.f, 1.f)) == LLVector3(0.f, 1.f, 0.f));
}

TEST_CASE("alskylayers: the moon fades as the square of its height near the horizon", "[alskylayers]")
{
    CHECK(ALSkyLayers::moonFade(0.f) == 1.f);
    CHECK(ALSkyLayers::moonFade(-0.3f) == 1.f);
    CHECK(fabsf(ALSkyLayers::moonFade(0.25f) - 0.25f) < 1e-6f);
    CHECK(ALSkyLayers::moonFade(0.5f) == 1.f);
    CHECK(ALSkyLayers::moonFade(0.9f) == 1.f);
}
