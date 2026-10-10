/**
 * @file alhoverglow.h
 * @brief The glow of what the pointer is over, fading in and out.
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
#include "llsingleton.h"
#include "lluuid.h"

#include <algorithm>
#include <vector>

class ALSelectionOutline;

/// The glow of what the pointer is over (RenderHoverGlowEnable): the world object LLToolPie's hover names as one a
/// click does something to, once the name has held for SETTLE_SECONDS, which fades in over RenderHighlightFadeTime
/// while it glows, while the one before it fades out. ALSelectionOutline draws each at PRIORITY_HOVER, its colour's alpha scaled by its fade, in
/// the world's call of LLSelectMgr::renderSilhouettes, which advances the fades first.
///
/// Objects are held by id and looked up as they are drawn, so a glow keeps nothing alive: one whose object is gone
/// is dropped.
class ALHoverGlow : public LLSingleton<ALHoverGlow>
{
    LLSINGLETON_EMPTY_CTOR(ALHoverGlow);

public:
    struct Glow
    {
        LLUUID mID;
        /// 0 to 1: how far it has faded in.
        F32 mFade = 0.f;
    };

    /// How long the pointer rests on a name, or on none, before the glow follows it: a sweep across objects lights
    /// none of them, and a frame or two off one, at its edge or a seam between its prims, does not put it out.
    static constexpr F64 SETTLE_SECONDS = 0.15;

    /// Names `id` as what the pointer is over in frame `frame` (LLFrameTimer::getFrameCount), or nothing with a null
    /// id. A name lapses once a frame has passed without it being named again: hover stops being handled when the
    /// pointer is over a floater, leaves the window or another tool takes it.
    void hover(const LLUUID& id, U32 frame)
    {
        mHovered = id;
        mHoveredFrame = frame;
    }

    /// Advances the fades to `now`, in seconds, in frame `frame`, over `fade_time` seconds each way: the glowing
    /// object's in, every other's out. What glows follows the name, or its lapse, once it has held for
    /// SETTLE_SECONDS. Dropped are the ones faded out and the ones whose object `alive(id)` says is gone. Called more
    /// than once in a frame, it moves nothing the second time.
    template <typename Alive>
    void update(F64 now, U32 frame, F32 fade_time, Alive&& alive);

    /// What glows, in the order it was first named.
    const std::vector<Glow>& glows() const { return mGlows; }

    /// The object named in `frame`, or null where its name has lapsed.
    LLUUID hovered(U32 frame) const { return (frame - mHoveredFrame <= 1) ? mHovered : LLUUID::null; }

    /// A fade `fade` moved `seconds` toward 1 when `in`, toward 0 otherwise, at a whole fade each `fade_time`
    /// seconds, held to 0 to 1; at once where fade_time is not above 0 or not a number.
    static F32 stepFade(F32 fade, F32 seconds, F32 fade_time, bool in);

    /// Adds what glows to `outline`, in RenderHighlightColor, for the world's call.
    void addTo(ALSelectionOutline& outline) const;

private:
    LLUUID mHovered;
    U32 mHoveredFrame = 0;
    // The name the pointer has been on since mPendingSince, and the one the glow follows, which becomes it once it
    // has settled.
    LLUUID mPending;
    F64 mPendingSince = 0.0;
    LLUUID mTarget;
    // When the fades were last moved; none yet before the first update.
    F64 mLastTime = 0.0;
    bool mTimed = false;
    std::vector<Glow> mGlows;
};

inline F32 ALHoverGlow::stepFade(F32 fade, F32 seconds, F32 fade_time, bool in)
{
    // Asked outright: /fp:fast may answer a comparison with a NaN either way.
    const F32 step = (llisnan(fade_time) || !(fade_time > 0.f)) ? 1.f : llmax(seconds, 0.f) / fade_time;
    return llclamp(fade + (in ? step : -step), 0.f, 1.f);
}

template <typename Alive>
void ALHoverGlow::update(F64 now, U32 frame, F32 fade_time, Alive&& alive)
{
    const F32 seconds = mTimed ? (F32)llmax(now - mLastTime, 0.0) : 0.f;
    mLastTime = now;
    mTimed = true;

    const LLUUID named = hovered(frame);
    if (named != mPending)
    {
        mPending = named;
        mPendingSince = now;
    }
    if (mPending != mTarget && now - mPendingSince >= SETTLE_SECONDS && (mPending.isNull() || alive(mPending)))
    {
        mTarget = mPending;
    }
    if (mTarget.notNull() && !alive(mTarget))
    {
        mTarget.setNull();
    }

    const LLUUID& target = mTarget;
    if (target.notNull() &&
        std::none_of(mGlows.begin(), mGlows.end(), [&target](const Glow& glow) { return glow.mID == target; }))
    {
        // It starts at nothing and takes this frame's step at once, as the ones fading out do.
        mGlows.push_back({ target, 0.f });
    }

    for (Glow& glow : mGlows)
    {
        glow.mFade = stepFade(glow.mFade, seconds, fade_time, glow.mID == target);
    }
    mGlows.erase(std::remove_if(mGlows.begin(), mGlows.end(),
                                [&](const Glow& glow)
                                { return (glow.mID != target && glow.mFade <= 0.f) || !alive(glow.mID); }),
                 mGlows.end());
}
