/**
 * @file llagentbenefits_test.cpp
 * @brief Unit tests for the account benefits the login sends, and the room they leave for groups
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../llagentbenefits.h"

namespace
{
    // Every field init requires, as a login's account_level_benefits has them.
    LLSD benefits_with_group_limit(S32 limit)
    {
        LLSD sd = LLSD::emptyMap();
        sd["animated_object_limit"] = 1;
        sd["animation_upload_cost"] = 10;
        sd["attachment_limit"] = 38;
        sd["create_group_cost"] = 100;
        sd["group_membership_limit"] = limit;
        sd["picks_limit"] = 10;
        sd["sound_upload_cost"] = 10;
        sd["texture_upload_cost"] = 10;
        return sd;
    }

    TEST_CASE("llagentbenefits: a group limit leaves room below it", "[llagentbenefits]")
    {
        LLAgentBenefits benefits;
        REQUIRE(benefits.init(benefits_with_group_limit(42)));
        CHECK(benefits.getGroupMembershipLimit() == 42);
        CHECK(benefits.hasRoomForGroup(0));
        CHECK(benefits.hasRoomForGroup(41));
        CHECK_FALSE(benefits.hasRoomForGroup(42));
        CHECK_FALSE(benefits.hasRoomForGroup(43));
    }

    // An invitation to a group the agent is in already, which changes their role in it, takes no new room.
    TEST_CASE("llagentbenefits: a group the agent is in already takes no new room", "[llagentbenefits]")
    {
        LLAgentBenefits benefits;
        REQUIRE(benefits.init(benefits_with_group_limit(42)));
        CHECK(benefits.hasRoomForGroup(42, true));
        CHECK_FALSE(benefits.hasRoomForGroup(43, true));
        CHECK(benefits.hasRoomForGroup(1, true));
    }

    TEST_CASE("llagentbenefits: a limit of no groups leaves no room", "[llagentbenefits]")
    {
        LLAgentBenefits benefits;
        REQUIRE(benefits.init(benefits_with_group_limit(0)));
        CHECK_FALSE(benefits.hasRoomForGroup(0));
    }

    // Benefits the login never sent, or sent without a group limit, set none: any number of groups may be joined,
    // the agent's own among them.
    TEST_CASE("llagentbenefits: benefits that set no group limit leave room for any number", "[llagentbenefits]")
    {
        const LLAgentBenefits never_read;
        CHECK(never_read.hasRoomForGroup(0));
        CHECK(never_read.hasRoomForGroup(500));
        CHECK(never_read.hasRoomForGroup(1, true));
        CHECK(never_read.hasRoomForGroup(500, true));

        LLSD without_limit = benefits_with_group_limit(42);
        without_limit.erase("group_membership_limit");
        LLAgentBenefits read_without_limit;
        CHECK_FALSE(read_without_limit.init(without_limit));
        CHECK(read_without_limit.hasRoomForGroup(100));
        CHECK(read_without_limit.hasRoomForGroup(100, true));
    }
}
