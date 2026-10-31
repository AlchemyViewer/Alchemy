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
#include "llcond.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "lltimer.h"

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

    TEST_CASE_METHOD(threadsafeschedule_data, "threadsafeschedule: a consumer already waiting pops an entry pushed for later at its time", "[threadsafeschedule]")
    {
        // The consumer, a coroutine, is waiting on the empty queue for up to
        // ten seconds when the entry is pushed, due 50 ms on. Coroutines on
        // one thread take turns only when one suspends, so it is certainly
        // waiting by then. Its wait has to end at the entry's time, not run
        // on to its own limit.
        LLTimer       waited;
        std::string   got;
        bool          popped{ false };
        F64           popped_at{ 0.0 };
        LLOneShotCond finished;
        LLCoros::instance().launch("threadsafeschedule consumer", [&]()
            {
                popped = queue.tryPopFor(10s, got);
                popped_at = waited.getElapsedTimeF64();
                finished.set_one();
            });
        llcoro::suspend();
        queue.push(Queue::Clock::now() + 50ms, "later"s);
        const bool finished_in_time(finished.wait_for(15s));
        if (! finished_in_time)
        {
            // The coroutine holds this case's locals: closing the queue
            // ends its wait, so it is done with them before they go.
            queue.close();
            finished.wait();
        }
        REQUIRE(finished_in_time);
        CHECK(popped);
        CHECK(got == "later"s);
        CHECK(popped_at >= 0.05);
        CHECK(popped_at < 5.0);
    }
} // anonymous namespace
