/**
 * @file diffuseV.glsl
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

uniform mat3 normal_matrix;
uniform mat4 texture_matrix0;
uniform mat4 modelview_projection_matrix;

in vec3 position;
in vec4 diffuse_color;
in vec3 normal;
in vec2 texcoord0;

out vec3 vary_normal;

out vec4 vertex_color;
out vec2 vary_texcoord0;
out vec3 vary_position;

void passTextureIndex();

uniform mat4 modelview_matrix;

#ifdef HAS_SKIN
mat4 getObjectSkinnedTransform();
uniform mat4 projection_matrix;

#endif

// Linearise a prim tint for a pass that shades in linear. Defined here rather than shared
// from environment/srgbF, which attachShaderFeatures attaches to the VERTEX stage only for
// programs that calculate atmospherics -- not this one. starsV and meteorsV carry their own
// sRGB math for the same reason.
//
// The tint arrives sRGB-encoded (an 8-bit LLColor4U attribute), and converting it in the
// vertex stage is deliberate: CPU-side would mean storing linear in the 4xU8 attribute,
// where steps near black are ~0.0039 against sRGB's ~0.0003, so a dark tint like (10,10,10)
// would quantise about 30% off. Here the attribute keeps its sRGB precision, the conversion
// is per-vertex rather than per-fragment, and the interpolation ends up linear -- which is
// what it should always have been. Alpha is never sRGB and passes through untouched.
vec4 linearizeVertexTint(vec4 tint)
{
    return vec4(pow(max(tint.rgb, vec3(0.0)), vec3(2.2)), tint.a);
}

void main()
{
#ifdef HAS_SKIN
    mat4 mat = getObjectSkinnedTransform();
    mat = modelview_matrix * mat;
    vec4 pos = mat * vec4(position.xyz, 1.0);
    vary_position = pos.xyz;
    gl_Position = projection_matrix * pos;
    vary_normal = normalize((mat*vec4(normal.xyz+position.xyz,1.0)).xyz-pos.xyz);
#else
    vary_position = (modelview_matrix * vec4(position.xyz, 1.0)).xyz;
    gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);
    vary_normal = normalize(normal_matrix * normal);
#endif

    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;

    passTextureIndex();

    // The diffuse map arrives LINEAR (decoded on the sampler); match the tint.
    // Why the vertex stage: see linearizeVertexTint.
    vertex_color = linearizeVertexTint(diffuse_color);
}
