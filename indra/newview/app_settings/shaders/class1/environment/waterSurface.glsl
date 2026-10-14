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

// The cells drawn as the waves' patches, counted as the cell map counts them: min i, min j, then max i and max j plus
// one. All 0, empty, but in the water's own draws while it has waves: the other draws of the field want every cell.
uniform ivec4 waterPatchRect;

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

// The waves' patches a cell side (ALWaterField::PATCHES_PER_CELL): 32 m of a 256 m cell.
#define WATER_PATCHES 8

// The point of the plane `at` cells from the square's south-west corner, in agent space. Every point of the cells, the
// fringe, the patches and ring 0's inner edges comes through here from a multiple of an eighth of a cell, and the
// offset from the centre is then exact, so a point two parts share is the same point in both.
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

// Whether cell `cell` lies beside the patches' rectangle, along one of its sides, and which of its edges faces the
// rectangle (water_turn's numbering). Never where no patches are drawn.
bool water_fringe(ivec2 cell, out int edge)
{
    edge = 0;
    if (any(greaterThanEqual(waterPatchRect.xy, waterPatchRect.zw)))
    {
        return false;
    }
    bool along_x = cell.x >= waterPatchRect.x && cell.x < waterPatchRect.z;
    bool along_y = cell.y >= waterPatchRect.y && cell.y < waterPatchRect.w;
    if (along_x && cell.y == waterPatchRect.y - 1)
    {
        edge = 2;
        return true;
    }
    if (along_x && cell.y == waterPatchRect.w)
    {
        edge = 0;
        return true;
    }
    if (along_y && cell.x == waterPatchRect.x - 1)
    {
        edge = 1;
        return true;
    }
    if (along_y && cell.x == waterPatchRect.z)
    {
        edge = 3;
        return true;
    }
    return false;
}

// Vertex `vertex` of part `part` of the field (ALWaterField::Draw), instance `instance`, in agent space; false where the
// instance is not drawn, all of whose vertices are then one point. Every vertex two parts share, or a part and the
// waves' patches, is worked from the same exact multiples, and none lies on an edge it is not a corner of, so the field
// has no T-junction for a crack to open along.
//  0, the cells: cell i + side * j, a four-vertex strip at its region's water height; not a void cell with the void
//     hidden, a cell the patches draw, or one beside them, which the fringe draws.
//  1, the rings: twelve quads a ring as ALWaterField::ringQuad lays them out, at the agent region's water height, each
//     three triangles, those along a finer ring's edge split at its middle, where the finer ring has a corner. Ring
//     0's quads along the square are the zippers'.
//  2, ring 0's four sides, south, east, north and west: the square's side fanned from the side's outer corners and
//     middle, so its edge meets every cell corner along the square.
//  3, the fringe: the cells beside the patches, each fanned from its far corners onto the patch corners along the edge
//     it shares with them.
bool water_vertex(int part, int instance, int vertex, out vec3 pos)
{
    float side = waterCellSquare.w;
    int n = int(side);
    if (part == 0)
    {
        ivec2 cell = ivec2(instance % n, instance / n);
        vec2 texel = texelFetch(waterCellMap, cell, 0).rg;
        bool patched = all(greaterThanEqual(cell, waterPatchRect.xy)) && all(lessThan(cell, waterPatchRect.zw));
        int edge;
        bool drawn = water_cell_drawn(texel) && !patched && !water_fringe(cell, edge);
        vec2 corner = drawn ? vec2(float(vertex & 1), float(vertex >> 1)) : vec2(0.0);
        pos = vec3(water_square_point(vec2(cell) + corner), texel.r);
        return drawn;
    }

    if (part == 3)
    {
        // Which cell: those south of the rectangle west to east, then north of it, then west of it south to north,
        // then east of it.
        int w = waterPatchRect.z - waterPatchRect.x;
        int h = waterPatchRect.w - waterPatchRect.y;
        ivec2 cell;
        if (instance < w)
        {
            cell = ivec2(waterPatchRect.x + instance, waterPatchRect.y - 1);
        }
        else if (instance < 2 * w)
        {
            cell = ivec2(waterPatchRect.x + instance - w, waterPatchRect.w);
        }
        else if (instance < 2 * w + h)
        {
            cell = ivec2(waterPatchRect.x - 1, waterPatchRect.y + instance - 2 * w);
        }
        else
        {
            cell = ivec2(waterPatchRect.z, waterPatchRect.y + instance - 2 * w - h);
        }
        vec2 texel = texelFetch(waterCellMap, cell, 0).rg;
        int edge;
        bool drawn = water_fringe(cell, edge) && water_cell_drawn(texel);
        // Its shared edge south, its patch corners p_k there: a fan from the south-west far corner over the west half,
        // a triangle across the middle, and a fan from the north-east far corner over the east half.
        int tri = vertex / 3;
        int k = vertex - tri * 3;
        const int HALF = WATER_PATCHES / 2;
        vec2 t;
        if (tri < HALF)
        {
            t = k == 0 ? vec2(float(tri), 0.0) : (k == 1 ? vec2(float(tri + 1), 0.0) : vec2(0.0, float(WATER_PATCHES)));
        }
        else if (tri == HALF)
        {
            t = k == 0 ? vec2(float(HALF), 0.0) : (k == 1 ? vec2(float(WATER_PATCHES)) : vec2(0.0, float(WATER_PATCHES)));
        }
        else
        {
            int p = tri - 1;
            t = k == 0 ? vec2(float(p), 0.0) : (k == 1 ? vec2(float(p + 1), 0.0) : vec2(float(WATER_PATCHES)));
        }
        t /= float(WATER_PATCHES);
        vec2 local = drawn ? water_turn(t, edge) : vec2(0.0);
        pos = vec3(water_square_point(vec2(cell) + local), texel.r);
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
    // east one; without, an empty first and two halves. The empty one comes first: llvmpipe draws nothing of an
    // instance whose last triangle is empty.
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
            t = vec2(0.0);
        }
        else if (tri == 1)
        {
            t = k == 0 ? vec2(0.0, 0.0) : (k == 1 ? vec2(1.0, 0.0) : vec2(1.0, 1.0));
        }
        else
        {
            t = k == 0 ? vec2(0.0, 0.0) : (k == 1 ? vec2(1.0, 1.0) : vec2(0.0, 1.0));
        }
    }
    pos = vec3(centre + (drawn ? water_between(lo, hi, t) : vec2(0.0)), waterDefaultHeight);
    return drawn;
}

