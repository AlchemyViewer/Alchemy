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

/// The GPU time one span of draws takes. begin() and end() bracket the span with a GL_TIME_ELAPSED query, and poll()
/// hands the time back once the GPU has finished the span, without waiting for it unless asked to. One span at a time:
/// begin() refuses while the last span's time is still to be read, since beginning the query again would throw that
/// time away under whoever is polling for it. GL_TIME_ELAPSED queries do not nest, so no other may be open across the
/// span.
///
/// All operations require a current GL context. Non-copyable.
class ALGPUTimer
{
public:
    ALGPUTimer() = default;
    ~ALGPUTimer();

    ALGPUTimer(const ALGPUTimer&) = delete;
    ALGPUTimer& operator=(const ALGPUTimer&) = delete;

    /// Starts timing the draws that follow. False, starting nothing, while a span is open or its time is pending.
    bool begin();

    /// Ends the span begin() started, if it started one. Its time is then pending.
    void end();

    /// Whether a span has ended and its time has not been read.
    bool isPending() const { return mPending; }

    /// The pending span's time in nanoseconds, once the GPU has finished it: false while it has not, or when nothing is
    /// pending. `wait` waits for the GPU instead.
    bool poll(U64& nanoseconds, bool wait = false);

    /// Deletes the query, while the context it was made in is current. A pending time is dropped.
    void release();

    /// The query's name: 0 until the first begin(), and after release().
    U32 getQuery() const { return mQuery; }

private:
    U32  mQuery   = 0;
    bool mOpen    = false;
    bool mPending = false;
};
