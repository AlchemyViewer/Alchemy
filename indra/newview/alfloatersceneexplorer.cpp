/**
 * @file alfloatersceneexplorer.cpp
 * @brief Scene Explorer floater: a filterable scene-graph tree of region content
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "llviewerprecompiledheaders.h"

#include "alfloatersceneexplorer.h"

#include "message.h"

#include "llcallbacklist.h"
#include "lltimer.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfolderview.h"
#include "llfolderviewitem.h"
#include "lllayoutstack.h"
#include "llmenubutton.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lltoggleablemenu.h"
#include "lltrans.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include "lldate.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llmaterial.h"
#include "llpermissionsflags.h"
#include "lltextureentry.h"
#include "lltexturectrl.h"
#include "llviewerinventory.h"

#include "alavataractions.h"
#include "alderenderlist.h"
#include "alfloatersceneexplorerfilters.h"
#include "alobjectproperties.h"
#include "llagent.h"
#include "llavataractions.h"
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "llcachename.h"
#include "llfloaterreg.h"
#include "llfloatertools.h"
#include "llselectmgr.h"
#include "llslurl.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "lltracker.h"
#include "llviewercontrol.h"
#include "llviewerjointattachment.h"
#include "llviewermenu.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "rlvactions.h"
#include "rlvcommon.h"

namespace
{
    // Mirrors LLSelectMgr's per-packet object cap (llselectmgr.cpp).
    constexpr S32 MAX_OBJECTS_PER_PACKET = 254;

    // Send a batched ObjectSelect / ObjectDeselect message by local id, mirroring
    // the batching in LLSelectMgr (MAX_OBJECTS_PER_PACKET / isSendFullFast).
    void sendObjectSelectionMessage(const char* message_name, const std::vector<U32>& local_ids, const LLHost& host)
    {
        LLMessageSystem* msg = gMessageSystem;
        bool start_new_message = true;
        S32 count = 0;

        for (U32 local_id : local_ids)
        {
            if (start_new_message)
            {
                msg->newMessageFast(message_name);
                msg->nextBlockFast(_PREHASH_AgentData);
                msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
                msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
                start_new_message = false;
            }

            msg->nextBlockFast(_PREHASH_ObjectData);
            msg->addU32Fast(_PREHASH_ObjectLocalID, local_id);
            ++count;

            if (msg->isSendFullFast(nullptr) || count >= MAX_OBJECTS_PER_PACKET)
            {
                msg->sendReliable(host);
                start_new_message = true;
                count = 0;
            }
        }

        if (!start_new_message)
        {
            msg->sendReliable(host);
        }
    }

    // Deterministic synthetic UUID for an avatar's attachment-point grouping
    // folder. Name-based (MD5) so it is stable across reconciles and will not
    // collide with real object ids.
    LLUUID attachmentPointKey(const LLUUID& avatar_id, S32 point_index)
    {
        LLUUID key;
        key.generate(avatar_id.asString() + ":ap:" + std::to_string(point_index));
        return key;
    }

    // 1-based position of 'child' within 'parent's maintained child list, which
    // is the viewer's canonical link order (LLViewerObject::addChild appends in
    // arrival/link order). Returns 0 if not a direct child.
    S32 linkOrderInParent(LLViewerObject* parent, const LLViewerObject* child)
    {
        if (!parent || !child)
            return 0;
        S32 idx = 1;
        for (const auto& c : parent->getChildren())
        {
            if (c.get() == child)
                return idx;
            ++idx;
        }
        return 0;
    }

    // RLVa-aware avatar row label: anonymize when @shownames restricts this
    // agent (self is never restricted).
    std::string displayNameFor(const LLUUID& id, const LLAvatarName& av_name)
    {
        if (RlvActions::isRlvEnabled() && id != gAgentID
            && !RlvActions::canShowName(RlvActions::SNC_DEFAULT, id))
        {
            return RlvStrings::getAnonym(av_name);
        }
        return av_name.getCompleteName();
    }

    // Empty when the name cache hasn't resolved this avatar yet.
    std::string avatarDisplayName(const LLUUID& id)
    {
        LLAvatarName av_name;
        if (!LLAvatarNameCache::get(id, &av_name))
            return std::string();
        return displayNameFor(id, av_name);
    }

    // Agent / group app-SLURLs: the URL machinery renders these as resolved,
    // clickable names (profile/group inspect on click) and applies RLVa
    // @shownames anonymization for us.
    std::string agentSlurl(const LLUUID& id)
    {
        return "secondlife:///app/agent/" + id.asString() + "/inspect";
    }
    std::string groupSlurl(const LLUUID& id)
    {
        return "secondlife:///app/group/" + id.asString() + "/inspect";
    }

    // Compact modify/copy/transfer triad, e.g. "MC-".
    std::string permTriad(U32 mask)
    {
        std::string s;
        s += (mask & PERM_MODIFY)   ? 'M' : '-';
        s += (mask & PERM_COPY)     ? 'C' : '-';
        s += (mask & PERM_TRANSFER) ? 'T' : '-';
        return s;
    }

    // Mirrors the build floater's copy-texture / Copy-Asset-UUID gating
    // (llpanelface.cpp): a texture/material asset id may be shown when the
    // agent could legitimately obtain it — real god powers (no admin-menu
    // fakery), a library/default asset, or a full-perm item referencing the
    // asset in their inventory. The inventory search is expensive, so results
    // are memoized per session (positives are stable; a stale negative just
    // means reopening the floater after acquiring a full-perm copy).
    bool canRevealAssetId(const LLUUID& asset_id)
    {
        if (asset_id.isNull())
            return false;
        if (gAgent.isGodlikeWithoutAdminMenuFakery())
            return true;

        static boost::unordered_map<LLUUID, bool> s_reveal_cache;
        auto cached = s_reveal_cache.find(asset_id);
        if (cached != s_reveal_cache.end())
            return cached->second;

        bool reveal = get_is_predefined_texture(asset_id);
        if (!reveal)
        {
            LLViewerInventoryCategory::cat_array_t cats;
            LLViewerInventoryItem::item_array_t items;
            LLAssetIDMatches asset_id_matches(asset_id);
            gInventory.collectDescendentsIf(gInventory.getRootFolderID(), cats, items,
                                            LLInventoryModel::EXCLUDE_TRASH, asset_id_matches);
            for (const auto& item : items)
            {
                if (item && item->getIsFullPerm())
                {
                    reveal = true;
                    break;
                }
            }
            if (!reveal)
            {
                // Library assets are free for everyone.
                cats.clear();
                items.clear();
                gInventory.collectDescendentsIf(gInventory.getLibraryRootFolderID(), cats, items,
                                                LLInventoryModel::EXCLUDE_TRASH, asset_id_matches);
                reveal = !items.empty();
            }
        }
        s_reveal_cache[asset_id] = reveal;
        return reveal;
    }

    // Asset id for display, or a friendly note when the user lacks the
    // permissions to see it.
    std::string assetIdForDisplay(const LLUUID& asset_id)
    {
        return canRevealAssetId(asset_id) ? asset_id.asString()
                                          : std::string("(needs a full-perm copy to view)");
    }

    // Rough heat indicator for cost numbers: normal / caution / red.
    // (LabelTextColor, not TextFgColor — the latter is for editor backgrounds
    // and reads near-black on dark skins.)
    const LLColor4& heatColor(F32 value, F32 caution, F32 alert)
    {
        static const LLUIColor normal = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
        static const LLUIColor warn   = LLUIColorTable::instance().getColor("AlertCautionTextColor", LLColor4::yellow);
        static const LLUIColor danger = LLUIColorTable::instance().getColor("Red", LLColor4::red);
        if (value >= alert)
            return danger.get();
        if (value >= caution)
            return warn.get();
        return normal.get();
    }

    // De-emphasis that is still readable on dark skins (LabelDisabledColor is
    // grey-on-grey there).
    const LLColor4& mutedColor()
    {
        static const LLUIColor muted = LLUIColorTable::instance().getColor("LtGray", LLColor4::grey);
        return muted.get();
    }

    const LLColor4& labelColor()
    {
        static const LLUIColor label = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
        return label.get();
    }

    const LLColor4& cautionColor()
    {
        static const LLUIColor caution = LLUIColorTable::instance().getColor("AlertCautionTextColor", LLColor4::yellow);
        return caution.get();
    }

    std::string placeholderName(ALSceneExplorerItem::EItemType type, const LLUUID& id)
    {
        switch (type)
        {
        case ALSceneExplorerItem::TYPE_AVATAR:
        {
            std::string name = avatarDisplayName(id);
            if (!name.empty())
                return name;
            return std::string("(loading avatar)");
        }
        case ALSceneExplorerItem::TYPE_ATTACHMENT:
            return std::string("(attachment)");
        default:
            return std::string("(object)");
        }
    }

    // ------------------------------------------------------------------
    // Context / gear menu helpers
    // ------------------------------------------------------------------

    // Evaluate a globally registered enable predicate (the same ones the pie
    // and main menus use) against the current selection, so the explorer's
    // permission gating can never diverge from the rest of the viewer.
    bool registryEnabled(const std::string& name)
    {
        const LLUICtrl::enable_callback_t* cb = LLUICtrl::EnableCallbackRegistry::getValue(name);
        return cb && (*cb)(nullptr, LLSD());
    }

    // Restore the all-visible/enabled baseline LLFolderView::updateMenuOptions
    // establishes before buildContextMenu, so the gear button path starts from
    // the same state as the right-click popup.
    void resetMenuEntries(LLMenuGL& menu)
    {
        for (LLView* menu_item : *menu.getChildList())
        {
            if (LLMenuItemBranchGL* branch = dynamic_cast<LLMenuItemBranchGL*>(menu_item))
            {
                if (branch->getBranch())
                    resetMenuEntries(*branch->getBranch());
            }
            menu_item->setVisible(false);
            menu_item->pushVisible(true);
            menu_item->setEnabled(true);
        }
    }

    // Show only the listed entries (the llinventorybridge hide_context_entries
    // pattern, local so the explorer doesn't drag the inventory bridge in):
    // recurses into submenus, drops leading/doubled separators, disables the
    // entries named in @disabled.
    void hideMenuEntries(LLMenuGL& menu,
                         const std::vector<std::string>& show,
                         const std::vector<std::string>& disabled)
    {
        bool prev_was_separator = true;
        for (LLView* menu_item : *menu.getChildList())
        {
            if (LLMenuItemBranchGL* branch = dynamic_cast<LLMenuItemBranchGL*>(menu_item))
            {
                if (branch->getBranch())
                    hideMenuEntries(*branch->getBranch(), show, disabled);
            }

            const std::string& name = menu_item->getName();
            bool found = std::find(show.begin(), show.end(), name) != show.end();
            if (found)
            {
                const bool is_separator = dynamic_cast<LLMenuItemSeparatorGL*>(menu_item) != nullptr;
                found = !(is_separator && prev_was_separator);
                prev_was_separator = is_separator;
            }

            if (!found)
            {
                // Multi-selection passes call this repeatedly; don't re-hide
                // an entry an earlier selected item explicitly showed.
                if (!menu_item->getLastVisible())
                    menu_item->setVisible(false);
                menu_item->setEnabled(false);
            }
            else
            {
                menu_item->setVisible(true);
                menu_item->pushVisible(true);
                menu_item->setEnabled(
                    std::find(disabled.begin(), disabled.end(), name) == disabled.end());
            }
        }
    }
}

// ============================================================================
ALFloaterSceneExplorer::ALFloaterSceneExplorer(const LLSD& key)
:   LLFloater(key)
{
    // Registered in the constructor (not postBuild) so the gear / view menu
    // buttons in the floater XML can resolve these while their menus build.
    // The same names serve the folder view's right-click popup. Entries the
    // superset menu reuses from the global registries (Object.Touch,
    // Object.Return, PayObject, Tools.TakeCopy, ...) resolve through the
    // default registrar and aren't repeated here.
    mCommitCallbackRegistrar.add("SceneExplorer.Focus",        boost::bind(&ALFloaterSceneExplorer::doFocus, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Edit",         boost::bind(&ALFloaterSceneExplorer::doEdit, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Inspect",      boost::bind(&ALFloaterSceneExplorer::doInspect, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Teleport",     boost::bind(&ALFloaterSceneExplorer::doTeleport, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Sit",          boost::bind(&ALFloaterSceneExplorer::doSit, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Copy",         boost::bind(&ALFloaterSceneExplorer::doCopy, this, _2));
    mCommitCallbackRegistrar.add("SceneExplorer.CopyResults",  boost::bind(&ALFloaterSceneExplorer::doCopyResults, this));
    mCommitCallbackRegistrar.add("SceneExplorer.ShowOnMap",    boost::bind(&ALFloaterSceneExplorer::doShowOnMap, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Beacon",       boost::bind(&ALFloaterSceneExplorer::doBeacon, this));
    mCommitCallbackRegistrar.add("SceneExplorer.BlockOwner",   boost::bind(&ALFloaterSceneExplorer::doBlockOwner, this));
    mCommitCallbackRegistrar.add("SceneExplorer.AvatarAction", boost::bind(&ALFloaterSceneExplorer::doAvatarAction, this, _2));
    mCommitCallbackRegistrar.add("SceneExplorer.FilterByOwner",boost::bind(&ALFloaterSceneExplorer::doFilterByOwner, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Derender",     boost::bind(&ALFloaterSceneExplorer::doDerender, this, _2));
    mCommitCallbackRegistrar.add("SceneExplorer.Restore",      boost::bind(&ALFloaterSceneExplorer::doRestore, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Refresh",      boost::bind(&ALFloaterSceneExplorer::doRefresh, this));
    mCommitCallbackRegistrar.add("SceneExplorer.SetSort",      boost::bind(&ALFloaterSceneExplorer::setSortMode, this, _2));
    mCommitCallbackRegistrar.add("SceneExplorer.ToggleShow",   boost::bind(&ALFloaterSceneExplorer::toggleShow, this, _2));
    mCommitCallbackRegistrar.add("SceneExplorer.ResetFilters", boost::bind(&ALFloaterSceneExplorer::doResetFilters, this));
    mCommitCallbackRegistrar.add("SceneExplorer.SelectAllResults", boost::bind(&ALFloaterSceneExplorer::doSelectAllResults, this));
    mCommitCallbackRegistrar.add("SceneExplorer.ShowFilters",  boost::bind(&ALFloaterSceneExplorer::doShowFilters, this));
    mEnableCallbackRegistrar.add("SceneExplorer.CheckSort",    boost::bind(&ALFloaterSceneExplorer::checkSortMode, this, _2));
    mEnableCallbackRegistrar.add("SceneExplorer.CheckShow",    boost::bind(&ALFloaterSceneExplorer::checkShow, this, _2));
}

ALFloaterSceneExplorer::~ALFloaterSceneExplorer()
{
    gIdleCallbacks.deleteFunction(onIdle, this);
    if (mPropsConn.connected())
        mPropsConn.disconnect();
    if (mDerenderConn.connected())
        mDerenderConn.disconnect();
    if (mWorldSelConn.connected())
        mWorldSelConn.disconnect();
}

bool ALFloaterSceneExplorer::postBuild()
{
    mTreePanel = getChild<LLPanel>("scene_tree");
    buildTree();

    // Shown by the folder view's status text when a filter matches nothing
    // (without it, a zero-hit filter renders a blank pane).
    mViewModel.getFilter().setEmptyLookupMessage(getString("no_matches"));

    mShowAvatars = gSavedSettings.getBOOL("ALSceneExplorerShowAvatars");
    mShowDerendered = gSavedSettings.getBOOL("ALSceneExplorerShowDerendered");
    // The 360 interest-list mode itself is applied in onOpen / released in
    // onClose, so the simulator only streams the full region while the
    // explorer is actually up.
    mFullRegion = gSavedSettings.getBOOL("ALSceneExplorerFullRegion");
    mSelectionSync = gSavedSettings.getBOOL("ALSceneExplorerSelectionSync");

    // Push persisted filter state into the controls before wiring the commit
    // callbacks, so the UI, the saved settings, and the filter object all
    // agree from the first frame. (Sort mode and the avatar/derendered
    // toggles live in the view menu now and read their members directly.)
    const U32 flag_mask = gSavedSettings.getU32("ALSceneExplorerFlagFilter");
    getChild<LLUICtrl>("flag_scripted")->setValue((flag_mask & ALObjectProperties::FLAG_SCRIPTED) != 0);
    getChild<LLUICtrl>("flag_light")->setValue((flag_mask & ALObjectProperties::FLAG_LIGHT) != 0);
    getChild<LLUICtrl>("flag_particles")->setValue((flag_mask & ALObjectProperties::FLAG_PARTICLES) != 0);
    getChild<LLUICtrl>("limit_radius_check")->setValue(
        gSavedSettings.getU32("ALSceneExplorerScope") == (U32)ALSceneExplorerFilter::SCOPE_RADIUS);
    getChild<LLUICtrl>("radius_slider")->setValue(gSavedSettings.getF32("ALSceneExplorerRadius"));
    // The "Selected owner" mode is session-only (its target id isn't
    // persisted), so never restore into it.
    S32 owner_idx = (S32)gSavedSettings.getU32("ALSceneExplorerOwnerFilter");
    if (owner_idx >= (S32)ALSceneExplorerFilter::OWNER_SPECIFIC)
        owner_idx = 0;
    getChild<LLComboBox>("owner_combo")->setCurrentByIndex(owner_idx);
    getChild<LLComboBox>("search_type_combo")->setCurrentByIndex(
        (S32)gSavedSettings.getU32("ALSceneExplorerSearchType"));

    getChild<LLFilterEditor>("filter_input")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("search_type_combo")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("owner_combo")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("limit_radius_check")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("radius_slider")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_scripted")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_light")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_particles")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));

    getChild<LLButton>("focus_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doFocus, this));
    getChild<LLButton>("edit_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doEdit, this));
    getChild<LLButton>("inspect_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doInspect, this));
    getChild<LLButton>("beacon_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doBeacon, this));
    getChild<LLButton>("teleport_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doTeleport, this));
    // The gear menu is the same superset menu the tree's right-click shows;
    // refresh its per-row state just before it opens (mouse-down fires ahead
    // of LLMenuButton::toggleMenu).
    getChild<LLMenuButton>("gear_btn")->setMouseDownCallback(boost::bind(&ALFloaterSceneExplorer::onGearMouseDown, this));

    // Detail pane, received-items style: one layout panel hosts the expander
    // bar + content, and collapses down to just the bar (min_dim) — the bar
    // stays visible and the panel-spacing gap above it is the drag area.
    mDetailHost = getChild<LLLayoutPanel>("detail_host_layout");
    mDetailsExpanded = gSavedSettings.getBOOL("ALSceneExplorerShowDetails");
    getChild<LLButton>("details_btn")->setToggleState(mDetailsExpanded);
    getChild<LLButton>("details_btn")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onToggleDetails, this));
    getChild<LLLayoutStack>("main_stack")->collapsePanel(mDetailHost, !mDetailsExpanded);

    // The perms / flag checkboxes are display-only: any click is reverted by
    // refilling from the model (keeps them full-brightness, unlike disabling).
    static const char* const READONLY_CHECKS[] = {
        "check_modify", "check_copy", "check_transfer",
        "check_scripted", "check_light", "check_physics", "check_phantom", "check_temp"
    };
    for (const char* check_name : READONLY_CHECKS)
    {
        getChild<LLUICtrl>(check_name)->setCommitCallback(
            boost::bind(&ALFloaterSceneExplorer::refreshDetail, this));
    }

    mPropsConn = ALObjectPropertiesCache::instance().setChangeCallback(
        boost::bind(&ALFloaterSceneExplorer::onPropsCacheChanged, this, _1));
    // Track derender changes from anywhere (explorer, build menu, Blocked
    // floater) so rows move between the live tree and the Derendered category.
    mDerenderConn = ALDerenderList::setChangeCallback(
        boost::bind(&ALFloaterSceneExplorer::onDerenderListChanged, this));
    // In-world selection -> tree highlight. The signal can fire many times a
    // frame during edits, so the handler only flags; idleUpdate processes.
    mWorldSelConn = LLSelectMgr::getInstance()->mUpdateSignal.connect(
        boost::bind(&ALFloaterSceneExplorer::onWorldSelectionChanged, this));

    // Restore persisted sort order, and seed the filter object from the
    // controls restored above.
    mViewModel.getSorter().setMode((ALSceneExplorerSort::ESortMode)gSavedSettings.getU32("ALSceneExplorerSortOrder"));
    onFilterChanged();

    // Sentinel so the first idle pass (no selection) disables the action
    // buttons rather than leaving them at their XML-default enabled state.
    mLastButtonStateID.generate();

    // Drive discovery / fetch / filtering / layout from the viewer idle loop, the
    // way LLInventoryPanel does, rather than from draw(). This keeps tree mutation
    // out of the render pass and lets the work time-slice across frames; idleUpdate
    // itself no-ops while the floater isn't visible.
    gIdleCallbacks.addFunction(onIdle, this);

    return true;
}

void ALFloaterSceneExplorer::onOpen(const LLSD& key)
{
    mReconcileTimer.reset();
    if (mFullRegion)
        applyFullRegionMode(true);
    reconcile();
    // Inventory muscle memory: typing starts filtering right away.
    getChild<LLFilterEditor>("filter_input")->setFocus(true);
}

void ALFloaterSceneExplorer::onClose(bool app_quitting)
{
    // Drop a tracker beacon the explorer set; never clobber one the user set
    // through the map or a landmark.
    if (mBeaconTrackedID.notNull())
    {
        LLTracker::stopTracking(false);
        mBeaconTrackedID.setNull();
    }
    // Release the full-region stream while the explorer is closed (the
    // preference itself persists; reopening re-applies it). At quit the
    // simulator cleans up the interest list with the circuit.
    if (!app_quitting && mFullRegion)
        applyFullRegionMode(false);
}

void ALFloaterSceneExplorer::draw()
{
    // All discovery / fetch / filter / layout now happens in idleUpdate() (driven
    // by gIdleCallbacks); draw() just renders the current tree state.
    LLFloater::draw();
}

bool ALFloaterSceneExplorer::handleKeyHere(KEY key, MASK mask)
{
    if (key == 'F' && mask == MASK_CONTROL)
    {
        getChild<LLFilterEditor>("filter_input")->setFocus(true);
        return true;
    }
    // Enter activates the selected row the way double-click does. The folder
    // view itself leaves RETURN unhandled (it only consumes it mid-rename),
    // and routing it through openItem() would be wrong — folder expansion
    // calls openItem() too, which is why it stays inert for folder types.
    if (key == KEY_RETURN && mask == MASK_NONE && mTree && mTree->hasFocus())
    {
        ALSceneExplorerItem* item = getSelectedItem();
        if (item && !item->isContainer() && !item->isDerenderedType())
        {
            item->activate();
            return true;
        }
    }
    return LLFloater::handleKeyHere(key, mask);
}

// static
void ALFloaterSceneExplorer::onIdle(void* user_data)
{
    static_cast<ALFloaterSceneExplorer*>(user_data)->idleUpdate();
}

void ALFloaterSceneExplorer::idleUpdate()
{
    // No work while hidden, minimised, or closed (single-instance floaters stay
    // alive when closed). An occluded-but-open floater still counts as visible,
    // which is what we want.
    if (!mTree || !isInVisibleChain())
        return;

    if (mReconcileTimer.getElapsedTimeF32() > 1.5f)
    {
        mReconcileTimer.reset();
        reconcile();
    }

    // Build newly-discovered nodes a few milliseconds at a time so a dense region
    // (up to ~60k objects) streams in instead of stalling the frame.
    if (!mBuildQueue.empty())
        drainBuildQueue(0.006);

    // Explicit-Refresh local re-fills, same time-sliced treatment (the full
    // fillFromObject walks every face).
    if (!mRefillQueue.empty())
        drainRefillQueue(0.003);

    if (mRetryTimer.getElapsedTimeF32() > 8.f)
    {
        mRetryTimer.reset();
        retryUnresolved();
    }
    // Adaptive drain cadence: snappy while the backlog is small (a normal
    // region's queue clears in seconds), halved when it is huge so the densest
    // regions don't see peak probe load for minutes on end.
    const F32 drain_interval = (mFetchQueue.size() > 2048) ? 1.f : 0.5f;
    if (mFetchTimer.getElapsedTimeF32() > drain_interval)
    {
        mFetchTimer.reset();
        drainPropsQueue();
    }

    // The folder view's own idle routine: runs (time-sliced) filtering, finalises
    // it, then arranges/lays out the tree in one consistent pass.
    mTree->update();

    // On the reconcile cadence, after the arrange so widget rects are fresh:
    // refresh on-screen suffixes, demand costs for visible rows, and bump
    // their pending props requests to the front of the queue.
    if (mScanVisible)
    {
        mScanVisible = false;
        scanVisibleRows();
    }
    updateStatusText();

    syncSelectionToWorld();
    if (mWorldSelectionDirty)
    {
        mWorldSelectionDirty = false;
        syncSelectionFromWorld();
    }
    updateActionButtons();

    // Detail pane follows the selection; also rebuilt when props arrive or a
    // reconcile pass refreshes live metrics (mDetailDirty). Skipped while the
    // host is collapsed to just the expander bar.
    if (mDetailHost && mDetailsExpanded)
    {
        ALSceneExplorerItem* detail_item = getSelectedItem();
        const LLUUID detail_id = detail_item ? detail_item->getUUID() : LLUUID::null;
        if (detail_id != mLastDetailID || mDetailDirty)
        {
            mLastDetailID = detail_id;
            mDetailDirty = false;
            refreshDetail();
        }
    }
}

void ALFloaterSceneExplorer::drainBuildQueue(F64 max_time)
{
    if (mBuildQueue.empty())
        return;
    LLViewerRegion* region = gAgent.getRegion();
    if (!region)
        return;

    // Always make progress on at least one node, then keep going until the time
    // budget for this frame is spent.
    const F64 end_time = LLTimer::getTotalSeconds() + max_time;
    do
    {
        const LLUUID id = mBuildQueue.front();
        mBuildQueue.pop_front();
        mQueued.erase(id);

        LLViewerObject* obj = gObjectList.findObject(id);
        if (obj && !obj->isDead() && obj->getRegion() == region)
            getOrCreateNode(obj); // creates the widget (and any missing ancestors)
    }
    while (!mBuildQueue.empty() && LLTimer::getTotalSeconds() < end_time);
}

void ALFloaterSceneExplorer::drainRefillQueue(F64 max_time)
{
    if (mRefillQueue.empty())
        return;

    const F64 end_time = LLTimer::getTotalSeconds() + max_time;
    do
    {
        const LLUUID id = mRefillQueue.front();
        mRefillQueue.pop_front();

        auto it = mItems.find(id);
        if (it == mItems.end())
            continue;
        ALSceneExplorerItem* item = it->second;
        LLViewerObject* obj = gObjectList.findObject(id);
        if (!obj || obj->isDead())
            continue;

        // Re-fill the locally-derived fields — per-face flags (glow,
        // fullbright, alpha, PBR), light/media, geometry, prim and triangle
        // counts — which are captured at node build and go stale as objects
        // are edited. Costs are peeked (never trigger) and the async server
        // fields are left untouched by fillFromObject().
        ALObjectProperties::Record rec = item->getRecord();
        ALObjectProperties::fillFromObject(rec, obj, /*fetch_costs=*/false);
        item->updateRecord(rec); // dirties this row's filter state
    }
    while (!mRefillQueue.empty() && LLTimer::getTotalSeconds() < end_time);
}

