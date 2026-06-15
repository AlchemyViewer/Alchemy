/**
 * @file emissiveIndexedF.glsl
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

// Indexed (multi-material) variant of emissiveF.glsl (legacy glow). Only alpha is
// written. vary_material_index selects this primitive's diffuse map (bound to unit
// s); its alpha modulates the per-vertex glow factor. If-chains (not switch) are
// used for driver portability.

out vec4 frag_color;

in vec4 vertex_color;
in vec2 vary_texcoord0;
flat in int vary_material_index;

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

void main()
{
    // NOTE: when this shader is used, only alpha is being written to
    float a = sample_diffuse(vary_texcoord0.xy).a * vertex_color.a;
    frag_color = max(vec4(0, 0, 0, a), vec4(0));
}
