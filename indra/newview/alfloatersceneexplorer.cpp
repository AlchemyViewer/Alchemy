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
#include "llclipboard.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfolderview.h"
#include "llfolderviewitem.h"
#include "llscrollcontainer.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include "alderenderlist.h"
#include "alobjectproperties.h"
#include "llagent.h"
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "llfloaterreg.h"
#include "llfloatertools.h"
#include "llselectmgr.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "llviewercontrol.h"
#include "llviewerjointattachment.h"
#include "llviewermenu.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatar.h"

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

    std::string placeholderName(ALSceneExplorerItem::EItemType type, const LLUUID& id)
    {
        switch (type)
        {
        case ALSceneExplorerItem::TYPE_AVATAR:
        {
            LLAvatarName av_name;
            if (LLAvatarNameCache::get(id, &av_name))
                return av_name.getCompleteName();
            return std::string("(loading avatar)");
        }
        case ALSceneExplorerItem::TYPE_ATTACHMENT:
            return std::string("(attachment)");
        default:
            return std::string("(object)");
        }
    }
}

// ============================================================================
ALFloaterSceneExplorer::ALFloaterSceneExplorer(const LLSD& key)
:   LLFloater(key)
{
}

ALFloaterSceneExplorer::~ALFloaterSceneExplorer()
{
    gIdleCallbacks.deleteFunction(onIdle, this);
    if (mPropsConn.connected())
        mPropsConn.disconnect();
}

