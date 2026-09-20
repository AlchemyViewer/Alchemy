/**
 * @file altextundo.cpp
 * @brief The steps back and forward through a document's edits.
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

#include "altextundo.h"

#include "llstring.h"

namespace
{
    // One grapheme, and no line break.
    bool oneCluster(const std::string& text)
    {
        return !text.empty() && text.find('\n') == std::string::npos &&
               utf8str_step_grapheme_forward(text, 0) == text.size();
    }
}

ALTextUndo::ALTextUndo(ALTextDocument& document)
:   mDocument(document)
{
}

const char* ALTextUndo::kindOf(const ALTextDocument::Edit& edit)
{
    if (edit.removed.empty() && oneCluster(edit.inserted))
    {
        return "typing";
    }
    if (edit.inserted.empty() && oneCluster(edit.removed))
    {
        return "erasing";
    }
    return "step";
}

bool ALTextUndo::carriesOn(const Step& last, const ALTextDocument::Edit& next)
{
    if (last.edits.empty())
    {
        return false;
    }
    const ALTextDocument::Edit& tail = last.edits.back();
    const char* kind = kindOf(next);
    if (kind != kindOf(tail))
    {
        return false;
    }
    if (kind[0] == 't')
    {
        // Typed where the last character ended.
        return next.range.begin == tail.endAfter();
    }
    if (kind[0] == 'e')
    {
        // A backspace takes the character before the last one taken; a
        // delete takes the one that moved into its place.
        return next.range.end == tail.range.begin || next.range.begin == tail.range.begin;
    }
    return false;
}

void ALTextUndo::join(Step& last, Step&& next)
{
    for (ALTextDocument::Edit& edit : next.edits)
    {
        last.edits.push_back(std::move(edit));
    }
    last.caretAfter = next.caretAfter;
}

void ALTextUndo::record(const ALTextDocument::Edit& edit, const ALTextPos& before, const ALTextPos& after, F64 now)
{
    if (edit.nothing())
    {
        return;
    }
    // A change after an undo throws the redo steps away; the saved text,
    // if it was among them, can no longer be reached by stepping.
    if (mSavedInForce != NOWHERE && mSavedInForce > mSteps.inForce())
    {
        mSavedInForce = NOWHERE;
    }

    Step step;
    step.edits.push_back(edit);
    step.caretBefore = before;
    step.caretAfter  = after;

    // The key a run is joined by: the open group, or the kind of change
    // where it carries on the last step; anything else ends the run first.
    std::string_view key = "group";
    if (mGroupDepth == 0)
    {
        key = kindOf(edit);
        if (mSteps.undone().empty() || !carriesOn(mSteps.undone().back(), edit))
        {
            mSteps.breakRun();
        }
    }
    // A group is one step however long it stays open; a run is one step
    // while its changes come within the window.
    mSteps.note(std::move(step), key, now, mGroupDepth > 0 ? 1e9 : mWindow, join);
}

void ALTextUndo::beginGroup()
{
    if (mGroupDepth++ == 0)
    {
        mSteps.breakRun();
    }
}

void ALTextUndo::endGroup()
{
    if (mGroupDepth > 0 && --mGroupDepth == 0)
    {
        mSteps.breakRun();
    }
}

std::optional<ALTextPos> ALTextUndo::undo()
{
    std::optional<Step> step = mSteps.takeUndo();
    if (!step)
    {
        return std::nullopt;
    }
    for (auto it = step->edits.rbegin(); it != step->edits.rend(); ++it)
    {
        const ALTextDocument::Edit back = it->inverse();
        mDocument.replace(back.range, back.inserted);
    }
    const ALTextPos caret = step->caretBefore;
    mSteps.pushRedo(std::move(*step));
    return caret;
}

std::optional<ALTextPos> ALTextUndo::redo()
{
    std::optional<Step> step = mSteps.takeRedo();
    if (!step)
    {
        return std::nullopt;
    }
    for (const ALTextDocument::Edit& edit : step->edits)
    {
        mDocument.replace(edit.range, edit.inserted);
    }
    const ALTextPos caret = step->caretAfter;
    mSteps.pushUndo(std::move(*step));
    return caret;
}

void ALTextUndo::clear()
{
    mSteps.clear();
    mGroupDepth   = 0;
    mSavedInForce = 0;
}

void ALTextUndo::markSaved()
{
    mSavedInForce = mSteps.inForce();
    mSteps.breakRun();
}

bool ALTextUndo::isPristine() const
{
    return mSavedInForce != NOWHERE && mSteps.inForce() == mSavedInForce;
}
