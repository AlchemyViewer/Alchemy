/**
 * @file   threadsafeschedule_test.cpp
 * @author Nat Goodspeed
 * @date   2021-10-04
 * @brief  Test for threadsafeschedule.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "threadsafeschedule.h"
// STL headers
// std headers
#include <chrono>
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>

using namespace std::literals::chrono_literals; // ms suffix
using namespace std::literals::string_literals; // s suffix
using Queue = LL::ThreadSafeSchedule<std::string>;

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct threadsafeschedule_data
    {
        Queue queue;
    };

    TEST_CASE_METHOD(threadsafeschedule_data, "threadsafeschedule: entries pop in timestamp order and a closed queue drains", "[threadsafeschedule]")
    {
        // Simply calling push() a few times might result in indeterminate
        // delivery order if the resolution of steady_clock is coarser than
        // the real time required for each push() call. Explicitly increment
        // the timestamp for each one -- but since we're passing explicit
        // timestamps, make the queue reorder them.
        auto now{ Queue::Clock::now() };
        queue.push(Queue::TimeTuple(now + 200ms, "ghi"s));
        // Given the various push() overloads, you have to match the type
        // exactly: conversions are ambiguous.
        queue.push(now, "abc"s);
        queue.push(now + 100ms, "def"s);
        queue.close();
        auto entry = queue.pop();
        CHECK(std::get<0>(entry) == "abc"s);
        entry = queue.pop();
        CHECK(std::get<0>(entry) == "def"s);
        CHECK(queue.isClosed());
        CHECK_FALSE(queue.done());
        std::string s;
        bool popped = queue.tryPopFor(1s, s);
        UNSCOPED_INFO("failed to pop third");
        CHECK(popped);
        CHECK(s == "ghi"s);
        popped = queue.tryPop(s);
        UNSCOPED_INFO("queue not empty");
        CHECK_FALSE(popped);
        CHECK(queue.done());
    }
} // anonymous namespace
