/**
 * @file llwebsocketmgr.cpp
 * @brief WebSocket manager singleton implementation
 *
 * $LicenseInfo:firstyear=2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Linden Research, Inc.
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

#include "llwebsocketmgr.h"
#include "llerror.h"
#include "llexception.h"
#include "llsdserialize.h"
#include "llhost.h"
#include "llsdjson.h"
#include "llthread.h"
#include "stringize.h"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <thread>
#include <vector>

//------------------------------------------------------------------------
namespace
{
    namespace beast     = boost::beast;
    namespace http      = boost::beast::http;
    namespace websocket = boost::beast::websocket;
    namespace net       = boost::asio;
    using tcp           = boost::asio::ip::tcp;

    using connection_state_t = LLWebsocketMgr::connection_state_t;

    /// The largest message a client may send: far past any script's text.
    constexpr std::size_t MAX_MESSAGE_BYTES = 16 * 1024 * 1024;
    /// The most an upgrade request's headers may take.
    constexpr std::size_t MAX_REQUEST_HEADER_BYTES = 8 * 1024;
    /// How long a client has to send its upgrade request: one that means
    /// to open a connection sends it at once.
    constexpr auto REQUEST_TIMEOUT = std::chrono::seconds(10);
    /// The most a connection's unsent messages may hold. A client that has
    /// stopped reading is dropped past it, rather than the viewer holding
    /// everything it would have been sent.
    constexpr std::size_t MAX_QUEUED_BYTES = 32 * 1024 * 1024;
    /// How long a close may wait -- behind messages the client is not
    /// reading, or for its answer -- before the connection is dropped.
    constexpr auto CLOSE_DEADLINE = std::chrono::seconds(5);
    /// The most connections at once, open or still being opened.
    constexpr std::size_t MAX_SESSIONS = 32;
    /// How long stopping waits for the connections' closes to finish.
    constexpr auto STOP_DEADLINE = std::chrono::seconds(1);
    /// How long after a failed accept the next is tried: at once would
    /// spin where, say, no descriptors are left.
    constexpr auto ACCEPT_RETRY = std::chrono::milliseconds(100);

    // A close's reason as a frame can carry it: 123 bytes at most, cut
    // where a UTF-8 character begins rather than through one.
    websocket::close_reason makeCloseReason(U16 code, const std::string& reason)
    {
        std::size_t length = std::min(reason.size(), websocket::reason_string::static_capacity);
        if (length < reason.size())
        {
            while (length > 0 && (static_cast<U8>(reason[length]) & 0xC0) == 0x80)
            {
                --length;
            }
        }
        websocket::close_reason close(code);
        close.reason.assign(reason.data(), length);
        return close;
    }

    /**
     * @brief One connection, from its upgrade request to its close
     *
     * All it does runs on its own strand, so that sends and closes asked
     * for from any thread go out in the order they were asked for, and a
     * close goes after every message asked for before it.
     */
    class WSSession : public std::enable_shared_from_this<WSSession>
    {
    public:
        WSSession(Server_impl& server, tcp::socket&& socket) :
            mServer(server),
            mStrand(socket.get_executor()),
            mStream(std::move(socket)),
            mCloseDeadline(mStrand)
        {
        }

        void run();

        /// Any thread's. False where the connection is not open, or where
        /// taking the message would leave more waiting unsent than the
        /// most it may hold, which drops it. A send taken may still go
        /// unsent where a close from another thread lands first.
        bool send(const std::string& message);
        bool close(U16 code, const std::string& reason);

        /// The server's, as it stops: a connection still being opened is
        /// dropped, and one that is open is closed as going away.
        void shutdown();

        /// Any thread's: the largest message taken from here on, zero for
        /// the transport's own.
        void setMessageLimit(std::size_t bytes);

        connection_state_t state() const { return mState; }

    private:
        /// What goes out, in order: a message, or the close after which
        /// nothing does.
        struct Outgoing
        {
            std::string             mText;
            bool                    mClose = false;
            websocket::close_reason mReason;
        };

        /// What a message counts against the most a connection may hold.
        static std::size_t heldBytes(const std::string& text) { return text.size() + sizeof(Outgoing); }

        LLWebsocketMgr::connection_h handle() { return weak_from_this(); }

        void readRequest();
        void onRequest(beast::error_code ec, std::size_t bytes);
        void refuse(http::status status);
        void onAccepted(beast::error_code ec);
        void readMessage();
        void onMessage(beast::error_code ec, std::size_t bytes);
        void enqueue(Outgoing&& outgoing);
        void writeNext();
        void onWritten(beast::error_code ec, std::size_t bytes);
        void onClosed(beast::error_code ec);
        void clearQueue();
        void finish();

        Server_impl&                                          mServer;
        const net::any_io_executor                            mStrand; ///< Kept apart from the stream, which is the strand's alone
        websocket::stream<beast::tcp_stream>                  mStream;
        net::steady_timer                                     mCloseDeadline; ///< The most a queued close waits
        beast::flat_buffer                                    mBuffer;
        std::optional<http::request_parser<http::empty_body>> mParser;
        http::request<http::empty_body>                       mRequest;
        std::optional<http::response<http::string_body>>      mRefusal;
        std::deque<Outgoing>                                  mQueue;
        std::atomic<std::size_t>                              mQueuedBytes{ 0 }; ///< What messages taken and not yet written hold, queued or on their way to mQueue
        std::atomic<connection_state_t>                       mState{ LLWebsocketMgr::connection_connecting };
        bool                                                  mWriting     = false;
        bool                                                  mCloseQueued = false;
        bool                                                  mOpened      = false;
        bool                                                  mFinished    = false;
    };

    std::shared_ptr<WSSession> sessionFor(const LLWebsocketMgr::connection_h& handle)
    {
        return std::static_pointer_cast<WSSession>(handle.lock());
    }
}

