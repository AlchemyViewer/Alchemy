/**
 * @file bloomExtractF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

// HDR bloom extract.
// rgb = soft-knee thresholded bloom source, plus the legacy alpha-tagged prim-glow signal
// promoted into linear light so it blooms as though it were a bright highlight.
// a   = halation intensity (red-weighted luminance excess), only when BLOOM_HALATION
//       is defined. With halation disabled the pyramid is allocated as
//       R11F_G11F_B10F so there is no alpha channel to write, and the warmth work
//       is skipped entirely.
#ifdef BLOOM_HALATION
out vec4 frag_color;
#else
out vec3 frag_color;
#endif

uniform sampler2D diffuseMap;
uniform float bloom_threshold;
uniform float bloom_knee;
uniform float alpha_glow_boost;
#ifdef BLOOM_HALATION
uniform vec3  halation_lum_weights;
#endif

in vec2 vary_texcoord0;

// Soft-knee excess: quadratic ramp of width 2*K centered on the threshold,
// falls back to a hard (lum - T) above the knee.
float softThreshold(float lum, float T, float K)
{
    float k = max(K, 1e-4);
    float soft = clamp(lum - T + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k + 1e-4);
    return max(soft, lum - T);
}

void main()
{
    vec4 col = texture(diffuseMap, vary_texcoord0);
    vec3 rgb = max(col.rgb, vec3(0.0));

    float lum = max(max(rgb.r, rgb.g), rgb.b);
    float factor = softThreshold(lum, bloom_threshold, bloom_knee) / max(lum, 1e-4);
    // Tonemap compensation is applied to the legacy alpha-glow term only: legacy glow
    // composited in LDR post-tonemap where small additive values survived, while HDR
    // bloom composites in linear pre-tonemap where ACES rolloff crushes low fringe
    // values. The HDR luminance path stays at 1x so authored HDR highlights bloom on
    // their own physical terms.
    const float legacy_tonemap_compensation = 2.0;
    vec3 bloom = rgb * factor
               + rgb * col.a * alpha_glow_boost * legacy_tonemap_compensation;

#ifdef BLOOM_HALATION
    float warmth = max(rgb.r * halation_lum_weights.r,
                   max(rgb.g * halation_lum_weights.g,
                       rgb.b * halation_lum_weights.b));
    float hal = softThreshold(warmth, bloom_threshold, bloom_knee);
    frag_color = vec4(bloom, hal);
#else
    frag_color = bloom;
#endif
}
