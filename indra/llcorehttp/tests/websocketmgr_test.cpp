/**
 * @file websocketmgr_test.cpp
 * @brief A WebSocket server lets programs in and turns pages away.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "llwebsocketmgr.h"
#include "lljsonrpcws.h"
#include "llsdjson.h"
#include "workqueue.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace
{
    // A port nothing on this machine is listening on, as far as can be
    // told; zero where none turned up.
    U16 unusedPort()
    {
        std::random_device random;
        for (int tries = 0; tries < 20; ++tries)
        {
            const U16                      port = static_cast<U16>(40000 + random() % 20000);
            boost::asio::io_context        io;
            boost::asio::ip::tcp::acceptor acceptor(io);
            boost::system::error_code      ec;
            acceptor.open(boost::asio::ip::tcp::v4(), ec);
            if (!ec)
            {
                acceptor.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), ec);
            }
            if (!ec)
            {
                return port;
            }
        }
        return 0;
    }

    // A WebSocket client, turned a slice at a time so that whoever waits
    // on it can keep a main loop turning meanwhile. Every step is
    // asynchronous: a stream's deadline holds only for those, and a server
    // that never answered would hold a synchronous one for good.
    class TestClient
    {
        // One step's outcome, shared with its handler: a step that ran out
        // of time may still be answered, and must not answer into a frame
        // that has gone.
        struct Step
        {
            bool                      mDone = false;
            boost::system::error_code mError;
        };

        // Ahead of what uses it: a deduced return type is not known
        // before its definition, even within the class.
        static auto handler(const std::shared_ptr<Step>& step)
        {
            return [step](const boost::system::error_code& error, std::size_t = 0)
            {
                step->mDone  = true;
                step->mError = error;
            };
        }

    public:
        // What a read brought: a message's text, or the close that ended
        // the connection, with its code and reason. Neither where nothing
        // came within the time.
        struct Received
        {
            std::string text;
            bool        closed = false;
            U16         code   = 0;
            std::string reason;
        };

        // The HTTP status the upgrade request is answered with, sent from
        // this origin -- from none, where empty: 101 where it opened, the
        // refusal's own where it was refused, -1 where it neither opened
        // nor was answered.
        int open(U16 port, const std::string& origin = std::string())
        {
            if (!connect(port))
            {
                return -1;
            }

            if (!origin.empty())
            {
                mStream.set_option(boost::beast::websocket::stream_base::decorator(
                    [origin](boost::beast::websocket::request_type& request)
                    { request.set(boost::beast::http::field::origin, origin); }));
            }
            // The response goes with the handler, which may outlive this
            // call where the time runs out.
            auto response   = std::make_shared<boost::beast::websocket::response_type>();
            auto handshaken = std::make_shared<Step>();
            mStream.async_handshake(*response, "127.0.0.1:" + std::to_string(port), "/",
                                    [handshaken, response](const boost::system::error_code& error)
                                    {
                                        handshaken->mDone  = true;
                                        handshaken->mError = error;
                                    });
            if (!drive(*handshaken))
            {
                return -1;
            }
            // Opened, or refused with a status of the server's choosing.
            if (!handshaken->mError || handshaken->mError == boost::beast::websocket::error::upgrade_declined)
            {
                return response->result_int();
            }
            return -1;
        }

        // The HTTP status a plain GET, asking for no upgrade, is answered
        // with; -1 where none came.
        int get(U16 port)
        {
            if (!connect(port))
            {
                return -1;
            }
            auto request = std::make_shared<boost::beast::http::request<boost::beast::http::empty_body>>(
                boost::beast::http::verb::get, "/", 11);
            request->set(boost::beast::http::field::host, "127.0.0.1:" + std::to_string(port));
            auto written = std::make_shared<Step>();
            boost::beast::http::async_write(mStream.next_layer(), *request,
                                            [written, request](const boost::system::error_code& error, std::size_t)
                                            {
                                                written->mDone  = true;
                                                written->mError = error;
                                            });
            if (!drive(*written) || written->mError)
            {
                return -1;
            }
            auto response = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();
            auto read     = std::make_shared<Step>();
            boost::beast::http::async_read(mStream.next_layer(), mBuffer, *response,
                                           [read, response](const boost::system::error_code& error, std::size_t)
                                           {
                                               read->mDone  = true;
                                               read->mError = error;
                                           });
            if (!drive(*read) || read->mError)
            {
                return -1;
            }
            return response->result_int();
        }

        bool send(const std::string& text)
        {
            auto message = std::make_shared<std::string>(text);
            auto written = std::make_shared<Step>();
            mStream.async_write(boost::asio::buffer(*message),
                                [written, message](const boost::system::error_code& error, std::size_t)
                                {
                                    written->mDone  = true;
                                    written->mError = error;
                                });
            return drive(*written) && !written->mError;
        }

        // The next message, or the close, calling pump while neither has
        // come yet.
        Received receive(const std::function<void()>& pump = std::function<void()>())
        {
            auto read = std::make_shared<Step>();
            mStream.async_read(mBuffer, handler(read));
            Received received;
            if (!drive(*read, pump))
            {
                return received;
            }
            if (read->mError == boost::beast::websocket::error::closed)
            {
                received.closed = true;
                received.code   = mStream.reason().code;
                received.reason = std::string(mStream.reason().reason.data(), mStream.reason().reason.size());
            }
            else if (!read->mError)
            {
                received.text = boost::beast::buffers_to_string(mBuffer.data());
                mBuffer.consume(mBuffer.size());
            }
            return received;
        }

        // Closes as a client does once it is done: its close, then the
        // server's answer. False where that did not go through.
        bool close()
        {
            auto closed = std::make_shared<Step>();
            mStream.async_close(boost::beast::websocket::close_code::normal, handler(closed));
            return drive(*closed) && !closed->mError;
        }

    private:
        bool connect(U16 port)
        {
            auto connected = std::make_shared<Step>();
            boost::beast::get_lowest_layer(mStream).async_connect(
                boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), handler(connected));
            return drive(*connected) && !connected->mError;
        }

        // Turns the client, and pump, until the step is done or ten
        // seconds pass. A step that runs out of time takes the connection
        // with it, so that nothing after it waits on it too.
        bool drive(const Step& step, const std::function<void()>& pump = std::function<void()>())
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!step.mDone && std::chrono::steady_clock::now() < deadline)
            {
                if (pump)
                {
                    pump();
                }
                mIO.run_for(std::chrono::milliseconds(10));
                if (mIO.stopped())
                {
                    mIO.restart();
                }
            }
            if (!step.mDone)
            {
                boost::beast::get_lowest_layer(mStream).close();
                mIO.run_for(std::chrono::seconds(1));
            }
            mIO.restart();
            return step.mDone;
        }

        boost::asio::io_context                                   mIO;
        boost::beast::websocket::stream<boost::beast::tcp_stream> mStream{ mIO };
        boost::beast::flat_buffer                                 mBuffer;
    };

    // A server that keeps the last connection to open, and counts those
    // that closed. Told both on its own thread.
    class RecordingServer : public LLWebsocketMgr::WSServer
    {
    public:
        using LLWebsocketMgr::WSServer::WSServer;

        void onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection) override
        {
            if (mLimit)
            {
                connection->setMessageLimit(mLimit);
            }
            LLMutexLock lock(&mMutex);
            mOpened = connection;
        }

        void onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t&) override { ++mClosed; }

        LLWebsocketMgr::WSConnection::ptr_t opened() const
        {
            LLMutexLock lock(&mMutex);
            return mOpened;
        }

        int closed() const { return mClosed; }
        int messages() const { return mMessages; }

        // The largest message each connection takes, set as it opens;
        // zero for the transport's own.
        size_t mLimit = 0;

    protected:
        // A connection that counts what it is sent.
        class Counting : public LLWebsocketMgr::WSConnection
        {
        public:
            Counting(const LLWebsocketMgr::WSServer::ptr_t& server, const LLWebsocketMgr::connection_h& handle, std::atomic<int>& count) :
                LLWebsocketMgr::WSConnection(server, handle),
                mCount(count)
            {
            }
            void onMessage(const std::string&) override { ++mCount; }

        private:
            std::atomic<int>& mCount;
        };

        LLWebsocketMgr::WSConnection::ptr_t connectionFactory(LLWebsocketMgr::WSServer::ptr_t server, LLWebsocketMgr::connection_h handle) override
        {
            return std::make_shared<Counting>(server, handle, mMessages);
        }

    private:
        mutable LLMutex                     mMutex;
        LLWebsocketMgr::WSConnection::ptr_t mOpened;
        std::atomic<int>                    mClosed{ 0 };
        std::atomic<int>                    mMessages{ 0 };
    };

    // Whether what is waited on comes about within ten seconds.
    bool waitFor(const std::function<bool()>& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!ready())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    // A JSON-RPC server with one method, which says whose thread it ran on.
    class TestRPCServer : public LLJSONRPCServer
    {
    public:
        using LLJSONRPCServer::LLJSONRPCServer;

    protected:
        LLSD handleGetVersion(const LLJSONRPCConnection::ptr_t&, const LLSD&) const override { return LLSD(); }

        void setupConnectionMethods(LLJSONRPCConnection::ptr_t connection) override
        {
            LLJSONRPCServer::setupConnectionMethods(connection);
            connection->registerAsyncMethod("test.echo",
                [](const std::string&, const LLSD&, const LLSD& params) -> LLSD
                {
                    LLSD result = params;
                    std::ostringstream thread;
                    thread << std::this_thread::get_id();
                    result["thread"] = thread.str();
                    return result;
                });
            // What a handler says it was asked wrong is the client's to
            // hear; what broke in it is not.
            connection->registerMethod("test.throw",
                [](const std::string&, const LLSD&, const LLSD&) -> LLSD
                {
                    throw std::runtime_error("C:\\Users\\someone\\secret");
                });
            connection->registerAsyncMethod("test.throw.async",
                [](const std::string&, const LLSD&, const LLSD&) -> LLSD
                {
                    throw std::runtime_error("C:\\Users\\someone\\secret");
                });
            connection->registerMethod("test.refuse",
                [](const std::string&, const LLSD&, const LLSD&) -> LLSD
                {
                    throw LLJSONRPCConnection::InvalidParams("no such script");
                });
        }
    };

    // One whose connections start out unauthenticated, as the script
    // editor's do, and are never told otherwise.
    class GatedRPCServer : public TestRPCServer
    {
    public:
        using TestRPCServer::TestRPCServer;

        void onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t& connection) override
        {
            TestRPCServer::onConnectionClosed(connection);
            ++mClosed;
        }

        std::atomic<int> mClosed{ 0 };

    protected:
        class Gated : public LLJSONRPCConnection
        {
        public:
            Gated(const LLWebsocketMgr::WSServer::ptr_t& server, const LLWebsocketMgr::connection_h& handle) :
                LLJSONRPCConnection(server, handle)
            {
                setAuthenticated(false);
            }
        };

        LLWebsocketMgr::WSConnection::ptr_t connectionFactory(LLWebsocketMgr::WSServer::ptr_t server, LLWebsocketMgr::connection_h handle) override
        {
            auto connection = std::make_shared<Gated>(server, handle);
            setupConnectionMethods(connection);
            return connection;
        }
    };
}

namespace
{
    struct WebsocketMgrTestData
    {
        // Each test adds its servers to the manager and removes them, and one
        // that fails first would leave them listening into the next. The
        // manager goes with the test, as it was not there before it, and
        // stops whatever servers it still holds as it goes.
        ~WebsocketMgrTestData()
        {
            LLWebsocketMgr::deleteSingleton();
        }
    };


    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a program sending no origin is let in and a page sending one refused", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name = "origin_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        CHECK(manager.startServer(name));

        const int program = TestClient().open(port);
        const int page    = TestClient().open(port, "https://example.com");
        const int opaque  = TestClient().open(port, "null");
        const int local   = TestClient().open(port, "http://127.0.0.1:8080");
        manager.removeServer(name);

        UNSCOPED_INFO("no origin: in");
        CHECK(program == 101);
        UNSCOPED_INFO("a web page: refused");
        CHECK(page == 403);
        UNSCOPED_INFO("a page with an opaque origin, a sandboxed frame or a file: refused");
        CHECK(opaque == 403);
        UNSCOPED_INFO("a page served from this machine: refused");
        CHECK(local == 403);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a request is answered from a main loop the server thread never waits on", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        // The main loop, drained here by hand: a server's thread that
        // waited on it would wait until the answer's own time ran out.
        LL::WorkQueue     mainloop("mainloop");
        const std::string name = "rpc_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101;

        LLSD params;
        params["item_id"] = "5748decc-f629-461c-9a36-a35a221fe21f";
        params["lines"]   = LLSD::emptyArray();
        params["lines"].append("default {}");
        const bool  sent  = opened && client.send(LlsdToJson(LLJSONRPCConnection::makeEnvelope(LLSD("req_1"), "test.echo", params, LLSD(), LLSD())));
        std::string reply = sent ? client.receive([&]() { mainloop.runPending(); }).text : std::string();
        // Done, as an editor would be: nothing is left for the stop to wait on.
        const bool  closed = opened && client.close();
        manager.removeServer(name);
        mainloop.close();

        std::ostringstream here;
        here << std::this_thread::get_id();
        LLSD answer;
        CHECK(opened);
        CHECK(sent);
        UNSCOPED_INFO("closed by the client");
        CHECK(closed);
        UNSCOPED_INFO("answered");
        REQUIRE(LlsdFromJsonString(reply, answer));
        UNSCOPED_INFO("to the request");
        CHECK(answer["id"].asString() == std::string("req_1"));
        UNSCOPED_INFO("run on the thread that drains the main loop");
        CHECK(answer["result"]["thread"].asString() == here.str());
        UNSCOPED_INFO("with its params");
        CHECK(answer["result"]["item_id"].asString() == params["item_id"].asString());
        UNSCOPED_INFO("all of them");
        CHECK(answer["result"]["lines"][0].asString() == std::string("default {}"));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a request asking for no upgrade is told to ask for one", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "http_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        CHECK(manager.startServer(name));

        const int status = TestClient().get(port);
        manager.removeServer(name);

        UNSCOPED_INFO("426 Upgrade Required");
        CHECK(status == 426);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: messages go before the close with its code and reason and none after", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "close_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened     = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        auto       connection = server->opened();
        // Several, and long, so that some are still waiting behind a
        // write when the close is asked for.
        const std::string filler(10000, 'x');
        bool              sent = opened;
        for (int i = 0; sent && i < 3; ++i)
        {
            sent = connection->sendMessage(std::to_string(i) + filler);
        }
        if (opened)
        {
            connection->closeConnection(1000, "done");
        }
        const bool after_close = opened && connection->sendMessage(std::string("too late"));
        bool       in_order    = true;
        for (int i = 0; in_order && i < 3; ++i)
        {
            in_order = client.receive().text == std::to_string(i) + filler;
        }
        const auto last = client.receive();
        const bool told = waitFor([&]() { return server->closed() == 1; });
        manager.removeServer(name);

        CHECK(opened);
        CHECK(sent);
        UNSCOPED_INFO("the messages first, in order");
        CHECK(in_order);
        UNSCOPED_INFO("then the close");
        CHECK(last.closed);
        CHECK(last.code == U16(1000));
        CHECK(last.reason == std::string("done"));
        UNSCOPED_INFO("a send after the close refused");
        CHECK_FALSE(after_close);
        UNSCOPED_INFO("the server told it closed");
        CHECK(told);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: sends from two threads at once arrive whole and in each thread's order", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "threads_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened     = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        auto       connection = server->opened();

        // Long enough to go in several frames.
        const int         count = 100;
        const std::string filler(10000, 'x');
        auto              message = [&](char tag, int index) { return std::string(1, tag) + ":" + std::to_string(index) + ":" + filler; };
        auto              sendAll = [&](char tag)
        {
            for (int i = 0; opened && i < count; ++i)
            {
                connection->sendMessage(message(tag, i));
            }
        };
        std::thread other([&]() { sendAll('b'); });
        sendAll('a');
        other.join();

        int  next_a = 0;
        int  next_b = 0;
        bool whole  = true;
        for (int i = 0; opened && i < 2 * count; ++i)
        {
            const std::string text = client.receive().text;
            if (!text.empty() && text[0] == 'a')
            {
                whole = whole && text == message('a', next_a++);
            }
            else if (!text.empty() && text[0] == 'b')
            {
                whole = whole && text == message('b', next_b++);
            }
            else
            {
                whole = false;
                break;
            }
        }
        // Done: nothing is left for the stop to wait on.
        const bool closed = opened && client.close();
        manager.removeServer(name);

        CHECK(opened);
        UNSCOPED_INFO("closed by the client");
        CHECK(closed);
        UNSCOPED_INFO("each whole, and in its thread's order");
        CHECK(whole);
        UNSCOPED_INFO("all of the first thread's");
        CHECK(next_a == count);
        UNSCOPED_INFO("all of the second's");
        CHECK(next_b == count);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: stopping closes clients as going away and tells the server at once", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "stop_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });

        // The client reads, and so answers the close, while the server stops.
        TestClient::Received received;
        std::thread          reader([&]() { received = client.receive(); });
        const auto           began = std::chrono::steady_clock::now();
        manager.stopServer(name);
        const auto took = std::chrono::steady_clock::now() - began;
        reader.join();
        const int  closed  = server->closed();
        const bool running = server->isRunning();
        manager.removeServer(name);

        CHECK(opened);
        CHECK(received.closed);
        UNSCOPED_INFO("as going away");
        CHECK(received.code == U16(1001));
        UNSCOPED_INFO("the server told, once");
        CHECK(closed == 1);
        CHECK_FALSE(running);
        UNSCOPED_INFO("without waiting out the deadline");
        CHECK(took < std::chrono::milliseconds(900));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: stopping waits little on a client that never answers yet says it closed", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "deadline_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        // Opened, and then never read again.
        TestClient client;
        const bool opened = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        const auto began  = std::chrono::steady_clock::now();
        manager.stopServer(name);
        const auto took   = std::chrono::steady_clock::now() - began;
        const int  closed = server->closed();
        manager.removeServer(name);

        CHECK(opened);
        UNSCOPED_INFO("the server told, once");
        CHECK(closed == 1);
        UNSCOPED_INFO("within about the deadline");
        CHECK(took < std::chrono::milliseconds(2500));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a port another socket listens on fails the start", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        boost::asio::io_context        io;
        boost::asio::ip::tcp::acceptor holder(io);
        boost::system::error_code      ec;
        holder.open(boost::asio::ip::tcp::v4(), ec);
        if (!ec)
        {
            holder.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), ec);
        }
        if (!ec)
        {
            holder.listen(boost::asio::socket_base::max_listen_connections, ec);
        }
        if (ec)
        {
            SKIP("could not hold the port");
        }

        const std::string name    = "busy_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<LLWebsocketMgr::WSServer>(name, port, true);
        CHECK(manager.addServer(server));
        const bool started = manager.startServer(name);
        const bool running = server->isRunning();
        manager.removeServer(name);

        CHECK_FALSE(started);
        CHECK_FALSE(running);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a server stopped with a client connected starts again on the same port", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "restart_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient first;
        const bool opened = first.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        std::thread reader([&]() { first.receive(); });
        manager.stopServer(name);
        reader.join();

        const bool restarted = manager.startServer(name);
        const int  second    = TestClient().open(port);
        manager.removeServer(name);

        CHECK(opened);
        CHECK(restarted);
        UNSCOPED_INFO("a new connection opens after the restart");
        CHECK(second == 101);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a client that stops reading is dropped once too much waits unsent to it", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "backlog_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        // Opened, and then never read.
        TestClient client;
        const bool opened     = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        auto       connection = server->opened();

        // Far more than the 32 MB it may hold, with the sockets' own
        // buffers on top; sent until it is refused.
        const std::string megabyte(1024 * 1024, 'x');
        int               sent = 0;
        while (opened && sent < 128 && connection->sendMessage(megabyte))
        {
            ++sent;
        }
        const bool dropped = waitFor([&]() { return server->closed() == 1; });
        manager.removeServer(name);

        CHECK(opened);
        CHECK(dropped);
        UNSCOPED_INFO("before all 128 MB were taken");
        CHECK(sent < 128);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a close stuck behind unread messages drops the connection at its deadline", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "stuck_close_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened     = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        auto       connection = server->opened();

        // Under what it may hold, over what the sockets take: the close
        // waits behind it, and the client answers nothing either way.
        const std::string megabyte(1024 * 1024, 'x');
        for (int i = 0; opened && i < 8; ++i)
        {
            connection->sendMessage(megabyte);
        }
        if (opened)
        {
            connection->closeConnection(1000, "done");
        }
        const auto began   = std::chrono::steady_clock::now();
        const bool dropped = waitFor([&]() { return server->closed() == 1; });
        const auto took    = std::chrono::steady_clock::now() - began;
        manager.removeServer(name);

        CHECK(opened);
        CHECK(dropped);
        UNSCOPED_INFO("at the deadline, not before");
        CHECK(took > std::chrono::seconds(3));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a message within a connection's limit is taken and one past it closes it", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "limit_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        server->mLimit            = 1024;
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101;
        const bool small  = opened && client.send(std::string(512, 's'));
        const bool taken  = small && waitFor([&]() { return server->messages() == 1; });
        // The connection fails as the frame says how large it is: the
        // client sees the close (1009) where it reads it before the socket
        // goes, and a reset where not.
        if (taken)
        {
            client.send(std::string(2048, 'l'));
        }
        const auto seen   = client.receive();
        const bool closed = taken && waitFor([&]() { return server->closed() == 1; });
        manager.removeServer(name);

        CHECK(opened);
        UNSCOPED_INFO("the small one taken");
        CHECK(taken);
        UNSCOPED_INFO("the connection closed by the large one");
        CHECK(closed);
        UNSCOPED_INFO("with only the small one taken");
        CHECK(server->messages() == 1);
        UNSCOPED_INFO("a close the client read says too big");
        CHECK((!seen.closed || seen.code == 1009));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a connection past the most at once goes unanswered until there is room", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "crowd_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        CHECK(manager.startServer(name));

        // Thirty-two connections that never ask for anything: each holds
        // a place while its request is waited on.
        boost::asio::io_context                   io;
        std::vector<boost::asio::ip::tcp::socket> crowd;
        for (int i = 0; i < 32; ++i)
        {
            crowd.emplace_back(io);
            boost::system::error_code ec;
            crowd.back().connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), ec);
        }
        const int refused = TestClient().open(port);

        for (auto& socket : crowd)
        {
            boost::system::error_code ec;
            socket.close(ec);
        }
        int        later = -1;
        const bool taken = waitFor([&]() { return (later = TestClient().open(port)) == 101; });
        manager.removeServer(name);

        UNSCOPED_INFO("one more is not answered");
        CHECK(refused == -1);
        UNSCOPED_INFO("once they have gone, one is");
        CHECK(taken);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a malformed request is answered -32600 and a notification not at all", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "malformed_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101;
        auto       pump   = [&]() { mainloop.runPending(); };

        // No "jsonrpc": not a request.
        LLSD       malformed;
        const bool sent_malformed = opened && client.send(R"({"id":"bad","method":"system.ping"})");
        const bool read_malformed = sent_malformed && LlsdFromJsonString(client.receive(pump).text, malformed);

        // test.echo is run on the main thread, and answered: called as a
        // notification, it is neither. The ping after it is answered next.
        const bool sent_notification = opened && client.send(R"({"jsonrpc":"2.0","method":"test.echo","params":{}})");
        const bool sent_ping         = opened && client.send(R"({"jsonrpc":"2.0","id":"ping","method":"system.ping"})");
        LLSD       next;
        const bool read_next = sent_ping && LlsdFromJsonString(client.receive(pump).text, next);
        client.close();
        manager.removeServer(name);
        mainloop.close();

        CHECK(opened);
        CHECK(read_malformed);
        UNSCOPED_INFO("as an invalid request");
        CHECK(malformed["error"]["code"].asInteger() == LLSD::Integer(-32600));
        UNSCOPED_INFO("with its id");
        CHECK(malformed["id"].asString() == std::string("bad"));
        CHECK(sent_notification);
        UNSCOPED_INFO("something answered after it");
        CHECK(read_next);
        UNSCOPED_INFO("the ping, not the notification");
        CHECK(next["id"].asString() == std::string("ping"));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: an unauthenticated connection is refused calls and reads 64 KB at most", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "gated_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<GatedRPCServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101;
        LLSD       refusal;
        const bool asked  = opened && client.send(R"({"jsonrpc":"2.0","id":"ask","method":"system.ping"})");
        const bool read   = asked && LlsdFromJsonString(client.receive().text, refusal);

        // Within the limit, and not JSON: read, and answered as such.
        LLSD       parse_error;
        const bool within        = read && client.send(std::string(32 * 1024, 'x'));
        const bool read_within   = within && LlsdFromJsonString(client.receive().text, parse_error);
        const bool open_after    = server->mClosed == 0;

        // Past it: the connection fails as the frame says how large it is,
        // likely before the client has finished writing it.
        if (read_within)
        {
            client.send(std::string(100 * 1024, 'x'));
        }
        const bool closed = read_within && waitFor([&]() { return server->mClosed == 1; });
        manager.removeServer(name);
        mainloop.close();

        CHECK(opened);
        UNSCOPED_INFO("answered");
        CHECK(read);
        UNSCOPED_INFO("as unauthorized");
        CHECK(refusal["error"]["code"].asInteger() == LLSD::Integer(-32002));
        CHECK(read_within);
        UNSCOPED_INFO("and answered as not JSON");
        CHECK(parse_error["error"]["code"].asInteger() == LLSD::Integer(-32700));
        CHECK(open_after);
        UNSCOPED_INFO("and closed by the one past it");
        CHECK(closed);
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: a throw is answered as a bare internal error and a refusal says why", "[LLWebsocketMgr_Tests]")
    {
        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "throw_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        CHECK(manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101;
        auto       pump   = [&]() { mainloop.runPending(); };
        auto       ask    = [&](const std::string& method, LLSD& answer)
        {
            return opened && client.send(R"({"jsonrpc":"2.0","id":"x","method":")" + method + R"("})") &&
                   LlsdFromJsonString(client.receive(pump).text, answer);
        };
        LLSD       sync_answer, async_answer, refusal;
        const bool read_sync    = ask("test.throw", sync_answer);
        const bool read_async   = ask("test.throw.async", async_answer);
        const bool read_refusal = ask("test.refuse", refusal);
        client.close();
        manager.removeServer(name);
        mainloop.close();

        CHECK(opened);
        for (const auto& [what, read, answer] : { std::tuple{ "run where it came in", read_sync, sync_answer },
                                                  std::tuple{ "run on the main thread", read_async, async_answer } })
        {
            INFO(what);
            UNSCOPED_INFO("answered");
            REQUIRE(read);
            UNSCOPED_INFO("as an internal error");
            REQUIRE(answer["error"]["code"].asInteger() == LLSD::Integer(-32603));
            UNSCOPED_INFO("and nothing more");
            REQUIRE(answer["error"]["message"].asString() == std::string("Internal error"));
        }
        CHECK(read_refusal);
        UNSCOPED_INFO("as invalid params");
        CHECK(refusal["error"]["code"].asInteger() == LLSD::Integer(-32602));
        CHECK_THAT(refusal["error"]["message"].asString(), Catch::Matchers::ContainsSubstring("no such script"));
    }

    TEST_CASE_METHOD(WebsocketMgrTestData, "LLWebsocketMgr Tests: sends while the server thread is busy count against the 32 MB limit", "[LLWebsocketMgr_Tests]")
    {
        // Holds the server thread as the connection opens, so that nothing
        // sent meanwhile is queued, let alone written, until it is let go.
        class HeldServer : public LLWebsocketMgr::WSServer
        {
        public:
            using LLWebsocketMgr::WSServer::WSServer;

            void onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection) override
            {
                mConnection = connection;
                mOpened     = true;
                // Let go at the latest here, where the test failed before
                // it could.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!mRelease && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            void onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t&) override { ++mClosed; }

            LLWebsocketMgr::WSConnection::ptr_t mConnection; ///< Set before mOpened
            std::atomic<bool>                   mOpened{ false };
            std::atomic<bool>                   mRelease{ false };
            std::atomic<int>                    mClosed{ 0 };
        };

        const U16 port = unusedPort();
        if (!port)
        {
            SKIP("no port free to listen on");
        }
        const std::string name    = "held_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<HeldServer>(name, port, true);
        CHECK(manager.addServer(server));
        CHECK(manager.startServer(name));

        TestClient client;
        const bool opened = client.open(port) == 101 && waitFor([&]() { return server->mOpened.load(); });

        // Nothing goes out while the thread is held: 31 whole megabytes,
        // and what each message costs, fit in the 32 MB; a 32nd does not.
        const std::string megabyte(1024 * 1024, 'x');
        int               sent = 0;
        while (opened && sent < 128 && server->mConnection->sendMessage(megabyte))
        {
            ++sent;
        }
        const bool refused = opened && !server->mConnection->sendMessage(megabyte);
        server->mRelease   = true;
        const bool dropped = waitFor([&]() { return server->mClosed == 1; });
        manager.removeServer(name);

        CHECK(opened);
        UNSCOPED_INFO("taken up to the most it may hold");
        CHECK(sent == 31);
        CHECK(refused);
        CHECK(dropped);
    }
}