//------------------------------------------------------------------------
// LLWebsocketMgr Implementation
//

void LLWebsocketMgr::initSingleton()
{ }

void LLWebsocketMgr::cleanupSingleton()
{
    stopAllServers();
}

void LLWebsocketMgr::update()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    std::vector<WSServer::ptr_t> stops;

    for (auto &[name, server] : mServers)
    {
        if (server && server->isRunning())
        {
            if (!server->update())
            {
                stops.push_back(server);
            }
        }
    }

    for (const auto& server : stops)
    {
        if (server)
        {
            LL_DEBUGS("WebSocket") << "Stopping server: " << server->mServerName << LL_ENDL;
            removeServer(server->mServerName);
        }
    }
}


LLWebsocketMgr::WSServer::ptr_t LLWebsocketMgr::findServerByName(const std::string &name) const
{
    auto it = mServers.find(std::string(name));
    if (it != mServers.end())
    {
        return it->second;
    }
    return nullptr;
}

bool LLWebsocketMgr::addServer(const LLWebsocketMgr::WSServer::ptr_t& server)
{
    if (!server)
    {
        LL_WARNS("WebSocket") << "Attempted to add a null server" << LL_ENDL;
        return false;
    }

    auto it = mServers.find(server->mServerName);
    if (it != mServers.end())
    {
        LL_WARNS("WebSocket") << "Server with name " << server->mServerName << " already exists" << LL_ENDL;
        return false;
    }
    mServers[server->mServerName] = server;
    LL_INFOS("WebSocket") << "Added WebSocket server: " << server->mServerName << LL_ENDL;
    return true;
}

bool LLWebsocketMgr::removeServer(const std::string& name)
{
    auto it = mServers.find(name);
    if (it == mServers.end())
    {
        LL_WARNS("WebSocket") << "No server found with name " << name << " to remove" << LL_ENDL;
        return false;
    }
    if (it->second && it->second->isRunning())
        it->second->stop();
    mServers.erase(it);
    LL_INFOS("WebSocket") << "Removed WebSocket server: " << name << LL_ENDL;
    return true;
}


bool LLWebsocketMgr::startServer(const std::string &name) const
{
    LLWebsocketMgr::WSServer::ptr_t server = findServerByName(name);
    if (!server)
    {
        LL_WARNS("WebSocket") << "No server found with name " << name << " to start" << LL_ENDL;
        return false;
    }
    if (server->isRunning())
    {
        LL_WARNS("WebSocket") << "Server " << name << " is already running" << LL_ENDL;
        return false;
    }
    return server->start();
}

void LLWebsocketMgr::stopServer(const std::string& name) const
{
    LLWebsocketMgr::WSServer::ptr_t server = findServerByName(name);
    if (!server)
    {
        LL_WARNS("WebSocket") << "No server found with name " << name << " to stop" << LL_ENDL;
        return;
    }
    if (!server->isRunning())
    {
        LL_WARNS("WebSocket") << "Server " << name << " is not running" << LL_ENDL;
        return;
    }
    server->stop();
}

void LLWebsocketMgr::stopAllServers()
{
    for (auto &[name, server] : mServers)
    {
        if (server && server->isRunning())
        {
            LL_INFOS("WebSocket") << "Stopping server: " << name << LL_ENDL;
            server->stop();
        }
    }

    mServers.clear();
}

