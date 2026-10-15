/**
 * @file algputimer.h
 * @brief The GPU time a span of draws takes, read back without waiting on the GPU.
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

#include "stdtypes.h"
#include "alquerypool.h"

/// The GPU time one span of draws takes, as a span of a query pool's. begin() and end() bracket the span, and poll()
/// hands the time back once the pool has read the span's frame, without waiting for it unless asked to. One span at a
/// time: begin() refuses while the last span's time is still to be read, since a second span would leave whoever is
/// polling for the first reading the second.
///
/// The pool's queries need its GL context current. Non-copyable.
class ALGPUTimer
{
public:
    /// Spans from `pool`, or from gGL's where it is null.
    explicit ALGPUTimer(ALQueryPool* pool = nullptr) : mPool(pool) {}

    ALGPUTimer(const ALGPUTimer&) = delete;
    ALGPUTimer& operator=(const ALGPUTimer&) = delete;

    /// Starts timing the draws that follow. False, starting nothing, while a span is open or its time is pending, or
    /// where the pool refuses the span.
    bool begin();

    /// Ends the span begin() started, if it started one. Its time is then pending.
    void end();

    /// Whether a span has ended and its time has not been read.
    bool isPending() const { return mPending; }

    /// The pending span's time in nanoseconds, once its frame has been read: false while it has not, when nothing is
    /// pending, or when the time expired unread. `wait` waits for the GPU instead.
    bool poll(U64& nanoseconds, bool wait = false);

private:
    ALQueryPool& pool() const;

    ALQueryPool* mPool;
    ALQuerySpan  mSpan;
    bool         mOpen    = false;
    bool         mPending = false;
};
