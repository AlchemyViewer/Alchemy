/**
 * @file pbgglowV.glsl
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


#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

uniform vec4[2] texture_base_color_transform;
uniform vec4[2] texture_emissive_transform;

in vec3 position;
in vec4 emissive;

in vec2 texcoord0;

out vec2 base_color_texcoord;
out vec2 emissive_texcoord;

out vec4 vertex_emissive;

vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);

void main()
{
#ifdef HAS_SKIN
    vec3 pos = skinTransformH(getSkinBlend(), position.xyz, modelview_matrix).xyz;

    gl_Position = projection_matrix*vec4(pos,1.0);
#else
    //transform vertex
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
#endif

    base_color_texcoord = texture_transform(texcoord0, texture_base_color_transform, texture_matrix0);
    emissive_texcoord = texture_transform(texcoord0, texture_emissive_transform, texture_matrix0);

    vertex_emissive = emissive;
}