// ============================================================================
// Tree construction
// ============================================================================
void ALFloaterSceneExplorer::buildTree()
{
    LLViewerRegion* region = gAgent.getRegion();
    const std::string region_name = region ? region->getName() : LLStringUtil::null;

    mRootItem = new ALSceneExplorerItem(ALSceneExplorerItem::TYPE_REGION, LLUUID::null,
                                        "Region: " + region_name, mViewModel, this);

    LLFolderView::Params p(LLUICtrlFactory::getDefaultParams<LLFolderView>());
    p.name = "scene_explorer_tree";
    p.title = mRootItem->getName();
    p.rect = LLRect(0, 0, mTreePanel->getRect().getWidth(), 0);
    p.parent_panel = mTreePanel;
    p.tool_tip = std::string("scene_explorer_tree");
    p.listener = mRootItem;
    p.view_model = &mViewModel;
    p.root = nullptr;
    p.use_ellipses = true;
    p.options_menu = "menu_scene_explorer.xml";
    mTree = LLUICtrlFactory::create<LLFolderView>(p);
    mTree->setCallbackRegistrar(&mCommitCallbackRegistrar);
    mTree->setEnableRegistrar(&mEnableCallbackRegistrar);

    LLRect scroller_rect = mTreePanel->getRect();
    scroller_rect.translate(-scroller_rect.mLeft, -scroller_rect.mBottom);
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLFolderViewScrollContainer>());
    sp.rect(scroller_rect);
    LLScrollContainer* scroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(sp);
    scroller->setFollowsAll();
    mTreePanel->addChild(scroller);
    scroller->addChild(mTree);
    mTree->setScrollContainer(scroller);
    mTree->setFollowsAll();
    mTree->addChild(mTree->mStatusTextBox);

    mViewModel.setFolderView(mTree);

    mObjectsCategory = new ALSceneExplorerItem(ALSceneExplorerItem::TYPE_CATEGORY_OBJECTS,
                                               LLUUID::generateNewID(), "Objects", mViewModel, this);
    mRootItem->addChild(mObjectsCategory);
    mObjectsWidget = createWidget(mObjectsCategory, true, mTree);

    mAvatarsCategory = new ALSceneExplorerItem(ALSceneExplorerItem::TYPE_CATEGORY_AVATARS,
                                               LLUUID::generateNewID(), "Avatars", mViewModel, this);
    mRootItem->addChild(mAvatarsCategory);
    mAvatarsWidget = createWidget(mAvatarsCategory, true, mTree);

    mTree->setOpen(true);
    static_cast<LLFolderViewFolder*>(mObjectsWidget)->setOpen(true);
}

LLFolderViewItem* ALFloaterSceneExplorer::createWidget(ALSceneExplorerItem* item, bool is_folder, LLFolderViewItem* parent_widget)
{
    LLFolderViewItem::Params params(LLUICtrlFactory::getDefaultParams<LLFolderViewItem>());
    params.name = item->getName();
    params.root = mTree;
    params.listener = item;
    params.tool_tip = item->getName();
    params.font_color = LLUIColorTable::instance().getColor("MenuItemEnabledColor", LLColor4::white);
    params.font_highlight_color = LLUIColorTable::instance().getColor("MenuItemHighlightColor", LLColor4::white);

    LLFolderViewItem* widget;
    if (is_folder)
    {
        // Folders are our custom subclass (double-click acts on the object;
        // disclosure arrow expands). We populate children eagerly (unlike
        // inventory's lazy fetch), so mark them inited immediately:
        // LLFolderViewFolder::arrange() gates both sort(this) and the
        // folder-complete computation on mAreChildrenInited; left false, our
        // folders would never sort their children and would forever report
        // "incomplete" (perpetual disclosure arrow).
        ALSceneExplorerFolder* folder = LLUICtrlFactory::create<ALSceneExplorerFolder>(params);
        folder->setChildrenInited(true);
        widget = folder;
    }
    else
    {
        widget = LLUICtrlFactory::create<ALSceneExplorerListItem>(params);
    }

    if (parent_widget)
        widget->addToFolder(static_cast<LLFolderViewFolder*>(parent_widget));

    return widget;
}

