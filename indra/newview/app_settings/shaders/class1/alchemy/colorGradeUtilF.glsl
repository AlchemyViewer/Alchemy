/**
 * @file colorGradeUtilF.glsl
 * @brief Colour-grading helpers attached to every program whose
 *        LLGLSLShader sets `mFeatures.hasColorGrade = true`
 *        (see llshadermgr.cpp:attachShaderFeatures).
 *
 * Consumer:
 *   - colorCorrectF.glsl — runs the full grading chain between exposure and
 *                          the final dither/blit.
 *
 * Public entry points, in pipeline order:
 *
 *   LINEAR SPACE (pre-tonemap)
 *     vec3 applyWhiteBalance         (vec3 col)   // step 2
 *     vec3 applyLiftGammaGain        (vec3 col)   // step 3
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
 * Optimisation notes:
 *   - Every uniform here is fed a PRECOMPUTED value from pipeline.cpp. The
 *     per-pixel work reduces to one FMA (or one FMA + one `pow`) per helper
 *     when effects are active — no per-pixel divides, max-guards, or ratio
 *     derivations. Identity defaults land exactly on the fast-path compare.
 *   - Fast-path identity checks still live shader-side because this file is
 *     attached to multiple programs with different defaults; doing them on
 *     the CPU would require shader-variant plumbing we don't have.
 *   - Shared helpers (`CG_LUMA`, `cg_rgb2hsv`/`cg_hsv2rgb`, `cg_sCurve`) are
 *     prefixed `cg_`/`CG_` to avoid colliding with `LUMA` in
 *     postEffectUtilsF, which can be linked into the same program.
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

// Rec.709 luma weights.
const vec3 CG_LUMA = vec3(0.2126, 0.7152, 0.0722);

// HSV conversions (Sam Hocevar branchless form). Display-space values in [0,1].
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


// =============================================================================
// Step 2 — White balance  (LINEAR space, pre-tonemap)
// =============================================================================
//
// Von Kries tint. The CPU resolves (CCT offset, Duv) into a linear-sRGB gain
// using the Kim et al. 2002 Planckian-locus polynomial, normalises the result
// so green pins to 1 (preserves luminance), and uploads the vec3. The shader
// is now a single multiply.

uniform vec3 uWhiteBalanceGain;  // default vec3(1) — per-channel linear gain.

vec3 applyWhiteBalance(vec3 col)
{
    if (all(equal(uWhiteBalanceGain, vec3(1.0))))
        return col;
    return col * uWhiteBalanceGain;
}


// =============================================================================
// Step 3 — Lift / Gamma / Gain  (LINEAR space, pre-tonemap)
// =============================================================================
//
// Colorist's three-way. `uInvGammaCC` is `1.0 / max(gamma, 1e-4)` from the
// CPU, so the per-pixel math is one FMA plus one `pow`.

uniform vec3 uLift;          // default vec3(0) — additive shadow offset.
uniform vec3 uInvGammaCC;    // default vec3(1) — pre-inverted midtone power.
uniform vec3 uGain;          // default vec3(1) — highlight multiplier.

vec3 applyLiftGammaGain(vec3 col)
{
    // Fast path: all three at identity. Compact bvec form collapses to a
    // single uniform branch.
    if (all(equal(uLift, vec3(0.0))) &&
        all(equal(uGain, vec3(1.0))) &&
        all(equal(uInvGammaCC, vec3(1.0))))
        return col;

    col = col * uGain + uLift;
    return pow(max(col, 0.0), uInvGammaCC);
}


// =============================================================================
// Step 5 — Split toning  (LINEAR space, post-tonemap)
// =============================================================================
//
// Lightroom-style, luminance-preserving. The CPU computes
// `tint / max(dot(tint, LUMA), 1e-4)` for each of the three tints and uploads
// them as `*Ratio` uniforms, and also precomputes the luma split point
// `uSplitToneMid = 0.5 + balance * 0.4`. The shader just multiplies ratios by
// the pixel's luma and blends through the luma masks.

uniform vec3  uShadowRatio;      // default vec3(1) — shadow tint / dot(tint, LUMA).
uniform vec3  uMidtoneRatio;     // default vec3(1) — midtone  tint / dot(tint, LUMA).
uniform vec3  uHighlightRatio;   // default vec3(1) — highlight tint / dot(tint, LUMA).
uniform float uMidtoneAmount;    // [0, 1] default 0 — midtone strength.
uniform float uSplitToneMid;     // default 0.5 — pre-slid luma split point.
uniform float uToneAmount;       // [0, 1] default 0 — shadow/highlight strength.

vec3 applySplitToning(vec3 col)
{
    // Both amounts. uMidtoneAmount drives its own mix() below, so gating on
    // uToneAmount alone left the midtone tint unreachable unless the user also
    // raised the shadow/highlight amount -- a setting that silently did
    // nothing on its own.
    if (uToneAmount <= 0.0 && uMidtoneAmount <= 0.0)
        return col;

    float l = dot(col, CG_LUMA);

    float hi = smoothstep(uSplitToneMid,        uSplitToneMid + 0.35, l);
    float lo = 1.0 - smoothstep(uSplitToneMid - 0.35, uSplitToneMid,  l);
    float md = max(1.0 - hi - lo, 0.0);

    vec3 result = col;
    result = mix(result, uShadowRatio    * l, lo * uToneAmount);
    result = mix(result, uMidtoneRatio   * l, md * uMidtoneAmount);
    result = mix(result, uHighlightRatio * l, hi * uToneAmount);
    return result;
}


// =============================================================================
// Step 6.5 — Black / white point  (DISPLAY space)
// =============================================================================
//
// Remaps the tonal range. CPU sends `uBWPScale = 1 / (white - black)` and
// `uBWPBias = -black * uBWPScale`; shader is one FMA.

uniform float uBWPScale;     // default 1.0.
uniform float uBWPBias;      // default 0.0.

vec3 applyBlackWhitePoint(vec3 col)
{
    if (uBWPScale == 1.0 && uBWPBias == 0.0)
        return col;
    return col * uBWPScale + uBWPBias;
}


// =============================================================================
// Step 7 — Brightness + contrast  (DISPLAY space)
// =============================================================================
//
// CPU sends `uBCScale = contrast` and `uBCBias = (brightness - 0.5) * contrast + 0.5`
// so the shader is one FMA.

uniform float uBCScale;      // default 1.0.
uniform float uBCBias;       // default 0.0.

vec3 applyBrightnessContrast(vec3 col)
{
    if (uBCScale == 1.0 && uBCBias == 0.0)
        return col;
    return col * uBCScale + uBCBias;
}


// =============================================================================
// Step 7.5 — Highlight / shadow recovery  (DISPLAY space)
// =============================================================================
//
// Luma-masked pull toward the opposite endpoint. CPU pre-multiplies the 0.3
// Lightroom scaling into the sliders so per-pixel work is two masks plus two
// FMAs.

uniform float uHighlightsScaled;   // default 0 — highlight slider × 0.3.
uniform float uShadowsScaled;      // default 0 — shadow   slider × 0.3.

vec3 applyShadowHighlightRecovery(vec3 col)
{
    if (uHighlightsScaled == 0.0 && uShadowsScaled == 0.0)
        return col;

    float l     = dot(col, CG_LUMA);
    float hMask = smoothstep(0.5, 1.0, l);
    float sMask = 1.0 - smoothstep(0.0, 0.5, l);

    col += (vec3(1.0) - col) * (uHighlightsScaled * hMask);
    col +=              col  * (uShadowsScaled    * sMask);
    return col;
}


// =============================================================================
// Step 8 — Saturation  (DISPLAY space)
// =============================================================================

uniform float uSaturation;   // default 1.0 — 0 is B&W.

vec3 applySaturation(vec3 col)
{
    if (uSaturation == 1.0)
        return col;
    float luma = dot(col, CG_LUMA);
    return mix(vec3(luma), col, uSaturation);
}


// =============================================================================
// Step 9 — Vibrance  (DISPLAY space)
// =============================================================================

uniform float uVibrance;     // default 0 — fades as existing saturation rises.

vec3 applyVibrance(vec3 col)
{
    if (uVibrance == 0.0)
        return col;

    float luma = dot(col, CG_LUMA);
    float mx   = max(col.r, max(col.g, col.b));
    float mn   = min(col.r, min(col.g, col.b));
    col = mix(vec3(luma), col, 1.0 + uVibrance * (1.0 - (mx - mn)));
    return clamp(col, 0.0, 1.0);
}


// =============================================================================
// Step 10 — Hue shift  (DISPLAY space)
// =============================================================================
//
// `uHueShiftNorm` carries `degrees / 360` from the CPU.

uniform float uHueShiftNorm; // default 0.

vec3 applyHueShift(vec3 col)
{
    if (uHueShiftNorm == 0.0)
        return col;
    vec3 hsv = cg_rgb2hsv(col);
    hsv.x    = fract(hsv.x + uHueShiftNorm);
    return cg_hsv2rgb(hsv);
}


// =============================================================================
// Step 11 — LUT grading  (DISPLAY space)
// =============================================================================
//
// Samples an artist-authored 3D LUT. `uColorGradeLutSize` packs edge length
// (x), a green-axis inversion flag (y), and a B/G-swap flag (z).

uniform sampler3D uColorGradeLut;
uniform vec4      uColorGradeLutSize;
uniform float     uColorGradeLutStrength;

vec3 applyLUTGrading(vec3 col)
{
    if (uColorGradeLutStrength <= 0.0)
        return col;

    vec3 original = col;

    col.g   = uColorGradeLutSize.y > 0.5 ? 1.0 - col.g : col.g;
    col.rgb = uColorGradeLutSize.z > 0.5 ? col.rbg     : col.rgb;

    float invN   = 1.0 / uColorGradeLutSize.x;
    float scale  = 1.0 - invN;
    float offset = 0.5 * invN;
    col = textureLod(uColorGradeLut, col.rgb * scale + offset, 0).rgb;

    return mix(original, col, uColorGradeLutStrength);
}


// =============================================================================
// Step 12 — Per-channel filmic curves  (DISPLAY space)
// =============================================================================
//
// `uCurveInvRange` holds `1 / max(shoulder - toe, 1e-4)` from the CPU, so the
// per-pixel inner loop is: subtract, multiply, clamp, cubic, mix — no divs.

uniform vec3 uCurveToe;        // default vec3(0).
uniform vec3 uCurveInvRange;   // default vec3(1) — 1 / (shoulder - toe).
uniform vec3 uCurveStrength;   // default vec3(0) — per-channel blend.

float cg_sCurve(float x, float toe, float invRange, float strength)
{
    float t = clamp((x - toe) * invRange, 0.0, 1.0);
    float s = t * t * (3.0 - 2.0 * t);
    return mix(x, s, strength);
}

vec3 applyChannelCurves(vec3 col)
{
    if (all(equal(uCurveStrength, vec3(0.0))))
        return col;

    return vec3(
        cg_sCurve(col.r, uCurveToe.r, uCurveInvRange.r, uCurveStrength.r),
        cg_sCurve(col.g, uCurveToe.g, uCurveInvRange.g, uCurveStrength.g),
        cg_sCurve(col.b, uCurveToe.b, uCurveInvRange.b, uCurveStrength.b));
}
