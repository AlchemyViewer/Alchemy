/**
 * @file skfarplane.h
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

#pragma once

#include "stdtypes.h"

#include <vector>

// The two numbers are kept apart on purpose. The camera's far plane is the draw distance: it sizes
// the cull sphere, fog, shadow splits, the minimap wedge and more. The projection's far plane only
// decides where the GPU clips and how depth is spread, so it follows the depth buffer's precision.

// The highest draw distance the depth convention supports: reverse-Z float depth keeps its
// precision to MAX_FAR_CLIP, forward 24-bit depth stays at the old ceiling.
F32 skDrawDistanceCeiling(bool reverse_z);

// The draw distance every consumer sees: the RenderFarClip setting held to skDrawDistanceCeiling.
F32 skClampDrawDistance(F32 setting, bool reverse_z);

// The main projection's far plane: infinite (SK_PROJECTION_INFINITE) whenever reverse-Z is on, with what is
// drawn limited by the draw distance and reach instead. Forward-Z and cube snapshots keep the old fixed plane.
F32 skProjectionFar(bool reverse_z, bool cube_snapshot);

// What skProjectionFar returns for an infinite projection.
extern const F32 SK_PROJECTION_INFINITE;
bool skIsInfinite(F32 projection_far);

// The sphere that bounds terrain and water, whose partitions ignore the draw distance and were bounded
// only by the projection: SK_REACH_TERRAIN under an infinite projection, 0 (unbounded) otherwise.
F32 skTerrainReach(F32 projection_far);

// The horizon from 4 km up, the top of a region, is 243.9 km away with standard refraction (225.8 km geometric), so edge
// water stretched 256 km past the regions reaches it from any height a region allows; vertices stay within ~3 cm.
constexpr F32 SK_EDGE_WATER_STRETCH = 256000.f;

// How far edge (void) water stretches past the loaded regions: out to SK_EDGE_WATER_STRETCH while the projection
// is infinite, the old 2048 m otherwise.
F32 skEdgeWaterStretch(bool infinite);

// Whether water is drawn from a camera this high: upstream stops at 1024 m, where its projection had already
// clipped the water away; under an infinite projection water shows from as high as the edge water stretches.
bool skWaterVisibleFrom(F32 camera_z, F32 water_height, F32 projection_far);

// Window depth updateFrustumPlanes takes the far corners at: SK_RECONSTRUCT_FAR's depth under an infinite
// projection, which has no far plane, otherwise the projection's own far depth.
F32 skFrustumFarWindowDepth(bool infinite, F32 near_plane, F32 window_far);

// How the shaders tell the pinned sky from real content under an infinite reverse-Z projection, where the
// nearest sky (SK_SKY_PIN_DEPTH_INFINITE) sits ~13,400 km out instead of the old ~974 m. Under a finite projection nothing is
// classified: mThreshold is -1 and mReachDepth 0, so every stored depth is left as it was.
struct SKSkyDepth
{
    F32 mThreshold = -1.f;      // stored depths at or below this are sky; the geometric mean of the pin and the farthest water
    F32 mReachDepth = 0.f;      // the smallest depth content within reach stores, near / reach
    F32 mLegacyDistance = 0.f;  // eye depth the pin had under the old 1024 m projection, which haze keeps
};
SKSkyDepth skSkyDepth(F32 near_plane, F32 projection_far);

// Distance to the farthest water the haze can see: a corner of the edge water, stretched skEdgeWaterStretch past
// regions loaded out to MAX_FAR_CLIP, seen from as high above it. The sky threshold stays past it.
F32 skFarthestWater();

// Horizontal distance to that corner, which the waves follow out to so none of the water shares one wave.
F32 skFarthestWaterHorizontal();

// The water shaders' far-plane terms (sk_water_far). Finite projections keep the defaults, which leave the
// shaders as upstream: waves clamped at 2560 m and no edge fade.
struct SKWaterFar
{
    F32 mWaveClamp = 2560.f;    // horizontal distance past which wave coordinates stop following the surface
    F32 mEdgeFade = 0.f;        // edge water stretch; water fades out over its last 15%
};
SKWaterFar skWaterFar(F32 near_plane, F32 projection_far);

// SKRenderFarPlaneForce, a debug A/B over skProjectionFar's choice: 0 keeps it, 1 forces the old finite plane,
// 2 forces infinite on reverse-Z (forward-Z ignores it). Cube snapshots are never forced.
F32 skForcedProjectionFar(F32 projection_far, S32 force, F32 draw_distance, bool reverse_z, bool cube_snapshot);

// One column of the Develop projection overlay (SKRenderFarProjectionOverlay), in camera-relative metres:
// horizontal distance, bearing in degrees clockwise from north, sideways offset, half width and height, all
// scaled with distance so each subtends the same angle. Columns come in pairs, the nearer first.
struct SKFarOverlayColumn
{
    F32 mDistance = 0.f;
    F32 mBearing = 0.f;
    F32 mOffset = 0.f;
    F32 mHalfWidth = 0.f;
    F32 mHeight = 0.f;
    bool mNearer = false;
};

// The overlay's pairs from 500 m to 100 km, one bearing each: the farther column is 2% further out and
// shifted half a width, so the nearer one must hide part of it if depth order holds at that distance.
const std::vector<SKFarOverlayColumn>& skFarOverlayColumns();
