/**
 * @file pbrShadowAlphaMaskIndexedV.glsl
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

// Indexed (multi-material) variant of pbrShadowAlphaMaskV. The per-vertex material
// slot (texture_index) selects this vertex's base-color transform and is forwarded
// to the fragment shader for per-slot alpha testing. The HAS_SKIN variant adds
// rigged-mesh matrix-palette skinning.

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
#if defined(HAS_SKIN)
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif

uniform float shadow_target_width;

uniform vec4 gltf_basecolor_transform[2*GLTF_INDEXED_CHANNELS];
vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);

in vec3 position;
in vec4 diffuse_color;
in vec2 texcoord0;
in int texture_index;

flat out int vary_material_index;
out vec4 post_pos;
out float target_pos_x;
out vec4 vertex_color;
out vec2 vary_texcoord0;

void main()
{
#if defined(HAS_SKIN)
    vec4 pre_pos = vec4(position.xyz, 1.0);
    vec4 pos = skinTransformH(getSkinBlend(), pre_pos.xyz, modelview_matrix);
    pos = projection_matrix * pos;
#else
    vec4 pre_pos = vec4(position.xyz, 1.0);
    vec4 pos = modelview_projection_matrix * pre_pos;
#endif

    target_pos_x = 0.5 * (shadow_target_width - 1.0) * pos.x;
    post_pos = pos;
    gl_Position = pos;

    int mi = texture_index;
    vary_material_index = mi;

    // Indexed faces never carry texture animation, so identity SL transform.
    mat4 ident = mat4(1.0);
    vec4 bc[2]; bc[0] = gltf_basecolor_transform[2*mi]; bc[1] = gltf_basecolor_transform[2*mi+1];
    vary_texcoord0 = texture_transform(texcoord0, bc, ident);

    vertex_color = diffuse_color;
}
