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

#include <atomic>
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
        explicit TestQueuedThread(bool should_pause = false) : LLQueuedThread("llqueuedthread_test", true, should_pause) {}

        using LLQueuedThread::addRequest;
        using LLQueuedThread::generateHandle;
        using LLQueuedThread::retryRequest;

        // Runs `work` on the queue's thread, in turn with its requests.
        void post(std::function<void()> work) { mRequestQueue.post(std::move(work)); }
    };

    // A queued thread with an update of its own to run each update().
    class UpdatingQueuedThread : public LLQueuedThread
    {
    public:
        UpdatingQueuedThread() : LLQueuedThread("llqueuedthread_test updating") {}

        std::atomic<int>           mUpdates{ 0 };
        std::atomic<LLThread::id_t> mUpdatedOn{};

    private:
        void threadedUpdate() override
        {
            mUpdatedOn = LLThread::currentID();
            ++mUpdates;
        }
        bool hasThreadedUpdate() const override { return true; }
    };

    // A request that runs `work` each time the queue gives it a turn, and is
    // done when `work` says so.
    class TestRequest : public LLQueuedThread::QueuedRequest
    {
    public:
        TestRequest(LLQueuedThread::handle_t handle, std::function<bool()> work, bool waits_for_wake = false) :
            QueuedRequest(handle, LLQueuedThread::FLAG_AUTO_COMPLETE),
            mWork(std::move(work)),
            mWaitsForWake(waits_for_wake)
        {
        }

    protected:
        bool processRequest() override { return mWork(); }
        bool waitsForWake() override { return mWaitsForWake; }

    private:
        std::function<bool()> mWork;
        bool                  mWaitsForWake;
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
                thread.addRequest(new TestRequest(thread.generateHandle(), [&]()
                    {
                        log.note("B");
                        return true;
                    }));
                // Read once B is posted, so no earlier than B's time in the
                // schedule. A's retry is due no sooner than 16 ms after
                // retry_asked.
                b_added = clock.getElapsedTimeF64();
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

    TEST_CASE("llqueuedthread: a woken request runs at once and a stale retry runs nothing", "[llqueuedthread]")
    {
        // A asks to retry every turn until it is told it is done. Woken
        // after its first turn, it runs again ahead of the retry due 16 ms
        // on, so a check posted after the wake finds its second turn run.
        // That first retry is then stale: run there and then, it does not
        // run A again.
        TestQueuedThread thread;
        thread.start();
        LLTimer clock;
        std::atomic<int> turns{ 0 };
        std::atomic<bool> done{ false };
        F64 first_turn(0.0);
        const LLQueuedThread::handle_t handle(thread.generateHandle());
        thread.addRequest(new TestRequest(handle, [&]()
            {
                if (0 == turns.load())
                {
                    first_turn = clock.getElapsedTimeF64();
                }
                ++turns;
                return done.load();
            }));
        while (turns.load() < 1 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        REQUIRE(turns.load() == 1);

        const F64 woken(clock.getElapsedTimeF64());
        thread.wakeRequest(handle);
        std::atomic<int> after_wake{ -1 };
        std::atomic<int> after_stale{ -1 };
        thread.post([&]()
            {
                after_wake = turns.load();
                thread.retryRequest(handle, 1); // the first retry's generation
                after_stale = turns.load();
                done = true;
            });
        while (after_stale.load() < 0 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        REQUIRE(after_stale.load() >= 0);
        CAPTURE(after_wake.load(), after_stale.load());

        CHECK(after_stale.load() == after_wake.load());
        if (woken - first_turn < 0.016)
        {
            CHECK(after_wake.load() == 2);
        }
        else
        {
            WARN("A was woken after its retry was due; which ran it says nothing");
        }
    }

    TEST_CASE("llqueuedthread: a request waiting for a wake is retried only as a fallback", "[llqueuedthread]")
    {
        // A waits for a wake every turn. Over 100 ms, well inside its
        // fallback, it is not run again; a request retried every 16 ms would
        // have been several times. Woken, it runs again at once.
        TestQueuedThread thread;
        thread.start();
        LLTimer clock;
        std::atomic<int> turns{ 0 };
        std::atomic<bool> done{ false };
        const LLQueuedThread::handle_t handle(thread.generateHandle());
        thread.addRequest(new TestRequest(handle, [&]()
            {
                ++turns;
                return done.load();
            }, true));
        while (turns.load() < 1 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        REQUIRE(turns.load() == 1);

        ms_sleep(100);
        CHECK(turns.load() == 1);

        thread.wakeRequest(handle);
        std::atomic<int> after_wake{ -1 };
        thread.post([&]()
            {
                after_wake = turns.load();
                done = true;
            });
        while (after_wake.load() < 0 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        CHECK(after_wake.load() == 2);
    }

    TEST_CASE("llqueuedthread: an abort reaches a request waiting for a wake at once", "[llqueuedthread]")
    {
        // Aborted while waiting for a wake, A is run, sees the abort and is
        // gone before a check posted after the abort, not at its fallback.
        TestQueuedThread thread;
        thread.start();
        LLTimer clock;
        std::atomic<int> turns{ 0 };
        const LLQueuedThread::handle_t handle(thread.generateHandle());
        thread.addRequest(new TestRequest(handle, [&turns]()
            {
                ++turns;
                return false;
            }, true));
        while (turns.load() < 1 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        REQUIRE(turns.load() == 1);

        thread.abortRequest(handle, true);
        std::atomic<int> status_after{ -100 };
        thread.post([&]()
            {
                status_after = thread.getRequestStatus(handle);
            });
        while (-100 == status_after.load() && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        CHECK(LLQueuedThread::STATUS_EXPIRED == status_after.load());
        CHECK(turns.load() == 1);
    }

    TEST_CASE("llqueuedthread: shutdown with a request retrying does not wait out its timeout", "[llqueuedthread]")
    {
        // The request asks for another turn every time, so a retry is always
        // queued. Shutdown runs it, sees it abort, and stops the thread, well
        // inside the five seconds it waits for the thread before giving up.
        TestQueuedThread thread;
        thread.start();
        std::atomic<int> turns{ 0 };
        thread.addRequest(new TestRequest(thread.generateHandle(), [&turns]()
            {
                ++turns;
                return false;
            }));
        LLTimer clock;
        while (turns.load() < 1 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        REQUIRE(turns.load() >= 1);

        clock.reset();
        thread.shutdown();
        CHECK(thread.isStopped());
        CHECK(clock.getElapsedTimeF64() < 2.5);
    }

    TEST_CASE("llqueuedthread: update leaves a queue with no update of its own asleep", "[llqueuedthread]")
    {
        // Made paused, the thread waits until something is pending. A queue
        // whose threadedUpdate() is the base's empty one has nothing to post,
        // so update() finds nothing pending and leaves it paused.
        TestQueuedThread thread(true);
        thread.start();
        for (int i(0); i < 3; ++i)
        {
            thread.update(0.f);
        }
        CHECK(thread.getPending() == 0);
        CHECK(thread.isPaused());
    }

    TEST_CASE("llqueuedthread: update runs a queue's own update on its thread", "[llqueuedthread]")
    {
        UpdatingQueuedThread thread;
        thread.start();
        LLTimer clock;
        thread.update(0.f);
        while (thread.mUpdates.load() < 1 && clock.getElapsedTimeF64() < 10.0)
        {
            ms_sleep(1);
        }
        CHECK(thread.mUpdates.load() == 1);
        CHECK(thread.mUpdatedOn.load() != LLThread::currentID());
    }
}
