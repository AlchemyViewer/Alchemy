/**
 * @file alphaV.glsl
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

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

in vec3 position;

#ifdef USE_INDEXED_TEX
void passTextureIndex();
#endif

in vec3 normal;

#ifdef USE_VERTEX_COLOR
in vec4 diffuse_color;
#endif

in vec2 texcoord0;

#ifdef HAS_SKIN
mat3x4 getSkinBlend();
vec3 skinDirection(mat3x4 b, vec3 dir);
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m);
#else
#ifdef IS_AVATAR_SKIN
mat4 getSkinnedTransform();
#endif
#endif

out vec3 vary_fragcoord;
out vec3 vary_position;

#ifdef USE_VERTEX_COLOR
out vec4 vertex_color;
#endif

out vec2 vary_texcoord0;
out vec3 vary_norm;

uniform float near_clip;

// Linearises an sRGB prim tint for a pass that shades in linear. Defined in
// deferred/textureUtilV.glsl, which every vertex stage attaches.
vec4 linearizeVertexTint(vec4 tint);

void main()
{
    vec4 pos;
    vec3 norm;

    //transform vertex
#ifdef HAS_SKIN
    mat3x4 skin = getSkinBlend();

    pos = skinTransformH(skin, position.xyz, modelview_matrix);

    norm = normalize(mat3(modelview_matrix) * skinDirection(skin, normal.xyz));
    vec4 frag_pos = projection_matrix * pos;
    gl_Position = frag_pos;
#else

#ifdef IS_AVATAR_SKIN
    mat4 trans = getSkinnedTransform();
    vec4 pos_in = vec4(position.xyz, 1.0);
    pos.x = dot(trans[0], pos_in);
    pos.y = dot(trans[1], pos_in);
    pos.z = dot(trans[2], pos_in);
    pos.w = 1.0;

    norm.x = dot(trans[0].xyz, normal);
    norm.y = dot(trans[1].xyz, normal);
    norm.z = dot(trans[2].xyz, normal);
    norm = normalize(norm);

    vec4 frag_pos = projection_matrix * pos;
    gl_Position = frag_pos;
#else
    norm = normalize(normal_matrix * normal);
    vec4 vert = vec4(position.xyz, 1.0);
    pos = (modelview_matrix * vert);
    gl_Position = modelview_projection_matrix*vec4(position.xyz, 1.0);
#endif //IS_AVATAR_SKIN

#endif // HAS_SKIN

#ifdef USE_INDEXED_TEX
    passTextureIndex();
#endif

    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;

    vary_norm = norm;
    vary_position = pos.xyz;

#ifdef USE_VERTEX_COLOR
    // Tint arrives sRGB. This pass decodes its diffuse on the sampler and shades in linear,
    // so the tint has to be linearized to match -- FOR_IMPOSTOR included, since the impostor
    // bake joined that convention. IS_HUD is the remaining exception: it samples raw and
    // decodes the tinted product in the fragment stage, staying pixel-exact. Guarded on
    // exactly the condition that leaves LLGLSLShader::mLinearDiffuse false, so the shader and
    // the bind cannot disagree about which space the multiply happens in.
#ifdef LINEAR_DIFFUSE
    vertex_color = linearizeVertexTint(diffuse_color);
#else
    vertex_color = diffuse_color;
#endif
#endif

// The FS derives a screen UV as vary_fragcoord.xy / vary_fragcoord.z. Forward-Z abuses the
// view-space z plus near_clip as a stand-in for clip w; under reverse-Z that pseudo-w is
// meaningless (and clip.z tends to 0 at the far plane), so carry the true clip w instead.
#ifdef HAS_SKIN
#ifdef REVERSE_Z
    vary_fragcoord.xyz = vec3(frag_pos.xy, frag_pos.w);
#else
    vary_fragcoord.xyz = frag_pos.xyz + vec3(0,0,near_clip);
#endif
#else

#ifdef IS_AVATAR_SKIN
#ifdef REVERSE_Z
    vary_fragcoord.xyz = vec3(frag_pos.xy, frag_pos.w);
#else
    vary_fragcoord.xyz = pos.xyz + vec3(0,0,near_clip);
#endif
#else
    pos = modelview_projection_matrix * vert;
#ifdef REVERSE_Z
    vary_fragcoord.xyz = vec3(pos.xy, pos.w);
#else
    vary_fragcoord.xyz = pos.xyz + vec3(0,0,near_clip);
#endif
#endif

#endif

}

