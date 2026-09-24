/**
 * @file llscripteditorws.h
 * @brief WebSocket server and connection classes for external script editor integration
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

#pragma once

#include "lljsonrpcws.h"
#include "alscriptworkspace.h"
#include "llpublishedobjectmgr.h"
#include "llsd.h"
#include "lluuid.h"
#include "llhandle.h"
#include "lltimer.h"
#include "lleventtimer.h"

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "llmutex.h"

#include <atomic>
#include <functional>

// Forward declarations
class LLLiveLSLEditor;
class LLScriptEditorWSServer;
class LLPanel;
class LLViewerObject;
class LLInventoryItem;

class LLScriptEditorWSConnection : public LLJSONRPCConnection, public std::enable_shared_from_this<LLScriptEditorWSConnection>
{
public:
    using ptr_t  = std::shared_ptr<LLScriptEditorWSConnection>;
    using wptr_t = std::weak_ptr<LLScriptEditorWSConnection>;

    enum class DisconnectReason : S32
    {
        NORMAL         = 0,
        EDITOR_CLOSED  = 1,
        PROTOCOL_ERROR = 2,
        TIMEOUT        = 3,
        INTERNAL_ERROR = 4
    };

    LLScriptEditorWSConnection(const LLWebsocketMgr::WSServer::ptr_t server, const LLWebsocketMgr::connection_h& handle) :
        LLJSONRPCConnection(server, handle)
    {
        // Anything that can reach the port can connect: nothing it asks
        // is answered until it has proven it can read the user's files.
        setAuthenticated(false);

        // Reserve id 0 as the "unassigned" sentinel used by EditorSubscription;
        // on wrap, skip past it.
        U32 id;
        do
        {
            id = sNextConnectionID.fetch_add(1, std::memory_order_relaxed);
        }
        while (id == 0);
        mConnectionID = id;
    }

    ~LLScriptEditorWSConnection() override = default;

    U32 getConnectionID() const { return mConnectionID; }

    // Connection lifecycle overrides
    void onOpen() override;
    void onClose() override;

    void sendDisconnect(DisconnectReason reason = DisconnectReason::NORMAL, const std::string& message = "Goodbye");
    bool hasFeature(const std::string& feature) const;

private:
    using string_set_t = std::set<std::string>;

    // What the client proves itself with: a secret written to a file only
    // the user can read, under a name that says nothing of it. The client
    // is told where the file is and answers with what it says.
    struct Challenge
    {
        LLUUID      mSecret;
        std::string mFile;
    };
    // Empty where no secret could be made or written.
    static Challenge writeChallenge();
    // Who the viewer is and what the client must answer, on the main
    // thread.
    void sendHandshake();

    /**
     * @brief Handle the handshake response from the client
     * @param result The response data from the client containing client information
     * @param secret What the challenge file said, which the client must answer with
     * @param agent_id Who is logged in, told with session.ok
     * @param agent_name Their username, told with it
     */
    void handleHandshakeResponse(const LLSD& result, const LLUUID& secret, const LLUUID& agent_id, const std::string& agent_name);
    // The handshake refused, unanswered in time, or never sent.
    void handleHandshakeError(const LLSD& error);

    std::shared_ptr<LLScriptEditorWSServer> getServer() const;

    U32 mConnectionID{ 0 }; ///< Unique identifier for this connection

    // Client handshake response data, written as the client answers, on
    // the server's thread, and read on the main one: under the lock.
    mutable LLMutex mHandshakeMutex;
    std::string  mClientName;      ///< Name of the external editor client
    std::string  mClientVersion;   ///< Version of the external editor client
    std::string  mProtocolVersion; ///< JSON-RPC protocol version supported by client
    std::string  mScriptName;      ///< Name of the script being edited
    std::string  mScriptLanguage;  ///< Programming language of the script (lsl, luau, etc.)
    string_set_t mLanguages;       ///< Set of supported scripting languages
    string_set_t mFeatures;        ///< Active client features (live_sync, compilation, etc.)

    static std::atomic<U32> sNextConnectionID;
};

