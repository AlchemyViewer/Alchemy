/**
 * @file crossFilterF.glsl
 * @brief Cross-screen (star) filter — streaks every thresholded highlight.
 *
 * Distinct from the lens flare's starburst, which is locked to the sun and
 * drawn procedurally around it. This is the glass filter: a fine grid etched
 * into a clear element, which diffracts *any* bright point in frame into a
 * star. So it runs on the bloom pyramid's thresholded highlights, where the
 * "which pixels are bright" question has already been answered.
 *
 * One invocation is one iteration of a Kawase-style streak. Each pass marches
 * a few taps out along every arm with the step scaled by uCrossPassScale, and
 * the caller runs it three times with the scale multiplied by four each time.
 * That reaches a streak length no single pass could afford: three passes of
 * four taps cover the same span as 64 taps of a naive loop, because each pass
 * is sampling a texture that already has the previous pass's reach baked in.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project
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

/*[EXTRA_CODE_HERE]*/

// Alpha is written explicitly rather than left to a vec3 output, and zero is
// the deliberate value: the streaks carry no alpha payload, and the borrowed
// target they are drawn into is rewritten before anything reads its alpha.
out vec4 frag_color;

uniform sampler2D diffuseMap;

uniform vec2  uCrossTexel;        // 1 / source size
uniform vec2  uCrossDir;          // unit direction for this pass; one arm at a time
uniform float uCrossLength;       // base step in texels; around 1 keeps the chain continuous
uniform float uCrossFalloff;      // per-step attenuation; > 1 decays faster
uniform float uCrossChromatic;    // 0 = white streaks, 1 = full dispersion
uniform float uCrossPassScale;    // 1, TAPS, TAPS^2 across the three iterations

in vec2 vary_texcoord0;

// Four taps per pass with the stride quadrupling to match is not a tuning
// choice -- it is the whole trick.
//
// Composing the three passes puts a sample at every offset i + 4j + 16k for
// i, j, k in 0..3. That is base-4 positional notation, so the chain reaches
// every integer offset from 0 to 63 exactly once, weighted falloff^-offset:
// an exact exponential line filter from twelve taps instead of sixty-four.
//
// Two things break that:
//
//   - TAPS not matching the stride. Six taps against a stride of four covers
//     the same span with multiplicity running 1,1,1,1,2,2,1,1,2,2,... -- a
//     modulation repeating at 4 and again at 16, which reads as self-similar
//     spikes along every arm.
//
//   - Sampling more than one direction per pass. The tiling argument assumes
//     offsets accumulate one-sided. Let a pass also step backwards and the net
//     offset becomes +/-i +/-4j +/-16k with independent signs: 127 offsets
//     instead of 64, reached by paths whose weight is set by how far the path
//     travelled rather than where it ended. Measured against a clean
//     exponential that is a 73% error with ten places where the arm gets
//     *brighter* further out.
//
// So this shader streaks exactly one direction, one-sided, and the caller runs
// a separate three-pass chain per arm. uCrossLength must also stay near one
// texel: it multiplies every offset, so at 2 the chain lands on even texels
// only and real gaps open between them.
//
// CROSS_TAPS is injected at compile time from CROSS_FILTER_TAPS
// (llviewershadermgr.h) -- the same constant pipeline.cpp derives the pass
// strides and the falloff remap from, so the three moving parts of the
// tiling can no longer disagree.
const int   TAPS        = CROSS_TAPS;

// Dispersion is a stride per channel. A real star filter is a diffraction
// grating, which deviates long wavelengths most, so at full uCrossChromatic
// red's taps reach 30% further along the arm and blue's 30% less.
//
// It has to be a stride, not a tint. A stride composes across the passes as
// the offsets do -- channel c's chain reaches every s_c * (i + 4j + 16k) --
// so each channel is the same exponential line the tiling makes, only longer
// or shorter, and keeps its energy, since the weights do not depend on it. A
// tint chosen per tap does not compose: the chain would multiply three
// passes' tints, tint(i) * tint(4j) * tint(16k), which is not tint(i + 4j +
// 16k), and the hue would jump at every multiple of 4 and 16. Offset 0 is
// common to all three channels, so the core stays white, blue gathers nearest
// the source and red reaches the tips.
//
// The line is exact only where a channel's pitch, uCrossLength times its
// stride, is a whole number of texels; elsewhere the bilinear taps add a
// texel-scale ripple along it, as they do to white at a fractional length,
// so red and blue carry a slight one at the default length. Past a pitch of
// 2 texels the taps stop overlapping and an arm breaks into a lattice, which
// is where RenderCrossFilterLength's range stops, so no channel's stride is
// let take its pitch past 2 either.
const vec3 CROSS_DISPERSION = vec3(0.3, 0.0, -0.3);

vec3 crossSample(vec2 offset)
{
    return texture(diffuseMap, vary_texcoord0 + offset).rgb;
}

void main()
{
    vec3  accum   = vec3(0.0);
    float total_w = 0.0;

    vec3 stride = vec3(1.0) + clamp(uCrossChromatic, 0.0, 1.0) * CROSS_DISPERSION;
    stride = min(stride, vec3(2.0 / max(uCrossLength, 1e-4)));

    for (int i = 0; i < TAPS; ++i)
    {
        float step_index = float(i) * uCrossPassScale;
        vec2  offset     = uCrossDir * uCrossTexel * uCrossLength * step_index;

        // Attenuation is exponential in the step index, so the three passes
        // compose into one continuous exponential rather than three banded
        // ones: weight(a) * weight(b) == weight(a + b).
        float weight = pow(uCrossFalloff, -step_index);

        vec3 c;
        if (uCrossChromatic > 0.0)
        {
            c.r = crossSample(offset * stride.r).r;
            c.g = crossSample(offset * stride.g).g;
            c.b = crossSample(offset * stride.b).b;
        }
        else
        {
            c = crossSample(offset);
        }

        accum   += c * weight;
        total_w += weight;
    }

    // Normalise by the total tap weight, making this a weighted average along
    // the arm rather than a sum, so a uniform region passes through unchanged
    // and falloff shapes the arm without also setting its brightness.
    accum /= max(total_w, 1e-4);

    frag_color = vec4(accum, 0.0);
}
