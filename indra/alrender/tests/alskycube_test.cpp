/**
 * @file alskycube_test.cpp
 * @brief The environment cube's directions: each texel's against the face and texel GL's cube lookup selects for it,
 *        and the turn into the dome's frame. And when two skies bake alike.
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

#include "../alskycube.h"

#include "llmath.h"

#include <array>
#include <cmath>

namespace
{
    // GL's cube map face selection, as the GL specification tabulates it: the major axis of @a r picks the face (+x,
    // -x, +y, -y, +z, -z), and sc and tc over its magnitude, taken from [-1, 1] to [0, 1], are s and t across it.
    void select(const LLVector3& r, U32& face, F32& s, F32& t)
    {
        const F32 ax = fabsf(r.mV[VX]);
        const F32 ay = fabsf(r.mV[VY]);
        const F32 az = fabsf(r.mV[VZ]);
        F32       sc = 0.f;
        F32       tc = 0.f;
        F32       ma = 0.f;
        if (ax >= ay && ax >= az)
        {
            face = r.mV[VX] > 0.f ? 0 : 1;
            sc   = r.mV[VX] > 0.f ? -r.mV[VZ] : r.mV[VZ];
            tc   = -r.mV[VY];
            ma   = ax;
        }
        else if (ay >= az)
        {
            face = r.mV[VY] > 0.f ? 2 : 3;
            sc   = r.mV[VX];
            tc   = r.mV[VY] > 0.f ? r.mV[VZ] : -r.mV[VZ];
            ma   = ay;
        }
        else
        {
            face = r.mV[VZ] > 0.f ? 4 : 5;
            sc   = r.mV[VZ] > 0.f ? r.mV[VX] : -r.mV[VX];
            tc   = -r.mV[VY];
            ma   = az;
        }
        s = 0.5f * (sc / ma + 1.f);
        t = 0.5f * (tc / ma + 1.f);
    }
}

TEST_CASE("alskycube: each texel looks along the direction GL's lookup selects it for", "[alskycube]")
{
    for (U32 face = 0; face < 6; ++face)
    {
        for (U32 y = 0; y < ALSkyCube::SIZE; ++y)
        {
            for (U32 x = 0; x < ALSkyCube::SIZE; ++x)
            {
                const LLVector3 dir = ALSkyCube::texelDirection(face, x, y);
                CAPTURE(face, x, y, dir.mV[VX], dir.mV[VY], dir.mV[VZ]);
                REQUIRE(fabsf(dir.length() - 1.f) < 1e-5f);

                U32 selected = 6;
                F32 s        = -1.f;
                F32 t        = -1.f;
                select(dir, selected, s, t);
                REQUIRE(selected == face);
                // The texel's centre.
                REQUIRE(fabsf(s * ALSkyCube::SIZE - ((F32)x + 0.5f)) < 1e-3f);
                REQUIRE(fabsf(t * ALSkyCube::SIZE - ((F32)y + 0.5f)) < 1e-3f);
            }
        }
    }
}

TEST_CASE("alskycube: a face's directions are its forward, right and up", "[alskycube]")
{
    for (U32 face = 0; face < 6; ++face)
    {
        CAPTURE(face);
        const ALSkyCube::Face basis = ALSkyCube::face(face);
        // Square to one another and of unit length, the forward along the face's own axis and sign.
        CHECK(fabsf(basis.mForward * basis.mRight) < 1e-6f);
        CHECK(fabsf(basis.mForward * basis.mUp) < 1e-6f);
        CHECK(fabsf(basis.mRight * basis.mUp) < 1e-6f);
        CHECK(fabsf(basis.mForward.mV[face / 2] - ((face & 1) ? -1.f : 1.f)) < 1e-6f);

        // The face's centre and corners as the first and last texels lie toward them.
        const LLVector3 first = ALSkyCube::texelDirection(face, 0, 0);
        const LLVector3 last  = ALSkyCube::texelDirection(face, ALSkyCube::SIZE - 1, ALSkyCube::SIZE - 1);
        CHECK(first * basis.mRight < 0.f);
        CHECK(first * basis.mUp < 0.f);
        CHECK(last * basis.mRight > 0.f);
        CHECK(last * basis.mUp > 0.f);
    }
}

TEST_CASE("alskycube: the agent's frame turns into the dome's as the light does", "[alskycube]")
{
    // LLEnvironment::toLightNorm: (east, north, up) to (north, up, east).
    const LLVector3 agent(0.2f, -0.7f, 0.3f);
    const LLVector3 dome = ALSkyCube::toDome(agent);
    CHECK(dome.mV[0] == agent.mV[VY]);
    CHECK(dome.mV[1] == agent.mV[VZ]);
    CHECK(dome.mV[2] == agent.mV[VX]);

    // Up in the agent's frame is up in the dome's, where windlightDome takes y as the height.
    const LLVector3 up = ALSkyCube::toDome(LLVector3(0.f, 0.f, 1.f));
    CHECK(up.mV[1] == 1.f);
}

TEST_CASE("alskycube: terms within the tolerance bake alike and terms past it do not", "[alskycube]")
{
    constexpr F32 T = ALSkyCube::TERM_TOLERANCE;
    // A sky's terms: a colour channel, a density, a term at zero, and the dome's radius.
    const std::array<F32, 4> last = { 0.25f, 1.5f, 0.f, 15000.f };
    CHECK(ALSkyCube::termsAlike(last, last));

    // Each moved by under the tolerance of the larger, the zero by under F_APPROXIMATELY_ZERO, as a day cycle moves
    // them between two bakes.
    const std::array<F32, 4> drifted = { 0.25f * (1.f + 0.4f * T), 1.5f * (1.f - 0.4f * T), 0.5f * F_APPROXIMATELY_ZERO,
                                         15000.f * (1.f + 0.9f * T) };
    CHECK(ALSkyCube::termsAlike(last, drifted));
    CHECK(ALSkyCube::termsAlike(drifted, last));

    // Any one moved past it.
    const std::array<F32, 4> moved = { 0.25f * (1.f + 2.f * T), 1.5f * (1.f - 2.f * T), 2.f * F_APPROXIMATELY_ZERO,
                                       15000.f * (1.f + 2.f * T) };
    for (size_t i = 0; i < last.size(); ++i)
    {
        CAPTURE(i);
        std::array<F32, 4> one = last;
        one[i] = moved[i];
        CHECK_FALSE(ALSkyCube::termsAlike(last, one));
        CHECK_FALSE(ALSkyCube::termsAlike(one, last));
    }

    // Skies of different lengths are not alike.
    const std::array<F32, 3> fewer = { last[0], last[1], last[2] };
    CHECK_FALSE(ALSkyCube::termsAlike(last, fewer));
}
