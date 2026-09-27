/**
 * @file alscripttextcache.cpp
 * @brief The texts of includes fetched from the world, kept for the session within a budget.
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

#include "alscripttextcache.h"

bool ALScriptTextCache::take(const std::string& path, const LLUUID& asset_id, std::string& text)
{
    auto cached = mTexts.find(path);
    if (cached == mTexts.end() || cached->second.assetId != asset_id)
    {
        return false;
    }
    cached->second.used = ++mUse;
    text                = cached->second.text;
    return true;
}

void ALScriptTextCache::put(const std::string& path, const LLUUID& asset_id, std::string text)
{
    Cached cached;
    cached.assetId = asset_id;
    cached.text    = std::move(text);
    cached.used    = ++mUse;
    if (const auto was = mTexts.find(path); was != mTexts.end())
    {
        mHeld -= was->second.text.size();
    }
    mHeld += cached.text.size();
    mTexts[path] = std::move(cached);
    mFailed.erase(path);
    trim();
}

bool ALScriptTextCache::held(const std::string& path, std::string& text) const
{
    const auto held = mTexts.find(path);
    if (held == mTexts.end())
    {
        return false;
    }
    text = held->second.text;
    return true;
}

std::vector<std::string> ALScriptTextCache::paths() const
{
    std::vector<std::string> out;
    out.reserve(mTexts.size());
    for (const auto& [path, cached] : mTexts)
    {
        out.push_back(path);
    }
    return out;
}

void ALScriptTextCache::trim()
{
    // What is held, within the budget: the least lately read let go of
    // first, and the newest always kept whatever its size.
    while (mHeld > BUDGET && mTexts.size() > 1)
    {
        auto oldest = mTexts.begin();
        for (auto it = mTexts.begin(); it != mTexts.end(); ++it)
        {
            if (it->second.used < oldest->second.used)
            {
                oldest = it;
            }
        }
        if (oldest->second.used == mUse)
        {
            break;
        }
        mHeld -= oldest->second.text.size();
        mTexts.erase(oldest);
    }
}