/**
 * @class LLScriptEditorWSServer
 * @brief JSON-RPC 2.0 WebSocket server for external script editor integration
 *
 * This server extends the JSON-RPC server to provide specialized functionality
 * for external script editor integration. It manages WebSocket connections from
 * external script editors and provides a structured JSON-RPC 2.0 interface
 * between the Second Life viewer's script editing functionality and external
 * development tools.
 *
 * ## Architecture
 *
 * The server acts as a JSON-RPC communication hub between:
 * - LLLiveLSLEditor instances (in-world script editing)
 * - External script editors (VS Code, Atom, Sublime Text, etc.)
 * - Script compilation and save services
 *
 * ## Usage
 *
 * @code
 * // Start the server, or find it running; null where it is disabled or
 * // could not start
 * LLScriptEditorWSServer::ptr_t server = LLScriptEditorWSServer::ensureServerRunning();
 *
 * // Hold a script open in a viewer editor for live sync with a client
 * const std::string script_id = LLScriptEditorWSServer::buildScriptSubscriptionId(object_id, item_id);
 * server->subscribeScriptEditor(object_id, item_id, script_name, editor_handle, script_id);
 * @endcode
 *
 * ## Security Considerations
 *
 * - Server binds to localhost only: a client anywhere else could never read
 *   the challenge's file
 * - A connection from a browser -- any upgrade request with an Origin -- is
 *   refused, so no web page, the viewer's own included, can reach it
 * - Nothing a client asks is answered, and nothing is sent it but the
 *   handshake, until it has answered the handshake's challenge with what a
 *   file only the user can read says; a wrong answer, or none within 30
 *   seconds, closes it. Who is logged in is told with session.ok, once
 *   it has
 * - A few connections at a time may be proving themselves; one more is
 *   closed (1013) before it is sent a challenge
 * - JSON-RPC 2.0 structured protocol with validation
 * - Error handling with standardized JSON-RPC error codes
 * - Nothing limits how often a client asks: an authenticated client is
 *   the user's own
 */
class LLScriptEditorWSServer : public LLJSONRPCServer
{
public:
    struct ItemRef
    {
        LLUUID      mRootID;
        LLUUID      mPrimID;
        LLUUID      mItemID;
        std::string mScriptName;
    };

    static constexpr U32 ALL_CONNECTIONS = 0xFFFFFFFF;
    enum class SubscriptionError
    {
        SUCCESS = 0,
        INVALID_EDITOR,
        INVALID_SUBSCRIPTION,
        ALREADY_SUBSCRIBED,
        INTERNAL_ERROR
    };

    static constexpr const char* DEFAULT_SERVER_NAME = "script_editor_server";
    static constexpr U16         DEFAULT_SERVER_PORT = 9020;

    using ptr_t = std::shared_ptr<LLScriptEditorWSServer>;
    using wptr_t = std::weak_ptr<LLScriptEditorWSServer>;

    LLScriptEditorWSServer(const std::string& name, U16 port, bool local_only = true);

    ~LLScriptEditorWSServer() override = default;

    static LLScriptEditorWSServer::ptr_t getServer();
    static LLScriptEditorWSServer::ptr_t ensureServerRunning();
    static std::string                   buildScriptSubscriptionId(const LLUUID& object_id,
                                                                   const LLUUID& item_id);
    static std::string                   buildVSCodeURI(const LLUUID& object_id = LLUUID::null,
                                                        const LLUUID& script_id = LLUUID::null);
    static bool                          launchVSCode(const LLUUID& object_id = LLUUID::null,
                                                      const LLUUID& script_id = LLUUID::null);

