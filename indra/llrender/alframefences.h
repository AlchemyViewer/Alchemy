/**
 * @file alframefences.h
 * @brief One fence a frame, and which frames the GPU has finished.
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

/// One fence a frame, laid by endFrame() after the frame's last command, and the frames it shows finished: a frame is
/// complete once every command issued in it has finished on the GPU. What reuses memory the GPU reads or writes notes
/// the frame it last used it in and reuses it once that frame is complete, laying no fence of its own: the query pool's
/// batches, the uniform buffer rings' segments, ALGPUReadback's slots and ALGPUStream's regions.
///
/// The fences are polled once a frame, by endFrame(), straight after the swap, on every driver, and nowhere else but a
/// wait. A threaded driver answers a fence laid or polled only once its own thread has caught up with it. Mesa's glthread
/// runs every call that returns a value so, and has caught up at the swap, since Mesa finishes glthread before it
/// flushes the frame. NVIDIA's threaded optimization answers a poll of a fence not yet signalled in order on its thread,
/// which a GPU-bound frame holds in the previous frame's present, so that a poll before the cull waits there for most
/// of a frame. Straight after the swap that wait is the swap's own.
///
/// All operations require the GL context the fences were laid in to be current. Non-copyable.
class ALFrameFences
{
public:
    /// The frames whose fences may be pending at once: endFrame() waits for the oldest past that.
    static constexpr U32 FRAMES = 8;

    ALFrameFences() = default;
    ALFrameFences(const ALFrameFences&) = delete;
    ALFrameFences& operator=(const ALFrameFences&) = delete;

    /// The open frame: a command issued now completes with it.
    U32 frame() const { return mFrame; }

    /// Whether every command issued in `frame` has finished, as far as the frames' ends have shown. No GL call. Frame
    /// 0, and every frame up to a release(), is complete.
    bool isComplete(U32 frame) const { return frame <= mComplete; }

    /// Waits up to `timeout_ns` for `frame` to complete, polling first: whether it has. For the open frame it lays a
    /// fence now and waits for every command issued so far. `stalled`, where given, says whether it had to wait. For a
    /// user that cannot go on without the frame, and for teardown and tests: in the middle of a frame a poll is the wait
    /// endFrame() keeps out of it.
    bool wait(U32 frame, U64 timeout_ns, bool* stalled = nullptr);

    /// Polls the fences of the frames ended before, oldest first, up to the first that has not signalled, then lays the
    /// open frame's fence and opens the next. Once a frame, after its last command: the viewer's is the swap. The frame
    /// it ends is shown finished by the next one's end at the earliest.
    void endFrame();

    /// Deletes every fence, while the context is current. Every frame so far, the open one too, is complete.
    void release();

    /// The waits that had to wait.
    U32 getStalls() const { return mStalls; }
    /// The fences laid and polls made, each a call that returns a value.
    U32 getCalls() const { return mCalls; }

private:
    // Polls the ended frames not yet shown complete, oldest first, up to the first that has not signalled.
    void poll();
    // Every frame up to `frame` is complete: their fences go.
    void completeThrough(U32 frame);

    void* mFences[FRAMES] = {};  // the GLsync laid at the end of each frame not yet shown complete, by frame % FRAMES
    bool  mFlushed[FRAMES] = {}; // its fence has been flushed toward the GPU
    U32   mFrame    = 1;         // the open frame
    U32   mComplete = 0;         // the newest frame shown complete, with every one before it
    U32   mStalls   = 0;
    U32   mCalls    = 0;
    U32   mFrameCalls = 0;       // calls since the last endFrame(), for the profiler
};
