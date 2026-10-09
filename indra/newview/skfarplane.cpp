/**
 * @file skfarplane.cpp
 * @brief How far the main projection reaches under each depth convention.
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

#include "skfarplane.h"

#include "llcamera.h"
#include "llmath.h"

#include <cmath>
#include <limits>

F32 skDrawDistanceCeiling(bool reverse_z)
{
    return reverse_z ? MAX_FAR_CLIP : FORWARD_Z_MAX_FAR_CLIP;
}

F32 skClampDrawDistance(F32 setting, bool reverse_z)
{
    return llmin(setting, skDrawDistanceCeiling(reverse_z));
}

const F32 SK_PROJECTION_INFINITE = std::numeric_limits<F32>::infinity();

bool skIsInfinite(F32 projection_far)
{
    return std::isinf(projection_far);
}

F32 skProjectionFar(bool reverse_z, bool cube_snapshot)
{
    return (reverse_z && !cube_snapshot) ? SK_PROJECTION_INFINITE : SK_FORWARD_Z_PROJECTION_FAR;
}

F32 skFrustumFarWindowDepth(bool infinite, F32 near_plane, F32 window_far)
{
    return infinite ? near_plane / SK_RECONSTRUCT_FAR : window_far;
}

F32 skFarthestWaterHorizontal()
{
    const F32 edge = skEdgeWaterStretch(true) + MAX_FAR_CLIP;
    return sqrtf(2.f * edge * edge);
}

SKWaterFar skWaterFar(F32 near_plane, F32 projection_far)
{
    SKWaterFar water;
    if (skIsInfinite(projection_far))
    {
        water.mWaveClamp = skFarthestWaterHorizontal();
        water.mEdgeFade = skEdgeWaterStretch(true);
    }
    return water;
}

F32 skTerrainReach(F32 projection_far)
{
    return skIsInfinite(projection_far) ? SK_REACH_TERRAIN : 0.f;
}

bool skWaterVisibleFrom(F32 camera_z, F32 water_height, F32 projection_far)
{
    return skIsInfinite(projection_far) ? camera_z - water_height < SK_EDGE_WATER_STRETCH : camera_z < 1024.f;
}

F32 skEdgeWaterStretch(bool infinite)
{
    return infinite ? SK_EDGE_WATER_STRETCH : 2048.f;
}

F32 skForcedProjectionFar(F32 projection_far, S32 force, F32 draw_distance, bool reverse_z, bool cube_snapshot)
{
    if (cube_snapshot || !reverse_z)
    {
        return projection_far;
    }
    switch (force)
    {
        // The finite plane the viewer used before the infinite one: twice the draw distance, never under 1024 m.
        case 1: return llmax(SK_FORWARD_Z_PROJECTION_FAR, draw_distance * 2.f);
        case 2: return SK_PROJECTION_INFINITE;
        default: return projection_far;
    }
}

const std::vector<SKFarOverlayColumn>& skFarOverlayColumns()
{
    static const std::vector<SKFarOverlayColumn> columns = []
    {
        constexpr F32 distances[] = { 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f, 32000.f, 64000.f, 100000.f };
        constexpr F32 half_width_per_metre = 0.01f;
        constexpr F32 height_per_metre = 0.04f;
        constexpr F32 farther = 1.02f;
        std::vector<SKFarOverlayColumn> out;
        F32 bearing = 0.f;
        for (F32 d : distances)
        {
            SKFarOverlayColumn c;
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
