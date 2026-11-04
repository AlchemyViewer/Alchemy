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
//
// The light a surface reflects is its albedo times the light that falls on it, so a meter of the
// reflected light cannot tell a white wall in dim light from a grey one in bright light, and
// exposes both as middle grey. With meter_albedo_params.x above 0 (incident metering, at 1) the
// non-sky mean is divided by the patch's mean albedo (from the G-buffer) to that power, which
// estimates the light that falls on the patch: a white wall stays white and a grey card stays
// grey. The share of the patch's exposure that its albedo has earned goes to alpha, because a
// near black surface says nothing about the light on it; at strength 0 the grid is what it
// always was, with alpha 1.
//
// What the estimate leaves alone: an emissive surface is metered as the light it is, and may go
// hot. The water never writes the G-buffer, so over the world the albedo under it is the
// seabed's, a fair stand-in in clear water (over nothing it is sky, below). Blended surfaces are
// not in the G-buffer either, so the pass that draws them marks its pixels in the stencil, and
// meterBlendMap carries that mask here: those pixels are taken to reflect NOMINAL_ALBEDO, the
// key, which meters them exactly as reflective metering does, whatever lies behind them.

out vec4 frag_color;

uniform sampler2D diffuseRect;   // the HDR scene, before bloom
uniform sampler2D depthMap;      // the scene's depth, the far plane wherever the sky is
uniform sampler2D meterWaterMap; // the G-buffer's normals under the water's own pixels, 0 elsewhere
uniform vec4 meter_params;       // xy: a patch's size in scene texels, zw: taps across it per axis
uniform sampler2D meterAlbedoMap; // the G-buffer's albedo, decoded to linear
uniform sampler2D meterBlendMap;  // r: 1 where a blended surface shades, else 0
// x: 0 meters the light a surface reflects, 1 the light that falls on it.
// y: a surface that reflects less than this is read as if it reflected this much, since dividing by next to nothing
//    would make a black surface the brightest light in the frame.
// zw: between these a patch's mean albedo earns its share of the exposure, none below z and all of it above w.
uniform vec4 meter_albedo_params;

// What a pixel the G-buffer cannot speak for is taken to reflect: the key (EXPOSURE_KEY in pipeline.cpp). The light
// over it is then its luminance over the key, which is what reflective metering reads, at any strength.
const float NOMINAL_ALBEDO = 0.18;

// The sky is what the world leaves at the far plane (deferredUtil.glsl).
bool isFarDepth(float d);

// And the water over it. The water writes the scene's depth but never the G-buffer, so where it lies over nothing the
// deferred pass drew, the G-buffer keeps its clear under it, whose 1 in the normals' alpha no writer's flag is (read
// with GET_GBUFFER_FLAG's tolerance). Nothing is water where there is no map to say so.
bool waterOverSky(ivec2 px, ivec2 water_size)
{
    return all(lessThan(px, water_size)) && texelFetch(meterWaterMap, px, 0).a > 0.9;
}

float lum(vec3 col)
{
    return dot(vec3(0.2126, 0.7152, 0.0722), col);
}

/// One patch of the meter's grid: taps across the patch of the scene, told apart by depth into sky and ground, written as
/// the ground's mean luminance (divided by its mean albedo under incident metering), the sky's mean luminance, the sky's
/// share, and the share of the exposure the albedo has earned.
void main()
{
    vec2 scene_size = vec2(textureSize(diffuseRect, 0));
    ivec2 water_size = textureSize(meterWaterMap, 0);
    vec2 patch_origin = floor(gl_FragCoord.xy) * meter_params.xy;
    vec2 tap_step = meter_params.xy / meter_params.zw;
    ivec2 taps = ivec2(meter_params.zw);

    bool by_albedo = meter_albedo_params.x > 0.0;
    float ground = 0.0;
    float ground_albedo = 0.0;
    float sky = 0.0;
    float sky_taps = 0.0;
    for (int j = 0; j < taps.y; ++j)
    {
        for (int i = 0; i < taps.x; ++i)
        {
            vec2 px = patch_origin + (vec2(i, j) + 0.5) * tap_step;
            // R16F tops out at 65504; the sun disc can be brighter. A NaN reads as the black the
            // frame shows it as, not as whatever min makes of it.
            float L = lum(textureLod(diffuseRect, px / scene_size, 0.0).rgb);
            L = isnan(L) ? 0.0 : min(L, 60000.0);
            if (isFarDepth(texelFetch(depthMap, ivec2(px), 0).r) || waterOverSky(ivec2(px), water_size))
            {
                sky += L;
                sky_taps += 1.0;
            }
            else
            {
                ground += L;
                if (by_albedo)
                {
                    bool blended = texelFetch(meterBlendMap, ivec2(px), 0).r > 0.5;
                    ground_albedo += blended ? NOMINAL_ALBEDO : lum(texelFetch(meterAlbedoMap, ivec2(px), 0).rgb);
                }
            }
        }
    }

    float count = float(taps.x * taps.y);
    float ground_taps = count - sky_taps;
    float ground_mean = ground_taps > 0.0 ? ground / ground_taps : 0.0;
    float earned = 1.0;
    if (by_albedo && ground_taps > 0.0)
    {
        float albedo = ground_albedo / ground_taps;
        ground_mean /= pow(max(albedo, meter_albedo_params.y), meter_albedo_params.x);
        earned = mix(1.0, smoothstep(meter_albedo_params.z, meter_albedo_params.w, albedo), meter_albedo_params.x);
    }
    frag_color = vec4(ground_mean,
                      sky_taps > 0.0 ? sky / sky_taps : 0.0,
                      sky_taps / count,
                      earned);
}
