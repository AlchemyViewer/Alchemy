/**
 * @file pbrShadowAlphaMaskIndexedF.glsl
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

// Indexed (multi-material) variant of pbrShadowAlphaMaskF. Each material slot owns
// a base-color sampler bound by the CPU to unit s, and a per-slot alpha cutoff.

out vec4 frag_color;

flat in int vary_material_index;
in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;

uniform float gltf_minimum_alpha[GLTF_INDEXED_CHANNELS];

uniform sampler2D basecolor0;
#if GLTF_INDEXED_CHANNELS > 1
uniform sampler2D basecolor1;
#endif
#if GLTF_INDEXED_CHANNELS > 2
uniform sampler2D basecolor2;
#endif
#if GLTF_INDEXED_CHANNELS > 3
uniform sampler2D basecolor3;
#endif
#if GLTF_INDEXED_CHANNELS > 4
uniform sampler2D basecolor4;
#endif
#if GLTF_INDEXED_CHANNELS > 5
uniform sampler2D basecolor5;
#endif
#if GLTF_INDEXED_CHANNELS > 6
uniform sampler2D basecolor6;
#endif
#if GLTF_INDEXED_CHANNELS > 7
uniform sampler2D basecolor7;
#endif

float sample_alpha(vec2 uv)
{
    if (vary_material_index == 0) return texture(basecolor0, uv).a;
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(basecolor1, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(basecolor2, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(basecolor3, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(basecolor4, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(basecolor5, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(basecolor6, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(basecolor7, uv).a;
#endif
    return 1.0;
}

void main()
{
    float alpha = sample_alpha(vary_texcoord0.xy) * vertex_color.a;

    if (alpha < gltf_minimum_alpha[vary_material_index])
    {
        discard;
    }

    frag_color = vec4(1, 1, 1, 1);
}
