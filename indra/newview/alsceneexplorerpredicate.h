/**
 * @file alsceneexplorerpredicate.h
 * @brief The pure, unit-testable core of the Scene Explorer's filter
 *        predicate: matches an object Record + pre-lowercased search strings
 *        against a constraint set, with no viewer-state dependencies.
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
#ifndef AL_SCENEEXPLORERPREDICATE_H
#define AL_SCENEEXPLORERPREDICATE_H

#include "alobjectproperties.h"

namespace ALSceneExplorerPredicate
{
    // Numeric twins of ALSceneExplorerFilter's UI-facing enums, kept here so
    // the predicate and its unit test need no llui/view dependencies. The
    // model static_asserts the two stay in lockstep.
    enum ESearchField : U32
    {
        SEARCH_NAME = 0,
        SEARCH_DESCRIPTION,
        SEARCH_OWNER,
        SEARCH_UUID,
        SEARCH_ALL
    };

    enum EOwnerMode : U32
    {
        OWNER_ANY = 0,
        OWNER_MINE,
        OWNER_GROUP,
        OWNER_OTHERS,
        OWNER_SPECIFIC
    };

    enum EScope : U32
    {
        SCOPE_REGION = 0,
        SCOPE_PARCEL,
        SCOPE_RADIUS
    };

    // Per-item inputs. The string pointers reference the item's
    // pre-lowercased search copies; facts the predicate cannot derive purely
    // (parcel containment) are resolved by the caller.
    struct ItemFacts
    {
        const ALObjectProperties::Record* mRecord = nullptr;
        const std::string* mSearchName  = nullptr;
        const std::string* mSearchDesc  = nullptr;
        const std::string* mSearchUUID  = nullptr;
        const std::string* mSearchOwner = nullptr;
        LLUUID mItemId;
        bool   mIsAvatar      = false;  // avatar rows count as their own owner
        bool   mIsChildPrim   = false;  // exempt from the heaviness thresholds
        bool   mInAgentParcel = true;   // resolved by the caller (impure)
    };

    // The active constraint set (the filter's persistent state).
    struct Constraints
    {
        std::string mFilterSubString;   // lowercased
        U32    mSearchType = SEARCH_NAME;
        U32    mOwnerMode  = OWNER_ANY;
        LLUUID mOwnerId;                // OWNER_SPECIFIC target
        LLUUID mAgentId;                // for OWNER_MINE / OWNER_OTHERS
        U32    mGeomMask   = 0;         // bits: 1 << EGeom; 0 = any
        U32    mFlagMask   = 0;         // EFlag bits; all set bits required
        U32    mScope      = SCOPE_REGION;
        F32    mRadius     = 64.f;
        F32    mMinLandImpact = 0.f;    // 0 = off; root-level rows only
        U32    mMinTriangles  = 0;      // 0 = off; root-level rows only
    };

    bool matches(const ItemFacts& item, const Constraints& c);

    // ------------------------------------------------------------------
    // Change classification for the multi-generation filter scheme: how a
    // single constraint change relates to its previous value. Only provably
    // monotonic cases classify; anything ambiguous is CHANGE_RESTART. The
    // predicate is a conjunction of independent clauses, so tightening or
    // loosening one clause tightens/loosens the composite.
    // ------------------------------------------------------------------
    enum EFilterChange : U32
    {
        CHANGE_NONE = 0,          // no effective change
        CHANGE_MORE_RESTRICTIVE,  // everything that failed before still fails
        CHANGE_LESS_RESTRICTIVE,  // everything that passed before still passes
        CHANGE_RESTART            // unrelated; both sides must re-evaluate
    };

    // Substring containment ("contains needle"): extending the needle is
    // stricter, shrinking it is looser (empty needle = no predicate).
    EFilterChange classifySubstringChange(const std::string& old_needle, const std::string& new_needle);

    // Require-all mask (the feature flag mask): adding required bits is
    // stricter, removing them is looser, mixing both is a restart.
    EFilterChange classifyRequireAllMaskChange(U32 old_mask, U32 new_mask);

    // Allowed-set mask (the geometry type mask; 0 = anything allowed):
    // allowing more kinds is looser, allowing fewer is stricter.
    EFilterChange classifyAllowedMaskChange(U32 old_mask, U32 new_mask);

    // Minimum threshold (0 = off): raising is stricter, lowering is looser.
    EFilterChange classifyMinThresholdChange(F32 old_min, F32 new_min);
}

#endif // AL_SCENEEXPLORERPREDICATE_H
