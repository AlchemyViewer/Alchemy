/**
 * @file   llcond_test.cpp
 * @author Nat Goodspeed
 * @date   2019-07-18
 * @brief  Test for llcond.
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Copyright (c) 2019, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llcond.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include "llcoros.h"

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct llcond_data
    {
        LLScalarCond<int> cond{0};
    };

    TEST_CASE_METHOD(llcond_data, "llcond: a wait for the value it already holds returns at once", "[llcond]")
    {
        cond.set_one(1);
        CHECK(cond.wait_for_equal(F32Milliseconds(1), 1));
        CHECK_FALSE(cond.wait_for_unequal(F32Milliseconds(1), 1));
    }

    TEST_CASE_METHOD(llcond_data, "llcond: two coroutines take turns through set_all and wait_equal", "[llcond]")
    {
        LLCoros::instance().launch(
            "test<2>",
            [this]()
            {
                // Lambda immediately entered -- control comes here first.
                // CHECKs: a REQUIRE that failed would end the coroutine before
                // it set what the test waits for, and the case would hang.
                CHECK(cond.get() == 0);
                cond.set_all(1);
                cond.wait_equal(2);
                CHECK(cond.get() == 2);
                cond.set_all(3);
            });
        // Main coroutine is resumed only when the lambda waits.
        CHECK(cond.get() == 1);
        cond.set_all(2);
        cond.wait_equal(3);
    }
} // anonymous namespace
