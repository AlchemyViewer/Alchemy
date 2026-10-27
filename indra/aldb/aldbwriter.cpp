/**
 * @file aldbwriter.cpp
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

#include "linden_common.h"

#include "aldbwriter.h"

#include "threadpool.h"

#include <sqlite3.h>

#include <fmt/format.h>

#include <algorithm>

namespace
{
    // Every live pool has a name of its own, and two files of one name --
    // the global settings.db and an account's -- may be open at once.
    std::atomic<U32> sThreads{ 0 };
}

void ALDbWriter::finish(Item& item, bool committed)
{
    if (item.done)
    {
        item.done->set_value(committed);
    }
    if (item.completed)
    {
        try
        {
            item.completed(committed);
        }
        catch (const std::exception& e)
        {
            LL_WARNS("ALDb") << "a job's completion threw: " << e.what() << LL_ENDL;
        }
        catch (...)
        {
            LL_WARNS("ALDb") << "a job's completion threw" << LL_ENDL;
        }
    }
}

// The writer's thread: a pool of one, whose run() is the writer's loop. It
// is closed by the writer, not by the viewer's quitting, which would leave
// what was posted unwritten.
class ALDbWriter::Thread final : public LL::ThreadPool
{
public:
    Thread(const std::string& name, std::shared_ptr<ALDbWriter> writer)
    :   LL::ThreadPool(name, 1, 1, /*auto_shutdown*/ false, /*fixed_width*/ true),
        mWriter(std::move(writer))
    {
    }

    ~Thread() override { close(); }

    void run() override
    {
        // The writer's, until the loop is done.
        std::shared_ptr<ALDbWriter> writer = std::move(mWriter);
        writer->run();
    }

private:
    std::shared_ptr<ALDbWriter> mWriter;
};

ALDbWriter::ALDbWriter(std::string name, const ALDbOptions& options)
:   mName(std::move(name)),
    mRetryFirst(options.retryFirst),
    mRetryLongest(options.retryLongest),
    mOnFailure(options.onFailure)
{
}

ALDbWriter::~ALDbWriter() = default;

void ALDbWriter::start()
{
    mThread = std::make_unique<Thread>(fmt::format("ALDb:{}#{}", mName, ++sThreads), shared_from_this());
    mThread->start();
}

bool ALDbWriter::closing() const
{
    LLCoros::LockType lock(mMutex);
    return mClosing;
}

bool ALDbWriter::post(Job job, Done done, Completion completed)
{
    {
        LLCoros::LockType lock(mMutex);
        if (mClosing)
        {
            return false;
        }
        mQueue.push_back(Item{ std::move(job), nullptr, std::move(done), std::move(completed) });
        mQueued = mQueue.size();
    }
    mWake.notify_one();
    return true;
}

bool ALDbWriter::control(Control control)
{
    llassert(!onWriterThread());
    auto                  done   = std::make_shared<LLCoros::Promise<bool>>();
    LLCoros::Future<bool> result = done->get_future();
    {
        LLCoros::LockType lock(mMutex);
        if (mClosing)
        {
            return false;
        }
        mQueue.push_back(Item{ nullptr, std::move(control), done, nullptr });
        mQueued = mQueue.size();
    }
    mWake.notify_one();
    return result.get();
}

bool ALDbWriter::close(std::chrono::milliseconds wait)
{
    {
        LLCoros::LockType lock(mMutex);
        if (!mClosing)
        {
            mClosing  = true;
            mDeadline = std::chrono::steady_clock::now() + wait;
        }
    }
    mWake.notify_all();
    if (!mThread)
    {
        return true;
    }
    // A little past the deadline: the writer gives up at the deadline and
    // still has the file to checkpoint and close.
    if (mFinished.wait_for(wait + std::chrono::milliseconds(500)))
    {
        mThread.reset();
        return true;
    }
    LL_WARNS("ALDb") << mName << ": the writer did not finish within " << wait.count()
                     << " ms of closing; left to finish on its own" << LL_ENDL;
    // The pool goes with its thread, and the writer the thread holds with it.
    LL::ThreadPoolBase::abandon(std::move(mThread));
    return false;
}