// The order the waves' patches are drawn in: x, 1 where the patches run along y fastest and x slowest, 0 the other
// way about; y and z, 1 where the slow and the fast axis run from their high end. Chosen from the view so the patches
// come front to back (ALWaterSurface::bindWaves), which lets the depth test turn away the water a nearer crest hides
// before it is shaded. All 0, rows of x from the south-west.
uniform ivec4 waterPatchOrder;

// Corner `vertex` of patch `instance` of the cells in waterPatchRect, WATER_PATCHES x WATER_PATCHES a cell, at the
// cell's water height: the rectangle's patches in rows, as waterPatchOrder runs them. The corners run counter-clockwise
// from above, (0, 0), (1, 0), (1, 1), (0, 1), and are placed as the cells and the fringe are (water_square_point), so a
// patch row's outer corners are the cells' corners exactly and the fringe's along their edge. Where a patch is depends
// only on which it is, never on its instance. A patch of a cell not drawn, a void one with the void hidden, has its
// four corners at one point, which the control stage culls.
void water_patch_corner(int instance, int vertex, out vec3 pos)
{
    ivec2 patches = max(waterPatchRect.zw - waterPatchRect.xy, ivec2(1)) * WATER_PATCHES;
    bool slow_x = waterPatchOrder.x != 0;
    int run = slow_x ? patches.y : patches.x;
    int slow = instance / run;
    int fast = instance - slow * run;
    slow = waterPatchOrder.y != 0 ? (slow_x ? patches.x : patches.y) - 1 - slow : slow;
    fast = waterPatchOrder.z != 0 ? run - 1 - fast : fast;
    ivec2 at_patch = slow_x ? ivec2(slow, fast) : ivec2(fast, slow);
    ivec2 cell = waterPatchRect.xy + at_patch / WATER_PATCHES;
    ivec2 square = at_patch - (at_patch / WATER_PATCHES) * WATER_PATCHES;
    vec2 texel = texelFetch(waterCellMap, cell, 0).rg;
    ivec2 corner = water_cell_drawn(texel) ? ivec2((vertex == 1 || vertex == 2) ? 1 : 0, vertex >= 2 ? 1 : 0) : ivec2(0);
    pos = vec3(water_square_point(vec2(cell) + vec2(square + corner) / float(WATER_PATCHES)), texel.r);
}

// A point `r` from the camera, horizontally, held to the wave clamp: past it the wave coordinates stop following the
// surface.
vec2 water_wave_clamp(vec2 r)
{
    float d = max(length(r), 0.001);
    return r * (min(d, waterFar.x) / d);
}

