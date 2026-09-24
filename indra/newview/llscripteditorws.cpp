/**
 * @file llscripteditorws.cpp
 * @brief JSON-RPC 2.0 WebSocket server implementation for external script editor integration
 *
 * For a full description of the JSON-RPC protocol and all supported methods,
 * see doc/external-editor-json-rpc.md in the repository root.
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

#include "llviewerprecompiledheaders.h"
#include "llscripteditorws.h"

#include "alfloaterscriptstudio.h"
#include "alscriptworkspace.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "llcompilequeue.h"
#include "llinventorymodel.h"
#include "lldate.h"
#include "llerror.h"
#include "lleventcoro.h"
#include "lleventfilter.h"
#include "llevents.h"
#include "llfilesystem.h"
#include "llfloaterperms.h"
#include "llfloaterreg.h"
#include "llinventorytype.h"
#include "llinventorydefines.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llpreviewnotecard.h"
#include "llpreviewscript.h"
#include "llprocess.h"
#include "alregex.h"
#include "llmd5.h"
#include "llsdjson.h"
#include "llselectmgr.h"
#include "lltrans.h"
#include "lluuid.h"
#include "llversioninfo.h"
#include "llviewerassetstorage.h"
#include "llviewerassettype.h"
#include "llviewerassetupload.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewermenu.h"
#include "llviewertexteditor.h"
#include "llvoinventorylistener.h"
#include "roles_constants.h"

#include <array>

#include <openssl/rand.h>

namespace
{
    // Per-operation timeouts (seconds) for coroutine-based async RPC handlers.
    constexpr F32 ASSET_FETCH_TIMEOUT     = 30.0f;
    constexpr F32 SCRIPT_UPLOAD_TIMEOUT   = 60.0f;
    constexpr F32 NOTECARD_UPLOAD_TIMEOUT = 30.0f;
    constexpr F32 ITEM_CREATE_TIMEOUT     = 30.0f;

    // Seconds a client has to answer the handshake: plenty to read a
    // file, and no longer than that is a connection kept that has not
    // proven itself.
    constexpr F64 HANDSHAKE_TIMEOUT = 30.0;

    // How many connections may be proving themselves at once. A client
    // answers in a moment; past these, a connection more is closed before
    // it is sent a challenge, and nothing is written for it.
    constexpr size_t MAX_UNAUTHENTICATED = 4;

    static const ALRegex LUAU_LOCATION_PATTERN(
        R"(^([^:]*):([0-9]+):\s*(.*)$)");

    static const ALRegex LSL_LOCATION_PATTERN(
        R"(\((\d+), (\d+)\) : ([^:]+) : (.+))");

    // Creates a uniquely-named LLEventMailDrop under "<prefix>.<uuid>", passes
    // its name to kickoff (which arranges for one post to that pump), then
    // suspends the current coroutine up to 	imeout seconds for the result.
    // Throws RequestTimeoutError(timeout_msg) if the deadline elapses.
    template <typename Kickoff>
    LLSD await_async_result(const std::string& pump_prefix,
                            F32 timeout,
                            const std::string& timeout_msg,
                            Kickoff&& kickoff)
    {
        LLEventMailDrop pump(pump_prefix + "." + LLUUID::generateNewID().asString(), true);
        std::string pump_name = pump.getName();
        std::forward<Kickoff>(kickoff)(pump_name);
        LLSD result = llcoro::suspendUntilEventOnWithTimeout(
            pump, timeout, LLSD().with("timeout", true));
        if (result.has("timeout"))
        {
            throw LLJSONRPCConnection::RequestTimeoutError(timeout_msg);
        }
        return result;
    }

    // Builds the (success, failure) callback pair used by LLResourceUploadInfo-
    // derived uploads. Both outcomes post a single LLSD to pump_name:
    //   - success: the server's response LLSD with item_id/task_id added.
    //   - failure: { "failed": true, "reason": <reason> }.
    auto make_asset_upload_callbacks(const std::string& pump_name)
    {
        auto on_success = [pump_name](LLUUID item_id, LLUUID task_id, LLUUID new_asset_id, LLSD response)
        {
            response["item_id"]      = item_id;
            response["task_id"]      = task_id;
            response["new_asset_id"] = new_asset_id;
            LLEventPumps::instance().post(pump_name, response);
        };
        auto on_failure = [pump_name](LLUUID /*item_id*/, LLUUID /*task_id*/, LLSD /*response*/, std::string reason)
        {
            LLSD failure;
            failure["failed"] = true;
            failure["reason"] = reason;
            LLEventPumps::instance().post(pump_name, failure);
            return false;
        };
        return std::make_pair(std::move(on_success), std::move(on_failure));
    }

    // Returns the value of NV pair key on obj as a string, or empty if
    // obj / pair / string is null or empty. NUL-safe.
    std::string nv_string(LLViewerObject* obj, const char* key)
    {
        if (!obj)
        {
            return std::string();
        }
        LLNameValue* nv = obj->getNVPair(key);
        if (!nv)
        {
            return std::string();
        }
        const char* s = nv->getString();
        if (!s || s[0] == '\0')
        {
            return std::string();
        }
        return std::string(s);
    }

    LLSD object_id_command_params()
    {
        LLSD params(LLSD::emptyMap());
        params["object_id"]["type"] = "string";
        params["object_id"]["required"] = true;
        return params;
    }

    // An item's asset as it stands, fetched through its prim's region, or
    // read from the cache where it is there. Throws as the fetch fails.
    // Runs in a coroutine: the prim and the item are used before it
    // suspends, and not after.
    std::string fetch_item_asset(LLViewerObject* prim, LLInventoryItem* item, LLAssetType::EType type);

    // The region a prim is in, which its messages and uploads go to. One
    // whose region has gone -- disconnected, or crossing -- is out of reach
    // for now; asked before any message is begun, since a throw part way
    // would leave the message system holding half of one.
    LLViewerRegion* region_of(const LLViewerObject* prim)
    {
        LLViewerRegion* region = prim->getRegion();
        if (!region)
        {
            throw LLJSONRPCConnection::ServiceUnavailableError("The object's region is not connected");
        }
        return region;
    }

    // The object a command names, in view and in a published linkset.
    // Publishing scopes what a client works on; it does not keep anything
    // from the client, which can publish any object the agent may modify
    // with object.request. An authenticated client acts as the user.
    LLViewerObject* published_object(const LLScriptEditorWSServer& server, const LLSD& params)
    {
        const LLUUID object_id = params["object_id"].asUUID();
        if (object_id.isNull())
        {
            throw LLJSONRPCConnection::InvalidParams("object_id is required");
        }
        LLViewerObject* object = gObjectList.findObject(object_id);
        if (!object)
        {
            throw LLJSONRPCConnection::InvalidParams("object_id not found");
        }
        LLViewerObject* root = object->getRootEdit();
        if (!root || !server.isObjectPublished(root->getID()))
        {
            throw LLJSONRPCConnection::ForbiddenError("Object is not published");
        }
        return object;
    }

    std::string fetch_item_asset(LLViewerObject* prim, LLInventoryItem* item, LLAssetType::EType type)
    {
        LLSD cb_result = await_async_result(
            "objectContentGet", ASSET_FETCH_TIMEOUT, "Asset fetch timed out",
            [&](const std::string& pump_name)
            {
                gAssetStorage->getInvItemAsset(
                    region_of(prim)->getHost(),
                    gAgent.getID(),
                    gAgent.getSessionID(),
                    item->getPermissions().getOwner(),
                    prim->getID(),
                    item->getUUID(),
                    item->getAssetUUID(),
                    type,
                    [pump_name](const LLUUID& asset_uuid, LLAssetType::EType asset_type, void*, S32 status, LLExtStat)
                    {
                        LLSD result;
                        if (status == LL_ERR_NOERR)
                        {
                            result["asset_uuid"] = asset_uuid;
                            result["asset_type"] = static_cast<S32>(asset_type);
                        }
                        else
                        {
                            result["error"] = status;
                        }
                        LLEventPumps::instance().post(pump_name, result);
                    },
                    nullptr,
                    true);
            });

        if (cb_result.has("error"))
        {
            S32 status = cb_result["error"].asInteger();
            if (status == LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE || status == LL_ERR_FILE_EMPTY)
                throw LLJSONRPCConnection::InvalidParams("Asset not found");
            if (status == LL_ERR_INSUFFICIENT_PERMISSIONS)
                throw LLJSONRPCConnection::ForbiddenError("Insufficient permissions to read asset");
            throw LLJSONRPCConnection::InternalError("Asset fetch failed: " + std::to_string(status));
        }

        LLUUID             asset_uuid = cb_result["asset_uuid"].asUUID();
        LLAssetType::EType asset_type = static_cast<LLAssetType::EType>(cb_result["asset_type"].asInteger());

        LLFileSystem file(asset_uuid, asset_type);
        S32 file_length = file.getSize();
        if (file_length <= 0)
            throw LLJSONRPCConnection::InternalError("Asset file empty or not found in cache");

        std::string asset(static_cast<size_t>(file_length), '\0');
        file.read(reinterpret_cast<U8*>(asset.data()), file_length);
        return asset;
    }
}

//========================================================================
LLScriptEditorWSServer::LLScriptEditorWSServer(const std::string& name, U16 port, bool local_only):
    LLJSONRPCServer(name, port, local_only),
    mPublishedObjectManager(this)
{
    LL_INFOS("ScriptEditorWS") << "Created JSON-RPC script editor server: " << name
                               << " on port " << port << LL_ENDL;

    // What scripts say reaches the IDE through the workspace, which hears
    // every object's debug and owner-say chat, when forwarding is on.
    mRuntimeConnection = ALScriptWorkspace::instance().onRuntime([this](const ALScriptWorkspace::RuntimeEvent& event) {
        static LLCachedControl<bool> forward(gSavedSettings, "ExternalWebsocketForwardDebug", false);
        if (forward)
        {
            sendRuntimeEvent(event);
        }
    });
    // And what the compiler said of a script saved through it, whichever
    // editor saved it: the workspace owns the compile, so a result is not
    // lost with a closed floater.
    mCompiledConnection = ALScriptWorkspace::instance().onCompiled([this](const ALScriptWorkspace::CompileResult& result) { sendCompiled(result); });

    registerCommand({ "viewer.teleport", "Teleport agent to an in-world object",
                      object_id_command_params() },
        [this](U32, const LLSD& p) -> LLSD
        {
            LLViewerObject* object = published_object(*this, p);

            LLVector3d global_pos = object->getPositionGlobal();
            gAgent.teleportViaLocation(global_pos);

            LLSD response;
            response["success"] = true;
            return response;
        });

    registerCommand({ "viewer.camera.focus",
                      "Zoom camera to an in-world object (same behavior as context menu Zoom In)",
                      object_id_command_params() },
        [this](U32, const LLSD& p) -> LLSD
        {
            LLViewerObject* object = published_object(*this, p);

            if (!handle_zoom_to_object(object->getID()))
            {
                throw LLJSONRPCConnection::InternalError(
                    "Object not found or not reachable");
            }

            LLSD response;
            response["success"] = true;
            return response;
        });

    registerCommand({ "viewer.object.save_back_to_contents",
                      "Save an in-world object back to source object contents",
                      object_id_command_params() },
        [this](U32 connection_id, const LLSD& p) -> LLSD
        {
            return this->handleSaveBackToObjectContents(connection_id, p);
        });

    registerCommand({ "viewer.script.reset_all",
                      "Reset all scripts in an in-world object",
                      object_id_command_params() },
        [this](U32 connection_id, const LLSD& p) -> LLSD
        {
            return this->handleObjectScriptResetAll(connection_id, p);
        });

    registerCommand({ "viewer.script.recompile_all",
                      "Recompile all scripts in an in-world object",
                      LLSDMap("object_id",
                              LLSDMap("type", "string")
                                  ("required", true))
                          ("target",
                              LLSDMap("type", "string")
                                  ("required", true)
                                  ("description",
                                      "Compilation target: luau, lsl2, mono, or auto")) },
        [this](U32 connection_id, const LLSD& p) -> LLSD
        {
            return this->handleObjectScriptRecompileAll(connection_id, p);
        });
}

