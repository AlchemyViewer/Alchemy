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

#include "../test/lltut.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace tut
{
    struct timer_data
    {
        // The global timer behind the uptime; an application makes it in
        // LLCommon::initClass, which the test runner does not call.
        timer_data() { LLTimer::initClass(); }
    };
    typedef test_group<timer_data> timer_group;
    typedef timer_group::object timer_object;
    tut::timer_group timer_instance("LLTimer");

    template<> template<>
    void timer_object::test<1>()
    {
        set_test_name("a new timer reads expired until it is given an expiry");
        LLTimer timer;
        ensure("expired", timer.hasExpired());
        timer.setTimerExpirySec(10.f);
        ensure("not expired once given ten seconds", !timer.hasExpired());
    }

    template<> template<>
    void timer_object::test<2>()
    {
        set_test_name("resetWithExpiry restarts the timer and keeps the expiry");
        LLTimer timer;
        ms_sleep(50);
        timer.resetWithExpiry(10.f);
        ensure("not expired", !timer.hasExpired());
        ensure("the whole expiry to run", timer.getRemainingTimeF32() > 9.9f);
        ensure("restarted", timer.getElapsedTimeF32() < 0.04f);
    }

    template<> template<>
    void timer_object::test<3>()
    {
        set_test_name("start() after an expiry clears it, which is why resetWithExpiry exists");
        LLTimer timer;
        timer.setTimerExpirySec(10.f);
        timer.start();
        ensure("start() cleared the expiry", timer.hasExpired());
    }

    template<> template<>
    void timer_object::test<4>()
    {
        set_test_name("the uptime is the application's, not this timer's");
        LLTimer timer;
        ms_sleep(50);
        timer.reset();
        ensure("the timer was just reset", timer.getElapsedTimeF64() < 0.04);
        ensure("the uptime runs from before the reset", LLTimer::getUptimeSeconds() > timer.getElapsedTimeF64());
    }

    template<> template<>
    void timer_object::test<5>()
    {
        set_test_name("the total time reads the calendar once and keeps pace with the steady clock");
        const auto calendar = std::chrono::system_clock::now().time_since_epoch();
        const F64  calendar_seconds = std::chrono::duration<F64>(calendar).count();
        const F64  total = LLTimer::getTotalSeconds();
        ensure("anchored to the calendar", std::fabs(total - calendar_seconds) < 1.0);

        const auto steady_start = std::chrono::steady_clock::now();
        const U64  total_start = totalTime();
        ms_sleep(200);
        const F64 steady = std::chrono::duration<F64>(std::chrono::steady_clock::now() - steady_start).count();
        const F64 counted = (F64)((U64)totalTime() - total_start) / 1000000.0;
        ensure("counts as the steady clock does", std::fabs(counted - steady) < 0.005);
    }

    template<> template<>
    void timer_object::test<6>()
    {
        set_test_name("the total time read on many threads at once never steps and never runs fast");
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
        const F64 steady = std::chrono::duration<F64>(std::chrono::steady_clock::now() - steady_start).count();
        const F64 counted = (F64)((U64)totalTime() - total_start) / 1000000.0;
        ensure("no thread saw it step back", !stepped_back);
        ensure("counted no faster than the steady clock", counted < steady + 0.005);
    }
}
