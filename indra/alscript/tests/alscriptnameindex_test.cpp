/**
 * @file alscriptnameindex_test.cpp
 * @brief Tests for ALScriptNameIndex: built once, then kept from each item said to change.
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

#include "../core/alscriptnameindex.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace
{
    struct alscriptnameindex_data
    {
        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[15] = static_cast<U8>(n);
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptnameindex_data, "alscriptnameindex: once built an item's changes move what its names give and the generation", "[alscriptnameindex]")
    {
        ALScriptNameIndex index;
        {
            INFO("not built, nothing given");
            CHECK_FALSE(index.built());
            REQUIRE(index.named("lib").empty());
        }
        index.changed(id(1), std::string("lib"));
        UNSCOPED_INFO("nothing kept before it is built");
        REQUIRE(index.named("lib").empty());

        index.build({ { id(1), "lib" }, { id(2), "lib" }, { id(3), "door" } });
        {
            INFO("two of a name, in order");
            CHECK(index.named("lib") == std::vector<LLUUID>({ id(1), id(2) }));
            REQUIRE(index.size() == 3);
        }
        U32 generation = index.generation();

        index.changed(id(3), std::string("door"));
        UNSCOPED_INFO("the same said again: nothing moves");
        CHECK(index.generation() == generation);
        index.changed(id(9), std::nullopt);
        UNSCOPED_INFO("an item never listed gone: nothing moves");
        CHECK(index.generation() == generation);

        index.changed(id(2), std::string("util"));
        {
            INFO("renamed: under its new name only");
            CHECK(index.named("lib") == std::vector<LLUUID>({ id(1) }));
            CHECK(index.named("util") == std::vector<LLUUID>({ id(2) }));
        }
        UNSCOPED_INFO("and the generation moved");
        CHECK(index.generation() != generation);
        generation = index.generation();

        index.changed(id(4), std::string("lib"));
        UNSCOPED_INFO("added, after the rest");
        CHECK(index.named("lib") == std::vector<LLUUID>({ id(1), id(4) }));
        index.changed(id(1), std::nullopt);
        {
            INFO("taken away, or into the trash");
            CHECK(index.named("lib") == std::vector<LLUUID>({ id(4) }));
            REQUIRE(index.size() == 3);
        }
        index.changed(id(3), std::nullopt);
        UNSCOPED_INFO("a name with nothing left gives nothing");
        REQUIRE(index.named("door").empty());

        generation = index.generation();
        index.forget();
        {
            INFO("forgotten: built again at the next question");
            CHECK_FALSE(index.built());
            CHECK(index.generation() != generation);
        }
    }

    TEST_CASE_METHOD(alscriptnameindex_data, "alscriptnameindex: forEach visits every listed item once with its current name", "[alscriptnameindex]")
    {
        ALScriptNameIndex index;
        index.build({ { id(1), "lib" }, { id(2), "lib" }, { id(3), "door" } });
        index.changed(id(2), std::string("util"));
        index.changed(id(3), std::nullopt);
        std::map<LLUUID, std::string> seen;
        index.forEach([&seen](const LLUUID& item, const std::string& name) { UNSCOPED_INFO("once each"); REQUIRE(seen.emplace(item, name).second); });
        REQUIRE(seen.size() == size_t(2));
        {
            INFO("by its name now");
            CHECK(seen[id(1)] == "lib");
            CHECK(seen[id(2)] == "util");
        }
    }
}
