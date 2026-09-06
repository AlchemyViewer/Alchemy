/**
 * @file altexturetable.h
 * @brief ALTextureTable: dense, key-indexed table of ref-counted items with a round-robin cursor
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

#include "llerror.h"
#include "llpointer.h"

#include <algorithm>
#include <map>
#include <vector>

// A set of ref-counted items addressed two ways: by key, for lookup, and by
// dense position, for iteration and for a round-robin window that visits every
// item once per cycle. The table holds exactly one reference to each item.
//
// Items live in one contiguous vector. Erase swaps the last item into the
// hole, so positions are not stable across an erase and iteration order is
// insertion order only until the first one. Each item records its own
// position through getListIndex()/setListIndex(): erase is then one key
// lookup with no second probe to patch the moved item, and an item can answer
// whether it is in a table without a lookup.
//
// T derives from LLRefCount and provides
//     S32  getListIndex() const;
//     void setListIndex(S32 index);
// where -1 means "in no table". Key provides operator<.
//
// Not thread-safe; the owner serialises access.
template <typename Key, typename T>
class ALTextureTable
{
public:
    using item_t = LLPointer<T>;
    using items_t = std::vector<item_t>;
    using const_iterator = typename items_t::const_iterator;

    ALTextureTable() = default;
    ~ALTextureTable() { clear(); }
    ALTextureTable(const ALTextureTable&) = delete;
    ALTextureTable& operator=(const ALTextureTable&) = delete;

    void reserve(size_t count) { mItems.reserve(count); }

    size_t size() const { return mItems.size(); }
    bool empty() const { return mItems.empty(); }

    const_iterator begin() const { return mItems.cbegin(); }
    const_iterator end() const { return mItems.cend(); }

    // Item at dense position i. Positions hold only until the next erase.
    T* operator[](size_t i) const { return mItems[i].get(); }

    T* find(const Key& key) const
    {
        auto it = mIndex.find(key);
        return it == mIndex.end() ? nullptr : it->second;
    }

    bool contains(const Key& key) const { return mIndex.find(key) != mIndex.end(); }

    // Adds item under key and takes a reference to it. Refuses, leaving the
    // table unchanged, when the key is already present or the item is already
    // in a table.
    bool insert(const Key& key, T* item)
    {
        llassert(item);
        llassert(!mVisiting);
        if (!item || item->getListIndex() >= 0)
        {
            return false;
        }
        auto [it, added] = mIndex.emplace(key, item);
        if (!added)
        {
            return false;
        }
        item->setListIndex(static_cast<S32>(mItems.size()));
        mItems.emplace_back(item);
        return true;
    }

    // Removes the item under key and hands its reference to the caller, so
    // the caller decides when the last reference drops. Null when absent.
    item_t erase(const Key& key)
    {
        llassert(!mVisiting);
        auto it = mIndex.find(key);
        if (it == mIndex.end())
        {
            return nullptr;
        }
        T* item = it->second;
        mIndex.erase(it);

        const S32 index = item->getListIndex();
        llassert(index >= 0 && static_cast<size_t>(index) < mItems.size() && mItems[index] == item);
        item_t removed = std::move(mItems[index]);
        const S32 last = static_cast<S32>(mItems.size()) - 1;
        if (index != last)
        {
            mItems[index] = std::move(mItems[last]);
            mItems[index]->setListIndex(index);
        }
        mItems.pop_back();
        item->setListIndex(-1);
        return removed;
    }

    // Drops every item. Indices are reset before any reference is released,
    // so an item that outlives the table reads as in no table.
    void clear()
    {
        llassert(!mVisiting);
        for (const item_t& item : mItems)
        {
            item->setListIndex(-1);
        }
        mIndex.clear();
        mItems.clear();
        mCursor = 0;
    }

    // Round-robin window. Visits up to count items starting at the cursor,
    // wrapping at the end and visiting no item twice, then leaves the cursor
    // after the last one visited. Returns how many were visited. The visitor
    // receives a raw pointer and must not insert or erase; copy out what needs
    // processing and do it afterwards.
    template <typename Visitor>
    U32 visitWindow(U32 count, Visitor&& visit)
    {
        const U32 n = static_cast<U32>(mItems.size());
        if (n == 0)
        {
            mCursor = 0;
            return 0;
        }
        if (mCursor >= n)
        {
            // an erase shrank the table below the cursor since the last window
            mCursor = 0;
        }
        count = std::min(count, n);
        mVisiting = true;
        for (U32 i = 0; i < count; ++i)
        {
            visit(mItems[mCursor].get());
            if (++mCursor == n)
            {
                mCursor = 0;
            }
        }
        mVisiting = false;
        return count;
    }

    // Moves the cursor back count positions so the next window revisits items
    // a caller took from the last one but did not get to. Positions are
    // counted against the current size, so an erase in between makes this
    // approximate: the window is a fairness heuristic, not a schedule.
    void rewind(U32 count)
    {
        const U32 n = static_cast<U32>(mItems.size());
        if (n == 0)
        {
            mCursor = 0;
            return;
        }
        count = std::min(count, n);
        mCursor = (mCursor + n - count) % n;
    }

    U32 cursor() const { return mCursor; }

    // Every item's recorded index matches its position, and the key index
    // agrees with the vector on membership and count. For tests and asserts.
    bool consistent() const
    {
        if (mIndex.size() != mItems.size())
        {
            return false;
        }
        for (size_t i = 0; i < mItems.size(); ++i)
        {
            const item_t& item = mItems[i];
            if (item.isNull() || item->getListIndex() != static_cast<S32>(i))
            {
                return false;
            }
        }
        for (const auto& [key, item] : mIndex)
        {
            const S32 index = item->getListIndex();
            if (index < 0 || static_cast<size_t>(index) >= mItems.size() || mItems[index] != item)
            {
                return false;
            }
        }
        return true;
    }

private:
    using index_t = std::map<Key, T*>;

    items_t mItems;
    index_t mIndex;
    U32     mCursor = 0;
    bool    mVisiting = false;
};
