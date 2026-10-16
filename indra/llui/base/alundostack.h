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

#include <deque>
#include <iterator>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

// The steps back from where things are, and the steps forward again once
// some have been taken, whatever a step is: a document as it was, what
// one change did, a colour. What a step is and how one is taken are the
// caller's; this keeps them in order, joins a run of changes to one thing
// within a moment of each other into one step -- a slider dragged is one
// step, not one per pixel -- makes everything done while a group is open
// one step, drops a step that turns out to have changed nothing, names
// each step by the first thing said after it, and forgets the oldest past
// a depth.
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
    // True where the oldest step was forgotten to make room: whoever
    // counts steps from the bottom -- a saved mark -- counts one fewer.
    // While a group is open the key and the window do not matter: its first
    // change makes its step and the rest join it, and the oldest is not
    // forgotten until it closes. A run joined back to where it began --
    // `nothing(step)` true of it, a slider let go where it was picked up --
    // is dropped, as if it had never been noted; the steps forward it threw
    // away stay thrown.
    template <typename Join, typename Nothing>
    bool note(Step step, std::string_view key, F64 now, F64 window, Join&& join, Nothing&& nothing)
    {
        mRedo.clear();
        mLabelPending = true;
        if (mGroupDepth > 0)
        {
            if (mGroupStep)
            {
                join(mUndo.back(), std::move(step));
            }
            else
            {
                mUndo.push_back(std::move(step));
                mGroupStep = true;
                if (!mGroupLabel.empty())
                {
                    mUndo.back().mLabel = mGroupLabel;
                }
            }
            // A group that gave its name keeps it.
            mLabelPending = mGroupLabel.empty();
            return false;
        }
        const bool same_run = !key.empty() && key == mLastKey && now - mLastTime < window && !mUndo.empty();
        if (same_run)
        {
            join(mUndo.back(), std::move(step));
        }
        else
        {
            mUndo.push_back(std::move(step));
        }
        mLastKey = std::string(key);
        mLastTime = now;
        if (nothing(mUndo.back()))
        {
            mUndo.pop_back();
            mLastKey.clear();
            mLabelPending = false;
            return false;
        }
        return forgetOverDepth() > 0;
    }
    template <typename Join>
    bool note(Step step, std::string_view key, F64 now, F64 window, Join&& join)
    {
        return note(std::move(step), key, now, window, std::forward<Join>(join), [](const Step&) { return false; });
    }

    // Everything noted until the matching endGroup() is one step, whatever
    // its keys and however long it takes: a paste of several pieces, a
    // section reset, a Look applied. Groups nest, and count as the
    // outermost; the outermost's label, where it gives one, names the step.
    // A group that noted nothing leaves no step. Closing the outermost may
    // find its step changed nothing after all -- `nothing(step)`, which may
    // also trim from the step what undid itself -- and drop it; and it
    // forgets the oldest past the depth, which a group put off. How many it
    // forgot.
    void beginGroup(std::string_view label = std::string_view())
    {
        if (mGroupDepth++ == 0)
        {
            breakRun();
            mGroupStep  = false;
            mGroupLabel = std::string(label);
        }
    }
    template <typename Nothing>
    size_t endGroup(Nothing&& nothing)
    {
        if (mGroupDepth == 0 || --mGroupDepth > 0)
        {
            return 0;
        }
        return closeGroup(std::forward<Nothing>(nothing));
    }
    size_t endGroup()
    {
        return endGroup([](Step&) { return false; });
    }
    // Every group still open closed at once: whoever opened them is gone.
    size_t closeGroups()
    {
        if (mGroupDepth == 0)
        {
            breakRun();
            return 0;
        }
        mGroupDepth = 0;
        return closeGroup([](Step&) { return false; });
    }
    bool inGroup() const { return mGroupDepth > 0; }
    // Whether the last group to close made the newest step and left it
    // there; and a group that goes on filling that step, as though it had
    // never closed -- for a caller that closed its group to wait, and
    // comes back knowing nothing has been noted since.
    bool closedWithStep() const { return mClosedWithStep; }
    void resumeGroup()
    {
        const bool outermost = mGroupDepth == 0;
        beginGroup();
        if (outermost && !mUndo.empty())
        {
            mGroupStep = true;
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

    // The next change is a step of its own whatever its key: what a caller
    // says when it knows a run has ended -- the caret moved, a word was
    // finished, the text was saved -- and the key and the window would not.
    // Inside a group too: the group goes on, in a step of its own.
    void breakRun()
    {
        mLastKey.clear();
        mGroupStep = false;
    }

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
        mGroupStep = false;
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
        mGroupStep = false;
        Step step = std::move(mRedo.back());
        mRedo.pop_back();
        return step;
    }
    // A step put on either stack: the way forward from an undo, the way
    // back from a redo, or one that could not be taken put back.
    void pushUndo(Step step) { mUndo.push_back(std::move(step)); }
    void pushRedo(Step step) { mRedo.push_back(std::move(step)); }
    // The steps forward taken out whole, and put back whole: what a change
    // about to be noted throws away, held by a caller who may yet take that
    // change back -- an edit that fails part way -- and give them back.
    std::vector<Step> takeForward() { return std::exchange(mRedo, std::vector<Step>()); }
    void              putForward(std::vector<Step> forward) { mRedo = std::move(forward); }

    // The oldest steps back forgotten while what they weigh together --
    // `weigh(step)` each -- is past a budget, the newest always kept: a
    // stack of whole texts capped by the bytes it holds rather than by how
    // many. How many were forgotten. Given what they weigh together as a
    // caller keeps it, that is moved down by what was forgotten, and only
    // the steps forgotten are weighed.
    template <typename Weigh>
    size_t forgetOverBudget(size_t budget, Weigh&& weigh, size_t& held)
    {
        size_t forgot = 0;
        while (forgot + 1 < mUndo.size() && held > budget)
        {
            held -= weigh(mUndo[forgot]);
            ++forgot;
        }
        mUndo.erase(mUndo.begin(), mUndo.begin() + static_cast<std::ptrdiff_t>(forgot));
        return forgot;
    }
    template <typename Weigh>
    size_t forgetOverBudget(size_t budget, Weigh&& weigh)
    {
        size_t held = 0;
        for (const Step& step : mUndo)
        {
            held += weigh(step);
        }
        return forgetOverBudget(budget, weigh, held);
    }

    // Everything done, and how many are in force: the steps back, oldest
    // first, the last the next to be taken back -- the oldest let go of
    // from the front, which moves none of the rest -- and the steps
    // forward, likewise the last the next to be taken: the reverse of the
    // order they would be taken in.
    const std::deque<Step>&  undone() const { return mUndo; }
    // The newest step back, for its caller to say more about it; there
    // must be one.
    Step&                    newest() { return mUndo.back(); }
    const std::vector<Step>& redone() const { return mRedo; }
    size_t inForce() const { return mUndo.size(); }
    // The label of the next step back and of the next step forward, or
    // nothing.
    std::string undoLabel() const { return mUndo.empty() ? std::string() : mUndo.back().mLabel; }
    std::string redoLabel() const { return mRedo.empty() ? std::string() : mRedo.back().mLabel; }

    // Everything forgotten, the groups still open with it.
    void clear()
    {
        mUndo.clear();
        mRedo.clear();
        mLabelPending = false;
        mLastKey.clear();
        mGroupDepth = 0;
        mGroupStep  = false;
        mGroupLabel.clear();
    }

    // Both stacks put back whole, as they were written somewhere: the
    // oldest steps back past the depth forgotten. How many were forgotten,
    // for whoever counts steps from the bottom.
    size_t restore(std::vector<Step> undo, std::vector<Step> redo)
    {
        clear();
        const size_t dropped = undo.size() > mDepth ? undo.size() - mDepth : 0;
        mUndo.assign(std::make_move_iterator(undo.begin() + static_cast<std::ptrdiff_t>(dropped)), std::make_move_iterator(undo.end()));
        mRedo = std::move(redo);
        return dropped;
    }

private:
    template <typename Nothing>
    size_t closeGroup(Nothing&& nothing)
    {
        const bool made = mGroupStep;
        breakRun();
        mGroupLabel.clear();
        mClosedWithStep = made && !mUndo.empty();
        if (mClosedWithStep && nothing(mUndo.back()))
        {
            mUndo.pop_back();
            mLabelPending   = false;
            mClosedWithStep = false;
        }
        return forgetOverDepth();
    }
    // The oldest forgotten past the depth: one as a change is noted, or as
    // many as a group made, the run breaks inside it each a step.
    size_t forgetOverDepth()
    {
        const size_t over = mUndo.size() > mDepth ? mUndo.size() - mDepth : 0;
        mUndo.erase(mUndo.begin(), mUndo.begin() + static_cast<std::ptrdiff_t>(over));
        return over;
    }

    std::deque<Step>    mUndo;
    std::vector<Step>   mRedo;
    size_t              mDepth;
    bool                mLabelPending = false;
    std::string         mLastKey;
    F64                 mLastTime = 0.0;
    // How deep the groups open are; whether the outermost has made the step
    // it is filling, which is the newest; and what it asked its steps to be
    // called.
    S32                 mGroupDepth = 0;
    bool                mGroupStep  = false;
    bool                mClosedWithStep = false;
    std::string         mGroupLabel;
};