    void onStarted() override;
    void onStopped() override;
    // False once nobody has been connected for the idle timeout, which
    // stops the server.
    bool update() override;
    void onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection) override;
    void onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t& connection) override;

    // A viewer editor holding a script open for live sync, by the
    // subscription id; `lua` says which compiler's words to expect in
    // its results.
    bool subscribeScriptEditor(const LLUUID& object_id, const LLUUID& item_id, std::string_view script_name,
        const LLHandle<LLPanel>& editor_handle, const std::string &script_id, bool lua = false);
    void unsubscribeEditor(const std::string &script_id);

    void notifyScript(const std::string& script_id, const std::string& method, const LLSD& message) const;
    void sendUnsubscribeScriptEditor(const std::string& script_id);
    // What the compiler said of a script, as the legacy editors report
    // it: the raw response with `compiled`, `is_running` and `errors`.
    void sendCompileResults(const std::string& script_id, const LLSD& results) const;

    LLHandle<LLPanel> findEditorForScript(const std::string& script_id) const;

    std::set<std::string> getActiveScripts() const;

    // --- Object Content Publishing ---
    bool publishObject(const LLUUID& object_id);
    void unpublishObject(const LLUUID& object_id, const std::string& reason = "");
    bool isObjectPublished(const LLUUID& object_id) const;

    // The world's way in to the publishing, which the manager does.
    void onPrimInventoryReady(const LLUUID& object_id, const LLUUID& prim_id);
    void onPrimInventoryChanged(const LLUUID& object_id, const LLUUID& prim_id);
    void onObjectPropertyChanged(const LLUUID& prim_id, const std::string& name, const std::string& desc, S16 inventory_serial = -1);
    void onLinksetChildAdded(const LLUUID& root_id, LLViewerObject* child);
    void onLinksetChildRemoved(const LLUUID& root_id, const LLUUID& child_id);

    static bool isEnabled();
    static bool isTightIntegration();

    // Connections open and not yet authenticated. Any thread's.
    size_t unauthenticatedConnectionCount() const;

protected:
    LLWebsocketMgr::WSConnection::ptr_t connectionFactory(LLWebsocketMgr::WSServer::ptr_t server,
                                                         LLWebsocketMgr::connection_h handle) override;

    void setupConnectionMethods(LLJSONRPCConnection::ptr_t connection) override;
    LLSD handlePing(const LLJSONRPCConnection::ptr_t& connection,
                    const LLSD& params) const override;
    LLSD handleGetVersion(const LLJSONRPCConnection::ptr_t& connection,
                          const LLSD& params) const override;

    void broadcastLanguageChange();

    LLSD handleLanguageIdRequest() const;
    LLSD handleSyntaxRequest(const LLSD &params) const;
    LLSD handleSyntaxCacheRequest() const;
    LLSD handleSyntaxCacheFileRequest(const LLSD& params) const;
    LLSD handleScriptSubscribe(U32 connection_id, const LLSD& params);
    LLSD handleScriptUnsubscribe(U32 connection_id, const LLSD& params);
    LLSD handleFileWatcherFileListRequest() const;
    LLSD handleObjectRequest(U32 connection_id, const LLSD& params);
    LLSD handleObjectContentGet(const std::string& method, const LLSD& id, const LLSD& params);
    LLSD handleObjectContentSave(const std::string& method, const LLSD& id, const LLSD& params);
    LLSD saveScript(LLViewerObject* prim, LLInventoryItem* item, const std::string& content, const LLSD& params);
    LLSD saveNotecard(LLViewerObject* prim, LLInventoryItem* item, const std::string& content);
    LLSD handleObjectItemDelete(U32 connection_id, const LLSD& params);
    LLSD handleObjectItemCreate(const std::string& method, const LLSD& id, const LLSD& params);
    LLSD handleObjectUnpublish(U32 connection_id, const LLSD& params);
    LLSD handleObjectList() const;
    LLSD handleObjectScriptSetRunning(U32 connection_id, const LLSD& params);
    LLSD handleObjectScriptReset(U32 connection_id, const LLSD& params);
    LLSD handleObjectScriptResetAll(U32 connection_id, const LLSD& params);
    LLSD handleObjectScriptRecompileAll(U32 connection_id, const LLSD& params);
    LLSD handleObjectModify(U32 connection_id, const LLSD& params);
    LLSD handleObjectItemModify(U32 connection_id, const LLSD& params);
    LLSD handleCommandExecute(U32 connection_id, const LLSD& params);
    LLSD handleSaveBackToObjectContents(U32 connection_id, const LLSD& params);
    LLSD handleCommandList();
    void sendCommandExecute(U32 connection_id, const std::string& command, const LLSD& params);

    struct ValidatedItem
    {
        LLViewerObject*    prim{ nullptr };
        LLViewerObject*    root{ nullptr };
        LLInventoryItem*   item{ nullptr };
        LLAssetType::EType type{ LLAssetType::AT_NONE };
    };
    ValidatedItem validatePublishedItem(const LLSD& params, U32 permMask) const;

    // --- Object Content Publishing (helpers) ---
    static std::string getPrimName(LLViewerObject* obj);
    void notifyConnection(U32 connection_id, const std::string& method, const LLSD& params) const;
    void notifyAll(const std::string& method, const LLSD& params) const;

    /// Wraps `fn` in a MethodHandler with a weak-ptr guard on this server,
    /// so the handler safely no-ops after server shutdown. `fn` is called
    /// with (LLScriptEditorWSServer&, method, id, params) and returns LLSD.
    template <typename Fn>
    LLJSONRPCConnection::MethodHandler bindHandler(Fn fn)
    {
        std::weak_ptr<LLWebsocketMgr::WSServer> weak_base = weak_from_this();
        return [weak_base, fn = std::move(fn)]
               (const std::string& method, const LLSD& id, const LLSD& params) -> LLSD
        {
            auto base = weak_base.lock();
            if (!base)
            {
                return LLSD();
            }
            auto server = std::static_pointer_cast<LLScriptEditorWSServer>(base);
            return fn(*server, method, id, params);
        };
    }

