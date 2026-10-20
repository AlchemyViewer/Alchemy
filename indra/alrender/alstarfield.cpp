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
#include "llmatrix4a.h"
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

U32 cellOf(const LLVector3& direction)
{
    const F32 length = direction.length();
    const F32 z = length > 0.f ? llclamp(direction.mV[VZ] / length, 0.f, 1.f) : 0.f;
    const U32 band = llmin((U32)(z * (F32)BANDS), BANDS - 1);
    F32 azimuth = atan2f(direction.mV[VY], direction.mV[VX]);
    if (azimuth < 0.f)
    {
        azimuth += F_TWO_PI;
    }
    const U32 segment = llmin((U32)(azimuth / F_TWO_PI * (F32)SEGMENTS), SEGMENTS - 1);
    return band * SEGMENTS + segment;
}

Layout layOut(std::vector<Star>& stars)
{
    // The stars in the buffer's order by a counting sort, which is stable: one key for each kind, cell and intensity,
    // the spiked stars' first, by intensity alone, then each cell's points', brightest first within each, and stars as
    // bright in the field's own order.
    constexpr U32 INTENSITIES = 256;
    constexpr U32 QUAD_KEYS   = INTENSITIES;
    constexpr U32 KEYS        = QUAD_KEYS + CELLS * INTENSITIES;

    const U32 count = (U32)stars.size();
    std::vector<U32> keys(count);
    std::vector<U32> starts(KEYS + 1, 0); // each key's count, then where its stars start
    U32 quads = 0;
    for (U32 s = 0; s < count; ++s)
    {
        const Star& star = stars[s];
        const U32 dimmer = INTENSITIES - 1 - star.mColour.mV[VALPHA];
        U32 key = dimmer;
        if (spiked(star))
        {
            ++quads;
        }
        else
        {
            key = QUAD_KEYS + cellOf(star.mPosition) * INTENSITIES + dimmer;
        }
        keys[s] = key;
        ++starts[key + 1];
    }
    for (U32 k = 1; k <= KEYS; ++k)
    {
        starts[k] += starts[k - 1];
    }

    Layout layout;
    layout.mQuads  = quads;
    layout.mPoints = count - quads;

    // Where each cell's points start among the points, before the scatter moves the starts on.
    std::vector<U32> cell_starts(CELLS + 1);
    for (U32 c = 0; c < CELLS; ++c)
    {
        cell_starts[c] = starts[QUAD_KEYS + c * INTENSITIES] - quads;
    }
    cell_starts[CELLS] = layout.mPoints;

    std::vector<Star> sorted(count);
    for (U32 s = 0; s < count; ++s)
    {
        sorted[starts[keys[s]]++] = stars[s];
    }
    stars.swap(sorted);
    const auto points = stars.begin() + quads;

    // Each cell's run, and the cone its stars' directions lie in: about their mean, as wide as the farthest.
    layout.mCells.resize(CELLS);
    for (U32 c = 0; c < CELLS; ++c)
    {
        const U32 first = cell_starts[c];
        Cell& run = layout.mCells[c];
        run.mFirst = layout.firstPoint() + first;
        run.mCount = cell_starts[c + 1] - first;

        LLVector3 sum;
        for (U32 p = first; p < first + run.mCount; ++p)
        {
            sum += points[p].mPosition / points[p].mPosition.length();
        }
        if (run.mCount == 0 || sum.normalize() == 0.f)
        {
            run.mAxis.set(0.f, 0.f, 1.f);
            run.mSinRadius = run.mCount == 0 ? 0.f : 1.f;
        }
        else
        {
            run.mAxis = sum;
            F32 least_cos = 1.f;
            for (U32 p = first; p < first + run.mCount; ++p)
            {
                least_cos = llmin(least_cos, run.mAxis * (points[p].mPosition / points[p].mPosition.length()));
            }
            run.mSinRadius = least_cos > 0.f ? sqrtf(llmax(0.f, 1.f - least_cos * least_cos)) : 1.f;
        }
    }

    layout.mIntensities.reserve(stars.size());
    for (const Star& star : stars)
    {
        layout.mIntensities.push_back(star.mColour.mV[VALPHA]);
    }
    return layout;
}

