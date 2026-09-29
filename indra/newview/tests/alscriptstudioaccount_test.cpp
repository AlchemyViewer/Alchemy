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

#include "../test/lltut.h"

namespace tut
{
    struct alscriptstudioaccount_data
    {
    };
    typedef test_group<alscriptstudioaccount_data> alscriptstudioaccount_group;
    typedef alscriptstudioaccount_group::object    alscriptstudioaccount_object;
    alscriptstudioaccount_group                    alscriptstudioaccount_instance("alscriptstudioaccount");

    template<> template<>
    void alscriptstudioaccount_object::test<1>()
    {
        set_test_name("the account's own taken out of a state written whole, the shape and switches left shared; a key it no longer has taken out of the account's too");
        LLSD state;
        state["rect"]           = LLSD().with(0, 1).with(1, 2);
        state["open"]["tabs"]   = LLSD::emptyArray();
        state["pinned"]         = LLSD().with(0, "a");
        state["recent_scripts"] = LLSD().with(0, "s");
        state["recent_files"]   = LLSD().with(0, "/f");
        LLSD mine;
        mine["windows"] = LLSD().with(0, "w");
        ALScriptStudioAccount::split(state, mine);
        ensure("out of the shared", !state.has("open") && !state.has("pinned") && !state.has("recent_scripts"));
        ensure("the shape, the switches and the files left", state.has("rect") && state.has("recent_files"));
        ensure("into the account's", mine.has("open") && mine["pinned"][0].asString() == "a" && mine["recent_scripts"][0].asString() == "s");
        ensure("what the state no longer has, gone from the account's", !mine.has("windows"));
    }

    template<> template<>
    void alscriptstudioaccount_object::test<2>()
    {
        set_test_name("read: the account's own over the shared where it has kept any, a key it has not taken out; the shared as it was where it has kept none");
        LLSD shared;
        shared["rect"]   = 1;
        shared["open"]   = "old tabs";
        shared["pinned"] = "old pins";
        const LLSD first = ALScriptStudioAccount::merged(shared, LLSD());
        ensure("none kept yet: the shared taken over", first["open"].asString() == "old tabs" && first["pinned"].asString() == "old pins");
        LLSD mine;
        mine["open"] = "my tabs";
        const LLSD read = ALScriptStudioAccount::merged(shared, mine);
        ensure("its own", read["open"].asString() == "my tabs");
        ensure("another account's pins not seen", !read.has("pinned"));
        ensure("the shape shared", read["rect"].asInteger() == 1);
    }
}
