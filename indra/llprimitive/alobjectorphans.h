/**
 * @file alobjectorphans.h
 * @brief The objects waiting for a parent the viewer has not got.
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

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <utility>
#include <vector>

// The objects waiting for a parent the viewer has not got, each for the one
// parent its latest update named. A parent is a key its owner makes of its
// region and local ID.
class ALObjectOrphans
{
public:
    // child waits for parent, and no longer for any other it was waiting for.
    void wait(const LLUUID& child, U64 parent)
    {
        const auto [iter, added] = mParentOf.try_emplace(child, parent);
        if (!added)
        {
            if (iter->second == parent)
            {
                return;
            }
            forget(child, iter->second);
            iter->second = parent;
        }
        mChildrenOf[parent].push_back(child);
    }

    // child waits for nothing. False where it was waiting for nothing already.
    bool release(const LLUUID& child)
    {
        const auto iter = mParentOf.find(child);
        if (iter == mParentOf.end())
        {
            return false;
        }
        forget(child, iter->second);
        mParentOf.erase(iter);
        return true;
    }

    // The children waiting for parent, in the order they began to, none of
    // them waiting any longer.
    std::vector<LLUUID> take(U64 parent)
    {
        const auto iter = mChildrenOf.find(parent);
        if (iter == mChildrenOf.end())
        {
            return {};
        }
        std::vector<LLUUID> children = std::move(iter->second);
        mChildrenOf.erase(iter);
        for (const LLUUID& child : children)
        {
            mParentOf.erase(child);
        }
        return children;
    }

    bool empty() const { return mParentOf.empty(); }
    S32 childCount() const { return static_cast<S32>(mParentOf.size()); }
    S32 parentCount() const { return static_cast<S32>(mChildrenOf.size()); }

private:
    void forget(const LLUUID& child, U64 parent)
    {
        const auto iter = mChildrenOf.find(parent);
        if (iter != mChildrenOf.end())
        {
            std::vector<LLUUID>& children = iter->second;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
            if (children.empty())
            {
                mChildrenOf.erase(iter);
            }
        }
    }

    boost::unordered_flat_map<LLUUID, U64> mParentOf;
    boost::unordered_flat_map<U64, std::vector<LLUUID>> mChildrenOf;
};
