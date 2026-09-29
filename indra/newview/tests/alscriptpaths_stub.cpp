/**
 * @file alscriptpaths_stub.cpp
 * @brief A script's id, and the preprocessor's names for scripts and files, stubbed for Script Studio's tests.
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

// The preprocessor's header reaches the inventory model's, which does not
// include what it uses.
#include <boost/unordered_map.hpp>

#include "../alscriptpreprocessor.h"
#include "../alscripttypes.h"

// Apart from the other stubs (alscriptstudio_stubs.cpp): a test that spells
// these itself takes nothing from here, and one that needs them takes
// them alone.

// A script's id is the bridge's name for it, and the bridge is the viewer's.
std::string ALScriptRef::id() const
{
    return object.asString() + "_" + item.asString();
}

// What the preprocessor calls a script and a file, as it spells them; the
// preprocessor itself is the viewer's.
bool ALScriptPreprocessor::refOf(const std::string& path, ALScriptRef& ref)
{
    if (path.rfind("object:", 0) == 0)
    {
        const size_t colon = path.find(':', 7);
        if (colon == std::string::npos)
        {
            return false;
        }
        ref.object.set(path.substr(7, colon - 7));
        ref.item.set(path.substr(colon + 1));
        return ref.object.notNull() && ref.item.notNull();
    }
    if (path.rfind("inventory:", 0) == 0)
    {
        ref.object.setNull();
        ref.item.set(path.substr(10));
        return ref.item.notNull();
    }
    return false;
}

std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return ref.inInventory() ? "inventory:" + ref.item.asString() : "object:" + ref.object.asString() + ":" + ref.item.asString();
}

bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.rfind("disk:", 0) != 0)
    {
        return false;
    }
    file = path.substr(5);
    return !file.empty();
}
