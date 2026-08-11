/**
 * @file algradehistory.h
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

#ifndef AL_GRADEHISTORY_H
#define AL_GRADEHISTORY_H

#include "llsd.h"

#include <string>
#include <vector>

/**
 * The Lightbox's undo stack.
 *
 * @par Why transactions rather than single values
 * "Undo" has to mean one thing the user did, and one thing the user did is
 * often several settings. A section's Reset All writes eleven controls; undoing
 * that eleven times would be absurd. So the unit here is a transaction -- a set
 * of (control, before, after) -- and a group of writes can be marked as one.
 *
 * @par Why coalescing
 * Dragging a wheel puck emits a commit per mouse-move. Recorded naively, one
 * drag would be a hundred undo steps and Ctrl+Z would appear broken. Successive
 * writes to the *same* control within a short window therefore extend the top
 * transaction instead of pushing a new one: its `before` stays the value from
 * the start of the drag, and its `after` follows the puck.
 *
 * @par No clock in here
 * `record` takes the current time rather than reading one, so the coalescing
 * window is exercised by the tests directly instead of by sleeping.
 *
 * @par What it deliberately does not do
 * It stores values, not dirty state. Undoing restores what the settings were;
 * it does not try to restore whether the active Look was considered modified.
 * Reconstructing that faithfully would mean modelling the Looks system's own
 * history as well, and the honest simple behaviour -- your values come back,
 * the Look stays dirty -- is one a user can predict.
 */
class ALGradeHistory
{
public:
    struct Change
    {
        std::string mName;
        LLSD        mBefore;
        LLSD        mAfter;
    };

    using Transaction = std::vector<Change>;

    /// Writes to one control closer together than this join the previous step.
    static constexpr F32 COALESCE_SECONDS = 0.5f;

    /// Steps kept. Beyond this the oldest is dropped: a grading session is long
    /// and the values are small, but it should not grow without bound.
    static constexpr size_t MAX_DEPTH = 64;

    /// Record one control changing from @a before to @a after at @a now
    /// (seconds, monotonic; only differences matter). Recording anything
    /// discards the redo tail, as every undo stack does.
    void record(const std::string& name, const LLSD& before, const LLSD& after, F32 now);

    /// @name Grouping
    /// Everything recorded between these becomes a single step, whatever the
    /// controls or the timing -- for a section reset, or applying a Look.
    /// Nesting is counted, so a group inside a group is still one step.
    /// @{
    void beginGroup();
    void endGroup();
    /// @}

    bool canUndo() const { return mCursor > 0; }
    bool canRedo() const { return mCursor < mStack.size(); }

    /// Step back one transaction and return it, or null if there is nothing to
    /// undo. Apply each Change's @c mBefore, in any order -- a transaction
    /// never contains the same control twice.
    const Transaction* undo();

    /// Step forward one transaction and return it, or null. Apply @c mAfter.
    const Transaction* redo();

    void   clear();
    size_t depth() const { return mStack.size(); }
    size_t cursor() const { return mCursor; }

private:
    /// True when the top transaction is a lone write to @a name that is recent
    /// enough to absorb another.
    bool canCoalesce(const std::string& name, F32 now) const;

    std::vector<Transaction> mStack;

    /// How many transactions are currently applied. Everything at or past this
    /// index is redoable; everything before it is undoable.
    size_t mCursor = 0;

    S32         mGroupDepth = 0;
    /// Index of the transaction the current group is accumulating into.
    size_t      mGroupIndex = 0;
    std::string mLastName;
    F32         mLastTime = 0.f;
    bool        mHaveLast = false;
};

#endif // AL_GRADEHISTORY_H
