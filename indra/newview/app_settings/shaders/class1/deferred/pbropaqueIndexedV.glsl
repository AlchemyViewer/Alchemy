/**
 * @file pbropaqueIndexedV.glsl
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

// Indexed (multi-material) PBR opaque GBuffer-write vertex shader.
// One draw call covers up to GLTF_INDEXED_CHANNELS materials. The per-vertex
// material slot arrives in the texture_index attribute (packed in position.w);
// it selects this vertex's KHR_texture_transform set and is forwarded to the
// fragment shader as vary_material_index. See pbropaqueV.glsl for the
// single-material equivalent.

uniform mat4 modelview_matrix;
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;

// Per-material KHR_texture_transform, two vec4 (packed scale/rotation/offset)
// per slot. Indexed by the material slot (texture_index).
uniform vec4 gltf_basecolor_transform[2*GLTF_INDEXED_CHANNELS];
uniform vec4 gltf_normal_transform[2*GLTF_INDEXED_CHANNELS];
uniform vec4 gltf_mr_transform[2*GLTF_INDEXED_CHANNELS];
uniform vec4 gltf_emissive_transform[2*GLTF_INDEXED_CHANNELS];

in vec3 position;
in vec4 diffuse_color;
in vec3 normal;
in vec4 tangent;
in vec2 texcoord0;
in int texture_index;

flat out int vary_material_index;

out vec2 base_color_texcoord;
out vec2 normal_texcoord;
out vec2 metallic_roughness_texcoord;
out vec2 emissive_texcoord;

out vec4 vertex_color;

out vec3 vary_tangent;
flat out float vary_sign;
out vec3 vary_normal;
out vec3 vary_position;

vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);
vec4 tangent_space_transform(vec4 vertex_tangent, vec3 vertex_normal, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);

void main()
{
    vary_position = (modelview_matrix * vec4(position.xyz, 1.0)).xyz;
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);

    int mi = texture_index;
    vary_material_index = mi;

    // Indexed faces never carry texture animation (excluded from batching), so
    // the SL animation transform is identity.
    mat4 ident = mat4(1.0);

    vec4 bc[2]; bc[0] = gltf_basecolor_transform[2*mi]; bc[1] = gltf_basecolor_transform[2*mi+1];
    vec4 nm[2]; nm[0] = gltf_normal_transform[2*mi];    nm[1] = gltf_normal_transform[2*mi+1];
    vec4 mr[2]; mr[0] = gltf_mr_transform[2*mi];        mr[1] = gltf_mr_transform[2*mi+1];
    vec4 em[2]; em[0] = gltf_emissive_transform[2*mi];  em[1] = gltf_emissive_transform[2*mi+1];

    base_color_texcoord         = texture_transform(texcoord0, bc, ident);
    normal_texcoord             = texture_transform(texcoord0, nm, ident);
    metallic_roughness_texcoord = texture_transform(texcoord0, mr, ident);
    emissive_texcoord           = texture_transform(texcoord0, em, ident);

    vec3 n = normalize(normal_matrix * normal);
    vec3 t = normal_matrix * tangent.xyz;

    vec4 transformed_tangent = tangent_space_transform(vec4(t, tangent.w), n, nm, ident);
    vary_tangent = normalize(transformed_tangent.xyz);
    vary_sign = transformed_tangent.w;
    vary_normal = n;

    vertex_color = diffuse_color;
}
