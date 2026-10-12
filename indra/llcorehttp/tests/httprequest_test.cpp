/**
 * @file httprequest_test.cpp
 * @brief unit tests for the LLCore::HttpRequest class
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

#include <catch2/catch_test_macros.hpp>

#include "httprequest.h"
#include "bufferarray.h"
#include "httphandler.h"
#include "httpheaders.h"
#include "httpresponse.h"
#include "httpoptions.h"
#include "_httpservice.h"
#include "_httprequestqueue.h"

#include <curl/curl.h>
#include "alregex.h"
#include <chrono>
#include <map>
#include <sstream>
#include <vector>

#include "llcleanup.h"
#include "llproxy.h"

#include <iostream>


using namespace LLCore;
using namespace LLCoreInt;

// spin/sleep waiting times for client/server exchange tests
//
// These are now fairly generous to try to get around timeout
// ('reasonable time') failures during execution on a heavily-
// loaded system where the unit test is in competition with
// other programs.
static const int LOOP_SLEEP_INTERVAL(10000);
static const int LOOP_COUNT_SHORT(500);         // 5-second dwell time
static const int LOOP_COUNT_LONG(3000);         // 30-second dwell time

namespace
{
#if defined(WIN32)

void usleep(unsigned long usec);

#endif

// Initialization and cleanup for libcurl: curl's global state and the
// LLProxy singleton, for the length of a test.
void init_curl()
{
    curl_global_init(CURL_GLOBAL_ALL);

    LLProxy::getInstance();
}


void term_curl()
{
    SUBSYSTEM_CLEANUP(LLProxy);
}


class ScopedCurlInit
{
public:
    ScopedCurlInit()
        {
            init_curl();
        }

    ~ScopedCurlInit()
        {
            term_curl();
        }
};


// The peer's address, which test_llcorehttp_peer.py, the program this test
// runs under, puts in LL_TEST_PORT.
std::string get_base_url()
{
    const char * env(getenv("LL_TEST_PORT"));

    if (! env)
    {
        std::cerr << "LL_TEST_PORT environment variable missing." << std::endl;
        std::cerr << "Test expects to run in test_llcorehttp_peer.py script." << std::endl;
        INFO("LL_TEST_PORT set in environment");
        REQUIRE(env != NULL);
    }

    int port(atoi(env));
    std::ostringstream out;
    out << "http://localhost:" << port << "/";
    return out.str();
}


void stop_thread(LLCore::HttpRequest * req)
{
    if (req)
    {
        req->requestStopThread(LLCore::HttpHandler::ptr_t());

        int count = 0;
        int limit = 10;
        while (count++ < limit && ! HttpService::isStopped())
        {
            req->update(1000);
            usleep(100000);
        }
    }
}

}

namespace
{

typedef std::vector<std::pair<ALRegex, ALRegex> > regex_container_t;

struct HttpRequestTestData
{
    // the test objects inherit from this so the member functions and variables
    // can be referenced directly inside of the test functions.
    int             mHandlerCalls;
    HttpStatus      mStatus;
};

class TestHandler2 : public LLCore::HttpHandler
{
public:
    TestHandler2(HttpRequestTestData * state,
                 const std::string & name)
        : mState(state),
          mName(name),
          mExpectHandle(LLCORE_HTTP_HANDLE_INVALID)
        {}

    virtual void onCompleted(HttpHandle handle, HttpResponse * response)
        {
            if (LLCORE_HTTP_HANDLE_INVALID != mExpectHandle)
            {
                REQUIRE(mExpectHandle == handle);
            }
            REQUIRE(response != NULL);
            if (response && mState)
            {
                const HttpStatus actual_status(response->getStatus());
                INFO("wanted status " << mState->mStatus.toHex() << ", received " << actual_status.toHex());
                REQUIRE(actual_status == mState->mStatus);
            }
            if (mState)
            {
                mState->mHandlerCalls++;
            }
            if (! mHeadersRequired.empty() || ! mHeadersDisallowed.empty())
            {
                REQUIRE(response != NULL);
                HttpHeaders::ptr_t header(response->getHeaders());  // Will not hold onto this
                REQUIRE(header != NULL);

                if (! mHeadersRequired.empty())
                {
                    for (int i(0); i < mHeadersRequired.size(); ++i)
                    {
                        bool found = false;
                        for (HttpHeaders::const_iterator iter(header->begin());
                             header->end() != iter;
                             ++iter)
                        {
                            // std::cerr << "Header: " << (*iter).first
                            //        << ": " << (*iter).second << std::endl;

                            if (mHeadersRequired[i].first.match((*iter).first) &&
                                mHeadersRequired[i].second.match((*iter).second))
                            {
                                found = true;
                                break;
                            }
                        }
                        INFO("required header " << mHeadersRequired[i].first << "=" << mHeadersRequired[i].second);
                        REQUIRE(found);
                    }
                }

                if (! mHeadersDisallowed.empty())
                {
                    for (int i(0); i < mHeadersDisallowed.size(); ++i)
                    {
                        for (HttpHeaders::const_iterator iter(header->begin());
                             header->end() != iter;
                             ++iter)
                        {
                            if (mHeadersDisallowed[i].first.match((*iter).first) &&
                                mHeadersDisallowed[i].second.match((*iter).second))
                            {
                                FAIL("disallowed header " << mHeadersDisallowed[i].first << "="
                                     << mHeadersDisallowed[i].second << " found in response");
                            }
                        }
                    }
                }
            }

            if (! mCheckContentType.empty())
            {
                REQUIRE(response != NULL);
                std::string con_type(response->getContentType());
                REQUIRE(mCheckContentType == con_type);
            }

            // std::cout << "TestHandler2::onCompleted() invoked" << std::endl;
        }

    HttpRequestTestData * mState;
    std::string mName;
    HttpHandle mExpectHandle;
    std::string mCheckContentType;
    regex_container_t mHeadersRequired;
    regex_container_t mHeadersDisallowed;
};


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: an HttpRequest is constructed and deleted with the service up", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // release the request object
        delete req;
        req = NULL;

        HttpRequest::destroyService();
    }
    catch (...)
    {
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a NoOp queued with no thread running leaves two queue references", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a NoOp
        HttpHandle handle = req->requestNoOp(LLCore::HttpHandler::ptr_t());
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // release the request object
        delete req;
        req = NULL;

        // Request queue should have two references:  global singleton & service object
        CHECK(HttpRequestQueue::instanceOf()->getRefCount() == 2);

        // Okay, tear it down
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

    void NoOpDeletor(LLCore::HttpHandler *) { }

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a NoOp is answered and a stop request stops the worker thread", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a NoOp
        HttpHandle handle = req->requestNoOp(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: two HttpRequest instances are served by one worker thread", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    TestHandler2 handler1(this, "handler1");
    TestHandler2 handler2(this, "handler2");

    LLCore::HttpHandler::ptr_t handler1p(&handler1, NoOpDeletor);
    LLCore::HttpHandler::ptr_t handler2p(&handler2, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req1 = NULL;
    HttpRequest * req2 = NULL;

    try
    {

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req1 = new HttpRequest();
        req2 = new HttpRequest();

        // Issue some NoOps
        HttpHandle handle = req1->requestNoOp(handler1p);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        handler1.mExpectHandle = handle;

        handle = req2->requestNoOp(handler2p);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        handler2.mExpectHandle = handle;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 2)
        {
            req1->update(1000000);
            req2->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // Okay, request a shutdown of the servicing thread
        handle = req2->requestStopThread(handler2p);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        handler2.mExpectHandle = handle;

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 3)
        {
            req1->update(1000000);
            req2->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 3);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req1;
        req1 = NULL;
        delete req2;
        req2 = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 3);
    }
    catch (...)
    {
        stop_thread(req1);
        delete req1;
        delete req2;
        HttpRequest::destroyService();
        throw;
    }
}

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a NoOp is answered behind a soft spin before hard termination", "[HttpRequest_Tests]")
{
#ifndef LL_WINDOWS
    SKIP("Skip due to issues with testing pthread cancellation");
#endif

    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a Spin
        HttpHandle handle = req->requestSpin(1);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Issue a NoOp
        handle = req->requestNoOp(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(mHandlerCalls == 1);

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a hard spin keeps a NoOp unanswered until hard termination", "[HttpRequest_Tests]")
{
#ifndef LL_WINDOWS
    SKIP("Skip due to issues with testing pthread cancellation");
#endif
    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a Spin
        HttpHandle handle = req->requestSpin(0);        // Hard spin
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Issue a NoOp
        handle = req->requestNoOp(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(mHandlerCalls == 0);

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET to a dead port fails with CURLE_COULDNT_CONNECT", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");

    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setRetries(1);            // Don't try for too long - default retries take about 18S

        // Issue a GET that can't connect
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_COULDNT_CONNECT);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     "http://127.0.0.1:2/nothing/here",
                                                     0,
                                                     0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET to the peer is answered 200", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET with a byte range sends its Range header and takes a 200 for the whole body", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setWantHeaders(true);

        // Issue a GET for 25 bytes from offset 100.  The peer reflects the
        // Range header it was sent and, ignoring it, answers 200 with the
        // whole body, which has no Content-Range for llcorehttp to check.
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex("bytes=100-124", ALRegex::ICASE)));
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base + "reflect/",
                                                     100,
                                                     25,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a PUT with a body to the peer is answered 200", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    BufferArray * body = new BufferArray;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a PUT that *can* connect
        static const char * body_text("Now is the time for all good men...");
        body->append(body_text, strlen(body_text));
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            body,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // Lose the request body
        body->release();
        body = NULL;

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        if (body)
        {
            body->release();
        }
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a POST with a body to the peer is answered 200", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    BufferArray * body = new BufferArray;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a POST that *can* connect
        static const char * body_text("Now is the time for all good men...");
        body->append(body_text, strlen(body_text));
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base,
                                             body,
                                             HttpOptions::ptr_t(),
                                             HttpHeaders::ptr_t(),
                                             handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // Lose the request body
        body->release();
        body = NULL;

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        if (body)
        {
            body->release();
        }
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET with tracing on is answered 200", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Enable tracing
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::DEFAULT_POLICY_ID, 2, NULL);

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                     0,
                                                     HttpOptions::ptr_t(),
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET that wants headers gets the peer's X-LL-Special header", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    handler.mHeadersRequired.reserve(20);               // Avoid memory leak test failure
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Enable tracing
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::DEFAULT_POLICY_ID, 2, NULL);

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setWantHeaders(true);

        // Issue a GET that succeeds
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(ALRegex("X-LL-Special", ALRegex::ICASE),
                                          ALRegex(".*", ALRegex::ICASE)));
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                 0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // release options
        opts.reset();

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET to a peer that sleeps times out after two seconds", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    std::string url_base(get_base_url() + "/sleep/");   // path to a 30-second sleep
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions);
        opts->setRetries(0);            // Don't retry
        opts->setTimeout(2);

        // Issue a GET that sleeps
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_OPERATION_TIMEDOUT);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                     0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// Test retrieval of Content-Type/Content-Encoding headers
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET reports the response's Content-Type", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);

    // Load and clear the string setting to preload std::string object
    // for memory return tests.
    handler.mCheckContentType = "application/llsd+xml";
    handler.mCheckContentType.clear();
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        handler.mCheckContentType = "application/llsd+xml";
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mCheckContentType.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        CHECK(mHandlerCalls == 2);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on GET requests
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a GET sends default headers and a texture fetch adds Range and Accept", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            options,
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Do a texture-style fetch
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("Accept", "image/x-j2c");

        mStatus = HttpStatus(200);
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("image/x-j2c", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("\\W*X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                          url_base + "reflect/",
                                          0,
                                          47,
                                          options,
                                          headers,
                                          handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 3)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 3);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on POST requests
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a POST sends default headers and a form-urlencoded Content-Type", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // And a buffer array
        const char * msg("It was the best of times, it was the worst of times.");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default POST
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-length", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("application/x-www-form-urlencoded", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-expect", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*chunked.*", ALRegex::ICASE)));
        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base + "reflect/",
                                             ba,
                                             options,
                                             HttpHeaders::ptr_t(),
                                             handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on PUT requests
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: a PUT sends default headers with a length and no Content-Type", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // And a buffer array
        const char * msg("It was the best of times, it was the worst of times.");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default PUT
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-length", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-expect", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*chunked.*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));

        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            ba,
                                            options,
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on GET requests with overrides
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: headers given to a GET replace its defaults", "[HttpRequest_Tests]")
{
    // It appears that HttpRequest is fully capable of sending duplicate header values in violation of
    // this test's expectations. Something needs to budge: is sending duplicate header values desired?
    //
    // Test server /reflect/ response headers (mirrored from request)
    //
    // X-Reflect-content-type: text/plain
    // X-Reflect-content-type: text/html
    // X-Reflect-content-type: application/llsd+xml
    //
    SKIP("FIXME: Bad assertions or broken functionality.");

    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("Keep-Alive", "120");
        headers->append("Accept-encoding", "deflate");
        headers->append("Accept", "text/plain");

        // Issue a GET with modified headers
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("text/plain", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("deflate", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("120", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("300", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            options,
                                            headers,
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on POST requests with overrides
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: headers given to a POST replace its defaults", "[HttpRequest_Tests]")
{
    // It appears that HttpRequest is fully capable of sending duplicate header values in violation of
    // this test's expectations. Something needs to budge: is sending duplicate header values desired?
    //
    // Test server /reflect/ response headers (mirrored from request)
    //
    // X-Reflect-content-type: text/plain
    // X-Reflect-content-type: text/html
    // X-Reflect-content-type: application/llsd+xml
    //
    SKIP("FIXME: Bad assertions or broken functionality.");

    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders());
        headers->append("keep-Alive", "120");
        headers->append("Accept", "text/html");
        headers->append("content-type", "application/llsd+xml");
        headers->append("cache-control", "no-store");

        // And a buffer array
        const char * msg("<xml><llsd><string>It was the best of times, it was the worst of times.</string></llsd></xml>");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a POST with modified headers
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("text/html", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("120", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-length", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("application/llsd\\+xml", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex("no-store", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("application/x-www-form-urlencoded", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("300", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-expect", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));

        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base + "reflect/",
                                             ba,
                                             options,
                                             headers,
                                             handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on PUT requests with overrides
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: the last of several Content-Types given to a PUT is the one sent", "[HttpRequest_Tests]")
{
    // It appears that HttpRequest is fully capable of sending duplicate header values in violation of
    // this test's expectations. Something needs to budge: is sending duplicate header values desired?
    //
    // Test server /reflect/ response headers (mirrored from request)
    //
    // X-Reflect-content-type: text/plain
    // X-Reflect-content-type: text/html
    // X-Reflect-content-type: application/llsd+xml
    //
    SKIP("FIXME: Bad assertions or broken functionality.");

    ScopedCurlInit ready;

    // Warmup the regex engine to pre-alloc memory for memory size tests
    ALRegex warmup("askldjflasdj;f", ALRegex::ICASE);
    warmup.match("akl;sjflajfk;ajsk");

    std::string url_base(get_base_url());

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("content-type", "text/plain");
        headers->append("content-type", "text/html");
        headers->append("content-type", "application/llsd+xml");

        // And a buffer array
        const char * msg("<xml><llsd><string>It was the best of times, it was the worst of times.</string></llsd></xml>");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a PUT with several Content-Types
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-connection", ALRegex::ICASE),
                ALRegex("keep-alive", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept", ALRegex::ICASE),
                ALRegex("\\*/\\*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-accept-encoding", ALRegex::ICASE),
                ALRegex("((gzip|deflate),\\s*)+(gzip|deflate)", ALRegex::ICASE))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-keep-alive", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-host", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-length", ALRegex::ICASE),
                ALRegex("\\d+", ALRegex::ICASE)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("application/llsd\\+xml", ALRegex::ICASE)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-cache-control", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-pragma", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-range", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-referer", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-expect", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-transfer-encoding", ALRegex::ICASE),
                ALRegex(".*", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("text/plain", ALRegex::ICASE)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                ALRegex("X-Reflect-content-type", ALRegex::ICASE),
                ALRegex("text/html", ALRegex::ICASE)));
        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            ba,
                                            options,
                                            headers,
                                            handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// BUG-2295 Tests - Content-Range header received but no body
TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: BUG-2295 a 206 with a missing or short body gets the right status", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpOptions::ptr_t options;
    HttpRequest * req = NULL;

    try
    {
        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setRetries(1);         // Partial_File is retryable and can timeout in here
        options->setDNSCacheTimeout(30);

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // ======================================
        // Issue bug2295 GETs that will get a 206
        // ======================================
        mStatus = HttpStatus(206);
        static const int test_count(3);
        for (int i(0); i < test_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < test_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time - ms1");
        CHECK(count < limit);
        CHECK(mHandlerCalls == test_count);

        // ======================================
        // Issue bug2295 GETs that will get a libcurl 18 (PARTIAL_FILE)
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_PARTIAL_FILE);
        static const int test2_count(1);
        for (int i(0); i < test2_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/00000012/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test2_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time - ms2");
        CHECK(count < limit);
        CHECK(mHandlerCalls == test2_count);

        // ======================================
        // Issue bug2295 GETs that will get an llcorehttp HE_INV_CONTENT_RANGE_HDR status
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::LLCORE, HE_INV_CONTENT_RANGE_HDR);
        static const int test3_count(1);
        for (int i(0); i < test3_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/inv_cont_range/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test3_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time - ms3");
        CHECK(count < limit);
        CHECK(mHandlerCalls == test3_count);

        // ======================================
        // Okay, request a shutdown of the servicing thread
        // ======================================
        mStatus = HttpStatus();
        mHandlerCalls = 0;
        HttpHandle handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Shutdown request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options
        options.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// Records, by URL, when each response was delivered and the retries it reports
class RetryRecordingHandler : public TestHandler2
{
public:
    struct Delivery
    {
        std::chrono::steady_clock::time_point mAt;
        unsigned int mRetries;
        unsigned int m503Retries;
    };

    RetryRecordingHandler(HttpRequestTestData * state,
                          const std::string & name)
        : TestHandler2(state, name)
        {}

    void onCompleted(HttpHandle handle, HttpResponse * response) override
        {
            TestHandler2::onCompleted(handle, response);

            Delivery delivery{ std::chrono::steady_clock::now(), 0, 0 };
            response->getRetries(&delivery.mRetries, &delivery.m503Retries);
            mDeliveries[response->getRequestURL()] = delivery;
        }

    std::map<std::string, Delivery> mDeliveries;
};

TEST_CASE_METHOD(HttpRequestTestData, "HttpRequest Tests: GETs answered 503 are retried once and a good Retry-After holds the retry back", "[HttpRequest_Tests]")
{
    ScopedCurlInit ready;

    // Each of the peer's seven 503 answers, /503/0/ to /503/6/, carries
    // a different Retry-After, well- or mis-formed, and the code must not
    // fall over on any of them.  The parsing result can't be inspected,
    // but its effect can: only /503/0/'s "2" is a delay llcorehttp takes,
    // and it holds that retry back two seconds where the others retry
    // after the default backoff of one.

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    RetryRecordingHandler handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    std::string url_base(get_base_url() + "/503/"); // path to 503 generators
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setRetries(1);            // Retry once only
        opts->setUseRetryAfter(true);   // Try to parse the retry-after header

        // Issue a GET to each of the peer's 503 answers
        mStatus = HttpStatus(503);
        const int url_limit(7);
        std::vector<std::string> urls;
        const auto issued(std::chrono::steady_clock::now());
        for (int i(0); i < url_limit; ++i)
        {
            std::ostringstream url;
            url << url_base << i << "/";
            urls.push_back(url.str());
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url.str(),
                                                         0,
                                                         0,
                                                         opts,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);

            CAPTURE(i);
            REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);
        }


        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < url_limit)
        {
            req->update(0);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == url_limit);

        // Each GET was retried once, as a 503, whatever its Retry-After said,
        // and the retry of the one with "Retry-After: 2" came no sooner than that
        for (int i(0); i < url_limit; ++i)
        {
            CAPTURE(i);
            const auto delivery(handler.mDeliveries.find(urls[i]));
            REQUIRE(delivery != handler.mDeliveries.end());
            CHECK(delivery->second.mRetries == 1u);
            CHECK(delivery->second.m503Retries == 1u);
            if (0 == i)
            {
                CHECK(delivery->second.mAt - issued >= std::chrono::milliseconds(1900));
            }
        }

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        mHandlerCalls = 0;
        HttpHandle handle = req->requestStopThread(handlerp);
        REQUIRE(handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        UNSCOPED_INFO("Second request executed in reasonable time");
        CHECK(count < limit);
        CHECK(mHandlerCalls == 1);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        CHECK(HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


}  // anonymous namespace

namespace
{

#if defined(WIN32)

void usleep(unsigned long usec)
{
    Sleep((DWORD) (usec / 1000UL));
}

#endif

}
