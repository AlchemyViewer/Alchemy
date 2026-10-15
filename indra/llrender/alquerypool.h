/**
 * @file alquerypool.h
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

#pragma once

#include "stdtypes.h"

#include <vector>

/// What a query begun with ALQueryPool::begin() counts. GPU time is counted by spans (ALQueryPool::beginSpan()), never
/// by a kind.
enum class ALQueryKind : U8
{
    Occlusion,      ///< whether any sample passed; conservative where the context has it, erring only toward yes
    OcclusionExact, ///< whether any sample passed, never conservative
    Samples,        ///< how many samples passed
    Primitives,     ///< primitives generated
    Statistics,     ///< every pipeline statistic of ALStatistic at once, where the context counts them
    Count
};

/// The pipeline statistics a Statistics query counts. Each of its queries counts all of them, as a pipeline statistics
/// query counts the set its query pool was made with in the explicit APIs.
enum class ALStatistic : U8
{
    FragmentInvocations, ///< fragment shader invocations
    ClippingPrimitives,  ///< primitives that reached clipping
    Count
};

/// What a Statistics query counted.
struct ALStatistics
{
    U64 mCounts[U32(ALStatistic::Count)] = {};

    U64 operator[](ALStatistic statistic) const { return mCounts[U32(statistic)]; }
};

/// One query of a frame's batch. The default is no query, which every call takes and does nothing with.
class ALQueryTicket
{
public:
    bool isValid() const { return mSerial != 0; }

    bool operator==(const ALQueryTicket& other) const
    {
        return mSerial == other.mSerial && mSlot == other.mSlot && mKind == other.mKind;
    }

private:
    friend class ALQueryPool;

    U32 mSerial   = 0; // the batch's frame, 0 for none
    U32 mSlot : 24 = 0;
    U32 mKind : 8  = 0; // an ALQueryKind, or one of the pool's own kinds for spans
};

/// The GPU time between two points in the commands: two timestamps where the context's count time, which nest and
/// overlap freely, and one TIME_ELAPSED query elsewhere, which does not nest and is GL's alone.
class ALQuerySpan
{
public:
    bool isValid() const { return mBegin.isValid(); }

private:
    friend class ALQueryPool;

    ALQueryTicket mBegin; // the first timestamp, or the elapsed query
    ALQueryTicket mEnd;   // the second timestamp; none while the span is open, and none for an elapsed span
};

/// The queries a context issues, from one pool. Every query begun in a frame belongs to that frame's batch; endFrame()
/// fences the batch, and retire() reads every result of a batch at once when its fence has signalled, so that reading a
/// result never waits on the GPU and never polls one query. FRAMES batches of names are in flight, and a batch's names
/// are begun again only once it has been read, so none is begun while its last result is pending. The results are kept
/// apart from the names, for RESULT_FRAMES frames after their own: a user that reads its queries only every few frames,
/// as the object cache's cull does while the camera rests (LLVOCachePartition::cull), still finds them. Past that a
/// ticket has expired.
///
/// One query may be open per exclusive target: GL counts SAMPLES_PASSED and the two ANY_SAMPLES_PASSED targets as one,
/// and the pipeline statistics are kept as one, as the explicit APIs keep them. begin() refuses a query whose target is
/// held -- by another query open on it, or by reserve() -- and the draws run uncounted. Its user takes the missing
/// result as it would an expired one. A query is begun and ended within one render pass (checkPassBoundary()).
///
/// All operations require the GL context the pool's queries were made in to be current. Non-copyable.
class ALQueryPool
{
public:
    static constexpr U32 FRAMES        = 4;
    static constexpr U32 RESULT_FRAMES = 64;

    enum class Status : U8
    {
        Pending, ///< issued, and its frame not read yet
        Ready,   ///< read
        Expired, ///< no query, or one whose results have been dropped, RESULT_FRAMES frames on, or released
    };

    ALQueryPool() = default;
    ALQueryPool(const ALQueryPool&) = delete;
    ALQueryPool& operator=(const ALQueryPool&) = delete;

    /// Whether the context can count `kind` at all.
    static bool supports(ALQueryKind kind);

    /// Whether begin(kind) would begin a query now: the context counts it, and its target is neither open nor
    /// reserved.
    bool canBegin(ALQueryKind kind) const;

    /// Starts counting the commands that follow, until end(). No query, beginning nothing, where canBegin() says no.
    ALQueryTicket begin(ALQueryKind kind);

    /// Ends the query `ticket` began. A ticket that began nothing ends nothing; one that is not open is a misuse.
    void end(const ALQueryTicket& ticket);

    /// Starts a span: no span where the context has no timestamps and its elapsed query is held.
    ALQuerySpan beginSpan();
    void        endSpan(ALQuerySpan& span);

    /// What the query counted, once its frame has been read.
    Status result(const ALQueryTicket& ticket, U64& value) const;
    /// What the Statistics query counted, once its frame has been read.
    Status result(const ALQueryTicket& ticket, ALStatistics& statistics) const;
    /// The span's nanoseconds, once its frame has been read.
    Status result(const ALQuerySpan& span, U64& nanoseconds) const;

    /// What the query counted, waiting for the GPU where it is pending: for tests, for teardown, and for a profile
    /// that is read in the frame it is taken. False for one expired or still open.
    bool wait(const ALQueryTicket& ticket, U64& value);
    bool wait(const ALQueryTicket& ticket, ALStatistics& statistics);
    bool wait(const ALQuerySpan& span, U64& nanoseconds);

    /// Holds the target `kind` is begun with, or lets it go: while it is held, begin() refuses every kind begun with
    /// that target, and only beginReserved() begins one. For a user that counts across a whole frame, such as the
    /// shader profiler.
    void          reserve(ALQueryKind kind, bool reserved);
    ALQueryTicket beginReserved(ALQueryKind kind);

    /// Draws issued until endConditional() run only where the occlusion or samples query `ticket`, begun and ended in
    /// this frame, let a sample through: the GPU waits for its count, the CPU for nothing. With no query they run.
    /// Not nested.
    void beginConditional(const ALQueryTicket& ticket);
    void endConditional();

    /// A render pass ends: the framebuffer is about to change (LLRenderTarget). A query that counts draws is begun and
    /// ended within one pass, as the explicit APIs require, and one still open here is logged, once a session; only
    /// the elapsed query of a span, which is GL's alone, may cross. Checked where asserts are.
    void checkPassBoundary() const;

    /// Reads every closed batch whose fence has signalled, oldest first, without waiting. Once a frame, before the
    /// results are wanted.
    void retire();

    /// Closes the frame's batch with a fence, and opens the next in the oldest one's place, reading that first: waiting
    /// for it only when the GPU is FRAMES frames behind. Once a frame, by LLRender::endFrame().
    void endFrame();

    /// Deletes every query and fence, while the context is current. Every ticket expires.
    void release();

    /// The frames whose batch endFrame() had to wait for.
    U32 getStalls() const { return mStalls; }
    /// The GL query objects the pool holds, in use or not.
    U32 getNameCount() const;

private:
    // The kinds a span is made of, after the public ones.
    static constexpr U32 KIND_ELAPSED   = U32(ALQueryKind::Count);
    static constexpr U32 KIND_TIMESTAMP = KIND_ELAPSED + 1;
    static constexpr U32 KINDS          = KIND_TIMESTAMP + 1;
    static constexpr U32 STATISTICS     = U32(ALStatistic::Count);
    static constexpr U32 TARGETS        = 4;

    struct Batch
    {
        U32              mSerial  = 0;       // the frame it holds, 0 for none
        void*            mFence   = nullptr; // the GLsync after its last command, while closed and not read
        bool             mClosed  = false;
        bool             mRead    = false;
        bool             mFlushed = false;   // the fence has been flushed toward the GPU
        std::vector<U32> mNames[KINDS];      // namesPer(kind) a slot, kept from frame to frame
        U32              mUsed[KINDS] = {};  // slots
    };

    // A read frame's results, by kind and slot, namesPer(kind) a slot.
    struct Results
    {
        U32              mSerial = 0;        // the frame they are, 0 for none
        std::vector<U64> mValues[KINDS];
    };

    Batch&       batch(U32 serial) { return mBatches[serial % FRAMES]; }
    const Batch* find(const ALQueryTicket& ticket) const;
    // The ticket's first result where its frame's results are still kept, or null.
    const U64*   kept(const ALQueryTicket& ticket) const;

    // The GL names a slot of `kind` holds: one for each pipeline statistic, one for every other kind.
    static U32  namesPer(U32 kind) { return kind == U32(ALQueryKind::Statistics) ? STATISTICS : 1; }
    // The exclusive target a kind is begun with, or -1 for one begun with none.
    static S32  targetOf(U32 kind);
    // The GL target a kind other than Statistics is begun or counted with.
    static U32  glTarget(U32 kind);
    // Whether the context counts `kind`, of the public kinds or the pool's own.
    static bool countable(U32 kind);
    // Begins a query of `kind` on its names, or ends the one open on its target.
    static void beginGL(U32 kind, const U32* names);
    static void endGL(U32 kind);

    bool          isFree(U32 kind) const;
    ALQueryTicket take(U32 kind);
    ALQueryTicket open(U32 kind);
    ALQueryTicket timestamp();
    // The results of `count` names from the ticket's first, read now or found kept.
    Status        results(const ALQueryTicket& ticket, U64* values, U32 count) const;
    bool          waitFor(const ALQueryTicket& ticket, U64* values, U32 count);
    bool          isOpenIn(U32 serial) const;
    void          read(Batch& batch);
    void          fence(Batch& batch);

    Batch         mBatches[FRAMES];
    Results       mResults[RESULT_FRAMES];
    U32           mSerial = 1;      // the open batch's frame
    U32           mFirstSerial = 1; // the first frame since release(): a ticket before it is gone
    ALQueryTicket mOpen[TARGETS];
    bool          mReserved[TARGETS] = {};
    bool          mConditional = false;
    U32           mStalls = 0;
};

/// begin() for the scope, end() as it goes. ticket() is what began: no query where the pool refused.
class ALQueryScope
{
public:
    ALQueryScope(ALQueryPool& pool, ALQueryKind kind) : mPool(pool), mTicket(pool.begin(kind)) {}
    ~ALQueryScope() { mPool.end(mTicket); }

    ALQueryScope(const ALQueryScope&) = delete;
    ALQueryScope& operator=(const ALQueryScope&) = delete;

    const ALQueryTicket& ticket() const { return mTicket; }

private:
    ALQueryPool&  mPool;
    ALQueryTicket mTicket;
};

/// beginConditional() for the scope, endConditional() as it goes.
class ALConditionalRender
{
public:
    ALConditionalRender(ALQueryPool& pool, const ALQueryTicket& ticket) : mPool(pool) { pool.beginConditional(ticket); }
    ~ALConditionalRender() { mPool.endConditional(); }

    ALConditionalRender(const ALConditionalRender&) = delete;
    ALConditionalRender& operator=(const ALConditionalRender&) = delete;

private:
    ALQueryPool& mPool;
};
