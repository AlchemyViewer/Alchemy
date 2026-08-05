/**
 * @file materialIndexedV.glsl
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

// Indexed (multi-material) variant of materialV.glsl (GBuffer write only). Forwards
// the per-vertex material slot to the fragment shader. Per-map texture transforms
// are baked into the texcoords at buffer build (indexed faces exclude texture
// animation), so texture_matrix0 is not applied here. HAS_SKIN adds rigged skinning.

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#endif

out vec3 vary_position;

in vec3 position;
in vec4 diffuse_color;
in vec3 normal;
in vec2 texcoord0;
in int texture_index;

flat out int vary_material_index;

#ifdef HAS_NORMAL_MAP
in vec4 tangent;
in vec2 texcoord1;
out vec3 vary_tangent;
flat out float vary_sign;
out vec3 vary_normal;
out vec2 vary_texcoord1;
#else
out vec3 vary_normal;
#endif

#ifdef HAS_SPECULAR_MAP
in vec2 texcoord2;
out vec2 vary_texcoord2;
#endif

out vec4 vertex_color;
out vec2 vary_texcoord0;

// Linearises an sRGB prim tint for a pass that shades in linear. Defined in
// deferred/textureUtilV.glsl, which every vertex stage attaches.
vec4 linearizeVertexTint(vec4 tint);

void main()
{
#ifdef HAS_SKIN
    mat3x4 skin = getSkinBlend();
    vec3 pos = skinTransformH(skin, position.xyz, modelview_matrix).xyz;
    vary_position = pos;
    gl_Position = projection_matrix * vec4(pos, 1.0);
#else
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
#endif

    vary_material_index = texture_index;

    vary_texcoord0 = texcoord0;
#ifdef HAS_NORMAL_MAP
    vary_texcoord1 = texcoord1;
#endif
#ifdef HAS_SPECULAR_MAP
    vary_texcoord2 = texcoord2;
#endif

#ifdef HAS_SKIN
    vec3 n = normalize(mat3(modelview_matrix) * skinDirection(skin, normal.xyz));
#ifdef HAS_NORMAL_MAP
    vec3 t = normalize(mat3(modelview_matrix) * skinDirection(skin, tangent.xyz));
    vary_tangent = t;
    vary_sign = tangent.w;
    vary_normal = n;
#else
    vary_normal = n;
#endif
#else
    vec3 n = normalize(normal_matrix * normal);
#ifdef HAS_NORMAL_MAP
    vec3 t = normalize(normal_matrix * tangent.xyz);
    vary_tangent = t;
    vary_sign = tangent.w;
    vary_normal = n;
#else
    vary_normal = n;
#endif
#endif

    // Tint arrives sRGB. A pass that decodes its diffuse on the sampler and shades in linear
    // wants the tint linearised to match; one that keeps the encoded texel does not. Keyed on
    // the same define LLGLSLShader::mLinearDiffuse is derived from, so the shader and the bind
    // cannot disagree about which space the multiply happens in.
#ifdef LINEAR_DIFFUSE
    vertex_color = linearizeVertexTint(diffuse_color);
#else
    vertex_color = diffuse_color;
#endif

#if !defined(HAS_SKIN)
    vary_position = (modelview_matrix * vec4(position.xyz, 1.0)).xyz;
#endif
}