ALDbStats ALDbWriter::stats() const
{
    ALDbStats stats;
    stats.commits        = mCommits;
    stats.committedItems = mCommittedItems;
    stats.droppedItems   = mDroppedItems;
    stats.retries        = mRetries;
    stats.queued         = mQueued;
    return stats;
}

void ALDbWriter::run()
{
    mThreadId = LLThread::currentID();

    std::deque<Item> pending;
    for (;;)
    {
        {
            LLCoros::LockType lock(mMutex);
            if (pending.empty())
            {
                mWake.wait(lock, [this] { return !mQueue.empty() || mClosing; });
                if (mQueue.empty())
                {
                    break; // closing, and nothing left
                }
            }
            // Whatever waits behind what is being tried again goes in after it.
            while (!mQueue.empty())
            {
                pending.push_back(std::move(mQueue.front()));
                mQueue.pop_front();
            }
        }

        const bool stopped = drain(pending);
        mQueued = pending.size();
        if (!stopped)
        {
            noteSuccess();
            continue;
        }

        // A failure that may pass: wait, longer each time, then try again.
        ++mRetries;
        mDelay = mDelay.count() == 0 ? mRetryFirst : std::min(mDelay * 2, mRetryLongest);
        LLCoros::LockType lock(mMutex);
        const auto now = std::chrono::steady_clock::now();
        if (mClosing && now >= mDeadline)
        {
            lock.unlock();
            giveUp(pending);
            continue;
        }
        // Woken early by close(), to wait no longer than its deadline.
        const bool was_closing = mClosing;
        const auto until       = mClosing ? std::min(now + mDelay, mDeadline) : now + mDelay;
        mWake.wait_until(lock, until, [this, was_closing] { return mClosing && !was_closing; });
    }

    if (mConnection)
    {
        // Back into the file, and the WAL emptied, so that a file copied or
        // moved once the viewer is gone is whole without it.
        mConnection->exec("PRAGMA wal_checkpoint(TRUNCATE)");
        mConnection.reset();
    }
    mQueued = 0;
    mFinished.set_all();
}

bool ALDbWriter::drain(std::deque<Item>& pending)
{
    while (!pending.empty())
    {
        if (pending.front().control)
        {
            Item item = std::move(pending.front());
            pending.pop_front();
            runControl(item);
            continue;
        }
        size_t count = 0;
        while (count < pending.size() && !pending[count].control)
        {
            ++count;
        }
        if (runBatch(pending, count))
        {
            return true;
        }
    }
    return false;
}

void ALDbWriter::runControl(Item& item)
{
    bool ok = true;
    try
    {
        item.control(mConnection);
    }
    catch (const std::exception& e)
    {
        LL_WARNS("ALDb") << mName << ": a control job threw: " << e.what() << LL_ENDL;
        ok = false;
    }
    catch (...)
    {
        LL_WARNS("ALDb") << mName << ": a control job threw" << LL_ENDL;
        ok = false;
    }
    finish(item, ok);
}

