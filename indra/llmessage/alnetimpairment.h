/**
 * @file alnetimpairment.h
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

#ifndef AL_NETIMPAIRMENT_H
#define AL_NETIMPAIRMENT_H

#include "linden_common.h"

// Not present in the build that ships. Everything below compiles away in Release, so there is no
// path by which a download build can be talked into dropping its own packets.
#if !defined(LL_RELEASE_FOR_DOWNLOAD)
#define AL_NET_IMPAIRMENT 1
#endif

#if AL_NET_IMPAIRMENT

#include "llpacketbuffer.h"

#include <deque>
#include <utility>
#include <vector>

/**
 * Damages the inbound UDP stream on purpose, so that the viewer's recovery paths can be watched
 * on a link that is not actually broken.
 *
 * Several things in the motion path only run when updates go missing or arrive in the wrong
 * order: the prediction phase-out, the per-object out-of-order guard, the region-crossing
 * expiry. On a healthy connection none of them fire, which makes them both easy to get wrong
 * and impossible to demonstrate.
 *
 * Loss is modelled as a two-state Gilbert-Elliott process rather than an independent coin flip
 * per packet, because real loss arrives in runs -- a congested queue drops a burst, not one
 * packet in twenty spread evenly. The distinction matters here: uniform loss at 5% mostly just
 * thins a stream of updates, while the same 5% arriving in runs of ten is what actually strands
 * an object long enough for prediction to give up on it.
 */
class ALNetImpairment
{
public:
    /// Mean length of a loss burst, in packets, at which losses stop clustering.
    static constexpr F32 MIN_BURST_LENGTH = 1.f;

    /// Ceiling on packets held back for reordering, so a misconfiguration cannot grow unbounded.
    static constexpr size_t MAX_HELD_PACKETS = 64;

    // -- configuration, safe to change at any time ---------------------------------------------

    /// Average share of inbound packets to discard, 0-100. Zero disables loss entirely.
    void setLossPercent(F32 percent);

    /// Mean number of packets a loss burst runs for. 1 leaves losses unclustered; larger values
    /// concentrate the same average loss into longer outages.
    void setBurstLength(F32 packets);

    /// Share of surviving packets to deliver late, 0-100.
    void setReorderPercent(F32 percent);

    /// How many later packets a delayed packet is released behind.
    void setReorderDelay(S32 packets);

    bool isActive() const { return mLossPercent > 0.f || (mReorderPercent > 0.f && mReorderDelay > 0); }

    // -- per-packet, called in socket-arrival order --------------------------------------------

    /// Whether the packet that just arrived should be discarded.
    bool shouldDrop();

    /// Take a packet out of the stream to be delivered later. Returns true if it was taken, in
    /// which case the caller must not deliver it now -- it comes back from @ref advance.
    bool hold(const LLPacketBuffer& packet);

    /// Age the held packets by one arrival and collect any now due. Call once per arriving
    /// packet, and deliver what it returns *after* that arrival, which is what puts the held
    /// packet behind the ones that overtook it.
    void advance(std::vector<LLPacketBuffer>& released);

    /// Hand back everything still held, for when impairment is switched off or the circuit ends.
    void flush(std::vector<LLPacketBuffer>& released);

    // -- observation ---------------------------------------------------------------------------

    U32  getDroppedCount() const   { return mDroppedCount; }
    U32  getReorderedCount() const { return mReorderedCount; }
    bool isInLossBurst() const     { return mInLossBurst; }

private:
    /// Recompute the Gilbert-Elliott transition probabilities from the loss/burst settings.
    void recomputeTransitions();

    F32  mLossPercent   = 0.f;
    F32  mBurstLength   = MIN_BURST_LENGTH;
    F32  mEnterBurst    = 0.f;  ///< P(good -> bad) per packet
    F32  mLeaveBurst    = 1.f;  ///< P(bad -> good) per packet; its reciprocal is the burst length
    bool mInLossBurst   = false;

    F32  mReorderPercent = 0.f;
    S32  mReorderDelay   = 0;

    /// Packets waiting to be let back in, each with the number of arrivals still to wait.
    std::deque<std::pair<LLPacketBuffer, S32>> mHeld;

    U32  mDroppedCount   = 0;
    U32  mReorderedCount = 0;
};

#endif // AL_NET_IMPAIRMENT
#endif // AL_NETIMPAIRMENT_H
