/**
 * @file altexturereadback.h
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

#pragma once

#include "algpubuffer.h"
#include "stdtypes.h"

#include <cstddef>
#include <vector>

class ALFrameFences;

/// Texture levels read back without waiting on the GPU, for a user that can take the pixels a few frames later: the
/// main thread's LLImageGL::requestReadBack, for the loaded callbacks that need a texture's pixels when no copy of them
/// is kept on the CPU. glGetTexImage into client memory waits for the GPU to finish everything queued ahead of it, and
/// on a threaded driver for the driver's own thread first; into a buffer bound for packing it is queued like a draw.
/// request() issues that read into a buffer of the pool, noting the frame; collect() copies the pixels out once the
/// frame fences have shown that frame finished (ALFrameFences), a few frames' ends later: with no GL call where the
/// buffers map persistently, by glGetBufferSubData of the finished read elsewhere (macOS's 4.1).
///
/// A buffer is kept for the reads after its own, each taking the smallest free one that holds it, and one is made only
/// where none is free: making one asks GL for a name and, where it maps, for the map, calls that return a value. A free
/// buffer is deleted only once the frame it was given back in has finished, since a read given up may still be writing
/// it. Where the frames' ends stamp their frames rather than lay fences (ALFrameFences::willStamp, NVIDIA's driver), each
/// read lays a fence of its own behind it, and is ready, or its buffer deleted, only once that fence has signalled as
/// well: a stamp is a query, not a sync object, and the staging ring's uploads through a buffer came out wrong on
/// NVIDIA's driver until it waited on fences of its own. A fence laid and polled is a call that returns a value each,
/// once a read; it is polled at the frame's end, once its frame has finished, never in the middle of a frame where the
/// reads are asked for and taken in. Then: one unused for TRIM_FRAMES frames goes, and so does a read no one has collected EXPIRE_FRAMES frames after it was
/// issued, so that a user that stops asking gives its buffer back. At most MAX_READS reads, and MAX_BYTES in all, are in
/// flight or kept; past that a request is refused, as it is where no buffer could be made, and its user asks again
/// later.
///
/// The context of the frame fences it is given must be current. Non-copyable.
class ALTextureReadback
{
public:
    static constexpr U32    MAX_READS     = 16;
    static constexpr size_t MAX_BYTES     = 64u << 20;
    /// The least a buffer holds, so that the small levels of many textures share one size.
    static constexpr size_t MIN_BUFFER    = 64u << 10;
    static constexpr U32    TRIM_FRAMES   = 600;
    static constexpr U32    EXPIRE_FRAMES = 600;

    /// A read requested: 0 is none, and none is given twice, release() or no.
    using Ticket = U64;

    enum EResult
    {
        PENDING, // the GPU has yet to be shown finished with it
        READY,   // copied out, and done with
        GONE     // cancelled, expired, released or never given
    };

    explicit ALTextureReadback(ALFrameFences& frames) : mFrames(frames) {}
    ALTextureReadback(const ALTextureReadback&) = delete;
    ALTextureReadback& operator=(const ALTextureReadback&) = delete;

    /// Reads level `level` of the texture bound to `target` on the active unit, as glGetTexImage writes it in `format`
    /// and `type` under the pack alignment of 1, which it sets, `bytes` of it, into a buffer of the pool. 0, and nothing
    /// read, where MAX_READS are in flight, the bytes would take the pool past MAX_BYTES, or no buffer could be made.
    Ticket request(U32 target, S32 level, U32 format, U32 type, size_t bytes);

    /// READY, with the `bytes` read (no more than were asked) copied into `out`, once the frame the read was issued in
    /// is shown finished; PENDING before; GONE for a ticket that is not a read's.
    EResult collect(Ticket ticket, U8* out, size_t bytes);

    /// What collect() would answer, copying nothing and keeping the read: READY once it can be collected.
    EResult state(Ticket ticket);

    /// Gives a read up, its buffer back to the pool. Nothing for a ticket that is not a read's.
    void cancel(Ticket ticket);

    /// Once a frame, at its end: expires reads no one collected and lets go of buffers unused.
    void endFrame();

    /// Lets every buffer go, while the context is current; every read is GONE.
    void release();

    /// The reads in flight or not yet collected, and the bytes of every buffer kept.
    U32    getReads() const { return (U32)mReads.size(); }
    size_t getBytes() const { return mBytes; }
    /// The buffers made, the requests refused, and the reads expired.
    U64 getMade() const { return mMade; }
    U64 getRefused() const { return mRefused; }
    U64 getExpired() const { return mExpired; }
    /// The fences laid and not yet seen signalled.
    U32 getFences() const;

private:
    struct Buffer
    {
        ALGPUBuffer mStore;
        U32         mLastUsed = 0;       // the frame it was last given back in
        void*       mFence    = nullptr; // the GLsync behind the last read into it, while stamping, until seen signalled
    };

    struct Read
    {
        Ticket mTicket = 0;
        Buffer mBuffer;
        size_t mBytes = 0;
        U32    mFrame = 0; // the frame the read was issued in
    };

    // The read `ticket` names, or null.
    Read* find(Ticket ticket);
    // Whether the GPU has written `read`: its frame shown finished, and its fence, if it has one, signalled.
    bool written(Read& read);
    // Whether nothing the GPU has yet to do writes `buffer`, so that it may be deleted.
    bool idle(Buffer& buffer);
    // The smallest free buffer holding `bytes`, taken from the free list, or one made: false where none can be.
    bool take(size_t bytes, Buffer& out);
    // `buffer` back on the free list.
    void giveBack(Buffer&& buffer);
    void erase(Read* read);

    ALFrameFences&      mFrames;
    std::vector<Buffer> mFree;
    std::vector<Read>   mReads;
    size_t              mBytes      = 0; // every buffer's, free and in flight
    Ticket              mLastTicket = 0;
    U32                 mFrameRequests = 0; // requests made since the last endFrame(), for the profiler

    U64 mMade    = 0;
    U64 mRefused = 0;
    U64 mExpired = 0;
};
