/**
 * @file   tuple_test.cpp
 * @author Nat Goodspeed
 * @date   2021-10-04
 * @brief  Test for tuple.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "tuple.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>

/*****************************************************************************
*   TUT
*****************************************************************************/
namespace
{
    TEST_CASE("tuple: tuple_split undoes tuple_cons", "[tuple]")
    {
        std::tuple<std::string, int> tup{ "abc", 17 };
        std::tuple<int, std::string, int> ptup{ tuple_cons(34, tup) };
        std::tuple<std::string, int> tup2;
        int i;
        std::tie(i, tup2) = tuple_split(ptup);
        UNSCOPED_INFO("tuple_car() fail");
        CHECK(i == 34);
        UNSCOPED_INFO("tuple_cdr() (0) fail");
        CHECK(std::get<0>(tup2) == "abc");
        UNSCOPED_INFO("tuple_cdr() (1) fail");
        CHECK(std::get<1>(tup2) == 17);
    }
} // anonymous namespace
