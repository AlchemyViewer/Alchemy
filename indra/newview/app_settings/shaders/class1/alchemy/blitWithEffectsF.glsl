/**
 * @file blitWithEffectsF.glsl
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

in vec2 vary_fragcoord;
out vec4 frag_color;

// =============================================================================
// Uniforms
// =============================================================================

// ---- Input ------------------------------------------------------------------
uniform sampler2D diffuseRect;                  // Linear Rec.709 / linear-sRGB.
uniform sampler2D depthMap;
uniform uint  uFrameId;                         // Frame counter, wraps at 2^32.
                                                // Set to 0 for deterministic static output.
uniform vec2 uResolution;                       // Framebuffer dimensions
                                                // in pixels. Used for
                                                // resolution-independent
                                                // scaling of grain, dither,
                                                // and vignette aspect.

// ---- Vignette ---------------------------------------------------------------
uniform float uVignetteAmount;                  // [0, 1] default 0
uniform float uVignetteRadius;                  // [0.25, 1.5] default 1.0 —
                                                // measured in image-height
                                                // units. 1.0 reaches top/
                                                // bottom edges on any aspect.
uniform float uVignetteSoft;                    // [0.05, 1] default 0.5.
uniform float uVignetteShape;                   // [0, 1] default 0 —
                                                // 0 circular, 1 rounded square.
uniform vec3  uVignetteColor;                   // Corner color.
uniform vec3  uVignetteMidColor;                // Intermediate ramp color.
uniform float uVignetteMidPoint;                // [0, 1] default 0 —
                                                // 0 disables three-color
                                                // ramp (two-color fallback).
uniform vec2  uVignetteCenter;                  // [-0.5, 0.5] default 0.
uniform vec2  uVignetteAspect;                  // Image (w, h). (1,1) disables.
uniform float uVignetteFeather;                 // [0.2, 4.0] default 1.0 — controls
                                                // the shape of the darkening curve
                                                // from edge to center.
                                                // <1 = spreads darkening inward
                                                //      (gentle haze across frame)
                                                //  1 = linear smoothstep falloff
                                                //      (current behavior)
                                                // >1 = concentrates darkening at edges
                                                //      (sharp corner darkening only)

// ---- CVD compensation -------------------------------------------------------
uniform int   uCompensateMode;                  // [0, 3] default 0
                                                // 0 = off
                                                // 1 = protanopia (no L cones)
                                                // 2 = deuteranopia (no M cones)
                                                // 3 = tritanopia (no S cones)
uniform float uCompensateAmount;                // [0, 1] default 0.

// ---- Film grain -------------------------------------------------------------
uniform float uGrainAmount;                     // [0, 1] default 0
uniform int   uGrainStyle;                      // [0, 3] default 0
                                                // 0=mono luma (classic film)
                                                // 1=color (digital push)
                                                // 2=coarse (16mm feel)
                                                // 3=photon shot (CCD sensor)
uniform float uGrainSize;                       // [1, 8] default 1.
uniform float uGrainRange;                      // [0, 1] default 0.5 — luma position
                                                // where grain peaks. 0 = shadows only,
                                                // 0.5 = midtones (classic film),
                                                // 1 = highlights only.
uniform vec3  uGrainTint;                       // [0, 1] default vec3(1) — color of
                                                // the grain. vec3(1) = neutral gray.
                                                // Try vec3(1.0, 0.9, 0.8) for warm
                                                // film grain, vec3(0.8, 0.9, 1.0) for
                                                // cool video noise.
uniform bool  uGrainAnimate;                    // false freezes grain pattern across
                                                // frames.

// ---- Dithering --------------------------------------------------------------
uniform float uDitherAmount;                    // [0, 1] default 1 — TPDF
                                                // ±1/255. Leave at 1.0.
uniform int   uDitherBits;                      // {8, 10} default 8 — output bit depth.
                                                // 8 for sRGB/standard displays,
                                                // 10 for HDR or wide-gamut.
uniform bool  uDitherAnimate;                   // false freezes dither pattern. Almost
                                                // always want this true — animated
                                                // dither is perceptually invisible,
                                                // static dither can show as fixed grain.

// ---- Preview / debug --------------------------------------------------------
uniform int   uPreviewMode;                     // [0, 7] default 0. MUST be 0
                                                // for final output.
                                                // 0 = off
                                                // 1 = protanopia (no L cones)
                                                // 2 = deuteranopia (no M cones)
                                                // 3 = tritanopia (no S cones)
                                                // 4 = Achromatopsia (rod monochromacy) — total color loss, luminance only
                                                // 5 = Blue cone monochromacy — only S-cones functional, blue-tinted near-gray
                                                // 6 = Achromatomaly — partial achromatopsia, severely reduced saturation
                                                // 7 = false-color exposure map

// ---- Constants ----------------------------------------------------------------

// Rec.709 luma weights.
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// =============================================================================
// Helpers
// =============================================================================

// Cheap hash → [0,1) for grain.
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Grain sample — returns unscaled noise in ~[-0.5, 0.5]. Caller applies
// amplitude and any luma weighting.
vec3 grainSample(vec2 fragCoord, int style, float size) {
    vec2 gp = floor(fragCoord / max(size, 1.0)) * max(size, 1.0);
    if (style == 1) {
        return vec3(hash12(gp),
                    hash12(gp + 17.0),
                    hash12(gp + 31.0)) - 0.5;
    }
    return vec3(hash12(gp) - 0.5);
}

// Interleaved Gradient Noise (Jimenez, COD:AW 2014).
// The frame offset is hashed to produce a per-frame jump rather than a
// linear translation, which prevents the noise from visibly scrolling
// across the screen when uFrameId increments smoothly.
float interleavedGradientNoise(vec2 p, uint frameId) {
    float fx = hash12(vec2(float(frameId), 13.7));
    float fy = hash12(vec2(float(frameId), 91.3));
    p += vec2(fx, fy) * 1024.0;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// CVD simulation. Modes 1-3 use Machado et al. 2009 matrices for the
// dichromat (full severity) case — physiologically-based, slightly more
// accurate than Brettel/Viénot, especially for tritanopia. Modes 4-5 are
// total and partial achromatopsia, implemented as desaturation toward
// luminance-weighted gray.
vec3 simulateCVD(vec3 c, int mode) {
    if (mode == 4) {
        // Achromatopsia (rod monochromacy). True rod response peaks at
        // ~507nm and gives a slightly cyan-green tint to neutral gray, but
        // the standard simulation uses Rec.709 luma as a reasonable proxy.
        return vec3(dot(c, LUMA));
    }
    if (mode == 5) {
        // Blue cone monochromacy. Only S-cones work, so the image collapses
        // to a blue-yellow axis. Approximated by extracting the blue-yellow
        // component (B - 0.5*(R+G)) and recombining as a desaturated blue tint.
        // This isn't physiologically exact but matches what most tools render
        // and reads correctly to a normal viewer as "what a BCM viewer sees."
        float l = dot(c, LUMA);
        float by = c.b - 0.5 * (c.r + c.g);  // blue-yellow opponent signal
        // Build desaturated output: luminance from luma, slight color from
        // the blue-yellow axis, biased blue.
        return vec3(l - by * 0.15, l - by * 0.05, l + by * 0.25);
    }
    if (mode == 6) {
        // Achromatomaly — partial color loss. Strong desaturation toward
        // luminance-weighted gray (80% blend), retaining a hint of color.
        return mix(c, vec3(dot(c, LUMA)), 0.8);
    }

    mat3 m;
    if (mode == 1) {
        // Protanopia (Machado 2009, severity 1.0)
        m = mat3(0.152286, 1.052583, -0.204868,
                 0.114503, 0.786281,  0.099216,
                -0.003882,-0.048116,  1.051998);
    } else if (mode == 2) {
        // Deuteranopia (Machado 2009, severity 1.0)
        m = mat3(0.367322, 0.860646, -0.227968,
                 0.280085, 0.672501,  0.047413,
                -0.011820, 0.042940,  0.968881);
    } else {
        // Tritanopia (Machado 2009, severity 1.0)
        m = mat3(1.255528,-0.078441, -0.004733,
                -0.076749, 0.930809,  0.691367,
                 0.178779,-0.147632,  0.303900);
    }
    return m * c;
}

// Daltonization: redistribute lost information into perceivable channels.
vec3 daltonize(vec3 c, int mode, float amount) {
    if (mode >= 4) return c; // monochromacies — no compensation possible

    vec3 sim = simulateCVD(c, mode);
    vec3 err = c - sim;
    vec3 shift;
    if (mode == 3) {
        float be = err.b;
        shift = vec3( be*0.7 + err.g*0.3, -be*0.7 - err.g*0.3, 0.0);
    } else {
        shift = vec3(0.0, err.r*0.7 + err.g, err.r*0.7);
    }
    return c + shift * amount;
}

// False-color exposure map (ARRI-style palette).
vec3 falseColor(vec3 c) {
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

// =============================================================================
// Forward Declarations
// =============================================================================

vec3 clampHDRRange(vec3 color);

// =============================================================================
// Main
// =============================================================================
void main()
{
    // === DISPLAY SPACE =======================================================

    vec4 diff = texture(diffuseRect, vary_fragcoord.xy);

    // Vignette — frame effect with aspect correction, offset, shape
    // morphing between circular and rounded square, and optional
    // three-color ramp (image → mid → edge).
    //
    // Normalization: distances are measured in units of half-image-height
    // on the shorter axis, so uVignetteRadius = 1.0 reaches the top and
    // bottom edges of the frame regardless of aspect ratio. Values above
    // 1.0 extend the falloff out past the corners (useful on widescreen
    // where you want a gentler vignette that only affects extreme edges).
    if (uVignetteAmount > 0.0) {
        // Center offset and aspect correction
        vec2 d = vary_fragcoord.xy - 0.5 - uVignetteCenter;
        float aspect = max(uVignetteAspect.x, 1e-4) / max(uVignetteAspect.y, 1e-4);
        if (aspect > 1.0) d.x *= aspect;        // widescreen: stretch x
        else              d.y /= max(aspect, 1e-4);  // tall: stretch y

        // Distance in shorter-axis units (0..1 reaches the image edges)
        float circular = length(d) * 2.0;
        float square   = pow(max(abs(d.x), abs(d.y)) * 2.0, 1.5);
        float dist = mix(circular, square, uVignetteShape);

        // Falloff boundaries. `start` is where the image begins fading;
        // `end` is where it fully reaches the edge color.
        float start = uVignetteRadius - uVignetteSoft;
        float end   = uVignetteRadius;

        // Two-color fallback when midpoint is at a default. Three-color ramp
        // only activates when the user deliberately places the midpoint
        // somewhere in the open interval (0, 1).
        if (uVignetteMidPoint > 1e-4 && uVignetteMidPoint < 0.9999) {
            float mid = mix(start, end, uVignetteMidPoint);
            float tMidRaw  = smoothstep(start, mid, dist);
            float tEdgeRaw = smoothstep(mid,   end, dist);
            // Reshape the mask via pow before scaling by amount
            float tMid  = pow(tMidRaw,  uVignetteFeather) * uVignetteAmount;
            float tEdge = pow(tEdgeRaw, uVignetteFeather) * uVignetteAmount;
            diff.rgb = mix(diff.rgb, uVignetteMidColor, tMid);
            diff.rgb = mix(diff.rgb, uVignetteColor,    tEdge);
        } else {
            float tRaw = smoothstep(start, end, dist);
            float t = pow(tRaw, uVignetteFeather) * uVignetteAmount;
            diff.rgb = mix(diff.rgb, uVignetteColor, t);
        }
    }

    // CVD compensation (accessibility).
    if (uCompensateMode > 0 && uCompensateAmount > 0.0) {
        diff.rgb = daltonize(diff.rgb, uCompensateMode, uCompensateAmount);
        diff.rgb = clamp(diff.rgb, 0.0, 1.0);
    }

    // Film grain — squared internally to bias the slider toward subtle
    // values. Max internal amplitude is 0.1, which is where grain stops
    // reading as "film texture" and starts reading as "broken image."
    if (uGrainAmount > 0.0) {
        float amp = uGrainAmount * uGrainAmount * 0.1;
        float gluma = dot(diff.rgb, LUMA);
        uint grainFrame = uGrainAnimate ? uFrameId : 0u;
        // Hash the frame id to get an unpredictable per-frame offset.
        // Two different hash inputs give independent x and y offsets so
        // the grain pattern fully reshuffles each frame instead of scrolling.
        float fx = hash12(vec2(float(grainFrame), 13.7));
        float fy = hash12(vec2(float(grainFrame), 91.3));
        vec2 timeOffset = vec2(fx, fy) * 1024.0;
        // Scale grain size proportionally to resolution so grain particles
        // stay perceptually constant regardless of display density.
        float pixelScale = max(uResolution.x, uResolution.y) / 1080.0;
        float effectiveSize = uGrainSize * pixelScale;
        vec3 n = grainSample(gl_FragCoord.xy + timeOffset, uGrainStyle, effectiveSize);

        if (uGrainStyle == 3) {
            diff.rgb += n * uGrainTint * sqrt(max(gluma, 0.0)) * amp;
        } else {
            float d = gluma - uGrainRange;
            float weight = max(1.0 - (d * d) * 4.0, 0.0);
            diff.rgb += n * uGrainTint * weight * amp;
        }
    }

    // Dithering. IGN with per-frame offset for temporal decorrelation.
    // TPDF shape from two offset samples. Amplitude scales to output
    // bit depth.
    if (uDitherAmount > 0.0) {
        uint ditherFrame = uDitherAnimate ? uFrameId : 0u;
        float n1 = interleavedGradientNoise(gl_FragCoord.xy, ditherFrame);
        float n2 = interleavedGradientNoise(gl_FragCoord.xy + vec2(1.618, 2.414), ditherFrame);
        float tpdf = (n1 + n2 - 1.0);

        float levels = (uDitherBits >= 10) ? 1023.0 : 255.0;
        diff.rgb += tpdf * (uDitherAmount / levels);
    }

    // Preview overlays — debug only. After dither so bands stay crisp.
    if (uPreviewMode == 7) {
        diff.rgb = falseColor(diff.rgb);
    } else if (uPreviewMode > 0) {
        diff.rgb = simulateCVD(clamp(diff.rgb, 0.0, 1.0), uPreviewMode);
    }

    diff.rgb = clampHDRRange(diff.rgb);
    frag_color = diff;

    gl_FragDepth = texture(depthMap, vary_fragcoord.xy).r;
}
