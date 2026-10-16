/**
 * @file terrainTC.glsl
 *
 * Tessellation levels for one terrain patch. Every edge's level is a
 * function of its two endpoints and uniforms all regions share, so the
 * patch on the other side of the edge -- in this region or the next --
 * computes the same value and the seam stays closed. Nothing about the
 * viewport enters: the shadow pass, given the same origin, tessellates
 * the same surface the lit pass does.
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

const int   TERRAIN_APRON     = 2;
const float TERRAIN_MAX_LEVEL = 64.0;
const float TERRAIN_GRID_LEVEL = 16.0;

vec3 corner(int i)
{
    vec2 p = gl_in[i].gl_Position.xy;
    ivec2 g = ivec2(round(p / terrain_grid_scale)) + TERRAIN_APRON;
    return vec3(p, texelFetch(terrain_height_map, g, 0).r);
}

float edgeLevel(vec3 a, vec3 b)
{
    if (terrain_tess_density <= 0.0)
    {
        return TERRAIN_GRID_LEVEL;
    }
    float len = distance(a, b);
    float d   = distance(0.5 * (a + b), terrain_tess_origin);
    return clamp(len * terrain_tess_density / max(d, 1.0), 1.0, TERRAIN_MAX_LEVEL);
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
