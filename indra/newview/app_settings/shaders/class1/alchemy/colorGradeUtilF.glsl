/**
 * @file colorGradeUtilF.glsl
 * @brief Display-space color grading helpers attached to every program whose
 *        LLGLSLShader sets `mFeatures.hasColorGrade = true`
 *        (see llshadermgr.cpp:attachShaderFeatures).
 *
 * Consumer:
 *   - colorCorrectF.glsl — runs the full grading chain between tonemap and
 *                          the final dither/blit. Split toning runs in LINEAR
 *                          space right after tonemap; every other helper here
 *                          runs in DISPLAY space after the gamma encode.
 *
 * Public entry points, in pipeline order:
 *
 *   LINEAR SPACE (post-tonemap, pre-gamma)
 *     vec3 applySplitToning          (vec3 col)   // step 5
 *
 *   DISPLAY SPACE (post-gamma)
 *     vec3 applyBlackWhitePoint      (vec3 col)   // step 6.5
 *     vec3 applyBrightnessContrast   (vec3 col)   // step 7
 *     vec3 applyShadowHighlightRecovery(vec3 col) // step 7.5
 *     vec3 applySaturation           (vec3 col)   // step 8
 *     vec3 applyVibrance             (vec3 col)   // step 9
 *     vec3 applyHueShift             (vec3 col)   // step 10
 *     vec3 applyLUTGrading           (vec3 col)   // step 11
 *     vec3 applyChannelCurves        (vec3 col)   // step 12
 *
 * Conventions used throughout:
 *   - Every helper has a no-op fast-path for its identity uniform values
 *     (amount <= 0, strength <= 0, or untouched endpoints), so the call site
 *     can chain them unconditionally without paying for disabled effects.
 *   - Uniforms are CPU-clamped in pipeline.cpp to the artist-facing ranges
 *     shown in their inline comments; the shader still guards against
 *     divide-by-zero at boundary values.
 *   - Split toning lives here instead of in postEffectUtilsF because it is
 *     gated on the LUT-grading shader variant and shares the `CG_LUMA`
 *     Rec.709 weights with the display-space saturation/vibrance helpers.
 *   - Shared helpers (CG_LUMA, cg_rgb2hsv/cg_hsv2rgb, cg_sCurve) are prefixed
 *     `cg_` / `CG_` to avoid colliding with `LUMA` defined in postEffectUtilsF,
 *     which may be linked into the same program.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2021-2026, Rye Mutt <rye@alchemyviewer.org>
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
// Shared helpers
// =============================================================================

// Rec.709 luma weights. Used by saturation, vibrance, recovery masks, and
// split toning. Prefixed to avoid colliding with postEffectUtilsF::LUMA when
// both utilities are linked into the same program.
const vec3 CG_LUMA = vec3(0.2126, 0.7152, 0.0722);

// HSV conversions (Sam Hocevar branchless form). Valid for display-space
// values in [0, 1]; hue in hsv.x is a fraction on [0, 1).
vec3 cg_rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 cg_hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Single-channel filmic S-curve. Maps x through a smoothstep between `toe`
// and `shoulder`, then blends back toward the input by `strength`.
float cg_sCurve(float x, float toe, float shoulder, float strength)
{
    float t = clamp((x - toe) / max(shoulder - toe, 1e-4), 0.0, 1.0);
    float s = t * t * (3.0 - 2.0 * t);
    return mix(x, s, clamp(strength, 0.0, 1.0));
}


// =============================================================================
// Step 5 — Split toning  (LINEAR space, after tonemap)
// =============================================================================
//
// Lightroom-style split toning. For each pixel, build three luma-masked
// versions of the input — one tinted toward uShadowTint, one toward
// uMidtoneTint, one toward uHighlightTint — each scaled so its perceived
// brightness matches the original (division by the tint's own luma). Blend
// them back in proportional to their masks. The shadow/highlight amount is
// shared (`uToneAmount`); midtones have their own amount so a full-strength
// shadow/highlight grade can coexist with an untouched midtone range, or
// vice versa.
//
// `uToneBalance` slides the split point across the luma histogram, giving
// the artist control over whether "shadows" means the bottom 30% or the
// bottom 70% of the tonal range.

uniform vec3  uShadowTint;       // [0, 1] default vec3(0.5) — target color for
                                 //   shadows. vec3(0.5) is the identity.
uniform vec3  uHighlightTint;    // [0, 1] default vec3(0.5) — target color for
                                 //   highlights.
uniform vec3  uMidtoneTint;      // [0, 1] default vec3(0.5) — target color for
                                 //   midtones.
uniform float uMidtoneAmount;    // [0, 1] default 0 — midtone tint strength.
uniform float uToneBalance;      // [-1, 1] default 0 — slides the luma midpoint.
                                 //   -0.6 for dark scenes, +0.4 for bright.
uniform float uToneAmount;       // [0, 1] default 0 — shadow/highlight strength.
                                 //   0.5 is typical. 0 skips the effect entirely.

vec3 applySplitToning(vec3 col)
{
    // Fast path: full feature disabled.
    if (uToneAmount <= 0.0)
        return col;

    float l   = dot(col, CG_LUMA);
    float mid = 0.5 + uToneBalance * 0.4;

    // Three masks over the luma axis — midtones peak at the slide point and
    // fall off into shadow/highlight as luma departs from mid.
    float hi = smoothstep(mid, mid + 0.35, l);
    float lo = 1.0 - smoothstep(mid - 0.35, mid, l);
    float md = max(1.0 - hi - lo, 0.0);

    // Tint / tint-luma ratios depend only on uniforms — driver hoists these
    // divisions to per-draw constants. Scaling by source luma `l` then matches
    // the tinted color's brightness to the pixel, preserving perceived value.
    vec3 shadowRatio    = uShadowTint    / max(dot(uShadowTint,    CG_LUMA), 1e-4);
    vec3 midtoneRatio   = uMidtoneTint   / max(dot(uMidtoneTint,   CG_LUMA), 1e-4);
    vec3 highlightRatio = uHighlightTint / max(dot(uHighlightTint, CG_LUMA), 1e-4);

    vec3 result = col;
    result = mix(result, shadowRatio    * l, lo * uToneAmount);
    result = mix(result, midtoneRatio   * l, md * uMidtoneAmount);
    result = mix(result, highlightRatio * l, hi * uToneAmount);
    return result;
}


// =============================================================================
// Step 6.5 — Black / white point  (DISPLAY space)
// =============================================================================
//
// Remap the tonal range to new endpoints. Any value at or below `uBlackPoint`
// clips to 0; any value at or above `uWhitePoint` clips to 1; everything
// between is linearly stretched. Use it to deepen blacks or bloom highlights
// without resorting to contrast, which moves midtones too.

uniform float uBlackPoint;   // [0, 0.5] default 0 — crush threshold.
uniform float uWhitePoint;   // [0.5, 1] default 1 — clip threshold.

vec3 applyBlackWhitePoint(vec3 col)
{
    // Fast path: both endpoints at identity.
    if (uBlackPoint <= 1e-4 && uWhitePoint >= 0.9999)
        return col;

    float range = max(uWhitePoint - uBlackPoint, 1e-4);
    return (col - vec3(uBlackPoint)) / range;
}


// =============================================================================
// Step 7 — Brightness + contrast  (DISPLAY space)
// =============================================================================
//
// Classic linear brightness offset followed by contrast around midgray. Kept
// as one function because they are always applied together and share no
// expensive work.

uniform float uBrightness;   // [-0.5, 0.5] default 0 — additive offset.
uniform float uContrast;     // [0, 2] default 1 — scale around 0.5. Less than
                             //   1 flattens, greater than 1 punches.

vec3 applyBrightnessContrast(vec3 col)
{
    // Fast path: both sliders at identity.
    if (abs(uBrightness) <= 1e-4 && abs(uContrast - 1.0) <= 1e-4)
        return col;

    // Folded `(col + B - 0.5) * C + 0.5` into one FMA per channel.
    return col * uContrast + ((uBrightness - 0.5) * uContrast + 0.5);
}


// =============================================================================
// Step 7.5 — Highlight / shadow recovery  (DISPLAY space)
// =============================================================================
//
// Compresses the top and bottom of the tonal range without disturbing the
// midtones. Implemented as a luma-masked pull toward the opposite endpoint,
// scaled by 0.3 so the slider range matches Lightroom's [-1, 1] feel.
// Positive values boost; negative values recover — the convention artists
// already know.

uniform float uHighlights;   // [-1, 1] default 0 — highlight recovery / boost.
uniform float uShadows;      // [-1, 1] default 0 — shadow lift / crush.

vec3 applyShadowHighlightRecovery(vec3 col)
{
    // Fast path: both sliders at identity.
    if (abs(uHighlights) <= 1e-4 && abs(uShadows) <= 1e-4)
        return col;

    float l     = dot(col, CG_LUMA);
    float hMask = smoothstep(0.5, 1.0, l);
    float sMask = 1.0 - smoothstep(0.0, 0.5, l);

    // `(col - 1) * -H` collapses to `(1 - col) * H`; shadow lift is `col * S`.
    col += (vec3(1.0) - col) * (uHighlights * hMask * 0.3);
    col += col                * (uShadows    * sMask * 0.3);
    return col;
}


// =============================================================================
// Step 8 — Saturation  (DISPLAY space)
// =============================================================================
//
// Uniform saturation scale around the luma axis. Applied in display space
// because the Rec.709 luma weights are defined for gamma-encoded values.

uniform float uSaturation;   // [0, 2] default 1 — 0 is B&W, 1 is identity.

vec3 applySaturation(vec3 col)
{
    // Fast path: slider at identity.
    if (abs(uSaturation - 1.0) <= 1e-4)
        return col;

    float luma = dot(col, CG_LUMA);
    return mix(vec3(luma), col, uSaturation);
}


// =============================================================================
// Step 9 — Vibrance  (DISPLAY space)
// =============================================================================
//
// "Smart saturation" — extrapolates away from the luma axis by an amount
// that fades as the pixel's existing saturation approaches 1. Pushes muted
// pixels harder than already-saturated pixels, which keeps skin tones from
// going radioactive when global saturation would. Clamped after.

uniform float uVibrance;     // [-1, 1] default 0 — 0.3 is a subtle lift.

vec3 applyVibrance(vec3 col)
{
    // Fast path: slider at identity.
    if (abs(uVibrance) <= 1e-4)
        return col;

    float luma = dot(col, CG_LUMA);
    float mx   = max(col.r, max(col.g, col.b));
    float mn   = min(col.r, min(col.g, col.b));
    float sat  = mx - mn;
    col = mix(vec3(luma), col, 1.0 + uVibrance * (1.0 - sat));
    return clamp(col, 0.0, 1.0);
}


// =============================================================================
// Step 10 — Hue shift  (DISPLAY space)
// =============================================================================
//
// Rotates every pixel around the HSV hue wheel by a signed degree amount.
// Uses the branchless Hocevar conversions from the shared helpers; hue is
// stored as a fraction so the shift is a plain `fract(h + deg/360)`.

uniform float uHueShift;     // [-180, 180] default 0 — degrees.

vec3 applyHueShift(vec3 col)
{
    // Fast path: slider at identity.
    if (abs(uHueShift) <= 1e-3)
        return col;

    vec3 hsv = cg_rgb2hsv(col);
    hsv.x    = fract(hsv.x + uHueShift / 360.0);
    return cg_hsv2rgb(hsv);
}


// =============================================================================
// Step 11 — LUT grading  (DISPLAY space)
// =============================================================================
//
// Samples a 3D LUT built from an artist-authored image. `uColorGradeLutSize`
// carries metadata alongside the edge length so a single uniform covers both
// the scale/offset math and layout quirks of LUTs exported from tools that
// disagree about axis orientation.
//
// See https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
// for the scale/offset derivation.

uniform sampler3D uColorGradeLut;
uniform vec4      uColorGradeLutSize;     // x: LUT edge length (e.g. 16, 32),
                                          // y: >0.5 inverts the green axis for
                                          //    DX-style LUTs,
                                          // z: >0.5 swaps blue / green.
uniform float     uColorGradeStrength;    // [0, 1] default 1 — blend the LUT
                                          //   result against the ungraded input.

vec3 applyLUTGrading(vec3 col)
{
    // Fast path: LUT fully ungraded.
    if (uColorGradeStrength <= 0.0)
        return col;

    vec3 original = col;

    // DX-style LUTs invert green; some authoring tools swap B/G.
    col.g   = uColorGradeLutSize.y > 0.5 ? 1.0 - col.g : col.g;
    col.rgb = uColorGradeLutSize.z > 0.5 ? col.rbg     : col.rgb;

    // Half-texel-inset sampling so the LUT's first/last slice aren't
    // bilerped with phantom neighbors at the boundary. Scale/offset depend
    // only on the uniform edge length; driver hoists them to per-draw
    // constants.
    float invN   = 1.0 / uColorGradeLutSize.x;
    float scale  = 1.0 - invN;
    float offset = 0.5 * invN;
    col = textureLod(uColorGradeLut, col.rgb * scale + offset, 0).rgb;

    return mix(original, col, uColorGradeStrength);
}


// =============================================================================
// Step 12 — Per-channel filmic curves  (DISPLAY space)
// =============================================================================
//
// Three independent filmic S-curves, one per channel. Useful after the LUT
// as a "tune" stage — e.g. pinch the red shoulder separately from green /
// blue to desaturate clipped highlights, or bias one channel's toe to
// produce cross-processed shadows.

uniform vec3 uCurveToe;        // [0, 1] default 0 — per-channel curve toe.
uniform vec3 uCurveShoulder;   // [0, 1] default 1 — per-channel curve shoulder.
uniform vec3 uCurveStrength;   // [0, 1] default 0 — per-channel blend into curve.

vec3 applyChannelCurves(vec3 col)
{
    // Fast path: every channel at zero strength.
    if (dot(uCurveStrength, vec3(1.0)) <= 0.0)
        return col;

    return vec3(
        cg_sCurve(col.r, uCurveToe.r, uCurveShoulder.r, uCurveStrength.r),
        cg_sCurve(col.g, uCurveToe.g, uCurveShoulder.g, uCurveStrength.g),
        cg_sCurve(col.b, uCurveToe.b, uCurveShoulder.b, uCurveStrength.b));
}
