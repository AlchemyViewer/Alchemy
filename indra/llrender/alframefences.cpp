/**
 * @file alframefences.cpp
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

#include "linden_common.h"

#include "alframefences.h"

#include "alprecisewaiter.h"
#include "llgl.h"
#include "llglheaders.h"
#include "lltimer.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>

namespace
{
    // How long one try of waitFinished(), or of the pacing wait, lasts.
    constexpr U64 WAIT_NANOSECONDS = 1000000000ull;

    // How long a wait for a stamp tries before a fence behind it waits it out, and how close to the end of what it
    // expects, either side, a wait for a stamp spins: the spin ends within microseconds of the write, as
    // glClientWaitSync does, where a stamp a frame quicker or slower than the last would fall to a sleep's step.
    constexpr F64 STAMP_WAIT_SECONDS = 1.0;
    constexpr F64 STAMP_SPIN_SECONDS = 0.002;
    // How long a wait for a stamp sleeps at a time: a millisecond while what it expects is far off, so that a stamp
    // that comes early is seen within one, and a quarter of one once it has spun past what it expected, so that it
    // wakes soon after a stamp that comes late.
    constexpr F64 STAMP_SLEEP_SECONDS      = 0.001;
    constexpr F64 STAMP_LATE_SLEEP_SECONDS = 0.00025;

    bool signalled(GLenum status)
    {
        // A wait that failed has no fence left to wait for: the context went, or the fence was never made.
        if (status == GL_WAIT_FAILED)
        {
            LL_WARNS_ONCE("FrameFences") << "A frame fence could not be waited on; taking its frame as finished" << LL_ENDL;
        }
        return status != GL_TIMEOUT_EXPIRED;
    }
}

void ALFrameFences::poll()
{
    while (mComplete + 1 < mFrame)
    {
        const U32 frame = mComplete + 1;
        const U32 slot  = frame % FRAMES;
        if (mFences[slot])
        {
            // The first look flushes the fence toward the GPU, so that a later one can find it signalled.
            const GLenum status = glClientWaitSync((GLsync)mFences[slot], mFlushed[slot] ? 0 : GL_SYNC_FLUSH_COMMANDS_BIT, 0);
            ++mCalls;
            ++mFrameCalls;
            mFlushed[slot] = true;
            if (!signalled(status))
            {
                return;
            }
        }
        else if (mStamped[slot] && !stampWritten(slot))
        {
            return;
        }
        completeThrough(frame);
    }
}

void ALFrameFences::completeThrough(U32 frame)
{
    // Commands finish in the order they were issued, so a frame's fence signalled says the same of every earlier frame.
    for (U32 done = mComplete + 1; done <= frame && done < mFrame; ++done)
    {
        void*& fence = mFences[done % FRAMES];
        if (fence)
        {
            glDeleteSync((GLsync)fence);
            fence = nullptr;
        }
        mStamped[done % FRAMES] = false;
    }
    if (frame > mComplete)
    {
        mComplete = frame;
    }
}

bool ALFrameFences::wait(U32 frame, U64 timeout_ns, bool* stalled)
{
    if (stalled)
    {
        *stalled = false;
    }
    if (isComplete(frame))
    {
        return true;
    }

    if (frame >= mFrame)
    {
        // The open frame has no fence yet: one laid now follows every command issued so far, and every earlier frame.
        llassert(frame == mFrame);
        GLsync now = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        GLenum status = glClientWaitSync(now, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        mCalls += 2;
        mFrameCalls += 2;
        if (status == GL_TIMEOUT_EXPIRED)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("frame fence stall");
            ++mStalls;
            if (stalled)
            {
                *stalled = true;
            }
            status = glClientWaitSync(now, GL_SYNC_FLUSH_COMMANDS_BIT, timeout_ns);
            ++mCalls;
            ++mFrameCalls;
        }
        glDeleteSync(now);
        if (!signalled(status))
        {
            return false;
        }
        completeThrough(mFrame - 1);
        return true;
    }

    // Polling up to it shows every earlier frame on the way.
    poll();
    if (isComplete(frame))
    {
        return true;
    }

    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("frame fence stall");
    ++mStalls;
    if (stalled)
    {
        *stalled = true;
    }
    const U32 slot = frame % FRAMES;
    if (mFences[slot])
    {
        const GLenum status = glClientWaitSync((GLsync)mFences[slot], GL_SYNC_FLUSH_COMMANDS_BIT, timeout_ns);
        ++mCalls;
        ++mFrameCalls;
        mFlushed[slot] = true;
        if (!signalled(status))
        {
            return false;
        }
    }
    else if (mStamped[slot] && !waitStamp(slot, F64(timeout_ns) * 1e-9, 0.0))
    {
        return false;
    }
    completeThrough(frame);
    return true;
}

void ALFrameFences::waitFinished(U32 frame, bool* stalled)
{
    if (stalled)
    {
        *stalled = false;
    }
    // In tries of a second, so that the log can say how long; a GPU that never finishes is the main loop's watchdog's
    // to report.
    for (U32 tries = 1;; ++tries)
    {
        bool this_wait = false;
        const bool finished = wait(frame, WAIT_NANOSECONDS, &this_wait);
        if (stalled && this_wait)
        {
            *stalled = true;
        }
        if (finished)
        {
            return;
        }
        if (mStamped[frame % FRAMES])
        {
            finishStamps();
            return;
        }
        if (tries == LONG_WAIT_SECONDS)
        {
            LL_WARNS_ONCE("FrameFences") << "The GPU has not finished a frame in " << LONG_WAIT_SECONDS
                                         << " seconds; still waiting for it" << LL_ENDL;
        }
    }
}

F64 ALFrameFences::pace(U32 frame, bool& behind)
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("frame pacing wait");
    const F64 start = LLTimer::getTotalSeconds();
    // No poll before it: on a threaded driver a poll of a fence not yet signalled waits on the driver's thread as long
    // as this wait would, and outside its zone. A fence already signalled answers at once either way.
    // In tries of a second, however long the GPU takes: a frame a slow GPU spends more than a second on is paced too.
    const U32 slot = frame % FRAMES;
    behind = false;
    if (mFences[slot])
    {
        GLenum status = GL_TIMEOUT_EXPIRED;
        for (U32 tries = 1; status == GL_TIMEOUT_EXPIRED; ++tries)
        {
            status = glClientWaitSync((GLsync)mFences[slot], GL_SYNC_FLUSH_COMMANDS_BIT, WAIT_NANOSECONDS);
            ++mCalls;
            ++mFrameCalls;
            behind = behind || status == GL_CONDITION_SATISFIED || status == GL_TIMEOUT_EXPIRED;
            if (status == GL_TIMEOUT_EXPIRED && tries == LONG_WAIT_SECONDS)
            {
                LL_WARNS_ONCE("FrameFences") << "The GPU has not finished a frame in " << LONG_WAIT_SECONDS
                                             << " seconds; still waiting for it" << LL_ENDL;
            }
        }
        mFlushed[slot] = true;
        if (behind)
        {
            ++mPacingWaits;
        }
        // Finished, or a fence that could not be waited on, which signalled() warns of: either way the frame is done.
        (void)signalled(status);
    }
    else if (mStamped[slot])
    {
        behind = !stampWritten(slot);
        if (behind)
        {
            ++mPacingWaits;
            if (!waitStamp(slot, STAMP_WAIT_SECONDS, mStampPace))
            {
                finishStamps();
            }
        }
        // What the next wait expects to take: a GPU-bound frame's takes about what the last one did.
        mStampPace = LLTimer::getTotalSeconds() - start;
    }
    completeThrough(frame);
    const F64 end = LLTimer::getTotalSeconds();
    return end - start;
}

//static
bool ALFrameFences::canStamp()
{
    return gGLManager.mHasQueryBufferObject && gGLManager.mHasTimestampQuery && gGLManager.mHasDirectStateAccess
           && glGetQueryBufferObjectui64v && ALGPUBuffer::canMapPersistently();
}

bool ALFrameFences::readyStamps()
{
    if (!mWantStamps || mStampsFailed || !canStamp())
    {
        if (mStampStore.isAllocated())
        {
            releaseStamps();
        }
        return false;
    }
    if (mStampStore.isAllocated())
    {
        return true;
    }

    // The first frame stamped: a slot a frame, each its own timestamp query, made once.
    const U64 zeros[FRAMES] = {};
    if (!mStampStore.allocate(sizeof(zeros), ALGPUBuffer::READBACK, zeros, "Frame stamps") || !mStampStore.getMapped())
    {
        LL_WARNS("FrameFences") << "No mapped store for the frames' stamps; the frames' ends lay fences" << LL_ENDL;
        mStampStore.release();
        mStampsFailed = true;
        return false;
    }
    glGenQueries(GLsizei(FRAMES), mStampQueries);
    std::fill(std::begin(mStampBefore), std::end(mStampBefore), 0ull);
    mStampPace = 0.0;
    return true;
}

void ALFrameFences::releaseStamps()
{
    // The frames stamped so far first, so that no stamp is still to be written into the store.
    for (U32 frame = mFrame - 1; frame > mComplete; --frame)
    {
        if (mStamped[frame % FRAMES])
        {
            waitFinished(frame);
            break;
        }
    }
    glDeleteQueries(GLsizei(FRAMES), mStampQueries);
    std::fill(std::begin(mStampQueries), std::end(mStampQueries), 0u);
    mStampStore.release();
}

void ALFrameFences::stamp(U32 slot)
{
    // The slot's stamp of a lap ago has come: its frame was waited out before this one's end laid anything.
    mStampBefore[slot] = *reinterpret_cast<const volatile U64*>(mStampStore.getMapped() + slot * sizeof(U64));
    glQueryCounter(mStampQueries[slot], GL_TIMESTAMP);
    glGetQueryBufferObjectui64v(mStampQueries[slot], mStampStore.getGLName(), GL_QUERY_RESULT,
                                GLintptr(slot * sizeof(U64)));
    // On toward the GPU behind the frame, as a fence's first poll flushes it: nothing else might send the stamp on
    // before the next frame's swap.
    glFlush();
    mFlushed[slot] = true;
    mStamped[slot] = true;
}

bool ALFrameFences::stampWritten(U32 slot) const
{
    llassert(mStampStore.getMapped());
    // The GPU writes the map behind the CPU's back: read afresh every time, and nothing read after a stamp that has
    // come, such as a query's result the GPU wrote before it, read before it.
    const U64 now = *reinterpret_cast<const volatile U64*>(mStampStore.getMapped() + slot * sizeof(U64));
    if (now == mStampBefore[slot])
    {
        return false;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    return true;
}

bool ALFrameFences::waitStamp(U32 slot, F64 timeout, F64 expect)
{
    using time_point = ALPreciseWaiter::time_point;
    const time_point start     = time_point::now();
    const time_point give_up   = start.plus(timeout);
    const time_point expected  = start.plus(expect);
    const time_point spin_from = expected.plus(-STAMP_SPIN_SECONDS);
    const time_point spin_to   = expected.plus(STAMP_SPIN_SECONDS);
    while (!stampWritten(slot))
    {
        const time_point now = time_point::now();
        if (now >= give_up)
        {
            return false;
        }
        // Asleep until a spin before what the wait expects, the last step ending there, spinning from there to a spin
        // past it, and after that asleep in shorter steps. The spin goes in two parts, to what it expects and past it,
        // neither longer than one that never yields the core.
        if (now < spin_from)
        {
            ALPreciseWaiter::sleepUntil((std::min)(now.plus(STAMP_SLEEP_SECONDS), spin_from));
        }
        else if (now < spin_to)
        {
            const time_point part = now < expected ? expected : spin_to;
            ALPreciseWaiter::spinUntil((std::min)(part, give_up), [this, slot]() { return stampWritten(slot); });
        }
        else
        {
            ALPreciseWaiter::sleepUntil((std::min)(now.plus(STAMP_LATE_SLEEP_SECONDS), give_up));
        }
    }
    return true;
}

void ALFrameFences::finishStamps()
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("frame stamp fence");
    LL_WARNS_ONCE("FrameFences") << "A frame's stamp has not come in a second; waiting on a fence behind it" << LL_ENDL;
    // A fence behind every command issued so far, the stamped frames' among them, waited on in tries of a second as a
    // fenced frame's is, not glFinish, which drains every queue of the context's: what this waits for is only what the
    // frames before need.
    const GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    ++mCalls;
    ++mFrameCalls;
    GLenum status = GL_TIMEOUT_EXPIRED;
    for (U32 tries = 1; status == GL_TIMEOUT_EXPIRED; ++tries)
    {
        status = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, WAIT_NANOSECONDS);
        ++mCalls;
        ++mFrameCalls;
        if (status == GL_TIMEOUT_EXPIRED && tries == LONG_WAIT_SECONDS)
        {
            LL_WARNS_ONCE("FrameFences") << "The GPU has not finished a frame in " << LONG_WAIT_SECONDS
                                         << " seconds; still waiting for it" << LL_ENDL;
        }
    }
    glDeleteSync(fence);
    // Signalled, or a fence that could not be waited on, which signalled() warns of: either way the frames are done.
    (void)signalled(status);
    for (U32 slot = 0; slot < FRAMES; ++slot)
    {
        if (mStamped[slot] && !stampWritten(slot))
        {
            LL_WARNS("FrameFences") << "A frame's stamp did not come once the GPU had finished; the frames' ends lay "
                                       "fences from here on" << LL_ENDL;
            mStampsFailed = true;
            // The fence has seen every frame stamped finished, the one a frame's end is ending too.
            std::fill(std::begin(mStamped), std::end(mStamped), false);
            break;
        }
    }
    // Every command issued before the fence has finished.
    completeThrough(mFrame - 1);
}

void ALFrameFences::endFrame(U32 frames_ahead)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    // The slot still holds the fence of the frame FRAMES before, while the GPU is that far behind.
    if (mFrame > FRAMES)
    {
        waitFinished(mFrame - FRAMES);
    }

    const U32 slot = mFrame % FRAMES;
    llassert(!mFences[slot] && !mStamped[slot]);
    // A frame some command of which was told its end lays a fence (willStamp()), and laid none of its own, ends with
    // one, though stamps were asked for since: they start with the next frame.
    const bool fence_counted_on = mFenceCountedOn && !isStamping();
    mFenceCountedOn = false;
    if (!fence_counted_on && readyStamps())
    {
        stamp(slot);
    }
    else
    {
        mFences[slot]  = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        mFlushed[slot] = false;
        ++mCalls;
        ++mFrameCalls;
    }

    // The pacing wait, every frame the GPU is behind: for the frame `frames_ahead` before this one, whose fence was
    // laid, or stamp asked for, a frame or more ago. A fence's flush sends this frame's fence on with it; a stamp has
    // flushed itself.
    F64 paced = 0.0;
    bool behind = false;
    if (frames_ahead > 0 && mFrame > frames_ahead && !isComplete(mFrame - frames_ahead))
    {
        paced = pace(mFrame - frames_ahead, behind);
    }
    if (frames_ahead > 0)
    {
        mSlack.endPaced(paced, behind);
    }
    else
    {
        mSlack.endUnpaced();
    }

    // The frame's one poll, of the frames ended before this one: straight after the swap, where a threaded driver's
    // own thread has caught up or is finishing the swap, or where the pacing wait has left it. The frame ending now
    // has only just been sent to the GPU.
    poll();
    ++mFrame;

    LL_PROFILE_PLOT("GL frame fence calls", (int64_t)mFrameCalls);
    LL_PROFILE_PLOT("frame pacing wait ms", paced * 1000.0);
    mFrameCalls = 0;
}

void ALFrameFences::release()
{
    // Not after the context has gone: the fences went with it.
    const bool current = gGLManager.mInited;
    for (U32 slot = 0; slot < FRAMES; ++slot)
    {
        if (current && mFences[slot])
        {
            glDeleteSync((GLsync)mFences[slot]);
        }
        mFences[slot]  = nullptr;
        mFlushed[slot] = false;
        mStamped[slot] = false;
    }
    // The stamps' store and queries go as the fences do: nothing the context ran is left to wait for.
    if (current && mStampQueries[0])
    {
        glDeleteQueries(GLsizei(FRAMES), mStampQueries);
    }
    std::fill(std::begin(mStampQueries), std::end(mStampQueries), 0u);
    std::fill(std::begin(mStampBefore), std::end(mStampBefore), 0ull);
    mStampStore.release();
    mStampPace = 0.0;
    mFenceCountedOn = false;
    // What was noted in the open frame is complete too: nothing the context ran is left to wait for.
    mComplete = mFrame;
    ++mFrame;
    mSlack = ALPacingSlack();
}
