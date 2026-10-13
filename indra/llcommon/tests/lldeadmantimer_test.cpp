/**
 * @file lldeadmantimer_test.cpp
 * @brief Tests for the LLDeadmanTimer class.
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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

#include "../lldeadmantimer.h"
#include "../llsd.h"
#include "../lltimer.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

// Convert between floating point time deltas and U64 time deltas.
// Reflects an implementation detail inside lldeadmantimer.cpp

static LLDeadmanTimer::time_type float_time_to_u64(F64 delta)
{
    return LLDeadmanTimer::time_type(delta * get_timer_info().mClockFrequency);
}

static F64 u64_time_to_float(LLDeadmanTimer::time_type delta)
{
    return delta * get_timer_info().mClockFrequencyInv;
}


namespace
{

struct deadmantimer_test
{
    deadmantimer_test()
        {
            // LLTimer internals updating
            get_timer_info().update();
        }
};

// Basic construction test and isExpired() call
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a new timer is not expired and isExpired changes nothing", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(10.0, false);

        CHECK_FALSE(timer.isExpired(0, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
    }

    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(10.0, true);

        CHECK_FALSE(timer.isExpired(0, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
        CHECK(user_cpu == U64L(29000));
        CHECK(sys_cpu == U64L(57000));
    }
}


// Construct with zero horizon - not useful generally but will be useful in testing
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: an unstarted zero-horizon timer is not expired", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(0.0, false);           // Zero is pre-expired

        CHECK_FALSE(timer.isExpired(0, started, stopped, count));
    }

    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(0.0, true);            // Zero is pre-expired

        CHECK_FALSE(timer.isExpired(0, started, stopped, count, user_cpu, sys_cpu));
    }
}


// "pre-expired" timer - starting a timer with a 0.0 horizon will result in
// expiration on first test.
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a started zero-horizon timer expires at the first test", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(0.0, false);

        timer.start(0);
        CHECK(timer.isExpired(0, started, stopped, count));
        UNSCOPED_INFO("with no bell rung, stopped stays equal to started");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(0.0, true);

        timer.start(0);
        CHECK(timer.isExpired(0, started, stopped, count, user_cpu, sys_cpu));
        UNSCOPED_INFO("with no bell rung, stopped stays equal to started");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
}


// "pre-expired" timer - bell rings are ignored as we're already expired.
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a pre-expired timer ignores the bell", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(0.0, false);

        timer.start(0);
        timer.ringBell(LLDeadmanTimer::getNow() + float_time_to_u64(1000.0), 1);
        CHECK(timer.isExpired(0, started, stopped, count));
        UNSCOPED_INFO("the bell leaves stopped equal to started");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(0.0, true);

        timer.start(0);
        timer.ringBell(LLDeadmanTimer::getNow() + float_time_to_u64(1000.0), 1);
        CHECK(timer.isExpired(0, started, stopped, count, user_cpu, sys_cpu));
        UNSCOPED_INFO("the bell leaves stopped equal to started");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
}


// start(0) test - unexpired timer reports unexpired
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: start(0) leaves a timer unexpired and isExpired changes nothing", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(10.0, false);

        timer.start(0);
        CHECK_FALSE(timer.isExpired(0, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(10.0, true);

        timer.start(0);
        CHECK_FALSE(timer.isExpired(0, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
        CHECK(user_cpu == U64L(29000));
        CHECK(sys_cpu == U64L(57000));
    }
}


// start() test - start in the past but not beyond 1 horizon
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a timer started less than a horizon ago is not expired", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(10.0, false);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(5.0));
        timer.start(the_past);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(10.0, true);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(5.0));
        timer.start(the_past);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
        CHECK(user_cpu == U64L(29000));
        CHECK(sys_cpu == U64L(57000));
    }
}


// start() test - start in the past but well beyond 1 horizon
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a timer started more than a horizon ago is expired", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(10.0, false);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(20.0));
        timer.start(the_past);
        CHECK(timer.isExpired(now,started, stopped, count));
        UNSCOPED_INFO("starting before the horizon still gives equal started and stopped");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(10.0, true);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(20.0));
        timer.start(the_past);
        CHECK(timer.isExpired(now,started, stopped, count, user_cpu, sys_cpu));
        UNSCOPED_INFO("starting before the horizon still gives equal started and stopped");
        CHECK_THAT(started, ll_test::WithinBits(stopped, 8));
    }
}


// isExpired() test - results are read-once.  Probes after first true are false.
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: isExpired reports an expiry once and then changes nothing", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(10.0, false);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(20.0));
        timer.start(the_past);
        CHECK(timer.isExpired(now, started, stopped, count));

        started = 42.0;
        stopped = 97.0;
        count = U64L(8);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(10.0, true);

        // Would like to do subtraction on current time but can't because
        // the implementation on Windows is zero-based.  We wrap around
        // the backside resulting in a large U64 number.

        LLDeadmanTimer::time_type the_past(LLDeadmanTimer::getNow());
        LLDeadmanTimer::time_type now(the_past + float_time_to_u64(20.0));
        timer.start(the_past);
        CHECK(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));

        started = 42.0;
        stopped = 97.0;
        count = U64L(8);
        user_cpu = 29000;
        sys_cpu = 57000;
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(F64(42.0), 2));
        CHECK_THAT(stopped, ll_test::WithinBits(F64(97.0), 2));
        CHECK(count == U64L(8));
        CHECK(user_cpu == U64L(29000));
        CHECK(sys_cpu == U64L(57000));
    }
}


// ringBell() test - see that we can keep a timer from expiring
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: ringing the bell keeps a timer from expiring", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(5.0, false);

        LLDeadmanTimer::time_type now(LLDeadmanTimer::getNow());
        F64 real_start(u64_time_to_float(now));
        timer.start(0);

        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
        F64 last_good_ring(u64_time_to_float(now));

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(10));
        UNSCOPED_INFO("an expiry is reported once");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);
        LLDeadmanTimer timer(5.0, true);

        LLDeadmanTimer::time_type now(LLDeadmanTimer::getNow());
        F64 real_start(u64_time_to_float(now));
        timer.start(0);

        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        F64 last_good_ring(u64_time_to_float(now));

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(10));
        UNSCOPED_INFO("an expiry is reported once");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
    }
}


// restart after expiration test - verify that restarts behave well
TEST_CASE_METHOD(deadmantimer_test, "LLDeadmanTimer: a timer restarted after expiring times the new run", "[LLDeadmanTimer]")
{
    {
        // Without cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8));
        LLDeadmanTimer timer(5.0, false);

        LLDeadmanTimer::time_type now(LLDeadmanTimer::getNow());
        F64 real_start(u64_time_to_float(now));
        timer.start(0);

        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
        F64 last_good_ring(u64_time_to_float(now));

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(10));
        UNSCOPED_INFO("an expiry is reported once");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));

        // Jump forward and restart
        now += float_time_to_u64(1.0);
        real_start = u64_time_to_float(now);
        timer.start(now);

        // Run a modified bell ring sequence
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
        last_good_ring = u64_time_to_float(now);

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(8));
        UNSCOPED_INFO("an expiry is reported once after the restart");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count));
    }
    {
        // With cpu metrics
        F64 started(42.0), stopped(97.0);
        U64 count(U64L(8)), user_cpu(29000), sys_cpu(57000);

        LLDeadmanTimer timer(5.0, true);

        LLDeadmanTimer::time_type now(LLDeadmanTimer::getNow());
        F64 real_start(u64_time_to_float(now));
        timer.start(0);

        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        F64 last_good_ring(u64_time_to_float(now));

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(10));
        UNSCOPED_INFO("an expiry is reported once");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));

        // Jump forward and restart
        now += float_time_to_u64(1.0);
        real_start = u64_time_to_float(now);
        timer.start(now);

        // Run a modified bell ring sequence
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        now += float_time_to_u64(1.0);
        timer.ringBell(now, 1);
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        last_good_ring = u64_time_to_float(now);

        // Jump forward and expire
        now += float_time_to_u64(10.0);
        CHECK(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
        CHECK_THAT(started, ll_test::WithinBits(real_start, 4));
        CHECK_THAT(stopped, ll_test::WithinBits(last_good_ring, 4));
        CHECK(count == U64L(8));
        UNSCOPED_INFO("an expiry is reported once after the restart");
        CHECK_FALSE(timer.isExpired(now, started, stopped, count, user_cpu, sys_cpu));
    }
}



} // anonymous namespace
