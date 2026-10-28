/**
 * @file oversleep_bench.cpp
 * @brief How late each of the viewer's waits wakes, idle and with every core
 *        busy.
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

// For waits of 0.5, 1, 2, 5 and 10 ms, how far past the time asked for each of
// the viewer's waits comes back: the least, the median, the 90th and 99th
// percentiles and the most, in microseconds, over a run of each.
//
//   waiter           ALPreciseWaiter::waitUntil(), a timed wait another
//                    thread could end
//   waiter + spin    the same to margin() before the deadline, then
//                    spinUntil(): a wait that has to end on its deadline
//     time spun      how long that spin ran, which is what it costs
//   plain sleep      ALPreciseWaiter::sleepFor(), which ALFrameFences' wait
//                    for a frame's stamp sleeps on
//   ms_sleep         whole milliseconds only
//   micro_sleep
//   pool timed wait  a thread pool worker's boost::this_fiber::sleep_for(),
//                    which on Windows is the pool's scheduler's timed wait
//
// Each table is run idle, then with every logical processor running a busy
// loop: the viewer is never idle. On Windows both run again holding a 1 ms
// timer resolution, as the viewer does.
//
// The output is not a verdict; a row is read against the same row from
// another build, on the same machine.

#include "linden_common.h"

#include "alprecisewaiter.h"
#include "altimerresolutionscope.h"
#include "lltimer.h"
#include "threadpool.h"

#include "altestbenchmark.h"

#include <boost/fiber/operations.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iterator>
#include <thread>
#include <vector>

namespace
{
    using time_point = ALPreciseWaiter::time_point;

    constexpr F64 WAITS[] = { 0.0005, 0.001, 0.002, 0.005, 0.01 };

    // Enough of each for a 99th percentile between the latest few, and few enough that a table of 10 ms waits ends in
    // seconds.
    int samplesFor(F64 seconds)
    {
        return seconds <= 0.002 ? 400 : 200;
    }

    // One wait of `seconds`, however the row waits: how far past them it came back.
    using Wait = std::function<F64(F64 seconds)>;

    struct Row
    {
        const char* mName;
        Wait        mWait;
        bool        mWholeMilliseconds = false;
    };

    // Each row's waits taken in turn, one wait of each row a round, so that whatever the machine does over the run
    // falls on every row alike, and what one row's wait leaves behind, such as a spin's use of its quantum, comes
    // before the others' as often as after them.
    void runTable(const char* condition, const std::vector<Row>& rows)
    {
        std::printf("\noversleep_bench: %s, in us past the time asked\n", condition);
        std::printf("  %-17s %5s %8s %8s %8s %8s %8s\n", "wait", "ms", "min", "p50", "p90", "p99", "max");
        std::vector<std::vector<std::vector<F64>>> late(rows.size(), std::vector<std::vector<F64>>(std::size(WAITS)));
        for (size_t w = 0; w < std::size(WAITS); ++w)
        {
            const F64 seconds = WAITS[w];
            for (int i = samplesFor(seconds); i > 0; --i)
            {
                for (size_t r = 0; r < rows.size(); ++r)
                {
                    if (!rows[r].mWholeMilliseconds || seconds >= 0.001)
                    {
                        late[r][w].push_back(rows[r].mWait(seconds));
                    }
                }
            }
        }
        for (size_t r = 0; r < rows.size(); ++r)
        {
            for (size_t w = 0; w < std::size(WAITS); ++w)
            {
                std::vector<F64>& each = late[r][w];
                if (each.empty())
                {
                    continue;
                }
                std::sort(each.begin(), each.end());
                const auto at = [&each](F64 fraction)
                    {
                        return each[std::min(each.size() - 1, size_t(fraction * F64(each.size())))] * 1.0e6;
                    };
                std::printf("  %-17s %5.1f %8.1f %8.1f %8.1f %8.1f %8.1f\n", rows[r].mName, WAITS[w] * 1000.0,
                            each.front() * 1.0e6, at(0.5), at(0.9), at(0.99), each.back() * 1.0e6);
            }
        }
        std::fflush(stdout);
    }

    void runTables(const char* resolution)
    {
        ALPreciseWaiter waiter;
        // A thread pool worker's timed wait, measured on the worker, which then wakes this thread.
        LL::ThreadPool  pool("oversleep_bench_pool", 1, 64, false, true);
        pool.start();
        ALPreciseWaiter pool_done;
        const std::vector<Row> rows = {
            { "waiter", [&waiter](F64 seconds)
                {
                    const time_point deadline = time_point::after(seconds);
                    return waiter.waitUntil(deadline).since(deadline);
                } },
            { "waiter + spin", [&waiter](F64 seconds)
                {
                    const time_point deadline = time_point::after(seconds);
                    waiter.waitUntil(deadline.plus(-waiter.margin()));
                    return ALPreciseWaiter::spinUntil(deadline).since(deadline);
                } },
            { "  time spun", [&waiter](F64 seconds)
                {
                    const time_point deadline = time_point::after(seconds);
                    const time_point woke     = waiter.waitUntil(deadline.plus(-waiter.margin()));
                    return ALPreciseWaiter::spinUntil(deadline).since(woke);
                } },
            { "plain sleep", [](F64 seconds)
                {
                    const time_point start = time_point::now();
                    return ALPreciseWaiter::sleepFor(seconds).since(start) - seconds;
                } },
            { "ms_sleep", [](F64 seconds)
                {
                    const time_point start = time_point::now();
                    ms_sleep(U32(seconds * 1000.0 + 0.5));
                    return time_point::now().since(start) - seconds;
                }, true },
            { "micro_sleep", [](F64 seconds)
                {
                    const time_point start = time_point::now();
                    micro_sleep(U64(seconds * 1.0e6 + 0.5));
                    return time_point::now().since(start) - seconds;
                } },
            { "pool timed wait", [&pool, &pool_done](F64 seconds)
                {
                    F64 late = 0.0;
                    REQUIRE(pool.getQueue().post([&late, &pool_done, seconds]()
                        {
                            const auto duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<F64>(seconds));
                            const time_point start = time_point::now();
                            boost::this_fiber::sleep_for(duration);
                            late = time_point::now().since(start) - seconds;
                            pool_done.wake();
                        }));
                    pool_done.waitUntil(time_point::max());
                    return late;
                } },
        };

        char condition[160];
        std::snprintf(condition, sizeof(condition), "idle, %s", resolution);
        runTable(condition, rows);

        // Every logical processor busy, at the priority the viewer's own threads run at.
        const size_t cores = (std::max)(1u, std::thread::hardware_concurrency());
        std::atomic<bool> stop{ false };
        LL::ThreadPool load("oversleep_bench_load", cores, 64, false, true);
        load.start();
        // However the table ends, a check that throws among them, the load stops before the pool joins its threads.
        struct StopLoad
        {
            std::atomic<bool>& mStop;
            ~StopLoad() { mStop = true; }
        } stop_load{ stop };
        for (size_t i = 0; i < cores; ++i)
        {
            REQUIRE(load.getQueue().post([&stop]()
                {
                    volatile F64 x = 1.0;
                    while (!stop.load(std::memory_order_relaxed))
                    {
                        x = x * 1.0000001 + 1.0e-9;
                    }
                }));
        }
        std::snprintf(condition, sizeof(condition), "%zu logical processors busy, %s", cores, resolution);
        runTable(condition, rows);
    }

    TEST_CASE("oversleep_bench: each wait idle and with every core busy", "[oversleep_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        runTables("the default timer resolution");
#if LL_WINDOWS
        // 1 ms, as the viewer asks, or the finest the system can do.
        const ALTimerResolutionScope resolution(1);
        REQUIRE(resolution.period() != 0);
        char held[64];
        std::snprintf(held, sizeof(held), "a %u ms timer resolution held", resolution.period());
        runTables(held);
#endif
    }
}