LLScriptEditorWSServer::ptr_t LLScriptEditorWSServer::getServer()
{
    if (!LLWebsocketMgr::instanceExists())
    {
        return nullptr;
    }
    LLWebsocketMgr&               wsmgr  = LLWebsocketMgr::instance();
    return std::static_pointer_cast<LLScriptEditorWSServer>(
            wsmgr.findServerByName(LLScriptEditorWSServer::DEFAULT_SERVER_NAME));
}

bool LLScriptEditorWSServer::isEnabled()
{
    static LLCachedControl<bool> OPT_ENABLESCRIPTEDITORWS(gSavedSettings, "ExternalWebsocketSyncEnable", false);
    return OPT_ENABLESCRIPTEDITORWS;
}

bool LLScriptEditorWSServer::isTightIntegration()
{
    static LLCachedControl<bool> OPT_TIGHTINTEGRATION(gSavedSettings, "ExternalWebsocketSyncTightIntegration", false);
    return OPT_TIGHTINTEGRATION;
}

LLScriptEditorWSServer::ptr_t LLScriptEditorWSServer::ensureServerRunning()
{
    if (!LLScriptEditorWSServer::isEnabled())
    {
        LL_DEBUGS("ScriptEditorWS") << "WebSocket server is disabled by ExternalWebsocketSyncEnable" << LL_ENDL;
        return nullptr;
    }

    LLWebsocketMgr& wsmgr = LLWebsocketMgr::instance();
    ptr_t server = std::static_pointer_cast<LLScriptEditorWSServer>(
        wsmgr.findServerByName(DEFAULT_SERVER_NAME));

    if (server && !server->isRunning())
    {
        // thread stopped/was joined
        wsmgr.removeServer(DEFAULT_SERVER_NAME);
        server.reset();
    }

    if (!server)
    {
        // Local only: a client anywhere else could never read the
        // challenge's file, so could never be let in.
        U16 port = static_cast<U16>(gSavedSettings.getS32("ExternalWebsocketSyncPort"));
        server = std::make_shared<LLScriptEditorWSServer>(DEFAULT_SERVER_NAME, port, true);
        wsmgr.addServer(server);
    }

    if (!server->isRunning())
    {
        U16 port = static_cast<U16>(gSavedSettings.getS32("ExternalWebsocketSyncPort"));
        LLSD args;
        args["PORT"] = static_cast<S32>(port);

        if (!wsmgr.startServer(DEFAULT_SERVER_NAME))
        {
            LL_WARNS("ScriptEditorWS") << "Failed to start script editor websocket server" << LL_ENDL;
            LLNotificationsUtil::add("ExternalEditorServerFailed", args);
            return nullptr;
        }

        LLNotificationsUtil::add("ExternalEditorServerStarted", args);
    }

    return server;
}

std::string LLScriptEditorWSServer::buildScriptSubscriptionId(const LLUUID& object_id,
                                                              const LLUUID& item_id)
{
    std::string script_id = object_id.asString() + "_" + item_id.asString();

    std::array<char, MD5HEX_STR_SIZE> script_id_hash_str = {};
    LLMD5 script_id_hash((const U8*)script_id.c_str());
    script_id_hash.hex_digest(script_id_hash_str.data());

    return std::string(script_id_hash_str.data());
}

std::string LLScriptEditorWSServer::buildVSCodeURI(const LLUUID& object_id,
                                                    const LLUUID& script_id)
{
    std::ostringstream uri;
    uri << "vscode://lindenlab.sl-vscode-plugin/connect";

    U16 port = static_cast<U16>(gSavedSettings.getS32("ExternalWebsocketSyncPort"));
    uri << "?port=" << port;

    if (object_id.notNull())
    {
        uri << "&object=" << object_id.asString();
    }

    if (script_id.notNull())
    {
        uri << "&script=" << script_id.asString();
    }

    return uri.str();
}

bool LLScriptEditorWSServer::launchVSCode(const LLUUID& object_id,
                                           const LLUUID& script_id)
{
    ptr_t server = ensureServerRunning();
    if (!server)
    {
        LL_WARNS("ScriptEditorWS") << "Cannot launch VS Code: WebSocket server failed to start" << LL_ENDL;
        return false;
    }

    std::string uri = buildVSCodeURI(object_id, script_id);

    LLProcess::Params params;
#if LL_WINDOWS
    // On Windows, VS Code's 'code' is a batch file (.cmd) which APR cannot
    // launch directly. Invoke it through cmd.exe instead.
    // The URI may contain '&' which cmd.exe treats as a command separator,
    // so the entire argument list is passed as a single quoted string.
    params.executable = "cmd.exe";
    params.args.add("/c");
    params.args.add("code --open-url \"" + uri + "\"");
#else
    params.executable = "code";
    params.args.add("--open-url");
    params.args.add(uri);
#endif
    params.autokill = false;

    LLProcessPtr process = LLProcess::create(params);
    if (!process)
    {
        LL_WARNS("ScriptEditorWS") << "Failed to launch VS Code. "
            << "Ensure the 'code' command is available on your PATH." << LL_ENDL;
        return false;
    }

    LL_INFOS("ScriptEditorWS") << "Launched VS Code with URI: " << uri << LL_ENDL;
    return true;
}


LLWebsocketMgr::WSConnection::ptr_t LLScriptEditorWSServer::connectionFactory(LLWebsocketMgr::WSServer::ptr_t server,
                                                                              LLWebsocketMgr::connection_h handle)
{
    auto connection = std::make_shared<LLScriptEditorWSConnection>(server, handle);
    {
        LLMutexLock lock(&mConnectionsMutex);
        mActiveConnections[connection->getConnectionID()] = connection;
    }

    // Call setupConnectionMethods to register any global methods
    setupConnectionMethods(connection);

    return connection;
}

void LLScriptEditorWSServer::onStarted()
{
    // Nobody is connected yet, and the idle time counts from now: a
    // server started for an editor that never connects stops too. One
    // that connects first has its count, which update() also asks for.
    mIdleSince = LLTimer::getTotalSeconds().value();

    LLSyntaxDefCache& syntax_id_mgr = LLSyntaxDefCache::instance();
    wptr_t that(std::static_pointer_cast<LLScriptEditorWSServer>(shared_from_this()));

    mLastSyntaxId = syntax_id_mgr.getSyntaxID();
    mLanguageChangeSignal = syntax_id_mgr.addSyntaxIDCallback(
        [that]()
        {
            auto server = that.lock();
            if (server && server->isRunning())
            {
                server->broadcastLanguageChange();
            }
        });
}

void LLScriptEditorWSServer::onStopped()
{
    mLanguageChangeSignal.disconnect();
    mLastSyntaxId.setNull();

    // Connections are already closed -- clean up all internal state silently.
    // Do not attempt to send notifications; the sockets are gone.

    mPublishedObjectManager.clearAllStateWithListenerCleanup();

    mSubscriptions.clear();
    {
        LLMutexLock lock(&mConnectionsMutex);
        mActiveConnections.clear();
    }

    LL_INFOS("ScriptEditorWS") << "Script editor WebSocket server stopped, all state cleaned up" << LL_ENDL;

    LLNotificationsUtil::add("ExternalEditorServerStopped");
}

void LLScriptEditorWSServer::onConnectionOpened(const LLWebsocketMgr::WSConnection::ptr_t& connection)
{
    // Call parent class to handle JSON-RPC setup and standard methods
    LLJSONRPCServer::onConnectionOpened(connection);

    LL_INFOS("ScriptEditorWS") << "New script editor client connected via JSON-RPC" << LL_ENDL;
    mIdleSince = 0.0;
}

void LLScriptEditorWSServer::onConnectionClosed(const LLWebsocketMgr::WSConnection::ptr_t& connection)
{
    // Call parent class to handle JSON-RPC cleanup
    LLJSONRPCServer::onConnectionClosed(connection);

    LL_INFOS("ScriptEditorWS") << "Script editor client disconnected" << LL_ENDL;

    // Remove from active connections. This runs on the server's thread;
    // the subscriptions are the main thread's, so letting the
    // connection's go is posted there, guarded against the server having
    // gone by then.
    auto script_connection = std::dynamic_pointer_cast<LLScriptEditorWSConnection>(connection);
    if (script_connection)
    {
        U32    connection_id = script_connection->getConnectionID();
        size_t left          = 0;
        {
            LLMutexLock lock(&mConnectionsMutex);
            mActiveConnections.erase(connection_id);
            left = mActiveConnections.size();
        }
        std::weak_ptr<LLWebsocketMgr::WSServer> weak = weak_from_this();
        const bool posted = LLJSONRPCConnection::postToMainThread([weak, connection_id]() {
            if (auto self = weak.lock())
            {
                std::static_pointer_cast<LLScriptEditorWSServer>(self)->unsubscribeConnection(connection_id);
            }
        });
        // Where it could not be, the subscriptions keep a connection that
        // has gone, which a new one may take over.
        LL_WARNS_IF(!posted, "ScriptEditorWS") << "Main loop not taking work; connection " << connection_id
                                               << "'s subscriptions not let go" << LL_ENDL;

        LL_DEBUGS("ScriptEditorWS") << "Removed connection from active connections. Total: " << left << LL_ENDL;
        if (left == 0)
        {
            // Nothing listening: from here the idle time counts, and
            // update() stops the server once it has run out.
            mIdleSince = LLTimer::getTotalSeconds().value();
        }
    }
}

size_t LLScriptEditorWSServer::unauthenticatedConnectionCount() const
{
    LLMutexLock lock(&mConnectionsMutex);
    size_t      count = 0;
    for (const auto& [id, weak] : mActiveConnections)
    {
        auto connection = weak.lock();
        if (connection && !connection->isAuthenticated())
        {
            ++count;
        }
    }
    return count;
}

bool LLScriptEditorWSServer::update()
{
    // A server nobody is connected to stops after a while, so that a
    // port is not held for a client that has gone; it starts again the
    // next time an editor asks for it. Zero keeps it up for the session.
    static LLCachedControl<S32> idle_timeout(gSavedSettings, "ExternalWebsocketSyncIdleTimeout", 600);
    const F64                   since = mIdleSince.load();
    if (since > 0.0 && idle_timeout > 0 && LLTimer::getTotalSeconds().value() - since >= static_cast<F64>(idle_timeout) && getConnectionCount() == 0)
    {
        LL_INFOS("ScriptEditorWS") << "No script editor client for " << static_cast<S32>(idle_timeout) << " seconds; stopping the server" << LL_ENDL;
        return false;
    }
    return true;
}

bool LLScriptEditorWSServer::subscribeScriptEditor(const LLUUID& object_id, const LLUUID& item_id, std::string_view script_name,
    const LLHandle<LLPanel>& editor_handle, const std::string& script_id, bool lua)
{
    // The subscriptions are the main thread's; a caller from any other
    // trips here rather than races.
    llassert(on_main_thread());
    if (editor_handle.isDead())
    {
        return false;
    }

    auto it = mSubscriptions.find(script_id);
    if (it == mSubscriptions.end())
    {
        // New subscription
            ItemRef item_ref;
            item_ref.mPrimID = object_id;
            item_ref.mItemID = item_id;
            item_ref.mScriptName = script_name;
            mSubscriptions.emplace(script_id,
                EditorSubscription(item_ref, editor_handle, lua));
    }
    else
    {
        // Refresh existing subscription with the new editor handle
        it->second.mEditorHandle = editor_handle;
        it->second.mLua          = lua;
    }
    return true;
}

void LLScriptEditorWSServer::unsubscribeEditor(const std::string &script_id)
{
    llassert(on_main_thread());
    auto it = mSubscriptions.find(script_id);
    if (it != mSubscriptions.end())
    {
        S32 connection_id = it->second.mConnectionID;
        auto connection = it->second.mConnection.lock();
        mSubscriptions.erase(it);

        if (connection_id != 0)
        {
            auto cit = mConnectionSubscriptionCounts.find(connection_id);
            if (cit != mConnectionSubscriptionCounts.end())
            {
                if (--cit->second <= 0)
                {
                    mConnectionSubscriptionCounts.erase(cit);
                }
            }
        }

    }
}

