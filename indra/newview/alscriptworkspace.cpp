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

#include "alobjectproperties.h"
#include "alscriptanalysis.h"
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
#include "llfilesystem.h"
#include "llfloaterperms.h"
#include "llfloaterreg.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llpreviewscript.h"
#include "llscripteditorws.h"
#include "llselectmgr.h"
#include "lltooldraganddrop.h"
#include "llsdutil.h"
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
#include "rlvactions.h"
#include "rlvcommon.h"
#include "rlvhandler.h"
#include "alscriptmessages.h"
#include "rlvlocks.h"
// [/RLVa:KB]

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>

namespace
{
    // How long after a run-time error's first line the lines that belong
    // with it may still come, and how many of them at most: a stack is
    // deep, but a script that also chatters on the channel is not to hold
    // its error back, gathering the chatter.
    const F32    BURST_WINDOW       = 1.0f;
    const size_t BURST_MOST_LINES   = 64;
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
            // By name, as the build floater's Contents lists them, not in
            // the order the simulator sends them in.
            std::stable_sort(contents.items.begin(), contents.items.end(),
                             [](const Item& a, const Item& b) { return LLStringUtil::compareDict(a.name, b.name) < 0; });
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
    size_t                   lines = 0;
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
    // An LSL item run on Luau is LSL on Luau -- or SLua, where its text was
    // converted and saved for Luau (a target of the other language picked
    // in the studio), which only the text tells.
    if (!language.lua && target == "luau")
    {
        language.lua = looksLikeLua(content);
        target       = language.lua ? "luau" : "lsl-luau";
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
        const std::string refused = rlvRefusal(nullptr, item->getType(), RlvUse::See);
        if (!answer.viewable || !refused.empty())
        {
            answer.error   = refused.empty() ? LLTrans::getString("WorkspaceNotPermitted") : refused;
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
    const std::string refused = rlvRefusal(object, item->getType(), RlvUse::See);
    if (!answer.viewable || !refused.empty())
    {
        answer.error   = refused.empty() ? LLTrans::getString("WorkspaceNotPermitted") : refused;
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
    if (result.success && !result.notecard)
    {
        // A new script runs from here; what the old one said is past.
        forgetRuntime(result.ref.item);
    }
    if (callback)
    {
        callback(result);
    }
    mCompiled(result);
}

bool ALScriptWorkspace::save(const ALScriptRef& ref, const std::string& text, const SaveOptions& options, compile_callback_t callback, std::string& error)
{
    if (!ref.inInventory())
    {
        if (std::string refused = rlvRefusal(gObjectList.findObject(ref.object), LLAssetType::AT_LSL_TEXT, RlvUse::Change); !refused.empty())
        {
            error = std::move(refused);
            return false;
        }
    }
    if (!ref.inInventory() && !options.experience)
    {
        // What stops it here stops it before the region is asked, as it
        // stops one that knows its experience.
        LLViewerObject* object = gObjectList.findObject(ref.object);
        if (!object || !object->getRegion())
        {
            error = LLTrans::getString("WorkspaceNoSuchObject");
            return false;
        }
        if (object->getRegion()->getCapability("UpdateScriptTask").empty())
        {
            error = LLTrans::getString("WorkspaceRegionCannotUpdateScripts");
            return false;
        }
        askExperience(ref, [this, ref, text, options, callback](const std::optional<LLUUID>& experience) {
            std::string why;
            if (experience)
            {
                SaveOptions known = options;
                known.experience  = *experience;
                if (save(ref, text, known, callback, why))
                {
                    return;
                }
            }
            CompileResult result;
            if (!experience)
            {
                why                      = LLTrans::getString("WorkspaceExperienceUnknown");
                result.experienceUnknown = true;
            }
            result.ref   = ref;
            result.error = why;
            deliver(result, callback);
        });
        return true;
    }
    // The upload finishes on a coroutine; the answer is handed to the main
    // loop before anything that draws hears of it, as the legacy floaters
    // did, since a text editor's reflow takes a mutex a fiber may not.
    // Nothing the simulator would refuse for its length is sent, whoever
    // sends it: the studio says so first, with more to say; the compile
    // queue and the rest are stopped here.
    if (text.size() > ALScriptEnvelope::MAX_ASSET_BYTES)
    {
        LLStringUtil::format_map_t args;
        args["[SIZE]"]  = std::to_string(text.size());
        args["[LIMIT]"] = std::to_string(ALScriptEnvelope::MAX_ASSET_BYTES);
        error           = LLTrans::getString("WorkspaceScriptTooLarge", args);
        return false;
    }
    const bool lua = options.compileTarget == "luau";
    auto answered  = [this, ref, lua, callback, running = options.running, experience = options.experience](const LLSD& response, const LLUUID& new_asset_id) {
        CompileResult result;
        result.ref        = ref;
        result.success    = response["compiled"].asBoolean();
        result.running    = running;
        result.newAssetId = new_asset_id;
        if (!ref.inInventory())
        {
            result.experience = experience;
        }
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
        ref.object, ref.item, options.compileTarget, options.running, options.experience.value_or(LLUUID::null), text,
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
    // Nothing a notecard is read back with takes more text than this -- the
    // reader refuses the whole notecard -- so nothing more is written.
    if (text.size() > static_cast<size_t>(LLNotecard::MAX_SIZE))
    {
        LLStringUtil::format_map_t args;
        args["[SIZE]"]  = std::to_string(text.size());
        args["[LIMIT]"] = std::to_string(static_cast<S32>(LLNotecard::MAX_SIZE));
        error           = LLTrans::getString("WorkspaceNotecardTooLarge", args);
        return false;
    }
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
    if (std::string refused = rlvRefusal(object, LLAssetType::AT_NOTECARD, RlvUse::Change); !refused.empty())
    {
        error = std::move(refused);
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
    request.source        = std::make_shared<const std::string>(envelope ? envelope->source : text);
    request.lua           = lua;
    request.compileTarget = target;
    ALScriptPreprocessor::instance().run(request, [request, envelope, lua, target, callback](const ALPreprocessor::Result& expanded) {
        Prepared prepared;
        if (expanded.hasErrors() || !expanded.pending.empty())
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
            // An include that never came would go up left out, with nobody
            // at a save to be told: not sent, and said why.
            for (const std::string& name : expanded.pending)
            {
                Diagnostic diagnostic;
                diagnostic.level   = "ERROR";
                diagnostic.message = alScriptKeyedWords("PreprocIncludeNotFetched", { name },
                                                        ALScriptProblem::fill("include file '[1]' could not be fetched, and the script is not sent without it", { name }));
                prepared.errors.push_back(std::move(diagnostic));
            }
            callback(prepared);
            return;
        }
        prepared.text = request.sourceText();
        if (!expanded.disabled)
        {
            ALScriptEnvelope wrapped = envelope ? *envelope : ALScriptEnvelope();
            wrapped.lua              = lua;
            wrapped.source           = request.sourceText();
            wrapped.expanded         = expanded.text;
            wrapped.compileTarget    = target;
            wrapped.programVersion   = LLVersionInfo::instance().getChannelAndVersion();
            wrapped.lastCompiled     = LLDate::now().asString();
            prepared.text            = wrapped.wrap();
        }
        callback(prepared);
    });
}

void ALScriptWorkspace::recompile(const ALScriptRef& ref, const std::string& requested, compile_callback_t callback, std::optional<bool> running)
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
        fail(LLTrans::getString(ref.inInventory() ? "WorkspaceNoSuchItem" : object ? "WorkspaceNoSuchItemInObject" : "WorkspaceNoSuchObject"));
        return;
    }
    if (std::string refused = object ? rlvRefusal(object, LLAssetType::AT_LSL_TEXT, RlvUse::Change) : std::string(); !refused.empty())
    {
        fail(refused);
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
        LLStringUtil::format_map_t args;
        args["[TARGET]"] = target;
        fail(LLTrans::getString(lua ? "WorkspaceLuaNotForTarget" : "WorkspaceLSLNotForTarget", args));
        return;
    }
    const std::string name = item->getName();
    // Its text, then the upload, which keeps the experience it runs under.
    load(ref, [this, target, lua, name, callback, fail, running](const Loaded& loaded) {
        if (!loaded.error.empty())
        {
            fail(loaded.error);
            return;
        }
        const ALScriptRef ref = loaded.ref;
        prepare(ref, name, loaded.assetId, loaded.text, lua, target, [this, ref, target, callback, fail, running](const Prepared& prepared) {
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
            options.running       = running.value_or(true);
            std::string error;
            if (!save(ref, prepared.text, options, callback, error))
            {
                fail(error);
            }
        });
    });
}

// --- between objects ---------------------------------------------------------------

namespace
{
    // How long the items taken are waited for in the agent's inventory.
    constexpr F32 TRANSFER_TIMEOUT = 30.f;
}

// static
std::string ALScriptWorkspace::objectName(LLViewerObject* object, const std::string& fallback)
{
    if (!object)
    {
        return fallback;
    }
    if (LLNameValue* nv = object->getNVPair("Name"); nv && nv->getString() && nv->getString()[0])
    {
        return nv->getString();
    }
    if (LLSelectNode* node = LLSelectMgr::getInstance()->getSelection()->findNode(object); node && !node->mName.empty())
    {
        return node->mName;
    }
    if (const ALObjectPropertiesCache::ServerProps* said = ALObjectPropertiesCache::instance().get(object->getID()); said && !said->mName.empty())
    {
        return said->mName;
    }
    return fallback;
}

// static
bool ALScriptWorkspace::luaEnabled(const ALScriptRef& ref)
{
    LLViewerRegion* region = nullptr;
    if (LLViewerObject* object = gObjectList.findObject(ref.object))
    {
        region = object->getRegion();
    }
    if (!region)
    {
        region = gAgent.getRegion();
    }
    if (region && region->simulatorFeaturesReceived())
    {
        LLSD features;
        region->getSimulatorFeatures(features);
        return features["LuaScriptsEnabled"].asBoolean();
    }
    return false;
}

// static
bool ALScriptWorkspace::takeable(LLViewerObject* object, const LLInventoryItem& item)
{
    const LLPermissions& perm     = item.getPermissions();
    const bool           can_copy = gAgent.allowOperation(PERM_COPY, perm, GP_OBJECT_MANIPULATE);
    if (rlv_handler_t::isEnabled() && gRlvAttachmentLocks.isLockedAttachment(object->getRootEdit()))
    {
        return false;
    }
    if (!can_copy && object->isAttachment())
    {
        return false;
    }
    return (can_copy && perm.allowTransferTo(gAgent.getID())) || object->permYouOwner();
}

// One transfer: the folder its items come into, and what is put in from it.
struct ALScriptWorkspace::Transfer final : public LLInventoryObserver
{
    LLUUID                            from;
    LLUUID                            to;
    LLUUID                            folder;
    bool                              running = true;
    // What is to be taken out of `from`, once its turn comes.
    std::vector<LLUUID>               taking;
    // The names of what was taken, until each has come.
    std::vector<std::string>          awaited;
    boost::unordered_flat_set<LLUUID> arrived;
    TransferResult                    result;
    transfer_callback_t               done;
    bool                              finished = false;
    std::weak_ptr<Transfer>           self;

    void changed(U32) override
    {
        if (const std::shared_ptr<Transfer> held = self.lock(); held && !finished && folder.notNull())
        {
            ALScriptWorkspace::instance().transferArrived(held);
        }
    }
};

void ALScriptWorkspace::transfer(const LLUUID& from_id, const std::vector<LLUUID>& items, const LLUUID& to_id, bool running, transfer_callback_t done)
{
    LLViewerObject* from = gObjectList.findObject(from_id);
    LLViewerObject* to   = gObjectList.findObject(to_id);
    auto            one  = std::make_shared<Transfer>();
    one->self            = one;
    one->from            = from_id;
    one->to              = to_id;
    one->running         = running;
    one->done            = std::move(done);
    if (!from || !to)
    {
        one->result.error = LLTrans::getString("WorkspaceNoSuchObject");
        one->done(one->result);
        return;
    }
    // Taken out of the one and put in the other: both changed.
    std::string refused = rlvRefusal(from, LLAssetType::AT_NONE, RlvUse::Change);
    if (refused.empty())
    {
        refused = rlvRefusal(to, LLAssetType::AT_NONE, RlvUse::Change);
    }
    if (!refused.empty())
    {
        one->result.error = refused;
        one->done(one->result);
        return;
    }
    std::vector<LLUUID> taking;
    for (const LLUUID& id : items)
    {
        LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(from->getInventoryObject(id));
        if (!item)
        {
            continue;
        }
        if (!takeable(from, *item) || !LLToolDragAndDrop::isInventoryDropAcceptable(to, item))
        {
            one->result.refused.push_back(item->getName());
            continue;
        }
        taking.push_back(id);
        one->awaited.push_back(item->getName());
    }
    const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (taking.empty() || trash.isNull())
    {
        one->done(one->result);
        return;
    }
    // After any under way: two at once would each see the other's items
    // come into the folder they share.
    one->taking = std::move(taking);
    mTransfers.push_back(one);
    if (mTransfers.size() == 1)
    {
        startTransfer(one);
    }
}

void ALScriptWorkspace::startTransfer(const std::shared_ptr<Transfer>& one)
{
    const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    const auto   begin = [this, one](const LLUUID& folder) {
        LLViewerObject* from = gObjectList.findObject(one->from);
        if (folder.isNull() || !from)
        {
            one->result.error = LLTrans::getString(folder.isNull() ? "WorkspaceTransferNoFolder" : "WorkspaceNoSuchObject");
            transferEnd(one);
            return;
        }
        // What the folder holds already came before, and is not this
        // transfer's.
        one->folder                           = folder;
        LLInventoryModel::cat_array_t*  cats  = nullptr;
        LLInventoryModel::item_array_t* items = nullptr;
        gInventory.getDirectDescendentsOf(folder, cats, items);
        for (const LLPointer<LLViewerInventoryItem>& item : items ? *items : LLInventoryModel::item_array_t())
        {
            one->arrived.insert(item->getUUID());
        }
        gInventory.addObserver(one.get());
        for (const LLUUID& id : one->taking)
        {
            from->moveInventory(folder, id);
        }
        doAfterInterval(
            [this, one]() {
                if (!one->finished)
                {
                    transferEnd(one);
                }
            },
            TRANSFER_TIMEOUT);
    };
    if (trash.isNull())
    {
        begin(LLUUID::null);
        return;
    }
    // One folder in the trash for everything that passes between objects,
    // made the first time.
    const std::string               name     = LLTrans::getString("WorkspaceTransferFolder");
    LLInventoryModel::cat_array_t*  in_trash = nullptr;
    LLInventoryModel::item_array_t* loose    = nullptr;
    gInventory.getDirectDescendentsOf(trash, in_trash, loose);
    for (const LLPointer<LLViewerInventoryCategory>& cat : in_trash ? *in_trash : LLInventoryModel::cat_array_t())
    {
        if (cat && cat->getName() == name)
        {
            begin(cat->getUUID());
            return;
        }
    }
    gInventory.createNewCategory(trash, LLFolderType::FT_NONE, name, begin);
}

void ALScriptWorkspace::transferArrived(const std::shared_ptr<Transfer>& one)
{
    LLInventoryModel::cat_array_t*  cats  = nullptr;
    LLInventoryModel::item_array_t* items = nullptr;
    gInventory.getDirectDescendentsOf(one->folder, cats, items);
    std::vector<LLPointer<LLViewerInventoryItem>> fresh;
    for (const LLPointer<LLViewerInventoryItem>& item : items ? *items : LLInventoryModel::item_array_t())
    {
        if (!item || one->arrived.count(item->getUUID()))
        {
            continue;
        }
        // Only what this one is waiting for: one before it that gave up on
        // an item may see it come late, and something put in by hand is
        // nobody's to take.
        const auto awaited = std::find(one->awaited.begin(), one->awaited.end(), item->getName());
        if (awaited == one->awaited.end())
        {
            continue;
        }
        one->awaited.erase(awaited);
        one->arrived.insert(item->getUUID());
        fresh.push_back(item);
    }
    if (fresh.empty())
    {
        return;
    }
    // Put in once the inventory has done telling of them, which putting in
    // what may not be copied changes again.
    doOnIdleOneTime([this, one, fresh]() {
        LLViewerObject* to = gObjectList.findObject(one->to);
        for (const LLPointer<LLViewerInventoryItem>& item : fresh)
        {
            if (!to || !LLToolDragAndDrop::isInventoryDropAcceptable(to, item))
            {
                one->result.stranded.push_back(item->getName());
                continue;
            }
            // As a drop from the inventory puts it in: what may not be
            // copied leaves the inventory as it goes.
            LLPointer<LLViewerInventoryItem> put = new LLViewerInventoryItem(item.get());
            if (!item->getPermissions().allowCopyBy(gAgent.getID()))
            {
                gInventory.deleteObject(item->getUUID());
                gInventory.notifyObservers();
            }
            if (item->getType() == LLAssetType::AT_LSL_TEXT)
            {
                to->saveScript(put, one->running, true, LLUUID::null);
            }
            else
            {
                put->setCreationDate(time_corrected());
                to->updateInventory(put, TASK_INVENTORY_ITEM_KEY, true);
            }
            ++one->result.moved;
        }
        if (one->awaited.empty() && !one->finished)
        {
            transferEnd(one);
        }
    });
}

void ALScriptWorkspace::transferEnd(const std::shared_ptr<Transfer>& one)
{
    if (one->finished)
    {
        return;
    }
    one->finished = true;
    if (one->folder.notNull())
    {
        gInventory.removeObserver(one.get());
    }
    // What never came, by the names it was taken by.
    one->result.lost = std::move(one->awaited);
    mTransfers.erase(std::remove(mTransfers.begin(), mTransfers.end(), one), mTransfers.end());
    one->done(one->result);
    // The next one's turn.
    if (!mTransfers.empty() && mTransfers.front()->folder.isNull() && !mTransfers.front()->finished)
    {
        startTransfer(mTransfers.front());
    }
}

ALScriptWorkspace::~ALScriptWorkspace()
{
    if (mBurstTimer)
    {
        delete mBurstTimer;
        mBurstTimer = nullptr;
    }
    // A transfer still waiting on its items is heard of no more.
    for (const std::shared_ptr<Transfer>& one : mTransfers)
    {
        if (one->folder.notNull() && !one->finished)
        {
            gInventory.removeObserver(one.get());
        }
    }
}

// --- what RLVa allows ------------------------------------------------------------

// static
std::string ALScriptWorkspace::rlvRefusal(LLViewerObject* object, LLAssetType::EType type, RlvUse use)
{
    if (!RlvActions::isRlvEnabled())
    {
        return std::string();
    }
    if (use == RlvUse::See)
    {
        const bool script = type == LLAssetType::AT_LSL_TEXT;
        if ((script && gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT)) || (type == LLAssetType::AT_NOTECARD && gRlvHandler.hasBehaviour(RLV_BHVR_VIEWNOTE)))
        {
            std::string words = RlvStrings::getString(RlvStringKeys::Blocked::ViewXxx);
            LLStringUtil::format(words, LLSD().with("[TYPE]", LLAssetType::lookup(script ? LLAssetType::AT_SCRIPT : LLAssetType::AT_NOTECARD)));
            return words;
        }
    }
    if (object && (!RlvActions::canEdit(object) || gRlvAttachmentLocks.isLockedAttachment(object->getRootEdit())))
    {
        return RlvStrings::getString(RlvStringKeys::Blocked::Generic);
    }
    return std::string();
}

// --- a script in an object -------------------------------------------------------

void ALScriptWorkspace::askExperience(const ALScriptRef& ref, experience_callback_t told)
{
    if (ref.inInventory())
    {
        told(LLUUID::null);
        return;
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    LLViewerRegion* region = object ? object->getRegion() : nullptr;
    if (!region)
    {
        told(std::nullopt);
        return;
    }
    // A region with no way to say keeps none: a grid without experiences.
    if (!region->isCapabilityAvailable("GetMetadata"))
    {
        told(LLUUID::null);
        return;
    }
    // Asked here rather than through the experience cache, which never
    // answers where the script runs under none.
    LLSD body;
    body["object-id"] = ref.object;
    body["item-id"]   = ref.item;
    body["fields"].append("experience");
    const bool asked = region->requestPostCapability(
        "GetMetadata", body, [told](const LLSD& result) { told(result.has("experience") ? result["experience"].asUUID() : LLUUID::null); },
        [told](const LLSD&) { told(std::nullopt); });
    if (!asked)
    {
        told(std::nullopt);
    }
}

void ALScriptWorkspace::askOwnExperiences(experiences_callback_t told)
{
    if (mOwnExperiencesKnown)
    {
        told(mOwnExperiences);
        return;
    }
    mOwnExperiencesWaiting.push_back(std::move(told));
    if (mOwnExperiencesAsked)
    {
        return;
    }
    // Everyone waiting told what is known; the list kept only where the
    // region gave it, so that one not asked yet is asked again.
    const auto tell = [this](bool known) {
        mOwnExperiencesAsked = false;
        mOwnExperiencesKnown = known;
        std::vector<experiences_callback_t> waiting;
        waiting.swap(mOwnExperiencesWaiting);
        for (const experiences_callback_t& each : waiting)
        {
            each(mOwnExperiences);
        }
    };
    LLViewerRegion* region = gAgent.getRegion();
    if (!region || !region->capabilitiesReceived())
    {
        tell(false);
        return;
    }
    if (!region->isCapabilityAvailable("GetCreatorExperiences"))
    {
        mOwnExperiences.clear();
        tell(true);
        return;
    }
    mOwnExperiencesAsked = true;
    const bool asked     = region->requestGetCapability(
        "GetCreatorExperiences",
        [this, tell](const LLSD& result) {
            mOwnExperiences.clear();
            for (const LLSD& id : llsd::inArray(result["experience_ids"]))
            {
                if (id.asUUID().notNull())
                {
                    mOwnExperiences.push_back(id.asUUID());
                }
            }
            tell(true);
        },
        [tell](const LLSD&) { tell(false); });
    if (!asked)
    {
        tell(false);
    }
}

bool ALScriptWorkspace::scriptMessage(const ALScriptRef& ref, const char* message, bool running, bool with_running)
{
    LLViewerObject* object = gObjectList.findObject(ref.object);
    if (!object || !object->getRegion())
    {
        LLNotificationsUtil::add("CouldNotStartStopScript");
        return false;
    }
// [RLVa:KB] - Checked: 2010-09-28 (RLVa-1.2.1f) | Modified: RLVa-1.0.5a
    if (!rlvRefusal(object, LLAssetType::AT_LSL_TEXT, RlvUse::Change).empty())
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

namespace
{
    // How often a restart asks whether the stop has reached the region,
    // and how many times before it starts the script all the same.
    constexpr F32 RESTART_ASK_EVERY = 0.5f;
    constexpr S32 RESTART_ASKS      = 20;
}

bool ALScriptWorkspace::restart(const ALScriptRef& ref)
{
    if (!setRunning(ref, false))
    {
        return false;
    }
    struct Pending
    {
        ALScriptRef                         ref;
        bool                                done = false;
        boost::signals2::scoped_connection  heard;
    };
    auto pending = std::make_shared<Pending>();
    pending->ref = ref;
    // Started once, by the first word that it has stopped or by the last
    // ask going unanswered.
    const auto start = [this](const std::shared_ptr<Pending>& one) {
        if (one->done)
        {
            return;
        }
        one->done = true;
        one->heard.disconnect();
        if (setRunning(one->ref, true))
        {
            askRunning(one->ref);
        }
    };
    pending->heard = mRunningState.connect([pending = std::weak_ptr<Pending>(pending), start](const RunningState& state) {
        const std::shared_ptr<Pending> one = pending.lock();
        if (one && state.ref == one->ref && !state.running)
        {
            start(one);
        }
    });
    askRunning(ref);
    // Asked again while it still runs, the stop not there yet.
    auto ask = std::make_shared<std::function<void(S32)>>();
    *ask = [this, pending, start, ask_weak = std::weak_ptr<std::function<void(S32)>>(ask)](S32 left) {
        if (pending->done)
        {
            return;
        }
        if (left <= 0)
        {
            start(pending);
            return;
        }
        askRunning(pending->ref);
        if (const auto again = ask_weak.lock())
        {
            doAfterInterval([again, left]() { (*again)(left - 1); }, RESTART_ASK_EVERY);
        }
    };
    doAfterInterval([ask]() { (*ask)(RESTART_ASKS); }, RESTART_ASK_EVERY);
    return true;
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

void ALScriptWorkspace::listContents(const LLUUID& prim, contents_callback_t callback, bool from_region)
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
    if (!rlvRefusal(object, LLAssetType::AT_NONE, RlvUse::See).empty())
    {
        // Not to be seen: listed as holding nothing, so that what was
        // listed before goes as well.
        Contents hidden;
        hidden.prim    = prim;
        hidden.fetched = true;
        callback(hidden);
        return;
    }
    if (from_region)
    {
        // Its copy let go of once the listener asks, which fetches anew.
        object->dirtyInventory();
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

std::vector<ALScriptWorkspace::RuntimeEvent> ALScriptWorkspace::runtimeErrorsOf(const LLUUID& prim, const LLUUID& item) const
{
    std::vector<RuntimeEvent> errors;
    const auto                since = mRuntimeSince.find(item);
    for (const RuntimeEvent& event : mRecent)
    {
        if (event.isError && event.prim == prim && event.item == item && (since == mRuntimeSince.end() || event.time > since->second))
        {
            errors.push_back(event);
        }
    }
    return errors;
}

void ALScriptWorkspace::forgetRuntime(const LLUUID& item)
{
    if (item.notNull())
    {
        mRuntimeSince[item] = LLDate::now().secondsSinceEpoch();
    }
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
    if (std::string refused = rlvRefusal(prim, LLAssetType::AT_NONE, RlvUse::Change); !refused.empty())
    {
        error = std::move(refused);
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
    if (std::string refused = rlvRefusal(object, item->getType(), RlvUse::Change); !refused.empty())
    {
        error = std::move(refused);
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

bool ALScriptWorkspace::renameObject(const LLUUID& prim_id, const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = LLTrans::getString("WorkspaceNameNeeded");
        return false;
    }
    LLViewerObject* prim   = gObjectList.findObject(prim_id);
    LLViewerRegion* region = prim ? prim->getRegion() : nullptr;
    if (!region)
    {
        error = LLTrans::getString("WorkspaceNoSuchObject");
        return false;
    }
    if (std::string refused = rlvRefusal(prim, LLAssetType::AT_NONE, RlvUse::Change); !refused.empty())
    {
        error = std::move(refused);
        return false;
    }
    if (!prim->permModify())
    {
        error = LLTrans::getString("WorkspaceObjectNotModifiable");
        return false;
    }
    // By its local id, as the selection's rename sends it, without its
    // being selected.
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ObjectName);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_ObjectData);
    msg->addU32Fast(_PREHASH_LocalID, prim->getLocalID());
    msg->addStringFast(_PREHASH_Name, name);
    msg->sendReliable(region->getHost());
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
    if (std::string refused = rlvRefusal(object, item->getType(), RlvUse::Change); !refused.empty())
    {
        error = std::move(refused);
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

bool ALScriptWorkspace::queue(Queue kind, const std::vector<std::pair<LLUUID, std::string>>& prims, const std::string& target, std::string& error,
                              std::map<std::pair<LLUUID, LLUUID>, bool> running)
{
    // Only the objects RLVa lets be changed; none, and why.
    std::vector<std::pair<LLUUID, std::string>> allowed;
    std::string                                  refused;
    for (const auto& prim : prims)
    {
        const std::string why = rlvRefusal(gObjectList.findObject(prim.first), LLAssetType::AT_NONE, RlvUse::Change);
        if (why.empty())
        {
            allowed.push_back(prim);
        }
        else
        {
            refused = why;
        }
    }
    if (allowed.empty())
    {
        error = refused.empty() ? LLTrans::getString("WorkspaceNothingToDo") : refused;
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
    queue->setKnownRunning(std::move(running));
    for (const auto& [prim, prim_name] : allowed)
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

    // What goes on with the error being gathered: from its script, on its
    // channel, the words straight after its first line, and after them
    // only what an error goes on with -- a place, a stack -- to a cap.
    if (mBurst && !header && mBurst->fromId == chat.mFromID && mBurst->fromName == chat.mFromName && mBurst->channel == channel &&
        mBurst->lines + lines.size() <= BURST_MOST_LINES &&
        (mBurst->texts.size() == 1 || std::all_of(lines.begin(), lines.end(), ALScriptMessages::continuesRuntimeError)))
    {
        mBurst->texts.push_back(chat.mText);
        mBurst->lines += lines.size();
        return;
    }
    // Anything else ends it; and a line that is not the start of another
    // error is an event by itself.
    flushRuntime();
    if (!header)
    {
        Burst alone;
        alone.fromId   = chat.mFromID;
        alone.fromName = chat.mFromName;
        alone.channel  = channel;
        alone.texts.push_back(chat.mText);
        deliverRuntime(alone);
        return;
    }

    // An error's start: which VM the script runs on, which its item says,
    // and what follows gathered for a moment from now, and no longer.
    bool lua = false;
    if (LLViewerObject* prim = gObjectList.findObject(chat.mFromID))
    {
        if (LLInventoryItem* item = scriptNamed(prim, named.script))
        {
            lua = item->getRuntime() == "luau";
        }
    }
    mBurst           = std::make_unique<Burst>();
    mBurst->fromId   = chat.mFromID;
    mBurst->fromName = chat.mFromName;
    mBurst->lua      = lua;
    mBurst->channel  = channel;
    mBurst->texts.push_back(chat.mText);
    mBurst->lines    = lines.size();
    mBurstTimer      = LLEventTimer::run_after(BURST_WINDOW, [this]() {
        // It lets itself go as this returns.
        mBurstTimer = nullptr;
        flushRuntime();
    });
}

void ALScriptWorkspace::flushRuntime()
{
    if (mBurstTimer)
    {
        delete mBurstTimer;
        mBurstTimer = nullptr;
    }
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
