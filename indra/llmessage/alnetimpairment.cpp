/**
 * @file alnetimpairment.cpp
 * @brief Synthetic packet loss and reordering, for exercising code paths that a
 *        healthy connection never reaches
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

#include "alnetimpairment.h"

#if AL_NET_IMPAIRMENT

#include "llrand.h"

void ALNetImpairment::setLossPercent(F32 percent)
{
    // 100% loss would be a disconnection rather than an impairment, and it makes the transition
    // probability below divide by zero. Leave a sliver of a working link.
    const F32 clamped = llclamp(percent, 0.f, 99.f);
    if (clamped != mLossPercent)
    {
        mLossPercent = clamped;
        if (mLossPercent <= 0.f)
        {
            mInLossBurst = false;
        }
        recomputeTransitions();
    }
}

void ALNetImpairment::setBurstLength(F32 packets)
{
    const F32 clamped = llmax(packets, MIN_BURST_LENGTH);
    if (clamped != mBurstLength)
    {
        mBurstLength = clamped;
        recomputeTransitions();
    }
}

void ALNetImpairment::setReorderPercent(F32 percent)
{
    mReorderPercent = llclamp(percent, 0.f, 100.f);
}

void ALNetImpairment::setReorderDelay(S32 packets)
{
    mReorderDelay = llmax(packets, 0);
}

void ALNetImpairment::recomputeTransitions()
{
    // Two states: one that loses nothing, one that loses everything. Leaving the lossy state with
    // probability 1/B gives runs averaging B packets. Choosing the entry probability so that the
    // chain spends a fraction L of its time in the lossy state then makes the long-run loss rate
    // L regardless of how that loss is clustered -- which is the point, since it lets burst
    // length be varied without also changing how much traffic goes missing.
    const F32 loss = mLossPercent * 0.01f;

    mLeaveBurst = 1.f / mBurstLength;
    mEnterBurst = (loss > 0.f) ? (loss * mLeaveBurst) / (1.f - loss) : 0.f;
}

bool ALNetImpairment::shouldDrop()
{
    if (mLossPercent <= 0.f)
    {
        return false;
    }

    // Advance the chain first, then let the state it landed in decide this packet's fate.
    if (mInLossBurst)
    {
        if (ll_frand() < mLeaveBurst)
        {
            mInLossBurst = false;
        }
    }
    else if (ll_frand() < mEnterBurst)
    {
        mInLossBurst = true;
    }

    if (mInLossBurst)
    {
        ++mDroppedCount;
        return true;
    }
    return false;
}

bool ALNetImpairment::hold(const LLPacketBuffer& packet)
{
    if (mReorderPercent <= 0.f || mReorderDelay <= 0)
    {
        return false;
    }

    // Holding more than the ceiling would start to look like added latency rather than
    // reordering, so past that point packets travel straight through.
    if (mHeld.size() >= MAX_HELD_PACKETS)
    {
        return false;
    }

    if (ll_frand(100.f) >= mReorderPercent)
    {
        return false;
    }

    mHeld.emplace_back(packet, mReorderDelay);
    ++mReorderedCount;
    return true;
}

void ALNetImpairment::advance(std::vector<LLPacketBuffer>& released)
{
    for (auto it = mHeld.begin(); it != mHeld.end(); )
    {
        if (--(it->second) <= 0)
        {
            released.push_back(it->first);
            it = mHeld.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ALNetImpairment::flush(std::vector<LLPacketBuffer>& released)
{
    for (auto& entry : mHeld)
    {
        released.push_back(entry.first);
    }
    mHeld.clear();
}

#endif // AL_NET_IMPAIRMENT