void LLScriptEditorWSServer::unsubscribeConnection(U32 connection_id)
{
    llassert(on_main_thread());
    for (auto it = mSubscriptions.begin(); it != mSubscriptions.end(); ++it)
    {
        if (it->second.mConnectionID == connection_id)
        {
            LL_DEBUGS("ScriptEditorWS") << "Unsubscribing script " << it->first
                                       << " from connection ID " << connection_id << LL_ENDL;
            it->second.mConnectionID = 0;
            it->second.mConnection.reset();
        }
    }
    // All subs for this connection now have mConnectionID == 0.
    mConnectionSubscriptionCounts.erase(connection_id);
}

LLScriptEditorWSServer::SubscriptionError LLScriptEditorWSServer::updateScriptSubscription(const std::string &script_id, U32 connection_id)
{
    llassert(on_main_thread());
    auto it = mSubscriptions.find(script_id);
    if (it != mSubscriptions.end())
    {
        if (it->second.mEditorHandle.isDead())
        {
            unsubscribeEditor(script_id);
            return SubscriptionError::INVALID_EDITOR;
        }

        LLScriptEditorWSConnection::wptr_t connection;
        {
            LLMutexLock lock(&mConnectionsMutex);
            auto        con_it = mActiveConnections.find(connection_id);
            if (con_it == mActiveConnections.end())
            {
                return SubscriptionError::INTERNAL_ERROR;
            }
            connection = con_it->second;
        }

        if ((it->second.mConnectionID != 0) && !it->second.mConnection.expired()
            && it->second.mConnection.lock()->isConnected())
        {
            LL_WARNS("ScriptEditorWS") << "Script " << script_id << " is already subscribed on connection ID " << it->second.mConnectionID
                                       << ", cannot subscribe again on connection ID " << connection_id << LL_ENDL;
            // In the future we may want to support multiple connections per script.
            // That would imply it was open in multiple editors.
            return SubscriptionError::ALREADY_SUBSCRIBED;
        }

        // If this entry was previously bound to a different (dead) connection,
        // it would have been cleared by unsubscribeConnection, so mConnectionID
        // is always 0 here.
        it->second.mConnectionID = connection_id;
        it->second.mConnection   = connection;
        ++mConnectionSubscriptionCounts[connection_id];
        return SubscriptionError::SUCCESS;
    }
    return SubscriptionError::INVALID_SUBSCRIPTION;
}


LLHandle<LLPanel> LLScriptEditorWSServer::findEditorForScript(const std::string& script_id) const
{
    llassert(on_main_thread());
    auto it = mSubscriptions.find(script_id);
    if (it != mSubscriptions.end())
    {
        return it->second.mEditorHandle;
    }
    return LLHandle<LLPanel>();
}

std::set<std::string> LLScriptEditorWSServer::getActiveScripts() const
{
    llassert(on_main_thread());
    std::set<std::string> active_scripts;
    for (const auto& [script_id, subinfo] : mSubscriptions)
    {
        if (!subinfo.mEditorHandle.isDead())
        {
            active_scripts.insert(script_id);
        }
    }
    return active_scripts;
}

void LLScriptEditorWSServer::setupConnectionMethods(LLJSONRPCConnection::ptr_t connection)
{
    // Call parent class to register global JSON-RPC methods
    LLJSONRPCServer::setupConnectionMethods(connection);

    // Cast to our specific connection type to access script editor functionality
    auto script_connection = std::dynamic_pointer_cast<LLScriptEditorWSConnection>(connection);
    if (script_connection)
    {
        LL_DEBUGS("ScriptEditorWS") << "Setting up script editor connection methods" << LL_ENDL;
        U32 connection_id = script_connection->getConnectionID();

        // Every one of them runs on the main thread, in a coroutine: what
        // they read -- the subscriptions, the syntax cache and its files,
        // the world -- is the main thread's, and the server's thread would
        // be reading it as the main one changed it.
        script_connection->registerAsyncMethod("language.syntax.id",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, auto&)
            {
                return s.handleLanguageIdRequest();
            }));

        script_connection->registerAsyncMethod("language.syntax",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleSyntaxRequest(params);
            }));

        script_connection->registerAsyncMethod("language.syntax.cache",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, auto&)
            {
                return s.handleSyntaxCacheRequest();
            }));

        script_connection->registerAsyncMethod("language.syntax.get",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleSyntaxCacheFileRequest(params);
            }));

        script_connection->registerAsyncMethod("script.subscribe",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleScriptSubscribe(connection_id, params);
            }));

        script_connection->registerAsyncMethod("script.list",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, auto&)
            {
                return s.handleFileWatcherFileListRequest();
            }));

        script_connection->registerAsyncMethod("object.unpublish",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectUnpublish(connection_id, params);
            }));

        script_connection->registerAsyncMethod("script.unsubscribe",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleScriptUnsubscribe(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.request",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectRequest(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.content.get",
            bindHandler([](LLScriptEditorWSServer& s, const std::string& method, const LLSD& id, const LLSD& params)
            {
                return s.handleObjectContentGet(method, id, params);
            }));

        script_connection->registerAsyncMethod("object.content.save",
            bindHandler([](LLScriptEditorWSServer& s, const std::string& method, const LLSD& id, const LLSD& params)
            {
                return s.handleObjectContentSave(method, id, params);
            }));

        script_connection->registerAsyncMethod("object.item.delete",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectItemDelete(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.item.create",
            bindHandler([](LLScriptEditorWSServer& s, const std::string& method, const LLSD& id, const LLSD& params)
            {
                return s.handleObjectItemCreate(method, id, params);
            }));

        script_connection->registerAsyncMethod("object.list",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, auto&)
            {
                return s.handleObjectList();
            }));

        script_connection->registerAsyncMethod("object.script.set_running",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectScriptSetRunning(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.script.reset",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectScriptReset(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.modify",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectModify(connection_id, params);
            }));

        script_connection->registerAsyncMethod("object.item.modify",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleObjectItemModify(connection_id, params);
            }));

        script_connection->registerAsyncMethod("command.execute",
            bindHandler([connection_id](LLScriptEditorWSServer& s, auto&, auto&, const LLSD& params)
            {
                return s.handleCommandExecute(connection_id, params);
            }));

        script_connection->registerAsyncMethod("command.list",
            bindHandler([](LLScriptEditorWSServer& s, auto&, auto&, auto&)
            {
                return s.handleCommandList();
            }));
    }
}

