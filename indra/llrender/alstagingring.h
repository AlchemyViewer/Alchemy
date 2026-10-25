/**
 * @file alstagingring.h
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

#pragma once

#include "algpubuffer.h"
#include "stdtypes.h"

#include <cstddef>
#include <vector>

class ALFrameFences;

/// Bytes the CPU hands the GPU through a buffer, for a command that would otherwise read them from client memory: the
/// main thread's texture uploads (LLImageGL), which read from the bound GL_PIXEL_UNPACK_BUFFER instead. A threaded
/// driver takes a command's client memory as it comes: NVIDIA's threaded optimization copies it on the application's
/// thread, and Mesa's glthread runs a texture upload from client memory only once its own thread has caught up. From a
/// buffer the command is queued like any other, and the copy into the buffer is a plain memcpy.
///
/// One store, mapped persistently for writing, in SEGMENTS segments. take() hands out room in the open segment, moving
/// to the next where the open one has too little, and a segment once left is entered again only once the frame it was
/// left in has finished (ALFrameFences), so nothing the GPU has yet to read is written over. Where the frames' ends
/// stamp their frames rather than lay fences (ALFrameFences::willStamp, NVIDIA's driver), the ring lays a fence of its
/// own as it leaves a segment, and enters it again only once that fence has signalled too: GL says a persistently
/// mapped store is no longer read by the commands before it only through a sync object, and a stamp is a query, not
/// one. The fence is laid in take(), which NVIDIA's threaded optimization answers at once, and polled at the frame's
/// end, once the frame it was laid in has finished, so that it has signalled and the poll answers at once too: take()
/// waits on nothing the driver's thread has queued. take() never waits: where
/// the next segment is still the GPU's, or the bytes asked for are more than a segment holds, it gives no room, and the
/// caller sends its bytes from client memory as it would without the ring. endFrame() sizes the ring to the traffic of
/// late, between MIN_BYTES and MAX_BYTES: what the last SLACK_FRAMES frames asked for together, a burst held once rather
/// than as though every frame asked as much, and its segments each as large as a take too large for one that two frames
/// of the window have asked for, a one-off going from client memory and leaving the ring as it was. It grows at the
/// frame's end after a frame that asked for more, and shrinks once a window of frames has asked for less than half, the
/// new store made there and not by the next take. A take is counted at the room it holds, a whole ALIGNMENT at least. A store
/// a new size replaces is kept until the frame it was replaced in has finished, and only then deleted: the uploads
/// issued from it may not have read it yet, and a driver may give a persistently mapped store's memory to the next
/// store as soon as the first is deleted. Where buffers cannot be mapped persistently (macOS's 4.1) it gives no room at
/// all, and setEnabled(false) has it give none anywhere, every upload reading client memory.
///
/// The context of the frame fences it is given must be current. Non-copyable.
class ALStagingRing
{
public:
    static constexpr U32    SEGMENTS  = 4;
    static constexpr size_t MIN_BYTES = 1u << 20;
    static constexpr size_t MAX_BYTES = 64u << 20;
    /// Where room begins: a multiple of the size of every type an upload reads its pixels in, which is what GL asks of an
    /// unpack buffer's offset (a 12-byte RGB32F pixel is three 4-byte floats).
    static constexpr size_t ALIGNMENT = 256;
    /// The frames the bytes a frame asks for are kept for: a segment, a quarter of the ring, comes round after the
    /// other three are filled, and must find its frame finished at the pacing depth (AlchemyRenderMaxFramesAhead, 3)
    /// with a frame to spare: 3/4 of 8 frames is more than the 5 that takes.
    static constexpr size_t SLACK_FRAMES = 8;
    /// The frames over which the ring's size may fall.
    static constexpr U32 WINDOW_FRAMES = 600;

    /// Room in the ring: `mData`, mapped, to write, which the GPU reads at `mOffset` in buffer `mBuffer`.
    struct Span
    {
        U8*    mData   = nullptr;
        size_t mOffset = 0;
        U32    mBuffer = 0;
        bool   isValid() const { return mData != nullptr; }
    };

    explicit ALStagingRing(ALFrameFences& frames) : mFrames(frames) {}
    ALStagingRing(const ALStagingRing&) = delete;
    ALStagingRing& operator=(const ALStagingRing&) = delete;

    /// Room for `bytes`, to be read by a command issued in this frame; none where the ring has none to give.
    Span take(size_t bytes);

    /// Once a frame, at its end: sizes the ring for the frames to come.
    void endFrame();

    /// Lets the store go, while the context is current; the next take() makes it again.
    void release();

    /// Whether take() gives room at all (AlchemyRenderStagedUploads). Off, the store goes once its frame has finished.
    void setEnabled(bool enabled) { mEnabled = enabled; }
    bool isEnabled() const { return mEnabled; }

    /// The ring's size, made or to be made.
    size_t size() const { return mBytes; }
    bool   isAllocated() const { return mStore.isAllocated(); }
    /// The takes given room, and those given none for want of a free segment or for asking more than a segment holds.
    U64 getTaken() const { return mTaken; }
    U64 getBusy() const { return mBusy; }
    U64 getTooLarge() const { return mTooLarge; }
    /// The stores replaced and not yet deleted, and the fences laid and not yet seen signalled.
    U32 getRetired() const { return (U32)mRetired.size(); }
    U32 getFences() const;

private:
    // A store replaced, and the frame it was replaced in, after which it may go.
    struct Retired
    {
        ALGPUBuffer mStore;
        U32         mFrame = 0;
        void*       mFence = nullptr; // laid behind its uploads where the frames' ends stamp, the GLsync
    };

    bool ready();
    void resize(size_t bytes);
    // The store, kept until the open frame has finished; the head back at the start.
    void retire();
    // A fence behind every command so far, where the frames' ends stamp rather than lay fences; null elsewhere.
    void* layFence() const;

    ALFrameFences& mFrames;
    ALGPUBuffer    mStore;
    size_t         mBytes = MIN_BYTES;
    size_t         mHead  = 0;               // where the next room begins
    U32            mSegment = 0;             // the open segment
    U32            mLeftIn[SEGMENTS] = {};   // the frame each segment was left in, 0 for none
    void*          mSegmentFences[SEGMENTS] = {}; // the GLsync laid as each was left, while stamping
    bool           mUnavailable = false;     // no persistent maps, or no store could be made: no room until release()
    bool           mEnabled = true;
    std::vector<Retired> mRetired;

    size_t mFrameAsked   = 0;  // bytes asked for this frame that a segment holds, given room or not
    size_t mFrameLargest = 0;  // the largest take asked for this frame
    size_t mRecentAsked[SLACK_FRAMES] = {}; // mFrameAsked of the last SLACK_FRAMES frames
    U32    mRecentAt      = 0;  // where the next frame's goes
    size_t mWindowTraffic = 0;  // the most the last SLACK_FRAMES frames asked for together, in the window
    size_t mWindowLargest = 0;  // the most of mFrameLargest in the window
    size_t mWindowRecurring = 0; // the largest take two frames of the window asked as much as
    U32    mWindowFrames = 0;

    U64 mTaken    = 0;
    U64 mBusy     = 0;
    U64 mTooLarge = 0;
};
