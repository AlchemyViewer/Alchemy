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

        CHECK(env.has("jsonrpc"));
        CHECK(env["jsonrpc"].asString() == "2.0");
        CHECK_FALSE(env.has("id"));
        CHECK(env["method"].asString() == "runtime.debug");
        CHECK(env["params"]["value"].asInteger() == 42);
    }

    TEST_CASE("LLJSONRPCConnection Tests: makeEnvelope response keeps id slot", "[LLJSONRPCConnection_Tests]")
    {
        LLSD env = LLJSONRPCConnection::makeEnvelope(LLSD(), std::string(), LLSD(), LLSD("ok"), LLSD());

        CHECK(env.has("id"));
        CHECK_FALSE(env.has("method"));
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
        CHECK(conn.validateMessage(valid, true));

        LLSD invalid = valid;
        invalid["params"] = "not-an-array-or-object";
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
                REQUIRE(method == "runtime.debug");
                seen_id = id;
                seen_params = params;
                return LLSD();
            });

        LLSD params;
        params["message"] = "hello";
        LLSD notification = LLJSONRPCConnection::makeEnvelope(LLSD(), "runtime.debug", params, LLSD(), LLSD());
        conn.processMessage(notification);

        CHECK(called);
        CHECK(seen_id.isUndefined());
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
                REQUIRE(result.isUndefined());
                REQUIRE(error["code"].asInteger() ==
                    LLJSONRPCConnection::RPCError::REQUEST_TIMEOUT);
            });

        CHECK(conn.testPendingRequestCount() == (size_t)1);
        conn.testSweepTimeouts();
        CHECK(callback_called);
        CHECK(conn.testPendingRequestCount() == (size_t)0);
    }

    TEST_CASE("LLJSONRPCConnection Tests: an unauthenticated connection dispatches nothing until authenticated", "[LLJSONRPCConnection_Tests]")
    {
        TestJSONRPCConnection conn;
        UNSCOPED_INFO("authenticated by default");
        CHECK(conn.isAuthenticated());

        int calls = 0;
        conn.registerMethod("object.list",
            [&](const std::string&, const LLSD&, const LLSD&) -> LLSD
            {
                ++calls;
                return LLSD();
            });

        conn.setAuthenticated(false);
        CHECK_FALSE(conn.isAuthenticated());
        const LLSD request = LLJSONRPCConnection::makeEnvelope(LLSD("req_1"), "object.list", LLSD::emptyMap(), LLSD(), LLSD());
        conn.processMessage(request);
        const LLSD notification = LLJSONRPCConnection::makeEnvelope(LLSD(), "object.list", LLSD::emptyMap(), LLSD(), LLSD());
        conn.processMessage(notification);
        CHECK(calls == 0);

        conn.setAuthenticated(true);
        conn.processMessage(request);
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
                REQUIRE(error.isUndefined());
                answer = result;
            });

        LLSD result;
        result["challenge_response"] = "secret";
        conn.processMessage(LLJSONRPCConnection::makeEnvelope(LLSD("rpc_1"), std::string(), LLSD(), result, LLSD()));
        CHECK(answer["challenge_response"].asString() == std::string("secret"));
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
        CHECK(conn.isClosed());
        CHECK(told["code"].asInteger() == LLJSONRPCConnection::RPCError::CONNECTION_CLOSED);

        bool called = false;
        const LLSD id = conn.call("session.handshake", LLSD(), [&](const LLSD&, const LLSD&) { called = true; }, 30.0);
        UNSCOPED_INFO("the call was not sent");
        CHECK(id.isUndefined());
        CHECK(conn.testPendingRequestCount() == (size_t)0);
        conn.testSweepTimeouts();
        UNSCOPED_INFO("a refused call is never told");
        CHECK_FALSE(called);
    }
}
