/**
 * @file test_websocketmgr.hpp
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

#ifndef TEST_LLCORE_WEBSOCKETMGR_H_
#define TEST_LLCORE_WEBSOCKETMGR_H_

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

namespace tut
{
    struct WebsocketMgrTestData
    {
    };

    typedef test_group<WebsocketMgrTestData> WebsocketMgrTestGroupType;
    typedef WebsocketMgrTestGroupType::object WebsocketMgrTestObjectType;
    WebsocketMgrTestGroupType WebsocketMgrTestGroup("LLWebsocketMgr Tests");

    template<> template<>
    void WebsocketMgrTestObjectType::test<1>()
    {
        set_test_name("a program, which sends no origin, is let in; a page, which always does, is refused");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name = "origin_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        ensure("started", manager.startServer(name));

        const int program = TestClient().open(port);
        const int page    = TestClient().open(port, "https://example.com");
        const int opaque  = TestClient().open(port, "null");
        const int local   = TestClient().open(port, "http://127.0.0.1:8080");
        manager.removeServer(name);

        ensure_equals("no origin: in", program, 101);
        ensure_equals("a web page: refused", page, 403);
        ensure_equals("a page with an opaque origin, a sandboxed frame or a file: refused", opaque, 403);
        ensure_equals("a page served from this machine: refused", local, 403);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<2>()
    {
        set_test_name("a request read on the server's thread is answered from the main loop, which that thread does not wait on");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        // The main loop, drained here by hand: a server's thread that
        // waited on it would wait until the answer's own time ran out.
        LL::WorkQueue     mainloop("mainloop");
        const std::string name = "rpc_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        ensure("started", manager.startServer(name));

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
        ensure("opened", opened);
        ensure("sent", sent);
        ensure("closed by the client", closed);
        ensure("answered", LlsdFromJsonString(reply, answer));
        ensure_equals("to the request", answer["id"].asString(), std::string("req_1"));
        ensure_equals("run on the thread that drains the main loop", answer["result"]["thread"].asString(), here.str());
        ensure_equals("with its params", answer["result"]["item_id"].asString(), params["item_id"].asString());
        ensure_equals("all of them", answer["result"]["lines"][0].asString(), std::string("default {}"));
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<3>()
    {
        set_test_name("a request asking for no upgrade is told to ask for one");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "http_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        ensure("started", manager.startServer(name));

        const int status = TestClient().get(port);
        manager.removeServer(name);

        ensure_equals("426 Upgrade Required", status, 426);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<4>()
    {
        set_test_name("messages sent just before a close go first, the close carries its code and reason, and nothing goes after it");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "close_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("sent", sent);
        ensure("the messages first, in order", in_order);
        ensure("then the close", last.closed);
        ensure_equals("with its code", last.code, U16(1000));
        ensure_equals("and its reason", last.reason, std::string("done"));
        ensure("a send after the close refused", !after_close);
        ensure("the server told it closed", told);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<5>()
    {
        set_test_name("messages sent from two threads at once each arrive whole, and each thread's in its order");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "threads_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("closed by the client", closed);
        ensure("each whole, and in its thread's order", whole);
        ensure_equals("all of the first thread's", next_a, count);
        ensure_equals("all of the second's", next_b, count);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<6>()
    {
        set_test_name("stopping closes each client as going away, and says so to the server without waiting out the deadline");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "stop_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("the client closed", received.closed);
        ensure_equals("as going away", received.code, U16(1001));
        ensure_equals("the server told, once", closed, 1);
        ensure("stopped", !running);
        ensure("without waiting out the deadline", took < std::chrono::milliseconds(900));
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<7>()
    {
        set_test_name("stopping waits little on a client that never answers the close, and still says it closed");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "deadline_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

        // Opened, and then never read again.
        TestClient client;
        const bool opened = client.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        const auto began  = std::chrono::steady_clock::now();
        manager.stopServer(name);
        const auto took   = std::chrono::steady_clock::now() - began;
        const int  closed = server->closed();
        manager.removeServer(name);

        ensure("opened", opened);
        ensure_equals("the server told, once", closed, 1);
        ensure("within about the deadline", took < std::chrono::milliseconds(2500));
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<8>()
    {
        set_test_name("a port another socket listens on fails the start");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
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
            skip("could not hold the port");
        }

        const std::string name    = "busy_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<LLWebsocketMgr::WSServer>(name, port, true);
        ensure("added", manager.addServer(server));
        const bool started = manager.startServer(name);
        const bool running = server->isRunning();
        manager.removeServer(name);

        ensure("not started", !started);
        ensure("not running", !running);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<9>()
    {
        set_test_name("a server stopped with a client connected starts again on the same port");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "restart_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

        TestClient first;
        const bool opened = first.open(port) == 101 && waitFor([&]() { return server->opened() != nullptr; });
        std::thread reader([&]() { first.receive(); });
        manager.stopServer(name);
        reader.join();

        const bool restarted = manager.startServer(name);
        const int  second    = TestClient().open(port);
        manager.removeServer(name);

        ensure("opened", opened);
        ensure("started again", restarted);
        ensure_equals("and opens a connection", second, 101);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<10>()
    {
        set_test_name("a client that stops reading is dropped once what waits unsent to it passes the most it may");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "backlog_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("dropped", dropped);
        ensure("before all 128 MB were taken", sent < 128);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<11>()
    {
        set_test_name("a close stuck behind messages the client will not read drops the connection after its deadline");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "stuck_close_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("dropped", dropped);
        ensure("at the deadline, not before", took > std::chrono::seconds(3));
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<12>()
    {
        set_test_name("a message within a connection's limit is taken, and one past it closes the connection");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "limit_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<RecordingServer>(name, port, true);
        server->mLimit            = 1024;
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("the small one taken", taken);
        ensure("the connection closed by the large one", closed);
        ensure_equals("with only the small one taken", server->messages(), 1);
        ensure("a close the client read says too big", !seen.closed || seen.code == 1009);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<13>()
    {
        set_test_name("past the most connections at once, one more is closed unanswered, and taken once there is room");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        const std::string name    = "crowd_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<LLWebsocketMgr::WSServer>(name, port, true)));
        ensure("started", manager.startServer(name));

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

        ensure_equals("one more is not answered", refused, -1);
        ensure("once they have gone, one is", taken);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<14>()
    {
        set_test_name("a malformed request is answered -32600 with its id; a method needing an id, called without one, is neither run nor answered");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "malformed_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("the malformed one answered", read_malformed);
        ensure_equals("as an invalid request", malformed["error"]["code"].asInteger(), LLSD::Integer(-32600));
        ensure_equals("with its id", malformed["id"].asString(), std::string("bad"));
        ensure("the notification sent", sent_notification);
        ensure("something answered after it", read_next);
        ensure_equals("the ping, not the notification", next["id"].asString(), std::string("ping"));
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<15>()
    {
        set_test_name("a connection not yet authenticated is refused what it asks, reads a message within 64 KB, and is closed by one past it");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "gated_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        auto              server  = std::make_shared<GatedRPCServer>(name, port, true);
        ensure("added", manager.addServer(server));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        ensure("answered", read);
        ensure_equals("as unauthorized", refusal["error"]["code"].asInteger(), LLSD::Integer(-32002));
        ensure("the one within the limit read", read_within);
        ensure_equals("and answered as not JSON", parse_error["error"]["code"].asInteger(), LLSD::Integer(-32700));
        ensure("the connection open after it", open_after);
        ensure("and closed by the one past it", closed);
    }

    template<> template<>
    void WebsocketMgrTestObjectType::test<16>()
    {
        set_test_name("a handler that throws is answered as an internal error saying nothing of what it threw; one that refuses says why");

        const U16 port = unusedPort();
        if (!port)
        {
            skip("no port free to listen on");
        }
        LL::WorkQueue     mainloop("mainloop");
        const std::string name    = "throw_test";
        LLWebsocketMgr&   manager = LLWebsocketMgr::instance();
        ensure("added", manager.addServer(std::make_shared<TestRPCServer>(name, port, true)));
        ensure("started", manager.startServer(name));

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

        ensure("opened", opened);
        for (const auto& [what, read, answer] : { std::tuple{ "run where it came in", read_sync, sync_answer },
                                                  std::tuple{ "run on the main thread", read_async, async_answer } })
        {
            ensure(std::string(what) + ": answered", read);
            ensure_equals(std::string(what) + ": as an internal error", answer["error"]["code"].asInteger(), LLSD::Integer(-32603));
            ensure_equals(std::string(what) + ": and nothing more", answer["error"]["message"].asString(), std::string("Internal error"));
        }
        ensure("the refusal answered", read_refusal);
        ensure_equals("as invalid params", refusal["error"]["code"].asInteger(), LLSD::Integer(-32602));
        ensure_contains("saying why", refusal["error"]["message"].asString(), "no such script");
    }
}

#endif
