/**
 * @file alscriptnameindex.h
 * @brief Names to items, built once and then told of each item that changed.
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

#pragma once

#include "llstl.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Names to items, as the index keeps them, with nothing of the inventory:
// built from a list once, then told of each item that changed -- its name
// now, where it is still to be listed, or nothing where it is not.
class ALScriptNameIndex
{
public:
    struct Item
    {
        LLUUID      id;
        std::string name;
    };

    void build(const std::vector<Item>& items);
    bool built() const { return mBuilt; }
    // Built again at the next question: what changed is more than the
    // items said to.
    void forget();
    // An item changed: `name` where it is to be listed under it, nothing
    // where it is not -- gone, in the trash, of another kind.
    void changed(const LLUUID& id, const std::optional<std::string>& name);
    // The items of a name, in the order they came.
    const std::vector<LLUUID>& named(std::string_view name) const;
    // Moves whenever what is listed does.
    U32    generation() const { return mGeneration; }
    size_t size() const { return mNameOf.size(); }

private:
    void take(const LLUUID& id);

    bool mBuilt      = false;
    U32  mGeneration = 1;
    boost::unordered_flat_map<std::string, std::vector<LLUUID>, ll::string_hash, std::equal_to<>> mByName;
    boost::unordered_flat_map<LLUUID, std::string>                                                mNameOf;
};
