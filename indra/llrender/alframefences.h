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

#include "algpubuffer.h"
#include "alpacingslack.h"
#include "stdtypes.h"

/// One fence a frame, laid by endFrame() after the frame's last command, and the frames it shows finished: a frame is
/// complete once every command issued in it has finished on the GPU. What reuses memory the GPU reads or writes notes
/// the frame it last used it in and reuses it once that frame is complete, laying no fence of its own: the query pool's
/// batches, the uniform buffer rings' segments, ALGPUReadback's slots and ALGPUStream's regions.
///
/// The fences are polled once a frame, by endFrame(), straight after the swap, on every driver, and nowhere else but a
/// wait. A threaded driver answers a poll of a fence not yet signalled only once its own thread has caught up with it;
/// NVIDIA's threaded optimization lays a fence, and answers a poll of one signalled, at once. Mesa's glthread
/// runs every call that returns a value so, and has caught up at the swap, since Mesa finishes glthread before it
/// flushes the frame. NVIDIA's threaded optimization answers a poll of a fence not yet signalled in order on its thread,
/// which a GPU-bound frame holds in the previous frame's present, so that a poll before the cull waits there for most
/// of a frame. Straight after the swap that wait is the swap's own.
///
/// Where they are asked to (setStamps(), which the viewer asks where the GPU writes the query results: NVIDIA's driver),
/// and the context can (canStamp()), the frames' ends lay no fence: each has the GPU stamp the frame instead, a
/// timestamp counted once every command before it has finished (glQueryCounter), written into a slot of a store mapped
/// for as long as it lives (glGetQueryBufferObjectui64v). A frame is complete once its slot holds another stamp than it
/// did before. Reading the map is no GL call, so neither a frame's poll nor its pacing wait waits on the driver's
/// thread: a poll of a fence not yet signalled, even at the swap, waits for that thread to run everything issued before
/// it, which leaves it idle while the CPU begins the next frame, and the frame as long as the two threads' work end to
/// end wherever the driver's thread is the slower. The pacing wait reads the slot until the stamp comes, sleeping
/// through what the last wait took but its end and yielding the rest. A frame whose stamp has not come in a second is
/// waited out on a fence laid behind it, and a stamp still missing after that sends the frames' ends back to fences.
/// The GPU writes a stamp only once the frame has finished, so its front end may wait there before it begins the next
/// frame.
///
/// The frame's end may also pace the CPU, waiting every frame for the frame a given number before the one it ends. A
/// poll waits only when it happens on a fence not yet signalled while the driver's thread is held in a present: on
/// NVIDIA's threaded driver, a GPU-bound CPU runs frames ahead until one frame's poll sheds the whole lead at once, two
/// frames quick and one slow. A wait every frame holds the lead to that number and spreads the wait over every frame.
/// GPU-bound, that wait is time the CPU could have spent on work: the frames' ends keep what it was (slack()), for the
/// work before the next one's to take a share of.
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

    /// Whether the context can stamp frames: query buffers written by name, timestamps and persistent maps.
    static bool canStamp();

    /// Whether the frames' ends are to stamp their frames rather than lay fences, from the next endFrame() on, where the
    /// context can. Off again, the frames stamped so far are waited out first, at the next endFrame().
    void setStamps(bool stamps) { mWantStamps = stamps; }
    /// Whether the frames' ends stamp their frames.
    bool isStamping() const { return mStampStore.isAllocated() && !mStampsFailed; }
    /// Whether the open frame's end stamps it: the frames' ends stamp, or are asked to from this one on and can. What a
    /// command issued now lays a fence of its own behind, where a stamp alone would be what shows it finished. Told no,
    /// the caller counts on the frame's end to lay a fence: that end lays one, whatever stamps are asked for since, and
    /// the stamps start with the next frame.
    bool willStamp() const
    {
        const bool stamps = isStamping() || (mWantStamps && !mStampsFailed && canStamp());
        mFenceCountedOn = mFenceCountedOn || !stamps;
        return stamps;
    }

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

    /// Waits for `frame` to complete however long the GPU takes, for a user that must reuse what the frame used: a slow
    /// GPU, however slow, is waited out. A wait that runs past LONG_WAIT_SECONDS is logged, once, so that a GPU that has
    /// hung shows in the log of the report the main loop's watchdog makes of it. `stalled` as for wait().
    void waitFinished(U32 frame, bool* stalled = nullptr);

    /// How long waitFinished() waits before it logs that it is waiting.
    static constexpr U32 LONG_WAIT_SECONDS = 10;

    /// Lays the open frame's fence, or stamps it; with `frames_ahead` above 0 waits for the frame that many before it to
    /// complete; then polls the fences of the frames ended before, oldest first, up to the first that has not signalled,
    /// and opens the next frame. Once a frame, after its last command: the viewer's is the swap. The frame it ends is
    /// shown finished by the next one's end at the earliest.
    ///
    /// `frames_ahead` is how many frames the GPU may still be working on when the CPU begins the next: with 1, the frame
    /// before the one ending has finished. That wait paces the CPU and is no stall: it has a zone and a count of its
    /// own, and is for the frames a user sees, never a capture's. 0 waits for none but the frame FRAMES before, whose
    /// fence must make room. Either way the frame's end is noted in slack().
    void endFrame(U32 frames_ahead = 0);

    /// Deletes every fence and the stamps' store, while the context is current. Every frame so far, the open one too, is
    /// complete, and the slack of the frames before is forgotten. Stamps asked for are made again at the next endFrame().
    void release();

    /// How long the paced frames' ends have waited for the GPU, and what work before the next one's wait may have of it.
    ALPacingSlack& slack() { return mSlack; }
    const ALPacingSlack& slack() const { return mSlack; }

    /// The waits that had to wait, the frames' ends' pacing waits apart.
    U32 getStalls() const { return mStalls; }
    /// The frames' ends whose pacing wait had to wait for the GPU.
    U32 getPacingWaits() const { return mPacingWaits; }
    /// The fences laid and polls made, each a call that returns a value, and the fence and waits a stamp that has not
    /// come makes. Reading a stamp is none.
    U32 getCalls() const { return mCalls; }

