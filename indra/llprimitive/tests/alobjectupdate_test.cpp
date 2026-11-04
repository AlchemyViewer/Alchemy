/**
 * @file alobjectupdate_test.cpp
 * @brief What a simulator's object messages may do to the viewer's objects.
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

#include "../alobjectupdate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
    TEST_CASE("ALObjectUpdate: an update makes a volume or an avatar or grass or a legacy tree and nothing else", "[ALObjectUpdate]")
    {
        for (S32 pcode = 0; pcode < 256; ++pcode)
        {
            CAPTURE(pcode);
            const bool expected = pcode == 0x09 || pcode == 0x2F || pcode == 0x5F || pcode == 0xFF;
            CHECK(ALObjectUpdate::makesObject((LLPCode)pcode) == expected);
        }
    }

    TEST_CASE("ALObjectUpdate: an update makes none of the viewer's own types", "[ALObjectUpdate]")
    {
        // A surface patch and the world's and the HUD's particle groups.
        CHECK_FALSE(ALObjectUpdate::makesObject(0x3E));
        CHECK_FALSE(ALObjectUpdate::makesObject(0xAE));
        CHECK_FALSE(ALObjectUpdate::makesObject(0xCE));
    }

    const LLUUID AGENT("11111111-1111-1111-1111-111111111111");
    const LLUUID HOLDER("22222222-2222-2222-2222-222222222222");
    const LLUUID TAKER("33333333-3333-3333-3333-333333333333");

    TEST_CASE("ALObjectUpdate: a local ID no other object has is free to take", "[ALObjectUpdate]")
    {
        using Claim = ALObjectUpdate::ELocalIDClaim;
        const bool holder_here = GENERATE(true, false);
        CAPTURE(holder_here);
        CHECK(ALObjectUpdate::claimLocalID(LLUUID::null, holder_here, TAKER, AGENT) == Claim::FREE);
        UNSCOPED_INFO("an object already under it takes its own again");
        CHECK(ALObjectUpdate::claimLocalID(TAKER, holder_here, TAKER, AGENT) == Claim::FREE);
        CHECK(ALObjectUpdate::claimLocalID(AGENT, holder_here, AGENT, AGENT) == Claim::FREE);
    }

    TEST_CASE("ALObjectUpdate: another object under a taken local ID goes", "[ALObjectUpdate]")
    {
        using Claim = ALObjectUpdate::ELocalIDClaim;
        CHECK(ALObjectUpdate::claimLocalID(HOLDER, true, TAKER, AGENT) == Claim::REMOVES_HOLDER);
        UNSCOPED_INFO("the agent's avatar takes one like any other");
        CHECK(ALObjectUpdate::claimLocalID(HOLDER, true, AGENT, AGENT) == Claim::REMOVES_HOLDER);
    }

    TEST_CASE("ALObjectUpdate: the agent's avatar keeps its local ID and the other update is dropped", "[ALObjectUpdate]")
    {
        using Claim = ALObjectUpdate::ELocalIDClaim;
        CHECK(ALObjectUpdate::claimLocalID(AGENT, true, TAKER, AGENT) == Claim::REFUSED);
    }

    TEST_CASE("ALObjectUpdate: a row a kill would not reach leaves its local ID free", "[ALObjectUpdate]")
    {
        using Claim = ALObjectUpdate::ELocalIDClaim;
        CHECK(ALObjectUpdate::claimLocalID(HOLDER, false, TAKER, AGENT) == Claim::FREE);
        UNSCOPED_INFO("the agent's avatar, once it has left the region, included");
        CHECK(ALObjectUpdate::claimLocalID(AGENT, false, TAKER, AGENT) == Claim::FREE);
    }

    TEST_CASE("ALObjectUpdate: a kill reaches the cache whether or not the object was made", "[ALObjectUpdate]")
    {
        CHECK(ALObjectUpdate::killReachesCache(HOLDER, AGENT, true));
        UNSCOPED_INFO("a local ID no object has");
        CHECK(ALObjectUpdate::killReachesCache(LLUUID::null, AGENT, true));
    }

    TEST_CASE("ALObjectUpdate: a kill leaves the cache alone for the agent's avatar and without culling", "[ALObjectUpdate]")
    {
        CHECK_FALSE(ALObjectUpdate::killReachesCache(AGENT, AGENT, true));
        CHECK_FALSE(ALObjectUpdate::killReachesCache(HOLDER, AGENT, false));
        CHECK_FALSE(ALObjectUpdate::killReachesCache(LLUUID::null, AGENT, false));
    }
}
