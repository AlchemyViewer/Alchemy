/**
 * @file alprecisewaiter.h
 * @brief A wait that ends at its deadline as a high-resolution timer keeps
 *        it, or sooner when another thread wakes it.
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

#pragma once

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <chrono>
#include <concepts>
#include <limits>

namespace ll_test { struct PreciseWaiterProbe; }

// A wait until a deadline that another thread can end early: one thread
// waits on it, and any thread wakes it.
//
// Its clock is get_clock_count()'s, the native counter LLTimer reads, and a
// deadline is that counter's ticks. Never a std::chrono::steady_clock time
// point: on macOS that clock counts on through the system's sleeps, where
// this one stops, so the two drift apart by every sleep.
//
// The timer is each platform's high-resolution one, which the system timer's
// tick does not round:
//   Windows: a high-resolution waitable timer (Windows 10 1803 and later) and
//     an auto-reset event, waited on together. Before 1803 an ordinary
//     waitable timer, which keeps to the timer resolution the process holds.
//   Linux: a timerfd armed at the deadline on CLOCK_MONOTONIC and an eventfd,
//     polled together with no timeout of poll's own. The kernel fires a
//     timerfd with no slack; a poll, futex or nanosleep timeout pays 50 us of
//     it.
//   macOS: a kqueue holding a timer at the deadline in mach ticks, which is
//     what the counter reads, and a user event for the wake.
//
// A timer still wakes its thread late, by however long the system takes to
// run it. Each timed wait notes how late it woke, and margin() is the 99th
// percentile of the recent ones. A wait that has to end on its deadline waits
// to margin() before it and spins the rest (spinUntil()). The spin is a call
// of its own, so a wait that does not need it never pays it.
//
// sleepUntil() and sleepFor() are a plain sleep on the same clock, for any
// thread, with no waiter to make.
class LL_COMMON_API ALPreciseWaiter
{
public:
    // A moment on get_clock_count()'s timeline, in its ticks.
    struct LL_COMMON_API time_point
    {
        U64 ticks = 0;

        // No deadline: a wait until it is untimed.
        static constexpr time_point max() { return { (std::numeric_limits<U64>::max)() }; }
        static time_point now();
        // `seconds` from now, rounded up to a tick. max() where that is past
        // the clock's end, as a saturated duration is; now where `seconds` is
        // 0, negative or NaN.
        static time_point after(F64 seconds);
        template <typename Rep, typename Period>
        static time_point after(std::chrono::duration<Rep, Period> duration)
        {
            return after(std::chrono::duration<F64>(duration).count());
        }
        // A whole number of seconds reads too easily as the milliseconds
        // ms_sleep() takes: seconds are F64, or a duration says what it is.
        template <std::integral INT>
        static time_point after(INT) = delete;

        // `seconds` later, or earlier where negative, rounded away from this
        // moment to a tick: max() where that is past the clock's end, and the
        // clock's start where it is before it. max() stays max(), and NaN
        // moves nothing.
        time_point plus(F64 seconds) const;
        template <std::integral INT>
        time_point plus(INT) const = delete;
        // The seconds from `earlier` to this moment, negative where this is
        // the earlier.
        F64 since(time_point earlier) const;

        friend bool operator==(time_point a, time_point b) { return a.ticks == b.ticks; }
        friend bool operator!=(time_point a, time_point b) { return a.ticks != b.ticks; }
        friend bool operator<(time_point a, time_point b) { return a.ticks < b.ticks; }
        friend bool operator<=(time_point a, time_point b) { return a.ticks <= b.ticks; }
        friend bool operator>(time_point a, time_point b) { return a.ticks > b.ticks; }
        friend bool operator>=(time_point a, time_point b) { return a.ticks >= b.ticks; }
    };

    // The most margin() can be: past it a wait spins longer than the timer's
    // lateness is worth.
    static constexpr F64 MARGIN_MAX = 0.002;
    // What margin() is before a waiter has timed enough waits to say: how
    // late an idle machine's timed waits wake, at the 99th percentile, from
    // llcommon's oversleep bench.
    static constexpr F64 MARGIN_SEED = 0.001;

    // How late a waiter's timed waits woke, and the margin that comes of it:
    // the 99th percentile of the last SAMPLES of them, from 0 to MARGIN_MAX,
    // worked out when it is asked for after new waits, and MARGIN_SEED until
    // there are MIN_SAMPLES. A waiter that never asks never sorts. One thread
    // notes and asks.
    class LL_COMMON_API Lateness
    {
    public:
        static constexpr U32 SAMPLES     = 256;
        static constexpr U32 MIN_SAMPLES = 32;

        // A timed wait woke `ticks` of the counter past its deadline.
        void note(U64 ticks);
        F64 margin() const;

    private:
        U32          mTicks[SAMPLES] = {};
        U32          mNext   = 0; // where the next goes
        U32          mCount  = 0; // how many there are, up to SAMPLES
        mutable F64  mMargin = MARGIN_SEED;
        mutable bool mStale  = false;
    };

    ALPreciseWaiter();
    ~ALPreciseWaiter();
    ALPreciseWaiter(const ALPreciseWaiter&)            = delete;
    ALPreciseWaiter& operator=(const ALPreciseWaiter&) = delete;

    // Whether another thread can wake it: the system gave it what it waits
    // on. Without that a timed wait sleeps to its deadline, and an untimed
    // one returns at once.
    bool canWake() const;

    // Waits until `deadline`, or until wake(): the time it woke. A wake that
    // came before the wait ends it at once, and the wait uses it up. A
    // deadline already past returns at once and leaves such a wake for the
    // next wait. time_point::max() waits for a wake alone. One thread at a
    // time waits.
    time_point waitUntil(time_point deadline);
    time_point waitFor(F64 seconds) { return waitUntil(time_point::after(seconds)); }
    template <typename Rep, typename Period>
    time_point waitFor(std::chrono::duration<Rep, Period> duration)
    {
        return waitUntil(time_point::after(duration));
    }
    template <std::integral INT>
    time_point waitFor(INT) = delete;

    // Ends the wait in progress, or else the next one. From any thread, any
    // number of times: the wakes that come before a wait are one wake.
    void wake();

    // How long before a deadline a wait that has to end on it stops waiting
    // and spins: the 99th percentile of how late this waiter's recent timed
    // waits woke, from 0 to MARGIN_MAX (Lateness). Until it has timed enough
    // of them, what an idle machine's waits come to. From the thread that
    // waits.
    F64 margin() const { return mLateness.margin(); }

    // Spins until `deadline`, or until `done()` is true, which it asks between
    // pauses: the time it stopped. It keeps its core, never yielding round
    // after round as a sleep of nothing would. Only while more than
    // MARGIN_MAX of the spin is left does it, now and then, let a thread that
    // is ready have the core, which may keep it for a whole quantum: a spin
    // no longer than that never yields.
    template <typename DONE>
    static time_point spinUntil(time_point deadline, DONE&& done)
    {
        U64 yielded = 0;
        for (;;)
        {
            const time_point now = time_point::now();
            if (done() || now >= deadline)
            {
                return now;
            }
            spinRound(now, deadline, yielded);
        }
    }
    static time_point spinUntil(time_point deadline)
    {
        return spinUntil(deadline, []() { return false; });
    }

    // Sleeps until `deadline`, on the same timer a wait uses, and nothing but
    // the deadline ends it: the time it woke. time_point::max() sleeps for
    // good. From any thread.
    static time_point sleepUntil(time_point deadline);
    static time_point sleepFor(F64 seconds) { return sleepUntil(time_point::after(seconds)); }
    template <typename Rep, typename Period>
    static time_point sleepFor(std::chrono::duration<Rep, Period> duration)
    {
        return sleepUntil(time_point::after(duration));
    }
    template <std::integral INT>
    static time_point sleepFor(INT) = delete;

private:
    friend struct ll_test::PreciseWaiterProbe;

    // One round of a spin, short of `deadline`: a pause, or now and then a
    // yield. `yielded` is when the last yield was.
    static void spinRound(time_point now, time_point deadline, U64& yielded);

    Lateness mLateness;

#if LL_WINDOWS
    void* mWake  = nullptr; // auto-reset event
    void* mTimer = nullptr; // waitable timer
#elif LL_LINUX
    int mWake  = -1; // eventfd
    int mTimer = -1; // timerfd
#elif LL_DARWIN
    int mQueue = -1; // kqueue with the wake's user event, and the timer while a wait is timed
#endif
};
