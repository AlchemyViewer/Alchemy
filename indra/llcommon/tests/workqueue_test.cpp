/**
 * @file   workqueue_test.cpp
 * @author Nat Goodspeed
 * @date   2021-10-07
 * @brief  Test for workqueue.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "workqueue.h"
// STL headers
// std headers
#include <chrono>
#include <deque>
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include "altestfailure.h"
#include "../test/catch_and_store_what_in.h"
#include "llcond.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llstring.h"
#include "stringize.h"

using namespace LL;
using namespace std::literals::chrono_literals; // ms suffix
using namespace std::literals::string_literals; // s suffix

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct workqueue_data
    {
        WorkSchedule queue{"queue"};
    };

    TEST_CASE_METHOD(workqueue_data, "workqueue: a schedule is findable by name and an unnamed one gets a default", "[workqueue]")
    {
        CHECK(queue.getKey() == "queue");
        CHECK(WorkSchedule::getInstance("queue") == queue.getWeak().lock());
        WorkSchedule q2;
        CHECK(LLStringUtil::startsWith(q2.getKey(), "WorkQueue"));
    }

    TEST_CASE_METHOD(workqueue_data, "workqueue: posted work runs only when the queue is serviced", "[workqueue]")
    {
        bool wasRun{ false };
        // We only get away with binding a simple bool because we're running
        // the work on the same thread.
        queue.post([&wasRun](){ wasRun = true; });
        queue.close();
        UNSCOPED_INFO("ran too soon");
        CHECK_FALSE(wasRun);
        queue.runUntilClose();
        CHECK(wasRun);
    }

    TEST_CASE_METHOD(workqueue_data, "workqueue: postEvery repeats at its interval until the work returns false", "[workqueue]")
    {
        // record of runs
        using Shared = std::deque<WorkSchedule::TimePoint>;
        // This is an example of how to share data between the originator of
        // postEvery(work) and the work item itself, since usually a WorkSchedule
        // is used to dispatch work to a different thread. Neither of them
        // should call any of LLCond's wait methods: you don't want to stall
        // either the worker thread or the originating thread (conventionally
        // main). Use LLCond or a subclass even if all you want to do is
        // signal the work item that it can quit; consider LLOneShotCond.
        LLCond<Shared> data;
        auto start = WorkSchedule::TimePoint::clock::now();
        // 2s seems like a long time to wait, since it directly impacts the
        // duration of this test program. Unfortunately GitHub's Mac runners
        // are pretty wimpy, and we're getting spurious "too late" errors just
        // because the thread doesn't wake up as soon as we want.
        auto interval = 2s;
        queue.postEvery(
            interval,
            [&data, count = 0]
            () mutable
            {
                // record the timestamp at which this instance is running
                data.update_one(
                    [](Shared& data)
                    {
                        data.push_back(WorkSchedule::TimePoint::clock::now());
                    });
                // by the 3rd call, return false to stop
                return (++count < 3);
            });
        // no convenient way to close() our queue while we've got a
        // postEvery() running, so run until we have exhausted the iterations
        // or we time out waiting
        for (auto finish = start + 10*interval;
             WorkSchedule::TimePoint::clock::now() < finish &&
             data.get([](const Shared& data){ return data.size(); }) < 3; )
        {
            queue.runPending();
            std::this_thread::sleep_for(interval/10);
        }
        // Take a copy of the captured deque.
        Shared result = data.get();
        REQUIRE(result.size() == 3);
        // postEvery() assumes you want the first call to happen right away.
        // Pretend our start time was (interval) earlier than that, to make
        // our too early/too late tests uniform for all entries.
        start -= interval;
        for (size_t i = 0; i < result.size(); ++i)
        {
            auto diff = result[i] - start;
            start += interval;
            try
            {
                CAPTURE(i);
                REQUIRE(diff >= interval);
                REQUIRE(diff < interval*1.5);
            }
            catch (const ll_test::Failure&)
            {
                auto interval_ms = interval / 1ms;
                auto diff_ms = diff / 1ms;
                std::cerr << "interval " << interval_ms
                          << "ms; diff " << diff_ms << "ms" << std::endl;
                throw;
            }
        }
    }

    TEST_CASE_METHOD(workqueue_data, "workqueue: postTo hands int and string results to the caller's callback", "[workqueue]")
    {
        WorkSchedule main("main");
        auto qptr = WorkSchedule::getInstance("queue");
        int result = 0;
        main.postTo(
            qptr,
            [](){ return 17; },
            // Note that a postTo() *callback* can safely bind a reference to
            // a variable on the invoking thread, because the callback is run
            // on the invoking thread. (Of course the bound variable must
            // survive until the callback is called.)
            [&result](int i){ result = i; });
        // this should post the callback to main
        qptr->runOne();
        // this should run the callback
        main.runOne();
        CHECK(result == 17);

        std::string alpha;
        // postTo() handles arbitrary return types
        main.postTo(
            qptr,
            [](){ return "abc"s; },
            [&alpha](const std::string& s){ alpha = s; });
        qptr->runPending();
        main.runPending();
        CHECK(alpha == "abc");
    }

    // Counts its copies. A copy made as work is posted stays with the thread that
    // posted it, and goes there while the queue's thread uses the original: an LLSD's
    // count of its owners is no atomic, so the two would race on it.
    struct CopyCount
    {
        int* mCopies;
        explicit CopyCount(int* copies): mCopies(copies) {}
        CopyCount(const CopyCount& other): mCopies(other.mCopies) { ++*mCopies; }
        CopyCount(CopyCount&& other) noexcept: mCopies(other.mCopies) {}
        CopyCount& operator=(const CopyCount& other) { mCopies = other.mCopies; ++*mCopies; return *this; }
        CopyCount& operator=(CopyCount&& other) noexcept { mCopies = other.mCopies; return *this; }
    };

    TEST_CASE_METHOD(workqueue_data, "workqueue: posting moves the work in, and postTo its result back", "[workqueue]")
    {
        int copies = 0;
        CopyCount held(&copies);
        queue.post([held = std::move(held)]() {});
        CHECK(copies == 0);
        queue.runOne();

        // The worker's thread hands its result to the caller's queue, keeping no copy.
        // (A schedule copies what it pops, on the thread popping it.)
        WorkSchedule main("main");
        auto qptr = WorkSchedule::getInstance("queue");
        bool called = false;
        copies = 0;
        main.postTo(
            qptr,
            [&copies](){ return CopyCount(&copies); },
            [&called](CopyCount){ called = true; });
        qptr->runOne();
        CHECK(copies == 0);
        main.runOne();
        CHECK(called);
    }

    TEST_CASE_METHOD(workqueue_data, "workqueue: postTo with void work still runs the callback", "[workqueue]")
    {
        WorkSchedule main("main");
        auto qptr = WorkSchedule::getInstance("queue");
        std::string observe;
        main.postTo(
            qptr,
            // The ONLY reason we can get away with binding a reference to
            // 'observe' in our work callable is because we're directly
            // calling qptr->runOne() on this same thread. It would be a
            // mistake to do that if some other thread were servicing 'queue'.
            [&observe](){ observe = "queue"; },
            [&observe](){ observe.append(";main"); });
        qptr->runOne();
        main.runOne();
        CHECK(observe == "queue;main");
    }

    TEST_CASE_METHOD(workqueue_data, "workqueue: waitForResult throws on the main coroutine but works on others", "[workqueue]")
    {
        std::string stored;
        // Try to call waitForResult() on this thread's main coroutine. It
        // should throw because the main coroutine must service the queue.
        auto what{ catch_what<WorkSchedule::Error>(
                [this, &stored](){ stored = queue.waitForResult(
                        [](){ return "should throw"; }); }) };
        UNSCOPED_INFO("lambda should not have run");
        CHECK(stored.empty());
        UNSCOPED_INFO("waitForResult() should have thrown");
        CHECK_FALSE(what.empty());
        UNSCOPED_INFO("exception: " << what);
        CHECK(what.find("waitForResult") != std::string::npos);

        // Call waitForResult() on a coroutine, with a string result.
        LLCoros::instance().launch(
            "waitForResult string",
            [this, &stored]()
            { stored = queue.waitForResult(
                    [](){ return "string result"; }); });
        llcoro::suspend();
        // Nothing will have happened yet because, even if the coroutine did
        // run immediately, all it did was to queue the inner lambda on
        // 'queue'. Service it.
        queue.runOne();
        llcoro::suspend();
        CHECK(stored == "string result");

        // Call waitForResult() on a coroutine, with a void callable.
        stored.clear();
        bool done = false;
        LLCoros::instance().launch(
            "waitForResult void",
            [this, &stored, &done]()
            {
                queue.waitForResult([&stored](){ stored = "ran"; });
                done = true;
            });
        llcoro::suspend();
        queue.runOne();
        llcoro::suspend();
        CHECK(stored == "ran");
        UNSCOPED_INFO("void waitForResult() didn't return");
        CHECK(done);
    }
} // anonymous namespace