//------------------------------------------------------------------------
/**
 * @brief The listener, and the sessions it has taken
 *
 * A server's context is run by its one thread, so the sessions' strands
 * never run at once, and what is kept here of the sessions is touched
 * only there. Each start makes a fresh context and each stop drops it, so
 * that nothing of one run's sessions is left to the next.
 */
struct Server_impl
{
    Server_impl(LLWebsocketMgr::WSServer *owner, U16 port, bool local_only) :
        mOwner(owner),
        mPort(port),
        mLocalOnly(local_only)
    {
    }

    ~Server_impl() { release(); }

    /**
     * @brief Make a fresh context and listen on the port
     * @return false where the port could not be had
     *
     * Called on the thread starting the server, so that a port another
     * program holds fails the start. Binds 127.0.0.1 where the server is
     * local only; otherwise every address, IPv6 and IPv4 on one socket, or
     * IPv4 alone where there is no IPv6.
     */
    bool listen()
    {
        release();
        mContext = std::make_unique<net::io_context>();
        mAcceptor.emplace(*mContext);
        mRetry.emplace(*mContext);
        mDeadline.emplace(*mContext);
        mStopping = false;

        beast::error_code ec;
        if (mLocalOnly)
        {
            bindTo(tcp::endpoint(net::ip::address_v4::loopback(), mPort), ec);
        }
        else
        {
            bindTo(tcp::endpoint(net::ip::address_v6::any(), mPort), ec);
            if (ec)
            {
                LL_INFOS("WebSocket") << name() << " cannot listen on IPv6 (" << ec.message() << "); trying IPv4 alone" << LL_ENDL;
                ec.clear();
                bindTo(tcp::endpoint(net::ip::address_v4::any(), mPort), ec);
            }
        }
        if (ec)
        {
            LL_WARNS("WebSocket") << name() << " cannot listen on port " << mPort << ": " << ec.message() << LL_ENDL;
            release();
            return false;
        }

        LL_INFOS("WebSocket") << name() << " listening on " << mAcceptor->local_endpoint(ec) << LL_ENDL;
        accept();
        return true;
    }

    /// The server's thread, until the context stops. A handler that
    /// throws is logged and the context run on: one bad event does not
    /// end the server.
    void run()
    {
        for (;;)
        {
            try
            {
                mContext->run();
                break;
            }
            catch (...)
            {
                LOG_UNHANDLED_EXCEPTION(STRINGIZE("WebSocket server " << name()));
            }
        }
        mRunning = false;
    }

    /// Any thread's: the server's thread ends once every session has
    /// closed, or at the deadline.
    void stop()
    {
        net::post(*mContext, [this]() { shutdown(); });
    }

    /// Once the server's thread has gone: the context goes, and with it
    /// every session its handlers still held.
    void release()
    {
        mSessions.clear();
        mDeadline.reset();
        mRetry.reset();
        mAcceptor.reset();
        mContext.reset();
    }

    // What the sessions tell the owner, on the server's thread.
    bool acceptOrigin(const std::string& origin)
    {
        bool accepted = false;
        guarded("judging an origin", [&]() { accepted = mOwner->acceptOrigin(origin); });
        return accepted;
    }

    void opened(const LLWebsocketMgr::connection_h& handle)
    {
        guarded("opening a connection", [&]() { mOwner->handleOpenConnection(handle); });
    }

    void message(const LLWebsocketMgr::connection_h& handle, const std::string& text)
    {
        guarded("taking a message", [&]() { mOwner->handleMessage(handle, text); });
    }

    void closed(const LLWebsocketMgr::connection_h& handle)
    {
        guarded("closing a connection", [&]() { mOwner->handleCloseConnection(handle); });
    }

    void sessionEnded(const WSSession* ended)
    {
        std::erase_if(mSessions,
                      [ended](const std::weak_ptr<WSSession>& weak)
                      {
                          const auto session = weak.lock();
                          return !session || session.get() == ended;
                      });
        if (mStopping && mSessions.empty())
        {
            mContext->stop();
        }
    }

    const std::string& name() const { return mOwner->mServerName; }

    //-------------------------------------------
    void bindTo(const tcp::endpoint& endpoint, beast::error_code& ec)
    {
        mAcceptor->open(endpoint.protocol(), ec);
        if (!ec && endpoint.address().is_v6())
        {
            // Both families on the one socket, whatever the system's
            // default: Windows' is IPv6 alone.
            mAcceptor->set_option(net::ip::v6_only(false), ec);
        }
#if !LL_WINDOWS
        if (!ec)
        {
            // So that a restart is not refused while the last run's
            // connections sit out their TIME_WAIT. Not on Windows, where
            // it would let another program bind the port as this one
            // listens.
            mAcceptor->set_option(tcp::acceptor::reuse_address(true), ec);
        }
#endif
        if (!ec)
        {
            mAcceptor->bind(endpoint, ec);
        }
        if (!ec)
        {
            mAcceptor->listen(net::socket_base::max_listen_connections, ec);
        }
        if (ec)
        {
            beast::error_code ignored;
            mAcceptor->close(ignored);
        }
    }