// The wave coordinates of a point `r` from the camera, horizontally, as far as the wave clamp: the big layer's, and
// the little map's xy and zw reads. LL's sweep bends x before the layers scale it. The phases hold every term of the
// camera's own position, so nothing large is worked here, and a point keeps its coordinates however the camera and the
// agent's region move.
void water_wave_coords(vec2 r, out vec2 big, out vec4 little)
{
    r = water_wave_clamp(r);
    vec2 v = r;
    v.x += (cos(r.x*0.08 + waterSweepPhase.x) + sin(r.y*0.02 + waterSweepPhase.y))*6.0;
    big = v * vec2(0.04,0.04) + waterBigPhase;
    little.xy = v * vec2(0.45, 0.9) + waterLittlePhase.xy;
    little.zw = v * vec2(0.1, 0.2) + waterLittlePhase.zw;
}

#if defined(FRAGMENT_SHADER) || defined(TESS_EVALUATION_SHADER)

// Which wave layers are hex-tiled (AlchemyRenderWaterHextile, a global define): 0 none, 1 the big layer, whose 25 m
// repeat is what reads as a grid, 2 the mid layer (the little map's zw read) too. The little map's xy read repeats
// every 2 m, lost under the others' structure a few metres out, and is never tiled.
#ifndef WATER_HEXTILE
#define WATER_HEXTILE 0
#endif

// The lattice's points one repeat of the layer apart, so a cell holds about a whole tile of the author's map
// (ALWaterField::HEX_GRID).
#define WATER_HEX_GRID 1.0
// The blend's contrast between cells, and the weight under which a cell is not read, as terrain's.
#define WATER_HEX_CONTRAST 7.0
#define WATER_HEX_THRESHOLD 0.01

uniform float blend_factor;
// The camera's place on each tiled layer's lattice (ALWaterField::latticePlaces): its fraction, and the low 32 bits of
// its floor as int bits, of the big layer (xy) and the mid layer (zw).
uniform vec4 waterLatticeFrac;
uniform ivec4 waterLatticeBase;

vec2 hex_skew(vec2 p);
void hex_triangle(vec2 f, out ivec2 corner0, out ivec2 corner1, out ivec2 corner2, out vec3 bary);
int hex_sharpen(vec3 bary, float contrast, float threshold, out vec3 weight);
uint hex_hash(uvec2 id);

// The cells of a tiled layer's lattice that reach `p`, a point from the camera in the layer's own scaled coordinates,
// before the sweep and the scroll, so the lattice never moves while the waves scroll through it: each cell's offset in
// the map, a whole repeat's worth of choice, and its weight, 0 for a cell not read. Cells are only shifted, never
// turned, so the waves keep the direction they scroll in. A cell's id is the camera's place plus the cell's from the
// camera, wrapping in 32 bits, so a point of the sea keeps its cell however far the camera goes.
void water_hex_cells(vec2 p, vec2 frac, ivec2 base, out vec2 offset[3], out vec3 weight)
{
    vec2 q = frac + hex_skew(p * WATER_HEX_GRID);
    vec2 cell = floor(q);
    ivec2 corner[3];
    vec3 bary;
    hex_triangle(q - cell, corner[0], corner[1], corner[2], bary);
    hex_sharpen(bary, WATER_HEX_CONTRAST, WATER_HEX_THRESHOLD, weight);
    // Through ivec2: a negative float converts to no uint.
    uvec2 id = uvec2(base) + uvec2(ivec2(cell));
    for (int i = 0; i < 3; ++i)
    {
        uint h = hex_hash(id + uvec2(corner[i]));
        offset[i] = vec2(h & 0x7ffu, (h >> 11) & 0x7ffu) * (1.0 / 2048.0);
    }
}

// The variance a blend of independent samples loses, given back: one over the root of the squared weights' sum, 1
// inside a cell and up to sqrt(3) where three cells weigh alike. Faded out as the layer minifies toward a cell spanning
// four texels of the mip read, where the three samples stop being independent. `footprint` is the texels of the map's
// base a step of the read covers: a pixel's, or a vertex's.
float water_hex_gain(vec3 weight, float footprint, vec2 texels)
{
    float cell_texels = min(texels.x, texels.y) / (WATER_HEX_GRID * max(footprint, 1.0));
    return mix(1.0, inversesqrt(dot(weight, weight)), smoothstep(4.0, 16.0, cell_texels));
}

#endif

#ifdef FRAGMENT_SHADER

// Each map's mean normal xy: the current map's (xy) and the next's (zw), 0 until its pixels have arrived.
uniform vec4 waterMapMean;

