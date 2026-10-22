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

#include "llgl.h"
#include "llglheaders.h"
#include "lltimer.h"

#include <cstdint>

namespace
{
    // How long one try of waitFinished(), or of the pacing wait, lasts.
    constexpr U64 WAIT_NANOSECONDS = 1000000000ull;

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
    completeThrough(frame);
    const F64 end = LLTimer::getTotalSeconds();
    return end - start;
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
    llassert(!mFences[slot]);
    mFences[slot]  = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    mFlushed[slot] = false;
    ++mCalls;
    ++mFrameCalls;

    // The pacing wait, every frame the GPU is behind: for the frame `frames_ahead` before this one, whose fence was
    // laid a frame or more ago. Its flush sends this frame's fence on with it.
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
    }
    // What was noted in the open frame is complete too: nothing the context ran is left to wait for.
    mComplete = mFrame;
    ++mFrame;
    mSlack = ALPacingSlack();
}
