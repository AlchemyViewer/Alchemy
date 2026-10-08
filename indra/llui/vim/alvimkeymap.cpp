/**
 * @file alvimkeymap.cpp
 * @brief A vim mode over the text view.
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

#include "alvimkeymap.h"
#include "alvimexcommands.h"
#include "alvimtext.h"

#include "altextchars.h"

#include "alanchoredranges.h"
#include "albracketindex.h"
#include "alsaid.h"
#include "altextsearch.h"
#include "llclipboard.h"
#include "llstring.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>

using namespace ALVimText;

ALVimKeymap::ALVimKeymap() : mEx(std::make_unique<ALVimExCommands>(*this)) {}
ALVimKeymap::~ALVimKeymap() = default;

// The : commands' own (ALVimExCommands).
// static
void ALVimKeymap::source(Shared& shared, std::string_view text, const std::function<bool(const std::string& option)>& host,
                         std::vector<std::string>& errors)
{
    ALVimExCommands::source(shared, text, host, errors);
}

// static
bool ALVimKeymap::setViewOption(ALTextView& view, const std::string& option, std::string& shown, std::string& error)
{
    return ALVimExCommands::setViewOption(view, option, shown, error);
}

// --- keys as text ------------------------------------------------------------------

// The notation's own, which the mappings keep: one spelling of every key
// for what q records, @ plays and :map reads.
// static
std::string ALVimKeymap::encodeInputs(const std::vector<Input>& inputs)
{
    return ALVimMappings::written(inputs);
}

// static
std::vector<ALVimKeymap::Input> ALVimKeymap::decodeInputs(std::string_view text)
{
    return ALVimMappings::keysWritten(text);
}

// --- what the outside sees -------------------------------------------------------------

// static
std::string ALVimKeymap::said(const char* key, const std::string& english, const LLStringUtil::format_map_t& args)
{
    return args.empty() ? alSaid(key, english) : alSaid(key, english, args);
}

// Two counted nouns in one saying: the lines in their own form, then
// the substitutions or the matches in theirs with the lines' words put in.
// static
std::string ALVimKeymap::substitutionsSaid(S32 count, S32 lines)
{
    const std::string on = alSaidCount("VimOnLines", lines, "on 1 line", "on [COUNT] lines");
    return alSaidCount("VimSubstitutions", count, "1 substitution [ON_LINES]", "[COUNT] substitutions [ON_LINES]", { { "[ON_LINES]", on } });
}

// static
std::string ALVimKeymap::matchesSaid(S32 count, S32 lines)
{
    const std::string on = alSaidCount("VimOnLines", lines, "on 1 line", "on [COUNT] lines");
    return alSaidCount("VimMatches", count, "1 match [ON_LINES]", "[COUNT] matches [ON_LINES]", { { "[ON_LINES]", on } });
}

// static
std::string ALVimKeymap::yankedSaid(S32 lines, bool block, char name)
{
    if (!name)
    {
        return block ? alSaidCount("VimBlockYanked", lines, "block of 1 line yanked", "block of [COUNT] lines yanked")
                     : alSaidCount("VimLinesYanked", lines, "1 line yanked", "[COUNT] lines yanked");
    }
    const LLStringUtil::format_map_t into = { { "[REGISTER]", std::string(1, name) } };
    return block ? alSaidCount("VimBlockYankedInto", lines, "block of 1 line yanked into \"[REGISTER]", "block of [COUNT] lines yanked into \"[REGISTER]", into)
                 : alSaidCount("VimLinesYankedInto", lines, "1 line yanked into \"[REGISTER]", "[COUNT] lines yanked into \"[REGISTER]", into);
}

bool ALVimKeymap::inserting() const
{
    return mMode == Mode::Insert || mMode == Mode::Replace;
}

std::string ALVimKeymap::status() const
{
    // Asked for every frame the band is drawn: the words come from a
    // cache, and the map is only made where a word goes into them.
    const std::string recording = mRecording ? said("VimRecording", "recording @[REGISTER]", { { "[REGISTER]", std::string(1, mRecording) } }) + " " : std::string();
    // Keys held for a mapping, after the mode, as vim's showcmd has them.
    const std::string held = mTypeahead.empty() ? std::string() : " " + heldShown();
    switch (mMode)
    {
        case Mode::Insert:      return recording + said("VimModeInsert", "-- INSERT --") + held;
        case Mode::Replace:     return recording + said("VimModeReplace", "-- REPLACE --") + held;
        case Mode::Visual:      return recording + said("VimModeVisual", "-- VISUAL --") + held;
        case Mode::VisualLine:  return recording + said("VimModeVisualLine", "-- VISUAL LINE --") + held;
        case Mode::VisualBlock: return recording + said("VimModeVisualBlock", "-- VISUAL BLOCK --") + held;
        case Mode::Command:
        case Mode::Search:      return utf8Of(mCommandLine.kind) + mCommandLine.line;
        case Mode::Confirm:
        {
            // The question, saying how many lines the match runs over
            // where it runs over more than one, since the selection is
            // the only other sign of that.
            const bool                 have  = mEx->confirming.at < mEx->confirming.edits.size();
            const ALTextRange          match = have ? mEx->confirming.edits[mEx->confirming.at].first.normalised() : ALTextRange();
            const S32                  lines = match.end.line - match.begin.line + 1;
            LLStringUtil::format_map_t args;
            args["[WITH]"]  = have ? mEx->confirming.edits[mEx->confirming.at].second : std::string();
            args["[COUNT]"] = std::to_string(lines);
            return recording + (lines > 1 ? said("VimConfirmReplaceLines", "replace with [WITH] (over [COUNT] lines) (y/n/a/q/l)?", args)
                                          : said("VimConfirmReplace", "replace with [WITH] (y/n/a/q/l)?", args));
        }
        case Mode::Normal:
        default:
        {
            // What is pending, as vim's showcmd has it.
            std::string pending;
            if (mRegisterCount > 0)
            {
                pending += std::to_string(mRegisterCount);
            }
            if (mRegister)
            {
                pending += std::string("\"") + mRegister;
            }
            if (mOperatorCount > 0)
            {
                pending += std::to_string(mOperatorCount);
            }
            if (mOperator)
            {
                pending += shownKey(mOperator);
            }
            if (mCount > 0)
            {
                pending += std::to_string(mCount);
            }
            if (mPending)
            {
                pending += shownKey(mPending);
            }
            if (mObjectKind)
            {
                pending += utf8Of(mObjectKind);
            }
            if (!mTypeahead.empty())
            {
                pending += heldShown();
            }
            if (mOneCommand)
            {
                return recording + said("VimOneCommand", "-- (insert) --") + (pending.empty() ? "" : " " + pending);
            }
            return recording + pending;
        }
    }
}

void ALVimKeymap::mouseChanged(ALTextView& view)
{
    // What was said is said; a click is as good as a key at clearing it,
    // as vim's redraw is.
    if (!mMessage.empty())
    {
        mMessage.clear();
        mMessageError = false;
        bump();
    }
    // Insert mode stays insert mode wherever the click lands, the caret
    // off what was typed; a line being typed is not the mouse's.
    if (inserting())
    {
        typingLeft(view, true);
        return;
    }
    if (mMode == Mode::Command || mMode == Mode::Search || mMode == Mode::Confirm)
    {
        return;
    }
    const ALTextDocument& d = view.document();
    // A drag: from the character pressed to the character under the
    // pointer, both included, as vim's own mouse has it -- a line's end
    // its last character -- and drawn so; the same character at both
    // ends is still a click.
    const auto on_character = [&d](const ALTextPos& at) {
        const ALTextPos last = lastCharOf(d, at.line);
        return at.column > last.column ? last : at;
    };
    const ALTextPos from = on_character(d.clamp(view.dragFromCharacter()));
    const ALTextPos to   = on_character(d.clamp(view.dragToCharacter()));
    if (view.mouseDragging() && from != to)
    {
        mVisualAnchor = from;
        mVisualCaret  = to;
        if (mMode == Mode::Normal || mMode == Mode::VisualBlock)
        {
            setMode(view, Mode::Visual);
        }
        showVisual(view);
        mWantColumn = -1;
        clearPending();
        bump();
        return;
    }
    if (view.hasSelection() && !view.mouseDragging())
    {
        // A word or a line taken by a double or a triple click, or a key
        // the plain keymap took: charwise, the character under either end
        // included as the view's selection reaches past it.
        const ALTextRange sel = view.selection();
        if (sel.end > sel.begin)
        {
            mVisualAnchor = sel.begin;
            mVisualCaret  = d.prevCluster(sel.end);
        }
        else
        {
            mVisualAnchor = d.prevCluster(sel.begin);
            mVisualCaret  = sel.end;
        }
        // A block's lit columns put out as it goes (setMode).
        if (mMode == Mode::Normal || mMode == Mode::VisualBlock)
        {
            setMode(view, Mode::Visual);
        }
    }
    else
    {
        // A click: normal mode, the caret on the character it landed on,
        // the selection kept for gv.
        if (isVisual())
        {
            setMode(view, Mode::Normal);
        }
        // The character pressed, whichever half of it, rather than the
        // boundary nearest the press; past a line's end, its last.
        if (view.mouseDragging())
        {
            if (view.caret() != from || view.hasSelection())
            {
                view.setCaret(from);
            }
        }
        else if (const ALTextPos last = lastCharOf(d, view.caret().line); view.caret().column > last.column)
        {
            view.setCaret(last);
        }
    }
    mWantColumn = -1;
    clearPending();
    bump();
}

std::string ALVimKeymap::registerText(char name) const
{
    return fetch(name).text;
}

bool ALVimKeymap::typingLine(std::string& line, S32& caret) const
{
    if (mMode != Mode::Command && mMode != Mode::Search)
    {
        return false;
    }
    const std::string kind = utf8Of(mCommandLine.kind);
    line                   = kind + mCommandLine.line;
    caret                  = static_cast<S32>(kind.size() + llmin(mCommandLine.cursor, mCommandLine.line.size()));
    return true;
}

void ALVimKeymap::say(const std::string& message, bool error)
{
    mMessage      = message;
    mMessageError = error;
    mFailed       = mFailed || error;
    bump();
}

bool ALVimKeymap::handleKey(ALTextView& view, KEY key, MASK mask)
{
    return type(view, Input::keyOf(key, mask));
}

bool ALVimKeymap::handleChar(ALTextView& view, llwchar ch)
{
    return type(view, Input::character(ch));
}

// --- mappings ----------------------------------------------------------------------

namespace
{
    // How many mappings may stand for one another, each fed in place of
    // the last without a key of their own fed between, before the chain
    // is taken to be a loop: vim's maxmapdepth.
    constexpr S32 MAX_MAP_DEPTH = 1000;
    // How many keys mappings may feed for one key drained that no mapping
    // put there -- one typed, or one of a macro's: a mapping that feeds
    // itself a key at a time, each doing something and none failing,
    // never trips the depth, which a key fed puts back to none. A macro's
    // own keys, however many, are none of them.
    constexpr S32 MAX_DRAINED = 10000;
}

bool ALVimKeymap::type(ALTextView& view, const Input& input)
{
    // What a macro records is what was typed, before any mapping: played,
    // the keys map again as they did.
    const bool record = mRecording && !mReplaying && mPlaying == 0;
    if (record)
    {
        mRecorded.push_back(input);
    }
    const bool taken = typeThrough(view, input);
    // A key nobody took is not part of what was typed: its character
    // follows, and is. One insert mode left the view to type is.
    if (record && mRecording && !taken && !input.isChar && !mTypedByView && !mRecorded.empty())
    {
        mRecorded.pop_back();
    }
    return taken;
}

bool ALVimKeymap::typeThrough(ALTextView& view, const Input& input)
{
    // No mapping in sight, which is most keys: fed as they come.
    if (!mappable(input) || (mTypeahead.empty() && (mShared->mappings.empty() || !mShared->mappings.starts(mapMode(), input))))
    {
        return feed(view, input);
    }
    Held held;
    held.input = input;
    held.typed = true;
    mTypeahead.push_back(held);
    mTypeaheadSince.reset();
    std::optional<bool> taken;
    drain(view, mTypeahead, false, false, &taken);
    flushHeld(view);
    bump();
    return taken.value_or(true);
}

void ALVimKeymap::drain(ALTextView& view, std::deque<Held>& queue, bool final, bool stop_on_error, std::optional<bool>* taken)
{
    S32 depth = 0;
    S32 fed   = 0;
    while (!queue.empty())
    {
        // The keys at the front that may be mapped, in the mode as it is
        // now -- each key fed may change it -- and no more of them than the
        // longest mapping in it: a macro's keys, played, would otherwise be
        // copied whole for every one of them fed.
        const U8           mode = mapMode();
        const size_t       most = mode != 0 ? mShared->mappings.longest(mode) : 0;
        std::vector<Input> front;
        for (const Held& held : queue)
        {
            if (!held.remap || front.size() >= most)
            {
                break;
            }
            front.push_back(held.input);
        }
        ALVimMappings::Match match;
        if (mode != 0 && !front.empty())
        {
            match = mShared->mappings.match(mode, front, !final && front.size() == queue.size());
        }
        if (match.longer && !(match.full && match.full->nowait))
        {
            // Held for what is typed next, or the timeout.
            return;
        }
        if (match.full)
        {
            if (++depth > MAX_MAP_DEPTH)
            {
                queue.clear();
                say(said("VimRecursiveMapping", "E223: Recursive mapping"), true);
                return;
            }
            // What it stands for in place of its keys: not mapped again
            // where it was made with :noremap, nor its first key where it
            // starts with the keys themselves, as vim has it. Keys of its
            // own that no mapping put there begin the count of what
            // mappings feed afresh.
            const ALVimMappings::Mapping mapping = *match.full;
            const auto                   end_of  = queue.begin() + static_cast<std::ptrdiff_t>(mapping.from.size());
            if (std::any_of(queue.begin(), end_of, [](const Held& held) { return !held.mapped; }))
            {
                fed = 0;
            }
            queue.erase(queue.begin(), end_of);
            bool starts_with_itself = mapping.to.size() >= mapping.from.size();
            for (size_t i = 0; starts_with_itself && i < mapping.from.size(); ++i)
            {
                starts_with_itself = mapping.to[i].sameAs(mapping.from[i]);
            }
            for (size_t i = mapping.to.size(); i-- > 0;)
            {
                Held held;
                held.input  = mapping.to[i];
                held.remap  = !mapping.noremap && !(i == 0 && starts_with_itself);
                held.mapped = true;
                queue.push_front(held);
            }
            continue;
        }
        // No mapping: the first key as it is, and what follows it looked
        // at again. One no mapping put there begins the count afresh.
        depth = 0;
        if (!queue.front().mapped)
        {
            fed = 0;
        }
        else if (++fed > MAX_DRAINED)
        {
            queue.clear();
            say(said("VimRecursiveMapping", "E223: Recursive mapping"), true);
            return;
        }
        const Held first = queue.front();
        queue.pop_front();
        if (first.typed && queue.empty() && taken)
        {
            // The key just typed, alone: as it came, for the view to do
            // with it what it would have.
            *taken = feed(view, first.input);
            return;
        }
        feedMapped(view, first.input);
        if (mFailed || mMessageError)
        {
            if (stop_on_error)
            {
                queue.clear();
                return;
            }
            // The rest of a mapping goes where a key of it failed, as in
            // vim; what was typed stays, to be fed as it came.
            queue.erase(std::remove_if(queue.begin(), queue.end(), [](const Held& held) { return !held.typed; }), queue.end());
        }
    }
}

void ALVimKeymap::feedMapped(ALTextView& view, const Input& input)
{
    ++mMapped;
    const bool taken = feed(view, input);
    --mMapped;
    if (!taken && !input.isChar)
    {
        // What the view's own keymap would have done with it, had it been
        // typed rather than mapped.
        const ALEditorCommand command = view.keymap().lookup(input.key, input.mask);
        if (command != ALEditorCommand::None && view.perform(command) && !inserting())
        {
            mouseChanged(view);
        }
    }
}

bool ALVimKeymap::holds(const Input& input) const
{
    return input.isChar && mMode == Mode::Insert && !mLiteral && !mInsertRegister && (mReplaying || mPlaying > 0 || mMapped > 0);
}

void ALVimKeymap::flushHeld(ALTextView& view)
{
    if (!mHeldTyped.empty())
    {
        const std::string text = std::move(mHeldTyped);
        mHeldTyped.clear();
        // At every caret, as the keys it held would have gone in.
        view.typeText(text);
    }
}

U8 ALVimKeymap::mapMode() const
{
    switch (mMode)
    {
        case Mode::Insert:
        case Mode::Replace:
            // Ctrl-V's and Ctrl-R's character is taken as it is.
            return mLiteral || mInsertRegister ? 0 : ALVimMappings::INSERT;
        case Mode::Command:
        case Mode::Search:
            return ALVimMappings::COMMAND_LINE;
        case Mode::Confirm:
            return 0;
        default:
            break;
    }
    // A character a command waits for -- f's, r's, m's, a register's name,
    // the second of g's and z's, a text object's -- is taken as it is
    // typed, as vim has it; a mapping of two keys is found from the first.
    if (mPending || mObjectKind)
    {
        return 0;
    }
    if (mOperator)
    {
        return ALVimMappings::OPERATOR;
    }
    return isVisual() ? ALVimMappings::VISUAL : ALVimMappings::NORMAL;
}

// static
bool ALVimKeymap::mappable(const Input& input)
{
    if (input.isChar)
    {
        return true;
    }
    if (input.key == KEY_SHIFT || input.key == KEY_CONTROL || input.key == KEY_ALT || input.key == KEY_CAPSLOCK)
    {
        return false;
    }
#if LL_DARWIN
    if (input.mask & MASK_CONTROL)
    {
        return false;
    }
#endif
    // A key its character follows: the character is what is mapped.
    const bool printable = input.key >= 0x20 && input.key < KEY_SPECIAL;
    return !printable || (input.mask & (CONTROL | MASK_ALT));
}

std::string ALVimKeymap::heldShown() const
{
    std::vector<Input> keys;
    for (const Held& held : mTypeahead)
    {
        keys.push_back(held.input);
    }
    return ALVimMappings::shown(keys);
}

void ALVimKeymap::idle(ALTextView& view)
{
    if (mTypeahead.empty() || !mShared->timeout || mTypeaheadSince.getElapsedTimeF32() * 1000.f < static_cast<F32>(mShared->timeoutLength))
    {
        return;
    }
    // Waited long enough: the longest mapping the keys make whole, or the
    // first as it is and the rest looked at again.
    drain(view, mTypeahead, true, false, nullptr);
    flushHeld(view);
    bump();
}

// --- the dispatch ------------------------------------------------------------------

bool ALVimKeymap::feed(ALTextView& view, const Input& input)
{
    if (!holds(input))
    {
        flushHeld(view);
    }
    followDocument(view);
    mTypedByView = false;
    mVerticalMove = false;
    if (!mReplaying)
    {
        mMessage.clear();
        mMessageError = false;
        mFailed       = false;
        // A command starts where nothing is pending in normal mode; what
        // it is typed as is kept until it is done, for `.`.
        if (mMode == Mode::Normal && mCount == 0 && !mRegister && !mOperator && !mPending)
        {
            mCommandInputs.clear();
            mVisualPending.valid = false;
            // Each command typed is a step of its own to undo, however
            // close on the last it came; what a macro or :normal plays is
            // one step, the group play holds open.
            if (mPlaying == 0)
            {
                view.undoJournal().breakRun();
            }
        }
        mCommandInputs.push_back(input);
    }
    bool taken = false;
    switch (mMode)
    {
        case Mode::Insert:
        case Mode::Replace:
            taken = insert(view, input);
            break;
        case Mode::Command:
        case Mode::Search:
            taken = mCommandLine.commandLine(view, input);
            // What is typed so far lit; leaving the line puts it out
            // (setMode).
            if (mMode == Mode::Search)
            {
                mSearch.incrementalSearch(view);
            }
            break;
        case Mode::Confirm:
            taken = mEx->confirmKey(view, input);
            break;
        default:
            taken = normal(view, input);
            break;
    }
    // A key nobody took is not part of what was typed: its character
    // follows, and is. One insert mode left the view to type is.
    if (!taken && !input.isChar && !mTypedByView && !mReplaying && !mCommandInputs.empty())
    {
        mCommandInputs.pop_back();
    }
    // A command done that was no vertical move forgets the wanted
    // column; one still being typed -- a count, a register's name, an
    // operator -- keeps it, and so does a key nobody took, whose
    // character is to come.
    if (taken && (mMode == Mode::Normal || isVisual()) && !mVerticalMove && mCount == 0 && !mRegister && !mOperator && !mPending)
    {
        // A block taken to every line's end with $ no longer reaches
        // them, and is lit again as it is.
        const bool to_end = mWantColumn == S32_MAX;
        mWantColumn       = -1;
        if (to_end && mMode == Mode::VisualBlock)
        {
            showVisual(view);
        }
    }
    // Ctrl-O's one command done -- nothing pending, back in normal mode --
    // inserting again where it left off; not by the Ctrl-O itself.
    if (mOneCommand == 2)
    {
        mOneCommand = 1;
    }
    else if (mOneCommand == 1 && taken && mMode == Mode::Normal && mCount == 0 && !mOperator && !mPending && !mRegister)
    {
        setMode(view, Mode::Insert);
    }
    bump();
    return taken;
}

void ALVimKeymap::followDocument(ALTextView& view)
{
    const ALTextDocument* doc = &view.document();
    if (doc == mMarksIn)
    {
        return;
    }
    // Another text: the marks were the old one's.
    mMarksIn = doc;
    mMarks.clear();
    mMarksSlide = view.document().onChanged([this](const ALTextDocument::Edit& edit) { slideHeld(edit); });
}

void ALVimKeymap::slideHeld(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // A place past the edit moves with the text; one inside what was
    // taken out lands where that began.
    auto slide = [&edit](ALTextPos& pos) { pos = edit.placed(pos); };
    for (auto& [name, pos] : mMarks)
    {
        slide(pos);
    }
    slide(mVisualLastAnchor);
    slide(mVisualLastCaret);
    slide(mVisualAnchor);
    slide(mVisualCaret);
    // What is typed at where insert mode began is after it, not before.
    mInsertStart = edit.placed(mInsertStart, false);
    // A match an edit took some of is no match any longer.
    if (!mSearch.lastMatch.empty() && !edit.slide(mSearch.lastMatch))
    {
        mSearch.lastMatch = ALTextRange();
    }
    if (mBlockInsert)
    {
        mBlockFirst = edit.placed(ALTextPos(mBlockFirst, 0)).line;
        mBlockLast  = edit.placed(ALTextPos(mBlockLast, 0)).line;
    }
    // The edits still to ask about, those already asked about being done
    // with: moved along, or let go where the text they would replace was
    // changed under them.
    auto&  edits = mEx->confirming.edits;
    size_t kept  = mEx->confirming.at;
    for (size_t k = mEx->confirming.at; k < edits.size(); ++k)
    {
        ALTextRange range = edits[k].first.normalised();
        if (edit.slide(range))
        {
            edits[k].first = range;
            if (kept != k)
            {
                edits[kept] = std::move(edits[k]);
            }
            ++kept;
        }
    }
    edits.resize(kept);
}

ALBracketIndex& ALVimKeymap::bracketsOf(ALTextView& view) const
{
    if (ALVimHost* host = view.vimHost())
    {
        return host->bracketIndex();
    }
    // Summed up afresh for each question: a plain view's text is asked of
    // seldom, and a text kept from before may not be this one.
    if (!mPlainBrackets)
    {
        mPlainBrackets = std::make_unique<ALBracketIndex>();
    }
    mPlainBrackets->attach(&view.document());
    return *mPlainBrackets;
}

bool ALVimKeymap::matchBracketIn(ALTextView& view, const ALTextPos& from, ALTextPos& match) const
{
    // An angle bracket is no bracket to the index -- a comparison in code
    // -- and pairs by the text alone, as vim's % has it where asked.
    const char c = at(view.document(), from);
    if (c == '<' || c == '>')
    {
        return matchBracket(view.document(), from, match);
    }
    // Asked for: as far as it takes.
    return bracketsOf(view).match(from, match, ALBracketIndex::ANYWHERE);
}

bool ALVimKeymap::play(ALTextView& view, const std::vector<Input>& inputs, bool remap)
{
    if (mPlaying >= 100)
    {
        say(said("VimTooRecursive", "E169: Command too recursive"), true);
        return false;
    }
    // From the top, one step to undo, whatever it does: a group holds
    // what is played inside it as one.
    const bool top = mPlaying == 0;
    if (top)
    {
        view.undoJournal().beginGroup();
    }
    ++mPlaying;
    bool ok = true;
    if (remap && !mShared->mappings.empty())
    {
        // All the keys are there: a mapping they may be the start of does
        // not wait for more.
        std::deque<Held> queue;
        for (const Input& in : inputs)
        {
            Held held;
            held.input = in;
            queue.push_back(held);
        }
        drain(view, queue, true, true, nullptr);
        ok = !mMessageError && !mFailed;
    }
    else
    {
        for (const Input& in : inputs)
        {
            feed(view, in);
            if (mMessageError || mFailed)
            {
                ok = false;
                break;
            }
        }
    }
    flushHeld(view);
    --mPlaying;
    if (top)
    {
        view.undoJournal().endGroup();
    }
    return ok;
}

void ALVimKeymap::clearPending()
{
    mSurroundWaiting = false;
    mCount         = 0;
    mRegister      = 0;
    mRegisterCount = 0;
    mOperator      = 0;
    mOperatorCount = 0;
    mPending       = 0;
    mObjectKind    = 0;
}

void ALVimKeymap::takeRegisterCount()
{
    if (mRegisterCount > 0)
    {
        mCount         = countTimes(mRegisterCount, countOr(mCount));
        mRegisterCount = 0;
    }
}

void ALVimKeymap::finishCommand(bool changed)
{
    if (mSurroundWaiting)
    {
        // ys's stretch is taken: the command goes on to its character.
        return;
    }
    clearPending();
    if (changed && !mReplaying)
    {
        if (mVisualPending.valid)
        {
            // A visual operation: what `.` repeats is the operator and
            // whatever followed it, over as much again.
            mLastVisual = mVisualPending;
            mLastChange.assign(mCommandInputs.begin() + static_cast<std::ptrdiff_t>(llmin(mVisualPending.opAt, mCommandInputs.size())), mCommandInputs.end());
        }
        else
        {
            mLastVisual.valid = false;
            mLastChange       = mCommandInputs;
        }
    }
    // A visual operation that goes on to insert -- c -- is done when the
    // insert is: what `.` repeats is the operator with what was typed.
    if (!inserting())
    {
        mVisualPending.valid = false;
    }
}

void ALVimKeymap::noteVisualOperation(const ALTextDocument& d, const Span& span, S32 lines_hint)
{
    mVisualPending.valid   = true;
    mVisualPending.mode    = span.linewise ? Mode::VisualLine : span.block ? Mode::VisualBlock : Mode::Visual;
    mVisualPending.lines   = lines_hint >= 0 ? lines_hint : span.range.end.line - span.range.begin.line;
    mVisualPending.columns = span.block ? span.right - span.left : 0;
    if (mVisualPending.mode == Mode::Visual)
    {
        // Characters: from the selection's ends, whether or not the span
        // took the break after the last; an end past a line's last
        // character, on its break, one past the line's length.
        const ALTextPos caret = d.clamp(mVisualCaret);
        const ALTextPos a     = std::min(mVisualAnchor, caret);
        const ALTextPos b     = std::max(mVisualAnchor, caret);
        const S32       past  = atLineEnd(d, b) ? b.column + 1 : d.nextCluster(b).column;
        mVisualPending.lines   = lines_hint >= 0 ? lines_hint : b.line - a.line;
        mVisualPending.columns = past - (b.line == a.line ? a.column : 0);
    }
    // To the end again, whatever its length, where the caret went there
    // with $: a block's every line's, or the last line's of characters.
    mVisualPending.toEnd   = span.block ? span.toEnd : mVisualPending.mode == Mode::Visual && mWantColumn == S32_MAX;
    // The operator is the key being handled: the last one typed.
    mVisualPending.opAt = mCommandInputs.empty() ? 0 : mCommandInputs.size() - 1;
}

ALTextPos ALVimKeymap::cursor(const ALTextView& view) const
{
    // A search line opened over a selection searches from its caret too.
    const bool visual = isVisual() || (mMode == Mode::Search && mSearchVisual != Mode::Normal);
    return visual ? view.document().clamp(mVisualCaret) : view.caret();
}

void ALVimKeymap::moveTo(ALTextView& view, const ALTextPos& to)
{
    const ALTextDocument& d = view.document();
    ALTextPos             p = d.clamp(to);
    if (isVisual())
    {
        mVisualCaret = p;
        showVisual(view);
        return;
    }
    // Normal mode's caret sits on a character, never past the last.
    const ALTextPos last = lastCharOf(d, p.line);
    if (p.column > last.column)
    {
        p = last;
    }
    view.setCaret(p);
}

// --- normal mode -------------------------------------------------------------------------

bool ALVimKeymap::normal(ALTextView& view, const Input& input)
{
    if (!input.isChar)
    {
        // A command waiting for a character -- r, f, t, m and the like --
        // takes Return and Tab as the characters they are: r<CR> breaks
        // the line, where the motion Return is would have been its
        // character.
        if (mPending && (input.key == KEY_RETURN || input.key == KEY_TAB) && !(input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)))
        {
            return command(view, input.key == KEY_RETURN ? '\r' : '\t');
        }
        // Keys: the ones vim gives a meaning, and the rest left to the
        // view's own keymap.
        const bool ctrl = (input.mask & CONTROL) != 0;
        // Back in the jump list, or forward again, the count times: the
        // host's, which goes between its tabs as well.
        const auto jump = [&](bool back) {
            takeRegisterCount();
            const S32 times = countOr(mCount);
            clearPending();
            for (S32 n = 0; n < times && mHooks.command; ++n)
            {
                mHooks.command(view, back ? "back" : "forward", std::string());
            }
            return true;
        };
        // Control-W's command held with Control, as vim takes it too:
        // ^W^W is ^Ww.
        if (mPending == WINDOW_PREFIX && ctrl && !(input.mask & MASK_ALT) && input.key >= 'A' && input.key <= 'Z')
        {
            return command(view, static_cast<llwchar>(input.key - 'A' + 'a'));
        }
        switch (input.key)
        {
            case KEY_ESCAPE:
            {
                // Normal mode with nothing begun: the view's, which lets a
                // selection go, or passes it on where it says -- out of a
                // comparison, back to its source.
                const bool begun = mMode != Mode::Normal || mPending || mCount || mOperator || mRegister || mSurroundWaiting;
                if (mMode != Mode::Normal)
                {
                    leaveVisual(view);
                }
                clearPending();
                return begun;
            }
            case KEY_LEFT:      return command(view, 'h');
            case KEY_RIGHT:     return command(view, 'l');
            case KEY_UP:        return command(view, 'k');
            case KEY_DOWN:      return command(view, 'j');
            case KEY_HOME:      return command(view, '0');
            case KEY_END:       return command(view, '$');
            case KEY_RETURN:    return command(view, '+');
            case KEY_BACKSPACE: return command(view, 'h');
            case KEY_DELETE:    return command(view, 'x');
            case KEY_PAGE_DOWN: view.perform(ALEditorCommand::MovePageDown); moveTo(view, view.caret()); clearPending(); return true;
            case KEY_PAGE_UP:   view.perform(ALEditorCommand::MovePageUp); moveTo(view, view.caret()); clearPending(); return true;
            case KEY_TAB:
                // Tab is Control-I, forward in the jump list; it indents
                // nothing out of insert mode. Shift-Tab is nothing. Held
                // with Control it goes round the host's tabs.
                if ((input.mask & ~MASK_SHIFT) == MASK_NONE)
                {
                    if (input.mask == MASK_NONE)
                    {
                        return jump(false);
                    }
                    clearPending();
                    return true;
                }
                break;
            default:
                break;
        }
        if (ctrl && !(input.mask & MASK_ALT))
        {
            // A chord is a command's own key, and no digit of its count.
            takeRegisterCount();
            switch (input.key)
            {
                case 'O':
                case 'I':
                    return jump(input.key == 'O');
                // Vim's motions under Control, which would otherwise be
                // the host's keys -- a new script, its quick open, the
                // editor's join -- or the world's.
                case 'H':
                    return command(view, 'h');
                case 'J':
                case 'N':
                    return command(view, 'j');
                case 'P':
                    return command(view, 'k');
                case 'M':
                    return command(view, '+');
                case 'W':
                    // The window commands, the next key: the host's tabs
                    // stand for vim's windows.
                    clearPending();
                    mPending = WINDOW_PREFIX;
                    return true;
                case 'L':
                    // Redraw, which drawing does: nothing to do, and not the
                    // host's either.
                    clearPending();
                    return true;
                case ']':
                    clearPending();
                    mEx->runCommand(view, "tag");
                    return true;
                case 'T':
                    clearPending();
                    mEx->runCommand(view, "pop");
                    return true;
                case 'G':
                    clearPending();
                    mEx->runCommand(view, "file");
                    return true;
                case '6':
                case '^':
                {
                    // The alternate tab, or with a count the Nth: :buffer.
                    const S32 given = mCount;
                    clearPending();
                    mEx->runCommand(view, given > 0 ? "buffer " + std::to_string(given) : std::string("buffer #"));
                    return true;
                }
                case 'R':
                    for (S32 n = countOr(mCount); n > 0; --n)
                    {
                        view.perform(ALEditorCommand::Redo);
                    }
                    // Normal mode works at one caret, whatever the step
                    // brought back.
                    view.singleSelection();
                    moveTo(view, view.caret());
                    clearPending();
                    return true;
                case 'V':
                    enterVisual(view, Mode::VisualBlock);
                    clearPending();
                    return true;
                case 'D':
                case 'U':
                {
                    const S32 half = llmax(1, view.rowsPerPage() / 2) * countOr(mCount);
                    view.setScrollY(view.scrollY() + (input.key == 'D' ? half : -half) * view.layout().rowHeight());
                    const Motion m = motion(view, input.key == 'D' ? 'j' : 'k', half, 0);
                    moveTo(view, m.to);
                    clearPending();
                    return true;
                }
                case 'F':
                    view.perform(ALEditorCommand::MovePageDown);
                    moveTo(view, view.caret());
                    clearPending();
                    return true;
                case 'B':
                    view.perform(ALEditorCommand::MovePageUp);
                    moveTo(view, view.caret());
                    clearPending();
                    return true;
                case 'A':
                case 'X':
                    finishCommand(!view.isReadOnly() && addToNumber(view, (input.key == 'A' ? 1 : -1) * static_cast<S64>(countOr(mCount))));
                    return true;
                case 'E':
                    view.setScrollY(view.scrollY() + countOr(mCount) * view.layout().rowHeight());
                    clearPending();
                    return true;
                case 'Y':
                    view.setScrollY(view.scrollY() - countOr(mCount) * view.layout().rowHeight());
                    clearPending();
                    return true;
                case '[':
                    if (mMode != Mode::Normal)
                    {
                        leaveVisual(view);
                    }
                    clearPending();
                    return true;
                default:
                    break;
            }
#if LL_DARWIN
            // On the Mac vim's Control is the Control key, and the
            // window's keys are Command's: a letter vim has no use for is
            // nobody's -- the editor's own under it, ^K deleting to the
            // line's end, would edit the text. Elsewhere Control is the
            // window's too, and one vim leaves -- ^S saving, ^K before a
            // second key -- goes on to it.
            if (input.key >= 'A' && input.key <= 'Z')
            {
                clearPending();
                return true;
            }
#endif
        }
        return false;
    }
    return command(view, input.ch);
}

bool ALVimKeymap::command(ALTextView& view, llwchar ch)
{
    // Something waiting for this character: what that is says what this
    // one does (afterPending).
    if (mPending)
    {
        const llwchar pending = mPending;
        mPending              = 0;
        return afterPending(view, pending, ch);
    }

    // A count.
    if (isDigit(ch) && !(ch == '0' && mCount == 0))
    {
        mCount = countTyped(mCount, ch);
        return true;
    }
    if (ch == '"' && !mOperator)
    {
        mPending = '"';
        return true;
    }
    // The command's own key: a count before a register's name counts too.
    takeRegisterCount();

    // An operator, or its motion.
    if (mOperator)
    {
        return operatorKey(view, ch);
    }
    // Visual mode's own commands, then everyone's; else a motion.
    if (mMode != Mode::Normal)
    {
        if (const std::optional<bool> taken = visualKey(view, ch))
        {
            return *taken;
        }
    }
    if (const std::optional<bool> taken = normalKey(view, ch))
    {
        return *taken;
    }
    return motionKey(view, ch);
}

// The keys that wait for the one after them, and what each does with it;
// any other -- f, F, t, T, ` and ' -- a motion with its argument.
const ALVimKeymap::PendingKey ALVimKeymap::PENDING_KEYS[] = {
    { SURROUND_WITH, &ALVimKeymap::afterSurroundWith },
    { CHANGE_SURROUND, &ALVimKeymap::afterChangeSurround },
    { DELETE_SURROUND, &ALVimKeymap::afterSurroundPair },
    { CHANGE_SURROUND_TO, &ALVimKeymap::afterSurroundPair },
    { '"', &ALVimKeymap::afterRegisterName },
    { 'm', &ALVimKeymap::afterMark },
    { 'q', &ALVimKeymap::afterRecord },
    { '@', &ALVimKeymap::afterPlay },
    { 'r', &ALVimKeymap::afterReplace },
    { 'g', &ALVimKeymap::afterG },
    { 'z', &ALVimKeymap::afterZ },
    { PENDING_GR, &ALVimKeymap::afterGr },
    { '[', &ALVimKeymap::afterBracket },
    { ']', &ALVimKeymap::afterBracket },
    { 'Z', &ALVimKeymap::afterBigZ },
    { WINDOW_PREFIX, &ALVimKeymap::afterWindow },
    { 'i', &ALVimKeymap::afterObject },
    { 'a', &ALVimKeymap::afterObject },
};

bool ALVimKeymap::afterPending(ALTextView& view, llwchar pending, llwchar ch)
{
    for (const PendingKey& waiting : PENDING_KEYS)
    {
        if (waiting.key == pending)
        {
            return (this->*waiting.take)(view, pending, ch);
        }
    }
    return afterMotionKey(view, pending, ch);
}

bool ALVimKeymap::afterSurroundWith(ALTextView& view, llwchar pending, llwchar ch)
{
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();
    // ys's stretch, or visual S's, with the pair round it.
    std::string open;
    std::string close;
    mSurroundWaiting = false;
    if (!editing || !surroundPair(ch, open, close))
    {
        clearPending();
        return true;
    }
    surround(view, mSurroundSpan, open, close);
    finishCommand(true);
    return true;
}

bool ALVimKeymap::afterChangeSurround(ALTextView& view, llwchar pending, llwchar ch)
{
    mSurroundOld = ch;
    mPending     = CHANGE_SURROUND_TO;
    return true;
}

bool ALVimKeymap::afterSurroundPair(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            editing = !view.isReadOnly();
    const S32             count   = countOr(mCount);
    // ds's pair taken away, or cs's changed; the count before
    // the d or the c the pair that many out.
    const bool changed = editing && changeSurround(view, pending == DELETE_SURROUND ? ch : mSurroundOld,
                                                   pending == DELETE_SURROUND ? 0 : ch, countOr(mOperatorCount));
    if (!changed)
    {
        clearPending();
        return true;
    }
    finishCommand(true);
    return true;
}

bool ALVimKeymap::afterRegisterName(ALTextView& view, llwchar pending, llwchar ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '"' || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '+' ||
        ch == '*')
    {
        // A count typed before the name is kept, for the one typed after
        // it to multiply (takeRegisterCount).
        mRegister = static_cast<char>(ch);
        if (mCount > 0)
        {
            mRegisterCount = countTimes(countOr(mRegisterCount), mCount);
        }
        mCount = 0;
        return true;
    }
    clearPending();
    return true;
}

bool ALVimKeymap::afterMark(ALTextView& view, llwchar pending, llwchar ch)
{
    if (ch >= 'a' && ch <= 'z')
    {
        mMarks[static_cast<char>(ch)] = cursor(view);
    }
    clearPending();
    return true;
}

bool ALVimKeymap::afterRecord(ALTextView& view, llwchar pending, llwchar ch)
{
    // Recording into a register: a-z afresh, A-Z onto what
    // is there. q: q/ and q? are the line with its history
    // in it, the last line entered up, rather than vim's
    // window of them.
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
    {
        mRecording = static_cast<char>(ch);
        mRecorded.clear();
    }
    else if (ch == ':' || ch == '/' || ch == '?')
    {
        clearPending();
        const std::vector<std::string>& history = mCommandLine.historyOf(ch);
        if (mHooks.historyWindow && !history.empty())
        {
            // The host's window of them; what is picked comes
            // back onto the line.
            const llwchar kind = ch;
            // Picked later: the tab, or vim in it, may have gone
            // meanwhile, and both are looked for again.
            const LLHandle<LLUICtrl> handle = view.getHandle();
            mHooks.historyWindow(view, kind, history, [handle, kind](const std::string& line, bool run) {
                ALTextView*  again = ALViewType::as<ALTextView>(handle.get());
                ALVimKeymap* vim   = again ? dynamic_cast<ALVimKeymap*>(again->modalKeymap()) : nullptr;
                if (vim)
                {
                    vim->takeLine(*again, kind, line, run);
                }
            });
            return true;
        }
        // Over a visual selection, as : and / are typed over one: q: lets
        // it go, q/ and q? keep it for what they find to extend.
        if (ch == ':' && isVisual())
        {
            leaveVisual(view);
        }
        mCommandLine.kind = ch;
        mCommandLine.historyPrefix.clear();
        mCommandLine.historyAt  = history.empty() ? -1 : static_cast<S32>(history.size()) - 1;
        mCommandLine.line       = history.empty() ? std::string() : history.back();
        mCommandLine.cursor = mCommandLine.line.size();
        setMode(view, ch == ':' ? Mode::Command : Mode::Search);
        return true;
    }
    clearPending();
    return true;
}

bool ALVimKeymap::afterPlay(ALTextView& view, llwchar pending, llwchar ch)
{
    const S32             count   = countOr(mCount);
    // A register's keys played, the count times over; @@ the
    // last one again.
    char name = static_cast<char>(ch);
    if (name == '@')
    {
        name = mLastPlayed;
    }
    if (name == ':')
    {
        // The last : line again.
        clearPending();
        mLastPlayed = ':';
        const std::vector<std::string>& history = mCommandLine.historyOf(':');
        if (history.empty())
        {
            say(said("VimNoPreviousCommand", "E30: No previous command line"), true);
            return true;
        }
        view.undoJournal().beginGroup();
        for (S32 n = 0; n < count; ++n)
        {
            mEx->runCommand(view, history.back());
            if (mMessageError)
            {
                break;
            }
        }
        view.undoJournal().endGroup();
        return true;
    }
    if (!((name >= 'a' && name <= 'z') || (name >= 'A' && name <= 'Z') || name == '"' || name == '0'))
    {
        clearPending();
        return true;
    }
    const std::vector<Input> inputs = decodeInputs(fetch(static_cast<char>(std::tolower(name))).text);
    mLastPlayed                     = static_cast<char>(std::tolower(name));
    clearPending();
    // The count's plays one step to undo, and the first to fail
    // the last, as in vim.
    view.undoJournal().beginGroup();
    for (S32 n = 0; n < count; ++n)
    {
        if (!play(view, inputs, true))
        {
            break;
        }
    }
    view.undoJournal().endGroup();
    return true;
}

bool ALVimKeymap::afterReplace(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();
    const S32             count   = countOr(mCount);
    if (!editing)
    {
        clearPending();
        return true;
    }
    if (visual)
    {
        // Every character of the selection becomes this one: what a
        // block holds of each line, or what each line has of the rest.
        const Span               span = visualSpan(view);
        std::vector<ALTextRange> pieces;
        if (span.block)
        {
            pieces = blockPieces(view, span);
        }
        else
        {
            for (S32 line = span.range.begin.line; line <= span.range.end.line; ++line)
            {
                const S32 c0 = span.linewise ? 0 : line == span.range.begin.line ? span.range.begin.column : 0;
                const S32 c1 = span.linewise ? d.lineLength(line) : line == span.range.end.line ? span.range.end.column : d.lineLength(line);
                const S32 lo = llmin(c0, d.lineLength(line));
                const S32 hi = llmin(llmax(lo, c1), d.lineLength(line));
                pieces.emplace_back(ALTextPos(line, lo), ALTextPos(line, hi));
            }
        }
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (const ALTextRange& piece : pieces)
        {
            if (!piece.empty())
            {
                // One for each character, however many bytes it takes.
                std::string with;
                for (ALTextPos p = piece.begin; p < piece.end; p = d.nextCluster(p))
                {
                    with += utf8Of(ch);
                }
                edits.emplace_back(piece, with);
            }
        }
        const ALTextPos start = span.range.begin;
        leaveVisual(view);
        view.replaceAll(std::move(edits));
        moveTo(view, start);
        finishCommand(true);
        return true;
    }
    // The character under the caret, and the count after it,
    // replaced; a return breaks the line -- once, whatever the
    // count, as vim's does -- the new line indented as a
    // Return would indent it, the caret at its start.
    ALTextPos from = view.caret();
    ALTextPos to   = from;
    for (S32 n = 0; n < count; ++n)
    {
        if (atLineEnd(d, to))
        {
            clearPending();
            return true;
        }
        to = d.nextCluster(to);
    }
    view.setSelection(ALTextRange(from, to));
    if (ch == '\r' || ch == '\n')
    {
        view.perform(ALEditorCommand::NewLine);
        moveTo(view, view.caret());
        finishCommand(true);
        return true;
    }
    std::string with;
    for (S32 n = 0; n < count; ++n)
    {
        with += utf8Of(ch);
    }
    view.insertText(with);
    moveTo(view, d.prevCluster(view.caret()));
    finishCommand(true);
    return true;
}

bool ALVimKeymap::afterG(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();
    const S32             count   = countOr(mCount);
    // After an operator the g keys that are motions -- gg g_ ge gE gj gk
    // g0 g^ gm g$ gM go g* g# gn gN -- go on to take it, gv to give it the
    // last visual area, gr to wait for its character, and a g operator
    // doubled, g~g~ gugu gUgU gcgc, is its lines as g~~ is. Any other key
    // is no motion, and the operator fails as it does before any key that
    // is none (operatorKey): nothing changed and nothing begun, the caret
    // moved only where vim's gI and gi move it first, to the line's first
    // column and to where inserting last stopped.
    if (mOperator)
    {
        switch (ch)
        {
            case 'g':
            case '_':
            case 'e':
            case 'E':
            case 'j':
            case 'k':
            case '0':
            case '^':
            case 'm':
            case '$':
            case 'M':
            case 'o':
            case '*':
            case '#':
            case 'n':
            case 'N':
            case 'r':
            case 'v':
                break;
            default:
            {
                const bool doubled = (ch == 'c' && mOperator == COMMENT_OPERATOR) || ((ch == '~' || ch == 'u' || ch == 'U') && mOperator == ch);
                if (doubled)
                {
                    return operatorKey(view, mOperator);
                }
                const auto inserted = mMarks.find('^');
                if (ch == 'I')
                {
                    moveTo(view, d.lineStart(cursor(view).line));
                }
                else if (ch == 'i' && inserted != mMarks.end())
                {
                    moveTo(view, inserted->second);
                }
                mFailed = true;
                clearPending();
                return true;
            }
        }
    }
    switch (ch)
    {
        case 'g':
            return command(view, GO_TOP);
        case 'c':
            // Comments: an operator over lines, gcc a line, gc
            // a visual selection's lines.
            if (visual)
            {
                const Span span = visualSpan(view);
                noteVisualOperation(d, span);
                applyOperator(view, COMMENT_OPERATOR, span, 1);
                leaveVisual(view);
                finishCommand(true);
                return true;
            }
            if (!editing)
            {
                clearPending();
                return true;
            }
            mOperator      = COMMENT_OPERATOR;
            mOperatorCount = mCount;
            mCount         = 0;
            return true;
        case 'r':
            mPending = PENDING_GR;
            return true;
        case '_':
            return command(view, LAST_NON_BLANK);
        case ';':
        case ',':
        {
            // The change list: older, or newer again.
            clearPending();
            if (view.changes().empty())
            {
                say(said("VimNoChanges", "E664: Changelist is empty"), true);
            }
            else if (!view.goToChange(ch == ';' ? -count : count))
            {
                say(ch == ';' ? said("VimChangesStart", "E662: At start of changelist")
                              : said("VimChangesEnd", "E663: At end of changelist"),
                    true);
            }
            else
            {
                moveTo(view, view.caret());
            }
            return true;
        }
        case 'i':
        {
            // Inserting again where it last stopped. In a visual mode the
            // caret goes there and the i is a text object's, which names
            // none: vim's gi there extends the selection and fails.
            clearPending();
            const auto mark = mMarks.find('^');
            if (visual)
            {
                if (mark != mMarks.end())
                {
                    moveTo(view, mark->second);
                }
                mFailed = true;
                return true;
            }
            if (!editing)
            {
                return true;
            }
            if (mark != mMarks.end())
            {
                view.setCaret(d.clamp(mark->second));
            }
            enterInsert(view, count);
            return true;
        }
        case 'I':
            // Inserting in the line's first column, before its
            // indent, where I goes after it.
            clearPending();
            if (editing && !visual)
            {
                view.setCaret(ALTextPos(view.caret().line, 0));
                enterInsert(view, count);
            }
            return true;
        case '*':
        case '#':
            // As * and #, the word anywhere, not only whole.
            return starSearch(view, ch == '*', false);
        case 'p':
        case 'P':
            if (editing && !visual)
            {
                put(view, mRegister, ch == 'p', count, true);
                finishCommand(true);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'n':
        case 'N':
        {
            // The last search's match here or next: selected, or
            // what an operator works on -- cgn, and . for the
            // next.
            const std::optional<ALTextRange> match = mSearch.matchNear(view, ch == 'n');
            if (!match)
            {
                clearPending();
                return true;
            }
            if (mOperator)
            {
                Span span;
                span.range       = *match;
                span.visual      = true;
                const llwchar op = mOperator;
                applyOperator(view, op, span, 1);
                finishCommand(op != 'y');
                return true;
            }
            clearPending();
            if (!visual)
            {
                mVisualAnchor = match->begin;
                enterVisual(view, Mode::Visual);
            }
            mVisualAnchor = match->begin;
            mVisualCaret  = d.prevCluster(match->end);
            showVisual(view);
            return true;
        }
        case 'O':
            // The script's symbols, to go to one: the host's.
            clearPending();
            if (mHooks.command)
            {
                mHooks.command(view, "go_to_symbol", std::string());
            }
            return true;
        case 'f':
        {
            // The file named under the caret, as :find finds
            // one: an include's or a module's where the host has
            // one.
            clearPending();
            const std::string name = fileUnderCursor(view);
            if (name.empty())
            {
                say(said("VimNoFileUnderCursor", "E446: No file name under cursor"), true);
            }
            else
            {
                mEx->runCommand(view, "find " + name);
            }
            return true;
        }
        case 'd':
        case 'D':
            // The declaration: the host's definition, as :tag
            // goes to it.
            clearPending();
            mEx->runCommand(view, "tag");
            return true;
        case 't':
        case 'T':
        {
            // The host's tabs: gt the next, {N}gt the Nth; gT
            // back one, or N.
            const S32 given = mCount;
            clearPending();
            const std::string tabs = ch == 't' ? "tabnext" : "tabprevious";
            mEx->runCommand(view, given > 0 ? tabs + " " + std::to_string(given) : tabs);
            return true;
        }
        case '&':
            // The last :s again on every line, with its flags.
            clearPending();
            if (editing)
            {
                mEx->runCommand(view, "%s//~/&");
            }
            return true;
        case 'v':
            if (mOperator)
            {
                // {op}gv: the operator over the last visual area as it was
                // selected -- characters, lines or a block -- as vim's is;
                // with none, no motion. `.` does it again as a visual
                // operation is done again, over as much from the caret, by
                // visual mode's key for the operator -- c's with what its
                // insert typed.
                const llwchar op = mOperator;
                if (mVisualLast == Mode::Normal)
                {
                    mFailed = true;
                    clearPending();
                    return true;
                }
                mVisualAnchor = d.clamp(mVisualLastAnchor);
                mVisualCaret  = d.clamp(mVisualLastCaret);
                // To the end as the area was taken -- a block's every line's,
                // characters' last line's, for `.` -- not as the caret last
                // went.
                mWantColumn = mVisualLastToEnd ? S32_MAX : -1;
                setMode(view, mVisualLast);
                // A line's break taken as visual mode's operator takes it:
                // not by one over lines, nor by surround's.
                const bool over_lines = op == '>' || op == '<' || op == '=' || op == COMMENT_OPERATOR || op == SURROUND_OPERATOR;
                const Span span       = visualSpan(view, !over_lines);
                setMode(view, Mode::Normal);
                noteVisualOperation(d, span);
                mVisualPending.opAt = mCommandInputs.size();
                if (op == COMMENT_OPERATOR)
                {
                    mCommandInputs.push_back(Input::character('g'));
                    mCommandInputs.push_back(Input::character('c'));
                }
                else
                {
                    mCommandInputs.push_back(Input::character(op == SURROUND_OPERATOR ? static_cast<llwchar>('S') : op));
                }
                applyOperator(view, op, span, 1);
                finishCommand(op != 'y');
                return true;
            }
            if (mVisualLast != Mode::Normal)
            {
                mVisualAnchor = mVisualLastAnchor;
                mVisualCaret  = mVisualLastCaret;
                // An area taken with $ to the end again -- a block to every
                // line's -- kept past this command as $ keeps it.
                mWantColumn   = mVisualLastToEnd ? S32_MAX : -1;
                mVerticalMove = mVerticalMove || mVisualLastToEnd;
                setMode(view, mVisualLast);
                showVisual(view);
            }
            clearPending();
            return true;
        case 'J':
        {
            // Lines joined as they are.
            if (!editing)
            {
                clearPending();
                return true;
            }
            const S32 first = visual ? llmin(mVisualAnchor.line, cursor(view).line) : view.caret().line;
            const S32 last  = visual ? llmax(mVisualAnchor.line, cursor(view).line) : first;
            const S32 until = visual ? llmax(last, first + 1) : first + llmax(1, count - 1);
            if (visual)
            {
                leaveVisual(view);
            }
            if (const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(d, first, until, true))
            {
                view.apply(*join);
                moveTo(view, join->caret);
            }
            finishCommand(true);
            return true;
        }
        case '~':
        case 'u':
        case 'U':
            if (visual)
            {
                const Span span = visualSpan(view, true);
                noteVisualOperation(d, span);
                applyOperator(view, ch, span, 1);
                leaveVisual(view);
                finishCommand(true);
                return true;
            }
            mOperator      = ch;
            mOperatorCount = mCount;
            mCount         = 0;
            return true;
        case 'e':
        case 'E':
            // The end of the previous word: a motion the
            // table knows, so an operator takes it too.
            return command(view, ch == 'e' ? WORD_END_BACK : BIG_WORD_END_BACK);
        case 'j':
        case 'k':
            // A row of the display down or up, through a
            // wrapped line.
            return command(view, ch == 'j' ? DISPLAY_DOWN : DISPLAY_UP);
        case '0':
        case '^':
        case 'm':
        case '$':
            // The screen line's first character, its first that is
            // not blank, its middle and its last.
            return command(view, ch == '0' ? DISPLAY_START : ch == '^' ? DISPLAY_FIRST : ch == 'm' ? DISPLAY_MIDDLE : DISPLAY_END);
        case 'M':
            // The middle of the line's text.
            return command(view, LINE_MIDDLE);
        case 'o':
            // A byte of the text, by its count.
            return command(view, GO_BYTE);
        default:
            clearPending();
            return true;
    }
}

bool ALVimKeymap::afterZ(ALTextView& view, llwchar pending, llwchar ch)
{
    const bool            editing = !view.isReadOnly();
    const S32 line = cursor(view).line;
    const S32 row  = view.layout().rowHeight();
    const S32 top  = view.layout().lineTop(line);
    const S32 given = mCount;
    clearPending();
    if (ch == 't')
    {
        view.setScrollY(top);
    }
    else if (ch == 'z')
    {
        view.setScrollY(top - (view.rowsPerPage() / 2) * row);
    }
    else if (ch == 'b')
    {
        view.setScrollY(top - (view.rowsPerPage() - 1) * row);
    }
    else if (ch == '=' || ch == 'g')
    {
        // The misspelled word at the caret: put right, or taken
        // into the dictionary.
        if (ch == 'g')
        {
            view.refreshSuggestions();
            if (view.canAddToDictionary())
            {
                view.addToDictionary();
            }
            else
            {
                say(said("VimNotAddable", "E764: Cannot add this word"), true);
            }
        }
        else if (editing)
        {
            suggest(view, given);
        }
    }
    else
    {
        foldCommand(view, ch, 'z');
    }
    return true;
}

bool ALVimKeymap::afterGr(ALTextView& view, llwchar pending, llwchar ch)
{
    // grn rename, grr the references, gra the fixes and actions
    // at the caret: the editor's own. After an operator gr is no motion,
    // and the character after it goes with the operator it fails.
    const ALEditorCommand command = ch == 'n' ? ALEditorCommand::Rename
                                    : ch == 'r' ? ALEditorCommand::FindReferences
                                    : ch == 'a' ? ALEditorCommand::QuickFix
                                                : ALEditorCommand::None;
    const bool operated = mOperator != 0;
    clearPending();
    if (operated)
    {
        mFailed = true;
    }
    else if (command != ALEditorCommand::None)
    {
        view.perform(command);
    }
    return true;
}

namespace
{
    // An exclusive motion an operator takes to a line's first column, a
    // line or more on, as vim has it (:help exclusive): the stretch ends
    // at the end of the line before instead, and where nothing but blanks
    // come before its start, it is those lines whole. d} leaves the blank
    // line it goes to, and y} from a line's start yanks lines.
    void adjustExclusiveEnd(const ALTextDocument& d, ALTextRange& range, bool& linewise)
    {
        if (linewise || range.end.column != 0 || range.end.line <= range.begin.line)
        {
            return;
        }
        range.end = d.lineEnd(range.end.line - 1);
        linewise  = range.begin.column <= firstNonBlankColumn(d, range.begin.line);
    }

    // A word object as vim's current_word() finds one: from the start of
    // the word or the blanks the caret is on, the count of them on, across
    // line ends, where a line's break is no character of its own and an
    // empty line is blanks. iw counts a word or a run of blanks as one; aw
    // a word with the blanks after it, or blanks with the word after them,
    // and one that ends on no blank takes the blanks before it instead, but
    // not a line's indent. `end` is where vim's cursor is left: on the
    // object's last character, or, where `inclusive` is false, at the start
    // of the line after it. On an empty line aw runs on past its break to
    // the end of the next word, or to the next empty line; iw is the empty
    // line alone -- or, on the last line, goes back to the last character of
    // the line before, which puts `end` before `start`. None where the text
    // runs out first.
    //
    // A visual selection of more than one character is taken on instead, as
    // vim's current_word() takes one on, from its caret, `from`: forward by
    // the count's objects as those after a count's first go; or -- the caret
    // before the anchor -- back, iw over a word to its start or over blanks
    // to the first of them, aw over blanks and the word before them or over
    // a word and the blanks before it. `start` is then `from`, and `end`
    // where the caret goes.
    struct WordObject
    {
        ALTextPos start;
        ALTextPos end;
        bool      inclusive = true;
    };
    enum class WordsFrom
    {
        Under,
        Forward,
        Back
    };
    std::optional<WordObject> wordObject(const ALTextDocument& d, const ALTextPos& from, bool around, bool big, S32 count,
                                         WordsFrom words_from = WordsFrom::Under)
    {
        ALTextPos  p   = from;
        const auto cls = [&d, &p, big] { return classAt(d, p, big); };
        // A character on, as vim's inc() steps: 0 along the line, 2 onto its
        // end, 1 onto the next line's start; -1 at the end of the text, where
        // it stays.
        const auto inc = [&d, &p]() -> S32 {
            if (!atLineEnd(d, p))
            {
                p = d.nextCluster(p);
                return atLineEnd(d, p) ? 2 : 0;
            }
            if (p.line + 1 >= d.lineCount())
            {
                return -1;
            }
            p = ALTextPos(p.line + 1, 0);
            return 1;
        };
        // On, and past the end of a line that is not empty, as incl().
        const auto incl = [&p, &inc]() -> S32 {
            const S32 r = inc();
            return r >= 1 && p.column > 0 ? inc() : r;
        };
        // fwd_word() for one word under an operator: past the characters of
        // the caret's class and the blanks after them, no further than the
        // line's end -- or from an empty line onto the next one's start.
        // False where it began at the last line's end or its last character.
        const auto fwd_word = [&]() -> bool {
            const S32  run       = cls();
            const bool last_line = p.line + 1 >= d.lineCount();
            const S32  first     = inc();
            if (first == -1 || (first >= 1 && last_line))
            {
                return false;
            }
            if (first >= 1)
            {
                return true;
            }
            if (run != 0)
            {
                while (cls() == run)
                {
                    if (inc() != 0)
                    {
                        return true;
                    }
                }
            }
            while (cls() == 0)
            {
                if (inc() != 0)
                {
                    return true;
                }
            }
            return true;
        };
        // end_word() for one word: to the last character of the run the
        // caret is in; or, from blanks, past them -- across line ends, but
        // stopping on an empty line -- to the last of the run after them.
        // False where the text ends in the blanks.
        const auto end_word = [&]() -> bool {
            const S32 run = cls();
            if (inc() == -1)
            {
                return false;
            }
            if (run != 0 && cls() == run)
            {
                while (cls() == run)
                {
                    inc();
                }
            }
            else if (run == 0)
            {
                while (cls() == 0)
                {
                    if (p.column == 0 && d.lineLength(p.line) == 0)
                    {
                        return true;
                    }
                    if (inc() == -1)
                    {
                        return false;
                    }
                }
                const S32 next = cls();
                while (cls() == next)
                {
                    inc();
                }
            }
            p = d.prevCluster(p);
            return true;
        };
        // A character back, as vim's dec() steps: 0 along the line, 1 onto
        // the end of the line before; -1 at the start of the text, where it
        // stays.
        const auto dec = [&d, &p]() -> S32 {
            if (p.column > 0)
            {
                p = d.prevCluster(p);
                return 0;
            }
            if (p.line <= 0)
            {
                return -1;
            }
            p = d.lineEnd(p.line - 1);
            return 1;
        };
        // Back, and back past the end of a line that is not empty, as decl().
        const auto decl = [&p, &dec]() -> S32 {
            const S32 r = dec();
            return r == 1 && p.column > 0 ? dec() : r;
        };
        // bck_word() for one word, a caret at a word's start already staying
        // there: back over blanks, stopping on an empty line, and to the start
        // of the word before them. False at the start of the text.
        const auto bck_word = [&]() -> bool {
            const S32 run = cls();
            if (dec() == -1)
            {
                return false;
            }
            if (run == cls() || run == 0)
            {
                while (cls() == 0)
                {
                    if (p.column == 0 && d.lineLength(p.line) == 0)
                    {
                        return true;
                    }
                    if (dec() == -1)
                    {
                        return true;
                    }
                }
                const S32 word = cls();
                while (cls() == word)
                {
                    if (dec() == -1)
                    {
                        return true;
                    }
                }
            }
            inc();
            return true;
        };
        // bckend_word() for one word, stopping at a line's start: back out of
        // the run the caret is in and over the blanks before it, stopping on
        // an empty line, onto the last character of the word before -- no
        // further than the end of the line before. False at the text's start.
        const auto bckend_word = [&]() -> bool {
            const S32 run = cls();
            S32       r   = dec();
            if (r != 0)
            {
                return r == 1;
            }
            if (run != 0)
            {
                while (cls() == run)
                {
                    r = dec();
                    if (r != 0)
                    {
                        return true;
                    }
                }
            }
            while (cls() == 0)
            {
                if (p.column == 0 && d.lineLength(p.line) == 0)
                {
                    break;
                }
                r = dec();
                if (r != 0)
                {
                    return true;
                }
            }
            return true;
        };

        if (words_from == WordsFrom::Back)
        {
            // Back from the character before the caret, the count of them:
            // iw's word to its start, or its blanks to the first of them; aw's
            // blanks and the word before, or its word and the blanks before.
            for (S32 n = 0; n < count; ++n)
            {
                if (decl() == -1)
                {
                    return std::nullopt;
                }
                if (around != (cls() != 0))
                {
                    if (!bck_word())
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    if (!bckend_word())
                    {
                        return std::nullopt;
                    }
                    incl();
                }
            }
            return WordObject{ from, p, true };
        }

        ALTextPos start         = p;
        bool      inclusive     = true;
        bool      include_white = false;
        S32       n             = 0;
        if (words_from == WordsFrom::Under)
        {
            // Back to the start of the word or the blanks under the caret.
            const S32 under = cls();
            while (p.column > 0 && classAt(d, d.prevCluster(p), big) == under)
            {
                p = d.prevCluster(p);
            }
            start = p;
            // The first of the count: iw's word, or aw's blanks and the word
            // after them, to the word's last character; else past the word
            // and its blanks, or iw's blanks, to the character before what
            // comes next -- the last of the line before, from a line's start.
            if ((under == 0) == around)
            {
                if (!end_word())
                {
                    return std::nullopt;
                }
            }
            else
            {
                fwd_word();
                if (p.column > 0)
                {
                    p = d.prevCluster(p);
                }
                else if (p.line > 0)
                {
                    p = lastCharOf(d, p.line - 1);
                }
                include_white = around;
            }
            n = 1;
        }
        // The rest, each from the character after where the last ended, the
        // same two ways -- every one of them, for a selection taken on; one
        // that stops at a line's start, from an empty line, ends there and
        // not on a character.
        for (; n < count; ++n)
        {
            inclusive = true;
            if (incl() == -1)
            {
                return std::nullopt;
            }
            if (around != (cls() == 0))
            {
                if (!fwd_word() && n + 1 < count)
                {
                    return std::nullopt;
                }
                if (p.column > 0)
                {
                    p = d.prevCluster(p);
                }
                else
                {
                    inclusive = false;
                }
            }
            else if (!end_word())
            {
                return std::nullopt;
            }
        }
        // An aw that ends on no blank: the blanks before its start instead,
        // where they are not the line's indent.
        if (include_white && (cls() != 0 || (p.column == 0 && !inclusive)) && start.column > 0)
        {
            ALTextPos before       = d.prevCluster(start);
            const S32 before_class = classAt(d, before, big);
            while (before.column > 0 && classAt(d, d.prevCluster(before), big) == before_class)
            {
                before = d.prevCluster(before);
            }
            if (before_class == 0 && before.column > 0)
            {
                start = before;
            }
        }
        return WordObject{ start, p, inclusive };
    }

    // vim's startPS() for a paragraph: a line that begins one with no blank
    // line before it -- a formfeed at its start, or after a dot one of the
    // nroff macros of the default 'paragraphs' and 'sections', a blank in
    // them matching a blank or the line's end.
    bool startsParagraph(const ALTextDocument& d, S32 line)
    {
        static constexpr std::string_view MACROS("IPLPPPQPP TPHPLIPpLpItpplpipbpSHNHH HUnhsh");
        const std::string&                text = d.line(line);
        if (!text.empty() && text[0] == '\f')
        {
            return true;
        }
        if (text.empty() || text[0] != '.')
        {
            return false;
        }
        const char first  = text.size() > 1 ? text[1] : '\0';
        const char second = text.size() > 2 ? text[2] : '\0';
        for (size_t i = 0; i + 1 < MACROS.size(); i += 2)
        {
            if ((MACROS[i] == first || (MACROS[i] == ' ' && (first == '\0' || first == ' '))) &&
                (MACROS[i + 1] == second || (MACROS[i + 1] == ' ' && (first == '\0' || second == '\0' || second == ' '))))
            {
                return true;
            }
        }
        return false;
    }

    // vim's current_par() over a line, nothing selected: back to the start of
    // the paragraph or of the blank lines the line is in, then on by the
    // count's -- ip counting a paragraph or a run of blank lines as one, the
    // run the line is in among them; ap a paragraph with the blank lines after
    // it, or blank lines with the paragraph after them. An ap that ends on no
    // blank line takes the blank lines before it instead. A paragraph ends
    // where another begins with no blank line between (startsParagraph). The
    // first and the last lines; false where the count runs past the text's
    // end.
    bool paragraphObject(const ALTextDocument& d, S32 line, bool around, S32 count, S32& first, S32& last)
    {
        const S32  lines          = d.lineCount();
        const bool white_in_front = lineBlank(d, line);
        S32        start          = line;
        while (start > 0 && lineBlank(d, start - 1) == white_in_front && (white_in_front || !startsParagraph(d, start)))
        {
            --start;
        }
        S32 end = start;
        while (end < lines && lineBlank(d, end))
        {
            ++end;
        }
        --end;
        S32 i = !around && white_in_front ? count - 1 : count;
        while (i-- > 0)
        {
            if (end == lines - 1)
            {
                return false;
            }
            const bool do_white = !around && lineBlank(d, end + 1);
            if (around || !do_white)
            {
                ++end;
                while (end < lines - 1 && !lineBlank(d, end + 1) && !startsParagraph(d, end + 1))
                {
                    ++end;
                }
            }
            if (i == 0 && white_in_front && around)
            {
                break;
            }
            if (around || do_white)
            {
                while (end < lines - 1 && lineBlank(d, end + 1))
                {
                    ++end;
                }
            }
        }
        if (!white_in_front && !lineBlank(d, end) && around)
        {
            while (start > 0 && lineBlank(d, start - 1))
            {
                --start;
            }
        }
        first = start;
        last  = end;
        return true;
    }

    // The opening bracket a bracket object names: ( ) b, [ ], { } B or < >.
    char objectOpener(llwchar what)
    {
        switch (what)
        {
            case '(':
            case ')':
            case 'b':
                return '(';
            case '[':
            case ']':
                return '[';
            case '{':
            case '}':
            case 'B':
                return '{';
            case '<':
            case '>':
                return '<';
            default:
                return '\0';
        }
    }

    // A position stepped as vim's inc() steps one: a character on, 0 along
    // the line, 2 onto its end, 1 onto the next line's start; -1 at the end
    // of the text, where it stays. incl() goes past the end of a line that is
    // not empty as well.
    S32 incPos(const ALTextDocument& d, ALTextPos& p)
    {
        if (!atLineEnd(d, p))
        {
            p = d.nextCluster(p);
            return atLineEnd(d, p) ? 2 : 0;
        }
        if (p.line + 1 >= d.lineCount())
        {
            return -1;
        }
        p = ALTextPos(p.line + 1, 0);
        return 1;
    }
    S32 inclPos(const ALTextDocument& d, ALTextPos& p)
    {
        const S32 r = incPos(d, p);
        return r >= 1 && p.column > 0 ? incPos(d, p) : r;
    }
    // And back, as dec() and decl(): 0 along the line, 1 onto the end of the
    // line before -- for decl(), onto its last character, where it has one;
    // -1 at the start of the text.
    S32 decPos(const ALTextDocument& d, ALTextPos& p)
    {
        if (p.column > 0)
        {
            p = d.prevCluster(p);
            return 0;
        }
        if (p.line <= 0)
        {
            return -1;
        }
        p = d.lineEnd(p.line - 1);
        return 1;
    }
    S32 declPos(const ALTextDocument& d, ALTextPos& p)
    {
        const S32 r = decPos(d, p);
        return r == 1 && p.column > 0 ? decPos(d, p) : r;
    }
    // vim's inindent(): whether a place is in its line's indent, `extra`
    // columns on from it as well.
    bool inIndent(const ALTextDocument& d, const ALTextPos& p, S32 extra)
    {
        return firstNonBlankColumn(d, p.line) >= p.column + extra;
    }

    // The opener of a kind left open before a place, the place itself not
    // counted, as vim's findmatch() looks back for one: through the view's
    // bracket index where there is one, as % goes, else -- an angle bracket
    // -- by the text alone, nesting counted. False where there is none.
    bool openerBefore(const ALTextDocument& d, ALBracketIndex* index, char opener, const ALTextPos& from, ALTextPos& open)
    {
        if (index)
        {
            return index->enclosing(from, opener, 1, open, ALBracketIndex::ANYWHERE);
        }
        const char closer = partnerOf(opener);
        S32        depth  = 0;
        open              = from;
        while (stepBack(d, open))
        {
            const char c = at(d, open);
            if (c == closer)
            {
                ++depth;
            }
            else if (c == opener)
            {
                if (depth == 0)
                {
                    return true;
                }
                --depth;
            }
        }
        return false;
    }
    // And the first after it that no closer before it leaves unmatched, as
    // findmatch() looks forward for one, across lines: in x ) (a) there is
    // none.
    bool openerAfter(const ALTextDocument& d, ALBracketIndex* index, char opener, const ALTextPos& from, ALTextPos& open)
    {
        const char closer = partnerOf(opener);
        S32        depth  = 0;
        const auto take   = [&depth, &open, opener, closer](char c, const ALTextPos& p) {
            if (c == closer)
            {
                ++depth;
            }
            else if (c == opener)
            {
                if (depth == 0)
                {
                    open = p;
                    return true;
                }
                --depth;
            }
            return false;
        };
        if (index)
        {
            for (S32 line = from.line; line < d.lineCount(); ++line)
            {
                for (const auto& [column, c] : index->bracketsOn(line))
                {
                    if ((line > from.line || column > from.column) && take(c, ALTextPos(line, column)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }
        ALTextPos p = from;
        while (stepOn(d, p))
        {
            if (take(at(d, p), p))
            {
                return true;
            }
        }
        return false;
    }

    // vim's current_block(): the block of a kind a bracket object takes, as
    // its two ends -- the opener and its closer, or, inside, the first and
    // the last characters between them. From the caret, past the opener
    // under it -- and for { past an indent before one: the opener left open
    // before it, the count of them out; or, where none is, the count's
    // opener after it. A count past the blocks there are is none. Inside
    // starts the next line where the opener ends its line, and ends on the
    // line before where only an indent comes before the closer, `sol` -- the
    // line's break the block's as well; where the brackets hold nothing,
    // `end` is before `start`. A selection of more than one character,
    // `anchor` to `caret`, is looked from its start; and over one of any size,
    // `visual`, where what the block holds is no more than the selection does,
    // the next block out is taken instead, and brackets that hold nothing are
    // none.
    struct BlockObject
    {
        ALTextPos start;
        ALTextPos end;
        bool      sol = false;
    };
    std::optional<BlockObject> blockObject(const ALTextDocument& d, ALBracketIndex* index, char opener, bool around, S32 count, const ALTextPos& anchor,
                                           const ALTextPos& caret, bool visual)
    {
        const auto match = [&d, index](const ALTextPos& open, ALTextPos& close) {
            return index ? index->match(open, close, ALBracketIndex::ANYWHERE) : matchBracket(d, open, close);
        };
        ALTextPos from      = caret;
        ALTextPos old_start = caret;
        ALTextPos old_end   = caret;
        if (anchor == caret)
        {
            if (opener == '{')
            {
                while (inIndent(d, from, 1) && incPos(d, from) == 0)
                {
                }
            }
            if (at(d, from) == opener)
            {
                from = d.nextCluster(from);
            }
        }
        else if (anchor < caret)
        {
            old_start = anchor;
            from      = anchor;
        }
        else
        {
            old_end = anchor;
        }
        ALTextPos  open;
        const bool back = openerBefore(d, index, opener, from, open);
        if (!back && !openerAfter(d, index, opener, from, open))
        {
            return std::nullopt;
        }
        for (S32 n = 1; n < count; ++n)
        {
            ALTextPos next;
            if (!(back ? openerBefore(d, index, opener, open, next) : openerAfter(d, index, opener, open, next)))
            {
                return std::nullopt;
            }
            open = next;
        }
        BlockObject block;
        block.start = open;
        if (!match(open, block.end))
        {
            return std::nullopt;
        }
        while (!around)
        {
            const ALTextPos closer = block.end;
            inclPos(d, block.start);
            block.sol = block.end.column == 0;
            declPos(d, block.end);
            while (inIndent(d, block.end, 1))
            {
                block.sol = true;
                if (declPos(d, block.end) != 0)
                {
                    break;
                }
            }
            // Brackets that hold nothing are nothing to select.
            if (visual && block.start == closer)
            {
                return std::nullopt;
            }
            if (!visual || block.start < old_start || old_end < block.end || block.start == block.end)
            {
                break;
            }
            ALTextPos before = old_start;
            declPos(d, before);
            if (!openerBefore(d, index, opener, before, block.start) || !match(block.start, block.end))
            {
                return std::nullopt;
            }
        }
        return block;
    }

    // --- sentences, as vim's findsent() and current_sent() have them --------------------

    // A blank of a sentence's: a space or a tab, never a line's end.
    bool sentenceBlank(const ALTextDocument& d, const ALTextPos& p)
    {
        const char c = at(d, p);
        return c == ' ' || c == '\t';
    }
    // A character of a set, which a line's end is not.
    bool oneOf(std::string_view set, char c)
    {
        return c != '\0' && set.find(c) != std::string_view::npos;
    }

    // vim's findsent(): the start of the count's sentence on from a place,
    // or back. A sentence ends at a . ! or ? that a blank or a line's end
    // follows, past any ) ] " or ' after it; an empty line, and a line a
    // paragraph begins with (startsParagraph), is a sentence of its own. The
    // blanks after one are passed over. None where the text runs out before
    // the count does; the last sentence's end, where it runs out with it.
    std::optional<ALTextPos> findSentence(const ALTextDocument& d, ALTextPos pos, bool forward, S32 count)
    {
        const auto step     = [&d, forward](ALTextPos& p) { return forward ? inclPos(d, p) : declPos(d, p); };
        const auto boundary = [&d](S32 line) { return d.lineLength(line) == 0 || startsParagraph(d, line); };
        bool       noskip   = false;
        while (count-- > 0)
        {
            const ALTextPos prev  = pos;
            bool            found = false;
            if (at(d, pos) == '\0')
            {
                // Off the empty lines, which going on is a sentence's start.
                do
                {
                    if (step(pos) == -1)
                    {
                        break;
                    }
                } while (at(d, pos) == '\0');
                found = forward;
            }
            else if (forward && pos.column == 0 && boundary(pos.line))
            {
                if (pos.line + 1 >= d.lineCount())
                {
                    return std::nullopt;
                }
                pos   = ALTextPos(pos.line + 1, 0);
                found = true;
            }
            else if (!forward)
            {
                declPos(d, pos);
            }
            if (!found)
            {
                // Back over the blanks and the closing marks before the place
                // to the sentence they end, but over no more than one . ! or ?.
                bool found_dot = false;
                for (char c = at(d, pos); c == ' ' || c == '\t' || oneOf(".!?)]\"'", c); c = at(d, pos))
                {
                    ALTextPos before = pos;
                    if (declPos(d, before) == -1 || (d.lineLength(before.line) == 0 && forward) || found_dot)
                    {
                        break;
                    }
                    found_dot = oneOf(".!?", c);
                    if (oneOf(")]\"'", c) && !oneOf(".!?)]\"'", at(d, before)))
                    {
                        break;
                    }
                    declPos(d, pos);
                }
                // Then to the end of a sentence, going the way asked.
                const S32 start_line = pos.line;
                while (true)
                {
                    const char c = at(d, pos);
                    if (c == '\0' || (pos.column == 0 && boundary(pos.line)))
                    {
                        if (!forward && pos.line != start_line)
                        {
                            pos = ALTextPos(pos.line + 1, 0);
                        }
                        break;
                    }
                    if (oneOf(".!?", c))
                    {
                        ALTextPos after = pos;
                        S32       r     = 0;
                        char      next  = '\0';
                        do
                        {
                            r    = incPos(d, after);
                            next = at(d, after);
                        } while (r != -1 && oneOf(")]\"'", next));
                        if (r == -1 || next == ' ' || next == '\t' || next == '\0')
                        {
                            pos = after;
                            if (at(d, pos) == '\0')
                            {
                                incPos(d, pos);
                            }
                            break;
                        }
                    }
                    if (step(pos) == -1)
                    {
                        if (count > 0)
                        {
                            return std::nullopt;
                        }
                        noskip = true;
                        break;
                    }
                }
            }
            while (!noskip && sentenceBlank(d, pos))
            {
                if (inclPos(d, pos) == -1)
                {
                    break;
                }
            }
            // Where that did not move, once more from a step on.
            if (pos == prev)
            {
                if (step(pos) == -1)
                {
                    if (count > 0)
                    {
                        return std::nullopt;
                    }
                    break;
                }
                ++count;
            }
        }
        return pos;
    }

    // A sentence's start found from a place, the place left where none is.
    void toSentence(const ALTextDocument& d, ALTextPos& p, bool forward)
    {
        if (const std::optional<ALTextPos> to = findSentence(d, p, forward, 1))
        {
            p = *to;
        }
    }
    // vim's find_first_blank(): back over the blanks before a place, to the
    // first of them.
    void firstBlank(const ALTextDocument& d, ALTextPos& p)
    {
        while (declPos(d, p) != -1)
        {
            if (!sentenceBlank(d, p))
            {
                inclPos(d, p);
                break;
            }
        }
    }
    // vim's findsent_forward(): on by the count's sentences and the blanks
    // between them, a sentence and its blanks two, to the last character.
    void sentencesOn(const ALTextDocument& d, ALTextPos& p, S32 count, bool at_start_sent)
    {
        while (count-- > 0)
        {
            toSentence(d, p, true);
            if (at_start_sent)
            {
                firstBlank(d, p);
            }
            if (count == 0 || at_start_sent)
            {
                declPos(d, p);
            }
            at_start_sent = !at_start_sent;
        }
    }

    // vim's current_sent(): the sentence objects -- is the count's sentences
    // or runs of blanks between them, each one; as each sentence with the
    // blanks after it, or where none follow, those before. `start` and `end`
    // are an operator's, through the end where `inclusive`, else up to it;
    // over a visual selection, the anchor and the caret: chosen `afresh` from
    // a selection of one character -- whose mode becomes characters -- or
    // else the selection taken on, its anchor as it was and the caret on by
    // the count's sentences, or back where it is before the anchor.
    struct SentenceObject
    {
        ALTextPos start;
        ALTextPos end;
        bool      inclusive = false;
        bool      afresh    = false;
    };
    SentenceObject sentenceTakenOn(const ALTextDocument& d, const ALTextPos& anchor, const ALTextPos& start_pos, ALTextPos pos, ALTextPos cur,
                                   S32 count, bool around)
    {
        bool at_start_sent = true;
        if (start_pos < anchor)
        {
            // The caret at the selection's start: in the blanks before a
            // sentence, in one, or at its start.
            declPos(d, pos);
            while (pos < cur)
            {
                if (!sentenceBlank(d, pos))
                {
                    at_start_sent = false;
                    break;
                }
                inclPos(d, pos);
            }
            if (!at_start_sent)
            {
                toSentence(d, cur, false);
                if (cur == start_pos)
                {
                    at_start_sent = true;
                }
                else
                {
                    toSentence(d, cur, true);
                }
            }
            if (around)
            {
                count *= 2;
            }
            while (count-- > 0)
            {
                if (at_start_sent)
                {
                    firstBlank(d, cur);
                }
                if (!at_start_sent || (!around && !sentenceBlank(d, cur)))
                {
                    toSentence(d, cur, false);
                }
                at_start_sent = !at_start_sent;
            }
        }
        else
        {
            // The caret at its end: just before a sentence, in the blanks
            // before one, or in one.
            inclPos(d, pos);
            if (pos != cur)
            {
                at_start_sent = false;
                while (pos < cur)
                {
                    if (!sentenceBlank(d, pos))
                    {
                        at_start_sent = true;
                        break;
                    }
                    inclPos(d, pos);
                }
                if (at_start_sent)
                {
                    toSentence(d, cur, false);
                }
                else
                {
                    cur = start_pos;
                }
            }
            if (around)
            {
                count *= 2;
            }
            sentencesOn(d, cur, count, at_start_sent);
        }
        SentenceObject taken;
        taken.start = anchor;
        taken.end   = cur;
        return taken;
    }
    SentenceObject sentenceObject(const ALTextDocument& d, const ALTextPos& caret, S32 count, bool around, const ALTextPos* anchor)
    {
        ALTextPos start_pos = caret;
        ALTextPos pos       = caret;
        ALTextPos cur       = caret;
        toSentence(d, cur, true);
        if (anchor && start_pos != *anchor)
        {
            return sentenceTakenOn(d, *anchor, start_pos, pos, cur, count, around);
        }
        // From blanks just before the next sentence, those blanks; else the
        // sentence the place is in.
        while (sentenceBlank(d, pos))
        {
            inclPos(d, pos);
        }
        const bool start_blank = pos == cur;
        if (start_blank)
        {
            firstBlank(d, start_pos);
        }
        else
        {
            toSentence(d, cur, false);
            start_pos = cur;
        }
        const S32 ncount = around ? count * 2 : start_blank ? count - 1 : count;
        if (ncount > 0)
        {
            sentencesOn(d, cur, ncount, true);
        }
        else
        {
            declPos(d, cur);
        }
        if (around)
        {
            // Where the blanks before it were taken, none after it; where it
            // ends on none, the blanks before it.
            if (start_blank)
            {
                firstBlank(d, cur);
                if (sentenceBlank(d, cur))
                {
                    declPos(d, cur);
                }
            }
            else if (!sentenceBlank(d, cur))
            {
                firstBlank(d, start_pos);
            }
        }
        SentenceObject taken;
        taken.start = start_pos;
        taken.end   = cur;
        if (anchor)
        {
            // A blank alone before a sentence taken on, so that is again does
            // not stay where it is.
            if (start_pos == cur)
            {
                return sentenceTakenOn(d, *anchor, start_pos, pos, cur, count, around);
            }
            taken.afresh = true;
            return taken;
        }
        // Up to the character after it -- the next line's start, after a
        // line's last -- or at the text's end through it.
        taken.inclusive = inclPos(d, taken.end) == -1;
        return taken;
    }

    // A tag and the one that closes it, as offsets into the text: where
    // each begins, and where each ends past its >.
    struct TagPair
    {
        size_t openBegin, openEnd, closeBegin, closeEnd;
    };
    // Where a tag that opens at `lt` ends, at its >: one inside a quoted
    // attribute value -- title="a>b" -- is passed over, and a quote that
    // closes nowhere is a character like any other.
    size_t tagEnd(const std::string& text, size_t lt)
    {
        for (size_t i = lt + 1; i < text.size(); ++i)
        {
            if (text[i] == '"' || text[i] == '\'')
            {
                const size_t close = text.find(text[i], i + 1);
                if (close != std::string::npos)
                {
                    i = close;
                }
            }
            else if (text[i] == '>')
            {
                return i;
            }
        }
        return std::string::npos;
    }
    // Every tag in a text paired with its closing one by nesting, so that an
    // inner tag of the same name is not taken for the closing of the outer.
    // Self-closing tags, comments and declarations are no tags.
    std::vector<TagPair> tagPairs(const std::string& text)
    {
        // A tag opened and not yet closed: its name, where it begins, and its
        // > (tagEnd).
        struct OpenTag
        {
            std::string name;
            size_t      begin = 0;
            size_t      end   = 0;
        };
        std::vector<TagPair> pairs;
        std::vector<OpenTag> open;
        auto nameAt = [&](size_t at, size_t& end) {
            end = at;
            while (end < text.size() && (isWordByte(text[end]) || text[end] == '-' || text[end] == ':' || text[end] == '.'))
            {
                ++end;
            }
            return text.substr(at, end - at);
        };
        for (size_t lt = text.find('<'); lt != std::string::npos; lt = text.find('<', lt + 1))
        {
            if (text.compare(lt, 4, "<!--") == 0)
            {
                const size_t close = text.find("-->", lt + 4);
                lt                 = close == std::string::npos ? text.size() : close + 2;
                continue;
            }
            const size_t gt = text.find('>', lt + 1);
            if (gt == std::string::npos)
            {
                break;
            }
            if (lt + 1 < text.size() && text[lt + 1] == '/')
            {
                size_t            name_end;
                const std::string name = nameAt(lt + 2, name_end);
                // The nearest open tag of the name closes; the ones
                // opened after it were left unclosed.
                for (size_t i = open.size(); i-- > 0;)
                {
                    if (open[i].name == name)
                    {
                        pairs.push_back(TagPair{ open[i].begin, open[i].end + 1, lt, gt + 1 });
                        open.resize(i);
                        break;
                    }
                }
            }
            else if (lt + 1 < text.size() && (std::isalpha(static_cast<unsigned char>(text[lt + 1])) || text[lt + 1] == '_'))
            {
                size_t            name_end;
                const std::string name = nameAt(lt + 1, name_end);
                const size_t      end  = tagEnd(text, lt);
                if (end == std::string::npos)
                {
                    break;
                }
                if (!name.empty() && text[end - 1] != '/')
                {
                    open.push_back(OpenTag{ name, lt, end });
                }
                lt = end;
                continue;
            }
            lt = gt;
        }
        return pairs;
    }

    // vim's find_next_quote(): the first quote at or after a column, the
    // character after a backslash passed over where `escaped`; -1 for none.
    S32 nextQuote(const std::string& line, S32 col, char quote, bool escaped)
    {
        const S32 size = static_cast<S32>(line.size());
        for (; col < size; ++col)
        {
            if (escaped && line[col] == '\\')
            {
                if (++col >= size)
                {
                    return -1;
                }
                continue;
            }
            if (line[col] == quote)
            {
                return col;
            }
        }
        return -1;
    }
    // vim's find_prev_quote(): the last quote before a column that no odd
    // run of backslashes escapes, where `escaped`; else where the looking
    // stopped, the line's start, which the caller tells from a quote.
    S32 prevQuote(const std::string& line, S32 col, char quote, bool escaped)
    {
        while (col > 0)
        {
            --col;
            S32 n = 0;
            while (escaped && col - n > 0 && line[col - n - 1] == '\\')
            {
                ++n;
            }
            if (n % 2 != 0)
            {
                col -= n;
            }
            else if (line[col] == quote)
            {
                break;
            }
        }
        return col;
    }
    // vim's current_quote() over a visual selection of more than one
    // character on one line, `anchor` and `caret` its columns: taken on to
    // the quoted text it is in or comes before -- after the caret, or before
    // it where the caret is before the anchor -- from the quote under the
    // caret to the next quoted text; inside the quotes, or for `around` with
    // them and the blanks after, or else those before. A selection of just
    // what a pair holds takes the quotes as well, and so does a count of two
    // or more. Where the anchor and the caret go; none where there is no
    // such text.
    std::optional<std::pair<S32, S32>> quoteOn(const std::string& line, S32 anchor, S32 caret, char quote, bool around, S32 count)
    {
        const S32  size    = static_cast<S32>(line.size());
        const auto ch      = [&line, size](S32 col) { return col >= 0 && col < size ? line[col] : '\0'; };
        const auto blank   = [&ch](S32 col) { return ch(col) == ' ' || ch(col) == '\t'; };
        const bool forward = anchor < caret;
        const S32  low     = forward ? anchor : caret;
        const S32  high    = forward ? caret : anchor;
        const bool inside  = low > 0 && ch(low - 1) == quote && ch(high) != '\0' && ch(high + 1) == quote;
        bool       quoted  = false;
        for (S32 i = low; i <= high && ch(i) != '\0' && !quoted; ++i)
        {
            quoted = ch(i) == quote;
        }
        S32 col_start = caret;
        S32 col_end   = caret;
        if (ch(caret) == quote)
        {
            if (forward)
            {
                // Taken to be the closing quote: the text the next pair holds,
                // or, with no more pairs, what this one opens.
                col_start = nextQuote(line, caret + 1, quote, false);
                if (col_start < 0)
                {
                    return std::nullopt;
                }
                col_end = nextQuote(line, col_start + 1, quote, true);
                if (col_end < 0)
                {
                    col_end   = col_start;
                    col_start = caret;
                }
            }
            else
            {
                col_end = prevQuote(line, caret, quote, false);
                if (ch(col_end) != quote)
                {
                    return std::nullopt;
                }
                col_start = prevQuote(line, col_end, quote, true);
                if (ch(col_start) != quote)
                {
                    col_start = col_end;
                    col_end   = caret;
                }
            }
        }
        else
        {
            // The pair, counted from the line's start, around the first
            // quote the caret faces.
            const S32 first = forward ? nextQuote(line, caret, quote, false) : prevQuote(line, caret, quote, false);
            col_start       = 0;
            while (true)
            {
                col_start = nextQuote(line, col_start, quote, false);
                if (col_start < 0 || col_start > first)
                {
                    return std::nullopt;
                }
                col_end = nextQuote(line, col_start + 1, quote, true);
                if (col_end < 0)
                {
                    return std::nullopt;
                }
                if (col_start <= first && first <= col_end)
                {
                    break;
                }
                col_start = col_end + 1;
            }
        }
        if (around)
        {
            if (blank(col_end + 1))
            {
                while (blank(col_end + 1))
                {
                    ++col_end;
                }
            }
            else
            {
                while (col_start > 0 && blank(col_start - 1))
                {
                    --col_start;
                }
            }
        }
        if (!around && count < 2 && !inside)
        {
            ++col_start;
        }
        // The last character: the closing quote or the blank after it, or the
        // one before the quote.
        const S32 last = around || count > 1 || inside ? col_end : col_end - 1;
        if (forward)
        {
            // The anchor to the start, where the selection held no quote and
            // was just what a pair holds, or began at none.
            const bool restart = !quoted && (inside || (ch(anchor) != quote && (anchor == 0 || ch(anchor - 1) != quote)));
            return std::make_pair(restart ? col_start : anchor, last);
        }
        // The anchor to the end, where the selection was just what a pair
        // holds, or held no quote and ended at none.
        const bool reend = inside || (!quoted && ch(anchor) != quote && (ch(anchor) == '\0' || ch(anchor + 1) != quote));
        return std::make_pair(reend ? last : anchor, col_start);
    }
}

bool ALVimKeymap::afterBracket(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const S32             count   = countOr(mCount);
    // ]d [d the problems, ]s [s the misspellings, [z ]z the fold
    // the caret is in, ]c [c the changes of a comparison.
    const bool    forward  = pending == ']';
    // An operator's count with the motion's: 2d]) is d2]).
    const S32     given    = mOperatorCount > 0 ? countTimes(mOperatorCount, countOr(mCount)) : mCount;
    const llwchar operated = mOperator;
    clearPending();
    if (ch == 'd')
    {
        mEx->runCommand(view, std::string(forward ? "cnext" : "cprevious") + (given > 0 ? " " + std::to_string(given) : ""));
    }
    else if (ch == 's')
    {
        misspelling(view, forward, countOr(given));
    }
    else if (ch == 'c')
    {
        // The count on, as far as there are changes.
        ALVimHost* host = view.vimHost();
        for (S32 n = 0; n < countOr(given) && host && host->stepChange(forward); ++n)
        {
        }
    }
    else if (ch == 'z')
    {
        foldCommand(view, ch, pending);
    }
    else if (ch == '[' || ch == ']' || ch == 'm' || ch == 'M')
    {
        // [[ ]] [m ]m the start of the function before or after
        // the caret; [] ][ [M ]M its end. The count on.
        const bool          ends   = ch == 'M' || (ch == '[' && forward) || (ch == ']' && !forward);
        ALVimHost* host = view.vimHost();
        // From past the character the caret is on, going on:
        // at a function's end already is not before it.
        ALTextPos                  at = forward && ends ? d.nextCluster(cursor(view)) : cursor(view);
        std::optional<ALTextRange> fn;
        for (S32 n = 0; n < countOr(given) && host; ++n)
        {
            const std::optional<ALTextRange> next = host->functionFrom(at, forward, ends);
            if (!next)
            {
                break;
            }
            fn = next;
            at = ends ? next->end : next->begin;
        }
        if (fn)
        {
            const ALTextPos to = ends ? d.prevCluster(fn->end) : fn->begin;
            if (operated)
            {
                Span span;
                span.range = ALTextRange(cursor(view), to).normalised();
                adjustExclusiveEnd(d, span.range, span.linewise);
                applyOperator(view, operated, span, 1);
                finishCommand(operated != 'y');
                return true;
            }
            noteJump(view, cursor(view));
            moveTo(view, to);
        }
    }
    else if ((forward && (ch == ')' || ch == '}')) || (!forward && (ch == '(' || ch == '{')))
    {
        ALTextPos to;
        if (unmatchedBracket(view, ch, countOr(given), to))
        {
            if (operated)
            {
                // Exclusive, as vim's are: the bracket itself is
                // left.
                Span span;
                span.range = ALTextRange(cursor(view), to).normalised();
                adjustExclusiveEnd(d, span.range, span.linewise);
                applyOperator(view, operated, span, 1);
                finishCommand(operated != 'y');
                return true;
            }
            moveTo(view, to);
        }
    }
    return true;
}

bool ALVimKeymap::afterBigZ(ALTextView& view, llwchar pending, llwchar ch)
{
    if (ch == 'Z')
    {
        mEx->runCommand(view, "x");
    }
    else if (ch == 'Q')
    {
        mEx->runCommand(view, "q!");
    }
    clearPending();
    return true;
}

bool ALVimKeymap::afterWindow(ALTextView& view, llwchar pending, llwchar ch)
{
    // A tab for each of vim's windows: closed, the others
    // closed, a new one, the next, the one before, the one
    // last in front. There are no splits to go between.
    const char* const line = ch == 'q' ? "quit"
                             : ch == 'c' ? "close"
                             : ch == 'o' ? "tabonly"
                             : ch == 'n' ? "tabnew"
                             : ch == 'w' ? "tabnext"
                             : ch == 'W' ? "tabprevious"
                             : ch == 'p' ? "buffer #"
                                         : nullptr;
    clearPending();
    if (line)
    {
        mEx->runCommand(view, line);
    }
    else
    {
        mFailed = true;
    }
    return true;
}

bool ALVimKeymap::afterObject(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    // The operator's count and the object's together: 2daw is d2aw.
    const S32             count   = countTimes(countOr(mOperatorCount), countOr(mCount));
    // A visual selection that is more than its caret's character taken on
    // by the object rather than replaced by it (visualObject); with nothing
    // more to take, a failure, as no object is.
    if (visual)
    {
        if (const std::optional<bool> taken = visualObject(view, pending, ch, count))
        {
            mFailed = mFailed || !*taken;
            clearPending();
            return true;
        }
    }
    if (visual && (ch == 'w' || ch == 'W'))
    {
        // A word object selected as vim's selects one: the anchor at its
        // start, the caret where vim's cursor is left (wordObject), which
        // may be the next line's first character. Lines become characters,
        // and a block stays one.
        if (const std::optional<WordObject> word = wordObject(d, cursor(view), pending == 'a', ch == 'W', count))
        {
            mVisualAnchor = word->start;
            mVisualCaret  = word->end;
            if (mMode == Mode::VisualLine)
            {
                setMode(view, Mode::Visual);
            }
            showVisual(view);
        }
        else
        {
            mFailed = true;
        }
        clearPending();
        return true;
    }
    // A text object, for the operator or the visual selection.
    Span span;
    if (textObject(view, pending, ch, count, span))
    {
        if (visual)
        {
            // Lines for a paragraph, the caret at the last one's start, else
            // characters -- but a quote object over a block keeps the block,
            // as vim's does.
            const bool quote = ch == '"' || ch == '\'' || ch == '`';
            mVisualAnchor    = span.range.begin;
            mVisualCaret     = span.linewise ? d.lineStart(span.range.end.line) : d.prevCluster(span.range.end);
            setMode(view, span.linewise ? Mode::VisualLine : quote && mMode == Mode::VisualBlock ? Mode::VisualBlock : Mode::Visual);
            showVisual(view);
            clearPending();
            return true;
        }
        const llwchar op = mOperator;
        applyOperator(view, op, span, 1);
        finishCommand(op != 'y');
        return true;
    }
    // No such object: a failure, as a motion that cannot move is one, which
    // stops a macro as vim's beep does; the operator and the count let go
    // of, nothing changed, and a selection as it was.
    mFailed = true;
    clearPending();
    return true;
}

bool ALVimKeymap::afterMotionKey(ALTextView& view, llwchar pending, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    // The operator's count and the motion's together: 2df. is d2f.
    const S32             count   = countTimes(countOr(mOperatorCount), countOr(mCount));
    // f, F, t, T, ` and ': motions with an argument.
    Motion m = motion(view, pending, count, ch);
    mFailed  = mFailed || (m.ok && !m.moved);
    // One that could not move -- f with no such character on the line, a
    // mark not set -- fails an operator with it, as vim's does: nothing
    // changed or kept, and no insert for c.
    if (!m.ok || (mOperator && !m.moved))
    {
        clearPending();
        return true;
    }
    if (mOperator)
    {
        Span span;
        const ALTextPos from = cursor(view);
        span.range           = ALTextRange(from, m.to).normalised();
        span.linewise        = m.linewise;
        span.inclusive       = m.inclusive;
        span.registerOne     = pending == '`';
        if (m.inclusive)
        {
            span.range.end = d.nextCluster(span.range.end);
        }
        else
        {
            adjustExclusiveEnd(d, span.range, span.linewise);
        }
        const llwchar op = mOperator;
        applyOperator(view, op, span, 1);
        finishCommand(op != 'y');
        return true;
    }
    if ((pending == '`' || pending == '\'') && m.to != cursor(view))
    {
        noteJump(view, cursor(view));
    }
    moveTo(view, m.to);
    clearPending();
    return true;
}

bool ALVimKeymap::operatorKey(ALTextView& view, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const S32             count   = countOr(mCount);
    const llwchar op = mOperator;
    // Surround's: ys an operator of its own, ds and cs the pair to
    // take away or change, as surround.vim has them.
    if (op == 'y' && ch == 's')
    {
        mOperator = SURROUND_OPERATOR;
        return true;
    }
    if ((op == 'd' || op == 'c') && ch == 's')
    {
        mOperator = 0;
        mPending  = op == 'd' ? DELETE_SURROUND : CHANGE_SURROUND;
        return true;
    }
    if (op == SURROUND_OPERATOR && ch == 's')
    {
        // yss: the line's text, from the first of it that is not blank
        // to the last.
        const S32          line = cursor(view).line;
        const std::string& text = d.line(line);
        S32                end  = static_cast<S32>(text.size());
        while (end > 0 && isSpace(text[end - 1]))
        {
            --end;
        }
        Span span;
        const S32 first = firstNonBlankColumn(d, line);
        span.range      = ALTextRange(ALTextPos(line, first), ALTextPos(line, llmax(end, first)));
        applyOperator(view, op, span, 1);
        finishCommand(true);
        return true;
    }
    // The operator doubled -- dd, yy, cc, >>, <<, == -- is the line, and
    // the count more: a motion from the caret, as vim's is, to the first
    // non-blank of the last of them, as many as there are -- but a count
    // past one from the last line fails, as vim's motion down fails there.
    // gUU leaves the caret at its start, on the line's first non-blank or
    // before it, and 2gUU where it was.
    if (ch == op || (op == '~' && ch == '~') || (op == 'u' && ch == 'u') || (op == 'U' && ch == 'U') ||
        (op == COMMENT_OPERATOR && ch == 'c'))
    {
        const S32       lines = countTimes(countOr(mOperatorCount), count);
        const ALTextPos from  = cursor(view);
        if (lines > 1 && from.line + 1 >= d.lineCount())
        {
            mFailed = true;
            clearPending();
            return true;
        }
        Span            span;
        span.linewise         = true;
        const S32       last  = llmin(d.lineCount() - 1, from.line + lines - 1);
        span.range            = ALTextRange(from, ALTextPos(last, firstNonBlankColumn(d, last))).normalised();
        applyOperator(view, op, span, 1);
        finishCommand(op != 'y');
        return true;
    }
    if (ch == 'i' || ch == 'a')
    {
        mPending = ch;
        return true;
    }
    if (ch == 'f' || ch == 'F' || ch == 't' || ch == 'T' || ch == '`' || ch == '\'' || ch == 'g' || ch == '[' || ch == ']')
    {
        mPending = ch;
        return true;
    }
    if (ch == '/' || ch == '?')
    {
        // A search the motion: its line typed, the operator waiting on it
        // -- over the stretch to where it goes once the line is entered
        // (searchMotion), or let go of with the line.
        mCommandLine.kind      = ch;
        mCommandLine.historyAt = -1;
        mCommandLine.line.clear();
        mCommandLine.cursor = 0;
        setMode(view, Mode::Search);
        return true;
    }
    if (ch == 'n' || ch == 'N')
    {
        return searchMotion(view, ch == 'n' ? mSearch.forward : !mSearch.forward);
    }
    if (ch == '*' || ch == '#')
    {
        return starSearch(view, ch == '*', true);
    }
    // cw on a word is ce: the space after it is not eaten. On the word's
    // last character the first word is the one it ends, as vim's cw has
    // it -- a one-letter word changes alone -- and the count's others are
    // the words after.
    llwchar         m_ch  = ch;
    S32             times = countTimes(countOr(mOperatorCount), count);
    const ALTextPos here  = cursor(view);
    const bool      big   = ch == 'W';
    if (op == 'c' && (ch == 'w' || ch == 'W') && classAt(d, here, big) != 0)
    {
        m_ch = big ? 'E' : 'e';
        if (classAt(d, d.nextCluster(here), big) != classAt(d, here, big))
        {
            --times;
        }
    }
    Motion m;
    if (times > 0)
    {
        m = motion(view, m_ch, times, 0);
    }
    else
    {
        m.ok        = true;
        m.to        = here;
        m.inclusive = true;
    }
    // h and l, and the word motions w W e E, that cannot move -- l or cw
    // on an empty line, h at a line's start, w on the last line's end --
    // leave the operator the empty stretch where they stay, as vim's do: c
    // inserts there, y yanks nothing, d takes nothing, and none fails.
    const bool stays = m.ok && (m_ch == 'h' || m_ch == 'l' || m_ch == ' ' || m_ch == 'w' || m_ch == 'W' || m_ch == 'e' || m_ch == 'E');
    if (!m.ok || (!m.moved && !stays))
    {
        // A key that is no motion, or one that could not move: the
        // operator fails, and the key is taken all the same -- never left
        // for the view to type into the text. gj, gk or g$ out of rows went
        // partway before it failed, and leaves the caret where it got to,
        // as vim's does; any other is where it began.
        if (m.ok && (m_ch == DISPLAY_DOWN || m_ch == DISPLAY_UP || m_ch == DISPLAY_END) && m.to != here)
        {
            moveTo(view, m.to);
        }
        mFailed = true;
        clearPending();
        return true;
    }
    Span            span;
    const ALTextPos from = cursor(view);
    span.range           = ALTextRange(from, m.to).normalised();
    span.linewise        = m.linewise;
    span.inclusive       = m.inclusive || m_ch == '$';
    span.registerOne     = m_ch == '%' || m_ch == '{' || m_ch == '}' || m_ch == '(' || m_ch == ')';
    // The character an inclusive motion ends on taken with it; none where
    // the stretch ends at a line's end -- g_ on an empty line, ge from
    // one -- whose break is no character of the stretch's.
    if (m.inclusive)
    {
        if (!atLineEnd(d, span.range.end))
        {
            span.range.end = d.nextCluster(span.range.end);
        }
    }
    else if (m_ch != '$')
    {
        // $ ends past the last character rather than on it, but is vim's
        // inclusive motion all the same: d2$ onto an empty line takes the
        // break before it.
        adjustExclusiveEnd(d, span.range, span.linewise);
    }
    applyOperator(view, op, span, 1);
    finishCommand(op != 'y');
    return true;
}

bool ALVimKeymap::searchMotion(ALTextView& view, bool forward, std::optional<ALTextPos> search_from)
{
    const ALTextDocument& d = view.document();
    // The operator's count and the motion's together: 2dn is d2n, the
    // second match on.
    const S32     count = countTimes(countOr(mOperatorCount), countOr(mCount));
    const llwchar op    = mOperator;
    if (mSearch.pattern.empty())
    {
        say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        clearPending();
        return true;
    }
    const ALTextPos                from   = cursor(view);
    const ALVimSearch::Offset      offset = mSearch.offset;
    const std::optional<ALTextPos> to     = mSearch.target(view, mSearch.pattern, forward, count, mSearch.noSmartCase, offset, search_from);
    if (!to)
    {
        // Said already: the operator fails, and nothing is changed.
        clearPending();
        return true;
    }
    Span span;
    span.range       = ALTextRange(from, *to).normalised();
    span.linewise    = offset.kind == 'l';
    span.inclusive   = offset.kind == 'e';
    span.registerOne = true;
    if (offset.kind == 'e')
    {
        span.range.end = d.nextCluster(span.range.end);
    }
    else
    {
        adjustExclusiveEnd(d, span.range, span.linewise);
    }
    applyOperator(view, op, span, 1);
    finishCommand(op != 'y');
    return true;
}

bool ALVimKeymap::starSearch(ALTextView& view, bool forward, bool whole)
{
    const ALTextDocument& d     = view.document();
    const ALTextPos       caret = d.clamp(cursor(view));
    // What vim's find_ident_under_cursor() takes, by vim's classes: the word
    // under the caret, or the first after it on the line -- a word any class
    // but a blank's and punctuation's, so on a blank, the word after it; and
    // where no word follows, the other characters under the caret or after
    // it, back to where their run begins and on to the next blank. Nothing
    // but blanks to the line's end is nothing.
    const auto cls   = [&d](const ALTextPos& p) { return classAt(d, p, false); };
    ALTextPos  begin = caret;
    while (!atLineEnd(d, begin) && cls(begin) < 2)
    {
        begin = d.nextCluster(begin);
    }
    const bool keyword = !atLineEnd(d, begin);
    if (!keyword)
    {
        begin = caret;
        while (!atLineEnd(d, begin) && cls(begin) == 0)
        {
            begin = d.nextCluster(begin);
        }
    }
    if (atLineEnd(d, begin))
    {
        mFailed = true;
        clearPending();
        return true;
    }
    const S32 kind = cls(begin);
    while (begin.column > 0 && cls(d.prevCluster(begin)) == kind)
    {
        begin = d.prevCluster(begin);
    }
    ALTextPos end = begin;
    while (!atLineEnd(d, end) && (keyword ? cls(end) == kind : cls(end) != 0))
    {
        end = d.nextCluster(end);
    }
    const ALTextRange word(begin, end);
    // A word is looked for whole, as \<word\>, or anywhere for g* and g#;
    // other characters anywhere, as themselves: a backslash before each
    // that a pattern reads as more, as vim's * puts one -- for # and g#
    // before a ? as well, which would end what ? types. A word has none
    // of them. Into the search history so, and kept as the last pattern as
    // ? reads it (backwardPattern), for what takes it up -- n, :s//, :g//
    // -- to look for the same, its case as ignorecase alone says.
    static constexpr std::string_view SPECIAL("/.*~[^$\\");
    static constexpr std::string_view SPECIAL_BACKWARD("/?.*~[^$\\");
    std::string                       pattern;
    for (const char c : d.text(word))
    {
        if ((forward ? SPECIAL : SPECIAL_BACKWARD).find(c) != std::string_view::npos)
        {
            pattern += '\\';
        }
        pattern += c;
    }
    if (whole && keyword)
    {
        pattern = "\\<" + pattern + "\\>";
    }
    // Into the search history as vim's * puts it there, for the search
    // line to find again: as a line not typed, which a typed one the same
    // is not.
    mCommandLine.remember('/', pattern, false);
    mSearch.pattern     = forward ? pattern : ALVimSearch::backwardPattern(pattern);
    mSearch.forward     = forward;
    mSearch.noSmartCase = true;
    mSearch.offset      = ALVimSearch::Offset();
    // Looked for from the word's start, as vim puts the caret there first:
    // # from inside a word goes to the one before it, not to its own start.
    if (mOperator)
    {
        return searchMotion(view, forward, word.begin);
    }
    mSearch.search(view, mSearch.pattern, forward, countOr(mCount), mSearch.noSmartCase, mSearch.offset, word.begin);
    clearPending();
    return true;
}

std::optional<bool> ALVimKeymap::visualKey(ALTextView& view, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();
    const S32             count   = countOr(mCount);
    switch (ch)
    {
        case 'd':
        case 'x':
        case 'y':
        case 'c':
        case 's':
        case '>':
        case '<':
        case '=':
        case '~':
        case 'u':
        case 'U':
        case 'J':
        case 'r':
        case 'p':
        case 'P':
        case 'I':
        case 'A':
        case 'D':
        case 'X':
        case 'Y':
        case 'C':
        case 'S':
        case 'R':
        {
            if (ch == 'r')
            {
                mPending = 'r';
                return true;
            }
            if (ch == 'S')
            {
                // The selection surrounded, as surround.vim's visual S:
                // kept for the character that says with what.
                const Span span = visualSpan(view);
                noteVisualOperation(d, span);
                leaveVisual(view);
                clearPending();
                mSurroundSpan    = span;
                mPending         = SURROUND_WITH;
                mSurroundWaiting = true;
                return true;
            }
            // A line's break taken by an operator over characters, not by
            // one over lines (visualSpan).
            const bool over_lines = ch == '>' || ch == '<' || ch == '=' || ch == 'J' || ch == 'I' || ch == 'A' || ch == 'D' || ch == 'X' || ch == 'Y' ||
                                    ch == 'C' || ch == 'R';
            Span span = visualSpan(view, !over_lines);
            if (ch == 'D' || ch == 'X' || ch == 'Y' || ch == 'C' || ch == 'S' || ch == 'R')
            {
                // The lines whole, whatever was selected.
                span.linewise = true;
                span.block    = false;
                span.range    = ALTextRange(d.lineStart(span.range.begin.line), d.lineEnd(span.range.end.line));
            }
            if (ch == 'J')
            {
                const S32 first = span.range.begin.line;
                const S32 last  = llmax(span.range.end.line, first + 1);
                leaveVisual(view);
                mCount = last - first + 1;
                view.setCaret(d.lineStart(first));
                return command(view, 'J');
            }
            if (ch == 'p' || ch == 'P')
            {
                // The selection replaced by the register, the count's
                // copies of it. Lines are replaced by its text, its lines
                // taking theirs, a copy a line or more; lines put into less
                // than a line go on lines of their own, the line broken
                // round them; characters go in straight on. What a block
                // holds of each line is replaced by a register of one line,
                // its copies side by side, as vim's blockwise put has it; by
                // anything else it is taken out, and the register put at
                // the block's corner -- lines under the block for p, over
                // it for P. What p replaced goes where a delete puts what
                // it takes -- the unnamed register, and 1 or - -- once the
                // register is put, whichever was named, so a further p puts
                // that; P leaves every register as it was, for a further P
                // to put the same again.
                const Register                 put_this = fetch(mRegister);
                const std::vector<ALTextRange> pieces   = span.block ? blockPieces(view, span) : std::vector<ALTextRange>();
                std::string                    taken    = span.block ? blockText(d, pieces, span.left, span.right, span.toEnd, view.getTabWidth()) : d.text(span.range);
                leaveVisual(view);
                if (!editing)
                {
                    clearPending();
                    return true;
                }
                if (count > 1 && put_this.text.size() * static_cast<size_t>(count) > MAX_COUNT_TEXT)
                {
                    tooMuch(put_this.text.size() * static_cast<size_t>(count));
                    clearPending();
                    return true;
                }
                view.undoJournal().beginGroup();
                if (span.block)
                {
                    const bool one_line = !put_this.linewise && !put_this.block && put_this.text.find('\n') == std::string::npos;
                    std::string copies;
                    for (S32 n = 0; one_line && n < count; ++n)
                    {
                        copies += put_this.text;
                    }
                    std::vector<std::pair<ALTextRange, std::string>> edits;
                    bool                                             put_first = false;
                    for (size_t i = 0; i < pieces.size(); ++i)
                    {
                        // A line that stops short of the block left as it
                        // is, as vim's blockwise put leaves it; one that
                        // reaches the block's first column has the text
                        // added at its end.
                        const ALTextRange& piece    = pieces[i];
                        const bool         short_of = piece.empty() && !padTo(d, piece.begin.line, span.left, view.getTabWidth()).empty();
                        std::string        with     = one_line && !short_of ? copies : std::string();
                        put_first                   = put_first || (i == 0 && !with.empty());
                        if (!piece.empty() || !with.empty())
                        {
                            edits.emplace_back(piece, std::move(with));
                        }
                    }
                    if (!edits.empty())
                    {
                        view.replaceAll(std::move(edits));
                    }
                    const ALTextPos corner = d.clamp(pieces.front().begin);
                    if (one_line)
                    {
                        // On the last character put on the block's first
                        // line, as vim leaves it; at the corner where none
                        // went there.
                        moveTo(view, put_first ? d.prevCluster(ALTextPos(corner.line, corner.column + static_cast<S32>(copies.size()))) : corner);
                    }
                    else
                    {
                        const bool below = put_this.linewise && ch == 'p';
                        view.setCaret(below ? d.lineStart(pieces.back().begin.line) : corner);
                        put(view, mRegister, below, count);
                    }
                }
                else
                {
                    // As lines -- a register's lines, or anything into lines
                    // -- each copy a line or more of its own, the caret on the
                    // first line put; characters into characters straight
                    // on, the caret on the last put, or where they begin
                    // where they go over more than one line, as put() leaves
                    // it.
                    const bool  lines = put_this.linewise || span.linewise;
                    const bool  split = put_this.linewise && !span.linewise;
                    std::string text;
                    for (S32 n = 0; n < count; ++n)
                    {
                        if (lines && n > 0)
                        {
                            text += '\n';
                        }
                        text += put_this.text;
                    }
                    view.deleteRange(span.range);
                    view.setCaret(span.range.begin);
                    view.insertText(split ? "\n" + text + "\n" : text);
                    if (lines)
                    {
                        const S32 line = span.range.begin.line + (split ? 1 : 0);
                        moveTo(view, ALTextPos(line, firstNonBlankColumn(d, line)));
                    }
                    else
                    {
                        moveTo(view, text.empty() ? view.caret() : text.find('\n') != std::string::npos ? span.range.begin : d.prevCluster(view.caret()));
                    }
                }
                view.undoJournal().endGroup();
                if (ch == 'p' && (span.linewise || span.block || !span.range.empty()))
                {
                    store(0, std::move(taken), span.linewise, span.block, false);
                }
                finishCommand(true);
                return true;
            }
            if (ch == 'I' || ch == 'A')
            {
                // Typed onto every line of the block, once insert mode
                // is left, at its columns as the reader counts them: I
                // before what it holds of each line, A past it -- past
                // the line's end for a block taken with $, else at the
                // block's right edge, a line short of it padded out with
                // blanks, as vim's A has it.
                const bool        block  = span.block;
                const S32         first  = span.range.begin.line;
                const S32         last   = span.range.end.line;
                const S32         tab    = view.getTabWidth();
                const S32         column = ch == 'A' ? span.right : span.left;
                const ALTextRange piece  = blockPiece(d, first, span.left, span.right, span.toEnd, tab);
                const ALTextPos   start  = block ? (ch == 'A' ? piece.end : piece.begin)
                                                 : ALTextPos(first, ch == 'A' ? d.lineLength(first) : firstNonBlankColumn(d, first));
                const std::string pad    = block && ch == 'A' && !span.toEnd ? padTo(d, first, span.right + 1, tab) : std::string();
                leaveVisual(view);
                if (!editing)
                {
                    clearPending();
                    return true;
                }
                mBlockInsert = block && last > first;
                mBlockFirst  = first;
                mBlockLast   = last;
                mBlockColumn = column;
                mBlockAppend = ch == 'A';
                mBlockToEnd  = span.toEnd;
                // The blanks are part of the insert, one step to undo
                // with what is typed.
                view.undoJournal().beginGroup();
                view.setCaret(start);
                if (!pad.empty())
                {
                    view.insertText(pad);
                }
                enterInsert(view, 1, true);
                return true;
            }
            const llwchar op = ch == 'x' ? 'd' : ch == 's' || ch == 'C' || ch == 'S' || ch == 'R' ? 'c' : ch == 'D' || ch == 'X' ? 'd' : ch == 'Y' ? 'y' : ch;
            const S32     n  = count;
            noteVisualOperation(d, span);
            leaveVisual(view);
            applyOperator(view, op, span, n);
            finishCommand(op != 'y');
            return true;
        }
        case 'o':
        {
            std::swap(mVisualAnchor, mVisualCaret);
            showVisual(view);
            clearPending();
            return true;
        }
        case 'O':
        {
            // o, but in a block the other corner on the caret's own line:
            // the caret and the anchor trade the block's columns as the
            // reader counts them, each staying on its line -- the caret to
            // the right edge, or to the left where it is at the right
            // already, as vim's O has it. A block taken with $ is one of
            // its columns again.
            if (mMode != Mode::VisualBlock)
            {
                std::swap(mVisualAnchor, mVisualCaret);
            }
            else
            {
                const S32       tab    = view.getTabWidth();
                const ALTextPos anchor = d.clamp(mVisualAnchor);
                const ALTextPos caret  = d.clamp(mVisualCaret);
                S32             left   = 0;
                S32             right  = 0;
                blockColumns(d, anchor, caret, tab, left, right);
                const ALTextPos to_right = d.posAtDisplayColumn(caret.line, right, tab);
                const bool      at_right = to_right == caret;
                mVisualAnchor            = d.posAtDisplayColumn(anchor.line, at_right ? right : left, tab);
                mVisualCaret             = at_right ? d.posAtDisplayColumn(caret.line, left, tab) : to_right;
            }
            showVisual(view);
            clearPending();
            return true;
        }
        case 'v':
        case 'V':
        case 0x16:
        {
            const Mode which = ch == 'v' ? Mode::Visual : ch == 'V' ? Mode::VisualLine : Mode::VisualBlock;
            if (mMode == which)
            {
                leaveVisual(view);
            }
            else
            {
                setMode(view, which);
                showVisual(view);
            }
            clearPending();
            return true;
        }
        case 'i':
        case 'a':
            mPending = ch;
            return true;
        default:
            return std::nullopt;
    }
}

std::optional<bool> ALVimKeymap::normalKey(ALTextView& view, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();
    const S32             count   = countOr(mCount);
    switch (ch)
    {
        case 'K':
            // The word under the caret looked up: the host's reference.
            clearPending();
            if (!mOperator && mHooks.command)
            {
                mHooks.command(view, "reference", std::string());
            }
            return true;
        case 'd':
        case 'c':
        case 'y':
        case '>':
        case '<':
        case '=':
            mOperator      = ch;
            mOperatorCount = mCount;
            mCount         = 0;
            return true;
        case 'q':
            if (mRecording)
            {
                // The q that stops is not part of what was recorded.
                if (!mRecorded.empty())
                {
                    mRecorded.pop_back();
                }
                const char        into    = mRecording;
                const std::string keys    = encodeInputs(mRecorded);
                mRecording                = 0;
                mRecorded.clear();
                // Into its register alone: what was typed is not what the
                // clipboard holds.
                mShared->registers.record(into, keys);
                clearPending();
                return true;
            }
            mPending = 'q';
            return true;
        case '@':
            mPending = '@';
            return true;
        case 'g':
        case 'z':
        case 'Z':
        case 'm':
        case 'f':
        case 'F':
        case 't':
        case 'T':
        case '`':
        case '\'':
        case '[':
        case ']':
            mPending = ch;
            return true;
        case 'r':
            mPending = 'r';
            return true;
        case 'i':
            if (editing)
            {
                enterInsert(view, count);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'a':
            if (editing)
            {
                if (!atLineEnd(d, view.caret()))
                {
                    view.setCaret(d.nextCluster(view.caret()));
                }
                enterInsert(view, count);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'I':
            if (editing)
            {
                view.setCaret(ALTextPos(view.caret().line, firstNonBlankColumn(d, view.caret().line)));
                enterInsert(view, count);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'A':
            if (editing)
            {
                view.setCaret(d.lineEnd(view.caret().line));
                enterInsert(view, count);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'o':
        case 'O':
        {
            if (!editing)
            {
                clearPending();
                return true;
            }
            const S32         line   = view.caret().line;
            const std::string indent = indentOf(d, line);
            view.undoJournal().beginGroup();
            if (ch == 'o')
            {
                // The view's own Return, which goes a level in under what
                // opens a block, as the language's rules have it.
                view.setCaret(d.lineEnd(line));
                view.perform(ALEditorCommand::NewLine);
            }
            else
            {
                view.setCaret(d.lineStart(line));
                view.insertText(indent + "\n");
                view.setCaret(ALTextPos(line, static_cast<S32>(indent.size())));
            }
            enterInsert(view, count, true);
            // A count opens as many lines.
            mInsertOpened = true;
            return true;
        }
        case 'x':
        case 'X':
        case 's':
        {
            if (!editing)
            {
                clearPending();
                return true;
            }
            const ALTextPos from = view.caret();
            Span            span;
            if (ch == 'X')
            {
                ALTextPos begin = from;
                for (S32 n = 0; n < count && begin.column > 0; ++n)
                {
                    begin = d.prevCluster(begin);
                }
                span.range = ALTextRange(begin, from);
            }
            else
            {
                ALTextPos end = from;
                for (S32 n = 0; n < count && !atLineEnd(d, end); ++n)
                {
                    end = d.nextCluster(end);
                }
                span.range = ALTextRange(from, end);
            }
            // Nothing to take: x and X do nothing, where s on an empty line
            // goes on to insert, as vim's cl does.
            if (span.range.empty() && ch != 's')
            {
                clearPending();
                return true;
            }
            applyOperator(view, ch == 's' ? 'c' : 'd', span, 1);
            finishCommand(true);
            return true;
        }
        case 'D':
        case 'C':
        case 'Y':
        {
            if (ch != 'Y' && !editing)
            {
                clearPending();
                return true;
            }
            Span span;
            if (ch == 'Y')
            {
                // yy: a count past one from the last line fails it, as
                // the operator doubled fails (operatorKey).
                const S32 first = view.caret().line;
                if (count > 1 && first + 1 >= d.lineCount())
                {
                    mFailed = true;
                    clearPending();
                    return true;
                }
                const S32 last  = llmin(d.lineCount() - 1, first + count - 1);
                span.linewise   = true;
                span.range      = ALTextRange(d.lineStart(first), d.lineEnd(last));
            }
            else
            {
                // d$ and c$, through the line's end as $ goes.
                const S32 last = llmin(d.lineCount() - 1, view.caret().line + count - 1);
                span.range     = ALTextRange(view.caret(), d.lineEnd(last));
                span.inclusive = true;
            }
            applyOperator(view, ch == 'D' ? 'd' : ch == 'C' ? 'c' : 'y', span, 1);
            finishCommand(ch != 'Y');
            return true;
        }
        case 'S':
        {
            if (!editing)
            {
                clearPending();
                return true;
            }
            // cc: a count past one from the last line fails it, as the
            // operator doubled fails (operatorKey).
            Span      span;
            const S32 first = view.caret().line;
            if (count > 1 && first + 1 >= d.lineCount())
            {
                mFailed = true;
                clearPending();
                return true;
            }
            const S32 last  = llmin(d.lineCount() - 1, first + count - 1);
            span.linewise   = true;
            span.range      = ALTextRange(d.lineStart(first), d.lineEnd(last));
            applyOperator(view, 'c', span, 1);
            finishCommand(true);
            return true;
        }
        case 'p':
        case 'P':
            if (editing)
            {
                put(view, mRegister, ch == 'p', count);
                finishCommand(true);
            }
            else
            {
                clearPending();
            }
            return true;
        case 'J':
        {
            if (!editing)
            {
                clearPending();
                return true;
            }
            // Lines joined with one space, their leading blanks gone: as many
            // as the count says, read as typed, as vim's J reads it, and no
            // more than there are. A visual J hands its lines on as the
            // count, however many it has.
            const S32 first = view.caret().line;
            const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(d, first, first + llmax(1, mCount - 1), false);
            if (!join)
            {
                clearPending();
                return true;
            }
            view.apply(*join);
            moveTo(view, join->caret);
            finishCommand(true);
            return true;
        }
        case 'u':
            for (S32 n = 0; n < count; ++n)
            {
                view.perform(ALEditorCommand::Undo);
            }
            // Normal mode works at one caret, whatever the step brought
            // back.
            view.singleSelection();
            moveTo(view, view.caret());
            clearPending();
            return true;
        case '.':
        {
            if (mLastChange.empty() || mReplaying)
            {
                clearPending();
                return true;
            }
            const std::vector<Input> change = mLastChange;
            const S32                given  = mCount;
            clearPending();
            mReplaying = true;
            size_t     from = 0;
            if (mLastVisual.valid)
            {
                // As much again from the caret, selected as it was, then
                // the operator and what followed it.
                const ALTextPos from_here = view.caret();
                // A selection taken with $ to the end again -- a block to
                // every line's -- and no other, whatever went to a line's
                // end last.
                mWantColumn = mLastVisual.toEnd ? S32_MAX : -1;
                enterVisual(view, mLastVisual.mode);
                ALTextPos to = from_here;
                to.line      = llmin(d.lineCount() - 1, from_here.line + mLastVisual.lines);
                if (mLastVisual.mode == Mode::VisualBlock)
                {
                    // As many columns again as the reader counts them.
                    const S32 tab = view.getTabWidth();
                    to            = d.posAtDisplayColumn(to.line, d.displayColumn(from_here, tab) + mLastVisual.columns, tab);
                }
                else if (mLastVisual.mode == Mode::Visual)
                {
                    // As many characters again; where the line has fewer,
                    // or the selection was taken with $, to past its last,
                    // which takes its break.
                    to.column = (mLastVisual.lines == 0 ? from_here.column : 0) + mLastVisual.columns;
                    if (mLastVisual.toEnd || to.column > d.lineLength(to.line))
                    {
                        to = d.lineEnd(to.line);
                    }
                    else
                    {
                        to = d.prevCluster(d.clamp(to));
                        if (to < from_here)
                        {
                            to = from_here;
                        }
                    }
                }
                mVisualCaret = d.clamp(to);
                showVisual(view);
            }
            else if (given > 0)
            {
                // The count given takes the place of the ones recorded,
                // before a register's name and after it, and after the
                // operator: vim keeps an operator's count and its motion's
                // as one count of the command's. A count begins with a
                // digit other than 0, which is a motion: d0 keeps its 0.
                const auto past_count = [&change](size_t at) {
                    if (at < change.size() && change[at].isChar && isDigit(change[at].ch) && change[at].ch != '0')
                    {
                        while (at < change.size() && change[at].isChar && isDigit(change[at].ch))
                        {
                            ++at;
                        }
                    }
                    return at;
                };
                const auto is_char = [&change](size_t at, llwchar key) { return at < change.size() && change[at].isChar && change[at].ch == key; };
                from = past_count(from);
                for (const char digit : std::to_string(given))
                {
                    Input in;
                    in.isChar = true;
                    in.ch     = static_cast<llwchar>(digit);
                    feed(view, in);
                }
                while (from + 1 < change.size() && is_char(from, '"'))
                {
                    feed(view, change[from]);
                    feed(view, change[from + 1]);
                    from = past_count(from + 2);
                }
                // The operator -- d c y > < =, ys, or g~ gu gU gc -- and
                // past the count typed after it.
                size_t op_end = from;
                if (is_char(from, 'd') || is_char(from, 'c') || is_char(from, 'y') || is_char(from, '>') || is_char(from, '<') || is_char(from, '='))
                {
                    op_end = is_char(from, 'y') && is_char(from + 1, 's') ? from + 2 : from + 1;
                }
                else if (is_char(from, 'g') && (is_char(from + 1, '~') || is_char(from + 1, 'u') || is_char(from + 1, 'U') || is_char(from + 1, 'c')))
                {
                    op_end = from + 2;
                }
                if (op_end > from)
                {
                    for (; from < op_end; ++from)
                    {
                        feed(view, change[from]);
                    }
                    from = past_count(from);
                }
            }
            for (size_t i = from; i < change.size(); ++i)
            {
                feed(view, change[i]);
            }
            flushHeld(view);
            mReplaying = false;
            return true;
        }
        case '&':
        {
            // The last :s again on this line, without its flags.
            if (!editing)
            {
                clearPending();
                return true;
            }
            const S32 line = view.caret().line;
            clearPending();
            if (mEx->substitute(view, line, line, std::string()))
            {
                finishCommand(true);
            }
            return true;
        }
        case '~':
        {
            if (!editing)
            {
                clearPending();
                return true;
            }
            ALTextPos from = view.caret();
            ALTextPos to   = from;
            for (S32 n = 0; n < count && !atLineEnd(d, to); ++n)
            {
                to = d.nextCluster(to);
            }
            if (to == from)
            {
                clearPending();
                return true;
            }
            view.replaceAll({ { ALTextRange(from, to), recased(d.text(ALTextRange(from, to)), '~') } });
            moveTo(view, to);
            finishCommand(true);
            return true;
        }
        case 'v':
            enterVisual(view, Mode::Visual);
            clearPending();
            return true;
        case 'V':
            enterVisual(view, Mode::VisualLine);
            clearPending();
            return true;
        case ':':
            mCommandLine.kind  = ':';
            mCommandLine.historyAt = -1;
            mCommandLine.line      = visual ? std::string("'<,'>") : mCount > 0 ? std::string(".,.+") + std::to_string(mCount - 1) : std::string();
            mCommandLine.cursor = mCommandLine.line.size();
            if (visual)
            {
                // Let go of first, for '< '> to be the lines it was.
                leaveVisual(view);
            }
            setMode(view, Mode::Command);
            return true;
        case '/':
        case '?':
            // Over a visual selection, which stays: what is found extends
            // it (setMode).
            mCommandLine.kind  = ch;
            mCommandLine.historyAt = -1;
            mCommandLine.line.clear();
            mCommandLine.cursor = 0;
            setMode(view, Mode::Search);
            return true;
        case 'n':
        case 'N':
            if (mSearch.pattern.empty())
            {
                say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
            }
            else
            {
                mSearch.search(view, mSearch.pattern, ch == 'n' ? mSearch.forward : !mSearch.forward, count, mSearch.noSmartCase, mSearch.offset);
            }
            clearPending();
            return true;
        case '*':
        case '#':
            return starSearch(view, ch == '*', true);
        case 'R':
            if (editing)
            {
                enterInsert(view, count);
                setMode(view, Mode::Replace);
            }
            else
            {
                clearPending();
            }
            return true;
        case 0x16:  // control-V typed as a character on some platforms
            enterVisual(view, Mode::VisualBlock);
            clearPending();
            return true;
        default:
            return std::nullopt;
    }
}

bool ALVimKeymap::motionKey(ALTextView& view, llwchar ch)
{
    const S32             count   = countOr(mCount);
    // A motion on its own; one of vim's jumps notes where it began. One
    // that could not move failed, as vim has it.
    Motion m = motion(view, ch, count, 0);
    mFailed  = mFailed || (m.ok && !m.moved);
    if (m.ok)
    {
        const ALTextPos from = cursor(view);
        const bool      jump = ch == 'G' || ch == GO_TOP || ch == GO_BYTE || ch == '%' || ch == '(' || ch == ')' || ch == '{' || ch == '}' ||
                          ch == 'H' || ch == 'M' || ch == 'L';
        if (jump && m.to != from)
        {
            noteJump(view, from);
        }
        moveTo(view, m.to);
    }
    clearPending();
    return true;
}

// --- motions -----------------------------------------------------------------------------

ALVimKeymap::Motion ALVimKeymap::motion(ALTextView& view, llwchar ch, S32 count, llwchar arg)
{
    const ALTextDocument& d = view.document();
    const ALTextPos       from = cursor(view);
    // A count typed, before an operator or after it: G and gg go to its
    // line and | to its column, where without one G is the last line.
    const bool            counted = mCount > 0 || mOperatorCount > 0;
    Motion                m;
    m.ok = true;
    m.to = from;
    switch (ch)
    {
        case 'h':
            for (S32 n = 0; n < count && m.to.column > 0; ++n)
            {
                m.to = d.prevCluster(m.to);
            }
            m.moved = m.to != from;
            return m;
        case 'l':
        case ' ':
            for (S32 n = 0; n < count && !atLineEnd(d, m.to); ++n)
            {
                m.to = d.nextCluster(m.to);
            }
            m.moved = m.to != from;
            return m;
        case 'j':
        case 'k':
        case '+':
        case '-':
        case GO_TOP:
        case 'G':
        {
            S32 line = from.line;
            if (ch == 'j' || ch == '+')
            {
                line = llmin(d.lineCount() - 1, from.line + count);
            }
            else if (ch == 'k' || ch == '-')
            {
                line = llmax(0, from.line - count);
            }
            else if (ch == GO_TOP)
            {
                line = llclamp(counted ? count - 1 - view.lineNumberBase() : 0, 0, d.lineCount() - 1);
            }
            else
            {
                line = llclamp(counted ? count - 1 - view.lineNumberBase() : d.lineCount() - 1, 0, d.lineCount() - 1);
            }
            m.linewise = true;
            m.moved    = line != from.line || ch == GO_TOP || ch == 'G';
            if (ch == 'j' || ch == 'k')
            {
                // The column kept, by where it is drawn: the one wanted
                // since the last move that was not up or down, so that a
                // short line on the way loses nothing; past every end
                // after $.
                if (mWantColumn < 0)
                {
                    mWantColumn = d.displayColumn(from, view.getTabWidth());
                }
                mVerticalMove = true;
                m.to          = d.posAtDisplayColumn(line, mWantColumn, view.getTabWidth());
            }
            else
            {
                // + and - to the line's first non-blank; with no line to go
                // to, nowhere, as vim's fail without moving.
                m.to = m.moved ? ALTextPos(line, firstNonBlankColumn(d, line)) : from;
            }
            return m;
        }
        case DISPLAY_DOWN:
        case DISPLAY_UP:
        {
            // A row of the display at a time, by the layout's rows, at
            // the x the caret is drawn at. Out of rows before the count's:
            // as far as there are, and a failure all the same, as vim's
            // gj and gk are, an operator's with them. A fold's lines are
            // passed over through the layout's heights, not one by one.
            ALTextLayout& layout  = view.layout();
            S32           row     = 0;
            const F32     x       = layout.xOf(from.line, from.column, &row);
            S32           line    = from.line;
            bool          ran_out = false;
            for (S32 n = 0; n < count && !ran_out; ++n)
            {
                if (ch == DISPLAY_DOWN)
                {
                    if (row + 1 < layout.rowCount(line))
                    {
                        ++row;
                    }
                    else if (const S32 below = layout.visibleAfter(line); below < layout.lineCount())
                    {
                        line = below;
                        row  = 0;
                    }
                    else
                    {
                        ran_out = true;
                    }
                }
                else
                {
                    if (row > 0)
                    {
                        --row;
                    }
                    else if (const S32 above = layout.visibleBefore(line); above >= 0)
                    {
                        line = above;
                        row  = layout.rowCount(line) - 1;
                    }
                    else
                    {
                        ran_out = true;
                    }
                }
            }
            m.to    = d.clamp(ALTextPos(line, layout.columnAt(line, row, x, true)));
            m.moved = m.to != from && !ran_out;
            return m;
        }
        case DISPLAY_START:
        case DISPLAY_FIRST:
        case DISPLAY_MIDDLE:
        case DISPLAY_END:
        {
            // The screen line the caret is drawn on: its row of the layout,
            // one of a wrapped line's or an unwrapped line whole, as much of
            // it as is in sight across. g0 to its first character, g^ on
            // from there over blanks, gm to the one half the view's width
            // along, g$ to its last -- the count's rows down first, as gj
            // goes them, where a wrapped text running out of rows fails it
            // as vim's does. Never past a line's last character; exclusive,
            // but g$, which takes its character with it.
            ALTextLayout& layout  = view.layout();
            const bool    wrapped = view.getWordWrap();
            const F32     left    = wrapped ? 0.f : view.scrollX();
            const F32     width   = static_cast<F32>(llmax(1, view.textRect().getWidth()));
            S32           line    = from.line;
            S32           row     = layout.rowOf(line, from.column);
            for (S32 n = 1; ch == DISPLAY_END && n < count; ++n)
            {
                if (row + 1 < layout.rowCount(line))
                {
                    ++row;
                }
                else if (const S32 below = layout.visibleAfter(line); below < layout.lineCount())
                {
                    line = below;
                    row  = 0;
                }
                else
                {
                    m.moved = !wrapped;
                    break;
                }
            }
            // A wrapped row's last character is the last it holds, a blank
            // that hangs past the edge as well.
            const F32 last_x = wrapped ? F32_MAX : left + width - 1.f;
            const F32 x      = ch == DISPLAY_END ? last_x : ch == DISPLAY_MIDDLE ? left + width / 2.f : left;
            m.to             = d.clamp(ALTextPos(line, layout.columnAt(line, row, x, false)));
            if (atLineEnd(d, m.to))
            {
                m.to = lastCharOf(d, line);
            }
            if (ch == DISPLAY_FIRST)
            {
                while ((at(d, m.to) == ' ' || at(d, m.to) == '\t') && !atLineEnd(d, d.nextCluster(m.to)))
                {
                    m.to = d.nextCluster(m.to);
                }
            }
            // An empty line has no character to take.
            m.inclusive = ch == DISPLAY_END && !atLineEnd(d, m.to);
            // A count's g$ over wrapped rows wants every line's end from here
            // on for j and k, as $ does and vim's curswant has it, whether or
            // not it ran out of rows; one row's wants its own column, as does
            // one an operator took.
            if (ch == DISPLAY_END && wrapped && count > 1 && !mOperator)
            {
                mWantColumn   = S32_MAX;
                mVerticalMove = true;
            }
            return m;
        }
        case LINE_MIDDLE:
        {
            // gM: half the line's text along, as the reader counts its
            // columns, or the count's percent of it, a hundred at most; its
            // last character where that is past it. Exclusive.
            const S32 tab   = view.getTabWidth();
            const S32 width = d.displayColumn(d.lineEnd(from.line), tab);
            m.to            = d.posAtDisplayColumn(from.line, counted && count <= 100 ? width * count / 100 : width / 2, tab);
            if (atLineEnd(d, m.to))
            {
                m.to = lastCharOf(d, from.line);
            }
            return m;
        }
        case GO_BYTE:
        {
            // go: the count's byte of the text, the first without one, each
            // line's break a byte; on the character it is part of, the one
            // before a break, and the last past the end. Exclusive. The
            // counts as typed, the operator's times the motion's, not held to
            // what a count does: a byte of a long text may be past it.
            const S64 typed = static_cast<S64>(llmax(1, mOperatorCount)) * static_cast<S64>(llmax(1, mCount));
            const S64 byte  = llmin(typed, static_cast<S64>(MAX_COUNT_TYPED));
            m.to = d.clamp(d.posAt(static_cast<size_t>(byte - 1)));
            if (atLineEnd(d, m.to))
            {
                m.to = lastCharOf(d, m.to.line);
            }
            return m;
        }
        case '0':
            m.to = d.lineStart(from.line);
            return m;
        case '^':
            m.to = ALTextPos(from.line, firstNonBlankColumn(d, from.line));
            return m;
        case '$':
        {
            const S32 line = llmin(d.lineCount() - 1, from.line + count - 1);
            m.to           = d.lineEnd(line);
            m.inclusive    = false;
            // To the line's end, taken with the last character: an
            // operator reaches the end, the caret sits on the last -- a
            // visual one past it, on the line's break, which the selection
            // takes, as vim's does. And every line's end from here on, for
            // j and k -- but not once an operator has taken it, which
            // forgets the column as vim's does, so that j after d$ keeps
            // the caret's.
            if (!mOperator && !isVisual() && m.to.column > 0)
            {
                m.to = d.prevCluster(m.to);
            }
            if (!mOperator)
            {
                mWantColumn   = S32_MAX;
                mVerticalMove = true;
            }
            return m;
        }
        case '|':
            m.to = d.clamp(ALTextPos(from.line, counted ? count - 1 : 0));
            return m;
        case 'w':
        case 'W':
        {
            const bool big = ch == 'W';
            for (S32 n = 0; n < count; ++n)
            {
                // Under an operator the last word moved over ends at its
                // line's end, as vim's does: dw on a line's last word leaves
                // the line break, where d2w from there goes on to take the
                // next line's first word. A step that starts at a line's
                // end -- an empty line's -- takes the break.
                const bool      last  = mOperator != 0 && n + 1 == count;
                const ALTextPos start = m.to;
                const S32       cls   = classAt(d, m.to, big);
                if (cls != 0)
                {
                    while (!atLineEnd(d, m.to) && classAt(d, m.to, big) == cls)
                    {
                        m.to = d.nextCluster(m.to);
                    }
                }
                // Over the blanks, onto the next line if need be; an empty
                // line is a word of its own.
                while (true)
                {
                    if (atLineEnd(d, m.to))
                    {
                        if (m.to.line + 1 >= d.lineCount() || (last && m.to != start))
                        {
                            break;
                        }
                        m.to = ALTextPos(m.to.line + 1, 0);
                        if (last || d.lineLength(m.to.line) == 0)
                        {
                            break;
                        }
                        continue;
                    }
                    if (classAt(d, m.to, big) != 0)
                    {
                        break;
                    }
                    m.to = d.nextCluster(m.to);
                }
            }
            m.moved = m.to != from;
            return m;
        }
        case 'e':
        case 'E':
        {
            const bool big = ch == 'E';
            for (S32 n = 0; n < count; ++n)
            {
                if (!stepOn(d, m.to))
                {
                    break;
                }
                while (classAt(d, m.to, big) == 0 && stepOn(d, m.to)) {}
                const S32 cls = classAt(d, m.to, big);
                ALTextPos next = m.to;
                while (!atLineEnd(d, next) && classAt(d, next, big) == cls)
                {
                    m.to = next;
                    next = d.nextCluster(next);
                }
            }
            m.inclusive = true;
            m.moved     = m.to != from;
            return m;
        }
        case LAST_NON_BLANK:
        {
            // The last character on the line that is not a blank, the count
            // lines on; inclusive, as $ is.
            const S32          line = llmin(d.lineCount() - 1, from.line + count - 1);
            const std::string& text = d.line(line);
            const size_t       last = text.find_last_not_of(" \t");
            m.to                    = ALTextPos(line, last == std::string::npos ? 0 : static_cast<S32>(last));
            m.inclusive             = true;
            return m;
        }
        case WORD_END_BACK:
        case BIG_WORD_END_BACK:
        {
            // The end of the previous word, taken with it: out of the
            // word the caret is in, back over the blanks, onto the last
            // character of the one before.
            const bool big = ch == BIG_WORD_END_BACK;
            for (S32 n = 0; n < count; ++n)
            {
                const S32 cls = classAt(d, m.to, big);
                if (!stepBack(d, m.to))
                {
                    break;
                }
                if (cls != 0)
                {
                    while (classAt(d, m.to, big) == cls && stepBack(d, m.to)) {}
                }
                while (classAt(d, m.to, big) == 0 && stepBack(d, m.to)) {}
            }
            m.inclusive = true;
            m.moved     = m.to != from;
            return m;
        }
        case 'b':
        case 'B':
        {
            const bool big = ch == 'B';
            for (S32 n = 0; n < count; ++n)
            {
                if (!stepBack(d, m.to))
                {
                    break;
                }
                while (classAt(d, m.to, big) == 0 && stepBack(d, m.to)) {}
                const S32 cls = classAt(d, m.to, big);
                while (m.to.column > 0 && classAt(d, d.prevCluster(m.to), big) == cls)
                {
                    m.to = d.prevCluster(m.to);
                }
            }
            m.moved = m.to != from;
            return m;
        }
        case '}':
        case '{':
        {
            S32 line = from.line;
            for (S32 n = 0; n < count; ++n)
            {
                const S32 step = ch == '}' ? 1 : -1;
                // Off the blank lines first, then to the next blank.
                while (line + step >= 0 && line + step < d.lineCount() && lineBlank(d, line))
                {
                    line += step;
                }
                while (line + step >= 0 && line + step < d.lineCount() && !lineBlank(d, line + step))
                {
                    line += step;
                }
                if (line + step >= 0 && line + step < d.lineCount())
                {
                    line += step;
                }
            }
            m.to    = lineBlank(d, line) ? d.lineStart(line) : (ch == '}' ? d.lineEnd(line) : d.lineStart(line));
            m.moved = m.to != from;
            return m;
        }
        case '(':
        case ')':
        {
            // The count's sentence on, or back (findSentence), exclusive; one
            // past the text's sentences fails. Outside a visual mode never
            // left past a line's last character, but on it, which it takes.
            const std::optional<ALTextPos> to = findSentence(d, from, ch == ')', count);
            if (!to)
            {
                m.moved = false;
                return m;
            }
            m.to = *to;
            if (!isVisual() && m.to.column > 0 && atLineEnd(d, m.to))
            {
                m.to        = d.prevCluster(m.to);
                m.inclusive = true;
            }
            return m;
        }
        case '%':
        {
            // The bracket under the caret or the first after it on the line.
            ALTextPos p = from;
            while (!atLineEnd(d, p) && !isBracket(at(d, p)))
            {
                p = d.nextCluster(p);
            }
            ALTextPos match;
            if (atLineEnd(d, p) || !matchBracketIn(view, p, match))
            {
                m.moved = false;
                return m;
            }
            m.to        = match;
            m.inclusive = true;
            return m;
        }
        case 'f':
        case 'F':
        case 't':
        case 'T':
        case ';':
        case ',':
        {
            llwchar want    = arg;
            bool    forward = ch == 'f' || ch == 't';
            bool    till    = ch == 't' || ch == 'T';
            if (ch == ';' || ch == ',')
            {
                if (!mFindChar)
                {
                    m.moved = false;
                    return m;
                }
                want    = mFindChar;
                forward = ch == ';' ? mFindForward : !mFindForward;
                till    = mFindTill;
            }
            else
            {
                mFindChar    = want;
                mFindForward = forward;
                mFindTill    = till;
            }
            const std::string needle = utf8Of(want);
            const std::string& line  = d.line(from.line);
            const S32          size  = static_cast<S32>(line.size());
            S32                col   = from.column;
            // A character on or back along the line, however many bytes it
            // takes, and never off the line.
            const auto after  = [&](S32 at) { return at < size ? d.nextCluster(ALTextPos(from.line, at)).column : size; };
            const auto before = [&](S32 at) { return at > 0 ? d.prevCluster(ALTextPos(from.line, at)).column : 0; };
            // A t or T typed to the character beside the caret stops where it
            // is, so an operator takes the character under it. Said again by
            // ; or , it goes past that character instead, as vim's does
            // without ; in 'cpoptions', and then only for a count of one.
            const bool past_beside = till && (ch == ';' || ch == ',') && count == 1;
            for (S32 n = 0; n < count; ++n)
            {
                size_t found;
                if (forward)
                {
                    // From the character after the caret's, or after the one
                    // after it.
                    S32 start = after(col);
                    if (past_beside && n == 0)
                    {
                        start = after(start);
                    }
                    found = start < size ? line.find(needle, static_cast<size_t>(start)) : std::string::npos;
                }
                else
                {
                    // Before the caret's character, or before the one before.
                    S32 end = col;
                    if (past_beside && n == 0)
                    {
                        end = before(end);
                    }
                    found = end > 0 ? line.rfind(needle, static_cast<size_t>(end - 1)) : std::string::npos;
                }
                if (found == std::string::npos)
                {
                    m.moved = false;
                    return m;
                }
                col = static_cast<S32>(found);
            }
            if (till)
            {
                // Beside it: on the character before it, or after it.
                col = forward ? before(col) : after(col);
            }
            m.to        = ALTextPos(from.line, col);
            m.inclusive = forward;
            return m;
        }
        case 'H':
        case 'M':
        case 'L':
        {
            const S32 first = view.firstVisibleLine();
            const S32 last  = view.lastVisibleLine();
            S32       line  = ch == 'H' ? llmin(last, first + count - 1) : ch == 'L' ? llmax(first, last - count + 1) : (first + last) / 2;
            line            = llclamp(line, 0, d.lineCount() - 1);
            m.to            = ALTextPos(line, firstNonBlankColumn(d, line));
            m.linewise      = true;
            return m;
        }
        case '`':
        case '\'':
        {
            // . the last change's place, as the change list has it.
            const auto mark = mMarks.find(static_cast<char>(arg));
            if (mark == mMarks.end() && !(arg == '.' && !view.changes().empty()))
            {
                say(said("VimMarkNotSet", "E20: Mark not set"), true);
                m.moved = false;
                return m;
            }
            const ALTextPos p = d.clamp(mark != mMarks.end() ? mark->second : view.changes().back());
            m.to              = ch == '\'' ? ALTextPos(p.line, firstNonBlankColumn(d, p.line)) : p;
            m.linewise        = ch == '\'';
            return m;
        }
        case 'n':
        case 'N':
            m.ok = false;
            return m;
        default:
            m.ok = false;
            return m;
    }
}

// --- text objects ----------------------------------------------------------------------

bool ALVimKeymap::textObject(ALTextView& view, llwchar kind, llwchar what, S32 count, Span& out)
{
    const ALTextDocument& d      = view.document();
    const ALTextPos       from   = cursor(view);
    const bool            around = kind == 'a';
    out                          = Span();
    switch (what)
    {
        case 'w':
        case 'W':
        {
            // vim's word object (wordObject) under an operator: its two ends
            // in order, through the last character, though not the line
            // break after a line's end it stops at, so that iw on an empty
            // line before the last is nothing at all; one that ends at a
            // line's start ends as an exclusive motion there does.
            const std::optional<WordObject> word = wordObject(d, from, around, what == 'W', count);
            if (!word)
            {
                return false;
            }
            const ALTextPos first = std::min(word->start, word->end);
            const ALTextPos last  = std::max(word->start, word->end);
            out.range             = ALTextRange(first, word->inclusive && !atLineEnd(d, last) ? d.nextCluster(last) : last);
            out.inclusive         = word->inclusive;
            if (!word->inclusive)
            {
                adjustExclusiveEnd(d, out.range, out.linewise);
            }
            return true;
        }
        case '"':
        case '\'':
        case '`':
        {
            // vim's current_quote() with nothing selected, on the caret's line:
            // on a quote, the pair it is one of, counted from the line's start;
            // else from the last quote before the caret -- which may be the one
            // that closes the quoted text before it, so that between two of
            // them it is what lies between -- or, with none before, from the
            // first after it, to the next quote. A quote after a backslash
            // closes nothing, as 'quoteescape' has it. i" is inside the quotes,
            // and a count of two or more takes them as well; a" takes them and
            // the blanks after, or where none follow, the blanks before.
            const std::string& line      = d.line(from.line);
            const char         quote     = static_cast<char>(what);
            const S32          size      = static_cast<S32>(line.size());
            S32                col_start = llmin(from.column, size);
            S32                col_end   = -1;
            if (col_start < size && line[col_start] == quote)
            {
                const S32 first = col_start;
                col_start       = 0;
                while (true)
                {
                    col_start = nextQuote(line, col_start, quote, false);
                    if (col_start < 0 || col_start > first)
                    {
                        return false;
                    }
                    col_end = nextQuote(line, col_start + 1, quote, true);
                    if (col_end < 0)
                    {
                        return false;
                    }
                    if (first <= col_end)
                    {
                        break;
                    }
                    col_start = col_end + 1;
                }
            }
            else
            {
                col_start = prevQuote(line, col_start, quote, true);
                if (col_start >= size || line[col_start] != quote)
                {
                    col_start = nextQuote(line, col_start, quote, false);
                    if (col_start < 0)
                    {
                        return false;
                    }
                }
                col_end = nextQuote(line, col_start + 1, quote, true);
                if (col_end < 0)
                {
                    return false;
                }
            }
            if (around)
            {
                const auto blank = [&line, size](S32 col) { return col >= 0 && col < size && (line[col] == ' ' || line[col] == '\t'); };
                if (blank(col_end + 1))
                {
                    while (blank(col_end + 1))
                    {
                        ++col_end;
                    }
                }
                else
                {
                    while (col_start > 0 && blank(col_start - 1))
                    {
                        --col_start;
                    }
                }
            }
            const bool quotes = around || count > 1;
            out.range         = ALTextRange(ALTextPos(from.line, quotes ? col_start : col_start + 1), ALTextPos(from.line, quotes ? col_end + 1 : col_end));
            return true;
        }
        case '(':
        case ')':
        case 'b':
        case '[':
        case ']':
        case '{':
        case '}':
        case 'B':
        case '<':
        case '>':
        {
            // The block around the caret, or after it (blockObject): a( its
            // brackets and what they hold. i( through the character before
            // the closer; where the closer is alone on its line, up to that
            // line's start, as an exclusive motion goes there -- the lines
            // between whole where the block starts a line; nothing where the
            // brackets hold nothing.
            const char                       opener = objectOpener(what);
            const std::optional<BlockObject> block  = blockObject(d, opener != '<' ? &bracketsOf(view) : nullptr, opener, around, count, from, from, false);
            if (!block)
            {
                return false;
            }
            ALTextPos end = block->end;
            if (block->sol && !around)
            {
                inclPos(d, end);
                out.range = ALTextRange(block->start, std::max(block->start, end));
                adjustExclusiveEnd(d, out.range, out.linewise);
            }
            else if (block->start <= end)
            {
                out.range = ALTextRange(block->start, atLineEnd(d, end) ? end : d.nextCluster(end));
            }
            else
            {
                out.range = ALTextRange(block->start, block->start);
            }
            return true;
        }
        case 't':
        {
            // The tag around the caret, the count of them out, of every tag
            // in the text paired with its closing one (tagPairs): the
            // innermost pair around the caret is the first, its parent the
            // second.
            const size_t               here  = d.offsetOf(from);
            const std::vector<TagPair> pairs = tagPairs(d.wholeText());
            // The pairs around the caret, innermost first: the inner of
            // two nested pairs starts later.
            std::vector<const TagPair*> around_caret;
            for (const TagPair& tag : pairs)
            {
                if (tag.openBegin <= here && here < tag.closeEnd)
                {
                    around_caret.push_back(&tag);
                }
            }
            std::sort(around_caret.begin(), around_caret.end(), [](const TagPair* a, const TagPair* b) { return a->openBegin > b->openBegin; });
            const size_t level = static_cast<size_t>(llmax(1, count)) - 1;
            if (level >= around_caret.size())
            {
                return false;
            }
            const TagPair& tag = *around_caret[level];
            out.range          = around ? ALTextRange(d.posAt(tag.openBegin), d.posAt(tag.closeEnd)) : ALTextRange(d.posAt(tag.openEnd), d.posAt(tag.closeBegin));
            return true;
        }
        case 's':
        {
            // The sentences or the blanks between them from the caret's, the
            // count of them (sentenceObject): through the last character, or
            // up to the line's break after it as an exclusive motion goes
            // there, which takes the lines whole where they start a line. An
            // end past a line's last character is put back onto it, as vim's
            // cursor is after an object, and the stretch runs from the earlier
            // end to the later, through the later where inclusive: so on the
            // ] of a last line's .] the object, which starts past the ], takes
            // the ] alone.
            const SentenceObject sentence = sentenceObject(d, from, count, around, nullptr);
            ALTextPos            end      = sentence.end;
            if (end.column > 0 && atLineEnd(d, end))
            {
                end = d.prevCluster(end);
            }
            out.range = ALTextRange(sentence.start, end).normalised();
            if (sentence.inclusive && !atLineEnd(d, out.range.end))
            {
                out.range.end = d.nextCluster(out.range.end);
            }
            out.inclusive = sentence.inclusive;
            if (!sentence.inclusive)
            {
                adjustExclusiveEnd(d, out.range, out.linewise);
            }
            return true;
        }
        case 'p':
        {
            // The paragraph or the blank lines the caret is on, the count of
            // them on (paragraphObject), lines whole.
            S32 first = 0;
            S32 last  = 0;
            if (!paragraphObject(d, from.line, around, count, first, last))
            {
                return false;
            }
            out.linewise = true;
            out.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
            return true;
        }
        case 'f':
        {
            // A function, as the host knows them: af its lines whole, if the
            // lines of its body -- not a { alone under its header, nor what
            // closes it. A count, the ones around it.
            ALVimHost* host = view.vimHost();
            std::optional<ALTextRange> fn = host ? host->functionAround(ALTextRange(from, from)) : std::nullopt;
            for (S32 n = 1; n < count && fn; ++n)
            {
                if (const std::optional<ALTextRange> outer = host->functionAround(*fn))
                {
                    fn = outer;
                }
            }
            if (!fn)
            {
                return false;
            }
            S32 first = fn->begin.line;
            S32 last  = fn->end.line;
            if (!around)
            {
                ++first;
                std::string brace = first <= last ? d.line(first) : std::string();
                LLStringUtil::trim(brace);
                if (d.line(fn->begin.line).find('{') == std::string::npos && brace == "{")
                {
                    ++first;
                }
                --last;
                if (first > last)
                {
                    return false;
                }
            }
            out.linewise = true;
            out.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
            return true;
        }
        default:
            return false;
    }
}

std::optional<bool> ALVimKeymap::visualObject(ALTextView& view, llwchar kind, llwchar what, S32 count)
{
    const ALTextDocument& d      = view.document();
    const ALTextPos       anchor = d.clamp(mVisualAnchor);
    const ALTextPos       caret  = d.clamp(mVisualCaret);
    const ALTextPos       low    = std::min(anchor, caret);
    const ALTextPos       high   = std::max(anchor, caret);
    const bool            around = kind == 'a';
    switch (what)
    {
        case 'w':
        case 'W':
        {
            if (anchor == caret)
            {
                return std::nullopt;
            }
            // The caret on by the count's words, or back by them where it is
            // before the anchor (wordObject); lines become characters.
            const std::optional<WordObject> word =
                wordObject(d, caret, around, what == 'W', count, caret < anchor ? WordsFrom::Back : WordsFrom::Forward);
            if (!word)
            {
                return false;
            }
            mVisualCaret = word->end;
            if (mMode == Mode::VisualLine)
            {
                setMode(view, Mode::Visual);
            }
            break;
        }
        case '"':
        case '\'':
        case '`':
        {
            if (anchor == caret)
            {
                return std::nullopt;
            }
            // On one line alone (quoteOn); lines become characters.
            const std::optional<std::pair<S32, S32>> taken =
                anchor.line == caret.line ? quoteOn(d.line(caret.line), anchor.column, caret.column, static_cast<char>(what), around, count) : std::nullopt;
            if (!taken)
            {
                return false;
            }
            mVisualAnchor = ALTextPos(caret.line, taken->first);
            mVisualCaret  = ALTextPos(caret.line, taken->second);
            if (mMode == Mode::VisualLine)
            {
                setMode(view, Mode::Visual);
            }
            break;
        }
        case '(':
        case ')':
        case 'b':
        case '[':
        case ']':
        case '{':
        case '}':
        case 'B':
        case '<':
        case '>':
        {
            // The block around the selection, from its start, or the next
            // block out where it holds no more than the selection does -- of
            // one character, too, so that i( on () fails (blockObject). Where
            // the closer is alone on its line, the selection takes the break
            // after the last character as well.
            const char                       opener = objectOpener(what);
            const std::optional<BlockObject> block  = blockObject(d, opener != '<' ? &bracketsOf(view) : nullptr, opener, around, count, anchor, caret, true);
            if (!block)
            {
                return false;
            }
            ALTextPos last = block->end;
            if (block->sol && !atLineEnd(d, last))
            {
                incPos(d, last);
            }
            mVisualAnchor = block->start;
            mVisualCaret  = last;
            setMode(view, Mode::Visual);
            break;
        }
        case 't':
        {
            if (anchor == caret)
            {
                return std::nullopt;
            }
            // The tags open before the selection's start and not closed
            // before it, innermost first (tagPairs): the count's, or the
            // first out from it that does not close before the selection's
            // end.
            const size_t               lo    = d.offsetOf(low);
            const size_t               hi    = d.offsetOf(high);
            const std::vector<TagPair> pairs = tagPairs(d.wholeText());
            std::vector<const TagPair*> open_before;
            for (const TagPair& tag : pairs)
            {
                if (tag.openBegin < lo && tag.closeBegin >= lo)
                {
                    open_before.push_back(&tag);
                }
            }
            std::sort(open_before.begin(), open_before.end(), [](const TagPair* a, const TagPair* b) { return a->openBegin > b->openBegin; });
            size_t level = static_cast<size_t>(llmax(1, count)) - 1;
            while (level < open_before.size() && open_before[level]->closeBegin < hi)
            {
                ++level;
            }
            if (level >= open_before.size())
            {
                return false;
            }
            // Through the closing tag's >; or what the tags hold, and where
            // the selection held just that already, the tags as well.
            const TagPair& tag   = *open_before[level];
            size_t         first = tag.openBegin;
            size_t         last  = tag.closeEnd - 1;
            if (!around && !(tag.openEnd == lo && tag.closeBegin == hi + 1))
            {
                first = tag.openEnd;
                last  = tag.closeBegin - 1;
            }
            // Nothing between the tags: the character after the first.
            mVisualAnchor = d.posAt(first);
            mVisualCaret  = last < first ? mVisualAnchor : d.posAt(last);
            setMode(view, Mode::Visual);
            break;
        }
        case 'p':
        {
            // A selection of one line is a paragraph afresh (paragraphObject)
            // -- but by lines, where that would start at the caret's line,
            // vim takes it on from there instead.
            S32 line = caret.line;
            if (anchor.line == caret.line)
            {
                S32 first = 0;
                S32 last  = 0;
                if (!paragraphObject(d, line, around, count, first, last))
                {
                    return false;
                }
                if (mMode != Mode::VisualLine || first != line)
                {
                    return std::nullopt;
                }
            }
            // On from the caret's line, or back where it is above the
            // anchor's, by the count's paragraphs or runs of blank lines --
            // ap's with the run after each -- the mode as it was. As far as
            // the text goes, where it ends first, and false.
            const S32 step = caret.line < anchor.line ? -1 : 1;
            const S32 edge = step < 0 ? 0 : d.lineCount() - 1;
            bool      ok   = true;
            for (S32 n = 0; n < count; ++n)
            {
                if (line == edge)
                {
                    ok = false;
                    break;
                }
                S32 was = -1;
                for (S32 t = 0; t < 2; ++t)
                {
                    line += step;
                    const S32 blank = lineBlank(d, line) ? 1 : 0;
                    if (blank == was)
                    {
                        line -= step;
                        break;
                    }
                    // A paragraph also ends where another begins with no blank
                    // line between (startsParagraph).
                    while (line != edge && (lineBlank(d, line + step) ? 1 : 0) == blank && (blank || !startsParagraph(d, step > 0 ? line + 1 : line)))
                    {
                        line += step;
                    }
                    if (!around || line == edge)
                    {
                        break;
                    }
                    was = blank;
                }
            }
            mVisualCaret = ALTextPos(line, 0);
            showVisual(view);
            return ok;
        }
        case 's':
        {
            // The sentence object over a selection of any size
            // (sentenceObject): chosen afresh from one character, which makes
            // the mode characters, else the selection taken on.
            const SentenceObject sentence = sentenceObject(d, caret, count, around, &anchor);
            mVisualAnchor                 = sentence.start;
            mVisualCaret                  = sentence.end;
            if (sentence.afresh)
            {
                setMode(view, Mode::Visual);
            }
            break;
        }
        default:
            return std::nullopt;
    }
    showVisual(view);
    return true;
}

// --- operators ---------------------------------------------------------------------------

void ALVimKeymap::applyOperator(ALTextView& view, llwchar op, const Span& span_in, S32 count)
{
    const ALTextDocument& d    = view.document();
    Span                  span = span_in;
    span.range                 = span.range.normalised();
    const S32 first            = span.range.begin.line;
    const S32 last             = span.range.end.line;
    const bool editing         = !view.isReadOnly();

    if (op == SURROUND_OPERATOR)
    {
        // ys: the stretch kept for the character that surrounds it, which
        // comes next; the command goes on until it does.
        clearPending();
        mSurroundSpan    = span;
        mPending         = SURROUND_WITH;
        mSurroundWaiting = true;
        return;
    }
    if (op == COMMENT_OPERATOR)
    {
        // Every line the span touches -- not one an exclusive motion only
        // reached the start of -- as Toggle Comment does them, one step.
        const S32 through = !span.linewise && !span.block && span.range.end.column == 0 && last > first ? last - 1 : last;
        if (editing)
        {
            view.setSelection(ALTextRange(d.lineStart(first), d.lineEnd(through)));
            view.perform(ALEditorCommand::ToggleComment);
        }
        moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
        return;
    }
    // A delete of characters over more than one line with nothing but
    // blanks before it on its first line and after it on its last is those
    // lines whole, as vim's is: 2daw over a line's words and the next's,
    // d2e from a line's start to the next line's end, d% from an opening
    // bracket that begins its line. A change, a yank and a visual selection
    // keep their characters.
    if (op == 'd' && !span.linewise && !span.block && !span.visual && last > first &&
        span.range.begin.column <= firstNonBlankColumn(d, first) &&
        d.line(last).find_first_not_of(" \t", static_cast<size_t>(span.range.end.column)) == std::string::npos)
    {
        span.linewise = true;
    }

    // The text the operator works on: whole lines, a block's columns
    // line by line, or the stretch as it is.
    std::vector<ALTextRange> pieces;
    if (span.block)
    {
        pieces = blockPieces(view, span);
    }
    else if (span.linewise)
    {
        pieces.emplace_back(d.lineStart(first), d.lineEnd(last));
    }
    else
    {
        pieces.push_back(span.range);
    }
    // A block's a line each, a line short of it as blanks (blockText).
    std::string text = span.block ? blockText(d, pieces, span.left, span.right, span.toEnd, view.getTabWidth()) : d.text(pieces.front());

    switch (op)
    {
        case 'y':
            store(mRegister, text, span.linewise, span.block, true);
            // How many lines the yank was over, said for more than vim's
            // report -- whole lines, a block's, or the lines characters
            // reach, a last line's break reaching the line after it -- with
            // the register named, as vim's says it; nothing for _.
            if (last - first + 1 > REPORT_THRESHOLD && mRegister != '_')
            {
                say(yankedSaid(last - first + 1, span.block, mRegister));
            }
            if (span.linewise)
            {
                moveTo(view, ALTextPos(first, view.caret().line == first ? view.caret().column : firstNonBlankColumn(d, first)));
            }
            else
            {
                moveTo(view, span.range.begin);
            }
            return;
        case 'd':
        case 'c':
        {
            if (!editing)
            {
                return;
            }
            // Nothing to take out -- D or d$ on an empty line, s on one --
            // puts nothing in a register either, as vim's delete has it:
            // what the registers and the clipboard hold stays. c still
            // goes on to insert over its empty stretch, as vim's change
            // does, and keeps an empty line's end it took inclusively --
            // C, c$ or ciw there -- as an empty register.
            const bool nothing = !span.linewise && !span.block && span.range.empty();
            if (nothing && op == 'd')
            {
                return;
            }
            if (!nothing || span.inclusive)
            {
                store(mRegister, text, span.linewise, span.block, false, span.registerOne);
            }
            if (op == 'c')
            {
                view.undoJournal().beginGroup();
            }
            if (span.block)
            {
                std::vector<std::pair<ALTextRange, std::string>> edits;
                for (const ALTextRange& piece : pieces)
                {
                    edits.emplace_back(piece, std::string());
                }
                view.replaceAll(std::move(edits));
                view.setCaret(d.clamp(pieces.front().begin));
            }
            else if (span.linewise)
            {
                if (op == 'c')
                {
                    // The lines' text goes; the first line's indentation
                    // stays for what is typed.
                    view.deleteRange(ALTextRange(ALTextPos(first, firstNonBlankColumn(d, first)), d.lineEnd(last)));
                    view.setCaret(d.lineEnd(first));
                }
                else
                {
                    ALTextRange whole;
                    if (last + 1 < d.lineCount())
                    {
                        whole = ALTextRange(d.lineStart(first), d.lineStart(last + 1));
                    }
                    else if (first > 0)
                    {
                        whole = ALTextRange(d.lineEnd(first - 1), d.lineEnd(last));
                    }
                    else
                    {
                        whole = ALTextRange(d.lineStart(first), d.lineEnd(last));
                    }
                    if (mEx->globalBatch)
                    {
                        mEx->globalBatch->edits.emplace_back(whole, std::string());
                        mEx->globalBatch->landing      = whole.begin;
                        mEx->globalBatch->landingBelow = 0;
                        mEx->globalBatch->landed       = true;
                        mEx->globalBatch->deletedLines += last - first + 1;
                        return;
                    }
                    view.deleteRange(whole);
                    const S32 line = llmin(first, d.lineCount() - 1);
                    view.setCaret(ALTextPos(line, firstNonBlankColumn(d, line)));
                    if (last - first + 1 > REPORT_THRESHOLD)
                    {
                        say(alSaidCount("VimFewerLines", last - first + 1, "1 fewer line", "[COUNT] fewer lines"));
                    }
                }
            }
            else
            {
                view.deleteRange(span.range);
                view.setCaret(span.range.begin);
            }
            if (op == 'c')
            {
                if (span.block && last > first)
                {
                    mBlockInsert = true;
                    mBlockFirst  = first;
                    mBlockLast   = last;
                    mBlockColumn = span.left;
                    mBlockAppend = false;
                }
                enterInsert(view, 1, true);
                // The group closes when insert mode is left.
            }
            else
            {
                moveTo(view, view.caret());
            }
            return;
        }
        case '>':
        case '<':
        {
            if (!editing)
            {
                return;
            }
            // Each line the count's levels further in, or back, as the
            // editor's own indent does (ALTextIndent::shiftLines): a tab, or
            // the tab's width in spaces where tabs are soft; an empty line
            // is left alone going in, as vim leaves it.
            ALTextIndent::Options options;
            options.tabWidth                   = llmax(1, view.getTabWidth());
            options.softTabs                   = view.getSoftTabs();
            if (op == '>' && count > 1)
            {
                // The count's levels on each line are text the count makes,
                // held to the most a count may make, as a put's are.
                size_t lines = 0;
                for (S32 l = first; l <= last && l < d.lineCount(); ++l)
                {
                    lines += d.line(l).empty() ? 0u : 1u;
                }
                const size_t level = options.softTabs ? static_cast<size_t>(options.tabWidth) : static_cast<size_t>(1);
                const size_t bytes = lines * static_cast<size_t>(count) * level;
                if (bytes > MAX_COUNT_TEXT)
                {
                    tooMuch(bytes);
                    return;
                }
            }
            const ALTextEditing::Change shifted = ALTextIndent::shiftLines(d, first, last, llmax(1, count), op == '>', options);
            std::vector<std::pair<ALTextRange, std::string>> edits;
            edits.reserve(shifted.replacements.size());
            for (const ALTextEditing::Replacement& replacement : shifted.replacements)
            {
                edits.emplace_back(replacement.range, replacement.text);
            }
            if (mEx->globalBatch)
            {
                for (auto& edit : edits)
                {
                    mEx->globalBatch->edits.push_back(std::move(edit));
                }
                mEx->globalBatch->landing      = ALTextPos(first, 0);
                mEx->globalBatch->landingBelow = 0;
                mEx->globalBatch->landed       = true;
                return;
            }
            if (!edits.empty())
            {
                view.replaceAll(std::move(edits));
            }
            moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
            if (last - first + 1 > REPORT_THRESHOLD)
            {
                say(alSaidCount(op == '>' ? "VimLinesShiftedRight" : "VimLinesShiftedLeft", last - first + 1, op == '>' ? "1 line >ed 1 time" : "1 line <ed 1 time",
                                op == '>' ? "[COUNT] lines >ed 1 time" : "[COUNT] lines <ed 1 time"));
            }
            return;
        }
        case '=':
            if (mHooks.format)
            {
                mHooks.format(view, first, last);
            }
            moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
            return;
        case '~':
        case 'u':
        case 'U':
        {
            if (!editing)
            {
                return;
            }
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const ALTextRange& piece : pieces)
            {
                edits.emplace_back(piece, recased(d.text(piece), op));
            }
            view.replaceAll(std::move(edits));
            moveTo(view, span.range.begin);
            return;
        }
        default:
            return;
    }
}

// --- registers --------------------------------------------------------------------------

void ALVimKeymap::store(char name, std::string text, bool linewise, bool block, bool yanked, bool register_one)
{
    mShared->registers.store(name, std::move(text), linewise, block, yanked, mShared->unnamedClipboard, register_one);
}

ALVimKeymap::Register ALVimKeymap::fetch(char name) const
{
    return mShared->registers.fetch(name, mShared->unnamedClipboard);
}

void ALVimKeymap::tooMuch(size_t bytes)
{
    say(said("VimCountTooLarge", "Too large a count: it would put in [SIZE,number,1] MB", { { "[SIZE]", fmt::format("{:f}", static_cast<F64>(bytes) / (1024.0 * 1024.0)) } }),
        true);
}

void ALVimKeymap::put(ALTextView& view, char name, bool after, S32 count, bool past)
{
    const ALTextDocument& d   = view.document();
    const Register        reg = fetch(name);
    // Nothing is a register never set. One set to no text -- yiw on an
    // empty line keeps one -- puts nothing and says nothing, as vim's does,
    // where a line is something however empty, as yy on an empty line keeps
    // one. What _ gives back is no text, put as nothing and said nothing of.
    if (!reg.held)
    {
        if (name != '_')
        {
            say(said("VimNothingInRegister", "E353: Nothing in register [REGISTER]", { { "[REGISTER]", std::string(1, name ? name : '"') } }), true);
        }
        return;
    }
    if (reg.text.empty() && !reg.linewise)
    {
        return;
    }
    if (count > 1 && reg.text.size() * static_cast<size_t>(count) > MAX_COUNT_TEXT)
    {
        tooMuch(reg.text.size() * static_cast<size_t>(count));
        return;
    }
    const ALTextPos from = view.caret();
    if (reg.block)
    {
        // Each line of the block onto a line of its own, from the column.
        std::vector<std::string> lines;
        size_t                   at_ = 0;
        while (true)
        {
            const size_t nl = reg.text.find('\n', at_);
            lines.push_back(reg.text.substr(at_, nl == std::string::npos ? std::string::npos : nl - at_));
            if (nl == std::string::npos)
            {
                break;
            }
            at_ = nl + 1;
        }
        // The column as the reader counts them, before the character
        // under the caret or past it: the bytes before it differ from line
        // to line.
        const S32 tab    = view.getTabWidth();
        const S32 column = after && !atLineEnd(d, from) ? d.displayColumn(d.nextCluster(from), tab) : d.displayColumn(from, tab);
        std::vector<std::pair<ALTextRange, std::string>> edits;
        std::string                                       tail;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const S32 line = from.line + static_cast<S32>(i);
            std::string piece;
            for (S32 n = 0; n < count; ++n)
            {
                piece += lines[i];
            }
            if (line < d.lineCount())
            {
                const ALTextPos where = d.posAtDisplayColumn(line, column, tab);
                edits.emplace_back(ALTextRange(where, where), padTo(d, line, column, tab) + piece);
            }
            else
            {
                tail += "\n" + std::string(static_cast<size_t>(column), ' ') + piece;
            }
        }
        if (!tail.empty())
        {
            edits.emplace_back(ALTextRange(d.end(), d.end()), tail);
        }
        view.replaceAll(std::move(edits));
        moveTo(view, d.posAtDisplayColumn(from.line, column, tab));
        return;
    }
    std::string text;
    text.reserve((reg.text.size() + 1) * static_cast<size_t>(count));
    for (S32 n = 0; n < count; ++n)
    {
        if (reg.linewise && n > 0)
        {
            text += '\n';
        }
        text += reg.text;
    }
    if (reg.linewise)
    {
        if (after)
        {
            view.setCaret(d.lineEnd(from.line));
            view.insertText("\n" + text);
        }
        else
        {
            view.setCaret(d.lineStart(from.line));
            view.insertText(text + "\n");
        }
        const S32 line = after ? from.line + 1 : from.line;
        if (past)
        {
            // The line after the last one put.
            const S32 below = llmin(line + static_cast<S32>(std::count(text.begin(), text.end(), '\n')) + 1, d.lineCount() - 1);
            moveTo(view, ALTextPos(below, 0));
            return;
        }
        moveTo(view, ALTextPos(line, firstNonBlankColumn(d, line)));
        return;
    }
    ALTextPos where = from;
    if (after && !atLineEnd(d, from))
    {
        where = d.nextCluster(from);
    }
    view.setCaret(where);
    view.insertText(text);
    // On the last character put, as vim leaves it; gp just after it.
    moveTo(view, past ? view.caret() : text.find('\n') == std::string::npos ? d.prevCluster(view.caret()) : where);
}

// --- modes ------------------------------------------------------------------------------

void ALVimKeymap::setMode(ALTextView& view, Mode to, bool grouped)
{
    const Mode from = mMode;
    if (to == from)
    {
        return;
    }
    const auto visual_mode = [](Mode which) { return which == Mode::Visual || which == Mode::VisualLine || which == Mode::VisualBlock; };
    const auto insert_mode = [](Mode which) { return which == Mode::Insert || which == Mode::Replace; };
    // The selection held before and after: a visual mode's own, or the one
    // the search line is typed over -- the one it was opened from.
    const Mode held = visual_mode(from) ? from : from == Mode::Search ? mSearchVisual : Mode::Normal;
    const Mode kept = visual_mode(to) ? to : to == Mode::Search ? held : Mode::Normal;
    if (held != Mode::Normal && kept == Mode::Normal)
    {
        // Let go of: what gv selects again and '< '> stand for.
        mVisualLast       = held;
        mVisualLastAnchor = mVisualAnchor;
        mVisualLastCaret  = mVisualCaret;
        mVisualLastToEnd  = mWantColumn == S32_MAX;
    }
    if (held == Mode::VisualBlock && kept != Mode::VisualBlock)
    {
        if (ALVimHost* host = view.vimHost())
        {
            host->clearLayer(ALVimHost::Layer::Block);
        }
    }
    mSearchVisual = to == Mode::Search ? kept : Mode::Normal;
    if (from == Mode::Search)
    {
        mSearch.endIncremental(view);
    }
    if (insert_mode(from) && !insert_mode(to))
    {
        view.undoJournal().endGroup();
    }
    mMode = to;
    if (insert_mode(to) && !insert_mode(from))
    {
        if (!grouped)
        {
            view.undoJournal().beginGroup();
        }
        // Ctrl-O's wait over, whether its command is done or inserts itself
        // -- o, A, cw -- which is then the insert it went back to: Escape
        // ends it, as vim's does.
        mOneCommand = 0;
    }
    bump();
}

// --- insert mode ------------------------------------------------------------------------

void ALVimKeymap::enterInsert(ALTextView& view, S32 count, bool grouped)
{
    setMode(view, Mode::Insert, grouped);
    mInsertCount = llmax(1, count);
    mWantColumn  = -1;
    mInsertStart = view.caret();
    mInsertMoved = false;
    mInsertOpened = false;
    mInsertRegister = false;
    mCount       = 0;
    mRegister    = 0;
    mOperator    = 0;
    mOperatorCount = 0;
    mPending     = 0;
    mObjectKind  = 0;
    bump();
}

void ALVimKeymap::leaveInsert(ALTextView& view)
{
    // Vim has one caret: the others typing went in at go, before a count
    // or a block puts in again at the main one.
    view.singleSelection();
    const ALTextDocument& d = view.document();
    // What was typed, read off the text. Nothing to type again where the
    // caret was moved off it and nothing has been typed since, as vim has
    // it after an arrow: what came before is the last insert's already.
    const bool        moved = mInsertMoved && view.caret() != mInsertLeftAt;
    const std::string typed = moved ? std::string() : typedText(view);
    if (!moved)
    {
        mLastTyped = typed;
    }
    // Again as many times as the count said: made once and put in as one
    // edit, not an edit a time. A line o or O opened is opened again for
    // each, at the indent it was given, as vim types a line break before
    // each -- with nothing typed on it too.
    const bool opened = mInsertOpened && !moved;
    if (mInsertCount > 1 && (!typed.empty() || opened))
    {
        const ALTextPos   start = d.clamp(mInsertStart);
        const std::string each  = opened ? "\n" + d.text(ALTextRange(d.lineStart(start.line), start)) + typed : typed;
        const size_t      size  = each.size() * static_cast<size_t>(mInsertCount - 1);
        if (size > MAX_COUNT_TEXT)
        {
            tooMuch(size);
        }
        else
        {
            std::string again;
            again.reserve(size);
            for (S32 n = 1; n < mInsertCount; ++n)
            {
                again += each;
            }
            view.insertText(again);
        }
    }
    // And onto every other line of a block, at its column as the reader
    // counts them: I's before it, a line that stops short of it left
    // alone; A's past what the block holds of the line, a line short of
    // it padded out to its edge, or past the line's end for a block taken
    // with $.
    if (mBlockInsert && !typed.empty() && typed.find('\n') == std::string::npos)
    {
        const S32                                        tab = view.getTabWidth();
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (S32 line = mBlockFirst + 1; line <= llmin(mBlockLast, d.lineCount() - 1); ++line)
        {
            if (mBlockAppend)
            {
                const ALTextPos where = blockPiece(d, line, mBlockColumn, mBlockColumn, mBlockToEnd, tab).end;
                edits.emplace_back(ALTextRange(where, where), (mBlockToEnd ? std::string() : padTo(d, line, mBlockColumn + 1, tab)) + typed);
                continue;
            }
            if (d.displayColumn(d.lineEnd(line), tab) < mBlockColumn)
            {
                continue;
            }
            const ALTextPos where = d.posAtDisplayColumn(line, mBlockColumn, tab);
            edits.emplace_back(ALTextRange(where, where), typed);
        }
        if (!edits.empty())
        {
            const ALTextPos keep = view.caret();
            view.replaceAll(std::move(edits));
            view.setCaret(d.clamp(keep));
        }
    }
    mBlockInsert    = false;
    mInsertRegister = false;
    mLiteral        = false;
    mLiteralCode.clear();
    mInsertMoved    = false;
    setMode(view, Mode::Normal);
    // Where inserting stopped, for gi to go back to: vim's ^ mark.
    mMarks['^'] = view.caret();
    // The caret steps back onto the last character typed.
    const ALTextPos caret = view.caret();
    moveTo(view, caret.column > 0 ? d.prevCluster(caret) : caret);
    finishCommand(true);
    bump();
}

void ALVimKeymap::typeIn(ALTextView& view, const std::string& text)
{
    if (!text.empty())
    {
        view.insertText(text);
    }
}

std::string ALVimKeymap::typedText(const ALTextView& view) const
{
    const ALTextDocument& d     = view.document();
    const ALTextPos       start = d.clamp(mInsertStart);
    return start < view.caret() ? d.text(ALTextRange(start, view.caret())) : std::string();
}

void ALVimKeymap::typingLeft(const ALTextView& view, bool gone)
{
    if (mInsertMoved)
    {
        return;
    }
    // Where the caret was: a key that was to move it and did not leaves
    // it typing on. One the mouse moved already was somewhere else.
    mInsertMoved  = true;
    mInsertLeftAt = gone ? ALTextPos(-1, -1) : view.caret();
    if (!gone)
    {
        mLastTyped = typedText(view);
    }
}

void ALVimKeymap::restartInsert(ALTextView& view)
{
    mInsertMoved = false;
    mInsertStart = view.caret();
    // Typed on where the caret went, not on a line of its own; a block's
    // other lines no longer line up with it.
    mInsertOpened = false;
    mBlockInsert  = false;
    // What `.` repeats is an i, and the key being fed.
    if (!mReplaying && !mCommandInputs.empty())
    {
        const Input now = mCommandInputs.back();
        mCommandInputs.assign({ Input::character('i'), now });
        mVisualPending.valid = false;
    }
}

bool ALVimKeymap::insertControl(ALTextView& view, const Input& input)
{
    const ALTextDocument& d = view.document();
    // A key vim gives its own meaning, as the key it stands for: Control-H
    // is Backspace, Control-J and Control-M Return, Control-I Tab.
    const auto as = [&](KEY key) {
        Input plain;
        plain.key = key;
        if (!insert(view, plain))
        {
            const ALEditorCommand command = view.keymap().lookup(key, MASK_NONE);
            if (command != ALEditorCommand::None)
            {
                view.perform(command);
            }
        }
        return true;
    };
    switch (input.key)
    {
        case 'O':
            // One command of normal mode, then inserting again; the
            // command may take the caret off what was typed.
            typingLeft(view);
            setMode(view, Mode::Normal);
            mOneCommand = 2;
            bump();
            return true;
        case 'V':
            mLiteral = true;
            mLiteralCode.clear();
            return true;
#if LL_DARWIN
        case 'S':
            // What the call being typed takes, as Neovim's Control-S shows
            // it: on a Mac, where Command-S saves and Control-S is free.
            view.perform(ALEditorCommand::SignatureHelp);
            return true;
#endif
        case 'W':
            view.perform(ALEditorCommand::DeleteWordLeft);
            return true;
        case 'U':
            view.deleteRange(ALTextRange(d.lineStart(view.caret().line), view.caret()));
            return true;
        case 'H': return as(KEY_BACKSPACE);
        case 'J':
        case 'M': return as(KEY_RETURN);
        case 'I': return as(KEY_TAB);
        case 'T':
        case 'D':
        {
            // The line a step further in, or a step back, as > and < step
            // it, the caret staying on the character it was on. In goes an
            // empty line too, as vim's Ctrl-T puts one in where o opened it;
            // out is <'s own step (ALTextIndent::shiftLines).
            const ALTextPos caret = view.caret();
            const S32       width = llmax(1, view.getTabWidth());
            ALTextRange     range(d.lineStart(caret.line), d.lineStart(caret.line));
            std::string     with;
            if (input.key == 'T')
            {
                with = view.getSoftTabs() ? std::string(width, ' ') : std::string("\t");
            }
            else
            {
                ALTextIndent::Options options;
                options.tabWidth                   = width;
                options.softTabs                   = view.getSoftTabs();
                const ALTextEditing::Change shifted = ALTextIndent::shiftLines(d, caret.line, caret.line, 1, false, options);
                if (shifted.replacements.empty())
                {
                    return true;
                }
                range = shifted.replacements.front().range;
            }
            const S32 moved = static_cast<S32>(with.size()) - (range.end.column - range.begin.column);
            view.replaceAll({ { range, with } });
            view.setCaret(d.clamp(ALTextPos(caret.line, llmax(0, caret.column + moved))));
            return true;
        }
        case 'N':
        case 'P':
            // The completions of the word being typed, which these walk
            // once they are up. Not when played back: the list is for
            // somebody to choose from.
            if (!mReplaying && mPlaying == 0)
            {
                view.perform(ALEditorCommand::Complete);
            }
            return true;
        case 'A':
            typeIn(view, mLastTyped);
            return true;
        case 'R':
            mInsertRegister = true;
            return true;
        case 'E':
        case 'Y':
        {
            // The character below the caret, or above it, where it is drawn.
            const ALTextPos caret = view.caret();
            const S32       line  = caret.line + (input.key == 'E' ? 1 : -1);
            if (line < 0 || line >= d.lineCount())
            {
                return true;
            }
            const S32       column = d.displayColumn(caret, view.getTabWidth());
            const ALTextPos from   = d.posAtDisplayColumn(line, column, view.getTabWidth());
            if (!atLineEnd(d, from) && d.displayColumn(from, view.getTabWidth()) == column)
            {
                typeIn(view, d.text(ALTextRange(from, d.nextCluster(from))));
            }
            return true;
        }
        default:
            return false;
    }
}

bool ALVimKeymap::insert(ALTextView& view, const Input& input)
{
    const ALTextDocument& d       = view.document();
    const bool            ctrl    = !input.isChar && (input.mask & CONTROL) != 0;
    const bool            leaving = !input.isChar && (input.key == KEY_ESCAPE || (ctrl && (input.key == '[' || input.key == 'C')));
    // An arrow, Home, End or a page: the caret moves and nothing is typed.
    const bool moving = !input.isChar && (input.key == KEY_LEFT || input.key == KEY_RIGHT || input.key == KEY_UP || input.key == KEY_DOWN ||
                                          input.key == KEY_HOME || input.key == KEY_END || input.key == KEY_PAGE_UP || input.key == KEY_PAGE_DOWN);
    // Anything else after the caret was moved off what was typed is an
    // insert of its own from where it is; nothing, where the key that was
    // to move it did not.
    if (mInsertMoved && !leaving && !moving)
    {
        if (view.caret() != mInsertLeftAt)
        {
            restartInsert(view);
        }
        else
        {
            mInsertMoved = false;
        }
    }
    if (mLiteral)
    {
        // After Ctrl-V: a tab as a tab, whatever the tabs are set to; a
        // character as it is, nothing typed along with it; u and four hex
        // digits the character by its code.
        if (!input.isChar)
        {
            if (input.key == KEY_TAB)
            {
                mLiteral = false;
                typeIn(view, "\t");
                return true;
            }
            const bool plain = !(input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)) && input.key >= 0x20 && input.key < KEY_SPECIAL;
            if (plain || input.key == KEY_SHIFT || input.key == KEY_CONTROL || input.key == KEY_ALT)
            {
                return false;
            }
            mLiteral = false;
            mLiteralCode.clear();
            return true;
        }
        const bool hex = input.ch < 0x80 && isxdigit(static_cast<int>(input.ch));
        if (mLiteralCode.empty() && input.ch == 'u')
        {
            mLiteralCode = "u";
            return true;
        }
        if (!mLiteralCode.empty() && hex)
        {
            mLiteralCode += static_cast<char>(input.ch);
            if (mLiteralCode.size() == 5)
            {
                mLiteral = false;
                const llwchar code = static_cast<llwchar>(std::strtoul(mLiteralCode.c_str() + 1, nullptr, 16));
                mLiteralCode.clear();
                typeIn(view, utf8Of(code));
            }
            return true;
        }
        mLiteral = false;
        const std::string before = mLiteralCode;
        mLiteralCode.clear();
        typeIn(view, before + utf8Of(input.ch));
        return true;
    }
    if (mInsertRegister)
    {
        // The register after a Control-R: its text typed in. The key a
        // character comes with, and one that only holds a modifier, wait
        // for it; anything else lets the Control-R go.
        if (!input.isChar)
        {
            const bool plain = !(input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)) && input.key >= 0x20 && input.key < KEY_SPECIAL;
            if (plain || input.key == KEY_SHIFT || input.key == KEY_CONTROL || input.key == KEY_ALT)
            {
                return false;
            }
            mInsertRegister = false;
            return true;
        }
        mInsertRegister = false;
        const char name = input.ch < 0x80 ? static_cast<char>(input.ch) : 0;
        if (name == '.')
        {
            typeIn(view, mLastTyped);
        }
        else if (isalnum(static_cast<unsigned char>(name)) || name == '"' || name == '-' || name == '+' || name == '*')
        {
            typeIn(view, fetch(name).text);
        }
        return true;
    }
    if (!input.isChar)
    {
        if (leaving)
        {
            leaveInsert(view);
            return true;
        }
        if (ctrl && !(input.mask & (MASK_ALT | MASK_SHIFT)) && insertControl(view, input))
        {
            return true;
        }
        if (moving)
        {
            typingLeft(view);
        }
        if (mReplaying || mPlaying > 0 || mMapped > 0)
        {
            // Nobody else will: the view's own keymap does what the key
            // would have done when it was typed.
            const ALEditorCommand command = view.keymap().lookup(input.key, input.mask);
            if (command != ALEditorCommand::None)
            {
                view.perform(command);
            }
            return true;
        }
        // The rest is the view's: arrows, return, backspace, the keymap.
        // What changes the text is part of what was typed all the same,
        // for `.` and a macro to type again, which they do above.
        mTypedByView = input.key == KEY_RETURN || input.key == KEY_TAB || input.key == KEY_BACKSPACE || input.key == KEY_DELETE;
        return false;
    }
    if (mMode == Mode::Replace)
    {
        // Over the character under each caret, where there is one.
        const auto over = [&d](const ALTextPos& from) { return atLineEnd(d, from) ? ALTextRange(from, from) : ALTextRange(from, d.nextCluster(from)); };
        if (view.hasOtherSelections())
        {
            std::vector<ALTextRange> others;
            for (const ALTextRange& one : view.otherSelections())
            {
                others.push_back(over(one.end));
            }
            view.setSelections(over(view.caret()), std::move(others));
        }
        else
        {
            view.setSelection(over(view.caret()));
        }
        view.typeText(utf8Of(input.ch));
        return true;
    }
    if (mReplaying || mPlaying > 0 || mMapped > 0)
    {
        // Fed by hand -- `.`, a macro, :normal, a mapping -- so nobody
        // else will: held, with those that follow it, to go in as one.
        mHeldTyped += utf8Of(input.ch);
        return true;
    }
    // The view puts the character in.
    return false;
}

// --- surround ---------------------------------------------------------------------------

void ALVimKeymap::surround(ALTextView& view, const Span& span, const std::string& open, const std::string& close)
{
    const ALTextDocument&                            d     = view.document();
    const ALTextRange                                range = span.range.normalised();
    std::vector<std::pair<ALTextRange, std::string>> edits;
    ALTextPos                                        caret = range.begin;
    const auto at_ = [&edits](const ALTextPos& where, const std::string& text) { edits.emplace_back(ALTextRange(where, where), text); };
    if (span.linewise)
    {
        // Lines: the pair on lines of their own round them, at the first
        // one's indent, without the spaces an opening bracket's has.
        std::string o = open;
        std::string c = close;
        LLStringUtil::trim(o);
        LLStringUtil::trim(c);
        const S32         first  = range.begin.line;
        const S32         last   = llmax(first, range.end.line);
        const std::string indent = indentOf(d, first);
        at_(d.lineStart(first), indent + o + "\n");
        at_(d.lineEnd(last), "\n" + indent + c);
        caret = ALTextPos(first, static_cast<S32>(indent.size()));
    }
    else if (span.block)
    {
        // A block: what it holds of each of its lines surrounded, a line
        // that stops short of it left alone.
        for (const ALTextRange& piece : blockPieces(view, span))
        {
            if (!piece.empty())
            {
                at_(piece.begin, open);
                at_(piece.end, close);
            }
        }
    }
    else if (range.begin == range.end)
    {
        at_(range.begin, open + close);
    }
    else
    {
        at_(range.begin, open);
        at_(range.end, close);
    }
    if (edits.empty())
    {
        return;
    }
    view.replaceAll(std::move(edits));
    moveTo(view, caret);
}

bool ALVimKeymap::changeSurround(ALTextView& view, llwchar target, llwchar with, S32 count)
{
    const ALTextDocument& d = view.document();
    std::string           open_text;
    std::string           close_text;
    if (with != 0 && !surroundPair(with, open_text, close_text))
    {
        return false;
    }
    // The two ends of the pair: a bracket's, the count out; a tag's; a
    // quote's on the line; or any other mark's, the nearest on the line
    // either side of the caret.
    const llwchar object = target == 'r' ? '[' : target == 'a' ? '<' : target;
    ALTextRange   open;
    ALTextRange   close;
    Span          outer;
    Span          inner;
    if (object == '(' || object == ')' || object == 'b' || object == '[' || object == ']' || object == '{' || object == '}' ||
        object == 'B' || object == '<' || object == '>')
    {
        if (!textObject(view, 'a', object, count, outer))
        {
            return false;
        }
        open  = ALTextRange(outer.range.begin, d.nextCluster(outer.range.begin));
        close = ALTextRange(d.prevCluster(outer.range.end), outer.range.end);
    }
    else if (object == 't')
    {
        if (!textObject(view, 'a', 't', count, outer) || !textObject(view, 'i', 't', count, inner))
        {
            return false;
        }
        open  = ALTextRange(outer.range.begin, inner.range.begin);
        close = ALTextRange(inner.range.end, outer.range.end);
    }
    else if (object == '"' || object == '\'' || object == '`')
    {
        if (!textObject(view, 'i', object, 1, inner))
        {
            return false;
        }
        open  = ALTextRange(d.prevCluster(inner.range.begin), inner.range.begin);
        close = ALTextRange(inner.range.end, d.nextCluster(inner.range.end));
    }
    else if (object >= 0x21 && object < 0x7F && !std::isalnum(static_cast<int>(object)))
    {
        const ALTextPos    caret = cursor(view);
        const std::string& line  = d.line(caret.line);
        const char         mark  = static_cast<char>(object);
        size_t             left  = line.rfind(mark, static_cast<size_t>(caret.column));
        if (left == std::string::npos)
        {
            return false;
        }
        size_t right = line.find(mark, left + 1);
        if (right == std::string::npos)
        {
            // The caret on the closing one.
            right = left;
            left  = left > 0 ? line.rfind(mark, left - 1) : std::string::npos;
            if (left == std::string::npos)
            {
                return false;
            }
        }
        open  = ALTextRange(ALTextPos(caret.line, static_cast<S32>(left)), ALTextPos(caret.line, static_cast<S32>(left) + 1));
        close = ALTextRange(ALTextPos(caret.line, static_cast<S32>(right)), ALTextPos(caret.line, static_cast<S32>(right) + 1));
    }
    else
    {
        return false;
    }
    if (target == '(' || target == '[' || target == '{')
    {
        // An opening bracket takes the blanks inside the pair with it, as
        // surround.vim's does.
        const auto blank = [&d](const ALTextPos& p) {
            const char c = at(d, p);
            return c == ' ' || c == '\t';
        };
        while (open.end < close.begin && open.end.column < d.lineLength(open.end.line) && blank(open.end))
        {
            ++open.end.column;
        }
        while (close.begin > open.end && close.begin.column > 0 && blank(ALTextPos(close.begin.line, close.begin.column - 1)))
        {
            --close.begin.column;
        }
    }
    view.replaceAll({ { open, open_text }, { close, close_text } });
    moveTo(view, open.begin);
    return true;
}

// --- visual mode ------------------------------------------------------------------------

void ALVimKeymap::enterVisual(ALTextView& view, Mode which)
{
    if (mMode == Mode::Normal)
    {
        mVisualAnchor = view.caret();
        mVisualCaret  = view.caret();
    }
    setMode(view, which);
    showVisual(view);
    bump();
}

void ALVimKeymap::leaveVisual(ALTextView& view)
{
    // Normal mode at the visual caret; the selection kept for gv, and a
    // block's columns put out (setMode).
    const ALTextPos caret = cursor(view);
    setMode(view, Mode::Normal);
    view.setCaret(caret);
    moveTo(view, caret);
    bump();
}

ALVimKeymap::Span ALVimKeymap::visualSpan(const ALTextView& view, bool line_break) const
{
    const ALTextDocument& d     = view.document();
    const ALTextPos       caret = d.clamp(mVisualCaret);
    const ALTextPos       a     = std::min(mVisualAnchor, caret);
    const ALTextPos       b     = std::max(mVisualAnchor, caret);
    Span                  span;
    if (mMode == Mode::VisualLine)
    {
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(a.line), d.lineEnd(b.line));
    }
    else if (mMode == Mode::VisualBlock)
    {
        // To every line's end where the caret went there with $, and has
        // gone up and down since, as vim's curswant has it.
        const S32 tab = view.getTabWidth();
        span.block    = true;
        span.toEnd    = mWantColumn == S32_MAX;
        blockColumns(d, d.clamp(mVisualAnchor), caret, tab, span.left, span.right);
        span.range = ALTextRange(blockPiece(d, a.line, span.left, span.right, span.toEnd, tab).begin,
                                 blockPiece(d, b.line, span.left, span.right, span.toEnd, tab).end);
    }
    else
    {
        // The character at either end; past a line's last, its break, for
        // an operator that takes one -- on to the next line's start, where
        // there is a next line.
        span.range = ALTextRange(a, line_break || !atLineEnd(d, b) ? d.nextCluster(b) : b);
    }
    span.visual = true;
    return span;
}

std::vector<ALTextRange> ALVimKeymap::blockPieces(const ALTextView& view, const Span& span) const
{
    const ALTextDocument&    d     = view.document();
    const ALTextRange        lines = span.range.normalised();
    std::vector<ALTextRange> pieces;
    pieces.reserve(static_cast<size_t>(llmax(0, lines.end.line - lines.begin.line + 1)));
    for (S32 line = lines.begin.line; line <= lines.end.line; ++line)
    {
        pieces.push_back(blockPiece(d, line, span.left, span.right, span.toEnd, view.getTabWidth()));
    }
    return pieces;
}

void ALVimKeymap::showVisual(ALTextView& view)
{
    // The selection as the view draws it: from the anchor's side to the
    // caret's, the character under each end included.
    const ALTextDocument& d     = view.document();
    const ALTextPos       caret = d.clamp(mVisualCaret);
    if (mMode == Mode::VisualLine)
    {
        const S32 a = llmin(mVisualAnchor.line, caret.line);
        const S32 b = llmax(mVisualAnchor.line, caret.line);
        if (caret.line >= mVisualAnchor.line)
        {
            view.setSelection(ALTextRange(d.lineStart(a), d.lineEnd(b)));
        }
        else
        {
            view.setSelection(ALTextRange(d.lineEnd(b), d.lineStart(a)));
        }
    }
    else if (mMode == Mode::VisualBlock)
    {
        // The rows between, each lit over what the block holds of it
        // where the view can; the selection itself the corners.
        if (ALVimHost* host = view.vimHost())
        {
            host->setLayer(ALVimHost::Layer::Block, blockPieces(view, visualSpan(view)));
        }
        view.setCaret(caret);
    }
    else
    {
        if (caret >= mVisualAnchor)
        {
            view.setSelection(ALTextRange(mVisualAnchor, atLineEnd(d, caret) ? caret : d.nextCluster(caret)));
        }
        else
        {
            view.setSelection(ALTextRange(atLineEnd(d, mVisualAnchor) ? mVisualAnchor : d.nextCluster(mVisualAnchor), caret));
        }
    }
    if (mMode != Mode::VisualBlock)
    {
        if (ALVimHost* host = view.vimHost())
        {
            host->clearLayer(ALVimHost::Layer::Block);
        }
    }
}

// --- searching --------------------------------------------------------------------------

// --- the : and / lines --------------------------------------------------------------------

bool ALVimKeymap::menu(std::vector<std::string>& items, S32& chosen) const
{
    if (mMode != Mode::Command || mCommandLine.completion.items.size() < 2)
    {
        return false;
    }
    items  = mCommandLine.completion.items;
    chosen = mCommandLine.completion.at;
    return true;
}

void ALVimKeymap::takeLine(ALTextView& view, llwchar kind, const std::string& text, bool run)
{
    // Handed over whenever the window is done, whatever vim is doing then,
    // which ends as its own keys would end it: an insert as Escape does,
    // the asking :s as q does, and a visual selection let go of for a :
    // line as : lets it go -- a search line goes over it, as / does.
    if (inserting())
    {
        leaveInsert(view);
    }
    else if (mMode == Mode::Confirm)
    {
        mEx->endConfirming(view);
    }
    else if (kind == ':' && isVisual())
    {
        leaveVisual(view);
    }
    clearPending();
    mCommandLine.completion = ALVimCommandLine::Completion();
    mCommandLine.kind   = kind;
    mCommandLine.line       = text;
    mCommandLine.cursor = mCommandLine.line.size();
    mCommandLine.historyAt  = -1;
    mCommandLine.historyPrefix.clear();
    setMode(view, kind == ':' ? Mode::Command : Mode::Search);
    if (run)
    {
        // As Enter on the line: vim's window runs the row it is pressed
        // on. The line is still on the : history, to be recalled and
        // edited with Up.
        Input as_key;
        as_key.key = KEY_RETURN;
        mCommandLine.commandLine(view, as_key);
    }
    bump();
}

void ALVimKeymap::share(std::shared_ptr<Shared> shared)
{
    if (shared)
    {
        mShared    = std::move(shared);
        mCommandLine.historyAt = -1;
    }
}

// --- :s asking about each match -------------------------------------------------

bool ALVimKeymap::addToNumber(ALTextView& view, S64 by)
{
    const ALTextDocument& d     = view.document();
    const ALTextPos       caret = view.caret();
    const std::string&    text  = d.line(caret.line);
    // The numbers on the line, as vim's nrformats=bin,hex reads them:
    // 0x1f and 0b101 as what they are, anything else in decimal; the
    // one under the caret, or the first after it, is the one changed.
    enum class Base : U8
    {
        Decimal,
        Hex,
        Binary
    };
    struct Number
    {
        size_t begin = 0;
        size_t end   = 0;
        Base   base  = Base::Decimal;
    };
    auto is_digit = [&](size_t i) { return i < text.size() && text[i] >= '0' && text[i] <= '9'; };
    auto is_hex   = [&](size_t i) { return i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i])); };
    auto is_bin   = [&](size_t i) { return i < text.size() && (text[i] == '0' || text[i] == '1'); };
    std::optional<Number> found;
    for (size_t i = 0; i < text.size() && !found;)
    {
        if (!is_digit(i))
        {
            ++i;
            continue;
        }
        Number number;
        number.begin = i;
        if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X') && is_hex(i + 2))
        {
            number.base = Base::Hex;
            i += 2;
            while (is_hex(i))
            {
                ++i;
            }
        }
        else if (text[i] == '0' && (text[i + 1] == 'b' || text[i + 1] == 'B') && is_bin(i + 2))
        {
            number.base = Base::Binary;
            i += 2;
            while (is_bin(i))
            {
                ++i;
            }
        }
        else
        {
            while (is_digit(i))
            {
                ++i;
            }
        }
        number.end = i;
        if (number.end > static_cast<size_t>(caret.column))
        {
            found = number;
        }
    }
    if (!found)
    {
        return false;
    }
    Number      number = *found;
    std::string with;
    if (number.base == Base::Decimal)
    {
        // A minus right before the digits is the number's; leading
        // zeros keep the number's width.
        const bool        negative = number.begin > 0 && text[number.begin - 1] == '-';
        const std::string digits   = text.substr(number.begin, number.end - number.begin);
        if (digits.size() > 18)
        {
            return false;
        }
        S64 value = std::strtoll(digits.c_str(), nullptr, 10);
        if (negative)
        {
            --number.begin;
            value = -value;
        }
        value += by;
        with = std::to_string(std::llabs(value));
        if (digits.size() > 1 && digits[0] == '0' && with.size() < digits.size())
        {
            with.insert(0, digits.size() - with.size(), '0');
        }
        if (value < 0)
        {
            with.insert(0, "-");
        }
    }
    else
    {
        // Unsigned, wrapping, the width kept, hex in the case its
        // letters were in.
        const std::string digits = text.substr(number.begin + 2, number.end - number.begin - 2);
        if (digits.size() > (number.base == Base::Hex ? 16u : 64u))
        {
            return false;
        }
        U64 value = std::strtoull(digits.c_str(), nullptr, number.base == Base::Hex ? 16 : 2);
        value += static_cast<U64>(by);
        std::string spelt;
        if (number.base == Base::Hex)
        {
            const bool upper = std::any_of(digits.begin(), digits.end(), [](char c) { return c >= 'A' && c <= 'F'; });
            char       buffer[24];
            snprintf(buffer, sizeof(buffer), upper ? "%llX" : "%llx", static_cast<unsigned long long>(value));
            spelt = buffer;
        }
        else
        {
            for (U64 v = value; v > 0 || spelt.empty(); v >>= 1)
            {
                spelt.insert(spelt.begin(), static_cast<char>('0' + (v & 1)));
            }
        }
        if (spelt.size() < digits.size())
        {
            spelt.insert(0, digits.size() - spelt.size(), '0');
        }
        with = text.substr(number.begin, 2) + spelt;
    }
    view.setSelection(ALTextRange(ALTextPos(caret.line, static_cast<S32>(number.begin)), ALTextPos(caret.line, static_cast<S32>(number.end))));
    view.insertText(with);
    // The caret on the last digit.
    moveTo(view, ALTextPos(caret.line, static_cast<S32>(number.begin + with.size()) - 1));
    return true;
}

void ALVimKeymap::noteJump(ALTextView& view, const ALTextPos& from)
{
    mMarks['\''] = from;
    mMarks['`']  = from;
    if (mHooks.jumped)
    {
        mHooks.jumped(view, from);
    }
}

std::string ALVimKeymap::fileUnderCursor(const ALTextView& view) const
{
    // A quoted name the caret is in, or the first after it on the line --
    // an #include's, a require's -- whole, as vim's gf looks along the
    // line; else the run of what a file's name may hold around the caret.
    const ALTextPos    at   = view.caret();
    const std::string& line = view.document().line(at.line);
    const size_t       here = static_cast<size_t>(llclamp(at.column, 0, static_cast<S32>(line.size())));
    for (size_t open = line.find_first_of("\"'"); open != std::string::npos; open = line.find_first_of("\"'", open + 1))
    {
        const size_t close = line.find(line[open], open + 1);
        if (close == std::string::npos)
        {
            break;
        }
        if (close >= here && close > open + 1)
        {
            return line.substr(open + 1, close - open - 1);
        }
        open = close;
    }
    auto name_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/._-+,#$%~=@:\\", c) != nullptr ||
               (static_cast<unsigned char>(c) & 0x80) != 0;
    };
    size_t begin = here;
    size_t end   = here;
    while (begin > 0 && name_char(line[begin - 1]))
    {
        --begin;
    }
    while (end < line.size() && name_char(line[end]))
    {
        ++end;
    }
    return line.substr(begin, end - begin);
}

bool ALVimKeymap::misspelling(ALTextView& view, bool forward, S32 count)
{
    if (!view.getSpellCheck())
    {
        say(said("VimNoSpellCheck", "E756: Spell checking is not enabled"), true);
        return false;
    }
    // The view's, round past the ends, the count times. A search that ran
    // out of time says so, rather than that there is none: what it checked
    // is kept, and the same search again goes on from there.
    std::optional<ALTextRange> word;
    ALTextPos                  at  = cursor(view);
    bool                       cut = false;
    for (S32 n = 0; n < count; ++n)
    {
        const std::optional<ALTextRange> next = view.misspellingFrom(at, forward, &cut);
        if (!next)
        {
            break;
        }
        word = next;
        at   = next->begin;
    }
    const std::string still_checking = cut ? said("VimMisspellingCut", "Still checking the spelling: search again to go on") : std::string();
    if (!word)
    {
        say(cut ? still_checking : said("VimNoMisspelling", "No misspelled words"), true);
        return false;
    }
    moveTo(view, word->begin);
    if (cut)
    {
        say(still_checking, false);
    }
    return true;
}

void ALVimKeymap::suggest(ALTextView& view, S32 given)
{
    if (!view.getSpellCheck())
    {
        say(said("VimNoSpellCheck", "E756: Spell checking is not enabled"), true);
        return;
    }
    ALTextRange word;
    if (!view.misspelledAt(cursor(view), &word))
    {
        say(said("VimNoSuggestions", "Sorry, no suggestions"), true);
        return;
    }
    view.setCaret(cursor(view));
    view.refreshSuggestions();
    const U32 count = view.getSuggestionCount();
    if (count == 0)
    {
        say(said("VimNoSuggestions", "Sorry, no suggestions"), true);
        return;
    }
    // 2z= the second, as vim takes it without asking.
    auto take = [this, &view](U32 index) {
        view.replaceWithSuggestion(index);
        moveTo(view, view.caret());
    };
    if (given > 0)
    {
        if (static_cast<U32>(given) <= count)
        {
            take(static_cast<U32>(given) - 1);
        }
        return;
    }
    std::vector<std::string> items;
    for (U32 i = 0; i < count; ++i)
    {
        items.push_back(view.getSuggestion(i));
    }
    if (!mHooks.pick)
    {
        // Said as vim lists them, for a count to pick with.
        std::string list;
        for (size_t i = 0; i < items.size(); ++i)
        {
            fmt::format_to(std::back_inserter(list), "{}{} \"{}\"", i ? "  " : "", i + 1, items[i]);
        }
        say(list);
        return;
    }
    // Picked later: the word may have changed by then, and is looked for
    // again at the caret.
    const LLHandle<LLUICtrl> handle = view.getHandle();
    mHooks.pick(view, view.document().text(word), items, [handle](size_t index) {
        ALTextView* again = ALViewType::as<ALTextView>(handle.get());
        if (again && again->misspelledAt(again->caret()))
        {
            again->refreshSuggestions();
            if (index < again->getSuggestionCount())
            {
                again->replaceWithSuggestion(static_cast<U32>(index));
            }
        }
    });
}

bool ALVimKeymap::foldCommand(ALTextView& view, llwchar ch, llwchar prefix)
{
    ALVimHost* host = view.vimHost();
    if (!host)
    {
        return false;
    }
    const S32 line = cursor(view).line;
    const ALTextDocument& d = view.document();
    if (prefix == 'z')
    {
        switch (ch)
        {
            case 'o':
            case 'O':
            case 'v':
                // Open: the block at the caret, or around it; zv what
                // hides the caret, which is the same here.
                host->unfoldAt(line);
                return true;
            case 'c':
            case 'C':
                host->foldAt(line);
                return true;
            case 'a':
            case 'A':
                if (host->isFolded(line))
                {
                    host->unfoldAt(line);
                }
                else
                {
                    host->foldAt(line);
                }
                return true;
            case 'R':
                host->unfoldAll();
                return true;
            case 'M':
                host->foldAll();
                return true;
            case 'j':
            case 'k':
            {
                // zj the start of the next block below; zk the end of the
                // last one above.
                S32 to = -1;
                for (const ALFoldModel::Region& region : host->foldRegions())
                {
                    if (ch == 'j' && region.start > line && (to < 0 || region.start < to))
                    {
                        to = region.start;
                    }
                    if (ch == 'k' && region.end < line && region.end > to)
                    {
                        to = region.end;
                    }
                }
                if (to >= 0)
                {
                    moveTo(view, ALTextPos(to, firstNonBlankColumn(d, to)));
                }
                return true;
            }
            default:
                return false;
        }
    }
    // [z and ]z: the start and the end of the innermost block the caret is
    // in.
    const ALFoldModel::Region* around = nullptr;
    for (const ALFoldModel::Region& region : host->foldRegions())
    {
        if (region.start <= line && line <= region.end && (!around || region.start >= around->start))
        {
            around = &region;
        }
    }
    if (around)
    {
        const S32 to = prefix == '[' ? around->start : around->end;
        moveTo(view, ALTextPos(to, firstNonBlankColumn(d, to)));
    }
    return true;
}

bool ALVimKeymap::unmatchedBracket(ALTextView& view, llwchar bracket, S32 count, ALTextPos& out) const
{
    // Out through the brackets of the other kind as well, counting pairs of
    // this kind only: [( from a(b(c)d|) is the second (. Asked for: as far
    // as it takes.
    return bracketsOf(view).enclosing(view.caret(), static_cast<char>(bracket), count, out, ALBracketIndex::ANYWHERE);
}

