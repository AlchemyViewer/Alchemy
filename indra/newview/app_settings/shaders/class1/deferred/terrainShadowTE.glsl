/**
 * @file terrainShadowTE.glsl
 *
 * The terrain's evaluation stage for the shadow pass: the same surface the
 * lit stages place, from the same terrainSurface body, and nothing else.
 * With the same control stage and the same origin, the depth the shadow
 * map holds is the surface the lit pass shades.
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

layout(quads, fractional_odd_spacing, ccw) in;

// terrainSurface.glsl
vec2  terrain_patch_xy();
float terrain_height(vec2 p_region);

void main()
{
    vec2 xy = terrain_patch_xy();
    gl_Position = modelview_projection_matrix * vec4(xy, terrain_height(xy), 1.0);
}
