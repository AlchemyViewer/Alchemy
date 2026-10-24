/**
 * @file alboxocclusion.h
 * @brief Whether boxes are hidden behind the depth drawn so far, asked of the GPU for all of a frame's at once.
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
#include "llmath.h"
#include "llvector4a.h"
#include "stdtypes.h"

#include <deque>
#include <vector>

class ALDepthPyramid;
class LLGLSLShader;
class LLMatrix4a;

/// Whether boxes are hidden behind the depth drawn so far, asked of the GPU for many at once. The boxes a pass adds go
/// up through a stream ring, one dispatch (groupOcclusionC.glsl) tests every one against a depth pyramid of the view
/// (ALDepthPyramid, depth_pyramid_box_visible), and the answers come back through a readback once the frame fences show
/// the frame finished, a frame or two later -- where a GL occlusion query costs a query, a draw and a result read for
/// each box. An answer errs only toward seen: a box is hidden only where every point of it lies behind the farthest
/// depth of the pyramid's texels it covers, so none is hidden that the scene does not hide. One the pyramid cannot
/// answer for -- too large on the screen for its levels, or reaching the eye's plane -- is answered Unanswered, for its
/// user to ask a query of instead; boxes past screenReach() are better asked by a query from the start.
///
/// A batch is the boxes added between two dispatches, the answers of the last RESULT_BATCHES of them kept, as the
/// query pool keeps its results, for a user that reads them only every few frames. Past that, before a batch's answers
/// arrive (Pending), or with none (a dispatch refused, or release()), a box counts as seen.
///
/// Main thread, with the context of gGL's frame fences current. Non-copyable.
class ALBoxOcclusion
{
public:
    /// The batches whose answers are kept.
    static constexpr U32 RESULT_BATCHES = 64;
    /// The batches that may be on their way at once: the readback's slots. A frame end paced three frames deep leaves
    /// the three before the one ending on the GPU, and the readback takes its answers a frame end after their frame:
    /// six covers them with one to spare. The stream keeps one region more, since a region is noted read only at the
    /// next dispatch's map, a frame later than its reads.
    static constexpr U32 IN_FLIGHT = 6;
    /// The most boxes one batch takes.
    static constexpr U32 MAX_BOXES = 32768;

    enum class Status : U8
    {
        Pending,    ///< added, and its answer not back yet
        Seen,       ///< some point of the box lies in front of the depth under it
        Hidden,     ///< the depth hides the whole box
        Unanswered, ///< the pyramid could not tell, the box too large on its screen or reaching the eye's plane
        Expired,    ///< no answer: no box, one refused, or one whose answer was dropped or released
    };

    struct Ticket
    {
        U32 mBatch = 0; ///< the batch's serial, 0 for none
        U32 mIndex = 0;
        bool isValid() const { return mBatch != 0; }
    };

    ALBoxOcclusion() = default;
    ALBoxOcclusion(const ALBoxOcclusion&) = delete;
    ALBoxOcclusion& operator=(const ALBoxOcclusion&) = delete;

    /// Whether add() takes a box now: the open batch has room, and a readback slot is free for its dispatch.
    bool canAdd() const;

    /// Adds the box `center` +- `half_extent` to the open batch, in the space the next dispatch's matrix takes to
    /// clip space. No ticket where canAdd() says no.
    Ticket add(const LLVector4a& center, const LLVector4a& half_extent);

    /// Tests every box of the open batch against `pyramid`, built from the depth the boxes are to be judged by, with
    /// `program` (gGroupOcclusionProgram), `view_proj` taking the boxes' space to the clip space the depth was drawn
    /// in, and opens the next batch. The boxes' answers are asked for at once; false where they could not be, and the
    /// batch's tickets expire. Binds `program` and its storage block and leaves neither bound.
    bool dispatch(LLGLSLShader& program, ALDepthPyramid& pyramid, const LLMatrix4a& view_proj);

    /// Takes in the answers of every batch the frame fences have shown finished, oldest first. No GL call where the
    /// readback is mapped. Once a frame, before the boxes' answers are read.
    void collect();

    /// The box's answer.
    Status result(const Ticket& ticket) const;

    /// Lets go of the buffers, while the context is current, and of every answer: every ticket expires.
    void release();

    /// The on-screen diameter in pixels past which a box spans more of the coarsest level than the pyramid's test
    /// reads, and is kept seen whatever hides it (depthPyramidUtil.glsl's DEPTH_PYRAMID_MOST_READ texels of
    /// 2^ALDepthPyramid::LEVELS pixels, less one for where the box's image may begin in its first texel).
    static F32 screenReach();

    /// The boxes the open batch holds.
    U32 getOpenCount() const { return (U32)(mBoxes.size() / 2); }
    /// Every box dispatched, and the batches refused for want of a readback slot or a store, since release().
    U64 getDispatched() const { return mDispatched; }
    U32 getRefused() const { return mRefused; }

private:
    // A batch whose answers are on their way: its serial and its boxes, in the readback's order.
    struct Flight
    {
        U32 mBatch = 0;
        U32 mCount = 0;
    };
    // A batch's answers, a bit a box in each of two sets: whether it was seen, and whether the pyramid could not tell.
    struct Answers
    {
        U32              mBatch = 0;
        U32              mCount = 0;
        std::vector<U64> mSeen;
        std::vector<U64> mUnanswered;
    };

    // Stores for `boxes` boxes a batch, made again past the room they have. False where GL could not make them.
    bool reserve(U32 boxes);
    bool isInFlight(U32 batch) const;

    std::vector<LLVector4a> mBoxes;        // the open batch: centre then half extent, a box
    U32                     mBatch = 1;    // the open batch's serial
    U32                     mCapacity = 0; // the boxes a batch's stores hold
    ALGPUStream             mStream;       // a region a batch: its boxes, then the GPU's answers
    ALGPUReadback           mReadback;     // a slot a batch: the answers, a word a box
    std::deque<Flight>      mFlights;      // the batches whose answers are on their way, oldest first
    Answers                 mAnswers[RESULT_BATCHES];
    std::vector<U32>        mWords;        // collect()'s scratch
    U64                     mDispatched = 0;
    U32                     mRefused = 0;
};
