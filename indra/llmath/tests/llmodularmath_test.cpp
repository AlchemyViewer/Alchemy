/**
 * @file modularmath_test.cpp
 * @author babbage
 * @date 2008-09
 * @brief llmodularmath tests
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "../llmodularmath.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("LLModularMath: subtract wraps when lhs is less than rhs", "[LLModularMath]")
    {
        // lhs < rhs
        const U32 lhs = 0x000001;
        const U32 rhs = 0xFFFFFF;
        const U32 width = 24;
        U32 result = LLModularMath::subtract<width>(lhs, rhs);
        CHECK(result == 2);
    }

    TEST_CASE("LLModularMath: subtract with lhs greater than rhs", "[LLModularMath]")
    {
        // lhs > rhs
        const U32 lhs = 0x000002;
        const U32 rhs = 0x000001;
        const U32 width = 24;
        U32 result = LLModularMath::subtract<width>(lhs, rhs);
        CHECK(result == 1);
    }

    TEST_CASE("LLModularMath: subtract of equal values is zero", "[LLModularMath]")
    {
        // lhs == rhs
        const U32 lhs = 0xABCDEF;
        const U32 rhs = 0xABCDEF;
        const U32 width = 24;
        U32 result = LLModularMath::subtract<width>(lhs, rhs);
        CHECK(result == 0);
    }
}
