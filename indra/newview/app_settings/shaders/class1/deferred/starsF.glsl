/**
 * @file starsF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
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

in vec3 vary_world_dir;      // normalized star direction in (rotated) sky local space
in vec2 vary_corner;         // corner offset in [-1, 1] — radial mask source
in vec4 vary_color;          // rgb = linear BB color, a = intensity
in float vary_intensity;
in float vary_subpixel_fade; // energy attenuation for stars widened for sub-pixel stability

uniform float custom_alpha;   // star_brightness slider output (0..2 range in practice)
uniform float time;           // seconds, wrapped to avoid precision drift
uniform vec3  moon_dir;       // world-space moon direction (z up)
uniform float moon_brightness;// moon illumination 0..1 (from LLSettingsSky)

// 3D value noise
// -------------------------------------------------------------------
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

// Kasten-Young airmass. cos_zenith is the cosine of the zenith angle (z
// component of the world-up aligned direction). Clamped to avoid the
// horizon singularity.
float airmassKastenYoung(float cos_zenith)
{
    float cz = clamp(cos_zenith, 0.02, 1.0);
    // Kasten & Young (1989): X = 1 / (cos(z) + 0.50572 * (96.07995 - z_deg)^-1.6364)
    // Rewritten using the elevation angle in degrees.
    float elev_deg = degrees(asin(cz));
    return 1.0 / (cz + 0.50572 * pow(max(6.07995 + elev_deg, 1.0), -1.6364));
}

void main()
{
    // Procedural star shape. Using squared radius keeps falloff radial without
    // a sqrt; the small halo term adds a soft skirt that helps bright stars
    // integrate into the HDR/bloom pipeline.
    float r2   = dot(vary_corner, vary_corner);
    float core = exp(-r2 * 9.0);
    float halo = exp(-r2 * 1.8) * 0.06;

    // Diffraction spikes on the brightest tail only. Gated tight (0.90..1.0)
    // so only real stand-out stars get crosses — the matching vertex shader
    // also expands those stars' quads, so the spike has room to render.
    float ax = 1.0 - abs(vary_corner.x);
    float ay = 1.0 - abs(vary_corner.y);
    float spike_h = exp(-abs(vary_corner.y) * 32.0) * ax;
    float spike_v = exp(-abs(vary_corner.x) * 32.0) * ay;
    float spike_strength = smoothstep(0.90, 1.0, vary_intensity);
    float spike = (spike_h + spike_v) * spike_strength * 0.30;

    float shape = core + halo + spike;

    // Discard pixels where the quad contributes effectively nothing - keeps
    // the overdraw budget in check given how many tiny quads we render.
    if (shape < 0.002) discard;

    // --- Atmospheric extinction ----------------------------------------------
    // Airmass grows sharply near the horizon. The wavelength-dependent
    // extinction (Rayleigh-dominated) reddens low stars, matching the familiar
    // deep-orange appearance of rising/setting bright stars.
    float cos_zenith = vary_world_dir.z; // +Z is up in sky local space
    float airmass    = airmassKastenYoung(cos_zenith);

    // Extinction coefficients tuned for a clear night (blue scatters ~5x more
    // than red). Multiplied by a small overall factor so the zenith is nearly
    // transparent while the horizon meaningfully dims.
    const vec3 k_ext = vec3(0.05, 0.13, 0.30);
    vec3 extinction  = exp(-airmass * k_ext);

    // --- Scintillation -------------------------------------------------------
    // Per-star seed via the full 3D direction so nearby stars with similar xy
    // but different elevations don't share the same twinkle pattern. Two
    // noise samples at different frequencies give an organic beat instead of
    // a periodic pulse.
    vec3 seed = vary_world_dir * 120.0;

    float n_fast = valueNoise3D(seed + vec3(0.0, 0.0, time * 2.3));
    float n_slow = valueNoise3D(seed * 0.37 + vec3(0.0, 0.0, time * 0.7));
    float twinkle_base = mix(n_fast, n_slow, 0.35);

    // Scintillation is much stronger near the horizon (airmass) and for the
    // brighter stars our eye actually resolves flickering on.
    float twinkle_amount = clamp(0.18 + 0.14 * (airmass - 1.0), 0.0, 0.65) * vary_intensity;
    float twinkle = 1.0 + (twinkle_base - 0.5) * 2.0 * twinkle_amount;

    // Subtle color scintillation: each channel samples its own noise with a
    // phase shift, so the twinkle occasionally tints the star.
    vec3 chroma_noise = vec3(
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.7 +  0.0)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.3 +  5.7)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 2.1 + 13.1)));
    float chroma_amt = 0.18 * twinkle_amount;
    vec3  color_jitter = 1.0 + (chroma_noise - 0.5) * 2.0 * chroma_amt;

    // --- Moon glare ----------------------------------------------------------
    // Two contributions: a broad "sky wash" that dims faint stars when a bright
    // moon is up (scattered moonlight raising the night-sky floor), and a
    // narrow direct glare around the moon itself that kills anything nearby.
    // Bright stars are almost immune to sky wash (they still outshine the
    // scattered background), but nothing survives direct glare at a full moon.
    float moon_up    = max(0.0, moon_dir.z);                  // horizon clip
    float moon_power = moon_brightness * moon_up;             // 0..~1
    float sky_wash   = moon_power * (1.0 - vary_intensity) * 0.55;
    float moon_cos   = max(0.0, dot(vary_world_dir, moon_dir));
    float direct_glare = pow(moon_cos, 10.0) * moon_power * 0.85;
    float moon_fade  = clamp(sky_wash + direct_glare, 0.0, 0.95);

    // --- Composition ---------------------------------------------------------
    vec3 star_color = vary_color.rgb * color_jitter * extinction * twinkle;
    star_color *= vary_intensity;
    star_color *= custom_alpha;
    star_color *= (1.0 - moon_fade);
    star_color *= vary_subpixel_fade;

    // Shape is our final alpha mask. Pre-multiply RGB so we can blend additively
    // from the draw pool without alpha weirdness.
    vec3 premult = star_color * shape;

    // HDR boost for the emissive/bloom buffer: bright stars should feed the
    // bloom pyramid with a larger signal than their on-screen dot suggests.
    // Kept modest so the bloom pass doesn't smear every mid-tier star into a
    // halo blob.
    float bloom_boost = 1.0 + vary_intensity * vary_intensity * 1.2;

    // GBuffer: stars opt out of atmospheric haze (SKIP_ATMOS).
    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    // With a dedicated emissive buffer the stars live there (bloom source);
    // main diffuse stays zero so atmospheric haze doesn't pick them up.
    frag_data[0] = vec4(0.0);
    frag_data[3] = vec4(premult * bloom_boost, shape);
#else
    // No emissive buffer - write to diffuse. The glow extraction still catches
    // the bright peaks via thresholding, so scale slightly up to compensate.
    frag_data[0] = vec4(premult * (1.0 + vary_intensity), shape);
#endif
}
