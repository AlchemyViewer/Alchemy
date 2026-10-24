/**
 * @file alboxocclusion.cpp
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

#include "linden_common.h"

#include "alboxocclusion.h"

#include "aldepthpyramid.h"
#include "llglslshader.h"
#include "llmatrix4a.h"
#include "llrender.h"
#include "llshadermgr.h"

#include <algorithm>
#include <cstring>

namespace
{
    // A box's words in a batch's region: its centre and half extent, a vec4 each (groupOcclusionC.glsl).
    constexpr size_t BOX_BYTES = 2 * sizeof(LLVector4a);
    // And its answer's, after every box.
    constexpr size_t ANSWER_BYTES = sizeof(U32);
    // The invocations of a work group (groupOcclusionC.glsl's local_size_x).
    constexpr U32 GROUP = 64;
    // The fewest boxes a batch's stores are made for, so that a light scene does not make them again box by box.
    constexpr U32 MIN_CAPACITY = 1024;
    // depthPyramidUtil.glsl's DEPTH_PYRAMID_MOST_READ.
    constexpr U32 MOST_READ = 4;
}

//static
F32 ALBoxOcclusion::screenReach()
{
    return F32((MOST_READ - 1) << ALDepthPyramid::LEVELS);
}

bool ALBoxOcclusion::canAdd() const
{
    return getOpenCount() < MAX_BOXES && mFlights.size() < IN_FLIGHT;
}

ALBoxOcclusion::Ticket ALBoxOcclusion::add(const LLVector4a& center, const LLVector4a& half_extent)
{
    if (!canAdd())
    {
        return {};
    }
    Ticket ticket;
    ticket.mBatch = mBatch;
    ticket.mIndex = getOpenCount();
    mBoxes.push_back(center);
    mBoxes.push_back(half_extent);
    return ticket;
}

bool ALBoxOcclusion::reserve(U32 boxes)
{
    if (boxes <= mCapacity && mStream.isAllocated() && mReadback.isAllocated())
    {
        return true;
    }
    // Made again larger: the answers on their way in the old readback go with it, and their batches expire.
    mFlights.clear();
    U32 capacity = std::max(mCapacity, MIN_CAPACITY);
    while (capacity < boxes)
    {
        capacity *= 2;
    }
    capacity = std::min(capacity, MAX_BOXES);
    if (!mStream.allocate(capacity * (BOX_BYTES + ANSWER_BYTES), IN_FLIGHT + 1, "Occlusion boxes")
        || !mReadback.allocate(capacity * ANSWER_BYTES, IN_FLIGHT, "Occlusion answers"))
    {
        LL_WARNS_ONCE("BoxOcclusion") << "No stores for the boxes the depth pyramid tests; they are asked by query" << LL_ENDL;
        mStream.release();
        mReadback.release();
        mCapacity = 0;
        return false;
    }
    mCapacity = capacity;
    return true;
}

bool ALBoxOcclusion::dispatch(LLGLSLShader& program, ALDepthPyramid& pyramid, const LLMatrix4a& view_proj)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    const U32 count = getOpenCount();
    const U32 batch = mBatch;
    // The next batch opens whatever becomes of this one: a ticket of a batch never dispatched expires.
    ++mBatch;
    if (mBatch == 0)
    {
        mBatch = 1;
    }
    if (count == 0)
    {
        return true;
    }

    const bool ok = LLRender::sCompute && program.isComplete() && pyramid.isAllocated() && mFlights.size() < IN_FLIGHT &&
                    reserve(count);
    if (!ok)
    {
        ++mRefused;
        mBoxes.clear();
        return false;
    }

    // The boxes into the batch's region, its answers to follow them there.
    const size_t box_bytes = size_t(count) * BOX_BYTES;
    U8* region = mStream.map();
    if (!region)
    {
        ++mRefused;
        mBoxes.clear();
        return false;
    }
    std::memcpy(region, mBoxes.data(), box_bytes);
    const size_t offset = mStream.publish(box_bytes);
    mBoxes.clear();

    const S32 binding = LLShaderMgr::instance()->getStorageBlockBinding(LLShaderMgr::OCCLUSION_BOXES);
    llassert(binding >= 0);
    program.bind();
    pyramid.bind(program);
    program.uniformMatrix4fv(LLShaderMgr::OCCLUSION_VIEW_PROJ, view_proj);
    program.uniform1i(LLShaderMgr::OCCLUSION_BOX_COUNT, (S32)count);
    gGL.bindStorageBuffer((U32)binding, mStream.getBuffer(), offset, box_bytes + size_t(count) * ANSWER_BYTES);
    program.dispatch((count + GROUP - 1) / GROUP);
    gGL.unbindStorageBuffer((U32)binding);
    program.unbindTexture(LLShaderMgr::DEPTH_PYRAMID);
    program.unbind();

    // The answers, read back once the frame is shown finished. The copy follows the dispatch's writes past the
    // barrier the request makes.
    if (!mReadback.request(mStream.getBuffer(), offset + box_bytes, size_t(count) * ANSWER_BYTES))
    {
        ++mRefused;
        return false;
    }
    mFlights.push_back({ batch, count });
    mDispatched += count;
    LL_PROFILE_PLOT("occlusion boxes", (int64_t)count);
    return true;
}

void ALBoxOcclusion::collect()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    while (!mFlights.empty())
    {
        const Flight flight = mFlights.front();
        mWords.resize(flight.mCount);
        if (!mReadback.collect(mWords.data(), size_t(flight.mCount) * ANSWER_BYTES))
        {
            // Not finished yet: nor is any after it.
            break;
        }
        mFlights.pop_front();

        Answers& answers = mAnswers[flight.mBatch % RESULT_BATCHES];
        answers.mBatch = flight.mBatch;
        answers.mCount = flight.mCount;
        answers.mSeen.assign((flight.mCount + 63) / 64, 0);
        answers.mUnanswered.assign((flight.mCount + 63) / 64, 0);
        for (U32 i = 0; i < flight.mCount; ++i)
        {
            // depthPyramidUtil.glsl's DEPTH_PYRAMID_HIDDEN, _SEEN and _UNANSWERED: 0, 1 and 2.
            const U64 bit = U64(1) << (i % 64);
            if (mWords[i] == 1)
            {
                answers.mSeen[i / 64] |= bit;
            }
            else if (mWords[i] != 0)
            {
                answers.mUnanswered[i / 64] |= bit;
            }
        }
    }
}

bool ALBoxOcclusion::isInFlight(U32 batch) const
{
    for (const Flight& flight : mFlights)
    {
        if (flight.mBatch == batch)
        {
            return true;
        }
    }
    return false;
}

ALBoxOcclusion::Status ALBoxOcclusion::result(const Ticket& ticket) const
{
    if (!ticket.isValid())
    {
        return Status::Expired;
    }
    const Answers& answers = mAnswers[ticket.mBatch % RESULT_BATCHES];
    if (answers.mBatch == ticket.mBatch && ticket.mIndex < answers.mCount)
    {
        const U32 word = ticket.mIndex / 64;
        const U32 bit  = ticket.mIndex % 64;
        if ((answers.mUnanswered[word] >> bit) & 1)
        {
            return Status::Unanswered;
        }
        return (answers.mSeen[word] >> bit) & 1 ? Status::Seen : Status::Hidden;
    }
    if (ticket.mBatch == mBatch || isInFlight(ticket.mBatch))
    {
        return Status::Pending;
    }
    return Status::Expired;
}

void ALBoxOcclusion::release()
{
    mStream.release();
    mReadback.release();
    mCapacity = 0;
    mFlights.clear();
    mBoxes.clear();
    for (Answers& answers : mAnswers)
    {
        answers.mBatch = 0;
        answers.mCount = 0;
        answers.mSeen.clear();
        answers.mUnanswered.clear();
    }
    // A ticket taken before is of no batch to come.
    ++mBatch;
    if (mBatch == 0)
    {
        mBatch = 1;
    }
    mDispatched = 0;
    mRefused    = 0;
}
