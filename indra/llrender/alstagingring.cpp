/**
 * @file alstagingring.cpp
 * @brief Bytes the CPU hands the GPU through a mapped buffer, for commands that would otherwise read client memory.
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

#include "alstagingring.h"

#include "alframefences.h"
#include "llglheaders.h"

#include <algorithm>
#include <iterator>

namespace
{
    // AlchemyRenderMaxFramesAhead's default, as the fenced uniform rings take it (aluniformbuffer.cpp): a segment comes
    // round once the other three have filled, and is free without a wait only once the frame fences have shown the
    // frame it was left in finished, which during frame G is G - MAX_FRAMES_AHEAD - 1.
    constexpr size_t MAX_FRAMES_AHEAD = 3;
    static_assert((ALStagingRing::SEGMENTS - 1) * ALStagingRing::SLACK_FRAMES >=
                      ALStagingRing::SEGMENTS * (MAX_FRAMES_AHEAD + 2),
                  "a segment comes round before the frame it was left in is shown finished");

    size_t aligned(size_t bytes)
    {
        return (bytes + ALStagingRing::ALIGNMENT - 1) / ALStagingRing::ALIGNMENT * ALStagingRing::ALIGNMENT;
    }

    size_t power_of_two_at_least(size_t bytes)
    {
        size_t power = 1;
        while (power < bytes)
        {
            power <<= 1;
        }
        return power;
    }

    // Whether `fence` has signalled, deleting it once it has: a poll, a call that returns a value. No fence is
    // signalled, and so is one GL could not wait on, which no wait would ever see signal.
    bool fence_signalled(void*& fence)
    {
        if (!fence)
        {
            return true;
        }
        if (glClientWaitSync(static_cast<GLsync>(fence), GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED)
        {
            return false;
        }
        glDeleteSync(static_cast<GLsync>(fence));
        fence = nullptr;
        return true;
    }

    void delete_fence(void*& fence)
    {
        if (fence)
        {
            glDeleteSync(static_cast<GLsync>(fence));
            fence = nullptr;
        }
    }

    // The ring that holds `asked` bytes a frame for SLACK_FRAMES frames, and a take of `largest` in each segment. A power
    // of two, so that its segments start at a multiple of the alignment.
    size_t wanted(size_t asked, size_t largest)
    {
        const size_t for_traffic = power_of_two_at_least(asked * ALStagingRing::SLACK_FRAMES);
        const size_t for_largest = power_of_two_at_least(aligned(largest)) * ALStagingRing::SEGMENTS;
        return std::clamp(std::max(for_traffic, for_largest), ALStagingRing::MIN_BYTES, ALStagingRing::MAX_BYTES);
    }
}

ALStagingRing::Span ALStagingRing::take(size_t bytes)
{
    if (bytes == 0)
    {
        return {};
    }
    mFrameAsked += bytes;
    mFrameLargest = std::max(mFrameLargest, bytes);

    const size_t room    = aligned(bytes);
    const size_t segment = mBytes / SEGMENTS;
    if (room > segment)
    {
        ++mTooLarge;
        return {};
    }
    if (!mEnabled || !ready())
    {
        return {};
    }

    if (mHead + room > size_t(mSegment + 1) * segment)
    {
        // Into the next segment, once the frame it was left in has finished; until then the open one stays open, for
        // the takes that still fit it.
        const U32 next = (mSegment + 1) % SEGMENTS;
        if (!mFrames.isComplete(mLeftIn[next]) || !fence_signalled(mSegmentFences[next]))
        {
            ++mBusy;
            return {};
        }
        mLeftIn[mSegment]        = mFrames.frame();
        mSegmentFences[mSegment] = layFence();
        mSegment                 = next;
        mHead             = size_t(next) * segment;
        mLeftIn[next]     = 0;
    }

    Span span;
    span.mData   = mStore.getMapped() + mHead;
    span.mOffset = mHead;
    span.mBuffer = mStore.getGLName();
    mHead += room;
    ++mTaken;
    return span;
}

bool ALStagingRing::ready()
{
    if (mStore.isAllocated())
    {
        return true;
    }
    if (mUnavailable)
    {
        return false;
    }
    if (!ALGPUBuffer::canMapPersistently())
    {
        mUnavailable = true;
        return false;
    }
    if (!mStore.allocate(mBytes, ALGPUBuffer::STREAM, nullptr, "Pixel staging") || !mStore.getMapped())
    {
        LL_WARNS_ONCE("StagingRing") << "No mapped store for staging uploads; they read client memory" << LL_ENDL;
        mStore.release();
        mUnavailable = true;
        return false;
    }
    mHead    = 0;
    mSegment = 0;
    std::fill(std::begin(mLeftIn), std::end(mLeftIn), 0u);
    return true;
}

U32 ALStagingRing::getFences() const
{
    U32 fences = 0;
    for (void* fence : mSegmentFences)
    {
        fences += fence != nullptr;
    }
    for (const Retired& retired : mRetired)
    {
        fences += retired.mFence != nullptr;
    }
    return fences;
}

void* ALStagingRing::layFence() const
{
    return mFrames.isStamping() ? glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;
}

void ALStagingRing::retire()
{
    if (mStore.isAllocated())
    {
        Retired retired;
        retired.mStore = std::move(mStore);
        retired.mFrame = mFrames.frame();
        // Behind every upload from the store: its segments' own fences are then of no more use.
        retired.mFence = layFence();
        mRetired.push_back(std::move(retired));
    }
    for (void*& fence : mSegmentFences)
    {
        delete_fence(fence);
    }
    mHead    = 0;
    mSegment = 0;
    std::fill(std::begin(mLeftIn), std::end(mLeftIn), 0u);
}

void ALStagingRing::resize(size_t bytes)
{
    retire();
    mBytes = bytes;
}

void ALStagingRing::endFrame()
{
    LL_PROFILE_PLOT("pixel staging KB", (int64_t)(mFrameAsked >> 10));

    // The stores replaced go once the frame they were replaced in has finished, and with it every upload from them.
    // Deleted at once, GL would keep them for those uploads, but a driver may hand a persistently mapped store's memory
    // to the next store first, and the uploads not yet read would read what is written there next.
    for (size_t i = 0; i < mRetired.size();)
    {
        if (mFrames.isComplete(mRetired[i].mFrame) && fence_signalled(mRetired[i].mFence))
        {
            mRetired[i].mStore.release();
            mRetired.erase(mRetired.begin() + i);
        }
        else
        {
            ++i;
        }
    }
    if (!mEnabled && mStore.isAllocated())
    {
        retire();
    }
    mWindowAsked   = std::max(mWindowAsked, mFrameAsked);
    mWindowLargest = std::max(mWindowLargest, mFrameLargest);

    if (!mUnavailable)
    {
        // Up after a frame that asked for more, at once; down only after a window that asked for less than half.
        const size_t now = wanted(mFrameAsked, mFrameLargest);
        if (now > mBytes)
        {
            resize(now);
        }
        else if (mWindowFrames + 1 >= WINDOW_FRAMES)
        {
            const size_t window = wanted(mWindowAsked, mWindowLargest);
            if (window <= mBytes / 2)
            {
                resize(window);
            }
        }
    }

    if (++mWindowFrames >= WINDOW_FRAMES)
    {
        mWindowFrames  = 0;
        mWindowAsked   = 0;
        mWindowLargest = 0;
    }
    mFrameAsked   = 0;
    mFrameLargest = 0;
}

void ALStagingRing::release()
{
    // The context is going, and its commands with it.
    for (Retired& retired : mRetired)
    {
        delete_fence(retired.mFence);
        retired.mStore.release();
    }
    mRetired.clear();
    for (void*& fence : mSegmentFences)
    {
        delete_fence(fence);
    }
    mStore.release();
    mUnavailable   = false;
    mBytes         = MIN_BYTES;
    mHead          = 0;
    mSegment       = 0;
    std::fill(std::begin(mLeftIn), std::end(mLeftIn), 0u);
    mFrameAsked    = 0;
    mFrameLargest  = 0;
    mWindowAsked   = 0;
    mWindowLargest = 0;
    mWindowFrames  = 0;
}
