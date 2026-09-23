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

#include "alscriptenvelope.h"
#include "alscriptpreprocessor.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llassetstorage.h"
#include "llcallbacklist.h"
#include "llchat.h"
#include "llcompilequeue.h"
#include "lldate.h"
#include "lleventtimer.h"
#include "llexperiencecache.h"
#include "llfilesystem.h"
#include "llfloaterperms.h"
#include "llfloaterreg.h"
#include "llinventory.h"
#include "llinventorymodel.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llpreviewscript.h"
#include "llscripteditorws.h"
#include "lltrans.h"
#include "llversioninfo.h"
#include "llviewerassettype.h"
#include "llviewerassetupload.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoinventorylistener.h"
#include "message.h"
// [RLVa:KB]
#include "rlvhandler.h"
#include "alscriptmessages.h"
#include "rlvlocks.h"
// [/RLVa:KB]

#include <boost/regex.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>

namespace
{
    // How long after a line the lines that belong with it may still come,
    // and how often that is looked at.
    const F32    BURST_TIMEOUT      = 1.0f;
    const F32    BURST_FLUSH_PERIOD = 0.25f;
    const size_t RECENT_RUNTIME     = 500;
    // How long a prim's contents are waited for before they are answered
    // as not fetched.
    const F32    CONTENTS_TIMEOUT   = 20.f;

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

// One prim's contents asked for: answered once the object has them, then
// let go of. The object's own asking can fail without a word -- the
// capability answering with an error, the object killed or out of sight
// before the answer -- and whatever waits on this one, a save among them,
// would wait for ever; so a while without an answer is answered as
// nothing fetched.
struct ALScriptWorkspace::ContentsListener final : public LLVOInventoryListener
{
    LLUUID              prim;
    contents_callback_t callback;
    bool                done = false;

    ContentsListener(LLViewerObject* object, const LLUUID& prim_in, contents_callback_t callback_in)
    :   prim(prim_in),
        callback(std::move(callback_in)),
        mAsked(object)
    {
        registerVOInventoryListener(object, nullptr);
        requestVOInventory();
    }

    ~ContentsListener() override { letGo(); }

    void inventoryChanged(LLViewerObject* from, LLInventoryObject::object_list_t* inventory, S32, void*) override
    {
        if (!done)
        {
            answer(from, inventory);
        }
    }

    // No answer in time.
    void expire()
    {
        if (!done)
        {
            answer(gObjectList.findObject(prim), nullptr);
        }
    }

private:
    // Off the object's list of listeners, where the object is still the
    // one asked. It forgets its listeners only as it is destroyed, and one
    // killed since -- gone from the list, whether or not something still
    // holds it -- is not to be followed to: this is only forgotten.
    void letGo()
    {
        if (mAsked && gObjectList.findObject(prim) == mAsked)
        {
            removeVOInventoryListener();
        }
        else
        {
            clearVOInventoryListener();
        }
        mAsked = nullptr;
    }

    void answer(LLViewerObject* from, LLInventoryObject::object_list_t* inventory)
    {
        done = true;
        Contents contents;
        contents.prim    = prim;
        contents.fetched = inventory != nullptr;
        if (from)
        {
            if (LLNameValue* name = from->getNVPair("Name"))
            {
                contents.name = name->getString() ? name->getString() : "";
            }
        }
        if (inventory)
        {
            for (const auto& entry : *inventory)
            {
                LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(entry.get());
                if (!item)
                {
                    continue;
                }
                Item one;
                one.id   = item->getUUID();
                one.name = item->getName();
                if (item->getType() == LLAssetType::AT_LSL_TEXT)
                {
                    one.script = true;
                    one.lua    = item->getRuntime() == "luau" || item->getInventorySubType() == SST_LUA;
                }
                else if (item->getType() == LLAssetType::AT_NOTECARD)
                {
                    one.script = false;
                }
                else
                {
                    continue;
                }
                contents.items.push_back(std::move(one));
            }
        }
        // The object may be walking its listeners: this one leaves the
        // walk now, and answers once it is over.
        letGo();
        LLAppViewer::instance()->postToMainCoro([told = std::move(callback), contents]() {
            told(contents);
            ALScriptWorkspace::instance().sweepListeners();
        });
    }

