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

#include "alcodeeditor.h"
#include "altextsearch.h"
#include "llclipboard.h"
#include "llstring.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>

namespace
{
    // --- the text, a character at a time ------------------------------------------------

    // The byte at a position, or 0 at a line's end.
    char at(const ALTextDocument& d, const ALTextPos& p)
    {
        const std::string& line = d.line(p.line);
        return p.column >= 0 && p.column < static_cast<S32>(line.size()) ? line[p.column] : '\0';
    }

    bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\0'; }
    bool isWordByte(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

    // vim's classes: blank, word, and everything else -- or, for a WORD,
    // blank and everything else.
    S32 classOf(char c, bool big)
    {
        if (isSpace(c))
        {
            return 0;
        }
        return big || isWordByte(c) ? 1 : 2;
    }

    bool atLineEnd(const ALTextDocument& d, const ALTextPos& p) { return p.column >= d.lineLength(p.line); }
    bool lineBlank(const ALTextDocument& d, S32 line)
    {
        for (const char c : d.line(line))
        {
            if (!isSpace(c))
            {
                return false;
            }
        }
        return true;
    }
    S32 firstNonBlankColumn(const ALTextDocument& d, S32 line)
    {
        const std::string& text = d.line(line);
        S32                c    = 0;
        while (c < static_cast<S32>(text.size()) && isSpace(text[c]))
        {
            ++c;
        }
        return c;
    }
    std::string indentOf(const ALTextDocument& d, S32 line) { return d.line(line).substr(0, firstNonBlankColumn(d, line)); }

    // One cluster on, across a line's end onto the next line's start;
    // false at the end of the text.
    bool stepOn(const ALTextDocument& d, ALTextPos& p)
    {
        if (atLineEnd(d, p))
        {
            if (p.line + 1 >= d.lineCount())
            {
                return false;
            }
            p = ALTextPos(p.line + 1, 0);
            return true;
        }
        p = d.nextCluster(p);
        return true;
    }
    // One cluster back, across a line's start onto the previous line's
    // end; false at the start of the text.
    bool stepBack(const ALTextDocument& d, ALTextPos& p)
    {
        if (p.column <= 0)
        {
            if (p.line <= 0)
            {
                return false;
            }
            p = d.lineEnd(p.line - 1);
            return true;
        }
        p = d.prevCluster(p);
        return true;
    }

    // The last character's position on a line, which normal mode's
    // caret never goes past; the start of an empty line.
    ALTextPos lastCharOf(const ALTextDocument& d, S32 line)
    {
        const ALTextPos end = d.lineEnd(line);
        return end.column > 0 ? d.prevCluster(end) : end;
    }

    bool isBracket(char c) { return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'; }
    char partnerOf(char c)
    {
        switch (c)
        {
            case '(': return ')';
            case ')': return '(';
            case '[': return ']';
            case ']': return '[';
            case '{': return '}';
            case '}': return '{';
            case '<': return '>';
            case '>': return '<';
            default:  return '\0';
        }
    }
    bool opensOf(char c) { return c == '(' || c == '[' || c == '{' || c == '<'; }

    // The bracket that matches the one at a position, nesting counted.
    bool matchBracket(const ALTextDocument& d, ALTextPos from, ALTextPos& match)
    {
        const char c       = at(d, from);
        const char partner = partnerOf(c);
        if (!partner)
        {
            return false;
        }
        const bool forward = opensOf(c);
        S32        depth   = 0;
        ALTextPos  p       = from;
        do
        {
            const char here = at(d, p);
            if (here == c)
            {
                ++depth;
            }
            else if (here == partner && --depth == 0)
            {
                match = p;
                return true;
            }
        } while (forward ? stepOn(d, p) : stepBack(d, p));
        return false;
    }

    std::string utf8Of(llwchar ch) { return utf8str_from_cp(ch); }

    // The case of a stretch changed: swapped, lowered or raised.
    std::string recased(std::string text, llwchar how)
    {
        for (char& c : text)
        {
            if (how == '~')
            {
                c = (c >= 'a' && c <= 'z') ? c - 32 : (c >= 'A' && c <= 'Z') ? c + 32 : c;
            }
            else if (how == 'u')
            {
                c = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            }
            else if (how == 'U')
            {
                c = (c >= 'a' && c <= 'z') ? c - 32 : c;
            }
        }
        return text;
    }

    bool isDigit(llwchar ch) { return ch >= '0' && ch <= '9'; }

    // A count typed before a command: the digits as a number, at least one.
    S32 countOr(S32 count, S32 fallback = 1) { return count > 0 ? count : fallback; }
}

ALVimKeymap::ALVimKeymap() = default;
ALVimKeymap::~ALVimKeymap() = default;

// --- keys as text ------------------------------------------------------------------

namespace
{
    struct KeyName
    {
        KEY         key;
        const char* name;
    };
    const KeyName KEY_NAMES[] = {
        { KEY_ESCAPE, "Esc" },     { KEY_RETURN, "CR" },     { KEY_BACKSPACE, "BS" }, { KEY_TAB, "Tab" },        { KEY_DELETE, "Del" },
        { KEY_UP, "Up" },          { KEY_DOWN, "Down" },     { KEY_LEFT, "Left" },    { KEY_RIGHT, "Right" },    { KEY_HOME, "Home" },
        { KEY_END, "End" },        { KEY_PAGE_UP, "PageUp" }, { KEY_PAGE_DOWN, "PageDown" }, { KEY_INSERT, "Ins" },
    };

    std::string keyName(KEY key)
    {
        for (const KeyName& known : KEY_NAMES)
        {
            if (known.key == key)
            {
                return known.name;
            }
        }
        if (key >= 0x20 && key < 0x7F)
        {
            return std::string(1, static_cast<char>(std::tolower(key)));
        }
        return std::string();
    }

    bool keyFromName(const std::string& name, KEY& key)
    {
        for (const KeyName& known : KEY_NAMES)
        {
            if (name == known.name)
            {
                key = known.key;
                return true;
            }
        }
        if (name.size() == 1)
        {
            key = static_cast<KEY>(std::toupper(static_cast<unsigned char>(name[0])));
            return true;
        }
        return false;
    }
}

// static
std::string ALVimKeymap::encodeInputs(const std::vector<Input>& inputs)
{
    std::string out;
    for (const Input& in : inputs)
    {
        if (in.isChar)
        {
            out += in.ch == '<' ? std::string("<lt>") : utf8Of(in.ch);
            continue;
        }
        const std::string name = keyName(in.key);
        if (name.empty())
        {
            continue;
        }
        std::string spelt;
        if (in.mask & MASK_CONTROL)
        {
            spelt += "C-";
        }
        if (in.mask & MASK_ALT)
        {
            spelt += "A-";
        }
        if (in.mask & MASK_SHIFT)
        {
            spelt += "S-";
        }
        out += "<" + spelt + name + ">";
    }
    return out;
}

// static
std::vector<ALVimKeymap::Input> ALVimKeymap::decodeInputs(std::string_view text)
{
    std::vector<Input> out;
    size_t             at = 0;
    while (at < text.size())
    {
        if (text[at] == '<')
        {
            const size_t close = text.find('>', at + 1);
            if (close != std::string_view::npos && close > at + 1)
            {
                std::string name(text.substr(at + 1, close - at - 1));
                if (name == "lt")
                {
                    Input in;
                    in.isChar = true;
                    in.ch     = '<';
                    out.push_back(in);
                    at = close + 1;
                    continue;
                }
                MASK mask = MASK_NONE;
                while (name.size() > 2 && name[1] == '-' && (name[0] == 'C' || name[0] == 'A' || name[0] == 'M' || name[0] == 'S'))
                {
                    mask |= name[0] == 'C' ? MASK_CONTROL : name[0] == 'S' ? MASK_SHIFT : MASK_ALT;
                    name.erase(0, 2);
                }
                KEY key;
                if (keyFromName(name, key))
                {
                    Input in;
                    in.key  = key;
                    in.mask = mask;
                    out.push_back(in);
                    at = close + 1;
                    continue;
                }
            }
        }
        // A character, whole.
        const LLCodepointAt cp = utf8str_decode_at(text, at);
        Input               in;
        in.isChar = true;
        in.ch     = cp.cp;
        out.push_back(in);
        at = llmax(cp.next, at + 1);
    }
    return out;
}

// --- what the outside sees -------------------------------------------------------------

bool ALVimKeymap::inserting() const
{
    return mMode == Mode::Insert || mMode == Mode::Replace;
}

std::string ALVimKeymap::status() const
{
    const std::string recording = mRecording ? std::string("recording @") + mRecording + " " : std::string();
    switch (mMode)
    {
        case Mode::Insert:      return recording + "-- INSERT --";
        case Mode::Replace:     return recording + "-- REPLACE --";
        case Mode::Visual:      return recording + "-- VISUAL --";
        case Mode::VisualLine:  return recording + "-- VISUAL LINE --";
        case Mode::VisualBlock: return recording + "-- VISUAL BLOCK --";
        case Mode::Command:
        case Mode::Search:      return utf8Of(mLineKind) + mLine;
        case Mode::Confirm:
        {
            const std::string& with = mConfirming.at < mConfirming.edits.size() ? mConfirming.edits[mConfirming.at].second : std::string();
            return recording + "replace with " + with + " (y/n/a/q/l)?";
        }
        case Mode::Normal:
        default:
        {
            // What is pending, as vim's showcmd has it.
            std::string pending;
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
                pending += utf8Of(mOperator);
            }
            if (mCount > 0)
            {
                pending += std::to_string(mCount);
            }
            if (mPending)
            {
                pending += utf8Of(mPending);
            }
            if (mObjectKind)
            {
                pending += utf8Of(mObjectKind);
            }
            return recording + pending;
        }
    }
}

void ALVimKeymap::mouseChanged(ALTextView& view)
{
    // Insert mode stays insert mode wherever the click lands; a line
    // being typed is not the mouse's.
    if (mMode == Mode::Insert || mMode == Mode::Replace || mMode == Mode::Command || mMode == Mode::Search || mMode == Mode::Confirm)
    {
        return;
    }
    const ALTextDocument& d = view.document();
    if (view.hasSelection())
    {
        // A drag is a visual selection, charwise, the character under
        // either end included as the view's selection reaches past it.
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
        if (mMode == Mode::Normal)
        {
            mMode = Mode::Visual;
        }
        else if (mMode == Mode::VisualBlock)
        {
            mMode = Mode::Visual;
            if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
            {
                editor->clearHighlights();
            }
        }
    }
    else
    {
        // A click: normal mode, the caret on the character it landed on.
        if (isVisual())
        {
            mVisualLast       = mMode;
            mVisualLastAnchor = mVisualAnchor;
            mVisualLastCaret  = mVisualCaret;
            mMode             = Mode::Normal;
            if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
            {
                editor->clearHighlights();
            }
        }
        const ALTextPos last = lastCharOf(d, view.caret().line);
        if (view.caret().column > last.column)
        {
            view.setCaret(last);
        }
    }
    clearPending();
    bump();
}

std::string ALVimKeymap::registerText(char name) const
{
    return fetch(name).text;
}

void ALVimKeymap::say(const std::string& message, bool error)
{
    mMessage      = message;
    mMessageError = error;
    bump();
}

bool ALVimKeymap::handleKey(ALTextView& view, KEY key, MASK mask)
{
    Input input;
    input.key  = key;
    input.mask = mask;
    return feed(view, input);
}

bool ALVimKeymap::handleChar(ALTextView& view, llwchar ch)
{
    Input input;
    input.isChar = true;
    input.ch     = ch;
    return feed(view, input);
}

// --- the dispatch ------------------------------------------------------------------

bool ALVimKeymap::feed(ALTextView& view, const Input& input)
{
    if (mRecording && !mReplaying && mPlaying == 0)
    {
        mRecorded.push_back(input);
    }
    if (!mReplaying)
    {
        mMessage.clear();
        mMessageError = false;
        // A command starts where nothing is pending in normal mode; what
        // it is typed as is kept until it is done, for `.`.
        if (mMode == Mode::Normal && mCount == 0 && !mRegister && !mOperator && !mPending)
        {
            mCommandInputs.clear();
            // Each command is a step of its own to undo, however close on
            // the last it came.
            view.undoJournal().breakRun();
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
            taken = commandLine(view, input);
            break;
        case Mode::Confirm:
            taken = confirmKey(view, input);
            break;
        default:
            taken = normal(view, input);
            break;
    }
    // A key nobody took is not part of what was typed: its character
    // follows, and is.
    if (!taken && !input.isChar)
    {
        if (!mReplaying && !mCommandInputs.empty())
        {
            mCommandInputs.pop_back();
        }
        if (mRecording && !mReplaying && mPlaying == 0 && !mRecorded.empty())
        {
            mRecorded.pop_back();
        }
    }
    bump();
    return taken;
}

bool ALVimKeymap::play(ALTextView& view, const std::vector<Input>& inputs)
{
    if (mPlaying >= 100)
    {
        say("E169: Command too recursive", true);
        return false;
    }
    ++mPlaying;
    bool ok = true;
    for (const Input& in : inputs)
    {
        feed(view, in);
        if (mMessageError)
        {
            ok = false;
            break;
        }
    }
    --mPlaying;
    return ok;
}

void ALVimKeymap::clearPending()
{
    mCount         = 0;
    mRegister      = 0;
    mOperator      = 0;
    mOperatorCount = 0;
    mPending       = 0;
    mObjectKind    = 0;
}

void ALVimKeymap::finishCommand(bool changed)
{
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
    mVisualPending.valid = false;
}

void ALVimKeymap::noteVisualOperation(const Span& span, S32 lines_hint)
{
    mVisualPending.valid   = true;
    mVisualPending.mode    = span.linewise ? Mode::VisualLine : span.block ? Mode::VisualBlock : Mode::Visual;
    mVisualPending.lines   = lines_hint >= 0 ? lines_hint : span.range.end.line - span.range.begin.line;
    mVisualPending.columns = span.block ? span.range.end.column - span.range.begin.column
                             : span.linewise ? 0
                                             : span.range.end.column - (span.range.end.line == span.range.begin.line ? span.range.begin.column : 0);
    // The operator is the key being handled: the last one typed.
    mVisualPending.opAt = mCommandInputs.empty() ? 0 : mCommandInputs.size() - 1;
}

ALTextPos ALVimKeymap::cursor(const ALTextView& view) const
{
    return isVisual() ? view.document().clamp(mVisualCaret) : view.caret();
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
        // Keys: the ones vim gives a meaning, and the rest left to the
        // view's own keymap.
        const bool ctrl = (input.mask & MASK_CONTROL) != 0;
        switch (input.key)
        {
            case KEY_ESCAPE:
                if (mMode != Mode::Normal)
                {
                    leaveVisual(view);
                }
                clearPending();
                return true;
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
            default:
                break;
        }
        if (ctrl && !(input.mask & MASK_ALT))
        {
            switch (input.key)
            {
                case 'R':
                    for (S32 n = countOr(mCount); n > 0; --n)
                    {
                        view.perform(ALEditorCommand::Redo);
                    }
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
                    ALTextPos p    = view.caret();
                    p.line         = llclamp(p.line + (input.key == 'D' ? half : -half), 0, view.document().lineCount() - 1);
                    view.setScrollY(view.scrollY() + (input.key == 'D' ? half : -half) * view.layout().rowHeight());
                    moveTo(view, p);
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
        }
        return false;
    }
    return command(view, input.ch);
}

bool ALVimKeymap::command(ALTextView& view, llwchar ch)
{
    const ALTextDocument& d       = view.document();
    const bool            visual  = mMode != Mode::Normal;
    const bool            editing = !view.isReadOnly();

    // Something waiting for this character.
    if (mPending)
    {
        const llwchar pending = mPending;
        mPending              = 0;
        const S32 count       = countOr(mCount);
        switch (pending)
        {
            case '"':
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '"' || ch == '0' || ch == '_')
                {
                    mRegister = static_cast<char>(ch);
                    mCount    = 0;
                    return true;
                }
                clearPending();
                return true;
            case 'm':
                if (ch >= 'a' && ch <= 'z')
                {
                    mMarks[static_cast<char>(ch)] = cursor(view);
                }
                clearPending();
                return true;
            case 'q':
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
                    const std::vector<std::string>& history = historyOf(ch);
                    mMode                                   = ch == ':' ? Mode::Command : Mode::Search;
                    mLineKind                               = ch;
                    mHistoryPrefix.clear();
                    mHistoryAt = history.empty() ? -1 : static_cast<S32>(history.size()) - 1;
                    mLine      = history.empty() ? std::string() : history.back();
                    return true;
                }
                clearPending();
                return true;
            case '@':
            {
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
                    const std::vector<std::string>& history = historyOf(':');
                    if (history.empty())
                    {
                        say("E30: No previous command line", true);
                        return true;
                    }
                    for (S32 n = 0; n < count; ++n)
                    {
                        runCommand(view, history.back());
                        if (mMessageError)
                        {
                            break;
                        }
                    }
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
                for (S32 n = 0; n < count; ++n)
                {
                    if (!play(view, inputs))
                    {
                        break;
                    }
                }
                return true;
            }
            case 'r':
            {
                if (!editing)
                {
                    clearPending();
                    return true;
                }
                if (visual)
                {
                    // Every character of the selection becomes this one.
                    const Span                                        span = visualSpan(view);
                    std::vector<std::pair<ALTextRange, std::string>> edits;
                    for (S32 line = span.range.begin.line; line <= span.range.end.line; ++line)
                    {
                        const S32 c0 = span.linewise ? 0 : (span.block || line == span.range.begin.line) ? span.range.begin.column : 0;
                        const S32 c1 = span.linewise ? d.lineLength(line)
                                       : span.block ? llmin(d.lineLength(line), span.range.end.column + 1)
                                       : line == span.range.end.line ? span.range.end.column : d.lineLength(line);
                        const S32 lo = llmin(c0, d.lineLength(line));
                        const S32 hi = llmin(llmax(lo, c1), d.lineLength(line));
                        if (hi > lo)
                        {
                            std::string with;
                            for (S32 c = lo; c < hi; ++c)
                            {
                                with += utf8Of(ch);
                            }
                            edits.emplace_back(ALTextRange(ALTextPos(line, lo), ALTextPos(line, hi)), with);
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
                // replaced; a return breaks the line.
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
                std::string with;
                for (S32 n = 0; n < count; ++n)
                {
                    with += ch == '\r' ? std::string("\n") : utf8Of(ch);
                }
                view.setSelection(ALTextRange(from, to));
                view.insertText(with);
                moveTo(view, ch == '\r' ? view.caret() : d.prevCluster(view.caret()));
                finishCommand(true);
                return true;
            }
            case 'g':
                switch (ch)
                {
                    case 'g':
                        return command(view, 0x01);  // gg, as a motion the table knows
                    case '&':
                        // The last :s again on every line, with its flags.
                        clearPending();
                        if (editing)
                        {
                            runCommand(view, "%s//~/&");
                        }
                        return true;
                    case 'v':
                        if (mVisualLast != Mode::Normal)
                        {
                            mVisualAnchor = mVisualLastAnchor;
                            mVisualCaret  = mVisualLastCaret;
                            mMode         = mVisualLast;
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
                        S32       until = visual ? last : llmin(d.lineCount() - 1, first + llmax(1, count - 1));
                        if (!visual && until == first)
                        {
                            until = llmin(d.lineCount() - 1, first + 1);
                        }
                        std::vector<std::pair<ALTextRange, std::string>> edits;
                        for (S32 line = first; line < until; ++line)
                        {
                            edits.emplace_back(ALTextRange(d.lineEnd(line), d.lineStart(line + 1)), std::string());
                        }
                        if (visual)
                        {
                            leaveVisual(view);
                        }
                        if (!edits.empty())
                        {
                            view.replaceAll(std::move(edits));
                        }
                        finishCommand(true);
                        return true;
                    }
                    case '~':
                    case 'u':
                    case 'U':
                        if (visual)
                        {
                            const Span span = visualSpan(view);
                            noteVisualOperation(span);
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
                    {
                        // The end of the previous word.
                        ALTextPos p = cursor(view);
                        for (S32 n = 0; n < count; ++n)
                        {
                            stepBack(d, p);
                            while (classOf(at(d, p), ch == 'E') == 0 && stepBack(d, p)) {}
                        }
                        moveTo(view, p);
                        clearPending();
                        return true;
                    }
                    default:
                        clearPending();
                        return true;
                }
            case 'z':
            {
                const S32 line = cursor(view).line;
                const S32 row  = view.layout().rowHeight();
                const S32 top  = view.layout().lineTop(line);
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
                clearPending();
                return true;
            }
            case 'Z':
                if (ch == 'Z')
                {
                    runCommand(view, "x");
                }
                else if (ch == 'Q')
                {
                    runCommand(view, "q!");
                }
                clearPending();
                return true;
            case 'i':
            case 'a':
            {
                // A text object, for the operator or the visual selection.
                Span span;
                if (textObject(view, pending, ch, count, span))
                {
                    if (visual)
                    {
                        mVisualAnchor = span.range.begin;
                        mVisualCaret  = span.linewise ? span.range.end : d.prevCluster(span.range.end);
                        mMode         = span.linewise ? Mode::VisualLine : Mode::Visual;
                        showVisual(view);
                        clearPending();
                        return true;
                    }
                    const llwchar op = mOperator;
                    applyOperator(view, op, span, 1);
                    finishCommand(op != 'y');
                    return true;
                }
                clearPending();
                return true;
            }
            default:
            {
                // f, F, t, T, ` and ': motions with an argument.
                Motion m = motion(view, pending, count, ch);
                if (!m.ok)
                {
                    clearPending();
                    return true;
                }
                if (mOperator)
                {
                    Span span;
                    const ALTextPos from = cursor(view);
                    span.range           = ALTextRange(from, m.to).normalised();
                    if (m.inclusive)
                    {
                        span.range.end = d.nextCluster(span.range.end);
                    }
                    span.linewise = m.linewise;
                    const llwchar op = mOperator;
                    applyOperator(view, op, span, 1);
                    finishCommand(op != 'y');
                    return true;
                }
                moveTo(view, m.to);
                clearPending();
                return true;
            }
        }
    }

    // A count.
    if (isDigit(ch) && !(ch == '0' && mCount == 0))
    {
        mCount = llmin(mCount * 10 + static_cast<S32>(ch - '0'), 100000);
        return true;
    }
    if (ch == '"' && !mOperator)
    {
        mPending = '"';
        return true;
    }

    // An operator, or its motion.
    const S32 count = countOr(mCount);
    if (mOperator)
    {
        const llwchar op = mOperator;
        // The operator doubled -- dd, yy, cc, >>, <<, == -- is the line, and
        // the count more.
        if (ch == op || (op == '~' && ch == '~') || (op == 'u' && ch == 'u') || (op == 'U' && ch == 'U'))
        {
            const S32 lines = countOr(mOperatorCount) * count;
            Span      span;
            span.linewise    = true;
            const S32 first  = cursor(view).line;
            const S32 last   = llmin(d.lineCount() - 1, first + lines - 1);
            span.range       = ALTextRange(d.lineStart(first), d.lineEnd(last));
            applyOperator(view, op, span, 1);
            finishCommand(op != 'y');
            return true;
        }
        if (ch == 'i' || ch == 'a')
        {
            mPending = ch;
            return true;
        }
        if (ch == 'f' || ch == 'F' || ch == 't' || ch == 'T' || ch == '`' || ch == '\'' || ch == 'g')
        {
            mPending = ch;
            return true;
        }
        // cw on a word is ce: the space after it is not eaten.
        llwchar m_ch = ch;
        if (op == 'c' && (ch == 'w' || ch == 'W') && classOf(at(d, cursor(view)), ch == 'W') != 0)
        {
            m_ch = ch == 'w' ? 'e' : 'E';
        }
        Motion m = motion(view, m_ch, countOr(mOperatorCount) * count, 0);
        if (!m.ok || !m.moved)
        {
            clearPending();
            return m.ok;
        }
        Span            span;
        const ALTextPos from = cursor(view);
        span.range           = ALTextRange(from, m.to).normalised();
        // A word motion under an operator stops at the line's end rather
        // than reaching the next line's first word.
        if ((ch == 'w' || ch == 'W') && span.range.end.line > from.line && !m.linewise)
        {
            span.range.end = d.lineEnd(from.line);
        }
        if (m.inclusive)
        {
            span.range.end = d.nextCluster(span.range.end);
        }
        span.linewise = m.linewise;
        applyOperator(view, op, span, 1);
        finishCommand(op != 'y');
        return true;
    }

    // Visual mode's own commands.
    if (visual)
    {
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
                Span span = visualSpan(view);
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
                    // The selection replaced by the register, which keeps
                    // what was there for a further put.
                    const Register put_this = fetch(mRegister);
                    leaveVisual(view);
                    if (!editing)
                    {
                        clearPending();
                        return true;
                    }
                    view.undoJournal().beginGroup();
                    view.deleteRange(span.range);
                    view.setCaret(span.range.begin);
                    if (put_this.linewise)
                    {
                        view.insertText(put_this.text + "\n");
                    }
                    else
                    {
                        view.insertText(put_this.text);
                    }
                    view.undoJournal().endGroup();
                    moveTo(view, put_this.text.empty() ? view.caret() : d.prevCluster(view.caret()));
                    finishCommand(true);
                    return true;
                }
                if (ch == 'I' || ch == 'A')
                {
                    // Typed onto every line of the block, once insert mode
                    // is left.
                    const bool block = span.block;
                    const S32  first = span.range.begin.line;
                    const S32  last  = span.range.end.line;
                    S32        column = ch == 'A' ? span.range.end.column : span.range.begin.column;
                    if (!block)
                    {
                        column = ch == 'A' ? d.lineLength(first) : firstNonBlankColumn(d, first);
                    }
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
                    view.setCaret(d.clamp(ALTextPos(first, column)));
                    enterInsert(view, 1);
                    return true;
                }
                const llwchar op = ch == 'x' ? 'd' : ch == 's' || ch == 'C' || ch == 'S' || ch == 'R' ? 'c' : ch == 'D' || ch == 'X' ? 'd' : ch == 'Y' ? 'y' : ch;
                const S32     n  = count;
                noteVisualOperation(span);
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
                    mMode = which;
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
                break;
        }
    }

    // Commands.
    switch (ch)
    {
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
                const bool        append  = into >= 'A' && into <= 'Z';
                const char        lower   = static_cast<char>(std::tolower(into));
                mRecording                = 0;
                mRecorded.clear();
                // Into its register alone: what was typed is not what the
                // clipboard holds.
                Register& reg = mRegisters[lower];
                reg.text      = append ? reg.text + keys : keys;
                reg.linewise  = false;
                reg.block     = false;
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
            mPending = ch;
            return true;
        case 'r':
            if (visual)
            {
                mPending = 'r';
                return true;
            }
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
                view.setCaret(d.lineEnd(line));
                view.insertText("\n" + indent);
            }
            else
            {
                view.setCaret(d.lineStart(line));
                view.insertText(indent + "\n");
                view.setCaret(ALTextPos(line, static_cast<S32>(indent.size())));
            }
            enterInsert(view, count);
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
            if (span.range.empty())
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
                const S32 first = view.caret().line;
                const S32 last  = llmin(d.lineCount() - 1, first + count - 1);
                span.linewise   = true;
                span.range      = ALTextRange(d.lineStart(first), d.lineEnd(last));
            }
            else
            {
                const S32 last = llmin(d.lineCount() - 1, view.caret().line + count - 1);
                span.range     = ALTextRange(view.caret(), d.lineEnd(last));
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
            Span      span;
            const S32 first = view.caret().line;
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
            // Lines joined with one space, their leading blanks gone.
            const S32 first = view.caret().line;
            const S32 until = llmin(d.lineCount() - 1, first + llmax(1, count - 1));
            if (until == first)
            {
                clearPending();
                return true;
            }
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (S32 line = first; line < until; ++line)
            {
                const ALTextPos next_text(line + 1, firstNonBlankColumn(d, line + 1));
                const bool      empty = d.lineLength(line + 1) == next_text.column || at(d, next_text) == ')';
                edits.emplace_back(ALTextRange(d.lineEnd(line), next_text), empty ? std::string() : std::string(" "));
            }
            ALTextPos caret = d.lineEnd(first);
            view.replaceAll(std::move(edits));
            moveTo(view, caret);
            finishCommand(true);
            return true;
        }
        case 'u':
            for (S32 n = 0; n < count; ++n)
            {
                view.perform(ALEditorCommand::Undo);
            }
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
                enterVisual(view, mLastVisual.mode);
                ALTextPos to = from_here;
                to.line      = llmin(d.lineCount() - 1, from_here.line + mLastVisual.lines);
                if (mLastVisual.mode == Mode::VisualBlock)
                {
                    to.column = from_here.column + mLastVisual.columns;
                }
                else if (mLastVisual.mode == Mode::Visual)
                {
                    to.column = (mLastVisual.lines == 0 ? from_here.column : 0) + mLastVisual.columns;
                    to        = d.prevCluster(d.clamp(to));
                    if (to < from_here)
                    {
                        to = from_here;
                    }
                }
                mVisualCaret = d.clamp(to);
                showVisual(view);
            }
            else if (given > 0)
            {
                // The count given takes the place of the one recorded.
                while (from < change.size() && change[from].isChar && isDigit(change[from].ch))
                {
                    ++from;
                }
                for (const char digit : std::to_string(given))
                {
                    Input in;
                    in.isChar = true;
                    in.ch     = static_cast<llwchar>(digit);
                    feed(view, in);
                }
            }
            for (size_t i = from; i < change.size(); ++i)
            {
                feed(view, change[i]);
            }
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
            if (substitute(view, line, line, std::string()))
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
            mMode      = Mode::Command;
            mLineKind  = ':';
            mHistoryAt = -1;
            mLine      = visual ? std::string("'<,'>") : mCount > 0 ? std::string(".,.+") + std::to_string(mCount - 1) : std::string();
            if (visual)
            {
                leaveVisual(view);
                mMode = Mode::Command;
            }
            return true;
        case '/':
        case '?':
            mMode      = Mode::Search;
            mLineKind  = ch;
            mHistoryAt = -1;
            mLine.clear();
            return true;
        case 'n':
        case 'N':
            if (mSearchPattern.empty())
            {
                say("E35: No previous regular expression", true);
            }
            else
            {
                search(view, mSearchPattern, ch == 'n' ? mSearchForward : !mSearchForward, count, mSearchWholeWord);
            }
            clearPending();
            return true;
        case '*':
        case '#':
        {
            const ALTextRange word = d.wordAt(view.caret());
            if (word.empty())
            {
                clearPending();
                return true;
            }
            mSearchPattern   = d.text(word);
            mSearchForward   = ch == '*';
            mSearchWholeWord = true;
            search(view, mSearchPattern, mSearchForward, count, true);
            clearPending();
            return true;
        }
        case 'R':
            if (editing)
            {
                enterInsert(view, count);
                mMode = Mode::Replace;
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
            break;
    }

