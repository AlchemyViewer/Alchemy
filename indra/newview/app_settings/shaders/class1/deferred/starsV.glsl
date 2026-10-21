/**
 * @file starsV.glsl
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

// One vertex a star, drawn as a point; or, for the stars with diffraction spikes (STAR_QUADS), six, drawn as a quad: GL
// drops a point whose centre leaves the screen, which would pop a spiked star's arms at the edge
// (ALSky::renderStars, ALStarField).
in vec3 position;       // the star's centre, in the stars' frame
in int texture_index;   // its colour, in the position's fourth word: rgb = sRGB black-body colour, a = intensity

uniform vec2 screen_res;       // the viewport the stars draw into, in pixels
#ifdef STAR_QUADS
out vec2 vary_corner;          // corner offset, pass-through for fragment shape
#else
flat out vec3 vary_point;      // the star's centre in window coordinates, and half its extent in pixels
#endif
flat out vec3 vary_star_color; // what the star adds where its shape is 1: every per-star term but the shape
flat out float vary_spike;     // the diffraction spikes' strength

uniform float custom_alpha;    // star_brightness slider output (0..2 range in practice)
uniform float time;            // seconds, wrapped to avoid precision drift
uniform vec3  moon_dir;        // moon direction in the stars' frame (z up)
uniform float moon_brightness; // moon illumination 0..1 (from LLSettingsSky)

// The moon's quad in the stars' frame, on the plane a unit away along moonQuadCenter (zero when no moon is drawn), and
// its texture (ALSky::renderStars).
uniform vec3 moonQuadCenter;
uniform vec3 moonQuadU;
uniform vec3 moonQuadV;
uniform sampler2D diffuseMap;

// The bounds of the per-star terms. ALStarField::brightest bounds what a star can add from the same values, under the
// same names less STAR_, and alstarfield_test holds the two to each other.
const float STAR_FAINT_PX      = 1.8;  // the ideal width of a star of intensity 0, in pixels
const float STAR_BRIGHT_PX     = 7.0;  // and of intensity 1, before its spikes
const float STAR_SPIKE_START   = 0.90; // the intensity past which a star grows diffraction spikes
const float STAR_SPIKE_PX      = 12.0; // the width the spikes add at intensity 1
const float STAR_MIN_PX        = 2.6;  // the least width a star is drawn at, its brightness spread over the area
const float STAR_TWINKLE_MAX   = 0.65; // the most the scintillation moves a star's brightness, a share of its intensity
const float STAR_CHROMA        = 0.18; // the colour scintillation, a share of the brightness's
const float STAR_MOON_WASH     = 0.55; // how far a moon at full power washes out a star of intensity 0
const float STAR_MOON_FADE_MAX = 0.95; // the most the moon's wash and glare dim a star together
const float STAR_BLOOM_BOOST   = 1.2;  // the emissive's gain past 1, times the intensity squared

// sRGB -> linear approximation; good enough for stars since BB color is smooth.
vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

// deferred/valueNoiseUtil.glsl
float valueNoise3D(vec3 p);

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

// SL-14113: no star behind the moon. The composite lays the moon over the stars and fades it low in the sky
// (deferred/skyBodiesF.glsl's skyMoon), which would let them through its disc, so a star whose direction falls on a
// texel the moon draws, taken from the moon's own quad and texture, is left out whole.
bool behindMoon(vec3 dir)
{
    float moon_facing = dot(dir, moonQuadCenter);
    if (moon_facing > 0.0)
    {
        vec3 on_quad = dir / moon_facing - moonQuadCenter;
        vec2 quad = vec2(dot(on_quad, moonQuadU), dot(on_quad, moonQuadV));
        return all(lessThan(abs(quad), vec2(1.0))) && textureLod(diffuseMap, quad * 0.5 + 0.5, 0.0).a > 2./255.;
    }
    return false;
}

void main()
{
    // Every term but the shape depends only on the star, so it is worked here, once a vertex, rather than once a
    // fragment, and the fragment stage multiplies the shape by it.
    vec3 dir = normalize(position);
    if (behindMoon(dir))
    {
        // Outside the clip volume: nothing of the star is drawn.
        gl_Position     = vec4(2.0, 2.0, 2.0, 1.0);
#ifdef STAR_QUADS
        vary_corner     = vec2(0.0);
#else
        gl_PointSize    = 1.0;
        vary_point      = vec3(0.0, 0.0, 1.0);
#endif
        vary_star_color = vec3(0.0);
        vary_spike      = 0.0;
        return;
    }

    // Transform the star center to clip space. A quad's 6 verts share the
    // same input position, so the center projects identically; the only
    // per-vertex change is the screen-space corner offset we add below.
    vec4 clip_center = modelview_projection_matrix * vec4(position, 1.0);

    vec4 star = unpackUnorm4x8(uint(texture_index));
    float intensity = star.a;

    // Resolution-adaptive pixel size. Base curve keeps the faint/mid majority
    // in the original 2..7px range (crisp point-sources), and only the very
    // brightest ~5% expand to give diffraction spikes room. The smoothstep
    // ensures the expansion isn't a hard step — spikes fade in smoothly as
    // intensity approaches 1.
    float base_px   = mix(STAR_FAINT_PX, STAR_BRIGHT_PX, intensity);
    float spike_px  = smoothstep(STAR_SPIKE_START, 1.0, intensity) * STAR_SPIKE_PX;
    float ideal_px  = base_px + spike_px;

    // Sub-pixel stability: faint stars rendered into a <3px quad flicker as
    // the center drifts across pixel boundaries. Widen modestly to a floor
    // and compensate by scaling output energy by the area ratio — integrated
    // brightness stays the same, but the distribution is smooth enough for
    // the pixel grid to resolve stably.
    float quad_pixels      = max(ideal_px, STAR_MIN_PX);
    float subpixel_fade    = (ideal_px * ideal_px) / (quad_pixels * quad_pixels);

#ifdef STAR_QUADS
    // The corner, from the vertex's place among its star's six: two triangles, BL BR TR and BL TR TL.
    const vec2 CORNERS[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = CORNERS[gl_VertexID % 6];
    vec2 pixel_offset = corner * (quad_pixels * 0.5);

    // Convert pixels to NDC then to clip space (clip = ndc * w).
    vec2 ndc_offset = pixel_offset * (2.0 / max(screen_res, vec2(1.0)));
    clip_center.xy += ndc_offset * clip_center.w;
#else
    // The star covers what its quad would: the pixels whose centres lie in a square quad_pixels a side about its
    // centre. GL rounds a point's size to its granularity (0.125 pixels on NVIDIA) and measures gl_PointCoord across
    // the rounded square, which would scale a 2.6-pixel star's shape by up to 5%; so the point is drawn a pixel wider,
    // and starsF.glsl works each pixel's offset from the star's own centre and leaves out those past its square.
    gl_PointSize = quad_pixels + 1.0;
    vary_point = vec3((clip_center.xy / clip_center.w * 0.5 + 0.5) * screen_res, quad_pixels * 0.5);
#endif

    // On the far plane with the rest of the sky, so geometry occludes stars and the sun and
    // moon, drawn after them, cover them. Reverse-Z (glClipControl ZERO_TO_ONE) puts the far
    // plane at ndc z 0, not 1; the sky depth func flips to GEQUAL so it still passes against a
    // 0-cleared buffer.
#ifdef REVERSE_Z
    clip_center.z = 0.0;
#else
    clip_center.z = clip_center.w;
#endif

    gl_Position = clip_center;
#ifdef STAR_QUADS
    vary_corner = corner;
#endif

    // This linearises the sRGB black-body colour, and the composite decodes it a second time: it adds what the stars
    // left in their attachment to the display-encoded dome, decodes the sum and multiplies it by sky_hdr_scale, the
    // dome's fake-HDR boost (deferred/skyCompositeF.glsl). Star brightness is tuned against that double decode, the
    // gains here (the bloom boost, the 1 + intensity gain) and sky_hdr_scale together: retune the chain, not one line.
    vec3 color = srgb_to_linear(star.rgb);

    // --- Atmospheric extinction ----------------------------------------------
    // Airmass grows sharply near the horizon. The wavelength-dependent
    // extinction (Rayleigh-dominated) reddens low stars, matching the familiar
    // deep-orange appearance of rising/setting bright stars.
    float cos_zenith = dir.z; // +Z is up in sky local space
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
    vec3 seed = dir * 120.0;

    float n_fast = valueNoise3D(seed + vec3(0.0, 0.0, time * 2.3));
    float n_slow = valueNoise3D(seed * 0.37 + vec3(0.0, 0.0, time * 0.7));
    float twinkle_base = mix(n_fast, n_slow, 0.35);

    // Scintillation is much stronger near the horizon (airmass) and for the
    // brighter stars our eye actually resolves flickering on.
    float twinkle_amount = clamp(0.18 + 0.14 * (airmass - 1.0), 0.0, STAR_TWINKLE_MAX) * intensity;
    float twinkle = 1.0 + (twinkle_base - 0.5) * 2.0 * twinkle_amount;

    // Subtle color scintillation: each channel samples its own noise with a
    // phase shift, so the twinkle occasionally tints the star.
    vec3 chroma_noise = vec3(
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.7 +  0.0)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 1.3 +  5.7)),
        valueNoise3D(seed * 0.55 + vec3(0.0, 0.0, time * 2.1 + 13.1)));
    float chroma_amt = STAR_CHROMA * twinkle_amount;
    vec3  color_jitter = 1.0 + (chroma_noise - 0.5) * 2.0 * chroma_amt;

    // --- Moon glare ----------------------------------------------------------
    // Two contributions: a broad "sky wash" that dims faint stars when a bright
    // moon is up (scattered moonlight raising the night-sky floor), and a
    // narrow direct glare around the moon itself that kills anything nearby.
    // Bright stars are almost immune to sky wash (they still outshine the
    // scattered background), but nothing survives direct glare at a full moon.
    float moon_up    = max(0.0, moon_dir.z);                  // horizon clip
    float moon_power = moon_brightness * moon_up;             // 0..~1
    float sky_wash   = moon_power * (1.0 - intensity) * STAR_MOON_WASH;
    float moon_cos   = max(0.0, dot(dir, moon_dir));
    float direct_glare = pow(moon_cos, 10.0) * moon_power * 0.85;
    float moon_fade  = clamp(sky_wash + direct_glare, 0.0, STAR_MOON_FADE_MAX);

    // --- Composition ---------------------------------------------------------
    vec3 star_color = color * color_jitter * extinction * twinkle;
    star_color *= intensity;
    star_color *= custom_alpha;
    star_color *= (1.0 - moon_fade);
    star_color *= subpixel_fade;

#if defined(HAS_EMISSIVE)
    // HDR boost for the emissive/bloom buffer: bright stars should feed the
    // bloom pyramid with a larger signal than their on-screen dot suggests.
    // Kept modest so the bloom pass doesn't smear every mid-tier star into a
    // halo blob.
    star_color *= 1.0 + intensity * intensity * STAR_BLOOM_BOOST;
#else
    // No emissive buffer - the stars go to diffuse. The glow extraction still catches
    // the bright peaks via thresholding, so scale slightly up to compensate.
    star_color *= 1.0 + intensity;
#endif

    vary_star_color = star_color;

    // Diffraction spikes on the brightest tail only. Gated tight (0.90..1.0)
    // so only real stand-out stars get crosses — their quads expand above,
    // so the spike has room to render.
    vary_spike = smoothstep(STAR_SPIKE_START, 1.0, intensity);
}
