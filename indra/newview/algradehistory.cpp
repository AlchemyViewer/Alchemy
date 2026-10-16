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

#include "llsdutil.h"

#include <algorithm>

namespace
{
    using Change      = ALGradeHistory::Change;
    using Transaction = ALGradeHistory::Transaction;

    // A write joined to a step: to the control's own change where the step
    // has one -- a drag still moving, or a control written twice in a group
    // -- so that a step holds one before and one after per control; else a
    // change of its own.
    template <typename Step>
    void join(Step& step, Step&& next)
    {
        for (Change& change : next.mChanges)
        {
            auto existing = std::find_if(step.mChanges.begin(), step.mChanges.end(),
                                         [&change](const Change& c) { return c.mName == change.mName; });
            if (existing != step.mChanges.end())
            {
                existing->mAfter = std::move(change.mAfter);
            }
            else
            {
                step.mChanges.push_back(std::move(change));
            }
        }
    }

    bool unchanged(const Change& change)
    {
        return llsd_equals(change.mBefore, change.mAfter);
    }
}

const ALGradeHistory::Step& ALGradeHistory::stepAt(size_t index) const
{
    const auto& back = mSteps.undone();
    if (index < back.size())
    {
        return back[index];
    }
    const auto& forward = mSteps.redone();
    return forward[forward.size() - 1 - (index - back.size())];
}

void ALGradeHistory::record(const std::string& name, const LLSD& before, const LLSD& after, F64 now)
{
    // Not a change, so not a reason to touch anything -- least of all the
    // redo tail, which a write of the value already there would otherwise
    // throw away for nothing.
    if (name.empty() || llsd_equals(before, after))
    {
        return;
    }

    ++mRevision;

    // Anything recorded invalidates the redo tail: the future that was
    // undone is no longer reachable from here. Writes to the same control
    // closer together than the window are one step -- one drag stays one
    // step, its before the value it started from and its after following
    // the puck -- and back where it started, it is not a step any more: the
    // drag may yet go on, and if it does it starts a new step from here,
    // which is this same starting value. Inside a group, every write joins
    // the group's step.
    Step step;
    step.mChanges.push_back({ name, before, after });
    mSteps.note(std::move(step), name, now, COALESCE_SECONDS, join<Step>,
                [](const Step& s) { return std::all_of(s.mChanges.begin(), s.mChanges.end(), unchanged); });
}

void ALGradeHistory::beginGroup(const std::string& label)
{
    mSteps.beginGroup(label);
}

void ALGradeHistory::endGroup()
{
    const bool open = mSteps.inGroup();
    // A control the group moved and then put back is not part of what the
    // group did, and a group made of nothing else did nothing at all.
    mSteps.endGroup([](Step& step) {
        step.mChanges.erase(std::remove_if(step.mChanges.begin(), step.mChanges.end(), unchanged), step.mChanges.end());
        return step.mChanges.empty();
    });
    if (open && !mSteps.inGroup())
    {
        ++mRevision;
    }
}

const ALGradeHistory::Transaction* ALGradeHistory::undo()
{
    std::optional<Step> step = mSteps.takeUndo();
    if (!step)
    {
        return nullptr;
    }
    ++mRevision;
    mSteps.pushRedo(std::move(*step));
    return &mSteps.redone().back().mChanges;
}

const ALGradeHistory::Transaction* ALGradeHistory::redo()
{
    std::optional<Step> step = mSteps.takeRedo();
    if (!step)
    {
        return nullptr;
    }
    ++mRevision;
    mSteps.pushUndo(std::move(*step));
    return &mSteps.undone().back().mChanges;
}

void ALGradeHistory::clear()
{
    ++mRevision;
    mSteps.clear();
}
