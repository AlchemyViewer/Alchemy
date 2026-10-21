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
/// Laying a fence and polling one are calls that return a value, and Mesa's glthread answers each such call only once
/// its thread has run every command queued before it (gGLManager.mMesaGLThread): in the middle of a frame the main
/// thread waits there for whatever that thread still has to do. At the swap it has caught up already -- Mesa finishes
/// glthread before it flushes the frame -- so a fence laid and polled straight after waits for nothing. Under glthread,
/// then, endFrame() polls and poll() does nothing; elsewhere a poll costs a few microseconds, and poll() polls as late
/// in the frame as its caller asks, which finds frames finished that the swap could not have.
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

    /// Whether every command issued in `frame` has finished, as far as the polls so far have shown. No GL call. Frame
    /// 0, and every frame up to a release(), is complete.
    bool isComplete(U32 frame) const { return frame <= mComplete; }

    /// Polls the fences of the frames not yet shown complete, oldest first, up to the first that has not signalled.
    /// Nothing under glthread, where endFrame() polls instead.
    void poll();

    /// Waits up to `timeout_ns` for `frame` to complete, polling first: whether it has. For the open frame it lays a
    /// fence now and waits for every command issued so far. `stalled`, where given, says whether it had to wait.
    bool wait(U32 frame, U64 timeout_ns, bool* stalled = nullptr);

    /// Lays the open frame's fence and opens the next. Once a frame, after its last command: the viewer's is the swap.
    void endFrame();

    /// Deletes every fence, while the context is current. Every frame so far, the open one too, is complete.
    void release();

    /// The waits that had to wait.
    U32 getStalls() const { return mStalls; }
    /// The fences laid and polls made, each a call that returns a value.
    U32 getCalls() const { return mCalls; }

private:
    // Polls the frames not yet shown complete, whatever the context.
    void pollPending();
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
