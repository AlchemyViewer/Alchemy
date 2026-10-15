/**
 * @file   coroutine_test.cpp
 * @author Nat Goodspeed
 * @date   2009-04-22
 * @brief  Test for coroutine.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#define BOOST_RESULT_OF_USE_TR1 1
#include <boost/bind.hpp>
#include <boost/range.hpp>
#include <boost/utility.hpp>

#include "linden_common.h"

#include <iostream>
#include <string>
#include <typeinfo>

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"
#include "../test/lltestapp.h"
#include "alteststateguard.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llevents.h"
#include "llcoros.h"
#include "lleventfilter.h"
#include "lleventcoro.h"
#include "../test/debug.h"
#include "../test/sync.h"

using namespace llcoro;

/*****************************************************************************
*   Test helpers
*****************************************************************************/
namespace
{

/// Simulate an event API whose response is immediate: sent on receipt of the
/// initial request, rather than after some delay. This is the case that
/// distinguishes postAndSuspend() from calling post(), then calling
/// suspendUntilEventOn().
class ImmediateAPI
{
public:
    ImmediateAPI(Sync& sync):
        mPump("immediate", true),
        mSync(sync)
    {
        mPump.listen("API", boost::bind(&ImmediateAPI::operator(), this, _1));
    }

    LLEventPump& getPump() { return mPump; }

