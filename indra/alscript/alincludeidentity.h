/**
 * @file alincludeidentity.h
 * @brief What an include is known by: an item in an object, one in the inventory, or a file on disk.
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

#pragma once

#include "lluuid.h"

#include <string>
#include <string_view>

// What an include, a module or a configuration is known by, as the source
// map names it and every cache keys it: `object:<prim>:<item>` for an item
// in an object, `inventory:<item>` for one in the agent's inventory,
// `disk:<path>` for a file on disk.
namespace ALIncludeIdentity
{
    constexpr std::string_view OBJECT    = "object:";
    constexpr std::string_view INVENTORY = "inventory:";
    constexpr std::string_view DISK      = "disk:";

    std::string ofItem(const LLUUID& object, const LLUUID& item);
    std::string ofFile(const std::string& file);
    // An identity back to the item it names -- its object, null for the
    // inventory -- or to the file; false for anything else.
    bool itemOf(std::string_view path, LLUUID& object, LLUUID& item);
    bool fileOf(std::string_view path, std::string& file);
    bool inWorld(std::string_view path);
}
