/**
 * @file class1\deferred\pbrterrainUtilF.glsl
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

/*[EXTRA_CODE_HERE]*/

/**
 * Triplanar mapping implementation adapted from Inigo Quilez' example shader,
 * MIT license.
 * https://www.shadertoy.com/view/MtsGWH
 * Copyright © 2015 Inigo Quilez
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: The above copyright
 * notice and this permission notice shall be included in all copies or
 * substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

// A relatively agressive threshold for terrain material mixing sampling
// cutoff. This ensures that only one or two materials are used in most places,
// making PBR terrain blending more performant. Should be greater than 0 to work.
#define TERRAIN_RAMP_MIX_THRESHOLD 0.1
// A small threshold for triplanar mapping sampling cutoff. This and
// TERRAIN_TRIPLANAR_BLEND_FACTOR together ensures that only one or two samples
// per texture are used in most places, making triplanar mapping more
// performant. Should be greater than 0 to work.
// There's also an artistic design choice in the use of these factors, and the
// use of triplanar generally. Don't take these triplanar constants for granted.
#define TERRAIN_TRIPLANAR_MIX_THRESHOLD 0.01

#define SAMPLE_X 1 << 0
#define SAMPLE_Y 1 << 1
#define SAMPLE_Z 1 << 2
#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

struct PBRMix
{
    vec4 col;       // RGB color with alpha, linear space
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    vec3 orm;       // Occlusion, roughness, metallic
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    vec2 rm;        // Roughness, metallic
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    vec3 vNt;       // Unpacked normal texture sample, vector
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    vec3 emissive;  // RGB emissive color, linear space
#endif
};

PBRMix init_pbr_mix()
{
    PBRMix mix;
    mix.col = vec4(0);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = vec3(0);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = vec2(0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = vec3(0);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = vec3(0);
#endif
    return mix;
}

// Usage example, for two weights:
// vec2 weights = ... // Weights must add up to 1
// PBRMix mix = init_pbr_mix();
// PBRMix mix1 = ...
// mix = mix_pbr(mix, mix1, weights.x);
// PBRMix mix2 = ...
// mix = mix_pbr(mix, mix2, weights.y);
PBRMix mix_pbr(PBRMix mix1, PBRMix mix2, float mix2_weight)
{
    PBRMix mix;
    mix.col      = mix1.col      + (mix2.col      * mix2_weight);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm      = mix1.orm      + (mix2.orm      * mix2_weight);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm       = mix1.rm       + (mix2.rm       * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt      = mix1.vNt      + (mix2.vNt      * mix2_weight);
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = mix1.emissive + (mix2.emissive * mix2_weight);
#endif
    return mix;
}

// Gradients arrive as parameters rather than being taken from uv here, because every call to
// this function is inside a switch that branches per fragment -- on which of the four terrain
// materials covers it, and under triplanar on which axis is being projected. An implicit-LOD
// texture() picks its mip from derivatives of the coordinate as evaluated, and the lanes of a
// quad that did not take the branch have no value for it. That is undefined by the spec, and
// undefined in the way that matters: the fragments where the branch diverges are exactly the
// material and axis boundaries, so the artifact lands on the seams.
//
// Callers compute these before any of that branching. See terrain_geometric_normal() in
// pbrterrainF.glsl for the other derivative this shader takes and the same reason for it.
//
// Under TERRAIN_HEX_TILING this is one cell's fetch and sample_pbr() below blends three of
// them; otherwise sample_pbr() is this.
PBRMix fetch_pbr(
    vec2 uv
    , vec2 uv_ddx
    , vec2 uv_ddy
    , sampler2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , sampler2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , sampler2D tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , sampler2D tex_emissive
#endif
    )
{
    PBRMix mix;
    // Colour arrives linear: lldrawpoolterrain binds base colour and emissive through
    // ALSamplers::AnisoWrapSRGB, the same GLTF_COLOR_SAMPLER LLFetchedGLTFMaterial::bind
    // uses, so the hardware decodes on the fetch. Data slots (orm, normal) bind without it
    // and are read raw.
    mix.col = textureGrad(tex_col, uv, uv_ddx, uv_ddy);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm = textureGrad(tex_orm, uv, uv_ddx, uv_ddy).xyz;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm = textureGrad(tex_orm, uv, uv_ddx, uv_ddy).yz;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    mix.vNt = textureGrad(tex_vNt, uv, uv_ddx, uv_ddy).xyz*2.0-1.0;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive = textureGrad(tex_emissive, uv, uv_ddx, uv_ddy).xyz;
#endif
    return mix;
}

#ifdef TERRAIN_HEX_TILING
// Hex tiling, after Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 11(2), 2022. A lattice
// of equilateral triangles covers the plane; a fragment lies in one triangle, whose three
// corners are the centres of the three hexagonal cells that reach it. Every cell shows the
// texture at its own offset and rotation, and the three are blended by the fragment's
// barycentric weights raised to a power, which keeps the blend band narrow enough that the
// texture's contrast survives it. Nothing is precomputed from the texture, which is what lets
// this run on assets the viewer first meets at fetch time.
//
// It sits inside the projection: under triplanar each slice is hex-tiled in its own plane. It
// breaks repetition within a plane and does nothing about the stretch a projection has on a
// slope; those are the two different problems.

// Contrast of the blend between cells. Higher is a narrower band, and less of the washed-out
// look a linear blend of three uncorrelated samples has.
#define TERRAIN_HEX_EXPONENT 7.0
// How far the brighter sample wins the blend beyond its barycentric share. 0 is off.
#define TERRAIN_HEX_FALLOFF 0.6
// 1 rotates each cell by up to a half turn either way.
#define TERRAIN_HEX_ROTATION 1.0
// Lattice density: 2*sqrt(3) puts about three cells across one repeat of the texture.
#define TERRAIN_HEX_GRID_SCALE 3.4641016
// A cell whose weight falls under this is not fetched. As TERRAIN_TRIPLANAR_MIX_THRESHOLD.
#define TERRAIN_HEX_MIX_THRESHOLD 0.01

#define HEX_A 1 << 0
#define HEX_B 1 << 1
#define HEX_C 1 << 2

struct HexTile
{
    ivec2 corner[3];
    vec3 weight;    // Sums to 1
    int type;       // HEX_A | HEX_B | HEX_C: the cells whose weight survived the threshold
};

// lowbias32 (Wellons) over the lattice id. Integer, so every GPU agrees on what a cell shows;
// the sin()-based hashes disagree once the argument is a few thousand.
uint _hex_hash(ivec2 corner)
{
    // Ids run a few thousand either side of zero; the bias keeps the conversion in range.
    uvec2 u = uvec2(corner + ivec2(1 << 20));
    uint h = u.x * 0x8da6b343u + u.y * 0xd8163841u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

HexTile hex_tile(vec2 uv)
{
    // Skew the plane into the lattice's own coordinates, where every triangle is half of a
    // unit square. The fragment's barycentrics come from its position in that square, and its
    // three corners from which half it is in.
    vec2 skewed = mat2(1.0, 0.0, -0.57735027, 1.15470054) * (uv * TERRAIN_HEX_GRID_SCALE);
    ivec2 base = ivec2(floor(skewed));
    vec2 f = fract(skewed);
    int upper = int(step(1.0, f.x + f.y));
    float flip = float(upper) * 2.0 - 1.0;

    HexTile ht;
    ht.corner[0] = base + ivec2(upper);
    ht.corner[1] = base + ivec2(upper, 1 - upper);
    ht.corner[2] = base + ivec2(1 - upper, upper);
    vec3 w = max(vec3(0.0), vec3((f.x + f.y - 1.0) * flip, float(upper) - f.y * flip, float(upper) - f.x * flip));

    w = pow(w, vec3(TERRAIN_HEX_EXPONENT));
    w /= (w.x + w.y + w.z);
    w -= TERRAIN_HEX_MIX_THRESHOLD;
    ivec3 usage = ivec3(round(max(vec3(0.0), sign(w))));
    ht.weight = max(vec3(0.0), w);
    ht.weight /= (ht.weight.x + ht.weight.y + ht.weight.z);
    ht.type = (usage.x * HEX_A) |
              (usage.y * HEX_B) |
              (usage.z * HEX_C);
    return ht;
}

// Where a cell samples: the texture turned about the cell's centre and shifted, both by the
// cell's hash. Returns the rotation so the caller can take the gradients and the normal
// through it.
mat2 hex_cell(ivec2 corner, vec2 uv, out vec2 st)
{
    uint h = _hex_hash(corner);
    vec2 offset = vec2(h & 0x7ffu, (h >> 11) & 0x7ffu) * (1.0 / 2048.0);
    float angle = (float(h >> 22) * (1.0 / 512.0) - 1.0) * radians(180.0) * TERRAIN_HEX_ROTATION;
    float c = cos(angle);
    float s = sin(angle);
    mat2 rot = mat2(c, s, -s, c);
    // The lattice id back to the plane, in uv units.
    vec2 centre = mat2(1.0, 0.0, 0.5, 0.8660254) * vec2(corner) * (1.0 / TERRAIN_HEX_GRID_SCALE);
    st = rot * (uv - centre) + centre + offset;
    return rot;
}

float hex_luma(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

// The blend of a fragment's three cells: each cell's share of the lattice, with the brighter
// sample taking a little more than that share. It reads as one cell's detail standing proud
// of the other's instead of the two fading through each other. A cell that was not fetched
// has a share of zero and stays out. Every map of a material blends by the one set of
// weights, so they stay one surface.
vec3 hex_weights(HexTile ht, vec3 luma)
{
    vec3 w = ht.weight * mix(vec3(1.0), luma, TERRAIN_HEX_FALLOFF);
    return w / (w.x + w.y + w.z);
}

PBRMix sample_pbr(
    vec2 uv
    , vec2 uv_ddx
    , vec2 uv_ddy
    , sampler2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , sampler2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , sampler2D tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , sampler2D tex_emissive
#endif
    )
{
    HexTile ht = hex_tile(uv);

    // One fetch per cell that survived the threshold; the others stay zero and weigh nothing.
    // The gradients go through the cell's rotation with the uv, so the mip is the one the
    // turned footprint asks for. A branch around textureGrad is safe: the gradients are
    // explicit, so no lane needs its neighbours.
    PBRMix cell[3];
    vec3 luma = vec3(0.0);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    vec2 slope[3];
#endif
    for (int i = 0; i < 3; ++i)
    {
        cell[i] = init_pbr_mix();
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        slope[i] = vec2(0.0);
#endif
        if ((ht.type & (1 << i)) != 0)
        {
            vec2 st;
            mat2 rot = hex_cell(ht.corner[i], uv, st);
            cell[i] = fetch_pbr(
                st
                , rot * uv_ddx
                , rot * uv_ddy
                , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
                , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
                , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
                , tex_emissive
#endif
                );
            luma[i] = hex_luma(cell[i].col.rgb);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            // The cell's texture is turned in the plane, so its normal's xy turn with it: by the
            // inverse of what took the uv there. Carried as a slope, which is what a blend of
            // heightfields adds, and which the shortening a mip filter does to the vector
            // leaves alone.
            vec3 n = cell[i].vNt;
            slope[i] = (n.xy * rot) / max(n.z, 0.015625);
#endif
        }
    }

    vec3 w = hex_weights(ht, luma);

    PBRMix mix = init_pbr_mix();
    mix = mix_pbr(mix, cell[0], w.x);
    mix = mix_pbr(mix, cell[1], w.y);
    mix = mix_pbr(mix, cell[2], w.z);
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    // The composed heightfield's slope, as a normal. Not unit; _t_normal_compose is linear in
    // its normal and normalises what it returns.
    mix.vNt = vec3(slope[0] * w.x + slope[1] * w.y + slope[2] * w.z, 1.0);
#endif
    return mix;
}
#else
#define sample_pbr fetch_pbr
#endif

// The fragment's region-local position and its screen derivatives. Every projection's uv is
// an affine map of a 2D slice of the position, applied per material below; the map is linear,
// so a slice's uv derivatives are its matrix applied to the position's. The derivatives are
// taken once by the caller, in uniform control flow, and the maps can then run inside the
// material and projection branches with nothing left to take there.
struct TerrainPoint
{
    vec3 p;
    vec3 ddx;
    vec3 ddy;
};

// Which projections cover a fragment and by how much, and which side of the x and y axes its
// surface faces, as +-1: sign() would give 0 on the axis itself, and the negative side of an
// axis is sampled through a mirrored slice, so 0 has to land on one side or the other.
struct TerrainTriplanar
{
    vec3 weight;
    int type;
    float sx;
    float sy;
};

struct TerrainMix
{
    vec4 weight;
    int type;
};

#define TerrainMixSample vec4[4]
#define TerrainMixSample3 vec3[4]

TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal)
{
    TerrainMix tm;
    vec4 sample_x = vec4(1,0,0,0);
    vec4 sample_y = vec4(0,1,0,0);
    vec4 sample_z = vec4(0,0,1,0);
    vec4 sample_w = vec4(0,0,0,1);

    tm.weight = mix( mix(sample_w, sample_z, alpha2), mix(sample_y, sample_x, alpha1), alphaFinal );
    tm.weight -= TERRAIN_RAMP_MIX_THRESHOLD;
    ivec4 usage = max(ivec4(0), ivec4(ceil(tm.weight)));
    // Prevent negative weights and keep weights balanced
    tm.weight = tm.weight*vec4(usage);
    tm.weight /= (tm.weight.x + tm.weight.y + tm.weight.z + tm.weight.w);

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// The four-way mix from the alpha ramp at a fragment's composition: the ramp
// sampled at the composition, and at one and two below it, are the three
// blend alphas. The composition value and its noise arrive as one vec2; the
// offsets are constants, so the evaluation stage need not carry them.
TerrainMix terrain_ramp_mix(sampler2D ramp, vec2 composition)
{
    float alpha1 = texture(ramp, composition).a;
    float alpha2 = texture(ramp, composition - vec2(2.0, 0.0)).a;
    float alphaFinal = texture(ramp, composition - vec2(1.0, 0.0)).a;
    return get_terrain_mix_weights(alpha1, alpha2, alphaFinal);
}

// A paintmap weight applier for 4 swatches. The input saves a channel by not
// storing swatch 1, and assuming the weights of the 4 swatches add to 1.
// The components of weight3 should be between 0 and 1
// The sum of the components of weight3 should be between 0 and 1
TerrainMix get_terrain_usage_from_weight3(vec3 weight3)
{
    // These steps ensure the output weights add to between 0 and 1
    weight3.xyz = max(vec3(0.0), weight3.xyz);
    weight3.xyz /= max(1.0, weight3.x + weight3.y + weight3.z);

    TerrainMix tm;

    // Extract the first weight from the other weights
    tm.weight.x = 1.0 - (weight3.x + weight3.y + weight3.z);
    tm.weight.yzw = weight3.xyz;
    ivec4 usage = max(ivec4(0), ivec4(ceil(tm.weight)));

    tm.type = (usage.x * MIX_X) |
              (usage.y * MIX_Y) |
              (usage.z * MIX_Z) |
              (usage.w * MIX_W);
    return tm;
}

// Inverse of get_terrain_usage_from_weight3, excluding usage flags
// The components of weight should be between 0 and 1
// The sum of the components of weight should be 1
vec3 get_weight3_from_terrain_weight(vec4 weight)
{
    // These steps ensure the input weights add to 1
    weight = max(vec4(0.0), weight);
    weight.x += 1.0 - sign(weight.x + weight.y + weight.z + weight.w);
    weight /= weight.x + weight.y + weight.z + weight.w;

    // Then return the input weights with the first weight truncated
    return weight.yzw;
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// facet_region is the surface's normal under the fragment, in region space, where the
// projection planes are the axes: terrain_facet in terrainSurface.glsl. It has to be evaluated
// at the fragment. A normal carried from the vertices is interpolated across every triangle
// that straddles a crease, and on the far side of a cliff top that hands a vertical face the
// top's slice, or the level ground beside it the face's -- a smear one triangle wide along
// every crease.
TerrainTriplanar terrain_triplanar_weights(vec3 facet_region)
{
    float sharpness = TERRAIN_TRIPLANAR_BLEND_FACTOR;
    float threshold = TERRAIN_TRIPLANAR_MIX_THRESHOLD;
    vec3 weight_signed = pow(abs(facet_region), vec3(sharpness));
    weight_signed /= (weight_signed.x + weight_signed.y + weight_signed.z);
    weight_signed -= vec3(threshold);
    TerrainTriplanar tw;
    // *NOTE: Make sure the threshold doesn't affect the materials
    tw.weight = max(vec3(0), weight_signed);
    tw.weight /= (tw.weight.x + tw.weight.y + tw.weight.z);
    ivec3 usage = ivec3(round(max(vec3(0), sign(weight_signed))));
    tw.type = ((usage.x) * SAMPLE_X) |
              ((usage.y) * SAMPLE_Y) |
              ((usage.z) * SAMPLE_Z);
    tw.sx = facet_region.x > 0.0 ? 1.0 : -1.0;
    tw.sy = facet_region.y > 0.0 ? 1.0 : -1.0;
    return tw;
}
#endif


// Assume weights add to 1
float terrain_mix(TerrainMix tm, vec4 tms4)
{
    return (tm.weight.x * tms4[0]) +
           (tm.weight.y * tms4[1]) +
           (tm.weight.z * tms4[2]) +
           (tm.weight.w * tms4[3]);
}

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
// Composes one projection's normal sample with the geometric normal, in the projection's own
// frame: u and v along the plane, n out of it. The frame is the plane's, not a vertex tangent's.
// A projected texture's tangent frame IS the plane, exactly, on every slope; a per-vertex
// tangent is the gradient of one uv set, which is a different thing for each slice, and on a
// slope it is sheared by the orthogonalisation that a non-conformal parametrisation forces.
//
// The sample's xy are slopes in the texture's own uv space. uv_axes takes them into the plane:
// the material's KHR rotation and scale sign, inverted. Scale magnitude is left out on purpose
// -- a texture tiled twice as densely is not twice as bumpy -- as the prim path leaves it out.
//
// g is the geometric normal, already swizzled into this frame. The sample and the terrain are
// both heightfields over the projection plane, so their slopes add; that is the partial-
// derivative blend, exact for this composition. Whiteout (n.xy + g.xy, n.z * g.z) drops the
// g.z factor on the sample's slope, which steepens every bump by 1 / g.z on an incline.
vec3 _t_normal_compose(vec3 n, mat2 uv_axes, vec3 g)
{
    n.xy = uv_axes * n.xy;
    return normalize(vec3(n.xy * g.z + g.xy * n.z, n.z * g.z));
}
#endif

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// Triplanar mapping

PBRMix terrain_sample_pbr(
    TerrainPoint pt
    , mat2 uv_transform
    , vec2 uv_offset
    , TerrainTriplanar tw
    , sampler2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , sampler2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , sampler2D tex_vNt
    , mat2 uv_axes
    , vec3 geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , sampler2D tex_emissive
#endif
    )
{
    PBRMix mix = init_pbr_mix();

    // Which side of each axis the surface faces, for both the uv slice and the normal frame
    // below. The x slice sees the surface as a heightfield over (sx y, z), the y slice over
    // (-sy x, z), the z slice over (x, y): u runs away from the axis on either side, so the
    // texture faces out of both faces of a ridge. The material's map takes each slice to its
    // uv, and its matrix takes the slice's derivatives to the uv's.
    float sx = tw.sx;
    float sy = tw.sy;

    switch (tw.type & SAMPLE_X)
    {
    case SAMPLE_X:
        PBRMix mix_x = sample_pbr(
            uv_transform * vec2(sx * pt.p.y, pt.p.z) + uv_offset
            , uv_transform * vec2(sx * pt.ddx.y, pt.ddx.z)
            , uv_transform * vec2(sx * pt.ddy.y, pt.ddy.z)
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        {
            // The x slices are uv = (sx*y, z): u = sx*Y, v = Z, n = sx*X.
            vec3 g = vec3(sx * geom_normal_region.y, geom_normal_region.z, sx * geom_normal_region.x);
            vec3 r = _t_normal_compose(mix_x.vNt, uv_axes, g);
            mix_x.vNt = vec3(sx * r.z, sx * r.x, r.y);
        }
#endif
        mix = mix_pbr(mix, mix_x, tw.weight.x);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Y)
    {
    case SAMPLE_Y:
        PBRMix mix_y = sample_pbr(
            uv_transform * vec2(-sy * pt.p.x, pt.p.z) + uv_offset
            , uv_transform * vec2(-sy * pt.ddx.x, pt.ddx.z)
            , uv_transform * vec2(-sy * pt.ddy.x, pt.ddy.z)
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        {
            // The y slices are uv = (-sy*x, z): u = -sy*X, v = Z, n = sy*Y.
            vec3 g = vec3(-sy * geom_normal_region.x, geom_normal_region.z, sy * geom_normal_region.y);
            vec3 r = _t_normal_compose(mix_y.vNt, uv_axes, g);
            mix_y.vNt = vec3(-sy * r.x, sy * r.z, r.y);
        }
#endif
        mix = mix_pbr(mix, mix_y, tw.weight.y);
        break;
    default:
        break;
    }

    switch (tw.type & SAMPLE_Z)
    {
    case SAMPLE_Z:
        PBRMix mix_z = sample_pbr(
            uv_transform * pt.p.xy + uv_offset
            , uv_transform * pt.ddx.xy
            , uv_transform * pt.ddy.xy
            , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , tex_emissive
#endif
            );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        // The z slice is uv = (x, y): the frame is the region's own axes. There is no flipped
        // slice -- a heightfield never faces down.
        mix_z.vNt = _t_normal_compose(mix_z.vNt, uv_axes, geom_normal_region);
#endif
        mix = mix_pbr(mix, mix_z, tw.weight.z);
        break;
    default:
        break;
    }

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    // Unit length again, so the material weights applied to this afterwards mean what they say.
    mix.vNt = normalize(mix.vNt);
#endif

    return mix;
}
#endif

PBRMix multiply_factors_pbr(
    PBRMix mix_in
    , vec4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , vec3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , vec2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , vec3 factor_emissive
#endif
    )
{
    PBRMix mix = mix_in;
    mix.col *= factor_col;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    mix.orm *= factor_orm;
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    mix.rm *= factor_rm;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    mix.emissive *= factor_emissive;
#endif
    return mix;
}

PBRMix terrain_sample_and_multiply_pbr(
    TerrainPoint pt
    , mat2 uv_transform
    , vec2 uv_offset
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    , TerrainTriplanar tw
#endif
    , sampler2D tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , sampler2D tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    , sampler2D tex_vNt
    , mat2 uv_axes
    , vec3 geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , sampler2D tex_emissive
#endif
    , vec4 factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    , vec3 factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    , vec2 factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
    , vec3 factor_emissive
#endif
    )
{
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    PBRMix mix = terrain_sample_pbr(
        pt
        , uv_transform
        , uv_offset
        , tw
        , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        , tex_vNt
        , uv_axes
        , geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , tex_emissive
#endif
        );
#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1
    PBRMix mix = sample_pbr(
        uv_transform * pt.p.xy + uv_offset
        , uv_transform * pt.ddx.xy
        , uv_transform * pt.ddy.xy
        , tex_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , tex_orm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
        , tex_vNt
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , tex_emissive
#endif
        );
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    // The one slice is the z projection, uv = (x, y): the frame is the region's own axes.
    mix.vNt = _t_normal_compose(mix.vNt, uv_axes, geom_normal_region);
#endif
#endif

    mix = multiply_factors_pbr(mix
        , factor_col
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
        , factor_orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        , factor_rm
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
        , factor_emissive
#endif
    );

    return mix;
}
