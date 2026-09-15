/**
 * @file class1\deferred\terrainF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

out vec4 frag_data[4];

uniform sampler2D detail_0;
uniform sampler2D detail_1;
uniform sampler2D detail_2;
uniform sampler2D detail_3;
uniform sampler2D alpha_ramp;
uniform sampler2D parcel_overlay;
uniform int show_parcel_owners;
uniform float region_scale;

in vec3 pos;
in vec3 vary_normal;
in vec4 vary_texcoord0;
in vec4 vary_texcoord1;
in vec2 vary_region_uv;
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
in vec4 vary_texcoord_side;
#endif

void mirrorClip(vec3 position);
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);
vec3 srgb_to_linear(vec3 cs);

// The four detail textures blend by the same weights, projections and cells the PBR terrain's
// materials do; those live in pbrterrainUtilF.glsl, linked into this program as well. The bit
// layout matches its.
#define SAMPLE_X 1 << 0
#define SAMPLE_Y 1 << 1
#define SAMPLE_Z 1 << 2
#define MIX_X    1 << 3
#define MIX_Y    1 << 4
#define MIX_Z    1 << 5
#define MIX_W    1 << 6

struct TerrainMix
{
    vec4 weight;
    int type;
};
TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal);

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
struct TerrainTriplanar
{
    vec3 weight;
    int type;
    float sx;
    float sy;
};
TerrainTriplanar terrain_triplanar_weights(vec3 facet_region);
// terrainSurface.glsl
vec3 terrain_facet(vec2 p_region);
#endif

#ifdef TERRAIN_HEX_TILING
struct HexTile
{
    ivec2 corner[3];
    vec3 weight;
    int type;
};
HexTile hex_tile(vec2 uv);
mat2 hex_cell(ivec2 corner, vec2 uv, out vec2 st);
float hex_luma(vec3 rgb);
vec3 hex_weights(HexTile ht, vec3 luma);
#endif

// One detail texture at one uv. Gradients arrive as parameters: every call sits inside a
// branch on which textures and which projections cover the fragment, where an implicit-LOD
// fetch has no derivative to take. See fetch_pbr() in pbrterrainUtilF.glsl.
vec4 detail_fetch(sampler2D tex, vec2 uv, vec2 uv_ddx, vec2 uv_ddy)
{
#ifdef TERRAIN_HEX_TILING
    HexTile ht = hex_tile(uv);
    vec4 cell[3];
    vec3 luma = vec3(0.0);
    for (int i = 0; i < 3; ++i)
    {
        cell[i] = vec4(0.0);
        if ((ht.type & (1 << i)) != 0)
        {
            vec2 st;
            mat2 rot = hex_cell(ht.corner[i], uv, st);
            cell[i] = textureGrad(tex, st, rot * uv_ddx, rot * uv_ddy);
            luma[i] = hex_luma(cell[i].rgb);
        }
    }
    vec3 w = hex_weights(ht, luma);
    return cell[0] * w.x + cell[1] * w.y + cell[2] * w.z;
#else
    return textureGrad(tex, uv, uv_ddx, uv_ddy);
#endif
}

#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
// One detail texture over the projections the surface faces. The side slices are the yz and
// xz uv with u negated on the far side of the axis, which is the frame the PBR slices use: a
// legacy texture has no rotation, so the flip is the sign and nothing else.
vec4 detail_sample(sampler2D tex, TerrainTriplanar tw, float sx, float sy
    , vec2 uv, vec2 uv_ddx, vec2 uv_ddy
    , vec4 side, vec4 side_ddx, vec4 side_ddy)
{
    vec4 c = vec4(0.0);
    if ((tw.type & SAMPLE_X) != 0)
    {
        vec2 flip = vec2(sx, 1.0);
        c += detail_fetch(tex, flip * side.xy, flip * side_ddx.xy, flip * side_ddy.xy) * tw.weight.x;
    }
    if ((tw.type & SAMPLE_Y) != 0)
    {
        vec2 flip = vec2(-sy, 1.0);
        c += detail_fetch(tex, flip * side.zw, flip * side_ddx.zw, flip * side_ddy.zw) * tw.weight.y;
    }
    if ((tw.type & SAMPLE_Z) != 0)
    {
        c += detail_fetch(tex, uv, uv_ddx, uv_ddy) * tw.weight.z;
    }
    return c;
}
#define TERRAIN_DETAIL(tex) detail_sample(tex, tw, sx, sy, vary_texcoord0.xy, uv_ddx, uv_ddy, vary_texcoord_side, side_ddx, side_ddy)
#else
#define TERRAIN_DETAIL(tex) detail_fetch(tex, vary_texcoord0.xy, uv_ddx, uv_ddy)
#endif

void main()
{
    // Every derivative this shader takes, taken here in uniform control flow and ahead of
    // mirrorClip's discard, which can cost the quad the lanes a derivative needs.
    vec2 uv_ddx = dFdx(vary_texcoord0.xy);
    vec2 uv_ddy = dFdy(vary_texcoord0.xy);
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    vec4 side_ddx = dFdx(vary_texcoord_side);
    vec4 side_ddy = dFdy(vary_texcoord_side);
#endif

    mirrorClip(pos);

    float alpha1 = texture(alpha_ramp, vary_texcoord0.zw).a;
    float alpha2 = texture(alpha_ramp,vary_texcoord1.xy).a;
    float alphaFinal = texture(alpha_ramp, vary_texcoord1.zw).a;
    // The ramps' four-way mix, as weights, with the ones under the threshold dropped and the
    // rest renormalised, so a texture that would barely show is not fetched at all.
    TerrainMix tm = get_terrain_mix_weights(alpha1, alpha2, alphaFinal);
#if TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT == 3
    // By the surface's normal under the fragment, in region space where the projection planes
    // are the axes; see terrain_facet.
    TerrainTriplanar tw = terrain_triplanar_weights(terrain_facet(vary_region_uv * region_scale));
    // Which side of each axis the surface faces, as +-1; the same choice the PBR slices make.
    float sx = tw.sx;
    float sy = tw.sy;
#endif

    vec4 outColor = vec4(0.0);
    if ((tm.type & MIX_X) != 0)
    {
        outColor += TERRAIN_DETAIL(detail_0) * tm.weight.x;
    }
    if ((tm.type & MIX_Y) != 0)
    {
        outColor += TERRAIN_DETAIL(detail_1) * tm.weight.y;
    }
    if ((tm.type & MIX_Z) != 0)
    {
        outColor += TERRAIN_DETAIL(detail_2) * tm.weight.z;
    }
    if ((tm.type & MIX_W) != 0)
    {
        outColor += TERRAIN_DETAIL(detail_3) * tm.weight.w;
    }

    if (show_parcel_owners != 0)
    {
        // The overlay's texels are encoded; the blend is in the linear space
        // the detail blend above happens in.
        vec4 overlay = texture(parcel_overlay, vary_region_uv);
        outColor.rgb = mix(outColor.rgb, srgb_to_linear(overlay.rgb), overlay.a);
    }

    outColor.a = 0.0; // yes, downstream atmospherics

    frag_data[0] = max(outColor, vec4(0));
    frag_data[1] = vec4(0.0,0.0,0.0,-1.0);
    vec3 nvn = normalize(vary_normal);
    frag_data[2] = encodeNormal(nvn.xyz, 0, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    frag_data[3] = vec4(0);
#endif
}

