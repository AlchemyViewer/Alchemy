/**
 * @file postEffectUtilsF.glsl
 * @brief Shared post-process effect helpers auto-linked into every program
 *        whose LLGLSLShader sets `mFeatures.hasPostEffects = true`
 *        (see llviewershadermgr.cpp).
 *
 * Two consumers currently link this file:
 *   - colorCorrectF.glsl  — calls applyChromaticAberration and computeLensFlare
 *                           in LINEAR space, before tonemap/gamma/LUT.
 *   - blitWithEffectsF.glsl — calls applyVignette, applyCVDCompensation,
 *                           applyFilmGrain, applyDither, applyPreview in
 *                           DISPLAY space, after all grading.
 *
 * Public entry points, grouped by logical pass:
 *
 *   LINEAR SPACE (colorCorrectF)
 *     vec4 applyChromaticAberration(sampler2D tex, vec2 uv)
 *     vec3 computeLensFlare       (sampler2D diff, sampler2D depth, vec2 uv)
 *
 *   DISPLAY SPACE (blitWithEffectsF)
 *     vec3 applyVignette          (vec3 color, vec2 uv)
 *     vec3 applyCVDCompensation   (vec3 color)
 *     vec3 applyFilmGrain         (vec3 color, vec2 fragCoord)
 *     vec3 applyDither            (vec3 color, vec2 fragCoord)
 *     vec3 applyPreview           (vec3 color)
 *
 * Conventions used throughout:
 *   - Every effect has an `amount <= 0` fast-path that returns the input
 *     unchanged, so the call site can unconditionally chain them.
 *   - Aspect correction, where needed, uses the branchless form
 *     `max(vec2(aspect, 1.0/aspect), 1.0)` — one axis stays 1.0 and the
 *     other carries the ratio, so the shorter axis's half-height is the
 *     normalization unit regardless of viewport shape.
 *   - Shared helpers (hash12, frameNoiseOffset, LUMA) live at the top.
 *   - Temporal decorrelation uses uFrameId hashed into a random UV offset,
 *     not a translation, so the noise fully reshuffles between frames
 *     instead of visibly scrolling.
 *
 * Several uniforms arrive in a CPU-baked form (see pipeline.cpp) — chromatic
 * aberration's amount/falloff/angle are the current examples. Those are
 * flagged inline next to their declarations.
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
 *
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

// =============================================================================
// Shared inputs
// =============================================================================

// Framebuffer resolution in pixels. Used for aspect correction so effects
// stay geometrically correct regardless of window size.
uniform vec2 uResolution;

// Atmospheric HDR scale. Provided by LLSettingsSky::applyToShader with the rest
// of the environment uniform set. Effects that emulate sun-lit phenomena (glow,
// starburst) scale by this so they read at the same relative brightness as the
// sky itself.
uniform float sky_hdr_scale;

// Frame counter, wraps at 2^32. Used by temporally-decorrelated noise
// effects (film grain, dither). Set to 0 for deterministic static output.
uniform uint uFrameId;

// Rec.709 luma weights — used by effects that need perceptual brightness
// (film grain luma weighting, etc.).
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// Cheap 2D → [0,1) hash used by noise-based effects (film grain, dither).
// Derived from Dave Hoskins' hash collection — quick and good enough for
// per-pixel decorrelated noise without needing a texture lookup.
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Per-frame pixel-space offset for temporally decorrelated noise. The frame
// id is hashed into two independent axes and scaled by 1024 so the sample
// location jumps to an effectively uncorrelated region each frame — giving
// a full pattern reshuffle instead of the scrolling you'd get from just
// adding the frame id to the input. Pass `animate=false` to freeze the
// pattern (useful for stills and debugging).
vec2 frameNoiseOffset(bool animate)
{
    uint  frame = animate ? uFrameId : 0u;
    float ff    = float(frame);
    return vec2(hash12(vec2(ff, 13.7)),
                hash12(vec2(ff, 91.3))) * 1024.0;
}


// =============================================================================
// Chromatic aberration
// =============================================================================
//
// Radial per-channel UV offset. Samples R and B along a (rotated) radial
// direction, leaves G and A at the center. Offset magnitude grows with
// distance from screen center so the center stays clean and fringing
// intensifies at the edges, mimicking real lens dispersion.
//
// uCAOffsetR / uCAOffsetB are directions in the (radial, tangential) basis
// built from the rotated radial vector. The x component rides the radial
// axis, the y component rides its perpendicular — this is what lets the
// effect do both classic SLR radial fringing and glitchier axis-swap looks
// from the same code path.

// Note: uCAAmount, uCAFalloff, and uCAAngleSinCos arrive in a pre-baked form
// from the CPU (pipeline.cpp) — squared-and-scaled strength, reciprocal
// falloff, and a (sin, cos) pair — so the shader avoids redoing a per-pixel
// mul / divide / sincos. The slider ranges shown in comments are the
// *user-facing* values before baking.
uniform float uCAAmount;                           // artist range [0, 1]: 0.15 SLR, 0.35 vintage,
                                                  //   0.6 anamorphic, 0.9 VHS. Uploaded pre-squared × 0.02.
uniform float uCAFalloff;                          // artist range [0.5, 4]: 0.5 corners-only, 1.0 SLR,
                                                  //   2.0 broad, 4.0 nearly uniform. Uploaded as 1/value.
uniform vec2  uCAAngleSinCos;                      // artist range [0, 360°] rotating the fringe axis
                                                  //   (0 radial, 45 diagonal, 90 tangential).
                                                  //   Uploaded as (sin, cos) of the radian-converted angle.
uniform vec2  uCAOffsetR;                          // R channel offset in (radial, tangential). Default: inward.
uniform vec2  uCAOffsetB;                          // B channel offset in (radial, tangential). Default: outward.
                                                  // Glitch preset: vec2(1,0) / vec2(0,1) = R→B swap.
uniform float uCAAnisotropy;                       // [-1, 1] axis stretch of the falloff region.
                                                  //         -1 = horizontal band only
                                                  //         +1 = vertical band only

vec4 applyChromaticAberration(sampler2D tex, vec2 uv)
{
    // Fast path when the effect is disabled — skip the extra taps.
    if (uCAAmount <= 0.0)
        return texture(tex, uv);

    // Vector from screen center, with anisotropy squashing one axis so the
    // fringe band can be biased horizontal or vertical.
    vec2 dir = uv - 0.5;
    dir *= vec2(1.0 - max( uCAAnisotropy, 0.0),
                1.0 - max(-uCAAnisotropy, 0.0));

    // Aspect correction as a branchless per-axis scale: one component is
    // always 1.0, the other is the larger-axis ratio. Applied to both the
    // per-pixel vector and the half-frame reference, so their ratio stays
    // meaningful regardless of viewport shape.
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2  scale  = max(vec2(aspect, 1.0 / max(aspect, 1e-4)), 1.0);

    // Normalize distance to [0, 1] over the aspect-corrected half-diagonal,
    // then raise to `uCAFalloff` (CPU already took the reciprocal of the
    // user-facing slider) so higher slider values produce MORE CA across
    // more of the frame.
    float normDist = clamp(length(dir * scale) / (0.5 * length(scale)), 0.0, 1.0);
    float dist     = pow(normDist, uCAFalloff);

    // Build the rotated radial basis (and its perpendicular) as a 2x2
    // matrix so uCAOffset*.y addresses the tangential axis via a single
    // matrix-vector multiply. Sin/cos come pre-computed from the CPU.
    float s = uCAAngleSinCos.x;
    float c = uCAAngleSinCos.y;
    vec2  rotDir = vec2(c * dir.x - s * dir.y,
                        s * dir.x + c * dir.y);
    mat2  basis  = mat2(rotDir, vec2(-rotDir.y, rotDir.x));

    // uCAAmount is already squared and scaled by 0.02 on the CPU, giving a
    // gentle slider toe and a plausible worst-case lens-like offset at 1.0.
    float strength = uCAAmount * dist;
    vec2  offR = basis * uCAOffsetR;
    vec2  offB = basis * uCAOffsetB;

    // Three taps: R offset, center (G + A), B offset.
    vec4 diff;
    diff.r  = texture(tex, uv + offR * strength).r;
    diff.ga = texture(tex, uv).ga;
    diff.b  = texture(tex, uv + offB * strength).b;
    return diff;
}


// =============================================================================
// Lens flare — anamorphic streak with optional glow, ghosts, halo, starburst
// =============================================================================
//
// Screen-space approximation of an anamorphic lens flare. Driven by a
// CPU-computed sun UV position and visibility (on-screen fade), plus a
// multi-tap depth occlusion check done here so geometry in front of the sun
// attenuates the flare smoothly.
//
// Each sub-effect (glow / ghosts / halo / starburst) is gated by its own
// intensity uniform (0 = disabled, else acts as a brightness multiplier), so
// this single function backs a variety of looks without branching from the
// call site.

// ---- Driver inputs (set by the viewer each frame) --------------------------
uniform float uLensFlareStrength;                 // master on/off + intensity
uniform vec2  uLensFlareSunPos;                   // sun position in UV space
uniform float uLensFlareSunVisibility;            // CPU-side on-screen fade
uniform vec3  uLensFlareLightColor;               // artist tint applied to whole flare

// ---- Depth occlusion -------------------------------------------------------
uniform float uLensFlareOcclusionRadius;          // Poisson disk radius in UV space
uniform int   uLensFlareOcclusionTaps;            // 1..32 — more taps = smoother partial occlusion

// ---- Anamorphic streak -----------------------------------------------------
uniform float uLensFlareStreakLength;             // horizontal extent in UV space
uniform float uLensFlareStreakFalloff;            // exponential falloff along the streak
uniform float uLensFlareStreakWidth;              // vertical half-thickness (gaussian sigma)
uniform float uLensFlareStreakIntensity;          // streak brightness multiplier
uniform vec3  uLensFlareStreakTint;               // anamorphic blue
uniform float uLensFlareChromaticSpread;          // vertical R/B offset for chromatic fringing

// ---- Central glow (intensity doubles as on/off) ----------------------------
uniform float uLensFlareGlow;                     // 0 = off, else brightness
uniform float uLensFlareGlowRadius;               // falloff radius in UV space
uniform float uLensFlareGlowFalloff;              // gaussian exponent sharpness

// ---- Ghosts: soft disks along the sun→center axis --------------------------
uniform float uLensFlareGhost;                    // 0 = off, else brightness
uniform int   uLensFlareGhostCount;               // number of ghosts to place
uniform float uLensFlareGhostSpacing;             // step size along the axis

// ---- Halo: ring opposite the sun -------------------------------------------
uniform float uLensFlareHalo;                     // 0 = off, else brightness
uniform float uLensFlareHaloRadius;               // ring radius in UV space
uniform float uLensFlareHaloWidth;                // ring thickness

// ---- Starburst: angular spikes radiating from the sun ----------------------
uniform float uLensFlareStarburst;                // 0 = off, else brightness
uniform int   uLensFlareStarburstSpikes;          // primary angular frequency — cos(θ·N) has 2N
                                                  //   lobes per full turn, so 4 here = 8 visible spikes
uniform float uLensFlareStarburstSharpness;       // pow() exponent — higher = tighter spikes
uniform float uLensFlareStarburstFalloff;         // radial decay rate from the sun

vec3 computeLensFlare(sampler2D diffuse, sampler2D depth, vec2 uv)
{
    // Master gate: cheapest possible early-out.
    float vis = uLensFlareSunVisibility * uLensFlareStrength;
    if (vis <= 0.0)
        return vec3(0.0);

    vec2 sun_uv = uLensFlareSunPos;

    // -------------------------------------------------------------------
    // Depth-based occlusion.
    //
    // CPU-side visibility only tracks whether the sun is on-screen; it
    // doesn't know about intervening geometry. Probe a Poisson disk of
    // depth taps around the sun's UV and count how many are at the far
    // plane (i.e. sky). This gives smooth partial occlusion when the sun
    // is half-behind an object.
    // -------------------------------------------------------------------
    if (all(greaterThanEqual(sun_uv, vec2(0.0))) && all(lessThanEqual(sun_uv, vec2(1.0))))
    {
        // Pre-baked Poisson disk samples, good spatial distribution.
        const vec2 taps[32] = vec2[32](
            vec2( 0.0,     0.0),
            vec2(-0.326,  -0.406),
            vec2(-0.840,  -0.074),
            vec2(-0.196,   0.457),
            vec2( 0.498,   0.336),
            vec2( 0.106,  -0.747),
            vec2( 0.736,  -0.290),
            vec2( 0.423,   0.767),
            vec2(-0.621,   0.572),
            vec2( 0.890,   0.156),
            vec2(-0.453,  -0.780),
            vec2( 0.215,  -0.945),
            vec2(-0.928,   0.326),
            vec2( 0.673,  -0.685),
            vec2(-0.158,   0.892),
            vec2( 0.952,   0.548),
            vec2(-0.756,  -0.518),
            vec2( 0.347,  -0.412),
            vec2(-0.089,  -0.290),
            vec2( 0.612,   0.710),
            vec2(-0.544,   0.815),
            vec2( 0.818,  -0.543),
            vec2(-0.987,  -0.321),
            vec2( 0.145,   0.623),
            vec2(-0.412,   0.178),
            vec2( 0.567,  -0.098),
            vec2(-0.278,  -0.654),
            vec2( 0.934,   0.389),
            vec2(-0.703,   0.112),
            vec2( 0.056,  -0.512),
            vec2( 0.289,   0.934),
            vec2(-0.867,   0.745)
        );
        int   num_taps = clamp(uLensFlareOcclusionTaps, 1, 32);
        float occluded = 0.0;
        for (int i = 0; i < num_taps; i++)
        {
            vec2  tap_uv = sun_uv + taps[i] * uLensFlareOcclusionRadius;
            float d      = texture(depth, tap_uv).r;
            // smoothstep against near-far plane — only sky counts as visible. Mirror the far
            // end under reverse-Z (far=0): smoothstep(0.9999,1,1-d) == 1-smoothstep(0,1e-4,d).
#ifdef REVERSE_Z
            occluded += smoothstep(0.9999, 1.0, 1.0 - d);
#else
            occluded += smoothstep(0.9999, 1.0, d);
#endif
        }
        vis *= occluded / float(num_taps);
    }
    if (vis <= 0.0)
        return vec3(0.0);

    // -------------------------------------------------------------------
    // Sample sun brightness once, convert to a normalized "overbright"
    // factor. We subtract 2.0 from luminance so only HDR-bright suns
    // drive the flare — prevents diffuse bright surfaces from flaring.
    // -------------------------------------------------------------------
    vec3  sun_color  = texture(diffuse, clamp(sun_uv, vec2(0.0), vec2(1.0))).rgb;
    float sun_lum    = dot(sun_color, LUMA);
    float sun_bright = max(sun_lum - 2.0, 0.0) / max(sun_lum, 1e-4);
    sun_color *= sun_bright;

    if (sun_bright <= 0.0)
        return vec3(0.0);

    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2  delta  = uv - sun_uv;
    vec3  flare  = vec3(0.0);

    // ---- Central glow: soft radial falloff --------------------------------
    // Scaled by sky_hdr_scale (same as starburst) so the glow reads at the
    // same relative brightness as the atmosphere it's painted against.
    if (uLensFlareGlow > 0.0)
    {
        vec2  gd      = vec2(delta.x * aspect, delta.y);
        float radius2 = max(uLensFlareGlowRadius * uLensFlareGlowRadius, 1e-8);
        // r² directly from dot() — avoids the length() sqrt.
        float glow    = exp(-dot(gd, gd) * uLensFlareGlowFalloff / radius2);
        flare += sun_color * glow * uLensFlareGlow * sky_hdr_scale;
    }

    // ---- Anamorphic streak: horizontal band with tight vertical gaussian --
    // Early-out when either the streak is disabled or the fragment is far
    // enough off-axis that the vertical gaussian is numerically zero. The
    // bound uses the widened fringe sigma when chromatic spread is active.
    float streak_w = max(uLensFlareStreakWidth,  1e-5);
    float streak_l = max(uLensFlareStreakLength, 1e-5);
    float fringe_w = streak_w + max(uLensFlareChromaticSpread, 0.0) * 0.5;
    if (uLensFlareStreakIntensity > 0.0 && abs(delta.y) < fringe_w * 8.0)
    {
        float horiz = abs(delta.x) * aspect;
        float vert  = abs(delta.y);

        float vert_falloff  = exp(-vert * vert / (streak_w * streak_w));
        float horiz_falloff = exp(-horiz * uLensFlareStreakFalloff / streak_l);
        float streak        = vert_falloff * horiz_falloff;

        // Chromatic fringing — offset R and B vertically from the base streak.
        // The fringe channels use a widened gaussian so they stay visible
        // even when the spread exceeds the base streak width. `fringe_w` is
        // already computed above for the early-out bound.
        if (uLensFlareChromaticSpread > 0.0)
        {
            float spread   = uLensFlareChromaticSpread;
            float vert_r   = abs(delta.y + spread);
            float vert_b   = abs(delta.y - spread);
            float streak_r = exp(-vert_r * vert_r / (fringe_w * fringe_w)) * horiz_falloff;
            float streak_b = exp(-vert_b * vert_b / (fringe_w * fringe_w)) * horiz_falloff;

            flare.r += sun_color.r * streak_r * uLensFlareStreakTint.r * uLensFlareStreakIntensity;
            flare.g += sun_color.g * streak   * uLensFlareStreakTint.g * uLensFlareStreakIntensity;
            flare.b += sun_color.b * streak_b * uLensFlareStreakTint.b * uLensFlareStreakIntensity;
        }
        else
        {
            flare += sun_color * streak * uLensFlareStreakTint * uLensFlareStreakIntensity;
        }
    }

    // ---- Ghosts: soft disks stepped along the sun→center axis -------------
    if (uLensFlareGhost > 0.0 && uLensFlareGhostCount > 0)
    {
        vec2 ghost_vec = vec2(0.5) - sun_uv;
        for (int i = 0; i < uLensFlareGhostCount; i++)
        {
            float t        = uLensFlareGhostSpacing * float(i + 1);
            vec2  ghost_uv = sun_uv + ghost_vec * t;
            float scale    = 1.0 / float(i + 1);        // later ghosts fade out
            float radius   = 0.04 * scale + 0.02;

            vec2 gd = uv - ghost_uv;
            gd.x *= aspect;
            float d    = length(gd);
            float disk = 1.0 - smoothstep(radius * 0.5, radius, d);

            flare += sun_color * disk * scale * uLensFlareGhost;
        }
    }

    // ---- Halo: thin ring on the opposite side of the screen from the sun -
    if (uLensFlareHalo > 0.0 && uLensFlareHaloRadius > 0.0)
    {
        vec2  halo_center = vec2(0.5) + (vec2(0.5) - sun_uv);
        float halo_dist   = length(uv - halo_center);
        float halo_w      = max(uLensFlareHaloWidth, 0.01);
        float halo        = 1.0 - abs(halo_dist - uLensFlareHaloRadius) / halo_w;
        halo  = clamp(halo, 0.0, 1.0);
        halo *= halo;                                    // soften the edges
        flare += sun_color * halo * 0.3 * uLensFlareHalo;
    }

    // ---- Starburst: angular spikes radiating from the sun -----------------
    // Three stacked cos^N harmonics give a richer star pattern than one.
    //
    // The starburst pattern factors as `envelope(sun_dist) * angular(θ)`.
    // Computing the envelope first lets us skip the three pow/cos terms
    // and the atan when envelope is vanishingly small — which is the case
    // for most fragments (spikes are localized around the sun).
    if (uLensFlareStarburst > 0.0)
    {
        vec2 sd = delta;
        sd.x *= aspect;
        float sun_dist = length(sd);

        // Radial decay × core-fade. The core_fade is what prevents the
        // spikes from stacking on top of the glow's bright center — they
        // emanate from its edge instead.
        float radial    = exp(-sun_dist * max(uLensFlareStarburstFalloff, 0.01));
        float core_fade = smoothstep(0.0, max(uLensFlareGlowRadius, 1e-4), sun_dist);
        float envelope  = radial * core_fade;

        if (envelope > 1e-5)
        {
            float angle   = atan(sd.y, sd.x);
            float primary = float(max(uLensFlareStarburstSpikes, 1));
            float sharp   = max(uLensFlareStarburstSharpness, 1.0);

            float pattern = pow(abs(cos(angle * primary)),             sharp       ) * 0.5
                          + pow(abs(cos(angle * primary * 2.0 + 0.5)), sharp * 1.33) * 0.3
                          + pow(abs(cos(angle * primary * 4.0 + 1.0)), sharp * 1.66) * 0.2;

            flare += sun_color * pattern * envelope
                   * uLensFlareStarburst * sky_hdr_scale;
        }
    }

    // Final scale: 0.15 tames peak intensity to a plausible lens-response
    // range; tint and visibility factor apply equally to all sub-effects.
    return max(flare * vis * uLensFlareLightColor * 0.15, vec3(0.0));
}


// =============================================================================
// Vignette — frame darkening with aspect correction and optional 3-color ramp
// =============================================================================
//
// Distances are measured in units of half-image-height on the shorter axis,
// so uVignetteRadius = 1.0 reaches the top/bottom edges on any aspect.
// Values above 1.0 extend the falloff past the corners — useful on
// widescreen where only extreme edges should be touched.
//
// The shape parameter morphs between a circular falloff (distance = length)
// and a rounded-square falloff (distance = max(|x|,|y|)^1.5), so the same
// effect covers classic optical vignettes and stylized CRT/screen looks.
//
// Optional three-color ramp (image → mid → edge) activates only when
// uVignetteMidPoint lands strictly in (0, 1). Otherwise, the two-color
// path is used, which is the common case.

uniform float uVignetteAmount;                   // [0, 1]   strength
uniform float uVignetteRadius;                   // [0.25, 1.5] edge distance in half-height units
uniform float uVignetteSoft;                     // [0.05, 1]   width of the falloff band
uniform float uVignetteShape;                    // [0, 1]   0 = circular, 1 = rounded square
uniform vec3  uVignetteColor;                    // outer (corner) color
uniform vec3  uVignetteMidColor;                 // intermediate ramp color
uniform float uVignetteMidPoint;                 // [0, 1]   0/1 = two-color fallback; else three-color
uniform vec2  uVignetteCenter;                   // [-0.5, 0.5] offset from frame center
uniform vec2  uVignetteAspect;                   // image (w, h); (1,1) disables aspect correction
uniform float uVignetteFeather;                  // [0.2, 4.0] shape of the darkening curve
                                                //   <1 = spreads darkening inward (gentle haze)
                                                //    1 = linear smoothstep falloff
                                                //   >1 = concentrates darkening at the edges

vec3 applyVignette(vec3 color, vec2 uv)
{
    if (uVignetteAmount <= 0.0)
        return color;

    // Center offset and branchless aspect correction: one component of
    // `scale` is always 1.0, the other is the larger-axis ratio, so the
    // "short-axis half-height" normalization below holds for any viewport.
    vec2  d      = uv - 0.5 - uVignetteCenter;
    float aspect = max(uVignetteAspect.x, 1e-4) / max(uVignetteAspect.y, 1e-4);
    d *= max(vec2(aspect, 1.0 / aspect), 1.0);

    // Two distance metrics, blended by `uVignetteShape`:
    //   - circular  = Euclidean length (classic optical vignette)
    //   - square    = Chebyshev length raised to 1.5 (rounded-square CRT look)
    // `sq_base` is non-negative by construction, so `x * sqrt(x)` is a valid
    // faster form of pow(x, 1.5) without a domain guard.
    float circular = length(d) * 2.0;
    float sq_base  = max(abs(d.x), abs(d.y)) * 2.0;
    float square   = sq_base * sqrt(sq_base);
    float dist     = mix(circular, square, uVignetteShape);

    // `start` is where fading begins; `end` is where the edge color is reached.
    float start = uVignetteRadius - uVignetteSoft;
    float end   = uVignetteRadius;

    // Three-color ramp only if midpoint is strictly inside (0, 1).
    if (uVignetteMidPoint > 1e-4 && uVignetteMidPoint < 0.9999)
    {
        float mid      = mix(start, end, uVignetteMidPoint);
        float tMidRaw  = smoothstep(start, mid, dist);
        float tEdgeRaw = smoothstep(mid,   end, dist);
        // pow reshapes the mask before scaling by overall amount.
        float tMid  = pow(tMidRaw,  uVignetteFeather) * uVignetteAmount;
        float tEdge = pow(tEdgeRaw, uVignetteFeather) * uVignetteAmount;
        color = mix(color, uVignetteMidColor, tMid);
        color = mix(color, uVignetteColor,    tEdge);
    }
    else
    {
        float tRaw = smoothstep(start, end, dist);
        float t    = pow(tRaw, uVignetteFeather) * uVignetteAmount;
        color = mix(color, uVignetteColor, t);
    }
    return color;
}


// =============================================================================
// Film grain — additive hash-based noise with luma weighting
// =============================================================================
//
// Four styles:
//   0 mono luma    — classic film, same noise in R/G/B, midtone-weighted
//   1 color        — digital push, independent noise per channel
//   2 coarse       — 16mm feel, larger "grains" via pixel quantization
//   3 photon shot  — CCD-style, amplitude scales with sqrt(luminance)
//
// Size is multiplied by the display's resolution-relative pixel scale so
// grain particles stay perceptually constant across 1080p / 4K / HiDPI
// outputs. Amount is squared in-shader for a gentler slider toe; keeping
// it in-shader (vs. pre-baking on the CPU like the CA uniforms) lets a
// future UI slider animate without rebinding on every change.

uniform float uGrainAmount;                      // [0, 1]   strength (squared internally)
uniform int   uGrainStyle;                       // 0 mono, 1 color, 2 coarse, 3 photon shot
uniform float uGrainSize;                        // [1, 8]   grain cell size in 1080p-equivalent pixels
uniform float uGrainRange;                       // [0, 1]   luma position where grain peaks
                                                //          0 = shadows, 0.5 = midtones, 1 = highlights
uniform vec3  uGrainTint;                        // neutral gray; try (1, 0.9, 0.8) warm, (0.8, 0.9, 1) cool
uniform bool  uGrainAnimate;                     // false = frozen pattern (static frame for stills)

// Noise sample in ~[-0.5, 0.5]. Style controls color vs luma and whether
// cells are quantized for a coarser look. The caller scales by amplitude.
vec3 grainSample(vec2 fragCoord, int style, float size)
{
    float cell = max(size, 1.0);
    vec2  gp   = floor(fragCoord / cell) * cell;
    if (style == 1)
    {
        // Independent per-channel noise for a "digital" look.
        return vec3(hash12(gp),
                    hash12(gp + 17.0),
                    hash12(gp + 31.0)) - 0.5;
    }
    return vec3(hash12(gp) - 0.5);
}

vec3 applyFilmGrain(vec3 color, vec2 fragCoord)
{
    if (uGrainAmount <= 0.0)
        return color;

    // 0.1 is the empirical ceiling where grain stops reading as "film
    // texture" and starts reading as "broken image." Squaring amount
    // biases the slider toward subtle values.
    float amp   = uGrainAmount * uGrainAmount * 0.1;
    float gluma = dot(color, LUMA);

    // Scale grain cells by resolution so a "size 1" grain looks the same
    // perceptual size on 1080p and 4K displays.
    float pixelScale    = max(uResolution.x, uResolution.y) / 1080.0;
    float effectiveSize = uGrainSize * pixelScale;
    vec3  n = grainSample(fragCoord + frameNoiseOffset(uGrainAnimate),
                          uGrainStyle, effectiveSize);

    if (uGrainStyle == 3)
    {
        // Photon shot noise: amplitude proportional to sqrt(luminance),
        // matching how CCD sensor noise scales with photon count.
        color += n * uGrainTint * sqrt(max(gluma, 0.0)) * amp;
    }
    else
    {
        // Midtone-biased bell: weight peaks at uGrainRange and falls off
        // toward shadows/highlights. Matches how real film grain is most
        // visible in midtones.
        float d      = gluma - uGrainRange;
        float weight = max(1.0 - (d * d) * 4.0, 0.0);
        color += n * uGrainTint * weight * amp;
    }
    return color;
}


// =============================================================================
// Color Vision Deficiency — simulation, daltonization, accessibility preview
// =============================================================================
//
// Two separate use cases share these helpers:
//   1. Compensation (daltonization) — run in the output pipeline to make the
//      image more distinguishable to CVD viewers. Controlled by uCompensate*.
//   2. Preview/debug — optionally re-simulate the output as a CVD viewer
//      would perceive it, or as a false-color exposure map. Controlled by
//      uPreviewMode. Must be 0 for normal shipping output.
//
// CVD matrices are Machado et al. 2009 (severity = 1.0) — physiologically
// grounded and slightly more accurate than Brettel/Viénot, especially for
// tritanopia. Monochromacy/achromatomaly modes are implemented as
// desaturation toward Rec.709 luma.

// ---- Compensation (daltonization, output path) -----------------------------
uniform int   uCompensateMode;           // [0, 3] — 0 off, 1 protan, 2 deutan, 3 tritan
uniform float uCompensateAmount;         // [0, 1]   strength of the channel redistribution

// ---- Preview / debug --------------------------------------------------------
uniform int   uPreviewMode;              // MUST be 0 for final output.
                                        // 1 = protanopia (no L cones)
                                        // 2 = deuteranopia (no M cones)
                                        // 3 = tritanopia (no S cones)
                                        // 4 = achromatopsia (rod monochromacy) — luminance only
                                        // 5 = blue cone monochromacy — blue-tinted near-gray
                                        // 6 = achromatomaly — strong desaturation
                                        // 7 = false-color exposure map

// Forward-simulate how a CVD viewer perceives `c`. Used both internally
// (daltonize computes the difference between original and simulated) and
// as the preview overlay.
vec3 simulateCVD(vec3 c, int mode)
{
    if (mode == 4)
    {
        // Achromatopsia (rod monochromacy). Real rod response peaks at
        // ~507nm and biases neutral gray slightly cyan-green; standard
        // practice uses Rec.709 luma as a reasonable proxy.
        return vec3(dot(c, LUMA));
    }
    if (mode == 5)
    {
        // Blue cone monochromacy — only S-cones work, so the image
        // collapses to a blue-yellow axis. Approximated by extracting the
        // blue-yellow opponent signal and recombining as a desaturated
        // blue tint. Not physiologically exact but matches common
        // simulator output and reads correctly to normal viewers.
        float l  = dot(c, LUMA);
        float by = c.b - 0.5 * (c.r + c.g);
        return vec3(l - by * 0.15, l - by * 0.05, l + by * 0.25);
    }
    if (mode == 6)
    {
        // Achromatomaly — partial color loss. 80% blend toward gray
        // retains a hint of residual color.
        return mix(c, vec3(dot(c, LUMA)), 0.8);
    }

    mat3 m;
    if (mode == 1)
    {
        // Protanopia (Machado 2009, severity 1.0)
        m = mat3(0.152286, 1.052583, -0.204868,
                 0.114503, 0.786281,  0.099216,
                -0.003882,-0.048116,  1.051998);
    }
    else if (mode == 2)
    {
        // Deuteranopia (Machado 2009, severity 1.0)
        m = mat3(0.367322, 0.860646, -0.227968,
                 0.280085, 0.672501,  0.047413,
                -0.011820, 0.042940,  0.968881);
    }
    else
    {
        // Tritanopia (Machado 2009, severity 1.0)
        m = mat3(1.255528,-0.078441, -0.004733,
                -0.076749, 0.930809,  0.691367,
                 0.178779,-0.147632,  0.303900);
    }
    return m * c;
}

// Daltonization: compute the error between original and CVD-simulated
// image, then redistribute that error into channels the viewer can still
// see. Monochromacies (modes ≥ 4) have no perceivable axis to shift into,
// so the function is a no-op for them.
vec3 daltonize(vec3 c, int mode, float amount)
{
    if (mode >= 4) return c;

    vec3 sim = simulateCVD(c, mode);
    vec3 err = c - sim;
    vec3 shift;
    if (mode == 3)
    {
        // Tritan — shift blue error into the red/green axis.
        float be = err.b;
        shift = vec3( be*0.7 + err.g*0.3, -be*0.7 - err.g*0.3, 0.0);
    }
    else
    {
        // Protan/deutan — shift red/green error into green and blue.
        shift = vec3(0.0, err.r*0.7 + err.g, err.r*0.7);
    }
    return c + shift * amount;
}

// False-color exposure map (ARRI-style palette). 8 bands from under-exposed
// (deep blue) to clipped (white), keyed on Rec.709 luminance.
vec3 falseColor(vec3 c)
{
    float l = dot(c, LUMA);
    if (l < 0.02) return vec3(0.0, 0.0, 0.4);
    if (l < 0.10) return vec3(0.0, 0.3, 0.8);
    if (l < 0.30) return vec3(0.0, 0.7, 0.5);
    if (l < 0.50) return vec3(0.3, 0.7, 0.0);
    if (l < 0.70) return vec3(0.8, 0.8, 0.0);
    if (l < 0.90) return vec3(1.0, 0.5, 0.0);
    if (l < 0.98) return vec3(1.0, 0.1, 0.0);
    return vec3(1.0);
}

// Accessibility pipeline entry. Zero-cost when disabled.
vec3 applyCVDCompensation(vec3 color)
{
    if (uCompensateMode <= 0 || uCompensateAmount <= 0.0)
        return color;
    return clamp(daltonize(color, uCompensateMode, uCompensateAmount), 0.0, 1.0);
}

// Preview overlay entry. Mode 7 replaces the image entirely with a
// false-color map; modes 1-6 re-simulate the output as a CVD viewer
// would perceive it. Intended for debugging only — must be 0 for final
// shipping output.
vec3 applyPreview(vec3 color)
{
    // Fast path — zero preview mode is the shipping configuration and should
    // cost a single compare.
    if (uPreviewMode == 0)
        return color;
    if (uPreviewMode == 7)
        return falseColor(color);
    return simulateCVD(clamp(color, 0.0, 1.0), uPreviewMode);
}


// =============================================================================
// Dither — triangular PDF noise for banding suppression at the output stage
// =============================================================================
//
// Interleaved Gradient Noise (Jimenez, COD:AW 2014) sampled twice with a
// small offset to produce a TPDF-shaped noise in [-1, 1]. TPDF is
// perceptually superior to rectangular PDF — it fully masks quantization
// banding with no residual noise-floor shift.
//
// Temporal decorrelation is handled by frameNoiseOffset() (see top of file):
// the noise pattern fully reshuffles between frames instead of visibly
// scrolling as uFrameId increments.
//
// Amplitude scales to output bit depth: ±1/255 for 8-bit, ±1/1023 for 10-bit.

uniform float uDitherAmount;             // [0, 1]   TPDF amplitude scale. Leave at 1.0.
uniform int   uDitherBits;              // {8, 10}  output bit depth.
uniform bool  uDitherAnimate;           // false = freeze pattern. Almost always want true —
                                        // animated dither is perceptually invisible,
                                        // static dither can show as fixed-pattern grain.

// Interleaved Gradient Noise core — hashes a 2D input to a well-distributed
// [0, 1) sample. Frame decorrelation is applied at the call site via
// frameNoiseOffset(); the TPDF pair must share the same offset so keeping it
// outside IGN is essential, not just an optimization.
float ign(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 applyDither(vec3 color, vec2 fragCoord)
{
    if (uDitherAmount <= 0.0)
        return color;

    // Compute the frame offset *once* and reuse for both TPDF taps — the
    // IGN calls are cheap but factoring the per-frame hash out keeps the
    // pattern coherent across the pair (otherwise the two taps would
    // decorrelate from each other, not just from previous frames).
    vec2 p = fragCoord + frameNoiseOffset(uDitherAnimate);

    float tpdf = ign(p) + ign(p + vec2(1.618, 2.414)) - 1.0;

    float levels = (uDitherBits >= 10) ? 1023.0 : 255.0;
    return color + tpdf * (uDitherAmount / levels);
}
