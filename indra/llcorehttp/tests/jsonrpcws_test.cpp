/**
 * @file jsonrpcws_test.cpp
 * @brief unit tests for LLJSONRPCConnection helpers and dispatch behavior
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "lljsonrpcws.h"
#include "lltimer.h"

namespace
{
class TestJSONRPCConnection : public LLJSONRPCConnection
{
public:
    TestJSONRPCConnection()
        : LLJSONRPCConnection(LLWebsocketMgr::WSServer::ptr_t(), LLWebsocketMgr::connection_h())
    {
    }

    using LLJSONRPCConnection::processMessage;
    using LLJSONRPCConnection::validateMessage;
    using LLJSONRPCConnection::testInjectPendingRequest;
    using LLJSONRPCConnection::testPendingRequestCount;
    using LLJSONRPCConnection::testSweepTimeouts;
    using LLJSONRPCConnection::setAuthenticated;
};
}

namespace
{
    TEST_CASE("LLJSONRPCConnection Tests: makeEnvelope notification omits id", "[LLJSONRPCConnection_Tests]")
    {
        LLSD params;
        params["value"] = 42;
        LLSD env = LLJSONRPCConnection::makeEnvelope(LLSD(), "runtime.debug", params, LLSD(), LLSD());

        UNSCOPED_INFO("jsonrpc field should exist");
        REQUIRE(env.has("jsonrpc"));
        UNSCOPED_INFO("jsonrpc version");
        CHECK(env["jsonrpc"].asString() == "2.0");
        UNSCOPED_INFO("notification should omit id");
        REQUIRE_FALSE(env.has("id"));
        UNSCOPED_INFO("method");
        CHECK(env["method"].asString() == "runtime.debug");
        UNSCOPED_INFO("param round trip");
        CHECK(env["params"]["value"].asInteger() == 42);
    }

    TEST_CASE("LLJSONRPCConnection Tests: makeEnvelope response keeps id slot", "[LLJSONRPCConnection_Tests]")
    {
        LLSD env = LLJSONRPCConnection::makeEnvelope(LLSD(), std::string(), LLSD(), LLSD("ok"), LLSD());

        UNSCOPED_INFO("response should include id");
        REQUIRE(env.has("id"));
        UNSCOPED_INFO("response should not include method");
        REQUIRE_FALSE(env.has("method"));
        UNSCOPED_INFO("result");
        CHECK(env["result"].asString() == "ok");
    }

    TEST_CASE("LLJSONRPCConnection Tests: validateMessage accepts valid request and rejects invalid params", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;

        LLSD valid;
        valid["jsonrpc"] = "2.0";
        valid["method"] = "session.ping";
        LLSD params = LLSD::emptyMap();
        params["timestamp"] = 123;
        valid["params"] = params;
        UNSCOPED_INFO("valid request should pass");
        CHECK(conn.validateMessage(valid, true));

        LLSD invalid = valid;
        invalid["params"] = "not-an-array-or-object";
        UNSCOPED_INFO("invalid params type should fail");
        CHECK_FALSE(conn.validateMessage(invalid, true));
    }

    TEST_CASE("LLJSONRPCConnection Tests: processMessage dispatches a notification to its handler", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;

        bool called = false;
        LLSD seen_id;
        LLSD seen_params;
        conn.registerMethod("runtime.debug",
            [&](const std::string& method, const LLSD& id, const LLSD& params) -> LLSD
            {
                called = true;
                UNSCOPED_INFO("method propagated");
                REQUIRE(method == "runtime.debug");
                seen_id = id;
                seen_params = params;
                return LLSD();
            });

        LLSD params;
        params["message"] = "hello";
        LLSD notification = LLJSONRPCConnection::makeEnvelope(LLSD(), "runtime.debug", params, LLSD(), LLSD());
        conn.processMessage(notification);

        UNSCOPED_INFO("handler should be called");
        CHECK(called);
        UNSCOPED_INFO("notification id should be undefined");
        REQUIRE(seen_id.isUndefined());
        UNSCOPED_INFO("payload should be forwarded");
        CHECK(seen_params["message"].asString() == "hello");
    }

    TEST_CASE("LLJSONRPCConnection Tests: sweepTimeouts expires overdue callbacks", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;

        bool callback_called = false;
        conn.testInjectPendingRequest(
            "req_1",
            LLTimer::getTotalSeconds() - 1.0,
            [&](const LLSD& result, const LLSD& error)
            {
                callback_called = true;
                UNSCOPED_INFO("timed out result should be undefined");
                REQUIRE(result.isUndefined());
                UNSCOPED_INFO("timeout code");
                REQUIRE(error["code"].asInteger() ==
                    LLJSONRPCConnection::RPCError::REQUEST_TIMEOUT);
            });

        UNSCOPED_INFO("pending request should be tracked");
        CHECK(conn.testPendingRequestCount() == (size_t)1);
        conn.testSweepTimeouts();
        UNSCOPED_INFO("timeout callback should be called");
        CHECK(callback_called);
        UNSCOPED_INFO("pending request should be removed");
        CHECK(conn.testPendingRequestCount() == (size_t)0);
    }

    TEST_CASE("LLJSONRPCConnection Tests: an unauthenticated connection dispatches nothing until authenticated", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;
        UNSCOPED_INFO("authenticated unless it says otherwise");
        CHECK(conn.isAuthenticated());

        int calls = 0;
        conn.registerMethod("object.list",
            [&](const std::string&, const LLSD&, const LLSD&) -> LLSD
            {
                ++calls;
                return LLSD();
            });

        conn.setAuthenticated(false);
        UNSCOPED_INFO("not authenticated");
        CHECK_FALSE(conn.isAuthenticated());
        const LLSD request = LLJSONRPCConnection::makeEnvelope(LLSD("req_1"), "object.list", LLSD::emptyMap(), LLSD(), LLSD());
        conn.processMessage(request);
        const LLSD notification = LLJSONRPCConnection::makeEnvelope(LLSD(), "object.list", LLSD::emptyMap(), LLSD(), LLSD());
        conn.processMessage(notification);
        UNSCOPED_INFO("neither a request nor a notification reaches the handler");
        CHECK(calls == 0);

        conn.setAuthenticated(true);
        conn.processMessage(request);
        UNSCOPED_INFO("dispatched once authenticated");
        CHECK(calls == 1);
    }

    TEST_CASE("LLJSONRPCConnection Tests: an unauthenticated connection still takes answers to its own calls", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;
        conn.setAuthenticated(false);

        LLSD answer;
        conn.testInjectPendingRequest(
            "rpc_1",
            LLTimer::getTotalSeconds() + 60.0,
            [&](const LLSD& result, const LLSD& error)
            {
                UNSCOPED_INFO("no error");
                REQUIRE(error.isUndefined());
                answer = result;
            });

        LLSD result;
        result["challenge_response"] = "secret";
        conn.processMessage(LLJSONRPCConnection::makeEnvelope(LLSD("rpc_1"), std::string(), LLSD(), result, LLSD()));
        UNSCOPED_INFO("the answer reached the caller");
        CHECK(answer["challenge_response"].asString() == std::string("secret"));
        UNSCOPED_INFO("and it is answered");
        CHECK(conn.testPendingRequestCount() == 0);
    }

    TEST_CASE("LLJSONRPCConnection Tests: a closed connection tells its waiting calls and refuses new ones", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;
        LLSD told;
        conn.testInjectPendingRequest(
            "rpc_1",
            LLTimer::getTotalSeconds() + 60.0,
            [&](const LLSD&, const LLSD& error) { told = error; });

        conn.onClose();
        UNSCOPED_INFO("closed");
        CHECK(conn.isClosed());
        UNSCOPED_INFO("the waiting call told it closed");
        CHECK(told["code"].asInteger() == LLJSONRPCConnection::RPCError::CONNECTION_CLOSED);

        bool called = false;
        const LLSD id = conn.call("session.handshake", LLSD(), [&](const LLSD&, const LLSD&) { called = true; }, 30.0);
        UNSCOPED_INFO("not sent");
        REQUIRE(id.isUndefined());
        UNSCOPED_INFO("and nothing waiting");
        CHECK(conn.testPendingRequestCount() == (size_t)0);
        conn.testSweepTimeouts();
        UNSCOPED_INFO("so nothing is ever told");
        CHECK_FALSE(called);
    }
}
