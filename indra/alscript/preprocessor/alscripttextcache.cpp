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

#include <algorithm>
#include <utility>

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
    // first, and the newest always kept whatever its size. Put in order
    // once for however many go, rather than the whole cache looked through
    // for each.
    if (mHeld <= BUDGET || mTexts.size() <= 1)
    {
        return;
    }
    std::vector<std::pair<U32, std::string>> by_use;
    by_use.reserve(mTexts.size());
    for (const auto& [path, cached] : mTexts)
    {
        by_use.emplace_back(cached.used, path);
    }
    std::sort(by_use.begin(), by_use.end());
    for (const auto& [used, path] : by_use)
    {
        if (mHeld <= BUDGET || used == mUse)
        {
            break;
        }
        const auto oldest = mTexts.find(path);
        mHeld -= oldest->second.text.size();
        mTexts.erase(oldest);
    }
}

void ALScriptTextCache::failed(const std::string& path)
{
    // A session that names a great many includes that never come is not
    // held to every one: all asked for again past a few thousand.
    if (mFailed.size() >= FAILURES_KEPT && !mFailed.contains(path))
    {
        mFailed.clear();
    }
    mFailed.insert(path);
}
