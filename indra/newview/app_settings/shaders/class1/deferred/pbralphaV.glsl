/**
 * @file class1\deferred\pbralphaV.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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


// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
#ifndef IS_HUD

// default alpha implementation

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif

#if !defined(HAS_SKIN)
#endif

out vec3 vary_position;

uniform vec4[2] texture_base_color_transform;
uniform vec4[2] texture_normal_transform;
uniform vec4[2] texture_metallic_roughness_transform;
uniform vec4[2] texture_emissive_transform;

out vec3 vary_fragcoord;

in vec3 position;
in vec4 diffuse_color;
in vec3 normal;
in vec4 tangent;
in vec2 texcoord0;

out vec2 base_color_texcoord;
out vec2 normal_texcoord;
out vec2 metallic_roughness_texcoord;
out vec2 emissive_texcoord;

out vec4 vertex_color;

out vec3 vary_tangent;
flat out float vary_sign;
out vec3 vary_normal;

vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);
vec4 tangent_space_transform(vec4 vertex_tangent, vec3 vertex_normal, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);


void main()
{
#ifdef HAS_SKIN
    mat3x4 skin = getSkinBlend();
    vec3 pos = skinTransformH(skin, position.xyz, modelview_matrix).xyz;
    vary_position = pos;
    vec4 vert = projection_matrix * vec4(pos,1.0);
#else
    //transform vertex
    vec4 vert = modelview_projection_matrix * vec4(position.xyz, 1.0);
#endif
    gl_Position = vert;

    // FS derives a screen UV as vary_fragcoord.xy / .z; carry true clip w under reverse-Z
    // so the divide stays clip.xy/clip.w == ndc.xy (clip.z -> ~0 at the far plane).
#ifdef REVERSE_Z
    vary_fragcoord.xyz = vec3(vert.xy, vert.w);
#else
    vary_fragcoord.xyz = vert.xyz;
#endif

    base_color_texcoord = texture_transform(texcoord0, texture_base_color_transform, texture_matrix0);
    normal_texcoord = texture_transform(texcoord0, texture_normal_transform, texture_matrix0);
    metallic_roughness_texcoord = texture_transform(texcoord0, texture_metallic_roughness_transform, texture_matrix0);
    emissive_texcoord = texture_transform(texcoord0, texture_emissive_transform, texture_matrix0);

#ifdef HAS_SKIN
    vec3 n = mat3(modelview_matrix) * skinDirection(skin, normal.xyz);
    vec3 t = mat3(modelview_matrix) * skinDirection(skin, tangent.xyz);
#else //HAS_SKIN
    vec3 n = normal_matrix * normal;
    vec3 t = normal_matrix * tangent.xyz;
#endif //HAS_SKIN

    n = normalize(n);

    vec4 transformed_tangent = tangent_space_transform(vec4(t, tangent.w), n, texture_normal_transform, texture_matrix0);
    vary_tangent = normalize(transformed_tangent.xyz);
    vary_sign = transformed_tangent.w;
    vary_normal = n;

    vertex_color = diffuse_color;

#if !defined(HAS_SKIN)
    vary_position = (modelview_matrix*vec4(position.xyz, 1.0)).xyz;
#endif
}

#else

// fullbright HUD alpha implementation




out vec3 vary_position;

uniform vec4[2] texture_base_color_transform;
uniform vec4[2] texture_emissive_transform;

in vec3 position;
in vec4 diffuse_color;
in vec2 texcoord0;

out vec2 base_color_texcoord;
out vec2 emissive_texcoord;

out vec4 vertex_color;

vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);


void main()
{
    //transform vertex
    vec4 vert = modelview_projection_matrix * vec4(position.xyz, 1.0);
    gl_Position = vert;
    vary_position = vert.xyz;

    base_color_texcoord = texture_transform(texcoord0, texture_base_color_transform, texture_matrix0);
    emissive_texcoord = texture_transform(texcoord0, texture_emissive_transform, texture_matrix0);

    vertex_color = diffuse_color;
}

#endif
