/**
 * @file alincludeidentity_test.cpp
 * @brief Tests for ALIncludeIdentity: an include's identity made and read back.
 *
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

#include "../alincludeidentity.h"

#include "../test/lltut.h"

namespace tut
{
    struct alincludeidentity_data
    {
    };

    typedef test_group<alincludeidentity_data> alincludeidentity_group;
    typedef alincludeidentity_group::object    alincludeidentity_object;
    alincludeidentity_group                    alincludeidentity_instance("alincludeidentity");

    template<> template<>
    void alincludeidentity_object::test<1>()
    {
        set_test_name("an item in an object, one in the inventory, a file: made and read back; anything else not");
        const LLUUID prim = LLUUID::generateNewID(), item = LLUUID::generateNewID();
        LLUUID       object, found;
        std::string  file;
        const std::string in_object = ALIncludeIdentity::ofItem(prim, item);
        ensure("an object's item", ALIncludeIdentity::itemOf(in_object, object, found) && object == prim && found == item);
        const std::string held = ALIncludeIdentity::ofItem(LLUUID::null, item);
        ensure("an inventory item", held.rfind("inventory:", 0) == 0 && ALIncludeIdentity::itemOf(held, object, found) && object.isNull() && found == item);
        ensure("both in the world", ALIncludeIdentity::inWorld(in_object) && ALIncludeIdentity::inWorld(held));
        ensure("a file", ALIncludeIdentity::fileOf(ALIncludeIdentity::ofFile("/lib/a.lsl"), file) && file == "/lib/a.lsl" &&
                             !ALIncludeIdentity::inWorld("disk:/lib/a.lsl"));
        ensure("an object's with no item", !ALIncludeIdentity::itemOf("object:" + prim.asString(), object, found));
        ensure("nor a file's item, nor an item's file", !ALIncludeIdentity::itemOf("disk:/a", object, found) && !ALIncludeIdentity::fileOf(held, file));
        ensure("an empty file", !ALIncludeIdentity::fileOf("disk:", file));
    }
}
