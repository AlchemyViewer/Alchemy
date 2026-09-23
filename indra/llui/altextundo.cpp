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
    LLSD posAsLLSD(const ALTextPos& pos)
    {
        return LLSD::emptyArray().with(0, pos.line).with(1, pos.column);
    }

    ALTextPos posFrom(const LLSD& sd)
    {
        return ALTextPos(sd[0].asInteger(), sd[1].asInteger());
    }

    // Whether a range stands within a text: its lines there, its columns
    // within them, and in order.
    bool within(const ALTextDocument& text, const ALTextRange& range)
    {
        const auto fits = [&text](const ALTextPos& pos) {
            return pos.line >= 0 && pos.line < text.lineCount() && pos.column >= 0 && pos.column <= static_cast<S32>(text.line(pos.line).size());
        };
        return fits(range.begin) && fits(range.end) && !(range.end < range.begin);
    }

    // One step's edits made over a text, forward or back, each checked
    // first: the text it takes away must be what stands there.
    template <typename Step>
    bool replay(ALTextDocument& text, const Step& step, bool forward)
    {
        if (forward)
        {
            for (const ALTextDocument::Edit& edit : step.edits)
            {
                if (!within(text, edit.range) || text.text(edit.range) != edit.removed)
                {
                    return false;
                }
                text.replace(edit.range, edit.inserted);
            }
            return true;
        }
        for (auto it = step.edits.rbegin(); it != step.edits.rend(); ++it)
        {
            const ALTextDocument::Edit back = it->inverse();
            if (!within(text, back.range) || text.text(back.range) != back.removed)
            {
                return false;
            }
            text.replace(back.range, back.inserted);
        }
        return true;
    }

    // One grapheme, and no line break.
    bool oneCluster(const std::string& text)
    {
        return !text.empty() && text.find('\n') == std::string::npos &&
               utf8str_step_grapheme_forward(text, 0) == text.size();
    }

    // What the history written out is, for a reader to refuse another.
    constexpr S32 HISTORY_VERSION = 2;
    // About what a step and an edit cost written beyond their text: the
    // carets, the label's quotes, the edit's places. What the budget
    // counts, so that a history of many small edits is held to it too.
    constexpr size_t STEP_WRITTEN = 48;
    constexpr size_t EDIT_WRITTEN = 16;

    // An edit as it is written: where it begins and ends, what it took and
    // what it put -- an array rather than a map, since a history is mostly
    // edits, and their names would be most of what was written.
    LLSD editAsLLSD(const ALTextDocument::Edit& edit)
    {
        return LLSD::emptyArray()
            .with(0, edit.range.begin.line)
            .with(1, edit.range.begin.column)
            .with(2, edit.range.end.line)
            .with(3, edit.range.end.column)
            .with(4, edit.removed)
            .with(5, edit.inserted);
    }

    bool editFrom(const LLSD& sd, ALTextDocument::Edit& edit)
    {
        if (!sd.isArray() || sd.size() != 6)
        {
            return false;
        }
        edit.range    = ALTextRange(ALTextPos(sd[0].asInteger(), sd[1].asInteger()), ALTextPos(sd[2].asInteger(), sd[3].asInteger()));
        edit.removed  = sd[4].asString();
        edit.inserted = sd[5].asString();
        return true;
    }

    // An edit and the one made right after it folded into one that does
    // both, where the second carries on from the first: typed on where the
    // first's text ended, or erased back from where the first began or on
    // from there. A run of typing -- an edit a character -- is written as
    // the one edit it amounts to.
    bool fold(ALTextDocument::Edit& first, const ALTextDocument::Edit& next)
    {
        if (first.removed.empty() && next.removed.empty() && next.range.begin == next.range.end && next.range.begin == first.endAfter())
        {
            first.inserted += next.inserted;
            return true;
        }
        if (!first.inserted.empty() || !next.inserted.empty())
        {
            return false;
        }
        if (next.range.end == first.range.begin)
        {
            // Backspaced: what it took stood right before what the first took.
            first.range   = ALTextRange(next.range.begin, first.range.end);
            first.removed = next.removed + first.removed;
            return true;
        }
        if (next.range.begin == first.range.begin)
        {
            // Deleted forward: what it took stood right after what the first
            // took, and ends where both, laid from the start, reach.
            first.removed += next.removed;
            ALTextDocument::Edit reach;
            reach.range    = ALTextRange(first.range.begin, first.range.begin);
            reach.inserted = first.removed;
            first.range    = ALTextRange(first.range.begin, reach.endAfter());
            return true;
        }
        return false;
    }
}

ALTextUndo::ALTextUndo(ALTextDocument& document)
:   mDocument(document)
{
}

ALTextUndo::Kind ALTextUndo::kindOf(const ALTextDocument::Edit& edit)
{
    if (edit.removed.empty() && oneCluster(edit.inserted))
    {
        return Kind::Typing;
    }
    if (edit.inserted.empty() && oneCluster(edit.removed))
    {
        return Kind::Erasing;
    }
    return Kind::Other;
}