ALSceneExplorerItem* ALFloaterSceneExplorer::getOrCreateNode(LLViewerObject* obj)
{
    if (!obj)
        return nullptr;

    const LLUUID& id = obj->getID();
    auto found = mItems.find(id);
    if (found != mItems.end())
        return found->second;

    ALSceneExplorerItem::EItemType type;
    ALSceneExplorerItem* parent_item = nullptr;
    LLFolderViewItem* parent_widget = nullptr;
    bool is_folder = false;
    S32 link_order = 0;

    if (obj->asAvatar())
    {
        type = ALSceneExplorerItem::TYPE_AVATAR;
        parent_item = mAvatarsCategory;
        parent_widget = mAvatarsWidget;
        is_folder = true;
    }
    else if (obj->isRootEdit())
    {
        // Root of a linkset: an attachment root nests under its avatar's
        // attachment-point folder; everything else is a world linkset.
        if (obj->isAttachment())
        {
            // getAvatarAncestor() walks the parent chain to the real wearer.
            // (getAvatar() would return an animesh object's own control avatar,
            // which has no attachment point for it.)
            ALSceneExplorerItem* point_node = getOrCreatePointFolder(obj->getAvatarAncestor(), obj);
            if (!point_node)
                return nullptr;
            type = ALSceneExplorerItem::TYPE_ATTACHMENT;
            parent_item = point_node;
            auto wit = mWidgets.find(point_node->getUUID());
            parent_widget = (wit != mWidgets.end()) ? wit->second : nullptr;
        }
        else
        {
            type = ALSceneExplorerItem::TYPE_LINKSET;
            parent_item = mObjectsCategory;
            parent_widget = mObjectsWidget;
        }
        // Every root is a folder so single- and multi-prim objects sort together
        // in one list; a childless folder simply draws no disclosure arrow.
        is_folder = true;
    }
    else
    {
        // Child prim of a linkset (world or attachment).
        LLViewerObject* root = obj->getRootEdit();
        ALSceneExplorerItem* root_node = (root && root != obj) ? getOrCreateNode(root) : nullptr;
        if (!root_node)
            return nullptr;
        type = ALSceneExplorerItem::TYPE_PRIM;
        parent_item = root_node;
        auto wit = mWidgets.find(root->getID());
        parent_widget = (wit != mWidgets.end()) ? wit->second : nullptr;
        link_order = linkOrderInParent(root, obj);
    }

    if (!parent_item || !parent_widget)
        return nullptr;

    ALSceneExplorerItem* item = new ALSceneExplorerItem(type, id, placeholderName(type, id), mViewModel, this);
    item->setLinkOrder(link_order);
    // Costs stay demand-driven (visible rows / LI sort / detail pane): a bulk
    // build triggering a GetObjectCost for every object would ask the server
    // to cost the whole region the moment it streams in under 360 mode.
    ALObjectProperties::Record rec = ALObjectProperties::fromObject(obj, /*fetch_costs=*/false);
    if (LLVOAvatar* av = obj->asAvatar())
    {
        // Avatar rows repurpose these fields: complexity + worn attachments.
        rec.mRenderCost = (F32)av->getVisualComplexity();
        rec.mPrimCount  = av->getAttachmentCount();
    }
    item->updateRecord(rec);

    parent_item->addChild(item);
    LLFolderViewItem* widget = createWidget(item, is_folder, parent_widget);

    mItems[id] = item;
    mWidgets[id] = widget;

    // Pick up already-cached server props, otherwise request them. Avatar
    // display names resolve through the name cache instead; subscribe once and
    // refresh when the name lands (handle-guarded in case we close first).
    applyServerProps(item);
    if (type != ALSceneExplorerItem::TYPE_AVATAR)
    {
        queueProps(id);
    }
    else
    {
        LLAvatarName av_name;
        if (!LLAvatarNameCache::get(id, &av_name))
        {
            LLHandle<ALFloaterSceneExplorer> handle = getDerivedHandle<ALFloaterSceneExplorer>();
            LLAvatarNameCache::get(id,
                [handle](const LLUUID& av_id, const LLAvatarName& name)
                {
                    if (ALFloaterSceneExplorer* self = handle.get())
                        self->onAvatarNameLoaded(av_id, name);
                });
        }
    }

    widget->refresh(); // populate label/suffix/icon from the model
    return item;
}

ALSceneExplorerItem* ALFloaterSceneExplorer::getOrCreatePointFolder(LLViewerObject* avatar_obj, LLViewerObject* attachment)
{
    LLVOAvatar* avatar = avatar_obj ? avatar_obj->asAvatar() : nullptr;
    if (!avatar || !attachment)
        return nullptr;

    // Resolve the hosting attachment point from the object's attachment state
    // (getTargetAttachmentPoint also handles the ATTACHMENT_ADD mask), then
    // recover its index for the synthetic folder key.
    LLViewerJointAttachment* point = avatar->getTargetAttachmentPoint(attachment);
    S32 point_index = 0;
    if (point)
    {
        for (const auto& [idx, jp] : avatar->mAttachmentPoints)
        {
            if (jp == point)
            {
                point_index = idx;
                break;
            }
        }
    }
    if (!point || !point_index)
        return nullptr;

    // The point folder hangs off the avatar node; ensure that exists first.
    ALSceneExplorerItem* av_node = getOrCreateNode(avatar_obj);
    if (!av_node)
        return nullptr;
    auto avwit = mWidgets.find(avatar_obj->getID());
    LLFolderViewItem* av_widget = (avwit != mWidgets.end()) ? avwit->second : nullptr;
    if (!av_widget)
        return nullptr;

    const LLUUID key = attachmentPointKey(avatar_obj->getID(), point_index);
    auto found = mItems.find(key);
    if (found != mItems.end())
        return found->second;

    // Localize the joint name the same way the attach menus do ("R Forearm",
    // "Skull", ... are keys in strings.xml).
    std::string point_name = point->getName();
    point_name = point_name.empty() ? std::string("Attachment point")
                                    : LLTrans::getString(point_name);

    ALSceneExplorerItem* point_node = new ALSceneExplorerItem(
        ALSceneExplorerItem::TYPE_ATTACHMENT_POINT, key, point_name, mViewModel, this);

    av_node->addChild(point_node);
    LLFolderViewItem* point_widget = createWidget(point_node, true, av_widget);

    mItems[key] = point_node;
    mWidgets[key] = point_widget;

    point_widget->refresh();
    return point_node;
}

void ALFloaterSceneExplorer::eraseSubtreeMaps(ALSceneExplorerItem* item)
{
    for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
    {
        eraseSubtreeMaps(static_cast<ALSceneExplorerItem*>(it->get()));
    }
    const LLUUID& id = item->getUUID();
    mWidgets.erase(id);
    mItems.erase(id);
}

void ALFloaterSceneExplorer::removeNode(ALSceneExplorerItem* item)
{
    if (!item)
        return;

    const LLUUID id = item->getUUID();
    auto wit = mWidgets.find(id);
    LLFolderViewItem* widget = (wit != mWidgets.end()) ? wit->second : nullptr;

    eraseSubtreeMaps(item);

    // getParent() is public on the LLFolderViewModelItem interface but protected
    // in the Common base, so reach it through a base-class pointer.
    LLFolderViewModelItem* base_item = item;
    if (LLFolderViewModelItem* parent = const_cast<LLFolderViewModelItem*>(base_item->getParent()))
        parent->removeChild(item);

    if (widget)
        widget->destroyView();
}

void ALFloaterSceneExplorer::clearTree()
{
    // Wholesale teardown (used on region change): drop every object/avatar
    // subtree but keep the structural root and category nodes. Collect ids
    // first — removing a root frees its descendants' model items, so holding
    // raw pointers across removals would dangle.
    std::vector<LLUUID> roots;
    auto collect_children = [&roots](ALSceneExplorerItem* category)
    {
        if (!category)
            return;
        for (auto it = category->getChildrenBegin(); it != category->getChildrenEnd(); ++it)
        {
            roots.push_back(static_cast<ALSceneExplorerItem*>(it->get())->getUUID());
        }
    };
    collect_children(mObjectsCategory);
    collect_children(mAvatarsCategory);
    for (const LLUUID& id : roots)
    {
        auto it = mItems.find(id);
        if (it != mItems.end())
            removeNode(it->second);
    }

    mFetchQueue.clear();
    mQueuedProps.clear();
    mBuildQueue.clear();
    mQueued.clear();
    mRefillQueue.clear();
}

// ============================================================================
// Derendered category
// ============================================================================
void ALFloaterSceneExplorer::ensureDerenderedCategory()
{
    if (mDerenderedCategory)
        return;
    mDerenderedCategory = new ALSceneExplorerItem(ALSceneExplorerItem::TYPE_CATEGORY_DERENDERED,
                                                  LLUUID::generateNewID(), "Derendered", mViewModel, this);
    mRootItem->addChild(mDerenderedCategory);
    mDerenderedWidget = createWidget(mDerenderedCategory, true, mTree);
    mDerenderedWidget->refresh();
}

void ALFloaterSceneExplorer::destroyDerenderedCategory()
{
    if (!mDerenderedCategory)
        return;

    // Remove the entry rows first (they live in mItems/mWidgets), then the
    // category node + widget themselves (member-held, not in the maps).
    std::vector<LLUUID> ids;
    for (auto it = mDerenderedCategory->getChildrenBegin(); it != mDerenderedCategory->getChildrenEnd(); ++it)
    {
        ids.push_back(static_cast<ALSceneExplorerItem*>(it->get())->getUUID());
    }
    for (const LLUUID& id : ids)
    {
        auto it = mItems.find(id);
        if (it != mItems.end())
            removeNode(it->second);
    }

    LLFolderViewModelItem* base_item = mDerenderedCategory;
    if (LLFolderViewModelItem* parent = const_cast<LLFolderViewModelItem*>(base_item->getParent()))
        parent->removeChild(mDerenderedCategory);
    if (mDerenderedWidget)
        mDerenderedWidget->destroyView();
    mDerenderedCategory = nullptr;
    mDerenderedWidget = nullptr;
}

void ALFloaterSceneExplorer::onDerenderListChanged()
{
    syncDerendered();
    updateCategoryCounts();
}

void ALFloaterSceneExplorer::syncDerendered()
{
    if (!mTree || !mRootItem)
        return;

    LLViewerRegion* region = gAgent.getRegion();
    const U64 region_handle = region ? region->getHandle() : 0;

    // Desired set: derendered objects scoped to the current region (their
    // local-id bookkeeping is region-specific); derendered avatars are
    // region-less and always listed.
    std::vector<const ALDerenderEntry*> wanted;
    if (mShowDerendered)
    {
        for (const auto& entry : ALDerenderList::instance().getEntries())
        {
            if (!entry || !entry->isValid())
                continue;
            if (entry->getType() == ALDerenderEntry::TYPE_OBJECT)
            {
                if (static_cast<const ALDerenderObject*>(entry.get())->idRegion != region_handle)
                    continue;
            }
            else if (entry->getType() != ALDerenderEntry::TYPE_AVATAR)
            {
                continue;
            }
            wanted.push_back(entry.get());
        }
    }

    if (wanted.empty())
    {
        destroyDerenderedCategory();
        return;
    }
    ensureDerenderedCategory();

    boost::unordered_set<LLUUID> present;
    for (const ALDerenderEntry* entry : wanted)
    {
        const LLUUID& id = entry->getID();
        const bool is_avatar_entry = (entry->getType() == ALDerenderEntry::TYPE_AVATAR);

        // Same anonymization rule as live avatar rows.
        std::string name = entry->getName();
        if (is_avatar_entry && RlvActions::isRlvEnabled() && id != gAgentID
            && !RlvActions::canShowName(RlvActions::SNC_DEFAULT, id))
        {
            name = RlvStrings::getAnonym(name);
        }

        auto it = mItems.find(id);
        ALSceneExplorerItem* node = (it != mItems.end()) ? it->second : nullptr;
        if (node && !node->isDerenderedType())
        {
            // A live node still holds this id (the kill is in flight); the next
            // sync pass picks the entry up once reconcile has swept it.
            continue;
        }
        present.insert(id);

        if (!node)
        {
            node = new ALSceneExplorerItem(
                is_avatar_entry ? ALSceneExplorerItem::TYPE_DERENDERED_AVATAR
                                : ALSceneExplorerItem::TYPE_DERENDERED_OBJECT,
                id, name, mViewModel, this);
            if (is_avatar_entry)
                node->getRecordRef().mGeom = ALObjectProperties::GEOM_AVATAR;
            mDerenderedCategory->addChild(node);
            LLFolderViewItem* widget = createWidget(node, false, mDerenderedWidget);
            mItems[id] = node;
            mWidgets[id] = widget;
            widget->refresh();
        }
        else if (node->getName() != name && !name.empty())
        {
            node->setName(name); // covers RLVa @shownames flips
            if (auto wit = mWidgets.find(id); wit != mWidgets.end())
                wit->second->refresh();
        }

        // Stored position drives the distance suffix / distance sort.
        if (!is_avatar_entry && region)
        {
            const ALDerenderObject* obj_entry = static_cast<const ALDerenderObject*>(entry);
            ALObjectProperties::Record& rec = node->getRecordRef();
            rec.mPosRegion = obj_entry->posRegion;
            rec.mPosGlobal = region->getPosGlobalFromRegion(obj_entry->posRegion);
            rec.mDistance  = (F32)(rec.mPosGlobal - gAgent.getPositionGlobal()).magVec();
        }
    }

    // Prune rows whose entries were restored/removed (or filtered out).
    std::vector<LLUUID> stale;
    for (auto it = mDerenderedCategory->getChildrenBegin(); it != mDerenderedCategory->getChildrenEnd(); ++it)
    {
        const LLUUID& id = static_cast<ALSceneExplorerItem*>(it->get())->getUUID();
        if (!present.count(id))
            stale.push_back(id);
    }
    for (const LLUUID& id : stale)
    {
        auto it = mItems.find(id);
        if (it != mItems.end())
            removeNode(it->second);
    }
    if (mDerenderedCategory->getChildrenCount() == 0)
        destroyDerenderedCategory();
}

