/**
 * @file alsharedmutex_test.cpp
 * @brief Unit tests for ALSharedMutex's sharing and its preference for writers
 *
 * Copyright (c) 2026, Rye <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "alsharedmutex.h"
#include "alprecisewaiter.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    // Runs a body on a thread of its own and says whether it has returned. A body
    // still blocked when the worker goes out of scope is left to finish without it:
    // a deadlock the test has already reported, or a probe kept out of a lock the
    // test still holds. The thread owns the flag it sets, and a probe owns what it
    // locks, so both outlive the worker.
    class Worker
    {
    public:
        template <typename FUNC>
        explicit Worker(FUNC&& body)
        : mDone(std::make_shared<std::atomic<bool>>(false))
        , mThread([done = mDone, body = std::forward<FUNC>(body)]() mutable
            {
                body();
                *done = true;
            })
        {
        }

        ~Worker()
        {
            if (*mDone)
            {
                mThread.join();
            }
            else
            {
                mThread.detach();
            }
        }

        bool finishesWithin(std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!*mDone)
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    return false;
                }
                ms_sleep(1);
            }
            return true;
        }

    private:
        std::shared_ptr<std::atomic<bool>> mDone;
        std::thread mThread;
    };

    // Long enough that a thread which could run has run; a body that has not
    // returned by then is blocked.
    constexpr auto BLOCKED = 50ms;
    // Long enough that a thread which was released has finished.
    constexpr auto RELEASED = 5s;

    // The lock and what the probes record, held by every probe as well as the test,
    // since a blocked probe outlives it.
    struct Shared
    {
        ALSharedMutex mMutex;
        std::atomic<int> mTurns{0};
        std::atomic<int> mWriterTurn{0};
        std::atomic<int> mReaderTurn{0};
    };

    struct alsharedmutex_data
    {
        std::shared_ptr<Shared> mShared = std::make_shared<Shared>();
        ALSharedMutex& mMutex = mShared->mMutex;

        Worker writer()
        {
            return Worker([shared = mShared]()
                {
                    std::unique_lock<ALSharedMutex> lock(shared->mMutex);
                    shared->mWriterTurn = ++shared->mTurns;
                });
        }

        Worker reader()
        {
            return Worker([shared = mShared]()
                {
                    std::shared_lock<ALSharedMutex> lock(shared->mMutex);
                    shared->mReaderTurn = ++shared->mTurns;
                });
        }
    };

    TEST_CASE_METHOD(alsharedmutex_data, "alsharedmutex: readers share and a writer waits for them", "[alsharedmutex]")
    {
        std::shared_lock<ALSharedMutex> held(mMutex);

        Worker second = reader();
        UNSCOPED_INFO("a second reader waited on the first");
        CHECK(second.finishesWithin(RELEASED));

        Worker waiting = writer();
        CHECK_FALSE(waiting.finishesWithin(BLOCKED));

        held.unlock();
        CHECK(waiting.finishesWithin(RELEASED));
    }

    TEST_CASE_METHOD(alsharedmutex_data, "alsharedmutex: a writer excludes readers and writers", "[alsharedmutex]")
    {
        std::unique_lock<ALSharedMutex> held(mMutex);

        Worker waiting_reader = reader();
        Worker waiting_writer = writer();
        CHECK_FALSE(waiting_reader.finishesWithin(BLOCKED));
        CHECK_FALSE(waiting_writer.finishesWithin(BLOCKED));

        held.unlock();
        CHECK(waiting_reader.finishesWithin(RELEASED));
        CHECK(waiting_writer.finishesWithin(RELEASED));
    }

    TEST_CASE_METHOD(alsharedmutex_data, "alsharedmutex: a reader that comes while a writer waits goes after it", "[alsharedmutex]")
    {
        // glibc's default rwlock, which is std::shared_mutex there, lets the late reader
        // straight in beside the first.
        std::shared_lock<ALSharedMutex> held(mMutex);

        Worker waiting = writer();
        CHECK_FALSE(waiting.finishesWithin(BLOCKED));

        Worker late = reader();
        UNSCOPED_INFO("the late reader got in past the waiting writer");
        CHECK_FALSE(late.finishesWithin(BLOCKED));

        held.unlock();
        CHECK(waiting.finishesWithin(RELEASED));
        CHECK(late.finishesWithin(RELEASED));
        UNSCOPED_INFO("the late reader went before the writer");
        CHECK(mShared->mWriterTurn < mShared->mReaderTurn);
    }

    TEST_CASE_METHOD(alsharedmutex_data, "alsharedmutex: a writer gets in while readers keep overlapping", "[alsharedmutex]")
    {
        // A lock that lets new readers in past a waiting writer keeps the writer out for
        // as long as the readers overlap. The SRW lock lets them in only when it wakes
        // the writer, so under load it fails here now and then.
        auto stop = std::make_shared<std::atomic<bool>>(false);
        auto read = [shared = mShared, stop]()
        {
            while (!*stop)
            {
                std::shared_lock<ALSharedMutex> lock(shared->mMutex);
                ms_sleep(1);
            }
        };
        Worker reader1(read);
        Worker reader2(read);
        Worker reader3(read);
        Worker reader4(read);
        ALPreciseWaiter::sleepFor(BLOCKED);

        Worker waiting = writer();
        UNSCOPED_INFO("the readers kept the writer out");
        CHECK(waiting.finishesWithin(RELEASED));

        *stop = true;
        CHECK(reader1.finishesWithin(RELEASED));
        CHECK(reader2.finishesWithin(RELEASED));
        CHECK(reader3.finishesWithin(RELEASED));
        CHECK(reader4.finishesWithin(RELEASED));
    }
}
