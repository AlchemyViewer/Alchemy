/**
 * @file almasterqueue.cpp
 * @brief The sends of master files to their scripts underway, and those waiting their turn.
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

#include "almasterqueue.h"

#include <algorithm>

ALMasterQueue::ALMasterQueue(size_t limit)
    : mLimit(std::max<size_t>(limit, 1))
{
}

// static
ALMasterQueue::Send ALMasterQueue::outranking(Send had, Send asked)
{
    return had == Send::Direct || asked == Send::Direct ? Send::Direct : Send::Derived;
}

bool ALMasterQueue::ask(const std::string& key, Send kind)
{
    // On its way: once more after it, the newest text going up last.
    if (auto found = mUnderway.find(key); found != mUnderway.end())
    {
        found->second = found->second ? outranking(*found->second, kind) : kind;
        return false;
    }
    // Waiting its turn: in its place still.
    if (auto found = mWaiting.find(key); found != mWaiting.end())
    {
        found->second.kind = outranking(found->second.kind, kind);
        return false;
    }
    if (mUnderway.size() < mLimit)
    {
        mUnderway.emplace(key, std::nullopt);
        return true;
    }
    line(key, kind);
    return false;
}

void ALMasterQueue::line(const std::string& key, Send kind)
{
    const U64 order = mNextOrder++;
    mWaiting.emplace(key, Waiting{ order, kind });
    mLine.emplace(order, key);
}

std::vector<std::pair<std::string, ALMasterPlan::Send>> ALMasterQueue::finished(const std::string& key)
{
    if (auto found = mUnderway.find(key); found != mUnderway.end())
    {
        const std::optional<Send> again = found->second;
        mUnderway.erase(found);
        // Asked again while it went: behind every other waiting, so that
        // what was asked first goes first.
        if (again)
        {
            line(key, *again);
        }
    }
    std::vector<std::pair<std::string, Send>> out;
    while (mUnderway.size() < mLimit && !mLine.empty())
    {
        const auto        first = mLine.begin();
        const std::string next  = first->second;
        mLine.erase(first);
        const auto waiting = mWaiting.find(next);
        const Send kind    = waiting->second.kind;
        mWaiting.erase(waiting);
        mUnderway.emplace(next, std::nullopt);
        out.emplace_back(next, kind);
    }
    return out;
}

void ALMasterQueue::drop(const std::string& key)
{
    if (auto found = mUnderway.find(key); found != mUnderway.end())
    {
        found->second.reset();
    }
    if (auto found = mWaiting.find(key); found != mWaiting.end())
    {
        mLine.erase(found->second.order);
        mWaiting.erase(found);
    }
}

bool ALMasterQueue::underway(const std::string& key) const
{
    return mUnderway.contains(key);
}

bool ALMasterQueue::waiting(const std::string& key) const
{
    if (const auto found = mUnderway.find(key); found != mUnderway.end())
    {
        return found->second.has_value();
    }
    return mWaiting.contains(key);
}

void ALMasterQueue::clear()
{
    mUnderway.clear();
    mWaiting.clear();
    mLine.clear();
}