void ALFloaterSceneExplorer::reconcile()
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region || !mTree)
        return;

    // Region crossing: tear the tree down wholesale and rebuild for the new
    // region — cheaper and cleaner than discovering each stale node's absence
    // one-by-one, and keeps the root label current. The props cache
    // deliberately persists (UUID-keyed; see ALObjectPropertiesCache), so
    // objects fetched on a previous visit skip re-probing entirely.
    const U64 region_handle = region->getHandle();
    if (region_handle != mLastRegionHandle)
    {
        mLastRegionHandle = region_handle;
        clearTree();
        if (mRootItem)
            mRootItem->setName("Region: " + region->getName());
    }

    // Snapshot the derender list once per pass: isDerendered() is a linear
    // list walk, far too slow to call per object on a dense region.
    boost::unordered_set<LLUUID> derendered;
    for (const auto& entry : ALDerenderList::instance().getEntries())
    {
        if (entry && entry->getType() == ALDerenderEntry::TYPE_OBJECT)
            derendered.insert(entry->getID());
    }

    boost::unordered_set<LLUUID> present;
    const S32 num = gObjectList.getNumObjects();
    present.reserve(num);
    for (S32 i = 0; i < num; ++i)
    {
        LLViewerObject* obj = gObjectList.getObject(i);
        if (!obj || obj->isDead())
            continue;
        if (obj->getRegion() != region)
            continue;
        if (obj->isHUDAttachment())
            continue;
        // Terrain (surface patches) has no server-side object properties.
        if (obj->getPCode() == LLViewerObject::LL_VO_SURFACE_PATCH)
            continue;

        LLVOAvatar* avatarp = obj->asAvatar();
        // Control avatars (animesh) and UI/preview avatars have no associated
        // user and aren't real scene avatars. The animesh object itself still
        // appears via the normal object / attachment path below.
        if (avatarp && (avatarp->isControlAvatar() || avatarp->isUIAvatar()))
            continue;

        const bool is_avatar = (avatarp != nullptr);
        const bool is_attachment = obj->isAttachment();
        if ((is_avatar || is_attachment) && !mShowAvatars)
            continue;
        // Unselectable objects (water/sky/ground, no-select prims) can neither be
        // fetched from the server nor edited, so leave them out entirely.
        if (!is_avatar && !obj->mbCanSelect)
            continue;
        if (!is_avatar && !is_attachment && derendered.count(obj->getID()))
            continue;

        const LLUUID& id = obj->getID();
        present.insert(id);

        auto it = mItems.find(id);
        if (it == mItems.end())
        {
            // Not built yet: enqueue for time-sliced creation in drainBuildQueue()
            // rather than building thousands of widgets inline this frame.
            if (mQueued.insert(id).second)
                mBuildQueue.push_back(id);
            continue;
        }

        // Cheap per-pass refresh. Distance changes as things move (drives the
        // distance sort + "Nm" suffix); land-impact / object cost arrive
        // asynchronously after the node-build fetch, so keep PEEKING the cached
        // values until they populate — peek, never getObjectCost(): the getter
        // re-queues a GetObjectCost request whenever the cost is stale, and a
        // failed fetch stays stale, so a triggering read here would re-request
        // failed/uncosted objects every pass forever. Flags, geometry and the
        // ARC render cost (a profiled per-face hotspot) were captured at build
        // time and rarely change, so the full fillFromObject() is skipped to
        // keep the 1.5s reconcile O(N)-cheap even on a 60k-object region. The
        // radius filter is re-armed below when the agent has moved, since this
        // write deliberately doesn't dirty per-item filter state.
        ALSceneExplorerItem* node = it->second;
        ALObjectProperties::Record& rec = node->getRecordRef();
        const LLVector3 new_pos = obj->getPositionRegion();
        // Track whether anything actually moved this pass: the position-keyed
        // sorts re-arm only then, instead of unconditionally every 1.5s.
        if ((new_pos - rec.mPosRegion).magVecSquared() > 0.25f) // > 0.5 m
            mObjectsMoved = true;
        rec.mPosRegion  = new_pos;
        rec.mPosGlobal  = obj->getPositionGlobal();
        rec.mDistance   = (F32)(rec.mPosGlobal - gAgent.getPositionGlobal()).magVec();
        rec.mObjectCost = obj->peekObjectCost();
        rec.mLandImpact = obj->peekLinksetCost();

        // Avatar row labels depend on RLVa name restrictions, which can change
        // at any time — re-derive per pass (a handful of avatars, one cache
        // lookup + string compare each) so @shownames anonymizes existing rows
        // promptly and lifting it restores them. Complexity / attachment count
        // shift as avatars change outfits, so refresh those here too.
        if (is_avatar)
        {
            rec.mRenderCost = (F32)avatarp->getVisualComplexity();
            rec.mPrimCount  = avatarp->getAttachmentCount();

            const std::string name = avatarDisplayName(id);
            if (!name.empty() && name != node->getName())
            {
                node->setName(name);
                if (auto wit = mWidgets.find(id); wit != mWidgets.end())
                    wit->second->refresh();
            }
        }
    }

    // Collect ids, not pointers: removing a linkset root frees its descendant
    // model items, so a raw descendant pointer collected earlier in the pass
    // would dangle. Each id is re-looked-up immediately before removal.
    std::vector<LLUUID> to_remove;
    for (const auto& entry : mItems)
    {
        // Synthetic nodes have no backing object in the present set:
        // attachment-point folders are pruned below once empty, and derendered
        // rows are managed entirely by syncDerendered().
        if (entry.second->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
            || entry.second->isDerenderedType())
        {
            continue;
        }
        if (!present.count(entry.first))
            to_remove.push_back(entry.first);
    }
    for (const LLUUID& id : to_remove)
    {
        auto it = mItems.find(id);
        if (it != mItems.end())
            removeNode(it->second);
    }

    // Drop attachment-point folders that no longer hold any attachments.
    std::vector<LLUUID> empty_points;
    for (const auto& entry : mItems)
    {
        if (entry.second->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
            && entry.second->getChildrenCount() == 0)
        {
            empty_points.push_back(entry.first);
        }
    }
    for (const LLUUID& id : empty_points)
    {
        auto it = mItems.find(id);
        if (it != mItems.end())
            removeNode(it->second);
    }

    // Reflect derender-list state (also re-checked here so region changes and
    // toggle flips stay correct even without a change signal).
    syncDerendered();
    updateCategoryCounts();
    // React to RLVa @shownames flips: scrub or re-resolve owner names.
    auditOwnerNames();
    mDetailDirty = true; // live metrics (distance/costs) were refreshed above
    mScanVisible = true; // visible-row pass runs after the next arrange

    // Self-healing 360 mode: the capture floater restores the agent mode on
    // its close without knowing about us, so while our toggle is on, re-claim
    // it. changeInterestListMode() no-ops when the mode is already 360, and
    // crossings re-apply it through LLAgent::capabilityReceivedCallback.
    if (mFullRegion && gAgent.getInterestListMode() != IL_MODE_360)
    {
        gAgent.changeInterestListMode(IL_MODE_360);
    }

    // The per-pass distance refresh above doesn't dirty per-item filter state,
    // so while a spatial scope (radius or parcel) is active, re-arm the whole
    // filter once the agent has actually moved — otherwise the scope would
    // keep showing the object set from wherever the filter last ran. (A
    // parcel change requires movement, so this re-arm covers parcel scope.)
    ALSceneExplorerFilter& filter = mViewModel.getFilter();
    if (filter.isScopeActive())
    {
        const LLVector3d agent_pos = gAgent.getPositionGlobal();
        if ((agent_pos - mLastFilterAgentPos).magVec() > 1.0)
        {
            mLastFilterAgentPos = agent_pos;
            filter.setModified(LLFolderViewFilter::FILTER_RESTART);
        }
    }

    // Re-sort only for the position-driven keys, and only when positions
    // actually changed: agent distance when the agent has moved, either key
    // when an object moved this pass (std::list::sort over tens of thousands
    // of roots every 1.5s is real cost on a 360-streamed region). Static keys
    // (name/land-impact/triangles/type) re-sort per-parent when nodes are
    // added or change. requestSortAll() only bumps the target sort version —
    // sorting actually runs inside arrange(), which nothing else re-arms on a
    // quiet tree — so force a re-arrange too, or the refreshed positions
    // never reorder anything. While the cursor is over the tree or a context
    // menu is up, the re-sort is deferred (not dropped — the trigger state
    // stays armed) so rows never jump under the mouse mid-click.
    const ALSceneExplorerSort::ESortMode sort_mode = mViewModel.getSorter().getMode();
    if (sort_mode == ALSceneExplorerSort::SORT_DISTANCE
        || sort_mode == ALSceneExplorerSort::SORT_REGION_ORIGIN)
    {
        const LLVector3d agent_pos = gAgent.getPositionGlobal();
        const bool agent_moved = (sort_mode == ALSceneExplorerSort::SORT_DISTANCE)
            && (agent_pos - mLastSortAgentPos).magVec() > 1.0;

        if (agent_moved || mObjectsMoved)
        {
            S32 mouse_x = 0, mouse_y = 0;
            LLUI::getInstance()->getMousePositionScreen(&mouse_x, &mouse_y);
            const bool pointer_over_tree =
                mTreePanel && mTreePanel->calcScreenRect().pointInRect(mouse_x, mouse_y);
            const bool menu_open =
                LLMenuGL::sMenuContainer && LLMenuGL::sMenuContainer->hasVisibleMenu();
            if (!pointer_over_tree && !menu_open)
            {
                mLastSortAgentPos = agent_pos;
                mObjectsMoved = false;
                mViewModel.requestSortAll();
                mTree->arrangeAll();
            }
        }
    }
}

// ============================================================================
// Async property fetch
// ============================================================================
void ALFloaterSceneExplorer::queueProps(const LLUUID& id)
{
    if (id.isNull() || mQueuedProps.count(id))
        return;
    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    // Already sent and awaiting the reply.
    if (cache.isPending(id))
        return;
    // Skip if we already have full data (creator/date) for this object; a
    // family-only entry (e.g. from a hover) still warrants a full fetch.
    const ALObjectPropertiesCache::ServerProps* p = cache.get(id);
    if (p && p->mHasFullData)
        return;
    mQueuedProps.insert(id);
    mFetchQueue.push_back(id);
}

void ALFloaterSceneExplorer::drainPropsQueue()
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region || (mFetchQueue.empty() && mPriorityFetch.empty()))
        return;

    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    std::vector<U32> local_ids;
    local_ids.reserve(MAX_OBJECTS_PER_PACKET);

    auto take = [&](const LLUUID& id)
    {
        mQueuedProps.erase(id);

        // Already in flight (covers the priority lane leaving its ids in the
        // main queue too). Deliberately NOT gated on cached full data: an
        // explicit Refresh re-queues resolved ids to pick up renames.
        if (cache.isPending(id))
            return;

        LLViewerObject* obj = gObjectList.findObject(id);
        // Local ids are a per-region namespace, so never address an object
        // that died or crossed into a neighbour region.
        if (!obj || obj->isDead() || obj->getRegion() != region)
            return;
        // Never disturb the user's live selection: a raw ObjectDeselect for an
        // actively edited object desyncs the simulator's selection state (the
        // sim halts physical objects and streams updates only while selected).
        // retryUnresolved() re-queues it once it is no longer selected.
        if (obj->isSelected())
            return;

        local_ids.push_back(obj->getLocalID());
        cache.markPending(id);
    };

    // On-screen rows first — a 360-streamed region can hold a multi-minute
    // backlog, and what the user is looking at shouldn't wait behind it.
    while (!mPriorityFetch.empty() && local_ids.size() < (size_t)MAX_OBJECTS_PER_PACKET)
    {
        const LLUUID id = mPriorityFetch.front();
        mPriorityFetch.pop_front();
        mPriorityQueued.erase(id);
        take(id);
    }
    while (!mFetchQueue.empty() && local_ids.size() < (size_t)MAX_OBJECTS_PER_PACKET)
    {
        const LLUUID id = mFetchQueue.front();
        mFetchQueue.pop_front();
        take(id);
    }
    if (local_ids.empty())
        return;

    // Bulk-select to provoke full ObjectProperties replies (which, unlike the
    // family variant, include creator and creation date), then immediately
    // deselect so nothing stays selected on the simulator. The replies are
    // captured by ALObjectPropertiesCache, whose in-flight tracking also tells
    // LLSelectMgr these node-less replies are expected (no warning).
    const LLHost& host = region->getHost();
    sendObjectSelectionMessage(_PREHASH_ObjectSelect, local_ids, host);
    sendObjectSelectionMessage(_PREHASH_ObjectDeselect, local_ids, host);
}

void ALFloaterSceneExplorer::retryUnresolved()
{
    // Property replies arrive over unreliable transport and can be dropped, so
    // periodically re-request objects whose full data never came back. Capped
    // per object so something the sim never answers for can't generate retry
    // traffic forever.
    constexpr S32 MAX_PROPS_RETRIES = 8;
    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    for (const auto& entry : mItems)
    {
        ALSceneExplorerItem* item = entry.second;
        const ALSceneExplorerItem::EItemType type = item->getItemType();
        // Avatars resolve via the name cache; attachment-point folders and
        // derendered rows are synthetic — none has server props to fetch.
        if (type == ALSceneExplorerItem::TYPE_AVATAR
            || type == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
            || item->isDerenderedType()
            || item->isContainer())
        {
            continue;
        }
        // Gate on the cache's full-data flag, not the record's mPropsValid: a
        // family-only entry (hover) marks the record valid but the full fetch
        // (creator / creation date) still needs its retry.
        const ALObjectPropertiesCache::ServerProps* p = cache.get(entry.first);
        if (p && p->mHasFullData)
            continue;
        if (mQueuedProps.count(entry.first))
            continue; // still waiting in the queue, not lost
        if (item->getPropsRetries() >= MAX_PROPS_RETRIES)
            continue;
        item->notePropsRetry();
        cache.clearPending(entry.first); // assume the in-flight reply was lost
        queueProps(entry.first);
    }
}

void ALFloaterSceneExplorer::applyServerProps(ALSceneExplorerItem* item)
{
    // Derendered rows keep their stored entry name; cached props (from before
    // the derender) must not overwrite it.
    if (!item || item->isDerenderedType())
        return;
    const ALObjectPropertiesCache::ServerProps* p =
        ALObjectPropertiesCache::instance().get(item->getUUID());
    if (!p)
        return;

    ALObjectProperties::Record rec = item->getRecord();
    rec.mName         = p->mName;
    rec.mDescription  = p->mDescription;
    rec.mOwnerId      = p->mOwnerId;
    rec.mGroupId      = p->mGroupId;
    rec.mCreatorId    = p->mCreatorId;
    rec.mGroupOwned   = p->mGroupOwned;
    rec.mCreationDate = p->mCreationDate;
    rec.mSaleType     = p->mSaleType;
    rec.mSalePrice    = p->mSalePrice;
    if (p->mSaleType != 0) // LLSaleInfo::FS_NOT
        rec.mFlags |= ALObjectProperties::FLAG_FOR_SALE;
    else
        rec.mFlags &= ~ALObjectProperties::FLAG_FOR_SALE;
    rec.mPropsValid   = true;

    // Adopt the server name in the same pass (avatars keep their display name
    // from the name cache instead).
    const std::string display_name =
        (!p->mName.empty() && item->getItemType() != ALSceneExplorerItem::TYPE_AVATAR)
            ? p->mName : LLStringUtil::null;
    item->updateRecord(rec, display_name);

    // Fold the owner's display name into the searchable text (resolved once
    // per unique owner; batched into the rows when the lookup lands).
    noteOwnerFor(item);
}

void ALFloaterSceneExplorer::noteOwnerFor(ALSceneExplorerItem* item)
{
    const ALObjectProperties::Record& rec = item->getRecord();
    if (!rec.mPropsValid)
        return;
    const LLUUID& owner = rec.mGroupOwned ? rec.mGroupId : rec.mOwnerId;
    if (owner.isNull())
        return;

    auto found = mOwnerNames.find(owner);
    if (found != mOwnerNames.end())
    {
        if (!found->second.mName.empty())
            item->setOwnerName(found->second.mName);
        return;
    }
    resolveOwnerName(owner, rec.mGroupOwned);
}

void ALFloaterSceneExplorer::resolveOwnerName(const LLUUID& owner_id, bool group_owned)
{
    if (!mOwnerNamesPending.insert(owner_id).second)
        return; // lookup already in flight

    // RLVa @shownames: a hidden agent name must not become searchable text
    // (an anonym would be useless to search anyway). Recorded as empty;
    // auditOwnerNames() re-resolves it if the restriction lifts.
    if (!group_owned && RlvActions::isRlvEnabled() && owner_id != gAgentID
        && !RlvActions::canShowName(RlvActions::SNC_DEFAULT, owner_id))
    {
        mOwnerNamesPending.erase(owner_id);
        mOwnerNames[owner_id] = ResolvedOwner();
        return;
    }

    LLHandle<ALFloaterSceneExplorer> handle = getDerivedHandle<ALFloaterSceneExplorer>();
    if (group_owned)
    {
        gCacheName->getGroup(owner_id,
            [handle](const LLUUID& id, const std::string& name, bool is_group)
            {
                if (ALFloaterSceneExplorer* self = handle.get())
                    self->onOwnerNameResolved(id, name, true);
            });
    }
    else
    {
        LLAvatarNameCache::get(owner_id,
            [handle](const LLUUID& id, const LLAvatarName& av_name)
            {
                if (ALFloaterSceneExplorer* self = handle.get())
                    self->onOwnerNameResolved(id, av_name.getCompleteName(), false);
            });
    }
}

void ALFloaterSceneExplorer::onOwnerNameResolved(const LLUUID& owner_id, const std::string& name, bool is_group)
{
    mOwnerNamesPending.erase(owner_id);

    // The @shownames restriction may have been imposed while the lookup was
    // in flight; record hidden rather than leaking the name.
    std::string stored = name;
    if (!is_group && RlvActions::isRlvEnabled() && owner_id != gAgentID
        && !RlvActions::canShowName(RlvActions::SNC_DEFAULT, owner_id))
    {
        stored.clear();
    }
    ResolvedOwner& entry = mOwnerNames[owner_id];
    entry.mName = stored;
    entry.mIsGroup = is_group;
    if (stored.empty())
        return;

    // One batched pass folds the name into every row this owner has (search
    // text + suffix); each setOwnerName dirties that row's filter state, and
    // the visible-row scan refreshes on-screen suffixes within a tick.
    for (const auto& item_entry : mItems)
    {
        ALSceneExplorerItem* item = item_entry.second;
        const ALObjectProperties::Record& rec = item->getRecord();
        if (rec.mPropsValid
            && (rec.mGroupOwned ? rec.mGroupId : rec.mOwnerId) == owner_id)
        {
            item->setOwnerName(stored);
        }
    }
    mScanVisible = true;

    // The owner-filter combo may have been waiting on this very name.
    if (owner_id == mFilterOwnerId)
        updateOwnerFilterLabel();
}