    // Invoke this with an LLSD map containing:
    // ["value"]: Integer value. We will reply with ["value"] + 1.
    // ["reply"]: Name of LLEventPump on which to send response.
    bool operator()(const LLSD& event) const
    {
        mSync.bump();
        LLSD::Integer value(event["value"]);
        LLEventPumps::instance().obtain(event["reply"]).post(value + 1);
        return false;
    }

private:
    LLEventStream mPump;
    Sync& mSync;
};

/// The coroutines need LLApp::isRunning(), and on Windows LLCoros hands an
/// exception escaping one to LLApp::instance(). An app per test, as this had,
/// ran ~LLApp after each, and that ends LLCommon -- the APR pool, the LLTrace
/// master recorder -- for every test after it in the process. So there is one
/// app for the process, never destroyed, which each test sees start running
/// and stop. Making it changes the LLApp and LLTrace's master recorder on
/// purpose, so the state guard takes those as they are then.
class CoroTestApp: public LLTestApp
{
public:
    struct Running
    {
        Running()
        {
            app();
            setStatus(APP_STATUS_RUNNING);
        }
        ~Running() { setStopped(); }
    };

private:
    static CoroTestApp& app()
    {
        static CoroTestApp* sApp = nullptr;
        if (!sApp)
        {
            sApp = new CoroTestApp;
            ll_test::StateGuard::accept("the LLApp");
            ll_test::StateGuard::accept("the LLTrace master recorder");
        }
        return *sApp;
    }
};

} // anonymous namespace

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    struct lleventcoro_data
    {
        Sync mSync;
        ImmediateAPI immediateAPI{mSync};
        std::string replyName, errorName, threw, stringdata;
        LLSD result, errordata;
        int which;
        CoroTestApp::Running running;

        void explicit_wait(std::shared_ptr<LLCoros::Promise<std::string>>& cbp);
        void waitForEventOn1();
        void coroPump();
        void postAndWait1();
        void coroPumpPost();
    };

    void lleventcoro_data::explicit_wait(std::shared_ptr<LLCoros::Promise<std::string>>& cbp)
    {
        BEGIN
        {
            mSync.bump();
            // The point of this test is to verify / illustrate suspending a
            // coroutine for something other than an LLEventPump. In other
            // words, this shows how to adapt to any async operation that
            // provides a callback-style notification (and prove that it
            // works).

            // Perhaps we would send a request to a remote server and arrange
            // for cbp->set_value() to be called on response.
            // For test purposes, instead of handing 'callback' (or an
            // adapter) off to some I/O subsystem, we'll just pass it back to
            // our caller.
            cbp = std::make_shared<LLCoros::Promise<std::string>>();
            LLCoros::Future<std::string> future = LLCoros::getFuture(*cbp);

            // calling get() on the future causes us to suspend
            debug("about to suspend");
            stringdata = future.get();
            mSync.bump();
            // A CHECK: a REQUIRE's throw would leave this coroutine for
            // LLCoros to rethrow in whichever case came next.
            CHECK(stringdata == "received");
        }
        END
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: a coroutine suspended on a future wakes when its promise is set", "[coroutine]")
    {
        DEBUG;

        // Construct the coroutine instance that will run explicit_wait.
        std::shared_ptr<LLCoros::Promise<std::string>> respond;
        LLCoros::instance().launch("test<1>",
                                   [this, &respond](){ explicit_wait(respond); });
        mSync.bump();
        // When the coroutine waits for the future, it returns here.
        debug("about to respond");
        // Now we're the I/O subsystem delivering a result. This should make
        // the coroutine ready.
        respond->set_value("received");
        // but give it a chance to wake up
        mSync.yield();
        // ensure the coroutine ran and woke up again with the intended result
        CHECK(stringdata == "received");
    }

    void lleventcoro_data::waitForEventOn1()
    {
        BEGIN
        {
            mSync.bump();
            result = suspendUntilEventOn("source");
            mSync.bump();
        }
        END
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: suspendUntilEventOn() wakes on a post to the named pump", "[coroutine]")
    {
        DEBUG;
        LLCoros::instance().launch("test<2>", [this](){ waitForEventOn1(); });
        mSync.bump();
        debug("about to send");
        LLEventPumps::instance().obtain("source").post("received");
        // give waitForEventOn1() a chance to run
        mSync.yield();
        debug("back from send");
        CHECK(result.asString() == "received");
    }

    void lleventcoro_data::coroPump()
    {
        BEGIN
        {
            mSync.bump();
            LLCoroEventPump waiter;
            replyName = waiter.getName();
            result = waiter.suspend();
            mSync.bump();
        }
        END
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: LLCoroEventPump::suspend() wakes on a post to its pump", "[coroutine]")
    {
        DEBUG;
        LLCoros::instance().launch("test<3>", [this](){ coroPump(); });
        mSync.bump();
        debug("about to send");
        LLEventPumps::instance().obtain(replyName).post("received");
        // give coroPump() a chance to run
        mSync.yield();
        debug("back from send");
        CHECK(result.asString() == "received");
    }

    void lleventcoro_data::postAndWait1()
    {
        BEGIN
        {
            mSync.bump();
            result = postAndSuspend(LLSDMap("value", 17),       // request event
                                 immediateAPI.getPump(),     // requestPump
                                 "reply1",                   // replyPump
                                 "reply");                   // request["reply"] = name
            mSync.bump();
        }
        END
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: postAndSuspend() gets an immediate reply", "[coroutine]")
    {
        DEBUG;
        LLCoros::instance().launch("test<4>", [this](){ postAndWait1(); });
        CHECK(result.asInteger() == 18);
    }

    void lleventcoro_data::coroPumpPost()
    {
        BEGIN
        {
            mSync.bump();
            LLCoroEventPump waiter;
            result = waiter.postAndSuspend(LLSDMap("value", 17),
                                        immediateAPI.getPump(), "reply");
            mSync.bump();
        }
        END
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: LLCoroEventPump::postAndSuspend() gets an immediate reply", "[coroutine]")
    {
        DEBUG;
        LLCoros::instance().launch("test<5>", [this](){ coroPumpPost(); });
        CHECK(result.asInteger() == 18);
    }

    // A coroutine consumes what a PUMP holds for it, one value a wait.
    template <class PUMP>
    static void check_coro_consumes()
    {
        PUMP pump(typeid(PUMP).name());
        bool running{false};
        LLSD data{LLSD::emptyArray()};
        // start things off by posting once before even starting the listener
        // coro
        LL_DEBUGS() << "test() posting first" << LL_ENDL;
        LLSD first{LLSDMap("desc", "first")("value", 0)};
        bool consumed = pump.post(first);
        UNSCOPED_INFO("should not have consumed first");
        CHECK_FALSE(consumed);
        // now launch the coro
        LL_DEBUGS() << "test() launching listener coro" << LL_ENDL;
        running = true;
        LLCoros::instance().launch(
            "listener",
            [&pump, &running, &data](){
                // important for this test that we consume posted values
                LLCoros::instance().set_consuming(true);
                // should immediately retrieve 'first' without waiting
                LL_DEBUGS() << "listener coro waiting for first" << LL_ENDL;
                data.append(llcoro::suspendUntilEventOnWithTimeout(pump, 0.1f, LLSD()));
                // Don't check from within the coro -- a failed REQUIRE
                // throws, which won't propagate out to the case, which will
                // result in an odd failure.
                // Wait for 'second' because it's not already pending.
                LL_DEBUGS() << "listener coro waiting for second" << LL_ENDL;
                data.append(llcoro::suspendUntilEventOnWithTimeout(pump, 0.1f, LLSD()));
                // and wait for 'third', which should involve no further waiting
                LL_DEBUGS() << "listener coro waiting for third" << LL_ENDL;
                data.append(llcoro::suspendUntilEventOnWithTimeout(pump, 0.1f, LLSD()));
                LL_DEBUGS() << "listener coro done" << LL_ENDL;
                running = false;
            });
        // back from coro at the point where it's waiting for 'second'
        LL_DEBUGS() << "test() posting second" << LL_ENDL;
        LLSD second{llsd::map("desc", "second", "value", 1)};
        consumed = pump.post(second);
        UNSCOPED_INFO("should have consumed second");
        CHECK(consumed);
        // This is a key point: even though we've post()ed the value for which
        // the coroutine is waiting, it's actually still suspended until we
        // pause for some other reason. The coroutine will only pick up one
        // value at a time from our 'pump'. It's important to exercise the
        // case when we post() two values before it picks up either.
        LL_DEBUGS() << "test() posting third" << LL_ENDL;
        LLSD third{llsd::map("desc", "third", "value", 2)};
        consumed = pump.post(third);
        UNSCOPED_INFO("should NOT yet have consumed third");
        CHECK_FALSE(consumed);
        // now just wait for coro to finish -- which it eventually will, given
        // that all its suspend calls have short timeouts.
        while (running)
        {
            LL_DEBUGS() << "test() waiting for coro done" << LL_ENDL;
            llcoro::suspendUntilTimeout(0.1f);
        }
        // okay, verify expected results
        CHECK_THAT(data, ll_test::LLSDEquals(llsd::array(first, second, third)));
        LL_DEBUGS() << "test() done" << LL_ENDL;
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: LLEventMailDrop gives a coroutine one value a wait", "[coroutine]")
    {
        check_coro_consumes<LLEventMailDrop>();
    }

    TEST_CASE_METHOD(lleventcoro_data, "coroutine: LLEventLogProxyFor<LLEventMailDrop> gives a coroutine one value a wait", "[coroutine]")
    {
        check_coro_consumes< LLEventLogProxyFor<LLEventMailDrop> >();
    }
}
