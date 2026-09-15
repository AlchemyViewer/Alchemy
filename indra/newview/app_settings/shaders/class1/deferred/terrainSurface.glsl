/**
 * @file terrainSurface.glsl
 *
 * The terrain surface, evaluated per tessellated vertex from the region's
 * height map. This is the one body every terrain evaluation stage links --
 * lit, PBR and shadow -- so all of them place a vertex identically.
 *
 * The height is the surface the simulator collides against:
 * LLSurface::resolveHeightRegion's two triangles per cell, split on the
 * (i,j)->(i+1,j+1) diagonal, linear within the triangle. The normal is
 * LLSurfacePatch::calcNormal's, the cross product of the two diagonals of
 * the +-2 grid quad, evaluated on that same surface so it is the vertex
 * normal exactly at a grid point and varies continuously between two.
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

uniform sampler2D terrain_height_map;
uniform sampler2D terrain_composition_map;
uniform float terrain_grid_scale;    // metres per grid

const int TERRAIN_APRON = 2;

// The point of the patch this invocation evaluates, in region-local metres.
// Corners arrive in the order the patch buffer holds them: (x0,y0) (x1,y0)
// (x1,y1) (x0,y1), so u runs along x and v along y.
vec2 terrain_patch_xy()
{
    vec2 bottom = mix(gl_in[0].gl_Position.xy, gl_in[1].gl_Position.xy, gl_TessCoord.x);
    vec2 top    = mix(gl_in[3].gl_Position.xy, gl_in[2].gl_Position.xy, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}

// Grid sample, clamped to the map: a cell's far corner lands one past the
// apron only at the region's edge, where its weight is zero.
float terrain_sample(ivec2 g)
{
    ivec2 last = textureSize(terrain_height_map, 0) - 1 - TERRAIN_APRON;
    return texelFetch(terrain_height_map, clamp(g, ivec2(-TERRAIN_APRON), last) + TERRAIN_APRON, 0).r;
}

float terrain_height_linear(vec2 g)
{
    ivec2 i = ivec2(floor(g));
    vec2  f = g - vec2(i);
    float lb = terrain_sample(i);
    float rb = terrain_sample(i + ivec2(1, 0));
    float lt = terrain_sample(i + ivec2(0, 1));
    float rt = terrain_sample(i + ivec2(1, 1));
    return (f.y > f.x) ? lb + f.y * (lt - lb) + f.x * (rt - lt)
                       : lb + f.x * (rb - lb) + f.y * (rt - rb);
}

vec3 terrain_normal_linear(vec2 g)
{
    float s = 2.0 * terrain_grid_scale;
    vec3 p00 = vec3(-s, -s, terrain_height_linear(g + vec2(-2.0, -2.0)));
    vec3 p01 = vec3(-s,  s, terrain_height_linear(g + vec2(-2.0,  2.0)));
    vec3 p10 = vec3( s, -s, terrain_height_linear(g + vec2( 2.0, -2.0)));
    vec3 p11 = vec3( s,  s, terrain_height_linear(g + vec2( 2.0,  2.0)));
    return normalize(cross(p11 - p00, p01 - p10));
}

float terrain_height(vec2 p_region)
{
    return terrain_height_linear(p_region / terrain_grid_scale);
}

vec3 terrain_surface(vec2 p_region, out vec3 n)
{
    vec2 g = p_region / terrain_grid_scale;
    n = terrain_normal_linear(g);
    return vec3(p_region, terrain_height_linear(g));
}

// Composition value and alpha-ramp noise at a point, bilinear between the
// grid samples: at a grid point the values the old vertices carried, and
// between two what the rasteriser interpolated from them.
vec2 terrain_composition(vec2 p_region)
{
    vec2 g = p_region / terrain_grid_scale + float(TERRAIN_APRON) + 0.5;
    return texture(terrain_composition_map, g / vec2(textureSize(terrain_composition_map, 0))).rg;
}
