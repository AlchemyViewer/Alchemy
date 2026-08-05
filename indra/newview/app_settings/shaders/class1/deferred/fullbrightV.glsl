/**
 * @file fullbrightV.glsl
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
void passTextureIndex();
in vec4 diffuse_color;
in vec2 texcoord0;

void calcAtmospherics(vec3 inPositionEye);

vec3 atmosAmbient();
vec3 atmosAffectDirectionalLight(float lightIntensity);

out vec3 vary_position;

out vec4 vertex_color;
out vec2 vary_texcoord0;

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
    vec4 vert = vec4(position.xyz, 1.0);
    passTextureIndex();

#ifdef HAS_SKIN
    vec4 pos = skinTransformH(getSkinBlend(), vert.xyz, modelview_matrix);
    gl_Position = projection_matrix * pos;
#else
    vec4 pos = (modelview_matrix * vert);
    gl_Position = modelview_projection_matrix*vec4(position.xyz, 1.0);
#endif

    vary_position = pos.xyz;

    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;

    calcAtmospherics(pos.xyz);

    // Tint arrives sRGB. This pass decodes its diffuse on the sampler and shades in linear,
    // so linearise the tint to match. Guarded on exactly the conditions that leave
    // LLGLSLShader::mLinearDiffuse false -- HUD outputs sRGB and keeps the encoded texel, and
    // FOR_IMPOSTOR writes the sRGB sample straight to the bake -- so the shader and the bind
    // cannot disagree about which space the multiply happens in.
#ifdef LINEAR_DIFFUSE
    vertex_color = linearizeVertexTint(diffuse_color);
#else
    vertex_color = diffuse_color;
#endif
}