    // A motion on its own.
    Motion m = motion(view, ch, count, 0);
    if (m.ok)
    {
        if (m.moved || true)
        {
            moveTo(view, m.to);
        }
        clearPending();
        return true;
    }
    clearPending();
    return true;
}

// --- motions -----------------------------------------------------------------------------

ALVimKeymap::Motion ALVimKeymap::motion(ALTextView& view, llwchar ch, S32 count, llwchar arg)
{
    const ALTextDocument& d = view.document();
    const ALTextPos       from = cursor(view);
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
        case 0x01:  // gg
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
            else if (ch == 0x01)
            {
                line = llclamp(mCount > 0 || count > 1 ? count - 1 : 0, 0, d.lineCount() - 1);
            }
            else
            {
                line = llclamp(mCount > 0 ? count - 1 : d.lineCount() - 1, 0, d.lineCount() - 1);
            }
            m.linewise = true;
            m.moved    = line != from.line || ch == 0x01 || ch == 'G';
            if (ch == 'j' || ch == 'k')
            {
                // The column kept, by where it is drawn.
                const S32 shown = d.displayColumn(from, view.getTabWidth());
                m.to            = d.posAtDisplayColumn(line, shown, view.getTabWidth());
            }
            else
            {
                m.to = ALTextPos(line, firstNonBlankColumn(d, line));
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
            // operator reaches the end, the caret sits on the last.
            if (!mOperator && m.to.column > 0)
            {
                m.to = d.prevCluster(m.to);
            }
            return m;
        }
        case '|':
            m.to = d.clamp(ALTextPos(from.line, mCount > 0 ? count - 1 : 0));
            return m;
        case 'w':
        case 'W':
        {
            const bool big = ch == 'W';
            for (S32 n = 0; n < count; ++n)
            {
                const S32 cls = classOf(at(d, m.to), big);
                if (cls != 0)
                {
                    while (!atLineEnd(d, m.to) && classOf(at(d, m.to), big) == cls)
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
                        if (m.to.line + 1 >= d.lineCount())
                        {
                            break;
                        }
                        m.to = ALTextPos(m.to.line + 1, 0);
                        if (d.lineLength(m.to.line) == 0)
                        {
                            break;
                        }
                        continue;
                    }
                    if (classOf(at(d, m.to), big) != 0)
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
                while (classOf(at(d, m.to), big) == 0 && stepOn(d, m.to)) {}
                const S32 cls = classOf(at(d, m.to), big);
                ALTextPos next = m.to;
                while (!atLineEnd(d, next) && classOf(at(d, next), big) == cls)
                {
                    m.to = next;
                    next = d.nextCluster(next);
                }
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
                while (classOf(at(d, m.to), big) == 0 && stepBack(d, m.to)) {}
                const S32 cls = classOf(at(d, m.to), big);
                while (m.to.column > 0 && classOf(at(d, d.prevCluster(m.to)), big) == cls)
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
        case '%':
        {
            // The bracket under the caret or the first after it on the line.
            ALTextPos p = from;
            while (!atLineEnd(d, p) && !isBracket(at(d, p)))
            {
                p = d.nextCluster(p);
            }
            ALTextPos match;
            if (atLineEnd(d, p) || !matchBracket(d, p, match))
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
            S32                col   = from.column;
            for (S32 n = 0; n < count; ++n)
            {
                size_t found;
                if (forward)
                {
                    const S32 start = col + 1 + ((till && n == 0) ? 1 : 0);
                    found           = start < static_cast<S32>(line.size()) ? line.find(needle, start) : std::string::npos;
                }
                else
                {
                    const S32 start = col - 1 - ((till && n == 0) ? 1 : 0);
                    found           = start >= 0 ? line.rfind(needle, start) : std::string::npos;
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
                col += forward ? -1 : static_cast<S32>(needle.size());
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
            const auto mark = mMarks.find(static_cast<char>(arg));
            if (mark == mMarks.end())
            {
                say("E20: Mark not set", true);
                m.moved = false;
                return m;
            }
            const ALTextPos p = d.clamp(mark->second);
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
            const bool big = what == 'W';
            ALTextPos  begin = from;
            ALTextPos  end   = from;
            const S32  cls   = classOf(at(d, from), big);
            while (begin.column > 0 && classOf(at(d, d.prevCluster(begin)), big) == cls)
            {
                begin = d.prevCluster(begin);
            }
            while (!atLineEnd(d, end) && classOf(at(d, end), big) == cls)
            {
                end = d.nextCluster(end);
            }
            for (S32 n = 1; n < count && !atLineEnd(d, end); ++n)
            {
                const S32 next = classOf(at(d, end), big);
                while (!atLineEnd(d, end) && classOf(at(d, end), big) == next)
                {
                    end = d.nextCluster(end);
                }
            }
            if (around)
            {
                // The blanks after it, or before it where there are none after.
                ALTextPos after = end;
                while (!atLineEnd(d, after) && isSpace(at(d, after)))
                {
                    after = d.nextCluster(after);
                }
                if (after != end)
                {
                    end = after;
                }
                else
                {
                    while (begin.column > 0 && isSpace(at(d, d.prevCluster(begin))))
                    {
                        begin = d.prevCluster(begin);
                    }
                }
            }
            if (begin == end)
            {
                return false;
            }
            out.range = ALTextRange(begin, end);
            return true;
        }
        case '"':
        case '\'':
        case '`':
        {
            const std::string& line  = d.line(from.line);
            const char         quote = static_cast<char>(what);
            // The pair around the caret: the first quote before or at it
            // that opens, and its close after.
            S32 open = -1;
            S32 close = -1;
            S32 col   = 0;
            while (col < static_cast<S32>(line.size()))
            {
                if (line[col] == quote)
                {
                    const size_t next = line.find(quote, col + 1);
                    if (next == std::string::npos)
                    {
                        break;
                    }
                    if (from.column >= col && from.column <= static_cast<S32>(next))
                    {
                        open  = col;
                        close = static_cast<S32>(next);
                        break;
                    }
                    col = static_cast<S32>(next) + 1;
                    continue;
                }
                ++col;
            }
            if (open < 0)
            {
                // The first pair after the caret, as vim allows.
                const size_t first = line.find(quote, from.column);
                const size_t next  = first == std::string::npos ? std::string::npos : line.find(quote, first + 1);
                if (next == std::string::npos)
                {
                    return false;
                }
                open  = static_cast<S32>(first);
                close = static_cast<S32>(next);
            }
            if (around)
            {
                S32 end = close + 1;
                while (end < static_cast<S32>(line.size()) && isSpace(line[end]))
                {
                    ++end;
                }
                out.range = ALTextRange(ALTextPos(from.line, open), ALTextPos(from.line, end));
            }
            else
            {
                out.range = ALTextRange(ALTextPos(from.line, open + 1), ALTextPos(from.line, close));
            }
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
            const char opener = (what == '(' || what == ')' || what == 'b') ? '(' : (what == '[' || what == ']') ? '[' : (what == '{' || what == '}' || what == 'B') ? '{' : '<';
            const char closer = partnerOf(opener);
            // Back to the opener that holds the caret, counting nesting.
            ALTextPos open  = from;
            S32       depth = 0;
            bool      found = at(d, open) == opener;
            while (!found)
            {
                if (!stepBack(d, open))
                {
                    return false;
                }
                const char c = at(d, open);
                if (c == closer)
                {
                    ++depth;
                }
                else if (c == opener)
                {
                    if (depth == 0)
                    {
                        found = true;
                    }
                    else
                    {
                        --depth;
                    }
                }
            }
            for (S32 n = 1; n < count; ++n)
            {
                ALTextPos outer = open;
                depth           = 0;
                bool more       = false;
                while (stepBack(d, outer))
                {
                    const char c = at(d, outer);
                    if (c == closer)
                    {
                        ++depth;
                    }
                    else if (c == opener)
                    {
                        if (depth == 0)
                        {
                            more = true;
                            break;
                        }
                        --depth;
                    }
                }
                if (!more)
                {
                    break;
                }
                open = outer;
            }
            ALTextPos close;
            if (!matchBracket(d, open, close))
            {
                return false;
            }
            if (around)
            {
                out.range = ALTextRange(open, d.nextCluster(close));
            }
            else
            {
                out.range = ALTextRange(d.nextCluster(open), close);
            }
            return true;
        }
        case 't':
        {
            // The tag around the caret, the count of them out: every tag
            // in the text paired with its closing one by nesting, so that
            // an inner tag of the same name is not taken for the closing
            // of the outer; the innermost pair around the caret is the
            // first, its parent the second. Self-closing tags, comments
            // and declarations are no tags.
            const std::string text = d.text();
            const size_t      here = d.offsetOf(from);
            struct Tag
            {
                size_t openBegin, openEnd, closeBegin, closeEnd;
            };
            std::vector<Tag>                             pairs;
            std::vector<std::pair<std::string, size_t>> open;
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
                        if (open[i].first == name)
                        {
                            const size_t open_begin = open[i].second;
                            pairs.push_back(Tag{ open_begin, text.find('>', open_begin) + 1, lt, gt + 1 });
                            open.resize(i);
                            break;
                        }
                    }
                }
                else if (lt + 1 < text.size() && (std::isalpha(static_cast<unsigned char>(text[lt + 1])) || text[lt + 1] == '_'))
                {
                    size_t            name_end;
                    const std::string name = nameAt(lt + 1, name_end);
                    if (!name.empty() && text[gt - 1] != '/')
                    {
                        open.emplace_back(name, lt);
                    }
                }
                lt = gt;
            }
            // The pairs around the caret, innermost first: the inner of
            // two nested pairs starts later.
            std::vector<const Tag*> around_caret;
            for (const Tag& tag : pairs)
            {
                if (tag.openBegin <= here && here < tag.closeEnd)
                {
                    around_caret.push_back(&tag);
                }
            }
            std::sort(around_caret.begin(), around_caret.end(), [](const Tag* a, const Tag* b) { return a->openBegin > b->openBegin; });
            const size_t level = static_cast<size_t>(llmax(1, count)) - 1;
            if (level >= around_caret.size())
            {
                return false;
            }
            const Tag& tag = *around_caret[level];
            out.range      = around ? ALTextRange(d.posAt(tag.openBegin), d.posAt(tag.closeEnd)) : ALTextRange(d.posAt(tag.openEnd), d.posAt(tag.closeBegin));
            return true;
        }
        case 'p':
        {
            // The paragraph: the lines around this one up to the blank
            // ones, and with them for `ap`.
            S32 first = from.line;
            S32 last  = from.line;
            const bool blank = lineBlank(d, from.line);
            while (first > 0 && lineBlank(d, first - 1) == blank)
            {
                --first;
            }
            while (last + 1 < d.lineCount() && lineBlank(d, last + 1) == blank)
            {
                ++last;
            }
            for (S32 n = 1; n < count && last + 1 < d.lineCount(); ++n)
            {
                const bool next_blank = lineBlank(d, last + 1);
                while (last + 1 < d.lineCount() && lineBlank(d, last + 1) == next_blank)
                {
                    ++last;
                }
            }
            if (around && !blank)
            {
                while (last + 1 < d.lineCount() && lineBlank(d, last + 1))
                {
                    ++last;
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

// --- operators ---------------------------------------------------------------------------

void ALVimKeymap::applyOperator(ALTextView& view, llwchar op, const Span& span_in, S32 count)
{
    const ALTextDocument& d    = view.document();
    Span                  span = span_in;
    span.range                 = span.range.normalised();
    const S32 first            = span.range.begin.line;
    const S32 last             = span.range.end.line;
    const bool editing         = !view.isReadOnly();

    // The text the operator works on: whole lines, a block's columns
    // line by line, or the stretch as it is.
    std::vector<ALTextRange> pieces;
    if (span.block)
    {
        const S32 c0 = llmin(span.range.begin.column, span.range.end.column);
        const S32 c1 = llmax(span.range.begin.column, span.range.end.column) + 1;
        for (S32 line = first; line <= last; ++line)
        {
            const S32 length = d.lineLength(line);
            pieces.emplace_back(ALTextPos(line, llmin(c0, length)), ALTextPos(line, llmin(c1, length)));
        }
    }
    else if (span.linewise)
    {
        pieces.emplace_back(d.lineStart(first), d.lineEnd(last));
    }
    else
    {
        pieces.push_back(span.range);
    }
    std::string text;
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        if (i > 0)
        {
            text += '\n';
        }
        text += d.text(pieces[i]);
    }

    switch (op)
    {
        case 'y':
            store(mRegister, text, span.linewise, span.block, true);
            if (span.linewise)
            {
                if (pieces.size() == 1 && last > first)
                {
                    say(std::to_string(last - first + 1) + " lines yanked");
                }
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
            store(mRegister, text, span.linewise, span.block, false);
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
                    view.deleteRange(whole);
                    const S32 line = llmin(first, d.lineCount() - 1);
                    view.setCaret(ALTextPos(line, firstNonBlankColumn(d, line)));
                    if (last > first)
                    {
                        say(std::to_string(last - first + 1) + " fewer lines");
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
                    mBlockColumn = pieces.front().begin.column;
                    mBlockAppend = false;
                }
                enterInsert(view, 1);
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
            // Each line one step further in, or one back: a tab, or the
            // tab's width in spaces where tabs are soft; a blank line is
            // left alone going in.
            const S32         width = llmax(1, view.getTabWidth());
            const std::string unit  = view.getSoftTabs() ? std::string(width, ' ') : std::string("\t");
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (S32 line = first; line <= last; ++line)
            {
                if (op == '>')
                {
                    if (lineBlank(d, line))
                    {
                        continue;
                    }
                    std::string in;
                    for (S32 n = 0; n < llmax(1, count); ++n)
                    {
                        in += unit;
                    }
                    edits.emplace_back(ALTextRange(d.lineStart(line), d.lineStart(line)), in);
                }
                else
                {
                    const std::string& text = d.line(line);
                    S32                cut  = 0;
                    for (S32 n = 0; n < llmax(1, count); ++n)
                    {
                        if (cut < static_cast<S32>(text.size()) && text[cut] == '\t')
                        {
                            ++cut;
                            continue;
                        }
                        S32 spaces = 0;
                        while (cut + spaces < static_cast<S32>(text.size()) && text[cut + spaces] == ' ' && spaces < width)
                        {
                            ++spaces;
                        }
                        if (spaces == 0)
                        {
                            break;
                        }
                        cut += spaces;
                    }
                    if (cut > 0)
                    {
                        edits.emplace_back(ALTextRange(d.lineStart(line), ALTextPos(line, cut)), std::string());
                    }
                }
            }
            if (!edits.empty())
            {
                view.replaceAll(std::move(edits));
            }
            moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
            if (last > first)
            {
                say(std::to_string(last - first + 1) + " lines " + (op == '>' ? ">" : "<") + "ed 1 time");
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

void ALVimKeymap::store(char name, std::string text, bool linewise, bool block, bool yanked)
{
    Register reg;
    reg.text     = std::move(text);
    reg.linewise = linewise;
    reg.block    = block;
    if (name == '_')
    {
        return;
    }
    if (name >= 'A' && name <= 'Z')
    {
        // Added to the named register, a line or straight on.
        Register& into = mRegisters[static_cast<char>(name - 'A' + 'a')];
        if (into.linewise || linewise)
        {
            into.text += (into.text.empty() ? "" : "\n") + reg.text;
            into.linewise = true;
        }
        else
        {
            into.text += reg.text;
        }
        mUnnamed = into;
        LLClipboard::instance().copyToClipboard(into.text, 0, static_cast<S32>(into.text.size()));
        return;
    }
    if (name >= 'a' && name <= 'z')
    {
        mRegisters[name] = reg;
    }
    else if (yanked)
    {
        mRegisters['0'] = reg;
    }
    // The unnamed register is the clipboard, which the world shares.
    mUnnamed = reg;
    LLClipboard::instance().copyToClipboard(reg.text, 0, static_cast<S32>(reg.text.size()));
}

ALVimKeymap::Register ALVimKeymap::fetch(char name) const
{
    if (name >= 'A' && name <= 'Z')
    {
        name = static_cast<char>(name - 'A' + 'a');
    }
    if ((name >= 'a' && name <= 'z') || name == '0')
    {
        const auto it = mRegisters.find(name);
        return it == mRegisters.end() ? Register() : it->second;
    }
    // What the clipboard holds now: ours, with how it was taken, or
    // somebody else's, taken as characters.
    Register    reg;
    std::string text;
    if (LLClipboard::instance().pasteFromClipboard(text))
    {
        reg.text = text;
        if (text == mUnnamed.text)
        {
            reg.linewise = mUnnamed.linewise;
            reg.block    = mUnnamed.block;
        }
    }
    return reg;
}

void ALVimKeymap::put(ALTextView& view, char name, bool after, S32 count)
{
    const ALTextDocument& d   = view.document();
    const Register        reg = fetch(name);
    if (reg.text.empty())
    {
        say("E353: Nothing in register " + std::string(1, name ? name : '"'), true);
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
        const S32 column = after && !atLineEnd(d, from) ? from.column + 1 : from.column;
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
                const S32   length = d.lineLength(line);
                std::string pad    = length < column ? std::string(column - length, ' ') : std::string();
                edits.emplace_back(ALTextRange(ALTextPos(line, llmin(column, length)), ALTextPos(line, llmin(column, length))), pad + piece);
            }
            else
            {
                tail += "\n" + std::string(column, ' ') + piece;
            }
        }
        if (!tail.empty())
        {
            edits.emplace_back(ALTextRange(d.end(), d.end()), tail);
        }
        view.replaceAll(std::move(edits));
        moveTo(view, ALTextPos(from.line, column));
        return;
    }
    std::string text;
    for (S32 n = 0; n < count; ++n)
    {
        text += reg.linewise && n > 0 ? "\n" + reg.text : reg.text;
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
    // On the last character put, as vim leaves it.
    moveTo(view, text.find('\n') == std::string::npos ? d.prevCluster(view.caret()) : where);
}

// --- insert mode ------------------------------------------------------------------------

void ALVimKeymap::enterInsert(ALTextView& view, S32 count)
{
    if (mMode == Mode::Normal)
    {
        view.undoJournal().beginGroup();
    }
    else if (mMode != Mode::Insert && mMode != Mode::Replace)
    {
        // From an operator: the group is the operator's, opened by it.
    }
    mMode        = Mode::Insert;
    mInsertCount = llmax(1, count);
    mTyped.clear();
    mInsertStart = view.caret();
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
    const ALTextDocument& d = view.document();
    // What was typed, again as many times as the count said.
    for (S32 n = 1; n < mInsertCount; ++n)
    {
        view.insertText(mTyped);
    }
    // And onto every other line of a block.
    if (mBlockInsert && !mTyped.empty() && mTyped.find('\n') == std::string::npos)
    {
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (S32 line = mBlockFirst + 1; line <= llmin(mBlockLast, d.lineCount() - 1); ++line)
        {
            const S32 length = d.lineLength(line);
            const S32 column = mBlockAppend ? llmin(mBlockColumn, length) : mBlockColumn;
            if (!mBlockAppend && column > length)
            {
                continue;
            }
            const std::string pad = length < column ? std::string(column - length, ' ') : std::string();
            const S32         at_ = llmin(column, length);
            edits.emplace_back(ALTextRange(ALTextPos(line, at_), ALTextPos(line, at_)), pad + mTyped);
        }
        if (!edits.empty())
        {
            const ALTextPos keep = view.caret();
            view.replaceAll(std::move(edits));
            view.setCaret(d.clamp(keep));
        }
    }
    mBlockInsert = false;
    view.undoJournal().endGroup();
    mMode = Mode::Normal;
    // The caret steps back onto the last character typed.
    const ALTextPos caret = view.caret();
    moveTo(view, caret.column > 0 ? d.prevCluster(caret) : caret);
    finishCommand(true);
    bump();
}

bool ALVimKeymap::insert(ALTextView& view, const Input& input)
{
    const ALTextDocument& d = view.document();
    if (!input.isChar)
    {
        const bool ctrl = (input.mask & MASK_CONTROL) != 0;
        if (input.key == KEY_ESCAPE || (ctrl && input.key == '[') || (ctrl && input.key == 'C'))
        {
            leaveInsert(view);
            return true;
        }
        if (ctrl && input.key == 'W')
        {
            view.perform(ALEditorCommand::DeleteWordLeft);
            return true;
        }
        if (ctrl && input.key == 'U')
        {
            view.deleteRange(ALTextRange(d.lineStart(view.caret().line), view.caret()));
            return true;
        }
        if (mReplaying || mPlaying > 0)
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
        if (input.key == KEY_RETURN || input.key == KEY_BACKSPACE || input.key == KEY_DELETE || input.key == KEY_TAB)
        {
            // Typed text with a break in it is no longer one block's.
            mTyped += input.key == KEY_RETURN ? "\n" : "";
        }
        // The rest is the view's: arrows, return, backspace, the keymap.
        return false;
    }
    if (mMode == Mode::Replace)
    {
        // Over the character under the caret, if there is one.
        const ALTextPos from = view.caret();
        if (!atLineEnd(d, from))
        {
            view.setSelection(ALTextRange(from, d.nextCluster(from)));
        }
        view.insertText(utf8Of(input.ch));
        mTyped += utf8Of(input.ch);
        return true;
    }
    mTyped += utf8Of(input.ch);
    if (mReplaying || mPlaying > 0)
    {
        // Fed by hand -- `.`, a macro, :normal -- so nobody else will.
        view.insertText(utf8Of(input.ch));
        return true;
    }
    // The view puts the character in.
    return false;
}

// --- visual mode ------------------------------------------------------------------------

void ALVimKeymap::enterVisual(ALTextView& view, Mode which)
{
    if (mMode == Mode::Normal)
    {
        mVisualAnchor = view.caret();
        mVisualCaret  = view.caret();
    }
    mMode = which;
    showVisual(view);
    bump();
}

void ALVimKeymap::leaveVisual(ALTextView& view)
{
    if (mMode == Mode::Visual || mMode == Mode::VisualLine || mMode == Mode::VisualBlock)
    {
        mVisualLast       = mMode;
        mVisualLastAnchor = mVisualAnchor;
        mVisualLastCaret  = mVisualCaret;
    }
    const ALTextPos caret = cursor(view);
    mMode                 = Mode::Normal;
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
    {
        editor->clearHighlights();
    }
    view.setCaret(caret);
    moveTo(view, caret);
    bump();
}

ALVimKeymap::Span ALVimKeymap::visualSpan(const ALTextView& view) const
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
        span.block = true;
        span.range = ALTextRange(ALTextPos(a.line, llmin(mVisualAnchor.column, caret.column)), ALTextPos(b.line, llmax(mVisualAnchor.column, caret.column)));
    }
    else
    {
        span.range = ALTextRange(a, atLineEnd(d, b) ? b : d.nextCluster(b));
    }
    return span;
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
        // The rows between, each lit over the block's columns where the
        // view can; the selection itself the corners.
        const Span span = visualSpan(view);
        if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
        {
            std::vector<ALTextRange> lit;
            const S32                c0 = span.range.begin.column;
            const S32                c1 = span.range.end.column + 1;
            for (S32 line = span.range.begin.line; line <= span.range.end.line; ++line)
            {
                const S32 length = d.lineLength(line);
                lit.emplace_back(ALTextPos(line, llmin(c0, length)), ALTextPos(line, llmin(c1, length)));
            }
            editor->setHighlights(std::move(lit));
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
        if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
        {
            editor->clearHighlights();
        }
    }
}

// --- searching --------------------------------------------------------------------------

bool ALVimKeymap::search(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool whole_word)
{
    const ALTextDocument& d = view.document();
    ALTextSearchOptions   options;
    options.regex     = !whole_word;
    options.wholeWord = whole_word;
    // A whole word -- * and # -- is looked for as it is, its case as
    // ignorecase alone says; a pattern in vim's spelling.
    const Pattern pattern_in = whole_word ? Pattern{ pattern, !mShared->ignoreCase } : patternOf(pattern);
    options.caseSensitive    = pattern_in.caseSensitive;
    std::string              error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = matchesOf(view, pattern_in, options, nullptr, error, wholes);
    if (!error.empty())
    {
        say("E486: " + error, true);
        return false;
    }
    if (matches.empty())
    {
        say("E486: Pattern not found: " + pattern, true);
        return false;
    }
    ALTextPos from = cursor(view);
    for (S32 n = 0; n < count; ++n)
    {
        const S32 index = ALTextSearch::nearest(matches, forward ? d.nextCluster(from) : from, forward);
        if (index < 0)
        {
            break;
        }
        from = matches[static_cast<size_t>(index)].begin;
    }
    // Every match lit, as hlsearch has it, until :noh or the caret
    // leaves them.
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view); editor && !isVisual())
    {
        editor->setHighlights(matches);
    }
    moveTo(view, from);
    return true;
}

// --- the : and / lines --------------------------------------------------------------------

bool ALVimKeymap::commandLine(ALTextView& view, const Input& input)
{
    if (!input.isChar)
    {
        switch (input.key)
        {
            case KEY_ESCAPE:
                mLine.clear();
                mMode = Mode::Normal;
                moveTo(view, view.caret());
                return true;
            case KEY_BACKSPACE:
                mHistoryAt = -1;
                if (mLine.empty())
                {
                    mMode = Mode::Normal;
                }
                else
                {
                    // The last character, whole.
                    size_t cut = mLine.size() - 1;
                    while (cut > 0 && (static_cast<unsigned char>(mLine[cut]) & 0xC0) == 0x80)
                    {
                        --cut;
                    }
                    mLine.erase(cut);
                }
                return true;
            case KEY_UP:
            case KEY_DOWN:
            {
                // The lines entered before that start as this one does,
                // older with Up and newer with Down, back to this one
                // past the newest.
                const std::vector<std::string>& history = historyOf(mLineKind);
                const S32                       n       = static_cast<S32>(history.size());
                if (mHistoryAt < 0)
                {
                    mHistoryPrefix = mLine;
                    mHistoryAt     = n;
                }
                S32 at = mHistoryAt;
                while (true)
                {
                    at += input.key == KEY_UP ? -1 : 1;
                    if (at < 0)
                    {
                        return true;
                    }
                    if (at >= n)
                    {
                        mHistoryAt = -1;
                        mLine      = mHistoryPrefix;
                        return true;
                    }
                    if (history[static_cast<size_t>(at)].compare(0, mHistoryPrefix.size(), mHistoryPrefix) == 0)
                    {
                        mHistoryAt = at;
                        mLine      = history[static_cast<size_t>(at)];
                        return true;
                    }
                }
            }
            case KEY_RETURN:
            {
                const std::string line = mLine;
                const llwchar     kind = mLineKind;
                mLine.clear();
                mMode      = Mode::Normal;
                mHistoryAt = -1;
                remember(kind, line);
                if (kind == ':')
                {
                    runCommand(view, line);
                }
                else if (!line.empty())
                {
                    mSearchPattern   = line;
                    mSearchForward   = kind == '/';
                    mSearchWholeWord = false;
                    search(view, line, mSearchForward, 1, false);
                }
                else if (!mSearchPattern.empty())
                {
                    search(view, mSearchPattern, kind == '/', 1, mSearchWholeWord);
                }
                if (mMode == Mode::Normal)
                {
                    moveTo(view, view.caret());
                }
                finishCommand(false);
                return true;
            }
            case KEY_TAB:
                return true;
            default:
                // A plain key's character follows; a control chord is
                // nobody's while the line is being typed.
                return (input.mask & (MASK_CONTROL | MASK_ALT)) != 0;
        }
    }
    if (input.ch == '\r' || input.ch == '\n')
    {
        Input as_key;
        as_key.key = KEY_RETURN;
        return commandLine(view, as_key);
    }
    mLine += utf8Of(input.ch);
    mHistoryAt = -1;
    return true;
}

void ALVimKeymap::share(std::shared_ptr<Shared> shared)
{
    if (shared)
    {
        mShared    = std::move(shared);
        mHistoryAt = -1;
    }
}

// --- :s asking about each match -------------------------------------------------

void ALVimKeymap::askNext(ALTextView& view)
{
    if (mConfirming.at >= mConfirming.edits.size())
    {
        endConfirming(view);
        return;
    }
    // The match shown as the selection, so that the question is plainly
    // about it; the ones still to come lit, as vim lights them.
    view.setSelection(mConfirming.edits[mConfirming.at].first);
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
    {
        std::vector<ALTextRange> left;
        for (size_t k = mConfirming.at; k < mConfirming.edits.size(); ++k)
        {
            left.push_back(mConfirming.edits[k].first);
        }
        editor->setHighlights(std::move(left));
    }
    bump();
}

void ALVimKeymap::applyConfirmed(ALTextView& view, size_t index)
{
    const ALTextRange r = mConfirming.edits[index].first.normalised();
    const std::string t = mConfirming.edits[index].second;
    if (!view.replaceAll({ { r, t } }))
    {
        return;
    }
    if (r.begin.line != mConfirming.lastLine)
    {
        ++mConfirming.lines;
    }
    mConfirming.lastLine = r.begin.line;
    ++mConfirming.made;
    // Where the replaced stretch now ends, and how the ones after move.
    const S32       t_lines = static_cast<S32>(std::count(t.begin(), t.end(), '\n'));
    const size_t    last_nl = t.rfind('\n');
    const S32       t_last  = static_cast<S32>(last_nl == std::string::npos ? t.size() : t.size() - last_nl - 1);
    const ALTextPos new_end = t_lines == 0 ? ALTextPos(r.begin.line, r.begin.column + t_last) : ALTextPos(r.begin.line + t_lines, t_last);
    const S32       delta_lines = new_end.line - r.end.line;
    auto            moved       = [&](ALTextPos p) {
        if (p.line == r.end.line && !(p < r.end))
        {
            return ALTextPos(new_end.line, p.column + new_end.column - r.end.column);
        }
        if (p.line > r.end.line)
        {
            return ALTextPos(p.line + delta_lines, p.column);
        }
        return p;
    };
    for (size_t k = index + 1; k < mConfirming.edits.size(); ++k)
    {
        ALTextRange& f = mConfirming.edits[k].first;
        f              = ALTextRange(moved(f.begin), moved(f.end));
    }
}

bool ALVimKeymap::confirmKey(ALTextView& view, const Input& input)
{
    if (!input.isChar)
    {
        if (input.key == KEY_ESCAPE)
        {
            endConfirming(view);
            return true;
        }
        // Control-E and Control-Y scroll while the question stands, as
        // they do in vim, the match staying where it is.
        if ((input.mask & MASK_CONTROL) && (input.key == 'E' || input.key == 'Y'))
        {
            view.setScrollY(view.scrollY() + (input.key == 'E' ? 1 : -1) * view.layout().rowHeight());
            return true;
        }
        // A plain key's character follows; a chord or a Return is nobody's.
        return input.key == KEY_RETURN || (input.mask & (MASK_CONTROL | MASK_ALT)) != 0;
    }
    switch (input.ch)
    {
        case 'y':
            applyConfirmed(view, mConfirming.at++);
            askNext(view);
            return true;
        case 'n':
            ++mConfirming.at;
            askNext(view);
            return true;
        case 'l':
            applyConfirmed(view, mConfirming.at++);
            endConfirming(view);
            return true;
        case 'a':
            while (mConfirming.at < mConfirming.edits.size())
            {
                applyConfirmed(view, mConfirming.at++);
            }
            endConfirming(view);
            return true;
        case 'q':
            endConfirming(view);
            return true;
        default:
            return true;
    }
}

void ALVimKeymap::endConfirming(ALTextView& view)
{
    const ALTextDocument& d = view.document();
    mMode                   = Mode::Normal;
    view.undoJournal().endGroup();
    if (mConfirming.lastLine >= 0)
    {
        const S32 line = llclamp(mConfirming.lastLine, 0, d.lineCount() - 1);
        moveTo(view, ALTextPos(line, firstNonBlankColumn(d, line)));
    }
    else
    {
        moveTo(view, view.caret());
    }
    if (mConfirming.made > 1)
    {
        say(std::to_string(mConfirming.made) + " substitutions on " + std::to_string(mConfirming.lines) + " line" + (mConfirming.lines == 1 ? "" : "s"));
    }
    const bool changed = mConfirming.made > 0;
    mConfirming        = Confirming();
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
    {
        editor->clearHighlights();
    }
    finishCommand(changed);
    bump();
}

void ALVimKeymap::remember(llwchar kind, const std::string& line)
{
    const size_t MOST = 50;
    if (line.empty())
    {
        return;
    }
    std::vector<std::string>& history = historyOf(kind);
    history.erase(std::remove(history.begin(), history.end(), line), history.end());
    history.push_back(line);
    if (history.size() > MOST)
    {
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - MOST));
    }
}

void ALVimKeymap::runCommand(ALTextView& view, const std::string& line_in)
{
    const ALTextDocument& d    = view.document();
    std::string           line = line_in;
    LLStringUtil::trim(line);
    if (line.empty())
    {
        return;
    }
    // A range first: %, '<,'>, a number, ., $, or two of those with a
    // comma; then the command.
    S32    first = view.caret().line;
    S32    last  = first;
    bool   ranged = false;
    size_t at_   = 0;
    auto   lineNumber = [&](S32& out) {
        if (at_ >= line.size())
        {
            return false;
        }
        if (line[at_] == '.')
        {
            out = view.caret().line;
            ++at_;
        }
        else if (line[at_] == '$')
        {
            out = d.lineCount() - 1;
            ++at_;
        }
        else if (line.compare(at_, 2, "'<") == 0 || line.compare(at_, 2, "'>") == 0)
        {
            const ALTextPos a = std::min(mVisualLastAnchor, mVisualLastCaret);
            const ALTextPos b = std::max(mVisualLastAnchor, mVisualLastCaret);
            out               = line[at_ + 1] == '<' ? a.line : b.line;
            at_ += 2;
        }
        else if (isDigit(line[at_]))
        {
            S32 n = 0;
            while (at_ < line.size() && isDigit(line[at_]))
            {
                n = n * 10 + (line[at_++] - '0');
            }
            out = n - 1;
        }
        else
        {
            return false;
        }
        // An offset: .+3, $-1.
        if (at_ < line.size() && (line[at_] == '+' || line[at_] == '-'))
        {
            const bool plus = line[at_++] == '+';
            S32        n    = 0;
            bool       any  = false;
            while (at_ < line.size() && isDigit(line[at_]))
            {
                n   = n * 10 + (line[at_++] - '0');
                any = true;
            }
            out += (plus ? 1 : -1) * (any ? n : 1);
        }
        out = llclamp(out, 0, d.lineCount() - 1);
        return true;
    };
    if (line[0] == '%')
    {
        first  = 0;
        last   = d.lineCount() - 1;
        ranged = true;
        at_    = 1;
    }
    else if (lineNumber(first))
    {
        last   = first;
        ranged = true;
        if (at_ < line.size() && line[at_] == ',')
        {
            ++at_;
            if (!lineNumber(last))
            {
                last = first;
            }
        }
        if (last < first)
        {
            std::swap(first, last);
        }
    }
    std::string rest = line.substr(at_);
    LLStringUtil::trim(rest);
    if (rest.empty())
    {
        if (ranged)
        {
            // A line number alone goes there.
            moveTo(view, ALTextPos(last, firstNonBlankColumn(d, last)));
        }
        return;
    }
    // The command's name: letters, or one symbol.
    size_t name_end = 0;
    while (name_end < rest.size() && ((rest[name_end] >= 'a' && rest[name_end] <= 'z') || (rest[name_end] >= 'A' && rest[name_end] <= 'Z')))
    {
        ++name_end;
    }
    if (name_end == 0)
    {
        name_end = 1;
    }
    std::string name = rest.substr(0, name_end);
    std::string args = rest.substr(name_end);
    // The bang belongs to the name: q!, w!.
    if (!args.empty() && args[0] == '!')
    {
        name += '!';
        args.erase(0, 1);
    }
    LLStringUtil::trim(args);

    if (name == "s" || name == "substitute" || name == "&" || name == "~")
    {
        // :& and :&& do the last one again; :~ likewise, on the last
        // pattern searched for, which here is the same one.
        if (!substitute(view, first, last, name == "s" || name == "substitute" ? args : "&" + args))
        {
            return;
        }
        finishCommand(true);
        return;
    }
    if (name == "d" || name == "delete")
    {
        Span span;
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
        applyOperator(view, 'd', span, 1);
        finishCommand(true);
        return;
    }
    if (name == "y" || name == "yank")
    {
        Span span;
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
        applyOperator(view, 'y', span, 1);
        return;
    }
    if (name == ">" || name == "<")
    {
        Span span;
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
        applyOperator(view, name[0], span, 1);
        finishCommand(true);
        return;
    }
    if (name == "g" || name == "global" || name == "v" || name == "vglobal" || name == "g!" || name == "global!")
    {
        if (global(view, first, last, ranged, args, name[0] == 'v' || name.back() == '!'))
        {
            finishCommand(true);
        }
        return;
    }
    if (name == "normal" || name == "norm" || name == "normal!" || name == "norm!")
    {
        // The keys as if typed in normal mode, on each line of the range
        // in turn, from the line's first character; whatever mode they
        // leave behind is left.
        const std::vector<Input> inputs = decodeInputs(args);
        for (S32 line = first; line <= last && line < d.lineCount(); ++line)
        {
            if (mMode != Mode::Normal)
            {
                Input escape;
                escape.key = KEY_ESCAPE;
                feed(view, escape);
            }
            view.setCaret(ALTextPos(line, 0));
            moveTo(view, view.caret());
            if (!play(view, inputs))
            {
                break;
            }
        }
        if (mMode == Mode::Insert || mMode == Mode::Replace || isVisual())
        {
            Input escape;
            escape.key = KEY_ESCAPE;
            feed(view, escape);
        }
        return;
    }
    if (name == "noh" || name == "nohlsearch")
    {
        if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
        {
            editor->clearHighlights();
        }
        return;
    }
    if (name == "set" || name == "se")
    {
        // The view's own settings; the rest the host's.
        std::string option = args;
        std::string value;
        if (const size_t eq = option.find('='); eq != std::string::npos)
        {
            value = option.substr(eq + 1);
            option.erase(eq);
        }
        const bool off = option.compare(0, 2, "no") == 0;
        if (off)
        {
            option.erase(0, 2);
        }
        if (option == "wrap")
        {
            view.setWordWrap(!off);
            return;
        }
        if (option == "et" || option == "expandtab")
        {
            view.setSoftTabs(!off);
            return;
        }
        if ((option == "ts" || option == "tabstop" || option == "sw" || option == "shiftwidth") && !value.empty())
        {
            view.setTabWidth(llclamp(std::atoi(value.c_str()), 1, 16));
            return;
        }
        if (option == "ic" || option == "ignorecase")
        {
            mShared->ignoreCase = !off;
            return;
        }
        if (option == "scs" || option == "smartcase")
        {
            mShared->smartCase = !off;
            return;
        }
        if (mHooks.command && mHooks.command(view, "set", args))
        {
            return;
        }
        say("E518: Unknown option: " + args, true);
        return;
    }
    if (mHooks.command && mHooks.command(view, name, args))
    {
        return;
    }
    say("E492: Not an editor command: " + line, true);
}

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

bool ALVimKeymap::global(ALTextView& view, S32 first, S32 last, bool ranged, const std::string& spec, bool invert)
{
    // g/pattern/command over the whole text unless a range was given;
    // the command the : line's own, run on each line the pattern picks
    // out, from the last up so that a deletion moves nothing still to
    // come.
    if (spec.empty())
    {
        say("E35: No previous regular expression", true);
        return false;
    }
    const ALTextDocument& d   = view.document();
    const char            sep = spec[0];
    size_t                p1  = 1;
    std::string           pattern;
    while (p1 < spec.size() && spec[p1] != sep)
    {
        if (spec[p1] == '\\' && p1 + 1 < spec.size() && spec[p1 + 1] == sep)
        {
            pattern += sep;
            p1 += 2;
            continue;
        }
        pattern += spec[p1++];
    }
    std::string command = p1 < spec.size() ? spec.substr(p1 + 1) : std::string();
    LLStringUtil::trim(command);
    if (pattern.empty())
    {
        pattern = mSearchPattern;
    }
    if (pattern.empty())
    {
        say("E35: No previous regular expression", true);
        return false;
    }
    mSearchPattern   = pattern;
    mSearchWholeWord = false;
    if (!ranged)
    {
        // The whole text; the empty line after a final newline is no
        // line of the text's, as vim has none, so it is not picked out.
        first = 0;
        last  = d.lineCount() - 1;
        if (last > 0 && d.lineLength(last) == 0)
        {
            --last;
        }
    }
    ALTextSearchOptions options;
    options.regex         = true;
    const Pattern pattern_in = patternOf(pattern);
    options.caseSensitive    = pattern_in.caseSensitive;
    const ALTextRange        scope(d.lineStart(first), d.lineEnd(last));
    std::string              error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = matchesOf(view, pattern_in, options, &scope, error, wholes);
    if (!error.empty())
    {
        say("E486: " + error, true);
        return false;
    }
    std::vector<S32> lines;
    for (S32 line = first; line <= last; ++line)
    {
        const bool hit = std::any_of(matches.begin(), matches.end(), [line](const ALTextRange& m) { return m.begin.line == line; });
        if (hit != invert)
        {
            lines.push_back(line);
        }
    }
    if (lines.empty())
    {
        say("E486: Pattern not found: " + pattern, true);
        return false;
    }
    if (command.empty())
    {
        // Nothing to do to them: the last one is where the caret goes,
        // and how many there were is said.
        moveTo(view, ALTextPos(lines.back(), firstNonBlankColumn(d, lines.back())));
        say(std::to_string(lines.size()) + " line" + (lines.size() == 1 ? "" : "s"));
        return true;
    }
    // One step to undo for the lot.
    view.undoJournal().beginGroup();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
        const S32 line = *it;
        if (line >= d.lineCount())
        {
            continue;
        }
        runCommand(view, std::to_string(line + 1) + command);
        if (mMessageError)
        {
            break;
        }
    }
    view.undoJournal().endGroup();
    return !mMessageError;
}

