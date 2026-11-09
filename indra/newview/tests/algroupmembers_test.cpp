/**
 * @file algroupmembers_test.cpp
 * @brief Unit tests for laying a reply's members over the ones a group holds
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

#include "../algroupmembers.h"

#include "lluuid.h"

#include <map>
#include <string>

namespace
{
    struct Role
    {
    };

    // A member's entry as LLGroupMemberData keeps one: its roles, by ID. Every entry alive is counted, so that one
    // the merge loses track of shows as a leak.
    struct Member
    {
        using roles_t = std::map<LLUUID, Role*>;

        static int sLive;

        explicit Member(std::string title) : mTitle(std::move(title)) { ++sLive; }
        ~Member() { --sLive; }
        Member(const Member&) = delete;
        Member& operator=(const Member&) = delete;

        void addRole(const LLUUID& id, Role* role) { mRoles[id] = role; }
        roles_t::iterator roleBegin() { return mRoles.begin(); }
        roles_t::iterator roleEnd() { return mRoles.end(); }

        std::string mTitle;
        roles_t     mRoles;
    };
    int Member::sLive = 0;

    using Members = std::map<LLUUID, Member*>;

    const LLUUID STAYS("00000000-0000-0000-0000-00000000000a");
    const LLUUID LEFT("00000000-0000-0000-0000-00000000000b");
    const LLUUID JOINED("00000000-0000-0000-0000-00000000000c");
    const LLUUID OFFICERS("00000000-0000-0000-0000-0000000000f0");

    // The group as a first load left it: two members, one of them an officer.
    struct Held
    {
        Held()
        {
            members[STAYS] = new Member("old title");
            members[STAYS]->addRole(OFFICERS, &officers);
            members[LEFT] = new Member("member");
        }
        ~Held()
        {
            for (auto& [id, entry] : members)
            {
                delete entry;
            }
        }
        Role    officers;
        Members members;
    };

    // A refresh lists the group as it is now: the member who left goes, and nothing of the old entries is kept
    // alive but what the group holds.
    TEST_CASE("algroupmembers: a reply from the first member replaces the members held", "[algroupmembers]")
    {
        {
            Held held;
            bool role_members_complete = true;
            const Members reply{ { STAYS, new Member("new title") }, { JOINED, new Member("member") } };

            const size_t added = ALGroupMembers::merge(held.members, reply, true, role_members_complete);

            CHECK(added == 1);
            CHECK(held.members.size() == 2);
            CHECK(held.members.count(LEFT) == 0);
            REQUIRE(held.members.count(STAYS) == 1);
            CHECK(held.members[STAYS]->mTitle == "new title");
            CHECK(held.members.count(JOINED) == 1);
            CHECK(Member::sLive == 2);
            // the member who joined has no roles yet, so they must be asked for again
            CHECK_FALSE(role_members_complete);
        }
        CHECK(Member::sLive == 0);
    }

    TEST_CASE("algroupmembers: a member kept takes the roles their old entry had", "[algroupmembers]")
    {
        Held held;
        bool role_members_complete = true;
        const Members reply{ { STAYS, new Member("new title") }, { LEFT, new Member("member") } };

        CHECK(ALGroupMembers::merge(held.members, reply, true, role_members_complete) == 0);

        REQUIRE(held.members.count(STAYS) == 1);
        CHECK(held.members[STAYS]->mRoles.size() == 1);
        CHECK(held.members[STAYS]->mRoles.count(OFFICERS) == 1);
        CHECK(role_members_complete);
        CHECK(Member::sLive == 2);
    }

    // Until the group's role members have been read the entries hold no roles to carry over.
    TEST_CASE("algroupmembers: no roles are carried over while the role members are incomplete", "[algroupmembers]")
    {
        Held held;
        bool role_members_complete = false;
        const Members reply{ { STAYS, new Member("new title") } };

        CHECK(ALGroupMembers::merge(held.members, reply, true, role_members_complete) == 0);

        REQUIRE(held.members.count(STAYS) == 1);
        CHECK(held.members[STAYS]->mRoles.empty());
        CHECK_FALSE(role_members_complete);
    }

    TEST_CASE("algroupmembers: a reply from later in the list adds to the members held", "[algroupmembers]")
    {
        {
            Held held;
            bool role_members_complete = true;
            const Members reply{ { STAYS, new Member("new title") }, { JOINED, new Member("member") } };

            const size_t added = ALGroupMembers::merge(held.members, reply, false, role_members_complete);

            CHECK(added == 1);
            CHECK(held.members.size() == 3);
            CHECK(held.members.count(LEFT) == 1);
            REQUIRE(held.members.count(STAYS) == 1);
            CHECK(held.members[STAYS]->mTitle == "new title");
            CHECK(held.members[STAYS]->mRoles.count(OFFICERS) == 1);
            CHECK(Member::sLive == 3);
            CHECK_FALSE(role_members_complete);
        }
        CHECK(Member::sLive == 0);
    }

    // A reply that lists only members already held asks for no further page.
    TEST_CASE("algroupmembers: a reply of members all held adds none", "[algroupmembers]")
    {
        Held held;
        bool role_members_complete = true;
        const Members reply{ { STAYS, new Member("member") }, { LEFT, new Member("member") } };

        CHECK(ALGroupMembers::merge(held.members, reply, true, role_members_complete) == 0);
        CHECK(held.members.size() == 2);
        CHECK(Member::sLive == 2);
    }
}
