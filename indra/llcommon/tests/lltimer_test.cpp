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
}