LLSD LLScriptEditorWSServer::handleObjectList() const
{
    LLSD response;
    response["objects"] = mPublishedObjectManager.buildObjectListLLSD();
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectScriptSetRunning(U32 connection_id, const LLSD& params)
{
    LLUUID prim_id = params["prim_id"].asUUID();
    LLUUID item_id = params["item_id"].asUUID();
    bool running = params["running"].asBoolean();

    if (prim_id.isNull() || item_id.isNull())
        throw LLJSONRPCConnection::InvalidParams("prim_id and item_id are required");

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
        throw LLJSONRPCConnection::InvalidParams("Prim not found");

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");

    LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(prim->getInventoryObject(item_id));
    if (!item)
        throw LLJSONRPCConnection::InvalidParams("Script not found in prim inventory");

    if (item->getType() != LLAssetType::AT_LSL_TEXT)
        throw LLJSONRPCConnection::InvalidParams("Item is not a script");

    if (!gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE))
        throw LLJSONRPCConnection::ForbiddenError("No modify permission on script");

    LLViewerRegion* region = region_of(prim);

    // Send SetScriptRunning message to simulator
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_SetScriptRunning);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_Script);
    msg->addUUIDFast(_PREHASH_ObjectID, prim_id);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->addBOOLFast(_PREHASH_Running, running);
    msg->sendReliable(region->getHost());

    LLSD response;
    response["success"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectScriptReset(U32 connection_id, const LLSD& params)
{
    LLUUID prim_id = params["prim_id"].asUUID();
    LLUUID item_id = params["item_id"].asUUID();

    if (prim_id.isNull() || item_id.isNull())
        throw LLJSONRPCConnection::InvalidParams("prim_id and item_id are required");

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
        throw LLJSONRPCConnection::InvalidParams("Prim not found");

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");

    LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(prim->getInventoryObject(item_id));
    if (!item)
        throw LLJSONRPCConnection::InvalidParams("Script not found in prim inventory");

    if (item->getType() != LLAssetType::AT_LSL_TEXT)
        throw LLJSONRPCConnection::InvalidParams("Item is not a script");

    if (!gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE))
        throw LLJSONRPCConnection::ForbiddenError("No modify permission on script");

    LLViewerRegion* region = region_of(prim);

    // Send ScriptReset message to simulator
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ScriptReset);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_Script);
    msg->addUUIDFast(_PREHASH_ObjectID, prim_id);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(region->getHost());

    LLSD response;
    response["success"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectScriptResetAll(U32 connection_id, const LLSD& params)
{
    LLUUID prim_id = params["object_id"].asUUID();
    if (prim_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams("object_id is required");
    }

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
    {
        throw LLJSONRPCConnection::InvalidParams("Object not found");
    }

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
    {
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");
    }

    if (!prim->flagScripted())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Prim contains no scripts");
    }

    if (!prim->permModify())
    {
        throw LLJSONRPCConnection::ForbiddenError(
            "No modify permission on prim");
    }

    LLUUID queue_id;
    queue_id.generate();

    LLFloaterScriptQueue* queue =
        LLFloaterReg::getTypedInstance<LLFloaterScriptQueue>(
            "reset_queue", LLSD(queue_id));
    if (!queue)
    {
        throw LLJSONRPCConnection::InternalError(
            "Unable to open reset queue");
    }

    queue->addObject(prim->getID(), prim->getID().asString());
    if (!queue->start())
    {
        queue->closeFloater();
        throw LLJSONRPCConnection::InternalError(
            "Unable to start reset queue");
    }

    queue->setTitle(LLTrans::getString("ResetQueueTitle"));

    LLSD response;
    response["success"] = true;
    response["object_id"] = prim->getID();
    response["queued"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectScriptRecompileAll(
    U32 connection_id, const LLSD& params)
{
    LLUUID object_id = params["object_id"].asUUID();
    if (object_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams("object_id is required");
    }

    std::string target = params["target"].asString();
    if (target != "luau" &&
        target != "lsl2" &&
        target != "mono" &&
        target != "auto")
    {
        throw LLJSONRPCConnection::InvalidParams(
            "target must be 'luau', 'lsl2', 'mono', or 'auto'");
    }

    LLViewerObject* object = gObjectList.findObject(object_id);
    if (!object)
    {
        throw LLJSONRPCConnection::InvalidParams("Object not found");
    }

    LLViewerObject* root = object->getRootEdit();
    if (!root || root->getID() != object_id || !isObjectPublished(root->getID()))
    {
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");
    }

    if (!root->flagScripted())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Object contains no scripts");
    }

    if (!root->permModify())
    {
        throw LLJSONRPCConnection::ForbiddenError(
            "No modify permission on object");
    }

    if (target == "luau")
    {
        target = "auto-luau";
    }

    LLUUID queue_id;
    queue_id.generate();

    LLFloaterCompileQueue* queue =
        LLFloaterReg::getTypedInstance<LLFloaterCompileQueue>(
            "compile_queue", LLSD(queue_id));
    if (!queue)
    {
        throw LLJSONRPCConnection::InternalError(
            "Unable to open compile queue");
    }

    queue->setCompileTarget(target);
    queue->addObject(root->getID(), root->getID().asString());
    if (!queue->start())
    {
        queue->closeFloater();
        throw LLJSONRPCConnection::InternalError(
            "Unable to start compile queue");
    }

    queue->setTitle(LLTrans::getString("CompileQueueTitle"));

    LLSD response;
    response["success"] = true;
    response["object_id"] = root->getID();
    response["target"] = (target == "auto-luau") ? "luau" : target;
    response["queued"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectModify(U32 connection_id, const LLSD& params)
{
    // Step 1: Parameter Validation
    LLUUID prim_id = params["prim_id"].asUUID();
    if (prim_id.isNull())
        throw LLJSONRPCConnection::InvalidParams("prim_id is required");

    bool has_name = params.has("name");
    bool has_desc = params.has("description");
    bool has_perms = params.has("permissions") && params["permissions"].has("next_owner");

    if (!has_name && !has_desc && !has_perms)
        throw LLJSONRPCConnection::InvalidParams(
            "At least one property (name, description, or permissions) must be specified");

    // Step 2: Find and Validate Object
    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
        throw LLJSONRPCConnection::InvalidParams("Prim not found");

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");

    if (!prim->permModify())
        throw LLJSONRPCConnection::ForbiddenError("No modify permission on object");

    // Step 3: Send Property Update Messages
    LLMessageSystem* msg = gMessageSystem;
    LLHost host = region_of(prim)->getHost();
    U32 local_id = prim->getLocalID();

    if (has_name)
    {
        std::string new_name = params["name"].asString();
        msg->newMessageFast(_PREHASH_ObjectName);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addU32Fast(_PREHASH_LocalID, local_id);
        msg->addStringFast(_PREHASH_Name, new_name);
        msg->sendReliable(host);
    }

    if (has_desc)
    {
        std::string new_desc = params["description"].asString();
        msg->newMessageFast(_PREHASH_ObjectDescription);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addU32Fast(_PREHASH_LocalID, local_id);
        msg->addStringFast(_PREHASH_Description, new_desc);
        msg->sendReliable(host);
    }

    if (has_perms)
    {
        U32 next_owner_mask = static_cast<U32>(params["permissions"]["next_owner"].asInteger());
        msg->newMessageFast(_PREHASH_ObjectPermissions);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_HeaderData);
        msg->addBOOLFast(_PREHASH_Override, false);
        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addU32Fast(_PREHASH_ObjectLocalID, local_id);
        msg->addU8Fast(_PREHASH_Field, PERM_NEXT_OWNER);
        msg->addBOOLFast(_PREHASH_Set, true);
        msg->addU32Fast(_PREHASH_Mask, next_owner_mask);
        msg->sendReliable(host);
    }

    // Force a properties reply so the editor sees the change without an in-world selection.
    LLSelectMgr::instance().requestObjectPropertiesFamily(prim);

    // Step 4: Return Success Response
    LLSD response;
    response["success"] = true;
    response["prim_id"] = prim_id.asString();
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectItemModify(U32 connection_id, const LLSD& params)
{
    // Step 1: Parameter Validation
    if (!params.has("prim_id") || !params.has("item_id"))
        throw LLJSONRPCConnection::InvalidParams("prim_id and item_id are required");

    bool has_name = params.has("name");
    bool has_desc = params.has("description");
    bool has_perms = params.has("permissions") && params["permissions"].has("next_owner");

    if (!has_name && !has_desc && !has_perms)
        throw LLJSONRPCConnection::InvalidParams(
            "At least one property (name, description, or permissions) must be specified");

    // Step 2: Validate Published Item (reuse existing helper)
    ValidatedItem v = validatePublishedItem(params, PERM_MODIFY);

    LLUUID prim_id = params["prim_id"].asUUID();
    LLUUID item_id = params["item_id"].asUUID();

    // Step 3: Create Modified Item Copy
    LLPointer<LLViewerInventoryItem> new_item =
        new LLViewerInventoryItem(static_cast<LLViewerInventoryItem*>(v.item));

    if (has_name)
    {
        new_item->rename(params["name"].asString());
    }

    if (has_desc)
    {
        new_item->setDescription(params["description"].asString());
    }

    if (has_perms)
    {
        LLPermissions perm = new_item->getPermissions();
        U32 next_owner_mask = static_cast<U32>(params["permissions"]["next_owner"].asInteger());
        perm.setMaskNext(next_owner_mask);
        new_item->setPermissions(perm);
    }

    // Step 4: Send UpdateTaskInventory Message
    v.prim->updateInventory(new_item, TASK_INVENTORY_ITEM_KEY, false);

    // Step 5: Return Success Response
    LLSD response;
    response["success"] = true;
    response["prim_id"] = prim_id.asString();
    response["item_id"] = item_id.asString();
    return response;
}

void LLScriptEditorWSServer::registerCommand(const WSCommandInfo& info, WSCommandHandler handler)
{
    mCommandRegistry.emplace(info.command, std::make_pair(info, std::move(handler)));
}

bool LLScriptEditorWSConnection::hasFeature(const std::string& feature) const
{
    LLMutexLock lock(&mHandshakeMutex);
    return mFeatures.count(feature) > 0;
}

LLSD LLScriptEditorWSServer::handleSaveBackToObjectContents(U32 connection_id, const LLSD& params)
{
    LLUUID object_id = params["object_id"].asUUID();
    if (object_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams("object_id is required");
    }

    const LLPublishedObjectMgr::PublishedObjectInfo* published_info =
        mPublishedObjectManager.getPublished(object_id);
    if (!published_info)
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Object is not published");
    }

    if (!published_info->mCanSaveBackToContents || published_info->mSourceTaskID.isNull())
    {
        throw LLJSONRPCConnection::ForbiddenError(
            "Save back is not available for this object");
    }

    LLViewerObject* root = gObjectList.findObject(object_id);
    if (!root)
    {
        throw LLJSONRPCConnection::InvalidParams(
            "object_id not found");
    }

    if (!save_object_back_to_contents(root, published_info->mSourceTaskID))
    {
        throw LLJSONRPCConnection::InternalError(
            "Failed to save object back to contents");
    }

    LL_DEBUGS("ScriptEditorWS") << "Save-back requested via command for object "
                                << object_id << " on connection " << connection_id << LL_ENDL;

    LLSD response;
    response["success"] = true;

    LLSD result;
    result["object_id"] = object_id;
    response["result"] = result;
    return response;
}

LLSD LLScriptEditorWSServer::handleCommandExecute(U32 connection_id, const LLSD& params)
{
    const std::string command = params["command"].asString();
    if (command.empty())
    {
        throw LLJSONRPCConnection::InvalidParams("command is required");
    }

    auto it = mCommandRegistry.find(command);
    if (it == mCommandRegistry.end())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Unknown command: " + command);
    }

    return it->second.second(connection_id, params["params"]);
}

LLSD LLScriptEditorWSServer::handleCommandList()
{
    LLSD commands(LLSD::emptyArray());
    for (const auto& [name, entry] : mCommandRegistry)
    {
        LLSD info;
        info["command"]     = entry.first.command;
        info["description"] = entry.first.description;
        if (!entry.first.params.isUndefined())
        {
            info["params"] = entry.first.params;
        }
        commands.append(info);
    }
    LLSD response;
    response["commands"] = commands;
    return response;
}

void LLScriptEditorWSServer::sendCommandExecute(
    U32 connection_id, const std::string& command, const LLSD& params)
{
    LLScriptEditorWSConnection::ptr_t connection;
    {
        LLMutexLock lock(&mConnectionsMutex);
        auto        it = mActiveConnections.find(connection_id);
        if (it == mActiveConnections.end())
        {
            return;
        }
        connection = it->second.lock();
    }
    if (!connection || !connection->hasFeature("commands"))
    {
        return;
    }

    LLSD call_params;
    call_params["command"] = command;
    call_params["params"]  = params;

    connection->call("command.execute", call_params,
        [command](const LLSD& result, const LLSD& error)
        {
            LL_WARNS_IF(!error.isUndefined() || !result["success"].asBoolean(), "WSCommand")
                << "command.execute failed for " << command << LL_ENDL;
        });
}

void LLScriptEditorWSServer::broadcastLanguageChange()
{
    LLUUID syntax_id = LLSyntaxDefCache::instance().getSyntaxID();

    if (syntax_id != mLastSyntaxId)
    {
        mLastSyntaxId = syntax_id;
        LLSD params;
        params["id"] = syntax_id;

        if (isRunning())
        {
            notifyAll("language.syntax.change", params);
        }
    }
}

LLSD LLScriptEditorWSServer::handlePing(
    const LLJSONRPCConnection::ptr_t& connection,
    const LLSD& params) const
{
    LLSD result;
    result["pong"] = "pong";

    if (params.has("timestamp"))
    {
        result["timestamp"] = params["timestamp"];
    }

    result["server_time"] = static_cast<LLSD::Integer>(
        LLDate::now().secondsSinceEpoch() * 1000.0);
    return result;
}

LLSD LLScriptEditorWSServer::handleGetVersion(
    const LLJSONRPCConnection::ptr_t& connection,
    const LLSD& params) const
{
    LLSD result;
    result["client_name"] = LLVersionInfo::instance().getChannel();
    result["client_version"] = LLVersionInfo::instance().getVersion();
    return result;
}

LLSD LLScriptEditorWSServer::handleLanguageIdRequest() const
{
    LLSD response;

    response["id"] = mLastSyntaxId;
    return response;
}

LLSD LLScriptEditorWSServer::handleSyntaxRequest(const LLSD& params) const
{
    LLSD        response(LLSD::emptyMap());
    std::string category = params["kind"].asString();

    if (category.empty())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "No syntax category specified");
    }

    response["id"] = mLastSyntaxId;
    if (category == "defs.lua")
    {
        response["defs"] = LLSyntaxDefCache::instance().getLuaKeywords();
    }
    else if (category == "defs.lsl")
    {
        response["defs"] = LLSyntaxDefCache::instance().getLSLKeywords();
    }
    else
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Unknown syntax category requested");
    }

    if (!response["defs"].isDefined())
    {
        throw LLJSONRPCConnection::InternalError(
            "Syntax definitions are unavailable");
    }

    response["success"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleSyntaxCacheRequest() const
{
    LLSD response;
    // Add array of cached syntax definition files
    LLSD syntax_files = LLSD::emptyArray();
    for (const auto& name : LLSyntaxDefCache::instance().getCacheFileNames())
    {
        syntax_files.append(name);
    }
    response["files"] = syntax_files;
    response["success"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleSyntaxCacheFileRequest(const LLSD& params) const
{
    std::string filename = params["filename"].asString();
    bool        as_json  = params["as_json"].asBoolean();

    LLSyntaxDefCache& cache = LLSyntaxDefCache::instance();
    LLSD              response;

    if (filename.empty())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "No filename specified");
    }
    if (!cache.hasCacheFile(filename))
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Requested syntax cache file not found");
    }
    if (as_json)
    {
        LLSD file_content   = cache.loadCacheFileAsLLSD(filename);
        if (file_content.isDefined())
        {
            response["content"] = file_content;
        }
        else
        {
            throw LLJSONRPCConnection::InternalError(
                "Failed to load and format syntax cache file.");
        }
    }
    else
    {
        std::string content = cache.loadCacheFile(filename);
        if (!content.empty())
        {
            response["content"] = content;
        }
        else
        {
            throw LLJSONRPCConnection::InternalError(
                "Failed to load syntax cache file");
        }
    }
    response["success"] = true;
    return response;
}

