/**
 * @file alfarplane.cpp
 * @brief The main view's distances: the draw distance, the projection's far plane, and how far
 *        terrain and water reach.
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

#include "alfarplane.h"

#include "llcamera.h"
#include "llmath.h"

#include <cmath>

namespace ALFarPlane
{

bool isInfinite(F32 projection_far)
{
    return std::isinf(projection_far);
}

F32 drawDistanceCeiling(bool reverse_z)
{
    return reverse_z ? MAX_FAR_CLIP : FORWARD_Z_MAX_FAR_CLIP;
}

F32 clampDrawDistance(F32 setting, bool reverse_z)
{
    return llmin(setting, drawDistanceCeiling(reverse_z));
}

F32 projectionFar(bool reverse_z, bool cube_snapshot, S32 force, F32 draw_distance)
{
    if (cube_snapshot || !reverse_z)
    {
        return FINITE_PROJECTION_FAR;
    }
    if (force == FORCE_FINITE)
    {
        return llmax(FINITE_PROJECTION_FAR, draw_distance * 2.f);
    }
    return INFINITE_FAR;
}

F32 frustumFarWindowDepth(bool infinite, F32 near_plane, F32 window_far)
{
    return infinite ? near_plane / MAX_RECONSTRUCT_DISTANCE : window_far;
}

F32 terrainReach(F32 projection_far)
{
    return isInfinite(projection_far) ? TERRAIN_REACH : 0.f;
}

F32 edgeWaterStretch(bool infinite)
{
    return infinite ? EDGE_WATER_STRETCH : FINITE_EDGE_WATER_STRETCH;
}

bool waterVisibleFrom(F32 camera_z, F32 water_height, F32 projection_far)
{
    return isInfinite(projection_far) ? camera_z - water_height < EDGE_WATER_STRETCH : camera_z < FINITE_PROJECTION_FAR;
}

WaterFar waterFar(F32 projection_far, bool have_rim, const LLVector2& rim_min, const LLVector2& rim_max)
{
    WaterFar water;
    if (!isInfinite(projection_far))
    {
        return water;
    }
    if (!have_rim)
    {
        // Region water alone, which lies within the regions.
        water.mWaveClamp = EDGE_WATER_STRETCH;
        return water;
    }
    const LLVector2 corner(llmax(fabsf(rim_min.mV[VX]), fabsf(rim_max.mV[VX])), llmax(fabsf(rim_min.mV[VY]), fabsf(rim_max.mV[VY])));
    water.mWaveClamp = corner.length();
    water.mEdgeFade = EDGE_WATER_STRETCH;
    return water;
}

const std::vector<OverlayColumn>& overlayColumns()
{
    static const std::vector<OverlayColumn> columns = []
    {
        constexpr F32 distances[] = { 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f, 32000.f, 64000.f, 100000.f };
        constexpr F32 half_width_per_metre = 0.01f;
        constexpr F32 height_per_metre = 0.04f;
        constexpr F32 farther = 1.02f;
        std::vector<OverlayColumn> out;
        F32 bearing = 0.f;
        for (F32 d : distances)
        {
            OverlayColumn c;
            c.mDistance = d;
            c.mBearing = bearing;
            c.mHalfWidth = d * half_width_per_metre;
            c.mHeight = d * height_per_metre;
            c.mNearer = true;
            out.push_back(c);
            c.mDistance = d * farther;
            c.mOffset = c.mHalfWidth;
            c.mHalfWidth *= farther;
            c.mHeight *= farther;
            c.mNearer = false;
            out.push_back(c);
            bearing += 40.f;
        }
        return out;
    }();
    return columns;
}

} // namespace ALFarPlane
