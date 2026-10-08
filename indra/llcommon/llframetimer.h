/**
 * @file llframetimer.h
 * @brief A lightweight timer that measures seconds and is only
 * updated once per frame.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_LLFRAMETIMER_H
#define LL_LLFRAMETIMER_H

/**
 * *NOTE: Because of limitations on linux which we do not really have
 * time to explore, the total time is derived from the frame time
 * and is recsynchronized on every frame.
 */

#include "lltimer.h"

// The frame clock is the main thread's: updateFrameTime() writes it there once a frame, and
// another thread reading it sees a time that thread did not sample and cannot tell when it
// moves. Anything timed off the main thread uses LLTimer. Debug builds check the statics.
#if LL_DEBUG
extern LL_COMMON_API bool on_main_thread();
#define LL_FRAME_CLOCK_ON_MAIN_THREAD() llassert_msg(on_main_thread(), "the frame clock read off the main thread")
#else
#define LL_FRAME_CLOCK_ON_MAIN_THREAD()
#endif

class LL_COMMON_API LLFrameTimer
{
public:
    LLFrameTimer() : mStartTime( sFrameTime ), mExpiry(0), mStarted(true) {}

    // Return the number of seconds since the start of this
    // application instance, as of the current frame. Static: it is not this
    // timer's elapsed time, which is getElapsedTimeF32().
    static F64SecondsImplicit getUptimeSeconds()
    {
        LL_FRAME_CLOCK_ON_MAIN_THREAD();
        return sFrameTime;
    }

    // Return a low precision usec since epoch
    static U64 getTotalTime()
    {
        LL_FRAME_CLOCK_ON_MAIN_THREAD();
        return sTotalTime ? U64MicrosecondsImplicit(sTotalTime) : totalTime();
    }

    // Return a low precision seconds since epoch
    static F64 getTotalSeconds()
    {
        LL_FRAME_CLOCK_ON_MAIN_THREAD();
        return sTotalSeconds;
    }

    // Call this method once per frame to update the current frame time.   This is actually called
    // at some other times as well
    static void updateFrameTime();

    // Call this method once, and only once, per frame to update the current frame count.
    static void updateFrameCount()                  { sFrameCount++; }

    static U32  getFrameCount()                     { LL_FRAME_CLOCK_ON_MAIN_THREAD(); return sFrameCount; }

    static F32  getFrameDeltaTimeF32();

    // The same interval in microseconds, as getTotalTime() counts them
    static U64  getFrameDeltaTime()                 { LL_FRAME_CLOCK_ON_MAIN_THREAD(); return sFrameDeltaTime; }

    // Return seconds since the current frame started
    static F32  getCurrentFrameTime();

    // MANIPULATORS
    void start();
    void stop();
    void reset();
    void resetWithExpiry(F32 expiration);
    void pause();
    void unpause();
    void setTimerExpirySec(F32 expiration);         // Expires this long from now, as LLTimer's does
    void setExpiryAt(F64 seconds_since_epoch);
    bool checkExpirationAndReset(F32 expiration);
    F32 getElapsedTimeAndResetF32()                 { F32 t = getElapsedTimeF32(); reset(); return t; }

    void setAge(const F64 age)                      { mStartTime = mStarted ? sFrameTime - age : age; }

    // ACCESSORS
    bool hasExpired() const                         { return (sFrameTime >= mExpiry); }
    F32  getTimeToExpireF32() const                 { return (F32)(mExpiry - sFrameTime); }
    F32  getElapsedTimeF32() const                  { return (F32)getElapsedTimeF64(); }
    F64  getElapsedTimeF64() const                  { return mStarted ? sFrameTime - mStartTime : mStartTime; }
    bool getStarted() const                         { return mStarted; }

    // return the seconds since epoch when this timer will expire.
    F64 expiresAt() const;

protected:
    // A single, high resolution timer that drives all LLFrameTimers
    // *NOTE: no longer used.
    //static LLTimer sInternalTimer;

    //
    // Aplication constants
    //

    // Start time of opp in usec since epoch
    static U64 sStartTotalTime;

    //
    // Data updated per frame
    //

    // Seconds since application start
    static F64 sFrameTime;

    // Time that has elapsed since last call to updateFrameTime()
    static U64 sFrameDeltaTime;

    // Total microseconds since epoch.
    static U64 sTotalTime;

    // Seconds since epoch.
    static F64 sTotalSeconds;

    // Total number of frames elapsed in application
    static S32 sFrameCount;

    //
    // Member data
    //

    // Number of seconds after application start when this timer was
    // started. Set equal to sFrameTime when reset.
    F64 mStartTime;

    // Timer expires this many seconds after application start time.
    F64 mExpiry;

    // Useful bit of state usually associated with timers, but does
    // not affect actual functionality
    bool mStarted;
};

// Glue code for Havok (or anything else that doesn't want the full .h files)
extern F32  getCurrentFrameTime();

#endif  // LL_LLFRAMETIMER_H
