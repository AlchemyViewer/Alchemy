/**
 * @file alsceneexplorerpredicate_test.cpp
 * @brief Unit tests for the Scene Explorer's pure filter predicate
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

#include "../test/lltut.h"

#include "../alsceneexplorerpredicate.h"

namespace tut
{
    using namespace ALSceneExplorerPredicate;

    struct predicate_data
    {
        ALObjectProperties::Record mRec;
        std::string mName;
        std::string mDesc;
        std::string mUUID;
        std::string mOwner;
        ItemFacts   mFacts;
        Constraints mC;

        LLUUID mAgentId;
        LLUUID mOtherId;
        LLUUID mGroupId;

        predicate_data()
        {
            mAgentId.generate();
            mOtherId.generate();
            mGroupId.generate();

            mRec.mId.generate();
            mRec.mGeom = ALObjectProperties::GEOM_MESH;
            mRec.mFlags = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_LIGHT;
            mRec.mDistance = 25.f;
            mRec.mLandImpact = 40.f;
            mRec.mNumTriangles = 12000;
            mRec.mPropsValid = true;
            mRec.mOwnerId = mOtherId;

            // Pre-lowercased search copies, as the model items keep them.
            mName  = "old oak tree";
            mDesc  = "a weathered prop";
            mUUID  = mRec.mId.asString();
            mOwner = "rye mutt";

            mFacts.mRecord      = &mRec;
            mFacts.mSearchName  = &mName;
            mFacts.mSearchDesc  = &mDesc;
            mFacts.mSearchUUID  = &mUUID;
            mFacts.mSearchOwner = &mOwner;
            mFacts.mItemId      = mRec.mId;

            mC.mAgentId = mAgentId;
        }
    };

    typedef test_group<predicate_data> predicate_group;
    typedef predicate_group::object    predicate_object;
    tut::predicate_group pred("ALSceneExplorerPredicate");

    // Default constraints match anything (and a null record never does).
    template<> template<>
    void predicate_object::test<1>()
    {
        ensure("defaults match", matches(mFacts, mC));

        ItemFacts no_record;
        ensure("null record never matches", !matches(no_record, mC));
    }

    // Name search: targeted field only; caller pre-lowercases both sides.
    template<> template<>
    void predicate_object::test<2>()
    {
        mC.mSearchType = SEARCH_NAME;
        mC.mFilterSubString = "oak";
        ensure("name substring matches", matches(mFacts, mC));

        mC.mFilterSubString = "weathered"; // description text
        ensure("name search ignores description", !matches(mFacts, mC));
    }

    // Description / UUID / owner searches hit only their own field.
    template<> template<>
    void predicate_object::test<3>()
    {
        mC.mSearchType = SEARCH_DESCRIPTION;
        mC.mFilterSubString = "weathered";
        ensure("description matches", matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        ensure("description search ignores name", !matches(mFacts, mC));

        mC.mSearchType = SEARCH_UUID;
        mC.mFilterSubString = mUUID.substr(0, 8);
        ensure("uuid prefix matches", matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        ensure("uuid search ignores name", !matches(mFacts, mC));

        mC.mSearchType = SEARCH_OWNER;
        mC.mFilterSubString = "rye";
        ensure("owner matches", matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        ensure("owner search ignores name", !matches(mFacts, mC));
    }

    // ALL searches every field; a miss everywhere fails.
    template<> template<>
    void predicate_object::test<4>()
    {
        mC.mSearchType = SEARCH_ALL;
        for (const char* needle : { "oak", "weathered", "rye" })
        {
            mC.mFilterSubString = needle;
            ensure(std::string("all-search finds ") + needle, matches(mFacts, mC));
        }
        mC.mFilterSubString = mUUID.substr(4, 8);
        ensure("all-search finds uuid", matches(mFacts, mC));
        mC.mFilterSubString = "no such text";
        ensure("all-search can fail", !matches(mFacts, mC));
    }

    // Owner modes for an owned object.
    template<> template<>
    void predicate_object::test<5>()
    {
        mC.mOwnerMode = OWNER_MINE;
        ensure("someone else's object is not mine", !matches(mFacts, mC));
        mRec.mOwnerId = mAgentId;
        ensure("my object is mine", matches(mFacts, mC));

        mC.mOwnerMode = OWNER_OTHERS;
        ensure("my object is not others'", !matches(mFacts, mC));
        mRec.mOwnerId = mOtherId;
        ensure("their object is others'", matches(mFacts, mC));

        mC.mOwnerMode = OWNER_GROUP;
        ensure("user-owned is not group-owned", !matches(mFacts, mC));
        mRec.mGroupOwned = true;
        mRec.mGroupId = mGroupId;
        ensure("group-owned passes group mode", matches(mFacts, mC));
        mC.mOwnerMode = OWNER_OTHERS;
        ensure("group-owned is excluded from others'", !matches(mFacts, mC));
    }

    // OWNER_SPECIFIC: direct owner or the deeding group.
    template<> template<>
    void predicate_object::test<6>()
    {
        mC.mOwnerMode = OWNER_SPECIFIC;
        mC.mOwnerId = mOtherId;
        ensure("specific owner matches", matches(mFacts, mC));
        mC.mOwnerId = mAgentId;
        ensure("specific owner mismatch fails", !matches(mFacts, mC));

        mRec.mGroupOwned = true;
        mRec.mGroupId = mGroupId;
        mC.mOwnerId = mGroupId;
        ensure("specific deeding group matches", matches(mFacts, mC));
    }

    // Owner gating: avatars are their own owner; objects without props yet
    // fail owner modes until the reply lands.
    template<> template<>
    void predicate_object::test<7>()
    {
        mFacts.mIsAvatar = true;
        mFacts.mItemId = mAgentId;
        mC.mOwnerMode = OWNER_MINE;
        ensure("own avatar row is mine", matches(mFacts, mC));
        mFacts.mItemId = mOtherId;
        ensure("other avatar row is not mine", !matches(mFacts, mC));

        mFacts.mIsAvatar = false;
        mFacts.mItemId = mRec.mId;
        mRec.mPropsValid = false;
        ensure("unresolved props fail owner modes", !matches(mFacts, mC));

        mRec.mPropsValid = true;
        mRec.mOwnerId = mAgentId;
        ensure("resolved props re-apply owner modes", matches(mFacts, mC));

        mRec.mPropsValid = false;
        mC.mOwnerMode = OWNER_ANY;
        ensure("unresolved props pass without an owner mode", matches(mFacts, mC));
    }

    // Geometry mask: empty = any; otherwise the item's kind must be in it.
    template<> template<>
    void predicate_object::test<8>()
    {
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_MESH;
        ensure("mesh passes mesh mask", matches(mFacts, mC));
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_PRIM;
        ensure("mesh fails prim-only mask", !matches(mFacts, mC));
        mC.mGeomMask = (1u << ALObjectProperties::GEOM_PRIM) | (1u << ALObjectProperties::GEOM_MESH);
        ensure("mesh passes prim|mesh mask", matches(mFacts, mC));
    }

    // Flag mask requires ALL set bits.
    template<> template<>
    void predicate_object::test<9>()
    {
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED;
        ensure("single present flag passes", matches(mFacts, mC));
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_LIGHT;
        ensure("two present flags pass", matches(mFacts, mC));
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_PHANTOM;
        ensure("one missing flag fails the AND", !matches(mFacts, mC));

        mRec.mFlags |= ALObjectProperties::FLAG_FOR_SALE | ALObjectProperties::FLAG_PAYABLE;
        mC.mFlagMask = ALObjectProperties::FLAG_FOR_SALE | ALObjectProperties::FLAG_PAYABLE;
        ensure("commerce flags filter", matches(mFacts, mC));
    }

    // Scope: region ignores position; radius compares distance; parcel uses
    // the caller-resolved containment fact.
    template<> template<>
    void predicate_object::test<10>()
    {
        mRec.mDistance = 100.f;
        mC.mScope = SCOPE_REGION;
        ensure("region scope ignores distance", matches(mFacts, mC));

        mC.mScope = SCOPE_RADIUS;
        mC.mRadius = 64.f;
        ensure("beyond radius fails", !matches(mFacts, mC));
        mC.mRadius = 128.f;
        ensure("within radius passes", matches(mFacts, mC));

        mC.mScope = SCOPE_PARCEL;
        mFacts.mInAgentParcel = true;
        ensure("inside parcel passes", matches(mFacts, mC));
        mFacts.mInAgentParcel = false;
        ensure("outside parcel fails", !matches(mFacts, mC));
    }

    // Heaviness thresholds: roots gated, child prims exempt, zero = off.
    template<> template<>
    void predicate_object::test<11>()
    {
        mC.mMinLandImpact = 50.f;       // record has 40
        ensure("under LI threshold fails", !matches(mFacts, mC));
        mC.mMinLandImpact = 40.f;
        ensure("at LI threshold passes", matches(mFacts, mC));

        mC.mMinLandImpact = 0.f;
        mC.mMinTriangles = 20000;       // record has 12000
        ensure("under triangle threshold fails", !matches(mFacts, mC));
        mC.mMinTriangles = 12000;
        ensure("at triangle threshold passes", matches(mFacts, mC));

        mC.mMinTriangles = 20000;
        mFacts.mIsChildPrim = true;
        ensure("child prims are exempt from thresholds", matches(mFacts, mC));
    }

    // Change classification: substring extension/shrink (the typing and
    // backspacing cases), including the empty-needle transitions.
    template<> template<>
    void predicate_object::test<13>()
    {
        ensure_equals("same needle", classifySubstringChange("oak", "oak"), CHANGE_NONE);
        ensure_equals("typing from empty", classifySubstringChange("", "oak"), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("extending", classifySubstringChange("oak", "oak t"), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("backspacing", classifySubstringChange("oak t", "oak"), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("clearing", classifySubstringChange("oak", ""), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("unrelated text", classifySubstringChange("oak", "pine"), CHANGE_RESTART);
    }

    // Change classification: require-all mask (the feature checkboxes).
    template<> template<>
    void predicate_object::test<14>()
    {
        const U32 a = ALObjectProperties::FLAG_SCRIPTED;
        const U32 b = ALObjectProperties::FLAG_LIGHT;
        const U32 c = ALObjectProperties::FLAG_PHANTOM;
        ensure_equals("same mask", classifyRequireAllMaskChange(a | b, a | b), CHANGE_NONE);
        ensure_equals("first requirement", classifyRequireAllMaskChange(0, a), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("added requirement", classifyRequireAllMaskChange(a, a | b), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("removed requirement", classifyRequireAllMaskChange(a | b, a), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("last requirement off", classifyRequireAllMaskChange(a, 0), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("swapped requirement", classifyRequireAllMaskChange(a | b, a | c), CHANGE_RESTART);
    }

    // Change classification: allowed-set mask (the type checkboxes; 0 = any).
    template<> template<>
    void predicate_object::test<15>()
    {
        const U32 prim = 1u << ALObjectProperties::GEOM_PRIM;
        const U32 mesh = 1u << ALObjectProperties::GEOM_MESH;
        ensure_equals("same set", classifyAllowedMaskChange(prim, prim), CHANGE_NONE);
        ensure_equals("any to one kind", classifyAllowedMaskChange(0, prim), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("one kind to any", classifyAllowedMaskChange(prim, 0), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("allowing another kind", classifyAllowedMaskChange(prim, prim | mesh), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("disallowing a kind", classifyAllowedMaskChange(prim | mesh, prim), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("swapped kind", classifyAllowedMaskChange(prim, mesh), CHANGE_RESTART);
    }

    // Change classification: minimum thresholds (0 = off).
    template<> template<>
    void predicate_object::test<16>()
    {
        ensure_equals("same threshold", classifyMinThresholdChange(50.f, 50.f), CHANGE_NONE);
        ensure_equals("enabling", classifyMinThresholdChange(0.f, 50.f), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("raising", classifyMinThresholdChange(50.f, 100.f), CHANGE_MORE_RESTRICTIVE);
        ensure_equals("lowering", classifyMinThresholdChange(100.f, 50.f), CHANGE_LESS_RESTRICTIVE);
        ensure_equals("disabling", classifyMinThresholdChange(50.f, 0.f), CHANGE_LESS_RESTRICTIVE);
    }

    // Predicates compose: every constraint must hold at once.
    template<> template<>
    void predicate_object::test<12>()
    {
        mC.mSearchType = SEARCH_NAME;
        mC.mFilterSubString = "oak";
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_MESH;
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED;
        mC.mScope = SCOPE_RADIUS;
        mC.mRadius = 64.f;
        mC.mMinLandImpact = 10.f;
        ensure("all constraints satisfied", matches(mFacts, mC));

        mC.mFlagMask |= ALObjectProperties::FLAG_PHANTOM;
        ensure("one failing constraint fails the set", !matches(mFacts, mC));
    }
}