void ALFloaterSceneExplorer::auditOwnerNames()
{
    // RLVa @shownames can flip at any time. Imposing it must SCRUB resolved
    // owner names back out of the rows (suffix and searchable text both);
    // lifting it re-resolves owners recorded as hidden. Group names are never
    // restricted. Runs per reconcile pass — the unique-owner map is small and
    // canShowName is a lookup.
    for (auto& owner_entry : mOwnerNames)
    {
        if (owner_entry.second.mIsGroup)
            continue;
        const LLUUID& id = owner_entry.first;
        const bool can_show = !RlvActions::isRlvEnabled() || id == gAgentID
            || RlvActions::canShowName(RlvActions::SNC_DEFAULT, id);

        if (!can_show && !owner_entry.second.mName.empty())
        {
            owner_entry.second.mName.clear();
            for (const auto& item_entry : mItems)
            {
                ALSceneExplorerItem* item = item_entry.second;
                const ALObjectProperties::Record& rec = item->getRecord();
                if (rec.mPropsValid && !rec.mGroupOwned && rec.mOwnerId == id)
                    item->setOwnerName(LLStringUtil::null);
            }
            mScanVisible = true;
            if (id == mFilterOwnerId)
                updateOwnerFilterLabel();
        }
        else if (can_show && owner_entry.second.mName.empty()
                 && !mOwnerNamesPending.count(id))
        {
            // Hidden when first seen (or imposed mid-flight): resolve now.
            resolveOwnerName(id, false);
        }
    }
}

void ALFloaterSceneExplorer::onAvatarNameLoaded(const LLUUID& id, const LLAvatarName& av_name)
{
    auto it = mItems.find(id);
    if (it == mItems.end())
        return;
    it->second->setName(displayNameFor(id, av_name)); // RLVa-aware
    auto wit = mWidgets.find(id);
    if (wit != mWidgets.end())
        wit->second->refresh();
}

void ALFloaterSceneExplorer::onPropsCacheChanged(const LLUUID& id)
{
    auto it = mItems.find(id);
    if (it == mItems.end())
        return;
    applyServerProps(it->second);

    // The folder-view widget caches its label, so make it re-read the model.
    // applyServerProps() already dirtied the filter via updateRecord()/setName(),
    // so the next idle pass re-filters this node.
    auto wit = mWidgets.find(id);
    if (wit != mWidgets.end())
        wit->second->refresh();

    if (id == mLastDetailID)
        mDetailDirty = true;
}

// ============================================================================
// Filters / sort
// ============================================================================
namespace
{
    // The flag bits the quick-bar checkboxes own; everything else in the
    // mask belongs to the companion filters floater.
    constexpr U32 QUICK_FLAG_BITS = ALObjectProperties::FLAG_SCRIPTED
        | ALObjectProperties::FLAG_LIGHT | ALObjectProperties::FLAG_PARTICLES;
}

void ALFloaterSceneExplorer::onFilterChanged()
{
    ALSceneExplorerFilter& f = mViewModel.getFilter();
    f.setFilterSubString(getChild<LLFilterEditor>("filter_input")->getText());

    const S32 search_idx = llmax(0, getChild<LLComboBox>("search_type_combo")->getCurrentIndex());
    f.setSearchType((ALSceneExplorerFilter::ESearchType)search_idx);
    gSavedSettings.setU32("ALSceneExplorerSearchType", (U32)search_idx);

    // "Selected owner" is only meaningful with a target id (set by the
    // context menu's Filter by This Owner); picked manually it means Any.
    S32 owner_idx = llmax(0, getChild<LLComboBox>("owner_combo")->getCurrentIndex());
    if (owner_idx == (S32)ALSceneExplorerFilter::OWNER_SPECIFIC && mFilterOwnerId.isNull())
        owner_idx = (S32)ALSceneExplorerFilter::OWNER_ANY;
    f.setOwnerId(mFilterOwnerId);
    f.setOwnerMode((ALSceneExplorerFilter::EOwnerMode)owner_idx);

    // The quick boxes own their three bits of the persisted mask; the rest
    // belongs to the companion filters floater and must survive a bar commit.
    U32 flags = gSavedSettings.getU32("ALSceneExplorerFlagFilter") & ~QUICK_FLAG_BITS;
    if (getChild<LLUICtrl>("flag_scripted")->getValue().asBoolean())  flags |= ALObjectProperties::FLAG_SCRIPTED;
    if (getChild<LLUICtrl>("flag_light")->getValue().asBoolean())     flags |= ALObjectProperties::FLAG_LIGHT;
    if (getChild<LLUICtrl>("flag_particles")->getValue().asBoolean()) flags |= ALObjectProperties::FLAG_PARTICLES;
    f.setFlagMask(flags);
    f.setGeomMask(gSavedSettings.getU32("ALSceneExplorerTypeFilter"));

    // The bar's Within checkbox toggles the radius scope; the parcel scope is
    // only reachable from the companion floater and survives until the user
    // explicitly switches away.
    U32 scope = gSavedSettings.getU32("ALSceneExplorerScope");
    const bool within = getChild<LLUICtrl>("limit_radius_check")->getValue().asBoolean();
    if (within)
        scope = (U32)ALSceneExplorerFilter::SCOPE_RADIUS;
    else if (scope == (U32)ALSceneExplorerFilter::SCOPE_RADIUS)
        scope = (U32)ALSceneExplorerFilter::SCOPE_REGION;
    const F32 radius = (F32)getChild<LLUICtrl>("radius_slider")->getValue().asReal();
    f.setScope((ALSceneExplorerFilter::EScope)scope, radius);

    f.setMinLandImpact(gSavedSettings.getF32("ALSceneExplorerMinLandImpact"));
    f.setMinTriangles(gSavedSettings.getU32("ALSceneExplorerMinTriangles"));

    // Persist the filter set (the text predicate is deliberately session-only).
    gSavedSettings.setU32("ALSceneExplorerOwnerFilter", (U32)owner_idx);
    gSavedSettings.setU32("ALSceneExplorerFlagFilter", flags);
    gSavedSettings.setU32("ALSceneExplorerScope", scope);
    gSavedSettings.setF32("ALSceneExplorerRadius", radius);

    updateOwnerFilterLabel();
    // The filter setters above bumped the filter generation, so the next idle
    // pass (mTree->update()) re-filters and re-arranges.
}

void ALFloaterSceneExplorer::refreshFilters()
{
    // Settings -> quick-bar controls, then the normal commit path (which
    // re-derives the same settings — stable). The companion floater calls
    // this after writing settings so both surfaces stay in agreement.
    const U32 flags = gSavedSettings.getU32("ALSceneExplorerFlagFilter");
    getChild<LLUICtrl>("flag_scripted")->setValue((flags & ALObjectProperties::FLAG_SCRIPTED) != 0);
    getChild<LLUICtrl>("flag_light")->setValue((flags & ALObjectProperties::FLAG_LIGHT) != 0);
    getChild<LLUICtrl>("flag_particles")->setValue((flags & ALObjectProperties::FLAG_PARTICLES) != 0);
    getChild<LLUICtrl>("limit_radius_check")->setValue(
        gSavedSettings.getU32("ALSceneExplorerScope") == (U32)ALSceneExplorerFilter::SCOPE_RADIUS);
    getChild<LLUICtrl>("radius_slider")->setValue(gSavedSettings.getF32("ALSceneExplorerRadius"));
    onFilterChanged();
}

void ALFloaterSceneExplorer::doResetFilters()
{
    getChild<LLFilterEditor>("filter_input")->setText(LLStringUtil::null);
    getChild<LLComboBox>("owner_combo")->setCurrentByIndex(0);
    mFilterOwnerId.setNull();
    gSavedSettings.setU32("ALSceneExplorerFlagFilter", 0);
    gSavedSettings.setU32("ALSceneExplorerTypeFilter", 0);
    gSavedSettings.setU32("ALSceneExplorerScope", 0);
    gSavedSettings.getControl("ALSceneExplorerRadius")->resetToDefault();
    gSavedSettings.setF32("ALSceneExplorerMinLandImpact", 0.f);
    gSavedSettings.setU32("ALSceneExplorerMinTriangles", 0);
    refreshFilters();

    // Push the cleared state into the companion floater if it is open.
    if (LLFloater* filters = LLFloaterReg::findInstance("scene_explorer_filters"))
    {
        static_cast<ALFloaterSceneExplorerFilters*>(filters)->refreshFromSettings();
    }
}

void ALFloaterSceneExplorer::doShowFilters()
{
    LLFloater* filters = LLFloaterReg::showInstance("scene_explorer_filters");
    if (filters)
    {
        // Parent it like inventory's filter finder: follows and closes with
        // the explorer.
        addDependentFloater(filters);
    }
}

void ALFloaterSceneExplorer::updateOwnerFilterLabel()
{
    // Show WHO the "Selected owner" filter targets on the combo button. The
    // list entry itself stays generic; the button text carries the name.
    LLComboBox* combo = getChild<LLComboBox>("owner_combo");
    if (combo->getCurrentIndex() != (S32)ALSceneExplorerFilter::OWNER_SPECIFIC
        || mFilterOwnerId.isNull())
    {
        return;
    }
    std::string name;
    auto found = mOwnerNames.find(mFilterOwnerId);
    if (found != mOwnerNames.end() && !found->second.mName.empty())
        name = found->second.mName;
    else if (mItems.count(mFilterOwnerId))
        name = mItems[mFilterOwnerId]->getName(); // avatar row labels are RLVa-anonymized
    combo->setLabel(name.empty() ? std::string("Owner: (loading)") : "Owner: " + name);
}

void ALFloaterSceneExplorer::doFilterByOwner()
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item || item->isContainer())
        return;

    // An avatar row is its own owner; object rows use their fetched owner
    // (the deeding group for group-owned objects).
    LLUUID owner;
    bool group_owned = false;
    if (item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR)
    {
        owner = item->getUUID();
    }
    else
    {
        const ALObjectProperties::Record& rec = item->getRecord();
        if (rec.mPropsValid)
        {
            owner = rec.mGroupOwned ? rec.mGroupId : rec.mOwnerId;
            group_owned = rec.mGroupOwned;
        }
    }
    if (owner.isNull())
        return;

    mFilterOwnerId = owner;
    // Make sure the combo can label itself with the owner's name.
    if (!mOwnerNames.count(owner))
        resolveOwnerName(owner, group_owned);
    getChild<LLComboBox>("owner_combo")->setCurrentByIndex((S32)ALSceneExplorerFilter::OWNER_SPECIFIC);
    onFilterChanged();
}

void ALFloaterSceneExplorer::updateCategoryCounts()
{
    auto update = [](ALSceneExplorerItem* category, LLFolderViewItem* widget, const char* base)
    {
        if (!category)
            return;
        const std::string name = llformat("%s (%d)", base, (S32)category->getChildrenCount());
        if (name != category->getName())
        {
            category->setName(name);
            if (widget)
                widget->refresh();
        }
    };
    update(mObjectsCategory,    mObjectsWidget,    "Objects");
    update(mAvatarsCategory,    mAvatarsWidget,    "Avatars");
    update(mDerenderedCategory, mDerenderedWidget, "Derendered");
}

void ALFloaterSceneExplorer::updateActionButtons()
{
    ALSceneExplorerItem* item = getSelectedItem();
    const LLUUID id = item ? item->getUUID() : LLUUID::null;
    if (id == mLastButtonStateID)
        return;
    mLastButtonStateID = id;

    const bool is_row = item && !item->isContainer();
    LLViewerObject* obj = is_row ? gObjectList.findObject(id) : nullptr;
    const bool derendered_obj = item && item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_OBJECT;

    getChild<LLButton>("focus_btn")->setEnabled(obj != nullptr);
    getChild<LLButton>("edit_btn")->setEnabled(
        obj && !obj->isAvatar()
        && (!RlvActions::isRlvEnabled() || RlvActions::canEdit(obj)));
    getChild<LLButton>("inspect_btn")->setEnabled(obj != nullptr);
    // Beacon needs a position: live rows have one, derendered objects keep
    // their stored one, derendered avatars don't.
    getChild<LLButton>("beacon_btn")->setEnabled(
        is_row && !item->getRecord().mPosGlobal.isExactlyZero());
    // Teleport works for live rows and for derendered objects via their
    // stored position.
    getChild<LLButton>("teleport_btn")->setEnabled(obj != nullptr || derendered_obj);
    // Everything else lives in the gear / context menus.
}

// ============================================================================
// Detail pane
// ============================================================================
void ALFloaterSceneExplorer::onToggleDetails()
{
    mDetailsExpanded = getChild<LLButton>("details_btn")->getToggleState();
    gSavedSettings.setBOOL("ALSceneExplorerShowDetails", mDetailsExpanded);
    if (mDetailHost)
        getChild<LLLayoutStack>("main_stack")->collapsePanel(mDetailHost, !mDetailsExpanded);
    mDetailDirty = true;
}

void ALFloaterSceneExplorer::refreshDetail()
{
    ALSceneExplorerItem* item = getSelectedItem();
    const bool avatar_row = item
        && (item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR
            || item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_AVATAR);

    // Type-specific panels: swap which one shows for a cleaner display.
    getChild<LLPanel>("detail_panel_avatar")->setVisible(avatar_row);
    getChild<LLPanel>("detail_panel_object")->setVisible(!avatar_row);

    if (avatar_row)
        fillAvatarDetail(item);
    else
        fillObjectDetail(item);
}

void ALFloaterSceneExplorer::fillAvatarDetail(ALSceneExplorerItem* item)
{
    getChild<LLView>("detail_faces_layout")->setVisible(false);
    getChild<LLScrollListCtrl>("detail_faces")->deleteAllItems();
    mLastFacesID.setNull();

    const ALObjectProperties::Record& rec = item->getRecord();
    getChild<LLTextBox>("avatar_name")->setText(agentSlurl(item->getUUID()));
    getChild<LLTextBox>("avatar_distance")->setText(llformat("%.1f m", rec.mDistance));

    LLTextBox* complexity = getChild<LLTextBox>("avatar_complexity");
    if (rec.mRenderCost > 0.f)
    {
        complexity->setText(llformat("%.0f", rec.mRenderCost));
        complexity->setColor(heatColor(rec.mRenderCost, 100000.f, 250000.f));
    }
    else
    {
        complexity->setText(std::string("-"));
        complexity->setColor(mutedColor());
    }
    getChild<LLTextBox>("avatar_attachments")->setText(
        rec.mPrimCount > 0 ? llformat("%d worn", rec.mPrimCount) : std::string("-"));

    LLTextBox* note = getChild<LLTextBox>("avatar_note");
    if (item->isDerenderedType())
    {
        note->setText(std::string("Derendered - right-click the row to Restore."));
        note->setColor(cautionColor());
    }
    else
    {
        note->setText(std::string("Click the name to open their profile."));
        note->setColor(mutedColor());
    }
}

