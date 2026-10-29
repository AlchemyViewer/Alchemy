/**
 * @file alstarfieldgenerate.h
 * @brief How the star field is drawn from its seed: ALStarField::generate's tiers and generators, over any density of
 *        the Milky Way's band.
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

#include "alrandmap.h"
#include "alstarfield.h"
#include "v4color.h"

#include <cmath>
#include <random>
#include <vector>

namespace ALStarField::detail
{

// The three tiers, layered at different simulated distances. The faint dust dominates the density, giving the eye
// a rich background without the per-star cost dominating memory.
struct StarTier
{
    F32 distance_factor;    // multiplies the dome's radius
    F32 intensity_floor;    // the least intensity the tier's distribution maps to
    F32 intensity_ceil;     // the most
    F32 milky_way_fraction; // 0..1, the fraction of the tier biased toward the galactic band
};

inline constexpr StarTier STAR_TIERS[] = {
    //  dist    i_floor  i_ceil  mw_frac
    {   1.00f,  0.55f,   1.00f,  0.25f },   // bright primary
    {   1.60f,  0.18f,   0.60f,  0.35f },   // mid
    {   3.00f,  0.05f,   0.22f,  0.55f },   // faint "star dust"
};
static_assert(LL_ARRAY_SIZE(STAR_TIERS) == LL_ARRAY_SIZE(TIER_COUNT));

// The field's generator, with a fixed seed: it draws once a star, the seed of the star's own generator, so the
// draws a star takes, however many its direction's rejection needs, leave every later star's seed where it was.
class FieldRandom
{
public:
    U64 operator()() { return mGenerator(); }

private:
    static constexpr U64 SEED = 0x53746172466965ull;
    std::mt19937_64      mGenerator{ SEED };
};

// A star's own generator, SplitMix64 from its seed, its draws mapped to [0, 1) as ll_frand maps the global one's.
// Integer arithmetic alone, so a seed draws the same numbers on every platform.
class StarRandom
{
public:
    explicit StarRandom(U64 seed) : mState(seed) {}

    F32 operator()() { return ALRandMap::unitF32(next()); }

private:
    U64 next()
    {
        mState += 0x9E3779B97F4A7C15ull;
        U64 z = mState;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    U64 mState;
};

// A stellar temperature, roughly as the stars are counted: mostly cool M and K dwarfs, with a long tail toward the
// blue.
inline F32 sampleStellarTemperature(StarRandom& draw)
{
    const F32 u = draw();
    if (u < 0.0025f)      return 18000.0f + draw() * 22000.0f; // O/B class (rare, very blue)
    if (u < 0.02f)        return 10000.0f + draw() *  8000.0f; // late-B / A class (blue-white)
    if (u < 0.10f)        return  7000.0f + draw() *  3000.0f; // F class (yellow-white)
    if (u < 0.30f)        return  5500.0f + draw() *  1500.0f; // G class (sun-like)
    if (u < 0.60f)        return  4000.0f + draw() *  1500.0f; // K class (orange)
    return                       2600.0f + draw() *  1400.0f; // M class (cool red, majority)
}

// A power-law luminosity in [0, 1], heavily toward the faint end; the tier maps it to its range, so each tier's
// distribution reads as a magnitude scale. The exponent of 4 is a rough match to the Salpeter-like tail: most stars
// lie near the floor, a few reach the ceiling.
inline F32 samplePowerLawIntensity(StarRandom& draw)
{
    return powf(draw(), 4.0f);
}

// A direction, rejection-sampled toward the galactic plane by the band's @a density where `milky_way_biased`: a
// Gaussian density about the plane yields a band a few tens of degrees wide, which reads as the Milky Way without
// a texture.
template <typename Density>
LLVector3 generateStarDirection(const Density& density, bool milky_way_biased, StarRandom& draw)
{
    constexpr U32 MAX_ATTEMPTS = 24;

    for (U32 attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
    {
        LLVector3 dir = uniformUpperHemisphere(draw);

        if (!milky_way_biased)
            return dir;

        if (draw() < density(dir))
            return dir;
    }

    // All but never reached; it keeps the function total.
    return uniformUpperHemisphere(draw);
}

/// ALStarField::generate's field, its band's stars drawn by @a density: a callable from a unit direction of the stars'
/// frame to the band's density there, milkyWayDensity in the field the sky draws.
template <typename Density>
std::vector<Star> generate(U32 count, F32 dome_radius, const Density& density)
{
    const U32 target_total = llmin(count, MAX_COUNT);
    const F32 scale = (F32)target_total / (F32)DEFAULT_COUNT;

    std::vector<Star> stars;
    stars.reserve(target_total + LL_ARRAY_SIZE(STAR_TIERS));

    FieldRandom field;
    for (size_t t = 0; t < LL_ARRAY_SIZE(STAR_TIERS); ++t)
    {
        const StarTier& tier = STAR_TIERS[t];
        const F32 tier_radius = dome_radius * tier.distance_factor;
        const U32 tier_count = (U32)((F32)TIER_COUNT[t] * scale + 0.5f);

        for (U32 i = 0; i < tier_count; ++i)
        {
            StarRandom draw(field());
            const bool in_milky_way = draw() < tier.milky_way_fraction;
            const LLVector3 dir = generateStarDirection(density, in_milky_way, draw);

            // The tier's distance dims it through its range, which the power-law draw is mapped into.
            const F32 raw = samplePowerLawIntensity(draw);
            const F32 intensity = llclamp(tier.intensity_floor + raw * (tier.intensity_ceil - tier.intensity_floor),
                                          0.0f, 1.0f);

            const LLColor3 bb = blackBodyColor(sampleStellarTemperature(draw));

            stars.push_back({ dir * tier_radius, LLColor4U(LLColor4(bb, intensity)) });
        }
    }
    return stars;
}

} // namespace ALStarField::detail
