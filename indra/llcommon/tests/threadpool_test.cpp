/**
 * @file threadpool_test.cpp
 * @brief A pool's worker sleeps until work or a deadline wakes it.
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
#include "../lltimer.h"

#if LL_WINDOWS
#include "../llwin32headers.h"
#endif

#include <boost/fiber/operations.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
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
            ms_sleep(1);
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
        ms_sleep(50);

        ULONG64 before = 0;
        ULONG64 after  = 0;
        REQUIRE(QueryThreadCycleTime(worker, &before));
        ms_sleep(250);
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
}
