/**
 * @file alobjectroster.h
 * @brief ALObjectRoster: a list of ref-counted objects that know their place in it, and the queue of those leaving
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
#include <deque>
#include <iterator>
#include <vector>

// The objects of a list, each holding its own position in it, and a queue of
// those marked for removal, which leave a few at a time.
//
// An object marked for removal stays in the list, where loops over the list
// still meet it, until the queue reaches it: then it leaves by a swap with the
// last object and a pop, and the queue lets go of its reference. Positions are
// therefore not stable across a removal. The queue's length is the number of
// objects waiting to leave, and an object is queued at most once however often
// it is marked, so the count and the queue cannot disagree.
//
// T derives from LLRefCount and provides
//     S32  getObjectListIndex() const;
//     void setObjectListIndex(S32 index);   // -1: in no list
//     bool isPendingRemoval() const;
//     void setPendingRemoval();
// An object may be queued without being in the list; leaving then only drops
// the queue's reference.
//
// Not thread-safe; the owner serialises access. An object's destructor, run as
// it leaves, may mark others for removal: they join the queue's back.
template <typename T>
class ALObjectRoster
{
public:
    using item_t = LLPointer<T>;
    using items_t = std::vector<item_t>;
    // Read-only: a write through an iterator would leave the objects' recorded positions behind.
    using const_iterator = typename items_t::const_iterator;

    // Objects a budgeted drain removes a frame whatever its time budget: a
    // floor, and a share of the queue that grows with it, so a queue that
    // grows faster than the budget drains still shrinks.
    static constexpr size_t MIN_DRAIN = 1;
    static constexpr size_t DRAIN_SHARE = 256;

    static size_t drainFloor(size_t queued)
    {
        return std::min(queued, std::max(MIN_DRAIN, queued / DRAIN_SHARE));
    }

    ALObjectRoster() = default;
    ~ALObjectRoster() { clear(); }
    ALObjectRoster(const ALObjectRoster&) = delete;
    ALObjectRoster& operator=(const ALObjectRoster&) = delete;

    size_t size() const { return mItems.size(); }
    bool empty() const { return mItems.empty(); }

    const_iterator begin() const { return mItems.cbegin(); }
    const_iterator end() const { return mItems.cend(); }

    // Object at position i. Positions hold only until the next removal.
    T* operator[](size_t i) const { return mItems[i].get(); }

    size_t queued() const { return mQueue.size(); }

    // Appends obj and takes a reference to it. Refuses an object already in a
    // list, or one already marked for removal.
    bool add(T* obj)
    {
        llassert(obj);
        if (!obj || obj->getObjectListIndex() >= 0 || obj->isPendingRemoval())
        {
            return false;
        }
        obj->setObjectListIndex(static_cast<S32>(mItems.size()));
        mItems.emplace_back(obj);
        return true;
    }

    // Marks obj for removal and queues it. False when it was already marked.
    bool queueRemoval(T* obj)
    {
        llassert(obj);
        if (!obj || obj->isPendingRemoval())
        {
            return false;
        }
        obj->setPendingRemoval();
        mQueue.emplace_back(obj);
        return true;
    }

    // Removes queued objects, oldest first, while within(removed so far) says
    // to go on. Returns how many left.
    template <typename Within>
    size_t drain(Within&& within)
    {
        size_t removed = 0;
        while (!mQueue.empty() && within(removed))
        {
            item_t obj = std::move(mQueue.front());
            mQueue.pop_front();
            leave(obj);
            ++removed;
        }
        return removed;
    }

    // Removes every queued object select(obj) picks, whatever the budget, and
    // keeps the others queued in order. Returns how many left.
    template <typename Select>
    size_t drainIf(Select&& select)
    {
        std::vector<item_t> leaving;
        auto kept = std::stable_partition(mQueue.begin(), mQueue.end(), [&](const item_t& obj) { return !select(obj.get()); });
        std::move(kept, mQueue.end(), std::back_inserter(leaving));
        mQueue.erase(kept, mQueue.end());
        for (item_t& obj : leaving)
        {
            leave(obj);
        }
        return leaving.size();
    }

    // Drops every object and the queue. Positions are reset before any
    // reference is released, so an object that outlives the list reads as in
    // no list.
    void clear()
    {
        for (const item_t& obj : mItems)
        {
            obj->setObjectListIndex(-1);
        }
        items_t items;
        items.swap(mItems);
        std::deque<item_t> queue;
        queue.swap(mQueue);
    }

    // Every object's recorded position is its position, and every queued
    // object is marked.
    bool consistent() const
    {
        for (size_t i = 0; i < mItems.size(); ++i)
        {
            if (mItems[i].isNull() || mItems[i]->getObjectListIndex() != static_cast<S32>(i))
            {
                return false;
            }
        }
        return std::all_of(mQueue.begin(), mQueue.end(), [](const item_t& obj) { return obj->isPendingRemoval(); });
    }

private:
    // Takes obj out of the list, if it is in it, by moving the last object
    // into its place, then drops the reference the caller held for the queue.
    void leave(item_t& obj)
    {
        const S32 index = obj->getObjectListIndex();
        if (index >= 0)
        {
            llassert(static_cast<size_t>(index) < mItems.size() && mItems[index] == obj);
            item_t removed = std::move(mItems[index]);
            const size_t last = mItems.size() - 1;
            if (static_cast<size_t>(index) != last)
            {
                mItems[index] = std::move(mItems[last]);
                mItems[index]->setObjectListIndex(index);
            }
            mItems.pop_back();
            obj->setObjectListIndex(-1);
        }
        obj = nullptr;
    }

    items_t mItems;
    std::deque<item_t> mQueue;
};