    // Never followed but where the object list says it is still there.
    LLViewerObject* mAsked = nullptr;
};

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
    return ALScriptMessages::looksLikeLua(content);
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
            answer.error   = LLTrans::getString("WorkspaceNoSuchItem");
            answer.failure = Loaded::Failure::Missing;
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
            answer.error   = LLTrans::getString("WorkspaceNotPermitted");
            answer.failure = Loaded::Failure::NotPermitted;
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
        answer.error   = LLTrans::getString(object ? "WorkspaceNoSuchItemInObject" : "WorkspaceNoSuchObject");
        answer.failure = Loaded::Failure::Missing;
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
        answer.error   = LLTrans::getString("WorkspaceNotPermitted");
        answer.failure = Loaded::Failure::NotPermitted;
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
        // Refused, or not there to be had, the same next time; anything
        // else -- a timeout, a lost capability -- may go through again.
        answer.error   = LLAssetStorage::getErrorString(status);
        answer.failure = status == LL_ERR_INSUFFICIENT_PERMISSIONS ? Loaded::Failure::NotPermitted
                         : status == LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE || status == LL_ERR_ASSET_REQUEST_NONEXISTENT_FILE
                             ? Loaded::Failure::Unreadable
                             : Loaded::Failure::Fetch;
    }
    else if (!readAsset(asset_id, type, answer.text))
    {
        answer.error   = LLTrans::getString("WorkspaceAssetUnreadable");
        answer.failure = Loaded::Failure::Unreadable;
    }
    else if (type == LLAssetType::AT_NOTECARD)
    {
        // A notecard in its format, or the bare text of one written
        // before there was a format.
        answer.assetId  = asset_id;
        answer.notecard = true;
        if (answer.text.compare(0, 19, "Linden text version") == 0)
        {
            LLNotecard         notecard(LLNotecard::MAX_SIZE);
            std::istringstream in(answer.text);
            if (notecard.importStream(in))
            {
                answer.text     = notecard.getText();
                answer.embedded = notecard.getItems();
            }
            else
            {
                answer.error   = LLTrans::getString("WorkspaceNotecardUnreadable");
                answer.failure = Loaded::Failure::Unreadable;
            }
        }
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
    for (const ALScriptMessages::Place& place : ALScriptMessages::readDiagnostics(errors, lua))
    {
        Diagnostic diagnostic;
        diagnostic.line      = place.line;
        diagnostic.column    = place.column;
        diagnostic.hasColumn = place.hasColumn;
        diagnostic.level     = place.level;
        diagnostic.message   = place.message;
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
        result.error = reason.empty() ? LLTrans::getString("WorkspaceUploadFailed") : reason;
        LLAppViewer::instance()->postToMainCoro([this, result, callback]() { deliver(result, callback); });
        return true;
    };

    if (ref.inInventory())
    {
        LLViewerRegion* region = gAgent.getRegion();
        if (!region)
        {
            error = LLTrans::getString("WorkspaceNoRegion");
            return false;
        }
        const std::string url = region->getCapability("UpdateScriptAgent");
        if (url.empty())
        {
            error = LLTrans::getString("WorkspaceRegionCannotUpdateScripts");
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
        error = LLTrans::getString("WorkspaceNoSuchObject");
        return false;
    }
    const std::string url = object->getRegion()->getCapability("UpdateScriptTask");
    if (url.empty())
    {
        error = LLTrans::getString("WorkspaceRegionCannotUpdateScripts");
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

bool ALScriptWorkspace::saveNotecard(const ALScriptRef& ref, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& embedded,
                                     compile_callback_t callback, std::string& error)
{
    LLNotecard notecard(LLNotecard::MAX_SIZE);
    notecard.setItems(embedded);
    notecard.setText(text);
    std::stringstream out;
    if (!notecard.exportStream(out))
    {
        error = LLTrans::getString("WorkspaceNotecardUnwritable");
        return false;
    }
    const std::string buffer   = out.str();
    const bool        carries  = !embedded.empty();
    auto              answered = [this, ref, callback, carries](const LLUUID& new_asset_id) {
        CompileResult result;
        result.ref        = ref;
        result.notecard   = true;
        result.success    = true;
        result.newAssetId = new_asset_id;
        if (carries)
        {
            // The uploader may have rewritten what it was given; the copy
            // in the cache is not to be trusted.
            LLFileSystem::removeFile(new_asset_id, LLAssetType::AT_NOTECARD);
        }
        LLAppViewer::instance()->postToMainCoro([this, result, callback]() { deliver(result, callback); });
    };
    auto failed = [this, ref, callback](LLUUID, LLUUID, LLSD, std::string reason) -> bool {
        CompileResult result;
        result.ref      = ref;
        result.notecard = true;
        result.error    = reason.empty() ? LLTrans::getString("WorkspaceUploadFailed") : reason;
        LLAppViewer::instance()->postToMainCoro([this, result, callback]() { deliver(result, callback); });
        return true;
    };

    if (ref.inInventory())
    {
        LLViewerRegion* region = gAgent.getRegion();
        if (!region)
        {
            error = LLTrans::getString("WorkspaceNoRegion");
            return false;
        }
        const std::string url = region->getCapability("UpdateNotecardAgentInventory");
        if (url.empty())
        {
            error = LLTrans::getString("WorkspaceRegionCannotUpdateNotecards");
            return false;
        }
        LLResourceUploadInfo::ptr_t info(std::make_shared<LLBufferedAssetUploadInfo>(
            ref.item, LLAssetType::AT_NOTECARD, buffer,
            [answered](LLUUID, LLUUID new_asset_id, LLUUID, LLSD) { answered(new_asset_id); }, failed));
        LLViewerAssetUpload::EnqueueInventoryUpload(url, info);
        return true;
    }

    LLViewerObject* object = gObjectList.findObject(ref.object);
    if (!object || !object->getRegion())
    {
        error = LLTrans::getString("WorkspaceNoSuchObject");
        return false;
    }
    const std::string url = object->getRegion()->getCapability("UpdateNotecardTaskInventory");
    if (url.empty())
    {
        error = LLTrans::getString("WorkspaceRegionCannotUpdateNotecards");
        return false;
    }
    LLResourceUploadInfo::ptr_t info(std::make_shared<LLBufferedAssetUploadInfo>(
        ref.object, ref.item, LLAssetType::AT_NOTECARD, buffer,
        [answered](LLUUID, LLUUID, LLUUID new_asset_id, LLSD) { answered(new_asset_id); }, failed));
    LLViewerAssetUpload::EnqueueInventoryUpload(url, info);
    return true;
}

void ALScriptWorkspace::prepare(const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id, const std::string& text, bool lua,
                                const std::string& target, prepared_callback_t callback)
{
    if (!ALScriptEnvelope::looksWrapped(text) && !ALScriptPreprocessor::enabled())
    {
        Prepared as_is;
        as_is.text = text;
        callback(as_is);
        return;
    }
    std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text);
    ALScriptPreprocessor::Request   request;
    request.ref           = ref;
    request.name          = name;
    request.assetId       = asset_id;
    request.source        = envelope ? envelope->source : text;
    request.lua           = lua;
    request.compileTarget = target;
    ALScriptPreprocessor::instance().run(request, [request, envelope, lua, target, callback](const ALPreprocessor::Result& expanded) {
        Prepared prepared;
        if (expanded.hasErrors())
        {
            for (const ALScriptProblem& problem : expanded.problems)
            {
                if (problem.severity != ALScriptProblem::Severity::Error)
                {
                    continue;
                }
                Diagnostic diagnostic;
                diagnostic.line      = problem.line;
                diagnostic.column    = problem.column;
                diagnostic.hasColumn = true;
                diagnostic.level     = "ERROR";
                diagnostic.message   = problem.file.empty() ? problem.message : problem.file + ": " + problem.message;
                prepared.errors.push_back(std::move(diagnostic));
            }
            callback(prepared);
            return;
        }
        prepared.text = request.source;
        if (!expanded.disabled)
        {
            ALScriptEnvelope wrapped = envelope ? *envelope : ALScriptEnvelope();
            wrapped.lua              = lua;
            wrapped.source           = request.source;
            wrapped.expanded         = expanded.text;
            wrapped.compileTarget    = target;
            wrapped.programVersion   = LLVersionInfo::instance().getChannelAndVersion();
            wrapped.lastCompiled     = LLDate::now().asString();
            prepared.text            = wrapped.wrap();
        }
        callback(prepared);
    });
}

void ALScriptWorkspace::recompile(const ALScriptRef& ref, const std::string& requested, compile_callback_t callback)
{
    auto fail = [this, ref, callback](const std::string& why) {
        CompileResult result;
        result.ref   = ref;
        result.error = why;
        deliver(result, callback);
    };
    LLViewerObject*        object = ref.inInventory() ? nullptr : gObjectList.findObject(ref.object);
    const LLInventoryItem* item   = ref.inInventory() ? gInventory.getItem(ref.item) : object ? object->getInventoryItem(ref.item) : nullptr;
    if (!item)
    {
        fail(ref.inInventory() ? "no such item" : object ? "no such item in the object" : "no such object");
        return;
    }
    // The target: what was asked, or what the script compiles for now.
    // A script is in one language, and only compiles for that language's
    // targets.
    const bool  lua    = item->getInventorySubType() == SST_LUA || item->getRuntime() == "luau";
    std::string target = requested;
    if (target.empty() || target == "auto")
    {
        target = item->getRuntime();
        if (target.empty())
        {
            target = lua ? "luau" : "mono";
        }
    }
    if ((target == "luau") != lua)
    {
        fail(std::string(lua ? "a Luau" : "an LSL") + " script does not compile for " + target);
        return;
    }
    const std::string name = item->getName();
    // Its experience, then its text, then the upload.
    auto go = [this, ref, target, lua, name, callback, fail](const LLUUID& experience) {
        load(ref, [this, target, lua, name, callback, fail, experience](const Loaded& loaded) {
            if (!loaded.error.empty())
            {
                fail(loaded.error);
                return;
            }
            const ALScriptRef ref = loaded.ref;
            prepare(ref, name, loaded.assetId, loaded.text, lua, target, [this, ref, target, callback, experience, fail](const Prepared& prepared) {
                if (!prepared.errors.empty())
                {
                    CompileResult result;
                    result.ref         = ref;
                    result.diagnostics = prepared.errors;
                    for (const Diagnostic& diagnostic : prepared.errors)
                    {
                        result.messages.push_back(diagnostic.message);
                    }
                    deliver(result, callback);
                    return;
                }
                SaveOptions options;
                options.compileTarget = target;
                options.running       = true;
                options.experience    = experience;
                std::string error;
                if (!save(ref, prepared.text, options, callback, error))
                {
                    fail(error);
                }
            });
        });
    };
    if (object && object->getRegion() && object->getRegion()->isCapabilityAvailable("GetMetadata"))
    {
        LLExperienceCache::instance().fetchAssociatedExperience(ref.object, ref.item, [go](const LLSD& result) {
            LLUUID experience;
            if (result.has(LLExperienceCache::EXPERIENCE_ID))
            {
                experience = result[LLExperienceCache::EXPERIENCE_ID].asUUID();
            }
            LLAppViewer::instance()->postToMainCoro([go, experience]() { go(experience); });
        });
    }
    else
    {
        go(LLUUID::null);
    }
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

// --- running -----------------------------------------------------------------------

bool ALScriptWorkspace::askRunning(const ALScriptRef& ref)
{
    if (ref.inInventory())
    {
        return false;
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    if (!object || !object->getRegion())
    {
        return false;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_GetScriptRunning);
    msg->nextBlockFast(_PREHASH_Script);
    msg->addUUIDFast(_PREHASH_ObjectID, ref.object);
    msg->addUUIDFast(_PREHASH_ItemID, ref.item);
    msg->sendReliable(object->getRegion()->getHost());
    return true;
}

// static
void ALScriptWorkspace::processScriptRunningReply(LLMessageSystem* msg, void** data)
{
    RunningState state;
    msg->getUUIDFast(_PREHASH_Script, _PREHASH_ObjectID, state.ref.object);
    msg->getUUIDFast(_PREHASH_Script, _PREHASH_ItemID, state.ref.item);
    msg->getBOOLFast(_PREHASH_Script, _PREHASH_Running, state.running);
    bool mono = false, luau = false, luau_language = false;
    msg->getBOOLFast(_PREHASH_Script, _PREHASH_Mono, mono);
    msg->getBOOLFast(_PREHASH_Script, _PREHASH_Luau, luau);
    msg->getBOOLFast(_PREHASH_Script, _PREHASH_LuauLanguage, luau_language);
    state.compileTarget = luau ? (luau_language ? "luau" : "lsl-luau") : mono ? "mono" : "lsl2";
    if (instanceExists())
    {
        instance().mRunningState(state);
    }
    LLLiveLSLEditor::processScriptRunningReply(msg, data);
}

// --- what an object holds ----------------------------------------------------------

void ALScriptWorkspace::listContents(const LLUUID& prim, contents_callback_t callback)
{
    sweepListeners();
    LLViewerObject* object = gObjectList.findObject(prim);
    if (!object)
    {
        Contents none;
        none.prim = prim;
        callback(none);
        return;
    }
    auto listener = std::make_shared<ContentsListener>(object, prim, std::move(callback));
    mListeners.push_back(listener);
    doAfterInterval([waiting = std::weak_ptr<ContentsListener>(listener)]() {
        if (const std::shared_ptr<ContentsListener> still = waiting.lock())
        {
            still->expire();
        }
    }, CONTENTS_TIMEOUT);
}

void ALScriptWorkspace::sweepListeners()
{
    mListeners.erase(std::remove_if(mListeners.begin(), mListeners.end(), [](const std::shared_ptr<ContentsListener>& l) { return l->done; }),
                     mListeners.end());
}

// --- changing what an object holds ---------------------------------------------------

bool ALScriptWorkspace::create(const LLUUID& prim_id, bool notecard, bool lua, const std::string& name, created_callback_t callback, std::string& error)
{
    if (name.empty())
    {
        error = LLTrans::getString("WorkspaceNameNeeded");
        return false;
    }
    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim || !prim->getRegion())
    {
        error = LLTrans::getString("WorkspaceNoSuchObject");
        return false;
    }
    if (!prim->permModify())
    {
        error = LLTrans::getString("WorkspaceObjectNotModifiable");
        return false;
    }
    // The region makes the item where it can; a script may still be asked
    // for the old way, a notecard may not.
    const bool cap = prim->getRegion()->isCapabilityAvailable("CreateTaskInventoryItem");
    if (notecard && !cap)
    {
        error = LLTrans::getString("WorkspaceRegionCannotMakeNotecard");
        return false;
    }
    const LLAssetType::EType     asset_type = notecard ? LLAssetType::AT_NOTECARD : LLAssetType::AT_LSL_TEXT;
    const LLInventoryType::EType inv_type   = notecard ? LLInventoryType::IT_NOTECARD : LLInventoryType::IT_LSL;
    const U8                     sub_type   = notecard ? 0 : lua ? SST_LUA : SST_LSL;
    const char*                  perm_key   = notecard ? "Notecards" : "Scripts";
    LLPermissions                perms;
    perms.init(gAgent.getID(), gAgent.getID(), LLUUID::null, LLUUID::null);
    perms.initMasks(PERM_ALL, PERM_ALL, LLFloaterPerms::getEveryonePerms(perm_key), LLFloaterPerms::getGroupPerms(perm_key),
                    PERM_MOVE | LLFloaterPerms::getNextOwnerPerms(perm_key));
    std::string description;
    LLViewerAssetType::generateDescriptionFor(asset_type, description);

    if (cap)
    {
        LLSD params;
        if (!notecard)
        {
            params["enabled"] = true;
            params["vm"]      = lua ? "luau" : "mono";
        }
        prim->createInventoryItem(asset_type, inv_type, sub_type, name, description, perms, params,
                                  [prim_id, name, callback](bool success, const LLSD& response) {
                                      Created made;
                                      made.prim = prim_id;
                                      made.name = name;
                                      if (success)
                                      {
                                          made.item = response["item_id"].asUUID();
                                          if (response.has("name"))
                                          {
                                              made.name = response["name"].asString();
                                          }
                                      }
                                      else
                                      {
                                          made.error = response.has("message") ? response["message"].asString() : std::string("the region refused");
                                      }
                                      LLAppViewer::instance()->postToMainCoro([made, callback]() { callback(made); });
                                  });
        return true;
    }
    LLPointer<LLViewerInventoryItem> item = new LLViewerInventoryItem(LLUUID::null, LLUUID::null, perms, LLUUID::null, asset_type, inv_type, name,
                                                                      description, LLSaleInfo::DEFAULT,
                                                                      LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK & sub_type, time_corrected());
    prim->saveScript(item, true, true, LLUUID::null);
    // The object's contents will show it, under whatever id the region
    // gives it.
    Created made;
    made.prim = prim_id;
    made.name = name;
    callback(made);
    return true;
}

bool ALScriptWorkspace::rename(const ALScriptRef& ref, const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = LLTrans::getString("WorkspaceNameNeeded");
        return false;
    }
    if (ref.inInventory())
    {
        LLViewerInventoryItem* item = gInventory.getItem(ref.item);
        if (!item)
        {
            error = LLTrans::getString("WorkspaceNoSuchItem");
            return false;
        }
        if (item->getName() != name)
        {
            LLSD updates;
            updates["name"] = name;
            update_inventory_item(ref.item, updates, nullptr);
        }
        return true;
    }
    LLViewerObject*  object = gObjectList.findObject(ref.object);
    LLInventoryItem* item   = object ? object->getInventoryItem(ref.item) : nullptr;
    if (!item)
    {
        error = LLTrans::getString(object ? "WorkspaceNoSuchItemInObject" : "WorkspaceNoSuchObject");
        return false;
    }
    if (!object->permModify())
    {
        error = LLTrans::getString("WorkspaceObjectNotModifiable");
        return false;
    }
    if (item->getName() == name)
    {
        return true;
    }
    LLPointer<LLViewerInventoryItem> renamed = new LLViewerInventoryItem(item);
    renamed->rename(name);
    object->updateInventory(renamed, TASK_INVENTORY_ITEM_KEY, false);
    return true;
}

bool ALScriptWorkspace::remove(const ALScriptRef& ref, std::string& error)
{
    if (ref.inInventory())
    {
        LLViewerInventoryItem* item = gInventory.getItem(ref.item);
        if (!item)
        {
            error = LLTrans::getString("WorkspaceNoSuchItem");
            return false;
        }
        const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
        if (trash.isNull())
        {
            error = LLTrans::getString("WorkspaceNoTrash");
            return false;
        }
        gInventory.changeItemParent(item, trash, false);
        return true;
    }
    LLViewerObject*  object = gObjectList.findObject(ref.object);
    LLInventoryItem* item   = object ? object->getInventoryItem(ref.item) : nullptr;
    if (!item)
    {
        error = LLTrans::getString(object ? "WorkspaceNoSuchItemInObject" : "WorkspaceNoSuchObject");
        return false;
    }
    if (!object->permModify())
    {
        error = LLTrans::getString("WorkspaceObjectNotModifiable");
        return false;
    }
    object->removeInventory(ref.item);
    return true;
}

bool ALScriptWorkspace::queue(Queue kind, const std::vector<std::pair<LLUUID, std::string>>& prims, const std::string& target, std::string& error)
{
    if (prims.empty())
    {
        error = LLTrans::getString("WorkspaceNothingToDo");
        return false;
    }
    const char* name  = kind == Queue::Recompile ? "compile_queue" : kind == Queue::Reset ? "reset_queue" : kind == Queue::Start ? "start_queue" : "stop_queue";
    const char* title = kind == Queue::Recompile ? "CompileQueueTitle" : kind == Queue::Reset ? "ResetQueueTitle" : kind == Queue::Start ? "RunQueueTitle" : "NotRunQueueTitle";
    LLUUID      id;
    id.generate();
    LLFloaterScriptQueue* queue = LLFloaterReg::getTypedInstance<LLFloaterScriptQueue>(name, LLSD(id));
    if (!queue)
    {
        error = LLTrans::getString("WorkspaceQueueCannotOpen");
        return false;
    }
    queue->setCompileTarget(target.empty() ? std::string("auto") : target);
    for (const auto& [prim, prim_name] : prims)
    {
        queue->addObject(prim, prim_name);
    }
    if (!queue->start())
    {
        queue->closeFloater();
        error = LLTrans::getString("WorkspaceQueueWouldNotStart");
        return false;
    }
    queue->setTitle(LLTrans::getString(title));
    return true;
}

// --- what scripts say ------------------------------------------------------------

void ALScriptWorkspace::ingestChat(const LLChat& chat)
{
    const RuntimeEvent::Channel channel = chat.mChatType == CHAT_TYPE_OWNER ? RuntimeEvent::Channel::OwnerSay : RuntimeEvent::Channel::Debug;
    const std::vector<std::string>  lines = LLStringUtil::getTokens(chat.mText, "\n");
    ALScriptMessages::Header        named;
    const bool                      header = !lines.empty() && ALScriptMessages::readRuntimeHeader(lines.front(), named);

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
            if (LLInventoryItem* item = scriptNamed(prim, named.script))
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
    if (!lines.empty() && ALScriptMessages::endsRuntimeError(lines.front()))
    {
        event.isError = true;
        ALScriptMessages::Header named;
        if (ALScriptMessages::readRuntimeHeader(lines.front(), named))
        {
            event.objectName = named.object;
            event.scriptName = named.script;
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
        std::vector<std::string> said;
        for (const std::string& text : burst.texts)
        {
            for (const std::string& line : LLStringUtil::getTokens(text, "\n"))
            {
                said.push_back(line);
            }
        }
        ALScriptMessages::Location where;
        const bool                 located = ALScriptMessages::readRuntimeLocation(said, event.lua, where);
        if (located)
        {
            event.line   = where.line;
            event.column = where.column;
            event.error  = where.message;
        }
        else
        {
            for (const std::string& line : lines)
            {
                if (!line.empty() && !ALScriptMessages::endsRuntimeError(line))
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