std::string_view ALTextUndo::keyOf(Kind kind)
{
    switch (kind)
    {
        case Kind::Typing:  return "typing";
        case Kind::Erasing: return "erasing";
        default:            return "step";
    }
}

bool ALTextUndo::carriesOn(const Step& last, const ALTextDocument::Edit& next)
{
    if (last.edits.empty())
    {
        return false;
    }
    const ALTextDocument::Edit& tail = last.edits.back();
    const Kind                  kind = kindOf(next);
    if (kind != kindOf(tail))
    {
        return false;
    }
    switch (kind)
    {
        case Kind::Typing:
            // Typed where the last character ended.
            return next.range.begin == tail.endAfter();
        case Kind::Erasing:
            // A backspace takes the character before the last one taken; a
            // delete takes the one that moved into its place.
            return next.range.end == tail.range.begin || next.range.begin == tail.range.begin;
        default:
            return false;
    }
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
    step.serial      = ++mNextSerial;

    // The key a run is joined by: the open group, or the kind of change
    // where it carries on the last step; anything else ends the run first.
    std::string_view key = "group";
    if (mGroupDepth == 0)
    {
        key = keyOf(kindOf(edit));
        if (mSteps.undone().empty() || !carriesOn(mSteps.undone().back(), edit))
        {
            mSteps.breakRun();
        }
    }
    // A group is one step however long it stays open; a run is one step
    // while its changes come within the window. The oldest step forgotten
    // past the depth takes the saved mark down with it -- or away, where
    // the saved text was what that step led from, since no stepping
    // back reaches it any more.
    if (mSteps.note(std::move(step), key, now, mGroupDepth > 0 ? 1e9 : mWindow, join))
    {
        ++mEra;
        if (mSavedInForce != NOWHERE)
        {
            mSavedInForce = mSavedInForce == 0 ? NOWHERE : mSavedInForce - 1;
        }
    }
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
    ++mEra;
}

void ALTextUndo::markSaved()
{
    mSavedInForce = mSteps.inForce();
    mSteps.breakRun();
}

LLSD ALTextUndo::asLLSD(size_t budget) const
{
    const auto stepAsLLSD = [](const Step& step, size_t& bytes) {
        LLSD out;
        out["label"]  = step.mLabel;
        out["before"] = posAsLLSD(step.caretBefore);
        out["after"]  = posAsLLSD(step.caretAfter);
        bytes += STEP_WRITTEN + step.mLabel.size();
        LLSD       edits = LLSD::emptyArray();
        const auto write = [&edits, &bytes](const ALTextDocument::Edit& edit) {
            bytes += EDIT_WRITTEN + edit.removed.size() + edit.inserted.size();
            edits.append(editAsLLSD(edit));
        };
        // Each run folded into the one edit it amounts to as it is written.
        std::optional<ALTextDocument::Edit> pending;
        for (const ALTextDocument::Edit& edit : step.edits)
        {
            if (pending && fold(*pending, edit))
            {
                continue;
            }
            if (pending)
            {
                write(*pending);
            }
            pending = edit;
        }
        if (pending)
        {
            write(*pending);
        }
        out["edits"] = edits;
        return out;
    };
    const std::vector<Step>& undone = mSteps.undone();
    const std::vector<Step>& redone = mSteps.redone();
    // The steps forward, all or none: part of them would lead nowhere the
    // text was.
    size_t used = 0;
    LLSD   ahead = LLSD::emptyArray();
    for (const Step& step : redone)
    {
        ahead.append(stepAsLLSD(step, used));
    }
    if (used > budget / 4)
    {
        ahead = LLSD::emptyArray();
        used  = 0;
    }
    // The steps back, newest first, as many as the budget holds.
    std::vector<LLSD> back;
    size_t            first = undone.size();
    while (first > 0)
    {
        size_t     bytes = 0;
        const LLSD step  = stepAsLLSD(undone[first - 1], bytes);
        if (used + bytes > budget && !back.empty())
        {
            break;
        }
        used += bytes;
        back.push_back(step);
        --first;
    }
    LLSD out;
    out["version"] = HISTORY_VERSION;
    out["undo"]    = LLSD::emptyArray();
    for (auto it = back.rbegin(); it != back.rend(); ++it)
    {
        out["undo"].append(*it);
    }
    out["redo"] = ahead;
    // Where the saved text stands, counted from the oldest step kept; a
    // mark among the steps let go of, or past the steps forward kept, is
    // nowhere.
    S32 saved = -1;
    if (mSavedInForce != NOWHERE && mSavedInForce >= first)
    {
        const size_t at = mSavedInForce - first;
        if (at <= back.size() + static_cast<size_t>(ahead.size()))
        {
            saved = static_cast<S32>(at);
        }
    }
    out["saved"] = saved;
    return out;
}

