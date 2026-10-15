/**
 * @file alincludeidentity_test.cpp
 * @brief Tests for ALIncludeIdentity: an include's identity made and read back.
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
 */

#include "linden_common.h"

#include "../preprocessor/alincludeidentity.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("alincludeidentity: object and inventory items and files read back but nothing else does", "[alincludeidentity]")
    {
        const LLUUID prim = LLUUID::generateNewID(), item = LLUUID::generateNewID();
        LLUUID       object, found;
        std::string  file;
        const std::string in_object = ALIncludeIdentity::ofItem(prim, item);
        {
            INFO("an object's item");
            REQUIRE(ALIncludeIdentity::itemOf(in_object, object, found));
            CHECK(object == prim);
            CHECK(found == item);
        }
        const std::string held = ALIncludeIdentity::ofItem(LLUUID::null, item);
        {
            INFO("an inventory item");
            CHECK(held.rfind("inventory:", 0) == 0);
            CHECK(ALIncludeIdentity::itemOf(held, object, found));
            CHECK(object.isNull());
            CHECK(found == item);
        }
        {
            INFO("both in the world");
            CHECK(ALIncludeIdentity::inWorld(in_object));
            CHECK(ALIncludeIdentity::inWorld(held));
        }
        {
            INFO("a file");
            REQUIRE(ALIncludeIdentity::fileOf(ALIncludeIdentity::ofFile("/lib/a.lsl"), file));
            CHECK(file == "/lib/a.lsl");
            CHECK_FALSE(ALIncludeIdentity::inWorld("disk:/lib/a.lsl"));
        }
        UNSCOPED_INFO("an object's with no item");
        CHECK_FALSE(ALIncludeIdentity::itemOf("object:" + prim.asString(), object, found));
        {
            INFO("nor a file's item, nor an item's file");
            CHECK_FALSE(ALIncludeIdentity::itemOf("disk:/a", object, found));
            CHECK_FALSE(ALIncludeIdentity::fileOf(held, file));
        }
        UNSCOPED_INFO("an empty file");
        CHECK_FALSE(ALIncludeIdentity::fileOf("disk:", file));
    }
}
