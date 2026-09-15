/**
 * @file terrainSurface.glsl
 *
 * The terrain surface, evaluated per tessellated vertex from the region's
 * height map. This is the one body every terrain evaluation stage links --
 * lit, PBR and shadow -- so all of them place a vertex identically.
 *
 * With terrain_smoothing off the height is the surface the simulator
 * collides against: LLSurface::resolveHeightRegion's two triangles per cell,
 * split on the (i,j)->(i+1,j+1) diagonal, linear within the triangle. The
 * normal is LLSurfacePatch::calcNormal's, the cross product of the two
 * diagonals of the +-2 grid quad, evaluated on that same surface so it is
 * the vertex normal exactly at a grid point and varies continuously between
 * two: the lighting the terrain has always had, which hides the metre grid.
 *
 * With it on the height is a monotone bicubic through the same samples: a
 * Hermite patch per cell whose corner tangents are the neighbouring secants'
 * mean, limited the Fritsch-Carlson way -- to three times the smaller secant,
 * and to zero where the two disagree -- so it is C1, passes through every
 * sample, and never leaves the range of the cell's four corners. A step
 * becomes an S-curve inside its cell, not a lip above it, which a Catmull-Rom
 * through a thirty-metre cliff grows metres tall. The normal is the patch's
 * analytic gradient. The CPU's copy of that surface is LLSurface::smoothHeight,
 * through the same arithmetic in the same order; the two must agree.
 *
 * The fragment stages choose their projections by terrain_facet, the smooth
 * surface's normal whichever surface is drawn. It is continuous, so no
 * triangle edge shows through a blend of projections, and its limited
 * tangents hold it upright to the lip of a cliff and turn it inside the
 * face -- where the linear normal's stencil reaches two grids past the
 * crease, and a drawn triangle's own plane steps at every tessellation edge.
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
uniform int   terrain_smoothing;     // 0 the collision surface, 1 the smooth one through it

const int TERRAIN_APRON = 2;

#ifdef TESS_EVALUATION_SHADER
// The point of the patch this invocation evaluates, in region-local metres.
// Corners arrive in the order the patch buffer holds them: (x0,y0) (x1,y0)
// (x1,y1) (x0,y1), so u runs along x and v along y.
vec2 terrain_patch_xy()
{
    vec2 bottom = mix(gl_in[0].gl_Position.xy, gl_in[1].gl_Position.xy, gl_TessCoord.x);
    vec2 top    = mix(gl_in[3].gl_Position.xy, gl_in[2].gl_Position.xy, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}
#endif

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

// The tangent at the middle sample of three, limited so the cubic between it
// and either neighbour stays monotone.
float terrain_limited_slope(float left, float mid, float right)
{
    float dl = mid - left;
    float dr = right - mid;
    if (dl * dr <= 0.0)
    {
        return 0.0;
    }
    float m = 0.5 * (dl + dr);
    float bound = 3.0 * min(abs(dl), abs(dr));
    return clamp(m, -bound, bound);
}

// Cubic Hermite basis at t -- value weights of the two ends in xy, tangent
// weights in zw -- and its derivative.
vec4 terrain_hermite(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4( 2.0 * t3 - 3.0 * t2 + 1.0,
                -2.0 * t3 + 3.0 * t2,
                       t3 - 2.0 * t2 + t,
                       t3 -       t2);
}

vec4 terrain_hermite_d(float t)
{
    float t2 = t * t;
    return vec4( 6.0 * t2 - 6.0 * t,
                -6.0 * t2 + 6.0 * t,
                 3.0 * t2 - 4.0 * t + 1.0,
                 3.0 * t2 - 2.0 * t);
}

// Height and gradient (per grid) of the monotone bicubic at g. The cell's
// corner values and limited x and y tangents, with no cross term, in a
// tensor Hermite patch; then the value is held to the corners' range for the
// saddle cells where the two axes' tangents can still add past it.
float terrain_height_smooth(vec2 g, out vec2 gradient)
{
    ivec2 i = ivec2(floor(g));
    vec2  f = g - vec2(i);

    // Rows j and j+1, four samples each; the outer two feed the x tangents.
    float r0[4], r1[4];
    for (int k = 0; k < 4; ++k)
    {
        r0[k] = terrain_sample(i + ivec2(k - 1, 0));
        r1[k] = terrain_sample(i + ivec2(k - 1, 1));
    }
    // Columns i and i+1, the samples above and below the cell.
    float c0m = terrain_sample(i + ivec2(0, -1));
    float c0p = terrain_sample(i + ivec2(0,  2));
    float c1m = terrain_sample(i + ivec2(1, -1));
    float c1p = terrain_sample(i + ivec2(1,  2));

    float f00 = r0[1], f10 = r0[2], f01 = r1[1], f11 = r1[2];
    float fx00 = terrain_limited_slope(r0[0], f00, f10);
    float fx10 = terrain_limited_slope(f00, f10, r0[3]);
    float fx01 = terrain_limited_slope(r1[0], f01, f11);
    float fx11 = terrain_limited_slope(f01, f11, r1[3]);
    float fy00 = terrain_limited_slope(c0m, f00, f01);
    float fy01 = terrain_limited_slope(f00, f01, c0p);
    float fy10 = terrain_limited_slope(c1m, f10, f11);
    float fy11 = terrain_limited_slope(f10, f11, c1p);

    vec4 hu = terrain_hermite(f.x), hv = terrain_hermite(f.y);
    vec4 du = terrain_hermite_d(f.x), dv = terrain_hermite_d(f.y);

    // Value terms, then the x-tangent terms, then the y-tangent terms.
    float h   = hu.x * (hv.x * f00 + hv.y * f01) + hu.y * (hv.x * f10 + hv.y * f11)
              + hu.z * (hv.x * fx00 + hv.y * fx01) + hu.w * (hv.x * fx10 + hv.y * fx11)
              + hu.x * (hv.z * fy00 + hv.w * fy01) + hu.y * (hv.z * fy10 + hv.w * fy11);
    float dhx = du.x * (hv.x * f00 + hv.y * f01) + du.y * (hv.x * f10 + hv.y * f11)
              + du.z * (hv.x * fx00 + hv.y * fx01) + du.w * (hv.x * fx10 + hv.y * fx11)
              + du.x * (hv.z * fy00 + hv.w * fy01) + du.y * (hv.z * fy10 + hv.w * fy11);
    float dhy = hu.x * (dv.x * f00 + dv.y * f01) + hu.y * (dv.x * f10 + dv.y * f11)
              + hu.z * (dv.x * fx00 + dv.y * fx01) + hu.w * (dv.x * fx10 + dv.y * fx11)
              + hu.x * (dv.z * fy00 + dv.w * fy01) + hu.y * (dv.z * fy10 + dv.w * fy11);

    gradient = vec2(dhx, dhy);
    return clamp(h, min(min(f00, f10), min(f01, f11)), max(max(f00, f10), max(f01, f11)));
}

float terrain_height(vec2 p_region)
{
    vec2 g = p_region / terrain_grid_scale;
    if (terrain_smoothing != 0)
    {
        vec2 gradient;
        return terrain_height_smooth(g, gradient);
    }
    return terrain_height_linear(g);
}

vec3 terrain_surface(vec2 p_region, out vec3 n)
{
    vec2 g = p_region / terrain_grid_scale;
    if (terrain_smoothing != 0)
    {
        vec2 gradient;
        float h = terrain_height_smooth(g, gradient);
        n = normalize(vec3(-gradient / terrain_grid_scale, 1.0));
        return vec3(p_region, h);
    }
    n = terrain_normal_linear(g);
    return vec3(p_region, terrain_height_linear(g));
}

// The normal a fragment chooses its projections by: the smooth surface's,
// whichever surface is drawn. A blend of projections shows every step in the
// normal it is taken from, and a drawn triangle's own plane steps at every
// tessellation edge -- on the collision surface the tessellation's triangles
// straddle the grid's diagonals besides, so even there the facets are not
// the surface's. This normal is continuous, and its limited tangents keep it
// upright to the lip of a cliff and turn it inside the face.
vec3 terrain_facet(vec2 p_region)
{
    vec2 gradient;
    terrain_height_smooth(p_region / terrain_grid_scale, gradient);
    return normalize(vec3(-gradient / terrain_grid_scale, 1.0));
}

// Composition value and alpha-ramp noise at a point, bilinear between the
// grid samples: at a grid point the values the old vertices carried, and
// between two what the rasteriser interpolated from them.
vec2 terrain_composition(vec2 p_region)
{
    vec2 g = p_region / terrain_grid_scale + float(TERRAIN_APRON) + 0.5;
    return texture(terrain_composition_map, g / vec2(textureSize(terrain_composition_map, 0))).rg;
}
