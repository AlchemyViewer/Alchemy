/**
 * @file alscriptworkspace.cpp
 * @brief The scripts the viewer can reach: one way to name, load, save and compile them.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptworkspace.h"

#include "llagent.h"
#include "llappviewer.h"
#include "llassetstorage.h"
#include "llchat.h"
#include "lldate.h"
#include "lleventtimer.h"
#include "llfilesystem.h"
#include "llinventory.h"
#include "llinventorymodel.h"
#include "llnotificationsutil.h"
#include "llscripteditorws.h"
#include "llviewerassetupload.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "message.h"
// [RLVa:KB]
#include "rlvhandler.h"
#include "rlvlocks.h"
// [/RLVa:KB]

#include <boost/regex.hpp>

#include <memory>

namespace
{
    // What the compilers say about where a problem is. Luau's names the
    // chunk and a one-based line; LSL's gives a zero-based line and column
    // as the viewer scrolls to them.
    const boost::regex LUAU_LOCATION(R"(^([^:]*):([0-9]+):\s*(.*)$)");
    const boost::regex LSL_LOCATION(R"(\((\d+), (\d+)\) : ([^:]+) : (.+))");
    const boost::regex DEFAULT_STATE(R"(\s*default\s*\{)");

    // How a script's run-time error starts: the object, the script, and
    // the words.
    const boost::regex RUNTIME_ERROR_HEADER(R"(^(.+?)\s+\[script:([^\]]+)\]\s+Script run-time error)");
    const char* const  RUNTIME_ERROR_MARKER = "Script run-time error";
    // How long after a line the lines that belong with it may still come,
    // and how often that is looked at.
    const F32    BURST_TIMEOUT      = 1.0f;
    const F32    BURST_FLUSH_PERIOD = 0.25f;
    const size_t RECENT_RUNTIME     = 500;

    bool endsWith(const std::string& text, const char* suffix)
    {
        const size_t n = strlen(suffix);
        return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
    }

    // The script a message names, in a prim's contents.
    LLInventoryItem* scriptNamed(LLViewerObject* prim, const std::string& name)
    {
        LLInventoryObject::object_list_t contents;
        prim->getInventoryContents(contents);
        for (const auto& object : contents)
        {
            LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(object.get());
            if (item && item->getName() == name)
            {
                return item;
            }
        }
        return nullptr;
    }
}

// The lines one script has said within a moment of each other, not yet
// delivered.
struct ALScriptWorkspace::Burst
{
    LLUUID                   fromId;
    std::string              fromName;
    bool                     lua = false;
    RuntimeEvent::Channel    channel = RuntimeEvent::Channel::Debug;
    std::vector<std::string> texts;
    LLTimer                  timer;
};

// --- ALScriptRef -------------------------------------------------------------

std::string ALScriptRef::id() const
{
    return LLScriptEditorWSServer::buildScriptSubscriptionId(object, item);
}

LLSD ALScriptRef::key() const
{
    LLSD key;
    key["taskid"] = object;
    key["itemid"] = item;
    return key;
}

ALScriptRef ALScriptRef::fromKey(const LLSD& key)
{
    if (key.isMap())
    {
        return ALScriptRef(key["taskid"].asUUID(), key["itemid"].asUUID());
    }
    return ALScriptRef(LLUUID::null, key.asUUID());
}

// --- language ------------------------------------------------------------------

ALScriptWorkspace::ALScriptWorkspace() = default;

ALScriptWorkspace::~ALScriptWorkspace() = default;

bool ALScriptWorkspace::looksLikeLua(std::string_view content)
{
    return !boost::regex_search(content.begin(), content.end(), DEFAULT_STATE);
}

ALScriptWorkspace::Language ALScriptWorkspace::resolve(const LLInventoryItem* item, std::string_view content, const std::string& requested)
{
    Language language;
    language.lua       = item && item->getInventorySubType() == SST_LUA;
    std::string target = requested;
    if (target.empty() && item)
    {
        target = item->getRuntime();
    }
    // An LSL script run on Luau is the LSL-on-Luau target, whatever asked.
    if (!language.lua && target == "luau")
    {
        target = "lsl-luau";
    }
    if (target.empty())
    {
        if (!language.lua)
        {
            language.lua = looksLikeLua(content);
        }
        target = language.lua ? "luau" : "mono";
    }
    language.compileTarget = target;
    return language;
}

bool ALScriptWorkspace::readAsset(const LLUUID& asset_id, LLAssetType::EType type, std::string& out)
{
    if (!LLFileSystem::getExists(asset_id, type))
    {
        out.clear();
        return false;
    }
    LLFileSystem file(asset_id, type);
    const S32    size = file.getSize();
    if (size <= 0)
    {
        out.clear();
        return true;
    }
    out.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<U8*>(out.data()), size))
    {
        out.clear();
        return false;
    }
    const size_t nul = out.find('\0');
    if (nul != std::string::npos)
    {
        out.resize(nul);
    }
    return true;
}

// --- loading -----------------------------------------------------------------

struct ALScriptWorkspace::LoadRequest
{
    load_callback_t callback;
    Loaded          answer;
};

void ALScriptWorkspace::load(const ALScriptRef& ref, load_callback_t callback)
{
    Loaded answer;
    answer.ref = ref;
    if (ref.inInventory())
    {
        LLViewerInventoryItem* item = gInventory.getItem(ref.item);
        if (!item)
        {
            answer.error = "no such item";
            callback(answer);
            return;
        }
        // A library script may be read without being modifiable; anyone
        // else's needs both copy and modify, unless the agent is a god.
        const bool library   = !gInventory.isObjectDescendentOf(ref.item, gInventory.getRootFolderID());
        const bool copyable  = gAgent.allowOperation(PERM_COPY, item->getPermissions(), GP_OBJECT_MANIPULATE);
        answer.modifiable    = gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE);
        answer.viewable      = gAgent.isGodlike() || (copyable && (answer.modifiable || library));
        answer.name          = item->getName();
        answer.assetId       = item->getAssetUUID();
        if (!answer.viewable)
        {
            answer.error = "not permitted";
            callback(answer);
            return;
        }
        auto* request = new LoadRequest{ std::move(callback), std::move(answer) };
        gAssetStorage->getInvItemAsset(LLHost(), gAgent.getID(), gAgent.getSessionID(), item->getPermissions().getOwner(),
                                       LLUUID::null, item->getUUID(), item->getAssetUUID(), item->getType(),
                                       &ALScriptWorkspace::onAssetLoaded, request, true);
        return;
    }

    LLViewerObject*  object = gObjectList.findObject(ref.object);
    LLInventoryItem* item   = object ? object->getInventoryItem(ref.item) : nullptr;
    if (!object || !item || !object->getRegion())
    {
        answer.error = object ? "no such item in the object" : "no such object";
        callback(answer);
        return;
    }
    const bool copyable = gAgent.allowOperation(PERM_COPY, item->getPermissions(), GP_OBJECT_MANIPULATE);
    answer.modifiable   = gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE);
    answer.viewable     = gAgent.isGodlike() || (copyable && answer.modifiable);
    answer.name         = item->getName();
    answer.assetId      = item->getAssetUUID();
    if (!answer.viewable)
    {
        answer.error = "not permitted";
        callback(answer);
        return;
    }
    auto* request = new LoadRequest{ std::move(callback), std::move(answer) };
    gAssetStorage->getInvItemAsset(object->getRegion()->getHost(), gAgent.getID(), gAgent.getSessionID(),
                                   item->getPermissions().getOwner(), object->getID(), item->getUUID(), item->getAssetUUID(),
                                   item->getType(), &ALScriptWorkspace::onAssetLoaded, request, true);
}

// static
void ALScriptWorkspace::onAssetLoaded(const LLUUID& asset_id, LLAssetType::EType type, void* user_data, S32 status, LLExtStat ext_status)
{
    std::unique_ptr<LoadRequest> request(static_cast<LoadRequest*>(user_data));
    Loaded&                      answer = request->answer;
    if (status != 0)
    {
        answer.error = LLAssetStorage::getErrorString(status);
    }
    else if (!readAsset(asset_id, type, answer.text))
    {
        answer.error = "the asset could not be read";
    }
    else
    {
        answer.assetId = asset_id;
        // The item as it is now, for the language; the text alone where it
        // has gone.
        const LLInventoryItem* item = nullptr;
        if (answer.ref.inInventory())
        {
            item = gInventory.getItem(answer.ref.item);
        }
        else if (LLViewerObject* object = gObjectList.findObject(answer.ref.object))
        {
            item = object->getInventoryItem(answer.ref.item);
        }
        answer.language = resolve(item, answer.text);
    }
    request->callback(answer);
}

// --- saving and compiling ------------------------------------------------------

std::vector<ALScriptWorkspace::Diagnostic> ALScriptWorkspace::parseDiagnostics(const LLSD& errors, bool lua)
{
    std::vector<Diagnostic> out;
    for (LLSD::array_const_iterator it = errors.beginArray(); it != errors.endArray(); ++it)
    {
        std::string line = it->asString();
        LLStringUtil::stripNonprintable(line);
        Diagnostic   diagnostic;
        boost::smatch found;
        if (lua && boost::regex_match(line, found, LUAU_LOCATION))
        {
            diagnostic.line    = llmax(0, std::atoi(found[2].str().c_str()) - 1);
            diagnostic.level   = "ERROR";
            diagnostic.message = found[3].str();
        }
        else if (!lua && boost::regex_search(line, found, LSL_LOCATION))
        {
            diagnostic.line      = std::atoi(found[1].str().c_str());
            diagnostic.column    = std::atoi(found[2].str().c_str());
            diagnostic.hasColumn = true;
            diagnostic.level     = found[3].str();
            diagnostic.message   = found[4].str();
        }
        else
        {
            diagnostic.level   = "ERROR";
            diagnostic.message = line;
        }
        out.push_back(std::move(diagnostic));
    }
    return out;
}

void ALScriptWorkspace::deliver(const CompileResult& result, const compile_callback_t& callback)
{
    if (callback)
    {
        callback(result);
    }
    mCompiled(result);
}

bool ALScriptWorkspace::save(const ALScriptRef& ref, const std::string& text, const SaveOptions& options, compile_callback_t callback, std::string& error)
{
    // The upload finishes on a coroutine; the answer is handed to the main
    // loop before anything that draws hears of it, as the legacy floaters
    // did, since a text editor's reflow takes a mutex a fiber may not.
    const bool lua = options.compileTarget == "luau";
    auto answered  = [this, ref, lua, callback, running = options.running](const LLSD& response, const LLUUID& new_asset_id) {
        CompileResult result;
        result.ref        = ref;
        result.success    = response["compiled"].asBoolean();
        result.running    = running;
        result.newAssetId = new_asset_id;
        for (LLSD::array_const_iterator it = response["errors"].beginArray(); it != response["errors"].endArray(); ++it)
        {
            result.messages.push_back(it->asString());
        }
        result.diagnostics = parseDiagnostics(response["errors"], lua);
        LLAppViewer::instance()->postToMainCoro([this, result, callback]() { deliver(result, callback); });
    };
    auto failed = [this, ref, callback](LLUUID, LLUUID, LLSD, std::string reason) -> bool {
        CompileResult result;
        result.ref   = ref;
        result.error = reason.empty() ? std::string("the upload failed") : reason;
        LLAppViewer::instance()->postToMainCoro([this, result, callback]() { deliver(result, callback); });
        return true;
    };

    if (ref.inInventory())
    {
        LLViewerRegion* region = gAgent.getRegion();
        if (!region)
        {
            error = "no region";
            return false;
        }
        const std::string url = region->getCapability("UpdateScriptAgent");
        if (url.empty())
        {
            error = "the region cannot update scripts";
            return false;
        }
        LLViewerInventoryItem* item      = gInventory.getItem(ref.item);
        const LLUUID           old_asset = item ? item->getAssetUUID() : LLUUID::null;
        LLResourceUploadInfo::ptr_t info(std::make_shared<LLScriptAssetUpload>(
            ref.item, options.compileTarget, text,
            [answered, old_asset](LLUUID, LLUUID new_asset_id, LLUUID, LLSD response) {
                LLFileSystem::removeFile(old_asset, LLAssetType::AT_LSL_TEXT);
                answered(response, new_asset_id);
            },
            failed));
        LLViewerAssetUpload::EnqueueInventoryUpload(url, info);
        return true;
    }

    LLViewerObject* object = gObjectList.findObject(ref.object);
    if (!object || !object->getRegion())
    {
        error = "no such object";
        return false;
    }
    const std::string url = object->getRegion()->getCapability("UpdateScriptTask");
    if (url.empty())
    {
        error = "the region cannot update scripts";
        return false;
    }
    LLInventoryItem* item      = object->getInventoryItem(ref.item);
    const LLUUID     old_asset = item ? item->getAssetUUID() : LLUUID::null;
    LLResourceUploadInfo::ptr_t info(std::make_shared<LLScriptAssetUpload>(
        ref.object, ref.item, options.compileTarget, options.running, options.experience, text,
        [answered, old_asset](LLUUID, LLUUID, LLUUID new_asset_id, LLSD response) {
            LLFileSystem::removeFile(old_asset, LLAssetType::AT_LSL_TEXT);
            answered(response, new_asset_id);
        },
        failed));
    LLViewerAssetUpload::EnqueueInventoryUpload(url, info);
    return true;
}

// --- a script in an object -------------------------------------------------------

bool ALScriptWorkspace::scriptMessage(const ALScriptRef& ref, const char* message, bool running, bool with_running)
{
    LLViewerObject* object = gObjectList.findObject(ref.object);
    if (!object || !object->getRegion())
    {
        LLNotificationsUtil::add("CouldNotStartStopScript");
        return false;
    }
// [RLVa:KB] - Checked: 2010-09-28 (RLVa-1.2.1f) | Modified: RLVa-1.0.5a
    if (rlv_handler_t::isEnabled() && gRlvAttachmentLocks.isLockedAttachment(object->getRootEdit()))
    {
        RlvUtil::notifyBlockedGeneric();
        return false;
    }
// [/RLVa:KB]
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(message);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_Script);
    msg->addUUIDFast(_PREHASH_ObjectID, ref.object);
    msg->addUUIDFast(_PREHASH_ItemID, ref.item);
    if (with_running)
    {
        msg->addBOOLFast(_PREHASH_Running, running);
    }
    msg->sendReliable(object->getRegion()->getHost());
    return true;
}

bool ALScriptWorkspace::setRunning(const ALScriptRef& ref, bool running)
{
    return scriptMessage(ref, _PREHASH_SetScriptRunning, running, true);
}

bool ALScriptWorkspace::reset(const ALScriptRef& ref)
{
    return scriptMessage(ref, _PREHASH_ScriptReset, false, false);
}

// --- what scripts say ------------------------------------------------------------

void ALScriptWorkspace::ingestChat(const LLChat& chat)
{
    const RuntimeEvent::Channel channel = chat.mChatType == CHAT_TYPE_OWNER ? RuntimeEvent::Channel::OwnerSay : RuntimeEvent::Channel::Debug;
    const std::vector<std::string> lines = LLStringUtil::getTokens(chat.mText, "\n");
    boost::smatch                  match;
    const bool                     header = !lines.empty() && boost::regex_match(lines.front(), match, RUNTIME_ERROR_HEADER);

    // A line that is not the start of an error, with nothing being
    // joined, is an event by itself.
    if (!header && !mBurst)
    {
        Burst alone;
        alone.fromId   = chat.mFromID;
        alone.fromName = chat.mFromName;
        alone.channel  = channel;
        alone.texts.push_back(chat.mText);
        deliverRuntime(alone);
        return;
    }

    // Which VM the script runs on, which its item says.
    bool lua = mBurst ? mBurst->lua : false;
    if (header)
    {
        if (LLViewerObject* prim = gObjectList.findObject(chat.mFromID))
        {
            if (LLInventoryItem* item = scriptNamed(prim, match[2].str()))
            {
                lua = item->getRuntime() == "luau";
            }
        }
    }
    // Another script's line, or another channel's, ends what was being
    // joined; so does a new error's start.
    if (mBurst && (header || mBurst->fromId != chat.mFromID || mBurst->fromName != chat.mFromName || mBurst->channel != channel))
    {
        flushRuntime();
    }
    if (!mBurst)
    {
        mBurst           = std::make_unique<Burst>();
        mBurst->fromId   = chat.mFromID;
        mBurst->fromName = chat.mFromName;
        mBurst->lua      = lua;
        mBurst->channel  = channel;
    }
    mBurst->texts.push_back(chat.mText);
    mBurst->timer.setTimerExpirySec(BURST_TIMEOUT);
    if (!mBurstTimer)
    {
        mBurstTimer.reset(LLEventTimer::run_every(BURST_FLUSH_PERIOD, [this]() { flushExpiredBurst(); }));
    }
}

void ALScriptWorkspace::flushExpiredBurst()
{
    if (mBurst && mBurst->timer.hasExpired())
    {
        flushRuntime();
    }
}

void ALScriptWorkspace::flushRuntime()
{
    if (!mBurst)
    {
        return;
    }
    const std::unique_ptr<Burst> burst = std::move(mBurst);
    deliverRuntime(*burst);
}

void ALScriptWorkspace::deliverRuntime(const Burst& burst)
{
    LLViewerObject* prim = gObjectList.findObject(burst.fromId);
    LLViewerObject* root = prim ? prim->getRootEdit() : nullptr;

    RuntimeEvent event;
    event.time       = LLDate::now().secondsSinceEpoch();
    event.prim       = burst.fromId;
    event.root       = root ? root->getID() : burst.fromId;
    event.objectName = burst.fromName;
    event.lua        = burst.lua;
    event.channel    = burst.channel;
    for (const std::string& text : burst.texts)
    {
        if (!event.message.empty())
        {
            event.message += "\n";
        }
        event.message += text;
    }

    // An error: the header names the object and the script, and the rest
    // is the stack.
    std::vector<std::string> lines = LLStringUtil::getTokens(event.message, "\n");
    if (!lines.empty() && endsWith(lines.front(), RUNTIME_ERROR_MARKER))
    {
        event.isError = true;
        boost::smatch match;
        if (boost::regex_match(lines.front(), match, RUNTIME_ERROR_HEADER))
        {
            event.objectName = match[1].str();
            event.scriptName = match[2].str();
            lines.erase(lines.begin());
        }
        else
        {
            lines.clear();
        }
    }
    if (prim && !event.scriptName.empty())
    {
        if (LLInventoryItem* item = scriptNamed(prim, event.scriptName))
        {
            event.item = item->getUUID();
            event.lua  = item->getRuntime() == "luau";
        }
    }
    if (event.isError && !lines.empty())
    {
        // Where: Luau names the chunk and a one-based line; LSL gives a
        // zero-based line and column, or nothing at all, as "Math Error"
        // comes.
        bool located = false;
        for (const std::string& text : burst.texts)
        {
            for (const std::string& line : LLStringUtil::getTokens(text, "\n"))
            {
                boost::smatch match;
                if (event.lua && boost::regex_match(line, match, LUAU_LOCATION))
                {
                    event.line  = static_cast<S32>(std::strtol(match[2].str().c_str(), nullptr, 10)) - 1;
                    event.error = match[3].str();
                    located     = true;
                }
                else if (!event.lua && boost::regex_match(line, match, LSL_LOCATION))
                {
                    event.line   = static_cast<S32>(std::strtol(match[1].str().c_str(), nullptr, 10));
                    event.column = static_cast<S32>(std::strtol(match[2].str().c_str(), nullptr, 10));
                    event.error  = match[4].str();
                    located      = true;
                }
                if (located)
                {
                    break;
                }
            }
            if (located)
            {
                break;
            }
        }
        if (!located)
        {
            for (const std::string& line : lines)
            {
                if (!line.empty() && line.find(RUNTIME_ERROR_MARKER) == std::string::npos)
                {
                    event.error = line;
                    break;
                }
            }
        }
        event.stack = std::move(lines);
    }

    mRecent.push_back(event);
    while (mRecent.size() > RECENT_RUNTIME)
    {
        mRecent.pop_front();
    }
    mRuntime(event);
}