ALVimKeymap::Pattern ALVimKeymap::patternOf(const std::string& vim, std::optional<bool> force_case) const
{
    // Vim's magic spelling to Perl's: with a backslash, ( ) | + ? = { }
    // < > are the engine's own ( ) | + ? ? { } \b \b, and without one
    // they are themselves; \v makes what follows very magic, where they
    // are the engine's own bare and themselves with a backslash; \V very
    // nomagic, where only ^ $ and the backslash items are special. \zs
    // is \K; \ze looks ahead at the rest; \{-} is *?; the classes \a \l
    // \u \x \o \h \i \k are brackets; \c and \C say how case is matched;
    // a bracket expression is copied through as it stands.
    Pattern             out;
    std::optional<bool> case_in_pattern;
    enum class Magic : U8
    {
        Magic,
        Very,
        None
    };
    Magic magic    = Magic::Magic;
    bool  looking  = false;
    bool  zs_seen  = false;
    // Where each \K was put, for the pattern without them.
    std::vector<size_t> k_at;
    auto  literal  = [&](char c) {
        static const std::string specials("\\^$.|?*+()[]{}");
        if (specials.find(c) != std::string::npos)
        {
            out.regex += '\\';
        }
        out.regex += c;
    };
    // %[abc]: a, ab or abc -- each item optional and the next only with
    // it. `at` is the [; the index of the ] comes back, or npos.
    auto optionalSequence = [&](size_t at) {
        const size_t close = vim.find(']', at + 1);
        if (close == std::string::npos)
        {
            return std::string::npos;
        }
        S32 opened = 0;
        for (size_t k = at + 1; k < close; ++k)
        {
            out.regex += "(?:";
            if (vim[k] == '\\' && k + 1 < close)
            {
                out.regex += '\\';
                out.regex += vim[++k];
            }
            else
            {
                literal(vim[k]);
            }
            ++opened;
        }
        for (S32 k = 0; k < opened; ++k)
        {
            out.regex += ")?";
        }
        return close;
    };
    // How deep in the engine's brackets the output is, so that a \zs at
    // the top can split the pattern into groups.
    S32   depth    = 0;
    for (size_t i = 0; i < vim.size(); ++i)
    {
        const char c = vim[i];
        if (c == '\\' && i + 1 < vim.size())
        {
            const char n = vim[++i];
            switch (n)
            {
                case 'v': magic = Magic::Very; continue;
                case 'm': magic = Magic::Magic; continue;
                case 'M':
                case 'V': magic = Magic::None; continue;
                case 'c': case_in_pattern = false; continue;
                case 'C': case_in_pattern = true; continue;
                case '(':
                    out.regex += magic == Magic::Very ? "\\(" : "(";
                    depth += magic == Magic::Very ? 0 : 1;
                    continue;
                case ')':
                    out.regex += magic == Magic::Very ? "\\)" : ")";
                    depth -= magic == Magic::Very ? 0 : 1;
                    continue;
                case '|': out.regex += magic == Magic::Very ? "\\|" : "|"; continue;
                case '+': out.regex += magic == Magic::Very ? "\\+" : "+"; continue;
                case '?':
                case '=': out.regex += magic == Magic::Very ? std::string(1, n) : "?"; continue;
                case '<':
                case '>': out.regex += magic == Magic::Very ? std::string(1, n) : "\\b"; continue;
                case '{':
                {
                    if (magic == Magic::Very)
                    {
                        out.regex += "\\{";
                        continue;
                    }
                    // \{n,m}, \{-n,m} lazy, \{} for *, \{-} for *?; the
                    // closing brace may carry a backslash of its own.
                    size_t close = vim.find('}', i + 1);
                    if (close == std::string::npos)
                    {
                        out.regex += "\\{";
                        continue;
                    }
                    std::string body = vim.substr(i + 1, close - i - 1);
                    if (!body.empty() && body.back() == '\\')
                    {
                        body.pop_back();
                    }
                    const bool lazy = !body.empty() && body[0] == '-';
                    if (lazy)
                    {
                        body.erase(0, 1);
                    }
                    if (body.empty())
                    {
                        out.regex += "*";
                    }
                    else if (body == ",")
                    {
                        out.regex += "*";
                    }
                    else
                    {
                        out.regex += "{" + body + "}";
                    }
                    if (lazy)
                    {
                        out.regex += "?";
                    }
                    i = close;
                    continue;
                }
                case 'z':
                    if (i + 1 < vim.size() && vim[i + 1] == 's')
                    {
                        // At the top level, what came before is put in a
                        // group of its own and the rest in the group the
                        // match is; inside brackets, the engine's \K.
                        if (depth == 0 && !zs_seen)
                        {
                            out.regex   = "(?:" + out.regex + ")(";
                            out.matchGroup = 1;
                            zs_seen     = true;
                        }
                        else
                        {
                            // The engine's \K, and a note of where it went,
                            // so that the pattern without it can be had.
                            k_at.push_back(out.regex.size());
                            out.regex += "\\K";
                            zs_seen = true;
                        }
                        ++i;
                        continue;
                    }
                    if (i + 1 < vim.size() && vim[i + 1] == 'e')
                    {
                        out.regex += "(?=";
                        looking = true;
                        ++i;
                        continue;
                    }
                    literal('z');
                    continue;
                case '%':
                {
                    if (i + 1 < vim.size() && vim[i + 1] == '(')
                    {
                        out.regex += "(?:";
                        ++depth;
                        ++i;
                        continue;
                    }
                    if (i + 1 < vim.size() && vim[i + 1] == '[')
                    {
                        const size_t close = optionalSequence(i + 1);
                        if (close == std::string::npos)
                        {
                            literal('%');
                            continue;
                        }
                        i = close;
                        continue;
                    }
                    // A character by its code: \%d123 \%x7b \%o173 \%u007b.
                    if (i + 1 < vim.size() && (vim[i + 1] == 'd' || vim[i + 1] == 'x' || vim[i + 1] == 'o' || vim[i + 1] == 'u' || vim[i + 1] == 'U'))
                    {
                        const char how   = vim[i + 1];
                        const int  radix = how == 'd' ? 10 : how == 'o' ? 8 : 16;
                        size_t     k     = i + 2;
                        U32        code  = 0;
                        S32        taken = 0;
                        while (k < vim.size() && taken < (how == 'U' ? 8 : how == 'u' ? 4 : how == 'x' ? 2 : 12))
                        {
                            const char h = vim[k];
                            int        v = -1;
                            if (h >= '0' && h <= '9') v = h - '0';
                            else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                            if (v < 0 || v >= radix)
                            {
                                break;
                            }
                            code = code * static_cast<U32>(radix) + static_cast<U32>(v);
                            ++k;
                            ++taken;
                        }
                        if (taken > 0)
                        {
                            // As the bytes it is, each escaped where it is
                            // anything to the engine.
                            for (const char b : utf8Of(static_cast<llwchar>(code)))
                            {
                                literal(b);
                            }
                            i = k - 1;
                            continue;
                        }
                    }
                    // The file's ends: \%^ and \%$.
                    if (i + 1 < vim.size() && (vim[i + 1] == '^' || vim[i + 1] == '$'))
                    {
                        Pattern::Where place;
                        place.kind       = vim[i + 1] == '^' ? Pattern::Where::Kind::FileStart : Pattern::Where::Kind::FileEnd;
                        place.afterStart = zs_seen;
                        out.where.push_back(place);
                        ++i;
                        continue;
                    }
                    // The places: \%V \%# \%23l \%<23l \%>23l \%23c \%23v.
                    size_t k    = i + 1;
                    S32    side = 0;
                    if (k < vim.size() && (vim[k] == '<' || vim[k] == '>'))
                    {
                        side = vim[k] == '<' ? -1 : 1;
                        ++k;
                    }
                    S32 number = 0;
                    bool digits = false;
                    while (k < vim.size() && vim[k] >= '0' && vim[k] <= '9')
                    {
                        number = number * 10 + (vim[k++] - '0');
                        digits = true;
                    }
                    if (k < vim.size())
                    {
                        Pattern::Where place;
                        place.side   = side;
                        place.number = number;
                        bool known   = true;
                        switch (vim[k])
                        {
                            case 'V': place.kind = Pattern::Where::Kind::Visual; known = !digits; break;
                            case '#': place.kind = Pattern::Where::Kind::Caret; known = !digits; break;
                            case 'l': place.kind = Pattern::Where::Kind::Line; known = digits; break;
                            case 'c':
                            case 'v': place.kind = Pattern::Where::Kind::Column; known = digits; break;
                            default: known = false; break;
                        }
                        if (known)
                        {
                            place.afterStart = zs_seen;
                            out.where.push_back(place);
                            i = k;
                            continue;
                        }
                    }
                    literal('%');
                    continue;
                }
                case '_':
                    // \_s and the like: the class with a line break in it,
                    // which the search must then be let cross.
                    if (i + 1 < vim.size())
                    {
                        const char cls  = vim[++i];
                        out.acrossLines = true;
                        if (cls == '.')
                        {
                            out.regex += "[\\s\\S]";
                        }
                        else if (cls == '[')
                        {
                            // \_[abc]: the bracket expression with a line
                            // break in it.
                            const size_t close = vim.find(']', i + 1);
                            if (close == std::string::npos)
                            {
                                out.regex += "\\[";
                            }
                            else
                            {
                                out.regex += "[\\n" + vim.substr(i + 1, close - i - 1) + "]";
                                i = close;
                            }
                        }
                        else
                        {
                            out.regex += "(?:\\" + std::string(1, cls) + "|\\n)";
                        }
                        continue;
                    }
                    literal('_');
                    continue;
                case 'a': out.regex += "[A-Za-z]"; continue;
                case 'A': out.regex += "[^A-Za-z]"; continue;
                case 'l': out.regex += "[a-z]"; continue;
                case 'L': out.regex += "[^a-z]"; continue;
                case 'u': out.regex += "[A-Z]"; continue;
                case 'U': out.regex += "[^A-Z]"; continue;
                case 'x': out.regex += "[0-9A-Fa-f]"; continue;
                case 'X': out.regex += "[^0-9A-Fa-f]"; continue;
                case 'o': out.regex += "[0-7]"; continue;
                case 'O': out.regex += "[^0-7]"; continue;
                case 'h': out.regex += "[A-Za-z_]"; continue;
                case 'H': out.regex += "[^A-Za-z_]"; continue;
                case 'i':
                case 'k': out.regex += "[A-Za-z0-9_]"; continue;
                case 'I':
                case 'K': out.regex += "[A-Za-z_]"; continue;
                case 'e': out.regex += "\\x1b"; continue;
                default:
                    // \s \S \d \D \w \W \n \t \r \b \. \* \[ \] \/ and the
                    // rest: as they are, a backslash before a letter or a
                    // symbol the engine reads the same way; \n is a line's
                    // end, which the search must be let cross.
                    out.acrossLines |= n == 'n';
                    out.regex += '\\';
                    out.regex += n;
                    continue;
            }
        }
        if (c == '[' && magic != Magic::None)
        {
            // A bracket expression through to its close, as it stands; an
            // unclosed [ is itself.
            size_t j = i + 1;
            if (j < vim.size() && vim[j] == '^')
            {
                ++j;
            }
            if (j < vim.size() && vim[j] == ']')
            {
                ++j;
            }
            while (j < vim.size() && vim[j] != ']')
            {
                if (vim[j] == '\\' && j + 1 < vim.size())
                {
                    ++j;
                }
                ++j;
            }
            if (j < vim.size())
            {
                out.acrossLines |= vim.find("\\n", i) < j;
                out.regex.append(vim, i, j - i + 1);
                i = j;
            }
            else
            {
                out.regex += "\\[";
            }
            continue;
        }
        switch (magic)
        {
            case Magic::Very:
                switch (c)
                {
                    case '<':
                    case '>': out.regex += "\\b"; break;
                    case '=': out.regex += "?"; break;
                    case '%':
                        if (i + 1 < vim.size() && vim[i + 1] == '(')
                        {
                            out.regex += "(?:";
                            ++depth;
                            ++i;
                        }
                        else if (i + 1 < vim.size() && vim[i + 1] == '[')
                        {
                            const size_t close = optionalSequence(i + 1);
                            if (close == std::string::npos)
                            {
                                literal('%');
                            }
                            else
                            {
                                i = close;
                            }
                        }
                        else
                        {
                            literal('%');
                        }
                        break;
                    case '{':
                    {
                        const size_t close = vim.find('}', i + 1);
                        std::string  body  = close == std::string::npos ? std::string() : vim.substr(i + 1, close - i - 1);
                        const bool   lazy  = !body.empty() && body[0] == '-';
                        if (close == std::string::npos)
                        {
                            out.regex += "\\{";
                            break;
                        }
                        if (lazy)
                        {
                            body.erase(0, 1);
                        }
                        out.regex += body.empty() || body == "," ? std::string("*") : "{" + body + "}";
                        if (lazy)
                        {
                            out.regex += "?";
                        }
                        i = close;
                        break;
                    }
                    case '~':
                        for (const char r : mLastReplacement)
                        {
                            literal(r);
                        }
                        break;
                    case '(': out.regex += c; ++depth; break;
                    case ')': out.regex += c; --depth; break;
                    default: out.regex += c; break;
                }
                break;
            case Magic::Magic:
                switch (c)
                {
                    case '^':
                    case '$':
                    case '.':
                    case '*': out.regex += c; break;
                    case '~':
                        // The last replacement, as the text it is.
                        for (const char r : mLastReplacement)
                        {
                            literal(r);
                        }
                        break;
                    default: literal(c); break;
                }
                break;
            case Magic::None:
                switch (c)
                {
                    case '^': out.regex += i == 0 ? "^" : "\\^"; break;
                    case '$': out.regex += i + 1 == vim.size() ? "$" : "\\$"; break;
                    default: literal(c); break;
                }
                break;
        }
    }
    if (looking)
    {
        out.regex += ")";
    }
    if (out.matchGroup)
    {
        out.regex += ")";
    }
    if (!k_at.empty() && !out.where.empty())
    {
        out.wholeRegex = out.regex;
        for (size_t k = k_at.size(); k-- > 0;)
        {
            out.wholeRegex.erase(k_at[k], 2);
        }
    }
    if (case_in_pattern)
    {
        out.caseSensitive = *case_in_pattern;
    }
    else if (force_case)
    {
        out.caseSensitive = *force_case;
    }
    else if (mShared->ignoreCase)
    {
        out.caseSensitive = mShared->smartCase && std::any_of(vim.begin(), vim.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    }
    else
    {
        out.caseSensitive = true;
    }
    return out;
}

std::vector<ALTextRange> ALVimKeymap::matchesOf(ALTextView& view, const Pattern& pattern, ALTextSearchOptions options, const ALTextRange* scope, std::string& error,
                                                std::vector<ALTextPos>& wholes) const
{
    const ALTextDocument& d = view.document();
    options.matchGroup      = pattern.matchGroup;
    options.acrossLines     = pattern.acrossLines;
    std::vector<ALTextRange> matches = ALTextSearch::matches(d, pattern.regex, options, scope, &error, &wholes);
    if (error.empty() && !pattern.wholeRegex.empty())
    {
        // The pattern without its \K matches the same stretches whole:
        // each match here is the whole one that ends where it does.
        std::string              other;
        std::vector<ALTextRange> full = ALTextSearch::matches(d, pattern.wholeRegex, options, scope, &other);
        for (size_t i = 0; i < matches.size() && i < wholes.size(); ++i)
        {
            for (const ALTextRange& f : full)
            {
                if (f.end == matches[i].end)
                {
                    wholes[i] = f.begin;
                    break;
                }
            }
        }
    }
    if (error.empty())
    {
        constrain(view, pattern, matches, wholes);
    }
    return matches;
}

void ALVimKeymap::constrain(ALTextView& view, const Pattern& pattern, std::vector<ALTextRange>& matches, const std::vector<ALTextPos>& wholes) const
{
    if (pattern.where.empty())
    {
        return;
    }
    const ALTextDocument& d = view.document();
    // The last visual area, as a range: whole lines for a line-wise one,
    // the character under either end included otherwise; a block is the
    // lines between its ends and the columns between them.
    ALTextRange visual;
    S32         block_left = -1, block_right = -1;
    if (mVisualLast != Mode::Normal)
    {
        const ALTextPos a = mVisualLastAnchor < mVisualLastCaret ? mVisualLastAnchor : mVisualLastCaret;
        const ALTextPos b = mVisualLastAnchor < mVisualLastCaret ? mVisualLastCaret : mVisualLastAnchor;
        visual            = mVisualLast == Mode::VisualLine ? ALTextRange(d.lineStart(a.line), d.lineEnd(b.line)) : ALTextRange(a, d.nextCluster(b));
        if (mVisualLast == Mode::VisualBlock)
        {
            block_left  = llmin(mVisualLastAnchor.column, mVisualLastCaret.column);
            block_right = llmax(mVisualLastAnchor.column, mVisualLastCaret.column);
            visual      = ALTextRange(d.lineStart(a.line), d.lineEnd(b.line));
        }
    }
    const ALTextPos caret = view.caret();
    auto            allowed = [&](size_t index) {
        const ALTextRange match = matches[index].normalised();
        for (const Pattern::Where& place : pattern.where)
        {
            // Before a \zs, the place is the whole match's start; after
            // it, the reported match's.
            const ALTextPos at = place.afterStart || index >= wholes.size() ? match.begin : wholes[index];
            switch (place.kind)
            {
                case Pattern::Where::Kind::FileStart:
                    if (at != d.start())
                    {
                        return false;
                    }
                    break;
                case Pattern::Where::Kind::FileEnd:
                {
                    // The end of the text, or of its last line where the
                    // text ends with a line break, which vim does not
                    // count as a line.
                    const ALTextPos end   = d.end();
                    const bool      last  = match.end == end;
                    const bool      above = end.column == 0 && end.line > 0 && match.end == d.lineEnd(end.line - 1);
                    if (!last && !above)
                    {
                        return false;
                    }
                    break;
                }
                case Pattern::Where::Kind::Visual:
                    if (mVisualLast == Mode::Normal || at < visual.begin || !(at < visual.end))
                    {
                        return false;
                    }
                    if (block_left >= 0 && (at.column < block_left || at.column > block_right))
                    {
                        return false;
                    }
                    break;
                case Pattern::Where::Kind::Caret:
                    if (at != caret)
                    {
                        return false;
                    }
                    break;
                case Pattern::Where::Kind::Line:
                {
                    const S32 line = at.line + 1;
                    if (place.side < 0 ? line >= place.number : place.side > 0 ? line <= place.number : line != place.number)
                    {
                        return false;
                    }
                    break;
                }
                case Pattern::Where::Kind::Column:
                {
                    const S32 column = at.column + 1;
                    if (place.side < 0 ? column >= place.number : place.side > 0 ? column <= place.number : column != place.number)
                    {
                        return false;
                    }
                    break;
                }
            }
        }
        return true;
    };
    std::vector<ALTextRange> kept;
    for (size_t i = 0; i < matches.size(); ++i)
    {
        if (allowed(i))
        {
            kept.push_back(matches[i]);
        }
    }
    matches.swap(kept);
}

std::string ALVimKeymap::replacementOf(const std::string& with) const
{
    // Vim's spelling to the search engine's: & and \0 are the whole
    // match, \1 to \9 the groups, \r and \n a line break, \t a tab;
    // \& \~ and \\ are themselves; \u \U \l \L \e \E change case as the
    // engine has them; a $ is only a $. The ~ was put in before this.
    std::string out;
    out.reserve(with.size() + 8);
    for (size_t i = 0; i < with.size(); ++i)
    {
        const char c = with[i];
        if (c == '\\' && i + 1 < with.size())
        {
            const char n = with[++i];
            switch (n)
            {
                case '&': out += '&'; break;
                case '~': out += '~'; break;
                case '\\': out += "\\\\"; break;
                case 'r':
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'e': out += "\\E"; break;
                case 'u':
                case 'U':
                case 'l':
                case 'L':
                case 'E': out += '\\'; out += n; break;
                default:
                    if (n >= '0' && n <= '9')
                    {
                        out += '$';
                        out += n;
                    }
                    else
                    {
                        out += n;
                    }
                    break;
            }
        }
        else if (c == '&')
        {
            out += "$&";
        }
        else if (c == '$')
        {
            out += "$$";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

bool ALVimKeymap::substitute(ALTextView& view, S32 first, S32 last, const std::string& spec)
{
    // s/pattern/replacement/flags, with whatever follows s as the
    // separator; g for every match on a line, i for any case, I for
    // case as written, n to count without changing, e to say nothing
    // where nothing matches, & first to keep the last flags. Nothing
    // after s, or & or &&, does the last one again -- on the last
    // pattern searched for, with the last replacement -- without its
    // flags, or with them after &&.
    std::string        pattern, with, flags;
    const std::string& rest = spec;
    if (rest.empty() || rest[0] == '&')
    {
        if (mSearchPattern.empty())
        {
            say("E35: No previous regular expression", true);
            return false;
        }
        pattern = mSearchPattern;
        with    = mLastReplacement;
        flags   = rest.empty() ? std::string() : rest.substr(1);
    }
    else
    {
        const char sep  = rest[0];
        size_t     p1   = 1;
        auto       part = [&](std::string& out) {
            out.clear();
            while (p1 < rest.size() && rest[p1] != sep)
            {
                if (rest[p1] == '\\' && p1 + 1 < rest.size() && rest[p1 + 1] == sep)
                {
                    out += sep;
                    p1 += 2;
                    continue;
                }
                out += rest[p1++];
            }
            const bool closed = p1 < rest.size();
            if (closed)
            {
                ++p1;
            }
            return closed;
        };
        part(pattern);
        if (part(with))
        {
            flags = rest.substr(p1);
        }
        if (pattern.empty())
        {
            pattern = mSearchPattern;
        }
        if (pattern.empty())
        {
            say("E35: No previous regular expression", true);
            return false;
        }
        // The replacement as it reads with ~ put in, which is what the
        // next ~ means.
        std::string expanded;
        for (size_t i = 0; i < with.size(); ++i)
        {
            if (with[i] == '\\' && i + 1 < with.size())
            {
                expanded += with[i];
                expanded += with[++i];
            }
            else if (with[i] == '~')
            {
                expanded += mLastReplacement;
            }
            else
            {
                expanded += with[i];
            }
        }
        with = expanded;
    }
    LLStringUtil::trim(flags);
    if (!flags.empty() && flags[0] == '&')
    {
        flags = mLastSubstituteFlags + flags.substr(1);
    }
    mSearchPattern        = pattern;
    mSearchWholeWord      = false;
    mLastReplacement      = with;
    mLastSubstituteFlags  = flags;
    const bool every      = flags.find('g') != std::string::npos;
    const bool anycase    = flags.find('i') != std::string::npos;
    const bool exactcase  = flags.find('I') != std::string::npos;
    const bool count_only = flags.find('n') != std::string::npos;
    const bool quiet      = flags.find('e') != std::string::npos;
    const bool asking     = flags.find('c') != std::string::npos;
    const ALTextDocument& d = view.document();
    ALTextSearchOptions   options;
    options.regex         = true;
    const Pattern pattern_in = patternOf(pattern, exactcase ? std::optional<bool>(true) : anycase ? std::optional<bool>(false) : std::nullopt);
    options.caseSensitive    = pattern_in.caseSensitive;
    options.matchGroup       = pattern_in.matchGroup;
    const ALTextRange     scope(d.lineStart(first), d.lineEnd(last));
    std::string           error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = matchesOf(view, pattern_in, options, &scope, error, wholes);
    if (!error.empty())
    {
        say("E486: " + error, true);
        return false;
    }
    if (matches.empty())
    {
        if (!quiet)
        {
            say("E486: Pattern not found: " + pattern, true);
        }
        return false;
    }
    const std::string                                format = replacementOf(with);
    std::vector<std::pair<ALTextRange, std::string>> edits;
    S32                                              seen_line = -1;
    S32                                              lines     = 0;
    for (size_t m = 0; m < matches.size(); ++m)
    {
        const ALTextRange& match = matches[m];
        if (!every && match.begin.line == seen_line)
        {
            continue;
        }
        if (match.begin.line != seen_line)
        {
            ++lines;
        }
        seen_line = match.begin.line;
        // The replacement is worked out over the whole of what the pattern
        // matched, where a \zs made the match a part of it, and put in
        // place of the part.
        const ALTextRange whole = pattern_in.matchGroup && m < wholes.size() ? ALTextRange(wholes[m], match.end) : match;
        edits.emplace_back(match, count_only ? std::string() : ALTextSearch::replacement(d, whole, pattern_in.regex, options, format));
    }
    const S32 count = static_cast<S32>(edits.size());
    if (count_only)
    {
        say(std::to_string(count) + " match" + (count == 1 ? "" : "es") + " on " + std::to_string(lines) + " line" + (lines == 1 ? "" : "s"));
        return false;
    }
    if (asking && !view.isReadOnly())
    {
        // Each match asked about in turn; the command finishes when the
        // asking ends, so nothing is done here.
        mConfirming       = Confirming();
        mConfirming.edits = std::move(edits);
        mMode             = Mode::Confirm;
        // Everything said yes to is one step to undo, as the :s is.
        view.undoJournal().beginGroup();
        askNext(view);
        return false;
    }
    // The caret goes to the last line substituted on, as it will lie
    // once the edits are in: the lines the ones before it add or take.
    S32 landing = seen_line;
    for (size_t i = 0; i + 1 < edits.size(); ++i)
    {
        const ALTextRange match = edits[i].first.normalised();
        landing += static_cast<S32>(std::count(edits[i].second.begin(), edits[i].second.end(), '\n')) - (match.end.line - match.begin.line);
    }
    landing += static_cast<S32>(std::count(edits.back().second.begin(), edits.back().second.end(), '\n'));
    if (view.isReadOnly() || !view.replaceAll(std::move(edits)))
    {
        return false;
    }
    landing = llclamp(landing, 0, d.lineCount() - 1);
    moveTo(view, ALTextPos(landing, firstNonBlankColumn(d, landing)));
    if (count > 1)
    {
        say(std::to_string(count) + " substitutions on " + std::to_string(lines) + " line" + (lines == 1 ? "" : "s"));
    }
    return true;
}