bool ALFloaterSceneExplorer::postBuild()
{
    mTreePanel = getChild<LLPanel>("scene_tree");
    buildTree();

    // Shown by the folder view's status text when a filter matches nothing
    // (without it, a zero-hit filter renders a blank pane).
    mViewModel.getFilter().setEmptyLookupMessage(getString("no_matches"));

    mShowAvatars = gSavedSettings.getBOOL("ALSceneExplorerShowAvatars");

    // Push persisted filter/sort state into the controls before wiring the
    // commit callbacks, so the UI, the saved settings, and the filter object
    // all agree from the first frame.
    getChild<LLUICtrl>("show_avatars_check")->setValue(mShowAvatars);
    const U32 flag_mask = gSavedSettings.getU32("ALSceneExplorerFlagFilter");
    getChild<LLUICtrl>("flag_scripted")->setValue((flag_mask & ALObjectProperties::FLAG_SCRIPTED) != 0);
    getChild<LLUICtrl>("flag_light")->setValue((flag_mask & ALObjectProperties::FLAG_LIGHT) != 0);
    getChild<LLUICtrl>("flag_particles")->setValue((flag_mask & ALObjectProperties::FLAG_PARTICLES) != 0);
    getChild<LLUICtrl>("limit_radius_check")->setValue(gSavedSettings.getBOOL("ALSceneExplorerLimitRadius"));
    getChild<LLUICtrl>("radius_slider")->setValue(gSavedSettings.getF32("ALSceneExplorerRadius"));
    getChild<LLComboBox>("owner_combo")->setCurrentByIndex((S32)gSavedSettings.getU32("ALSceneExplorerOwnerFilter"));
    getChild<LLComboBox>("sort_combo")->setCurrentByIndex((S32)gSavedSettings.getU32("ALSceneExplorerSortOrder"));

    getChild<LLFilterEditor>("filter_input")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("owner_combo")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("limit_radius_check")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("radius_slider")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_scripted")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_light")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("flag_particles")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("show_avatars_check")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onFilterChanged, this));
    getChild<LLUICtrl>("sort_combo")->setCommitCallback(boost::bind(&ALFloaterSceneExplorer::onSortChanged, this));

    getChild<LLButton>("refresh_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::reconcile, this));
    getChild<LLButton>("focus_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doFocus, this));
    getChild<LLButton>("edit_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doEdit, this));
    getChild<LLButton>("inspect_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doInspect, this));
    getChild<LLButton>("teleport_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doTeleport, this));
    getChild<LLButton>("copy_id_btn")->setClickedCallback(boost::bind(&ALFloaterSceneExplorer::doCopyID, this));

    // Context-menu commands (menu_scene_explorer.xml, shown by the folder view).
    mCommitCallbackRegistrar.add("SceneExplorer.Focus",    boost::bind(&ALFloaterSceneExplorer::doFocus, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Edit",     boost::bind(&ALFloaterSceneExplorer::doEdit, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Inspect",  boost::bind(&ALFloaterSceneExplorer::doInspect, this));
    mCommitCallbackRegistrar.add("SceneExplorer.Teleport", boost::bind(&ALFloaterSceneExplorer::doTeleport, this));
    mCommitCallbackRegistrar.add("SceneExplorer.CopyID",   boost::bind(&ALFloaterSceneExplorer::doCopyID, this));

    mPropsConn = ALObjectPropertiesCache::instance().setChangeCallback(
        boost::bind(&ALFloaterSceneExplorer::onPropsCacheChanged, this, _1));

    // Restore persisted sort order, and seed the filter object from the
    // controls restored above.
    mViewModel.getSorter().setMode((ALSceneExplorerSort::ESortMode)gSavedSettings.getU32("ALSceneExplorerSortOrder"));
    onFilterChanged();

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
    reconcile();
}

void ALFloaterSceneExplorer::onClose(bool app_quitting)
{
}

void ALFloaterSceneExplorer::draw()
{
    // All discovery / fetch / filter / layout now happens in idleUpdate() (driven
    // by gIdleCallbacks); draw() just renders the current tree state.
    LLFloater::draw();
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
    syncSelectionToWorld();
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
        widget = LLUICtrlFactory::create<LLFolderViewItem>(params);
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
    ALObjectProperties::Record rec = ALObjectProperties::fromObject(obj);
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
        rec.mPosRegion  = obj->getPositionRegion();
        rec.mPosGlobal  = obj->getPositionGlobal();
        rec.mDistance   = (F32)(rec.mPosGlobal - gAgent.getPositionGlobal()).magVec();
        rec.mObjectCost = obj->peekObjectCost();
        rec.mLandImpact = obj->peekLinksetCost();
    }

    // Collect ids, not pointers: removing a linkset root frees its descendant
    // model items, so a raw descendant pointer collected earlier in the pass
    // would dangle. Each id is re-looked-up immediately before removal.
    std::vector<LLUUID> to_remove;
    for (const auto& entry : mItems)
    {
        // Attachment-point folders are synthetic (no backing object); they are
        // pruned below once empty rather than matched against the present set.
        if (entry.second->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT)
            continue;
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

    // The per-pass distance refresh above doesn't dirty per-item filter state,
    // so when the radius predicate is active, re-arm the whole filter once the
    // agent has actually moved — otherwise "Within N m" would keep showing the
    // object set from wherever the filter last ran.
    ALSceneExplorerFilter& filter = mViewModel.getFilter();
    if (filter.isLimitRadiusActive())
    {
        const LLVector3d agent_pos = gAgent.getPositionGlobal();
        if ((agent_pos - mLastFilterAgentPos).magVec() > 1.0)
        {
            mLastFilterAgentPos = agent_pos;
            filter.setModified(LLFolderViewFilter::FILTER_RESTART);
        }
    }

    // Re-sort everything periodically only for the distance key, whose order
    // shifts as the agent moves. Static keys (name/land-impact/triangles/type)
    // re-sort per-parent when nodes are added or change, so a full periodic
    // re-sort would just be wasted work on a large tree. requestSortAll() only
    // bumps the target sort version — sorting actually runs inside arrange(),
    // which nothing else re-arms on a quiet tree — so force a re-arrange too,
    // or the refreshed distances never reorder anything.
    if (mViewModel.getSorter().getMode() == ALSceneExplorerSort::SORT_DISTANCE)
    {
        mViewModel.requestSortAll();
        mTree->arrangeAll();
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
    if (!region || mFetchQueue.empty())
        return;

    ALObjectPropertiesCache& cache = ALObjectPropertiesCache::instance();
    std::vector<U32> local_ids;
    local_ids.reserve(MAX_OBJECTS_PER_PACKET);
    while (!mFetchQueue.empty() && local_ids.size() < (size_t)MAX_OBJECTS_PER_PACKET)
    {
        const LLUUID id = mFetchQueue.front();
        mFetchQueue.pop_front();
        mQueuedProps.erase(id);

        LLViewerObject* obj = gObjectList.findObject(id);
        // Local ids are a per-region namespace, so never address an object
        // that died or crossed into a neighbour region.
        if (!obj || obj->isDead() || obj->getRegion() != region)
            continue;
        // Never disturb the user's live selection: a raw ObjectDeselect for an
        // actively edited object desyncs the simulator's selection state (the
        // sim halts physical objects and streams updates only while selected).
        // retryUnresolved() re-queues it once it is no longer selected.
        if (obj->isSelected())
            continue;

        local_ids.push_back(obj->getLocalID());
        cache.markPending(id);
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
        // Avatars resolve via the name cache and attachment-point folders are
        // synthetic — neither has server object properties to fetch.
        if (type == ALSceneExplorerItem::TYPE_AVATAR
            || type == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
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
    if (!item)
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
    rec.mPropsValid   = true;

    // Adopt the server name in the same pass (avatars keep their display name
    // from the name cache instead).
    const std::string display_name =
        (!p->mName.empty() && item->getItemType() != ALSceneExplorerItem::TYPE_AVATAR)
            ? p->mName : LLStringUtil::null;
    item->updateRecord(rec, display_name);
}

void ALFloaterSceneExplorer::onAvatarNameLoaded(const LLUUID& id, const LLAvatarName& av_name)
{
    auto it = mItems.find(id);
    if (it == mItems.end())
        return;
    it->second->setName(av_name.getCompleteName());
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
}

// ============================================================================
// Filters / sort
// ============================================================================
void ALFloaterSceneExplorer::onFilterChanged()
{
    ALSceneExplorerFilter& f = mViewModel.getFilter();
    f.setFilterSubString(getChild<LLFilterEditor>("filter_input")->getText());

    const S32 owner_idx = llmax(0, getChild<LLComboBox>("owner_combo")->getCurrentIndex());
    f.setOwnerMode((ALSceneExplorerFilter::EOwnerMode)owner_idx);

    U32 flags = 0;
    if (getChild<LLUICtrl>("flag_scripted")->getValue().asBoolean())  flags |= ALObjectProperties::FLAG_SCRIPTED;
    if (getChild<LLUICtrl>("flag_light")->getValue().asBoolean())     flags |= ALObjectProperties::FLAG_LIGHT;
    if (getChild<LLUICtrl>("flag_particles")->getValue().asBoolean()) flags |= ALObjectProperties::FLAG_PARTICLES;
    f.setFlagMask(flags);

    const bool limit = getChild<LLUICtrl>("limit_radius_check")->getValue().asBoolean();
    const F32 radius = (F32)getChild<LLUICtrl>("radius_slider")->getValue().asReal();
    f.setRadius(radius, limit);

    // Persist the filter set (the text predicate is deliberately session-only).
    gSavedSettings.setU32("ALSceneExplorerOwnerFilter", (U32)owner_idx);
    gSavedSettings.setU32("ALSceneExplorerFlagFilter", flags);
    gSavedSettings.setBOOL("ALSceneExplorerLimitRadius", limit);
    gSavedSettings.setF32("ALSceneExplorerRadius", radius);

    const bool show_av = getChild<LLUICtrl>("show_avatars_check")->getValue().asBoolean();
    if (show_av != mShowAvatars)
    {
        mShowAvatars = show_av;
        gSavedSettings.setBOOL("ALSceneExplorerShowAvatars", show_av);
        reconcile();
    }
    // The filter setters above bumped the filter generation, so the next idle
    // pass (mTree->update()) re-filters and re-arranges.
}

void ALFloaterSceneExplorer::onSortChanged()
{
    const U32 mode = (U32)llmax(0, getChild<LLComboBox>("sort_combo")->getCurrentIndex());
    mViewModel.getSorter().setMode((ALSceneExplorerSort::ESortMode)mode);
    gSavedSettings.setU32("ALSceneExplorerSortOrder", mode);
    mViewModel.requestSortAll();
    if (mTree)
        mTree->arrangeAll();
}

// ============================================================================
// Actions
// ============================================================================
LLViewerObject* ALFloaterSceneExplorer::getSelectedObject() const
{
    if (!mTree)
        return nullptr;
    LLFolderViewItem* sel = mTree->getCurSelectedItem();
    if (!sel)
        return nullptr;
    ALSceneExplorerItem* item = static_cast<ALSceneExplorerItem*>(sel->getViewModelItem());
    if (!item)
        return nullptr;
    return gObjectList.findObject(item->getUUID());
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
    static LLCachedControl<bool> edit_linked(gSavedSettings, "EditLinkedParts", false);
    for (LLViewerObject* obj : objs)
    {
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
    if (mSyncingSelection)
        return;
    // Drive the in-world selection only while an editing surface is up. Plain
    // browsing must never grab the user's selection — pushing one has side
    // effects (avatar look-at/point-at targeting, edit-mode behaviour) that
    // read as the camera/avatar reacting to every tree click. mLastSelectedID
    // deliberately isn't advanced while gated, so opening Build/Inspect adopts
    // the currently selected row once.
    if (!LLFloaterReg::instanceVisible("build") && !LLFloaterReg::instanceVisible("inspect"))
        return;
    LLViewerObject* obj = getSelectedObject();
    const LLUUID id = obj ? obj->getID() : LLUUID::null;
    if (id == mLastSelectedID)
        return;
    mLastSelectedID = id;
    if (obj)
    {
        uuid_vec_t ids;
        ids.push_back(id);
        selectInWorld(ids);
    }
}

void ALFloaterSceneExplorer::activateItem(const LLUUID& id)
{
    // Act on the activated item itself, not on whatever the tree currently has
    // selected — double-click paths can fire without a selection change.
    LLViewerObject* obj = gObjectList.findObject(id);
    if (!obj)
        return;
    mLastSelectedID = id; // keep the per-frame selection sync from re-selecting
    uuid_vec_t ids;
    ids.push_back(id);
    selectInWorld(ids);

    switch (gSavedSettings.getU32("ALSceneExplorerActivateAction"))
    {
    case 1: openBuildTools();                       break;
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
    LLViewerObject* obj = getSelectedObject();
    if (!obj)
        return;
    gAgent.teleportViaLocation(obj->getPositionGlobal());
}

void ALFloaterSceneExplorer::doCopyID()
{
    LLViewerObject* obj = getSelectedObject();
    LLUUID id;
    if (obj)
    {
        id = obj->getID();
    }
    else if (mTree && mTree->getCurSelectedItem())
    {
        if (ALSceneExplorerItem* item = static_cast<ALSceneExplorerItem*>(mTree->getCurSelectedItem()->getViewModelItem()))
            id = item->getUUID();
    }
    if (id.notNull())
    {
        const std::string id_str = id.asString();
        const LLWString wid = utf8str_to_wstring(id_str);
        LLClipboard::instance().copyToClipboard(wid, 0, (S32)wid.size());
    }
}
