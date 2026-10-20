/**
 * @file alskylayers.cpp
 * @brief What the sky's composite takes of the layers it works out along each view ray.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alskylayers.h"

#include "alfarplane.h"

#include "llmath.h"
#include "llquaternion.h"

#include <cmath>

namespace ALSkyLayers
{

namespace
{
    // A body's half-extent per unit of its disk radius and its distance.
    constexpr F32 BODY_FACTOR = 0.1f;
} // namespace

Corners bodyCorners(const LLQuaternion& rotation, F32 distance, F32 scale, F32 disk_radius)
{
    const LLVector3 to_dir = LLVector3::x_axis * rotation;

    LLVector3 right = to_dir % LLVector3::z_axis;
    LLVector3 up    = right % to_dir;

    // At the zenith the cross products above vanish; take the body's own axes.
    if ((to_dir * LLVector3::z_axis) > 0.99f)
    {
        right = LLVector3::y_axis_neg * rotation;
        up    = LLVector3::z_axis * rotation;
    }

    const LLVector3 centre = to_dir * distance;

    right.normalize();
    up.normalize();

    const F32 enlargement       = 1 - to_dir.mV[VZ];
    const F32 horiz_enlargement = 1 + enlargement * 0.3f;
    const F32 vert_enlargement  = 1 + enlargement * 0.2f;

    const LLVector3 half_right = horiz_enlargement * scale * distance * BODY_FACTOR * disk_radius * right;
    const LLVector3 half_up    = vert_enlargement * scale * distance * BODY_FACTOR * disk_radius * up;

    return { centre - half_right + half_up, centre - half_right - half_up, centre + half_right + half_up,
             centre + half_right - half_up };
}

F32 moonFade(F32 moon_z)
{
    return moon_z > 0.f ? llclamp(moon_z * moon_z * 4.f, 0.f, 1.f) : 1.f;
}

std::array<LLVector3, 3> rayQuad(const Corners& corners, const LLVector3& offset)
{
    const LLVector3 centre = (corners[0] + corners[3]) * 0.5f + offset;
    LLVector3 half_u = (corners[2] - corners[0]) * 0.5f;
    LLVector3 half_v = (corners[0] - corners[1]) * 0.5f;
    half_u /= half_u.lengthSquared();
    half_v /= half_v.lengthSquared();
    return { ALFarPlane::skyFrame(centre), ALFarPlane::skyFrame(half_u), ALFarPlane::skyFrame(half_v) };
}

bool quadCoord(const std::array<LLVector3, 3>& quad, const LLVector3& dir, F32& u, F32& v)
{
    const LLVector3 normal = quad[1] % quad[2];
    const F32 facing = dir * normal;
    if (fabsf(facing) < 1e-12f)
    {
        return false;
    }
    const F32 t = (quad[0] * normal) / facing;
    if (t <= 0.f)
    {
        return false;
    }
    const LLVector3 offset = dir * t - quad[0];
    u = (offset * quad[1]) * 0.5f + 0.5f;
    v = (offset * quad[2]) * 0.5f + 0.5f;
    return true;
}

} // namespace ALSkyLayers
