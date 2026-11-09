/**
 * @file alfriendbacklog_test.cpp
 * @brief Unit tests for what the friends tracker holds of residents not yet in the friends list
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

#include "../alfriendbacklog.h"

namespace
{
    using Kind = ALFriendBacklog::Kind;

    const LLUUID ALICE("11111111-1111-1111-1111-111111111111");
    const LLUUID BOB("22222222-2222-2222-2222-222222222222");

    TEST_CASE("alfriendbacklog: notices before the friends list are held and handed back in the order they came", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.hold({ ALICE, Kind::ONLINE }));
        CHECK(backlog.hold({ BOB, Kind::ONLINE }));
        CHECK(backlog.hold({ ALICE, Kind::OFFLINE }));
        CHECK(backlog.heldCount() == 3);

        const std::vector<ALFriendBacklog::Notice> held = backlog.applyList();
        REQUIRE(held.size() == 3);
        CHECK(held[0].mID == ALICE);
        CHECK(held[0].mKind == Kind::ONLINE);
        CHECK(held[1].mID == BOB);
        CHECK(held[1].mKind == Kind::ONLINE);
        CHECK(held[2].mID == ALICE);
        CHECK(held[2].mKind == Kind::OFFLINE);
        CHECK(backlog.isListApplied());
        CHECK(backlog.heldCount() == 0);
    }

    // Once the list is applied, a resident it doesn't name isn't a friend, and nothing about them is kept: a
    // session's notices about such residents would otherwise pile up, never applied.
    TEST_CASE("alfriendbacklog: once the friends list is applied a notice about a resident not in it is dropped", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.applyList().empty());

        for (int i = 0; i < 100; ++i)
        {
            CHECK_FALSE(backlog.hold({ ALICE, Kind::ONLINE }));
            CHECK_FALSE(backlog.hold({ BOB, Kind::OFFLINE }));
        }
        CHECK(backlog.heldCount() == 0);
        CHECK(backlog.applyList().empty());
    }

    // The login's list was made before the circuit said a friendship ended or a friend's rights changed: what it
    // said comes back to be laid over the list, in the order it came, with the rights it gave.
    TEST_CASE("alfriendbacklog: an ended friendship and changed rights before the friends list come back in order", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.hold({ ALICE, Kind::RIGHTS_FROM, 3 }));
        CHECK(backlog.hold({ BOB, Kind::TERMINATED }));
        CHECK(backlog.hold({ ALICE, Kind::RIGHTS_TO, 5 }));

        const std::vector<ALFriendBacklog::Notice> held = backlog.applyList();
        REQUIRE(held.size() == 3);
        CHECK(held[0].mID == ALICE);
        CHECK(held[0].mKind == Kind::RIGHTS_FROM);
        CHECK(held[0].mRights == 3);
        CHECK(held[1].mID == BOB);
        CHECK(held[1].mKind == Kind::TERMINATED);
        CHECK(held[2].mID == ALICE);
        CHECK(held[2].mKind == Kind::RIGHTS_TO);
        CHECK(held[2].mRights == 5);

        CHECK_FALSE(backlog.hold({ BOB, Kind::TERMINATED }));
        CHECK_FALSE(backlog.hold({ ALICE, Kind::RIGHTS_FROM, 1 }));
    }

    // The resident whose offer the agent accepted is no friend until the server answers, and the server may tell
    // of them first: an online notice then waits for the answer.
    TEST_CASE("alfriendbacklog: notices about a resident whose offer was accepted wait for the answer", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.applyList().empty());

        backlog.expect(ALICE);
        CHECK(backlog.isExpected(ALICE));
        CHECK(backlog.hold({ ALICE, Kind::ONLINE }));
        CHECK_FALSE(backlog.hold({ BOB, Kind::ONLINE }));

        const std::vector<ALFriendBacklog::Notice> held = backlog.answered(ALICE);
        REQUIRE(held.size() == 1);
        CHECK(held[0].mID == ALICE);
        CHECK(held[0].mKind == Kind::ONLINE);
        CHECK_FALSE(backlog.isExpected(ALICE));
        CHECK(backlog.heldCount() == 0);
    }

    TEST_CASE("alfriendbacklog: once the acceptance is answered nothing more is held about them", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.applyList().empty());
        backlog.expect(ALICE);
        CHECK(backlog.hold({ ALICE, Kind::ONLINE }));
        CHECK(backlog.answered(ALICE).size() == 1);

        CHECK_FALSE(backlog.hold({ ALICE, Kind::OFFLINE }));
        CHECK(backlog.answered(ALICE).empty());
    }

    // An offer accepted during startup: the notices about that resident outlast the list, which may not name them.
    TEST_CASE("alfriendbacklog: notices about a resident whose acceptance is unanswered outlast the friends list", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        backlog.expect(ALICE);
        CHECK(backlog.hold({ ALICE, Kind::ONLINE }));
        CHECK(backlog.hold({ BOB, Kind::ONLINE }));

        const std::vector<ALFriendBacklog::Notice> at_list = backlog.applyList();
        REQUIRE(at_list.size() == 1);
        CHECK(at_list[0].mID == BOB);

        CHECK(backlog.hold({ ALICE, Kind::OFFLINE }));
        const std::vector<ALFriendBacklog::Notice> answered = backlog.answered(ALICE);
        REQUIRE(answered.size() == 2);
        CHECK(answered[0].mKind == Kind::ONLINE);
        CHECK(answered[1].mKind == Kind::OFFLINE);
    }

    TEST_CASE("alfriendbacklog: what was held is handed back once", "[alfriendbacklog]")
    {
        ALFriendBacklog backlog;
        CHECK(backlog.hold({ ALICE, Kind::ONLINE }));
        CHECK(backlog.applyList().size() == 1);
        CHECK(backlog.applyList().empty());
    }
}
