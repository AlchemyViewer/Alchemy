/**
 * @file alsceneexplorermodel.cpp
 * @brief Folder-view model for the Scene Explorer tree
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

#include "alsceneexplorermodel.h"

#include "alfloatersceneexplorer.h"
#include "llagent.h"
#include "lltooltip.h"
#include "llui.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"

// ============================================================================
// ALSceneExplorerSort
// ============================================================================
bool ALSceneExplorerSort::operator()(const ALSceneExplorerItem* a, const ALSceneExplorerItem* b) const
{
    if (!a || !b)
        return false;

    // Container nodes (region / categories / attachment points) keep a fixed
    // structural order. Sibling attachment-point folders share a type, so order
    // those alphabetically by point name ("Chest", "Right Hand", ...).
    if (a->isContainer() || b->isContainer())
    {
        if (a->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT
            && b->getItemType() == ALSceneExplorerItem::TYPE_ATTACHMENT_POINT)
        {
            return LLStringUtil::compareInsensitive(a->getName(), b->getName()) < 0;
        }
        return a->getItemType() < b->getItemType();
    }

    // Child prims of a linkset keep their native link order so a linkset's
    // contents read top-down as they're linked, not scattered by distance/name.
    // Every other node here is a root-level object (world linkset, single prim,
    // avatar, or attachment root) and honours the active sort key. Siblings are
    // always the same kind (the comparator only sees one folder's children).
    if (a->getItemType() == ALSceneExplorerItem::TYPE_PRIM)
        return a->getLinkOrder() < b->getLinkOrder();

    const ALObjectProperties::Record& ra = a->getRecord();
    const ALObjectProperties::Record& rb = b->getRecord();

    switch (mMode)
    {
    case SORT_NAME:
        break;
    case SORT_LAND_IMPACT:
        if (ra.mLandImpact != rb.mLandImpact)
            return ra.mLandImpact > rb.mLandImpact;
        break;
    case SORT_TRIANGLES:
        if (ra.mNumTriangles != rb.mNumTriangles)
            return ra.mNumTriangles > rb.mNumTriangles;
        break;
    case SORT_TYPE:
        if (ra.mGeom != rb.mGeom)
            return ra.mGeom < rb.mGeom;
        break;
    case SORT_REGION_ORIGIN:
    {
        // Agent-independent ordering for builders: distance from the region's
        // <0,0,0>, computed from the live region position.
        const F32 da = ra.mPosRegion.magVecSquared();
        const F32 db = rb.mPosRegion.magVecSquared();
        if (da != db)
            return da < db;
        break;
    }
    case SORT_DISTANCE:
    default:
        if (ra.mDistance != rb.mDistance)
            return ra.mDistance < rb.mDistance;
        break;
    }

    return LLStringUtil::compareInsensitive(a->getName(), b->getName()) < 0;
}

// ============================================================================
// ALSceneExplorerFilter
// ============================================================================

// The UI-facing enums must mirror the pure predicate's numeric twins; the
// floater persists and casts the raw values.
static_assert((U32)ALSceneExplorerFilter::SEARCH_NAME == (U32)ALSceneExplorerPredicate::SEARCH_NAME);
static_assert((U32)ALSceneExplorerFilter::SEARCH_DESCRIPTION == (U32)ALSceneExplorerPredicate::SEARCH_DESCRIPTION);
static_assert((U32)ALSceneExplorerFilter::SEARCH_OWNER == (U32)ALSceneExplorerPredicate::SEARCH_OWNER);
static_assert((U32)ALSceneExplorerFilter::SEARCH_UUID == (U32)ALSceneExplorerPredicate::SEARCH_UUID);
static_assert((U32)ALSceneExplorerFilter::SEARCH_ALL == (U32)ALSceneExplorerPredicate::SEARCH_ALL);
static_assert((U32)ALSceneExplorerFilter::OWNER_ANY == (U32)ALSceneExplorerPredicate::OWNER_ANY);
static_assert((U32)ALSceneExplorerFilter::OWNER_MINE == (U32)ALSceneExplorerPredicate::OWNER_MINE);
static_assert((U32)ALSceneExplorerFilter::OWNER_GROUP == (U32)ALSceneExplorerPredicate::OWNER_GROUP);
static_assert((U32)ALSceneExplorerFilter::OWNER_OTHERS == (U32)ALSceneExplorerPredicate::OWNER_OTHERS);
static_assert((U32)ALSceneExplorerFilter::OWNER_SPECIFIC == (U32)ALSceneExplorerPredicate::OWNER_SPECIFIC);
static_assert((U32)ALSceneExplorerFilter::SCOPE_REGION == (U32)ALSceneExplorerPredicate::SCOPE_REGION);
static_assert((U32)ALSceneExplorerFilter::SCOPE_PARCEL == (U32)ALSceneExplorerPredicate::SCOPE_PARCEL);
static_assert((U32)ALSceneExplorerFilter::SCOPE_RADIUS == (U32)ALSceneExplorerPredicate::SCOPE_RADIUS);

ALSceneExplorerFilter::ALSceneExplorerFilter()
:   mName("scene_explorer")
{
}

void ALSceneExplorerFilter::applyChange(ALSceneExplorerPredicate::EFilterChange change)
{
    switch (change)
    {
    case ALSceneExplorerPredicate::CHANGE_NONE:
        return;
    case ALSceneExplorerPredicate::CHANGE_MORE_RESTRICTIVE:
        setModified(FILTER_MORE_RESTRICTIVE);
        return;
    case ALSceneExplorerPredicate::CHANGE_LESS_RESTRICTIVE:
        setModified(FILTER_LESS_RESTRICTIVE);
        return;
    case ALSceneExplorerPredicate::CHANGE_RESTART:
    default:
        setModified(FILTER_RESTART);
        return;
    }
}

void ALSceneExplorerFilter::setFilterSubString(const std::string& string)
{
    std::string lower(string);
    LLStringUtil::toLower(lower);
    if (lower != mConstraints.mFilterSubString)
    {
        const ALSceneExplorerPredicate::EFilterChange change =
            ALSceneExplorerPredicate::classifySubstringChange(mConstraints.mFilterSubString, lower);
        mConstraints.mFilterSubString = lower;
        applyChange(change);
    }
}

void ALSceneExplorerFilter::setSearchType(ESearchType type)
{
    if ((U32)type != mConstraints.mSearchType)
    {
        mConstraints.mSearchType = (U32)type;
        // A different field is an unrelated predicate; only matters while a
        // text predicate is set.
        if (!mConstraints.mFilterSubString.empty())
            setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setOwnerMode(EOwnerMode mode)
{
    if ((U32)mode != mConstraints.mOwnerMode)
    {
        // ANY passes everything, so entering a mode only narrows and leaving
        // one only widens; switching between two real modes is unrelated.
        const bool was_any = (mConstraints.mOwnerMode == (U32)OWNER_ANY);
        mConstraints.mOwnerMode = (U32)mode;
        if (was_any)
            setModified(FILTER_MORE_RESTRICTIVE);
        else if (mode == OWNER_ANY)
            setModified(FILTER_LESS_RESTRICTIVE);
        else
            setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setOwnerId(const LLUUID& id)
{
    if (id != mConstraints.mOwnerId)
    {
        mConstraints.mOwnerId = id;
        // A different target invalidates passes and fails alike.
        if (mConstraints.mOwnerMode == (U32)OWNER_SPECIFIC)
            setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setGeomMask(U32 mask)
{
    if (mask != mConstraints.mGeomMask)
    {
        const ALSceneExplorerPredicate::EFilterChange change =
            ALSceneExplorerPredicate::classifyAllowedMaskChange(mConstraints.mGeomMask, mask);
        mConstraints.mGeomMask = mask;
        applyChange(change);
    }
}

void ALSceneExplorerFilter::setFlagMask(U32 mask)
{
    if (mask != mConstraints.mFlagMask)
    {
        const ALSceneExplorerPredicate::EFilterChange change =
            ALSceneExplorerPredicate::classifyRequireAllMaskChange(mConstraints.mFlagMask, mask);
        mConstraints.mFlagMask = mask;
        applyChange(change);
    }
}

void ALSceneExplorerFilter::setScope(EScope scope, F32 radius)
{
    const EScope old_scope = (EScope)mConstraints.mScope;
    const F32 old_radius = mConstraints.mRadius;
    mConstraints.mScope = (U32)scope;
    mConstraints.mRadius = radius; // kept current even while the scope is off

    if (scope == old_scope)
    {
        // Same scope: only a radius change while the radius scope is active
        // affects results, and it is monotonic.
        if (scope == SCOPE_RADIUS && radius != old_radius)
        {
            setModified(radius < old_radius ? FILTER_MORE_RESTRICTIVE
                                            : FILTER_LESS_RESTRICTIVE);
        }
        return;
    }
    // REGION passes everything spatial, so entering a spatial scope narrows
    // and leaving one widens; switching between parcel and radius is
    // unrelated.
    if (old_scope == SCOPE_REGION)
        setModified(FILTER_MORE_RESTRICTIVE);
    else if (scope == SCOPE_REGION)
        setModified(FILTER_LESS_RESTRICTIVE);
    else
        setModified(FILTER_RESTART);
}

void ALSceneExplorerFilter::setMinLandImpact(F32 min_li)
{
    if (min_li != mConstraints.mMinLandImpact)
    {
        const ALSceneExplorerPredicate::EFilterChange change =
            ALSceneExplorerPredicate::classifyMinThresholdChange(mConstraints.mMinLandImpact, min_li);
        mConstraints.mMinLandImpact = min_li;
        applyChange(change);
    }
}

void ALSceneExplorerFilter::setMinTriangles(U32 min_tris)
{
    if (min_tris != mConstraints.mMinTriangles)
    {
        const ALSceneExplorerPredicate::EFilterChange change =
            ALSceneExplorerPredicate::classifyMinThresholdChange(
                (F32)mConstraints.mMinTriangles, (F32)min_tris);
        mConstraints.mMinTriangles = min_tris;
        applyChange(change);
    }
}

bool ALSceneExplorerFilter::isActive() const
{
    return !mConstraints.mFilterSubString.empty()
        || mConstraints.mOwnerMode != (U32)OWNER_ANY
        || mConstraints.mGeomMask != 0
        || mConstraints.mFlagMask != 0
        || mConstraints.mScope != (U32)SCOPE_REGION
        || mConstraints.mMinLandImpact > 0.f
        || mConstraints.mMinTriangles > 0;
}

void ALSceneExplorerFilter::setModified(EFilterModified behavior)
{
    mModified = true;
    ++mCurrentGeneration;

    // Merge with whatever kind of change is already pending this refilter:
    // two different kinds in one batch degrade to a restart (inventory's
    // rule). clearModified() resets the batch once the refilter completes.
    if (mFilterModified == FILTER_NONE)
        mFilterModified = behavior;
    else if (mFilterModified != behavior)
        mFilterModified = FILTER_RESTART;

    switch (mFilterModified)
    {
    case FILTER_MORE_RESTRICTIVE:
        // Old passes need revalidating; old fails certainly still fail, so
        // first-required stays put and they can be re-stamped cheaply.
        mFirstSuccessGeneration = mCurrentGeneration;
        break;
    case FILTER_LESS_RESTRICTIVE:
        // Old fails need revalidating; old passes certainly still pass, so
        // first-success stays put and the visible set never blanks out.
        mFirstRequiredGeneration = mCurrentGeneration;
        break;
    case FILTER_RESTART:
    default:
        mFirstRequiredGeneration = mCurrentGeneration;
        mFirstSuccessGeneration = mCurrentGeneration;
        break;
    }
}

// Only a name match can be highlighted: the widget draws the highlight within
// its displayed label, and the name is the only searched field the label shows
// (description/UUID/owner matches return no match). The search copy is
// lowercased, and lowercasing UTF-8 changes byte lengths, so the raw find
// offset does not index the label.
LLFolderViewFilter::Match ALSceneExplorerFilter::getFilterMatch(LLFolderViewModelItem* item) const
{
    Match match;
    if (mConstraints.mFilterSubString.empty())
        return match;
    if (mConstraints.mSearchType != (U32)SEARCH_NAME
        && mConstraints.mSearchType != (U32)SEARCH_ALL)
    {
        return match;
    }

    const std::string::size_type at =
        static_cast<ALSceneExplorerItem*>(item)->searchName().find(mConstraints.mFilterSubString);
    if (at == std::string::npos)
        return match;

    const std::string& label = item->getDisplayName();

    // All-ASCII means lowercasing moved nothing, so the offset already indexes
    // the label as it stands.
    if (utf8str_is_ascii(label) && utf8str_is_ascii(mConstraints.mFilterSubString))
    {
        match.mOffset = at;
        match.mLength = mConstraints.mFilterSubString.size();
        return match;
    }

    match.mOffset = utf8str_bytes_from_cased_bytes(label, at, false);
    const size_t end = utf8str_bytes_from_cased_bytes(
        label, at + mConstraints.mFilterSubString.size(), false);
    match.mLength = (end > match.mOffset) ? end - match.mOffset : 0;
    return match;
}

bool ALSceneExplorerFilter::check(const LLFolderViewModelItem* item)
{
    if (!isActive())
        return true;

    const ALSceneExplorerItem* sit = static_cast<const ALSceneExplorerItem*>(item);
    if (!sit || sit->isContainer())
        return false; // containers are only shown through matching descendants

    // Gather the per-item facts (including the impure ones the pure predicate
    // can't derive) and delegate to the unit-tested core.
    const ALObjectProperties::Record& rec = sit->getRecord();
    ALSceneExplorerPredicate::ItemFacts facts;
    facts.mRecord      = &rec;
    facts.mSearchName  = &sit->searchName();
    facts.mSearchDesc  = &sit->searchDesc();
    facts.mSearchUUID  = &sit->searchUUID();
    facts.mSearchOwner = &sit->searchOwner();
    facts.mItemId      = sit->getUUID();
    facts.mIsAvatar    = (sit->getItemType() == ALSceneExplorerItem::TYPE_AVATAR);
    facts.mIsChildPrim = (sit->getItemType() == ALSceneExplorerItem::TYPE_PRIM);
    facts.mInAgentParcel = mConstraints.mScope != (U32)SCOPE_PARCEL
        || LLViewerParcelMgr::getInstance()->inAgentParcel(rec.mPosGlobal);
    mConstraints.mAgentId = gAgentID;

    return ALSceneExplorerPredicate::matches(facts, mConstraints);
}

// ============================================================================
// ALSceneExplorerItem
// ============================================================================
ALSceneExplorerItem::ALSceneExplorerItem(EItemType type, const LLUUID& id, const std::string& name,
        LLFolderViewModelInterface& root_view_model, ALFloaterSceneExplorer* floater)
:   LLFolderViewModelItemCommon(root_view_model),
    mItemType(type),
    mUUID(id),
    mName(name),
    mSearchUUID(id.asString()), // never changes; asString() is lowercase hex
    mFloater(floater)
{
    mRecord.mId = id;
    rebuildSearchable();
}

void ALSceneExplorerItem::setName(const std::string& name)
{
    if (name != mName)
    {
        mName = name;
        rebuildSearchable();
        dirtyFilter();
    }
}

void ALSceneExplorerItem::updateRecord(const ALObjectProperties::Record& rec)
{
    updateRecord(rec, LLStringUtil::null);
}

void ALSceneExplorerItem::updateRecord(const ALObjectProperties::Record& rec, const std::string& display_name)
{
    mRecord = rec;
    mRecord.mId = mUUID;
    if (!display_name.empty())
    {
        mName = display_name;
    }
    rebuildSearchable();
    dirtyFilter();
}

void ALSceneExplorerItem::setOwnerName(const std::string& name)
{
    if (name == mOwnerDisplay)
        return;
    mOwnerDisplay = name;
    mOwnerSearch = name;
    LLStringUtil::toLower(mOwnerSearch);
    rebuildSearchable();
    dirtyFilter();
}

void ALSceneExplorerItem::rebuildSearchable()
{
    mSearchName = mName;
    LLStringUtil::toLower(mSearchName);
    mSearchDesc = mRecord.mDescription;
    LLStringUtil::toLower(mSearchDesc);
    // mSearchUUID is fixed at construction; mOwnerSearch is set by the
    // floater's owner-name resolution.
}

LLPointer<LLUIImage> ALSceneExplorerItem::getIcon() const
{
    return LLUI::getUIImage(ALObjectProperties::iconName(mRecord));
}

LLFontGL::StyleFlags ALSceneExplorerItem::getLabelStyle() const
{
    return LLFontGL::NORMAL;
}

std::string ALSceneExplorerItem::getLabelSuffix() const
{
    if (isContainer())
        return LLStringUtil::null;

    // Derendered entries have no live metrics; objects keep their stored
    // position (distance), region-less avatar entries show nothing.
    if (mItemType == TYPE_DERENDERED_AVATAR)
        return LLStringUtil::null;
    if (mItemType == TYPE_DERENDERED_OBJECT)
        return llformat("%.0fm", mRecord.mDistance);

    // Avatar rows show render complexity + worn attachment count (stored in
    // mRenderCost / mPrimCount by the reconcile pass) instead of object costs.
    if (mItemType == TYPE_AVATAR)
    {
        std::string suffix = llformat("%.0fm", mRecord.mDistance);
        if (mRecord.mRenderCost > 0.f)
            suffix += llformat("  cmplx %.0fk", mRecord.mRenderCost / 1000.f);
        if (mRecord.mPrimCount > 0)
            suffix += llformat("  %d att", mRecord.mPrimCount);
        return suffix;
    }

    std::string suffix = llformat("%.0fm", mRecord.mDistance);
    if (mItemType == TYPE_LINKSET && mRecord.mLandImpact > 0.f)
    {
        suffix += llformat("  LI %.0f", mRecord.mLandImpact);
    }
    if (mRecord.mNumTriangles > 0)
    {
        suffix += llformat("  %u tris", mRecord.mNumTriangles);
    }
    // Owner display name once resolved (toggleable from the eye menu).
    static LLCachedControl<bool> show_owner(gSavedSettings, "ALSceneExplorerOwnerSuffix", true);
    if (show_owner && !mOwnerDisplay.empty())
    {
        suffix += "  (" + mOwnerDisplay + ")";
    }
    return suffix;
}

std::string ALSceneExplorerItem::getTooltip() const
{
    if (isContainer())
        return mName;

    std::string tip = mName;
    if (!mRecord.mDescription.empty())
        tip += "\n" + mRecord.mDescription;
    tip += llformat("\nDistance: %.0f m", mRecord.mDistance);

    if (mItemType == TYPE_AVATAR)
    {
        if (mRecord.mRenderCost > 0.f)
            tip += llformat("\nComplexity: %.0f", mRecord.mRenderCost);
        if (mRecord.mPrimCount > 0)
            tip += llformat("\nAttachments: %d", mRecord.mPrimCount);
        return tip;
    }
    if (isDerenderedType())
    {
        tip += "\n(derendered)";
        return tip;
    }

    if (mRecord.mPrimCount > 1)
        tip += llformat("\nPrims: %d", mRecord.mPrimCount);
    if (mRecord.mLandImpact > 0.f)
        tip += llformat("\nLand impact: %.0f", mRecord.mLandImpact);
    if (mRecord.mRenderCost > 0.f)
        tip += llformat("\nRender cost: %.0f", mRecord.mRenderCost);
    if (mRecord.mNumTriangles > 0)
        tip += llformat("\nTriangles: %u  (%u verts, %d faces)",
                        mRecord.mNumTriangles, mRecord.mNumVertices, mRecord.mNumFaces);
    const std::string flags = ALObjectProperties::flagsToString(mRecord.mFlags);
    if (!flags.empty())
        tip += "\nFlags: " + flags;
    return tip;
}

void ALSceneExplorerItem::activate()
{
    if (mFloater)
    {
        mFloater->activateItem(mUUID);
    }
}

void ALSceneExplorerItem::buildContextMenu(LLMenuGL& menu, U32 flags)
{
    if (mFloater)
    {
        mFloater->buildRowContextMenu(menu, flags);
    }
}

void ALSceneExplorerItem::openItem(void)
{
    // openItem() fires from leaf double-click AND from
    // LLFolderViewFolder::setOpenArrangeRecursively whenever a folder is
    // expanded (disclosure arrow, keyboard, ancestor auto-open). Expanding a
    // linkset must never move the camera or open an editor, so only leaf prims
    // activate here; folder-typed scene objects activate from
    // ALSceneExplorerFolder::handleDoubleClick.
    if (mItemType == TYPE_PRIM)
    {
        activate();
    }
}

void ALSceneExplorerItem::setPassedFilter(bool passed, S32 filter_generation,
                                          std::string::size_type string_offset,
                                          std::string::size_type string_size)
{
    // Mirrors LLFolderViewModelItemInventory::setPassedFilter: ask the parent
    // folder to re-arrange when this item's filtered state (or its validity
    // after a generation bump) changed. arrange() is the only thing that
    // applies pass/fail state to widget visibility, and nothing else re-arms
    // it on filter changes — without this, toggling a filter only takes
    // visual effect when some unrelated event happens to arrange the tree.
    const bool generation_skip = mMarkedDirtyGeneration >= 0
        && mPrevPassedAllFilters
        && mMarkedDirtyGeneration < mRootViewModel.getFilter().getFirstSuccessGeneration();
    const S32 last_generation = mLastFilterGeneration;
    LLFolderViewModelItemCommon::setPassedFilter(passed, filter_generation, string_offset, string_size);
    const bool before = mPrevPassedAllFilters;
    mPrevPassedAllFilters = passedFilter(filter_generation);

    if (before != mPrevPassedAllFilters   // change of state
        || generation_skip                // was marked dirty while passing
        // (Re)stamped as passing after first-success moved past this item's
        // last stamp: its visibility-by-generation had been invalidated (a
        // more-restrictive change or restart), so it must arrange back in
        // even though it passed before too. Deliberately keyed on
        // first-SUCCESS, not inventory's first-required: required doesn't
        // move on more-restrictive changes, which would leave rows hidden by
        // a mid-refilter arrange with nothing to bring them back.
        || (mPrevPassedAllFilters
            && last_generation < mRootViewModel.getFilter().getFirstSuccessGeneration()))
    {
        LLFolderViewFolder* parent_folder =
            mFolderViewItem ? mFolderViewItem->getParentFolder() : nullptr;
        if (parent_folder)
        {
            parent_folder->requestArrange();
        }
    }
}

bool ALSceneExplorerItem::filter(LLFolderViewFilter& filter)
{
    const S32 filter_generation = filter.getCurrentGeneration();
    const S32 must_pass_generation = filter.getFirstRequiredGeneration();

    if (getLastFilterGeneration() >= must_pass_generation
        && getLastFolderFilterGeneration() >= must_pass_generation
        && !passedFilter(must_pass_generation))
    {
        // Already failed a filter at least as strict as this one.
        setPassedFilter(false, filter_generation);
        setPassedFolderFilter(false, filter_generation);
        return true;
    }

    const bool passed_filter_folder = isFolderType() ? filter.checkFolder(this) : true;
    setPassedFolderFilter(passed_filter_folder, filter_generation);

    bool continue_filtering = true;
    if (!mChildren.empty()
        && (getLastFilterGeneration() < must_pass_generation
            || descendantsPassedFilter(must_pass_generation)))
    {
        for (auto& childp : mChildren)
        {
            ALSceneExplorerItem* child = static_cast<ALSceneExplorerItem*>(childp.get());
            if (child->getLastFilterGeneration() < filter_generation)
            {
                // Child returns false when the per-pass time budget is spent;
                // stop here and resume from this child on the next idle.
                continue_filtering = child->filter(filter);
            }
            if (child->passedFilter())
            {
                ALSceneExplorerItem* vm = this;
                while (vm && vm->mMostFilteredDescendantGeneration < filter_generation)
                {
                    vm->mMostFilteredDescendantGeneration = filter_generation;
                    vm = static_cast<ALSceneExplorerItem*>(vm->mParent);
                }
            }
            if (!continue_filtering)
                break;
        }
    }

    if (continue_filtering)
    {
        const bool passed = filter.check(this);
        // The match span makes the folder view draw the standard
        // inventory-style highlight over the matched substring.
        const LLFolderViewFilter::Match match = filter.getFilterMatch(this);
        setPassedFilter(passed, filter_generation,
                        match.mOffset, match.mLength);
        continue_filtering = !filter.isTimedOut();
    }

    return continue_filtering;
}

// ============================================================================
// ALSceneExplorerFolder
// ============================================================================
bool ALSceneExplorerFolder::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    ALSceneExplorerItem* item = static_cast<ALSceneExplorerItem*>(getViewModelItem());

    // Childless scene objects (single-prim roots) have nothing to expand, so
    // double-click activates them the way a leaf prim does. Everything else —
    // containers, multi-prim linksets, avatars — keeps the standard
    // expand/collapse double-click; activation for those lives in the action
    // buttons and context menu. (activate() rather than openItem(): the model
    // openItem() also fires on folder expansion and must stay inert here.)
    if (item && !item->isContainer() && item->getChildrenCount() == 0)
    {
        if (getRoot())
            getRoot()->setSelection(this, false);
        item->activate();
        return true;
    }
    return LLFolderViewFolder::handleDoubleClick(x, y, mask);
}

namespace
{
    // Shared rich-tooltip display for both widget flavours: query the model on
    // demand so the contents are always current.
    bool showSceneTooltip(LLFolderViewItem* widget)
    {
        ALSceneExplorerItem* item = static_cast<ALSceneExplorerItem*>(widget->getViewModelItem());
        if (!item || item->isContainer())
            return false;
        LLToolTip::Params params;
        params.message = item->getTooltip();
        params.sticky_rect = widget->calcScreenRect();
        LLToolTipMgr::instance().show(params);
        return true;
    }
}

bool ALSceneExplorerFolder::handleToolTip(S32 x, S32 y, MASK mask)
{
    // An open folder's children own their own tooltips.
    if (isOpen() && LLView::childrenHandleToolTip(x, y, mask) != nullptr)
        return true;
    if (showSceneTooltip(this))
        return true;
    return LLFolderViewFolder::handleToolTip(x, y, mask);
}

bool ALSceneExplorerListItem::handleToolTip(S32 x, S32 y, MASK mask)
{
    if (showSceneTooltip(this))
        return true;
    return LLFolderViewItem::handleToolTip(x, y, mask);
}
