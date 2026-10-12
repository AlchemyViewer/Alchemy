/**
 * @file lluserrelations_tut.cpp
 * @author Phoenix
 * @date 2006-10-12
 * @brief Unit tests for the LLRelationship class.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include <catch2/catch_test_macros.hpp>

#include "linden_common.h"
#include "lluserrelations.h"

namespace
{
    struct user_relationship
    {
        LLRelationship mRelationship;
    };

    TEST_CASE_METHOD(user_relationship, "relationships: a default relationship grants nothing and is offline", "[relationships]")
    {
        // Test the default construction
        UNSCOPED_INFO("No granted rights to");
        CHECK_FALSE(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("No granted rights from");
        CHECK_FALSE(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("No online status");
        CHECK_FALSE(mRelationship.isOnline());
    }

    TEST_CASE_METHOD(user_relationship, "relationships: grantRights grants rights to and from separately", "[relationships]")
    {
        // Test some granting
        mRelationship.grantRights(
            LLRelationship::GRANT_ONLINE_STATUS,
            LLRelationship::GRANT_MODIFY_OBJECTS);
        UNSCOPED_INFO("Granted rights to has online");
        CHECK(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("Granted rights from does not have online");
        CHECK_FALSE(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("Granted rights to does not have modify");
        CHECK_FALSE(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_MODIFY_OBJECTS));
        UNSCOPED_INFO("Granted rights from has modify");
        CHECK(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_MODIFY_OBJECTS));
    }

    TEST_CASE_METHOD(user_relationship, "relationships: revokeRights removes only the rights it names", "[relationships]")
    {
        // Test revoking
        mRelationship.grantRights(
            LLRelationship::GRANT_ONLINE_STATUS
            | LLRelationship::GRANT_MAP_LOCATION,
            LLRelationship::GRANT_ONLINE_STATUS);
        UNSCOPED_INFO("Granted rights to has online and map");
        CHECK(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_ONLINE_STATUS
                | LLRelationship::GRANT_MAP_LOCATION));
        UNSCOPED_INFO("Granted rights from has online");
        CHECK(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS));

        mRelationship.revokeRights(
            LLRelationship::GRANT_MAP_LOCATION,
            LLRelationship::GRANT_NONE);
        UNSCOPED_INFO("Granted rights revoked map");
        CHECK_FALSE(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_ONLINE_STATUS
                | LLRelationship::GRANT_MAP_LOCATION));
        UNSCOPED_INFO("Granted rights revoked still has online");
        CHECK(mRelationship.isRightGrantedTo(
                LLRelationship::GRANT_ONLINE_STATUS));

        mRelationship.grantRights(
            LLRelationship::GRANT_NONE,
            LLRelationship::GRANT_MODIFY_OBJECTS);
        UNSCOPED_INFO("Granted rights from still has online");
        CHECK(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("Granted rights from has full grant");
        CHECK(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS
                | LLRelationship::GRANT_MODIFY_OBJECTS));
        mRelationship.revokeRights(
            LLRelationship::GRANT_NONE,
            LLRelationship::GRANT_MODIFY_OBJECTS);
        UNSCOPED_INFO("Granted rights from still has online");
        CHECK(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_ONLINE_STATUS));
        UNSCOPED_INFO("Granted rights from no longer modify");
        CHECK_FALSE(mRelationship.isRightGrantedFrom(
                LLRelationship::GRANT_MODIFY_OBJECTS));
    }

    TEST_CASE_METHOD(user_relationship, "relationships: online sets the online status", "[relationships]")
    {
        UNSCOPED_INFO("No online status");
        CHECK_FALSE(mRelationship.isOnline());
        mRelationship.online(true);
        UNSCOPED_INFO("Online status");
        CHECK(mRelationship.isOnline());
        mRelationship.online(false);
        UNSCOPED_INFO("No online status");
        CHECK_FALSE(mRelationship.isOnline());
    }

/*
    template<> template<>
    void user_relationship_object_t::test<>()
    {
    }
*/
}
