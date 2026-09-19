/**
 * @file alundostack.h
 * @brief The steps back and forward from where things are, whatever a step is
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

#include "stdtypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The steps back from where things are, and the steps forward again once
// some have been taken, whatever a step is: a document as it was, what
// one change did, a colour. What a step is and how one is taken are the
// caller's; this keeps them in order, joins a run of changes to one thing
// within a moment of each other into one step -- a slider dragged is one
// step, not one per pixel -- names each step by the first thing said
// after it, and forgets the oldest past a depth.
//
// A step has a `mLabel` the stack writes; everything else in it is the
// caller's.
template <typename Step>
class ALUndoStack
{
public:
    explicit ALUndoStack(size_t depth = 100) : mDepth(depth) {}

    // A change made: the way back from it, noted. A change to the same
    // thing -- the same key, within the window of the last -- joins the
    // step before rather than adding one, through `join(last, step)`,
    // which folds what the new step covers that the old did not. The next
    // label given names the step, either way.
    template <typename Join>
    void note(Step step, std::string_view key, F64 now, F64 window, Join&& join)
    {
        const bool same_run = !key.empty() && key == mLastKey && now - mLastTime < window && !mUndo.empty();
        if (same_run)
        {
            join(mUndo.back(), std::move(step));
        }
        else
        {
            mUndo.push_back(std::move(step));
        }
        mRedo.clear();
        mLabelPending = true;
        mLastKey = std::string(key);
        mLastTime = now;
        if (mUndo.size() > mDepth)
        {
            mUndo.erase(mUndo.begin());
        }
    }

    // What the last change was called, said once: the first label after a
    // change names it, and later ones are about something else.
    void label(std::string_view text)
    {
        if (mLabelPending && !mUndo.empty())
        {
            mUndo.back().mLabel = std::string(text);
        }
        mLabelPending = false;
    }
    // A frame later is something else.
    void closeLabel() { mLabelPending = false; }

    bool canUndo() const { return !mUndo.empty(); }
    bool canRedo() const { return !mRedo.empty(); }

    // The step back, taken off the stack for the caller to take; what it
    // took becomes the step forward once the caller says so.
    std::optional<Step> takeUndo()
    {
        if (mUndo.empty())
        {
            return std::nullopt;
        }
        mLastKey.clear();
        closeLabel();
        Step step = std::move(mUndo.back());
        mUndo.pop_back();
        return step;
    }
    std::optional<Step> takeRedo()
    {
        if (mRedo.empty())
        {
            return std::nullopt;
        }
        mLastKey.clear();
        closeLabel();
        Step step = std::move(mRedo.back());
        mRedo.pop_back();
        return step;
    }
    // A step put on either stack: the way forward from an undo, the way
    // back from a redo, or one that could not be taken put back.
    void pushUndo(Step step) { mUndo.push_back(std::move(step)); }
    void pushRedo(Step step) { mRedo.push_back(std::move(step)); }

    // Everything done, oldest first, and how many are in force: the steps
    // back, then the steps forward in the order they would be taken.
    const std::vector<Step>& undone() const { return mUndo; }
    const std::vector<Step>& redone() const { return mRedo; }
    size_t inForce() const { return mUndo.size(); }
    // The label of the next step back and of the next step forward, or
    // nothing.
    std::string undoLabel() const { return mUndo.empty() ? std::string() : mUndo.back().mLabel; }
    std::string redoLabel() const { return mRedo.empty() ? std::string() : mRedo.back().mLabel; }

    void clear()
    {
        mUndo.clear();
        mRedo.clear();
        mLabelPending = false;
        mLastKey.clear();
    }

private:
    std::vector<Step>   mUndo;
    std::vector<Step>   mRedo;
    size_t              mDepth;
    bool                mLabelPending = false;
    std::string         mLastKey;
    F64                 mLastTime = 0.0;
};
