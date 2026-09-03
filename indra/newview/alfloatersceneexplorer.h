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
#include <functional>

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
    AL_VIEW_TYPE(ALFloaterSceneExplorer, LLFloater);

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

    // Re-read the persisted filter settings into the quick-bar controls and
    // the filter object. The companion filters floater calls this after
    // writing settings, keeping both surfaces views of the same state.
    void refreshFilters();

    // Canonical "reset all": clears the quick-bar search/owner state and every
    // persisted filter (flags, types, scope, radius, thresholds) back to its
    // default. The companion filters floater's reset button delegates here.
    void doResetFilters();

private:
    ALFloaterSceneExplorer(const LLSD& key);
    ~ALFloaterSceneExplorer() override;

    // --- idle update (driven by gIdleCallbacks, not draw) -----------------
    static void onIdle(void* user_data);
    void idleUpdate();
    void drainBuildQueue(F64 max_time);
    void drainRefillQueue(F64 max_time);

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

    // --- full-region (360) coverage + scalability ---------------------------
    void applyFullRegionMode(bool active);
    void forEachVisibleRow(const std::function<void(ALSceneExplorerItem*, LLFolderViewItem*)>& fn);
    void scanVisibleRows();
    void updateStatusText();
    void requestCostsFor(ALSceneExplorerItem* item);

    // --- owner name resolution (search + suffix) ----------------------------
    void noteOwnerFor(ALSceneExplorerItem* item);
    void resolveOwnerName(const LLUUID& owner_id, bool group_owned);
    void onOwnerNameResolved(const LLUUID& owner_id, const std::string& name, bool is_group);
    void auditOwnerNames();

    // --- filters / sort ---------------------------------------------------
    void onFilterChanged();
    void setSortMode(const LLSD& param);
    bool checkSortMode(const LLSD& param) const;
    void toggleShow(const LLSD& param);
    bool checkShow(const LLSD& param) const;
    void doFilterByOwner();
    void doShowFilters();
    void updateOwnerFilterLabel();
    void updateCategoryCounts();
    void updateActionButtons();

    // --- detail pane --------------------------------------------------------
    void onToggleDetails();
    void refreshDetail();
    void fillObjectDetail(ALSceneExplorerItem* item);
    void fillAvatarDetail(ALSceneExplorerItem* item);

    // --- actions ----------------------------------------------------------
    void syncSelectionToWorld();
    void onWorldSelectionChanged();
    void syncSelectionFromWorld();
    void doSelectAllResults();
    void doRefresh();
    ALSceneExplorerItem* getSelectedItem() const;
    std::vector<ALSceneExplorerItem*> getSelectedSceneItems() const;
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
    // On-screen rows jump the line through mPriorityFetch (mPriorityQueued
    // mirrors it), drained ahead of the main queue.
    std::deque<LLUUID>            mFetchQueue;
    boost::unordered_set<LLUUID>  mQueuedProps;
    std::deque<LLUUID>            mPriorityFetch;
    boost::unordered_set<LLUUID>  mPriorityQueued;

    // New objects discovered by reconcile() awaiting time-sliced widget creation
    // in drainBuildQueue(), so a 60k-object region populates without a stall.
    std::deque<LLUUID>            mBuildQueue;
    boost::unordered_set<LLUUID>  mQueued;

    // Explicit Refresh: rows awaiting a time-sliced local Record re-fill
    // (per-face flags, geometry, prim counts go stale after node build).
    std::deque<LLUUID>            mRefillQueue;

    boost::signals2::connection mPropsConn;
    boost::signals2::connection mDerenderConn;
    boost::signals2::connection mWorldSelConn;

    // Owner display names for the searchable text and row suffixes, resolved
    // once per unique owner/group id. An empty name records "unresolved or
    // RLVa-hidden"; auditOwnerNames() re-resolves/scrubs entries when the
    // @shownames restriction flips mid-session (groups are never hidden).
    struct ResolvedOwner
    {
        std::string mName;
        bool        mIsGroup = false;
    };
    boost::unordered_map<LLUUID, ResolvedOwner> mOwnerNames;
    boost::unordered_set<LLUUID>                mOwnerNamesPending;

    LLFrameTimer mReconcileTimer;
    LLFrameTimer mFetchTimer;
    LLFrameTimer mRetryTimer;

    U64        mLastRegionHandle = 0;
    // Last id set synced between the tree and the world selection, kept
    // sorted — used for change detection in both directions, which is also
    // what breaks the world<->tree feedback loop.
    uuid_vec_t mLastPushedSelection;
    LLUUID     mLastButtonStateID;  // selection the action buttons were last gated for
    LLUUID     mLastDetailID;       // selection the detail pane was last built for
    LLUUID     mLastFacesID;        // selection the faces list was last built for
    LLUUID     mBeaconTrackedID;    // row the location tracker beacon was set for
    LLUUID     mFilterOwnerId;      // "Filter by this owner" target (session-only)
    LLVector3d mLastFilterAgentPos; // last agent position the radius filter ran at
    LLVector3d mLastSortAgentPos;   // last agent position the distance sort ran at
    std::string mPrevILMode;        // interest-list mode before 360 was applied
    std::string mLastStatus;        // last status-line text (avoid re-set churn)
    bool       mShowAvatars   = true;
    bool       mShowDerendered = false;
    bool       mFullRegion    = false;
    bool       mSelectionSync = true;  // passive tree<->world selection mirroring
    bool       mObjectsMoved  = false; // any object moved since the last re-sort
    bool       mScanVisible   = false; // run scanVisibleRows after the next arrange
    bool       mWorldSelectionDirty = false; // LLSelectMgr signalled a change
    bool       mDetailsExpanded = false;
    bool       mDetailDirty   = false;
    bool       mSyncingSelection = false;
};

#endif // AL_FLOATERSCENEEXPLORER_H
