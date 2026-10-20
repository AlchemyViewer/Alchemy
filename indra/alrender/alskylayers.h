/**
 * @file alskylayers.h
 * @brief What the sky's composite takes of the layers it works out along each view ray from a few values a frame: the
 *        sun's and moon's quads.
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

#pragma once

#include "stdtypes.h"
#include "v3math.h"

#include <array>

class LLQuaternion;

/// The sky's layers as the composite takes them (deferred/skyBodiesF.glsl): no GL and no settings, so LLVOSky, the
/// pass that binds the layers and the GL tests share one definition.
namespace ALSkyLayers
{

/// A sun's or moon's quad about the camera, in the agent's frame: corners 0 to 3 at (-right, +up), (-right, -up),
/// (+right, +up) and (+right, -up), texture coordinates (0, 1), (0, 0), (1, 1) and (1, 0). A parallelogram, its right
/// and up axes square to each other.
using Corners = std::array<LLVector3, 4>;

/// The quad of a body whose direction is @a rotation's turn of +x, centred @a distance from the camera along it, its
/// half-extents @a scale times @a disk_radius times the distance (times HEAVENLY_BODY_FACTOR, 0.1, as LLVOSky built
/// it): wider by up to 30% and taller by up to 20% as the body nears the horizon.
Corners bodyCorners(const LLQuaternion& rotation, F32 distance, F32 scale, F32 disk_radius);

/// The moon's opacity as it nears the horizon, from @a moon_z, the agent's z of its direction: four times its square,
/// to 1, the pre-EEP fade the moon's program restored; 1 below the horizon, where the moon is not drawn.
F32 moonFade(F32 moon_z);

/// How far below its quad the sun disc was drawn, in metres down the agent's z: the sun's vertex stage lowered every
/// corner by it.
constexpr F32 SUN_DROP = 50.f;

/// What the composite's ray test takes of a quad (deferred/skyBodiesF.glsl's skyQuadCoord), the quad moved by
/// @a offset in the agent's frame: its centre, then its right and up half-extents each over its squared length, in the
/// dome's frame (north, up, east). A ray hits the quad where both of its offset's dot products with those axes lie in
/// [-1, 1].
std::array<LLVector3, 3> rayQuad(const Corners& corners, const LLVector3& offset = LLVector3::zero);

/// The texture coordinates, unclipped, where @a dir, a direction from the camera in the dome's frame, meets the plane
/// of @a quad (rayQuad's); false, and the coordinates unset, where it runs parallel to the plane or away from it. A CPU
/// mirror of skyQuadCoord, for the tests.
bool quadCoord(const std::array<LLVector3, 3>& quad, const LLVector3& dir, F32& u, F32& v);

} // namespace ALSkyLayers
