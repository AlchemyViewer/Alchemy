/**
 * @file lltiming_test.cpp
 * @date 2006-07-23
 * @brief Tests the timers.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llframetimer.h"
#include "../llsd.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

namespace
{
    struct frametimer_test
    {
        frametimer_test()
        {
            LLFrameTimer::updateFrameTime();
        }
    };

    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: expiresAt returns the time setExpiryAt set", "[LLFrameTimer]")
    {
        F64 seconds_since_epoch = LLFrameTimer::getTotalSeconds();
        LLFrameTimer timer;
        timer.setExpiryAt(seconds_since_epoch);
        F64 expires_at = timer.expiresAt();
        CHECK_THAT(expires_at,
            ll_test::WithinDistance<double>(seconds_since_epoch,
            0.001));
    }

    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: setExpiryAt moves an expiry already set", "[LLFrameTimer]")
    {
        F64 seconds_since_epoch = LLFrameTimer::getTotalSeconds();
        seconds_since_epoch += 10.0;
        LLFrameTimer timer;
        timer.setExpiryAt(seconds_since_epoch);
        F64 expires_at = timer.expiresAt();
        CHECK_THAT(expires_at,
            ll_test::WithinDistance<double>(seconds_since_epoch,
            0.001));
        seconds_since_epoch += 10.0;
        timer.setExpiryAt(seconds_since_epoch);
        expires_at = timer.expiresAt();
        CHECK_THAT(expires_at,
            ll_test::WithinDistance<double>(seconds_since_epoch,
            0.001));
    }
    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: an expiry two seconds out passes within ten 200 ms sleeps", "[LLFrameTimer]")
    {
        clock_t t1 = clock();
        ms_sleep(200);
        clock_t t2 = clock();
        clock_t elapsed = t2 - t1 + 1;
        std::cout << "Note: using clock(), ms_sleep() actually took " << (long)elapsed << "ms" << std::endl;

        F64 seconds_since_epoch = LLFrameTimer::getTotalSeconds();
        seconds_since_epoch += 2.0;
        LLFrameTimer timer;
        timer.setExpiryAt(seconds_since_epoch);
        /*
         * Note that the ms_sleep(200) below is only guaranteed to return
         * in 200ms _or_more_, so it should be true that by the 10th
         * iteration we've gotten to the 2 seconds requested above
         * and the timer should expire, but it can expire in fewer iterations
         * if one or more of the ms_sleep calls takes longer.
         * (as it did when we moved to Mac OS X 10.10)
         */
        int iterations_until_expiration = 0;
        while ( !timer.hasExpired() )
        {
            ms_sleep(200);
            LLFrameTimer::updateFrameTime();
            iterations_until_expiration++;
        }
        CHECK(iterations_until_expiration <= 10);
    }

    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: an expiry counts from now and not from the timer's last reset", "[LLFrameTimer]")
    {
        LLFrameTimer timer;
        // Longer than the expiry passes between the timer starting and the
        // expiry being set.
        ms_sleep(300);
        LLFrameTimer::updateFrameTime();
        timer.setTimerExpirySec(0.2f);
        UNSCOPED_INFO("not expired the moment it is set");
        CHECK_FALSE(timer.hasExpired());
        UNSCOPED_INFO("the whole expiry still to run");
        CHECK(timer.getTimeToExpireF32() > 0.19f);
    }

    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: a stopped timer keeps the time it ran and resumes from it", "[LLFrameTimer]")
    {
        // Far enough from the start of the run that the absolute start time
        // cannot pass for the time run.
        ms_sleep(300);
        LLFrameTimer::updateFrameTime();
        LLFrameTimer timer;
        timer.start();
        ms_sleep(200);
        LLFrameTimer::updateFrameTime();
        const F32 ran = timer.getElapsedTimeF32();
        timer.stop();
        CHECK_FALSE(timer.getStarted());
        UNSCOPED_INFO("the time it ran");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(ran, 12));
        ms_sleep(100);
        LLFrameTimer::updateFrameTime();
        UNSCOPED_INFO("not running while stopped");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(ran, 12));
        timer.unpause();
        UNSCOPED_INFO("resumed from where it stopped");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(ran, 12));
    }

    TEST_CASE_METHOD(frametimer_test, "LLFrameTimer: a reset while paused zeroes the time run and leaves it paused", "[LLFrameTimer]")
    {
        ms_sleep(300);
        LLFrameTimer::updateFrameTime();
        LLFrameTimer timer;
        timer.start();
        ms_sleep(100);
        LLFrameTimer::updateFrameTime();
        timer.pause();
        timer.reset();
        CHECK_FALSE(timer.getStarted());
        UNSCOPED_INFO("no time run");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(0.f, 12));
        ms_sleep(100);
        LLFrameTimer::updateFrameTime();
        UNSCOPED_INFO("not running while paused");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(0.f, 12));
        timer.unpause();
        UNSCOPED_INFO("runs on from zero");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(0.f, 12));
        timer.pause();
        timer.start();
        CHECK(timer.getStarted());
        UNSCOPED_INFO("start() runs from zero, paused or not");
        CHECK_THAT(timer.getElapsedTimeF32(), ll_test::WithinBits(0.f, 12));
    }
}
