/**
 * @file alobjectorphans_test.cpp
 * @brief The objects waiting for a parent the viewer has not got.
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

#include "../alobjectorphans.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    const LLUUID CHILD_A("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    const LLUUID CHILD_B("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    constexpr U64 PARENT_1 = 1;
    constexpr U64 PARENT_2 = 2;

    using Children = std::vector<LLUUID>;

    TEST_CASE("ALObjectOrphans: a parent's arrival takes the children waiting for it", "[ALObjectOrphans]")
    {
        ALObjectOrphans orphans;
        CHECK(orphans.empty());
        orphans.wait(CHILD_A, PARENT_1);
        orphans.wait(CHILD_B, PARENT_1);
        CHECK(orphans.childCount() == 2);
        CHECK(orphans.parentCount() == 1);

        CHECK(orphans.take(PARENT_2).empty());
        CHECK(orphans.childCount() == 2);

        CHECK(orphans.take(PARENT_1) == Children{ CHILD_A, CHILD_B });
        CHECK(orphans.empty());
        CHECK(orphans.parentCount() == 0);
        CHECK(orphans.take(PARENT_1).empty());
    }

    TEST_CASE("ALObjectOrphans: an orphan given no parent no longer waits for its first", "[ALObjectOrphans]")
    {
        ALObjectOrphans orphans;
        orphans.wait(CHILD_A, PARENT_1);
        orphans.wait(CHILD_B, PARENT_1);

        CHECK(orphans.release(CHILD_A));
        CHECK_FALSE(orphans.release(CHILD_A));
        CHECK(orphans.childCount() == 1);
        CHECK(orphans.parentCount() == 1);

        CHECK(orphans.take(PARENT_1) == Children{ CHILD_B });

        UNSCOPED_INFO("nor is a parent no child waits for kept");
        orphans.wait(CHILD_A, PARENT_2);
        CHECK(orphans.release(CHILD_A));
        CHECK(orphans.parentCount() == 0);
        CHECK(orphans.empty());
    }

    TEST_CASE("ALObjectOrphans: a child waits for the parent its latest update named alone", "[ALObjectOrphans]")
    {
        ALObjectOrphans orphans;
        orphans.wait(CHILD_A, PARENT_1);
        orphans.wait(CHILD_A, PARENT_2);
        CHECK(orphans.childCount() == 1);
        CHECK(orphans.parentCount() == 1);

        CHECK(orphans.take(PARENT_1).empty());
        CHECK(orphans.take(PARENT_2) == Children{ CHILD_A });
    }

    TEST_CASE("ALObjectOrphans: naming the same parent again keeps a child's place", "[ALObjectOrphans]")
    {
        ALObjectOrphans orphans;
        orphans.wait(CHILD_A, PARENT_1);
        orphans.wait(CHILD_B, PARENT_1);
        orphans.wait(CHILD_A, PARENT_1);
        CHECK(orphans.childCount() == 2);
        CHECK(orphans.take(PARENT_1) == Children{ CHILD_A, CHILD_B });
    }
}