bool ALTextUndo::fromLLSD(const LLSD& sd)
{
    if (!sd.isMap() || sd["version"].asInteger() != HISTORY_VERSION || !sd["undo"].isArray() || !sd["redo"].isArray())
    {
        return false;
    }
    // Numbered afresh as they are read: a save point taken before this is
    // of another journal.
    U64        serial   = mNextSerial;
    const auto stepFrom = [&serial](const LLSD& one, std::vector<Step>& into) {
        Step step;
        step.serial      = ++serial;
        step.mLabel      = one["label"].asString();
        step.caretBefore = posFrom(one["before"]);
        step.caretAfter  = posFrom(one["after"]);
        for (LLSD::array_const_iterator it = one["edits"].beginArray(); it != one["edits"].endArray(); ++it)
        {
            ALTextDocument::Edit edit;
            if (!editFrom(*it, edit))
            {
                return false;
            }
            step.edits.push_back(std::move(edit));
        }
        into.push_back(std::move(step));
        return true;
    };
    std::vector<Step> undo;
    std::vector<Step> redo;
    for (LLSD::array_const_iterator it = sd["undo"].beginArray(); it != sd["undo"].endArray(); ++it)
    {
        if (!stepFrom(*it, undo))
        {
            return false;
        }
    }
    for (LLSD::array_const_iterator it = sd["redo"].beginArray(); it != sd["redo"].endArray(); ++it)
    {
        if (!stepFrom(*it, redo))
        {
            return false;
        }
    }
    // Every step tried first: back from the text as it stands, the newest
    // first, and forward from it, the next first. A history of another
    // text fails here rather than taking a text apart when it is stepped.
    {
        ALTextDocument back(mDocument.text());
        for (auto it = undo.rbegin(); it != undo.rend(); ++it)
        {
            if (!replay(back, *it, false))
            {
                return false;
            }
        }
        ALTextDocument ahead(mDocument.text());
        for (auto it = redo.rbegin(); it != redo.rend(); ++it)
        {
            if (!replay(ahead, *it, true))
            {
                return false;
            }
        }
    }
    const S32    saved   = sd["saved"].asInteger();
    const size_t steps   = undo.size() + redo.size();
    const size_t dropped = mSteps.restore(std::move(undo), std::move(redo));
    mNextSerial          = serial;
    ++mEra;
    mGroupDepth   = 0;
    mSavedInForce = saved >= 0 && static_cast<size_t>(saved) <= steps && static_cast<size_t>(saved) >= dropped ? static_cast<size_t>(saved) - dropped : NOWHERE;
    return true;
}

std::optional<std::string> ALTextUndo::savedText() const
{
    if (mSavedInForce == NOWHERE)
    {
        return std::nullopt;
    }
    // Stepped to on a copy, back or forward from the text as it stands.
    ALTextDocument           text(mDocument.text());
    const std::vector<Step>& undone = mSteps.undone();
    const std::vector<Step>& redone = mSteps.redone();
    if (mSavedInForce <= undone.size())
    {
        for (size_t i = undone.size(); i > mSavedInForce; --i)
        {
            if (!replay(text, undone[i - 1], false))
            {
                return std::nullopt;
            }
        }
        return text.text();
    }
    size_t forward = mSavedInForce - undone.size();
    for (auto it = redone.rbegin(); it != redone.rend() && forward > 0; ++it, --forward)
    {
        if (!replay(text, *it, true))
        {
            return std::nullopt;
        }
    }
    return forward == 0 ? std::optional<std::string>(text.text()) : std::nullopt;
}

void ALTextUndo::markNeverSaved()
{
    mSavedInForce = NOWHERE;
    mSteps.breakRun();
}

bool ALTextUndo::isPristine() const
{
    return mSavedInForce != NOWHERE && mSteps.inForce() == mSavedInForce;
}

ALTextUndo::SavePoint ALTextUndo::savePoint()
{
    mSteps.breakRun();
    SavePoint point;
    point.serial = mSteps.undone().empty() ? 0 : mSteps.undone().back().serial;
    point.era    = mEra;
    return point;
}

void ALTextUndo::markSaved(const SavePoint& point)
{
    if (point.serial == 0)
    {
        // The text before any step: reachable while the bottom of the
        // stack is still the one it was.
        mSavedInForce = point.era == mEra ? 0 : NOWHERE;
        return;
    }
    const std::vector<Step>& undone = mSteps.undone();
    for (size_t i = 0; i < undone.size(); ++i)
    {
        if (undone[i].serial == point.serial)
        {
            mSavedInForce = i + 1;
            return;
        }
    }
    // Among the steps forward, the next of which is the last.
    const std::vector<Step>& redone = mSteps.redone();
    for (size_t i = 0; i < redone.size(); ++i)
    {
        if (redone[i].serial == point.serial)
        {
            mSavedInForce = undone.size() + (redone.size() - i);
            return;
        }
    }
    mSavedInForce = NOWHERE;
}
