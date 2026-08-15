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
uniform vec2 screen_res;

in vec3 position;       // star center world position (shared by all 6 verts of a star)
in vec4 diffuse_color;  // RGB = sRGB black-body color, A = intrinsic intensity (0..1)
in vec2 texcoord0;      // corner offset in [-1, 1] for GPU-side billboarding

out vec3 vary_world_dir;     // world-space direction from camera to star (post-rotation)
out vec2 vary_corner;        // corner offset, pass-through for fragment shape
out vec4 vary_color;         // rgb = linear BB color, a = intensity
out float vary_intensity;
out float vary_subpixel_fade; // energy attenuation from sub-pixel widening (1.0 = no widening)

// sRGB -> linear approximation; good enough for stars since BB color is smooth.
vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    // Transform the star center to clip space. All 6 verts of a star share
    // the same input position, so the center projects identically; the only
    // per-vertex change is the screen-space corner offset we add below.
    vec4 clip_center = modelview_projection_matrix * vec4(position, 1.0);

    float intensity = diffuse_color.a;

    // Resolution-adaptive pixel size. Base curve keeps the faint/mid majority
    // in the original 2..7px range (crisp point-sources), and only the very
    // brightest ~5% expand to give diffraction spikes room. The smoothstep
    // ensures the expansion isn't a hard step — spikes fade in smoothly as
    // intensity approaches 1.
    float base_px   = mix(1.8, 7.0, intensity);
    float spike_px  = smoothstep(0.90, 1.0, intensity) * 12.0;
    float ideal_px  = base_px + spike_px;

    // Sub-pixel stability: faint stars rendered into a <3px quad flicker as
    // the center drifts across pixel boundaries. Widen modestly to a floor
    // and compensate by scaling output energy by the area ratio — integrated
    // brightness stays the same, but the distribution is smooth enough for
    // the pixel grid to resolve stably.
    const float min_px    = 2.6;
    float quad_pixels     = max(ideal_px, min_px);
    vary_subpixel_fade    = (ideal_px * ideal_px) / (quad_pixels * quad_pixels);

    vec2 pixel_offset = texcoord0 * (quad_pixels * 0.5);

    // Convert pixels to NDC then to clip space (clip = ndc * w).
    vec2 ndc_offset = pixel_offset * (2.0 / max(screen_res, vec2(1.0)));
    clip_center.xy += ndc_offset * clip_center.w;

    // Smash Z to the far clip plane so stars never poke through the moon/sky. Reverse-Z
    // (glClipControl ZERO_TO_ONE) puts the far plane at ndc z 0, not 1; the sky depth func
    // flips to GEQUAL so it still passes against a 0-cleared buffer.
#ifdef REVERSE_Z
    clip_center.z = 0.0;
#else
    clip_center.z = clip_center.w;
#endif

    gl_Position = clip_center;

    vary_corner    = texcoord0;
    vary_world_dir = normalize(position);
    // NB: this linearises the sRGB black-body colour, and softenLight's SKIP_ATMOS branch
    // then srgb_to_linear's it a SECOND time and multiplies by sky_hdr_scale (the sky dome's
    // fake-HDR boost, ~2x with HDR on -- stars inherit it via the shared SKIP_ATMOS flag).
    // Long-standing upstream behaviour. Net star brightness balances that double-decode
    // against the gains here (bloom_boost, the (1 + intensity) gain) and sky_hdr_scale -- if
    // star brightness is ever retuned, this whole chain is the place to look, not one line.
    vary_color     = vec4(srgb_to_linear(diffuse_color.rgb), intensity);
    vary_intensity = intensity;
}
