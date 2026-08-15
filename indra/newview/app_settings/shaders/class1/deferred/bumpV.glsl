/**
 * @file bumpV.glsl
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

in vec3 position;
in vec4 diffuse_color;
in vec3 normal;
in vec2 texcoord0;
in vec4 tangent;

out vec3 vary_mat0;
out vec3 vary_mat1;
out vec3 vary_mat2;
out vec4 vertex_color;
out vec2 vary_texcoord0;
out vec3 vary_position;

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#endif

// Linearises an sRGB prim tint for a pass that shades in linear. Defined in
// deferred/textureUtilV.glsl, which every vertex stage attaches.
vec4 linearizeVertexTint(vec4 tint);

void main()
{
    //transform vertex
#ifdef HAS_SKIN
    mat3x4 skin = getSkinBlend();
    vec3 pos = skinTransformH(skin, position.xyz, modelview_matrix).xyz;
    vary_position = pos;
    gl_Position = projection_matrix*vec4(pos, 1.0);

    vec3 n = normalize(mat3(modelview_matrix) * skinDirection(skin, normal.xyz));
    vec3 t = normalize(mat3(modelview_matrix) * skinDirection(skin, tangent.xyz));
#else
    vary_position = (modelview_matrix*vec4(position.xyz, 1.0)).xyz;
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
    vec3 n = normalize(normal_matrix * normal);
    vec3 t = normalize(normal_matrix * tangent.xyz);
#endif

    vec3 b = cross(n, t) * tangent.w;
    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;

    vary_mat0 = vec3(t.x, b.x, n.x);
    vary_mat1 = vec3(t.y, b.y, n.y);
    vary_mat2 = vec3(t.z, b.z, n.z);

    // Tint arrives sRGB. A pass that decodes its diffuse on the sampler and shades in linear
    // wants the tint linearised to match; one that keeps the encoded texel does not. Keyed on
    // the same define LLGLSLShader::mLinearDiffuse is derived from, so the shader and the bind
    // cannot disagree about which space the multiply happens in.
#ifdef LINEAR_DIFFUSE
    vertex_color = linearizeVertexTint(diffuse_color);
#else
    vertex_color = diffuse_color;
#endif
}