LLSD LLScriptEditorWSServer::handleScriptSubscribe(U32 connection_id, const LLSD& params)
{
    LLSD response(LLSD::emptyMap());

    std::string script_id = params["script_id"].asString();
    std::string script_name = params["script_name"].asString();
    std::string language    = params["script_language"].asString();

    SubscriptionError result = updateScriptSubscription(script_id, connection_id);

    response["script_id"] = script_id;
    response["success"]   = (result == SubscriptionError::SUCCESS);
    response["status"]    = static_cast<S32>(result);

    LL_WARNS_IF(result != SubscriptionError::SUCCESS, "ScriptEditorWS")
        << "Script connect request for script " << script_id << " failed with status " << static_cast<S32>(result) << LL_ENDL;
    switch (result)
    {
    case SubscriptionError::SUCCESS:
        response["message"] = "OK";
        break;
    case SubscriptionError::INVALID_EDITOR:
        response["message"] = "Invalid editor handle";
        break;
    case SubscriptionError::INVALID_SUBSCRIPTION:
        response["message"] = "No subscription found for script";
        break;
    case SubscriptionError::ALREADY_SUBSCRIBED:
        response["message"] = "Script already subscribed";
        break;
    case SubscriptionError::INTERNAL_ERROR:
        response["message"] = "Internal server error";
        break;
    }

    if (result == SubscriptionError::SUCCESS)
    {
        auto it = mSubscriptions.find(script_id);
        if (it != mSubscriptions.end())
        {
            LLUUID prim_id = (*it).second.mItemRef.mPrimID;
            LLUUID root_id = prim_id;
            LLViewerObject* object = gObjectList.findObject(prim_id);
            if (object)
            {
                LLViewerObject* root = object->getRootEdit();
                if (root)
                {
                    root_id = root->getID();
                }
            }

            response["object_id"] = prim_id;
            response["root_id"] = root_id;
            //response["object_name"] = object ? object->getName() : "Unknown";
            response["item_id"] = (*it).second.mItemRef.mItemID;
        }
    }

    return response;
}

LLSD LLScriptEditorWSServer::handleScriptUnsubscribe(U32 connection_id, const LLSD& params)
{
    std::string script_id = params["script_id"].asString();

    auto it = mSubscriptions.find(script_id);
    if (it != mSubscriptions.end() && (it->second.mConnectionID == connection_id))
    {
        unsubscribeEditor(script_id);
    }
    return LLSD();
}

LLSD LLScriptEditorWSServer::handleFileWatcherFileListRequest() const
{
    LLSD response;

    response["temp_dir"] = LLFile::tmpdir();

    // Add array of script_id's from active scripts
    LLSD script_ids_array = LLSD::emptyArray();
    for (const auto& [script_id, subinfo] : mSubscriptions)
    {
        script_ids_array.append(script_id);
    }
    response["script_ids"] = script_ids_array;

    response["success"] = true;

    return response;
}

LLSD LLScriptEditorWSServer::handleObjectRequest(U32 connection_id, const LLSD& params)
{
    LLUUID object_id = params["object_id"].asUUID();
    LLSD response;

    if (object_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams(
            "No object_id specified");
    }

    LLViewerObject* object = gObjectList.findObject(object_id);
    if (!object)
    {
        throw LLJSONRPCConnection::InvalidParams(
            "Object not found");
    }

    if (!object->permModify())
    {
        throw LLJSONRPCConnection::ForbiddenError(
            "Permission denied");
    }

    bool accepted = publishObject(object_id);
    if (!accepted)
    {
        throw LLJSONRPCConnection::InternalError(
            "Failed to initiate publish");
    }

    response["success"] = true;
    return response;
}

// Helper function to validate that the specified prim and
// item are valid, published, and have the required permissions.
// Throws JSON-RPC exceptions if validation fails.
LLScriptEditorWSServer::ValidatedItem LLScriptEditorWSServer::validatePublishedItem(
    const LLSD& params, U32 permMask) const
{
    LLUUID prim_id = params["prim_id"].asUUID();
    LLUUID item_id = params["item_id"].asUUID();

    if (prim_id.isNull() || item_id.isNull())
        throw LLJSONRPCConnection::InvalidParams("prim_id and item_id are required");

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
        throw LLJSONRPCConnection::InvalidParams("Prim not found");

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");

    // Everything done with an item goes to the prim's region.
    region_of(prim);

    LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(prim->getInventoryObject(item_id));
    if (!item)
        throw LLJSONRPCConnection::InvalidParams("Item not found in prim inventory");

    LLAssetType::EType type = item->getType();
    if (type != LLAssetType::AT_LSL_TEXT && type != LLAssetType::AT_NOTECARD)
        throw LLJSONRPCConnection::InvalidParams("Item is not a script or notecard");

    if ((permMask & PERM_COPY) &&
        !gAgent.allowOperation(PERM_COPY, item->getPermissions(), GP_OBJECT_MANIPULATE))
        throw LLJSONRPCConnection::ForbiddenError("Insufficient permissions");

    if (permMask & PERM_MODIFY)
    {
        // Writes into task inventory require modify permission on both the
        // item AND the containing prim. A no-mod object can be published
        // (read-only), but its contents cannot be changed.
        if (!gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE))
            throw LLJSONRPCConnection::ForbiddenError("Insufficient permissions");

        if (!prim->permModify())
            throw LLJSONRPCConnection::ForbiddenError("No modify permission on object");
    }

    return { prim, root, item, type };
}

