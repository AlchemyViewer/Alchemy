/**
 * @file materialIndexedF.glsl
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

// Indexed (multi-material) variant of the legacy Blinn-Phong material GBuffer write
// (the non-blend path of materialF.glsl). One draw call covers up to
// GLTF_INDEXED_CHANNELS materials selected per-vertex by vary_material_index. Each
// slot owns its diffuse (and, per permutation, normal/spec) sampler bound to units
// [s], [N+s], [2N+s]; per-material scalars arrive as uniform arrays. Forward/alpha
// shading lives in materialF.glsl and is not batched here.

#define DIFFUSE_ALPHA_MODE_NONE     0
#define DIFFUSE_ALPHA_MODE_BLEND    1
#define DIFFUSE_ALPHA_MODE_MASK     2
#define DIFFUSE_ALPHA_MODE_EMISSIVE 3

out vec4 frag_data[4];

in vec3 vary_position;
in vec4 vertex_color;
in vec2 vary_texcoord0;
flat in int vary_material_index;

in vec3 vary_normal;
#ifdef HAS_NORMAL_MAP
in vec3 vary_tangent;
flat in float vary_sign;
in vec2 vary_texcoord1;
#endif
#ifdef HAS_SPECULAR_MAP
in vec2 vary_texcoord2;
#endif

uniform vec4  mat_specular_color[GLTF_INDEXED_CHANNELS]; // rgb = specular color, a = glossiness/exponent

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);
uniform float mat_env_intensity[GLTF_INDEXED_CHANNELS];
uniform float mat_emissive_brightness[GLTF_INDEXED_CHANNELS]; // fullbright flag
#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
uniform float mat_minimum_alpha[GLTF_INDEXED_CHANNELS];
#endif

uniform sampler2D diffuse0;
#if GLTF_INDEXED_CHANNELS > 1
uniform sampler2D diffuse1;
#endif
#if GLTF_INDEXED_CHANNELS > 2
uniform sampler2D diffuse2;
#endif
#if GLTF_INDEXED_CHANNELS > 3
uniform sampler2D diffuse3;
#endif
#if GLTF_INDEXED_CHANNELS > 4
uniform sampler2D diffuse4;
#endif
#if GLTF_INDEXED_CHANNELS > 5
uniform sampler2D diffuse5;
#endif
#if GLTF_INDEXED_CHANNELS > 6
uniform sampler2D diffuse6;
#endif
#if GLTF_INDEXED_CHANNELS > 7
uniform sampler2D diffuse7;
#endif

vec4 sample_diffuse(vec2 uv)
{
    if (vary_material_index == 0) return texture(diffuse0, uv);
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(diffuse1, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(diffuse2, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(diffuse3, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(diffuse4, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(diffuse5, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(diffuse6, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(diffuse7, uv);
#endif
    return vec4(1, 0, 1, 1);
}

#ifdef HAS_NORMAL_MAP
uniform sampler2D bump0;
#if GLTF_INDEXED_CHANNELS > 1
uniform sampler2D bump1;
#endif
#if GLTF_INDEXED_CHANNELS > 2
uniform sampler2D bump2;
#endif
#if GLTF_INDEXED_CHANNELS > 3
uniform sampler2D bump3;
#endif
#if GLTF_INDEXED_CHANNELS > 4
uniform sampler2D bump4;
#endif
#if GLTF_INDEXED_CHANNELS > 5
uniform sampler2D bump5;
#endif
#if GLTF_INDEXED_CHANNELS > 6
uniform sampler2D bump6;
#endif
#if GLTF_INDEXED_CHANNELS > 7
uniform sampler2D bump7;
#endif

vec4 sample_bump(vec2 uv)
{
    if (vary_material_index == 0) return texture(bump0, uv);
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(bump1, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(bump2, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(bump3, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(bump4, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(bump5, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(bump6, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(bump7, uv);
#endif
    return vec4(0.5, 0.5, 1.0, 1.0);
}
#endif

#ifdef HAS_SPECULAR_MAP
uniform sampler2D spec0;
#if GLTF_INDEXED_CHANNELS > 1
uniform sampler2D spec1;
#endif
#if GLTF_INDEXED_CHANNELS > 2
uniform sampler2D spec2;
#endif
#if GLTF_INDEXED_CHANNELS > 3
uniform sampler2D spec3;
#endif
#if GLTF_INDEXED_CHANNELS > 4
uniform sampler2D spec4;
#endif
#if GLTF_INDEXED_CHANNELS > 5
uniform sampler2D spec5;
#endif
#if GLTF_INDEXED_CHANNELS > 6
uniform sampler2D spec6;
#endif
#if GLTF_INDEXED_CHANNELS > 7
uniform sampler2D spec7;
#endif

vec4 sample_spec(vec2 uv)
{
    if (vary_material_index == 0) return texture(spec0, uv);
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(spec1, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(spec2, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(spec3, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(spec4, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(spec5, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(spec6, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(spec7, uv);
#endif
    return vec4(1.0);
}
#endif

void mirrorClip(vec3 pos);
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);

vec3 getNormal(int mi, inout float glossiness)
{
#ifdef HAS_NORMAL_MAP
    vec4 vNt = sample_bump(vary_texcoord1.xy);
    glossiness *= vNt.a;
    vNt.xyz = vNt.xyz * 2 - 1;
    float sign = vary_sign;
    vec3 vN = vary_normal;
    vec3 vT = vary_tangent.xyz;
    vec3 vB = sign * cross(vN, vT);
    vec3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );
    return tnorm;
#else
    return normalize(vary_normal);
#endif
}

vec4 getSpecular(int mi)
{
#ifdef HAS_SPECULAR_MAP
    vec4 spec = sample_spec(vary_texcoord2.xy);
    // The map is decoded on the sampler (filtered in linear), so linearise the tint to
    // match. Yields LINEAR spec: the forward path lights with it directly, the deferred
    // writer re-encodes to sRGB for the shared RGBA8 store.
    spec.rgb *= srgb_to_linear(mat_specular_color[mi].rgb);
#else
    vec4 spec = vec4(srgb_to_linear(mat_specular_color[mi].rgb), 1.0);
#endif
    return spec;
}

float getEmissive(int mi, vec4 diffcol)
{
#if (DIFFUSE_ALPHA_MODE != DIFFUSE_ALPHA_MODE_EMISSIVE)
    return mat_emissive_brightness[mi];
#else
    return max(diffcol.a, mat_emissive_brightness[mi]);
#endif
}

void main()
{
    mirrorClip(vary_position);

    int mi = vary_material_index;

    vec4 diffcol = sample_diffuse(vary_texcoord0.xy);
    diffcol.rgb *= vertex_color.rgb;

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
    float bias = 0.001953125; // 1/512, half an 8-bit quantization
    if (diffcol.a < mat_minimum_alpha[mi] - bias)
    {
        discard;
    }
#endif

    vec4 spec = getSpecular(mi);
    float env = mat_env_intensity[mi] * spec.a;
    float glossiness = mat_specular_color[mi].a;
    vec3 norm = getNormal(mi, glossiness);

    float emissive = getEmissive(mi, diffcol);

    float flag = GBUFFER_FLAG_HAS_ATMOS;

    frag_data[0] = max(vec4(diffcol.rgb, emissive), vec4(0)); // gbuffer is sRGB for legacy materials
    // frag_data[1] is a plain RGBA8 shared with PBR's linear ORM, so it cannot be sRGB
    // storage. The spec was FILTERED in linear (sampler decode); re-encode it here so it
    // lands sRGB the way softenLight reads it. Storage constraint, not a filtering one.
    frag_data[1] = max(vec4(linear_to_srgb(spec.rgb), glossiness), vec4(0));  // XYZ = Specular color. W = Specular exponent.
    frag_data[2] = encodeNormal(norm, env, flag);             // XY = Normal. Z = Env intensity.

#if defined(HAS_EMISSIVE)
    frag_data[3] = vec4(0, 0, 0, 0);
#endif
}