    void accept()
    {
        mAcceptor->async_accept(net::make_strand(*mContext),
                                [this](beast::error_code ec, tcp::socket socket) { onAccept(ec, std::move(socket)); });
    }

    void onAccept(beast::error_code ec, tcp::socket socket)
    {
        if (mStopping)
        {
            return;
        }
        if (ec)
        {
            LL_WARNS("WebSocket") << name() << " failed to accept a connection: " << ec.message() << LL_ENDL;
            mRetry->expires_after(ACCEPT_RETRY);
            mRetry->async_wait(
                [this](beast::error_code ec)
                {
                    if (!ec && !mStopping)
                    {
                        accept();
                    }
                });
            return;
        }

        if (mSessions.size() >= MAX_SESSIONS)
        {
            // Closed unanswered: past the most at once, a connection more
            // is somebody holding the port rather than using it.
            LL_WARNS_ONCE("WebSocket") << name() << " refused a connection: " << MAX_SESSIONS << " are open already" << LL_ENDL;
            beast::error_code ignored;
            socket.close(ignored);
            accept();
            return;
        }

        auto session = std::make_shared<WSSession>(*this, std::move(socket));
        mSessions.push_back(session);
        session->run();
        accept();
    }

    void shutdown()
    {
        mStopping = true;
        beast::error_code ignored;
        mAcceptor->close(ignored);
        mRetry->cancel();
        if (mSessions.empty())
        {
            mContext->stop();
            return;
        }

        for (const auto& weak : mSessions)
        {
            if (auto session = weak.lock())
            {
                session->shutdown();
            }
        }
        mDeadline->expires_after(STOP_DEADLINE);
        mDeadline->async_wait(
            [this](beast::error_code ec)
            {
                if (!ec)
                {
                    LL_WARNS("WebSocket") << name() << " stopping with " << mSessions.size()
                                          << " connection(s) not closed in time" << LL_ENDL;
                    mContext->stop();
                }
            });
    }

    /// What the owner does with an event, so that an exception from it
    /// is logged rather than ending the session it came from.
    template <typename F>
    void guarded(const char* what, F&& f)
    {
        try
        {
            std::forward<F>(f)();
        }
        catch (...)
        {
            LOG_UNHANDLED_EXCEPTION(STRINGIZE(name() << " " << what));
        }
    }

    //-------------------------------------------
    LLWebsocketMgr::WSServer*             mOwner{ nullptr }; ///< Back-reference to the owning WSServer instance (guaranteed non-null)
    U16                                   mPort{ 0 };        ///< TCP port number the server listens on
    bool                                  mLocalOnly{ true }; ///< Whether to bind to localhost only (true) or all interfaces (false)
    std::atomic<bool>                     mRunning{ false }; ///< Whether the server's thread is running the context

    // Made by listen() and dropped by release(); the server's thread's in between.
    std::unique_ptr<net::io_context>      mContext;
    std::optional<tcp::acceptor>          mAcceptor;
    std::optional<net::steady_timer>      mRetry;    ///< The next accept, after one failed
    std::optional<net::steady_timer>      mDeadline; ///< The most stopping waits
    std::vector<std::weak_ptr<WSSession>> mSessions; ///< Every session not yet ended
    bool                                  mStopping{ false };
};

//------------------------------------------------------------------------
namespace
{
    void WSSession::run()
    {
        net::dispatch(mStrand, beast::bind_front_handler(&WSSession::readRequest, shared_from_this()));
    }

    bool WSSession::send(const std::string& message)
    {
        if (mState != LLWebsocketMgr::connection_open)
        {
            return false;
        }
        // Counted as it is taken, not as the strand queues it: a sender
        // faster than the strand would otherwise have everything it sent
        // held on its way to the queue, however far past the most it may.
        // A client that lets this much wait unsent has stopped reading.
        // One message alone may be larger: nothing else is waiting.
        const std::size_t bytes  = heldBytes(message);
        const std::size_t queued = mQueuedBytes.fetch_add(bytes);
        if (queued > 0 && queued + bytes > MAX_QUEUED_BYTES)
        {
            mQueuedBytes -= bytes;
            // Refused from here on, and dropped once however many senders
            // find it full.
            connection_state_t open = LLWebsocketMgr::connection_open;
            if (mState.compare_exchange_strong(open, LLWebsocketMgr::connection_closing))
            {
                net::post(mStrand,
                          [self = shared_from_this(), queued]()
                          {
                              if (!self->mFinished)
                              {
                                  LL_WARNS("WebSocket") << self->mServer.name() << " dropped a connection with " << queued
                                                        << " bytes unsent: its client is not reading" << LL_ENDL;
                                  self->finish();
                              }
                          });
            }
            return false;
        }
        Outgoing outgoing;
        outgoing.mText = message;
        net::post(mStrand, [self = shared_from_this(), outgoing = std::move(outgoing)]() mutable { self->enqueue(std::move(outgoing)); });
        return true;
    }

