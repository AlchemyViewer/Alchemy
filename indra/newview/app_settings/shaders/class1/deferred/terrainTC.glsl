/**
 * @file terrainTC.glsl
 *
 * Tessellation levels for one terrain patch. Every edge's level is a
 * function of its two endpoints, taken from the tessellation origin, and of
 * uniforms all regions share, so the patch on the other side of the edge --
 * in this region or the next -- computes the same value and the seam stays
 * closed. The level falls with distance from the origin, the camera the
 * frame is drawn for. A shadow map or a probe capture also holds an edge to
 * no more segments than its own raster resolves, one per
 * terrain_tess_raster's span of texels or pixels; where that is the coarser,
 * the shadow caster is a coarser cut of the surface the lit pass shades.
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

layout(vertices = 4) out;

uniform sampler2D terrain_height_map;
uniform vec3  terrain_tess_origin;   // the camera, region-local
uniform float terrain_tess_density;  // level = edge length * density / distance; <= 0 pins every level to the grid
uniform float terrain_grid_scale;    // metres per grid
// The pass's projection and view in agent space, taken to a point from the origin, and where it puts the origin: the
// clip position of p is terrain_tess_clip * (p - origin, 0) + terrain_tess_clip_origin, so every region works it from
// the same numbers.
uniform mat4  terrain_tess_clip;
uniform vec4  terrain_tess_clip_origin;
// Half the raster's size over the texels or pixels a segment spans; 0 leaves the level to the distance alone.
uniform vec2  terrain_tess_raster;

const int   TERRAIN_APRON     = 2;
const float TERRAIN_MAX_LEVEL = 64.0;
const float TERRAIN_GRID_LEVEL = 16.0;

vec3 corner(int i)
{
    vec2 p = gl_in[i].gl_Position.xy;
    ivec2 g = ivec2(round(p / terrain_grid_scale)) + TERRAIN_APRON;
    return vec3(p, texelFetch(terrain_height_map, g, 0).r);
}

// Precise, as waterTC's is: a shared edge is worked by two patches at different call sites (one's
// Outer[2] is the other's Outer[0]), which the compiler may otherwise contract or reorder apart by
// an ulp, and an ulp of level moves every vertex along the edge.
float edgeLevel(vec3 a, vec3 b)
{
    if (terrain_tess_density <= 0.0)
    {
        return TERRAIN_GRID_LEVEL;
    }
    precise float len   = distance(a, b);
    precise float d     = distance(0.5 * (a + b), terrain_tess_origin);
    precise float level = len * terrain_tess_density / max(d, 1.0);
    if (terrain_tess_raster.x > 0.0)
    {
        precise vec4 ca = terrain_tess_clip * vec4(a - terrain_tess_origin, 0.0) + terrain_tess_clip_origin;
        precise vec4 cb = terrain_tess_clip * vec4(b - terrain_tess_origin, 0.0) + terrain_tess_clip_origin;
        // An end at or behind a perspective pass's eye has no place on its raster, and the edge keeps the
        // distance's level. An orthographic pass's w is 1.
        if (min(ca.w, cb.w) > 0.0)
        {
            precise vec2 span = (cb.xy / cb.w - ca.xy / ca.w) * terrain_tess_raster;
            level = min(level, length(span));
        }
    }
    return clamp(level, 1.0, TERRAIN_MAX_LEVEL);
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        vec3 c0 = corner(0);
        vec3 c1 = corner(1);
        vec3 c2 = corner(2);
        vec3 c3 = corner(3);
        gl_TessLevelOuter[0] = edgeLevel(c0, c3);   // u = 0
        gl_TessLevelOuter[1] = edgeLevel(c0, c1);   // v = 0
        gl_TessLevelOuter[2] = edgeLevel(c1, c2);   // u = 1
        gl_TessLevelOuter[3] = edgeLevel(c3, c2);   // v = 1
        gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
        gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
    }
}
