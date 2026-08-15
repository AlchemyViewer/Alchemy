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

// Shared per-frame sky/water constants, spliced from class1/deferred/environmentBlock.glsl
// and bound at UB_ENVIRONMENT. Members are read by bare name. Declared unconditionally --
// a block whose presence varied by define could not unify across co-attached units; only
// the gamma USE below stays gated on LEGACY_GAMMA.
//[ENGINE_BLOCK Environment]

#ifdef BLOOM_COMPOSITE
// Pre-tonemap bloom pyramid (top mip). Read once here so the blend no longer
// needs a standalone pass over the scene buffer. With BLOOM_HALATION the
// halation signal rides in the alpha channel; otherwise the pyramid is RGB-only.
uniform sampler2D bloomMap;
uniform float bloom_strength;
#ifdef BLOOM_HALATION
uniform float halation_strength;
uniform vec3  halation_tint;
#endif
#endif

// =============================================================================
// Forward Declarations
// =============================================================================

vec3 linear_to_srgb(vec3 cl);
vec3 clampHDRRange(vec3 color);

#ifdef TONEMAP
vec3 applyExposure(vec3 color);
vec3 applyToneMap(vec3 color);
#endif

#ifdef COLOR_GRADE
vec3 applyWhiteBalance(vec3 diff);
vec3 applyLiftGammaGain(vec3 diff);
vec3 applyLUTGrading(vec3 diff);
vec3 applySplitToning(vec3 diff);
vec3 applyBlackWhitePoint(vec3 diff);
vec3 applyBrightnessContrast(vec3 diff);
vec3 applyShadowHighlightRecovery(vec3 diff);
vec3 applySaturation(vec3 diff);
vec3 applyVibrance(vec3 diff);
vec3 applyHueShift(vec3 diff);
vec3 applyChannelCurves(vec3 diff);
#endif

#ifdef HAS_POST_EFFECTS
vec3 computeLensFlare(sampler2D diffuse, sampler2D depth, vec2 uv);
vec4 applyChromaticAberration(sampler2D tex, vec2 uv);
#endif

#ifdef DITHER
vec3 applyDither(vec3 color, vec2 fragCoord);
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

#ifdef HAS_POST_EFFECTS
    vec4 diff = applyChromaticAberration(diffuseRect, vary_fragcoord);
    diff.rgb += computeLensFlare(diffuseRect, depthMap, vary_fragcoord);
#else
    vec4 diff = texture(diffuseRect, vary_fragcoord);
#endif

#ifdef BLOOM_COMPOSITE
    // Additively composite the HDR bloom pyramid before exposure/tonemap so the
    // glow is rolled through the same exposure curve as the underlying scene.
    {
        vec4 bloom_sample = texture(bloomMap, vary_fragcoord);
    #ifdef BLOOM_HALATION
        diff.rgb += bloom_sample.rgb * bloom_strength
                  + bloom_sample.a   * halation_strength * halation_tint;
    #else
        diff.rgb += bloom_sample.rgb * bloom_strength;
    #endif
    }
#endif

#ifdef TONEMAP
    diff.rgb = applyExposure(diff.rgb);
#endif

#ifdef COLOR_GRADE
    // White balance and lift/gamma/gain run in linear light, between exposure
    // and tonemap, so chromatic adaptation and the three-way grade act on
    // physically meaningful HDR values rather than rolled-off display ones.
    diff.rgb = applyWhiteBalance(diff.rgb);
    diff.rgb = applyLiftGammaGain(diff.rgb);
#endif

#ifdef TONEMAP
    diff.rgb = applyToneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
#endif

#ifdef COLOR_GRADE
    // Split toning after tonemap so tints apply to rolled-off values.
    diff.rgb = applySplitToning(diff.rgb);
#endif

    diff.rgb = linear_to_srgb(diff.rgb);

    // === DISPLAY SPACE =======================================================

#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif

#ifdef COLOR_GRADE
    // 6.5 black/white point → 7 brightness+contrast → 7.5 hi/shadow recovery
    // → 8 saturation → 9 vibrance → 10 hue shift → 11 LUT → 12 curves.
    diff.rgb = applyBlackWhitePoint(diff.rgb);
    diff.rgb = applyBrightnessContrast(diff.rgb);
    diff.rgb = applyShadowHighlightRecovery(diff.rgb);
    diff.rgb = applySaturation(diff.rgb);
    diff.rgb = applyVibrance(diff.rgb);
    diff.rgb = applyHueShift(diff.rgb);
    diff.rgb = applyLUTGrading(diff.rgb);
    diff.rgb = applyChannelCurves(diff.rgb);
#endif

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0)); // We should always be 0-1 past this point

#ifdef DITHER
    // Post chain is 8-bit — dither before the first quantization rather than
    // waiting for the final blit.
    diff.rgb = applyDither(diff.rgb, gl_FragCoord.xy);
#endif

    //debugExposure(diff.rgb);
    frag_color = diff;
}