F32 brightest(F32 intensity, F32 star_alpha, F32 moon_power, bool emissive)
{
    const F32 i = llclamp(intensity, 0.f, 1.f);

    // starsV.glsl's extent: the spikes' strength, the star's ideal width, and the fade of one widened to MIN_PX.
    const F32 t      = llclamp((i - SPIKE_START) / (1.0f - SPIKE_START), 0.f, 1.f);
    const F32 spike  = t * t * (3.f - 2.f * t);
    const F32 ideal  = FAINT_PX + (BRIGHT_PX - FAINT_PX) * i + spike * SPIKE_PX;
    const F32 width  = llmax(ideal, MIN_PX);
    const F32 fade   = (ideal * ideal) / (width * width);

    // The scintillation at its peak: twinkle_amount reaches TWINKLE_MAX of the intensity, and the colour jitter
    // CHROMA of that.
    const F32 twinkle = 1.f + TWINKLE_MAX * i;
    const F32 jitter  = 1.f + CHROMA * TWINKLE_MAX * i;

    // The moon's glare at its least, none, which leaves its wash the whole of moon_fade.
    const F32 wash = 1.f - llmin(llmax(moon_power, 0.f) * (1.f - i) * MOON_WASH, MOON_FADE_MAX);

    // The bloom boost, or the albedo path's gain.
    const F32 gain = emissive ? 1.f + i * i * BLOOM_BOOST : 1.f + i;

    // starsF.glsl's shape at the star's centre: the core, the halo and both spikes.
    const F32 shape = 1.f + HALO + 2.f * SPIKE * spike;

    return jitter * twinkle * i * llmax(star_alpha, 0.f) * wash * fade * gain * shape;
}

U32 faintestVisible(F32 star_alpha, F32 moon_power, bool emissive)
{
    // brightest() rises with the intensity, so the first byte to reach LEFT_OUT is found by bisection; 256 if none.
    U32 low  = 0;
    U32 high = 256;
    while (low < high)
    {
        const U32 mid = (low + high) / 2;
        if (brightest((F32)mid / 255.f, star_alpha, moon_power, emissive) >= LEFT_OUT)
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    return low;
}

namespace
{
    // How many of the run of intensities from @a first, @a count long and brightest first, are at @a faintest or brighter.
    U32 drawnOf(const ALStarField::Layout& layout, U32 first, U32 count, U32 faintest)
    {
        const auto begin = layout.mIntensities.begin() + first;
        return (U32)(std::partition_point(begin, begin + count, [faintest](U8 intensity) { return intensity >= faintest; })
                     - begin);
    }
}

U32 quadsDrawn(const Layout& layout, U32 faintest)
{
    return drawnOf(layout, 0, layout.mQuads, faintest);
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

Frustum frustum(const LLMatrix4a& mvp, F32 margin_x, F32 margin_y)
{
    // The clip coordinates' x, y and w as products with a point of the stars' frame, through the lanes the matrix's
    // rows weight them by.
    const LLVector4a x = mvp.getColumn<0>();
    const LLVector4a y = mvp.getColumn<1>();
    const LLVector4a w = mvp.getColumn<3>();
    const auto plane = [&w](F32 margin, const LLVector4a& axis, F32 sign)
    {
        LLVector3 normal;
        for (S32 i = 0; i < 3; ++i)
        {
            normal.mV[i] = (1.f + margin) * w[i] + sign * axis[i];
        }
        normal.normalize();
        return normal;
    };

    Frustum frustum;
    frustum.mNormals[0] = plane(margin_x, x, 1.f);
    frustum.mNormals[1] = plane(margin_x, x, -1.f);
    frustum.mNormals[2] = plane(margin_y, y, 1.f);
    frustum.mNormals[3] = plane(margin_y, y, -1.f);
    return frustum;
}

Frustum pointFrustum(const LLMatrix4a& mvp, U32 width, U32 height)
{
    return frustum(mvp, 2.f * POINT_REACH / (F32)llmax(width, 1u), 2.f * POINT_REACH / (F32)llmax(height, 1u));
}

bool inView(const Frustum& frustum, const Cell& cell)
{
    // The cone lies wholly outside a plane through its apex where its axis makes more than 90 degrees and its
    // half-angle with the plane's normal; the slack keeps a star rounding onto the plane in view.
    constexpr F32 SLACK = 1e-4f;
    for (const LLVector3& normal : frustum.mNormals)
    {
        if (normal * cell.mAxis < -(cell.mSinRadius + SLACK))
        {
            return false;
        }
    }
    return true;
}

void pointDraws(const Layout& layout, const Frustum& frustum, U32 faintest, Draws& draws)
{
    draws.clear();
    for (const Cell& cell : layout.mCells)
    {
        if (cell.mCount == 0 || !inView(frustum, cell))
        {
            continue;
        }
        const U32 drawn = drawnOf(layout, cell.mFirst - layout.firstPoint() + layout.mQuads, cell.mCount, faintest);
        if (drawn == 0)
        {
            continue;
        }
        if (!draws.mFirst.empty() && (U32)(draws.mFirst.back() + draws.mCount.back()) == cell.mFirst)
        {
            draws.mCount.back() += (S32)drawn;
        }
        else
        {
            draws.mFirst.push_back((S32)cell.mFirst);
            draws.mCount.push_back((S32)drawn);
        }
    }
}

} // namespace ALStarField
