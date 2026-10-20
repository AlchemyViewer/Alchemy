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
    const auto points = std::stable_partition(stars.begin(), stars.end(), spiked);

    Layout layout;
    layout.mQuads  = (U32)(points - stars.begin());
    layout.mPoints = (U32)(stars.end() - points);

    // The points by cell.
    std::vector<U32> cell(layout.mPoints);
    std::vector<U32> order(layout.mPoints);
    for (U32 p = 0; p < layout.mPoints; ++p)
    {
        cell[p]  = cellOf(points[p].mPosition);
        order[p] = p;
    }
    std::stable_sort(order.begin(), order.end(), [&cell](U32 a, U32 b) { return cell[a] < cell[b]; });
    std::vector<Star> sorted;
    sorted.reserve(layout.mPoints);
    for (U32 p : order)
    {
        sorted.push_back(points[p]);
    }
    std::copy(sorted.begin(), sorted.end(), points);

    // Each cell's run, and the cone its stars' directions lie in: about their mean, as wide as the farthest.
    layout.mCells.resize(CELLS);
    U32 first = 0;
    for (U32 c = 0; c < CELLS; ++c)
    {
        Cell& run = layout.mCells[c];
        run.mFirst = layout.firstPoint() + first;
        while (first + run.mCount < layout.mPoints && cell[order[first + run.mCount]] == c)
        {
            ++run.mCount;
        }

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
        first += run.mCount;
    }
    llassert(first == layout.mPoints);
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

void pointDraws(const Layout& layout, const Frustum& frustum, Draws& draws)
{
    draws.clear();
    for (const Cell& cell : layout.mCells)
    {
        if (cell.mCount == 0 || !inView(frustum, cell))
        {
            continue;
        }
        if (!draws.mFirst.empty() && (U32)(draws.mFirst.back() + draws.mCount.back()) == cell.mFirst)
        {
            draws.mCount.back() += (S32)cell.mCount;
        }
        else
        {
            draws.mFirst.push_back((S32)cell.mFirst);
            draws.mCount.push_back((S32)cell.mCount);
        }
    }
}

} // namespace ALStarField
