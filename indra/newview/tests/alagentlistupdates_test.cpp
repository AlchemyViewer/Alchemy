/**
 * @file alagentlistupdates_test.cpp
 * @brief Unit tests for holding a chat session's roster updates that come before its speaker manager
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

#include "../alagentlistupdates.h"

#include "altestmatchers.h"

namespace
{
    const std::string ALICE = "11111111-1111-1111-1111-111111111111";
    const std::string BOB = "22222222-2222-2222-2222-222222222222";

    LLSD entered()
    {
        return LLSD().with("transition", "ENTER").with("info", LLSD().with("is_moderator", false));
    }

    LLSD moderating()
    {
        return LLSD().with("info", LLSD().with("is_moderator", true));
    }

    // An update in the agent_updates format alone, as a simulator may send it.
    TEST_CASE("alagentlistupdates: an update with only agent_updates is held", "[alagentlistupdates]")
    {
        LLSD held = LLSD::emptyMap();
        ALAgentListUpdates::hold(held, LLSD().with("agent_updates", LLSD().with(ALICE, entered())));

        CHECK_THAT(held, ll_test::LLSDEquals(LLSD().with("agent_updates", LLSD().with(ALICE, entered()))));
    }

    TEST_CASE("alagentlistupdates: an update with only the older updates is held", "[alagentlistupdates]")
    {
        LLSD held = LLSD::emptyMap();
        ALAgentListUpdates::hold(held, LLSD().with("updates", LLSD().with(ALICE, "ENTER")));

        CHECK_THAT(held, ll_test::LLSDEquals(LLSD().with("updates", LLSD().with(ALICE, "ENTER"))));
    }

    TEST_CASE("alagentlistupdates: an update in both formats holds both", "[alagentlistupdates]")
    {
        LLSD held = LLSD::emptyMap();
        ALAgentListUpdates::hold(held, LLSD()
                                           .with("agent_updates", LLSD().with(ALICE, entered()))
                                           .with("updates", LLSD().with(ALICE, "ENTER")));

        CHECK_THAT(held, ll_test::LLSDEquals(LLSD()
                                                 .with("agent_updates", LLSD().with(ALICE, entered()))
                                                 .with("updates", LLSD().with(ALICE, "ENTER"))));
    }

    TEST_CASE("alagentlistupdates: an agent's latest entry replaces the one held and others stay", "[alagentlistupdates]")
    {
        LLSD held = LLSD::emptyMap();
        ALAgentListUpdates::hold(held, LLSD().with("agent_updates", LLSD().with(ALICE, entered()).with(BOB, entered())));
        ALAgentListUpdates::hold(held, LLSD().with("agent_updates", LLSD().with(ALICE, moderating())));
        ALAgentListUpdates::hold(held, LLSD().with("updates", LLSD().with(BOB, "LEAVE")));

        CHECK_THAT(held, ll_test::LLSDEquals(LLSD()
                                                 .with("agent_updates", LLSD().with(ALICE, moderating()).with(BOB, entered()))
                                                 .with("updates", LLSD().with(BOB, "LEAVE"))));
    }

    TEST_CASE("alagentlistupdates: a format that is not a map is passed over", "[alagentlistupdates]")
    {
        LLSD held = LLSD::emptyMap();
        ALAgentListUpdates::hold(held, LLSD().with("agent_updates", "garbled").with("updates", LLSD().with(ALICE, "ENTER")));
        ALAgentListUpdates::hold(held, LLSD("not a map"));

        CHECK_THAT(held, ll_test::LLSDEquals(LLSD().with("updates", LLSD().with(ALICE, "ENTER"))));
    }
}
