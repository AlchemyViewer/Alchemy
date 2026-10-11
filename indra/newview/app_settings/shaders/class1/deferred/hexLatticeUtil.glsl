/**
 * @file hexLatticeUtil.glsl
 * @brief The hex-tiling lattice terrain and water share: the plane skewed into triangles, the three cells reaching a
 *        point and their weights, and the hash a cell's id draws its variation from.
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

// After Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(2), 2022. A lattice of equilateral triangles covers the
// plane; a point lies in one triangle, whose three corners are the centres of the three hexagonal cells that reach it,
// and the three are blended by the point's barycentric weights raised to a power, which keeps the band between cells
// narrow.

// The plane in the lattice's own coordinates, where every triangle is half of a unit square.
vec2 hex_skew(vec2 p)
{
    return mat2(1.0, 0.0, -0.57735027, 1.15470054) * p;
}

// The triangle holding a point whose skewed coordinates have fraction `f`: its three corners, as offsets from the
// floor of the skewed coordinates, and the point's barycentric weights over them.
void hex_triangle(vec2 f, out ivec2 corner0, out ivec2 corner1, out ivec2 corner2, out vec3 bary)
{
    int upper = int(step(1.0, f.x + f.y));
    float flip = float(upper) * 2.0 - 1.0;
    corner0 = ivec2(upper);
    corner1 = ivec2(upper, 1 - upper);
    corner2 = ivec2(1 - upper, upper);
    bary = max(vec3(0.0), vec3((f.x + f.y - 1.0) * flip, float(upper) - f.y * flip, float(upper) - f.x * flip));
}

// The barycentric weights raised to `contrast` and normalised, with a cell under `threshold` dropped and the rest
// normalised again into `weight`. Returns the cells kept, bit i for corner i: a dropped cell is not fetched.
int hex_sharpen(vec3 bary, float contrast, float threshold, out vec3 weight)
{
    vec3 w = pow(bary, vec3(contrast));
    w /= (w.x + w.y + w.z);
    w -= threshold;
    ivec3 usage = ivec3(round(max(vec3(0.0), sign(w))));
    weight = max(vec3(0.0), w);
    weight /= (weight.x + weight.y + weight.z);
    return usage.x | (usage.y << 1) | (usage.z << 2);
}

// lowbias32 (Wellons) over a cell's id. Integer, so every GPU agrees on what a cell shows; the sin()-based hashes
// disagree once the argument is a few thousand.
uint hex_hash(uvec2 id)
{
    uint h = id.x * 0x8da6b343u + id.y * 0xd8163841u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
