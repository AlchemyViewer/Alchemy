/**
 * @file llprocinfo_test.cpp
 * @brief Tests for the LLProcInfo class.
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

#include "../llprocinfo.h"

#include <catch2/catch_test_macros.hpp>
#include "../lltimer.h"


static const LLProcInfo::time_type bad_user(289375U), bad_system(275U);


namespace
{

struct procinfo_test
{
    procinfo_test()
        {
        }
};

// Basic invocation works
TEST_CASE_METHOD(procinfo_test, "LLProcInfo: getCPUUsage() writes both its arguments", "[LLProcInfo]")
{
    LLProcInfo::time_type user(bad_user), system(bad_system);

    LLProcInfo::getCPUUsage(user, system);

    CHECK(user != bad_user);
    CHECK(system != bad_system);
}


// Time increases
TEST_CASE_METHOD(procinfo_test, "LLProcInfo: getCPUUsage() never goes down over time", "[LLProcInfo]")
{
    LLProcInfo::time_type user(bad_user), system(bad_system);
    LLProcInfo::time_type user2(bad_user), system2(bad_system);

    LLProcInfo::getCPUUsage(user, system);

    for (int i(0); i < 100000; ++i)
    {
        ms_sleep(0);
    }

    LLProcInfo::getCPUUsage(user2, system2);

    CHECK(user2 >= user);
    CHECK(system2 >= system);
}


} // anonymous namespace
