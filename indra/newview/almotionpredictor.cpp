/**
 * @file almotionpredictor.cpp
 * @brief The arithmetic behind viewer-side prediction of object motion between
 *        simulator updates
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

#include "almotionpredictor.h"

#include "llmath.h"

void ALMotionPredictor::noteUpdate(F64 now_seconds)
{
    if (mLastUpdate > 0.0)
    {
        const F32 gap = (F32)(now_seconds - mLastUpdate);
        if (gap >= MIN_CREDIBLE_INTERVAL && gap <= MAX_CREDIBLE_INTERVAL)
        {
            mUpdateInterval = (mUpdateInterval > 0.f)
                ? lerp(mUpdateInterval, gap, CADENCE_SMOOTHING)
                : gap;
        }
    }

    mLastUpdate = now_seconds;
}

void ALMotionPredictor::forgetCadence()
{
    mLastUpdate     = 0.0;
    mUpdateInterval = 0.f;
}

ALMotionPredictor::Window ALMotionPredictor::phaseOutWindow(const Tuning& tuning) const
{
    Window window;

    // Either bound switched off means prediction is never tapered, which is how the viewer
    // behaves with the interpolation times set to zero.
    if (tuning.mMaxTime <= 0.f || tuning.mPhaseOutTime <= 0.f)
    {
        return window;
    }

    if (!tuning.mCadenceAware)
    {
        window.mStart = tuning.mPhaseOutTime;
        window.mEnd   = llmax(tuning.mMaxTime, tuning.mPhaseOutTime);
        return window;
    }

    // One update is a timestamp, not a rate. Without a second one there is nothing to call late.
    if (!hasCadence())
    {
        return window;
    }

    // Scale the wait with how often this object actually speaks, so a 1 Hz scripted mover is not
    // stopped for a silence that is normal for it, and a 10 Hz avatar is not left coasting for
    // three seconds after its stream dies. The cap keeps a very slow mover bounded regardless.
    const F32 taper = tuning.mMaxTime - tuning.mPhaseOutTime;
    const F32 ceiling = llmax(tuning.mCadenceCap, tuning.mPhaseOutTime);

    window.mStart = llclamp(mUpdateInterval * tuning.mCadenceFactor, tuning.mPhaseOutTime, ceiling);
    window.mEnd   = llmax(tuning.mMaxTime, window.mStart + llmax(taper, 0.f));

    return window;
}

// static
F32 ALMotionPredictor::phaseOutFactor(F32 time_since_update, const Window& window)
{
    if (!window.isTapering() || time_since_update <= window.mStart)
    {
        return 1.f;
    }

    if (time_since_update >= window.mEnd)
    {
        return 0.f;
    }

    return (window.mEnd - time_since_update) / (window.mEnd - window.mStart);
}

// static
F32 ALMotionPredictor::clampFrameStep(F32 dt, const Tuning& tuning)
{
    if (dt <= 0.f)
    {
        return 0.f;
    }

    return (tuning.mMaxFrameStep > 0.f) ? llmin(dt, tuning.mMaxFrameStep) : dt;
}

// static
LLVector3 ALMotionPredictor::finalVelocity(const LLVector3& reported, const LLVector3& accel)
{
    return reported + accel * (SIGN_AVERAGE_TO_FINAL * 0.5f * SIM_TIMESTEP);
}

// static
LLVector3 ALMotionPredictor::positionDelta(const LLVector3& vel, const LLVector3& accel, F32 dt)
{
    return vel * dt + accel * (0.5f * dt * dt);
}
