/**
 * @file auroraF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

out vec4 frag_data[4];

in vec3 vary_local_pos;

uniform float aurora_intensity; // 0..~1, CPU-side attenuated by moon etc.
uniform float aurora_time;      // seconds, wrapped to avoid precision drift
uniform vec3  camPosLocal;      // camera position in dome-local space (Y-up)

// 3D value noise
float hash31(vec3 p)
{
    p  = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
               u.z);
}

// Hides the R11F_G11F_B10F emissive attachment's 6/6/5-mantissa banding in this writer's
// smooth gradients. Defined in deferred/globalF.glsl, which every fragment stage attaches.
vec3 ditherEmissive(vec3 v, vec2 frag_px);

void main()
{
    vec3  dir  = normalize(vary_local_pos - camPosLocal);
    float elev = dir.y;

    vec2  horiz = dir.xz;
    float hlen  = length(horiz);
    if (hlen < 1e-3) discard;
    vec2  az = horiz / hlen;

    // Keep a generous performance cap but make sure the visible fade itself is
    // driven by the smooth envelopes below — a tight discard near the visible
    // edge produces a hard line where bloom amplifies sub-threshold values.
    if (elev < -0.05) discard;
    if (elev > 0.90) discard;

    float t = aurora_time;

    // ----- Multi-cluster coverage -----------------------------------------
    // Two-scale noise gives several active sectors distributed around the
    // sky with natural gaps between them, so the aurora reads as a
    // multi-direction display rather than a single sheet stuck on one
    // side. Slow temporal drift reshapes the clusters over minutes.
    float cov_big = valueNoise3D(vec3(az * 1.0, t * 0.03));
    float cov_med = valueNoise3D(vec3(az * 2.8, t * 0.07));
    // Wider smoothstep so the sub-threshold tail fades smoothly instead of
    // cutting off at a cov=0.01 edge. Let the final strength discard handle
    // the "no contribution" case.
    float cov     = smoothstep(0.22, 0.90, cov_big * 0.62 + cov_med * 0.38);

    // ----- Base arc with traveling wave folds -----------------------------
    // The arc's elevation undulates as a sum of waves moving in opposite
    // directions; interference between them produces the slow, organic
    // curtain-folding motion seen in real displays rather than a clean
    // periodic wobble. Amplitudes kept modest so the arc sits low on the
    // horizon rather than bowing up into the middle of the sky.
    float fold = 0.06 * sin(az.x * 2.0 + t * 0.18)
               + 0.04 * sin(az.y * 3.1 + t * 0.27 + 1.4)
               + 0.03 * sin(az.x * 4.3 - t * 0.33)
               + 0.03 * valueNoise3D(vec3(az * 2.5, t * 0.11));
    float base = 0.10 + fold;

    float d_above = elev - base;
    if (d_above > 0.55) discard;   // no below-base discard — let it fade naturally

    // ----- Soft vertical streaks ------------------------------------------
    // Strongly anisotropic fBm: azimuth sampled at ~8x the frequency of
    // elevation, so noise cells stretch vertically into soft streaks with
    // gently varying widths — neither sharp filaments nor isotropic
    // blobs. Each octave carries its own lateral drift and temporal
    // morph rate so the pattern never settles into a visible period.
    vec2 drift = vec2(t * 0.045, t * 0.028);
    vec3 p     = vec3(az * 16.0 + drift, elev * 2.0);

    float n  = 0.50 * valueNoise3D(p             + vec3(0.0, 0.0, t * 0.09));
    n       += 0.28 * valueNoise3D(p * 2.1       + vec3(drift,       t * 0.14));
    n       += 0.14 * valueNoise3D(p * 4.5       + vec3(drift * 1.4, t * 0.22));
    n       += 0.08 * valueNoise3D(p * 9.5       + vec3(drift * 1.9, t * 0.33));

    // Soft smoothstep instead of clamp+pow so the brightest curtain peaks
    // don't plateau at 1.0 — that plateau becomes a visible "ceiling" where
    // the elevation fade then scales it down, producing the hard top edge
    // the user was seeing.
    float curtain_mask = pow(smoothstep(0.05, 0.92, n), 1.6);

    // ----- Elevation envelope --------------------------------------------
    // A single smooth factor for the horizon and zenith transitions,
    // applied uniformly across body/halo/base_line. The ranges extend
    // slightly past the visible regions so both ends ease out below the
    // perceptual threshold rather than ending on a smoothstep knee.
    float horizon_fade = smoothstep(-0.03, 0.28, elev);
    float zenith_fade  = smoothstep(0.88, 0.38, elev);
    float elev_env     = horizon_fade * zenith_fade;

    // ----- Body envelope --------------------------------------------------
    // Below-base fade rate softened further (was 7) so the green trails
    // gradually toward the horizon with no visible edge.
    float body_top = 0.30;
    float body_env = exp(min(d_above, 0.0) * 3.5)
                   * smoothstep(body_top, body_top * 0.15, d_above);
    body_env *= elev_env;

    // ----- Top halo -------------------------------------------------------
    // Diffuse pink/purple cap above the body. Overlaps the body top so
    // the colour transition reads as a smooth gradient along each fold
    // rather than a hard seam.
    float halo_lo  = 0.18;
    float halo_hi  = 0.45;
    float halo_env = smoothstep(halo_lo * 0.40, halo_lo,        d_above)
                   * smoothstep(halo_hi * 1.15, halo_hi * 0.55, d_above);
    halo_env *= elev_env;

    // Halo modulation is a softened version of the curtain mask — some
    // structure is preserved so bright folds extend their colour up into
    // the halo, but contrast is reduced so the halo feels diffuse.
    float halo_mask = 0.5 + 0.5 * sqrt(curtain_mask);

    // ----- Base line glow -------------------------------------------------
    // Concentrated bright green at the 100km O layer. Narrow in elevation
    // but modulated along azimuth so it isn't a painted stripe. Lower
    // smoothstep extended so the line has a gentle skirt rather than a
    // hard lower cut-off.
    float base_line = exp(-pow((d_above - 0.02) * 20.0, 2.0));
    base_line *= smoothstep(-0.22, -0.02, d_above);
    base_line *= 0.45 + 0.55 * valueNoise3D(vec3(az * 6.0, t * 0.10));
    base_line *= elev_env;

    // ----- Animation layers -----------------------------------------------
    // Three independent time scales: a slow global breath with an
    // azimuth-dependent phase offset (different sky sectors brighten at
    // different moments), a medium pulse, and a fast shimmer sampled
    // with time-evolving noise. Together they give a rich, non-periodic
    // motion without any single animation reading as a repeating loop.
    float breath_phase = valueNoise3D(vec3(az * 1.2, 0.0)) * 6.28318;
    float breath  = 0.80 + 0.20 * sin(t * 0.18 + breath_phase);
    float pulse   = 0.88 + 0.12 * sin(t * 0.65);
    float shimmer = 0.88 + 0.12 * valueNoise3D(vec3(az * 22.0, t * 1.4));

    // ----- Palette --------------------------------------------------------
    vec3 col_green   = vec3(0.30, 1.50, 0.60);
    vec3 col_magenta = vec3(1.15, 0.50, 0.95);
    vec3 col_purple  = vec3(0.60, 0.35, 1.10);
    vec3 col_baseln  = vec3(0.50, 1.80, 0.80);

    float halo_t   = clamp((d_above - halo_lo) / (halo_hi - halo_lo), 0.0, 1.0);
    vec3  halo_col = mix(col_magenta, col_purple, smoothstep(0.35, 1.00, halo_t));

    // ----- Composition ----------------------------------------------------
    vec3 body_rgb = col_green   * body_env * curtain_mask;
    vec3 halo_rgb = halo_col    * halo_env * halo_mask * 0.60;
    vec3 base_rgb = col_baseln  * base_line;

    float strength = body_env * curtain_mask
                   + halo_env * halo_mask * 0.40
                   + base_line;
    strength *= cov * breath * pulse * shimmer;
    if (strength <= 0.003) discard;

    vec3 aurora_rgb = (body_rgb + halo_rgb + base_rgb)
                    * cov * breath * pulse * shimmer * aurora_intensity;

    // Per-fold chromatic bias — subtle warmer/cooler variation between
    // neighbouring sectors, matching per-column variability in O vs N2
    // emission ratios.
    float chroma_n = valueNoise3D(vec3(az * 11.0, 0.0));
    aurora_rgb *= mix(vec3(0.95, 1.05, 0.98),
                      vec3(1.05, 0.98, 1.02), chroma_n);

    float alpha = clamp(strength, 0.0, 1.0);

    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    frag_data[0] = vec4(0.0);
    frag_data[3] = vec4(ditherEmissive(aurora_rgb * 1.5, gl_FragCoord.xy), alpha);
#else
    frag_data[0] = vec4(aurora_rgb, alpha);
#endif
}
