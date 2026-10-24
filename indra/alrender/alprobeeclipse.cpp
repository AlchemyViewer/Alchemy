/**
 * @file alprobeeclipse.cpp
 * @brief Whether a placed reflection probe has swallowed an automatic one, for every pair of a frame's at once.
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

#include "alprobeeclipse.h"

#include <algorithm>
#include <cmath>

namespace
{
    // A reach a little past the shape's, so that the reject, which rounds otherwise than the shape's own test, never
    // turns away a pair that test would take at its edge.
    F32 slack(F32 reach_squared)
    {
        return reach_squared * 1.0001f + 1e-4f;
    }
}

void ALProbeEclipse::clear(F32 max_margin)
{
    mEclipsers.clear();
    mMaxMargin = std::max(0.f, max_margin);
}

void ALProbeEclipse::addBox(const LLVector3& centre, const LLQuaternion& rotation, const LLVector3& half)
{
    Eclipser box;
    box.mCentre = centre;
    box.mBox    = true;
    box.mHalf   = half;
    // A centre's coordinate on the box's own axis i is its offset turned back by the rotation, which is the offset's
    // dot with the axis turned forward by it.
    for (U32 i = 0; i < 3; ++i)
    {
        LLVector3 axis;
        axis.mV[i] = 1.f;
        box.mAxes[i] = axis.rotVec(rotation);
    }
    // Swallowed, a centre lies within the box grown by the margin on every axis, and so within its half-diagonal.
    const LLVector3 grown(std::max(0.f, half.mV[0] + mMaxMargin), std::max(0.f, half.mV[1] + mMaxMargin),
                          std::max(0.f, half.mV[2] + mMaxMargin));
    box.mReachSquared = slack(grown.lengthSquared());
    mEclipsers.push_back(box);
}

void ALProbeEclipse::addSphere(const LLVector3& centre, F32 radius)
{
    Eclipser sphere;
    sphere.mCentre = centre;
    sphere.mInner  = radius * SPHERE_FULL_WEIGHT_FRACTION;
    const F32 reach = std::max(0.f, sphere.mInner + mMaxMargin);
    sphere.mReachSquared = slack(reach * reach);
    mEclipsers.push_back(sphere);
}

bool ALProbeEclipse::eclipses(const LLVector3& centre, F32 radius, F32 margin) const
{
    llassert(margin <= mMaxMargin);
    for (const Eclipser& eclipser : mEclipsers)
    {
        const LLVector3 delta = centre - eclipser.mCentre;
        if (delta.lengthSquared() > eclipser.mReachSquared)
        {
            continue;
        }
        if (eclipser.mBox)
        {
            bool inside = true;
            for (U32 i = 0; i < 3 && inside; ++i)
            {
                inside = std::fabs(delta * eclipser.mAxes[i]) + radius <= eclipser.mHalf.mV[i] + margin;
            }
            if (inside)
            {
                return true;
            }
        }
        else if (delta.length() + radius <= eclipser.mInner + margin)
        {
            return true;
        }
    }
    return false;
}
