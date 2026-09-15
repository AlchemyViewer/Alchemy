/**
 * @file class1\deferred\terrainF.glsl
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

#define TERRAIN_PBR_DETAIL_EMISSIVE 0
#define TERRAIN_PBR_DETAIL_OCCLUSION -1
#define TERRAIN_PBR_DETAIL_NORMAL -2
#define TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS -3

#define TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE 0
#define TERRAIN_PAINT_TYPE_PBR_PAINTMAP 1

struct TerrainPoint
{
    vec3 p;
    vec3 ddx;
    vec3 ddy;
};

#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

struct TerrainMix
{
    vec4 weight;
    int type;
};

TerrainMix terrain_ramp_mix(sampler2D ramp, vec2 composition);
TerrainMix get_terrain_usage_from_weight3(vec3 weight3);

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
struct TerrainTriplanar
{
    vec3 weight;
    int type;
    float sx;
    float sy;
};
TerrainTriplanar terrain_triplanar_weights(vec3 facet_region);
#endif

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

PBRMix init_pbr_mix();

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
    );

PBRMix mix_pbr(PBRMix mix1, PBRMix mix2, float mix2_weight);

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

out vec4 frag_data[4];

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
uniform sampler2D alpha_ramp;
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
uniform sampler2D paint_map;
#endif

// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
uniform sampler2D detail_0_base_color;
uniform sampler2D detail_1_base_color;
uniform sampler2D detail_2_base_color;
uniform sampler2D detail_3_base_color;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
uniform sampler2D detail_0_normal;
uniform sampler2D detail_1_normal;
uniform sampler2D detail_2_normal;
uniform sampler2D detail_3_normal;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
uniform sampler2D detail_0_metallic_roughness;
uniform sampler2D detail_1_metallic_roughness;
uniform sampler2D detail_2_metallic_roughness;
uniform sampler2D detail_3_metallic_roughness;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
uniform sampler2D detail_0_emissive;
uniform sampler2D detail_1_emissive;
uniform sampler2D detail_2_emissive;
uniform sampler2D detail_3_emissive;
#endif

uniform vec4[4] baseColorFactors; // See also vertex_color in pbropaqueV.glsl
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
uniform vec4 metallicFactors;
uniform vec4 roughnessFactors;
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
uniform vec3[4] emissiveColors;
#endif
uniform vec4 minimum_alphas; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
// Per material, its KHR texture transform as the affine map uv = A p + b of a projection's 2D
// point of the region position, the v flips folded in. The pool builds it; every slice of a
// material goes through the same map, and the derivatives through A alone.
uniform mat2[4] terrain_uv_transform;
uniform vec2[4] terrain_uv_offset;
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
// Per material, the texture's u and v axes in the projection plane: the rotation and scale
// sign of its texture transform, inverted. See _t_normal_compose().
uniform mat2[4] terrain_normal_axes;
#endif

uniform sampler2D parcel_overlay;
uniform int show_parcel_owners;
uniform float region_scale;

in vec3 vary_position;
in vec3 vary_normal;
in vec3 vary_region_position;

#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
in vec2 vary_composition; // composition value, alpha-ramp noise
#endif

void mirrorClip(vec3 position);
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);
vec4 encodeNormalGeo(vec3 n, vec3 geometric_normal, float gbuffer_flag);
vec4 packORM(vec3 orm);
float filterSpecularRoughness(float perceptualRoughness, vec3 n);
vec3 srgb_to_linear(vec3 cs);

float terrain_mix(TerrainMix tm, vec4 tms4);

// terrainSurface.glsl
vec3 terrain_facet(vec2 p_region);

// The geometric normal this fragment shades against. Written once at the top of main().
vec3 geom_normal;

#ifdef TERRAIN_FLAT_NORMALS
// The drawn triangle's normal, recovered from the screen-space derivatives of the eye-space
// position, which are constant across it: exact at every fragment and every tessellation
// level, with nothing stored. It cannot come from a per-vertex array -- a terrain vertex is
// shared by up to six triangles, and the rasterizer interpolates all three corners regardless.
//
// MUST be evaluated in uniform control flow. The material switches in main() branch per
// fragment, and a derivative taken inside one is undefined.
vec3 terrain_triangle_normal()
{
    vec3 n = normalize(cross(dFdx(vary_position), dFdy(vary_position)));
    // The cross follows window-space winding, which a mirrored view or a back-facing patch
    // inverts. The interpolated vertex normal is the reference for which side is out.
    return dot(n, vary_normal) < 0.0 ? -n : n;
}
#endif

vec3 terrain_geometric_normal()
{
#ifdef TERRAIN_FLAT_NORMALS
    return terrain_triangle_normal();
#else
    return vary_normal;
#endif
}

void main()
{
    // Ahead of mirrorClip: a discard can leave the quad without the neighbouring lanes a
    // derivative needs, so every derivative this shader takes is taken while all four are live.
    // The region position's are the ones every projection's uv derivatives come from; the
    // material and projection switches below are then free to branch.
    geom_normal = terrain_geometric_normal();
    TerrainPoint pt;
    pt.p = vary_region_position;
    pt.ddx = dFdx(vary_region_position);
    pt.ddy = dFdy(vary_region_position);
    vec2 region_uv = pt.p.xy / region_scale;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    // The projections are chosen by the surface's normal under the fragment, in region space
    // where the projection planes are the axes, whatever the lighting normal is -- see
    // terrain_facet -- and decided once for every material.
    TerrainTriplanar tw = terrain_triplanar_weights(terrain_facet(pt.p.xy));
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    // The same normal in region space, where the projection planes are the axes and every
    // material's normal is composed. The terrain's modelview is rigid, so the transpose is the
    // inverse. Under TERRAIN_FLAT_NORMALS this is the per-triangle normal, which is what the
    // detail must be composed with: it is what the fragment is lit against.
    vec3 geom_normal_region = transpose(normal_matrix) * geom_normal;
#endif

    // Make sure we clip the terrain if we're in a mirror.
    mirrorClip(vary_position);

    TerrainMix tm;
#if TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    tm = terrain_ramp_mix(alpha_ramp, vary_composition);
#elif TERRAIN_PAINT_TYPE == TERRAIN_PAINT_TYPE_PBR_PAINTMAP
    tm = get_terrain_usage_from_weight3(texture(paint_map, region_uv).xyz);
#endif

#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
    // RGB = Occlusion, Roughness, Metal
    // default values, see LLViewerTexture::sDefaultPBRORMImagep
    //   occlusion 1.0
    //   roughness 0.0
    //   metal     0.0
    vec3[4] orm_factors;
    orm_factors[0] = vec3(1.0, roughnessFactors.x, metallicFactors.x);
    orm_factors[1] = vec3(1.0, roughnessFactors.y, metallicFactors.y);
    orm_factors[2] = vec3(1.0, roughnessFactors.z, metallicFactors.z);
    orm_factors[3] = vec3(1.0, roughnessFactors.w, metallicFactors.w);
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    vec2[4] rm_factors;
    rm_factors[0] = vec2(roughnessFactors.x, metallicFactors.x);
    rm_factors[1] = vec2(roughnessFactors.y, metallicFactors.y);
    rm_factors[2] = vec2(roughnessFactors.z, metallicFactors.z);
    rm_factors[3] = vec2(roughnessFactors.w, metallicFactors.w);
#endif

    PBRMix pbr_mix = init_pbr_mix();
    PBRMix mix2;
    // Each material's fetches happen inside its branch, through textureGrad with the
    // derivatives derived above -- see sample_pbr().
    switch (tm.type & MIX_X)
    {
    case MIX_X:
        mix2 = terrain_sample_and_multiply_pbr(
            pt
            , terrain_uv_transform[0]
            , terrain_uv_offset[0]
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , tw
#endif
            , detail_0_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_0_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_0_normal
            , terrain_normal_axes[0]
            , geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_0_emissive
#endif
            , baseColorFactors[0]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[0]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[0]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[0]
#endif
        );
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.x);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Y)
    {
    case MIX_Y:
        mix2 = terrain_sample_and_multiply_pbr(
            pt
            , terrain_uv_transform[1]
            , terrain_uv_offset[1]
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , tw
#endif
            , detail_1_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_1_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_1_normal
            , terrain_normal_axes[1]
            , geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_1_emissive
#endif
            , baseColorFactors[1]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[1]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[1]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[1]
#endif
        );
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.y);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_Z)
    {
    case MIX_Z:
        mix2 = terrain_sample_and_multiply_pbr(
            pt
            , terrain_uv_transform[2]
            , terrain_uv_offset[2]
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , tw
#endif
            , detail_2_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_2_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_2_normal
            , terrain_normal_axes[2]
            , geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_2_emissive
#endif
            , baseColorFactors[2]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[2]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[2]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[2]
#endif
        );
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.z);
        break;
    default:
        break;
    }
    switch (tm.type & MIX_W)
    {
    case MIX_W:
        mix2 = terrain_sample_and_multiply_pbr(
            pt
            , terrain_uv_transform[3]
            , terrain_uv_offset[3]
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
            , tw
#endif
            , detail_3_base_color
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , detail_3_metallic_roughness
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
            , detail_3_normal
            , terrain_normal_axes[3]
            , geom_normal_region
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , detail_3_emissive
#endif
            , baseColorFactors[3]
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
            , orm_factors[3]
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
            , rm_factors[3]
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
            , emissiveColors[3]
#endif
        );
        pbr_mix = mix_pbr(pbr_mix, mix2, tm.weight.w);
        break;
    default:
        break;
    }

    float minimum_alpha = terrain_mix(tm, minimum_alphas);
    if (pbr_mix.col.a < minimum_alpha)
    {
        discard;
    }
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL)
    // The materials' normals were composed and blended in region space; one transform brings
    // the blend into view space. No facing flip before this point: the one below has to be
    // applied exactly once, and applying it per material as well cancels out on the back faces
    // it exists to correct.
    vec3 tnorm = normalize(normal_matrix * pbr_mix.vNt);
#else
    vec3 tnorm = geom_normal;
#endif
    tnorm *= gl_FrontFacing ? 1.0 : -1.0;


#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE)
#define mix_emissive pbr_mix.emissive
#else
#define mix_emissive vec3(0)
#endif
#if (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION)
#define mix_orm pbr_mix.orm
#elif (TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
#define mix_orm vec3(1.0, pbr_mix.rm)
#else
// Matte plastic potato terrain
#define mix_orm vec3(1.0, 1.0, 0.0)
#endif
    // Terrain is the worst case for this: a ground plane runs to the horizon, so its normal
    // maps reach their minification limit within the visible frame every time.
    vec3 orm_out = mix_orm;
    orm_out.g = filterSpecularRoughness(orm_out.g, tnorm);

    vec3 base_color = pbr_mix.col.xyz;
    if (show_parcel_owners != 0)
    {
        // The overlay's texels are encoded; the blend is in the linear space
        // the material mix above happens in.
        vec4 overlay = texture(parcel_overlay, region_uv);
        base_color = mix(base_color, srgb_to_linear(overlay.rgb), overlay.a);
    }

    frag_data[0] = max(vec4(base_color, 0.0), vec4(0));                                                   // Diffuse
    // Alpha is zero, as every other PBR GBuffer writer leaves it. Nothing reads this channel for
    // a fragment flagged GBUFFER_FLAG_HAS_PBR -- softenLightF and the local lights take spec.a
    // as legacy glossiness, and every one of those reads sits behind a non-PBR branch.
    frag_data[1] = packORM(max(orm_out.rgb, vec3(0)));  // Occlusion, Roughness (green+alpha), Metal
    // geom_normal is already the un-perturbed surface normal here, and under
    // TERRAIN_FLAT_NORMALS it is the exact per-triangle one.
    frag_data[2] = encodeNormalGeo(tnorm, geom_normal * (gl_FrontFacing ? 1.0 : -1.0), GBUFFER_FLAG_HAS_PBR);

#if defined(HAS_EMISSIVE)
    frag_data[3] = max(vec4(mix_emissive,0), vec4(0));                                                // PBR linear Emissive (sampler-decoded, float attachment stores it verbatim)
#endif
}

