/**
 * @file class1\deferred\terrainTE.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

layout(quads, fractional_odd_spacing, ccw) in;

out vec3 pos;
out vec3 vary_normal;
out vec4 vary_texcoord0;
out vec4 vary_texcoord1;
out vec2 vary_region_uv;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
out vec4 vary_texcoord_side; // The yz projection's uv, then the xz projection's
#endif

uniform vec4 object_plane_s;
uniform vec4 object_plane_t;
uniform float region_scale;

// terrainSurface.glsl
vec2 terrain_patch_xy();
vec3 terrain_surface(vec2 p_region, out vec3 n);
vec2 terrain_composition(vec2 p_region);

vec2 texgen_object(vec4 vpos, mat4 mat, vec4 tp0, vec4 tp1)
{
    vec4 tcoord;

    tcoord.x = dot(vpos, tp0);
    tcoord.y = dot(vpos, tp1);
    tcoord.z = 0;
    tcoord.w = 1;

    tcoord = mat * tcoord;

    return tcoord.xy;
}

void main()
{
    vec2 xy = terrain_patch_xy();
    vec3 normal;
    vec3 position = terrain_surface(xy, normal);

    //transform vertex
    vec4 pre_pos = vec4(position.xyz, 1.0);
    vec4 t_pos = modelview_projection_matrix * pre_pos;

    gl_Position = t_pos;
    pos = (modelview_matrix*pre_pos).xyz;

    vary_normal = normalize(normal_matrix * normal);

    // Transform and pass tex coords
    vary_texcoord0.xy = texgen_object(pre_pos, texture_matrix0, object_plane_s, object_plane_t);
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    // The side projections tile at the plane's scale and keep the origin phase of the axis
    // they share with it, so they meet across region borders as the top one does. Height
    // needs no phase: a region's origin is at z = 0, so region z is world z.
    float scale = object_plane_s.x;
    vec2 phase = vec2(object_plane_s.w, object_plane_t.w);
    vary_texcoord_side.xy = vec2(position.y * scale + phase.y, position.z * scale);
    vary_texcoord_side.zw = vec2(position.x * scale + phase.x, position.z * scale);
#endif

    vec2 t = terrain_composition(xy);

    vary_texcoord0.zw = t.xy;
    vary_texcoord1.xy = t.xy-vec2(2.0, 0.0);
    vary_texcoord1.zw = t.xy-vec2(1.0, 0.0);

    vary_region_uv = xy / region_scale;
}
