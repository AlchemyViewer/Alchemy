/**
 * @file alincludeidentity.cpp
 * @brief What an include is known by: an item in an object, one in the inventory, or a file on disk.
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

#include "alincludeidentity.h"

namespace ALIncludeIdentity
{
    std::string ofItem(const LLUUID& object, const LLUUID& item)
    {
        if (object.isNull())
        {
            return std::string(INVENTORY) + item.asString();
        }
        return std::string(OBJECT) + object.asString() + ":" + item.asString();
    }

    std::string ofFile(const std::string& file)
    {
        return std::string(DISK) + file;
    }

    bool itemOf(std::string_view path, LLUUID& object, LLUUID& item)
    {
        if (path.substr(0, OBJECT.size()) == OBJECT)
        {
            const size_t colon = path.find(':', OBJECT.size());
            if (colon == std::string_view::npos)
            {
                return false;
            }
            object.set(std::string(path.substr(OBJECT.size(), colon - OBJECT.size())));
            item.set(std::string(path.substr(colon + 1)));
            return object.notNull() && item.notNull();
        }
        if (path.substr(0, INVENTORY.size()) == INVENTORY)
        {
            object.setNull();
            item.set(std::string(path.substr(INVENTORY.size())));
            return item.notNull();
        }
        return false;
    }

    bool fileOf(std::string_view path, std::string& file)
    {
        if (path.substr(0, DISK.size()) == DISK)
        {
            file = std::string(path.substr(DISK.size()));
            return !file.empty();
        }
        return false;
    }

    bool inWorld(std::string_view path)
    {
        return path.substr(0, OBJECT.size()) == OBJECT || path.substr(0, INVENTORY.size()) == INVENTORY;
    }
}
