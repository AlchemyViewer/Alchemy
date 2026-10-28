/**
 * @file altimerresolutionscope.h
 * @brief Holds the system timer's resolution at a period while it lives.
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

// Holds the system timer's resolution at a period for as long as it lives, on
// Windows: what Sleep(), a wait's timeout and an ordinary waitable timer round
// to, 15.6 ms unless a process asks for finer. Since Windows 10 2004 a
// process's request applies to that process alone, and Windows 11 sets it
// aside while every window of the process is out of sight.
//
// The period asked for is clamped to what the system can do
// (timeGetDevCaps()), and given back only where it was granted.
// ALPreciseWaiter's waits do not depend on it; a timed condition variable, a
// wait's timeout and a sleep of the system's own still do. Elsewhere it holds
// nothing.
class LL_COMMON_API ALTimerResolutionScope
{
public:
    explicit ALTimerResolutionScope(U32 milliseconds = 1);
    ~ALTimerResolutionScope();
    ALTimerResolutionScope(const ALTimerResolutionScope&)            = delete;
    ALTimerResolutionScope& operator=(const ALTimerResolutionScope&) = delete;

    // The period held, in milliseconds: 0 where none was granted.
    U32 period() const { return mPeriod; }

private:
    U32 mPeriod = 0;
};