void ALFloaterSceneExplorer::fillObjectDetail(ALSceneExplorerItem* item)
{
    auto show = [this](const char* name, bool visible)
    {
        getChild<LLView>(name)->setVisible(visible);
    };
    auto set_text = [this](const char* name, const std::string& value)
    {
        getChild<LLTextBox>(name)->setText(value);
    };
    auto show_row = [&](const char* label, const char* value, bool visible)
    {
        show(label, visible);
        show(value, visible);
    };

    LLScrollListCtrl* faces = getChild<LLScrollListCtrl>("detail_faces");

    const bool is_row = item && !item->isContainer();
    const bool derendered = is_row && item->isDerenderedType();
    LLViewerObject* obj = is_row ? gObjectList.findObject(item->getUUID()) : nullptr;
    if (obj && obj->isDead())
        obj = nullptr;

    LLTextBox* name_box = getChild<LLTextBox>("detail_name");
    LLTextBox* desc_box = getChild<LLTextBox>("detail_desc");

    if (!is_row)
    {
        name_box->setText(std::string("Select an object or avatar to see its details."));
        desc_box->setVisible(false);
        show_row("label_owner", "detail_owner", false);
        show_row("label_creator", "detail_creator", false);
        show("label_perms", false);
        show("check_modify", false);
        show("check_copy", false);
        show("check_transfer", false);
        show("detail_perms_next", false);
        show_row("label_pos", "detail_where", false);
        show_row("label_size", "detail_size", false);
        show_row("label_geom", "detail_build", false);
        show("label_costs", false);
        show("label_li", false);
        show("detail_li", false);
        show("label_render", false);
        show("detail_render", false);
        show("label_phys", false);
        show("detail_phys", false);
        show("label_stream", false);
        show("detail_stream", false);
        show("label_flags", false);
        show("check_scripted", false);
        show("check_light", false);
        show("check_physics", false);
        show("check_phantom", false);
        show("check_temp", false);
        show("detail_flags_extra", false);
        getChild<LLView>("detail_faces_layout")->setVisible(false);
        faces->deleteAllItems();
        mLastFacesID.setNull();
        return;
    }

    const ALObjectProperties::Record& rec = item->getRecord();
    const LLUUID& id = item->getUUID();

    name_box->setText(item->getName());

    // Description doubles as the derendered notice.
    if (derendered)
    {
        desc_box->setVisible(true);
        desc_box->setText(std::string("Derendered - right-click the row to Restore."));
        desc_box->setColor(cautionColor());
    }
    else
    {
        desc_box->setVisible(!rec.mDescription.empty());
        desc_box->setText(rec.mDescription);
        desc_box->setColor(labelColor());
    }

    // Value + colour in one step so loading placeholders read muted and real
    // values reset to full brightness.
    auto set_value = [this](const char* name, const std::string& value, bool muted)
    {
        LLTextBox* box = getChild<LLTextBox>(name);
        box->setText(value);
        box->setColor(muted ? mutedColor() : labelColor());
    };

    // Who. Rows stay visible while data is in flight (muted "loading...")
    // so the form doesn't reflow as replies arrive. Hidden only for
    // derendered entries, whose props are stale.
    const ALObjectPropertiesCache::ServerProps* props =
        derendered ? nullptr : ALObjectPropertiesCache::instance().get(id);
    const bool props_full = props && props->mHasFullData;

    show_row("label_owner", "detail_owner", !derendered);
    if (!derendered)
    {
        if (props)
            set_value("detail_owner", props->mGroupOwned ? groupSlurl(props->mGroupId)
                                                         : agentSlurl(props->mOwnerId), false);
        else
            set_value("detail_owner", std::string("loading..."), true);
    }

    show_row("label_creator", "detail_creator", !derendered);
    if (!derendered)
    {
        if (!props_full)
        {
            set_value("detail_creator", std::string("loading..."), true);
        }
        else if (props->mCreatorId.isNull())
        {
            set_value("detail_creator", std::string("-"), true);
        }
        else
        {
            std::string creator = agentSlurl(props->mCreatorId);
            if (props->mCreationDate)
                creator += "  on " + LLDate((F64)props->mCreationDate).asString().substr(0, 10);
            set_value("detail_creator", creator, false);
        }
    }

    show("label_perms", !derendered);
    show("check_modify", !derendered);
    show("check_copy", !derendered);
    show("check_transfer", !derendered);
    show("detail_perms_next", !derendered);
    if (!derendered)
    {
        getChild<LLCheckBoxCtrl>("check_modify")->setValue(props_full && (props->mOwnerMask & PERM_MODIFY) != 0);
        getChild<LLCheckBoxCtrl>("check_copy")->setValue(props_full && (props->mOwnerMask & PERM_COPY) != 0);
        getChild<LLCheckBoxCtrl>("check_transfer")->setValue(props_full && (props->mOwnerMask & PERM_TRANSFER) != 0);
        set_text("detail_perms_next", props_full ? "next owner " + permTriad(props->mNextOwnerMask)
                                                 : std::string("loading..."));
    }

    // Where
    show_row("label_pos", "detail_where", true);
    if (derendered)
    {
        set_text("detail_where", rec.mPosRegion.isExactlyZero()
            ? std::string("unknown")
            : llformat("<%.0f, %.0f, %.0f>  (%.0f m away)",
                       rec.mPosRegion.mV[VX], rec.mPosRegion.mV[VY], rec.mPosRegion.mV[VZ],
                       rec.mDistance));
    }
    else if (obj)
    {
        const LLVector3 pos = obj->getPositionRegion();
        set_text("detail_where", llformat("<%.1f, %.1f, %.1f>  (%.1f m away)",
                                          pos.mV[VX], pos.mV[VY], pos.mV[VZ], rec.mDistance));
    }
    else
    {
        set_text("detail_where", llformat("%.1f m away", rec.mDistance));
    }

    show_row("label_size", "detail_size", !derendered);
    if (!derendered)
    {
        if (obj)
        {
            const LLVector3 scale = obj->getScale();
            F32 roll, pitch, yaw;
            obj->getRotationRegion().getEulerAngles(&roll, &pitch, &yaw);
            set_value("detail_size", llformat("%.2f x %.2f x %.2f m   rot <%.0f, %.0f, %.0f>",
                                              scale.mV[VX], scale.mV[VY], scale.mV[VZ],
                                              roll * RAD_TO_DEG, pitch * RAD_TO_DEG, yaw * RAD_TO_DEG),
                      false);
        }
        else
        {
            set_value("detail_size", std::string("loading..."), true);
        }
    }

    std::string geom;
    if (rec.mPrimCount > 1)
        geom += llformat("%d prims   ", rec.mPrimCount);
    if (rec.mNumTriangles > 0)
        geom += llformat("%u tris   %u verts   %d faces", rec.mNumTriangles, rec.mNumVertices, rec.mNumFaces);
    show_row("label_geom", "detail_build", !derendered);
    if (!derendered)
        set_value("detail_build", geom.empty() ? std::string("-") : geom, geom.empty());

    // Costs: grey metric labels with aligned, heat-colored values; a missing
    // metric shows a muted dash so cells never shift. Showing the detail pane
    // is explicit demand for this object's costs.
    requestCostsFor(item);
    const bool show_costs = !derendered;
    show("label_costs", show_costs);
    show("label_li", show_costs);
    show("detail_li", show_costs);
    show("label_render", show_costs);
    show("detail_render", show_costs);
    show("label_phys", show_costs);
    show("detail_phys", show_costs);
    show("label_stream", show_costs);
    show("detail_stream", show_costs);
    if (show_costs)
    {
        auto set_cost = [this](const char* name, const std::string& value, const LLColor4& color)
        {
            LLTextBox* box = getChild<LLTextBox>(name);
            box->setText(value);
            box->setColor(color);
        };
        set_cost("detail_li",
                 rec.mLandImpact > 0.f ? llformat("%.0f", rec.mLandImpact) : std::string("-"),
                 rec.mLandImpact > 0.f ? heatColor(rec.mLandImpact, 50.f, 200.f) : mutedColor());
        set_cost("detail_render",
                 rec.mRenderCost > 0.f ? llformat("%.0f", rec.mRenderCost) : std::string("-"),
                 rec.mRenderCost > 0.f ? heatColor(rec.mRenderCost, 30000.f, 100000.f) : mutedColor());
        set_cost("detail_phys",
                 rec.mPhysicsCost > 0.f ? llformat("%.1f", rec.mPhysicsCost) : std::string("-"),
                 rec.mPhysicsCost > 0.f ? labelColor() : mutedColor());
        set_cost("detail_stream",
                 rec.mStreamingCost > 0.f ? llformat("%.1f", rec.mStreamingCost) : std::string("-"),
                 rec.mStreamingCost > 0.f ? labelColor() : mutedColor());
    }

    // Flags: the common ones as read-only checkboxes, the rest as muted text.
    const bool show_flags = !derendered;
    show("label_flags", show_flags);
    show("check_scripted", show_flags);
    show("check_light", show_flags);
    show("check_physics", show_flags);
    show("check_phantom", show_flags);
    show("check_temp", show_flags);
    if (show_flags)
    {
        getChild<LLCheckBoxCtrl>("check_scripted")->setValue(rec.hasFlag(ALObjectProperties::FLAG_SCRIPTED));
        getChild<LLCheckBoxCtrl>("check_light")->setValue(rec.hasFlag(ALObjectProperties::FLAG_LIGHT));
        getChild<LLCheckBoxCtrl>("check_physics")->setValue(rec.hasFlag(ALObjectProperties::FLAG_PHYSICS));
        getChild<LLCheckBoxCtrl>("check_phantom")->setValue(rec.hasFlag(ALObjectProperties::FLAG_PHANTOM));
        getChild<LLCheckBoxCtrl>("check_temp")->setValue(rec.hasFlag(ALObjectProperties::FLAG_TEMPORARY));
    }
    // Suppress everything already conveyed elsewhere in the pane: the five
    // checkboxes above, and the per-face columns (glow / fullbright / alpha /
    // PBR) in the materials list below.
    constexpr U32 SHOWN_ELSEWHERE = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_LIGHT
        | ALObjectProperties::FLAG_PHYSICS | ALObjectProperties::FLAG_PHANTOM
        | ALObjectProperties::FLAG_TEMPORARY
        | ALObjectProperties::FLAG_GLOW | ALObjectProperties::FLAG_FULLBRIGHT
        | ALObjectProperties::FLAG_ALPHA | ALObjectProperties::FLAG_PBR_MATERIAL
        | ALObjectProperties::FLAG_AVATAR;
    const std::string extra_flags = ALObjectProperties::flagsToString(rec.mFlags & ~SHOWN_ELSEWHERE);
    show("detail_flags_extra", show_flags && !extra_flags.empty());
    if (!extra_flags.empty())
        set_text("detail_flags_extra", "Also: " + extra_flags);

    // Per-face material/texture list (live objects only); asset ids are
    // perm-gated like the build floater's copy checks. Only rebuilt when the
    // selection changes — the periodic detail refresh must not reset the
    // user's scroll position mid-read.
    getChild<LLView>("detail_faces_layout")->setVisible(obj != nullptr);
    if (!obj)
    {
        faces->deleteAllItems();
        mLastFacesID.setNull();
    }
    else if (id != mLastFacesID)
    {
        mLastFacesID = id;
        faces->deleteAllItems();
        const U8 num_tes = obj->getNumTEs();
        for (U8 i = 0; i < num_tes; ++i)
        {
            const LLTextureEntry* te = obj->getTE(i);
            if (!te)
                continue;

            std::string material;
            const LLUUID& mat_id = obj->getRenderMaterialID(i);
            if (mat_id.notNull())
            {
                material = "PBR " + assetIdForDisplay(mat_id);
            }
            else
            {
                material = "tex " + assetIdForDisplay(te->getID());
                if (const LLMaterialPtr mat = te->getMaterialParams())
                {
                    if (mat->getNormalID().notNull())
                        material += " +norm";
                    if (mat->getSpecularID().notNull())
                        material += " +spec";
                }
            }

            const F32 alpha = te->getColor().mV[VALPHA];
            LLSD row;
            row["columns"][0]["column"] = "face";
            row["columns"][0]["value"]  = (S32)i;
            row["columns"][1]["column"] = "material";
            row["columns"][1]["value"]  = material;
            row["columns"][2]["column"] = "alpha";
            row["columns"][2]["value"]  = (alpha < 0.999f) ? llformat("%.2f", alpha) : std::string();
            row["columns"][3]["column"] = "glow";
            row["columns"][3]["value"]  = (te->getGlow() > 0.f) ? llformat("%.2f", te->getGlow()) : std::string();
            row["columns"][4]["column"] = "bright";
            row["columns"][4]["value"]  = te->getFullbright() ? std::string("Y") : std::string();
            faces->addElement(row);
        }
    }
}

namespace
{
    ALSceneExplorerSort::ESortMode sortModeFromParam(const LLSD& param)
    {
        const std::string key = param.asString();
        if (key == "name")          return ALSceneExplorerSort::SORT_NAME;
        if (key == "land_impact")   return ALSceneExplorerSort::SORT_LAND_IMPACT;
        if (key == "triangles")     return ALSceneExplorerSort::SORT_TRIANGLES;
        if (key == "type")          return ALSceneExplorerSort::SORT_TYPE;
        if (key == "region_origin") return ALSceneExplorerSort::SORT_REGION_ORIGIN;
        return ALSceneExplorerSort::SORT_DISTANCE;
    }
}

void ALFloaterSceneExplorer::setSortMode(const LLSD& param)
{
    const ALSceneExplorerSort::ESortMode mode = sortModeFromParam(param);
    mViewModel.getSorter().setMode(mode);
    gSavedSettings.setU32("ALSceneExplorerSortOrder", (U32)mode);

    // Choosing the land-impact key is explicit demand for every row's cost
    // (the order is meaningless while they read 0). The requests run through
    // the existing GetObjectCost pipeline, chunked and deduplicated there.
    if (mode == ALSceneExplorerSort::SORT_LAND_IMPACT)
    {
        for (const auto& entry : mItems)
        {
            requestCostsFor(entry.second);
        }
    }

    mViewModel.requestSortAll();
    if (mTree)
        mTree->arrangeAll();
}

bool ALFloaterSceneExplorer::checkSortMode(const LLSD& param) const
{
    return mViewModel.getSorter().getMode() == sortModeFromParam(param);
}

void ALFloaterSceneExplorer::toggleShow(const LLSD& param)
{
    const std::string key = param.asString();
    if (key == "avatars")
    {
        mShowAvatars = !mShowAvatars;
        gSavedSettings.setBOOL("ALSceneExplorerShowAvatars", mShowAvatars);
        reconcile(); // prunes / restores the avatar rows immediately
    }
    else if (key == "derendered")
    {
        mShowDerendered = !mShowDerendered;
        gSavedSettings.setBOOL("ALSceneExplorerShowDerendered", mShowDerendered);
        syncDerendered();
        updateCategoryCounts();
    }
    else if (key == "full_region")
    {
        mFullRegion = !mFullRegion;
        gSavedSettings.setBOOL("ALSceneExplorerFullRegion", mFullRegion);
        if (mFullRegion)
        {
            // The load is the feature, but the user should know they are
            // asking the simulator for it ("don't show again" supported).
            LLNotificationsUtil::add("SceneExplorerFullRegion");
        }
        applyFullRegionMode(mFullRegion);
    }
    else if (key == "selection_sync")
    {
        mSelectionSync = !mSelectionSync;
        gSavedSettings.setBOOL("ALSceneExplorerSelectionSync", mSelectionSync);
    }
    else if (key == "owners")
    {
        gSavedSettings.setBOOL("ALSceneExplorerOwnerSuffix",
                               !gSavedSettings.getBOOL("ALSceneExplorerOwnerSuffix"));
        // Refresh what's on screen now; scrolled-in rows pick the change up
        // from the periodic visible-row scan.
        forEachVisibleRow([](ALSceneExplorerItem*, LLFolderViewItem* widget)
        {
            widget->refresh();
        });
        if (mTree)
            mTree->arrangeAll();
    }
}

bool ALFloaterSceneExplorer::checkShow(const LLSD& param) const
{
    const std::string key = param.asString();
    if (key == "avatars")
        return mShowAvatars;
    if (key == "derendered")
        return mShowDerendered;
    if (key == "full_region")
        return mFullRegion;
    if (key == "selection_sync")
        return mSelectionSync;
    if (key == "owners")
        return gSavedSettings.getBOOL("ALSceneExplorerOwnerSuffix");
    return false;
}

// ============================================================================
// Full-region (360) coverage + scalability
// ============================================================================
void ALFloaterSceneExplorer::applyFullRegionMode(bool active)
{
    if (active)
    {
        // Remember what to restore; LLAgent re-applies the agent-level mode
        // to new regions after crossings, and reconcile() re-claims it if the
        // 360 capture floater restores the mode out from under us.
        mPrevILMode = gAgent.getInterestListMode();
        if (gAgent.getInterestListMode() != IL_MODE_360)
        {
            gAgent.changeInterestListMode(IL_MODE_360);
        }
        return;
    }

    // Releasing: never clobber another driver of the mode. If the 360 capture
    // floater is up it owns 360 for as long as it lives; if someone already
    // switched the mode away, there is nothing of ours to restore.
    if (gAgent.getInterestListMode() != IL_MODE_360)
        return;
    if (LLFloaterReg::findInstance("360capture"))
        return;
    // A saved mode of 360 means something else had it on when we enabled —
    // restoring "360" would strand the stream on, so fall back to default.
    const std::string restore_mode =
        (!mPrevILMode.empty() && mPrevILMode != IL_MODE_360) ? mPrevILMode : IL_MODE_DEFAULT;
    gAgent.changeInterestListMode(restore_mode);
}

