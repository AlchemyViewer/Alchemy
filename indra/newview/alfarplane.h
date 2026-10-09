/**
 * @file alfarplane.h
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

#pragma once

#include "stdtypes.h"
#include "v2math.h"

#include <limits>
#include <vector>

/// The main view keeps four distances apart, each read by its own code:
///
/// - The draw distance (RenderFarClip, the camera's far plane) is a content budget: the cull sphere, the
///   interest list, the object cache, fog, shadow splits, the minimap wedge. Its ceiling follows the depth
///   convention only because forward 24-bit depth cannot hold more.
/// - The projection's far plane says how depth is stored and nothing else. Reverse-Z float depth holds
///   near / distance at any range, so there it is infinite; forward-Z and probe captures keep a finite one.
/// - The sky lies on the far plane and writes no depth, so it is wherever the depth buffer still holds the
///   cleared value, under either projection. No distance places it.
/// - Terrain and water, whose partitions ignore the draw distance, reach as far as the projection lets them:
///   under an infinite one, terrain to TERRAIN_REACH and water to the horizon.
namespace ALFarPlane
{

/// The far plane of an infinite projection.
constexpr F32 INFINITE_FAR = std::numeric_limits<F32>::infinity();

bool isInfinite(F32 projection_far);

/// How far terrain is drawn once an infinite projection no longer bounds it.
constexpr F32 TERRAIN_REACH = 8192.f;

/// How far edge (void) water stretches past the loaded regions under an infinite projection. The horizon from
/// 4 km up, the top of a region, is 243.9 km away with standard refraction (225.8 km geometric), so this
/// reaches it from any height a region allows; its vertices stay within about 3 cm.
constexpr F32 EDGE_WATER_STRETCH = 256000.f;

/// The edge water stretch and the wave clamp a finite projection keeps.
constexpr F32 FINITE_EDGE_WATER_STRETCH = 2048.f;
constexpr F32 FINITE_WAVE_CLAMP = 2560.f;

/// The highest draw distance the depth convention supports: MAX_FAR_CLIP under reverse-Z, whose float depth
/// keeps its precision there, FORWARD_Z_MAX_FAR_CLIP under forward 24-bit depth.
F32 drawDistanceCeiling(bool reverse_z);

/// The draw distance every consumer sees: the RenderFarClip setting held to drawDistanceCeiling.
F32 clampDrawDistance(F32 setting, bool reverse_z);

/// AlchemyRenderFarPlaneForce, a debug A/B over projectionFar's choice for the main view.
enum EForce : S32
{
    FORCE_AUTOMATIC = 0,
    FORCE_FINITE    = 1, ///< the finite plane the viewer used before: twice the draw distance, never under FINITE_PROJECTION_FAR
    FORCE_INFINITE  = 2, ///< infinite; forward-Z cannot hold it and ignores the force
};

/// The projection's far plane: INFINITE_FAR under reverse-Z, FINITE_PROJECTION_FAR under forward-Z and in probe
/// captures, which no force changes.
F32 projectionFar(bool reverse_z, bool cube_snapshot, S32 force = FORCE_AUTOMATIC, F32 draw_distance = 0.f);

/// The window depth LLViewerCamera::updateFrustumPlanes takes the far corners at: MAX_RECONSTRUCT_DISTANCE's under
/// an infinite projection, which has no far plane, and the projection's own far depth otherwise.
F32 frustumFarWindowDepth(bool infinite, F32 near_plane, F32 window_far);

/// The sphere terrain is culled to: TERRAIN_REACH under an infinite projection, 0 (unbounded) otherwise.
F32 terrainReach(F32 projection_far);

/// How far edge water stretches past the loaded regions.
F32 edgeWaterStretch(bool infinite);

/// Whether water is drawn from a camera this high: below FINITE_PROJECTION_FAR under a finite projection, which
/// has clipped the water away from higher, and while the edge water stretches under an infinite one.
bool waterVisibleFrom(F32 camera_z, F32 water_height, F32 projection_far);

/// The water shaders' far terms (waterFar). A finite projection keeps the defaults, which leave the shaders as
/// they were: waves clamped at FINITE_WAVE_CLAMP and no edge fade.
struct WaterFar
{
    F32 mWaveClamp = FINITE_WAVE_CLAMP; ///< horizontal distance past which wave coordinates stop following the surface
    F32 mEdgeFade  = 0.f;               ///< the edge water stretch, over whose last 15% the water fades
};

/// rim_min and rim_max are the edge water's outer rectangle relative to the camera, horizontally, and have_rim
/// whether there is one. Under an infinite projection the waves follow the surface to the rim's farthest
/// corner, so no stretch of water shares one wave coordinate, and the water fades out toward the rim.
WaterFar waterFar(F32 projection_far, bool have_rim, const LLVector2& rim_min, const LLVector2& rim_max);

/// One column of the Develop far plane overlay (ALFarPlaneOverlay), in camera-relative metres: horizontal
/// distance, bearing in degrees clockwise from north, sideways offset, half width and height, all scaled with
/// distance so each subtends the same angle.
struct OverlayColumn
{
    F32  mDistance  = 0.f;
    F32  mBearing   = 0.f;
    F32  mOffset    = 0.f;
    F32  mHalfWidth = 0.f;
    F32  mHeight    = 0.f;
    bool mNearer    = false;
};

/// The overlay's pairs from 500 m to 100 km, the nearer first, one bearing each: the farther column is 2%
/// further out and shifted half a width, so the nearer one must hide part of it if depth order holds there.
const std::vector<OverlayColumn>& overlayColumns();

} // namespace ALFarPlane
