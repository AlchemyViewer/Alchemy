/**
 * @file threadpool_test.cpp
 * @brief A pool's worker sleeps until work or a deadline wakes it, and a pool
 *        let go of while its threads still run is destroyed by the last of
 *        them.
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

#include "../threadpool.h"

#include "../llcond.h"
#include "../llcoros.h"
#include "../llevents.h"
#include "../llthread.h"
#include "../lltimer.h"

#if LL_WINDOWS
#include "../llwin32headers.h"
#endif

#include "altestsanitizer.h"

#include LLCOROS_MUTEX_HEADER
#include LLCOROS_CONDVAR_HEADER
#include <boost/fiber/operations.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    // Whether done() comes true within a few seconds.
    template <typename DONE>
    bool waitFor(DONE&& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!done())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    // How long a timed wait on the pool's worker overran its deadline, each
    // of `count` times. The worker holds what it writes into as well, since a
    // wait that never ends outlives the check that gave up on it.
    std::vector<std::chrono::duration<double>> overruns(LL::ThreadPool& pool, std::chrono::milliseconds wait, size_t count)
    {
        struct Overruns
        {
            std::vector<std::chrono::duration<double>> mOverran;
            std::atomic<size_t>                         mDone{ 0 };
        };
        auto measured = std::make_shared<Overruns>();
        measured->mOverran.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const bool posted = pool.getQueue().post([measured, wait, i]()
                {
                    const auto start = std::chrono::steady_clock::now();
                    boost::this_fiber::sleep_for(wait);
                    measured->mOverran[i] = std::chrono::steady_clock::now() - start - wait;
                    measured->mDone.store(i + 1, std::memory_order_release);
                });
            REQUIRE(posted);
            REQUIRE(waitFor([&measured, i]() { return measured->mDone.load(std::memory_order_acquire) > i; }));
        }
        return measured->mOverran;
    }

    TEST_CASE("threadpool: a worker's timed wait comes back once its deadline passes", "[threadpool]")
    {
        LL::ThreadPool pool("threadpool_test_deadline", 1, 1024, false, true);
        pool.start();
        for (const auto& overran : overruns(pool, 20ms, 5))
        {
            CHECK(overran.count() >= 0.0);
        }
    }

#if LL_WINDOWS
    TEST_CASE("threadpool: an idle worker runs nothing until work comes", "[threadpool]")
    {
        std::atomic<HANDLE> worker{ nullptr };
        LL::ThreadPool      pool("threadpool_test_idle", 1, 1024, false, true);
        pool.start();
        REQUIRE(pool.getQueue().post([&worker]()
            {
                worker = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
            }));
        REQUIRE(waitFor([&worker]() { return worker.load() != nullptr; }));
        // Back to waiting for the next.
        std::this_thread::sleep_for(50ms);

        ULONG64 before = 0;
        ULONG64 after  = 0;
        REQUIRE(QueryThreadCycleTime(worker, &before));
        std::this_thread::sleep_for(250ms);
        REQUIRE(QueryThreadCycleTime(worker, &after));
        CloseHandle(worker);
        INFO("cycles while idle: " << (after - before));
        // A worker that looks for work every tick of the system timer runs
        // sixteen times or more in a quarter second, a thousand at the 1 ms
        // the viewer's window asks for, and thousands of cycles each time.
        CHECK(after - before < 20000);
    }

    TEST_CASE("threadpool: a worker's timed wait is not rounded to the system timer's tick", "[threadpool]")
    {
        LL::ThreadPool pool("threadpool_test_tick", 1, 1024, false, true);
        pool.start();
        std::vector<std::chrono::duration<double>> overran = overruns(pool, 2ms, 21);
        std::nth_element(overran.begin(), overran.begin() + overran.size() / 2, overran.end());
        const double median = overran[overran.size() / 2].count();
        INFO("median overrun: " << median * 1000.0 << " ms");
        // A wait the tick rounds wakes at the next tick: up to 15.6 ms late
        // at Windows' default.
        CHECK(median < 0.0015);
    }
#endif

    // A pool whose thread answers each ping, waiting between them as a
    // worker waits for work: on a condition of the coroutines' own.
    class EchoPool final : public LL::ThreadPool
    {
    public:
        explicit EchoPool(const std::string& name)
        :   LL::ThreadPool(name, 1, 1, /*auto_shutdown*/ false, /*fixed_width*/ true)
        {
        }

        ~EchoPool() override
        {
            {
                LLCoros::LockType lock(mMutex);
                mStop = true;
            }
            mWake.notify_all();
            close();
        }

        // Asks the thread for an answer and spins until it comes: seconds.
        F64 ping()
        {
            U32 asked = 0;
            {
                LLCoros::LockType lock(mMutex);
                asked = ++mAsked;
            }
            LLTimer timer;
            mWake.notify_one();
            while (mAnswered.load() != asked && timer.getElapsedTimeF64() < 5.0)
            {
                LLThread::yield();
            }
            return timer.getElapsedTimeF64();
        }

        void run() override
        {
            LLCoros::LockType lock(mMutex);
            while (!mStop)
            {
                mWake.wait(lock, [this] { return mStop || mAsked != mAnswered.load(); });
                mAnswered = mAsked;
            }
        }

    private:
        LLCoros::Mutex             mMutex;
        LLCoros::ConditionVariable mWake;
        U32                        mAsked = 0;
        std::atomic<U32>           mAnswered{ 0 };
        bool                       mStop = false;
    };

    TEST_CASE("threadpool: a pool's thread wakes as soon as work is posted to it", "[threadpool]")
    {
        EchoPool pool("threadpool_test_echo");
        pool.start();
        std::vector<F64> waits;
        for (int i = 0; i < 51; ++i)
        {
            waits.push_back(pool.ping());
        }
        std::nth_element(waits.begin(), waits.begin() + waits.size() / 2, waits.end());
        const F64 median = waits[waits.size() / 2];
        INFO("median wake: " << median * 1000.0 << " ms");
        // A thread that looks for work only once a tick of the system timer
        // answers half a tick late on average: 7.8 ms at Windows' default. A
        // sanitizer's run is too slow and too loaded to time a wake by: one
        // took 3 ms beside the other tests on a CI runner.
        if constexpr (!ll_test::SLOW_SANITIZER)
        {
            CHECK(median < 0.0005);
        }
    }

    // Whether a pool of this name is alive.
    bool live(const std::string& name)
    {
        for (const std::string& key : LL::ThreadPoolBase::key_snapshot())
        {
            if (key == name)
            {
                return true;
            }
        }
        return false;
    }

    // Waits for a pool of this name to be gone, a while at most.
    bool waitGone(const std::string& name)
    {
        LLTimer timer;
        timer.setTimerExpirySec(10.f);
        while (live(name))
        {
            if (timer.hasExpired())
            {
                return false;
            }
            ms_sleep(5);
        }
        return true;
    }

    // A pool whose threads wait to be let go, as one stuck in a call to a
    // disk that does not answer would.
    class StuckPool final : public LL::ThreadPool
    {
    public:
        StuckPool(const std::string& name, size_t width, LLOneShotCond& release, std::atomic<int>& finished)
        :   LL::ThreadPool(name, width, 1, /*auto_shutdown*/ false, /*fixed_width*/ true),
            mRelease(release),
            mFinished(finished)
        {
        }

        ~StuckPool() override { close(); }

        void run() override
        {
            mRelease.wait();
            ++mFinished;
        }

    private:
        LLOneShotCond&    mRelease;
        std::atomic<int>& mFinished;
    };

    TEST_CASE("threadpool: abandon returns at once and the last thread to finish destroys the pool", "[threadpool]")
    {
        const std::string name = "threadpool_test_stuck";
        LLOneShotCond     release;
        std::atomic<int>  finished{ 0 };
        {
            auto pool = std::make_unique<StuckPool>(name, 3, release, finished);
            pool->start();
            LLTimer timer;
            LL::ThreadPoolBase::abandon(std::move(pool));
            CHECK(timer.getElapsedTimeF32() < 1.f);
        }
        // The threads still run, and the pool is theirs.
        CHECK(live(name));
        CHECK(finished == 0);

        release.set_all();
        CHECK(waitGone(name));
        CHECK(finished == 3);
    }

    TEST_CASE("threadpool: abandon of a pool that never started destroys it at once", "[threadpool]")
    {
        const std::string name = "threadpool_test_unstarted";
        auto              pool = std::make_unique<LL::ThreadPool>(name, 2, 16, /*auto_shutdown*/ false, true);
        REQUIRE(live(name));
        LL::ThreadPoolBase::abandon(std::move(pool));
        CHECK_FALSE(live(name));
    }

    TEST_CASE("threadpool: abandon closes the queue and its threads finish and take the pool", "[threadpool]")
    {
        const std::string name = "threadpool_test_queue";
        std::atomic<int>  ran{ 0 };
        {
            auto pool = std::make_unique<LL::ThreadPool>(name, 2, 16, /*auto_shutdown*/ false, true);
            pool->start();
            for (int i = 0; i < 4; ++i)
            {
                REQUIRE(pool->getQueue().post([&ran] { ++ran; }));
            }
            LL::ThreadPoolBase::abandon(std::move(pool));
        }
        CHECK(waitGone(name));
        // What was posted before the queue closed is still run: a closed
        // queue is drained.
        CHECK(ran == 4);
    }

    TEST_CASE("threadpool: a pool listening for the viewer's shutdown stops on abandon", "[threadpool]")
    {
        const std::string name     = "threadpool_test_listening";
        const std::string listener = "ThreadPool:" + name;
        {
            auto pool = std::make_unique<LL::ThreadPool>(name, 1, 16, /*auto_shutdown*/ true, true);
            pool->start();
            REQUIRE(LLEventPumps::instance().obtain("LLApp").getListener(listener).connected());
            LL::ThreadPoolBase::abandon(std::move(pool));
        }
        CHECK_FALSE(LLEventPumps::instance().obtain("LLApp").getListener(listener).connected());
        CHECK(waitGone(name));
    }

    // Set as a thread's thread-local objects go, after everything its
    // function did.
    std::atomic<bool> sThreadGone{ false };

    struct ThreadGone
    {
        ~ThreadGone() { sThreadGone = true; }
    };

    // Where a pool that destroyed itself was: kept rather than freed, with
    // its bytes as its destructors left them, so that a write after shows.
    void*                      sRemains = nullptr;
    std::vector<unsigned char> sAsDestroyed;

    // Lets SelfDeletingPool's thread end; outside the pool, which is gone
    // once the thread wakes.
    LLOneShotCond sLetGo;

    // A pool whose run() destroys it, as the Win32 window's thread does: its
    // owner lets the thread go and closes the queue, and the thread deletes
    // the pool as it ends.
    class SelfDeletingPool final : public LL::ThreadPool
    {
    public:
        explicit SelfDeletingPool(const std::string& name)
        :   LL::ThreadPool(name, 1, 1, /*auto_shutdown*/ false, /*fixed_width*/ true)
        {
        }

        static void* operator new(size_t size) { return ::operator new(size); }

        static void operator delete(void* block, size_t size)
        {
            sRemains = block;
            const auto* bytes = static_cast<const unsigned char*>(block);
            sAsDestroyed.assign(bytes, bytes + size);
        }

        // What LLWindowWin32Thread::wakeAndDestroy() does.
        void letGo()
        {
            for (auto& pair : mThreads)
            {
                pair.second.detach();
            }
            mQueue->close();
            sLetGo.set_all();
        }

        void run() override
        {
            thread_local ThreadGone gone;
            (void)gone;
            sLetGo.wait();
            delete this;
        }
    };

    TEST_CASE("threadpool: a pool its own run() destroys is not touched after", "[threadpool]")
    {
        sThreadGone = false;
        auto* pool  = new SelfDeletingPool("threadpool_test_self_deleting");
        pool->start();
        // From here the thread may destroy the pool at any moment.
        pool->letGo();

        LLTimer timer;
        timer.setTimerExpirySec(10.f);
        while (!sThreadGone && !timer.hasExpired())
        {
            ms_sleep(1);
        }
        REQUIRE(sThreadGone);
        REQUIRE(sRemains);
        const auto* now = static_cast<const unsigned char*>(sRemains);
        CHECK(std::equal(sAsDestroyed.begin(), sAsDestroyed.end(), now));
        ::operator delete(sRemains);
        sRemains = nullptr;
    }
}
