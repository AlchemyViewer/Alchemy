/**
 * @file blurLightUtil.glsl
 * @brief The taps of the light map's ambient occlusion blur, which the fragment passes (blurLightF.glsl) and the
 *        compute pass (blurLightC.glsl) both take from here, so the two blur alike.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

uniform float dist_factor;
uniform vec3 kern[4];
uniform float kern_scale;
uniform vec2 screen_res;

vec4 getNorm(vec2 pos_screen);
float getDepth(vec2 pos_screen);
vec4 getPositionWithDepth(vec2 pos_screen, float depth);
bool isFarDepth(float d);

// The texel a tap at texel `i` reads along an axis of `size` texels, as the light map's mirrored sampler reads it: the
// texels run forward, then backward, each `size` long. A texel before the first is mirrored about it first, so the
// remainder is never taken of a negative.
int blurLightMirror(int i, int size)
{
    int period = 2 * size;
    int m = (i < 0 ? -1 - i : i) % period;
    return m < size ? m : period - 1 - m;
}

// Whether the world drew pixel `px`: the blur runs over geometry alone, and the sky keeps the light map's clear.
bool blurLightDrawn(ivec2 px)
{
    return !isFarDepth(getDepth((vec2(px) + 0.5) / screen_res));
}

// The twelve taps about pixel `px` along `delta`, (1, 0) across or (0, 1) down: the light map's texel each reads, and
// its weight, 0 where the tap lies off the pixel's plane. Returns the weight of the pixel's own texel. The taps run
// out from it in order, those ahead and then those behind, which is the order the weights are summed in.
//
// AO uses the y weight of the kernel. The shadow term, which used the x weight, is no longer blurred: it is a
// Gaussian-weighted PCF (deferred/shadowUtil.glsl) that is smooth at the source, so a second screen-space blur only
// widened its penumbra and let the bilateral tolerance bleed it across depth edges the PCF had already resolved. AO is
// still stochastic per pixel and wants the denoise, so it keeps it.
float blurLightTaps(ivec2 px, vec2 delta, out ivec2 tap[12], out float weight[12])
{
    ivec2 size = ivec2(screen_res);
    vec2 tc = (vec2(px) + 0.5) / screen_res;
    vec4 norm = getNorm(tc);
    vec3 pos = getPositionWithDepth(tc, getDepth(tc)).xyz;

    vec2 dlt = kern_scale * delta / (1.0 + norm.xy * norm.xy);
    dlt /= max(-pos.z * dist_factor, 1.0);

    // relax tolerance according to distance to avoid speckling artifacts, as angles and distances are a lot more
    // abrupt within a small screen area at larger distances
    float pointplanedist_tolerance_pow2 = pos.z * pos.z * 0.00005;

    // perturb the sampling origin in screen space, a checkerboard of half a step either way, to hide edge-ghosting
    // artifacts where the smoothing radius is quite large
    float tc_mod = float((px.x + px.y + 1) & 1);
    vec2 origin = vec2(px) + 0.5 + (tc_mod - 0.5) * kern[1].z * dlt * 0.5;

    vec3 k[7];
    k[0] = kern[0];
    k[2] = kern[1];
    k[4] = kern[2];
    k[6] = kern[3];

    k[1] = (k[0] + k[2]) * 0.5f;
    k[3] = (k[2] + k[4]) * 0.5f;
    k[5] = (k[4] + k[6]) * 0.5f;

    for (int i = 0; i < 12; ++i)
    {
        int step = i < 6 ? i + 1 : i - 5;
        float side = i < 6 ? 1.0 : -1.0;

        // The tap's depth as the scene's clamped sampler reads it, and its position where the tap lies.
        vec2 at = origin + side * k[step].z * dlt * 2.0;
        ivec2 texel = ivec2(floor(at));
        ivec2 depth_texel = clamp(texel, ivec2(0), size - 1);
        vec3 samppos = getPositionWithDepth(at / screen_res, getDepth((vec2(depth_texel) + 0.5) / screen_res)).xyz;

        float d = dot(norm.xyz, samppos.xyz - pos.xyz); // dist from plane

        tap[i] = ivec2(blurLightMirror(texel.x, size.x), blurLightMirror(texel.y, size.y));
        weight[i] = d * d <= pointplanedist_tolerance_pow2 ? k[step].y : 0.0;
    }

    return kern[0].y;
}