    bool WSSession::close(U16 code, const std::string& reason)
    {
        // Closing from here on, so that what is sent after is refused.
        connection_state_t open = LLWebsocketMgr::connection_open;
        if (!mState.compare_exchange_strong(open, LLWebsocketMgr::connection_closing))
        {
            return false;
        }
        Outgoing outgoing;
        outgoing.mClose  = true;
        outgoing.mReason = makeCloseReason(code, reason);
        net::post(mStrand, [self = shared_from_this(), outgoing = std::move(outgoing)]() mutable { self->enqueue(std::move(outgoing)); });
        return true;
    }

    void WSSession::shutdown()
    {
        net::post(mStrand,
                  [self = shared_from_this()]()
                  {
                      if (self->mFinished)
                      {
                          return;
                      }
                      if (self->mState == LLWebsocketMgr::connection_connecting)
                      {
                          // Nothing to close gracefully: whichever step it
                          // is on is cut short, and it ends there.
                          beast::get_lowest_layer(self->mStream).cancel();
                          return;
                      }
                      self->close(1001, "Server shutting down");
                  });
    }

    void WSSession::setMessageLimit(std::size_t bytes)
    {
        // At once where on the strand already -- as the connection opens --
        // so that the limit holds from the first message.
        net::dispatch(mStrand,
                      [self = shared_from_this(), bytes]()
                      {
                          self->mStream.read_message_max(bytes ? bytes : MAX_MESSAGE_BYTES);
                      });
    }

    void WSSession::readRequest()
    {
        mParser.emplace();
        mParser->header_limit(MAX_REQUEST_HEADER_BYTES);
        beast::get_lowest_layer(mStream).expires_after(REQUEST_TIMEOUT);
        http::async_read(mStream.next_layer(), mBuffer, *mParser,
                         beast::bind_front_handler(&WSSession::onRequest, shared_from_this()));
    }

    void WSSession::onRequest(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            LL_DEBUGS("WebSocket") << mServer.name() << " read no request: " << ec.message() << LL_ENDL;
            finish();
            return;
        }
        // A client sends nothing more until it has been answered.
        mBuffer.consume(mBuffer.size());
        mRequest = mParser->release();

        if (!websocket::is_upgrade(mRequest))
        {
            refuse(http::status::upgrade_required);
            return;
        }
        const std::string origin(mRequest[http::field::origin]);
        if (!mServer.acceptOrigin(origin))
        {
            // Warned of once, not once for each origin: whoever connects
            // can name as many as they like, and each would be kept.
            LL_WARNS_ONCE("WebSocket") << mServer.name() << " refused a connection from a web page" << LL_ENDL;
            LL_DEBUGS("WebSocket") << mServer.name() << " refused a connection from origin " << origin << LL_ENDL;
            refuse(http::status::forbidden);
            return;
        }

