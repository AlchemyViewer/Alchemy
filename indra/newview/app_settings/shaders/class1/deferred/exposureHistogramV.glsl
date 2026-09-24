/**
 * @file exposureHistogramV.glsl
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

// One point per part of a meter grid texel: its non-sky pixels (position.z 0) and its sky
// pixels (position.z 1). Each point lands in the histogram bin of its log2 luminance, and the
// target's additive blend sums, per bin, the points' weights and their weighted log2
// luminances. The sums keep the mean exact; the bins only decide which points the percentile
// trim keeps.

in vec3 position; // xy: the meter texel, z: which part of it

uniform sampler2D meterMap;
uniform vec4 meter_params; // x: metering mode, y: sky weight, z: log2 luminance at the first bin's low edge, w: bins per EV

out vec2 vary_weight_log;

const float BINS = 64.0;

// How much a point at uv (0..1 across the frame) counts, by metering mode: 0 averages the whole
// frame, 1 weights toward the centre, 2 meters a spot in the middle.
float meterWeight(vec2 uv, float mode)
{
    vec2 q = uv * 2.0 - 1.0;
    float d2 = dot(q, q);
    if (mode < 0.5)
    {
        return 1.0;
    }
    if (mode < 1.5)
    {
        return exp(-2.0 * d2);
    }
    return 1.0 - smoothstep(0.15, 0.3, sqrt(d2));
}

void main()
{
    ivec2 texel = ivec2(position.xy);
    vec4 grid = texelFetch(meterMap, texel, 0);
    bool is_sky = position.z > 0.5;
    float L = is_sky ? grid.g : grid.r;
    float share = is_sky ? grid.b : 1.0 - grid.b;

    vec2 uv = (position.xy + 0.5) / vec2(textureSize(meterMap, 0));
    float w = share * meterWeight(uv, meter_params.x) * (is_sky ? meter_params.y : 1.0);

    // Held to the histogram's range, so the first and last bins cannot drag the mean with
    // values far outside it.
    float ev = clamp(log2(max(L, 1e-10)), meter_params.z, meter_params.z + BINS / meter_params.w);
    float bin = min(floor((ev - meter_params.z) * meter_params.w), BINS - 1.0);

    // A point that carries nothing lands outside the target. Points are drawn at the fixed
    // size, which every glPointSize caller puts back to 1, so each covers exactly one bin.
    gl_Position = (w > 0.0 && L > 0.0) ? vec4((bin + 0.5) / BINS * 2.0 - 1.0, 0.0, 0.0, 1.0)
                                       : vec4(2.0, 2.0, 0.0, 1.0);
    vary_weight_log = vec2(w, w * ev);
}
