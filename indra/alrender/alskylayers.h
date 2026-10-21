/**
 * @file alskylayers.h
 * @brief What the sky's composite takes of the layers it works out along each view ray from a few values a frame: the
 *        sun's and moon's quads, the sun bloom's square, and the meteors' streaks on the screen.
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
#include "v4coloru.h"
#include "v4math.h"

#include <array>
#include <vector>

class LLMatrix4a;
class LLQuaternion;

/// The sky's layers as the composite takes them (deferred/skyBodiesF.glsl, deferred/skyMeteorsF.glsl): no GL and no
/// settings, so LLVOSky, LLVOWLSky, the pass that binds the layers and the GL tests share one definition.
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

/// The sun bloom's square about the sun's quad @a sun: centred on it, a half-extent every way of four times the disc's
/// half-width, along the disc's axes. The square the sun's halo was drawn in before WindLight
/// (LLVOSky::updateSunHaloGeometry at the open-source import).
Corners bloomCorners(const Corners& sun);

/// The moon's opacity as it nears the horizon, from @a moon_z, the agent's z of its direction: four times its square,
/// to 1, the pre-EEP fade the moon's program restored; 1 below the horizon, where the moon is not drawn.
F32 moonFade(F32 moon_z);

/// How far below its quad the sun disc was drawn, in metres down the agent's z: the sun's vertex stage lowered every
/// corner by it. The bloom is lowered with it, so the two stay centred on each other.
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

/// The most meteors LLVOWLSky keeps, and the composite's array of them.
constexpr U32 MAX_METEORS = 64;

/// The vec4s a meteor takes in the composite's skyMeteorData. deferred/skyMeteorsF.glsl's SKY_METEOR_VECTORS must equal
/// it, and its SKY_MAX_METEORS MAX_METEORS.
constexpr U32 METEOR_VECTORS = 3;

/// The width the meteors draw at, in pixels each side of the streak for a meteor of width 1.
constexpr F32 METEOR_WIDTH_PIXELS = 1.5f;

/// A meteor as LLVOWLSky ticks it for a frame, about the camera in the agent's frame: the streak's midpoint and half
/// of it toward the head, its colour times its envelope (sRGB) and its width class as eight bits each, the width a
/// quarter in alpha, and its width class as it is.
struct Meteor
{
    LLVector3 mMidpoint;
    LLVector3 mHalf;
    LLColor4U mColour;
    F32       mWidth = 1.f;
};

/// The streaks of @a meteors on the screen, into @a out as skyMeteorData takes them, METEOR_VECTORS a streak:
/// - the midpoint's normalized device coordinates, and the first row of the inverse of the streak's frame (the half
///   streak toward the head and the half width across it, each in normalized device coordinates);
/// - that inverse's second row, the width class the quad reaches across, and the one its profile is divided by, read
///   back from eight bits as the vertex stage read it;
/// - the colour, linear as the vertex stage made it (sRGB to the power 2.2).
///
/// Each streak is a quad on the screen of @a width_px pixels a side for each unit of width, through @a mvp (the
/// agent's frame about the camera to clip space) onto a @a width by @a height viewport, as the meteors' vertex stage
/// placed it. A streak with an end at or behind the camera, one wholly off the screen, and one with no area are left
/// out. Returns how many are in @a out.
U32 packMeteors(const std::vector<Meteor>& meteors, const LLMatrix4a& mvp, F32 width, F32 height, F32 width_px,
                std::vector<LLVector4>& out);

} // namespace ALSkyLayers
