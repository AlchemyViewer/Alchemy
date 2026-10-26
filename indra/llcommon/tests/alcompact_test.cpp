/**
 * @file alcompact_test.cpp
 * @brief Tests of al_compact.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "../alcompact.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace
{
    // What is kept stays in its order, what is dropped goes, and the count says how many went.
    TEST_CASE("al_compact: keeps what keep() keeps, in order, and counts what it drops", "[alcompact]")
    {
        std::vector<int> items = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::vector<int> seen;
        const size_t dropped = al_compact(items, [&](int item)
        {
            seen.push_back(item);
            return item % 3 != 0;
        });
        CHECK(dropped == 3);
        CHECK(items == std::vector<int>({ 1, 2, 4, 5, 7, 8, 10 }));
        INFO("every item walked once, in order");
        CHECK(seen == std::vector<int>({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }));
    }

    // An empty vector, all kept and none kept.
    TEST_CASE("al_compact: an empty vector, everything kept, nothing kept", "[alcompact]")
    {
        std::vector<int> none;
        CHECK(al_compact(none, [](int) { return true; }) == 0);
        CHECK(none.empty());

        std::vector<int> all = { 1, 2, 3 };
        CHECK(al_compact(all, [](int) { return true; }) == 0);
        CHECK(all == std::vector<int>({ 1, 2, 3 }));

        CHECK(al_compact(all, [](int) { return false; }) == 3);
        CHECK(all.empty());
    }

    // An item appended while the walk runs is walked in its turn, and kept or dropped like the rest, however often the
    // vector's store moves to make room for it.
    TEST_CASE("al_compact: what keep() appends is walked too", "[alcompact]")
    {
        std::vector<int> items = { 1, 2, 3 };
        items.shrink_to_fit();
        std::vector<int> seen;
        const size_t dropped = al_compact(items, [&](int item)
        {
            seen.push_back(item);
            if (item < 100)
            {
                // Each first-generation item appends one more, the store growing under the walk.
                items.push_back(item + 100);
            }
            return item % 2 == 1;
        });
        CHECK(seen == std::vector<int>({ 1, 2, 3, 101, 102, 103 }));
        CHECK(items == std::vector<int>({ 1, 3, 101, 103 }));
        CHECK(dropped == 2);
    }

    // The items held are let go of when they are dropped, and moved, not copied, into their places.
    TEST_CASE("al_compact: dropped items are let go of", "[alcompact]")
    {
        std::vector<std::shared_ptr<int>> items;
        for (int i = 0; i < 6; ++i)
        {
            items.push_back(std::make_shared<int>(i));
        }
        std::weak_ptr<int> dropped_one = items[1];
        std::weak_ptr<int> kept_one    = items[4];
        al_compact(items, [](const std::shared_ptr<int>& item) { return *item % 2 == 0; });
        CHECK(items.size() == 3);
        CHECK(dropped_one.expired());
        REQUIRE(!kept_one.expired());
        CHECK(kept_one.use_count() == 1);
        CHECK(*items[2] == 4);
    }
}
