/**
 * @file alstarfield.h
 * @brief The night sky's stars: where they lie and the colour each one is.
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
 * $/LicenseInfo$
 */

#pragma once

#include "llmath.h"
#include "stdtypes.h"
#include "v3color.h"
#include "v3math.h"
#include "v4coloru.h"

#include <cmath>
#include <vector>

/// The star field, worked from a star count and the dome's radius alone: no GL and no settings, so LLVOWLSky's buffer
/// and the GL tests that draw it share one definition.
///
/// The stars' frame is the agent's turned slowly about z (LLDrawPoolWLSky::renderStarsDeferred): x and y on the
/// horizon, z up. Every star lies on the upper hemisphere.
namespace ALStarField
{

/// A star: its centre on its tier's sphere about the camera, and its colour as the vertex stage takes it, the
/// black-body colour (sRGB) in rgb and the star's intrinsic intensity in alpha.
struct Star
{
    LLVector3 mPosition;
    LLColor4U mColour;
};

/// The stars a count of DEFAULT_COUNT lays out in each of the three tiers: bright primaries, mid stars and the faint
/// dust, at 1, 1.6 and 3 dome radii. Any other count scales every tier alike, so the mix stays the same.
constexpr U32 TIER_COUNT[3] = { 5000, 30000, 120000 };
constexpr U32 DEFAULT_COUNT = TIER_COUNT[0] + TIER_COUNT[1] + TIER_COUNT[2];

/// The most stars a count may ask for.
constexpr U32 MAX_COUNT = 700000;

/// Tanner Helland's black-body colour for a temperature in kelvin, sRGB 0..1: within a few percent over the visual
/// range 1000 K..40000 K, and the familiar red to white to blue of the stars. The meteors take theirs from it too.
LLColor3 blackBodyColor(F32 temperature_K);

/// A direction uniform over the upper hemisphere (z >= 0), from two of @a draw's numbers in [0, 1): z, which is
/// uniform by area, then the azimuth. The meteors start from it too.
template <typename Rand> LLVector3 uniformUpperHemisphere(Rand& draw)
{
    const F32 z   = draw();
    const F32 phi = F_TWO_PI * draw();
    const F32 r   = sqrtf(llmax(0.0f, 1.0f - z * z));
    return LLVector3(r * cosf(phi), r * sinf(phi), z);
}

/// The Milky Way band's density at @a direction, a unit vector of the stars' frame: 1 on the galactic plane, falling as
/// a Gaussian to under 0.0001 at its pole. A star of the band keeps a direction drawn uniformly over the hemisphere
/// where a draw in [0, 1) falls under the density there.
F32 milkyWayDensity(const LLVector3& direction);

/// The field of @a count stars, held to MAX_COUNT, about a dome of @a dome_radius, the band's stars drawn by
/// milkyWayDensity (detail::generate, alstarfieldgenerate.h). The stars draw from a generator of their own with a fixed
/// seed, so a count lays out the same field in every session and on every platform, and images of the sky compare.
/// Each star takes one draw of it, the seed of a generator of the star's own that every draw of the star comes from; so
/// where a platform's maths rounds one of a star's comparisons the other way, as expf may for the band's, that star may
/// differ, and no other does.
std::vector<Star> generate(U32 count, F32 dome_radius);

} // namespace ALStarField
