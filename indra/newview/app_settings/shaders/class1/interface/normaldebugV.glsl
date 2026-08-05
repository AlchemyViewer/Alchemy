/**
 * @file normaldebugV.glsl
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

in vec3 position;
in vec3 normal;
out vec4 normal_g;
#ifdef HAS_ATTRIBUTE_TANGENT
in vec4 tangent;
out vec4 tangent_g;
#endif

uniform float debug_normal_draw_length;

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif
// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

// *NOTE: Should use the modelview_projection_matrix here in the non-skinned
// case for efficiency, but opting for the simplier implementation for now as
// this is debug code.
//
// The direction arrives already in view space, so this no longer recovers it by
// subtracting two transformed points -- that subtraction was the ill-conditioned step
// (see the precision contract in avatar/objectSkinV.glsl).
vec4 get_screen_normal(vec4 view_pos, vec3 view_dir)
{
    vec4 world_norm = view_pos;
    world_norm.xyz += debug_normal_draw_length * normalize(view_dir);
    return projection_matrix * world_norm;
}

void main()
{
#ifdef HAS_SKIN
    mat3x4 skin = getSkinBlend();
    vec4 world_pos = skinTransformH(skin, position.xyz, modelview_matrix);
    vec3 view_normal = mat3(modelview_matrix) * skinDirection(skin, normal.xyz);
#ifdef HAS_ATTRIBUTE_TANGENT
    vec3 view_tangent = mat3(modelview_matrix) * skinDirection(skin, tangent.xyz);
#endif
#else
    vec4 world_pos = modelview_matrix * vec4(position.xyz, 1.0);
    vec3 view_normal = mat3(modelview_matrix) * normal.xyz;
#ifdef HAS_ATTRIBUTE_TANGENT
    vec3 view_tangent = mat3(modelview_matrix) * tangent.xyz;
#endif
#endif

    gl_Position = projection_matrix * world_pos;
    normal_g = get_screen_normal(world_pos, view_normal);
#ifdef HAS_ATTRIBUTE_TANGENT
    tangent_g = get_screen_normal(world_pos, view_tangent);
#endif
}

