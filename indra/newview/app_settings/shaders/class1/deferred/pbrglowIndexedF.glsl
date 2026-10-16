/**
 * @file pbrglowIndexedF.glsl
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

// NOTE: base colour and emissive are NOT converted here. Their samplers ask the hardware
// to decode (GLTF_COLOR_SAMPLER, pushGLTFBatchIndexed), which converts each texel before
// filtering rather than after -- converting a blend taken in sRGB space is what made
// minified albedo read too dark. Doing it here as well would convert twice.

/*[EXTRA_CODE_HERE]*/

// Indexed (multi-material) PBR glow/emissive fragment shader. vary_material_index
// selects this primitive's material slot; each slot binds its base-color map to
// unit s and its emissive map to unit 3N+s (N == GLTF_INDEXED_CHANNELS), matching
// the GBuffer indexed layout so pushGLTFBatchIndexed can drive both. If-chains
// (not switch) are used for driver portability. See pbrglowF.glsl for the
// single-material equivalent.

uniform vec3  gltf_emissive_color[GLTF_INDEXED_CHANNELS];
uniform float gltf_minimum_alpha[GLTF_INDEXED_CHANNELS]; // PBR alphaMode MASK cutoff, -1 for opaque

uniform sampler2D basecolor0;    // always in sRGB space
uniform sampler2D emissivemap0;
#if GLTF_INDEXED_CHANNELS > 1
uniform sampler2D basecolor1; uniform sampler2D emissivemap1;
#endif
#if GLTF_INDEXED_CHANNELS > 2
uniform sampler2D basecolor2; uniform sampler2D emissivemap2;
#endif
#if GLTF_INDEXED_CHANNELS > 3
uniform sampler2D basecolor3; uniform sampler2D emissivemap3;
#endif
#if GLTF_INDEXED_CHANNELS > 4
uniform sampler2D basecolor4; uniform sampler2D emissivemap4;
#endif
#if GLTF_INDEXED_CHANNELS > 5
uniform sampler2D basecolor5; uniform sampler2D emissivemap5;
#endif
#if GLTF_INDEXED_CHANNELS > 6
uniform sampler2D basecolor6; uniform sampler2D emissivemap6;
#endif
#if GLTF_INDEXED_CHANNELS > 7
uniform sampler2D basecolor7; uniform sampler2D emissivemap7;
#endif

out vec4 frag_color;

in vec4 vertex_emissive;
flat in int vary_material_index;

in vec2 base_color_texcoord;
in vec2 emissive_texcoord;

vec3 linear_to_srgb(vec3 c);

vec4 sample_basecolor(vec2 uv)
{
    if (vary_material_index == 0) return texture(basecolor0, uv);
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(basecolor1, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(basecolor2, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(basecolor3, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(basecolor4, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(basecolor5, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(basecolor6, uv);
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(basecolor7, uv);
#endif
    return vec4(1, 0, 1, 1);
}

vec3 sample_emissive(vec2 uv)
{
    if (vary_material_index == 0) return texture(emissivemap0, uv).rgb;
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(emissivemap1, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(emissivemap2, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(emissivemap3, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(emissivemap4, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(emissivemap5, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(emissivemap6, uv).rgb;
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(emissivemap7, uv).rgb;
#endif
    return vec3(1.0);
}

void main()
{
    int mi = vary_material_index;

    vec4 basecolor = sample_basecolor(base_color_texcoord.xy).rgba;

    if (basecolor.a < gltf_minimum_alpha[mi])
    {
        discard;
    }

    vec3 emissive = gltf_emissive_color[mi];
    emissive *= sample_emissive(emissive_texcoord.xy);

    float lum = max(max(emissive.r, emissive.g), emissive.b);
    lum *= vertex_emissive.a;

    frag_color.rgb = vec3(0);
    frag_color.a = lum;
}
