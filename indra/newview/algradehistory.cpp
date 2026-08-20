/**
 * @file algradehistory.cpp
 * @brief Undo/redo for the Lightbox's settings, as transactions
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "algradehistory.h"

#include <algorithm>

bool ALGradeHistory::canCoalesce(const std::string& name, F32 now) const
{
    // Only ever into the newest step, and only when that step is a lone write
    // to this same control. A transaction that already covers two controls was
    // a deliberate group, and swallowing a stray write into it would make the
    // group mean something its author did not intend.
    return mHaveLast
        && mGroupDepth == 0
        && mCursor == mStack.size()
        && !mStack.empty()
        && mStack.back().size() == 1
        && mLastName == name
        && (now - mLastTime) <= COALESCE_SECONDS;
}

void ALGradeHistory::record(const std::string& name, const LLSD& before, const LLSD& after, F32 now)
{
    if (name.empty())
    {
        return;
    }

    // Anything recorded invalidates the redo tail: the future that was undone
    // is no longer reachable from here.
    if (mCursor < mStack.size())
    {
        mStack.resize(mCursor);
    }

    if (mGroupDepth > 0)
    {
        // Inside a group. Extend the group's transaction, unless this control
        // is already in it -- in which case only the destination moves, so the
        // group still describes one before and one after per control.
        //
        // MAX_DEPTH is deliberately not enforced here: evicting the front
        // would shift mGroupIndex out from under the group. endGroup does it,
        // once the index is dead.
        if (mGroupIndex >= mStack.size())
        {
            mStack.emplace_back();
            mGroupIndex = mStack.size() - 1;
        }

        Transaction& group = mStack[mGroupIndex];
        auto existing = std::find_if(group.begin(), group.end(),
                                     [&name](const Change& c) { return c.mName == name; });
        if (existing != group.end())
        {
            existing->mAfter = after;
        }
        else
        {
            group.push_back({ name, before, after });
        }

        mCursor    = mStack.size();
        mHaveLast  = false;
        return;
    }

    if (canCoalesce(name, now))
    {
        // Same control, still moving: keep the value it started from and let
        // the destination follow. One drag stays one step.
        mStack.back().front().mAfter = after;
    }
    else
    {
        mStack.push_back(Transaction{ { name, before, after } });

        if (mStack.size() > MAX_DEPTH)
        {
            mStack.erase(mStack.begin());
        }
    }

    mCursor   = mStack.size();
    mLastName = name;
    mLastTime = now;
    mHaveLast = true;
}

void ALGradeHistory::beginGroup()
{
    if (mGroupDepth++ == 0)
    {
        // One past the end: the transaction is created by the first write, so
        // a group that records nothing leaves no empty step behind.
        mGroupIndex = mStack.size();
        mHaveLast   = false;
    }
}

void ALGradeHistory::endGroup()
{
    if (mGroupDepth > 0 && --mGroupDepth == 0)
    {
        // The eviction that record()'s plain path does as it pushes, deferred
        // to here, where erasing the front can no longer shift mGroupIndex
        // out from under an open group. A group adds at most one transaction
        // -- that is its whole point -- so one erase restores the bound. The
        // cursor counts applied transactions and the one dropped was applied,
        // so it comes down with the stack.
        if (mStack.size() > MAX_DEPTH)
        {
            mStack.erase(mStack.begin());
            if (mCursor > 0)
            {
                --mCursor;
            }
        }

        // A fresh write after the group starts its own step rather than
        // coalescing into it.
        mHaveLast = false;
    }
}

const ALGradeHistory::Transaction* ALGradeHistory::undo()
{
    if (!canUndo())
    {
        return nullptr;
    }

    // Applying the result must not fold back into the step it came from.
    mHaveLast = false;
    return &mStack[--mCursor];
}

const ALGradeHistory::Transaction* ALGradeHistory::redo()
{
    if (!canRedo())
    {
        return nullptr;
    }

    mHaveLast = false;
    return &mStack[mCursor++];
}

void ALGradeHistory::clear()
{
    mStack.clear();
    mCursor     = 0;
    mGroupDepth = 0;
    mGroupIndex = 0;
    mHaveLast   = false;
}
