/**
 * @file colorCorrectF.glsl
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
uniform sampler2D diffuseRect;
uniform sampler2D depthMap;
uniform vec2 uResolution;
#ifdef LEGACY_GAMMA
uniform float gamma;
#endif

// ---- Chromatic aberration ---------------------------------------------------
uniform float uCAAmount            = 0.0;       // [0, 1] default 0 — squared
                                                // internally for ergonomic
                                                // curve. 0.15 SLR, 0.35 vintage
                                                // lens, 0.6 anamorphic, 0.9 VHS.
uniform float uCAFalloff            = 1.0;      // [0.5, 4] default 1 — how much of the
                                                // frame is affected by CA.
                                                // 0.5 = corners only (extreme tight)
                                                // 1.0 = realistic SLR falloff (default)
                                                // 2.0 = noticeable across most of the frame
                                                // 4.0 = nearly uniform across the entire frame
uniform float uCAAngle             = 0.0;       // [0, 360] default 0 — degrees. Rotates the
                                                // CA fringe axis. 0 = standard radial,
                                                // 90 = perpendicular (tangential fringing),
                                                // 45 = diagonal lens-decentering look.
uniform vec2  uCAOffsetR           = vec2(-1.0, 0.0);  // R channel offset direction
uniform vec2  uCAOffsetB           = vec2( 1.0, 0.0);  // B channel offset direction
                                                // Defaults give standard R-in/B-out radial.
                                                // For glitch: vec2(1,0)/vec2(0,1) = R→B swap.
uniform float uCAAnisotropy        = 0.0;       // [-1, 1] default 0 — axis stretch.
                                                // -1 horizontal only, +1 vertical only.

// =============================================================================
// Forward Declarations
// =============================================================================

vec3 linear_to_srgb(vec3 cl);
vec3 clampHDRRange(vec3 color);

#ifdef TONEMAP
vec3 toneMap(vec3 color);
#endif

#ifdef COLOR_GRADE
vec3 applyLUTGrading(vec3 diff);
#endif

#ifdef HAS_POST_EFFECTS
vec3 computeLensFlare(sampler2D diffuse, sampler2D depth, vec2 uv);
#endif

// =============================================================================
// Helpers
// =============================================================================

#ifdef LEGACY_GAMMA
vec3 legacyGamma(vec3 color) {
    vec3 c = 1. - clamp(color, vec3(0.), vec3(1.));
    c = 1. - pow(c, vec3(gamma)); // s/b inverted already CPU-side

    return c;
}
#endif

// =============================================================================
// Main
// =============================================================================
void main()
{
    // === LINEAR SPACE ========================================================

    // Chromatic aberration with angle, per-channel direction, anisotropy.
    vec4 diff;
    if (uCAAmount > 0.0) {
        vec2 dir = vary_fragcoord - 0.5;
        // Anisotropy squashes one axis: negative = horizontal bias, positive = vertical
        dir.x *= 1.0 - max(uCAAnisotropy, 0.0);
        dir.y *= 1.0 + min(uCAAnisotropy, 0.0);

        // Aspect-correct the distance calculation. Scale the longer axis
        // so the half-diagonal correctly represents the frame's actual
        // corner distance, not a square assumption.
        float aspect = uResolution.x / max(uResolution.y, 1.0);
        vec2 aspectDir = dir;
        if (aspect > 1.0) aspectDir.x *= aspect;
        else              aspectDir.y /= max(aspect, 1e-4);

        // Half-diagonal of the aspect-corrected UV space.
        vec2 cornerHalf = vec2(0.5, 0.5);
        if (aspect > 1.0) cornerHalf.x *= aspect;
        else              cornerHalf.y /= max(aspect, 1e-4);
        float maxDist = length(cornerHalf);

        // Normalize distance to [0, 1] over the half-diagonal of UV space,
        // then raise to the inverse of falloff so higher values produce
        // MORE CA across more of the frame (intuitive slider behavior).
        // Low falloff = corners only; high falloff = bleeds into the whole frame.
        float normDist = clamp(length(aspectDir) / maxDist, 0.0, 1.0);
        float dist = pow(normDist, 1.0 / max(uCAFalloff, 0.1));
        // Optional rotation of the base radial direction
        float a = uCAAngle * 0.01745329;  // degrees → radians (π / 180)
        float ca = cos(a), sa = sin(a);
        vec2 rotDir = vec2(ca * dir.x - sa * dir.y, sa * dir.x + ca * dir.y);
        float strength = (uCAAmount * uCAAmount) * 0.02 * dist;
        vec2 perp = vec2(-rotDir.y, rotDir.x);
        vec2 offR = rotDir * uCAOffsetR.x + perp * uCAOffsetR.y;
        vec2 offB = rotDir * uCAOffsetB.x + perp * uCAOffsetB.y;
        diff.r = texture(diffuseRect, vary_fragcoord + offR * strength).r;
        diff.ga = texture(diffuseRect, vary_fragcoord).ga;
        diff.b = texture(diffuseRect, vary_fragcoord + offB * strength).b;
    } else {
        diff = texture(diffuseRect, vary_fragcoord);
    }

#ifdef HAS_POST_EFFECTS
    diff.rgb += computeLensFlare(diffuseRect, depthMap, vary_fragcoord);
#endif

#ifdef TONEMAP
    diff.rgb = toneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
#endif

    diff.rgb = linear_to_srgb(diff.rgb);

    // === DISPLAY SPACE =======================================================

#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif

#ifdef COLOR_GRADE
    diff.rgb = applyLUTGrading(diff.rgb);
#endif

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0)); // We should always be 0-1 past this point

    //debugExposure(diff.rgb);
    frag_color = diff;
}

