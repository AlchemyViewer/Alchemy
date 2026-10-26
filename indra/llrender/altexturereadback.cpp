/**
 * @file altexturereadback.cpp
 * @brief Texture levels read back into buffers of a pool, collected once the GPU has written them.
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

#include "altexturereadback.h"

#include "alframefences.h"
#include "llgl.h"
#include "llglheaders.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace
{
    size_t power_of_two_at_least(size_t bytes)
    {
        size_t power = 1;
        while (power < bytes)
        {
            power <<= 1;
        }
        return power;
    }

    // A free buffer serves a read only up to this many times the size a buffer made for the read would have, so that a
    // large one is kept for the large reads.
    constexpr size_t MAX_REUSE_RATIO = 4;

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
        // Not after the context has gone: its fences went with it.
        if (fence && gGLManager.mInited)
        {
            glDeleteSync(static_cast<GLsync>(fence));
        }
        fence = nullptr;
    }
}

ALTextureReadback::Ticket ALTextureReadback::request(U32 target, S32 level, U32 format, U32 type, size_t bytes)
{
    if (bytes == 0)
    {
        llassert(false);
        return 0;
    }
    ++mFrameRequests;

    Buffer buffer;
    if (mReads.size() >= MAX_READS || !take(bytes, buffer))
    {
        ++mRefused;
        return 0;
    }

    // Through the pack buffer binding, which nothing else in the renderer caches, and back to none after: while a
    // buffer is bound there, the read writes into it at the offset it is given in place of a pointer. The rows tight,
    // as `bytes` counts them: LLRender::init sets that alignment, and a context before it, or code after it, may not
    // have it.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer.mStore.getGLName());
    glGetTexImage(target, level, format, type, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    // Behind this read and the buffer's reads before it.
    delete_fence(buffer.mFence);
    if (mFrames.willStamp())
    {
        buffer.mFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    Read read;
    read.mTicket = ++mLastTicket;
    read.mBuffer = std::move(buffer);
    read.mBytes  = bytes;
    read.mFrame  = mFrames.frame();
    mReads.push_back(std::move(read));
    return mReads.back().mTicket;
}

ALTextureReadback::EResult ALTextureReadback::collect(Ticket ticket, U8* out, size_t bytes)
{
    Read* read = find(ticket);
    if (!read)
    {
        return GONE;
    }
    if (!written(*read))
    {
        return PENDING;
    }

    llassert(out);
    const size_t copied = std::min(bytes, read->mBytes);
    if (out && copied > 0)
    {
        const ALGPUBuffer& store = read->mBuffer.mStore;
        if (store.getMapped())
        {
            memcpy(out, store.getMapped(), copied);
        }
        else
        {
            // The read has finished, so this waits on no GPU work; it is a call that returns a value, all the same.
            store.read(0, out, copied);
        }
    }
    erase(read);
    return READY;
}

ALTextureReadback::EResult ALTextureReadback::state(Ticket ticket)
{
    Read* read = find(ticket);
    if (!read)
    {
        return GONE;
    }
    return written(*read) ? READY : PENDING;
}

bool ALTextureReadback::written(Read& read)
{
    // No GL call: the read's own fence is polled at the frame's end (endFrame), and goes once it has signalled.
    return mFrames.isComplete(read.mFrame) && !read.mBuffer.mFence;
}

bool ALTextureReadback::idle(Buffer& buffer)
{
    return mFrames.isComplete(buffer.mLastUsed) && !buffer.mFence;
}

U32 ALTextureReadback::getFences() const
{
    U32 fences = 0;
    for (const Buffer& buffer : mFree)
    {
        fences += buffer.mFence != nullptr;
    }
    for (const Read& read : mReads)
    {
        fences += read.mBuffer.mFence != nullptr;
    }
    return fences;
}

void ALTextureReadback::cancel(Ticket ticket)
{
    if (Read* read = find(ticket))
    {
        erase(read);
    }
}

void ALTextureReadback::endFrame()
{
    // The fences of the reads, and of the free buffers' last reads, whose frames have finished: polled here, where the
    // frame fences are, and not where the reads are asked for and taken in, in the middle of a frame, where a threaded
    // driver answers a poll only once its own thread has caught up. Behind a finished frame they have all but surely
    // signalled, and a poll of one that has answers at once.
    for (Read& read : mReads)
    {
        if (read.mBuffer.mFence && mFrames.isComplete(read.mFrame))
        {
            fence_signalled(read.mBuffer.mFence);
        }
    }
    for (Buffer& buffer : mFree)
    {
        if (buffer.mFence && mFrames.isComplete(buffer.mLastUsed))
        {
            fence_signalled(buffer.mFence);
        }
    }

    const U32 frame = mFrames.frame();
    for (size_t i = 0; i < mReads.size();)
    {
        if (frame - mReads[i].mFrame > EXPIRE_FRAMES)
        {
            ++mExpired;
            erase(&mReads[i]);
        }
        else
        {
            ++i;
        }
    }
    for (size_t i = 0; i < mFree.size();)
    {
        if (frame - mFree[i].mLastUsed > TRIM_FRAMES && idle(mFree[i]))
        {
            mBytes -= mFree[i].mStore.size();
            mFree[i].mStore.release();
            mFree.erase(mFree.begin() + i);
        }
        else
        {
            ++i;
        }
    }

    LL_PROFILE_PLOT("texture readbacks queued", (int64_t)mFrameRequests);
    LL_PROFILE_PLOT("texture readback KB", (int64_t)(mBytes >> 10));
    mFrameRequests = 0;
}

void ALTextureReadback::release()
{
    for (Buffer& buffer : mFree)
    {
        delete_fence(buffer.mFence);
        buffer.mStore.release();
    }
    for (Read& read : mReads)
    {
        delete_fence(read.mBuffer.mFence);
        read.mBuffer.mStore.release();
    }
    mFree.clear();
    mReads.clear();
    mBytes         = 0;
    mFrameRequests = 0;
}

ALTextureReadback::Read* ALTextureReadback::find(Ticket ticket)
{
    if (ticket == 0)
    {
        return nullptr;
    }
    for (Read& read : mReads)
    {
        if (read.mTicket == ticket)
        {
            return &read;
        }
    }
    return nullptr;
}

bool ALTextureReadback::take(size_t bytes, Buffer& out)
{
    const size_t made = power_of_two_at_least(std::max(bytes, MIN_BUFFER));

    size_t best = mFree.size();
    for (size_t i = 0; i < mFree.size(); ++i)
    {
        const size_t size = mFree[i].mStore.size();
        if (size >= bytes && size <= made * MAX_REUSE_RATIO &&
            (best == mFree.size() || size < mFree[best].mStore.size()))
        {
            best = i;
        }
    }
    if (best < mFree.size())
    {
        out = std::move(mFree[best]);
        mFree.erase(mFree.begin() + best);
        return true;
    }

    if (made > MAX_BYTES)
    {
        return false;
    }
    // Room for it: the free buffers go, the longest unused first, while the pool would hold too much -- those whose
    // last read the GPU has finished only. A read given up may still be writing its buffer, and a driver may hand a
    // persistently mapped store's memory to the next store as soon as the first is deleted.
    while (mBytes + made > MAX_BYTES)
    {
        auto oldest = mFree.end();
        for (auto it = mFree.begin(); it != mFree.end(); ++it)
        {
            if ((oldest == mFree.end() || it->mLastUsed < oldest->mLastUsed) && idle(*it))
            {
                oldest = it;
            }
        }
        if (oldest == mFree.end())
        {
            break;
        }
        mBytes -= oldest->mStore.size();
        oldest->mStore.release();
        mFree.erase(oldest);
    }
    if (mBytes + made > MAX_BYTES)
    {
        return false;
    }
    if (!out.mStore.allocate(made, ALGPUBuffer::READBACK, nullptr, "Texture readback"))
    {
        LL_WARNS_ONCE("TextureReadback") << "No buffer for a texture readback; the read waits for one" << LL_ENDL;
        return false;
    }
    mBytes += made;
    ++mMade;
    return true;
}

void ALTextureReadback::giveBack(Buffer&& buffer)
{
    buffer.mLastUsed = mFrames.frame();
    mFree.push_back(std::move(buffer));
}

void ALTextureReadback::erase(Read* read)
{
    const size_t index = (size_t)(read - mReads.data());
    llassert(index < mReads.size());
    giveBack(std::move(read->mBuffer));
    mReads.erase(mReads.begin() + index);
}