void ALFloaterSceneExplorer::requestCostsFor(ALSceneExplorerItem* item)
{
    // One-shot demand trigger: a failed fetch leaves the cost stale forever,
    // so without the guard every read would re-request it (the Stage-0 loop).
    if (!item || item->wasCostRequested()
        || item->isContainer() || item->isDerenderedType()
        || item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR)
    {
        return;
    }
    LLViewerObject* obj = gObjectList.findObject(item->getUUID());
    if (!obj || obj->isDead())
        return;
    if (obj->peekLinksetCost() > 0.f)
        return; // already costed (the reconcile peek keeps the record fresh)
    item->noteCostRequested();
    obj->getObjectCost();
    obj->getLinksetCost();
}

void ALFloaterSceneExplorer::forEachVisibleRow(
    const std::function<void(ALSceneExplorerItem*, LLFolderViewItem*)>& fn)
{
    if (!mTree || !mTreePanel || mItems.empty())
        return;

    const LLRect viewport = mTreePanel->calcScreenRect();
    for (const auto& entry : mItems)
    {
        ALSceneExplorerItem* item = entry.second;
        if (item->isContainer())
            continue;
        auto wit = mWidgets.find(entry.first);
        if (wit == mWidgets.end())
            continue;
        LLFolderViewItem* widget = wit->second;
        if (!widget->getVisible() || !widget->isInVisibleChain())
            continue;
        if (!viewport.overlaps(widget->calcScreenRect()))
            continue;
        fn(item, widget);
    }
}

void ALFloaterSceneExplorer::scanVisibleRows()
{
    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    forEachVisibleRow([this, &cache](ALSceneExplorerItem* item, LLFolderViewItem* widget)
    {
        // Keep on-screen label suffixes (distance, complexity, LI) current —
        // they are captured at widget refresh and would otherwise go stale as
        // the agent moves.
        widget->refresh();

        if (item->isDerenderedType())
            return;

        // Viewport-bounded cost demand: what the user is looking at gets its
        // LI without costing the whole region.
        requestCostsFor(item);

        // Bump this row's queued props request ahead of the off-screen
        // backlog (no-op unless it is still waiting in the main queue).
        const LLUUID& id = item->getUUID();
        const ALObjectPropertiesCache::ServerProps* p = cache.get(id);
        if (mQueuedProps.count(id) && !(p && p->mHasFullData)
            && mPriorityQueued.insert(id).second)
        {
            mPriorityFetch.push_back(id);
        }
    });
}

void ALFloaterSceneExplorer::updateStatusText()
{
    // Transient pipeline feedback; quiet when there is nothing in flight.
    std::string status;
    if (const size_t building = mBuildQueue.size())
    {
        status = llformat("Loading %d objects...", (S32)building);
    }
    else if (const size_t refilling = mRefillQueue.size())
    {
        status = llformat("Refreshing %d objects...", (S32)refilling);
    }
    else if (const size_t fetching = mFetchQueue.size() + mPriorityFetch.size())
    {
        status = llformat("Fetching details: %d...", (S32)fetching);
    }
    else if (mItems.size() > 30000)
    {
        status = llformat("%d rows - large region", (S32)mItems.size());
    }

    if (status != mLastStatus)
    {
        mLastStatus = status;
        getChild<LLTextBox>("status_text")->setText(status);
    }
}

// ============================================================================
// Actions
// ============================================================================
ALSceneExplorerItem* ALFloaterSceneExplorer::getSelectedItem() const
{
    if (!mTree)
        return nullptr;
    LLFolderViewItem* sel = mTree->getCurSelectedItem();
    return sel ? static_cast<ALSceneExplorerItem*>(sel->getViewModelItem()) : nullptr;
}

// Every selected scene row (containers excluded), in tree selection order.
std::vector<ALSceneExplorerItem*> ALFloaterSceneExplorer::getSelectedSceneItems() const
{
    std::vector<ALSceneExplorerItem*> items;
    if (!mTree)
        return items;
    for (LLFolderViewItem* widget : mTree->getSelectedItems())
    {
        ALSceneExplorerItem* item =
            widget ? static_cast<ALSceneExplorerItem*>(widget->getViewModelItem()) : nullptr;
        if (item && !item->isContainer())
            items.push_back(item);
    }
    return items;
}

LLViewerObject* ALFloaterSceneExplorer::getSelectedObject() const
{
    ALSceneExplorerItem* item = getSelectedItem();
    return item ? gObjectList.findObject(item->getUUID()) : nullptr;
}

void ALFloaterSceneExplorer::selectInWorld(const uuid_vec_t& ids)
{
    std::vector<LLViewerObject*> objs;
    objs.reserve(ids.size());
    for (const LLUUID& id : ids)
    {
        if (LLViewerObject* o = gObjectList.findObject(id))
            objs.push_back(o);
    }

    LLSelectMgr* sm = LLSelectMgr::getInstance();
    mSyncingSelection = true;
    sm->deselectAll();

    // Mirror LLToolSelect's EditLinkedParts split exactly: with "Edit linked"
    // on, select the individual prim; otherwise always the whole linkset.
    // Selecting a lone child while the build tools are in whole-object mode
    // (or vice versa) desyncs the manipulators from what the user expects.
    // Also mirror its RLVa gate: while the build tools are up, objects the
    // user may not edit are refused, exactly as clicking them in-world is.
    static LLCachedControl<bool> edit_linked(gSavedSettings, "EditLinkedParts", false);
    const bool rlv_gate_edit = RlvActions::isRlvEnabled() && LLFloaterReg::instanceVisible("build");
    for (LLViewerObject* obj : objs)
    {
        if (rlv_gate_edit && !obj->isAvatar() && !RlvActions::canEdit(obj))
            continue;
        if (edit_linked && !obj->isAvatar())
        {
            sm->selectObjectOnly(obj, SELECT_ALL_TES);
        }
        else
        {
            sm->selectObjectAndFamily(obj, /*add_to_end=*/true);
        }
    }
    mSyncingSelection = false;
}

void ALFloaterSceneExplorer::syncSelectionToWorld()
{
    if (mSyncingSelection || !mSelectionSync)
        return;
    // Drive the in-world selection only while an editing surface is up. Plain
    // browsing must never grab the user's selection — pushing one has side
    // effects (avatar look-at/point-at targeting, edit-mode behaviour) that
    // read as the camera/avatar reacting to every tree click. The sync state
    // deliberately doesn't advance while gated, so opening Build/Inspect
    // adopts the currently selected rows once.
    if (!LLFloaterReg::instanceVisible("build") && !LLFloaterReg::instanceVisible("inspect"))
        return;

    uuid_vec_t ids;
    for (ALSceneExplorerItem* item : getSelectedSceneItems())
    {
        if (item->isDerenderedType())
            continue;
        if (gObjectList.findObject(item->getUUID()))
            ids.push_back(item->getUUID());
    }
    std::sort(ids.begin(), ids.end());
    if (ids == mLastPushedSelection)
        return;
    mLastPushedSelection = ids;
    if (!ids.empty())
        selectInWorld(ids);
}

void ALFloaterSceneExplorer::onWorldSelectionChanged()
{
    // Fires for every selection update (including per-frame property updates
    // while editing) — just flag; idleUpdate processes once per pass.
    mWorldSelectionDirty = true;
}

void ALFloaterSceneExplorer::syncSelectionFromWorld()
{
    // The sync toggle silences the passive mirroring in both directions;
    // explicit actions (Edit / Inspect / menu staging) still select.
    if (mSyncingSelection || !mSelectionSync || !mTree)
        return;
    // Mirror only while an editing surface is up, matching the tree->world
    // direction. While plain browsing, transient world selections (pie menus,
    // the gear menu's own staging — which selects the whole family and would
    // bounce the tree from a child row to its root) must not yank the tree's
    // selection or scroll position.
    if (!LLFloaterReg::instanceVisible("build") && !LLFloaterReg::instanceVisible("inspect"))
        return;

    // Mirror the in-world selection onto the tree at the granularity it was
    // made: selection roots (an individually selected child prim is its own
    // root node). Only ids the tree actually holds participate.
    uuid_vec_t ids;
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    for (LLObjectSelection::valid_root_iterator it = selection->valid_root_begin();
         it != selection->valid_root_end(); ++it)
    {
        LLViewerObject* obj = (*it)->getObject();
        if (obj && mWidgets.count(obj->getID()))
            ids.push_back(obj->getID());
    }
    // Never clear the tree's selection because the world's emptied (closing
    // the build floater deselects, but the user's place in the tree remains).
    if (ids.empty())
        return;
    std::sort(ids.begin(), ids.end());
    if (ids == mLastPushedSelection)
        return; // our own push echoing back through mUpdateSignal
    mLastPushedSelection = ids;

    mSyncingSelection = true;
    mTree->clearSelection();
    bool first = true;
    for (const LLUUID& id : ids)
    {
        auto wit = mWidgets.find(id);
        if (wit == mWidgets.end())
            continue;
        if (first)
        {
            // Open ancestors and scroll so the row is actually seen; don't
            // steal keyboard focus from the world the user is clicking in.
            mTree->setSelection(wit->second, /*openitem=*/true, /*take_keyboard_focus=*/false);
            first = false;
        }
        else
        {
            mTree->changeSelection(wit->second, true);
        }
    }
    mTree->scrollToShowSelection();
    mSyncingSelection = false;
}

void ALFloaterSceneExplorer::doSelectAllResults()
{
    if (!mTree)
        return;
    // Select every root-level row that passes the current filter (linkset
    // granularity — child prims follow their roots through selectInWorld),
    // feeding the batch menu actions (Take Copy / Return / Derender).
    mSyncingSelection = true;
    mTree->clearSelection();
    bool first = true;
    auto select_category = [&](ALSceneExplorerItem* category)
    {
        if (!category)
            return;
        for (auto it = category->getChildrenBegin(); it != category->getChildrenEnd(); ++it)
        {
            ALSceneExplorerItem* child = static_cast<ALSceneExplorerItem*>(it->get());
            if (!child->passedFilter())
                continue;
            auto wit = mWidgets.find(child->getUUID());
            if (wit == mWidgets.end())
                continue;
            if (first)
            {
                mTree->setSelection(wit->second, false, true);
                first = false;
            }
            else
            {
                mTree->changeSelection(wit->second, true);
            }
        }
    };
    select_category(mObjectsCategory);
    select_category(mAvatarsCategory);
    mSyncingSelection = false;
    // The next idle pass pushes the new multi-selection to the world if an
    // editing surface is up (and the gear menu pushes it at open regardless).
}

void ALFloaterSceneExplorer::doRefresh()
{
    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    mRefillQueue.clear();
    for (const auto& entry : mItems)
    {
        ALSceneExplorerItem* item = entry.second;
        const ALSceneExplorerItem::EItemType type = item->getItemType();
        if (type == ALSceneExplorerItem::TYPE_AVATAR
            || type == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
            || item->isDerenderedType()
            || item->isContainer())
        {
            continue;
        }
        // Re-arm the bounded retry and the one-shot cost demand, and
        // re-request anything that never resolved (assume the reply is lost).
        item->resetFetchState();
        const ALObjectPropertiesCache::ServerProps* p = cache.get(entry.first);
        if (!(p && p->mHasFullData))
        {
            cache.clearPending(entry.first);
            queueProps(entry.first);
        }
        // Local fields (per-face flags, geometry, prim counts) go stale after
        // node build; re-fill them time-sliced so filters see current state.
        mRefillQueue.push_back(entry.first);
    }

    // Rows on screen get a forced re-request even when data is cached, so a
    // rename or permission change made while standing here shows up — without
    // re-probing the whole (possibly 360-streamed) region.
    forEachVisibleRow([this, &cache](ALSceneExplorerItem* item, LLFolderViewItem*)
    {
        if (item->isDerenderedType()
            || item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR
            || item->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT)
        {
            return;
        }
        const LLUUID& id = item->getUUID();
        if (cache.isPending(id) || mQueuedProps.count(id))
            return;
        if (mPriorityQueued.insert(id).second)
            mPriorityFetch.push_back(id);
    });

    reconcile();
}

void ALFloaterSceneExplorer::activateItem(const LLUUID& id)
{
    // Act on the activated item itself, not on whatever the tree currently has
    // selected — double-click paths can fire without a selection change.
    LLViewerObject* obj = gObjectList.findObject(id);
    if (!obj)
        return;
    uuid_vec_t ids;
    ids.push_back(id);
    mLastPushedSelection = ids; // keep the per-frame selection sync from re-selecting
    selectInWorld(ids);

    switch (gSavedSettings.getU32("ALSceneExplorerActivateAction"))
    {
    case 1:
        // Same RLVa edit gate as the in-world edit paths.
        if (!RlvActions::isRlvEnabled() || RlvActions::canEdit(obj))
        {
            openBuildTools();
        }
        break;
    case 2: LLFloaterReg::showInstance("inspect");  break;
    case 3:                                         break; // select only
    case 0:
    default:
        // Focus zooms the camera only while browsing freely. With Build or
        // Inspect up the user is editing — double-click means "retarget the
        // selection", and yanking the camera mid-edit is jarring (the explicit
        // Focus button / context entry still zooms regardless).
        if (!LLFloaterReg::instanceVisible("build") && !LLFloaterReg::instanceVisible("inspect"))
        {
            handle_zoom_to_object(id);
        }
        break;
    }
}

void ALFloaterSceneExplorer::openBuildTools()
{
    LLFloaterReg::showInstance("build");
    LLToolMgr::getInstance()->setCurrentToolset(gBasicToolset);
    if (gFloaterTools)
        gFloaterTools->setEditTool(LLToolCompTranslate::getInstance());
}

void ALFloaterSceneExplorer::doFocus()
{
    if (LLViewerObject* obj = getSelectedObject())
    {
        // Frames the object from the bounding box / FOV like the standard
        // "Zoom To" everywhere else, instead of a fixed-offset camera jump.
        handle_zoom_to_object(obj->getID());
    }
}

void ALFloaterSceneExplorer::doEdit()
{
    LLViewerObject* obj = getSelectedObject();
    if (!obj)
        return;
    // The menu's edit path enforces RLVa @edit; entering build mode from the
    // explorer must not bypass it.
    if (RlvActions::isRlvEnabled() && !RlvActions::canEdit(obj))
        return;
    uuid_vec_t ids;
    ids.push_back(obj->getID());
    selectInWorld(ids);
    openBuildTools();
}

void ALFloaterSceneExplorer::doInspect()
{
    LLViewerObject* obj = getSelectedObject();
    if (!obj)
        return;
    uuid_vec_t ids;
    ids.push_back(obj->getID());
    selectInWorld(ids);
    LLFloaterReg::showInstance("inspect");
}

void ALFloaterSceneExplorer::doTeleport()
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item)
        return;
    if (LLViewerObject* obj = gObjectList.findObject(item->getUUID()))
    {
        gAgent.teleportViaLocation(obj->getPositionGlobal());
        return;
    }
    // Derendered objects keep their stored position.
    if (item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_OBJECT)
    {
        const LLVector3d& pos = item->getRecord().mPosGlobal;
        if (!pos.isExactlyZero())
            gAgent.teleportViaLocation(pos);
    }
}

void ALFloaterSceneExplorer::doSit()
{
    LLViewerObject* obj = getSelectedObject();
    if (!obj || obj->isAvatar())
        return;
    // The shared sit handler carries the RLVa / already-sitting handling.
    handle_object_sit(obj->getID());
}

