/**
 * @file alstarfield.cpp
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

#include "linden_common.h"

#include "alstarfield.h"

#include "alstarfieldgenerate.h"
#include "llmath.h"
#include "v3color.h"
#include "v4color.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace
{
    // An arbitrary but fixed galactic pole: turning it would turn the Milky Way's band, which it sets running across
    // the sky at a slight incline.
    LLVector3 galacticPole()
    {
        LLVector3 pole(0.30f, 0.72f, 0.62f);
        pole.normVec();
        return pole;
    }
}

namespace ALStarField
{

LLColor3 blackBodyColor(F32 temperature_K)
{
    const F32 t = temperature_K * 0.01f; // in hundreds of kelvin

    F32 r, g, b;

    if (t <= 66.0f)
    {
        r = 1.0f;
    }
    else
    {
        const F32 x = t - 60.0f;
        r = 329.698727446f * powf(x, -0.1332047592f) / 255.0f;
    }

    if (t <= 66.0f)
    {
        g = (99.4708025861f * logf(llmax(1.0f, t)) - 161.1195681661f) / 255.0f;
    }
    else
    {
        const F32 x = t - 60.0f;
        g = 288.1221695283f * powf(x, -0.0755148492f) / 255.0f;
    }

    if (t >= 66.0f)
    {
        b = 1.0f;
    }
    else if (t <= 19.0f)
    {
        b = 0.0f;
    }
    else
    {
        b = (138.5177312231f * logf(t - 10.0f) - 305.0447927307f) / 255.0f;
    }

    return LLColor3(llclamp(r, 0.0f, 1.0f), llclamp(g, 0.0f, 1.0f), llclamp(b, 0.0f, 1.0f));
}

F32 milkyWayDensity(const LLVector3& direction)
{
    static const LLVector3 galactic_pole = galacticPole();

    // 0 at the plane, 1 at the pole.
    const F32 cos_to_pole = fabsf(direction * galactic_pole);
    // A Gaussian density peaking at the plane.
    return expf(-cos_to_pole * cos_to_pole * 10.0f);
}

std::vector<Star> generate(U32 count, F32 dome_radius)
{
    return detail::generate(count, dome_radius, [](const LLVector3& direction) { return milkyWayDensity(direction); });
}

Layout layOut(std::vector<Star>& stars)
{
    const auto points = std::stable_partition(stars.begin(), stars.end(), spiked);

    Layout layout;
    layout.mQuads  = (U32)(points - stars.begin());
    layout.mPoints = (U32)(stars.end() - points);
    return layout;
}

void fillVertices(const std::vector<Star>& stars, const Layout& layout, LLStrider<LLVector4a> vertices)
{
    llassert(stars.size() == (size_t)layout.mQuads + layout.mPoints);

    const auto put = [&vertices](const Star& star)
    {
        LLVector4a& vertex = *(vertices++);
        vertex.load3(star.mPosition.mV);
        // The colour's bytes as they are, never through a float: an F32 with these bits may be a NaN.
        std::memcpy(reinterpret_cast<U8*>(&vertex) + 3 * sizeof(F32), star.mColour.mV, sizeof(star.mColour.mV));
    };

    for (U32 s = 0; s < layout.mQuads; ++s)
    {
        for (U32 v = 0; v < QUAD_VERTICES; ++v)
        {
            put(stars[s]);
        }
    }
    for (U32 s = layout.mQuads; s < layout.mQuads + layout.mPoints; ++s)
    {
        put(stars[s]);
    }
}

} // namespace ALStarField
