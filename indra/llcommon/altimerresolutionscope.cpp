/**
 * @file altimerresolutionscope.cpp
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

#include "linden_common.h"

#include "altimerresolutionscope.h"

#if LL_WINDOWS
#include "llwin32headers.h"
#include <timeapi.h>

#include <algorithm>
#endif

ALTimerResolutionScope::ALTimerResolutionScope(U32 milliseconds)
{
#if LL_WINDOWS
    TIMECAPS caps;
    if (timeGetDevCaps(&caps, sizeof(caps)) != MMSYSERR_NOERROR)
    {
        LL_WARNS("Timer") << "The system timer's range is unknown; its resolution is left as it is" << LL_ENDL;
        return;
    }
    const UINT period = std::clamp(UINT(milliseconds), caps.wPeriodMin, caps.wPeriodMax);
    if (timeBeginPeriod(period) == TIMERR_NOERROR)
    {
        mPeriod = period;
    }
    else
    {
        LL_WARNS("Timer") << "The system timer's resolution was not set to " << period << " ms" << LL_ENDL;
    }
#else
    (void)milliseconds;
#endif
}

ALTimerResolutionScope::~ALTimerResolutionScope()
{
#if LL_WINDOWS
    if (mPeriod)
    {
        timeEndPeriod(mPeriod);
    }
#endif
}
