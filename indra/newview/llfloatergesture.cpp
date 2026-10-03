/**
 * @file llfloatergesture.cpp
 * @brief Read-only list of gestures from your inventory.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llfloatergesture.h"

#include "llinventory.h"
#include "llinventorybridge.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llclipboard.h"

#include "llagent.h"
#include "llappearancemgr.h"
#include "llclipboard.h"
#include "llgesturemgr.h"
#include "llfiltereditor.h"
#include "llkeyboard.h"
#include "llmenugl.h"
#include "llmultigesture.h"
#include "llnotificationsutil.h"
#include "llpreviewgesture.h"
#include "llscrolllistctrl.h"
#include "lltrans.h"
#include "llviewergesture.h"
#include "llviewermenu.h"
#include "llviewerinventory.h"
#include "llviewercontrol.h"
#include "llfloaterperms.h"

namespace
{
enum GestureColumn
{
    ACTIVE_COLUMN,
    NAME_COLUMN,
    CHAT_COLUMN,
    KEY_SORT_COLUMN,
    KEY_COLUMN,
    LAST_USED_COLUMN
};
}

bool item_name_precedes( LLInventoryItem* a, LLInventoryItem* b )
{
    return LLStringUtil::precedesDict( a->getName(), b->getName() );
}

class LLFloaterGestureObserver : public LLGestureManagerObserver
{
public:
    LLFloaterGestureObserver(LLFloaterGesture* floater) : mFloater(floater) {}
    virtual ~LLFloaterGestureObserver() {}
    virtual void changed() { mFloater->refreshAll(); }

private:
    LLFloaterGesture* mFloater;
};
//-----------------------------
// GestureCallback
//-----------------------------

class GestureShowCallback : public LLInventoryCallback
{
public:
    void fire(const LLUUID &inv_item)
    {
        LLPreviewGesture::show(inv_item, LLUUID::null);

        LLInventoryItem* item = gInventory.getItem(inv_item);
        if (item)
        {
            LLPermissions perm = item->getPermissions();
            perm.setMaskNext(LLFloaterPerms::getNextOwnerPerms("Gestures"));
            perm.setMaskEveryone(LLFloaterPerms::getEveryonePerms("Gestures"));
            perm.setMaskGroup(LLFloaterPerms::getGroupPerms("Gestures"));
            item->setPermissions(perm);
            item->updateServer(false);
        }
    }
};

class GestureCopiedCallback : public LLInventoryCallback
{
private:
    LLFloaterGesture* mFloater;

public:
    GestureCopiedCallback(LLFloaterGesture* floater): mFloater(floater)
    {}
    void fire(const LLUUID &inv_item)
    {
        if(mFloater)
        {
            mFloater->addGesture(inv_item,NULL,mFloater->getChild<LLScrollListCtrl>("gesture_list"));

            // EXP-1909 (Pasted gesture displayed twice)
            // The problem is that addGesture is called here for the second time for the same item (which is copied)
            // First time addGesture is called from LLFloaterGestureObserver::changed(), which is a callback for inventory
            // change. So we need to refresh the gesture list to avoid duplicates.
            mFloater->refreshAll();
        }
    }
};

//---------------------------------------------------------------------------
// LLFloaterGesture
//---------------------------------------------------------------------------
LLFloaterGesture::LLFloaterGesture(const LLSD& key)
    : LLFloater(key)
{
    mObserver = new LLFloaterGestureObserver(this);
    LLGestureMgr::instance().addObserver(mObserver);

    mCommitCallbackRegistrar.add("Gesture.Action.ToggleActiveState", boost::bind(&LLFloaterGesture::onActivateBtnClick, this));
    mCommitCallbackRegistrar.add("Gesture.Action.ShowPreview", boost::bind(&LLFloaterGesture::onClickEdit, this));
    mCommitCallbackRegistrar.add("Gesture.Action.CopyPaste", boost::bind(&LLFloaterGesture::onCopyPasteAction, this, _2));
    mCommitCallbackRegistrar.add("Gesture.Action.SaveToCOF", boost::bind(&LLFloaterGesture::addToCurrentOutFit, this));
    mCommitCallbackRegistrar.add("Gesture.Action.Rename", boost::bind(&LLFloaterGesture::onRenameSelected, this));

    mEnableCallbackRegistrar.add("Gesture.EnableAction", boost::bind(&LLFloaterGesture::isActionEnabled, this, _2));
}

void LLFloaterGesture::done()
{
    //this method can be called twice: for GestureFolder and once after loading all sudir of GestureFolder
    if (gInventory.isCategoryComplete(mGestureFolderID))
    {
        LL_DEBUGS("Gesture")<< "mGestureFolderID loaded" << LL_ENDL;
        // we load only gesture folder without childred.
        LLInventoryModel::cat_array_t* categories;
        LLInventoryModel::item_array_t* items;
        uuid_vec_t unloaded_folders;
        LL_DEBUGS("Gesture")<< "Get subdirs of Gesture Folder...." << LL_ENDL;
        gInventory.getDirectDescendentsOf(mGestureFolderID, categories, items);
        if (categories->empty())
        {
            gInventory.removeObserver(this);
            LL_INFOS("Gesture")<< "Gesture dos NOT contains sub-directories."<< LL_ENDL;
            return;
        }
        LL_DEBUGS("Gesture")<< "There are " << categories->size() << " Folders "<< LL_ENDL;
        for (LLInventoryModel::cat_array_t::iterator it = categories->begin(); it != categories->end(); it++)
        {
            if (!gInventory.isCategoryComplete(it->get()->getUUID()))
            {
                unloaded_folders.push_back(it->get()->getUUID());
                LL_DEBUGS("Gesture")<< it->get()->getName()<< " Folder added to fetchlist"<< LL_ENDL;
            }

        }
        if (!unloaded_folders.empty())
        {
            LL_DEBUGS("Gesture")<< "Fetching subdirectories....." << LL_ENDL;
            setFetchIDs(unloaded_folders);
            startFetch();
        }
        else
        {
            LL_DEBUGS("Gesture")<< "All Gesture subdirectories have been loaded."<< LL_ENDL;
            gInventory.removeObserver(this);
            buildGestureList();
        }
    }
    else
    {
        LL_WARNS("Gesture")<< "Gesture list was NOT loaded"<< LL_ENDL;
    }
}

// virtual
LLFloaterGesture::~LLFloaterGesture()
{
    LLGestureMgr::instance().removeObserver(mObserver);
    delete mObserver;
    mObserver = NULL;
    gInventory.removeObserver(this);
}

// virtual
bool LLFloaterGesture::postBuild()
{
    std::string label;

    label = getTitle();

    setTitle(label);
    mGestureList = getChild<LLScrollListCtrl>("gesture_list");
    mGestureList->setCommitCallback(boost::bind(&LLFloaterGesture::onCommitList, this));
    mGestureList->setDoubleClickCallback(boost::bind(&LLFloaterGesture::onClickPlay, this));
    mGestureList->setSortCallback([](S32 column, const LLScrollListItem* lhs, const LLScrollListItem* rhs)
    {
        if (column == LAST_USED_COLUMN)
        {
            const auto lhs_time = LLGestureMgr::instance().getLastUsedTime(lhs->getUUID());
            const auto rhs_time = LLGestureMgr::instance().getLastUsedTime(rhs->getUUID());
            // Ascending elapsed time puts the most recently used first and
            // gestures without retained history last.
            return (lhs_time < rhs_time) - (lhs_time > rhs_time);
        }
        return LLStringUtil::compareDict(lhs->getColumn(column)->getValue().asString(),
                                        rhs->getColumn(column)->getValue().asString());
    });
    getChild<LLFilterEditor>("gesture_filter")->setCommitCallback(
        [this](LLUICtrl* ctrl, const LLSD&) { onFilterEdit(ctrl->getValue().asString()); });

    getChild<LLUICtrl>("edit_btn")->setCommitCallback(boost::bind(&LLFloaterGesture::onClickEdit, this));

    getChild<LLUICtrl>("play_btn")->setCommitCallback(boost::bind(&LLFloaterGesture::onClickPlay, this));
    getChild<LLUICtrl>("stop_btn")->setCommitCallback(boost::bind(&LLFloaterGesture::onClickPlay, this));
    getChild<LLButton>("activate_btn")->setClickedCallback(boost::bind(&LLFloaterGesture::onActivateBtnClick, this));

    getChild<LLUICtrl>("new_gesture_btn")->setCommitCallback(boost::bind(&LLFloaterGesture::onClickNew, this));
    getChild<LLButton>("del_btn")->setClickedCallback(boost::bind(&LLFloaterGesture::onDeleteSelected, this));

    getChildView("play_btn")->setVisible( true);
    getChildView("stop_btn")->setVisible( false);
    setDefaultBtn("play_btn");
    mGestureFolderID = gInventory.findCategoryUUIDForType(LLFolderType::FT_GESTURE);

    uuid_vec_t folders;
    folders.push_back(mGestureFolderID);
    //perform loading Gesture directory anyway to make sure that all subdirectory are loaded too. See method done() for details.
    gInventory.addObserver(this);
    setFetchIDs(folders);
    startFetch();

    if (mGestureList)
    {
        buildGestureList();

        mGestureList->setFocus(true);

        constexpr bool ascending = true;
        mGestureList->sortByColumn(std::string("name"), ascending);
        mGestureList->selectFirstItem();
    }

    // Update button labels
    onCommitList();

    return true;
}


void LLFloaterGesture::refreshAll()
{
    if (!mGestureList) return;

    buildGestureList();

    if (!mGestureList->getFirstSelected())
    {
        mGestureList->selectFirstItem();
    }

    // Update button labels
    onCommitList();
}

void LLFloaterGesture::onFilterEdit(const std::string& filter)
{
    std::string normalized = utf8str_tolower(filter);
    LLStringUtil::trim(normalized);
    if (normalized == mFilter)
        return;

    mFilter = std::move(normalized);
    refreshAll();
    mGestureList->setScrollPos(0);
}

std::string LLFloaterGesture::getLastUsedText(const LLUUID& item_id) const
{
    const auto last_used = LLGestureMgr::instance().getLastUsedTime(item_id);
    if (!last_used)
        return getString("never_used");

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        LLGestureMgr::usage_clock::now() - *last_used).count();
    std::string unit = "last_used_seconds";
    if (elapsed >= 86400)
    {
        elapsed /= 86400;
        unit = "last_used_days";
    }
    else if (elapsed >= 3600)
    {
        elapsed /= 3600;
        unit = "last_used_hours";
    }
    else if (elapsed >= 60)
    {
        elapsed /= 60;
        unit = "last_used_minutes";
    }
    LLStringUtil::format_map_t args;
    args["[COUNT]"] = std::to_string(elapsed);
    return getString(unit, args);
}

void LLFloaterGesture::draw()
{
    if (mLastUsedRefreshTimer.checkExpirationAndReset(1.f))
    {
        // Refresh labels in place without rebuilding rows or disturbing selection.
        for (LLScrollListItem* item : mGestureList->getAllData())
        {
            item->getColumn(LAST_USED_COLUMN)->setValue(getLastUsedText(item->getUUID()));
        }
    }
    LLFloater::draw();
}

void LLFloaterGesture::buildGestureList()
{
    S32 scroll_pos = mGestureList->getScrollPos();
    uuid_vec_t selected_items;
    getSelectedIds(selected_items);
    LL_DEBUGS("Gesture")<< "Rebuilding gesture list "<< LL_ENDL;
    mGestureList->deleteAllItems();

    LLGestureMgr::item_map_t::const_iterator it;
    const LLGestureMgr::item_map_t& active_gestures = LLGestureMgr::instance().getActiveGestures();
    for (it = active_gestures.begin(); it != active_gestures.end(); ++it)
    {
        addGesture(it->first,it->second, mGestureList);
    }
    if (gInventory.isCategoryComplete(mGestureFolderID))
    {
        LLIsType is_gesture(LLAssetType::AT_GESTURE);
        LLInventoryModel::cat_array_t categories;
        LLInventoryModel::item_array_t items;
        gInventory.collectDescendentsIf(mGestureFolderID, categories, items,
                LLInventoryModel::EXCLUDE_TRASH, is_gesture);

        for (LLInventoryModel::item_array_t::iterator it = items.begin(); it!= items.end(); ++it)
        {
            LLInventoryItem* item = it->get();
            if (active_gestures.find(item->getUUID()) == active_gestures.end())
            {
                // if gesture wasn't loaded yet, we can display only name
                addGesture(item->getUUID(), NULL, mGestureList);
            }
        }
    }

    // attempt to preserve scroll position through re-builds
    // since we do re-build whenever something gets dirty
    mGestureList->selectMultiple(selected_items);
    mGestureList->setScrollPos(scroll_pos);
    mGestureList->setCommentText(getString(mFilter.empty() ? "no_gestures" : "no_matching_gestures"));
}

void LLFloaterGesture::addGesture(const LLUUID& item_id, LLMultiGesture* gesture, LLCtrlListInterface* list)
{
    // Unloaded gestures expose only their inventory name.
    const LLInventoryItem* item = gInventory.getItem(item_id);
    std::string item_name = item ? item->getName() : gesture ? gesture->mName : getString("loading");
    const std::string trigger = gesture ? gesture->mTrigger : std::string();
    const bool has_key = gesture && gesture->mKey != KEY_NONE;
    const std::string shortcut = has_key ? LLKeyboard::stringFromAccelerator(gesture->mMask, gesture->mKey) : "---";

    if (!mFilter.empty()
        && utf8str_tolower(item_name).find(mFilter) == std::string::npos
        && utf8str_tolower(trigger).find(mFilter) == std::string::npos
        && (!has_key || utf8str_tolower(shortcut).find(mFilter) == std::string::npos))
    {
        return;
    }

    const bool playing = gesture && gesture->mPlaying;
    const bool active = LLGestureMgr::instance().isGestureActive(item_id);
    if (item && playing)
        item_name += " " + getString("playing");

    LLSD element;
    element["id"] = item_id;
    LLSD& columns = element["columns"];
    columns[ACTIVE_COLUMN]["column"] = "active";
    columns[ACTIVE_COLUMN]["type"] = "icon";
    columns[ACTIVE_COLUMN]["value"] = active ? "Activate_Checkmark" : "";
    columns[NAME_COLUMN]["column"] = "name";
    columns[NAME_COLUMN]["value"] = item_name;
    columns[CHAT_COLUMN]["column"] = "trigger";
    columns[CHAT_COLUMN]["value"] = trigger;
    columns[KEY_SORT_COLUMN]["column"] = "key";
    columns[KEY_SORT_COLUMN]["value"] = has_key ? LLKeyboard::stringFromKey(gesture->mKey) : "~~~";
    columns[KEY_COLUMN]["column"] = "shortcut";
    columns[KEY_COLUMN]["value"] = shortcut;
    columns[LAST_USED_COLUMN]["column"] = "last_used";
    columns[LAST_USED_COLUMN]["value"] = getLastUsedText(item_id);
    for (S32 column = NAME_COLUMN; column <= LAST_USED_COLUMN; ++column)
    {
        columns[column]["font"]["name"] = "SANSSERIF";
        columns[column]["font"]["style"] = playing ? "BOLD" : "NORMAL";
    }
    columns[NAME_COLUMN]["font"]["style"] = active ? "BOLD" : "NORMAL";

    if (LLScrollListItem* row = list->addElement(element, ADD_BOTTOM))
    {
        static_cast<LLScrollListIcon*>(row->getColumn(ACTIVE_COLUMN))->setIconSize(10);
    }
}

void LLFloaterGesture::getSelectedIds(uuid_vec_t& ids)
{
    std::vector<LLScrollListItem*> items = mGestureList->getAllSelected();
    for(std::vector<LLScrollListItem*>::const_iterator it = items.begin(); it != items.end(); it++)
    {
        ids.push_back((*it)->getUUID());
    }
}

bool LLFloaterGesture::isActionEnabled(const LLSD& command)
{
    // paste copy_uuid edit_gesture
    std::string command_name = command.asString();
    if("paste" == command_name)
    {
        if(!LLClipboard::instance().hasContents())
            return false;

        std::vector<LLUUID> ids;
        LLClipboard::instance().pasteFromClipboard(ids);
        for(std::vector<LLUUID>::iterator it = ids.begin(); it != ids.end(); it++)
        {
            LLInventoryItem* item = gInventory.getItem(*it);

            if(item && item->getInventoryType() == LLInventoryType::IT_GESTURE)
            {
                return true;
            }
        }
        return false;
    }
    else if("copy_uuid" == command_name || "edit_gesture" == command_name)
    {
        return  mGestureList->getAllSelected().size() == 1;
    }
    else if ("rename_gesture" == command_name)
    {
        if (mGestureList->getAllSelected().size() == 1)
        {
            LLViewerInventoryItem* item = gInventory.getItem(mGestureList->getCurrentID());

            if (item && item->getPermissions().allowModifyBy(gAgentID))
            {
                return true;
            }
        }
        return false;
    }
    return true;
}

void LLFloaterGesture::onClickPlay()
{
    const LLUUID& item_id = mGestureList->getCurrentID();
    if(item_id.isNull()) return;

    LL_DEBUGS("Gesture")<<" Trying to play gesture id: "<< item_id <<LL_ENDL;
    if(!LLGestureMgr::instance().isGestureActive(item_id))
    {
        // we need to inform server about gesture activating to be consistent with LLPreviewGesture and  LLGestureComboList.
        bool inform_server = true;
        bool deactivate_similar = false;
        LLGestureMgr::instance().setGestureLoadedCallback(item_id, boost::bind(&LLFloaterGesture::playGesture, this, item_id));
        LLViewerInventoryItem *item = gInventory.getItem(item_id);
        llassert(item);
        if (item)
        {
            LLGestureMgr::instance().activateGestureWithAsset(item_id, item->getAssetUUID(), inform_server, deactivate_similar);
            LL_DEBUGS("Gesture")<< "Activating gesture with inventory ID: " << item_id <<LL_ENDL;
        }
    }
    else
    {
        playGesture(item_id);
    }
}

void LLFloaterGesture::onClickNew()
{
    LLPointer<LLInventoryCallback> cb = new GestureShowCallback();
    create_inventory_item(gAgent.getID(),
                          gAgent.getSessionID(),
                          LLUUID::null,
                          LLTransactionID::tnull,
                          "New Gesture",
                          "",
                          LLAssetType::AT_GESTURE,
                          LLInventoryType::IT_GESTURE,
                          NO_INV_SUBTYPE,
                          PERM_MOVE | LLFloaterPerms::getNextOwnerPerms("Gestures"),
                          cb);
}

void LLFloaterGesture::onActivateBtnClick()
{
    uuid_vec_t ids;
    getSelectedIds(ids);
    if(ids.empty())
        return;

    LLGestureMgr* gm = LLGestureMgr::getInstance();
    uuid_vec_t::const_iterator it = ids.begin();
    bool first_gesture_state = gm->isGestureActive(*it);
    bool is_mixed = false;
    while( ++it != ids.end() )
    {
        if(first_gesture_state != gm->isGestureActive(*it))
        {
            is_mixed = true;
            break;
        }
    }
    for(uuid_vec_t::const_iterator it = ids.begin(); it != ids.end(); it++)
    {
        if(is_mixed)
        {
            gm->activateGesture(*it);
        }
        else
        {
            if(first_gesture_state)
            {
                gm->deactivateGesture(*it);
            }
            else
            {
                gm->activateGesture(*it);
            }
        }
    }
}

void LLFloaterGesture::onRenameSelected()
{
    LLViewerInventoryItem* gesture = gInventory.getItem(mGestureList->getCurrentID());
    if (!gesture)
    {
        return;
    }

    LLSD args;
    args["NAME"] = gesture->getName();

    LLSD payload;
    payload["gesture_id"] = mGestureList->getCurrentID();

    LLNotificationsUtil::add("RenameGesture", args, payload, boost::bind(onGestureRename, _1, _2));

}

void LLFloaterGesture::onGestureRename(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (option != 0) return; // canceled

    std::string new_name = response["new_name"].asString();
    LLInventoryObject::correctInventoryName(new_name);
    if (!new_name.empty())
    {
        LLUUID item_id = notification["payload"]["gesture_id"].asUUID();
        LLViewerInventoryItem* gesture = gInventory.getItem(item_id);
        if (gesture && (gesture->getName() != new_name))
        {
            LLSD updates;
            updates["name"] = new_name;
            update_inventory_item(item_id, updates, NULL);
        }
    }
}

void LLFloaterGesture::onCopyPasteAction(const LLSD& command)
{
    std::string command_name  = command.asString();
    // Since we select this command, the inventory items must have already arrived
    if("copy_gesture" == command_name)
    {
        uuid_vec_t ids;
        getSelectedIds(ids);
        // Make sure the clipboard is empty
        LLClipboard::instance().reset();
        for(uuid_vec_t::iterator it = ids.begin(); it != ids.end(); it++)
        {
            LLInventoryItem* item = gInventory.getItem(*it);
            if(item  && item->getInventoryType() == LLInventoryType::IT_GESTURE)
            {
                LLClipboard::instance().addToClipboard(*it);
            }
        }
    }
    else if ("paste" == command_name)
    {
        std::vector<LLUUID> ids;
        LLClipboard::instance().pasteFromClipboard(ids);
        if(ids.empty() || !gInventory.isCategoryComplete(mGestureFolderID))
            return;
        LLInventoryCategory* gesture_dir = gInventory.getCategory(mGestureFolderID);
        llassert(gesture_dir);
        LLPointer<GestureCopiedCallback> cb = new GestureCopiedCallback(this);

        for(std::vector<LLUUID>::iterator it = ids.begin(); it != ids.end(); it++)
        {
            LLInventoryItem* item = gInventory.getItem(*it);
            if(gesture_dir && item && item->getInventoryType() == LLInventoryType::IT_GESTURE)
            {
                LLStringUtil::format_map_t string_args;
                string_args["[COPY_NAME]"] = item->getName();
                LL_DEBUGS("Gesture")<< "Copying gesture " << item->getName() << "  "<< item->getUUID() << " into "
                                        << gesture_dir->getName() << "  "<< gesture_dir->getUUID() << LL_ENDL;
                copy_inventory_item(gAgent.getID(), item->getPermissions().getOwner(), item->getUUID(),
                        gesture_dir->getUUID(), getString("copy_name", string_args), cb);
            }
        }
        LLClipboard::instance().reset();
    }
    else if ("copy_uuid" == command_name)
    {
        LLClipboard::instance().copyToClipboard(mGestureList->getCurrentID(),LLAssetType::AT_GESTURE);
    }
}

void LLFloaterGesture::onClickEdit()
{
    const LLUUID& item_id = mGestureList->getCurrentID();

    LLInventoryItem* item = gInventory.getItem(item_id);
    if (!item) return;

    LLPreviewGesture* previewp = LLPreviewGesture::show(item_id, LLUUID::null);
    if (!previewp->getHost())
    {
        previewp->setRect(gFloaterView->findNeighboringPosition(this, previewp));
    }
}

void LLFloaterGesture::onCommitList()
{
    const LLUUID& item_id = mGestureList->getCurrentID();

    const bool has_selection = item_id.notNull();
    getChildView("play_btn")->setEnabled(has_selection);
    getChildView("stop_btn")->setEnabled(has_selection);
    getChildView("edit_btn")->setEnabled(has_selection);
    getChildView("del_btn")->setEnabled(has_selection);
    getChildView("activate_btn")->setEnabled(has_selection);
    if (LLGestureMgr::instance().isGesturePlaying(item_id))
    {
        getChildView("play_btn")->setVisible( false);
        getChildView("stop_btn")->setVisible( true);
    }
    else
    {
        getChildView("play_btn")->setVisible( true);
        getChildView("stop_btn")->setVisible( false);
    }
}

void LLFloaterGesture::onDeleteSelected()
{
    uuid_vec_t ids;
    getSelectedIds(ids);
    if(ids.empty())
        return;

    const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    LLGestureMgr* gm = LLGestureMgr::getInstance();
    for(uuid_vec_t::const_iterator it = ids.begin(); it != ids.end(); it++)
    {
        const LLUUID& selected_item = *it;
        LLInventoryItem* inv_item = gInventory.getItem(selected_item);
        if (inv_item && inv_item->getInventoryType() == LLInventoryType::IT_GESTURE)
        {
            if(gm->isGestureActive(selected_item))
            {
                gm->deactivateGesture(selected_item);
            }
            LLInventoryModel::update_list_t update;
            LLInventoryModel::LLCategoryUpdate old_folder(inv_item->getParentUUID(), -1);
            update.push_back(old_folder);
            LLInventoryModel::LLCategoryUpdate new_folder(trash_id, 1);
            update.push_back(new_folder);
            gInventory.accountForUpdate(update);

            LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(inv_item);
            new_item->setParent(trash_id);
            // no need to restamp it though it's a move into trash because
            // it's a brand new item already.
            new_item->updateParentOnServer(false);
            gInventory.updateItem(new_item);
        }
    }
    gInventory.notifyObservers();
    buildGestureList();
}

void LLFloaterGesture::addToCurrentOutFit()
{
    uuid_vec_t ids;
    getSelectedIds(ids);
    LLAppearanceMgr* am = LLAppearanceMgr::getInstance();
    LLPointer<LLInventoryCallback> cb = new LLUpdateAppearanceOnDestroy;
    for(uuid_vec_t::const_iterator it = ids.begin(); it != ids.end(); it++)
    {
        am->addCOFItemLink(*it, cb);
    }
}

void LLFloaterGesture::playGesture(LLUUID item_id)
{
    LL_DEBUGS("Gesture")<<"Playing gesture "<< item_id<<LL_ENDL;

    if (LLGestureMgr::instance().isGesturePlaying(item_id))
    {
        LLGestureMgr::instance().stopGesture(item_id);
    }
    else
    {
        LLGestureMgr::instance().playGesture(item_id);
    }
}
