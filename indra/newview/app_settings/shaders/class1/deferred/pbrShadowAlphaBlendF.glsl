/**
 * @file pbrShadowAlphaBlendF.glsl
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

out vec4 frag_color;

uniform sampler2D diffuseMap;

in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;
uniform float minimum_alpha;

void main()
{
    float alpha = texture(diffuseMap,vary_texcoord0.xy).a;

    // The cutoff renderAlphaObjects sets for blended casters (ALPHA_BLEND_CUTOFF), on the
    // texture's alpha, as shadowAlphaMaskF takes it for a legacy blended prim.
    if (alpha < minimum_alpha)
    {
        discard;
    }

    alpha *= vertex_color.a;

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

    // Semi-transparent: a half-density shadow, in shadowAlphaMaskF's checkerboard.
    if (alpha < 0.88)
    {
        if (((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) == 0)
        {
            discard;
        }
    }

    frag_color = vec4(1,1,1,1);
}