// One map's normal over a tiled layer's cells. The xy blend about the map's mean, so the gain scales only what varies
// and the mean slope the author set stays where it was; z blends plainly. The gradients are the plain uv's: a cell's
// offset jumps at its edge, and implicit derivatives there would pick the smallest mip.
vec3 water_hex_read(sampler2D map, vec2 uv, vec2 dx, vec2 dy, vec2 offset[3], vec3 weight, float gain, vec2 mean)
{
    vec2 xy = vec2(0.0);
    float z = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        if (weight[i] > 0.0)
        {
            vec3 n = textureGrad(map, uv + offset[i], dx, dy).xyz * 2.0 - 1.0;
            xy += weight[i] * (n.xy - mean);
            z += weight[i] * n.z;
        }
    }
    return vec3(mean + gain * xy, z);
}

// A tiled layer's normal at map coordinate `uv` and lattice point `p` (water_hex_cells), from the current map and,
// while a transition runs, the next, at the same cells and weights: two arrangements cross-fading would crawl. With no
// transition the next map is not read at all.
vec3 water_tiled_layer(sampler2D map_a, sampler2D map_b, vec2 uv, vec2 p, vec2 frac, ivec2 base)
{
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    vec2 offset[3];
    vec3 weight;
    water_hex_cells(p, frac, base, offset, weight);
    // The mip by hand, from the plain uv's derivatives: textureQueryLod is GLSL 4.00.
    vec2 texels = vec2(textureSize(map_a, 0));
    float gain = water_hex_gain(weight, max(length(dx * texels), length(dy * texels)), texels);
    vec3 n = water_hex_read(map_a, uv, dx, dy, offset, weight, gain, waterMapMean.xy);
    if (blend_factor > 0.0)
    {
        n = mix(n, water_hex_read(map_b, uv, dx, dy, offset, weight, gain, waterMapMean.zw), blend_factor);
    }
    return n;
}

// A layer read plainly, from the current map and, while a transition runs, the next: implicit gradients, the same as
// explicit ones taken of the same uv, at the sampler's full rate. The blend is uniform, so the read stays in uniform
// control flow.
vec3 water_plain_layer(sampler2D map_a, sampler2D map_b, vec2 uv)
{
    vec3 n = texture(map_a, uv).xyz * 2.0 - 1.0;
    if (blend_factor > 0.0)
    {
        n = mix(n, texture(map_b, uv).xyz * 2.0 - 1.0, blend_factor);
    }
    return n;
}

// The wave layers' normals at a point `r` from the camera, horizontally, decoded but not normalised: the big layer,
// and the little map's xy and zw reads, each blended between the environment's two maps by its transition. Every read
// of the water's normal maps comes through here. In uniform control flow: it takes derivatives.
void water_wave_normals(sampler2D map_a, sampler2D map_b, vec2 r, out vec3 big, out vec3 little_xy, out vec3 little_zw)
{
    vec2 big_uv;
    vec4 little_uv;
    water_wave_coords(r, big_uv, little_uv);
#if WATER_HEXTILE >= 1
    vec2 p = water_wave_clamp(r);
    big = water_tiled_layer(map_a, map_b, big_uv, p * 0.04, waterLatticeFrac.xy, waterLatticeBase.xy);
#else
    big = water_plain_layer(map_a, map_b, big_uv);
#endif
    little_xy = water_plain_layer(map_a, map_b, little_uv.xy);
#if WATER_HEXTILE >= 2
    little_zw = water_tiled_layer(map_a, map_b, little_uv.zw, p * vec2(0.1, 0.2), waterLatticeFrac.zw, waterLatticeBase.zw);
#else
    little_zw = water_plain_layer(map_a, map_b, little_uv.zw);
#endif
}

#endif

#ifdef TESS_EVALUATION_SHADER

uniform vec3 eyeVec;
// The current and next maps' baked heights (ALWaterHeightBake): the big layer's field in r and the mid layer's in g, in
// repeats of the map, on their own trilinear sampler.
uniform sampler2D waterHeightMap;
uniform sampler2D waterHeightMap2;
// Each map's wave height, the dial times how far its waves have risen since its bake landed: the current map's (x)
// and the next's (y); 0 for a map not baked.
uniform vec2 waterWaveAmp;
// x: the tessellation reach, by which the waves have faded out; y: A, the most they move the surface.
uniform vec2 waterWaveReach;
// The camera the waves were laid out around (ALWaterSurface's layout), in agent space: the main one even in a mirror,
// whose own eye is eyeVec. Their fade and the flat ground under the camera follow it, as the patches drawn do.
uniform vec3 waterWaveEye;
uniform float waterTessDensity;

