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

in vec2 vary_corner;          // corner offset in [-1, 1] — radial mask source
flat in vec3 vary_star_color; // the star's colour where its shape is 1 (starsV.glsl)
flat in float vary_spike;     // the diffraction spikes' strength

void main()
{
    // Procedural star shape. Using squared radius keeps falloff radial without
    // a sqrt; the small halo term adds a soft skirt that helps bright stars
    // integrate into the HDR/bloom pipeline.
    float r2   = dot(vary_corner, vary_corner);
    float core = exp(-r2 * 9.0);
    float halo = exp(-r2 * 1.8) * 0.06;

    // Diffraction spikes on the brightest tail only (starsV.glsl).
    float ax = 1.0 - abs(vary_corner.x);
    float ay = 1.0 - abs(vary_corner.y);
    float spike_h = exp(-abs(vary_corner.y) * 32.0) * ax;
    float spike_v = exp(-abs(vary_corner.x) * 32.0) * ay;
    float spike = (spike_h + spike_v) * vary_spike * 0.30;

    float shape = core + halo + spike;

    // Discard pixels where the quad contributes effectively nothing - keeps
    // the overdraw budget in check given how many tiny quads we render.
    if (shape < 0.002) discard;

    // Shape is our final alpha mask. Pre-multiply RGB so we can blend additively
    // from the draw pool without alpha weirdness.
    vec3 premult = vary_star_color * shape;

    // GBuffer: stars opt out of atmospheric haze (SKIP_ATMOS).
    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    // With a dedicated emissive buffer the stars live there (bloom source);
    // main diffuse stays zero so atmospheric haze doesn't pick them up.
    frag_data[0] = vec4(0.0);
    frag_data[3] = vec4(premult, shape);
#else
    frag_data[0] = vec4(premult, shape);
#endif
}
