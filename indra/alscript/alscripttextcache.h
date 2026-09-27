/**
 * @file alscripttextcache.h
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

#pragma once

#include "llstl.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <string>
#include <vector>

// The texts fetched, by the identity they came under, with the asset each
// was: kept for the session so that a check need wait for nothing, and the
// oldest let go of past a budget, since a session that opens a hundred
// scripts would otherwise hold every include any of them ever named. And
// what failed to come, which is not asked for again until a run retries it.
class ALScriptTextCache
{
public:
    static constexpr size_t BUDGET = 16u * 1024u * 1024u;

    // The text held under `path` as the asset `asset_id` -- read, so kept
    // the longer; false where none is, or one of another asset.
    bool take(const std::string& path, const LLUUID& asset_id, std::string& text);
    // A text fetched, in place of whatever was held under its identity;
    // no longer failed.
    void put(const std::string& path, const LLUUID& asset_id, std::string text);
    // Whatever is held under `path`, of whatever asset.
    bool                     held(const std::string& path, std::string& text) const;
    bool                     holds(const std::string& path) const { return mTexts.contains(path); }
    std::vector<std::string> paths() const;
    size_t                   bytes() const { return mHeld; }

    void failed(const std::string& path) { mFailed.insert(path); }
    bool hasFailed(const std::string& path) const { return mFailed.contains(path); }
    void forgetFailure(const std::string& path) { mFailed.erase(path); }

private:
    struct Cached
    {
        LLUUID      assetId;
        std::string text;
        U32         used = 0;
    };
    // The oldest let go of until what is held is within the budget.
    void trim();

    boost::unordered_flat_map<std::string, Cached, ll::string_hash, std::equal_to<>> mTexts;
    U32                                                                             mUse  = 0;
    size_t                                                                          mHeld = 0;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>        mFailed;
};