// A patch's side, a cell's over WATER_PATCHES, and the most its edges are split (waterTC.glsl's WATER_MAX_LEVEL): no
// vertex step is finer than their ratio, however near the camera.
#define WATER_PATCH_METRES 32.0
#define WATER_MAX_LEVEL 64.0

// Metres of height per unit of each field: the layer's weight in the shading over its repeats a metre along x
// (ALWaterHeightBake::BIG_METRES and MID_METRES).
#define WATER_BIG_METRES 12.5
#define WATER_MID_METRES 3.0

// One field of a map over a tiled layer's cells at a mip: the cells' heights by their weights, and the gain. The
// heights' mean is 0, so nothing is held about it.
float water_hex_height(sampler2D map, int field, vec2 uv, float lod, vec2 offset[3], vec3 weight, float gain)
{
    float h = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        if (weight[i] > 0.0)
        {
            h += weight[i] * textureLod(map, uv + offset[i], lod)[field];
        }
    }
    return gain * h;
}

// A map's height in metres, at the big layer's and the mid layer's coordinates and the lattice point `p`
// (water_wave_clamp's), each layer read through the lattice the shading reads it through, so a cell's crests lie under
// its shading, and at the mip whose texels a vertex step covers: sampled sparser than the map, a layer would crawl.
float water_map_height(sampler2D map, vec2 big_uv, vec2 mid_uv, vec2 p, float spacing)
{
    vec2 texels = vec2(textureSize(map, 0));
    float big_step = spacing * 0.04 * max(texels.x, texels.y);
    float mid_step = spacing * 0.2 * max(texels.x, texels.y);
    float big_lod = log2(max(big_step, 1.0));
    float mid_lod = log2(max(mid_step, 1.0));
#if WATER_HEXTILE >= 1
    vec2 offset[3];
    vec3 weight;
    water_hex_cells(p * 0.04, waterLatticeFrac.xy, waterLatticeBase.xy, offset, weight);
    float big = water_hex_height(map, 0, big_uv, big_lod, offset, weight, water_hex_gain(weight, big_step, texels));
#else
    float big = textureLod(map, big_uv, big_lod).r;
#endif
#if WATER_HEXTILE >= 2
    water_hex_cells(p * vec2(0.1, 0.2), waterLatticeFrac.zw, waterLatticeBase.zw, offset, weight);
    float mid = water_hex_height(map, 1, mid_uv, mid_lod, offset, weight, water_hex_gain(weight, mid_step, texels));
#else
    float mid = textureLod(map, mid_uv, mid_lod).g;
#endif
    return WATER_BIG_METRES * big + WATER_MID_METRES * mid;
}

// How far the waves move the surface at `p`, a point of the flat field in agent space: the maps' heights at its wave
// coordinates, faded out by the tessellation reach, and around the camera they were laid out for (waterWaveEye). While
// that camera is within A of the plane the surface over its xy stays flat, so the plane's underwater test and the
// surface drawn agree where the camera is.
float water_displacement(vec3 p)
{
    float A = waterWaveReach.y;
    if (A <= 0.0)
    {
        return 0.0;
    }
    vec2 r_fade = p.xy - waterWaveEye.xy;
    float d = length(r_fade);
    float fade = 1.0 - smoothstep(0.75 * waterWaveReach.x, waterWaveReach.x, d);
    float level = 1.0 - smoothstep(A, 2.0 * A, abs(waterWaveEye.z - p.z));
    fade *= mix(1.0, smoothstep(0.0, max(8.0, 4.0 * A), d), level);
    if (fade <= 0.0)
    {
        return 0.0;
    }
    // The wave coordinates from the eye drawn from, which their phases are worked for; the vertex step as the patch's
    // edges are split, distance over density but never finer than the most levels a patch's edge takes.
    vec2 r = p.xy - eyeVec.xy;
    float spacing = max(distance(p, eyeVec) / waterTessDensity, WATER_PATCH_METRES / WATER_MAX_LEVEL);
    vec2 big_uv;
    vec4 little_uv;
    water_wave_coords(r, big_uv, little_uv);
    vec2 q = water_wave_clamp(r);
    float h = waterWaveAmp.x > 0.0 ? waterWaveAmp.x * water_map_height(waterHeightMap, big_uv, little_uv.zw, q, spacing) : 0.0;
    if (blend_factor > 0.0)
    {
        float next = waterWaveAmp.y > 0.0 ? waterWaveAmp.y * water_map_height(waterHeightMap2, big_uv, little_uv.zw, q, spacing) : 0.0;
        h = mix(h, next, blend_factor);
    }
    return fade * h;
}

#endif