        // The stream's own timeouts from here: the handshake, the close,
        // and a ping for a client quiet for half the idle time, which is
        // dropped where it answers nothing by the end of it.
        beast::get_lowest_layer(mStream).expires_never();
        mStream.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        mStream.read_message_max(MAX_MESSAGE_BYTES);
        mStream.async_accept(mRequest, beast::bind_front_handler(&WSSession::onAccepted, shared_from_this()));
    }

    void WSSession::refuse(http::status status)
    {
        mRefusal.emplace(status, mRequest.version());
        mRefusal->set(http::field::content_type, "text/plain");
        if (status == http::status::upgrade_required)
        {
            // What it should have asked for.
            mRefusal->set(http::field::upgrade, "websocket");
        }
        mRefusal->keep_alive(false);
        mRefusal->body() = std::string(http::obsolete_reason(status));
        mRefusal->prepare_payload();
        http::async_write(mStream.next_layer(), *mRefusal,
                          [self = shared_from_this()](beast::error_code, std::size_t) { self->finish(); });
    }

    void WSSession::onAccepted(beast::error_code ec)
    {
        if (ec)
        {
            LL_DEBUGS("WebSocket") << mServer.name() << " did not open a connection: " << ec.message() << LL_ENDL;
            finish();
            return;
        }
        mState  = LLWebsocketMgr::connection_open;
        mOpened = true;
        mServer.opened(handle());
        readMessage();
    }

    void WSSession::readMessage()
    {
        mStream.async_read(mBuffer, beast::bind_front_handler(&WSSession::onMessage, shared_from_this()));
    }

    void WSSession::onMessage(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            if (ec != websocket::error::closed)
            {
                LL_DEBUGS("WebSocket") << mServer.name() << " connection ended: " << ec.message() << LL_ENDL;
            }
            finish();
            return;
        }
        // Whole, however many frames it came in. Binary messages are
        // taken as text too, as they always have been.
        const std::string message = beast::buffers_to_string(mBuffer.data());
        mBuffer.consume(mBuffer.size());
        mServer.message(handle(), message);
        // Reading on while closing too: the client's close comes this way.
        readMessage();
    }

    void WSSession::enqueue(Outgoing&& outgoing)
    {
        if (mFinished || mCloseQueued)
        {
            if (!outgoing.mClose)
            {
                mQueuedBytes -= heldBytes(outgoing.mText);
            }
            return;
        }
        if (outgoing.mClose)
        {
            mCloseQueued = true;
            // However long the messages before it take to go, or the
            // client to answer it: past the deadline, the connection is
            // dropped.
            mCloseDeadline.expires_after(CLOSE_DEADLINE);
            mCloseDeadline.async_wait(
                [self = shared_from_this()](beast::error_code ec)
                {
                    if (!ec && !self->mFinished)
                    {
                        LL_WARNS("WebSocket") << self->mServer.name() << " dropped a connection whose close did not complete in time" << LL_ENDL;
                        self->finish();
                    }
                });
        }
        mQueue.push_back(std::move(outgoing));
        if (!mWriting)
        {
            writeNext();
        }
    }

    void WSSession::writeNext()
    {
        if (mQueue.empty() || mFinished)
        {
            mWriting = false;
            return;
        }
        mWriting = true;
        const Outgoing& next = mQueue.front();
        if (next.mClose)
        {
            mStream.async_close(next.mReason, beast::bind_front_handler(&WSSession::onClosed, shared_from_this()));
        }
        else
        {
            mStream.async_write(net::buffer(next.mText), beast::bind_front_handler(&WSSession::onWritten, shared_from_this()));
        }
    }

    void WSSession::onWritten(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            // The read says why, and ends the session.
            LL_DEBUGS("WebSocket") << mServer.name() << " failed to send a message: " << ec.message() << LL_ENDL;
            clearQueue();
            mWriting = false;
            return;
        }
        mQueuedBytes -= heldBytes(mQueue.front().mText);
        mQueue.pop_front();
        writeNext();
    }

    void WSSession::onClosed(beast::error_code ec)
    {
        if (ec)
        {
            LL_DEBUGS("WebSocket") << mServer.name() << " close did not complete: " << ec.message() << LL_ENDL;
        }
        clearQueue();
        mWriting = false;
        finish();
    }

    // Only what it holds: sends taken meanwhile are still on their way.
    void WSSession::clearQueue()
    {
        for (const Outgoing& outgoing : mQueue)
        {
            if (!outgoing.mClose)
            {
                mQueuedBytes -= heldBytes(outgoing.mText);
            }
        }
        mQueue.clear();
    }

    // Once, however it ended. A write still in flight keeps its message
    // until it completes: the socket may be reading it until then.
    void WSSession::finish()
    {
        if (mFinished)
        {
            return;
        }
        mFinished = true;
        mState    = LLWebsocketMgr::connection_closed;
        mCloseDeadline.cancel();
        beast::get_lowest_layer(mStream).close();
        if (mOpened)
        {
            mServer.closed(handle());
        }
        mServer.sessionEnded(this);
    }
}

//------------------------------------------------------------------------
LLWebsocketMgr::WSServer::WSServer(std::string_view name, U16 port, bool local_only):
    mServerName(name),
    mImpl(std::make_unique<Server_impl>(this, port, local_only))
{
    LL_INFOS("WebSocket") << "Creating WebSocket server: " << name <<
        ", to listen " << (local_only ? "locally" : "ON ALL INTERFACES") <<
        " on port " << port << LL_ENDL;
}

LLWebsocketMgr::WSServer::~WSServer()
{
    // Ensure the server is stopped before destruction
    stop();
}

LLWebsocketMgr::WSConnection::ptr_t LLWebsocketMgr::WSServer::connectionFactory(LLWebsocketMgr::WSServer::ptr_t server,
    LLWebsocketMgr::connection_h handle)
{
    return std::make_shared<LLWebsocketMgr::WSConnection>(server, handle);
}

