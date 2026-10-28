/**
 * @file llqueuedthread_test.cpp
 * @brief Tests of LLQueuedThread's request queue.
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

#include "llqueuedthread.h"

#include "llmutex.h"
#include "lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

namespace
{
    // The order requests ran in, from the queue's thread.
    class RunLog
    {
    public:
        void note(const std::string& what)
        {
            LLMutexLock lock(&mMutex);
            mRuns.push_back(what);
        }

        std::vector<std::string> runs()
        {
            LLMutexLock lock(&mMutex);
            return mRuns;
        }

    private:
        LLMutex                  mMutex;
        std::vector<std::string> mRuns;
    };

    class TestQueuedThread : public LLQueuedThread
    {
    public:
        TestQueuedThread() : LLQueuedThread("llqueuedthread_test") {}

        using LLQueuedThread::addRequest;
        using LLQueuedThread::generateHandle;
    };

    // A request that runs `work` each time the queue gives it a turn, and is
    // done when `work` says so.
    class TestRequest : public LLQueuedThread::QueuedRequest
    {
    public:
        TestRequest(LLQueuedThread::handle_t handle, std::function<bool()> work) :
            QueuedRequest(handle, LLQueuedThread::FLAG_AUTO_COMPLETE),
            mWork(std::move(work))
        {
        }

    protected:
        bool processRequest() override { return mWork(); }

    private:
        std::function<bool()> mWork;
    };

    TEST_CASE("llqueuedthread: a request that asks to retry lets one queued after it run first", "[llqueuedthread]")
    {
        // A asks for a second turn. C, queued behind A, adds B as it runs,
        // which is after A asked. A's retry is due 16 ms on, so B, due at
        // once, runs before it, rather than behind a retry that holds the
        // thread asleep until its time.
        RunLog           log;
        TestQueuedThread thread;
        thread.start();

        LLTimer clock;
        F64     retry_asked(0.0);
        F64     b_added(0.0);
        int     a_turns(0);
        thread.addRequest(new TestRequest(thread.generateHandle(), [&]()
            {
                if (0 == a_turns++)
                {
                    log.note("A asks to retry");
                    retry_asked = clock.getElapsedTimeF64();
                    return false;
                }
                log.note("A again");
                return true;
            }));
        thread.addRequest(new TestRequest(thread.generateHandle(), [&]()
            {
                log.note("C");
                b_added = clock.getElapsedTimeF64();
                thread.addRequest(new TestRequest(thread.generateHandle(), [&]()
                    {
                        log.note("B");
                        return true;
                    }));
                return true;
            }));

        while (log.runs().size() < 4 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        const std::vector<std::string> runs(log.runs());
        REQUIRE(runs.size() == 4);
        CHECK(runs[0] == "A asks to retry");
        CHECK(runs[1] == "C");
        if (b_added - retry_asked < 0.016)
        {
            CHECK(runs[2] == "B");
            CHECK(runs[3] == "A again");
        }
        else
        {
            WARN("B was added after A's retry was due; the order says nothing");
        }
    }
}
