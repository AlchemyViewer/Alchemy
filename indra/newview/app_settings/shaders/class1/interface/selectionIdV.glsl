/**
 * @file selectionIdV.glsl
 * @brief The selection outline's id pass (ALSelectionOutline): a selected face, and where on its texture it is.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

in vec3 position;
in vec2 texcoord0;

out vec2 vary_texcoord0;

// A GLTF face's base colour transform (KHR_texture_transform); the identity for a legacy face, whose texture
// coordinates have their transform in them. texture_matrix0 is the face's texture animation where it is drawn
// rather than baked into those coordinates.
uniform vec4[2] texture_base_color_transform;
vec2 texture_transform(vec2 vertex_texcoord, vec4[2] khr_gltf_transform, mat4 sl_animation_transform);

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#endif

void main()
{
#ifdef HAS_SKIN
    vec4 pos = skinTransformH(getSkinBlend(), position.xyz, modelview_matrix);
    gl_Position = projection_matrix * pos;
#else
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
#endif
    vary_texcoord0 = texture_transform(texcoord0, texture_base_color_transform, texture_matrix0);
}
