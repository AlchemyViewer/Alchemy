/**
 * @file bumpF.glsl
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_data[4];

uniform float minimum_alpha;
uniform sampler2D diffuseMap;
uniform sampler2D bumpMap;

in vec3 vary_mat0;
in vec3 vary_mat1;
in vec3 vary_mat2;

in vec4 vertex_color;
in vec2 vary_texcoord0;
in vec3 vary_position;

void mirrorClip(vec3 pos);
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);
float filterSpecularRoughness(float perceptualRoughness, vec3 n);

void main()
{
    mirrorClip(vary_position);

    vec4 col = texture(diffuseMap, vary_texcoord0.xy);

    if(col.a < minimum_alpha)
    {
        discard;
    }
    col *= vertex_color;

    vec3 norm = texture(bumpMap, vary_texcoord0.xy).rgb * 2.0 - 1.0;

    vec3 tnorm = vec3(dot(norm,vary_mat0),
            dot(norm,vary_mat1),
            dot(norm,vary_mat2));

    vec3 nvn = normalize(tnorm);

    // Widen the lobe by whatever bump detail this pixel lost to minification, as the PBR
    // writers do. The bump map at distance packs many normals into one texel; averaging them
    // leaves the shading with a single direction and the authored lobe width, so a narrow
    // Blinn-Phong highlight snaps between pixels as the camera moves. Worked in the
    // (1 - glossiness) domain, which is what the legacy path treats as perceptual roughness.
    //
    // Only the exponent moves. vertex_color.a is also the specular tint here and the
    // environment intensity below, and neither of those is a lobe width.
    float glossiness = 1.0 - filterSpecularRoughness(1.0 - vertex_color.a, nvn);

    frag_data[0] = vec4(col.rgb, 0.0);
    frag_data[1] = vec4(vertex_color.aaa, glossiness); // spec
    //frag_data[1] = vec4(vec3(vertex_color.a), vertex_color.a+(1.0-vertex_color.a)*vertex_color.a); // spec - from former class3 - maybe better, but not so well tested
    frag_data[2] = encodeNormal(nvn, vertex_color.a, GBUFFER_FLAG_HAS_ATMOS);

#if defined(HAS_EMISSIVE)
    frag_data[3] = vec4(0, 0, 0, 0);
#endif
}
