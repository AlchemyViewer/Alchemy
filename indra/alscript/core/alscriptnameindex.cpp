/**
 * @file alscriptnameindex.cpp
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

#include "linden_common.h"

#include "alscriptnameindex.h"

#include <algorithm>

void ALScriptNameIndex::build(const std::vector<Item>& items)
{
    mByName.clear();
    mNameOf.clear();
    for (const Item& item : items)
    {
        if (mNameOf.emplace(item.id, item.name).second)
        {
            mByName[item.name].push_back(item.id);
        }
    }
    mBuilt = true;
    ++mGeneration;
}

void ALScriptNameIndex::forget()
{
    if (mBuilt)
    {
        mBuilt = false;
        ++mGeneration;
    }
}

void ALScriptNameIndex::take(const LLUUID& id)
{
    const auto was = mNameOf.find(id);
    if (was == mNameOf.end())
    {
        return;
    }
    if (const auto listed = mByName.find(was->second); listed != mByName.end())
    {
        std::erase(listed->second, id);
        if (listed->second.empty())
        {
            mByName.erase(listed);
        }
    }
    mNameOf.erase(was);
}

void ALScriptNameIndex::changed(const LLUUID& id, const std::optional<std::string>& name)
{
    if (!mBuilt)
    {
        return;
    }
    const auto was = mNameOf.find(id);
    if (was != mNameOf.end() && name && was->second == *name)
    {
        return;
    }
    if (was == mNameOf.end() && !name)
    {
        return;
    }
    take(id);
    if (name)
    {
        mNameOf.emplace(id, *name);
        mByName[*name].push_back(id);
    }
    ++mGeneration;
}

const std::vector<LLUUID>& ALScriptNameIndex::named(std::string_view name) const
{
    static const std::vector<LLUUID> NONE;
    const auto                       found = mByName.find(name);
    return found == mByName.end() ? NONE : found->second;
}
