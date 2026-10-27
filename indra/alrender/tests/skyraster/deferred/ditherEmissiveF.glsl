/**
 * @file skyraster/deferred/ditherEmissiveF.glsl
 * @brief The emissive dither the sky's raster programs wrote through, as deferred/globalF.glsl held it for them.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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

// Interleaved gradient noise (Jimenez 2014). Screen-stable: no temporal term, so the
// pattern does not shimmer frame to frame.
float screenNoise(vec2 frag_px)
{
    return fract(52.9829189 * fract(dot(frag_px, vec2(0.06711056, 0.00583715))));
}

// Break quantization banding at the emissive store. R11F_G11F_B10F keeps 6/6/5 explicit
// mantissa bits, so a value quantizes at roughly 2^-7/2^-7/2^-6 of its own magnitude --
// coarse enough to band in the sky writers' smooth gradients, and coarsest in blue, which is
// exactly where a sky lives. Adding ~1 ulp of screen-stable noise before the ROP rounds
// trades the bands for imperceptible grain. Multiplicative, so black stays black and the
// amplitude tracks the format's scale-relative precision; on the non-HDR GL_RGB8 emissive
// it lands near a single 8-bit step, harmless.
vec3 ditherEmissive(vec3 v, vec2 frag_px)
{
    float n = screenNoise(frag_px) - 0.5;
    return v * (1.0 + n * vec3(1.0 / 64.0, 1.0 / 64.0, 1.0 / 32.0));
}
