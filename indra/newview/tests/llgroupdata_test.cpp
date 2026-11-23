/**
 * @file llgroupdata_test.cpp
 * @brief Unit tests for what the agent holds of a group it is in
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

#include "../llagent.h"

#include <type_traits>

namespace
{
    // A group made as processAgentGroupDataUpdate makes the one it reads each block into, `LLGroupData group;`, with
    // the fields AgentGroupDataUpdate doesn't set read back. Constant evaluation refuses to read a field that was
    // never initialized, so this is a constant expression only where every field has an initializer.
    template <class Group>
    constexpr bool reads_as_nothing()
    {
        Group group;
        return !group.mListInProfile && !group.mAcceptNotices && group.mPowers == 0 && group.mContribution == 0;
    }

    template <class Group>
    concept MadeAsNothing = requires { typename std::enable_if_t<reads_as_nothing<Group>()>; };

    // AgentGroupDataUpdate has no field for listing a group in the profile: a group it adds is not listed until the
    // event queue's NewGroupData says so.
    TEST_CASE("llgroupdata: a group made with no message's fields reads as nothing", "[llgroupdata]")
    {
        CHECK(MadeAsNothing<LLGroupData>);

        const LLGroupData group;
        CHECK(group.mID.isNull());
        CHECK(group.mInsigniaID.isNull());
        CHECK(group.mName.empty());
    }
}