bool ALDbWriter::runBatch(std::deque<Item>& pending, size_t count)
{
    LL_PROFILE_ZONE_SCOPED;

    // Nothing open to write to: the open failed, or the file was closed.
    if (!mConnection)
    {
        for (size_t i = 0; i < count; ++i)
        {
            finish(pending.front(), false);
            pending.pop_front();
            ++mDroppedItems;
        }
        return false;
    }

    ALDbConnection& connection = *mConnection;
    sqlite3* const  db         = connection.handle();
    connection.clearFirstError();
    // A file this process may only read takes no write lock: its reads run,
    // and its writes fail alone. One that becomes so under the connection --
    // moved aside, its media made read-only -- is begun the same way.
    bool begun = connection.exec(sqlite3_db_readonly(db, "main") == 1 ? "BEGIN" : "BEGIN IMMEDIATE") == SQLITE_OK;
    if (!begun && (connection.firstError() & 0xff) == SQLITE_READONLY)
    {
        connection.clearFirstError();
        begun = connection.exec("BEGIN") == SQLITE_OK;
    }
    if (!begun)
    {
        const int         code    = connection.firstError();
        const std::string message = connection.firstErrorMessage();
        connection.clearFirstError();
        if (ALDb::isTransient(code))
        {
            noteFailure(code, message);
            return true;
        }
        LL_WARNS("ALDb") << mName << ": could not begin a transaction, " << count << " jobs dropped: " << message
                         << LL_ENDL;
        for (size_t i = 0; i < count; ++i)
        {
            finish(pending.front(), false);
            pending.pop_front();
            ++mDroppedItems;
        }
        return false;
    }

    // Each job in a savepoint: one that fails is undone alone. One that
    // failed for good is dropped; one that failed in a way that may pass
    // stops the batch there, and it and what follows wait to be tried again.
    size_t      ran         = 0;
    bool        stopped     = false;
    bool        undone      = false;
    int         stop_code   = SQLITE_OK;
    std::string stop_message;
    std::vector<bool> dropped(count, false);
    for (; ran < count; ++ran)
    {
        Item& item = pending[ran];
        connection.clearFirstError();
        ALDbSavepoint savepoint(connection, "aldb_job");
        if (savepoint.begun())
        {
            try
            {
                item.job(connection);
            }
            catch (const std::exception& e)
            {
                connection.fail(SQLITE_ABORT, std::string("the job threw: ") + e.what());
            }
            catch (...)
            {
                connection.fail(SQLITE_ABORT, "the job threw");
            }
        }
        if (connection.firstError() == SQLITE_OK)
        {
            savepoint.release();
        }
        const int code = connection.firstError();
        if (code == SQLITE_OK)
        {
            continue;
        }
        const std::string message = connection.firstErrorMessage();
        savepoint.rollback();
        // An I/O error, a full disk or memory running out can make SQLite
        // undo the whole transaction, not the job alone: then nothing of the
        // batch is in the file, and no job may run on outside a transaction.
        undone = sqlite3_get_autocommit(db) != 0;
        if (ALDb::isTransient(code))
        {
            stopped      = true;
            stop_code    = code;
            stop_message = message;
            break;
        }
        LL_WARNS("ALDb") << mName << ": a job failed and was dropped: " << message << LL_ENDL;
        if (ALDb::isCorrupt(code) && !mDamageReported)
        {
            // Damage found after the open, which only looks after a crash.
            mDamageReported = true;
            report(code, message);
        }
        else if ((code & 0xff) == SQLITE_READONLY && !mReadOnlyReported)
        {
            mReadOnlyReported = true;
            report(code, message);
        }
        dropped[ran] = true;
        if (undone)
        {
            // Failed for good, and took the jobs before it with it.
            stop_code    = code;
            stop_message = message;
            ++ran;
            break;
        }
    }

    if (undone)
    {
        // Nothing of the batch reached the file. The jobs that failed for
        // good are done with; the others that ran, and the one that stopped
        // the batch, are tried again with what waited behind them.
        connection.clearFirstError();
        std::deque<Item> kept;
        for (size_t i = 0; i < count; ++i)
        {
            Item item = std::move(pending.front());
            pending.pop_front();
            if (i < ran && dropped[i])
            {
                finish(item, false);
                ++mDroppedItems;
            }
            else
            {
                kept.push_back(std::move(item));
            }
        }
        while (!kept.empty())
        {
            pending.push_front(std::move(kept.back()));
            kept.pop_back();
        }
        LL_DEBUGS("ALDb") << mName << ": the transaction was undone (" << ALDb::errorName(stop_code)
                          << "); its jobs are tried again: " << stop_message << LL_ENDL;
        if (stopped)
        {
            noteFailure(stop_code, stop_message);
        }
        return true;
    }

    connection.clearFirstError();
    if (connection.exec("COMMIT") != SQLITE_OK)
    {
        const int         code    = connection.firstError();
        const std::string message = connection.firstErrorMessage();
        connection.clearFirstError();
        sqlite3_exec(connection.handle(), "ROLLBACK", nullptr, nullptr, nullptr);

        // Nothing of the batch is in the file. The jobs that failed for good
        // are done with; the others that ran are tried again, or, if the
        // commit itself failed for good, dropped with them. Those the batch
        // stopped before had no part in it and wait their turn.
        const bool retry = ALDb::isTransient(code);
        std::deque<Item> kept;
        for (size_t i = 0; i < count; ++i)
        {
            Item item = std::move(pending.front());
            pending.pop_front();
            if (i >= ran || (retry && !dropped[i]))
            {
                kept.push_back(std::move(item));
            }
            else
            {
                finish(item, false);
                ++mDroppedItems;
            }
        }
        while (!kept.empty())
        {
            pending.push_front(std::move(kept.back()));
            kept.pop_back();
        }
        if (retry)
        {
            noteFailure(code, message);
            return true;
        }
        LL_WARNS("ALDb") << mName << ": a commit failed, the " << ran << " jobs in it dropped: " << message << LL_ENDL;
        if (stopped)
        {
            noteFailure(stop_code, stop_message);
        }
        return stopped;
    }

    ++mCommits;
    for (size_t i = 0; i < ran; ++i)
    {
        Item item = std::move(pending.front());
        pending.pop_front();
        if (dropped[i])
        {
            finish(item, false);
            ++mDroppedItems;
        }
        else
        {
            finish(item, true);
            ++mCommittedItems;
        }
    }
    if (stopped)
    {
        noteFailure(stop_code, stop_message);
    }
    return stopped;
}

