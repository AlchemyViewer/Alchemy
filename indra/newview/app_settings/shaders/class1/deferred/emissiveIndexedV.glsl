/**
 * @file emissiveIndexedV.glsl
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

// Indexed (multi-material) variant of emissiveV.glsl (legacy glow). Forwards the
// per-vertex material slot to the fragment shader so each slot's diffuse alpha can
// be sampled. The diffuse texture transform is baked into texcoord0 at buffer build
// (indexed faces exclude texture animation), so texture_matrix0 is not applied.
// HAS_SKIN adds rigged matrix-palette skinning.

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif

in vec3 position;
in vec4 emissive;
in vec2 texcoord0;
in int texture_index;

flat out int vary_material_index;
out vec4 vertex_color;
out vec2 vary_texcoord0;

void main()
{
#ifdef HAS_SKIN
    vec3 pos = skinTransformH(getSkinBlend(), position.xyz, modelview_matrix).xyz;
    gl_Position = projection_matrix*vec4(pos,1.0);
#else
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
#endif

    vary_material_index = texture_index;
    vary_texcoord0 = texcoord0;
    vertex_color = emissive;
}
