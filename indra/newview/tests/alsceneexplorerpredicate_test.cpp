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

#include <catch2/catch_test_macros.hpp>

#include "../alsceneexplorerpredicate.h"

namespace
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

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: default constraints match anything but a null record", "[ALSceneExplorerPredicate]")
    {
        UNSCOPED_INFO("defaults match");
        CHECK(matches(mFacts, mC));

        ItemFacts no_record;
        UNSCOPED_INFO("null record never matches");
        CHECK_FALSE(matches(no_record, mC));
    }

    // Name search: targeted field only; caller pre-lowercases both sides.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: a name search looks in the name and nowhere else", "[ALSceneExplorerPredicate]")
    {
        mC.mSearchType = SEARCH_NAME;
        mC.mFilterSubString = "oak";
        UNSCOPED_INFO("name substring matches");
        CHECK(matches(mFacts, mC));

        mC.mFilterSubString = "weathered"; // description text
        UNSCOPED_INFO("name search ignores description");
        CHECK_FALSE(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: description / UUID / owner searches hit only their own field", "[ALSceneExplorerPredicate]")
    {
        mC.mSearchType = SEARCH_DESCRIPTION;
        mC.mFilterSubString = "weathered";
        UNSCOPED_INFO("description matches");
        CHECK(matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        UNSCOPED_INFO("description search ignores name");
        CHECK_FALSE(matches(mFacts, mC));

        mC.mSearchType = SEARCH_UUID;
        mC.mFilterSubString = mUUID.substr(0, 8);
        UNSCOPED_INFO("uuid prefix matches");
        CHECK(matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        UNSCOPED_INFO("uuid search ignores name");
        CHECK_FALSE(matches(mFacts, mC));

        mC.mSearchType = SEARCH_OWNER;
        mC.mFilterSubString = "rye";
        UNSCOPED_INFO("owner matches");
        CHECK(matches(mFacts, mC));
        mC.mFilterSubString = "oak";
        UNSCOPED_INFO("owner search ignores name");
        CHECK_FALSE(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: SEARCH_ALL finds text in any field and fails a miss in all", "[ALSceneExplorerPredicate]")
    {
        mC.mSearchType = SEARCH_ALL;
        for (const char* needle : { "oak", "weathered", "rye" })
        {
            mC.mFilterSubString = needle;
            CAPTURE(needle);
            REQUIRE(matches(mFacts, mC));
        }
        mC.mFilterSubString = mUUID.substr(4, 8);
        UNSCOPED_INFO("all-search finds uuid");
        CHECK(matches(mFacts, mC));
        mC.mFilterSubString = "no such text";
        UNSCOPED_INFO("all-search can fail");
        CHECK_FALSE(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: mine / others / group owner modes for an owned object", "[ALSceneExplorerPredicate]")
    {
        mC.mOwnerMode = OWNER_MINE;
        UNSCOPED_INFO("someone else's object is not mine");
        CHECK_FALSE(matches(mFacts, mC));
        mRec.mOwnerId = mAgentId;
        UNSCOPED_INFO("my object is mine");
        CHECK(matches(mFacts, mC));

        mC.mOwnerMode = OWNER_OTHERS;
        UNSCOPED_INFO("my object is not others'");
        CHECK_FALSE(matches(mFacts, mC));
        mRec.mOwnerId = mOtherId;
        UNSCOPED_INFO("their object is others'");
        CHECK(matches(mFacts, mC));

        mC.mOwnerMode = OWNER_GROUP;
        UNSCOPED_INFO("user-owned is not group-owned");
        CHECK_FALSE(matches(mFacts, mC));
        mRec.mGroupOwned = true;
        mRec.mGroupId = mGroupId;
        UNSCOPED_INFO("group-owned passes group mode");
        CHECK(matches(mFacts, mC));
        mC.mOwnerMode = OWNER_OTHERS;
        UNSCOPED_INFO("group-owned is excluded from others'");
        CHECK_FALSE(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: OWNER_SPECIFIC matches the direct owner or the deeding group", "[ALSceneExplorerPredicate]")
    {
        mC.mOwnerMode = OWNER_SPECIFIC;
        mC.mOwnerId = mOtherId;
        UNSCOPED_INFO("specific owner matches");
        CHECK(matches(mFacts, mC));
        mC.mOwnerId = mAgentId;
        UNSCOPED_INFO("specific owner mismatch fails");
        CHECK_FALSE(matches(mFacts, mC));

        mRec.mGroupOwned = true;
        mRec.mGroupId = mGroupId;
        mC.mOwnerId = mGroupId;
        UNSCOPED_INFO("specific deeding group matches");
        CHECK(matches(mFacts, mC));
    }

    // Owner gating: avatars are their own owner; objects without props yet
    // fail owner modes until the reply lands.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: owner modes take avatars as their own owner and wait for props", "[ALSceneExplorerPredicate]")
    {
        mFacts.mIsAvatar = true;
        mFacts.mItemId = mAgentId;
        mC.mOwnerMode = OWNER_MINE;
        UNSCOPED_INFO("own avatar row is mine");
        CHECK(matches(mFacts, mC));
        mFacts.mItemId = mOtherId;
        UNSCOPED_INFO("other avatar row is not mine");
        CHECK_FALSE(matches(mFacts, mC));

        mFacts.mIsAvatar = false;
        mFacts.mItemId = mRec.mId;
        mRec.mPropsValid = false;
        UNSCOPED_INFO("unresolved props fail owner modes");
        CHECK_FALSE(matches(mFacts, mC));

        mRec.mPropsValid = true;
        mRec.mOwnerId = mAgentId;
        UNSCOPED_INFO("resolved props re-apply owner modes");
        CHECK(matches(mFacts, mC));

        mRec.mPropsValid = false;
        mC.mOwnerMode = OWNER_ANY;
        UNSCOPED_INFO("unresolved props pass without an owner mode");
        CHECK(matches(mFacts, mC));
    }

    // Geometry mask: empty = any; otherwise the item's kind must be in it.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: a geometry mask passes an item only if its kind is in it", "[ALSceneExplorerPredicate]")
    {
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_MESH;
        UNSCOPED_INFO("mesh passes mesh mask");
        CHECK(matches(mFacts, mC));
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_PRIM;
        UNSCOPED_INFO("mesh fails prim-only mask");
        CHECK_FALSE(matches(mFacts, mC));
        mC.mGeomMask = (1u << ALObjectProperties::GEOM_PRIM) | (1u << ALObjectProperties::GEOM_MESH);
        UNSCOPED_INFO("mesh passes prim|mesh mask");
        CHECK(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: a flag mask requires every bit it sets", "[ALSceneExplorerPredicate]")
    {
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED;
        UNSCOPED_INFO("single present flag passes");
        CHECK(matches(mFacts, mC));
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_LIGHT;
        UNSCOPED_INFO("two present flags pass");
        CHECK(matches(mFacts, mC));
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED | ALObjectProperties::FLAG_PHANTOM;
        UNSCOPED_INFO("one missing flag fails the AND");
        CHECK_FALSE(matches(mFacts, mC));

        mRec.mFlags |= ALObjectProperties::FLAG_FOR_SALE | ALObjectProperties::FLAG_PAYABLE;
        mC.mFlagMask = ALObjectProperties::FLAG_FOR_SALE | ALObjectProperties::FLAG_PAYABLE;
        UNSCOPED_INFO("commerce flags filter");
        CHECK(matches(mFacts, mC));
    }

    // Scope: region ignores position; radius compares distance; parcel uses
    // the caller-resolved containment fact.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: region scope ignores distance while radius and parcel scopes filter", "[ALSceneExplorerPredicate]")
    {
        mRec.mDistance = 100.f;
        mC.mScope = SCOPE_REGION;
        UNSCOPED_INFO("region scope ignores distance");
        CHECK(matches(mFacts, mC));

        mC.mScope = SCOPE_RADIUS;
        mC.mRadius = 64.f;
        UNSCOPED_INFO("beyond radius fails");
        CHECK_FALSE(matches(mFacts, mC));
        mC.mRadius = 128.f;
        UNSCOPED_INFO("within radius passes");
        CHECK(matches(mFacts, mC));

        mC.mScope = SCOPE_PARCEL;
        mFacts.mInAgentParcel = true;
        UNSCOPED_INFO("inside parcel passes");
        CHECK(matches(mFacts, mC));
        mFacts.mInAgentParcel = false;
        UNSCOPED_INFO("outside parcel fails");
        CHECK_FALSE(matches(mFacts, mC));
    }

    // Heaviness thresholds: roots gated, child prims exempt, zero = off.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: land impact and triangle minimums gate roots but not child prims", "[ALSceneExplorerPredicate]")
    {
        mC.mMinLandImpact = 50.f;       // record has 40
        UNSCOPED_INFO("under LI threshold fails");
        CHECK_FALSE(matches(mFacts, mC));
        mC.mMinLandImpact = 40.f;
        UNSCOPED_INFO("at LI threshold passes");
        CHECK(matches(mFacts, mC));

        mC.mMinLandImpact = 0.f;
        mC.mMinTriangles = 20000;       // record has 12000
        UNSCOPED_INFO("under triangle threshold fails");
        CHECK_FALSE(matches(mFacts, mC));
        mC.mMinTriangles = 12000;
        UNSCOPED_INFO("at triangle threshold passes");
        CHECK(matches(mFacts, mC));

        mC.mMinTriangles = 20000;
        mFacts.mIsChildPrim = true;
        UNSCOPED_INFO("child prims are exempt from thresholds");
        CHECK(matches(mFacts, mC));
    }

    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: constraints compose and every one must hold at once", "[ALSceneExplorerPredicate]")
    {
        mC.mSearchType = SEARCH_NAME;
        mC.mFilterSubString = "oak";
        mC.mGeomMask = 1u << ALObjectProperties::GEOM_MESH;
        mC.mFlagMask = ALObjectProperties::FLAG_SCRIPTED;
        mC.mScope = SCOPE_RADIUS;
        mC.mRadius = 64.f;
        mC.mMinLandImpact = 10.f;
        UNSCOPED_INFO("all constraints satisfied");
        CHECK(matches(mFacts, mC));

        mC.mFlagMask |= ALObjectProperties::FLAG_PHANTOM;
        UNSCOPED_INFO("one failing constraint fails the set");
        CHECK_FALSE(matches(mFacts, mC));
    }

    // Change classification: substring extension/shrink (the typing and
    // backspacing cases), including the empty-needle transitions.
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: classifySubstringChange tells typing from backspacing", "[ALSceneExplorerPredicate]")
    {
        CHECK(classifySubstringChange("oak", "oak") == CHANGE_NONE);
        UNSCOPED_INFO("typing from empty");
        CHECK(classifySubstringChange("", "oak") == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("extending");
        CHECK(classifySubstringChange("oak", "oak t") == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("backspacing");
        CHECK(classifySubstringChange("oak t", "oak") == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("clearing");
        CHECK(classifySubstringChange("oak", "") == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("unrelated text");
        CHECK(classifySubstringChange("oak", "pine") == CHANGE_RESTART);
    }

    // Change classification: require-all mask (the feature checkboxes).
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: classifyRequireAllMaskChange restricts on added bits", "[ALSceneExplorerPredicate]")
    {
        const U32 a = ALObjectProperties::FLAG_SCRIPTED;
        const U32 b = ALObjectProperties::FLAG_LIGHT;
        const U32 c = ALObjectProperties::FLAG_PHANTOM;
        CHECK(classifyRequireAllMaskChange(a | b, a | b) == CHANGE_NONE);
        UNSCOPED_INFO("first requirement");
        CHECK(classifyRequireAllMaskChange(0, a) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("added requirement");
        CHECK(classifyRequireAllMaskChange(a, a | b) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("removed requirement");
        CHECK(classifyRequireAllMaskChange(a | b, a) == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("last requirement off");
        CHECK(classifyRequireAllMaskChange(a, 0) == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("swapped requirement");
        CHECK(classifyRequireAllMaskChange(a | b, a | c) == CHANGE_RESTART);
    }

    // Change classification: allowed-set mask (the type checkboxes; 0 = any).
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: classifyAllowedMaskChange relaxes on added kinds with 0 as any", "[ALSceneExplorerPredicate]")
    {
        const U32 prim = 1u << ALObjectProperties::GEOM_PRIM;
        const U32 mesh = 1u << ALObjectProperties::GEOM_MESH;
        CHECK(classifyAllowedMaskChange(prim, prim) == CHANGE_NONE);
        UNSCOPED_INFO("any to one kind");
        CHECK(classifyAllowedMaskChange(0, prim) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("one kind to any");
        CHECK(classifyAllowedMaskChange(prim, 0) == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("allowing another kind");
        CHECK(classifyAllowedMaskChange(prim, prim | mesh) == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("disallowing a kind");
        CHECK(classifyAllowedMaskChange(prim | mesh, prim) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("swapped kind");
        CHECK(classifyAllowedMaskChange(prim, mesh) == CHANGE_RESTART);
    }

    // Change classification: minimum thresholds (0 = off).
    TEST_CASE_METHOD(predicate_data, "ALSceneExplorerPredicate: classifyMinThresholdChange restricts on a raise with 0 as off", "[ALSceneExplorerPredicate]")
    {
        CHECK(classifyMinThresholdChange(50.f, 50.f) == CHANGE_NONE);
        UNSCOPED_INFO("enabling");
        CHECK(classifyMinThresholdChange(0.f, 50.f) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("raising");
        CHECK(classifyMinThresholdChange(50.f, 100.f) == CHANGE_MORE_RESTRICTIVE);
        UNSCOPED_INFO("lowering");
        CHECK(classifyMinThresholdChange(100.f, 50.f) == CHANGE_LESS_RESTRICTIVE);
        UNSCOPED_INFO("disabling");
        CHECK(classifyMinThresholdChange(50.f, 0.f) == CHANGE_LESS_RESTRICTIVE);
    }
}