void ALDbWriter::giveUp(std::deque<Item>& pending)
{
    size_t jobs = 0;
    while (!pending.empty())
    {
        Item item = std::move(pending.front());
        pending.pop_front();
        if (item.control)
        {
            runControl(item);
            continue;
        }
        finish(item, false);
        ++mDroppedItems;
        ++jobs;
    }
    if (jobs)
    {
        LL_WARNS("ALDb") << mName << ": closing before " << jobs
                         << " jobs could be written; they are lost" << LL_ENDL;
    }
}

void ALDbWriter::noteFailure(int code, const std::string& message)
{
    ++mConsecutiveFailures;
    LL_DEBUGS("ALDb") << mName << ": " << ALDb::errorName(code) << ", trying again: " << message << LL_ENDL;
    // Busy or locked for a moment is ordinary; anything else, or being busy
    // past a few tries, is worth saying.
    const int  primary = code & 0xff;
    const bool serious = (primary != SQLITE_BUSY && primary != SQLITE_LOCKED) || mConsecutiveFailures >= 3;
    if (serious && !mReported)
    {
        mReported = true;
        report(code, message);
    }
}

void ALDbWriter::report(int code, const std::string& message)
{
    LL_WARNS("ALDb") << mName << ": cannot write (" << ALDb::errorName(code) << "): " << message << LL_ENDL;
    if (mOnFailure)
    {
        try
        {
            mOnFailure(ALDbFailure{ code, message });
        }
        catch (...)
        {
        }
    }
}

void ALDbWriter::noteSuccess()
{
    if (mReported)
    {
        LL_INFOS("ALDb") << mName << ": writing again" << LL_ENDL;
    }
    mReported            = false;
    mConsecutiveFailures = 0;
    mDelay               = std::chrono::milliseconds(0);
}
