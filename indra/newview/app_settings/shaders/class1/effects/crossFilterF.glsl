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
// the deliberate value: every target in the chain is R11F_G11F_B10F and has no
// alpha to keep, so the write is discarded. Declaring vec4 keeps the output
// shape stable if a future target ever does carry one.
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
// Two things break that, and both were shipped and had to be found the hard
// way:
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
const float CHAIN_REACH = float(TAPS * TAPS * TAPS - 1);

// Cheap spectrum for the dispersion. A real star filter is a diffraction
// grating, and a grating separates wavelengths by angle -- which is why the
// tips of the streaks go rainbow while the core stays white. Three overlapping
// triangular lobes are enough to read as that without a LUT.
vec3 spectrum(float t)
{
    float s = clamp(t, 0.0, 1.0) * 3.0;
    return clamp(vec3(1.5 - abs(s - 0.5),
                      1.5 - abs(s - 1.5),
                      1.5 - abs(s - 2.5)), 0.0, 1.0);
}

void main()
{
    vec3  accum   = vec3(0.0);
    float total_w = 0.0;

    for (int i = 0; i < TAPS; ++i)
    {
        float step_index = float(i) * uCrossPassScale;
        vec2  offset     = uCrossDir * uCrossTexel * uCrossLength * step_index;

        // Attenuation is exponential in the step index, so the three passes
        // compose into one continuous exponential rather than three banded
        // ones: weight(a) * weight(b) == weight(a + b).
        float weight = pow(uCrossFalloff, -step_index);

        vec3 tint = vec3(1.0);
        if (uCrossChromatic > 0.0)
        {
            // Normalised against the chain's full reach so the hue sweep spans
            // the whole arm.
            float t = clamp(step_index / CHAIN_REACH, 0.0, 1.0);

            // Normalised so the three lobes always average to white.
            // Dispersion redistributes a tap's energy across the channels; it
            // must not add or remove any, or the dispersion slider doubles as a
            // brightness slider.
            vec3 sp = spectrum(t);
            sp *= 3.0 / max(sp.r + sp.g + sp.b, 1e-4);

            // The ramp by t keeps the core white, and it is not cosmetic. A
            // grating deviates by wavelength, so at zero deviation every
            // wavelength lands in the same place: the centre of a streak is
            // white by construction and only the tips separate into colour.
            tint = mix(vec3(1.0), sp, uCrossChromatic * t);
        }

        accum   += texture(diffuseMap, vary_texcoord0 + offset).rgb * weight * tint;
        total_w += weight;
    }

    // Normalise by the total tap weight, making this a weighted average along
    // the arm rather than a sum, so a uniform region passes through unchanged
    // and falloff shapes the arm without also setting its brightness.
    accum /= max(total_w, 1e-4);

    frag_color = vec4(accum, 0.0);
}
