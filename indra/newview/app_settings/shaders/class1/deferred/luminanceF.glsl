/**
 * @file luminanceF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

// The exposure meter's grid. Each texel covers a patch of the HDR scene and holds the patch's
// mean luminance over its non-sky pixels (r) and over its sky pixels (g), and the share of the
// patch that is sky (b), so the histogram can weight the two apart.
//
// Every pixel of the patch counts: bilinear taps about two texels apart each average a 2x2
// block. A meter that point-samples the scene sees a sun disc or a glint appear and vanish as
// the camera moves, and the exposure pumps with it.

out vec4 frag_color;

uniform sampler2D diffuseRect;  // the HDR scene, before bloom
uniform sampler2D normalMap;    // the G-buffer normals, whose flags mark the sky
uniform vec4 meter_params;      // xy: a patch's size in scene texels, zw: taps across it per axis

float lum(vec3 col)
{
    return dot(vec3(0.2126, 0.7152, 0.0722), col);
}

void main()
{
    vec2 scene_size = vec2(textureSize(diffuseRect, 0));
    vec2 patch_origin = floor(gl_FragCoord.xy) * meter_params.xy;
    vec2 tap_step = meter_params.xy / meter_params.zw;
    ivec2 taps = ivec2(meter_params.zw);

    float ground = 0.0;
    float sky = 0.0;
    float sky_taps = 0.0;
    for (int j = 0; j < taps.y; ++j)
    {
        for (int i = 0; i < taps.x; ++i)
        {
            vec2 px = patch_origin + (vec2(i, j) + 0.5) * tap_step;
            // R16F tops out at 65504; the sun disc can be brighter
            float L = min(lum(textureLod(diffuseRect, px / scene_size, 0.0).rgb), 60000.0);
            vec4 norm = texelFetch(normalMap, ivec2(px), 0);
            if (GET_GBUFFER_FLAG(norm.w, GBUFFER_FLAG_SKIP_ATMOS) || GET_GBUFFER_FLAG(norm.w, GBUFFER_FLAG_HAS_HDRI))
            {
                sky += L;
                sky_taps += 1.0;
            }
            else
            {
                ground += L;
            }
        }
    }

    float count = float(taps.x * taps.y);
    float ground_taps = count - sky_taps;
    frag_color = vec4(ground_taps > 0.0 ? ground / ground_taps : 0.0,
                      sky_taps > 0.0 ? sky / sky_taps : 0.0,
                      sky_taps / count,
                      1.0);
}