LLSD LLScriptEditorWSServer::handleObjectContentGet(const std::string& method, const LLSD& id, const LLSD& params)
{
    // Permission policy for reading item contents:
    //   - Scripts:   require both PERM_COPY and PERM_MODIFY. No-copy or
    //                no-modify scripts cannot have their source exposed.
    //   - Notecards: no permission requirement -- no-mod notecards remain
    //                readable so external editors can view their contents.
    U32 required_perms = 0;
    {
        LLUUID prim_id_peek = params["prim_id"].asUUID();
        LLUUID item_id_peek = params["item_id"].asUUID();
        LLViewerObject* prim_peek = gObjectList.findObject(prim_id_peek);
        if (prim_peek)
        {
            if (auto* it = dynamic_cast<LLInventoryItem*>(prim_peek->getInventoryObject(item_id_peek)))
            {
                if (it->getType() == LLAssetType::AT_LSL_TEXT)
                    required_perms = PERM_COPY | PERM_MODIFY;
            }
        }
    }

    auto v = validatePublishedItem(params, required_perms);

    LLUUID prim_id = params["prim_id"].asUUID();
    LLUUID item_id = params["item_id"].asUUID();

    const LLAssetType::EType type  = v.type;
    const std::string        asset = fetch_item_asset(v.prim, v.item, type);

    LLSD        response;
    std::string text_content;
    if (type == LLAssetType::AT_NOTECARD)
    {
        // Notecards are stored in an envelope format -- use LLNotecard to extract the text
        LLNotecard notecard;
        std::istringstream istr(asset);
        if (notecard.importStream(istr))
        {
            text_content = notecard.getText();
        }
        else
        {
            throw LLJSONRPCConnection::InternalError("Failed to parse notecard format");
        }
        // What the text cannot carry, so that a client can say why a
        // notecard holding any cannot be saved from it.
        response["embedded_items"] = static_cast<S32>(notecard.getItems().size());
    }
    else
    {
        // Up to the first NUL, as a script's source has always been read.
        text_content = std::string(asset.c_str());
    }

    response["success"] = true;
    response["prim_id"] = prim_id;
    response["item_id"] = item_id;
    response["content"] = text_content;
    response["encoding"] = "utf-8";
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectContentSave(const std::string& method, const LLSD& id, const LLSD& params)
{
    // Required, and may be empty: a notecard can be emptied.
    if (!params.has("content") || !params["content"].isString())
        throw LLJSONRPCConnection::InvalidParams("content is required");
    const std::string content = params["content"].asString();

    auto v = validatePublishedItem(params, PERM_MODIFY);

    if (v.type == LLAssetType::AT_LSL_TEXT)
    {
        return saveScript(v.prim, v.item, content, params);
    }
    else
    {
        return saveNotecard(v.prim, v.item, content);
    }
}

LLSD LLScriptEditorWSServer::saveScript(LLViewerObject* prim, LLInventoryItem* item,
                                         const std::string& content, const LLSD& params)
{
    // Determine compile target
    std::string compile_target;
    if (params.has("vm"))
    {
        compile_target = params["vm"].asString();
        // Only what the protocol names goes to the simulator as a target.
        if (compile_target != "luau" && compile_target != "mono" && compile_target != "lsl2")
        {
            throw LLJSONRPCConnection::InvalidParams("vm must be 'luau', 'mono', or 'lsl2'");
        }
        // The client sends "luau" for the Luau VM -- but if the script is LSL
        // (not native Luau), the internal compile target is "lsl-luau".
        if (compile_target == "luau" && item->getInventorySubType() != SST_LUA)
        {
            compile_target = "lsl-luau";
        }
    }
    else
    {
        U8 subtype = item->getInventorySubType();
        std::string runtime = item->getRuntime();
        bool is_lua = (subtype == SST_LUA);
        if (!is_lua && runtime == "luau")
            compile_target = "lsl-luau";
        else if (!runtime.empty())
            compile_target = runtime;
        else
        {
            is_lua = is_lua_script(content);
            compile_target = is_lua ? "luau" : "mono";
        }
    }

    // The task inventory can be refreshed while the upload is in flight,
    // invalidating the raw item pointer returned by validatePublishedItem().
    // Keep stable identifiers for use after await_async_result().
    const LLUUID prim_id = prim->getID();
    const LLUUID item_id = item->getUUID();
    const LLViewerInventoryItem* viewer_item = dynamic_cast<const LLViewerInventoryItem*>(item);
    bool is_running = viewer_item ? viewer_item->getIsRunning() : false;
    if (params.has("running"))
    {
        is_running = params["running"].asBoolean();
    }
    if (region_of(prim)->getCapability("UpdateScriptTask").empty())
        throw LLJSONRPCConnection::InternalError("UpdateScriptTask capability not available");

    // The experience it runs under: the upload sets whatever it is sent, and
    // none takes it away, so it is asked of the region first, as the
    // studio's own save asks. A save that cannot learn it is refused rather
    // than strip it.
    const LLSD asked = await_async_result(
        "objectContentSaveExperience", ASSET_FETCH_TIMEOUT, "The script's experience was not answered",
        [prim_id, item_id](const std::string& pump_name)
        {
            ALScriptWorkspace::instance().askExperience(ALScriptRef(prim_id, item_id), [pump_name](const std::optional<LLUUID>& experience) {
                LLSD said;
                if (experience)
                {
                    said["experience"] = *experience;
                }
                else
                {
                    said["unknown"] = true;
                }
                LLEventPumps::instance().post(pump_name, said);
            });
        });
    if (asked.has("unknown"))
        throw LLJSONRPCConnection::InternalError(LLTrans::getString("WorkspaceExperienceUnknown"));
    const LLUUID experience = asked["experience"].asUUID();

    // The object again, after the wait: it may have gone out of view.
    LLViewerObject* holder = gObjectList.findObject(prim_id);
    if (!holder)
        throw LLJSONRPCConnection::InvalidParams("Prim not found");
    std::string url = region_of(holder)->getCapability("UpdateScriptTask");
    if (url.empty())
        throw LLJSONRPCConnection::InternalError("UpdateScriptTask capability not available");

    LLSD cb_result = await_async_result(
        "objectContentSave", SCRIPT_UPLOAD_TIMEOUT, "Script upload/compile timed out",
        [&, prim_id, item_id](const std::string& pump_name)
        {
            auto [on_success, on_failure] = make_asset_upload_callbacks(pump_name);
            LLResourceUploadInfo::ptr_t uploadInfo(std::make_shared<LLScriptAssetUpload>(
                prim_id, item_id,
                compile_target, is_running, experience, content,
                std::move(on_success), std::move(on_failure)));
            LLViewerAssetUpload::EnqueueInventoryUpload(url, uploadInfo);
        });

    if (cb_result.has("failed"))
        throw LLJSONRPCConnection::InternalError("Upload failed: " + cb_result["reason"].asString());

    LLSD response;
    response["success"]  = true;
    response["prim_id"]  = prim_id;
    response["item_id"]  = item_id;
    response["compiled"] = cb_result["compiled"];
    if (!cb_result["compiled"].asBoolean() && cb_result.has("errors"))
    {
        response["diagnostics"] = LLSD::emptyArray();

        const bool is_lua =
            compile_target == "luau" ||
            compile_target == "lsl-luau";

        for (const auto& error : llsd::inArray(cb_result["errors"]))
        {
            // The match views the text, so the text is kept for as long.
            const std::string text = error.asString();
            ALRegexMatch match;
            LLSD diagnostic;
            diagnostic["level"] = "ERROR";
            S32 line_number = 0;
            S32 col_number = 0;

            if (is_lua &&
                LUAU_LOCATION_PATTERN.match(text, &match) &&
                LLStringUtil::convertToS32(match.str(2), line_number))
            {
                diagnostic["row"] = line_number;
                diagnostic["column"] = 0;
                diagnostic["message"] = match.str(3);
            }
            else if (!is_lua &&
                     LSL_LOCATION_PATTERN.match(text, &match) &&
                     LLStringUtil::convertToS32(match.str(1), line_number) &&
                     LLStringUtil::convertToS32(match.str(2), col_number) &&
                     line_number < S32_MAX &&
                     col_number < S32_MAX)
            {
                diagnostic["row"] = line_number + 1;
                diagnostic["column"] = col_number + 1;
                diagnostic["level"] = match.str(3);
                diagnostic["message"] = match.str(4);
                diagnostic["format"] = "lsl";
            }
            else
            {
                diagnostic["row"] = 0;
                diagnostic["column"] = 0;
                diagnostic["message"] = text;
            }

            response["diagnostics"].append(diagnostic);
        }
    }

    // If the script is open in the viewer's editor, update it
    LLSD floater_key;
    floater_key["taskid"] = prim_id;
    floater_key["itemid"] = item_id;
    LLLiveLSLEditor* editor = LLFloaterReg::findTypedInstance<LLLiveLSLEditor>("preview_scriptedit", floater_key);
    if (editor)
    {
        LLScriptEdCore* sed = editor->getScriptEdCore();
        if (sed)
        {
            sed->setScriptText(LLStringExplicit(content), true);
            sed->makeEditorPristine();
        }
    }
    ALFloaterScriptStudio::savedElsewhere(ALScriptRef(prim_id, item_id), content, cb_result["new_asset_id"].asUUID());

    return response;
}

LLSD LLScriptEditorWSServer::saveNotecard(LLViewerObject* prim, LLInventoryItem* item,
                                           const std::string& content)
{
    // More text than a notecard is read back with makes one nobody can open.
    if (content.size() > static_cast<size_t>(LLNotecard::MAX_SIZE))
    {
        throw LLJSONRPCConnection::InvalidParams("The notecard's text is " + std::to_string(content.size()) + " bytes; a notecard may hold at most " +
                                                 std::to_string(static_cast<S32>(LLNotecard::MAX_SIZE)));
    }
    // The task inventory can be refreshed while the upload is in flight,
    // invalidating the raw item pointer returned by validatePublishedItem().
    // Keep stable identifiers for use after await_async_result().
    const LLUUID prim_id = prim->getID();
    const LLUUID item_id = item->getUUID();

    // A notecard's embedded items -- landmarks, textures, anything dropped
    // into it -- are kept beside its text, and a save of the text alone
    // would lose every one. One that holds any is left to the viewer's
    // editor. One with no asset yet, new and never saved, holds none.
    if (item->getAssetUUID().notNull())
    {
        LLNotecard         current;
        std::istringstream istr(fetch_item_asset(prim, item, LLAssetType::AT_NOTECARD));
        if (current.importStream(istr) && !current.getItems().empty())
        {
            throw LLJSONRPCConnection::ForbiddenError(
                "The notecard holds embedded items, which saving its text would lose; edit it in the viewer");
        }
        // The fetch waited: the prim is found again, and the item not used.
        prim = gObjectList.findObject(prim_id);
        if (!prim)
        {
            throw LLJSONRPCConnection::InvalidParams("Prim not found");
        }
    }

    std::string url = region_of(prim)->getCapability("UpdateNotecardTaskInventory");
    if (url.empty())
        throw LLJSONRPCConnection::InternalError("UpdateNotecardTaskInventory capability not available");

    // Use LLNotecard to produce the proper notecard format
    LLNotecard notecard;
    notecard.setText(content);

    std::ostringstream ostr;
    notecard.exportStream(ostr);

    LLSD cb_result = await_async_result(
        "objectContentSaveNotecard", NOTECARD_UPLOAD_TIMEOUT, "Notecard upload timed out",
        [&, prim_id, item_id](const std::string& pump_name)
        {
            auto [on_success, on_failure] = make_asset_upload_callbacks(pump_name);
            LLResourceUploadInfo::ptr_t uploadInfo(std::make_shared<LLBufferedAssetUploadInfo>(
                prim_id, item_id,
                LLAssetType::AT_NOTECARD, ostr.str(),
                std::move(on_success), std::move(on_failure)));
            LLViewerAssetUpload::EnqueueInventoryUpload(url, uploadInfo);
        });

    if (cb_result.has("failed"))
        throw LLJSONRPCConnection::InternalError("Upload failed: " + cb_result["reason"].asString());

    LLSD response;
    response["success"] = true;
    response["prim_id"] = prim_id;
    response["item_id"] = item_id;

    // If the notecard is open in the viewer's editor, update it
    LLSD floater_key;
    floater_key["taskid"] = prim_id;
    floater_key["itemid"] = item_id;
    LLPreviewNotecard* nc = LLFloaterReg::findTypedInstance<LLPreviewNotecard>("preview_notecard", floater_key);
    if (nc)
    {
        LLViewerTextEditor* nc_editor = nc->getChild<LLViewerTextEditor>("Notecard Editor");
        if (nc_editor)
        {
            nc_editor->setText(content);
            nc_editor->makePristine();
        }
    }

    return response;
}

LLSD LLScriptEditorWSServer::handleObjectItemDelete(U32 connection_id, const LLSD& params)
{
    auto v = validatePublishedItem(params, PERM_MODIFY);

    const LLUUID prim_id = v.prim->getID();
    const LLUUID root_id = v.root->getID();
    const LLUUID item_id = v.item->getUUID();

    // Optimistic local delete then emit immediate update
    // for published clients and request authoritative server refresh.
    v.prim->removeInventory(item_id);
    onPrimInventoryChanged(root_id, prim_id);

    if (!mPublishedObjectManager.hasInventoryRequestStart(prim_id))
    {
        v.prim->dirtyInventory();
        mPublishedObjectManager.setInventoryRequestStart(
            prim_id,
            LLTimer::getTotalSeconds().value());
        v.prim->requestInventory();
    }

    LLSD response;
    response["success"] = true;
    response["prim_id"] = prim_id;
    response["item_id"] = item_id;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectUnpublish(U32 connection_id, const LLSD& params)
{
    LLUUID object_id = params["object_id"].asUUID();
    if (object_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams("object_id is required");
    }

    if (!mPublishedObjectManager.hasPublished(object_id))
    {
        throw LLJSONRPCConnection::InvalidParams("Object is not published");
    }

    unpublishObject(object_id, "manual");

    LLSD response;
    response["success"]   = true;
    response["object_id"] = object_id;
    return response;
}

LLSD LLScriptEditorWSServer::handleObjectItemCreate(const std::string& method, const LLSD& id, const LLSD& params)
{
    std::string type = params["type"].asString();
    if (type != "script" && type != "notecard")
    {
        throw LLJSONRPCConnection::InvalidParams("Unsupported item type: " + type);
    }

    LLUUID prim_id = params["prim_id"].asUUID();
    if (prim_id.isNull())
    {
        throw LLJSONRPCConnection::InvalidParams("prim_id is required");
    }

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
    {
        throw LLJSONRPCConnection::InvalidParams("Prim not found");
    }

    LLViewerObject* root = prim->getRootEdit();
    if (!root || !isObjectPublished(root->getID()))
    {
        throw LLJSONRPCConnection::ForbiddenError("Object is not published");
    }

    // Nothing goes into a prim the agent may not change: the simulator
    // would drop the request without a word, and the wait for the item
    // run out.
    if (!prim->permModify())
    {
        throw LLJSONRPCConnection::ForbiddenError("No modify permission on prim");
    }

    std::string name = params["name"].asString();
    if (name.empty())
    {
        throw LLJSONRPCConnection::InvalidParams("name is required");
    }

    bool has_cap = !region_of(prim)->getCapability("CreateTaskInventoryItem").empty();

    if (type == "notecard" && !has_cap)
    {
        throw LLJSONRPCConnection::ForbiddenError("Notecard creation requires CreateTaskInventoryItem capability");
    }

    // Resolve type-specific fields
    LLAssetType::EType asset_type;
    LLInventoryType::EType inv_type;
    U8 sub_type = 0;
    const char* perm_key;
    LLSD cap_params;

    if (type == "script")
    {
        std::string vm = params["vm"].asString();
        if (vm == "luau")
        {
            sub_type = SST_LUA;
        }
        else if (vm == "mono" || vm == "lsl2")
        {
            sub_type = SST_LSL;
        }
        else
        {
            throw LLJSONRPCConnection::InvalidParams("vm must be 'luau', 'mono', or 'lsl2'");
        }

        asset_type            = LLAssetType::AT_LSL_TEXT;
        inv_type              = LLInventoryType::IT_LSL;
        perm_key              = "Scripts";
        cap_params["enabled"] = true;
        cap_params["vm"]      = vm;
    }
    else
    {
        asset_type = LLAssetType::AT_NOTECARD;
        inv_type   = LLInventoryType::IT_NOTECARD;
        perm_key   = "Notecards";
        if (params.has("text"))
        {
            cap_params["text"] = params["text"].asString();
        }
    }

    LLPermissions perms;
    perms.init(gAgent.getID(), gAgent.getID(), LLUUID::null, LLUUID::null);
    perms.initMasks(
        PERM_ALL,
        PERM_ALL,
        LLFloaterPerms::getEveryonePerms(perm_key),
        LLFloaterPerms::getGroupPerms(perm_key),
        PERM_MOVE | LLFloaterPerms::getNextOwnerPerms(perm_key));

    std::string desc;
    LLViewerAssetType::generateDescriptionFor(asset_type, desc);

    // Snapshot existing item IDs before creation
    std::set<LLUUID> existing_items;
    {
        LLInventoryObject::object_list_t inv;
        prim->getInventoryContents(inv);
        for (auto& obj : inv)
        {
            existing_items.insert(obj->getUUID());
        }
    }

    // Set up event pump to wait for inventory change
    LLEventMailDrop result_pump("objectItemCreate." + LLUUID::generateNewID().asString(), true);

    // Reject if another item.create is already in flight for this prim; the
    // map keys by prim, so two concurrent creates would clobber one another.
    if (!mPublishedObjectManager.reservePendingItemCreate(prim_id, result_pump.getName()))
    {
        throw LLJSONRPCConnection::InvalidRequest(
            "An item.create is already in flight for this prim");
    }

    // RAII: guarantee the pending entry is cleared on every exit path (throw
    // or normal return); until then, every change to the prim's inventory
    // is posted to the pump. Uses a shared_ptr custom deleter as a
    // lightweight scope guard.
    std::shared_ptr<void> pending_guard(nullptr, [this, prim_id](void*)
    {
        mPublishedObjectManager.clearPendingItemCreate(prim_id);
    });

    if (has_cap)
    {
        prim->createInventoryItem(asset_type, inv_type, sub_type, name, desc, perms, cap_params,
            [pump_name = result_pump.getName()](bool success, const LLSD& answer)
            {
                // Told from an inventory change by `answered`; posted, not
                // obtained, since the pump is gone where the wait ran out
                // first, and obtaining it would make one nobody reads.
                LLEventPumps::instance().post(
                    pump_name, LLSD().with("answered", true).with("success", success).with("answer", answer));
            });
    }
    else
    {
        // Fallback: legacy RezScript UDP (scripts only -- notecards already rejected above)
        LLPointer<LLViewerInventoryItem> new_item =
            new LLViewerInventoryItem(
                LLUUID::null, LLUUID::null, perms, LLUUID::null,
                asset_type, inv_type, name, desc, LLSaleInfo::DEFAULT,
                LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK & sub_type,
                time_corrected());
        prim->saveScript(new_item, true, true, LLUUID::null);
    }

    // The new item, where the prim's inventory holds one it did not before.
    auto find_created = [&](LLViewerObject* in, LLSD& response)
    {
        LLInventoryObject::object_list_t inv;
        in->getInventoryContents(inv);
        for (auto& obj : inv)
        {
            if (existing_items.find(obj->getUUID()) != existing_items.end())
            {
                continue;
            }
            LLInventoryItem* created = dynamic_cast<LLInventoryItem*>(obj.get());
            if (!created || created->getType() != asset_type)
            {
                continue;
            }
            response["item_id"]     = created->getUUID();
            response["name"]        = created->getName();
            response["description"] = created->getDescription();
            response["type"]        = type;

            if (type == "script")
            {
                response["subtype"] = static_cast<S32>(created->getInventorySubType());
                const std::string& runtime = created->getRuntime();
                if (!runtime.empty())
                {
                    response["vm"] = runtime;
                }
            }

            const LLPermissions& item_perms = created->getPermissions();
            LLSD perm_entry;
            perm_entry["owner"]      = static_cast<S32>(item_perms.getMaskOwner());
            perm_entry["next_owner"] = static_cast<S32>(item_perms.getMaskNextOwner());
            response["permissions"]  = perm_entry;
            response["creator_id"]   = item_perms.getCreator();
            response["prim_id"]      = prim_id;
            return true;
        }
        return false;
    };

    // The capability's answer, which names the item; or -- on the legacy
    // path, or where the answer names none -- the prim's inventory coming
    // to hold it. A change to the inventory that is some other item's is
    // waited past, for as long as the time allows.
    const F64 deadline = LLTimer::getTotalSeconds().value() + ITEM_CREATE_TIMEOUT;
    LLSD      response;
    for (;;)
    {
        const F64 left = deadline - LLTimer::getTotalSeconds().value();
        if (left <= 0.0)
        {
            throw LLJSONRPCConnection::RequestTimeoutError("Timed out waiting for item creation");
        }
        const LLSD event = llcoro::suspendUntilEventOnWithTimeout(result_pump, static_cast<F32>(left), LLSD().with("timeout", true));
        if (event.has("timeout"))
        {
            throw LLJSONRPCConnection::RequestTimeoutError("Timed out waiting for item creation");
        }

        prim = gObjectList.findObject(prim_id);
        if (!prim)
        {
            throw LLJSONRPCConnection::InternalError("Prim no longer exists");
        }

        const bool  answered = event["answered"].asBoolean();
        const LLSD& answer   = event["answer"];
        if (answered && event["success"].asBoolean() && answer["item_id"].asUUID().notNull())
        {
            response["item_id"]     = answer["item_id"];
            response["name"]        = answer["name"];
            response["description"] = desc;
            response["type"]        = type;
            response["prim_id"]     = prim_id;

            if (type == "script")
            {
                response["subtype"] = static_cast<S32>(sub_type);
            }

            LLSD perm_entry;
            perm_entry["owner"]      = static_cast<S32>(perms.getMaskOwner());
            perm_entry["next_owner"] = static_cast<S32>(perms.getMaskNextOwner());
            response["permissions"]  = perm_entry;
            response["creator_id"]   = gAgent.getID();
            return response;
        }

        if (find_created(prim, response))
        {
            return response;
        }
        if (answered && !event["success"].asBoolean())
        {
            const std::string why = answer["message"].asString();
            throw LLJSONRPCConnection::InternalError("Item creation failed" + (why.empty() ? std::string() : ": " + why));
        }
    }
}


void LLScriptEditorWSServer::notifyScript(const std::string& script_id, const std::string &method, const LLSD& message) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    auto it = mSubscriptions.find(script_id);
    if (it != mSubscriptions.end())
    {
        auto connection = it->second.mConnection.lock();
        if (connection)
        {
            connection->notify(method, message);
        }
    }
}


void LLScriptEditorWSServer::sendUnsubscribeScriptEditor(const std::string& script_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLSD params;
    params["script_id"] = script_id;

    notifyScript(script_id, "script.unsubscribe", params);
}

void LLScriptEditorWSServer::sendCompileResults(const std::string &script_id, const LLSD &results) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    auto it = mSubscriptions.find(script_id);
    if (it == mSubscriptions.end())
    {
        return;
    }
    const bool lua = it->second.mLua;
    notifyScript(script_id, "script.compiled",
                 compiledMessage(script_id, results["compiled"].asBoolean(), results["is_running"].asBoolean(),
                                 ALScriptWorkspace::parseDiagnostics(results["errors"], lua), lua));
}

