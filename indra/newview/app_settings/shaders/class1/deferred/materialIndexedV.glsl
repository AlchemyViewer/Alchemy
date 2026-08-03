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

uniform mat4 modelview_matrix;
uniform mat4 projection_matrix;
uniform mat4 modelview_projection_matrix;

#ifdef HAS_SKIN
mat4 getObjectSkinnedTransform();
#else
uniform mat3 normal_matrix;
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
    vec3 pos = (mat * vec4(position.xyz, 1.0)).xyz;
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
    vec3 n = normalize((mat * vec4(normal.xyz + position.xyz, 1.0)).xyz - pos.xyz);
#ifdef HAS_NORMAL_MAP
    vec3 t = normalize((mat * vec4(tangent.xyz + position.xyz, 1.0)).xyz - pos.xyz);
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

    // The diffuse map arrives LINEAR (decoded on the sampler); match the tint.
    // Why the vertex stage: see linearizeVertexTint.
    vertex_color = linearizeVertexTint(diffuse_color);

#if !defined(HAS_SKIN)
    vary_position = (modelview_matrix * vec4(position.xyz, 1.0)).xyz;
#endif
}
