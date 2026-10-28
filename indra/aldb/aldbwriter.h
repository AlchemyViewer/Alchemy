/**
 * @file aldbwriter.h
 * @brief The one thread that writes a database file, and its queue.
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

#include "aldatabase.h"

#include "llcond.h"
#include "llcoros.h"
#include "llthread.h"

#include LLCOROS_MUTEX_HEADER
#include LLCOROS_CONDVAR_HEADER

#include <atomic>
#include <deque>

// The writer of one file: a thread, the connection it alone uses, and the
// jobs waiting for it. Jobs run in transactions, each job in a savepoint of
// its own; control jobs run alone, between transactions, with the connection
// itself to open, replace or close.
//
// Posted to, and waited on, from any thread or coroutine: its locks and the
// promises it keeps are the coroutines' own, so a coroutine that waits for
// it gives the rest of its thread's coroutines their turn.
//
// It lives as long as its thread: a writer left behind by close() -- on a
// disk that hangs -- keeps what it needs until it is done.
class ALDbWriter : public std::enable_shared_from_this<ALDbWriter>
{
public:
    using Job        = ALDatabase::Job;
    using Completion = ALDatabase::Completion;
    using Control    = std::function<void(std::unique_ptr<ALDbConnection>&)>;
    using Done       = std::shared_ptr<LLCoros::Promise<bool>>;

    ALDbWriter(std::string name, const ALDbOptions& options);
    ~ALDbWriter();
    ALDbWriter(const ALDbWriter&)            = delete;
    ALDbWriter& operator=(const ALDbWriter&) = delete;

    void start();

    // A job for the next transaction; `done` and `completed`, if given, say
    // whether it committed. False when closing.
    bool post(Job job, Done done = nullptr, Completion completed = nullptr);
    // Runs on the writer, alone, and waits for it. False when closing or
    // when the job threw.
    bool control(Control control);

    // See ALDatabase::close.
    bool close(std::chrono::milliseconds wait);
    bool closing() const;

    bool onWriterThread() const { return LLThread::currentID() == mThreadId.load(); }
    ALDbStats stats() const;

private:
    class Thread;

    struct Item
    {
        Job        job;
        Control    control;
        Done       done;
        Completion completed;
    };

    // Tells the item's waiter and its completion how it ended.
    static void finish(Item& item, bool committed);

    void run();
    // Writes what it can of `pending`, from the front. True when it stopped
    // at a failure that may pass, the rest left in `pending`.
    bool drain(std::deque<Item>& pending);
    bool runBatch(std::deque<Item>& pending, size_t count);
    void runControl(Item& item);
    void giveUp(std::deque<Item>& pending);
    void noteFailure(int code, const std::string& message);
    void noteSuccess();
    // Says a failure through the log and the owner's onFailure.
    void report(int code, const std::string& message);

    const std::string                       mName;
    const std::chrono::milliseconds         mRetryFirst;
    const std::chrono::milliseconds         mRetryLongest;
    const std::function<void(const ALDbFailure&)> mOnFailure;

    mutable LLCoros::Mutex                mMutex;
    LLCoros::ConditionVariable            mWake;
    std::deque<Item>                      mQueue;
    bool                                  mClosing = false;
    std::chrono::steady_clock::time_point mDeadline;

    std::unique_ptr<Thread>        mThread;
    std::atomic<LLThread::id_t>    mThreadId;
    LLOneShotCond                  mFinished;

    // The writer thread's alone.
    std::unique_ptr<ALDbConnection> mConnection;
    std::chrono::milliseconds       mDelay{ 0 };
    U32                             mConsecutiveFailures = 0;
    // A failure that may pass is said once until the writer next succeeds;
    // damage once for the writer's life, since writes that miss the damaged
    // pages still succeed and the file stays damaged until the next open;
    // a file that takes no writes once too, as a connection opened only to
    // read stays one.
    bool                            mReported            = false;
    bool                            mDamageReported      = false;
    bool                            mReadOnlyReported    = false;

    std::atomic<U64> mCommits{ 0 };
    std::atomic<U64> mCommittedItems{ 0 };
    std::atomic<U64> mDroppedItems{ 0 };
    std::atomic<U64> mRetries{ 0 };
    std::atomic<U64> mQueued{ 0 };
};
