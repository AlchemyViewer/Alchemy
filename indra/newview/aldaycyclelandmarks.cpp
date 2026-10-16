/**
 * @file aldaycyclelandmarks.cpp
 * @brief Finding sunrise, noon, sunset and midnight in an arbitrary day cycle
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "aldaycyclelandmarks.h"

#include <cmath>
#include <vector>

namespace ALDayCycleLandmarks
{

namespace
{
/// Below this much variation in altitude across the whole cycle there is no
/// meaningful high or low point to name. Well under a degree of arc, so a
/// cycle a person would call static reads as static, and any cycle with real
/// sun movement in it clears this by orders of magnitude.
constexpr F32 FLAT_CYCLE_EPSILON = 1e-4f;

/// Where the segment from (0, a) to (step, b) crosses zero, as an offset into
/// the segment. Only called when a and b straddle zero, so the denominator
/// cannot vanish.
F32 crossingOffset(F32 a, F32 b, F32 step)
{
    return step * (-a / (b - a));
}
} // namespace

Landmarks find(const altitude_sampler_t& sampler, S32 samples)
{
    Landmarks out;

    if (!sampler || samples < 2)
    {
        return out;
    }

    const F32 step = 1.f / (F32)samples;

    std::vector<F32> altitude((size_t)samples, 0.f);
    for (S32 i = 0; i < samples; ++i)
    {
        altitude[(size_t)i] = sampler((F32)i * step);
    }

    S32 highest = 0;
    S32 lowest  = 0;
    for (S32 i = 1; i < samples; ++i)
    {
        if (altitude[(size_t)i] > altitude[(size_t)highest])
        {
            highest = i;
        }
        if (altitude[(size_t)i] < altitude[(size_t)lowest])
        {
            lowest = i;
        }
    }

    // A cycle that does not move the sun has no moment worth jumping to, and
    // saying so is better than handing back position zero four times over.
    if ((altitude[(size_t)highest] - altitude[(size_t)lowest]) < FLAT_CYCLE_EPSILON)
    {
        return out;
    }

    // Noon and midnight are the extremes, but only where the horizon makes
    // them mean what they are called. A sun that stays up all cycle has a
    // brightest moment and no midnight; one that never rises has neither.
    if (altitude[(size_t)highest] > 0.f)
    {
        out.has_noon = true;
        out.noon     = (F32)highest * step;
    }
    if (altitude[(size_t)lowest] < 0.f)
    {
        out.has_midnight = true;
        out.midnight     = (F32)lowest * step;
    }

    // The cycle wraps, so the last sample's neighbour is the first: a sunrise
    // sitting across the seam is still a sunrise.
    for (S32 i = 0; i < samples; ++i)
    {
        const F32 here = altitude[(size_t)i];
        const F32 next = altitude[(size_t)((i + 1) % samples)];

        if (here < 0.f && next >= 0.f && !out.has_sunrise)
        {
            out.has_sunrise = true;
            out.sunrise     = std::fmod((F32)i * step + crossingOffset(here, next, step), 1.f);
        }
        else if (here >= 0.f && next < 0.f && !out.has_sunset)
        {
            out.has_sunset = true;
            out.sunset     = std::fmod((F32)i * step + crossingOffset(here, next, step), 1.f);
        }

        if (out.has_sunrise && out.has_sunset)
        {
            break;
        }
    }

    return out;
}

} // namespace ALDayCycleLandmarks
