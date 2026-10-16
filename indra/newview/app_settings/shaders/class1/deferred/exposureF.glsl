/**
 * @file exposureF.glsl
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

// The exposure, from the meter's histogram (exposureHistogramV.glsl), into a 1x1 target:
//     x  the scale the tonemapper multiplies the scene by, 2^-EV
//     y  the EV, which the next frame adapts from
//     z  the EV the meter asked for this frame, before adaptation
//
// The EV is the mean log2 luminance of the points between two percentiles of the histogram's
// weight, less log2 of the key (middle grey) and the compensation, clamped to a range: the
// trim keeps the sun, glints and deep shadow from moving it. It adapts in log space, faster
// toward a brighter scene than toward a darker one, at rates in 1/s, so the frame rate does
// not change the speed.
//
// scripts/content_tools/check_exposure.py mirrors this statement for statement.

out vec4 frag_color;

uniform sampler2D exposureHistogram;
#ifdef USE_LAST_EXPOSURE
uniform sampler2D exposureMap; // last frame's
#endif

uniform float dt;

uniform vec4 dynamic_exposure_params;  // x: log2 of the key plus the compensation, y: min EV, z: max EV, w: 1 meters, 0 holds EV 0
uniform vec4 dynamic_exposure_params2; // x, y: the low and high fractions of the weight kept, z, w: rates toward brighter and darker scenes

const int BINS = 64;

void main()
{
    float total = 0.0;
    for (int i = 0; i < BINS; ++i)
    {
        total += texelFetch(exposureHistogram, ivec2(i, 0), 0).r;
    }

    float low = total * dynamic_exposure_params2.x;
    float high = total * dynamic_exposure_params2.y;
    float below = 0.0;
    float kept = 0.0;
    float kept_log = 0.0;
    for (int i = 0; i < BINS; ++i)
    {
        vec2 bin = texelFetch(exposureHistogram, ivec2(i, 0), 0).rg;
        float from = max(low, below);
        float to = min(high, below + bin.x);
        if (to > from)
        {
            float share = (to - from) / bin.x;
            kept += bin.x * share;
            kept_log += bin.y * share;
        }
        below += bin.x;
    }

    // Nothing lit at all: open up as far as the range allows.
    float target = kept > 0.0 ? kept_log / kept - dynamic_exposure_params.x : dynamic_exposure_params.y;
    target = clamp(target, dynamic_exposure_params.y, dynamic_exposure_params.z);

    float ev = target;
#ifdef USE_LAST_EXPOSURE
    float prev = texture(exposureMap, vec2(0.5, 0.5)).g;
    float rate = target > prev ? dynamic_exposure_params2.z : dynamic_exposure_params2.w;
    ev = mix(prev, target, 1.0 - exp(-rate * dt));
#endif

    if (dynamic_exposure_params.w < 0.5)
    {
        ev = 0.0;
        target = 0.0;
    }

    frag_color = vec4(exp2(-ev), ev, target, 1.0);
}
