/**
 * @file alcurvemodel.cpp
 * @brief Curve shapes for the curve editor widget
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

#include "linden_common.h"

#include "alcurvemodel.h"

#include "llmath.h"

#include <algorithm>
#include <cmath>

// static
F32 ALCurveModel::smoothstep(F32 x, F32 toe, F32 shoulder, F32 strength)
{
    // cg_sCurve, colorGradeUtilF.glsl. The reciprocal is guarded the same way
    // pipeline.cpp guards it when it uploads uCurveInvRange, so a shoulder at
    // or below the toe degenerates identically on both sides: the ramp becomes
    // a step at the toe rather than a division by zero.
    const F32 inv_range = 1.f / llmax(shoulder - toe, 1e-4f);
    const F32 t = llclamp((x - toe) * inv_range, 0.f, 1.f);
    const F32 s = t * t * (3.f - 2.f * t);
    const F32 k = llclamp(strength, 0.f, 1.f);
    return x + (s - x) * k;
}

namespace
{
/// GLSL's smoothstep: the clamped Hermite, edges given rather than derived.
/// Guarded against a zero-width pair, which GLSL leaves undefined; ours are
/// always SPLIT_TONE_HALF_WIDTH apart, so the guard never engages in practice
/// and exists so a future caller cannot make it divide by zero.
F32 hermiteStep(F32 edge0, F32 edge1, F32 x)
{
    const F32 t = llclamp((x - edge0) / llmax(edge1 - edge0, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
} // namespace

// static
ALCurveModel::SplitToneWeights ALCurveModel::splitToneWeights(F32 l, F32 mid)
{
    // applySplitToning, colorGradeUtilF.glsl.
    SplitToneWeights w;
    w.mHighlight = hermiteStep(mid, mid + SPLIT_TONE_HALF_WIDTH, l);
    w.mShadow    = 1.f - hermiteStep(mid - SPLIT_TONE_HALF_WIDTH, mid, l);
    w.mMidtone   = llmax(1.f - w.mHighlight - w.mShadow, 0.f);
    return w;
}

// static
F32 ALCurveModel::splitToneMid(F32 balance)
{
    return 0.5f + llclamp(balance, -1.f, 1.f) * 0.4f;
}

// static
F32 ALCurveModel::splitToneBalance(F32 mid)
{
    return llclamp((mid - 0.5f) / 0.4f, -1.f, 1.f);
}

void ALCurveModel::setSmoothstep(F32 toe, F32 shoulder, F32 strength)
{
    mToe = toe;
    mShoulder = shoulder;
    mStrength = strength;
}

void ALCurveModel::setPoints(std::vector<Point> points)
{
    if (points.size() < 2)
    {
        // A curve needs two ends. Anything shorter is treated as "reset".
        mPoints = { { 0.f, 0.f }, { 1.f, 1.f } };
        return;
    }
    mPoints = std::move(points);
    normalize();
}

S32 ALCurveModel::addPoint(F32 x, F32 y)
{
    Point p;
    p.mX = llclamp(x, 0.f, 1.f);
    p.mY = llclamp(y, 0.f, 1.f);

    auto it = std::upper_bound(mPoints.begin(), mPoints.end(), p,
                               [](const Point& a, const Point& b) { return a.mX < b.mX; });
    const S32 index = static_cast<S32>(it - mPoints.begin());
    mPoints.insert(it, p);
    normalize();
    return index;
}

bool ALCurveModel::removePoint(S32 index)
{
    if (index < 0 || index >= getPointCount() || getPointCount() <= 2)
    {
        return false;
    }
    if (mEndpointsLocked && (index == 0 || index == getPointCount() - 1))
    {
        return false;
    }
    mPoints.erase(mPoints.begin() + index);
    return true;
}

S32 ALCurveModel::movePoint(S32 index, F32 x, F32 y)
{
    if (index < 0 || index >= getPointCount())
    {
        return index;
    }

    const S32 last = getPointCount() - 1;
    F32 lo = 0.f;
    F32 hi = 1.f;
    if (index > 0)
    {
        lo = mPoints[index - 1].mX + MIN_POINT_GAP;
    }
    if (index < last)
    {
        hi = mPoints[index + 1].mX - MIN_POINT_GAP;
    }

    if (mEndpointsLocked && (index == 0 || index == last))
    {
        // Pinned horizontally: the ends define the domain, so only their
        // height is the user's to set.
        x = (index == 0) ? 0.f : 1.f;
    }
    else if (lo > hi)
    {
        // Neighbours are already as close as MIN_POINT_GAP allows; there is
        // nowhere legal to go, so hold position.
        x = mPoints[index].mX;
    }
    else
    {
        x = llclamp(x, lo, hi);
    }

    mPoints[index].mX = x;
    mPoints[index].mY = llclamp(y, 0.f, 1.f);
    return index;
}

void ALCurveModel::setEndpointsLocked(bool locked)
{
    mEndpointsLocked = locked;
    if (locked && getPointCount() >= 2)
    {
        mPoints.front().mX = 0.f;
        mPoints.back().mX = 1.f;
        normalize();
    }
}

void ALCurveModel::normalize()
{
    std::stable_sort(mPoints.begin(), mPoints.end(),
                     [](const Point& a, const Point& b) { return a.mX < b.mX; });

    for (Point& p : mPoints)
    {
        p.mX = llclamp(p.mX, 0.f, 1.f);
        p.mY = llclamp(p.mY, 0.f, 1.f);
    }

    if (mEndpointsLocked && mPoints.size() >= 2)
    {
        mPoints.front().mX = 0.f;
        mPoints.back().mX = 1.f;
    }

    // Push coincident points apart from the left. Walking left to right keeps
    // the ordering the sort established; the trailing clamp to 1 can only
    // matter if a caller supplied more points than 1/MIN_POINT_GAP allows, in
    // which case the tail collapses onto 1 and evaluate() still terminates.
    for (size_t i = 1; i < mPoints.size(); ++i)
    {
        const F32 floor_x = mPoints[i - 1].mX + MIN_POINT_GAP;
        if (mPoints[i].mX < floor_x)
        {
            mPoints[i].mX = llmin(floor_x, 1.f);
        }
    }
}

F32 ALCurveModel::evaluate(F32 x) const
{
    x = llclamp(x, 0.f, 1.f);

    if (mKind == KIND_SMOOTHSTEP)
    {
        return smoothstep(x, mToe, mShoulder, mStrength);
    }

    const size_t n = mPoints.size();
    if (n == 0)
    {
        return x;
    }
    if (n == 1)
    {
        return mPoints[0].mY;
    }
    if (x <= mPoints.front().mX)
    {
        return mPoints.front().mY;
    }
    if (x >= mPoints.back().mX)
    {
        return mPoints.back().mY;
    }

    // Locate the segment. Point counts here are small (a tone curve is single
    // digits), so a linear scan beats the constant factor of a binary search.
    size_t i = 0;
    while (i + 2 < n && mPoints[i + 1].mX <= x)
    {
        ++i;
    }

    const F32 h = mPoints[i + 1].mX - mPoints[i].mX;
    if (h <= 0.f)
    {
        return mPoints[i + 1].mY;
    }
    const F32 delta = (mPoints[i + 1].mY - mPoints[i].mY) / h;

    // Fritsch-Carlson tangents for this segment's two ends. Computed locally
    // rather than cached because the point list is tiny and a cache would be
    // one more thing to invalidate on every drag tick.
    auto secant = [this, n](size_t k) -> F32
    {
        if (k + 1 >= n)
        {
            return 0.f;
        }
        const F32 dx = mPoints[k + 1].mX - mPoints[k].mX;
        return (dx > 0.f) ? (mPoints[k + 1].mY - mPoints[k].mY) / dx : 0.f;
    };

    auto tangent = [&](size_t k) -> F32
    {
        if (k == 0)
        {
            return secant(0);
        }
        if (k == n - 1)
        {
            return secant(n - 2);
        }
        const F32 d_prev = secant(k - 1);
        const F32 d_next = secant(k);
        // A local extremum must stay one: averaging across a sign change is
        // exactly what produces the overshoot this spline exists to avoid.
        if (d_prev * d_next <= 0.f)
        {
            return 0.f;
        }
        return 0.5f * (d_prev + d_next);
    };

    F32 m0 = tangent(i);
    F32 m1 = tangent(i + 1);

    if (delta == 0.f)
    {
        m0 = 0.f;
        m1 = 0.f;
    }
    else
    {
        // Keep (m0, m1) inside the circle of radius 3 in units of delta; past
        // it the Hermite segment is no longer monotone.
        const F32 alpha = m0 / delta;
        const F32 beta = m1 / delta;
        const F32 sum_sq = alpha * alpha + beta * beta;
        if (sum_sq > 9.f)
        {
            const F32 tau = 3.f / sqrtf(sum_sq);
            m0 = tau * alpha * delta;
            m1 = tau * beta * delta;
        }
    }

    const F32 t = (x - mPoints[i].mX) / h;
    const F32 t2 = t * t;
    const F32 t3 = t2 * t;
    const F32 h00 = 2.f * t3 - 3.f * t2 + 1.f;
    const F32 h10 = t3 - 2.f * t2 + t;
    const F32 h01 = -2.f * t3 + 3.f * t2;
    const F32 h11 = t3 - t2;

    const F32 y = h00 * mPoints[i].mY + h10 * h * m0
                + h01 * mPoints[i + 1].mY + h11 * h * m1;
    return llclamp(y, 0.f, 1.f);
}

void ALCurveModel::sample(std::vector<F32>& out, S32 count) const
{
    out.clear();
    if (count < 2)
    {
        return;
    }
    out.reserve(count);
    const F32 step = 1.f / static_cast<F32>(count - 1);
    for (S32 i = 0; i < count; ++i)
    {
        out.push_back(evaluate(static_cast<F32>(i) * step));
    }
}