private:
    // Polls the ended frames not yet shown complete, oldest first, up to the first that has not signalled.
    void poll();
    // Waits for the ended `frame` as a frame's end paces the CPU, however long the GPU takes: on that frame's fence, or
    // its stamp, alone, with no poll first, so that what the wait costs is the pacing zone's. The seconds it took;
    // `behind` says whether the GPU was still drawing it.
    F64 pace(U32 frame, bool& behind);
    // Every frame up to `frame` is complete: their fences go.
    void completeThrough(U32 frame);

    // Whether the frame's end is to stamp the open frame: makes the store and the queries the first time, and lets
    // them go, the frames stamped so far waited out, once stamps are no longer asked for or have failed.
    bool readyStamps();
    void releaseStamps();
    // Asks the GPU for the stamp of the frame in `slot`, behind every command issued so far.
    void stamp(U32 slot);
    // Whether the GPU has written the stamp in `slot`.
    bool stampWritten(U32 slot) const;
    // Waits up to `timeout` seconds for the stamp in `slot`: whether it came. Sleeps through all but the last of
    // `expect` seconds, and past them, and spins between.
    bool waitStamp(U32 slot, F64 timeout, F64 expect);
    // A stamp has not come in a second: a fence laid behind every command so far, waited on until it signals, after
    // which every stamp asked for has come and every ended frame is complete. One still missing sends the frames' ends
    // back to fences.
    void finishStamps();

    void* mFences[FRAMES] = {};  // the GLsync laid at the end of each frame not yet shown complete, by frame % FRAMES
    bool  mFlushed[FRAMES] = {}; // its fence, or its stamp, has been flushed toward the GPU
    U32   mFrame    = 1;         // the open frame
    U32   mComplete = 0;         // the newest frame shown complete, with every one before it
    U32   mStalls   = 0;
    U32   mPacingWaits = 0;
    U32   mCalls    = 0;
    U32   mFrameCalls = 0;       // calls since the last endFrame(), for the profiler
    ALPacingSlack mSlack;

    ALGPUBuffer mStampStore;                  // a stamp a slot, mapped for reading
    U32         mStampQueries[FRAMES] = {};   // the timestamp query each slot's stamp is counted by
    U64         mStampBefore[FRAMES] = {};    // what the slot held when its frame's stamp was asked for
    bool        mStamped[FRAMES] = {};        // the frame in the slot was stamped and is not yet shown complete
    F64         mStampPace = 0.0;             // how long the last pacing wait for a stamp took, in seconds
    bool        mWantStamps = false;
    bool        mStampsFailed = false;        // a stamp did not come once the GPU had finished: fences for good
    mutable bool mFenceCountedOn = false;     // willStamp() told the open frame's commands it lays a fence
};
