/**
 * @file   lllogin_test.cpp
 * @author Mark Palange
 * @date   2009-02-26
 * @brief  Tests of lllogin.cpp.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2009-2010, Linden Research, Inc.
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

#if LL_WINDOWS
#pragma warning (disable : 4355) // 'this' used in initializer list: yes, intentionally
#endif

// Precompiled header
#include "linden_common.h"
// associated header
#include "../lllogin.h"
// STL headers
// std headers
#include <chrono>
#include <iostream>
// external library headers
// other Linden headers
#include "../../../test/debug.h"
#include "../../../test/lltestapp.h"
#include "alteststateguard.h"
#include "altestteardown.h"
#include <catch2/catch_test_macros.hpp>
#include "llevents.h"
#include "lleventcoro.h"
#include "llsd.h"
#include "stringize.h"

/*****************************************************************************
*   Helper classes
*****************************************************************************/
namespace
{
// This is a listener to receive results from lllogin.
class LoginListener: public LLEventTrackable
{
    std::string mName;
    LLSD mLastEvent;
    size_t mCalls{ 0 };
    Debug mDebug;
public:
    LoginListener(const std::string& name) :
        mName(name),
        mDebug(stringize(*this))
    {}

    bool call(const LLSD& event)
    {
        mDebug(STRINGIZE("LoginListener called!: " << event));

        mLastEvent = event;
        ++mCalls;
        return false;
    }

    LLBoundListener listenTo(LLEventPump& pump)
    {
        return pump.listen(mName, boost::bind(&LoginListener::call, this, _1));
    }

    LLSD lastEvent() const { return mLastEvent; }

    size_t getCalls() const { return mCalls; }

    // wait for arbitrary predicate to become true
    template <typename PRED>
    LLSD waitFor(const std::string& desc, PRED&& pred, double seconds=2.0) const
    {
        // remember when we started waiting
        auto start = std::chrono::steady_clock::now();
        // Break loop when the passed predicate returns true
        while (! std::forward<PRED>(pred)())
        {
            // but if we've been spinning here too long, test failed
            // how long have we been here, anyway?
            auto now = std::chrono::steady_clock::now();
            // the default ratio for duration is seconds
            std::chrono::duration<double> elapsed = (now - start);
            if (elapsed.count() > seconds)
            {
                FAIL("LoginListener::waitFor() took more than "
                     << seconds << " seconds waiting for " << desc);
            }
            // haven't yet received the new call, nor have we timed out --
            // just wait
            llcoro::suspend();
        }
        // oh good, we've gotten at least one new call! Return its event.
        return lastEvent();
    }

    // wait for any call() calls beyond prevcalls
    LLSD waitFor(size_t prevcalls, double seconds) const
    {
        return waitFor(STRINGIZE("more than " << prevcalls << " calls"),
                       [this, prevcalls]()->bool{ return getCalls() > prevcalls; },
                       seconds);
    }

    friend std::ostream& operator<<(std::ostream& out, const LoginListener& listener)
    {
        return out << "LoginListener(" << listener.mName << ')';
    }
};

// Answers on the XMLRPC transaction pump in place of the viewer's XMLRPC code.
class FakeXMLRPCListener: public LLEventTrackable
{
    std::string mName;
    LLSD mEvent;
    bool mImmediateResponse;
    LLSD mResponse;
    size_t mRequests{ 0 };
    Debug mDebug;

public:
    FakeXMLRPCListener(const std::string& name,
                       bool i = false,
                       const LLSD& response = LLSD()
                       ) :
        mName(name),
        mImmediateResponse(i),
        mResponse(response),
        mDebug(stringize(*this))
    {
        if(mResponse.isUndefined())
        {
            mResponse["status"] = "Complete"; // StatusComplete
            mResponse["errorcode"] = 0;
            mResponse["error"] = "dummy response";
            mResponse["transfer_rate"] = 0;
            mResponse["responses"]["login"] = true;
        }
    }

    void setResponse(const LLSD& r)
    {
        mResponse = r;
    }

    // How many requests have come, and the last of them.
    size_t requests() const { return mRequests; }
    LLSD lastRequest() const { return mEvent; }

    bool handle_event(const LLSD& event)
    {
        mDebug(STRINGIZE("FakeXMLRPCListener called!: " << event));
        mEvent = event;
        ++mRequests;
        if(mImmediateResponse)
        {
            sendReply();
        }
        return false;
    }

    void sendReply()
    {
        LLEventPumps::instance().obtain(mEvent["reply"]).post(mResponse);
    }

    LLBoundListener listenTo(LLEventPump& pump)
    {
        return pump.listen(mName, boost::bind(&FakeXMLRPCListener::handle_event, this, _1));
    }

