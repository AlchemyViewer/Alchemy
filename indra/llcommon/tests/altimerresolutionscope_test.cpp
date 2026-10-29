/**
 * @file altimerresolutionscope_test.cpp
 * @brief The period a scope holds is one the system can do, and held only
 *        where it was granted.
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

#include "../altimerresolutionscope.h"

#if LL_WINDOWS
#include "../llwin32headers.h"
#include <timeapi.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
#if LL_WINDOWS
    TEST_CASE("altimerresolutionscope: the period held is one the system can do", "[altimerresolutionscope]")
    {
        TIMECAPS caps;
        REQUIRE(timeGetDevCaps(&caps, sizeof(caps)) == MMSYSERR_NOERROR);

        {
            const ALTimerResolutionScope scope(1);
            CHECK(scope.period() == (std::max)(UINT(1), caps.wPeriodMin));
        }
        {
            // Below the least the system can do: the least.
            const ALTimerResolutionScope scope(0);
            CHECK(scope.period() == caps.wPeriodMin);
        }
        {
            // Above the most: the most.
            const ALTimerResolutionScope scope(caps.wPeriodMax + 1000);
            CHECK(scope.period() == caps.wPeriodMax);
        }
        {
            // Scopes nest, each holding its own.
            const ALTimerResolutionScope outer(4);
            const ALTimerResolutionScope inner(2);
            CHECK(outer.period() == 4);
            CHECK(inner.period() == 2);
        }
    }
#else
    TEST_CASE("altimerresolutionscope: elsewhere a scope holds nothing", "[altimerresolutionscope]")
    {
        const ALTimerResolutionScope scope(1);
        CHECK(scope.period() == 0);
    }
#endif
}