// static
LLSD LLScriptEditorWSServer::compiledMessage(const std::string& script_id, bool success, bool running,
                                             const std::vector<ALScriptWorkspace::Diagnostic>& diagnostics, bool lua)
{
    LLSD params;
    params["script_id"]   = script_id;
    params["success"]     = success;
    params["running"]     = running;
    params["diagnostics"] = LLSD::emptyArray();
    for (const ALScriptWorkspace::Diagnostic& diagnostic : diagnostics)
    {
        // The protocol counts from one, and says zero for a place the
        // compiler did not name.
        LLSD entry;
        entry["row"]     = diagnostic.line + 1;
        entry["column"]  = diagnostic.hasColumn ? diagnostic.column + 1 : 0;
        entry["level"]   = diagnostic.level.empty() ? std::string("ERROR") : diagnostic.level;
        entry["message"] = diagnostic.message;
        if (!lua)
        {
            entry["format"] = "lsl";
        }
        params["diagnostics"].append(entry);
    }
    return params;
}

namespace
{
    // Whether an item is a Luau script.
    bool isLuaItem(const LLInventoryItem* item)
    {
        return item && item->getType() == LLAssetType::AT_LSL_TEXT && item->getInventorySubType() == SST_LUA;
    }
}

void LLScriptEditorWSServer::sendCompiled(const ALScriptWorkspace::CompileResult& result)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // Nothing compiled: a notecard, or an upload that failed.
    if (result.notecard || !result.error.empty() || result.ref.item.isNull())
    {
        return;
    }
    const std::string script_id = buildScriptSubscriptionId(result.ref.object, result.ref.item);
    const auto        subscribed = mSubscriptions.find(script_id);

    LLViewerObject* prim = result.ref.inInventory() ? nullptr : gObjectList.findObject(result.ref.object);
    LLUUID          root_id;
    if (prim)
    {
        root_id = prim->getRootEdit() ? prim->getRootEdit()->getID() : prim->getID();
    }
    const bool published = root_id.notNull() && isObjectPublished(root_id);
    if (subscribed == mSubscriptions.end() && !published)
    {
        return;
    }
    const LLInventoryItem* item = result.ref.inInventory() ? gInventory.getItem(result.ref.item) : prim ? prim->getInventoryItem(result.ref.item) : nullptr;
    const bool             lua  = subscribed != mSubscriptions.end() ? subscribed->second.mLua : isLuaItem(item);

    LLSD message = compiledMessage(script_id, result.success, result.running, result.diagnostics, lua);
    if (prim)
    {
        message["object_id"] = root_id;
        message["prim_id"]   = result.ref.object;
    }
    message["item_id"] = result.ref.item;
    if (subscribed != mSubscriptions.end())
    {
        notifyScript(script_id, "script.compiled", message);
    }
    else
    {
        notifyAll("script.compiled", message);
    }

    // The item's asset is another now: the prim's inventory fetched
    // again, so that the object.update that follows says so.
    if (published && prim && result.success && !mPublishedObjectManager.hasInventoryRequestStart(prim->getID()))
    {
        prim->dirtyInventory();
        mPublishedObjectManager.setInventoryRequestStart(prim->getID(), LLTimer::getTotalSeconds().value());
        prim->requestInventory();
    }
}

void LLScriptEditorWSServer::sendRuntimeEvent(const ALScriptWorkspace::RuntimeEvent& event) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;

    std::string script_id;
    if (event.item.notNull())
    {
        script_id = buildScriptSubscriptionId(event.prim, event.item);
    }

    if (!isObjectPublished(event.root) &&
        (script_id.empty() || mSubscriptions.find(script_id) == mSubscriptions.end()))
    {
        return;
    }

    LLSD message;
    if (!script_id.empty())
    {
        message["script_id"] = script_id;
    }
    message["object_id"] = event.root;
    message["prim_id"] = event.prim;
    message["item_id"] = event.item;
    message["object_name"] = event.objectName;
    message["message"] = event.message;

    LLSD item;
    item["root_id"] = event.root;
    item["prim_id"] = event.prim;
    item["item_id"] = event.item;
    item["name"] = event.scriptName;
    item["language"] = event.lua ? "luau" : "lsl";
    message["item"] = item;

    switch (event.channel)
    {
    case ALScriptWorkspace::RuntimeEvent::Channel::Debug:
        message["channel"] = "debug";
        break;
    case ALScriptWorkspace::RuntimeEvent::Channel::OwnerSay:
        message["channel"] = "owner_say";
        break;
    }

    if (event.isError)
    {
        // The protocol counts from one, and says zero for a place the
        // message did not name.
        message["error"] = event.error;
        message["line"] = event.line < 0 ? 0 : event.line + 1;
        message["column"] = event.column < 0 ? 0 : event.column + 1;
        message["stack"] = LLSD::emptyArray();
        for (const auto& line : event.stack)
        {
            message["stack"].append(line);
        }
    }

    notifyAll(event.isError ? "runtime.error" : "runtime.debug", message);
}

void LLScriptEditorWSServer::notifyConnection(U32 connection_id, const std::string& method, const LLSD& params) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLScriptEditorWSConnection::ptr_t connection;
    {
        LLMutexLock lock(&mConnectionsMutex);
        auto        it = mActiveConnections.find(connection_id);
        if (it != mActiveConnections.end())
        {
            connection = it->second.lock();
        }
    }
    if (connection && connection->isAuthenticated())
    {
        connection->notify(method, params);
    }
}

void LLScriptEditorWSServer::notifyAll(const std::string& method, const LLSD& params) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // Serialize once, deliver many: build the JSON-RPC envelope and its wire
    // string a single time, then hand the bytes to each connection.
    LLSD envelope = LLJSONRPCConnection::makeEnvelope(
        LLSD(), method, params, LLSD(), LLSD());
    std::string payload = LlsdToJson(envelope);

    // The connections taken under the lock, the sending done outside it;
    // one that has not proven itself hears nothing.
    std::vector<LLScriptEditorWSConnection::ptr_t> connections;
    {
        LLMutexLock lock(&mConnectionsMutex);
        for (const auto& pair : mActiveConnections)
        {
            auto connection = pair.second.lock();
            if (connection && connection->isAuthenticated())
            {
                connections.push_back(std::move(connection));
            }
        }
    }
    for (const auto& connection : connections)
    {
        connection->sendMessage(payload);
    }
}


// static
std::string LLScriptEditorWSServer::getPrimName(LLViewerObject* obj)
{
    std::string name = nv_string(obj, "Name");
    if (!name.empty())
    {
        return name;
    }

    if (!obj)
    {
        return std::string();
    }

    LLSelectNode* node = LLSelectMgr::instance().getSelection()->findNode(obj);
    if (node && !node->mName.empty())
    {
        return node->mName;
    }

    // Never emit an empty prim/object name to downstream tooling.
    return obj->getID().asString();
}

