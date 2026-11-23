/**
 * @file almutelookup_test.cpp
 * @brief Unit tests for which of the mute list's mutes held by ID a lookup reaches
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

#include "../almutelookup.h"

#include "lluuid.h"

#include <set>
#include <string>

namespace
{
    // A mute as the mute list holds one, with LLMute's types.
    struct Mute
    {
        enum EType { BY_NAME = 0, AGENT = 1, OBJECT = 2, GROUP = 3 };

        LLUUID      mID;
        std::string mName;
        EType       mType = BY_NAME;
    };

    struct ById
    {
        bool operator()(const Mute& a, const Mute& b) const { return a.mID < b.mID; }
    };

    using Mutes = std::set<Mute, ById>;

    const LLUUID AGENT_ID("33333333-3333-3333-3333-333333333333");
    const std::string GROUP_NAME = "Group:44444444-4444-4444-4444-444444444444";

    // The list holds a mute of the null ID, as the server can send one, beside a resident's.
    Mutes held()
    {
        Mutes mutes;
        mutes.insert({ LLUUID::null, "Nobody", Mute::AGENT });
        mutes.insert({ AGENT_ID, "Somebody", Mute::AGENT });
        return mutes;
    }

    // A group's mute is a mute by name, looked up under the null ID: isGroupMuted, removeGroup. It must not find
    // the mute of the null ID, or that mute would mute every group, and unmuting a group would remove it.
    TEST_CASE("almutelookup: a lookup by name never reaches a mute of the null ID", "[almutelookup]")
    {
        Mutes mutes = held();
        const Mutes& const_mutes = mutes;
        CHECK(ALMuteLookup::findById(mutes, Mute{ LLUUID::null, GROUP_NAME, Mute::BY_NAME }) == mutes.end());
        CHECK(ALMuteLookup::findById(mutes, Mute{ LLUUID::null, "Nobody", Mute::BY_NAME }) == mutes.end());
        CHECK(ALMuteLookup::findById(const_mutes, Mute{ LLUUID::null, std::string(), Mute::BY_NAME }) == const_mutes.end());
    }

    // The block list unmutes an entry with the mute it lists, whose type is the one it was held with.
    TEST_CASE("almutelookup: a mute of the null ID is found when looked up as the mute it is", "[almutelookup]")
    {
        Mutes mutes = held();
        const auto found = ALMuteLookup::findById(mutes, Mute{ LLUUID::null, "Nobody", Mute::AGENT });
        REQUIRE(found != mutes.end());
        CHECK(found->mName == "Nobody");
    }

    // isMuted looks an ID up with a key of no name and the default type, which is by name.
    TEST_CASE("almutelookup: a lookup of a resident's ID finds their mute whatever the key's type", "[almutelookup]")
    {
        Mutes mutes = held();
        const auto by_default = ALMuteLookup::findById(mutes, Mute{ AGENT_ID, std::string(), Mute::BY_NAME });
        REQUIRE(by_default != mutes.end());
        CHECK(by_default->mName == "Somebody");
        CHECK(ALMuteLookup::findById(mutes, Mute{ AGENT_ID, std::string(), Mute::OBJECT }) != mutes.end());
        const LLUUID stranger("55555555-5555-5555-5555-555555555555");
        CHECK(ALMuteLookup::findById(mutes, Mute{ stranger, std::string(), Mute::AGENT }) == mutes.end());
    }
}
