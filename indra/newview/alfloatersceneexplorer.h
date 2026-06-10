/**
 * @file alfloatersceneexplorer.h
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
#ifndef AL_FLOATERSCENEEXPLORER_H
#define AL_FLOATERSCENEEXPLORER_H

#include <deque>

#include <boost/signals2.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "llfloater.h"
#include "llframetimer.h"
#include "lluuid.h"

#include "alsceneexplorermodel.h"

class LLAvatarName;
class LLFolderView;
class LLFolderViewItem;
class LLLayoutPanel;
class LLMenuGL;
class LLPanel;
class LLViewerObject;

class ALFloaterSceneExplorer final : public LLFloater
{
    friend class LLFloaterReg;
public:
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

    // Invoked from ALSceneExplorerItem::openItem (double-click / Enter).
    void activateItem(const LLUUID& id);

    // Shows/hides the shared superset menu's entries for the selected row's
    // type and stages live object rows as the in-world selection so reused
    // viewer handlers (touch/buy/return/...) and their enable predicates act
    // on them. Driven by the folder view's right-click popup (via
    // ALSceneExplorerItem::buildContextMenu) and by the gear button.
    void buildRowContextMenu(LLMenuGL& menu, U32 flags);

private:
    ALFloaterSceneExplorer(const LLSD& key);
    ~ALFloaterSceneExplorer() override;

    // --- idle update (driven by gIdleCallbacks, not draw) -----------------
    static void onIdle(void* user_data);
    void idleUpdate();
    void drainBuildQueue(F64 max_time);

    // --- tree construction ------------------------------------------------
    void buildTree();
    void reconcile();
    ALSceneExplorerItem* getOrCreateNode(LLViewerObject* obj);
    ALSceneExplorerItem* getOrCreatePointFolder(LLViewerObject* avatar, LLViewerObject* attachment);
    LLFolderViewItem*    createWidget(ALSceneExplorerItem* item, bool is_folder, LLFolderViewItem* parent_widget);
    void eraseSubtreeMaps(ALSceneExplorerItem* item);
    void removeNode(ALSceneExplorerItem* item);
    void clearTree();

    // --- derendered category (synthetic rows from ALDerenderList) ----------
    void syncDerendered();
    void ensureDerenderedCategory();
    void destroyDerenderedCategory();
    void onDerenderListChanged();

    // --- async property fetch --------------------------------------------
    void queueProps(const LLUUID& id);
    void drainPropsQueue();
    void retryUnresolved();
    void onPropsCacheChanged(const LLUUID& id);
    void applyServerProps(ALSceneExplorerItem* item);
    void onAvatarNameLoaded(const LLUUID& id, const LLAvatarName& av_name);

    // --- filters / sort ---------------------------------------------------
    void onFilterChanged();
    void setSortMode(const LLSD& param);
    bool checkSortMode(const LLSD& param) const;
    void toggleShow(const LLSD& param);
    bool checkShow(const LLSD& param) const;
    void doResetFilters();
    void doFilterByOwner();
    void updateCategoryCounts();
    void updateActionButtons();

    // --- detail pane --------------------------------------------------------
    void onToggleDetails();
    void refreshDetail();
    void fillObjectDetail(ALSceneExplorerItem* item);
    void fillAvatarDetail(ALSceneExplorerItem* item);

    // --- actions ----------------------------------------------------------
    void syncSelectionToWorld();
    ALSceneExplorerItem* getSelectedItem() const;
    LLViewerObject* getSelectedObject() const;
    void selectInWorld(const uuid_vec_t& ids);
    void openBuildTools();
    void onGearMouseDown();
    void doFocus();
    void doEdit();
    void doInspect();
    void doTeleport();
    void doSit();
    void doCopy(const LLSD& param);
    void doCopyResults();
    void doShowOnMap();
    void doBeacon();
    void doBlockOwner();
    void doAvatarAction(const LLSD& param);
    void doDerender(const LLSD& param);
    void doRestore();

    // --- members ----------------------------------------------------------
    ALSceneExplorerViewModel mViewModel;
    LLFolderView*  mTree        = nullptr;
    LLPanel*       mTreePanel   = nullptr;
    // Received-items-style host: holds the expander bar + detail content and
    // collapses down to the bar via LLLayoutStack::collapsePanel().
    LLLayoutPanel* mDetailHost  = nullptr;

    ALSceneExplorerItem* mRootItem        = nullptr;
    ALSceneExplorerItem* mObjectsCategory = nullptr;
    ALSceneExplorerItem* mAvatarsCategory = nullptr;
    LLFolderViewItem*    mObjectsWidget   = nullptr;
    LLFolderViewItem*    mAvatarsWidget   = nullptr;
    // Created lazily while derendered entries exist and the toggle is on, so
    // an empty category never clutters the tree.
    ALSceneExplorerItem* mDerenderedCategory = nullptr;
    LLFolderViewItem*    mDerenderedWidget   = nullptr;

    boost::unordered_map<LLUUID, ALSceneExplorerItem*> mItems;
    boost::unordered_map<LLUUID, LLFolderViewItem*>    mWidgets;

    // Property-fetch pipeline: mFetchQueue holds ids waiting to be sent
    // (mQueuedProps mirrors its membership for dedup); once sent, an id is
    // tracked as in-flight by ALObjectPropertiesCache until its reply lands.
    std::deque<LLUUID>            mFetchQueue;
    boost::unordered_set<LLUUID>  mQueuedProps;

    // New objects discovered by reconcile() awaiting time-sliced widget creation
    // in drainBuildQueue(), so a 60k-object region populates without a stall.
    std::deque<LLUUID>            mBuildQueue;
    boost::unordered_set<LLUUID>  mQueued;

    boost::signals2::connection mPropsConn;
    boost::signals2::connection mDerenderConn;

    LLFrameTimer mReconcileTimer;
    LLFrameTimer mFetchTimer;
    LLFrameTimer mRetryTimer;

    U64        mLastRegionHandle = 0;
    LLUUID     mLastSelectedID;
    LLUUID     mLastButtonStateID;  // selection the action buttons were last gated for
    LLUUID     mLastDetailID;       // selection the detail pane was last built for
    LLUUID     mLastFacesID;        // selection the faces list was last built for
    LLUUID     mBeaconTrackedID;    // row the location tracker beacon was set for
    LLUUID     mFilterOwnerId;      // "Filter by this owner" target (session-only)
    LLVector3d mLastFilterAgentPos; // last agent position the radius filter ran at
    bool       mShowAvatars   = true;
    bool       mShowDerendered = false;
    bool       mDetailsExpanded = false;
    bool       mDetailDirty   = false;
    bool       mSyncingSelection = false;
};

#endif // AL_FLOATERSCENEEXPLORER_H
