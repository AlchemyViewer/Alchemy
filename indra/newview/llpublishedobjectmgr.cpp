/**
 * @file llpublishedobjectmgr.cpp
 * @brief Published object state/logic manager extracted from llscripteditorws
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
#include "llpublishedobjectmgr.h"

#include "llscripteditorws.h"

#include "llchat.h"
#include "llinventorydefines.h"
#include "llsdutil.h"
#include "llselectmgr.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoinventorylistener.h"

namespace
{
    // Linkset flush coalescing delays (seconds).
    constexpr F32 LINKSET_ADD_FLUSH_DELAY    = 5.0f;
    constexpr F32 LINKSET_REMOVE_FLUSH_DELAY = 0.2f;
}

namespace
{
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

    std::string get_prim_name(LLViewerObject* obj)
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

    void add_object_permissions(LLSD& object_data, LLViewerObject* object)
    {
        LLPermissions* permissions =
            LLSelectMgr::getInstance()->findObjectPermissions(object);
        if (!permissions)
        {
            return;
        }

        LLSD permission_entry;
        permission_entry["owner"] =
            static_cast<S32>(permissions->getMaskOwner());
        permission_entry["next_owner"] =
            static_cast<S32>(permissions->getMaskNextOwner());
        object_data["permissions"] = permission_entry;
    }
}

class LLPublishedPrimListener : public LLVOInventoryListener
{
public:
    LLPublishedPrimListener(LLScriptEditorWSServer* server, const LLUUID& object_id, const LLUUID& prim_id,
                            LLViewerObject* object)
        : mServer(server)
        , mObjectID(object_id)
        , mPrimID(prim_id)
    {
        registerVOInventoryListener(object, nullptr);
    }

    ~LLPublishedPrimListener() override = default;

    void inventoryChanged(LLViewerObject* object,
                         LLInventoryObject::object_list_t* inventory,
                         S32 serial_num, void* user_data) override
    {
        if (mServer)
        {
            if (mServer->isObjectPublished(mObjectID))
            {
                mServer->onPrimInventoryChanged(mObjectID, mPrimID);
            }
            else
            {
                mServer->onPrimInventoryReady(mObjectID, mPrimID);
            }
        }
    }

    const LLUUID& getObjectID() const { return mObjectID; }
    const LLUUID& getPrimID() const { return mPrimID; }

private:
    LLScriptEditorWSServer* mServer;
    LLUUID                  mObjectID;
    LLUUID                  mPrimID;
};

LLPublishedObjectMgr::LLPublishedObjectMgr(LLScriptEditorWSServer* server):
    mServer(server)
{
}

LLPublishedObjectMgr::~LLPublishedObjectMgr() = default;

LLPublishedObjectMgr::PublishedObjectInfo::PublishedObjectInfo() = default;
LLPublishedObjectMgr::PublishedObjectInfo::~PublishedObjectInfo() = default;
LLPublishedObjectMgr::PublishedObjectInfo::PublishedObjectInfo(PublishedObjectInfo&&) noexcept = default;
LLPublishedObjectMgr::PublishedObjectInfo& LLPublishedObjectMgr::PublishedObjectInfo::operator=(PublishedObjectInfo&&) noexcept = default;

LLPublishedObjectMgr::PendingPublish::PendingPublish() = default;
LLPublishedObjectMgr::PendingPublish::~PendingPublish() = default;
LLPublishedObjectMgr::PendingPublish::PendingPublish(PendingPublish&&) noexcept = default;
LLPublishedObjectMgr::PendingPublish& LLPublishedObjectMgr::PendingPublish::operator=(PendingPublish&&) noexcept = default;

void LLPublishedObjectMgr::beginPendingPublish(const LLUUID& object_id, const std::vector<LLViewerObject*>& prims)
{
    PendingPublish pending;
    pending.mObjectID = object_id;
    for (LLViewerObject* prim : prims)
    {
        pending.mPendingPrims.insert(prim->getID());
        auto listener = std::make_unique<LLPublishedPrimListener>(
            mServer, object_id, prim->getID(), prim);
        pending.mListeners.push_back(std::move(listener));
    }
    mPendingPublishes[object_id] = std::move(pending);
}

bool LLPublishedObjectMgr::hasPendingPublish(const LLUUID& object_id) const
{
    return mPendingPublishes.find(object_id) != mPendingPublishes.end();
}

bool LLPublishedObjectMgr::markPendingPublishPrimReady(const LLUUID& object_id, const LLUUID& prim_id)
{
    auto it = mPendingPublishes.find(object_id);
    if (it == mPendingPublishes.end())
    {
        return false;
    }

    it->second.mPendingPrims.erase(prim_id);
    return it->second.mPendingPrims.empty();
}

void LLPublishedObjectMgr::recordPendingPropertyChange(
    const LLUUID& root_id,
    const LLUUID& prim_id,
    const std::string& name,
    const std::string& desc)
{
    auto it = mPendingPublishes.find(root_id);
    if (it == mPendingPublishes.end())
    {
        return;
    }

    PendingPublish& pending = it->second;
    if (prim_id == root_id)
    {
        pending.mHasRootProperties = true;
        pending.mObjectDescription = desc;
        if (!name.empty())
        {
            pending.mObjectName = name;
        }
        return;
    }

    if (!name.empty())
    {
        pending.mPrimNames[prim_id] = name;
    }
    pending.mPrimDescriptions[prim_id] = desc;
}

std::vector<std::unique_ptr<LLPublishedPrimListener>> LLPublishedObjectMgr::takePendingPublishListeners(const LLUUID& object_id)
{
    auto it = mPendingPublishes.find(object_id);
    if (it == mPendingPublishes.end())
    {
        return {};
    }

    auto listeners = std::move(it->second.mListeners);
    mPendingPublishes.erase(it);
    return listeners;
}

void LLPublishedObjectMgr::cancelPendingPublish(const LLUUID& object_id)
{
    mPendingPublishes.erase(object_id);
}

void LLPublishedObjectMgr::cancelPendingPublishWithCleanup(const LLUUID& object_id)
{
    auto it = mPendingPublishes.find(object_id);
    if (it == mPendingPublishes.end())
    {
        return;
    }

    it->second.mListeners.clear();
    mPendingPublishes.erase(it);
}

LLPublishedObjectMgr::PublishedObjectInfo* LLPublishedObjectMgr::getPublished(const LLUUID& object_id)
{
    auto it = mPublishedObjects.find(object_id);
    if (it == mPublishedObjects.end())
    {
        return nullptr;
    }

    return &it->second;
}

const LLPublishedObjectMgr::PublishedObjectInfo* LLPublishedObjectMgr::getPublished(const LLUUID& object_id) const
{
    auto it = mPublishedObjects.find(object_id);
    if (it == mPublishedObjects.end())
    {
        return nullptr;
    }

    return &it->second;
}

bool LLPublishedObjectMgr::reservePendingItemCreate(const LLUUID& prim_id, std::string&& pump_name)
{
    auto it = mPendingItemCreates.find(prim_id);
    if (it != mPendingItemCreates.end())
    {
        return false;
    }

    mPendingItemCreates[prim_id] = std::move(pump_name);
    return true;
}

bool LLPublishedObjectMgr::findPendingItemCreate(const LLUUID& prim_id, std::string& pump_name) const
{
    auto it = mPendingItemCreates.find(prim_id);
    if (it == mPendingItemCreates.end())
    {
        return false;
    }

    pump_name = it->second;
    return true;
}

void LLPublishedObjectMgr::clearPendingItemCreate(const LLUUID& prim_id)
{
    mPendingItemCreates.erase(prim_id);
}

LLSD LLPublishedObjectMgr::buildPrimInventoryLLSD(LLViewerObject* object) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLSD items = LLSD::emptyArray();
    if (!object)
    {
        return items;
    }

    LLInventoryObject::object_list_t contents;
    object->getInventoryContents(contents);
    const LLUUID root_id = object->getRootEdit()->getID();

    for (const auto& obj : contents)
    {
        LLInventoryItem* item = dynamic_cast<LLInventoryItem*>(obj.get());
        if (!item)
        {
            continue;
        }

        LLAssetType::EType type = item->getType();
        if (type != LLAssetType::AT_LSL_TEXT && type != LLAssetType::AT_NOTECARD)
        {
            continue;
        }

        LLSD entry;
        entry["item_id"]     = item->getUUID();
        entry["name"]        = item->getName();
        entry["description"] = item->getDescription();
        entry["type"]        = (type == LLAssetType::AT_LSL_TEXT) ? "script" : "notecard";

        // The revision goes up with the asset, which is never sent itself.
        Revision& revision = mRevisions[item->getUUID()];
        if (revision.number == 0 || revision.asset != item->getAssetUUID())
        {
            revision.asset  = item->getAssetUUID();
            revision.number = ++mLastRevision;
        }
        revision.root = root_id;
        entry["revision"] = static_cast<S32>(revision.number);

        if (type == LLAssetType::AT_LSL_TEXT)
        {
            U8 subtype = item->getInventorySubType();
            entry["subtype"] = static_cast<S32>(subtype);

            const std::string& runtime = item->getRuntime();
            if (!runtime.empty())
            {
                entry["vm"] = runtime;
            }

            LLViewerInventoryItem* viewer_item = dynamic_cast<LLViewerInventoryItem*>(item);
            if (viewer_item)
            {
                entry["running"] = viewer_item->getIsRunning();
                entry["faulted"] = viewer_item->getIsFaulted();
            }
        }

        const LLPermissions& perms = item->getPermissions();
        LLSD perm_entry;
        perm_entry["owner"]      = static_cast<S32>(perms.getMaskOwner());
        perm_entry["next_owner"] = static_cast<S32>(perms.getMaskNextOwner());
        entry["permissions"]     = perm_entry;

        entry["creator_id"] = perms.getCreator();

        items.append(entry);
    }

    return items;
}

LLSD LLPublishedObjectMgr::buildPublishedObjectLLSD(LLViewerObject* root) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLSD pub;
    add_object_permissions(pub, root);
    pub["object_id"]          = root->getID();
    pub["object_name"]        = get_prim_name(root);
    pub["object_description"] = nv_string(root, "Desc");
    pub["owner_id"]           = root->mOwnerID;
    if (root->getRegion())
    {
        pub["region"] = root->getRegion()->getName();
    }
    const PublishedObjectInfo* published_info = getPublished(root->getID());
    if (published_info)
    {
        pub["can_save_back"] = published_info->mCanSaveBackToContents;
    }
    pub["inventory"] = buildPrimInventoryLLSD(root);

    LLSD linked_objects = LLSD::emptyArray();
    S32 link_number = 2;
    for (LLViewerObject* child : root->getChildren())
    {
        LLSD link;
        link["link_id"]          = child->getID();
        link["link_number"]      = link_number++;
        link["link_name"]        = get_prim_name(child);
        link["link_description"] = nv_string(child, "Desc");
        add_object_permissions(link, child);
        link["inventory"]        = buildPrimInventoryLLSD(child);
        linked_objects.append(link);
    }
    if (linked_objects.size() > 0)
    {
        pub["linked_objects"] = linked_objects;
    }

    return pub;
}

LLSD LLPublishedObjectMgr::buildObjectListLLSD() const
{
    LLSD objects = LLSD::emptyArray();
    for (const auto& [object_id, info] : mPublishedObjects)
    {
        LLViewerObject* root = gObjectList.findObject(object_id);
        if (!root)
        {
            LL_DEBUGS("ScriptEditorWS") << "object.list: skipping " << object_id
                << " (no longer in scene)" << LL_ENDL;
            continue;
        }

        LLSD pub;
        add_object_permissions(pub, root);
        pub["object_id"]          = info.mObjectID;
        pub["object_name"]        = info.mObjectName;
        pub["object_description"] = info.mObjectDescription;
        pub["owner_id"]           = info.mOwnerID;
        if (!info.mRegionName.empty())
        {
            pub["region"] = info.mRegionName;
        }
        pub["can_save_back"] = info.mCanSaveBackToContents;
        pub["inventory"] = buildPrimInventoryLLSD(root);

        LLSD linked_objects = LLSD::emptyArray();
        for (const auto& prim_info : info.mPrims)
        {
            if (prim_info.mLinkNumber == 1)
            {
                continue;
            }

            LLViewerObject* child = gObjectList.findObject(prim_info.mPrimID);
            if (!child)
            {
                continue;
            }

            LLSD link;
            link["link_id"]     = prim_info.mPrimID;
            link["link_number"] = prim_info.mLinkNumber;
            link["link_name"]   = prim_info.mPrimName;
            link["link_description"] = prim_info.mPrimDescription;
            add_object_permissions(link, child);
            link["inventory"]   = buildPrimInventoryLLSD(child);
            linked_objects.append(link);
        }
        if (linked_objects.size() > 0)
        {
            pub["linked_objects"] = linked_objects;
        }

        objects.append(pub);
    }

    return objects;
}

bool LLPublishedObjectMgr::buildLinksetUpdateLLSD(
    const LLUUID& root_id, LLSD& update) const
{
    const PublishedObjectInfo* info = getPublished(root_id);
    if (!info)
    {
        return false;
    }

    LLSD linked_objects = LLSD::emptyArray();
    for (const PublishedPrimInfo& prim_info : info->mPrims)
    {
        if (prim_info.mPrimID == root_id)
        {
            continue;
        }

        LLSD entry;
        entry["link_id"]     = prim_info.mPrimID;
        entry["link_number"] = prim_info.mLinkNumber;

        LLViewerObject* prim = gObjectList.findObject(prim_info.mPrimID);
        std::string link_name = prim ? get_prim_name(prim) : std::string();
        if (link_name.empty())
        {
            link_name = prim_info.mPrimName;
        }
        std::string link_desc = prim ? nv_string(prim, "Desc") : std::string();
        if (link_desc.empty())
        {
            link_desc = prim_info.mPrimDescription;
        }
        entry["link_name"] = link_name;
        entry["link_description"] = link_desc;
        entry["inventory"] = prim ? buildPrimInventoryLLSD(prim) : LLSD::emptyArray();

        linked_objects.append(entry);
    }

    update = LLSD();
    update["object_id"]      = root_id;
    update["linked_objects"] = linked_objects;
    return true;
}

bool LLPublishedObjectMgr::reconcileLinksetChildAdded(
    const LLUUID& root_id,
    LLViewerObject* child,
    F64 request_start_sec)
{
    PublishedObjectInfo* info = getPublished(root_id);
    if (!info || !child)
    {
        return false;
    }

    const LLUUID child_id = child->getID();

    info->mPrims.erase(
        std::remove_if(
            info->mPrims.begin(),
            info->mPrims.end(),
            [&](const PublishedPrimInfo& p) { return p.mPrimID == child_id; }),
        info->mPrims.end());

    PublishedPrimInfo prim_info;
    prim_info.mPrimID          = child_id;
    prim_info.mPrimName        = get_prim_name(child);
    prim_info.mPrimDescription = nv_string(child, "Desc");
    prim_info.mLinkNumber      = static_cast<S32>(info->mPrims.size()) + 1;
    prim_info.mInventorySerial = -1;
    info->mPrims.push_back(prim_info);

    auto listener = std::make_unique<LLPublishedPrimListener>(
        mServer, root_id, child_id, child);
    info->mListeners.push_back(std::move(listener));

    mInventoryRequestStartSec[child_id] = request_start_sec;
    mNewChildPrims[root_id].insert(child_id);
    return true;
}

bool LLPublishedObjectMgr::reconcileLinksetChildRemoved(
    const LLUUID& root_id, const LLUUID& child_id)
{
    PublishedObjectInfo* info = getPublished(root_id);
    if (!info)
    {
        return false;
    }

    info->mPrims.erase(
        std::remove_if(
            info->mPrims.begin(),
            info->mPrims.end(),
            [&](const PublishedPrimInfo& p) { return p.mPrimID == child_id; }),
        info->mPrims.end());

    info->mListeners.erase(
        std::remove_if(
            info->mListeners.begin(),
            info->mListeners.end(),
            [&](const std::unique_ptr<LLPublishedPrimListener>& l)
            {
                return l->getPrimID() == child_id;
            }),
        info->mListeners.end());

    bool root_empty_after_remove = false;
    consumePendingNewChild(root_id, child_id, root_empty_after_remove);

    mInventoryRequestStartSec.erase(child_id);

    S32 link_num = 2;
    for (auto& p : info->mPrims)
    {
        if (p.mPrimID != root_id)
        {
            p.mLinkNumber = link_num++;
        }
    }

    return true;
}

bool LLPublishedObjectMgr::handlePrimInventoryReadyEvent(
    const LLUUID& object_id, const LLUUID& prim_id)
{
    return markPendingPublishPrimReady(object_id, prim_id);
}

LLPublishedObjectMgr::PrimInventoryEventResult
LLPublishedObjectMgr::handlePrimInventoryChangedEvent(
    const LLUUID& object_id,
    const LLUUID& prim_id,
    LLViewerObject* prim,
    F64 now_sec)
{
    PrimInventoryEventResult result;
    if (!hasPublished(object_id) || !prim)
    {
        return result;
    }

    F64 request_start_sec = 0.0;
    if (consumeInventoryRequestStart(prim_id, request_start_sec))
    {
        result.mTimingConsumed = true;
        result.mTimingElapsedSec = llmax(0.0, now_sec - request_start_sec);
    }

    InventoryChangeResult inv_result = reconcileInventoryChanged(object_id, prim_id, prim);
    result.mKind = inv_result.mKind;
    result.mUpdate = inv_result.mUpdate;

    if (result.mKind == InventoryChangeKind::ROOT_INVENTORY_UPDATE ||
        result.mKind == InventoryChangeKind::CHILD_INVENTORY_UPDATE)
    {
        std::string pending_item_create_pump;
        if (findPendingItemCreate(prim_id, pending_item_create_pump))
        {
            result.mHasPendingItemCreate = true;
            result.mPendingItemCreatePump = pending_item_create_pump;
        }
    }

    return result;
}

LLPublishedObjectMgr::InventoryChangeResult
LLPublishedObjectMgr::reconcileInventoryChanged(
    const LLUUID& object_id,
    const LLUUID& prim_id,
    LLViewerObject* prim)
{
    InventoryChangeResult result;
    PublishedObjectInfo* pub_info = getPublished(object_id);
    if (!pub_info || !prim)
    {
        return result;
    }

    bool root_empty_after_remove = false;
    if (consumePendingNewChild(object_id, prim_id, root_empty_after_remove))
    {
        for (auto& p : pub_info->mPrims)
        {
            if (p.mPrimID == prim_id)
            {
                p.mPrimName        = get_prim_name(prim);
                p.mPrimDescription = nv_string(prim, "Desc");
                p.mInventorySerial = 0;
                break;
            }
        }
        result.mKind = root_empty_after_remove
            ? InventoryChangeKind::CHILD_READY_FLUSH_NOW
            : InventoryChangeKind::CHILD_READY_WAIT;
        return result;
    }

    LLSD inv = buildPrimInventoryLLSD(prim);
    // What was sent already is not sent again: the prim answers whoever
    // asks it, and every answer comes here.
    for (auto& p : pub_info->mPrims)
    {
        if (p.mPrimID != prim_id)
        {
            continue;
        }
        if (p.mSentInventory.isDefined() && llsd_equals(p.mSentInventory, inv))
        {
            result.mKind = InventoryChangeKind::UNCHANGED;
            return result;
        }
        p.mSentInventory = inv;
        break;
    }
    result.mUpdate = LLSD();
    result.mUpdate["object_id"] = object_id;
    if (prim_id == object_id)
    {
        result.mUpdate["inventory"] = inv;
        result.mKind = InventoryChangeKind::ROOT_INVENTORY_UPDATE;
    }
    else
    {
        LLSD modified_entry;
        modified_entry["link_id"]   = prim_id;
        modified_entry["inventory"] = inv;
        LLSD modified_arr = LLSD::emptyArray();
        modified_arr.append(modified_entry);
        result.mUpdate["changes"]["linked_objects"]["modified"] = modified_arr;
        result.mKind = InventoryChangeKind::CHILD_INVENTORY_UPDATE;
    }
    return result;
}

bool LLPublishedObjectMgr::applyPropertyChange(
    const LLUUID& root_id,
    const LLUUID& prim_id,
    const std::string& name,
    const std::string& desc,
    LLSD& update)
{
    PublishedObjectInfo* pub_info = getPublished(root_id);
    if (!pub_info)
    {
        return false;
    }

    update = LLSD();
    update["object_id"] = root_id;

    if (prim_id == root_id)
    {
        bool has_name = !name.empty();
        bool name_changed = has_name && (pub_info->mObjectName != name);
        bool desc_changed = (pub_info->mObjectDescription != desc);
        if (!name_changed && !desc_changed)
        {
            return false;
        }

        if (name_changed)
        {
            pub_info->mObjectName = name;
            update["object_name"] = name;
        }
        if (desc_changed)
        {
            pub_info->mObjectDescription = desc;
            update["object_description"] = desc;
        }
        return true;
    }

    auto prim_it = std::find_if(pub_info->mPrims.begin(), pub_info->mPrims.end(),
        [&](const PublishedPrimInfo& p) { return p.mPrimID == prim_id; });
    if (prim_it == pub_info->mPrims.end())
    {
        return false;
    }
    const bool name_changed = !name.empty() && prim_it->mPrimName != name;
    const bool desc_changed = prim_it->mPrimDescription != desc;
    if (!name_changed && !desc_changed)
    {
        return false;
    }

    LLSD modified_entry;
    modified_entry["link_id"] = prim_id;
    if (name_changed)
    {
        prim_it->mPrimName = name;
        modified_entry["link_name"] = name;
    }
    if (desc_changed)
    {
        prim_it->mPrimDescription = desc;
        modified_entry["link_description"] = desc;
    }
    LLSD modified_arr = LLSD::emptyArray();
    modified_arr.append(modified_entry);
    update["changes"]["linked_objects"]["modified"] = modified_arr;
    return true;
}

bool LLPublishedObjectMgr::hasActiveLinksetFlushTimer(const LLUUID& root_id) const
{
    auto it = mLinksetFlushTimers.find(root_id);
    if (it == mLinksetFlushTimers.end())
    {
        return false;
    }

    return !it->second.expired();
}

void LLPublishedObjectMgr::setLinksetFlushTimer(
    const LLUUID& root_id, const std::weak_ptr<LLEventTimer>& timer)
{
    mLinksetFlushTimers[root_id] = timer;
}

bool LLPublishedObjectMgr::cancelLinksetFlushTimer(const LLUUID& root_id)
{
    auto it = mLinksetFlushTimers.find(root_id);
    if (it == mLinksetFlushTimers.end())
    {
        return false;
    }

    if (auto locked = it->second.lock())
    {
        delete locked.get();
    }

    mLinksetFlushTimers.erase(it);
    return true;
}

void LLPublishedObjectMgr::clearLinksetFlushTimer(const LLUUID& root_id)
{
    mLinksetFlushTimers.erase(root_id);
}

bool LLPublishedObjectMgr::consumeInventoryRequestStart(
    const LLUUID& prim_id, F64& start_sec)
{
    auto it = mInventoryRequestStartSec.find(prim_id);
    if (it == mInventoryRequestStartSec.end())
    {
        return false;
    }

    start_sec = it->second;
    mInventoryRequestStartSec.erase(it);
    return true;
}

bool LLPublishedObjectMgr::markPrimInventorySerialAndDetectChange(
    const LLUUID& root_id, const LLUUID& prim_id, S16 inventory_serial)
{
    if (inventory_serial < 0)
    {
        return false;
    }

    PublishedObjectInfo* info = getPublished(root_id);
    if (!info)
    {
        return false;
    }

    auto it = std::find_if(
        info->mPrims.begin(),
        info->mPrims.end(),
        [&](const PublishedPrimInfo& p)
        {
            return p.mPrimID == prim_id;
        });
    if (it == info->mPrims.end())
    {
        return false;
    }

    if (it->mInventorySerial == inventory_serial)
    {
        return false;
    }

    it->mInventorySerial = inventory_serial;
    return true;
}

bool LLPublishedObjectMgr::consumePendingNewChild(
    const LLUUID& root_id, const LLUUID& child_id, bool& root_empty_after_remove)
{
    root_empty_after_remove = false;
    auto root_it = mNewChildPrims.find(root_id);
    if (root_it == mNewChildPrims.end())
    {
        return false;
    }

    auto child_it = root_it->second.find(child_id);
    if (child_it == root_it->second.end())
    {
        return false;
    }

    root_it->second.erase(child_it);
    if (root_it->second.empty())
    {
        root_empty_after_remove = true;
        mNewChildPrims.erase(root_it);
    }

    return true;
}

LLPublishedObjectMgr::PublishedObjectInfo& LLPublishedObjectMgr::finalizePendingPublish(
    const LLUUID& object_id, PublishedObjectInfo&& info)
{
    PublishedObjectInfo& published_info = mPublishedObjects[object_id];
    auto pending_it = mPendingPublishes.find(object_id);
    published_info = std::move(info);

    if (pending_it != mPendingPublishes.end())
    {
        PendingPublish& pending = pending_it->second;
        if (pending.mHasRootProperties)
        {
            if (!pending.mObjectName.empty())
            {
                published_info.mObjectName = pending.mObjectName;
            }
            published_info.mObjectDescription = pending.mObjectDescription;
        }

        for (PublishedPrimInfo& prim_info : published_info.mPrims)
        {
            auto name_it = pending.mPrimNames.find(prim_info.mPrimID);
            if (name_it != pending.mPrimNames.end() && !name_it->second.empty())
            {
                prim_info.mPrimName = name_it->second;
            }

            auto desc_it = pending.mPrimDescriptions.find(prim_info.mPrimID);
            if (desc_it != pending.mPrimDescriptions.end())
            {
                prim_info.mPrimDescription = desc_it->second;
            }
        }

        published_info.mListeners = std::move(pending.mListeners);
        mPendingPublishes.erase(pending_it);
    }
    else
    {
        published_info.mListeners = takePendingPublishListeners(object_id);
    }

    return published_info;
}

bool LLPublishedObjectMgr::cleanupObjectStateForUnpublish(const LLUUID& object_id)
{
    const bool was_published = hasPublished(object_id);

    cancelPendingPublishWithCleanup(object_id);
    clearPublishedListeners(object_id);
    erasePublished(object_id);

    cancelLinksetFlushTimer(object_id);
    clearPendingNewChildren(object_id);

    // Its items' revisions go with it; the count they were drawn from
    // stays, so that they come back higher.
    std::erase_if(mRevisions, [&object_id](const auto& entry) { return entry.second.root == object_id; });

    return was_published;
}

void LLPublishedObjectMgr::clearPublishedListeners(const LLUUID& object_id)
{
    auto pub_info = getPublished(object_id);
    if (!pub_info)
    {
        return;
    }

    pub_info->mListeners.clear();
}

void LLPublishedObjectMgr::clearAllStateWithListenerCleanup()
{
    for (auto& [id, pending] : mPendingPublishes)
    {
        pending.mListeners.clear();
    }
    mPendingPublishes.clear();

    for (auto& [id, info] : mPublishedObjects)
    {
        info.mListeners.clear();
    }
    mPublishedObjects.clear();
    mRevisions.clear();
}

// --- what the world tells the server, and the publishing it comes to ---

// static
std::vector<LLViewerObject*> LLPublishedObjectMgr::linksetOf(LLViewerObject* root)
{
    // [root, *root->getChildren()] in stable order. Root must be non-null.
    std::vector<LLViewerObject*> prims;
    const auto& children = root->getChildren();
    prims.reserve(1 + children.size());
    prims.push_back(root);
    for (LLViewerObject* child : children)
    {
        prims.push_back(child);
    }
    return prims;
}

void LLPublishedObjectMgr::onPrimInventoryReady(const LLUUID& object_id, const LLUUID& prim_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (handlePrimInventoryReadyEvent(object_id, prim_id))
    {
        LL_DEBUGS("ScriptEditorWS") << "All prim inventories ready for object " << object_id << LL_ENDL;
        buildAndSendPublish(object_id);
    }
}

void LLPublishedObjectMgr::buildAndSendPublish(const LLUUID& object_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!hasPendingPublish(object_id))
    {
        LL_WARNS("ScriptEditorWS") << "buildAndSendPublish: no pending publish for " << object_id << LL_ENDL;
        return;
    }

    LLViewerObject* root = gObjectList.findObject(object_id);
    if (!root)
    {
        LL_WARNS("ScriptEditorWS") << "buildAndSendPublish: root object gone: " << object_id << LL_ENDL;
        cancelPendingPublish(object_id);
        return;
    }

    LLSD pub = buildPublishedObjectLLSD(root);

    // Store in the published registry
    LLPublishedObjectMgr::PublishedObjectInfo info;
    info.mObjectID          = root->getID();
    info.mOwnerID           = root->mOwnerID;
    info.mObjectName        = pub["object_name"].asString();
    info.mObjectDescription = pub["object_description"].asString();
    if (root->getRegion())
    {
        info.mRegionName = root->getRegion()->getName();
    }
    LLSelectNode* root_select_node = LLSelectMgr::instance().getSelection()->findNode(root);
    if (root_select_node
        && root_select_node->mValid
        && !root_select_node->mFromTaskID.isNull()
        && !root->isAttachment())
    {
        info.mCanSaveBackToContents = true;
        info.mSourceTaskID = root_select_node->mFromTaskID;
    }
    else
    {
        info.mCanSaveBackToContents = false;
        info.mSourceTaskID.setNull();
    }

    // Each prim's inventory as the publish sends it, for what changes later
    // to be told from what does not.
    std::map<LLUUID, LLSD> sent_inventories;
    sent_inventories[root->getID()] = pub.get("inventory");
    if (pub.has("linked_objects"))
    {
        for (const LLSD& link : llsd::inArray(pub.get("linked_objects")))
        {
            sent_inventories[link.get("link_id").asUUID()] = link.get("inventory");
        }
    }
    S32 link_num = 1;
    std::vector<LLViewerObject*> prims = linksetOf(root);
    for (LLViewerObject* prim : prims)
    {
        LLPublishedObjectMgr::PublishedPrimInfo prim_info;
        prim_info.mPrimID          = prim->getID();
        prim_info.mPrimName        = LLScriptEditorWSServer::getPrimName(prim);  // Use helper with selection fallback
        prim_info.mLinkNumber      = link_num++;
        prim_info.mInventorySerial = static_cast<S16>(prim->getInventorySerial());
        if (const auto sent = sent_inventories.find(prim_info.mPrimID); sent != sent_inventories.end())
        {
            prim_info.mSentInventory = sent->second;
        }
        info.mPrims.push_back(prim_info);
    }

    LLPublishedObjectMgr::PublishedObjectInfo& published_info = finalizePendingPublish(object_id, std::move(info));

    // Align outgoing publish payload with any property responses that arrived
    // while inventory-gated publish was still pending.
    pub["object_name"] = published_info.mObjectName;
    pub["can_save_back"] = published_info.mCanSaveBackToContents;
    pub["object_description"] = published_info.mObjectDescription;
    if (pub.has("linked_objects"))
    {
        LLSD& linked_objects = pub["linked_objects"];
        for (S32 i = 0; i < linked_objects.size(); ++i)
        {
            const LLUUID link_id = linked_objects[i]["link_id"].asUUID();
            auto prim_it = std::find_if(
                published_info.mPrims.begin(),
                published_info.mPrims.end(),
                [&](const LLPublishedObjectMgr::PublishedPrimInfo& p)
                {
                    return p.mPrimID == link_id;
                });
            if (prim_it != published_info.mPrims.end())
            {
                linked_objects[i]["link_name"] = prim_it->mPrimName;
                linked_objects[i]["link_description"] = prim_it->mPrimDescription;
            }
        }
    }

    // Send notification
    LLSD message;
    message["object"] = pub;
    mServer->notifyAll("object.publish", message);

    LL_INFOS("ScriptEditorWS") << "Published object " << object_id
        << " (" << pub["object_name"].asString() << ") with "
        << (prims.size() - 1) << " linked prim(s)" << LL_ENDL;

    // Re-request object properties now that the object is published so
    // onObjectPropertyChanged can emit object.update for root and linked prims.
    for (LLViewerObject* prim : prims)
    {
        LLSelectMgr::instance().requestObjectPropertiesFamily(prim);
    }
}

void LLPublishedObjectMgr::onLinksetChildAdded(const LLUUID& root_id, LLViewerObject* child)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!child)
    {
        return;
    }

    if (!reconcileLinksetChildAdded(
            root_id,
            child,
            LLTimer::getTotalSeconds().value()))
    {
        return;
    }

    // Request inventory (async; fires onPrimInventoryChanged when ready).
    child->requestInventory();

    // Start safety-timeout timer (no-op if one is already pending for this root)
    scheduleLinksetFlush(root_id, LINKSET_ADD_FLUSH_DELAY);
}

void LLPublishedObjectMgr::onLinksetChildRemoved(const LLUUID& root_id, const LLUUID& child_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!reconcileLinksetChildRemoved(root_id, child_id))
    {
        return;
    }

    // Schedule coalesced flush - multiple simultaneous removes share one timer
    scheduleLinksetFlush(root_id, LINKSET_REMOVE_FLUSH_DELAY);
}

void LLPublishedObjectMgr::scheduleLinksetFlush(const LLUUID& root_id, F32 delay)
{
    // No-op if a timer is already pending for this root_id
    if (hasActiveLinksetFlushTimer(root_id))
    {
        return;
    }

    // The manager lives inside the server, so the server standing is
    // the manager standing.
    std::weak_ptr<LLWebsocketMgr::WSServer> weak = mServer->weak_from_this();
    LLEventTimer* t = LLEventTimer::run_after(delay, [weak, this, root_id]()
    {
        if (weak.lock())
        {
            clearLinksetFlushTimer(root_id);
            clearPendingNewChildren(root_id); // clear any remaining pending children (timeout path)
            flushLinksetUpdate(root_id);
        }
    });
    setLinksetFlushTimer(root_id, t->getWeak());
}

void LLPublishedObjectMgr::flushLinksetUpdate(const LLUUID& root_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLSD update;
    if (!buildLinksetUpdateLLSD(root_id, update))
    {
        return;
    }
    mServer->notifyAll("object.update", update);

    const LLSD linked_objects = update["linked_objects"];
    LL_INFOS("ScriptEditorWS") << "Linkset update for " << root_id
        << ": " << linked_objects.size() << " child(ren)" << LL_ENDL;
}

void LLPublishedObjectMgr::onPrimInventoryChanged(const LLUUID& object_id, const LLUUID& prim_id)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (!hasPublished(object_id))
    {
        return;
    }

    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
    {
        return;
    }

    auto inv_result = handlePrimInventoryChangedEvent(
        object_id, prim_id, prim, LLTimer::getTotalSeconds().value());

    if (inv_result.mTimingConsumed)
    {
        LL_DEBUGS("ScriptEditorWS") << "[Phase0] inventory refresh object_id=" << object_id
            << " prim_id=" << prim_id
            << " elapsed_sec=" << inv_result.mTimingElapsedSec << LL_ENDL;
    }

    if (inv_result.mKind == LLPublishedObjectMgr::InventoryChangeKind::CHILD_READY_WAIT)
    {
        return;
    }
    if (inv_result.mKind == LLPublishedObjectMgr::InventoryChangeKind::CHILD_READY_FLUSH_NOW)
    {
        cancelLinksetFlushTimer(object_id);
        flushLinksetUpdate(object_id);
        return;
    }
    if (inv_result.mKind == LLPublishedObjectMgr::InventoryChangeKind::ROOT_INVENTORY_UPDATE ||
        inv_result.mKind == LLPublishedObjectMgr::InventoryChangeKind::CHILD_INVENTORY_UPDATE)
    {
        mServer->notifyAll("object.update", inv_result.mUpdate);
        if (inv_result.mHasPendingItemCreate)
        {
            LLEventPumps::instance().post(
                inv_result.mPendingItemCreatePump,
                LLSD().with("prim_id", prim_id));
        }

        LL_DEBUGS("ScriptEditorWS") << "Sent object.update for prim " << prim_id
                                    << " in object " << object_id << LL_ENDL;
    }
}

void LLPublishedObjectMgr::onObjectPropertyChanged(
    const LLUUID& prim_id, const std::string& name, const std::string& desc, S16 inventory_serial)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    LLViewerObject* prim = gObjectList.findObject(prim_id);
    if (!prim)
    {
        return;
    }

    LLUUID root_id = prim->getRootEdit()->getID();

    recordPendingPropertyChange(root_id, prim_id, name, desc);

    bool should_refresh_inventory = markPrimInventorySerialAndDetectChange(
        root_id,
        prim_id,
        inventory_serial);

    LLSD update;
    if (applyPropertyChange(root_id, prim_id, name, desc, update))
    {
        mServer->notifyAll("object.update", update);
    }

    if (should_refresh_inventory && !hasInventoryRequestStart(prim_id))
    {
        prim->dirtyInventory();
        setInventoryRequestStart(prim_id, LLTimer::getTotalSeconds().value());
        prim->requestInventory();
    }
}
