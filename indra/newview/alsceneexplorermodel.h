/**
 * @file alsceneexplorermodel.h
 * @brief Folder-view model for the Scene Explorer tree (region -> linkset -> prim, avatars)
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
#ifndef AL_SCENEEXPLORERMODEL_H
#define AL_SCENEEXPLORERMODEL_H

#include "../llui/llfolderviewitem.h"
#include "../llui/llfolderviewmodel.h"

#include "alobjectproperties.h"
#include "alsceneexplorerpredicate.h"
#include "lltimer.h"
#include "lluuid.h"

class ALFloaterSceneExplorer;
class ALSceneExplorerItem;

// ============================================================================
// Sort comparator
// ============================================================================
class ALSceneExplorerSort
{
public:
    enum ESortMode : U32
    {
        SORT_DISTANCE = 0,      // from the agent (reorders as the agent moves)
        SORT_NAME,
        SORT_LAND_IMPACT,
        SORT_TRIANGLES,
        SORT_TYPE,
        SORT_REGION_ORIGIN      // from the region's <0,0,0> (agent-independent)
    };

    ALSceneExplorerSort(ESortMode mode = SORT_DISTANCE) : mMode(mode) {}

    void setMode(ESortMode mode) { mMode = mode; }
    ESortMode getMode() const { return mMode; }

    bool operator()(const ALSceneExplorerItem* a, const ALSceneExplorerItem* b) const;

private:
    ESortMode mMode;
};

// ============================================================================
// Filter
// ============================================================================
class ALSceneExplorerFilter final : public LLFolderViewFilter
{
public:
    enum EOwnerMode : U32
    {
        OWNER_ANY = 0,
        OWNER_MINE,
        OWNER_GROUP,
        OWNER_OTHERS,
        OWNER_SPECIFIC      // a single owner/group id ("Filter by this owner")
    };

    // Which field the text predicate searches (the inventory search-type
    // combo pattern). Order matches the search_type combo in the XUI.
    enum ESearchType : U32
    {
        SEARCH_NAME = 0,
        SEARCH_DESCRIPTION,
        SEARCH_OWNER,       // resolved owner/group display name
        SEARCH_UUID,
        SEARCH_ALL
    };

    // Spatial scope. Order matches the scope radio in the filters floater.
    enum EScope : U32
    {
        SCOPE_REGION = 0,
        SCOPE_PARCEL,       // the parcel the agent is standing on
        SCOPE_RADIUS        // within N m of the agent
    };

    ALSceneExplorerFilter();
    ~ALSceneExplorerFilter() override = default;

    // Predicate setters (called by the floater); each bumps the generation.
    void setFilterSubString(const std::string& string);
    void setSearchType(ESearchType type);
    void setOwnerMode(EOwnerMode mode);
    void setOwnerId(const LLUUID& id);  // target for OWNER_SPECIFIC
    void setGeomMask(U32 mask);     // bits: 1 << ALObjectProperties::EGeom
    void setFlagMask(U32 mask);     // bits: ALObjectProperties::EFlag (require all)
    void setScope(EScope scope, F32 radius);
    void setMinLandImpact(F32 min_li);
    void setMinTriangles(U32 min_tris);
    // Scope predicates depend on the agent's position/parcel, which the
    // filter can't observe — the floater re-arms on agent movement.
    bool isScopeActive() const
    {
        return mConstraints.mScope != (U32)ALSceneExplorerPredicate::SCOPE_REGION;
    }

    // LLFolderViewFilter
    bool check(const LLFolderViewModelItem* item) override;
    bool checkFolder(const LLFolderViewModelItem* folder) const override { return true; }

    void setEmptyLookupMessage(const std::string& message) override { mEmptyLookupMessage = message; }
    std::string getEmptyLookupMessage(bool is_empty_folder = false) const override { return mEmptyLookupMessage; }

    bool showAllResults() const override { return false; }

    // Span of the text match within the item's display name, in codepoints
    // (empty when the match landed in another field), so the folder view draws
    // the standard inventory match highlight.
    Match getFilterMatch(LLFolderViewModelItem* item) const override;

    bool isActive() const override;
    bool isModified() const override { return mModified; }
    void clearModified() override
    {
        mModified = false;
        // The pending refilter is done; the next change starts a new batch.
        mFilterModified = FILTER_NONE;
    }
    const std::string& getName() const override { return mName; }
    const std::string& getFilterText() override { return mConstraints.mFilterSubString; }
    void setModified(EFilterModified behavior = FILTER_RESTART) override;

    // Time-slice filtering the way LLInventoryFilter does: each pass gets a few
    // ms, then bails and resumes next idle, so filtering a 60k-node tree stays
    // responsive instead of stalling on one frame. Must be a real-time LLTimer:
    // an LLFrameTimer's clock only advances once per frame, so it could never
    // expire inside a single pass and the budget would be a no-op.
    void resetTime(S32 timeout) override
    {
        mFilterTime.reset();
        mFilterTime.setTimerExpirySec((F32)timeout / 1000.f);
    }
    bool isTimedOut() override { return mFilterTime.hasExpired(); }

    bool isDefault() const override { return !isActive(); }
    bool isNotDefault() const override { return isActive(); }
    void markDefault() override {}
    void resetDefault() override {}

    // Multi-generation scheme (the LLInventoryFilter model): current bumps on
    // every change; items that PASSED at >= first-success still pass (so they
    // stay visible while a less-restrictive change refilters); items that
    // FAILED at >= first-required still fail (so a more-restrictive change
    // can re-stamp them without re-evaluating the predicate).
    S32 getCurrentGeneration() const override { return mCurrentGeneration; }
    S32 getFirstSuccessGeneration() const override { return mFirstSuccessGeneration; }
    S32 getFirstRequiredGeneration() const override { return mFirstRequiredGeneration; }

private:
    // Map a classified constraint change onto the generation bookkeeping
    // (CHANGE_NONE is a no-op).
    void applyChange(ALSceneExplorerPredicate::EFilterChange change);

    std::string mName;
    std::string mEmptyLookupMessage;
    // The predicate state itself lives in the pure, unit-tested constraint
    // set; this class is the LLFolderViewFilter adapter around it. The
    // setters above keep writing through, and check() supplies the impure
    // facts (agent id, parcel containment) per item.
    ALSceneExplorerPredicate::Constraints mConstraints;
    bool            mModified   = false;
    EFilterModified mFilterModified = FILTER_NONE; // merged kind of the pending batch
    S32         mCurrentGeneration       = 1;
    S32         mFirstSuccessGeneration  = 1;
    S32         mFirstRequiredGeneration = 1;
    LLTimer     mFilterTime;        // per-pass time budget for filter()
};

// ============================================================================
// View model
// ============================================================================
class ALSceneExplorerViewModel final
:   public LLFolderViewModel<ALSceneExplorerSort, ALSceneExplorerItem, ALSceneExplorerItem, ALSceneExplorerFilter>
{
public:
    typedef LLFolderViewModel<ALSceneExplorerSort, ALSceneExplorerItem, ALSceneExplorerItem, ALSceneExplorerFilter> base_t;

    ALSceneExplorerViewModel() : base_t(new ALSceneExplorerSort(), new ALSceneExplorerFilter()) {}

    bool startDrag(std::vector<LLFolderViewModelItem*>& items) override { return false; }
};

// ============================================================================
// Model item
// ============================================================================
class ALSceneExplorerItem final : public LLFolderViewModelItemCommon
{
public:
    enum EItemType : U8
    {
        TYPE_REGION = 0,
        TYPE_CATEGORY_OBJECTS,
        TYPE_CATEGORY_AVATARS,
        TYPE_CATEGORY_DERENDERED,
        TYPE_LINKSET,
        TYPE_PRIM,
        TYPE_AVATAR,
        TYPE_ATTACHMENT_POINT,  // per-avatar attachment-point grouping folder
        TYPE_ATTACHMENT,        // attachment linkset root
        TYPE_DERENDERED_OBJECT, // ALDerenderList entry; synthetic, no live object
        TYPE_DERENDERED_AVATAR
    };

    ALSceneExplorerItem(EItemType type, const LLUUID& id, const std::string& name,
                        LLFolderViewModelInterface& root_view_model, ALFloaterSceneExplorer* floater);

    // Accessors used by filter / sort / floater
    EItemType getItemType() const { return mItemType; }
    bool isContainer() const
    {
        return mItemType == TYPE_REGION || isCategory() || mItemType == TYPE_ATTACHMENT_POINT;
    }
    bool isCategory() const
    {
        return mItemType == TYPE_CATEGORY_OBJECTS
            || mItemType == TYPE_CATEGORY_AVATARS
            || mItemType == TYPE_CATEGORY_DERENDERED;
    }
    // Synthetic rows backed by an ALDerenderList entry instead of a live
    // object: excluded from the present-set sweep, the props fetch/retry, and
    // live-object actions (Restore / Copy-ID only).
    bool isDerenderedType() const
    {
        return mItemType == TYPE_DERENDERED_OBJECT || mItemType == TYPE_DERENDERED_AVATAR;
    }
    // Folder-ness is fully derived from the item type: structural containers
    // plus every root scene object (so single- and multi-prim objects sort
    // together in one widget list). Only child prims are leaf widgets.
    bool isFolderType() const
    {
        return isContainer()
            || mItemType == TYPE_LINKSET
            || mItemType == TYPE_AVATAR
            || mItemType == TYPE_ATTACHMENT;
    }
    const LLUUID& getUUID() const { return mUUID; }
    const ALObjectProperties::Record& getRecord() const { return mRecord; }
    ALObjectProperties::Record& getRecordRef() { return mRecord; }

    // Pre-lowercased per-field search text for the filter (kept separate so
    // the search-type combo can target one field without allocation churn).
    const std::string& searchName() const  { return mSearchName; }
    const std::string& searchDesc() const  { return mSearchDesc; }
    const std::string& searchUUID() const  { return mSearchUUID; }
    const std::string& searchOwner() const { return mOwnerSearch; }

    void setName(const std::string& name);
    void updateRecord(const ALObjectProperties::Record& rec);
    // Same, but also adopts @display_name (when non-empty) in the one
    // rebuildSearchable()/dirtyFilter() pass instead of dirtying twice.
    void updateRecord(const ALObjectProperties::Record& rec, const std::string& display_name);

    // Index within the parent linkset/avatar child list (1-based; 0 = unset/root).
    // Used to keep child prims in native link order regardless of the active sort.
    void setLinkOrder(S32 order) { mLinkOrder = order; }
    S32  getLinkOrder() const { return mLinkOrder; }

    // Bounded re-request bookkeeping for the floater's property fetch.
    S32  getPropsRetries() const { return mPropsRetries; }
    void notePropsRetry() { ++mPropsRetries; }

    // One-shot guard for demand-driven cost fetches (visible row / LI sort /
    // detail pane): a failed fetch leaves the object's cost stale forever, so
    // each node asks at most once per session unless explicitly refreshed.
    bool wasCostRequested() const { return mCostRequested; }
    void noteCostRequested() { mCostRequested = true; }

    // Explicit Refresh: re-arm the bounded retry and cost demand so the
    // fetch pipeline may ask the server again.
    void resetFetchState() { mPropsRetries = 0; mCostRequested = false; }

    // Resolved owner (or owning group) display name: folded into the
    // searchable text and shown in the row suffix. Resolved and RLVa-gated
    // by the floater.
    void setOwnerName(const std::string& name);
    const std::string& getOwnerName() const { return mOwnerDisplay; }

    // Run the configured activate action (focus/edit/inspect) on this object.
    // Invoked from the view's double-click paths only — openItem() also fires
    // when a folder is expanded, which must never trigger activation.
    void activate();

    // LLFolderViewModelItem (non-Common)
    const std::string& getName() const override { return mName; }
    const std::string& getDisplayName() const override { return mName; }
    const std::string& getSearchableName() const override { return mSearchName; }
    std::string getSearchableDescription() const override { return mRecord.mDescription; }
    std::string getSearchableCreatorName() const override { return LLStringUtil::null; }
    std::string getSearchableUUIDString() const override { return mUUID.asString(); }

    LLPointer<LLUIImage> getIcon() const override;
    LLFontGL::StyleFlags getLabelStyle() const override;
    std::string getLabelSuffix() const override;

    // Multi-line hover tooltip built from the record (always current — the
    // widgets query it on demand instead of caching it).
    std::string getTooltip() const;

    void openItem(void) override;
    void closeItem(void) override {}
    void selectItem(void) override {}
    void navigateToFolder(bool new_window = false, bool change_mode = false) override {}

    bool isFavorite() const override { return false; }
    bool isItemRenameable() const override { return false; }
    bool renameItem(const std::string& new_name) override { return false; }
    bool isItemMovable(void) const override { return false; }
    void move(LLFolderViewModelItem* parent_listener) override {}
    bool isItemRemovable(bool check_worn = true) const override { return false; }
    bool isItemInTrash(void) const override { return false; }
    bool removeItem() override { return false; }
    void removeBatch(std::vector<LLFolderViewModelItem*>& batch) override {}
    bool isItemCopyable(bool can_copy_as_link = true) const override { return true; }
    bool copyToClipboard() const override { return false; }
    bool cutToClipboard() override { return false; }
    bool isClipboardPasteable() const override { return false; }
    void pasteFromClipboard() override {}
    void pasteLinkFromClipboard() override {}
    bool isAgentInventory() const override { return false; }
    bool isAgentInventoryRoot() const override { return false; }
    // Forwards to the floater, which shows/hides the shared superset menu per
    // row type (and stages the row as the live selection for reused viewer
    // handlers). Also reused by the floater's gear button.
    void buildContextMenu(LLMenuGL& menu, U32 flags) override;

    bool hasChildren() const override { return getChildrenCount() > 0; }

    bool dragOrDrop(MASK mask, bool drop, EDragAndDropType cargo_type,
                    void* cargo_data, std::string& tooltip_msg) override { return false; }

    // Real filtering (mirrors LLFolderViewModelItemInventory::filter).
    bool filter(LLFolderViewFilter& filter) override;
    // Requests a re-arrange when this item's filtered state changes — the
    // framework has no other filter->arrange link, so without this a filter
    // toggle only becomes visible when something else happens to arrange
    // (mirrors LLFolderViewModelItemInventory::setPassedFilter).
    void setPassedFilter(bool passed, S32 filter_generation,
                         std::string::size_type string_offset = std::string::npos,
                         std::string::size_type string_size = 0) override;

private:
    void rebuildSearchable();

    EItemType                   mItemType;
    LLUUID                      mUUID;
    std::string                 mName;
    std::string                 mSearchName;    // lowercased name
    std::string                 mSearchDesc;    // lowercased description
    std::string                 mSearchUUID;    // uuid string (already lowercase hex)
    std::string                 mOwnerSearch;   // lowercased owner display name
    std::string                 mOwnerDisplay;  // owner display name (row suffix)
    S32                         mLinkOrder = 0;
    S32                         mPropsRetries = 0;
    bool                        mCostRequested = false;
    bool                        mPrevPassedAllFilters = false;
    ALObjectProperties::Record  mRecord;
    ALFloaterSceneExplorer*     mFloater;
};

// ============================================================================
// Folder widget
//
// Scene objects (linksets, single prims, avatars, attachment roots) are all
// represented as folders so they sort together in one list regardless of prim
// count (LLFolderViewFolder lays out sub-folders above leaf items, which would
// otherwise force multi-prim linksets above single prims). Childless folders
// simply draw no disclosure arrow. This subclass restores leaf-style activation:
// a double-click acts on the object (focus/edit/inspect) instead of toggling,
// while the disclosure arrow still expands multi-prim linksets. Structural
// containers (region/category/attachment-point) keep the default toggle.
// ============================================================================
class ALSceneExplorerFolder final : public LLFolderViewFolder
{
public:
    typedef LLFolderViewFolder::Params Params; // LLFolderViewFolder has no Params of its own
    ALSceneExplorerFolder(const Params& p) : LLFolderViewFolder(p) {}
    ~ALSceneExplorerFolder() override = default;

    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
};

// Leaf widget: identical to the stock item except for the rich model tooltip.
class ALSceneExplorerListItem final : public LLFolderViewItem
{
public:
    typedef LLFolderViewItem::Params Params;
    ALSceneExplorerListItem(const Params& p) : LLFolderViewItem(p) {}
    ~ALSceneExplorerListItem() override = default;

    bool handleToolTip(S32 x, S32 y, MASK mask) override;
};

#endif // AL_SCENEEXPLORERMODEL_H
