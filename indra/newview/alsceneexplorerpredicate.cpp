/**
 * @file alsceneexplorerpredicate.cpp
 * @brief Pure Scene Explorer filter predicate
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

#include "linden_common.h"

#include "alsceneexplorerpredicate.h"

namespace ALSceneExplorerPredicate
{

bool matches(const ItemFacts& item, const Constraints& c)
{
    if (!item.mRecord)
        return false;
    const ALObjectProperties::Record& rec = *item.mRecord;

    // Spatial scope. Parcel containment is impure (agent parcel state), so
    // the caller resolves it into the facts.
    if (c.mScope == SCOPE_RADIUS && rec.mDistance > c.mRadius)
        return false;
    if (c.mScope == SCOPE_PARCEL && !item.mInAgentParcel)
        return false;

    if (c.mGeomMask != 0 && !(c.mGeomMask & (1u << rec.mGeom)))
        return false;

    // Feature mask: every checked flag is required.
    if (c.mFlagMask != 0 && (rec.mFlags & c.mFlagMask) != c.mFlagMask)
        return false;

    // Heaviness thresholds apply to root-level rows; the children of a
    // passing linkset stay browsable (their own LI/triangles are fractions
    // of the total the threshold matched).
    if (!item.mIsChildPrim)
    {
        if (c.mMinLandImpact > 0.f && rec.mLandImpact < c.mMinLandImpact)
            return false;
        if (c.mMinTriangles > 0 && rec.mNumTriangles < c.mMinTriangles)
            return false;
    }

    if (c.mOwnerMode != OWNER_ANY)
    {
        // Avatars are their own "owner" so owner filters behave sensibly for
        // them. Objects with unresolved props stay hidden until the fetch
        // lands and the row re-filters.
        if (!item.mIsAvatar && !rec.mPropsValid)
            return false;

        const LLUUID& owner = item.mIsAvatar ? item.mItemId : rec.mOwnerId;
        const bool group_owned = !item.mIsAvatar && rec.mGroupOwned;
        switch (c.mOwnerMode)
        {
        case OWNER_MINE:   if (owner != c.mAgentId) return false; break;
        case OWNER_GROUP:  if (!group_owned) return false; break;
        case OWNER_OTHERS: if (owner == c.mAgentId || group_owned) return false; break;
        case OWNER_SPECIFIC:
            if (owner != c.mOwnerId && !(group_owned && rec.mGroupId == c.mOwnerId))
                return false;
            break;
        default: break;
        }
    }

    if (!c.mFilterSubString.empty())
    {
        auto contains = [&c](const std::string* haystack)
        {
            return haystack && haystack->find(c.mFilterSubString) != std::string::npos;
        };
        bool found = false;
        switch (c.mSearchType)
        {
        case SEARCH_DESCRIPTION: found = contains(item.mSearchDesc);  break;
        case SEARCH_OWNER:       found = contains(item.mSearchOwner); break;
        case SEARCH_UUID:        found = contains(item.mSearchUUID);  break;
        case SEARCH_ALL:
            found = contains(item.mSearchName) || contains(item.mSearchDesc)
                 || contains(item.mSearchUUID) || contains(item.mSearchOwner);
            break;
        case SEARCH_NAME:
        default:                 found = contains(item.mSearchName);  break;
        }
        if (!found)
            return false;
    }

    return true;
}

EFilterChange classifySubstringChange(const std::string& old_needle, const std::string& new_needle)
{
    if (old_needle == new_needle)
        return CHANGE_NONE;
    // The predicate is "haystack contains needle". If the old needle occurs
    // inside the new one, any haystack containing the new needle also
    // contains the old — strictly fewer matches. An empty needle (matches
    // everything) classifies through find() naturally for both directions.
    if (new_needle.find(old_needle) != std::string::npos)
        return CHANGE_MORE_RESTRICTIVE;
    if (old_needle.find(new_needle) != std::string::npos)
        return CHANGE_LESS_RESTRICTIVE;
    return CHANGE_RESTART;
}

EFilterChange classifyRequireAllMaskChange(U32 old_mask, U32 new_mask)
{
    if (old_mask == new_mask)
        return CHANGE_NONE;
    // All previously required bits still required, plus more.
    if ((new_mask & old_mask) == old_mask)
        return CHANGE_MORE_RESTRICTIVE;
    // A subset of the previous requirements.
    if ((new_mask & old_mask) == new_mask)
        return CHANGE_LESS_RESTRICTIVE;
    return CHANGE_RESTART;
}

EFilterChange classifyAllowedMaskChange(U32 old_mask, U32 new_mask)
{
    if (old_mask == new_mask)
        return CHANGE_NONE;
    // 0 means "anything allowed" — treat it as the full set.
    const U32 old_allowed = old_mask ? old_mask : ~0u;
    const U32 new_allowed = new_mask ? new_mask : ~0u;
    if (old_allowed == new_allowed)
        return CHANGE_NONE;
    // Everything previously allowed is still allowed, plus more.
    if ((new_allowed & old_allowed) == old_allowed)
        return CHANGE_LESS_RESTRICTIVE;
    if ((new_allowed & old_allowed) == new_allowed)
        return CHANGE_MORE_RESTRICTIVE;
    return CHANGE_RESTART;
}

EFilterChange classifyMinThresholdChange(F32 old_min, F32 new_min)
{
    if (old_min == new_min)
        return CHANGE_NONE;
    return (new_min > old_min) ? CHANGE_MORE_RESTRICTIVE : CHANGE_LESS_RESTRICTIVE;
}

} // namespace ALSceneExplorerPredicate
