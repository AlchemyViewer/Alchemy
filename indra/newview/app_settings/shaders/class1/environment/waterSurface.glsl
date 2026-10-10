/**
 * @file waterSurface.glsl
 * @brief The water's field (ALWaterSurface, ALWaterField): where its quads lie, the water height over any point of the
 *        plane, and the wave coordinates of a point.
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

// Texel (i, j) is cell (i, j) of the square counted from its south-west corner: r the water height, g its flags.
uniform sampler2D waterCellMap;
// xy: the square's south-west corner in agent space; z: a cell's width; w: the cells a side.
uniform vec4 waterCellSquare;
// Past the square, where the rings lie: the agent region's water height.
uniform float waterDefaultHeight;
// x: the square's half-size, S_0; y: the rings; z: the rim, where the last ring ends each way from the square's
// centre; w: 1 where the void (the cells no region holds, and the rings) is drawn, 0 where it is hidden.
uniform vec4 waterRings;

// x: the wave clamp, the horizontal distance past which wave coordinates stop following the surface.
uniform vec2 waterFar;
// Each layer's phase at the camera: its scroll plus the camera's place in it, and the sweep's (ALWaterField::wavePhases).
uniform vec2 waterBigPhase;
uniform vec4 waterLittlePhase;
uniform vec2 waterSweepPhase;

// The water height under an agent-space point: its cell's, or past the square the agent region's.
float water_height_at(vec2 xy)
{
    vec2 cell = floor((xy - waterCellSquare.xy) / waterCellSquare.z);
    if (any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, vec2(waterCellSquare.w))))
    {
        return waterDefaultHeight;
    }
    return texelFetch(waterCellMap, ivec2(cell), 0).r;
}

// The point of the plane `at` cells from the square's south-west corner, in agent space. Every point of the cells and
// ring 0's inner edges comes through here from a whole number of cells, and the offset from the centre is then exact,
// so a point two parts share is the same point in both.
vec2 water_square_point(vec2 at)
{
    float side = waterCellSquare.w;
    vec2 centre = waterCellSquare.xy + 0.5 * side * waterCellSquare.z;
    return centre + (at - 0.5 * side) * waterCellSquare.z;
}

// The square's centre, from which the rings are placed.
vec2 water_square_centre()
{
    return waterCellSquare.xy + 0.5 * waterCellSquare.w * waterCellSquare.z;
}

// Whether the cell whose texel is `texel` is drawn at all: a region's, or the void with the void drawn.
bool water_cell_drawn(vec2 texel)
{
    return waterRings.w > 0.0 || (int(texel.g) & 1) != 0;
}

// A unit square's point `t` turned about its middle so its south edge becomes edge `edge`: 0 south, 1 east, 2 north,
// 3 west, a quarter turn counter-clockwise each, which keeps a triangle's winding. Its coordinates stay exact.
vec2 water_turn(vec2 t, int edge)
{
    if (edge == 1)
    {
        return vec2(1.0 - t.y, t.x);
    }
    if (edge == 2)
    {
        return vec2(1.0 - t.x, 1.0 - t.y);
    }
    if (edge == 3)
    {
        return vec2(t.y, 1.0 - t.x);
    }
    return t;
}

// An offset from the centre turned as water_turn turns a cell's point, about the centre itself.
vec2 water_turn_offset(vec2 o, int edge)
{
    if (edge == 1)
    {
        return vec2(-o.y, o.x);
    }
    if (edge == 2)
    {
        return -o;
    }
    if (edge == 3)
    {
        return vec2(o.y, -o.x);
    }
    return o;
}

// Between lo and hi at t, which is 0, 1/2 or 1 each way: exactly lo, exactly hi, or their middle.
vec2 water_between(vec2 lo, vec2 hi, vec2 t)
{
    return vec2(t.x <= 0.0 ? lo.x : (t.x >= 1.0 ? hi.x : 0.5 * (lo.x + hi.x)),
                t.y <= 0.0 ? lo.y : (t.y >= 1.0 ? hi.y : 0.5 * (lo.y + hi.y)));
}

// Vertex `vertex` of part `part` of the field (ALWaterField::Draw), instance `instance`, in agent space; false where the
// instance is not drawn, all of whose vertices are then one point. Every vertex two parts share is worked from the same
// exact multiples, and none lies on an edge it is not a corner of, so the field has no T-junction for a crack to open
// along.
//  0, the cells: cell i + side * j, a four-vertex strip at its region's water height; not a void cell with the void
//     hidden.
//  1, the rings: twelve quads a ring as ALWaterField::ringQuad lays them out, at the agent region's water height, each
//     three triangles, those along a finer ring's edge split at its middle, where the finer ring has a corner. Ring
//     0's quads along the square are the zippers'.
//  2, ring 0's four sides, south, east, north and west: the square's side fanned from the side's outer corners and
//     middle, so its edge meets every cell corner along the square.
bool water_vertex(int part, int instance, int vertex, out vec3 pos)
{
    float side = waterCellSquare.w;
    int n = int(side);
    if (part == 0)
    {
        ivec2 cell = ivec2(instance % n, instance / n);
        vec2 texel = texelFetch(waterCellMap, cell, 0).rg;
        bool drawn = water_cell_drawn(texel);
        vec2 corner = drawn ? vec2(float(vertex & 1), float(vertex >> 1)) : vec2(0.0);
        pos = vec3(water_square_point(vec2(cell) + corner), texel.r);
        return drawn;
    }

    vec2 centre = water_square_centre();
    float rim = waterRings.z;
    bool void_drawn = waterRings.w > 0.0;

    if (part == 2)
    {
        // South side, from the centre: its inner edge the square's, at the cell corners c_m, m = 0 to n, and its outer
        // edge at its two ends and middle, o_0, o_1 and o_2, as ring 0's quads place them. A fan from o_0 over the
        // west half of the cells, a triangle under the middle cell, a fan from o_1 over the rest and a triangle under
        // the last cell.
        int cells = n;
        int half_cells = (n - 1) / 2;
        float s = waterRings.x;
        float inner = (0.0 - 0.5 * side) * waterCellSquare.z;
        float outer = clamp(-2.0 * s, -rim, rim);
        int tri = vertex / 3;
        int k = vertex - tri * 3;
        vec2 o;
        if (tri < half_cells)
        {
            o = k == 0 ? vec2(-s, outer) : vec2((float(tri + (k == 1 ? 1 : 0)) - 0.5 * side) * waterCellSquare.z, inner);
        }
        else if (tri == half_cells)
        {
            o = k == 0 ? vec2(-s, outer) : (k == 1 ? vec2(0.0, outer) : vec2((float(half_cells) - 0.5 * side) * waterCellSquare.z, inner));
        }
        else if (tri <= cells)
        {
            int m = tri - 1;
            o = k == 0 ? vec2(0.0, outer) : vec2((float(m + (k == 1 ? 1 : 0)) - 0.5 * side) * waterCellSquare.z, inner);
        }
        else
        {
            o = k == 0 ? vec2(0.0, outer) : (k == 1 ? vec2(s, outer) : vec2((float(cells) - 0.5 * side) * waterCellSquare.z, inner));
        }
        pos = vec3(centre + (void_drawn ? water_turn_offset(o, instance) : vec2(0.0)), waterDefaultHeight);
        return void_drawn;
    }

    int ring = instance / 12;
    int quad = instance - ring * 12;
    ivec2 at;
    // The edge a finer ring, or for ring 0 the square, runs along: the north edge of the bottom row's middle quads,
    // the east of the west column's, the west of the east column's and the south of the top row's; none for corners.
    int edge = -1;
    if (quad < 4)
    {
        at = ivec2(quad, 0);
        edge = (quad == 1 || quad == 2) ? 2 : -1;
    }
    else if (quad < 8)
    {
        at = ivec2((quad & 1) * 3, 1 + (quad - 4) / 2);
        edge = (quad & 1) == 0 ? 1 : 3;
    }
    else
    {
        at = ivec2(quad - 8, 3);
        edge = (quad == 9 || quad == 10) ? 0 : -1;
    }
    float s = waterRings.x * float(1 << ring);
    vec2 lo = clamp((vec2(at) - 2.0) * s, -rim, rim);
    vec2 hi = clamp((vec2(at) - 1.0) * s, -rim, rim);
    bool drawn = void_drawn && !(ring == 0 && edge >= 0);
    // Three triangles: with an edge to split, that edge south, its middle m, the west triangle, the middle one and the
    // east one; without, two halves and an empty third.
    int tri = vertex / 3;
    int k = vertex - tri * 3;
    vec2 t;
    if (edge >= 0)
    {
        if (tri == 0)
        {
            t = k == 0 ? vec2(0.0, 0.0) : (k == 1 ? vec2(0.5, 0.0) : vec2(0.0, 1.0));
        }
        else if (tri == 1)
        {
            t = k == 0 ? vec2(0.5, 0.0) : (k == 1 ? vec2(1.0, 1.0) : vec2(0.0, 1.0));
        }
        else
        {
            t = k == 0 ? vec2(0.5, 0.0) : (k == 1 ? vec2(1.0, 0.0) : vec2(1.0, 1.0));
        }
        t = water_turn(t, edge);
    }
    else
    {
        if (tri == 0)
        {
            t = k == 0 ? vec2(0.0, 0.0) : (k == 1 ? vec2(1.0, 0.0) : vec2(1.0, 1.0));
        }
        else if (tri == 1)
        {
            t = k == 0 ? vec2(0.0, 0.0) : (k == 1 ? vec2(1.0, 1.0) : vec2(0.0, 1.0));
        }
        else
        {
            t = vec2(0.0);
        }
    }
    pos = vec3(centre + (drawn ? water_between(lo, hi, t) : vec2(0.0)), waterDefaultHeight);
    return drawn;
}

// The wave coordinates of a point `r` from the camera, horizontally, as far as the wave clamp: the big layer's, and
// the little map's xy and zw reads. LL's sweep bends x before the layers scale it. The phases hold every term of the
// camera's own position, so nothing large is worked here, and a point keeps its coordinates however the camera and the
// agent's region move.
void water_wave_coords(vec2 r, out vec2 big, out vec4 little)
{
    float d = max(length(r), 0.001);
    r *= min(d, waterFar.x) / d;
    vec2 v = r;
    v.x += (cos(r.x*0.08 + waterSweepPhase.x) + sin(r.y*0.02 + waterSweepPhase.y))*6.0;
    big = v * vec2(0.04,0.04) + waterBigPhase;
    little.xy = v * vec2(0.45, 0.9) + waterLittlePhase.xy;
    little.zw = v * vec2(0.1, 0.2) + waterLittlePhase.zw;
}
