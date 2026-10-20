/**
 * @file blurLightF.glsl
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

// One axis of the light map's ambient occlusion blur, the taps blurLightUtil.glsl's: across into a scratch, then down
// from it. Where compute is, one dispatch does both (blurLightC.glsl).

out vec4 frag_color;

uniform sampler2D lightMap;

uniform vec2 delta;

float blurLightTaps(ivec2 px, vec2 delta, out ivec2 tap[12], out float weight[12]);

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 ccol = texelFetch(lightMap, px, 0);

    ivec2 tap[12];
    float weight[12];
    float defined_weight = blurLightTaps(px, delta, tap, weight); // the centre's weight; we have to sample it anyway so we get it for 'free'
    float ao = defined_weight * ccol.g;

    for (int i = 0; i < 12; ++i)
    {
        if (weight[i] > 0.0)
        {
            ao += texelFetch(lightMap, tap[i], 0).g * weight[i];
            defined_weight += weight[i];
        }
    }

    ao /= defined_weight;

    // shadow (r) and spot shadows (b, a) pass through unblurred; only AO (g) is smoothed
    frag_color = vec4(ccol.r, max(ao, 0.0), ccol.b, ccol.a);
}
