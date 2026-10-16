/**
 * @file lltimer_test.cpp
 * @brief When an LLTimer's expiry is, and what resets it.
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

#include "../lltimer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace
{
    struct timer_data
    {
        // The global timer behind the uptime; an application makes it in
        // LLCommon::initClass, which the test runner does not call.
        timer_data() { LLTimer::initClass(); }
    };

    TEST_CASE_METHOD(timer_data, "LLTimer: a new timer reads expired until it is given an expiry", "[LLTimer]")
    {
        LLTimer timer;
        CHECK(timer.hasExpired());
        timer.setTimerExpirySec(10.f);
        CHECK_FALSE(timer.hasExpired());
    }

    TEST_CASE_METHOD(timer_data, "LLTimer: resetWithExpiry restarts the timer and keeps the expiry", "[LLTimer]")
    {
        LLTimer timer;
        // A timer the reset did not restart reads at least this quarter
        // second (a Windows Sleep can come back a tick, 16 ms, early); one it
        // did reads under 0.2 s, with more than 9.8 s of its 10 left, unless
        // this thread is kept off the processor that long between the reset
        // and the reads.
        ms_sleep(250);
        timer.resetWithExpiry(10.f);
        CHECK_FALSE(timer.hasExpired());
        CHECK(timer.getRemainingTimeF32() > 9.8f);
        CHECK(timer.getElapsedTimeF32() < 0.2f);
    }

    TEST_CASE_METHOD(timer_data, "LLTimer: start() after an expiry clears it which is why resetWithExpiry exists", "[LLTimer]")
    {
        LLTimer timer;
        timer.setTimerExpirySec(10.f);
        timer.start();
        CHECK(timer.hasExpired());
    }

    TEST_CASE_METHOD(timer_data, "LLTimer: the uptime is the application's and not this timer's", "[LLTimer]")
    {
        LLTimer timer;
        // As above: unrestarted, at least a quarter second; restarted, under
        // 0.2 s however busy the machine, short of a 0.2 s stall.
        ms_sleep(250);
        timer.reset();
        CHECK(timer.getElapsedTimeF64() < 0.2);
        UNSCOPED_INFO("the uptime runs from before the reset");
        CHECK(LLTimer::getUptimeSeconds() > timer.getElapsedTimeF64());
    }

    TEST_CASE_METHOD(timer_data, "LLTimer: total time matches the calendar and keeps pace with the steady clock", "[LLTimer]")
    {
        const auto calendar = std::chrono::system_clock::now().time_since_epoch();
        const F64  calendar_seconds = std::chrono::duration<F64>(calendar).count();
        const F64  total = LLTimer::getTotalSeconds();
        CHECK(std::fabs(total - calendar_seconds) < 1.0);

        // Each read of the total is between two of the steady clock's, so
        // what the total counted lies between what the steady clock counted
        // inside those brackets and outside them, however long this thread is
        // kept off the processor at any point. The millisecond either side is
        // for the total's whole microseconds and the clocks' grain: a total
        // fast or slow by half a percent over the 200 ms falls outside it.
        const auto outer_start = std::chrono::steady_clock::now();
        const U64  total_start = totalTime();
        const auto inner_start = std::chrono::steady_clock::now();
        ms_sleep(200);
        const auto inner_end = std::chrono::steady_clock::now();
        const U64  total_end = totalTime();
        const auto outer_end = std::chrono::steady_clock::now();
        const F64 counted = (F64)(total_end - total_start) / 1000000.0;
        CHECK(counted > std::chrono::duration<F64>(inner_end - inner_start).count() - 0.001);
        CHECK(counted < std::chrono::duration<F64>(outer_end - outer_start).count() + 0.001);
    }

    TEST_CASE_METHOD(timer_data, "LLTimer: total time read on many threads never steps back or runs fast", "[LLTimer]")
    {
        const auto steady_start = std::chrono::steady_clock::now();
        const U64  total_start = totalTime();
        std::atomic<bool> stepped_back{ false };
        std::vector<std::thread> readers;
        for (int i = 0; i < 8; ++i)
        {
            readers.emplace_back([&stepped_back]
            {
                U64 last = totalTime();
                for (int read = 0; read < 200000; ++read)
                {
                    const U64 now = totalTime();
                    if (now < last)
                    {
                        stepped_back = true;
                    }
                    last = now;
                }
            });
        }
        for (std::thread& reader : readers)
        {
            reader.join();
        }
        // The total read between the steady clock's two reads, so it cannot
        // have counted more than they did, however the readers were
        // scheduled; the millisecond is the clocks' grain, as above.
        const U64 total_end = totalTime();
        const F64 steady = std::chrono::duration<F64>(std::chrono::steady_clock::now() - steady_start).count();
        const F64 counted = (F64)(total_end - total_start) / 1000000.0;
        CHECK_FALSE(stepped_back);
        CHECK(counted < steady + 0.001);
    }
}
