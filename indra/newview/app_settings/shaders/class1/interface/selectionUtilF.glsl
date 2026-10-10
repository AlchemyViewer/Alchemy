/**
 * @file selectionUtilF.glsl
 * @brief What the selection outline's passes (ALSelectionOutline) read the id target with, linked into each.
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

// An id texel: the id in .rg, low byte then high, 0 for none; in .b whether the outline draws the surface, 1
// visible, 0.5 hidden and drawn dimmed, 0 hidden and left out; in .a the priority, 0 the highest, below 128, and
// 128 more on a texel the jump pass marks as the near side of a jump in its object's surface.

int decodeId(vec4 texel)
{
    return int(texel.r * 255.0 + 0.5) + (int(texel.g * 255.0 + 0.5) << 8);
}

int decodePriority(vec4 texel)
{
    return int(texel.a * 255.0 + 0.5) & 127;
}

bool isJumpFront(vec4 texel)
{
    return int(texel.a * 255.0 + 0.5) >= 128;
}

bool isDrawn(vec4 texel)
{
    return texel.b > 0.25;
}

bool isVisible(vec4 texel)
{
    return texel.b > 0.75;
}

// Distance in front of the eye of a stored depth, through the projection loaded for the pass. The cleared depth of
// an infinite projection unprojects to w = 0, floored as deferredUtil.glsl floors it.
float eyeDistance(float depth)
{
#ifdef REVERSE_Z
    float ndc_z = depth;
#else
    float ndc_z = depth * 2.0 - 1.0;
#endif
    vec4 pos = inv_proj * vec4(0.0, 0.0, ndc_z, 1.0);
    return -pos.z / max(pos.w, 0.000001);
}
