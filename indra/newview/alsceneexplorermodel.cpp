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
#include "llui.h"

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
ALSceneExplorerFilter::ALSceneExplorerFilter()
:   mName("scene_explorer")
{
}

void ALSceneExplorerFilter::setFilterSubString(const std::string& string)
{
    std::string lower(string);
    LLStringUtil::toLower(lower);
    if (lower != mFilterSubString)
    {
        mFilterSubString = lower;
        setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setOwnerMode(EOwnerMode mode)
{
    if (mode != mOwnerMode)
    {
        mOwnerMode = mode;
        setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setGeomMask(U32 mask)
{
    if (mask != mGeomMask)
    {
        mGeomMask = mask;
        setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setFlagMask(U32 mask)
{
    if (mask != mFlagMask)
    {
        mFlagMask = mask;
        setModified(FILTER_RESTART);
    }
}

void ALSceneExplorerFilter::setRadius(F32 radius, bool limit)
{
    if (radius != mRadius || limit != mLimitRadius)
    {
        mRadius = radius;
        mLimitRadius = limit;
        setModified(FILTER_RESTART);
    }
}

bool ALSceneExplorerFilter::isActive() const
{
    return !mFilterSubString.empty()
        || mOwnerMode != OWNER_ANY
        || mGeomMask != 0
        || mFlagMask != 0
        || mLimitRadius;
}

void ALSceneExplorerFilter::setModified(EFilterModified behavior)
{
    mModified = true;
    ++mGeneration;
}

bool ALSceneExplorerFilter::check(const LLFolderViewModelItem* item)
{
    if (!isActive())
        return true;

    const ALSceneExplorerItem* sit = static_cast<const ALSceneExplorerItem*>(item);
    if (!sit || sit->isContainer())
        return false; // containers are only shown through matching descendants

    return matches(sit);
}

bool ALSceneExplorerFilter::matches(const ALSceneExplorerItem* item) const
{
    const ALObjectProperties::Record& rec = item->getRecord();

    if (mLimitRadius && rec.mDistance > mRadius)
        return false;

    if (mGeomMask != 0 && !(mGeomMask & (1u << rec.mGeom)))
        return false;

    if (mFlagMask != 0 && (rec.mFlags & mFlagMask) != mFlagMask)
        return false;

    // Owner predicate only applies once we actually know the owner.
    if (mOwnerMode != OWNER_ANY && rec.mPropsValid)
    {
        switch (mOwnerMode)
        {
        case OWNER_MINE:   if (rec.mOwnerId != gAgentID) return false; break;
        case OWNER_GROUP:  if (!rec.mGroupOwned) return false; break;
        case OWNER_OTHERS: if (rec.mOwnerId == gAgentID || rec.mGroupOwned) return false; break;
        default: break;
        }
    }

    if (!mFilterSubString.empty()
        && item->getSearchableText().find(mFilterSubString) == std::string::npos)
    {
        return false;
    }

    return true;
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

void ALSceneExplorerItem::rebuildSearchable()
{
    mSearchable = mName + " " + mRecord.mDescription + " " + mUUID.asString();
    LLStringUtil::toLower(mSearchable);
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

    std::string suffix = llformat("%.0fm", mRecord.mDistance);
    if ((mItemType == TYPE_LINKSET || mItemType == TYPE_AVATAR) && mRecord.mLandImpact > 0.f)
    {
        suffix += llformat("  LI %.0f", mRecord.mLandImpact);
    }
    if (mRecord.mNumTriangles > 0)
    {
        suffix += llformat("  %u tris", mRecord.mNumTriangles);
    }
    return suffix;
}

void ALSceneExplorerItem::activate()
{
    if (mFloater)
    {
        mFloater->activateItem(mUUID);
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
        setPassedFilter(passed, filter_generation);
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

    // Structural containers (region / category / attachment point) keep the
    // standard expand-on-double-click behaviour.
    if (!item || item->isContainer())
        return LLFolderViewFolder::handleDoubleClick(x, y, mask);

    // A real scene object: let an open folder's children claim the click first,
    // otherwise act on the object (focus/edit/inspect per the activate-action
    // setting) the way a leaf prim does, so single- and multi-prim objects
    // behave identically. The disclosure arrow still expands children. Note:
    // activate() rather than openItem() — the latter also fires on folder
    // expansion and is therefore a no-op for folder-typed scene objects.
    if (isOpen() && childrenHandleDoubleClick(x, y, mask) != nullptr)
        return true;

    if (getRoot())
        getRoot()->setSelection(this, false);
    item->activate();
    return true;
}