bool LLScriptEditorWSServer::publishObject(const LLUUID& object_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLViewerObject* root = gObjectList.findObject(object_id);
    if (!root)
    {
        LL_WARNS("ScriptEditorWS") << "publishObject: object not found: " << object_id << LL_ENDL;
        return false;
    }

    if (!root->permModify())
    {
        LL_WARNS("ScriptEditorWS") << "publishObject: no modify permission on object: " << object_id << LL_ENDL;
        return false;
    }

    // If already published, unpublish first to replace cleanly
    if (isObjectPublished(object_id))
    {
        unpublishObject(object_id, "republish");
    }

    // Collect root + all children
    std::vector<LLViewerObject*> prims = LLPublishedObjectMgr::linksetOf(root);

    // Request object properties for each prim in the linkset (root + children),
    // matching the hover path so name/description metadata is refreshed.
    for (LLViewerObject* prim : prims)
    {
        LLSelectMgr::instance().requestObjectPropertiesFamily(prim);
    }

    // Set up a PendingPublish to coordinate inventory loading across all prims.
    // We register a listener and call requestInventory() on every prim.
    // If inventory is already loaded, requestInventory() fires the callback
    // synchronously via doInventoryCallback(), so all_ready will naturally
    // become true before this function returns in the common case.
    mPublishedObjectManager.beginPendingPublish(object_id, prims);

    // Request inventory for each prim. If already loaded, onPrimInventoryReady()
    // will be called immediately (possibly building and sending the publish
    // before this loop even finishes).
    for (LLViewerObject* prim : prims)
    {
        if (!mPublishedObjectManager.hasPendingPublish(object_id))
        {
            break;  // publish completed synchronously during a previous iteration
        }
        mPublishedObjectManager.setInventoryRequestStart(prim->getID(), LLTimer::getTotalSeconds().value());
        prim->requestInventory();
    }

    return true;
}

bool LLScriptEditorWSServer::isObjectPublished(const LLUUID& object_id) const
{
    return mPublishedObjectManager.hasPublished(object_id);
}

// The publishing's bookkeeping lives in the manager; these are the
// world's way in, kept on the server since that is what the world holds.
void LLScriptEditorWSServer::onPrimInventoryReady(const LLUUID& object_id, const LLUUID& prim_id)
{
    mPublishedObjectManager.onPrimInventoryReady(object_id, prim_id);
}

void LLScriptEditorWSServer::onLinksetChildAdded(const LLUUID& root_id, LLViewerObject* child)
{
    mPublishedObjectManager.onLinksetChildAdded(root_id, child);
}

void LLScriptEditorWSServer::onLinksetChildRemoved(const LLUUID& root_id, const LLUUID& child_id)
{
    mPublishedObjectManager.onLinksetChildRemoved(root_id, child_id);
}

void LLScriptEditorWSServer::onPrimInventoryChanged(const LLUUID& object_id, const LLUUID& prim_id)
{
    mPublishedObjectManager.onPrimInventoryChanged(object_id, prim_id);
}

void LLScriptEditorWSServer::onObjectPropertyChanged(
    const LLUUID& prim_id, const std::string& name, const std::string& desc, S16 inventory_serial)
{
    mPublishedObjectManager.onObjectPropertyChanged(prim_id, name, desc, inventory_serial);
}

void LLScriptEditorWSServer::unpublishObject(const LLUUID& object_id, const std::string& reason)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!mPublishedObjectManager.cleanupObjectStateForUnpublish(object_id))
    {
        return;
    }

    LLSD message;
    message["object_id"] = object_id;
    if (!reason.empty())
    {
        message["reason"] = reason;
    }
    notifyAll("object.unpublish", message);

    LL_DEBUGS("ScriptEditorWS") << "Unpublished object " << object_id
        << " reason: " << reason << LL_ENDL;
}


//========================================================================
std::atomic<U32> LLScriptEditorWSConnection::sNextConnectionID{1};

std::shared_ptr<LLScriptEditorWSServer> LLScriptEditorWSConnection::getServer() const
{
    return std::static_pointer_cast<LLScriptEditorWSServer>(mOwningServer.lock());
}

void LLScriptEditorWSConnection::onOpen()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // A few at a time may be proving themselves: this one counts too.
    auto server = getServer();
    if (server && server->unauthenticatedConnectionCount() > MAX_UNAUTHENTICATED)
    {
        LL_WARNS_ONCE("ScriptEditorWS") << "Too many connections waiting to authenticate; closing new ones" << LL_ENDL;
        closeConnection(1013, "Try again later");
        return;
    }

    // Call parent class to set up JSON-RPC infrastructure
    LLJSONRPCConnection::onOpen();

    LL_INFOS("ScriptEditorWS") << "Script editor JSON-RPC connection opened" << LL_ENDL;

    // Who is logged in, the syntax in use and the challenge's file are
    // the main thread's to say: the handshake goes from there.
    wptr_t that = weak_from_this();
    const bool posted = postToMainThread(
        [that]()
        {
            if (auto self = that.lock())
            {
                self->sendHandshake();
            }
        });
    if (!posted)
    {
        LL_WARNS("ScriptEditorWS") << "Main loop not taking work; closing the new connection" << LL_ENDL;
        closeConnection(1013, "Try again later");
    }
}

void LLScriptEditorWSConnection::sendHandshake()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    llassert(on_main_thread());
    if (isClosed())
    {
        return;
    }

    // Build hello data
    LLSD handshake;
    handshake["server_version"]   = "1.0.0";
    handshake["protocol_version"] = "1.0";
    handshake["viewer_name"]      = LLVersionInfo::instance().getChannel();
    handshake["viewer_version"]   = LLVersionInfo::instance().getVersion();

    // Who is logged in is told once the client has proven itself, with
    // session.ok: anything can connect and be sent this. Read here, on the
    // main thread; kept as a plain id and string, which -- unlike an LLSD --
    // may be read on the server's thread as a copy here goes.
    const LLUUID      agent_id   = gAgent.getID();
    const std::string agent_name = gAgentUsername;

    const Challenge challenge = writeChallenge();
    if (challenge.mFile.empty())
    {
        // Nothing to prove itself by, so nothing it may do.
        sendDisconnect(DisconnectReason::INTERNAL_ERROR, "Unable to issue a challenge");
        return;
    }
    handshake["challenge"] = challenge.mFile;

    LLSD languages = LLSD::emptyArray();
    languages.append("lsl");
    languages.append("luau");
    handshake["languages"] = languages;
    handshake["syntax_id"] = LLSyntaxDefCache::instance().getSyntaxID();

    // Features object
    LLSD features;
    features["live_sync"]        = true;
    features["compilation"]      = true;
    features["syntax_cache"]     = true;
    features["commands"]         = true;
    features["unified_diagnostics"] = true;
    handshake["features"]        = features;

    wptr_t that = weak_from_this();

    // The answer comes once, whichever way: from the client, as the
    // time runs out, or as the connection closes.
    const LLSD sent = call(
        "session.handshake", handshake,
        [that, challenge, agent_id, agent_name](const LLSD& result, const LLSD& error)
        {
            // Whatever the answer, the file has done its work.
            LLFile::remove(challenge.mFile);
            auto self = that.lock();
            if (!self)
            {
                return;
            }
            if (error.isDefined())
            {
                self->handleHandshakeError(error);
                return;
            }
            self->handleHandshakeResponse(result, challenge.mSecret, agent_id, agent_name);
        },
        HANDSHAKE_TIMEOUT);
    if (sent.isUndefined())
    {
        LLFile::remove(challenge.mFile);
        if (!isClosed())
        {
            handleHandshakeError(LLSD().with("code", LLJSONRPCConnection::RPCError::INTERNAL_ERROR).with("message", "Handshake not sent"));
        }
        return;
    }

    LL_INFOS("ScriptEditorWS") << "Sent handshake call to new editor client" << LL_ENDL;
}

void LLScriptEditorWSConnection::onClose()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // Call parent class to clean up JSON-RPC infrastructure. What the
    // client said of itself, and the server it came to, go with the
    // connection: the main thread may be reading them as this closes.
    LLJSONRPCConnection::onClose();
}

void LLScriptEditorWSConnection::sendDisconnect(DisconnectReason reason, const std::string& message)
{
    LL_INFOS("ScriptEditorWS") << "Sending disconnect to client: " << message << LL_ENDL;
    LLSD params;
    params["reason"]  = static_cast<S32>(reason);
    params["message"] = message;
    notify("session.disconnect", params);
    closeConnection(1000, message);
}

void LLScriptEditorWSConnection::handleHandshakeError(const LLSD& error)
{
    const S32 code = error["code"].asInteger();
    if (code == LLJSONRPCConnection::RPCError::CONNECTION_CLOSED)
    {
        // Gone already.
        return;
    }
    LL_WARNS("ScriptEditorWS") << "Handshake failed: " << error["message"].asString() << LL_ENDL;
    sendDisconnect(code == LLJSONRPCConnection::RPCError::REQUEST_TIMEOUT ? DisconnectReason::TIMEOUT : DisconnectReason::PROTOCOL_ERROR,
                   "Handshake failed");
}

void LLScriptEditorWSConnection::handleHandshakeResponse(const LLSD& result, const LLUUID& secret,
                                                         const LLUUID& agent_id, const std::string& agent_name)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LL_INFOS("ScriptEditorWS") << "Processing handshake response from client" << LL_ENDL;

    // Only something that could read the file knows what it says.
    if (!result.isMap() || result["challenge_response"].asUUID() != secret)
    {
        LL_WARNS("ScriptEditorWS") << "Invalid or missing challenge response from client" << LL_ENDL;
        sendDisconnect(DisconnectReason::PROTOCOL_ERROR, "Invalid challenge response");
        return;
    }

    const std::string protocol_version = result["protocol_version"].asString();
    if (protocol_version != "1.0")
    {
        LL_WARNS("ScriptEditorWS") << "Protocol version mismatch. Expected: 1.0, Got: "
                                    << protocol_version << LL_ENDL;
    }

    {
        // Written here, on the server's thread, and read on the main one.
        LLMutexLock lock(&mHandshakeMutex);
        mClientName      = result["client_name"].asString();
        mClientVersion   = result["client_version"].asString();
        mProtocolVersion = protocol_version;
        mScriptName      = result["script_name"].asString();
        mScriptLanguage  = result["script_language"].asString();

        for (const auto& lang : llsd::inArray(result["languages"]))
        {
            if (lang.isString())
            {
                mLanguages.insert(lang.asString());
            }
        }

        for (const auto& [feature, enabled] : llsd::inMap(result["features"]))
        {
            if (enabled.asBoolean())
            {
                mFeatures.insert(feature);
            }
        }
    }

    // Let in before it is told so: what it asks on hearing it must be
    // answered. Who is logged in is told now, to a client that has shown
    // it runs as the user.
    setAuthenticated(true);
    LLSD ok;
    ok["agent_id"]   = agent_id;
    ok["agent_name"] = agent_name;
    notify("session.ok", ok);

    LL_INFOS("ScriptEditorWS") << "Handshake completed successfully." << LL_ENDL;
}

// static
LLScriptEditorWSConnection::Challenge LLScriptEditorWSConnection::writeChallenge()
{
    // A secret nobody could work out: a new id is made of the time and
    // the network card's address, which are anybody's to know.
    Challenge challenge;
    if (RAND_bytes(challenge.mSecret.mData, UUID_BYTES) != 1)
    {
        LL_WARNS("ScriptEditorWS") << "Unable to make a challenge secret" << LL_ENDL;
        return {};
    }

    // Named for nothing it holds, made new rather than written over
    // anything, or through a link, already by that name, and readable by
    // the user alone: the name goes to whoever connects, and the temp
    // folder can be everyone's.
    const std::string file = LLFile::tmpdir() + "sl_script_challenge_" + LLUUID::generateNewID().asString() + ".tmp";
    const std::string text = challenge.mSecret.asString();
    std::error_code   ec;
    LLFile            out(file, LLFile::out | LLFile::noreplace, ec, 0600);
    if (ec)
    {
        // Whatever is there by that name is somebody else's, and stays.
        LL_WARNS("ScriptEditorWS") << "Unable to make challenge file " << file << ": " << ec.message() << LL_ENDL;
        return {};
    }
    const bool written = out.write(text.data(), static_cast<S64>(text.size()), ec) == static_cast<S64>(text.size()) && !ec;
    if (out.close(ec) != 0 || !written)
    {
        LL_WARNS("ScriptEditorWS") << "Unable to write challenge file " << file << LL_ENDL;
        LLFile::remove(file);
        return {};
    }

    challenge.mFile = file;
    return challenge;
}

