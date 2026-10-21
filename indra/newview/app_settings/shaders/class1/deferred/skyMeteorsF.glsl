/**
 * @file class1/deferred/skyMeteorsF.glsl
 * @brief The meteors as a function of the pixel: what their streaks add to the sky there, for the sky's composite.
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

// The most meteors, and the vectors each takes: ALSkyLayers::MAX_METEORS and METEOR_VECTORS, which these must equal.
#define SKY_MAX_METEORS    64
#define SKY_METEOR_VECTORS 3

// The streaks on the screen, SKY_METEOR_VECTORS vectors each (ALSkyLayers::packMeteors): the midpoint in normalized
// device coordinates and the first row of the inverse of the streak's frame; that inverse's second row, the width class
// the streak reaches across, and the one its profile is divided by; and its colour, linear.
uniform vec4 skyMeteorData[SKY_MAX_METEORS * SKY_METEOR_VECTORS];
uniform int  skyMeteorCount;

// What the meteors add at ndc, the pixel's normalized device coordinates: each streak's colour times its profile and
// the gain of the path, 4 with the emissive buffer and 2 without.
vec3 skyMeteors(vec2 ndc)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < skyMeteorCount; ++i)
    {
        vec4 a = skyMeteorData[i * SKY_METEOR_VECTORS];
        vec4 b = skyMeteorData[i * SKY_METEOR_VECTORS + 1];

        // (u, v): along the streak, -1 at the tail to 1 at the head, and across it in units of the width class.
        vec2 d = ndc - a.xy;
        vec2 coord = vec2(dot(a.zw, d), dot(b.xy, d));
        if (abs(coord.x) > 1.0 || abs(coord.y) > b.z)
        {
            continue;
        }

        // Along-streak profile: faded tail (u=-1), bright head (u=+1). The head
        // gets an extra localized flash from the cubic term to read as a distinct
        // "tip" instead of a uniform streak.
        float along_t = clamp((coord.x + 1.0) * 0.5, 0.0, 1.0);
        float head_flash = pow(along_t, 6.0);
        float along = along_t + head_flash * 1.2;

        // Across-streak: Gaussian falloff, normalized by the meteor's width_scale
        // so that thick meteors stay solid in the middle instead of looking like a
        // dim wide quad. y_norm is in [-1, +1] regardless of class.
        float y_norm = coord.y / b.w;
        float across = exp(-y_norm * y_norm * 6.0);

        float shape = along * across;
        if (shape < 0.003)
        {
            continue;
        }

        sum += skyMeteorData[i * SKY_METEOR_VECTORS + 2].rgb * shape;
    }

#if defined(HAS_EMISSIVE)
    return sum * 4.0;
#else
    return sum * 2.0;
#endif
}
