/**
 * @file   llmainthreadtask_test.cpp
 * @author Nat Goodspeed
 * @date   2019-12-05
 * @brief  Test for llmainthreadtask.
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Copyright (c) 2019, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llmainthreadtask.h"
// STL headers
// std headers
#include <atomic>
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include "../test/sync.h"
#include "llthread.h"               // on_main_thread()
#include "lleventtimer.h"
#include "lockstatic.h"

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct llmainthreadtask_data
    {
        // 5-second timeout
        Sync mSync{F32Milliseconds(5000.0f)};

        llmainthreadtask_data()
        {
            // we're not testing the result; this is just to cache the
            // initial thread as the main thread.
            on_main_thread();
        }
    };

    TEST_CASE_METHOD(llmainthreadtask_data, "llmainthreadtask: dispatch on the main thread runs the task inline", "[llmainthreadtask]")
    {
        bool ran = false;
        bool result = LLMainThreadTask::dispatch(
            [&ran]()->bool{
                ran = true;
                return true;
            });
        CHECK(ran);
        CHECK(result);
    }

    // LockStatic finds its one instance a process by the type's name, even
    // in an anonymous namespace, so the name is this test's own.
    struct MainThreadTaskStatic
    {
        std::mutex mMutex;          // LockStatic looks for mMutex
        bool ran{false};
    };
    typedef llthread::LockStatic<MainThreadTaskStatic> LockStatic;

    TEST_CASE_METHOD(llmainthreadtask_data, "llmainthreadtask: dispatch from another thread runs the task on the main thread", "[llmainthreadtask]")
    {
        SKIP("This test is prone to build-time hangs");
        std::atomic_bool result(false);
        // wrapping our thread lambda in a packaged_task will catch any
        // exceptions it might throw and deliver them via future
        std::packaged_task<void()> thread_work(
            [this, &result](){
                // unblock the test's yield_until(1)
                mSync.set(1);
                // dispatch work to main thread -- should block here
                bool on_main(
                    LLMainThreadTask::dispatch(
                        []()->bool{
                            // have to lock static mutex to set static data
                            LockStatic()->ran = true;
                            // indicate whether task was run on the main thread
                            return on_main_thread();
                        }));
                // wait for the test to unblock us again
                mSync.yield_until(3);
                result = on_main;
            });
        auto thread_result = thread_work.get_future();
        std::thread thread;
        try
        {
            // run thread_work
            thread = std::thread(std::move(thread_work));
            // wait for thread to set(1)
            mSync.yield_until(1);
            // try to acquire the lock, should block because thread has it
            LockStatic lk;
            // wake up when dispatch() unlocks the static mutex
            UNSCOPED_INFO("shouldn't have run yet");
            CHECK_FALSE(lk->ran);
            UNSCOPED_INFO("shouldn't have returned yet");
            CHECK_FALSE(result);
            // unlock so the task can acquire the lock
            lk.unlock();
            // run the task -- should unblock thread, which will immediately block
            // on mSync
            LLEventTimer::updateClass();
            // 'lk', having unlocked, can no longer be used to access; relock with
            // a new LockStatic instance
            UNSCOPED_INFO("should now have run");
            CHECK(LockStatic()->ran);
            UNSCOPED_INFO("returned too early");
            CHECK_FALSE(result);
            // okay, let thread perform the assignment
            mSync.set(3);
        }
        catch (...)
        {
            // A test failure exception anywhere in the try block can cause
            // the test program to terminate without explanation when
            // ~thread() finds that 'thread' is still joinable. We could
            // either join() or detach() it -- but since it might be blocked
            // waiting for something from the main thread that now can never
            // happen, it's safer to detach it.
            thread.detach();
            throw;
        }
        // 'thread' should be all done now
        thread.join();
        // deliver any exception thrown by thread_work
        thread_result.get();
        CHECK(LockStatic()->ran);
        UNSCOPED_INFO("didn't run on main thread");
        CHECK(result);
    }
} // anonymous namespace
