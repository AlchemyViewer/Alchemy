/**
 * @file alquerypool.cpp
 * @brief GL queries from one pool, each frame's read together once the GPU has finished the frame.
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

#include "alquerypool.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"

#include <algorithm>

namespace
{
    // The targets one query at a time may hold, by ALQueryPool::targetOf().
    enum : S32
    {
        TARGET_OCCLUSION,
        TARGET_PRIMITIVES,
        TARGET_STATISTICS,
        TARGET_ELAPSED,
    };

    [[maybe_unused]] const char* const TARGET_NAMES[] = { "occlusion", "primitives", "pipeline statistics", "elapsed" };

    // The GL target of each pipeline statistic, by ALStatistic.
    constexpr GLenum STATISTIC_TARGETS[] = { GL_FRAGMENT_SHADER_INVOCATIONS, GL_CLIPPING_INPUT_PRIMITIVES };
    static_assert(sizeof(STATISTIC_TARGETS) / sizeof(STATISTIC_TARGETS[0]) == U32(ALStatistic::Count));

    // Slots are made this many at a time.
    constexpr U32 SLOTS_AT_ONCE = 32;

    // How long endFrame() waits on one try for a batch the GPU has not finished.
    constexpr GLuint64 WAIT_NANOSECONDS = 1000000000ull;
}

//static
U32 ALQueryPool::glTarget(U32 kind)
{
    switch (kind)
    {
    case U32(ALQueryKind::Occlusion):
        return gGLManager.mHasConservativeOcclusion ? GL_ANY_SAMPLES_PASSED_CONSERVATIVE : GL_ANY_SAMPLES_PASSED;
    case U32(ALQueryKind::OcclusionExact):
        return GL_ANY_SAMPLES_PASSED;
    case U32(ALQueryKind::Samples):
        return GL_SAMPLES_PASSED;
    case U32(ALQueryKind::Primitives):
        return GL_PRIMITIVES_GENERATED;
    case KIND_ELAPSED:
        return GL_TIME_ELAPSED;
    case KIND_TIMESTAMP:
        return GL_TIMESTAMP;
    default:
        return GL_NONE;
    }
}

//static
bool ALQueryPool::supports(ALQueryKind kind)
{
    return kind < ALQueryKind::Count && countable(U32(kind));
}

//static
bool ALQueryPool::countable(U32 kind)
{
    switch (kind)
    {
    case U32(ALQueryKind::Statistics):
        return gGLManager.mHasPipelineStatistics;
    case KIND_TIMESTAMP:
        return gGLManager.mHasTimestampQuery;
    default:
        return kind < KINDS;
    }
}

//static
S32 ALQueryPool::targetOf(U32 kind)
{
    switch (kind)
    {
    case U32(ALQueryKind::Occlusion):
    case U32(ALQueryKind::OcclusionExact):
    case U32(ALQueryKind::Samples):
        return TARGET_OCCLUSION;
    case U32(ALQueryKind::Primitives):
        return TARGET_PRIMITIVES;
    case U32(ALQueryKind::Statistics):
        return TARGET_STATISTICS;
    case KIND_ELAPSED:
        return TARGET_ELAPSED;
    default:
        return -1;
    }
}

bool ALQueryPool::isFree(U32 kind) const
{
    const S32 target = targetOf(kind);
    return target >= 0 && countable(kind) && !mOpen[target].isValid() && !mReserved[target];
}

bool ALQueryPool::canBegin(ALQueryKind kind) const
{
    return kind < ALQueryKind::Count && isFree(U32(kind));
}

ALQueryTicket ALQueryPool::begin(ALQueryKind kind)
{
    if (!canBegin(kind))
    {
        return {};
    }
    return open(U32(kind));
}

ALQueryTicket ALQueryPool::beginReserved(ALQueryKind kind)
{
    const S32 target = kind < ALQueryKind::Count ? targetOf(U32(kind)) : -1;
    if (target < 0 || !countable(U32(kind)) || mOpen[target].isValid())
    {
        return {};
    }
    return open(U32(kind));
}

void ALQueryPool::reserve(ALQueryKind kind, bool reserved)
{
    const S32 target = kind < ALQueryKind::Count ? targetOf(U32(kind)) : -1;
    if (target >= 0)
    {
        mReserved[target] = reserved;
    }
}

ALQueryTicket ALQueryPool::open(U32 kind)
{
    const ALQueryTicket ticket = take(kind);
    // Draws still batched in gGL were issued before the query.
    gGL.flush();
    beginGL(kind, &batch(ticket.mSerial).mNames[kind][ticket.mSlot * namesPer(kind)]);
    mOpen[targetOf(kind)] = ticket;
    return ticket;
}

//static
void ALQueryPool::beginGL(U32 kind, const U32* names)
{
    if (kind == U32(ALQueryKind::Statistics))
    {
        for (U32 i = 0; i < STATISTICS; ++i)
        {
            glBeginQuery(STATISTIC_TARGETS[i], names[i]);
        }
    }
    else
    {
        glBeginQuery(glTarget(kind), names[0]);
    }
}

//static
void ALQueryPool::endGL(U32 kind)
{
    if (kind == U32(ALQueryKind::Statistics))
    {
        for (U32 i = 0; i < STATISTICS; ++i)
        {
            glEndQuery(STATISTIC_TARGETS[i]);
        }
    }
    else
    {
        glEndQuery(glTarget(kind));
    }
}

void ALQueryPool::end(const ALQueryTicket& ticket)
{
    // None, or one whose query went with release().
    if (!ticket.isValid() || ticket.mSerial < mFirstSerial)
    {
        return;
    }
    const S32  target = targetOf(ticket.mKind);
    const bool open   = target >= 0 && mOpen[target] == ticket;
    llassert_msg(open, "a query ended that is not open");
    if (!open)
    {
        return;
    }
    gGL.flush();
    endGL(ticket.mKind);
    mOpen[target] = {};

    // A query begun in a frame since closed ends after that batch's fence, which is laid again behind it, so that the
    // fence still follows every command of the batch and reading it never waits.
    Batch& begun = batch(ticket.mSerial);
    if (ticket.mSerial != mSerial && begun.mSerial == ticket.mSerial && begun.mClosed && !begun.mRead)
    {
        fence(begun);
    }
}

ALQueryTicket ALQueryPool::timestamp()
{
    const ALQueryTicket ticket = take(KIND_TIMESTAMP);
    // The time the draws still batched in gGL finish at, as well.
    gGL.flush();
    glQueryCounter(batch(ticket.mSerial).mNames[KIND_TIMESTAMP][ticket.mSlot], GL_TIMESTAMP);
    return ticket;
}

ALQuerySpan ALQueryPool::beginSpan()
{
    ALQuerySpan span;
    if (countable(KIND_TIMESTAMP))
    {
        span.mBegin = timestamp();
    }
    else if (isFree(KIND_ELAPSED))
    {
        span.mBegin = open(KIND_ELAPSED);
    }
    return span;
}

void ALQueryPool::endSpan(ALQuerySpan& span)
{
    if (!span.mBegin.isValid() || span.mEnd.isValid())
    {
        return;
    }
    if (span.mBegin.mKind == KIND_ELAPSED)
    {
        end(span.mBegin);
    }
    else
    {
        span.mEnd = timestamp();
    }
}

ALQueryTicket ALQueryPool::take(U32 kind)
{
    Batch& open = batch(mSerial);
    // The first batch, and the first after release(), are opened by their first query.
    open.mSerial = mSerial;
    std::vector<U32>& names = open.mNames[kind];
    const U32 per = namesPer(kind);
    if ((open.mUsed[kind] + 1) * per > names.size())
    {
        const size_t had = names.size();
        names.resize(had + SLOTS_AT_ONCE * per);
        glGenQueries(GLsizei(SLOTS_AT_ONCE * per), names.data() + had);
    }
    ALQueryTicket ticket;
    ticket.mSerial = mSerial;
    ticket.mSlot   = open.mUsed[kind]++;
    ticket.mKind   = kind;
    return ticket;
}

const ALQueryPool::Batch* ALQueryPool::find(const ALQueryTicket& ticket) const
{
    if (!ticket.isValid() || ticket.mKind >= KINDS)
    {
        return nullptr;
    }
    const Batch& found = mBatches[ticket.mSerial % FRAMES];
    if (found.mSerial != ticket.mSerial || ticket.mSlot >= found.mUsed[ticket.mKind])
    {
        return nullptr;
    }
    return &found;
}

const U64* ALQueryPool::kept(const ALQueryTicket& ticket) const
{
    if (!ticket.isValid() || ticket.mKind >= KINDS)
    {
        return nullptr;
    }
    const Results& frame = mResults[ticket.mSerial % RESULT_FRAMES];
    const U32      per   = namesPer(ticket.mKind);
    const size_t   first = size_t(ticket.mSlot) * per;
    if (frame.mSerial != ticket.mSerial || first + per > frame.mValues[ticket.mKind].size())
    {
        return nullptr;
    }
    return &frame.mValues[ticket.mKind][first];
}

ALQueryPool::Status ALQueryPool::results(const ALQueryTicket& ticket, U64* values, U32 count) const
{
    if (const U64* read = kept(ticket))
    {
        std::copy(read, read + count, values);
        return Status::Ready;
    }
    const Batch* found = find(ticket);
    return found && !found->mRead ? Status::Pending : Status::Expired;
}

ALQueryPool::Status ALQueryPool::result(const ALQueryTicket& ticket, U64& value) const
{
    llassert(ticket.mKind != U32(ALQueryKind::Statistics));
    return results(ticket, &value, 1);
}

ALQueryPool::Status ALQueryPool::result(const ALQueryTicket& ticket, ALStatistics& statistics) const
{
    llassert(!ticket.isValid() || ticket.mKind == U32(ALQueryKind::Statistics));
    if (ticket.mKind != U32(ALQueryKind::Statistics))
    {
        return Status::Expired;
    }
    return results(ticket, statistics.mCounts, STATISTICS);
}

ALQueryPool::Status ALQueryPool::result(const ALQuerySpan& span, U64& nanoseconds) const
{
    if (span.mBegin.mKind == KIND_ELAPSED)
    {
        return results(span.mBegin, &nanoseconds, 1);
    }
    U64 began = 0;
    const Status first = results(span.mBegin, &began, 1);
    if (first == Status::Expired)
    {
        return Status::Expired;
    }
    if (!span.mEnd.isValid())
    {
        // Still open.
        return Status::Pending;
    }
    U64 ended = 0;
    const Status second = results(span.mEnd, &ended, 1);
    if (second != Status::Ready || first != Status::Ready)
    {
        return second == Status::Expired ? Status::Expired : Status::Pending;
    }
    nanoseconds = ended > began ? ended - began : 0;
    return Status::Ready;
}

bool ALQueryPool::waitFor(const ALQueryTicket& ticket, U64* values, U32 count)
{
    if (const U64* read = kept(ticket))
    {
        std::copy(read, read + count, values);
        return true;
    }
    const Batch* found = find(ticket);
    if (!found || found->mRead)
    {
        return false;
    }
    const S32 target = targetOf(ticket.mKind);
    if (target >= 0 && mOpen[target] == ticket)
    {
        return false;
    }
    const U32* names = &found->mNames[ticket.mKind][ticket.mSlot * namesPer(ticket.mKind)];
    for (U32 i = 0; i < count; ++i)
    {
        GLuint64 counted = 0;
        glGetQueryObjectui64v(names[i], GL_QUERY_RESULT, &counted);
        values[i] = counted;
    }
    return true;
}

bool ALQueryPool::wait(const ALQueryTicket& ticket, U64& value)
{
    llassert(ticket.mKind != U32(ALQueryKind::Statistics));
    return waitFor(ticket, &value, 1);
}

bool ALQueryPool::wait(const ALQueryTicket& ticket, ALStatistics& statistics)
{
    llassert(!ticket.isValid() || ticket.mKind == U32(ALQueryKind::Statistics));
    return ticket.mKind == U32(ALQueryKind::Statistics) && waitFor(ticket, statistics.mCounts, STATISTICS);
}

bool ALQueryPool::wait(const ALQuerySpan& span, U64& nanoseconds)
{
    if (span.mBegin.mKind == KIND_ELAPSED)
    {
        return waitFor(span.mBegin, &nanoseconds, 1);
    }
    U64 began = 0;
    U64 ended = 0;
    if (!span.mEnd.isValid() || !waitFor(span.mBegin, &began, 1) || !waitFor(span.mEnd, &ended, 1))
    {
        return false;
    }
    nanoseconds = ended > began ? ended - began : 0;
    return true;
}

void ALQueryPool::beginConditional(const ALQueryTicket& ticket)
{
    llassert(!mConditional);
    // Of this frame: the names of an earlier frame's batch may be counting again.
    llassert(!ticket.isValid() || ticket.mSerial == mSerial);
    const Batch* found = targetOf(ticket.mKind) == TARGET_OCCLUSION ? find(ticket) : nullptr;
    const bool   open  = mOpen[TARGET_OCCLUSION] == ticket;
    llassert(!found || !open);
    if (found && !open)
    {
        gGL.flush();
        glBeginConditionalRender(found->mNames[ticket.mKind][ticket.mSlot], GL_QUERY_WAIT);
        mConditional = true;
    }
}

void ALQueryPool::endConditional()
{
    if (mConditional)
    {
        gGL.flush();
        glEndConditionalRender();
        mConditional = false;
    }
}

void ALQueryPool::checkPassBoundary() const
{
#ifdef SHOW_ASSERT
    for (S32 target = 0; target < S32(TARGETS); ++target)
    {
        if (target != TARGET_ELAPSED && mOpen[target].isValid())
        {
            LL_WARNS_ONCE("Queries") << "A " << TARGET_NAMES[target] << " query is open across a render pass" << LL_ENDL;
        }
    }
#endif
}

bool ALQueryPool::isOpenIn(U32 serial) const
{
    for (const ALQueryTicket& open : mOpen)
    {
        if (open.isValid() && open.mSerial == serial)
        {
            return true;
        }
    }
    return false;
}

void ALQueryPool::read(Batch& batch)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    // The fence has signalled, and it follows every command of the batch, so every result is in and reading it does
    // not wait.
    Results& frame = mResults[batch.mSerial % RESULT_FRAMES];
    frame.mSerial = batch.mSerial;
    for (U32 k = 0; k < KINDS; ++k)
    {
        const U32 names = batch.mUsed[k] * namesPer(k);
        frame.mValues[k].resize(names);
        for (U32 i = 0; i < names; ++i)
        {
            GLuint64 counted = 0;
            glGetQueryObjectui64v(batch.mNames[k][i], GL_QUERY_RESULT, &counted);
            frame.mValues[k][i] = counted;
        }
    }
    if (batch.mFence)
    {
        glDeleteSync((GLsync)batch.mFence);
        batch.mFence = nullptr;
    }
    batch.mRead = true;
}

void ALQueryPool::fence(Batch& batch)
{
    if (batch.mFence)
    {
        glDeleteSync((GLsync)batch.mFence);
    }
    batch.mFence   = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    batch.mFlushed = false;
}

void ALQueryPool::retire()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    for (U32 back = FRAMES - 1; back > 0; --back)
    {
        if (mSerial <= back)
        {
            continue;
        }
        Batch& closed = batch(mSerial - back);
        if (closed.mSerial != mSerial - back || !closed.mClosed || closed.mRead)
        {
            continue;
        }
        // A query still open in it is ended in a later frame; the batch waits for it.
        if (isOpenIn(closed.mSerial))
        {
            break;
        }
        const GLenum status = glClientWaitSync((GLsync)closed.mFence, closed.mFlushed ? 0 : GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        closed.mFlushed = true;
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
        {
            break;
        }
        read(closed);
    }
}

void ALQueryPool::endFrame()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    Batch& closing = batch(mSerial);
    closing.mSerial = mSerial;
    closing.mClosed = true;
    U32 used = 0;
    for (U32 k = 0; k < KINDS; ++k)
    {
        used += closing.mUsed[k];
    }
    if (used > 0)
    {
        fence(closing);
    }
    else
    {
        closing.mRead = true;
    }

    ++mSerial;
    Batch& reused = batch(mSerial);
    // A query left open for FRAMES frames would have its name begun again under it.
    llassert(reused.mSerial == 0 || !isOpenIn(reused.mSerial));
    bool stalled = false;
    if (reused.mSerial != 0 && !reused.mRead && reused.mFence && !isOpenIn(reused.mSerial))
    {
        GLenum status = glClientWaitSync((GLsync)reused.mFence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (status == GL_TIMEOUT_EXPIRED)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("query batch stall");
            ++mStalls;
            stalled = true;
            do
            {
                status = glClientWaitSync((GLsync)reused.mFence, GL_SYNC_FLUSH_COMMANDS_BIT, WAIT_NANOSECONDS);
            } while (status == GL_TIMEOUT_EXPIRED);
        }
        read(reused);
    }
    if (reused.mFence)
    {
        glDeleteSync((GLsync)reused.mFence);
        reused.mFence = nullptr;
    }
    reused.mSerial  = mSerial;
    reused.mClosed  = false;
    reused.mRead    = false;
    reused.mFlushed = false;
    for (U32 k = 0; k < KINDS; ++k)
    {
        reused.mUsed[k] = 0;
    }

    LL_PROFILE_PLOT("GL queries", (int64_t)used);
    LL_PROFILE_PLOT("GL query stalls", (int64_t)(stalled ? 1 : 0));
    LL_PROFILE_PLOT("GL query names", (int64_t)getNameCount());
}

U32 ALQueryPool::getNameCount() const
{
    size_t names = 0;
    for (const Batch& each : mBatches)
    {
        for (const std::vector<U32>& kind : each.mNames)
        {
            names += kind.size();
        }
    }
    return U32(names);
}

void ALQueryPool::release()
{
    // Not after the context has gone: the names went with it.
    const bool current = gGLManager.mInited;
    if (current && mConditional)
    {
        glEndConditionalRender();
    }
    mConditional = false;
    // GL keeps a query deleted while it counts until its target is ended, which would refuse the next begin.
    for (const ALQueryTicket& open : mOpen)
    {
        if (current && open.isValid())
        {
            endGL(open.mKind);
        }
    }
    for (Batch& each : mBatches)
    {
        for (U32 k = 0; k < KINDS; ++k)
        {
            if (current && !each.mNames[k].empty())
            {
                glDeleteQueries(GLsizei(each.mNames[k].size()), each.mNames[k].data());
            }
            each.mNames[k].clear();
            each.mUsed[k] = 0;
        }
        if (current && each.mFence)
        {
            glDeleteSync((GLsync)each.mFence);
        }
        each.mFence   = nullptr;
        each.mSerial  = 0;
        each.mClosed  = false;
        each.mRead    = false;
        each.mFlushed = false;
    }
    for (Results& frame : mResults)
    {
        frame.mSerial = 0;
        for (std::vector<U64>& values : frame.mValues)
        {
            values.clear();
        }
    }
    for (U32 t = 0; t < TARGETS; ++t)
    {
        mOpen[t]     = {};
        mReserved[t] = false;
    }
    // Tickets taken before stay expired.
    ++mSerial;
    mFirstSerial = mSerial;
}
