/**
 * @file alprecisewaiter.cpp
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

#include "linden_common.h"

#include "alprecisewaiter.h"

#include "alsimd.h"
#include "llthread.h"
#include "lltimer.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#if LL_WINDOWS
#   include "llwin32headers.h"
#   ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#       define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#   endif
#else
#   include <cerrno>
#   include <climits>
#   include <ctime>
#   include <unistd.h>
#   if LL_LINUX
#       include <poll.h>
#       include <sys/eventfd.h>
#       include <sys/timerfd.h>
#   elif LL_DARWIN
#       include <mach/mach_time.h>
#       include <sys/event.h>
#   endif
#endif

namespace
{
    // A spin yields only while more than the longest margin is left of it, so that the spin at the end of a wait never
    // does, and no more often than this.
    constexpr F64 SPIN_YIELD_LEFT  = ALPreciseWaiter::MARGIN_MAX;
    constexpr F64 SPIN_YIELD_EVERY = 0.0005;

    // Past this many ticks a duration is the clock's end: below 2^63, so that adding one to any tick the counter
    // reads cannot wrap.
    constexpr F64 TICKS_SATURATED = 9.0e18;

    // The counter's ticks a second.
    F64 ticksPerSecond()
    {
        static const F64 hz = get_timer_info().mClockFrequency.value();
        return hz;
    }

    U64 ticksIn(F64 seconds)
    {
        return U64(std::ceil(seconds * ticksPerSecond()));
    }

#if LL_WINDOWS
    // A timer is armed for no more than this at a time, and a wait past it goes round again: it keeps the
    // arithmetic within range for a deadline however far off.
    constexpr F64 LONGEST_ARM_SECONDS = 3600.0;

    // `ticks` of the counter in the timer's 100 ns units, rounded up, at least one, at most LONGEST_ARM_SECONDS.
    LONGLONG hundredNanoseconds(U64 ticks)
    {
        static const U64 longest = ticksIn(LONGEST_ARM_SECONDS);
        const F64 units = std::ceil(F64((std::min)(ticks, longest)) * 1.0e7 / ticksPerSecond());
        return (std::max)(LONGLONG(1), LONGLONG(units));
    }

    // A timeout in whole milliseconds for `units` of 100 ns, rounded up: never 0, which would not wait, and never
    // INFINITE.
    DWORD milliseconds(LONGLONG units)
    {
        const LONGLONG ms = ((std::min)(units, LONGLONG(INFINITE - 1) * 10000) + 9999) / 10000;
        return DWORD(std::clamp(ms, LONGLONG(1), LONGLONG(INFINITE - 1)));
    }

    // A high-resolution timer where the system has them, Windows 10 1803 and later; else an ordinary one, which
    // keeps to the timer resolution the process holds.
    HANDLE makeTimer()
    {
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer)
        {
            timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        return timer;
    }

    // Relative, so that the calendar the timer's absolute times are on never moves it.
    bool armTimer(HANDLE timer, LONGLONG units)
    {
        LARGE_INTEGER due;
        due.QuadPart = -units;
        return SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0) != FALSE;
    }

    // The plain sleep's timer: one for each thread that sleeps, made at its first sleep and closed with the thread,
    // since a handle belongs to the process and would outlive it. Closed, it forgets the handle: a sleep later in
    // the thread's end, from a destructor that runs after this one, takes the fallback rather than arming a handle
    // the system may since have given to something else.
    struct SleepTimer
    {
        HANDLE mHandle = makeTimer();

        ~SleepTimer()
        {
            if (mHandle)
            {
                CloseHandle(mHandle);
                mHandle = nullptr;
            }
        }
    };
#endif

#if LL_LINUX
    timespec toTimespec(U64 nanoseconds)
    {
        timespec at;
        at.tv_sec  = time_t(nanoseconds / 1000000000ull);
        at.tv_nsec = long(nanoseconds % 1000000000ull);
        return at;
    }

    // Reads what a descriptor holds, so that it no longer polls ready.
    void drain(int fd)
    {
        U64 count;
        while (read(fd, &count, sizeof(count)) < 0 && errno == EINTR)
        {
        }
    }
#endif

#if LL_DARWIN
    constexpr uintptr_t WAKE_EVENT  = 1;
    constexpr uintptr_t TIMER_EVENT = 2;
#endif
}

//static
ALPreciseWaiter::time_point ALPreciseWaiter::time_point::now()
{
    return { get_clock_count() };
}

//static
ALPreciseWaiter::time_point ALPreciseWaiter::time_point::after(F64 seconds)
{
    // A NaN fails the comparison too.
    return now().plus(seconds > 0.0 ? seconds : 0.0);
}

ALPreciseWaiter::time_point ALPreciseWaiter::time_point::plus(F64 seconds) const
{
    if (*this == max() || std::isnan(seconds))
    {
        return *this;
    }
    const F64 whole = std::ceil(std::fabs(seconds) * ticksPerSecond());
    const U64 step  = whole >= TICKS_SATURATED ? max().ticks : U64(whole);
    if (seconds >= 0.0)
    {
        return step >= max().ticks - ticks ? max() : time_point{ ticks + step };
    }
    return { step >= ticks ? 0 : ticks - step };
}

F64 ALPreciseWaiter::time_point::since(time_point earlier) const
{
    const F64 ticks_between = ticks >= earlier.ticks ? F64(ticks - earlier.ticks) : -F64(earlier.ticks - ticks);
    return ticks_between / ticksPerSecond();
}

//static
void ALPreciseWaiter::spinRound(time_point now, time_point deadline, U64& yielded)
{
    // A yield may hand the core over for a whole quantum, and the viewer always has a thread ready to take it: only
    // while much of the spin is left, and only now and then.
    static const U64 yield_left  = ticksIn(SPIN_YIELD_LEFT);
    static const U64 yield_every = ticksIn(SPIN_YIELD_EVERY);
    if (!yielded)
    {
        yielded = now.ticks;
    }
    if (deadline.ticks - now.ticks > yield_left && now.ticks - yielded > yield_every)
    {
        yielded = now.ticks;
        LLThread::yield();
        return;
    }
    alsimd::pause();
}

void ALPreciseWaiter::Lateness::note(U64 ticks)
{
    mTicks[mNext] = U32((std::min)(ticks, U64((std::numeric_limits<U32>::max)())));
    mNext         = (mNext + 1) % SAMPLES;
    mCount        = (std::min)(mCount + 1, SAMPLES);
    mStale        = true;
}

F64 ALPreciseWaiter::Lateness::margin() const
{
    if (mStale && mCount >= MIN_SAMPLES)
    {
        U32 sorted[SAMPLES];
        std::copy_n(mTicks, mCount, sorted);
        // The ceiling of 99% of the count, as an index.
        const U32 p99 = (mCount * 99 + 99) / 100 - 1;
        std::nth_element(sorted, sorted + p99, sorted + mCount);
        mMargin = std::clamp(F64(sorted[p99]) / ticksPerSecond(), 0.0, MARGIN_MAX);
        mStale  = false;
    }
    return mMargin;
}

#if LL_WINDOWS

ALPreciseWaiter::ALPreciseWaiter()
:   mWake(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
    mTimer(makeTimer())
{
}

ALPreciseWaiter::~ALPreciseWaiter()
{
    if (mTimer)
    {
        CloseHandle(mTimer);
        mTimer = nullptr;
    }
    if (mWake)
    {
        CloseHandle(mWake);
        mWake = nullptr;
    }
}

bool ALPreciseWaiter::canWake() const
{
    return mWake != nullptr;
}

ALPreciseWaiter::time_point ALPreciseWaiter::waitUntil(time_point deadline)
{
    if (!mWake)
    {
        return deadline == time_point::max() ? time_point::now() : sleepUntil(deadline);
    }
    if (deadline == time_point::max())
    {
        WaitForSingleObject(mWake, INFINITE);
        return time_point::now();
    }
    bool timed = false;
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            if (timed)
            {
                mLateness.note(now - deadline.ticks);
            }
            return { now };
        }
        const LONGLONG due = hundredNanoseconds(deadline.ticks - now);
        DWORD woke;
        if (mTimer && armTimer(mTimer, due))
        {
            const HANDLE waits[] = { mWake, mTimer };
            woke = WaitForMultipleObjects(DWORD(std::size(waits)), waits, FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0)
            {
                // Woken before the deadline, which nothing waits for now.
                CancelWaitableTimer(mTimer);
            }
        }
        else
        {
            // No timer to arm: a timeout, which the system timer's tick rounds, and which a wake still ends.
            woke = WaitForSingleObject(mWake, milliseconds(due));
        }
        if (woke == WAIT_OBJECT_0)
        {
            return time_point::now();
        }
        if (woke == WAIT_FAILED)
        {
            return sleepUntil(deadline);
        }
        timed = true;
    }
}

void ALPreciseWaiter::wake()
{
    // Auto-reset: a wake that comes before the wait is kept for it.
    if (mWake)
    {
        SetEvent(mWake);
    }
}

//static
ALPreciseWaiter::time_point ALPreciseWaiter::sleepUntil(time_point deadline)
{
    thread_local SleepTimer timer;
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            return { now };
        }
        const LONGLONG due = hundredNanoseconds(deadline.ticks - now);
        if (!timer.mHandle || !armTimer(timer.mHandle, due) || WaitForSingleObject(timer.mHandle, INFINITE) != WAIT_OBJECT_0)
        {
            Sleep(milliseconds(due));
        }
    }
}

#elif LL_LINUX

ALPreciseWaiter::ALPreciseWaiter()
:   mWake(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
    mTimer(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK))
{
}

ALPreciseWaiter::~ALPreciseWaiter()
{
    if (mTimer >= 0)
    {
        close(mTimer);
        mTimer = -1;
    }
    if (mWake >= 0)
    {
        close(mWake);
        mWake = -1;
    }
}

bool ALPreciseWaiter::canWake() const
{
    return mWake >= 0;
}

ALPreciseWaiter::time_point ALPreciseWaiter::waitUntil(time_point deadline)
{
    if (mWake < 0)
    {
        return deadline == time_point::max() ? time_point::now() : sleepUntil(deadline);
    }
    if (deadline == time_point::max())
    {
        pollfd wake{ mWake, POLLIN, 0 };
        while (poll(&wake, 1, -1) < 0 && errno == EINTR)
        {
        }
        drain(mWake);
        return time_point::now();
    }
    bool timed = false;
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            if (timed)
            {
                mLateness.note(now - deadline.ticks);
            }
            return { now };
        }
        // The counter is CLOCK_MONOTONIC in nanoseconds, so the deadline arms the timer as it is; the tests hold the
        // counter to that clock.
        pollfd waits[] = { { mWake, POLLIN, 0 }, { mTimer, POLLIN, 0 } };
        nfds_t count   = 1;
        int    timeout = -1;
        itimerspec at{};
        at.it_value = toTimespec(deadline.ticks);
        if (mTimer >= 0 && timerfd_settime(mTimer, TFD_TIMER_ABSTIME, &at, nullptr) == 0)
        {
            count = 2;
        }
        else
        {
            // No timer to arm: poll's own timeout, in whole milliseconds rounded up, which a wake still ends.
            const U64 ms = ((std::min)(deadline.ticks - now, U64(INT_MAX - 1) * 1000000ull) + 999999ull) / 1000000ull;
            timeout = int(std::clamp(ms, U64(1), U64(INT_MAX)));
        }
        if (poll(waits, count, timeout) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return sleepUntil(deadline);
        }
        if (waits[0].revents & POLLIN)
        {
            drain(mWake);
            if (count == 2)
            {
                // Woken before the deadline, which nothing waits for now.
                const itimerspec off{};
                timerfd_settime(mTimer, 0, &off, nullptr);
            }
            return time_point::now();
        }
        if (count == 2 && (waits[1].revents & POLLIN))
        {
            drain(mTimer);
        }
        timed = true;
    }
}

void ALPreciseWaiter::wake()
{
    // A counter, which the wait's read empties: wakes before the wait are kept for it, as one.
    if (mWake >= 0)
    {
        const U64 one = 1;
        while (write(mWake, &one, sizeof(one)) < 0 && errno == EINTR)
        {
        }
    }
}

//static
ALPreciseWaiter::time_point ALPreciseWaiter::sleepUntil(time_point deadline)
{
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            return { now };
        }
        // Absolute, so that a signal that cuts it short loses nothing when it sleeps again.
        const timespec at = toTimespec(deadline.ticks);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &at, nullptr);
    }
}

#elif LL_DARWIN

ALPreciseWaiter::ALPreciseWaiter()
:   mQueue(kqueue())
{
    if (mQueue >= 0)
    {
        struct kevent wake;
        EV_SET(&wake, WAKE_EVENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(mQueue, &wake, 1, nullptr, 0, nullptr) < 0)
        {
            close(mQueue);
            mQueue = -1;
        }
    }
}

ALPreciseWaiter::~ALPreciseWaiter()
{
    if (mQueue >= 0)
    {
        close(mQueue);
        mQueue = -1;
    }
}

bool ALPreciseWaiter::canWake() const
{
    return mQueue >= 0;
}

ALPreciseWaiter::time_point ALPreciseWaiter::waitUntil(time_point deadline)
{
    if (mQueue < 0)
    {
        return deadline == time_point::max() ? time_point::now() : sleepUntil(deadline);
    }
    if (deadline == time_point::max())
    {
        for (;;)
        {
            struct kevent event;
            const int ready = kevent(mQueue, nullptr, 0, &event, 1, nullptr);
            if ((ready > 0 && event.filter == EVFILT_USER) || (ready < 0 && errno != EINTR))
            {
                return time_point::now();
            }
        }
    }
    bool timed = false;
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            if (timed)
            {
                mLateness.note(now - deadline.ticks);
            }
            return { now };
        }
        // The counter is mach absolute time, which NOTE_MACHTIME with NOTE_ABSOLUTE takes as it is; the tests hold the
        // counter to that clock. An absolute timer goes once it has fired; NOTE_CRITICAL keeps the system from
        // coalescing it with others.
        struct kevent timer;
        EV_SET(&timer, TIMER_EVENT, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_MACHTIME | NOTE_ABSOLUTE | NOTE_CRITICAL,
               intptr_t(deadline.ticks), nullptr);
        struct kevent events[2];
        const int ready = kevent(mQueue, &timer, 1, events, 2, nullptr);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return sleepUntil(deadline);
        }
        bool woken  = false;
        bool failed = false;
        for (int i = 0; i < ready; ++i)
        {
            woken  = woken || events[i].filter == EVFILT_USER;
            failed = failed || (events[i].flags & EV_ERROR);
        }
        if (woken)
        {
            // Woken before the deadline, which nothing waits for now.
            EV_SET(&timer, TIMER_EVENT, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
            kevent(mQueue, &timer, 1, nullptr, 0, nullptr);
            return time_point::now();
        }
        if (failed)
        {
            return sleepUntil(deadline);
        }
        timed = true;
    }
}

void ALPreciseWaiter::wake()
{
    // Triggered until a wait takes it, which clears it: wakes before the wait are kept for it, as one.
    if (mQueue >= 0)
    {
        struct kevent trigger;
        EV_SET(&trigger, WAKE_EVENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(mQueue, &trigger, 1, nullptr, 0, nullptr);
    }
}

//static
ALPreciseWaiter::time_point ALPreciseWaiter::sleepUntil(time_point deadline)
{
    for (;;)
    {
        const U64 now = get_clock_count();
        if (now >= deadline.ticks)
        {
            return { now };
        }
        // Mach ticks, as the counter reads; an interrupted wait returns early and sleeps again.
        mach_wait_until(deadline.ticks);
    }
}

#endif
