/**
 * @file alscriptstudioaccount_test.cpp
 * @brief What of Script Studio's state is an account's own, kept apart and put back.
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

#include "../alscriptstudioaccount.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // The account's own is taken out of a state written whole, the shape and switches left shared;
    // a key it no longer has is taken out of the account's too.
    TEST_CASE("alscriptstudioaccount: split takes the account's own out of the state and drops what it lost", "[alscriptstudioaccount]")
    {
        LLSD state;
        state["rect"]           = LLSD().with(0, 1).with(1, 2);
        state["scroll_map"]     = true;
        state["open"]["tabs"]   = LLSD::emptyArray();
        state["pinned"]         = LLSD().with(0, "a");
        state["recent_scripts"] = LLSD().with(0, "s");
        state["recent_files"]   = LLSD().with(0, "/f");
        LLSD mine;
        mine["windows"] = LLSD().with(0, "w");
        ALScriptStudioAccount::split(state, mine);
        {
            INFO("out of the shared");
            REQUIRE_FALSE(state.has("open"));
            REQUIRE_FALSE(state.has("pinned"));
            REQUIRE_FALSE(state.has("recent_scripts"));
        }
        {
            INFO("the shape, the switches and the files left");
            CHECK(state.has("rect"));
            CHECK(state.has("scroll_map"));
            CHECK(state.has("recent_files"));
        }
        {
            INFO("into the account's");
            REQUIRE(mine.has("open"));
            CHECK(mine["pinned"][0].asString() == "a");
            CHECK(mine["recent_scripts"][0].asString() == "s");
        }
        UNSCOPED_INFO("what the state no longer has, gone from the account's");
        REQUIRE_FALSE(mine.has("windows"));
    }

    // Read: the account's own over the shared where it has kept any, a key it has not taken out;
    // the shared as it was where it has kept none.
    TEST_CASE("alscriptstudioaccount: merged lays the account's own over the shared once it has kept any", "[alscriptstudioaccount]")
    {
        LLSD shared;
        shared["rect"]   = 1;
        shared["open"]   = "old tabs";
        shared["pinned"] = "old pins";
        const LLSD first = ALScriptStudioAccount::merged(shared, LLSD());
        {
            INFO("none kept yet: the shared taken over");
            CHECK(first["open"].asString() == "old tabs");
            CHECK(first["pinned"].asString() == "old pins");
        }
        LLSD mine;
        mine["open"] = "my tabs";
        const LLSD read = ALScriptStudioAccount::merged(shared, mine);
        UNSCOPED_INFO("its own");
        CHECK(read["open"].asString() == "my tabs");
        UNSCOPED_INFO("another account's pins not seen");
        REQUIRE_FALSE(read.has("pinned"));
        UNSCOPED_INFO("the shape shared");
        CHECK(read["rect"].asInteger() == 1);
    }
}
