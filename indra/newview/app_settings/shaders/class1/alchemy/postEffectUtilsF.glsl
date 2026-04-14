/**
 * @file postEffectUtilsF.glsl
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
// Lens Flare — anamorphic streak with optional ghosts & halo
// =============================================================================

uniform float uLensFlareStrength        = 0.0;
uniform vec2  uLensFlareSunPos          = vec2(0.5);
uniform float uLensFlareSunVisibility   = 0.0;

// Anamorphic streak
uniform float uLensFlareStreakLength    = 0.5;   // horizontal extent in UV space
uniform float uLensFlareStreakFalloff   = 1.5;   // exponential falloff along streak
uniform float uLensFlareStreakWidth     = 0.004;  // vertical half-thickness
uniform float uLensFlareStreakIntensity = 1.0;    // streak brightness multiplier
uniform vec3  uLensFlareStreakTint      = vec3(0.6, 0.7, 1.0); // anamorphic blue
uniform float uLensFlareChromaticSpread = 0.008;  // vertical chromatic offset

// Central glow (uLensFlareGlow acts as both on/off and intensity)
uniform float uLensFlareGlow           = 1.0;
uniform float uLensFlareGlowRadius     = 0.12;
uniform float uLensFlareGlowFalloff    = 8.0;

// Optional ghosts & halo (uLensFlareGhost / uLensFlareHalo are on/off + intensity)
uniform float uLensFlareGhost          = 0.0;
uniform int   uLensFlareGhostCount     = 4;
uniform float uLensFlareGhostSpacing   = 0.3;
uniform float uLensFlareHalo           = 0.0;
uniform float uLensFlareHaloRadius     = 0.5;
uniform float uLensFlareHaloWidth      = 0.15;
uniform float uLensFlareStarburst         = 0.0;
uniform int   uLensFlareStarburstSpikes   = 4;     // primary angular frequency (produces 2x spikes)
uniform float uLensFlareStarburstSharpness = 48.0; // pow() exponent — higher = tighter spikes
uniform float uLensFlareStarburstFalloff  = 30.0;  // radial decay rate from sun
uniform float uLensFlareOcclusionRadius = 0.02;
uniform int   uLensFlareOcclusionTaps  = 9;

uniform vec3  uLensFlareLightColor     = vec3(1.0);

// Shared with colorCorrectF.glsl
uniform vec2 uResolution;

// Provided by LLSettingsSky::applyToShader via mShaderList
uniform float sky_hdr_scale;

vec3 computeLensFlare(sampler2D diffuse, sampler2D depth, vec2 uv)
{
    float vis = uLensFlareSunVisibility * uLensFlareStrength;
    if (vis <= 0.0)
        return vec3(0.0);

    vec2 sun_uv = uLensFlareSunPos;

    // Depth-based occlusion — multi-tap Poisson disk for smooth partial occlusion.
    // CPU-side visibility only tracks on-screen fade, not geometry occlusion.
    if (all(greaterThanEqual(sun_uv, vec2(0.0))) && all(lessThanEqual(sun_uv, vec2(1.0))))
    {
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
        int num_taps = clamp(uLensFlareOcclusionTaps, 1, 32);
        float occluded = 0.0;
        for (int i = 0; i < num_taps; i++)
        {
            vec2 tap_uv = sun_uv + taps[i] * uLensFlareOcclusionRadius;
            float d = texture(depth, tap_uv).r;
            occluded += smoothstep(0.9999, 1.0, d);
        }
        vis *= occluded / float(num_taps);
    }
    if (vis <= 0.0)
        return vec3(0.0);

    // Sample sun brightness once
    vec3 sun_color = texture(diffuse, clamp(sun_uv, vec2(0.0), vec2(1.0))).rgb;
    float sun_lum  = dot(sun_color, vec3(0.2126, 0.7152, 0.0722));
    float sun_bright = max(sun_lum - 2.0, 0.0) / max(sun_lum, 1e-4);
    sun_color *= sun_bright;

    if (sun_bright <= 0.0)
        return vec3(0.0);

    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 delta = uv - sun_uv;

    vec3 flare = vec3(0.0);

    // ---- Central glow: soft radial falloff ----
    // Shares sky_hdr_scale with the starburst so both read at the same brightness
    // against the atmosphere and carry the same sun color.
    if (uLensFlareGlow > 0.0)
    {
        vec2 gd = delta;
        gd.x *= aspect;
        float r = length(gd) / max(uLensFlareGlowRadius, 1e-4);
        float glow = exp(-r * r * uLensFlareGlowFalloff);
        flare += sun_color * glow * uLensFlareGlow * sky_hdr_scale;
    }

    // ---- Anamorphic streak: horizontal with tight vertical gaussian ----
    {
        float horiz = abs(delta.x) * aspect;
        float vert  = abs(delta.y);

        float streak_w = max(uLensFlareStreakWidth, 1e-5);
        float streak_l = max(uLensFlareStreakLength, 1e-5);

        float vert_falloff  = exp(-vert * vert / (streak_w * streak_w));
        float horiz_falloff = exp(-horiz * uLensFlareStreakFalloff / streak_l);
        float streak = vert_falloff * horiz_falloff;

        // Chromatic fringing — offset R and B vertically for anamorphic look.
        // The fringe channels use a widened gaussian so they remain visible
        // even when the spread exceeds the base streak width.
        if (uLensFlareChromaticSpread > 0.0)
        {
            float spread = uLensFlareChromaticSpread;
            float fringe_w = streak_w + spread * 0.5;
            float vert_r = abs(delta.y + spread);
            float vert_b = abs(delta.y - spread);
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

    // ---- Optional ghosts: soft disks along sun-to-center axis ----
    if (uLensFlareGhost > 0.0 && uLensFlareGhostCount > 0)
    {
        vec2 ghost_vec = vec2(0.5) - sun_uv;
        for (int i = 0; i < uLensFlareGhostCount; i++)
        {
            float t        = uLensFlareGhostSpacing * float(i + 1);
            vec2  ghost_uv = sun_uv + ghost_vec * t;
            float scale    = 1.0 / float(i + 1);
            float radius   = 0.04 * scale + 0.02;

            vec2 gd = uv - ghost_uv;
            gd.x *= aspect;
            float d = length(gd);
            float disk = 1.0 - smoothstep(radius * 0.5, radius, d);

            flare += sun_color * disk * scale * uLensFlareGhost;
        }
    }

    // ---- Optional halo: ring opposite the sun ----
    if (uLensFlareHalo > 0.0 && uLensFlareHaloRadius > 0.0)
    {
        vec2  halo_center = vec2(0.5) + (vec2(0.5) - sun_uv);
        float halo_dist   = length(uv - halo_center);
        float halo_w      = max(uLensFlareHaloWidth, 0.01);
        float halo        = 1.0 - abs(halo_dist - uLensFlareHaloRadius) / halo_w;
        halo = clamp(halo, 0.0, 1.0);
        halo *= halo;
        flare += sun_color * halo * 0.3 * uLensFlareHalo;
    }

    // ---- Optional starburst: angular spikes radiating from the sun ----
    if (uLensFlareStarburst > 0.0)
    {
        vec2 sd = delta;
        sd.x *= aspect;
        float angle = atan(sd.y, sd.x);
        float primary  = float(max(uLensFlareStarburstSpikes, 1));
        float sharp    = max(uLensFlareStarburstSharpness, 1.0);
        float pattern = 0.0;
        pattern += pow(abs(cos(angle * primary)),              sharp)        * 0.5;
        pattern += pow(abs(cos(angle * primary * 2.0 + 0.5)),  sharp * 1.33) * 0.3;
        pattern += pow(abs(cos(angle * primary * 4.0 + 1.0)),  sharp * 1.66) * 0.2;
        float sun_dist = length(sd);
        pattern *= exp(-sun_dist * max(uLensFlareStarburstFalloff, 0.01));

        // Harmonize with the glow: fade spikes out inside the glow's core so the two
        // elements compose (bright core, rays emanating from its edge) instead of
        // stacking and producing a double-bright, conflicting center.
        float core_fade = smoothstep(0.0, max(uLensFlareGlowRadius, 1e-4), sun_dist);
        pattern *= core_fade;

        flare += sun_color * pattern * uLensFlareStarburst * sky_hdr_scale;
    }

    return max(flare * vis * uLensFlareLightColor * 0.15, vec3(0.0));
}
