/**
 * @file materialShadowIndexedF.glsl
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

// Indexed (multi-material) shadow alpha-mask fragment shader for legacy materials.
// Each material slot owns a diffuse sampler bound by the CPU to unit s and its own
// alpha cutoff, matching the scalar material shadow (which sets minimum_alpha per
// batch from mAlphaMaskCutoff).

out vec4 frag_color;

flat in int vary_material_index;
in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;

uniform float mat_minimum_alpha[GLTF_INDEXED_CHANNELS];

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

float sample_diffuse_alpha(vec2 uv)
{
    if (vary_material_index == 0) return texture(diffuse0, uv).a;
#if GLTF_INDEXED_CHANNELS > 1
    if (vary_material_index == 1) return texture(diffuse1, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 2
    if (vary_material_index == 2) return texture(diffuse2, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 3
    if (vary_material_index == 3) return texture(diffuse3, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 4
    if (vary_material_index == 4) return texture(diffuse4, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 5
    if (vary_material_index == 5) return texture(diffuse5, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 6
    if (vary_material_index == 6) return texture(diffuse6, uv).a;
#endif
#if GLTF_INDEXED_CHANNELS > 7
    if (vary_material_index == 7) return texture(diffuse7, uv).a;
#endif
    return 1.0;
}

void main()
{
    if (sample_diffuse_alpha(vary_texcoord0.xy) < mat_minimum_alpha[vary_material_index])
    {
        discard;
    }

    frag_color = vec4(1, 1, 1, 1);
}