private:
    // What a script said, from the workspace, to whoever published its
    // object or subscribed to it.
    void sendRuntimeEvent(const ALScriptWorkspace::RuntimeEvent& event) const;
    // What the compiler said of a script saved through the workspace --
    // by Script Studio, the compile queue, anything but a client's own
    // object.content.save, which is answered inline -- to the connection
    // subscribed to it, else to everyone with its object published; and
    // the prim's inventory fetched again, so that the object.update
    // that follows carries the item's new revision.
    void sendCompiled(const ALScriptWorkspace::CompileResult& result);
    // The script.compiled message for a result, in the protocol's terms.
    static LLSD compiledMessage(const std::string& script_id, bool success, bool running,
                                const std::vector<ALScriptWorkspace::Diagnostic>& diagnostics, bool lua);

    struct EditorSubscription
    {
        EditorSubscription(const ItemRef& item_ref, LLHandle<LLPanel> editor_handle, bool lua):
            mItemRef(item_ref),
            mEditorHandle(editor_handle),
            mLua(lua)
        {
        }
        U32 mConnectionID{ 0 };
        ItemRef mItemRef;
        LLScriptEditorWSConnection::wptr_t mConnection;
        LLHandle<LLPanel> mEditorHandle;
        bool mLua{ false };
    };
    using subscriptions_t = std::unordered_map<std::string, EditorSubscription>;

    SubscriptionError updateScriptSubscription(const std::string &script_id, U32 connection_id);
    void                unsubscribeConnection(U32 connection_id);

    subscriptions_t mSubscriptions;
    std::unordered_map<U32, S32> mConnectionSubscriptionCounts;
    // Written on the server's thread as connections come and go, read on
    // the main one as messages go out: under the lock either way.
    mutable LLMutex                                   mConnectionsMutex;
    std::map<U32, LLScriptEditorWSConnection::wptr_t> mActiveConnections;

    // The manager sends through the server and names prims as it does.
    friend class LLPublishedObjectMgr;
    mutable LLPublishedObjectMgr mPublishedObjectManager;
    // Since when nobody has been connected -- the server's start, or the
    // last client's going -- in the timer's seconds; zero while one is.
    std::atomic<F64>             mIdleSince{ 0.0 };
    boost::signals2::scoped_connection mRuntimeConnection;
    boost::signals2::scoped_connection mCompiledConnection;

    struct WSCommandInfo
    {
        std::string command;
        std::string description;
        LLSD params;
    };
    enum WSCommandError
    {
        UnknownCommand  = 1,
        InvalidParams   = 2,
        NotPermitted    = 3,
        ExecutionError  = 4,
    };
    using WSCommandHandler = std::function<LLSD(U32 connection_id, const LLSD& params)>;
    std::unordered_map<std::string, std::pair<WSCommandInfo, WSCommandHandler>> mCommandRegistry;

    void registerCommand(const WSCommandInfo& info, WSCommandHandler handler);

    boost::signals2::connection mLanguageChangeSignal;
    LLUUID mLastSyntaxId;

};
