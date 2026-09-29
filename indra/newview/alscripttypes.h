/**
 * @file alscripttypes.h
 * @brief What the viewer knows a script by, and what the workspace answers about one.
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

#include "lluuid.h"

#include <boost/container_hash/hash.hpp>

#include <string>

class LLSD;

// Where a script lives: an item of the agent's inventory, or an item of an
// object's contents. The one identity for a script wherever the viewer
// meets it, so that the studio, the legacy floaters, the bridge and the
// compile queue agree about which script is which -- the floater keys and
// the bridge's subscription hash both come from it.
struct ALScriptRef
{
    // Null for the agent's inventory.
    LLUUID object;
    LLUUID item;

    ALScriptRef() = default;
    ALScriptRef(const LLUUID& object_in, const LLUUID& item_in) : object(object_in), item(item_in) {}

    bool inInventory() const { return object.isNull(); }
    bool isNull() const { return item.isNull(); }

    // The bridge's subscription id, which is also the name of the temp
    // file an external editor is given.
    std::string id() const;
    // A floater key, and back.
    LLSD               key() const;
    static ALScriptRef fromKey(const LLSD& key);

    friend bool operator==(const ALScriptRef& a, const ALScriptRef& b) { return a.object == b.object && a.item == b.item; }
    friend bool operator!=(const ALScriptRef& a, const ALScriptRef& b) { return !(a == b); }
    // For a map by it.
    friend size_t hash_value(const ALScriptRef& ref) noexcept
    {
        size_t seed = hash_value(ref.object);
        boost::hash_combine(seed, hash_value(ref.item));
        return seed;
    }
};
