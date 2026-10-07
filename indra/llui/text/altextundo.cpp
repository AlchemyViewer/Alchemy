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

#include "llsdserialize.h"
#include "llstring.h"

#include <algorithm>
#include <sstream>
#include <utility>

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

    using Change = ALTextUndo::Step::Change;

    // Where a place stands in a text that begins at another: its byte,
    // walked to from a place at or before it whose byte is known -- `at`
    // and `offset`, moved on to it -- so that a batch's stretches, in
    // order, are found in one walk. False where the text has no such
    // place.
    bool walkTo(std::string_view text, ALTextPos& at, size_t& offset, const ALTextPos& to)
    {
        while (at.line < to.line)
        {
            const size_t brk = text.find('\n', offset);
            if (brk == std::string_view::npos)
            {
                return false;
            }
            offset = brk + 1;
            ++at.line;
            at.column = 0;
        }
        if (at.line != to.line || to.column < at.column)
        {
            return false;
        }
        const size_t along    = static_cast<size_t>(to.column - at.column);
        const size_t line_end = std::min(text.find('\n', offset), text.size());
        if (offset + along > line_end)
        {
            return false;
        }
        offset += along;
        at      = to;
        return true;
    }

    // An edit as a step keeps it: a batch by its stretches, each one's text
    // cut from the edit's, which run from the first stretch to the last;
    // any other whole. A batch whose stretches do not stand within those
    // texts where it says is kept whole, and so is one of a single stretch,
    // which replaceMany would put back as a plain edit.
    Change changeOf(const ALTextDocument::Edit& edit)
    {
        Change change;
        if (edit.parts.size() > 1)
        {
            ALTextPos was_at = edit.range.begin;
            ALTextPos is_at  = edit.range.begin;
            size_t    was    = 0;
            size_t    is     = 0;
            change.stretches.reserve(edit.parts.size());
            for (const ALTextDocument::Edit::Part& part : edit.parts)
            {
                if (!walkTo(edit.removed, was_at, was, part.before.begin) || !walkTo(edit.inserted, is_at, is, part.after.begin))
                {
                    break;
                }
                const size_t took_from = was;
                const size_t put_from  = is;
                if (!walkTo(edit.removed, was_at, was, part.before.end) || !walkTo(edit.inserted, is_at, is, part.after.end))
                {
                    break;
                }
                change.stretches.push_back({ edit.removed.substr(took_from, was - took_from), edit.inserted.substr(put_from, is - put_from) });
            }
            if (change.stretches.size() == edit.parts.size())
            {
                change.edit.range = edit.range;
                change.edit.parts = edit.parts;
                change.edit.keepEnd(edit.endAfter());
                return change;
            }
            change.stretches.clear();
        }
        change.edit = edit;
        return change;
    }

    // Whether the text a change takes away, forward or back, is what stands
    // there: for a batch, each stretch's.
    bool stands(const ALTextDocument& text, const Change& change, bool forward)
    {
        if (change.stretches.empty())
        {
            const ALTextRange range = forward ? change.edit.range : change.edit.rangeAfter();
            return within(text, range) && text.text(range) == (forward ? change.edit.removed : change.edit.inserted);
        }
        for (size_t i = 0; i < change.stretches.size(); ++i)
        {
            const ALTextRange& range = forward ? change.edit.parts[i].before : change.edit.parts[i].after;
            if (!within(text, range) || text.text(range) != (forward ? change.stretches[i].removed : change.stretches[i].inserted))
            {
                return false;
            }
        }
        return true;
    }

    // A change made over a text, forward or back: a batch's stretches all
    // at once, each where it stands in the text as it is, which tells
    // whoever listens one edit with its parts, as it did when it was made.
    void put(ALTextDocument& text, const Change& change, bool forward)
    {
        if (change.stretches.empty())
        {
            if (forward)
            {
                text.replace(change.edit.range, change.edit.inserted, change.edit.parts);
                return;
            }
            const ALTextDocument::Edit back = change.edit.inverse();
            text.replace(back.range, back.inserted, back.parts);
            return;
        }
        std::vector<std::pair<ALTextRange, std::string>> pieces;
        pieces.reserve(change.stretches.size());
        for (size_t i = 0; i < change.stretches.size(); ++i)
        {
            const ALTextDocument::Edit::Part& part    = change.edit.parts[i];
            const Change::Stretch&            stretch = change.stretches[i];
            pieces.emplace_back(forward ? part.before : part.after, forward ? stretch.inserted : stretch.removed);
        }
        text.replaceMany(std::move(pieces));
    }

    // One step's edits made over a text, forward or back, each checked
    // first: the text it takes away must be what stands there.
    template <typename Step>
    bool replay(ALTextDocument& text, const Step& step, bool forward)
    {
        const auto made = [&text, forward](const Change& change) {
            if (!stands(text, change, forward))
            {
                return false;
            }
            put(text, change, forward);
            return true;
        };
        if (forward)
        {
            for (const Change& change : step.edits)
            {
                if (!made(change))
                {
                    return false;
                }
            }
            return true;
        }
        for (auto it = step.edits.rbegin(); it != step.edits.rend(); ++it)
        {
            if (!made(*it))
            {
                return false;
            }
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
    // And each other selection a step keeps, before or after.
    constexpr size_t RANGE_WRITTEN = 20;

    // What a change weighs: a batch an edit a stretch, as it is written.
    size_t weighed(const Change& change)
    {
        if (change.stretches.empty())
        {
            return EDIT_WRITTEN + change.edit.removed.size() + change.edit.inserted.size();
        }
        size_t bytes = 0;
        for (const Change::Stretch& stretch : change.stretches)
        {
            bytes += EDIT_WRITTEN + stretch.removed.size() + stretch.inserted.size();
        }
        return bytes;
    }

    LLSD rangesAsLLSD(const std::vector<ALTextRange>& ranges)
    {
        LLSD out = LLSD::emptyArray();
        for (const ALTextRange& range : ranges)
        {
            out.append(LLSD::emptyArray().with(0, range.begin.line).with(1, range.begin.column).with(2, range.end.line).with(3, range.end.column));
        }
        return out;
    }

    std::vector<ALTextRange> rangesFrom(const LLSD& sd)
    {
        std::vector<ALTextRange> out;
        for (LLSD::array_const_iterator it = sd.beginArray(); it != sd.endArray(); ++it)
        {
            const LLSD& one = *it;
            out.emplace_back(ALTextPos(one[0].asInteger(), one[1].asInteger()), ALTextPos(one[2].asInteger(), one[3].asInteger()));
        }
        return out;
    }

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
        // A batch's stretches say where each of its parts is: nothing
        // folds into one, nor one into anything.
        if (!first.parts.empty() || !next.parts.empty())
        {
            return false;
        }
        if (first.removed.empty() && next.removed.empty() && next.range.begin == next.range.end && next.range.begin == first.endAfter())
        {
            // Both end where the second does.
            first.inserted += next.inserted;
            first.keepEnd(next.endAfter());
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
            first.keepEnd(first.range.begin);
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
            first.keepEnd(first.range.begin);
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
    // A batch kept by its stretches is no run's tail: keys typed at several
    // carets are joined by the typing scope (beginTyping) instead.
    if (last.edits.empty() || !last.edits.back().stretches.empty())
    {
        return false;
    }
    // The tail told by what it does rather than by how much: a run's edits
    // are folded into one as they join, so a run of typing ends in an edit
    // of many characters. A step that is not a run has another key, which
    // is what keeps the next edit from joining it.
    const ALTextDocument::Edit& tail = last.edits.back().edit;
    const Kind                  kind = kindOf(next);
    const Kind                  was  = tail.removed.empty() && !tail.inserted.empty() ? Kind::Typing
                                       : tail.inserted.empty() && !tail.removed.empty() ? Kind::Erasing
                                                                                        : Kind::Other;
    if (kind != was)
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
    // Each edit folded into the one before it where it carries straight on
    // from it -- a run of typing kept as the one edit it amounts to, and
    // undone and redone as one -- and kept after it otherwise.
    for (Change& change : next.edits)
    {
        if (!last.edits.empty() && fold(last.edits.back().edit, change.edit))
        {
            // A fold joins the texts, which weigh what they did apart.
            last.bytes += change.edit.removed.size() + change.edit.inserted.size();
            continue;
        }
        last.bytes += weighed(change);
        last.edits.push_back(std::move(change));
    }
    last.caretAfter  = next.caretAfter;
    last.anchorAfter = next.anchorAfter;
    last.bytes       = last.bytes - RANGE_WRITTEN * last.othersAfter.size() + RANGE_WRITTEN * next.othersAfter.size();
    last.othersAfter = std::move(next.othersAfter);
    last.written.clear();
}

void ALTextUndo::record(const ALTextDocument::Edit& edit, const ALTextPos& before, const ALTextPos& after, F64 now)
{
    record(edit, ALTextRange(before, before), after, now);
}

void ALTextUndo::settle(const ALTextRange& selection)
{
    if (!mSettling || mSteps.undone().empty())
    {
        return;
    }
    ++mRevision;
    Step& newest       = mSteps.newest();
    newest.anchorAfter = selection.begin;
    newest.caretAfter  = selection.end;
    newest.written.clear();
}

void ALTextUndo::settle(const ALTextRange& selection, std::vector<ALTextRange> others)
{
    if (!mSettling || mSteps.undone().empty())
    {
        return;
    }
    settle(selection);
    Step& newest       = mSteps.newest();
    // What the others weigh, with the step and with the steps back.
    const size_t was   = newest.bytes;
    newest.bytes       = newest.bytes - RANGE_WRITTEN * newest.othersAfter.size() + RANGE_WRITTEN * others.size();
    newest.othersAfter = std::move(others);
    mUndoneBytes       = mUndoneBytes - was + newest.bytes;
}

void ALTextUndo::label(std::string_view text)
{
    ++mRevision;
    // The newest may be named by it, and be written again.
    if (!mSteps.undone().empty())
    {
        mSteps.newest().written.clear();
    }
    mSteps.label(text);
}

void ALTextUndo::record(const ALTextDocument::Edit& edit, const ALTextRange& before_in, const ALTextPos& after, F64 now,
                        std::vector<ALTextRange> others_before)
{
    if (edit.nothing())
    {
        return;
    }
    ++mRevision;
    mSettling               = true;
    mResumeStep             = 0;
    const ALTextPos& before = before_in.end;
    // A change after an undo throws the redo steps away; the saved text,
    // if it was among them, can no longer be reached by stepping.
    if (mSavedInForce != NOWHERE && mSavedInForce > mSteps.inForce())
    {
        mSavedInForce = NOWHERE;
    }

    Step step;
    step.edits.push_back(changeOf(edit));
    step.caretBefore  = before;
    step.anchorBefore = before_in.begin;
    step.caretAfter   = after;
    step.anchorAfter  = after;
    step.othersBefore = std::move(others_before);
    step.serial       = ++mNextSerial;
    step.bytes        = STEP_WRITTEN + weighed(step.edits.front()) + RANGE_WRITTEN * step.othersBefore.size();
    step.typed        = mTypingDepth > 0 && !mSteps.inGroup();

    // The key a run is joined by: the kind of change, where it carries on
    // the last step; anything else ends the run first. A group is one step
    // however long it stays open, which the stack keeps; a run is one step
    // while its changes come within the window.
    std::string_view key = keyOf(kindOf(edit));
    if (step.typed)
    {
        // A key typed: its first edit carries on a run of keys typed where
        // the key was typed where the run left the caret, or its text
        // ended, with nothing selected; the rest of what the key does is
        // one with the first, at the same moment.
        key = keyOf(mTypingErases ? Kind::Erasing : Kind::Typing);
        if (!mTypingNoted)
        {
            mTypingNoted                    = true;
            const std::vector<Step>& undone = mSteps.undone();
            const bool               on     = !undone.empty() && undone.back().typed && mTypingAt.begin == mTypingAt.end &&
                                              (mTypingAt.end == undone.back().caretAfter ||
                                               (!undone.back().edits.empty() && mTypingAt.end == undone.back().edits.back().edit.endAfter()));
            if (!on)
            {
                mSteps.breakRun();
            }
        }
    }
    else if (!mSteps.inGroup() && (mSteps.undone().empty() || !carriesOn(mSteps.undone().back(), edit)))
    {
        mSteps.breakRun();
    }
    // A new step, or the newest joined: what it weighs now counted.
    const size_t steps  = mSteps.undone().size();
    const size_t newest = steps > 0 ? mSteps.undone().back().bytes : 0;
    mSteps.note(std::move(step), key, now, mWindow, join);
    mUndoneBytes += mSteps.undone().back().bytes - (mSteps.undone().size() > steps ? 0 : newest);
    forgetOverBudget();
}

void ALTextUndo::beginTyping(const ALTextRange& selection, bool erasing)
{
    if (mTypingDepth++ == 0)
    {
        mTypingAt     = selection;
        mTypingNoted  = false;
        mTypingErases = erasing;
    }
}

void ALTextUndo::endTyping()
{
    if (mTypingDepth > 0 && --mTypingDepth == 0)
    {
        mTypingNoted = false;
    }
}

void ALTextUndo::forgetOverBudget()
{
    // A group still filling its step is weighed once it closes.
    if (mSteps.inGroup() || mUndoneBytes <= BUDGET)
    {
        return;
    }
    for (size_t forgot = mSteps.forgetOverBudget(BUDGET, weigh); forgot > 0; --forgot)
    {
        forgotOldest();
    }
    mUndoneBytes = undoneBytes();
}

size_t ALTextUndo::undoneBytes() const
{
    size_t bytes = 0;
    for (const Step& step : mSteps.undone())
    {
        bytes += step.bytes;
    }
    return bytes;
}

void ALTextUndo::forgotOldest()
{
    // The oldest step forgotten past the budget takes the saved mark down
    // with it -- or away, where the saved text was what that step led from,
    // since no stepping back reaches it any more.
    ++mEra;
    ++mRevision;
    if (mSavedInForce != NOWHERE)
    {
        mSavedInForce = mSavedInForce == 0 ? NOWHERE : mSavedInForce - 1;
    }
}

void ALTextUndo::beginGroup()
{
    mSteps.beginGroup();
}

void ALTextUndo::endGroup()
{
    const bool open = mSteps.inGroup();
    mSteps.endGroup();
    if (open && !mSteps.inGroup())
    {
        mResumeStep = mSteps.closedWithStep() && !mSteps.undone().empty() ? mSteps.undone().back().serial : 0;
    }
    forgetOverBudget();
}

void ALTextUndo::resumeGroup(U64 step)
{
    if (step != 0 && step == mResumeStep && !mSteps.inGroup() && !mSteps.undone().empty() && mSteps.undone().back().serial == step)
    {
        mSteps.resumeGroup();
    }
    else
    {
        mSteps.beginGroup();
    }
}

void ALTextUndo::closeGroups()
{
    mSteps.closeGroups();
    mResumeStep = 0;
    forgetOverBudget();
}

std::optional<ALTextRange> ALTextUndo::undo(std::vector<ALTextRange>* others)
{
    mResumeStep = 0;
    std::optional<Step> step = mSteps.takeUndo();
    if (!step)
    {
        return std::nullopt;
    }
    mSettling = false;
    ++mRevision;
    mUndoneBytes -= step->bytes;
    for (auto it = step->edits.rbegin(); it != step->edits.rend(); ++it)
    {
        put(mDocument, *it, false);
    }
    const ALTextRange selection(step->anchorBefore, step->caretBefore);
    if (others)
    {
        *others = step->othersBefore;
    }
    mSteps.pushRedo(std::move(*step));
    return selection;
}

std::optional<ALTextRange> ALTextUndo::redo(std::vector<ALTextRange>* others)
{
    mResumeStep = 0;
    std::optional<Step> step = mSteps.takeRedo();
    if (!step)
    {
        return std::nullopt;
    }
    mSettling = false;
    ++mRevision;
    mUndoneBytes += step->bytes;
    for (const Change& change : step->edits)
    {
        put(mDocument, change, true);
    }
    const ALTextRange selection(step->anchorAfter, step->caretAfter);
    if (others)
    {
        *others = step->othersAfter;
    }
    mSteps.pushUndo(std::move(*step));
    return selection;
}

void ALTextUndo::clear()
{
    mResumeStep = 0;
    ++mRevision;
    mSteps.clear();
    mUndoneBytes  = 0;
    mSavedInForce = 0;
    mSettling     = false;
    ++mEra;
}

void ALTextUndo::markSaved()
{
    ++mRevision;
    mResumeStep = 0;
    mSavedInForce = mSteps.inForce();
    mSteps.breakRun();
}

namespace
{
    // A step as it is written: its carets, its name, and its edits, each
    // run folded into the one edit it amounts to. A batch is written as the
    // plain edits it amounts to, one a stretch, made from its last stretch
    // back so that each stands where it stood: no stretch before it has
    // moved anything yet.
    LLSD stepAsLLSD(const ALTextUndo::Step& step)
    {
        LLSD out;
        out["label"]  = step.mLabel;
        out["before"] = posAsLLSD(step.caretBefore);
        out["after"]  = posAsLLSD(step.caretAfter);
        // The anchors only where something was selected.
        if (step.anchorBefore != step.caretBefore)
        {
            out["anchor_before"] = posAsLLSD(step.anchorBefore);
        }
        if (step.anchorAfter != step.caretAfter)
        {
            out["anchor_after"] = posAsLLSD(step.anchorAfter);
        }
        // The other selections only where there were any.
        if (!step.othersBefore.empty())
        {
            out["others_before"] = rangesAsLLSD(step.othersBefore);
        }
        if (!step.othersAfter.empty())
        {
            out["others_after"] = rangesAsLLSD(step.othersAfter);
        }
        LLSD                                edits = LLSD::emptyArray();
        std::optional<ALTextDocument::Edit> pending;
        const auto                          add = [&edits, &pending](const ALTextDocument::Edit& edit) {
            if (pending && fold(*pending, edit))
            {
                return;
            }
            if (pending)
            {
                edits.append(editAsLLSD(*pending));
            }
            pending = edit;
        };
        for (const Change& change : step.edits)
        {
            if (change.stretches.empty())
            {
                add(change.edit);
                continue;
            }
            for (size_t i = change.stretches.size(); i-- > 0;)
            {
                ALTextDocument::Edit one;
                one.range    = change.edit.parts[i].before;
                one.removed  = change.stretches[i].removed;
                one.inserted = change.stretches[i].inserted;
                if (!one.nothing())
                {
                    add(one);
                }
            }
        }
        if (pending)
        {
            edits.append(editAsLLSD(*pending));
        }
        out["edits"] = edits;
        return out;
    }
}

ALTextUndo::Written ALTextUndo::writtenWithin(size_t budget) const
{
    const std::vector<Step>& undone = mSteps.undone();
    const std::vector<Step>& redone = mSteps.redone();
    Written                  out;
    // The steps forward, all or none: part of them would lead nowhere the
    // text was.
    size_t used = 0;
    for (const Step& step : redone)
    {
        used += weighWritten(step);
    }
    out.ahead = used <= budget / 4;
    if (!out.ahead)
    {
        used = 0;
    }
    // The steps back, newest first, as many as the budget holds, and the
    // newest whatever it weighs.
    out.first = undone.size();
    while (out.first > 0)
    {
        const size_t bytes = weighWritten(undone[out.first - 1]);
        if (used + bytes > budget && out.first < undone.size())
        {
            break;
        }
        used += bytes;
        --out.first;
    }
    // Where the saved text stands, counted from the oldest step kept; a
    // mark among the steps let go of, or past the steps forward kept, is
    // nowhere.
    if (mSavedInForce != NOWHERE && mSavedInForce >= out.first)
    {
        const size_t at = mSavedInForce - out.first;
        if (at <= undone.size() - out.first + (out.ahead ? redone.size() : 0))
        {
            out.saved = static_cast<S32>(at);
        }
    }
    return out;
}

LLSD ALTextUndo::asLLSD(size_t budget) const
{
    const Written            within = writtenWithin(budget);
    const std::vector<Step>& undone = mSteps.undone();
    LLSD                     out;
    out["version"] = HISTORY_VERSION;
    out["undo"]    = LLSD::emptyArray();
    for (size_t i = within.first; i < undone.size(); ++i)
    {
        out["undo"].append(stepAsLLSD(undone[i]));
    }
    out["redo"] = LLSD::emptyArray();
    if (within.ahead)
    {
        for (const Step& step : mSteps.redone())
        {
            out["redo"].append(stepAsLLSD(step));
        }
    }
    out["saved"] = within.saved;
    return out;
}

std::string ALTextUndo::asNotation(size_t budget) const
{
    // Each step as it was written the last time, or written now and kept;
    // the frame around them as LLSD notation writes a map and its arrays,
    // so that it reads back as asLLSD's.
    const auto written = [](const Step& step) -> const std::string& {
        if (step.written.empty())
        {
            std::ostringstream text;
            LLSDSerialize::toNotation(stepAsLLSD(step), text);
            step.written = text.str();
        }
        return step.written;
    };
    const Written            within = writtenWithin(budget);
    const std::vector<Step>& undone = mSteps.undone();
    std::string              out    = "{'version':i" + std::to_string(HISTORY_VERSION) + ",'undo':[";
    for (size_t i = within.first; i < undone.size(); ++i)
    {
        if (i > within.first)
        {
            out += ',';
        }
        out += written(undone[i]);
    }
    out += "],'redo':[";
    if (within.ahead)
    {
        bool first = true;
        for (const Step& step : mSteps.redone())
        {
            if (!first)
            {
                out += ',';
            }
            first = false;
            out += written(step);
        }
    }
    out += "],'saved':i" + std::to_string(within.saved) + "}";
    return out;
}

// static
std::optional<ALTextUndo::History> ALTextUndo::historyFrom(const LLSD& sd, std::string_view text)
{
    if (!sd.isMap() || sd["version"].asInteger() != HISTORY_VERSION || !sd["undo"].isArray() || !sd["redo"].isArray())
    {
        return std::nullopt;
    }
    const auto stepFrom = [](const LLSD& one, std::vector<Step>& into) {
        Step step;
        step.mLabel       = one["label"].asString();
        step.caretBefore  = posFrom(one["before"]);
        step.caretAfter   = posFrom(one["after"]);
        step.anchorBefore = one.has("anchor_before") ? posFrom(one["anchor_before"]) : step.caretBefore;
        step.anchorAfter  = one.has("anchor_after") ? posFrom(one["anchor_after"]) : step.caretAfter;
        step.othersBefore = rangesFrom(one["others_before"]);
        step.othersAfter  = rangesFrom(one["others_after"]);
        step.bytes        = STEP_WRITTEN + RANGE_WRITTEN * (step.othersBefore.size() + step.othersAfter.size());
        for (LLSD::array_const_iterator it = one["edits"].beginArray(); it != one["edits"].endArray(); ++it)
        {
            Change change;
            if (!editFrom(*it, change.edit))
            {
                return false;
            }
            step.bytes += weighed(change);
            step.edits.push_back(std::move(change));
        }
        into.push_back(std::move(step));
        return true;
    };
    History history;
    for (LLSD::array_const_iterator it = sd["undo"].beginArray(); it != sd["undo"].endArray(); ++it)
    {
        if (!stepFrom(*it, history.undo))
        {
            return std::nullopt;
        }
    }
    for (LLSD::array_const_iterator it = sd["redo"].beginArray(); it != sd["redo"].endArray(); ++it)
    {
        if (!stepFrom(*it, history.redo))
        {
            return std::nullopt;
        }
    }
    // Every step tried: back from the text, the newest first, and forward
    // from it, the next first. A history of another text fails here rather
    // than taking a text apart when it is stepped.
    {
        ALTextDocument back(text);
        for (auto it = history.undo.rbegin(); it != history.undo.rend(); ++it)
        {
            if (!replay(back, *it, false))
            {
                return std::nullopt;
            }
        }
        ALTextDocument ahead(text);
        for (auto it = history.redo.rbegin(); it != history.redo.rend(); ++it)
        {
            if (!replay(ahead, *it, true))
            {
                return std::nullopt;
            }
        }
    }
    history.saved = sd["saved"].asInteger();
    return history;
}

void ALTextUndo::restore(History history)
{
    mResumeStep = 0;
    // Numbered afresh: a save point taken before this is of another
    // journal.
    for (Step& step : history.undo)
    {
        step.serial = ++mNextSerial;
    }
    for (Step& step : history.redo)
    {
        step.serial = ++mNextSerial;
    }
    const S32    saved   = history.saved;
    const size_t steps   = history.undo.size() + history.redo.size();
    const size_t dropped = mSteps.restore(std::move(history.undo), std::move(history.redo));
    mUndoneBytes         = undoneBytes();
    ++mEra;
    ++mRevision;
    mSettling     = false;
    mSavedInForce = saved >= 0 && static_cast<size_t>(saved) <= steps && static_cast<size_t>(saved) >= dropped ? static_cast<size_t>(saved) - dropped : NOWHERE;
    forgetOverBudget();
}

bool ALTextUndo::fromLLSD(const LLSD& sd)
{
    std::optional<History> history = historyFrom(sd, mDocument.text());
    if (!history)
    {
        return false;
    }
    restore(std::move(*history));
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
    ++mRevision;
    mResumeStep = 0;
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
    mResumeStep = 0;
    SavePoint point;
    point.serial = mSteps.undone().empty() ? 0 : mSteps.undone().back().serial;
    point.era    = mEra;
    return point;
}

void ALTextUndo::markSaved(const SavePoint& point)
{
    ++mRevision;
    mResumeStep = 0;
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
