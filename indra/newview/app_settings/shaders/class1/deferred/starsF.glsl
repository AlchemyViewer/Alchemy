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

#ifdef STAR_QUADS
in vec2 vary_corner;          // corner offset in [-1, 1] — radial mask source
#else
flat in vec3 vary_point;      // the star's centre in window coordinates, and half its extent in pixels
#endif
flat in vec3 vary_star_color; // the star's colour where its shape is 1 (starsV.glsl)
flat in float vary_spike;     // the diffraction spikes' strength

// The shape's terms at the star's centre, which ALStarField::brightest bounds a star by under the same names less
// STAR_ (alstarfield_test holds the two to each other); the core is 1 there.
const float STAR_HALO  = 0.06; // the halo's
const float STAR_SPIKE = 0.30; // each spike's at full strength

void main()
{
#ifdef STAR_QUADS
    vec2 corner = vary_corner;
#else
    // The pixel's offset from the star's centre, in [-1, 1] across the star with y up, as a quad's corner offset runs;
    // the point is drawn wider than the star (starsV.glsl), and a pixel past the star's square is not its.
    vec2 corner = (gl_FragCoord.xy - vary_point.xy) / vary_point.z;
    if (max(abs(corner.x), abs(corner.y)) >= 1.0) discard;
#endif

    // Procedural star shape. Using squared radius keeps falloff radial without
    // a sqrt; the small halo term adds a soft skirt that helps bright stars
    // integrate into the HDR/bloom pipeline.
    float r2   = dot(corner, corner);
    float core = exp(-r2 * 9.0);
    float halo = exp(-r2 * 1.8) * STAR_HALO;

    // Diffraction spikes on the brightest tail only (starsV.glsl).
    float ax = 1.0 - abs(corner.x);
    float ay = 1.0 - abs(corner.y);
    float spike_h = exp(-abs(corner.y) * 32.0) * ax;
    float spike_v = exp(-abs(corner.x) * 32.0) * ay;
    float spike = (spike_h + spike_v) * vary_spike * STAR_SPIKE;

    float shape = core + halo + spike;

    // Discard pixels where the star contributes effectively nothing - keeps
    // the overdraw budget in check given how many stars we render.
    if (shape < 0.002) discard;

    // Shape is our final alpha mask. Pre-multiply RGB so we can blend additively
    // from the draw pool without alpha weirdness.
    vec3 premult = vary_star_color * shape;

    // The stars draw into one attachment and leave the rest as the sky wrote them, its flags among them
    // (ALSky::renderStars narrows the G-buffer's draw buffers to it).
#if defined(HAS_EMISSIVE)
    // With a dedicated emissive buffer the stars live there (bloom source).
    frag_data[3] = vec4(premult, shape);
#else
    frag_data[0] = vec4(premult, shape);
#endif
}
