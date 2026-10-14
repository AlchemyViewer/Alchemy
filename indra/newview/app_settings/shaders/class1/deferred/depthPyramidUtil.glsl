/**
 * @file depthPyramidUtil.glsl
 * @brief Whether a box can be in front of the scene's depth, from the depth pyramid (ALDepthPyramid).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

// Each texel the farthest depth under it (ALDepthPyramid): level k's texel t covers depth pixels
// [t * 2^(k+1), (t + 1) * 2^(k+1)) each way, and level 0 holds at least the depth halved, so depth pixel p lies under
// texel p >> (k + 1) of level k.
uniform sampler2D depthPyramid;
// xy: the depth's size in pixels, which the viewport covered; z: the pyramid's levels.
uniform ivec4 depthPyramidSize;

// The most texels of the coarsest level read each way: past it, the box is kept.
#define DEPTH_PYRAMID_MOST_READ 4

// The farther of two depths: the smaller under reverse-Z, the larger under forward-Z.
float depth_pyramid_farthest(float a, float b)
{
#ifdef REVERSE_Z
    return min(a, b);
#else
    return max(a, b);
#endif
}

// True unless every point of the box [lo, hi], taken to clip space by `to_clip`, lies behind the farthest depth under
// the pixels it covers, so the depth test would reject all of it. A box reaching the eye's plane is in front.
bool depth_pyramid_box_visible(vec3 lo, vec3 hi, mat4 to_clip)
{
    vec2 ndc_lo = vec2(1.0);
    vec2 ndc_hi = vec2(-1.0);
#ifdef REVERSE_Z
    float nearest = 0.0;
#else
    float nearest = 1.0;
#endif
    for (int i = 0; i < 8; ++i)
    {
        vec3 corner = vec3((i & 1) != 0 ? hi.x : lo.x, (i & 2) != 0 ? hi.y : lo.y, (i & 4) != 0 ? hi.z : lo.z);
        vec4 clip = to_clip * vec4(corner, 1.0);
        if (clip.w <= 0.0)
        {
            return true;
        }
        vec3 ndc = clip.xyz / clip.w;
        ndc_lo = min(ndc_lo, ndc.xy);
        ndc_hi = max(ndc_hi, ndc.xy);
        // Window depth: clip control's zero-to-one under reverse-Z, GL's minus-one-to-one otherwise.
#ifdef REVERSE_Z
        nearest = max(nearest, ndc.z);
#else
        nearest = min(nearest, ndc.z * 0.5 + 0.5);
#endif
    }

    // Every pixel whose centre the box's image covers lies in the pixels its rectangle's corners fall in. Off the
    // screen nothing is drawn, so the rectangle is held to it.
    ivec2 size = depthPyramidSize.xy;
    ivec2 p_lo = clamp(ivec2(floor((ndc_lo * 0.5 + 0.5) * vec2(size))), ivec2(0), size - 1);
    ivec2 p_hi = clamp(ivec2(floor((ndc_hi * 0.5 + 0.5) * vec2(size))), ivec2(0), size - 1);

    // The finest level at which the rectangle lies under at most two texels each way, or the coarsest, every texel of
    // which under the rectangle is then read, up to DEPTH_PYRAMID_MOST_READ each way: a box wider than that at the
    // coarsest level covers much of the screen, and is kept rather than read texel by texel.
    int level = 0;
    while (level < depthPyramidSize.z - 1 && any(greaterThan((p_hi >> (level + 1)) - (p_lo >> (level + 1)), ivec2(1))))
    {
        ++level;
    }
    ivec2 t_lo = p_lo >> (level + 1);
    ivec2 t_hi = p_hi >> (level + 1);
    if (any(greaterThanEqual(t_hi - t_lo, ivec2(DEPTH_PYRAMID_MOST_READ))))
    {
        return true;
    }

    float farthest = texelFetch(depthPyramid, t_lo, level).r;
    for (int y = t_lo.y; y <= t_hi.y; ++y)
    {
        for (int x = (y == t_lo.y ? t_lo.x + 1 : t_lo.x); x <= t_hi.x; ++x)
        {
            farthest = depth_pyramid_farthest(farthest, texelFetch(depthPyramid, ivec2(x, y), level).r);
        }
    }

    // Hidden only where the box's nearest point is strictly behind the farthest of what covers it; a NaN on either
    // side compares false both ways, and the box is kept.
#ifdef REVERSE_Z
    return !(nearest < farthest);
#else
    return !(nearest > farthest);
#endif
}
