/**
 * @file alskydome.h
 * @brief The WindLight sky dome's mesh, in the dome's frame.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

#include "llstrider.h"
#include "stdtypes.h"

class LLVector2;
class LLVector3;

/// The WindLight sky dome's mesh, in the dome's frame (north, up, east): rings of a sphere about the origin, from the
/// zenith down past the camera's level, which the sky program shaded from the camera's point inside the sphere
/// (windlight/windlightDomeF.glsl's windlightDomeAt). The last rings are a skirt: the third from last lowered 2048 m,
/// the next collapsed onto the axis below it, and the last two back out on the sphere, so the strips overlap below the
/// camera's level and the later band drawn covers the earlier. Triangle strips of SLICES vertices a ring. The sky's
/// layers were drawn over it before its composite worked them out along the view ray; the GL tests draw it, with the
/// programs that drew them (tests/skyraster), beside the composite.
namespace ALSkyDome
{
    /// The stacks the dome has, and the vertices a ring has. Any fewer and the facets show.
    constexpr U32 STACKS = 18;
    constexpr U32 SLICES = 2 * STACKS;

    /// The vertices and indices buildStrips writes for @a stacks stacks.
    constexpr U32 vertexCount(U32 stacks) { return SLICES * (stacks + 1); }
    constexpr U32 indexCount(U32 stacks) { return 1 + stacks * (2 + 2 * SLICES); }

    /// The polar angle of ring @a ring, from the zenith: PI/8 (1 - (1 - t^4)^2) at t = ring / STACKS, which gathers the
    /// rings toward the horizon.
    F32 ringAngle(U32 ring);

    /// Stacks [@a begin_stack, @a end_stack) of a dome of @a radius: rings begin_stack + 1 to end_stack + 1, their
    /// positions, their planar texture coordinates ((1 - east) / 2, (1 - north) / 2) and one triangle strip over them.
    void buildStrips(U32 begin_stack, U32 end_stack, F32 radius, LLStrider<LLVector3>& vertices,
                     LLStrider<LLVector2>& tex_coords, LLStrider<U16>& indices);
} // namespace ALSkyDome
