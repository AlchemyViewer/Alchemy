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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

vec4 getNorm(vec2 pos_screen);
float getDepth(vec2 pos_screen);
bool isFarDepth(float d);
float ndcZFromScreenDepth(float d);

// The texel a tap at texel `i` reads along an axis of `size` texels, as the light map's mirrored sampler reads it: the
// texels run forward, then backward, each `size` long. A texel before the first is mirrored about it first, so the
// remainder is never taken of a negative; one within a mirror of the map, which is every tap of a map wider than its
// reach, needs no remainder.
int blurLightMirror(int i, int size)
{
    if (i >= -size && i < 2 * size)
    {
        return i < 0 ? -1 - i : (i < size ? i : 2 * size - 1 - i);
    }
    int period = 2 * size;
    int m = (i < 0 ? -1 - i : i) % period;
    return m < size ? m : period - 1 - m;
}

// The view depth of the texel the scene's clamped sampler reads for a tap at `texel`, or -1 at the sky, which
// blurLightDrawn and blurLightWeight turn away. A texel lies at its view depth along its ray: inv_proj's unprojection
// is linear in the screen position over w, and w is the view depth's reciprocal, floored as getPositionWithNDC floors
// it, so a drawn texel's is positive. The ray is linear in the texel (BlurLightPixel's plane), so a tap's position
// costs it no unprojection.
float blurLightViewDepth(ivec2 texel)
{
    ivec2 size = ivec2(screen_res);
    vec2 tc = (vec2(clamp(texel, ivec2(0), size - 1)) + 0.5) / screen_res;
    float depth = getDepth(tc);
    if (isFarDepth(depth))
    {
        return -1.0;
    }
    vec2 sc = tc * 2.0 - 1.0;
    float w = inv_proj[0].w * sc.x + inv_proj[1].w * sc.y + inv_proj[2].w * ndcZFromScreenDepth(depth) + inv_proj[3].w;
    return 1.0 / max(w, 0.000001);
}

// Whether a texel of view depth `view_depth` (blurLightViewDepth) is the world's: the blur runs over geometry alone,
// and the sky keeps the light map's clear.
bool blurLightDrawn(float view_depth)
{
    return view_depth > 0.0;
}

// A pixel's taps along one axis, (1, 0) across or (0, 1) down: what blurLightTap and blurLightWeight take.
struct BlurLightPixel
{
    vec2  step;      // a kernel step in pixels, shortened by the normal and the distance
    vec2  origin;    // where the taps run out from, moved by the checkerboard
    vec3  plane;     // n . ray at texel (0, 0), and per texel across and down: n . p over a texel's view depth
    float offset;    // n . p at the pixel, how far its plane lies from the eye along its normal
    float tolerance; // how far off it, squared, a tap may lie
};

// The kernel's seven offsets and weights out from the centre: the four of `kern` and those halfway between each two.
vec3 blurLightKernel(int step)
{
    int i = step >> 1;
    return (step & 1) == 0 ? kern[i] : (kern[i] + kern[i + 1]) * 0.5;
}

// Pixel `px`'s taps along `delta`, at view depth `view_depth` (blurLightViewDepth).
BlurLightPixel blurLightPixel(ivec2 px, vec2 delta, float view_depth)
{
    BlurLightPixel p;
    vec2 tc = (vec2(px) + 0.5) / screen_res;
    vec3 norm = getNorm(tc).xyz;

    // The ray through a texel's centre is linear in the texel, so its plane's term is too.
    vec2 texel_size = 2.0 / screen_res;
    float across = dot(norm, inv_proj[0].xyz) * texel_size.x;
    float down = dot(norm, inv_proj[1].xyz) * texel_size.y;
    p.plane = vec3(dot(norm, inv_proj[3].xyz) + (across + down) * 0.5 - dot(norm, inv_proj[0].xyz + inv_proj[1].xyz),
                   across, down);
    vec3 pos = (inv_proj[0].xyz * (tc.x * 2.0 - 1.0) + inv_proj[1].xyz * (tc.y * 2.0 - 1.0) + inv_proj[3].xyz) * view_depth;
    p.offset = (p.plane.x + float(px.x) * p.plane.y + float(px.y) * p.plane.z) * view_depth;

    p.step = kern_scale * delta / (1.0 + norm.xy * norm.xy);
    p.step /= max(-pos.z * dist_factor, 1.0);

    // relax tolerance according to distance to avoid speckling artifacts, as angles and distances are a lot more
    // abrupt within a small screen area at larger distances
    p.tolerance = pos.z * pos.z * 0.00005;

    // perturb the sampling origin in screen space, a checkerboard of half a step either way, to hide edge-ghosting
    // artifacts where the smoothing radius is quite large
    float tc_mod = float((px.x + px.y + 1) & 1);
    p.origin = vec2(px) + 0.5 + (tc_mod - 0.5) * kern[1].z * p.step * 0.5;
    return p;
}

// The texel tap `i` of twelve reads: the taps run out from the pixel in order, those ahead and then those behind,
// which is the order the weights are summed in.
ivec2 blurLightTap(BlurLightPixel p, int i)
{
    int step = i < 6 ? i + 1 : i - 5;
    float side = i < 6 ? 1.0 : -1.0;
    return ivec2(floor(p.origin + side * blurLightKernel(step).z * p.step * 2.0));
}

// Tap `i`'s weight, at texel `texel` of view depth `view_depth`: 0 at the sky, and where the texel's centre lies off
// the pixel's plane.
//
// AO uses the y weight of the kernel. The shadow term, which used the x weight, is no longer blurred: it is a
// Gaussian-weighted PCF (deferred/shadowUtil.glsl) that is smooth at the source, so a second screen-space blur only
// widened its penumbra and let the bilateral tolerance bleed it across depth edges the PCF had already resolved. AO is
// still stochastic per pixel and wants the denoise, so it keeps it.
float blurLightWeight(BlurLightPixel p, int i, ivec2 texel, float view_depth)
{
    int step = i < 6 ? i + 1 : i - 5;
    float d = (p.plane.x + float(texel.x) * p.plane.y + float(texel.y) * p.plane.z) * view_depth - p.offset;
    return blurLightDrawn(view_depth) && d * d <= p.tolerance ? blurLightKernel(step).y : 0.0;
}

// The twelve taps about pixel `px` along `delta`: the light map's texel each reads, and its weight. Returns the weight
// of the pixel's own texel.
float blurLightTaps(ivec2 px, vec2 delta, out ivec2 tap[12], out float weight[12])
{
    ivec2 size = ivec2(screen_res);
    BlurLightPixel p = blurLightPixel(px, delta, blurLightViewDepth(px));
    for (int i = 0; i < 12; ++i)
    {
        ivec2 texel = blurLightTap(p, i);
        tap[i] = ivec2(blurLightMirror(texel.x, size.x), blurLightMirror(texel.y, size.y));
        weight[i] = blurLightWeight(p, i, texel, blurLightViewDepth(texel));
    }
    return kern[0].y;
}
