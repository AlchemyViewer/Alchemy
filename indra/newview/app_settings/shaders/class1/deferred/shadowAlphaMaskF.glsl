/**
 * @file shadowAlphaMaskF.glsl
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

out vec4 frag_color;

in vec4 post_pos;
in float target_pos_x;
in vec4 vertex_color;
in vec2 vary_texcoord0;
uniform float minimum_alpha;
// 1 for blended content, whose semi-transparent part casts a stippled half shadow; 0 for
// masked content, opaque wherever it is drawn at all. The same program serves both passes.
uniform float shadow_stipple;

void main()
{
    float alpha = diffuseLookup(vary_texcoord0.xy).a;

    // Masked content's cutoff is a material's 8-bit one, which a texel of that value is compared
    // with half an 8-bit step under it, as the masked passes that draw it compare it. Blended
    // content's is the shadow's own (ALPHA_BLEND_CUTOFF).
    float bias = (shadow_stipple > 0.0) ? 0.0 : 0.001953125; // 1/512, half an 8-bit quantization
    if (alpha < minimum_alpha - bias)
    {
        discard;
    }

#if !defined(IS_FULLBRIGHT)
    alpha *= vertex_color.a;
#endif

    if (alpha < 0.05) // treat as totally transparent
    {
        discard;
    }

    // Semi-transparent: a half-density shadow, every other texel of the shadow map in a
    // checkerboard. The shadow filter all but averages a checkerboard flat, where a pattern
    // of columns leaves lines along them.
    if (shadow_stipple > 0.0 && alpha < 0.88)
    {
        if (((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) == 0)
        {
            discard;
        }
    }

    frag_color = vec4(1,1,1,1);
}