void ALFloaterSceneExplorer::doCopy(const LLSD& param)
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item || item->isContainer())
        return;

    const ALObjectProperties::Record& rec = item->getRecord();
    const std::string what = param.asString();
    std::string out;
    if (what == "name")
    {
        out = item->getName(); // avatar rows are already RLVa-anonymized
    }
    else if (what == "uuid")
    {
        out = item->getUUID().asString();
    }
    else if (what == "pos")
    {
        out = llformat("<%.1f, %.1f, %.1f>",
                       rec.mPosRegion.mV[VX], rec.mPosRegion.mV[VY], rec.mPosRegion.mV[VZ]);
    }
    else if (what == "slurl")
    {
        // The menu disables this under RLVa @showloc; double-check anyway.
        if (RlvActions::isRlvEnabled() && !RlvActions::canShowLocation())
            return;
        LLViewerRegion* region = gAgent.getRegion();
        if (!region || rec.mPosGlobal.isExactlyZero())
            return;
        out = LLSLURL(region->getName(), rec.mPosGlobal).getSLURLString();
    }
    if (out.empty())
        return;

    const LLWString wout = utf8str_to_wstring(out);
    LLClipboard::instance().copyToClipboard(wout, 0, (S32)wout.size());
}

void ALFloaterSceneExplorer::doCopyResults()
{
    // Export the rows that pass the current filter (linkset granularity) as
    // CSV for offline auditing. Owners resolve to a name only when the name
    // cache already has it; otherwise the UUID still identifies them.
    std::string out = "Name,Owner,Land Impact,Triangles,Distance (m),UUID\n";
    auto csv_escape = [](std::string s)
    {
        LLStringUtil::replaceString(s, "\"", "\"\"");
        return "\"" + s + "\"";
    };

    S32 rows = 0;
    auto append_category = [&](ALSceneExplorerItem* category)
    {
        if (!category)
            return;
        for (auto it = category->getChildrenBegin(); it != category->getChildrenEnd(); ++it)
        {
            ALSceneExplorerItem* child = static_cast<ALSceneExplorerItem*>(it->get());
            if (!child->passedFilter())
                continue;
            const ALObjectProperties::Record& rec = child->getRecord();

            std::string owner;
            if (child->getItemType() == ALSceneExplorerItem::TYPE_AVATAR)
            {
                owner = child->getName();
            }
            else if (rec.mPropsValid)
            {
                // The resolved-owner map covers groups and avatars alike;
                // RLVa-hidden avatar owners fall through to the anonymized
                // display name, anything still unresolved to the bare id.
                const LLUUID& owner_id = rec.mGroupOwned ? rec.mGroupId : rec.mOwnerId;
                auto found = mOwnerNames.find(owner_id);
                if (found != mOwnerNames.end() && !found->second.mName.empty())
                    owner = found->second.mName;
                else if (!rec.mGroupOwned)
                    owner = avatarDisplayName(rec.mOwnerId);
                if (owner.empty())
                    owner = owner_id.asString();
            }

            out += csv_escape(child->getName()) + "," + csv_escape(owner) + ","
                + (rec.mLandImpact > 0.f ? llformat("%.0f", rec.mLandImpact) : std::string())
                + "," + (rec.mNumTriangles > 0 ? llformat("%u", rec.mNumTriangles) : std::string())
                + "," + llformat("%.1f", rec.mDistance)
                + "," + child->getUUID().asString() + "\n";
            ++rows;
        }
    };
    append_category(mObjectsCategory);
    append_category(mAvatarsCategory);
    append_category(mDerenderedCategory);
    if (!rows)
        return;

    const LLWString wout = utf8str_to_wstring(out);
    LLClipboard::instance().copyToClipboard(wout, 0, (S32)wout.size());
}

void ALFloaterSceneExplorer::doShowOnMap()
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item || item->isContainer())
        return;
    if (RlvActions::isRlvEnabled() && !RlvActions::canShowLocation())
        return;

    if (item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR)
    {
        LLAvatarActions::showOnMap(item->getUUID());
        return;
    }
    const LLVector3d& pos = item->getRecord().mPosGlobal;
    if (pos.isExactlyZero())
        return;
    LLTracker::trackLocation(pos, item->getName(), LLStringUtil::null, LLTracker::LOCATION_ITEM);
    mBeaconTrackedID = item->getUUID();
    LLFloaterReg::showInstance("world_map", "center");
}

void ALFloaterSceneExplorer::doBeacon()
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item || item->isContainer())
        return;

    // Toggle: a second use on the same row clears its beacon.
    if (mBeaconTrackedID == item->getUUID())
    {
        LLTracker::stopTracking(false);
        mBeaconTrackedID.setNull();
        return;
    }
    const LLVector3d& pos = item->getRecord().mPosGlobal;
    if (pos.isExactlyZero())
        return;
    LLTracker::trackLocation(pos, item->getName(), LLStringUtil::null, LLTracker::LOCATION_ITEM);
    mBeaconTrackedID = item->getUUID();
}

void ALFloaterSceneExplorer::doBlockOwner()
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item || item->isContainer())
        return;
    const ALObjectProperties::Record& rec = item->getRecord();
    if (!rec.mPropsValid || rec.mGroupOwned
        || rec.mOwnerId.isNull() || rec.mOwnerId == gAgentID)
    {
        return;
    }
    LLAvatarActions::toggleBlock(rec.mOwnerId);
}

void ALFloaterSceneExplorer::doAvatarAction(const LLSD& param)
{
    ALSceneExplorerItem* item = getSelectedItem();
    if (!item)
        return;
    const LLUUID id = item->getUUID();
    if (id.isNull())
        return;

    // All of these carry their own permission / RLVa gating and confirmation
    // dialogs; the menu only decides visibility.
    const std::string action = param.asString();
    if      (action == "profile")        LLAvatarActions::showProfile(id);
    else if (action == "im")             LLAvatarActions::startIM(id);
    else if (action == "offer_tp")       LLAvatarActions::offerTeleport(id);
    else if (action == "request_tp")     LLAvatarActions::teleportRequest(id);
    else if (action == "zoom")           ALAvatarActions::zoomIn(id);
    else if (action == "teleport_to")    ALAvatarActions::teleportTo(id);
    else if (action == "freeze")         ALAvatarActions::parcelFreeze(id);
    else if (action == "eject")          ALAvatarActions::parcelEject(id);
    else if (action == "estate_tp_home") ALAvatarActions::estateTeleportHome(id);
    else if (action == "estate_kick")    ALAvatarActions::estateKick(id);
    else if (action == "estate_ban")     ALAvatarActions::estateBan(id);
    else if (action == "block")          LLAvatarActions::toggleBlock(id);
    else if (action == "report")         ALAvatarActions::reportAbuse(id);
}

// ============================================================================
// Context / gear menu
// ============================================================================
void ALFloaterSceneExplorer::onGearMouseDown()
{
    // Mouse-down fires before LLMenuButton::toggleMenu shows the menu, so
    // this is the per-open hook to mirror the right-click popup's state.
    LLToggleableMenu* menu = getChild<LLMenuButton>("gear_btn")->getMenu();
    if (!menu)
        return;
    resetMenuEntries(*menu);
    buildRowContextMenu(*menu, 0);
}

void ALFloaterSceneExplorer::buildRowContextMenu(LLMenuGL& menu, U32 flags)
{
    ALSceneExplorerItem* item = getSelectedItem();

    std::vector<std::string> show;
    std::vector<std::string> disabled;

    // Structural rows (and no selection at all): tree-level utilities only.
    if (!item || item->isContainer())
    {
        show = { "refresh", "copy_results" };
        hideMenuEntries(menu, show, disabled);
        return;
    }

    // Multi-selection: only the naturally batching entries. Everything is
    // staged into the world selection so the reused handlers (Take Copy,
    // Return) and their enable predicates act on the full set; Derender and
    // Restore batch through their own handlers. Single-target entries
    // (Touch / Pay / Profile / ...) don't appear at all.
    const std::vector<ALSceneExplorerItem*> selected = getSelectedSceneItems();
    if (selected.size() > 1)
    {
        bool all_derendered = true;
        uuid_vec_t ids;
        for (ALSceneExplorerItem* sel_item : selected)
        {
            all_derendered &= sel_item->isDerenderedType();
            if (!sel_item->isDerenderedType()
                && sel_item->getItemType() != ALSceneExplorerItem::TYPE_AVATAR
                && gObjectList.findObject(sel_item->getUUID()))
            {
                ids.push_back(sel_item->getUUID());
            }
        }

        if (all_derendered)
        {
            show = { "restore_derendered" };
            hideMenuEntries(menu, show, disabled);
            return;
        }

        if (!ids.empty())
        {
            uuid_vec_t sorted = ids;
            std::sort(sorted.begin(), sorted.end());
            mLastPushedSelection = sorted;
            selectInWorld(ids);

            show = { "take_copy", "sep_derender", "derender_temp", "derender_perm" };
            if (!registryEnabled("Tools.EnableTakeCopy"))
                disabled.push_back("take_copy");
            if (registryEnabled("Object.EnableReturn"))
                show.insert(show.begin() + 1, { "sep_admin", "return" });
        }
        hideMenuEntries(menu, show, disabled);
        return;
    }

    const LLUUID id = item->getUUID();
    const ALObjectProperties::Record& rec = item->getRecord();
    const bool rlv = RlvActions::isRlvEnabled();
    const bool can_show_location = !rlv || RlvActions::canShowLocation();

    if (item->isDerenderedType())
    {
        show = { "restore_derendered", "sep_derender", "sep_copy", "copy_menu", "copy_name", "copy_uuid" };
        if (item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_OBJECT)
        {
            show.insert(show.end(), { "teleport", "show_map", "beacon", "copy_pos", "copy_slurl" });
            if (!can_show_location)
                disabled.insert(disabled.end(), { "show_map", "copy_slurl" });
        }
        hideMenuEntries(menu, show, disabled);
        return;
    }

    if (item->getItemType() == ALSceneExplorerItem::TYPE_AVATAR)
    {
        const bool is_self = (id == gAgentID);
        show = { "focus", "av_zoom", "av_teleport", "show_map", "beacon",
                 "sep_copy", "copy_menu", "copy_name", "copy_uuid", "copy_pos", "copy_slurl",
                 "filter_by_owner" };
        if (!ALAvatarActions::canZoomIn(id))
            disabled.push_back("av_zoom");
        if (!ALAvatarActions::canTeleportTo(id))
            disabled.push_back("av_teleport");
        if (!can_show_location)
            disabled.insert(disabled.end(), { "show_map", "copy_slurl" });

        if (is_self)
        {
            // Social / moderation actions make no sense on yourself.
            show.insert(show.begin(), { "av_profile", "sep_avatar_actions" });
        }
        else
        {
            show.insert(show.begin(), { "av_profile", "av_im", "av_offer_tp", "av_request_tp",
                                        "sep_avatar_actions" });
            if (!LLAvatarActions::canOfferTeleport(id))
                disabled.push_back("av_offer_tp");

            // Admin entries are hidden outright without the matching power.
            if (ALAvatarActions::canFreezeEject(id))
                show.insert(show.end(), { "sep_admin", "freeze", "eject" });
            if (ALAvatarActions::canManageAvatarsEstate(id))
                show.insert(show.end(), { "sep_admin", "estate_home", "estate_kick", "estate_ban" });

            show.insert(show.end(), { "sep_moderation", "av_block", "av_report" });
            if (!LLAvatarActions::canBlock(id))
                disabled.push_back("av_block");
        }
        hideMenuEntries(menu, show, disabled);
        return;
    }

    // Live object rows (world linkset / child prim / attachment root).
    LLViewerObject* obj = gObjectList.findObject(id);
    if (!obj || obj->isDead())
    {
        // Row went stale between reconciles; offer the inert copies only.
        show = { "copy_menu", "copy_name", "copy_uuid", "copy_pos" };
        hideMenuEntries(menu, show, disabled);
        return;
    }

    // Stage the row as the live selection so the reused viewer handlers
    // (Object.Touch / Object.Return / PayObject / ...) and their registered
    // enable predicates act on it — the same model as the in-world
    // right-click, which also selects what it targets.
    uuid_vec_t ids;
    ids.push_back(id);
    mLastPushedSelection = ids;
    selectInWorld(ids);

    const bool is_attachment = obj->isAttachment();

    show = { "touch", "open", "buy", "pay", "take_copy", "sep_object_actions",
             "focus", "edit", "inspect", "teleport", "show_map", "beacon",
             "sep_copy", "copy_menu", "copy_name", "copy_uuid", "copy_pos", "copy_slurl",
             "filter_by_owner" };
    if (!is_attachment)
    {
        show.insert(show.begin() + 1, "sit");
        if (registryEnabled("Object.EnableReturn"))
            show.insert(show.end(), { "sep_admin", "return" });
        show.insert(show.end(), { "sep_derender", "derender_temp", "derender_perm" });
    }
    show.insert(show.end(), { "sep_moderation", "block_object", "block_owner", "report" });

    if (!(obj->flagHandleTouch() && (!rlv || RlvActions::canTouch(obj))))
        disabled.push_back("touch");
    if (rlv && !RlvActions::canSit(obj))
        disabled.push_back("sit");
    if (!registryEnabled("Object.EnableOpen"))
        disabled.push_back("open");
    if (!registryEnabled("Object.EnableBuy"))
        disabled.push_back("buy");
    if (!registryEnabled("EnablePayObject"))
        disabled.push_back("pay");
    if (!registryEnabled("Tools.EnableTakeCopy"))
        disabled.push_back("take_copy");
    if (rlv && !RlvActions::canEdit(obj))
        disabled.push_back("edit");
    if (!registryEnabled("Object.EnableMute"))
        disabled.push_back("block_object");
    if (!(rec.mPropsValid && !rec.mGroupOwned
          && rec.mOwnerId.notNull() && rec.mOwnerId != gAgentID))
        disabled.push_back("block_owner");
    if (!registryEnabled("Object.EnableReportAbuse"))
        disabled.push_back("report");
    if (!(rec.mPropsValid && (rec.mOwnerId.notNull() || rec.mGroupId.notNull())))
        disabled.push_back("filter_by_owner");
    if (!ALDerenderList::canAdd(obj))
        disabled.insert(disabled.end(), { "derender_temp", "derender_perm" });
    if (!can_show_location)
        disabled.insert(disabled.end(), { "show_map", "copy_slurl" });

    hideMenuEntries(menu, show, disabled);
}

void ALFloaterSceneExplorer::doDerender(const LLSD& param)
{
    // Every selected derenderable object row (multi-select batches).
    std::vector<LLViewerObject*> objs;
    for (ALSceneExplorerItem* item : getSelectedSceneItems())
    {
        if (item->isDerenderedType())
            continue;
        LLViewerObject* obj = gObjectList.findObject(item->getUUID());
        if (!obj || obj->isAvatar() || obj->isAttachment())
            continue;
        if (!ALDerenderList::canAdd(obj))
            continue;
        objs.push_back(obj);
    }
    if (objs.empty())
        return;

    // ALDerenderList::addSelection() consumes the live selection; always feed
    // it the whole families regardless of the EditLinkedParts split so each
    // entry's root/child local-id bookkeeping is complete.
    LLSelectMgr* sm = LLSelectMgr::getInstance();
    mSyncingSelection = true;
    sm->deselectAll();
    for (LLViewerObject* obj : objs)
    {
        sm->selectObjectAndFamily(obj, /*add_to_end=*/true);
    }
    mSyncingSelection = false;

    if (ALDerenderList::canAddSelection())
    {
        ALDerenderList::instance().addSelection(param.asString() == "permanent");
    }
    sm->deselectAll(); // whatever the kill left behind
    // The change signal already ran syncDerendered(); the live nodes fall out
    // on the next reconcile sweep.
}

void ALFloaterSceneExplorer::doRestore()
{
    // Every selected derendered row (multi-select batches), split by entry
    // type for ALDerenderList's two namespaces.
    uuid_vec_t object_ids;
    uuid_vec_t avatar_ids;
    for (ALSceneExplorerItem* item : getSelectedSceneItems())
    {
        if (item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_OBJECT)
            object_ids.push_back(item->getUUID());
        else if (item->getItemType() == ALSceneExplorerItem::TYPE_DERENDERED_AVATAR)
            avatar_ids.push_back(item->getUUID());
    }
    if (!object_ids.empty())
        ALDerenderList::instance().removeObjects(ALDerenderEntry::TYPE_OBJECT, object_ids);
    if (!avatar_ids.empty())
        ALDerenderList::instance().removeObjects(ALDerenderEntry::TYPE_AVATAR, avatar_ids);
    // removeObjects() requests the objects back from the sim (cache-miss
    // refetch) and fires the change signal, which prunes the rows.
}
