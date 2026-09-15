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

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
in vec3 vary_vertex_normal;
#endif

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

struct TerrainTriplanar
{
    vec3 weight;
    int type;
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
TerrainTriplanar _t_triplanar()
{
    float sharpness = TERRAIN_TRIPLANAR_BLEND_FACTOR;
    float threshold = TERRAIN_TRIPLANAR_MIX_THRESHOLD;
    vec3 weight_signed = pow(abs(vary_vertex_normal), vec3(sharpness));
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

// Pre-transformed texture coordinates for each axial uv slice (Packing: xy, yz, (-x)z, unused)
#define TerrainCoord vec4[3]

// axis_sign is +1 for the unflipped slice and -1 for the flipped one. It is the same +-1 the
// caller builds the projection's normal frame from, so the uv a fragment samples and the frame
// its normal is placed in never disagree.
vec2 _t_uv(vec2 uv_unflipped, vec2 uv_flipped, float axis_sign)
{
    return mix(uv_flipped, uv_unflipped, max(0.0, axis_sign));
}

PBRMix terrain_sample_pbr(
    TerrainCoord terrain_coord
    , TerrainCoord terrain_coord_ddx
    , TerrainCoord terrain_coord_ddy
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

    // Which side of each axis the surface faces. Decided once, as +-1, and used for both the uv
    // slice and the normal frame below. sign() would give 0 on the axis itself; the vertex stage
    // supplies a flipped slice for the negative side, so 0 has to land there.
    float sx = vary_vertex_normal.x > 0.0 ? 1.0 : -1.0;
    float sy = vary_vertex_normal.y > 0.0 ? 1.0 : -1.0;

#define get_uv_x() _t_uv(terrain_coord[0].zw, terrain_coord[1].zw, sx)
#define get_uv_y() _t_uv(terrain_coord[1].xy, terrain_coord[2].xy, sy)
#define get_uv_z() terrain_coord[0].xy
// The same slice selection, applied to the gradients. A uv and the gradients that size its mip
// must come from the same projection.
#define get_ddx_x() _t_uv(terrain_coord_ddx[0].zw, terrain_coord_ddx[1].zw, sx)
#define get_ddx_y() _t_uv(terrain_coord_ddx[1].xy, terrain_coord_ddx[2].xy, sy)
#define get_ddx_z() terrain_coord_ddx[0].xy
#define get_ddy_x() _t_uv(terrain_coord_ddy[0].zw, terrain_coord_ddy[1].zw, sx)
#define get_ddy_y() _t_uv(terrain_coord_ddy[1].xy, terrain_coord_ddy[2].xy, sy)
#define get_ddy_z() terrain_coord_ddy[0].xy
    switch (tw.type & SAMPLE_X)
    {
    case SAMPLE_X:
        PBRMix mix_x = sample_pbr(
            get_uv_x()
            , get_ddx_x()
            , get_ddy_x()
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
            get_uv_y()
            , get_ddx_y()
            , get_ddy_y()
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
            get_uv_z()
            , get_ddx_z()
            , get_ddy_z()
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

#elif TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 1

#define TerrainCoord vec2

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
    TerrainCoord terrain_coord
    , TerrainCoord terrain_coord_ddx
    , TerrainCoord terrain_coord_ddy
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
        terrain_coord
        , terrain_coord_ddx
        , terrain_coord_ddy
        , _t_triplanar()
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
        terrain_coord
        , terrain_coord_ddx
        , terrain_coord_ddy
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
