/**
 * @file class1\deferred\pbrterrainTE.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

layout(quads, fractional_odd_spacing, ccw) in;

// terrainSurface.glsl
vec2 terrain_patch_xy();
vec3 terrain_surface(vec2 p_region, out vec3 n);
vec2 terrain_composition(vec2 p_region);

out vec3 vary_position;
out vec3 vary_normal;
// Region-local metres. Every projection's uv is an affine map of this, and the
// fragment stage applies the maps itself: a vertex carrying the slices ready-made
// would carry forty floats for the four materials, each interpolated per fragment
// and set up per triangle, where the position is three and the maps are uniforms.
out vec3 vary_region_position;

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
out vec4 vary_texcoord0;
out vec4 vary_texcoord1;
#endif

void main()
{
    vec2 xy = terrain_patch_xy();
    vec3 normal;
    vec3 position = terrain_surface(xy, normal);

    //transform vertex
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
    vary_position = (modelview_matrix*vec4(position.xyz, 1.0)).xyz;

    vary_normal = normalize(normal_matrix * normal);
    vary_region_position = position;

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    vec2 tc = terrain_composition(xy);
    vary_texcoord0.zw = tc.xy;
    vary_texcoord1.xy = tc.xy-vec2(2.0, 0.0);
    vary_texcoord1.zw = tc.xy-vec2(1.0, 0.0);
#endif
}