bool LLWebsocketMgr::WSServer::start()
{
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;

    LLMutexLock lock(&mThreadMutex);

    // Check if already running
    if (isRunning())
    {
        LL_WARNS("WebSocket") << "Server " << mServerName << " is already running" << LL_ENDL;
        return false;
    }
    if (mServerThread.joinable())
    {
        // A thread that ended of its own accord: nothing left to wait on.
        mServerThread.join();
        mImpl->release();
    }

    // The port is had here, on the caller's thread, so that one another
    // program holds fails this call rather than the server's thread.
    if (!mImpl->listen())
    {
        return false;
    }

    // Reset the stop flag
    mShouldStop = false;
    mImpl->mRunning = true;

    // Start the server thread
    mServerThread = std::thread([this]() {
        set_thread_fp_mode();
        LL_PROFILER_SET_THREAD_NAME(mServerName.c_str());
        LL_INFOS("WebSocket") << "WebSocket server thread starting for: " << mServerName << LL_ENDL;
        mImpl->run();
        LL_INFOS("WebSocket") << "WebSocket server thread exiting for: " << mServerName << LL_ENDL;
    });

    onStarted();
    LL_INFOS("WebSocket") << "Started WebSocket server thread: " << mServerName << LL_ENDL;
    return true;
}

void LLWebsocketMgr::WSServer::stop()
{
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;

    std::thread thread;
    {
        LLMutexLock lock(&mThreadMutex);

        // Check if already stopped
        if (!mServerThread.joinable())
        {
            return;
        }

        LL_INFOS("WebSocket") << "Stopping WebSocket server: " << mServerName << LL_ENDL;

        mShouldStop = true;

        // Close frames to every client, then the listener and whatever is
        // still being opened; the server's thread ends once they are done.
        closeAllConnections(1001, "Server shutting down");
        mImpl->stop();
        thread = std::move(mServerThread);
    } // Release the lock here

    thread.join();
    LL_INFOS("WebSocket") << "WebSocket server thread joined for: " << mServerName << LL_ENDL;

    // A connection whose close did not finish by the deadline is closed
    // here, so that every one that opened hears that it closed.
    std::vector<connection_h> left;
    {
        LLMutexLock lock(&mConnectionMutex);
        for (const auto& [handle, connection] : mConnections)
        {
            left.push_back(handle);
        }
    }
    for (const auto& handle : left)
    {
        handleCloseConnection(handle);
    }

    mImpl->release();
    onStopped();
}

bool LLWebsocketMgr::WSServer::isRunning() const
{
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;

    // The thread running the server's context, with no stop asked for.
    return mImpl->mRunning && !mShouldStop;
}

void LLWebsocketMgr::WSServer::broadcastMessage(const std::string& message)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;
    LLMutexLock lock(&mConnectionMutex);
    for (const auto& [handle, conn] : mConnections)
    {
        sendMessageTo(handle, message);
    }
}

bool LLWebsocketMgr::WSServer::sendMessageTo(const connection_h& handle, const std::string& message)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;
    auto session = sessionFor(handle);
    if (!session || !session->send(message))
    {
        LL_WARNS("WebSocket") << mServerName << " failed to send message: the connection is not open" << LL_ENDL;
        return false;
    }
    return true;
}

bool LLWebsocketMgr::WSServer::closeConnection(const connection_h& handle, U16 code, const std::string& reason)
{
    LL_ERRS_IF(!mImpl, "WebSocket") << "WebSocket server " << mServerName << " implementation is null !" << LL_ENDL;

    auto session = sessionFor(handle);
    if (!session || !session->close(code, reason))
    {
        LL_WARNS("WebSocket") << mServerName << " failed to close connection: it is not open" << LL_ENDL;
        return false;
    }

    LL_INFOS("WebSocket") << mServerName << " initiated close for connection with code "
                          << code << " and reason: " << reason << LL_ENDL;
    return true;
}

LLWebsocketMgr::WSConnection::ptr_t LLWebsocketMgr::WSServer::getConnection(const connection_h& handle)
{
    LLMutexLock lock(&mConnectionMutex);
    auto        it = mConnections.find(handle);
    if (it != mConnections.end())
    {
        return it->second;
    }
    return nullptr;
}

void LLWebsocketMgr::WSServer::setMessageLimit(const connection_h& handle, size_t bytes)
{
    if (auto session = sessionFor(handle))
    {
        session->setMessageLimit(bytes);
    }
}

LLWebsocketMgr::connection_state_t LLWebsocketMgr::WSServer::getConnectionState(const connection_h& handle) const
{
    // A session gone is a connection closed: its handle outlives it.
    auto session = sessionFor(handle);
    return session ? session->state() : connection_closed;
}

