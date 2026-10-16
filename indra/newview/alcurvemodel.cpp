/**
 * @file alcurvemodel.cpp
 * @brief Curve shapes for the curve editor widget and the tone curve bake
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

// =============================================================================
// Split-tone ramps
// =============================================================================

// static
ALCurveModel::SplitToneRamp ALCurveModel::splitToneRamp(F32 edge0, F32 width)
{
    // The reciprocal is guarded here and nowhere else, so the upload in
    // pipeline.cpp and the bands the Lightbox draws degenerate identically: a
    // zero width is a step at edge0, not a division by zero.
    const F32 w = llmax(width, SPLIT_TONE_MIN_WIDTH);
    SplitToneRamp ramp;
    ramp.mScale = 1.f / w;
    ramp.mBias  = -edge0 / w;
    return ramp;
}

// static
ALCurveModel::SplitToneRamp ALCurveModel::splitToneShadowRamp(F32 mid, F32 width)
{
    // Floor before forming the left edge, so edge0 + w lands on mid exactly.
    // Flooring inside splitToneRamp alone would leave the ramp ending short
    // of the split for a sub-floor width, and the shadow and highlight bands
    // would then overlap there.
    const F32 w = llmax(width, SPLIT_TONE_MIN_WIDTH);
    return splitToneRamp(mid - w, w);
}

// static
ALCurveModel::SplitToneRamp ALCurveModel::splitToneHighlightRamp(F32 mid, F32 width)
{
    return splitToneRamp(mid, width);
}

// static
F32 ALCurveModel::splitToneRampValue(F32 l, const SplitToneRamp& ramp)
{
    // cg_ramp, colorGradeUtilF.glsl.
    const F32 t = llclamp(l * ramp.mScale + ramp.mBias, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// static
ALCurveModel::SplitToneWeights ALCurveModel::splitToneWeights(F32 l, F32 mid, F32 shadow_width, F32 highlight_width)
{
    // applySplitToning, colorGradeUtilF.glsl.
    SplitToneWeights w;
    w.mHighlight = splitToneRampValue(l, splitToneHighlightRamp(mid, highlight_width));
    w.mShadow    = 1.f - splitToneRampValue(l, splitToneShadowRamp(mid, shadow_width));
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

// =============================================================================
// Control points
// =============================================================================

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
    if (getPointCount() >= MAX_POINTS)
    {
        return -1;
    }

    Point p;
    p.mX = llclamp(x, 0.f, 1.f);
    p.mY = llclamp(y, 0.f, 1.f);
    if (mEndpointsLocked)
    {
        // Strictly inside the pinned ends. A point at exactly 1 would sort
        // after the right end and then be the one pinned there, leaving the
        // old end interior and the curve ending on the new height.
        p.mX = llclamp(p.mX, MIN_POINT_GAP, 1.f - MIN_POINT_GAP);
    }

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

    // Cap first, keeping both ends: the last point is the one pinned to x = 1,
    // so dropping it would leave a locked curve that no longer spans the
    // domain. Only a caller feeding setPoints directly can reach this; the
    // LLSD path rejects an oversized list outright.
    if (mPoints.size() > static_cast<size_t>(MAX_STORED_POINTS))
    {
        mPoints.erase(mPoints.begin() + (MAX_STORED_POINTS - 1), mPoints.end() - 1);
    }

    // Push coincident points apart: left to right, then right to left for
    // whatever the first pass could only pile up against 1. A pinned end is
    // already at 1 before this runs, so a second point there has nowhere to
    // go on the first pass; the second moves it, and any run behind it, down
    // instead. Both passes keep the order the sort established, and
    // MAX_STORED_POINTS gaps fit in the domain many times over, so the clamps
    // at 0 and 1 never engage in practice.
    for (size_t i = 1; i < mPoints.size(); ++i)
    {
        const F32 floor_x = mPoints[i - 1].mX + MIN_POINT_GAP;
        if (mPoints[i].mX < floor_x)
        {
            mPoints[i].mX = llmin(floor_x, 1.f);
        }
    }
    if (mPoints.size() >= 2)
    {
        for (size_t i = mPoints.size() - 1; i-- > 0;)
        {
            const F32 ceil_x = mPoints[i + 1].mX - MIN_POINT_GAP;
            if (mPoints[i].mX > ceil_x)
            {
                mPoints[i].mX = llmax(ceil_x, 0.f);
            }
        }
    }
}

// =============================================================================
// Evaluation
// =============================================================================

F32 ALCurveModel::evaluate(F32 x) const
{
    x = llclamp(x, 0.f, 1.f);

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
    for (S32 i = 0; i < count; ++i)
    {
        // A division rather than a multiplied step, so the last sample sits
        // on exactly 1 and the ends of a baked row read back exactly.
        out.push_back(evaluate(static_cast<F32>(i) / static_cast<F32>(count - 1)));
    }
}

// =============================================================================
// ALToneCurveSet
// =============================================================================

// static
ALToneCurveSet::EChannel ALToneCurveSet::channelFromCombo(S32 combo_value)
{
    switch (combo_value)
    {
        case 0:  return CH_RED;
        case 1:  return CH_GREEN;
        case 2:  return CH_BLUE;
        default: return CH_MASTER;
    }
}

// static
const char* ALToneCurveSet::settingName(EChannel c)
{
    static const char* const NAMES[CH_COUNT] = {
        "RenderColorGradeCurveMaster",
        "RenderColorGradeCurveRed",
        "RenderColorGradeCurveGreen",
        "RenderColorGradeCurveBlue" };
    return NAMES[c];
}

namespace
{
bool isFiniteNumber(const LLSD& v, F32& out)
{
    // Reject by type rather than trusting asReal(): a string converts to 0
    // silently, and a map to 0 too, and either would plant a point at the
    // origin instead of being noticed.
    if (v.type() != LLSD::TypeReal && v.type() != LLSD::TypeInteger)
    {
        return false;
    }
    const F64 d = v.asReal();
    if (!std::isfinite(d))
    {
        return false;
    }
    out = static_cast<F32>(d);
    return true;
}
} // namespace

// static
bool ALToneCurveSet::pointsFromLLSD(const LLSD& sd, std::vector<ALCurveModel::Point>& out)
{
    out = { { 0.f, 0.f }, { 1.f, 1.f } };
    if (!sd.isArray())
    {
        return false;
    }

    std::vector<ALCurveModel::Point> points;
    for (LLSD::array_const_iterator it = sd.beginArray(); it != sd.endArray(); ++it)
    {
        const LLSD& pair = *it;
        if (!pair.isArray() || pair.size() < 2)
        {
            continue;
        }
        ALCurveModel::Point p;
        if (!isFiniteNumber(pair[0], p.mX) || !isFiniteNumber(pair[1], p.mY))
        {
            continue;
        }
        points.push_back(p);
    }

    if (points.size() < 2 || points.size() > static_cast<size_t>(ALCurveModel::MAX_STORED_POINTS))
    {
        // Too few is not a curve; too many is not a curve the editor could
        // have made, and truncating it would render something the file does
        // not describe. Identity is the honest answer for both.
        return false;
    }
    out = std::move(points);
    return true;
}

// static
LLSD ALToneCurveSet::pointsToLLSD(const std::vector<ALCurveModel::Point>& points)
{
    LLSD out = LLSD::emptyArray();
    for (const ALCurveModel::Point& p : points)
    {
        LLSD pair = LLSD::emptyArray();
        pair.append(LLSD::Real(p.mX));
        pair.append(LLSD::Real(p.mY));
        out.append(pair);
    }
    return out;
}

// static
LLSD ALToneCurveSet::identityLLSD()
{
    return pointsToLLSD({ { 0.f, 0.f }, { 1.f, 1.f } });
}

bool ALToneCurveSet::setCurveFromLLSD(EChannel c, const LLSD& sd)
{
    std::vector<ALCurveModel::Point> points;
    const bool ok = pointsFromLLSD(sd, points);
    mCurves[c].setPoints(std::move(points));
    return ok;
}

LLSD ALToneCurveSet::getCurveAsLLSD(EChannel c) const
{
    return pointsToLLSD(mCurves[c].getPoints());
}

F32 ALToneCurveSet::evaluate(EChannel c, F32 x) const
{
    if (c == CH_MASTER)
    {
        return mCurves[CH_MASTER].evaluate(x);
    }
    return mCurves[CH_MASTER].evaluate(mCurves[c].evaluate(x));
}

// static
bool ALToneCurveSet::isIdentityCurve(const ALCurveModel& curve)
{
    for (const ALCurveModel::Point& p : curve.getPoints())
    {
        if (fabsf(p.mY - p.mX) >= IDENTITY_EPS)
        {
            return false;
        }
    }
    return true;
}

bool ALToneCurveSet::isIdentity() const
{
    for (const ALCurveModel& curve : mCurves)
    {
        if (!isIdentityCurve(curve))
        {
            return false;
        }
    }
    return true;
}

void ALToneCurveSet::bake(std::vector<U16>& out, S32 size) const
{
    size = llmax(size, 2);
    out.assign(static_cast<size_t>(size) * 4, ALPHA_OPAQUE);
    for (S32 i = 0; i < size; ++i)
    {
        // Division, not a multiplied step: i / (size - 1) is exactly 1 for the
        // last texel, so the top of the row is the curve's value at 1 and not
        // at a float short of it.
        const F32 x = static_cast<F32>(i) / static_cast<F32>(size - 1);
        for (S32 c = 0; c < 3; ++c)
        {
            const F32 y = llclamp(evaluate(static_cast<EChannel>(CH_RED + c), x), 0.f, 1.f);
            out[static_cast<size_t>(i) * 4 + c] = static_cast<U16>(y * 65535.f + 0.5f);
        }
    }
}

// static
void ALToneCurveSet::lutScaleBias(S32 size, F32& scale, F32& bias)
{
    size = llmax(size, 2);
    scale = 1.f - 1.f / static_cast<F32>(size);
    bias  = 0.5f / static_cast<F32>(size);
}