    friend std::ostream& operator<<(std::ostream& out, const FakeXMLRPCListener& listener)
    {
        return out << "FakeXMLRPCListener(" << listener.mName << ')';
    }
};
}

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    // One LLTestApp for the process. An LLApp sets LLCommon up as it is made
    // and tears it down as it ends, APR and all, so one a case made and ended
    // would leave the cases after it without APR, and LLTrace without a
    // master recorder. This one ends with the harness's teardown.
    void makeTestApp()
    {
        static LLTestApp* sApp = nullptr;
        if (!sApp)
        {
            sApp = new LLTestApp;
            ll_test::Teardown::add([] { delete sApp; });
            // What making it changes on purpose: the LLApp, and the master
            // recorder LLCommon puts in place of the harness's.
            ll_test::StateGuard::accept("the LLApp");
            ll_test::StateGuard::accept("the LLTrace master recorder");
        }
    }

    struct llviewerlogin_data
    {
        llviewerlogin_data() :
            pumps(LLEventPumps::instance())
        {
            makeTestApp();
        }
        ~llviewerlogin_data()
        {
            pumps.clear();
        }
        LLEventPumps& pumps;
    };


    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: an immediate XMLRPC response still reaches the online state", "[LLViewerLogin]")
    {
        DEBUG;
        // Testing login with an immediate response from XMLPRC
        // The response will come before the post request exits.
        // This tests an edge case of the login state handling.
        LLEventStream xmlrpcPump("LLXMLRPCTransaction"); // Dummy XMLRPC pump

        bool respond_immediately = true;

        // Have dummy XMLRPC respond immediately.
        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc", respond_immediately);
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;

        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        LLSD credentials;
        credentials["first"] = "foo";
        credentials["last"] = "bar";
        credentials["passwd"] = "secret";

        login.connect("login.bar.com", credentials);
        listener.waitFor(
            "online state",
            [&listener]()->bool{ return listener.lastEvent()["state"].asString() == "online"; });
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: a complete response refusing the login goes offline", "[LLViewerLogin]")
    {
        DEBUG;
        // Test completed response, that fails to login.

        // Testing normal login procedure.
        LLEventStream xmlrpcPump("LLXMLRPCTransaction"); // Dummy XMLRPC pump

        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        LLSD credentials;
        credentials["first"] = "who";
        credentials["last"] = "what";
        credentials["passwd"] = "badpasswd";

        login.connect("login.bar.com", credentials);
        llcoro::suspend();

        CHECK(listener.lastEvent()["change"].asString() == "authenticating");

        auto prev = listener.getCalls();

        // Send the failed auth request reponse
        LLSD data;
        data["status"] = "Complete";
        data["errorcode"] = 0;
        data["error"] = "dummy response";
        data["transfer_rate"] = 0;
        data["responses"]["login"] = "false";
        dummyXMLRPC.setResponse(data);
        dummyXMLRPC.sendReply();
        // we happen to know LLLogin uses a 10-second timeout to try to sync
        // with SLVersionChecker -- allow at least that much time before
        // giving up
        listener.waitFor(prev, 11.0);

        CHECK(listener.lastEvent()["state"].asString() == "offline");
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: an incomplete response ends the attempt offline", "[LLViewerLogin]")
    {
        DEBUG;
        // Test incomplete response, that end the attempt.

        // Testing normal login procedure.
        LLEventStream xmlrpcPump("LLXMLRPCTransaction"); // Dummy XMLRPC pump

        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        LLSD credentials;
        credentials["first"] = "these";
        credentials["last"] = "don't";
        credentials["passwd"] = "matter";

        login.connect("login.bar.com", credentials);
        llcoro::suspend();

        CHECK(listener.lastEvent()["change"].asString() == "authenticating");

        auto prev = listener.getCalls();

        // Send the failed auth request reponse
        LLSD data;
        data["status"] = "OtherError";
        data["errorcode"] = 0;
        data["error"] = "dummy response";
        data["transfer_rate"] = 0;
        dummyXMLRPC.setResponse(data);
        dummyXMLRPC.sendReply();
        // we happen to know LLLogin uses a 10-second timeout to try to sync
        // with SLVersionChecker -- allow at least that much time before
        // giving up
        listener.waitFor(prev, 11.0);

        CHECK(listener.lastEvent()["state"].asString() == "offline");
    }

    // A login's answer that sends it on to another URI.
    LLSD redirectTo(const std::string& uri)
    {
        LLSD data;
        data["status"] = "Complete";
        data["errorcode"] = 0;
        data["error"] = "dummy response";
        data["transfer_rate"] = 0;
        data["responses"]["login"] = "indeterminate";
        data["responses"]["next_url"] = uri;
        data["responses"]["next_method"] = "login_to_simulator";
        return data;
    }

    // Starts a login at from_uri, answers it with a redirect to to_uri, and
    // gives what the login does next: the event that ends it, or that of its
    // next attempt, after which listener and the requests can be read.
    LLSD afterRedirect(const std::string& from_uri, const std::string& to_uri,
                       LLLogin& login, LoginListener& listener, FakeXMLRPCListener& xmlrpc)
    {
        LLSD credentials;
        credentials["first"] = "who";
        credentials["last"] = "what";
        credentials["passwd"] = "secret";

        login.connect(from_uri, credentials);
        llcoro::suspend();
        REQUIRE(xmlrpc.requests() == 1);

        xmlrpc.setResponse(redirectTo(to_uri));
        xmlrpc.sendReply();
        return listener.waitFor(
            "the login to end or try again",
            [&listener]()->bool
            {
                const LLSD event(listener.lastEvent());
                return event["change"].asString() == "fail.login"
                    || event["data"]["attempt"].asInteger() == 2;
            });
    }

    // Answers a login's attempt with a refusal, so that its coroutine ends.
    void endLogin(LoginListener& listener, FakeXMLRPCListener& xmlrpc)
    {
        LLSD data;
        data["status"] = "Complete";
        data["errorcode"] = 0;
        data["error"] = "dummy response";
        data["transfer_rate"] = 0;
        data["responses"]["login"] = "false";
        xmlrpc.setResponse(data);
        xmlrpc.sendReply();
        listener.waitFor(
            "the login to end",
            [&listener]()->bool{ return listener.lastEvent()["change"].asString() == "fail.login"; });
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: a secure login isn't sent on to a plain URI", "[LLViewerLogin]")
    {
        LLEventStream xmlrpcPump("LLXMLRPCTransaction");
        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        const LLSD event(afterRedirect("https://login.bar.com/", "http://login.bar.com/",
                                       login, listener, dummyXMLRPC));

        CHECK(event["state"].asString() == "offline");
        CHECK(event["change"].asString() == "fail.login");
        CHECK(event["data"]["reason"].asString() == "InsecureRedirect");
        CHECK(event["data"]["message_id"].asString() == "LoginFailedInsecureRedirect");
        CHECK(dummyXMLRPC.requests() == 1);
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: a secure login isn't sent on to a URI with no scheme", "[LLViewerLogin]")
    {
        LLEventStream xmlrpcPump("LLXMLRPCTransaction");
        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        // The scheme's case doesn't make the login any less secure.
        const LLSD event(afterRedirect("HTTPS://login.bar.com/", "login.bar.com",
                                       login, listener, dummyXMLRPC));

        CHECK(event["change"].asString() == "fail.login");
        CHECK(dummyXMLRPC.requests() == 1);
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: a secure login is sent on to another secure URI", "[LLViewerLogin]")
    {
        LLEventStream xmlrpcPump("LLXMLRPCTransaction");
        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        const LLSD event(afterRedirect("https://login.bar.com/", "HTTPS://login2.bar.com/",
                                       login, listener, dummyXMLRPC));

        CHECK(event["change"].asString() == "authenticating");
        CHECK(dummyXMLRPC.requests() == 2);
        CHECK(dummyXMLRPC.lastRequest()["uri"].asString() == "HTTPS://login2.bar.com/");
        CHECK(dummyXMLRPC.lastRequest()["method"].asString() == "login_to_simulator");

        endLogin(listener, dummyXMLRPC);
    }

    TEST_CASE_METHOD(llviewerlogin_data, "LLViewerLogin: a plain login is sent on to a plain URI", "[LLViewerLogin]")
    {
        LLEventStream xmlrpcPump("LLXMLRPCTransaction");
        FakeXMLRPCListener dummyXMLRPC("dummy_xmlrpc");
        LLTempBoundListener conn1 = dummyXMLRPC.listenTo(xmlrpcPump);

        LLLogin login;
        LoginListener listener("test_ear");
        LLTempBoundListener conn2 = listener.listenTo(login.getEventPump());

        const LLSD event(afterRedirect("http://localhost:8002/", "http://localhost:8003/",
                                       login, listener, dummyXMLRPC));

        CHECK(event["change"].asString() == "authenticating");
        CHECK(dummyXMLRPC.requests() == 2);
        CHECK(dummyXMLRPC.lastRequest()["uri"].asString() == "http://localhost:8003/");

        endLogin(listener, dummyXMLRPC);
    }
}
