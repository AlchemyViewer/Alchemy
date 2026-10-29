/**
 * @file alprecisewaiter_test.cpp
 * @brief A wait ends at its deadline and no sooner, or at a wake.
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

#include "../alprecisewaiter.h"
#include "../lltimer.h"
#include "../threadpool.h"

#if LL_WINDOWS
#include "../llwin32headers.h"
#elif LL_DARWIN
#include <mach/mach_time.h>
#include <unistd.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace ll_test
{
    // Takes what a waiter waits on away from it, so that its tests run on the fallbacks a system short of it leaves.
    struct PreciseWaiterProbe
    {
#if LL_WINDOWS
        // An ordinary waitable timer in place of the high-resolution one, as before Windows 10 1803.
        static bool ordinaryTimer(ALPreciseWaiter& waiter)
        {
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            if (!timer)
            {
                return false;
            }
            CloseHandle(waiter.mTimer);
            waiter.mTimer = timer;
            return true;
        }
#endif

#if !LL_DARWIN
        // No timer at all: a timed wait is a timeout of the wait's own.
        static void noTimer(ALPreciseWaiter& waiter)
        {
#if LL_WINDOWS
            CloseHandle(waiter.mTimer);
            waiter.mTimer = nullptr;
#else
            close(waiter.mTimer);
            waiter.mTimer = -1;
#endif
        }
#endif

        // Nothing to wake it.
        static void noWake(ALPreciseWaiter& waiter)
        {
#if LL_WINDOWS
            CloseHandle(waiter.mWake);
            waiter.mWake = nullptr;
#elif LL_LINUX
            close(waiter.mWake);
            waiter.mWake = -1;
#else
            close(waiter.mQueue);
            waiter.mQueue = -1;
#endif
        }
    };
}

namespace
{
    using time_point = ALPreciseWaiter::time_point;

    // Seconds are F64 or a duration that says what it is; a whole number, which reads as the milliseconds ms_sleep()
    // takes, does not compile.
    template <typename T>
    concept TakesAfter = requires(T t) { time_point::after(t); };
    template <typename T>
    concept TakesPlus = requires(time_point p, T t) { p.plus(t); };
    template <typename T>
    concept TakesWaitFor = requires(ALPreciseWaiter& waiter, T t) { waiter.waitFor(t); };
    template <typename T>
    concept TakesSleepFor = requires(T t) { ALPreciseWaiter::sleepFor(t); };
    static_assert(!TakesAfter<int> && !TakesAfter<U32> && !TakesAfter<S64>);
    static_assert(!TakesPlus<int> && !TakesPlus<U64>);
    static_assert(!TakesWaitFor<int> && !TakesWaitFor<U32>);
    static_assert(!TakesSleepFor<int> && !TakesSleepFor<U32> && !TakesSleepFor<long long>);
    static_assert(TakesAfter<F64> && TakesAfter<F32> && TakesAfter<std::chrono::milliseconds>);
    static_assert(TakesPlus<F64>);
    static_assert(TakesWaitFor<F64> && TakesWaitFor<std::chrono::microseconds>);
    static_assert(TakesSleepFor<F64> && TakesSleepFor<std::chrono::seconds>);

    // Every wait is checked against the clock: it ends no sooner than it may, which is exact, and no later than LATE
    // past that, which only a wait that did not end of itself comes near, however loaded the machine.
    constexpr F64 LATE = 1.0;
    // A wait still going this long after the wake that should have ended it has lost the wake.
    constexpr F64 LOST = 5.0;

    // The other side of a case, on a thread of its own: a pool of one, closed with the case once what it was given
    // has returned.
    class Waker
    {
    public:
        Waker()
        : mPool("alprecisewaiter_test_waker", 1, 64, false, true)
        {
            mPool.start();
        }

        template <typename FUNC>
        void post(FUNC&& body)
        {
            REQUIRE(mPool.getQueue().post(std::forward<FUNC>(body)));
        }

    private:
        LL::ThreadPool mPool;
    };

    // One wake, as the waiter and the waker each saw it.
    struct Wake
    {
        std::atomic<bool> mWaiting{ false };  // the waiter is about to wait
        std::atomic<bool> mReturned{ false }; // the waiter's wait has returned
        std::atomic<bool> mWoken{ false };    // the waker has called wake()
        std::atomic<bool> mRescued{ false };  // the wake was lost, and a second one ended the wait
        std::atomic<U64>  mAt{ time_point::max().ticks }; // the clock just before wake(), and no time before it
    };

    // The waker's side. Once the waiter says it is about to wait, `into` seconds on, wakes it, noting the clock just
    // before. A wait that wake has not ended LOST seconds later gets a second wake, and the case is told, so that a
    // lost wake fails the case rather than hanging it.
    void wakeWhenWaiting(ALPreciseWaiter& waiter, Wake& wake, F64 into)
    {
        ALPreciseWaiter::spinUntil(time_point::after(LOST), [&wake]() { return wake.mWaiting.load(); });
        if (into > 0.0)
        {
            // Long enough that the waiter has gone into its wait; one that has not yet is ended the same way, by the
            // wake it is given before it.
            ALPreciseWaiter::sleepFor(into);
        }
        wake.mAt = time_point::now().ticks;
        waiter.wake();
        wake.mWoken = true;
        const time_point lost = time_point::after(LOST);
        while (!wake.mReturned && time_point::now() < lost)
        {
            ALPreciseWaiter::sleepFor(0.001);
        }
        if (!wake.mReturned)
        {
            wake.mRescued = true;
            waiter.wake();
        }
    }

    // A wait that should end of itself, run with a wake LOST seconds on in case it does not.
    template <typename WAIT>
    time_point rescued(ALPreciseWaiter& waiter, WAIT&& wait, bool& was_rescued)
    {
        Wake  wake;
        Waker waker;
        waker.post([&waiter, &wake]()
            {
                ALPreciseWaiter::spinUntil(time_point::after(LOST), [&wake]() { return wake.mWaiting.load(); });
                const time_point lost = time_point::after(LOST);
                while (!wake.mReturned && time_point::now() < lost)
                {
                    ALPreciseWaiter::sleepFor(0.001);
                }
                if (!wake.mReturned)
                {
                    wake.mRescued = true;
                    waiter.wake();
                }
            });
        wake.mWaiting = true;
        const time_point woke = wait();
        wake.mReturned = true;
        was_rescued = wake.mRescued;
        return woke;
    }

#if !LL_WINDOWS
    // The clock the waiter's timers are armed on: CLOCK_MONOTONIC in nanoseconds, for the timerfd and
    // clock_nanosleep, and mach absolute time, for the kqueue timer and mach_wait_until.
    U64 timerClock()
    {
#if LL_DARWIN
        return mach_absolute_time();
#else
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return U64(now.tv_sec) * 1000000000ull + U64(now.tv_nsec);
#endif
    }

    TEST_CASE("alprecisewaiter: the counter is the clock its timers are armed on", "[alprecisewaiter]")
    {
        // A deadline goes to the timer as the counter's ticks, unconverted: a counter with an origin or a rate of
        // its own would arm every timer at another time. Each read of the counter lies between two of the clock's.
        for (int i = 0; i < 1000; ++i)
        {
            const U64 before  = timerClock();
            const U64 counter = get_clock_count();
            const U64 after   = timerClock();
            CHECK(counter >= before);
            CHECK(counter <= after);
        }
    }
#endif

    TEST_CASE("alprecisewaiter: a timed wait ends no sooner than its deadline", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        REQUIRE(waiter.canWake());
        for (F64 seconds : { 0.0001, 0.0005, 0.001, 0.002, 0.005, 0.0105 })
        {
            INFO("a wait of " << seconds * 1000.0 << " ms");
            for (int i = 0; i < 8; ++i)
            {
                const time_point deadline = time_point::after(seconds);
                const time_point woke     = waiter.waitUntil(deadline);
                const time_point after    = time_point::now();
                CHECK(woke >= deadline);
                CHECK(woke.since(deadline) < LATE);
                CHECK(after >= woke);
            }
        }
    }

    TEST_CASE("alprecisewaiter: a deadline already past returns at once and leaves a wake for the next wait", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        const time_point start = time_point::now();
        bool was_rescued = false;
        const time_point woke = rescued(waiter, [&waiter]() { return waiter.waitUntil(time_point{ 0 }); }, was_rescued);
        CHECK_FALSE(was_rescued);
        CHECK(woke >= start);
        CHECK(woke.since(start) < LATE);

        const time_point earlier = time_point::now().plus(-0.001);
        const time_point woke_earlier = rescued(waiter, [&waiter, earlier]() { return waiter.waitUntil(earlier); }, was_rescued);
        CHECK_FALSE(was_rescued);
        CHECK(woke_earlier.since(earlier) < LATE);

        // A wake given before a wait that did not wait is the next wait's.
        waiter.wake();
        CHECK(waiter.waitUntil(time_point::now()) >= start);
        const time_point next = time_point::now();
        const time_point woke_next = waiter.waitUntil(time_point::after(LOST));
        CHECK(woke_next.since(next) < LATE);
    }

    TEST_CASE("alprecisewaiter: a wake before the wait ends it at once and is used up", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        waiter.wake();
        waiter.wake();
        const time_point start = time_point::now();
        const time_point woke  = waiter.waitUntil(time_point::after(LOST));
        CHECK(woke >= start);
        CHECK(woke.since(start) < LATE);

        // Two wakes before a wait are one: the next timed wait runs to its deadline.
        const time_point deadline = time_point::after(0.002);
        const time_point timed    = waiter.waitUntil(deadline);
        CHECK(timed >= deadline);
        CHECK(timed.since(deadline) < LATE);

        // Untimed alike.
        waiter.wake();
        bool was_rescued = false;
        rescued(waiter, [&waiter]() { return waiter.waitUntil(time_point::max()); }, was_rescued);
        CHECK_FALSE(was_rescued);
    }

    TEST_CASE("alprecisewaiter: a wake during a timed wait ends it", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        Wake  wake;
        Waker waker;
        waker.post([&waiter, &wake]() { wakeWhenWaiting(waiter, wake, 0.02); });
        wake.mWaiting = true;
        const time_point woke = waiter.waitUntil(time_point::after(2.0 * LOST));
        wake.mReturned = true;
        CHECK_FALSE(wake.mRescued);
        CHECK(woke.ticks >= wake.mAt.load());
        CHECK(woke.since(time_point{ wake.mAt.load() }) < LATE);
    }

    TEST_CASE("alprecisewaiter: an untimed wait ends at a wake", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        Wake  wake;
        Waker waker;
        waker.post([&waiter, &wake]() { wakeWhenWaiting(waiter, wake, 0.02); });
        wake.mWaiting = true;
        const time_point woke = waiter.waitUntil(time_point::max());
        wake.mReturned = true;
        CHECK_FALSE(wake.mRescued);
        CHECK(woke.ticks >= wake.mAt.load());
        CHECK(woke.since(time_point{ wake.mAt.load() }) < LATE);
    }

    TEST_CASE("alprecisewaiter: a timer and a wake that race leave the next wait whole", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        Waker waker;
        for (int i = 0; i < 200; ++i)
        {
            // The wake aimed at the deadline, a little either side of it from one round to the next.
            Wake wake;
            const time_point deadline = time_point::after(0.001);
            const time_point aim      = deadline.plus(F64(i % 5 - 2) * 0.0001);
            waker.post([&waiter, &wake, aim]()
                {
                    ALPreciseWaiter::spinUntil(aim);
                    wake.mAt = time_point::now().ticks;
                    waiter.wake();
                    wake.mWoken = true;
                });
            const time_point woke = waiter.waitUntil(deadline);
            // Ended by the timer, or by the wake once it was given.
            CHECK((woke >= deadline || woke.ticks >= wake.mAt.load()));
            CHECK(woke.since(deadline) < LATE);

            ALPreciseWaiter::spinUntil(time_point::after(LOST), [&wake]() { return wake.mWoken.load(); });
            REQUIRE(wake.mWoken);
            // The wake given, the next wait either has it still, or runs to its deadline: the timer the race left
            // behind ends nothing early.
            const time_point next_deadline = time_point::after(0.002);
            const time_point next          = waiter.waitUntil(next_deadline);
            CHECK(next.since(next_deadline) < LATE);
            if (woke < deadline)
            {
                // The wake ended the race, and was used up there.
                CHECK(next >= next_deadline);
            }
        }
    }

    TEST_CASE("alprecisewaiter: zero negative and NaN durations do not wait", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        const time_point start = time_point::now();
        for (F64 seconds : { 0.0, -0.0, -1.0, -1.0e300, -std::numeric_limits<F64>::infinity(),
                             std::numeric_limits<F64>::quiet_NaN() })
        {
            INFO("a wait of " << seconds << " s");
            bool was_rescued = false;
            const time_point woke = rescued(waiter, [&waiter, seconds]() { return waiter.waitFor(seconds); }, was_rescued);
            CHECK_FALSE(was_rescued);
            CHECK(woke.since(start) < LATE);
            CHECK(ALPreciseWaiter::sleepFor(seconds).since(start) < LATE);
        }
        bool was_rescued = false;
        rescued(waiter, [&waiter]() { return waiter.waitFor(std::chrono::nanoseconds::min()); }, was_rescued);
        CHECK_FALSE(was_rescued);
        rescued(waiter, [&waiter]() { return waiter.waitFor(std::chrono::seconds::zero()); }, was_rescued);
        CHECK_FALSE(was_rescued);
        CHECK(time_point::now().since(start) < LATE);
    }

    // macOS's waiter holds its timer and its wake in one kqueue, so there is no timer to take from it alone.
#if !LL_DARWIN
    // Timed waits that end no sooner than their deadlines, and one that a wake during it ends, on `waiter` however it
    // was left.
    void checkDeadlinesAndWake(ALPreciseWaiter& waiter)
    {
        for (F64 seconds : { 0.0005, 0.002, 0.0105 })
        {
            INFO("a wait of " << seconds * 1000.0 << " ms");
            for (int i = 0; i < 4; ++i)
            {
                const time_point deadline = time_point::after(seconds);
                const time_point woke     = waiter.waitUntil(deadline);
                CHECK(woke >= deadline);
                CHECK(woke.since(deadline) < LATE);
            }
        }
        Wake  wake;
        Waker waker;
        waker.post([&waiter, &wake]() { wakeWhenWaiting(waiter, wake, 0.02); });
        wake.mWaiting = true;
        const time_point woke = waiter.waitUntil(time_point::after(2.0 * LOST));
        wake.mReturned = true;
        CHECK_FALSE(wake.mRescued);
        CHECK(woke.ticks >= wake.mAt.load());
        CHECK(woke.since(time_point{ wake.mAt.load() }) < LATE);
    }

#if LL_WINDOWS
    TEST_CASE("alprecisewaiter: with an ordinary timer a waiter keeps to its deadlines and its wakes", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        REQUIRE(ll_test::PreciseWaiterProbe::ordinaryTimer(waiter));
        checkDeadlinesAndWake(waiter);
    }
#endif

    TEST_CASE("alprecisewaiter: with no timer a waiter keeps to its deadlines and its wakes", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        ll_test::PreciseWaiterProbe::noTimer(waiter);
        REQUIRE(waiter.canWake());
        checkDeadlinesAndWake(waiter);
    }
#endif

    TEST_CASE("alprecisewaiter: with nothing to wake it a waiter sleeps its timed waits and does not hang", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        ll_test::PreciseWaiterProbe::noWake(waiter);
        CHECK_FALSE(waiter.canWake());
        // A wake goes nowhere.
        waiter.wake();
        for (F64 seconds : { 0.0005, 0.002 })
        {
            const time_point deadline = time_point::after(seconds);
            const time_point woke     = waiter.waitUntil(deadline);
            CHECK(woke >= deadline);
            CHECK(woke.since(deadline) < LATE);
        }
        // Untimed, with nothing that could end it: at once.
        const time_point start = time_point::now();
        CHECK(waiter.waitUntil(time_point::max()).since(start) < LATE);
    }

    TEST_CASE("alprecisewaiter: a saturated duration has no deadline", "[alprecisewaiter]")
    {
        CHECK(time_point::after(std::chrono::seconds::max()) == time_point::max());
        CHECK(time_point::after(1.0e300) == time_point::max());
        CHECK(time_point::after(std::numeric_limits<F64>::infinity()) == time_point::max());
        CHECK(time_point::max().plus(-1.0) == time_point::max());
        CHECK(time_point::max().plus(1.0) == time_point::max());
        CHECK(time_point{ 5 }.plus(-1.0e300) == time_point{ 0 });
        CHECK(time_point{ (std::numeric_limits<U64>::max)() - 5 }.plus(1.0) == time_point::max());
        CHECK(time_point{ 5 }.plus(std::numeric_limits<F64>::quiet_NaN()) == time_point{ 5 });

        // An hour is no saturation, and a day from now is a day from now.
        const time_point now = time_point::now();
        const time_point day = now.plus(86400.0);
        CHECK(day != time_point::max());
        CHECK(day.since(now) >= 86400.0);
        CHECK(day.since(now) < 86400.001);
        CHECK(now.since(day) <= -86400.0);

        // A wait with no deadline, which a wake ends.
        ALPreciseWaiter waiter;
        Wake  wake;
        Waker waker;
        waker.post([&waiter, &wake]() { wakeWhenWaiting(waiter, wake, 0.02); });
        wake.mWaiting = true;
        const time_point woke = waiter.waitFor(std::chrono::seconds::max());
        wake.mReturned = true;
        CHECK_FALSE(wake.mRescued);
        CHECK(woke.ticks >= wake.mAt.load());
        CHECK(woke.since(time_point{ wake.mAt.load() }) < LATE);
    }

    TEST_CASE("alprecisewaiter: a spin ends at its deadline or once it is done", "[alprecisewaiter]")
    {
        for (F64 seconds : { 0.0, 0.0001, 0.001, 0.003 })
        {
            INFO("a spin of " << seconds * 1000.0 << " ms");
            const time_point deadline = time_point::after(seconds);
            const time_point stopped  = ALPreciseWaiter::spinUntil(deadline);
            CHECK(stopped >= deadline);
            CHECK(stopped.since(deadline) < LATE);
        }

        int asked = 0;
        const time_point start   = time_point::now();
        const time_point stopped = ALPreciseWaiter::spinUntil(time_point::after(LOST), [&asked]() { return ++asked == 1000; });
        CHECK(asked == 1000);
        CHECK(stopped.since(start) < LATE);
    }

    TEST_CASE("alprecisewaiter: a plain sleep ends no sooner than its deadline", "[alprecisewaiter]")
    {
        for (F64 seconds : { 0.0001, 0.0005, 0.001, 0.002, 0.005 })
        {
            INFO("a sleep of " << seconds * 1000.0 << " ms");
            for (int i = 0; i < 8; ++i)
            {
                const time_point deadline = time_point::after(seconds);
                const time_point woke     = ALPreciseWaiter::sleepUntil(deadline);
                CHECK(woke >= deadline);
                CHECK(woke.since(deadline) < LATE);
                CHECK(time_point::now() >= woke);
            }
        }
        const time_point start = time_point::now();
        CHECK(ALPreciseWaiter::sleepUntil(time_point{ 0 }).since(start) < LATE);
        CHECK(ALPreciseWaiter::sleepUntil(start.plus(-1.0)).since(start) < LATE);

        // A duration of the standard library's, in the counter's ticks as the sleep rounds it.
        for (const auto duration : { std::chrono::microseconds(500), std::chrono::microseconds(2000) })
        {
            const time_point before = time_point::now();
            const time_point woke   = ALPreciseWaiter::sleepFor(duration);
            CHECK(woke.ticks - before.ticks >= time_point{ 0 }.plus(std::chrono::duration<F64>(duration).count()).ticks);
            CHECK(woke.since(before) < LATE);
        }
    }

    TEST_CASE("alprecisewaiter: the margin is the 99th percentile of the recent lateness", "[alprecisewaiter]")
    {
        using Lateness = ALPreciseWaiter::Lateness;
        // Lateness in whole microseconds, as the counter's ticks.
        const auto us = [](U32 microseconds) { return time_point{ 0 }.plus(F64(microseconds) * 1.0e-6).ticks; };
        const auto seconds = [](U64 ticks) { return time_point{ ticks }.since(time_point{ 0 }); };

        Lateness lateness;
        CHECK(lateness.margin() == ALPreciseWaiter::MARGIN_SEED);
        for (U32 i = 1; i < Lateness::MIN_SAMPLES; ++i)
        {
            lateness.note(us(i));
        }
        // Too few to say yet.
        CHECK(lateness.margin() == ALPreciseWaiter::MARGIN_SEED);
        lateness.note(us(Lateness::MIN_SAMPLES));
        // 1 to 32 us: the ceiling of 99% of 32 is the 32nd.
        CHECK(lateness.margin() == seconds(us(32)));
        // Each wait counts from the next ask on, not every so many waits.
        lateness.note(us(1500));
        CHECK(lateness.margin() == seconds(us(1500)));

        // A full window of 0 to 255 us, after waits a second late that it has let go of: the 254th of the 256.
        for (U32 i = 0; i < 40; ++i)
        {
            lateness.note(us(1000000));
        }
        for (U32 i = 0; i < Lateness::SAMPLES; ++i)
        {
            lateness.note(us((i * 37) % Lateness::SAMPLES));
        }
        CHECK(lateness.margin() == seconds(us(253)));

        // No more than MARGIN_MAX, however late, and past what a sample holds.
        for (U32 i = 0; i < Lateness::SAMPLES; ++i)
        {
            lateness.note(i % 2 ? us(5000) : (std::numeric_limits<U64>::max)());
        }
        CHECK(lateness.margin() == ALPreciseWaiter::MARGIN_MAX);
        for (U32 i = 0; i < Lateness::SAMPLES; ++i)
        {
            lateness.note(0);
        }
        CHECK(lateness.margin() == 0.0);
    }

    TEST_CASE("alprecisewaiter: a waiter's margin comes from its own timed waits", "[alprecisewaiter]")
    {
        ALPreciseWaiter waiter;
        CHECK(waiter.margin() == ALPreciseWaiter::MARGIN_SEED);

        // Each wait's lateness as the clock it returns shows it. A wait whose deadline passed before it could look
        // notes nothing, so the margin is of some of these, which bounds it by the latest of all of them.
        F64 latest = 0.0;
        for (int i = 0; i < 320; ++i)
        {
            const time_point deadline = time_point::after(0.0005);
            latest = (std::max)(latest, waiter.waitUntil(deadline).since(deadline));
        }
        INFO("latest " << latest * 1000.0 << " ms; margin " << waiter.margin() * 1000.0 << " ms");
        CHECK(waiter.margin() <= (std::min)(latest, ALPreciseWaiter::MARGIN_MAX));
        CHECK(waiter.margin() >= 0.0);
    }

#if LL_WINDOWS
    TEST_CASE("alprecisewaiter: every handle a waiter or a sleep makes is closed", "[alprecisewaiter]")
    {
        // A cycle first, so that what the pool and the log make once is made before the count.
        const auto cycle = []()
            {
                LL::ThreadPool pool("alprecisewaiter_test_sleeper", 1, 64, false, true);
                pool.start();
                std::atomic<bool> slept{ false };
                REQUIRE(pool.getQueue().post([&slept]()
                    {
                        ALPreciseWaiter::sleepFor(0.0001);
                        slept = true;
                    }));
                ALPreciseWaiter::spinUntil(time_point::after(LOST), [&slept]() { return slept.load(); });
                REQUIRE(slept);
            };
        cycle();
        ALPreciseWaiter::sleepFor(0.0001);

        DWORD before = 0;
        DWORD after  = 0;
        REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &before));
        for (int i = 0; i < 64; ++i)
        {
            ALPreciseWaiter waiter;
            waiter.wake();
            waiter.waitFor(0.0001);
        }
        // A thread that sleeps makes a timer of its own for it, which goes with the thread.
        for (int i = 0; i < 16; ++i)
        {
            cycle();
        }
        REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &after));
        INFO("handles before " << before << ", after " << after);
        // Two a waiter and one a thread, were they kept: a few either way are the process's own comings and goings.
        CHECK(after < before + 8);
    }
#endif
}