void LLWebsocketMgr::WSServer::handleOpenConnection(const connection_h& handle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    WSConnection::ptr_t connection;
    size_t              size(0);
    {
        LLMutexLock lock(&mConnectionMutex);
        auto        it = mConnections.find(handle);
        if (it == mConnections.end())
        {
            connection = connectionFactory(shared_from_this(), handle);
            if (!connection)
            {
                LL_WARNS("WebSocket") << "Failed to create connection for websocket server " << mServerName << LL_ENDL;
                return;
            }
            mConnections[handle] = connection;
        }
        else
        {
            connection = it->second;
        }

        if (!connection)
        {
            LL_WARNS("WebSocket") << mServerName << " failed to create connection object" << LL_ENDL;
            return;
        }
        // Removed redundant assignment to mConnections[handle]
        size = mConnections.size();
    }

    onConnectionOpened(connection); // TODO: consider letting the server reject the connection here
    connection->onOpen();
    LL_INFOS("WebSocket") << mServerName << " opened new connection, total connections: " << size << LL_ENDL;
}

void LLWebsocketMgr::WSServer::handleCloseConnection(const connection_h& handle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    size_t              size(0);
    WSConnection::ptr_t connection;
    {
        LLMutexLock lock(&mConnectionMutex);
        auto        it = mConnections.find(handle);
        if (it != mConnections.end())
        {
            connection = it->second;
            mConnections.erase(it);
        }
        size = mConnections.size();
    }
    if (connection)
    {
        connection->onClose();
        onConnectionClosed(connection);
        LL_INFOS("WebSocket") << mServerName << " closed connection, total connections: " << size << LL_ENDL;
    }
    else
    {
        LL_WARNS("WebSocket") << mServerName << " attempted to close unknown connection" << LL_ENDL;
    }
}

void LLWebsocketMgr::WSServer::handleMessage(const connection_h& handle, const std::string& message)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_WEBSOCKET;
    WSConnection::ptr_t connection = getConnection(handle);
    if (connection)
    {
        connection->onMessage(message);
    }
    else
    {
        LL_WARNS("WebSocket") << mServerName << " received message for unknown connection" << LL_ENDL;
    }
}

//------------------------------------------------------------------------
bool LLWebsocketMgr::WSConnection::sendMessage(const std::string& message) const
{
    if (mOwningServer.expired())
    {
        LL_WARNS("WebSocket") << "Attempted to send message on connection with null server reference" << LL_ENDL;
        return false;
    }
    return mOwningServer.lock()->sendMessageTo(mConnectionHandle, message);
}

bool LLWebsocketMgr::WSConnection::sendMessage(const LLSD& data) const
{
    return sendMessage(LlsdToJson(data));
}

void LLWebsocketMgr::WSConnection::closeConnection(U16 code, const std::string& reason)
{
    if (mOwningServer.expired())
    {
        LL_WARNS("WebSocket") << "Attempted to close connection with null server reference" << LL_ENDL;
        return;
    }

    LL_INFOS("WebSocket") << "WSConnection closing connection with code " << code
                          << " and reason: " << (reason.empty() ? "(no reason)" : reason) << LL_ENDL;

    if (!mOwningServer.lock()->closeConnection(mConnectionHandle, code, reason))
    {
        LL_WARNS("WebSocket") << "Failed to close connection through server" << LL_ENDL;
    }
}

void LLWebsocketMgr::WSConnection::setMessageLimit(size_t bytes) const
{
    if (auto server = mOwningServer.lock())
    {
        server->setMessageLimit(mConnectionHandle, bytes);
    }
}

bool LLWebsocketMgr::WSConnection::isConnected() const
{
    if (mOwningServer.expired())
    {
        return false;
    }

    LLWebsocketMgr::WSServer::ptr_t server = mOwningServer.lock();
    if (!server)
    {
        return false;
    }
    return server->getConnectionState(mConnectionHandle) == connection_open;
}

LLWebsocketMgr::WSConnection::ptr_t LLWebsocketMgr::WSConnection::getSelfPtr()
{
    auto server = mOwningServer.lock();
    if (!server) return nullptr;
    return server->getConnection(mConnectionHandle);
}

void LLWebsocketMgr::WSServer::closeAllConnections(U16 code, const std::string& reason)
{
    std::vector<connection_h> handles;
    {
        LLMutexLock lock(&mConnectionMutex);
        for (const auto& [handle, conn] : mConnections)
        {
            handles.push_back(handle);
        }
    }
    for (const auto& handle : handles)
    {
        closeConnection(handle, code, reason);
    }
}
