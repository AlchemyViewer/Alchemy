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

#include "altextchars.h"

#include "alcodeeditor.h"
#include "alsaid.h"
#include "altextsearch.h"
#include "llclipboard.h"
#include "llstring.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
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
    bool isWordByte(char c) { return alWordByte(c); }

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

    // The case of a stretch changed: swapped, lowered or raised, by
    // codepoint.
    std::string recased(const std::string& text, llwchar how)
    {
        return alRecased(text, static_cast<char>(how));
    }

    bool isDigit(llwchar ch) { return ch >= '0' && ch <= '9'; }

    // A count typed before a command: the digits as a number, at least one;
    // no more than this.
    constexpr S32 MAX_COUNT = 100000;
    S32 countOr(S32 count, S32 fallback = 1) { return count > 0 ? count : fallback; }
    // Keys pending, and an operator, that are two typed: gr, and gc.
    constexpr llwchar PENDING_GR       = 0xE000;
    constexpr llwchar COMMENT_OPERATOR = 0xE001;
    // Surround's: ys, an operator; the character a ys stretch waits for;
    // ds and cs waiting for the pair to take away or change, and cs for
    // the pair it becomes.
    constexpr llwchar SURROUND_OPERATOR  = 0xE002;
    constexpr llwchar SURROUND_WITH      = 0xE003;
    constexpr llwchar DELETE_SURROUND    = 0xE004;
    constexpr llwchar CHANGE_SURROUND    = 0xE005;
    constexpr llwchar CHANGE_SURROUND_TO = 0xE006;
    std::string shownKey(llwchar key)
    {
        switch (key)
        {
            case PENDING_GR:         return "gr";
            case COMMENT_OPERATOR:   return "gc";
            case SURROUND_OPERATOR:
            case SURROUND_WITH:      return "ys";
            case DELETE_SURROUND:    return "ds";
            case CHANGE_SURROUND:
            case CHANGE_SURROUND_TO: return "cs";
            default:                 return utf8Of(key);
        }
    }
    // What a surround character puts round a stretch, as surround.vim has
    // it: an opening bracket the pair with a space inside each, a closing
    // one -- or b, r, B, a -- the pair alone, and any other mark itself on
    // both sides. A letter, a digit or a blank is none.
    bool surroundPair(llwchar ch, std::string& open, std::string& close)
    {
        switch (ch)
        {
            case '(': open = "( "; close = " )"; return true;
            case ')':
            case 'b': open = "("; close = ")"; return true;
            case '[': open = "[ "; close = " ]"; return true;
            case ']':
            case 'r': open = "["; close = "]"; return true;
            case '{': open = "{ "; close = " }"; return true;
            case '}':
            case 'B': open = "{"; close = "}"; return true;
            case '<':
            case '>':
            case 'a': open = "<"; close = ">"; return true;
            default:  break;
        }
        if (ch < 0x21 || ch == 0x7F || (ch < 0x80 && std::isalnum(static_cast<int>(ch))))
        {
            return false;
        }
        open = close = utf8Of(ch);
        return true;
    }
    // A letter of a : command's name.
    bool isNameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    // :set, and its local and global forms, which are one here.
    bool isSetCommand(const std::string& name)
    {
        return name == "set" || name == "se" || name == "setl" || name == "setlocal" || name == "setg" || name == "setglobal";
    }
    // An operator's count and its motion's together -- 3d2w is six words --
    // held to the same most, which their product would otherwise pass far
    // enough to wrap round.
    S32 countTimes(S32 a, S32 b) { return static_cast<S32>(llmin<S64>(static_cast<S64>(a) * static_cast<S64>(b), MAX_COUNT)); }
    // The most text a count may make of text there already -- a register
    // put, what an insert typed -- which the count alone does not bound:
    // a hundred thousand of a register a hundred kilobytes long is ten
    // gigabytes. A megabyte is more than any script.
    constexpr size_t MAX_COUNT_TEXT = 1024 * 1024;
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
        if (in.mask & CONTROL)
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
                    mask |= name[0] == 'C' ? CONTROL : name[0] == 'S' ? MASK_SHIFT : MASK_ALT;
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
        case Mode::Search:      return utf8Of(mLineKind) + mLine;
        case Mode::Confirm:
        {
            // The question, saying how many lines the match runs over
            // where it runs over more than one, since the selection is
            // the only other sign of that.
            const bool                 have  = mConfirming.at < mConfirming.edits.size();
            const ALTextRange          match = have ? mConfirming.edits[mConfirming.at].first.normalised() : ALTextRange();
            const S32                  lines = match.end.line - match.begin.line + 1;
            LLStringUtil::format_map_t args;
            args["[WITH]"]  = have ? mConfirming.edits[mConfirming.at].second : std::string();
            args["[COUNT]"] = std::to_string(lines);
            return recording + (lines > 1 ? said("VimConfirmReplaceLines", "replace with [WITH] (over [COUNT] lines) (y/n/a/q/l)?", args)
                                          : said("VimConfirmReplace", "replace with [WITH] (y/n/a/q/l)?", args));
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
    // Insert mode stays insert mode wherever the click lands; a line
    // being typed is not the mouse's.
    if (mMode == Mode::Insert || mMode == Mode::Replace || mMode == Mode::Command || mMode == Mode::Search || mMode == Mode::Confirm)
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
            mMode = Mode::Visual;
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
    const std::string kind = utf8Of(mLineKind);
    line                   = kind + mLine;
    caret                  = static_cast<S32>(kind.size() + llmin(mLineCursor, mLine.size()));
    return true;
}

void ALVimKeymap::say(const std::string& message, bool error)
{
    mMessage      = message;
    mMessageError = error;
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
    bump();
    return taken.value_or(true);
}

void ALVimKeymap::drain(ALTextView& view, std::deque<Held>& queue, bool final, bool stop_on_error, std::optional<bool>* taken)
{
    S32 depth = 0;
    while (!queue.empty())
    {
        // The keys at the front that may be mapped, in the mode as it is
        // now: each key fed may change it.
        const U8           mode = mapMode();
        std::vector<Input> front;
        for (const Held& held : queue)
        {
            if (!held.remap)
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
            // starts with the keys themselves, as vim has it.
            const ALVimMappings::Mapping mapping = *match.full;
            queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(mapping.from.size()));
            bool starts_with_itself = mapping.to.size() >= mapping.from.size();
            for (size_t i = 0; starts_with_itself && i < mapping.from.size(); ++i)
            {
                starts_with_itself = mapping.to[i].sameAs(mapping.from[i]);
            }
            for (size_t i = mapping.to.size(); i-- > 0;)
            {
                Held held;
                held.input = mapping.to[i];
                held.remap = !mapping.noremap && !(i == 0 && starts_with_itself);
                queue.push_front(held);
            }
            continue;
        }
        // No mapping: the first key as it is, and what follows it looked
        // at again.
        depth              = 0;
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
        if (stop_on_error && mMessageError)
        {
            queue.clear();
            return;
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
    bump();
}

// --- the dispatch ------------------------------------------------------------------

bool ALVimKeymap::feed(ALTextView& view, const Input& input)
{
    followDocument(view);
    mTypedByView = false;
    mVerticalMove = false;
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
            if (mMode == Mode::Search)
            {
                incrementalSearch(view);
            }
            else if (mIncrementalShown)
            {
                endIncremental(view);
            }
            break;
        case Mode::Confirm:
            taken = confirmKey(view, input);
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
    // column; one still being typed -- a count, an operator -- keeps
    // it, and so does a key nobody took, whose character is to come.
    if (taken && (mMode == Mode::Normal || isVisual()) && !mVerticalMove && mCount == 0 && !mOperator && !mPending)
    {
        mWantColumn = -1;
    }
    // Ctrl-O's one command done -- nothing pending, back in normal mode --
    // inserting again where it left off; not by the Ctrl-O itself.
    if (mOneCommand == 2)
    {
        mOneCommand = 1;
    }
    else if (mOneCommand == 1 && taken && mMode == Mode::Normal && mCount == 0 && !mOperator && !mPending && !mRegister)
    {
        mOneCommand = 0;
        mMode       = Mode::Insert;
        view.undoJournal().beginGroup();
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
    mMarksSlide = view.document().onChanged([this](const ALTextDocument::Edit& edit) { slideMarks(edit); });
}

void ALVimKeymap::slideMarks(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // A mark past the edit moves with the text; one inside what was
    // taken out lands where that began.
    const ALTextRange removed = edit.range.normalised();
    auto              slide   = [&](ALTextPos& pos) {
        if (pos < removed.begin)
        {
            return;
        }
        pos = removed.end <= pos ? edit.slidPast(pos) : removed.begin;
    };
    for (auto& [name, pos] : mMarks)
    {
        slide(pos);
    }
    slide(mVisualLastAnchor);
    slide(mVisualLastCaret);
}

bool ALVimKeymap::matchBracketIn(ALTextView& view, const ALTextPos& from, ALTextPos& match) const
{
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
    {
        return editor->matchBracketAt(from, match);
    }
    return matchBracket(view.document(), from, match);
}

bool ALVimKeymap::play(ALTextView& view, const std::vector<Input>& inputs, bool remap)
{
    if (mPlaying >= 100)
    {
        say(said("VimTooRecursive", "E169: Command too recursive"), true);
        return false;
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
        ok = !mMessageError;
    }
    else
    {
        for (const Input& in : inputs)
        {
            feed(view, in);
            if (mMessageError)
            {
                ok = false;
                break;
            }
        }
    }
    --mPlaying;
    return ok;
}

void ALVimKeymap::clearPending()
{
    mSurroundWaiting = false;
    mCount         = 0;
    mRegister      = 0;
    mOperator      = 0;
    mOperatorCount = 0;
    mPending       = 0;
    mObjectKind    = 0;
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
                case 'O':
                case 'I':
                {
                    // The jump list is the host's, which goes between its
                    // tabs as well: back, or forward again, the count times.
                    const S32 times = countOr(mCount);
                    clearPending();
                    for (S32 n = 0; n < times && mHooks.command; ++n)
                    {
                        mHooks.command(view, input.key == 'O' ? "back" : "forward", std::string());
                    }
                    return true;
                }
                case ']':
                    clearPending();
                    runCommand(view, "tag");
                    return true;
                case 'T':
                    clearPending();
                    runCommand(view, "pop");
                    return true;
                case 'G':
                    clearPending();
                    runCommand(view, "file");
                    return true;
                case '6':
                case '^':
                {
                    // The alternate tab, or with a count the Nth: :buffer.
                    const S32 given = mCount;
                    clearPending();
                    runCommand(view, given > 0 ? "buffer " + std::to_string(given) : std::string("buffer #"));
                    return true;
                }
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
            case SURROUND_WITH:
            {
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
            case CHANGE_SURROUND:
                mSurroundOld = ch;
                mPending     = CHANGE_SURROUND_TO;
                return true;
            case DELETE_SURROUND:
            case CHANGE_SURROUND_TO:
            {
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
            case '"':
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '"' || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '+' ||
                    ch == '*')
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
                    if (mHooks.historyWindow && !history.empty())
                    {
                        // The host's window of them; what is picked comes
                        // back onto the line.
                        const llwchar kind = ch;
                        mHooks.historyWindow(view, kind, history, [this, &view, kind](const std::string& line, bool run) { takeLine(view, kind, line, run); });
                        return true;
                    }
                    mMode     = ch == ':' ? Mode::Command : Mode::Search;
                    mLineKind = ch;
                    mHistoryPrefix.clear();
                    mHistoryAt  = history.empty() ? -1 : static_cast<S32>(history.size()) - 1;
                    mLine       = history.empty() ? std::string() : history.back();
                    mLineCursor = mLine.size();
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
                        say(said("VimNoPreviousCommand", "E30: No previous command line"), true);
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
                    if (!play(view, inputs, true))
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
            case 'g':
                switch (ch)
                {
                    case 'g':
                        return command(view, 0x01);  // gg, as a motion the table knows
                    case 'c':
                        // Comments: an operator over lines, gcc a line, gc
                        // a visual selection's lines.
                        if (visual)
                        {
                            const Span span = visualSpan(view);
                            noteVisualOperation(span);
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
                        return command(view, 0x06);
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
                        // Inserting again where it last stopped.
                        clearPending();
                        if (!editing)
                        {
                            return true;
                        }
                        const auto mark = mMarks.find('^');
                        if (mark != mMarks.end())
                        {
                            view.setCaret(d.clamp(mark->second));
                        }
                        enterInsert(view, count);
                        return true;
                    }
                    case '*':
                    case '#':
                    {
                        // As * and #, the word anywhere, not only whole.
                        const ALTextRange word = d.wordAt(view.caret());
                        clearPending();
                        if (!word.empty())
                        {
                            mSearchPattern   = d.text(word);
                            mSearchForward   = ch == '*';
                            mSearchWholeWord = false;
                            mSearchOffset    = SearchOffset();
                            search(view, mSearchPattern, mSearchForward, count, false);
                        }
                        return true;
                    }
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
                        const std::optional<ALTextRange> match = matchNear(view, ch == 'n');
                        if (!match)
                        {
                            clearPending();
                            return true;
                        }
                        if (mOperator)
                        {
                            Span span;
                            span.range       = *match;
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
                            runCommand(view, "find " + name);
                        }
                        return true;
                    }
                    case 'd':
                    case 'D':
                        // The declaration: the host's definition, as :tag
                        // goes to it.
                        clearPending();
                        runCommand(view, "tag");
                        return true;
                    case 't':
                    case 'T':
                    {
                        // The host's tabs: gt the next, {N}gt the Nth; gT
                        // back one, or N.
                        const S32  given    = mCount;
                        const bool operated = mOperator != 0;
                        clearPending();
                        if (!operated)
                        {
                            const std::string tabs = ch == 't' ? "tabnext" : "tabprevious";
                            runCommand(view, given > 0 ? tabs + " " + std::to_string(given) : tabs);
                        }
                        return true;
                    }
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
                        // The end of the previous word: a motion the
                        // table knows, so an operator takes it too.
                        return command(view, ch == 'e' ? 0x02 : 0x03);
                    case 'j':
                    case 'k':
                        // A row of the display down or up, through a
                        // wrapped line.
                        return command(view, ch == 'j' ? 0x04 : 0x05);
                    default:
                        clearPending();
                        return true;
                }
            case 'z':
            {
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
            case PENDING_GR:
            {
                // grn rename, grr the references, gra the fixes and actions
                // at the caret: the editor's own.
                const ALEditorCommand command = ch == 'n' ? ALEditorCommand::Rename
                                                : ch == 'r' ? ALEditorCommand::FindReferences
                                                : ch == 'a' ? ALEditorCommand::QuickFix
                                                            : ALEditorCommand::None;
                clearPending();
                if (command != ALEditorCommand::None)
                {
                    view.perform(command);
                }
                return true;
            }
            case '[':
            case ']':
            {
                // ]d [d the problems, ]s [s the misspellings, [z ]z the fold
                // the caret is in.
                const bool    forward  = pending == ']';
                const S32     given    = mCount;
                const llwchar operated = mOperator;
                clearPending();
                if (ch == 'd')
                {
                    runCommand(view, std::string(forward ? "cnext" : "cprevious") + (given > 0 ? " " + std::to_string(given) : ""));
                }
                else if (ch == 's')
                {
                    misspelling(view, forward, countOr(given));
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
                    const ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view);
                    // From past the character the caret is on, going on:
                    // at a function's end already is not before it.
                    ALTextPos                  at = forward && ends ? d.nextCluster(cursor(view)) : cursor(view);
                    std::optional<ALTextRange> fn;
                    for (S32 n = 0; n < countOr(given) && editor; ++n)
                    {
                        const std::optional<ALTextRange> next = editor->functionFrom(at, forward, ends);
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
                            applyOperator(view, operated, span, 1);
                            finishCommand(operated != 'y');
                            return true;
                        }
                        moveTo(view, to);
                    }
                }
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
                if ((pending == '`' || pending == '\'') && m.to != cursor(view))
                {
                    noteJump(view, cursor(view));
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
        mCount = llmin(mCount * 10 + static_cast<S32>(ch - '0'), MAX_COUNT);
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
        // the count more.
        if (ch == op || (op == '~' && ch == '~') || (op == 'u' && ch == 'u') || (op == 'U' && ch == 'U') ||
            (op == COMMENT_OPERATOR && ch == 'c'))
        {
            const S32 lines = countTimes(countOr(mOperatorCount), count);
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
        if (ch == 'f' || ch == 'F' || ch == 't' || ch == 'T' || ch == '`' || ch == '\'' || ch == 'g' || ch == '[' || ch == ']')
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
        Motion m = motion(view, m_ch, countTimes(countOr(mOperatorCount), count), 0);
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
                if (ch == 'S')
                {
                    // The selection surrounded, as surround.vim's visual S:
                    // kept for the character that says with what.
                    const Span span = visualSpan(view);
                    noteVisualOperation(span);
                    leaveVisual(view);
                    clearPending();
                    mSurroundSpan    = span;
                    mPending         = SURROUND_WITH;
                    mSurroundWaiting = true;
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
                mRegisters.record(into, keys);
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
            const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(d, first, first + llmax(1, count - 1), false);
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
            mLineCursor = mLine.size();
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
            mLineCursor = 0;
            return true;
        case 'n':
        case 'N':
            if (mSearchPattern.empty())
            {
                say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
            }
            else
            {
                search(view, mSearchPattern, ch == 'n' ? mSearchForward : !mSearchForward, count, mSearchWholeWord, mSearchOffset);
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
            mSearchOffset    = SearchOffset();
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

    // A motion on its own; one of vim's jumps notes where it began.
    Motion m = motion(view, ch, count, 0);
    if (m.ok)
    {
        const ALTextPos from = cursor(view);
        const bool      jump = ch == 'G' || ch == 0x01 || ch == '%' || ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == 'H' ||
                          ch == 'M' || ch == 'L';
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
                m.to = ALTextPos(line, firstNonBlankColumn(d, line));
            }
            return m;
        }
        case 0x04:  // gj
        case 0x05:  // gk
        {
            // A row of the display at a time, by the layout's rows, at
            // the x the caret is drawn at.
            ALTextLayout& layout = view.layout();
            S32           row    = 0;
            const F32     x      = layout.xOf(from.line, from.column, &row);
            S32           line   = from.line;
            for (S32 n = 0; n < count; ++n)
            {
                if (ch == 0x04)
                {
                    if (row + 1 < layout.rowCount(line))
                    {
                        ++row;
                    }
                    else if (const S32 below = layout.visibleFrom(line + 1, 1); below >= 0)
                    {
                        line = below;
                        row  = 0;
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    if (row > 0)
                    {
                        --row;
                    }
                    else if (const S32 above = layout.visibleFrom(line - 1, -1); above >= 0)
                    {
                        line = above;
                        row  = layout.rowCount(line) - 1;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            m.to    = d.clamp(ALTextPos(line, layout.columnAt(line, row, x, true)));
            m.moved = m.to != from;
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
            // operator reaches the end, the caret sits on the last. And
            // every line's end from here on, for j and k.
            if (!mOperator && m.to.column > 0)
            {
                m.to = d.prevCluster(m.to);
            }
            mWantColumn   = S32_MAX;
            mVerticalMove = true;
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
        case 0x06:  // g_
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
        case 0x02:  // ge
        case 0x03:  // gE
        {
            // The end of the previous word, taken with it: out of the
            // word the caret is in, back over the blanks, onto the last
            // character of the one before.
            const bool big = ch == 0x03;
            for (S32 n = 0; n < count; ++n)
            {
                const S32 cls = classOf(at(d, m.to), big);
                if (!stepBack(d, m.to))
                {
                    break;
                }
                if (cls != 0)
                {
                    while (classOf(at(d, m.to), big) == cls && stepBack(d, m.to)) {}
                }
                while (classOf(at(d, m.to), big) == 0 && stepBack(d, m.to)) {}
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
            if (!matchBracketIn(view, open, close))
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
            const std::string& text = d.wholeText();
            const size_t       here = d.offsetOf(from);
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
        case 'f':
        {
            // A function, as the host knows them: af its lines whole, if the
            // lines of its body -- not a { alone under its header, nor what
            // closes it. A count, the ones around it.
            const ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view);
            std::optional<ALTextRange> fn = editor ? editor->functionAround(ALTextRange(from, from)) : std::nullopt;
            for (S32 n = 1; n < count && fn; ++n)
            {
                if (const std::optional<ALTextRange> outer = editor->functionAround(*fn))
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
                    say(alSaidCount("VimLinesYanked", last - first + 1, "1 line yanked", "[COUNT] lines yanked"));
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
                    mBlockColumn = pieces.front().begin.column;
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

void ALVimKeymap::store(char name, std::string text, bool linewise, bool block, bool yanked)
{
    mRegisters.store(name, std::move(text), linewise, block, yanked, mShared->unnamedClipboard);
}

ALVimKeymap::Register ALVimKeymap::fetch(char name) const
{
    return mRegisters.fetch(name, mShared->unnamedClipboard);
}

void ALVimKeymap::tooMuch(size_t bytes)
{
    say(said("VimCountTooLarge", "Too large a count: it would put in [SIZE] MB", { { "[SIZE]", llformat("%.1f", (F64)bytes / (1024.0 * 1024.0)) } }), true);
}

void ALVimKeymap::put(ALTextView& view, char name, bool after, S32 count, bool past)
{
    const ALTextDocument& d   = view.document();
    const Register        reg = fetch(name);
    if (reg.text.empty())
    {
        say(said("VimNothingInRegister", "E353: Nothing in register [REGISTER]", { { "[REGISTER]", std::string(1, name ? name : '"') } }), true);
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

// --- insert mode ------------------------------------------------------------------------

void ALVimKeymap::enterInsert(ALTextView& view, S32 count, bool grouped)
{
    if (mMode == Mode::Normal && !grouped)
    {
        view.undoJournal().beginGroup();
    }
    else if (mMode != Mode::Insert && mMode != Mode::Replace)
    {
        // From an operator: the group is the operator's, opened by it.
    }
    mMode        = Mode::Insert;
    mInsertCount = llmax(1, count);
    mWantColumn  = -1;
    mTyped.clear();
    mInsertRegister = false;
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
    // What was typed, again as many times as the count said: made once and
    // put in as one edit, not an edit a time.
    if (mInsertCount > 1 && !mTyped.empty())
    {
        const size_t size = mTyped.size() * static_cast<size_t>(mInsertCount - 1);
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
                again += mTyped;
            }
            view.insertText(again);
        }
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
    mBlockInsert    = false;
    mInsertRegister = false;
    mLastTyped      = mTyped;
    view.undoJournal().endGroup();
    mMode = Mode::Normal;
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
        mTyped += text;
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
            // One command of normal mode, then inserting again.
            view.undoJournal().endGroup();
            mMode       = Mode::Normal;
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
            // it, the caret staying on the character it was on.
            const ALTextPos    caret = view.caret();
            const S32          width = llmax(1, view.getTabWidth());
            const std::string& text  = d.line(caret.line);
            ALTextRange        range(d.lineStart(caret.line), d.lineStart(caret.line));
            std::string        with;
            if (input.key == 'T')
            {
                with = view.getSoftTabs() ? std::string(width, ' ') : std::string("\t");
            }
            else
            {
                S32 cut = 0;
                if (!text.empty() && text[0] == '\t')
                {
                    cut = 1;
                }
                while (cut < width && cut < static_cast<S32>(text.size()) && text[cut] == ' ' && text[0] == ' ')
                {
                    ++cut;
                }
                if (cut == 0)
                {
                    return true;
                }
                range.end = ALTextPos(caret.line, cut);
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
    const ALTextDocument& d = view.document();
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
        const bool ctrl = (input.mask & CONTROL) != 0;
        if (input.key == KEY_ESCAPE || (ctrl && input.key == '[') || (ctrl && input.key == 'C'))
        {
            leaveInsert(view);
            return true;
        }
        if (ctrl && !(input.mask & (MASK_ALT | MASK_SHIFT)) && insertControl(view, input))
        {
            return true;
        }
        if (input.key == KEY_RETURN)
        {
            // Typed text with a break in it is no longer one block's.
            mTyped += "\n";
        }
        else if (input.key == KEY_TAB && !(input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)))
        {
            mTyped += view.tabText(view.caret());
        }
        else if (input.key == KEY_BACKSPACE && !mTyped.empty())
        {
            // Taking back what was typed takes it out of what is typed
            // again: `.` repeats what stood, as vim's does.
            size_t cut = mTyped.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(mTyped[cut]) & 0xC0) == 0x80)
            {
                --cut;
            }
            mTyped.erase(cut);
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
    if (mReplaying || mPlaying > 0 || mMapped > 0)
    {
        // Fed by hand -- `.`, a macro, :normal, a mapping -- so nobody
        // else will.
        view.insertText(utf8Of(input.ch));
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
        // A block: each of its lines' pieces surrounded.
        for (S32 line = range.begin.line; line <= range.end.line; ++line)
        {
            const S32 length = d.lineLength(line);
            if (range.begin.column >= length)
            {
                continue;
            }
            at_(ALTextPos(line, range.begin.column), open);
            at_(ALTextPos(line, llmin(range.end.column + 1, length)), close);
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

bool ALVimKeymap::search(ALTextView& view, const std::string& pattern, bool forward, S32 count, bool whole_word, const SearchOffset& offset)
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
        say(said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", error } }), true);
        return false;
    }
    if (matches.empty())
    {
        say(said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        return false;
    }
    const ALTextPos start = cursor(view);
    // From the last match where an offset left the caret by it: n after
    // /x/e goes on from that match, not from past its end.
    ALTextPos   from = offset.kind && !mLastMatch.empty() && offsetFrom(d, mLastMatch, offset) == start ? mLastMatch.begin : start;
    ALTextRange hit;
    for (S32 n = 0; n < count; ++n)
    {
        const S32 index = ALTextSearch::nearest(matches, forward ? d.nextCluster(from) : from, forward);
        if (index < 0)
        {
            break;
        }
        hit  = matches[static_cast<size_t>(index)];
        from = hit.begin;
    }
    if (!hit.empty())
    {
        mLastMatch = hit;
        from       = offsetFrom(d, hit, offset);
    }
    // Every match lit, as hlsearch has it, until :noh or the caret
    // leaves them.
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view); editor && !isVisual() && mShared->highlightSearch)
    {
        editor->setHighlights(matches);
    }
    if (!mOperator && from != start)
    {
        noteJump(view, start);
    }
    moveTo(view, from);
    return true;
}

// --- the : and / lines --------------------------------------------------------------------

bool ALVimKeymap::commandLine(ALTextView& view, const Input& input)
{
    // The cursor moves by whole characters.
    auto back = [this](size_t at) {
        while (at > 0 && (static_cast<unsigned char>(mLine[--at]) & 0xC0) == 0x80) {}
        return at;
    };
    auto forward = [this](size_t at) {
        if (at < mLine.size())
        {
            ++at;
            while (at < mLine.size() && (static_cast<unsigned char>(mLine[at]) & 0xC0) == 0x80)
            {
                ++at;
            }
        }
        return at;
    };
    mLineCursor = llmin(mLineCursor, mLine.size());
    // Tab walks the completions; anything else keeps what it put on the
    // line and lets the rest go -- but Escape with them up only lets
    // them go, the word as typed back on the line.
    if (!input.isChar && input.key == KEY_TAB && !(input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)))
    {
        complete(view, !(input.mask & MASK_SHIFT));
        return true;
    }
    if (!input.isChar && input.key == KEY_ESCAPE && !mCompletion.items.empty())
    {
        mLine       = mLine.substr(0, mCompletion.wordStart) + mCompletion.typed + mCompletion.tail;
        mLineCursor = mCompletion.wordStart + mCompletion.typed.size();
        dropCompletion();
        return true;
    }
    dropCompletion();
    if (!input.isChar)
    {
        // Vim's own editing of the line: Control-B and Control-E to the
        // ends, Control-W a word back, Control-U to the start, Control-H
        // a character back.
        if ((input.mask & CONTROL) && !(input.mask & MASK_ALT))
        {
            switch (input.key)
            {
                case KEY_LEFT:
                case KEY_RIGHT:
                {
                    // A WORD, as with shift.
                    Input as_shift = input;
                    as_shift.mask  = MASK_SHIFT;
                    return commandLine(view, as_shift);
                }
                case 'B': mLineCursor = 0; return true;
                case 'E': mLineCursor = mLine.size(); return true;
                case 'U':
                    mLine.erase(0, mLineCursor);
                    mLineCursor = 0;
                    mHistoryAt  = -1;
                    return true;
                case 'W':
                {
                    // Blanks before the cursor, then the word or the run
                    // of other characters before them.
                    size_t at = mLineCursor;
                    while (at > 0 && mLine[at - 1] == ' ')
                    {
                        --at;
                    }
                    if (at > 0)
                    {
                        const bool word = isalnum(static_cast<unsigned char>(mLine[at - 1])) || mLine[at - 1] == '_' || static_cast<unsigned char>(mLine[at - 1]) >= 0x80;
                        while (at > 0 && mLine[at - 1] != ' ' &&
                               (isalnum(static_cast<unsigned char>(mLine[at - 1])) || mLine[at - 1] == '_' || static_cast<unsigned char>(mLine[at - 1]) >= 0x80) == word)
                        {
                            --at;
                        }
                    }
                    mLine.erase(at, mLineCursor - at);
                    mLineCursor = at;
                    mHistoryAt  = -1;
                    return true;
                }
                case 'H':
                {
                    Input as_key;
                    as_key.key = KEY_BACKSPACE;
                    return commandLine(view, as_key);
                }
                default:
                    return true;
            }
        }
        switch (input.key)
        {
            case KEY_ESCAPE:
                mLine.clear();
                mLineCursor = 0;
                mMode       = Mode::Normal;
                endIncremental(view);
                moveTo(view, view.caret());
                return true;
            case KEY_LEFT:
            case KEY_RIGHT:
            {
                // A character, or with shift a WORD -- what stands between
                // blanks -- as vim's <S-Left> and <S-Right> have it.
                const bool left = input.key == KEY_LEFT;
                if (!(input.mask & MASK_SHIFT))
                {
                    mLineCursor = left ? back(mLineCursor) : forward(mLineCursor);
                    return true;
                }
                size_t at = mLineCursor;
                if (left)
                {
                    while (at > 0 && mLine[at - 1] == ' ') { --at; }
                    while (at > 0 && mLine[at - 1] != ' ') { --at; }
                }
                else
                {
                    while (at < mLine.size() && mLine[at] != ' ') { ++at; }
                    while (at < mLine.size() && mLine[at] == ' ') { ++at; }
                }
                mLineCursor = at;
                return true;
            }
            case KEY_HOME: mLineCursor = 0; return true;
            case KEY_END: mLineCursor = mLine.size(); return true;
            case KEY_DELETE:
                if (mLineCursor < mLine.size())
                {
                    mLine.erase(mLineCursor, forward(mLineCursor) - mLineCursor);
                    mHistoryAt = -1;
                }
                return true;
            case KEY_BACKSPACE:
                mHistoryAt = -1;
                if (mLine.empty())
                {
                    mMode = Mode::Normal;
                }
                else if (mLineCursor > 0)
                {
                    // The character before the cursor, whole.
                    const size_t cut = back(mLineCursor);
                    mLine.erase(cut, mLineCursor - cut);
                    mLineCursor = cut;
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
                        mHistoryAt  = -1;
                        mLine       = mHistoryPrefix;
                        mLineCursor = mLine.size();
                        return true;
                    }
                    if (history[static_cast<size_t>(at)].compare(0, mHistoryPrefix.size(), mHistoryPrefix) == 0)
                    {
                        mHistoryAt  = at;
                        mLine       = history[static_cast<size_t>(at)];
                        mLineCursor = mLine.size();
                        return true;
                    }
                }
            }
            case KEY_RETURN:
            {
                const std::string line = mLine;
                const llwchar     kind = mLineKind;
                mLine.clear();
                mLineCursor = 0;
                mMode       = Mode::Normal;
                mHistoryAt  = -1;
                remember(kind, line);
                if (kind == ':')
                {
                    runCommand(view, line);
                }
                else
                {
                    // /pattern/offset: an empty pattern the last one, with
                    // the offset given, or the last one too where none is.
                    std::string pattern;
                    std::string offset_text;
                    splitOffset(line, kind, pattern, offset_text);
                    SearchOffset offset{};
                    const bool   has_offset = line.size() > pattern.size();
                    if (has_offset && !parseOffset(offset_text, offset))
                    {
                        endIncremental(view);
                        say(said("VimBadOffset", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", line } }), true);
                        finishCommand(false);
                        return true;
                    }
                    if (!pattern.empty())
                    {
                        mSearchPattern   = pattern;
                        mSearchWholeWord = false;
                        mSearchOffset    = offset;
                    }
                    else if (has_offset)
                    {
                        mSearchOffset = offset;
                    }
                    mSearchForward = kind == '/';
                    endIncremental(view);
                    if (!mSearchPattern.empty())
                    {
                        search(view, mSearchPattern, mSearchForward, 1, mSearchWholeWord, mSearchOffset);
                    }
                }
                if (mMode == Mode::Normal)
                {
                    moveTo(view, view.caret());
                }
                finishCommand(false);
                return true;
            }
            default:
                // A plain key's character follows; a control chord is
                // nobody's while the line is being typed.
                return (input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)) != 0;
        }
    }
    if (input.ch == '\r' || input.ch == '\n')
    {
        Input as_key;
        as_key.key = KEY_RETURN;
        return commandLine(view, as_key);
    }
    const std::string typed = utf8Of(input.ch);
    mLine.insert(mLineCursor, typed);
    mLineCursor += typed.size();
    mHistoryAt = -1;
    return true;
}

void ALVimKeymap::dropCompletion()
{
    if (!mCompletion.items.empty())
    {
        mCompletion = Completion();
        bump();
    }
}

bool ALVimKeymap::menu(std::vector<std::string>& items, S32& chosen) const
{
    if (mMode != Mode::Command || mCompletion.items.size() < 2)
    {
        return false;
    }
    items  = mCompletion.items;
    chosen = mCompletion.at;
    return true;
}

void ALVimKeymap::complete(ALTextView& view, bool forward)
{
    if (mMode != Mode::Command)
    {
        return;
    }
    if (mCompletion.items.empty())
    {
        // The word at the cursor: the command's name where the cursor is
        // in it -- past any range -- else the word after the last blank,
        // which is the command's to complete.
        size_t at = 0;
        while (at < mLine.size() && (isDigit(mLine[at]) || mLine[at] == '%' || mLine[at] == '.' || mLine[at] == '$' || mLine[at] == ',' || mLine[at] == '+' ||
                                     mLine[at] == '-' || mLine[at] == '\'' || mLine[at] == '<' || mLine[at] == '>' || mLine[at] == ' '))
        {
            ++at;
        }
        const size_t name_start = at;
        while (at < mLine.size() && (isNameChar(mLine[at]) || (at > name_start && mLine[at] == '_')))
        {
            ++at;
        }
        const size_t name_end = at;
        if (at < mLine.size() && mLine[at] == '!')
        {
            ++at;
        }
        std::string              command;
        size_t                   word_start = name_start;
        std::vector<std::string> found;
        if (mLineCursor <= name_end)
        {
            // The name itself: the keymap's own, the long forms, and the
            // host's.
            static const char* OWN[] = { "center",   "changes",  "cmap",     "cnoremap", "cunmap",   "delete",   "display",  "global",
                                         "imap",     "inoremap", "iunmap",   "join",     "left",     "let",      "map",      "mapclear",
                                         "mark",     "marks",    "nmap",     "nnoremap", "nohlsearch", "noremap", "normal",  "nunmap",
                                         "omap",     "onoremap", "ounmap",   "put",      "redo",     "registers", "retab",   "right",
                                         "set",      "substitute", "undo",   "unmap",    "vglobal",  "vmap",     "vnoremap", "vunmap",
                                         "xmap",     "xnoremap", "xunmap",   "yank" };
            found.assign(std::begin(OWN), std::end(OWN));
        }
        else
        {
            command    = mLine.substr(name_start, name_end - name_start);
            word_start = mLine.rfind(' ', mLineCursor > 0 ? mLineCursor - 1 : 0);
            word_start = word_start == std::string::npos ? at : word_start + 1;
            if (word_start < at)
            {
                word_start = at;
            }
            if (isSetCommand(command))
            {
                static const char* OPTIONS[] = { "clipboard=",  "clipboard=unnamed", "expandtab",     "hlsearch",  "ignorecase",
                                                 "incsearch",   "noexpandtab",       "nohlsearch",    "noignorecase", "noincsearch",
                                                 "nosmartcase", "notimeout",         "nowrap",        "shiftwidth=", "smartcase",
                                                 "tabstop=",    "timeout",           "timeoutlen=",   "wrap" };
                found.assign(std::begin(OPTIONS), std::end(OPTIONS));
            }
        }
        const std::string typed = mLine.substr(word_start, mLineCursor - word_start);
        if (mHooks.complete)
        {
            mHooks.complete(view, command, typed, found);
        }
        std::vector<std::string> items;
        for (const std::string& each : found)
        {
            if (each.size() > typed.size() && each.compare(0, typed.size(), typed) == 0)
            {
                items.push_back(each);
            }
        }
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        if (items.empty())
        {
            return;
        }
        mCompletion.items     = std::move(items);
        mCompletion.at        = forward ? 0 : static_cast<S32>(mCompletion.items.size()) - 1;
        mCompletion.wordStart = word_start;
        mCompletion.typed     = typed;
        mCompletion.tail      = mLine.substr(mLineCursor);
    }
    else
    {
        // On to the next, or back; the word as typed stands past either
        // end, as vim's wildmenu has it.
        const S32 n    = static_cast<S32>(mCompletion.items.size());
        mCompletion.at = mCompletion.at + (forward ? 1 : -1);
        if (mCompletion.at >= n)
        {
            mCompletion.at = -1;
        }
        else if (mCompletion.at < -1)
        {
            mCompletion.at = n - 1;
        }
    }
    const std::string& word = mCompletion.at < 0 ? mCompletion.typed : mCompletion.items[static_cast<size_t>(mCompletion.at)];
    mLine                   = mLine.substr(0, mCompletion.wordStart) + word + mCompletion.tail;
    mLineCursor             = mCompletion.wordStart + word.size();
    mHistoryAt              = -1;
    if (mCompletion.items.size() == 1)
    {
        // One answer is the answer; nothing to walk.
        mCompletion = Completion();
    }
    bump();
}

void ALVimKeymap::takeLine(ALTextView& view, llwchar kind, const std::string& text, bool run)
{
    clearPending();
    mCompletion = Completion();
    mMode       = kind == ':' ? Mode::Command : Mode::Search;
    mLineKind   = kind;
    mLine       = text;
    mLineCursor = mLine.size();
    mHistoryAt  = -1;
    mHistoryPrefix.clear();
    if (run)
    {
        // As Enter on the line: vim's window runs the row it is pressed
        // on. The line is still on the : history, to be recalled and
        // edited with Up.
        Input as_key;
        as_key.key = KEY_RETURN;
        commandLine(view, as_key);
    }
    bump();
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
        if ((input.mask & CONTROL) && (input.key == 'E' || input.key == 'Y'))
        {
            view.setScrollY(view.scrollY() + (input.key == 'E' ? 1 : -1) * view.layout().rowHeight());
            return true;
        }
        // A plain key's character follows; a chord or a Return is nobody's.
        return input.key == KEY_RETURN || (input.mask & (CONTROL | MASK_CONTROL | MASK_ALT)) != 0;
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
        say(substitutionsSaid(mConfirming.made, mConfirming.lines));
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

bool ALVimKeymap::lineAddress(ALTextView& view, const std::string& line, size_t& at_, S32& out) const
{
    const ALTextDocument& d = view.document();
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
        // The last line: not the empty one after a final line break,
        // which vim does not count as a line.
        out = d.lineCount() - 1;
        if (out > 0 && d.lineLength(out) == 0)
        {
            --out;
        }
        ++at_;
    }
    else if (line.compare(at_, 2, "'<") == 0 || line.compare(at_, 2, "'>") == 0)
    {
        const ALTextPos a = std::min(mVisualLastAnchor, mVisualLastCaret);
        const ALTextPos b = std::max(mVisualLastAnchor, mVisualLastCaret);
        out               = line[at_ + 1] == '<' ? a.line : b.line;
        at_ += 2;
    }
    else if (line[at_] == '\'' && at_ + 1 < line.size() && line[at_ + 1] >= 'a' && line[at_ + 1] <= 'z')
    {
        // A mark's line.
        const auto mark = mMarks.find(line[at_ + 1]);
        if (mark == mMarks.end())
        {
            return false;
        }
        out = mark->second.line;
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
}

// --- options ---------------------------------------------------------------------------

namespace
{
    // One option as :set is given it: its name, and what is done with it
    // -- set, unset, toggled (inv, !), shown (?), set back (&), given a
    // value (= or :), or added to or taken from (+= -=).
    struct OptionSetting
    {
        enum class Op : U8
        {
            On,
            Off,
            Toggle,
            Query,
            Default,
            Assign,
            Add,
            Remove
        };
        std::string name;
        Op          op = Op::On;
        std::string value;
    };

    OptionSetting optionSetting(const std::string& word)
    {
        typedef OptionSetting::Op Op;
        OptionSetting             out;
        std::string               rest = word;
        if (const size_t at = rest.find_first_of("=:"); at != std::string::npos)
        {
            out.value = rest.substr(at + 1);
            rest.erase(at);
            out.op = Op::Assign;
            if (!rest.empty() && (rest.back() == '+' || rest.back() == '-' || rest.back() == '^'))
            {
                out.op = rest.back() == '-' ? Op::Remove : Op::Add;
                rest.pop_back();
            }
        }
        else if (!rest.empty() && (rest.back() == '?' || rest.back() == '!' || rest.back() == '&'))
        {
            out.op = rest.back() == '?' ? Op::Query : rest.back() == '!' ? Op::Toggle : Op::Default;
            rest.pop_back();
        }
        else if (rest.compare(0, 3, "inv") == 0)
        {
            out.op = Op::Toggle;
            rest.erase(0, 3);
        }
        else if (rest.compare(0, 2, "no") == 0)
        {
            out.op = Op::Off;
            rest.erase(0, 2);
        }
        out.name = rest;
        return out;
    }

    // What follows :set, a word an option: split at blanks, a backslash
    // keeping the character after it.
    std::vector<std::string> optionWords(const std::string& args)
    {
        std::vector<std::string> out;
        std::string              word;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == '\\' && i + 1 < args.size())
            {
                word += args[++i];
            }
            else if (args[i] == ' ' || args[i] == '\t')
            {
                if (!word.empty())
                {
                    out.push_back(word);
                    word.clear();
                }
            }
            else
            {
                word += args[i];
            }
        }
        if (!word.empty())
        {
            out.push_back(word);
        }
        return out;
    }

    std::string badOption(const std::string& word)
    {
        return alSaid("VimBadOptionValue", "E474: Invalid argument: [OPTION]", { { "[OPTION]", word } });
    }

    // A boolean option: set, unset, toggled, set back to `fallback`, or
    // shown; a value is no value for it.
    bool setFlag(bool& flag, bool fallback, const OptionSetting& setting, const char* name, const std::string& word, std::string& shown,
                 std::string& error)
    {
        typedef OptionSetting::Op Op;
        switch (setting.op)
        {
            case Op::On:      flag = true; break;
            case Op::Off:     flag = false; break;
            case Op::Toggle:  flag = !flag; break;
            case Op::Default: flag = fallback; break;
            case Op::Query:   shown = (flag ? "  " : "no") + std::string(name); break;
            default:          error = badOption(word); break;
        }
        return true;
    }

    // A number option, from `least` to `most`: given, added to or taken
    // from, set back to `fallback`, or shown -- by its name alone, too.
    bool setNumber(S32& number, S32 fallback, S32 least, S32 most, const OptionSetting& setting, const char* name, const std::string& word,
                   std::string& shown, std::string& error)
    {
        typedef OptionSetting::Op Op;
        switch (setting.op)
        {
            case Op::On:
            case Op::Query:
                shown = llformat("  %s=%d", name, number);
                return true;
            case Op::Default:
                number = fallback;
                return true;
            case Op::Assign:
            case Op::Add:
            case Op::Remove:
            {
                const std::string& value = setting.value;
                if (value.empty() || value.size() > 6 || value.find_first_not_of("0123456789") != std::string::npos)
                {
                    error = badOption(word);
                    return true;
                }
                const S32 given = std::atoi(value.c_str());
                const S32 to    = setting.op == Op::Assign ? given : setting.op == Op::Add ? number + given : number - given;
                if (to < least || to > most)
                {
                    error = badOption(word);
                    return true;
                }
                number = to;
                return true;
            }
            default:
                error = badOption(word);
                return true;
        }
    }

    // One of the options the mode keeps, set; false where the option is
    // none of those.
    bool setSharedOption(ALVimKeymap::Shared& shared, const OptionSetting& setting, const std::string& word, std::string& shown,
                         std::string& error)
    {
        const std::string& name = setting.name;
        if (name == "ic" || name == "ignorecase")
        {
            return setFlag(shared.ignoreCase, false, setting, "ignorecase", word, shown, error);
        }
        if (name == "scs" || name == "smartcase")
        {
            return setFlag(shared.smartCase, false, setting, "smartcase", word, shown, error);
        }
        if (name == "is" || name == "incsearch")
        {
            return setFlag(shared.incrementalSearch, true, setting, "incsearch", word, shown, error);
        }
        if (name == "hls" || name == "hlsearch")
        {
            return setFlag(shared.highlightSearch, true, setting, "hlsearch", word, shown, error);
        }
        if (name == "to" || name == "timeout")
        {
            return setFlag(shared.timeout, true, setting, "timeout", word, shown, error);
        }
        if (name == "tm" || name == "timeoutlen")
        {
            return setNumber(shared.timeoutLength, 1000, 0, 100000, setting, "timeoutlen", word, shown, error);
        }
        if (name == "cb" || name == "clipboard")
        {
            // unnamed or unnamedplus, which are one clipboard here, among
            // what the value lists; none for the editor's own.
            typedef OptionSetting::Op Op;
            bool                      named = false;
            for (const std::string& part : LLStringUtil::getTokens(setting.value, ","))
            {
                named = named || part == "unnamed" || part == "unnamedplus";
            }
            switch (setting.op)
            {
                case Op::On:
                case Op::Query:
                    shown = std::string("  clipboard=") + (shared.unnamedClipboard ? "unnamed" : "");
                    break;
                case Op::Default:
                    shared.unnamedClipboard = true;
                    break;
                case Op::Assign:
                    if (named || setting.value.empty())
                    {
                        shared.unnamedClipboard = named;
                    }
                    else
                    {
                        error = badOption(word);
                    }
                    break;
                case Op::Add:
                case Op::Remove:
                    if (named)
                    {
                        shared.unnamedClipboard = setting.op == Op::Add;
                    }
                    break;
                default:
                    error = badOption(word);
                    break;
            }
            return true;
        }
        return false;
    }

    bool isViewOption(const std::string& name)
    {
        return name == "wrap" || name == "et" || name == "expandtab" || name == "ts" || name == "tabstop" || name == "sw" ||
               name == "shiftwidth" || name == "sts" || name == "softtabstop";
    }
}

// static
bool ALVimKeymap::setViewOption(ALTextView& view, const std::string& word, std::string& shown, std::string& error)
{
    const OptionSetting setting = optionSetting(word);
    const std::string&  name    = setting.name;
    if (name == "wrap")
    {
        bool wrap = view.getWordWrap();
        setFlag(wrap, true, setting, "wrap", word, shown, error);
        view.setWordWrap(wrap);
        return true;
    }
    if (name == "et" || name == "expandtab")
    {
        bool spaces = view.getSoftTabs();
        setFlag(spaces, false, setting, "expandtab", word, shown, error);
        view.setSoftTabs(spaces);
        return true;
    }
    if (isViewOption(name))
    {
        // One width for a tab, an indent and a Tab typed, here; softtabstop
        // 0, vim's own, leaves it be.
        if ((name == "sts" || name == "softtabstop") && setting.op == OptionSetting::Op::Assign && setting.value == "0")
        {
            return true;
        }
        S32 width = view.getTabWidth();
        const char* whole = name.size() > 3 ? name.c_str() : name == "ts" ? "tabstop" : name == "sw" ? "shiftwidth" : "softtabstop";
        setNumber(width, 4, 1, 16, setting, whole, word, shown, error);
        view.setTabWidth(width);
        return true;
    }
    return false;
}

// static
void ALVimKeymap::source(Shared& shared, std::string_view text, const std::function<bool(const std::string& option)>& host,
                         std::vector<std::string>& errors)
{
    shared.mappings.forgetVimrc();
    shared.viewOptions.clear();
    // Its lines, with their numbers; one that starts with a backslash
    // goes on the end of the one before it.
    std::vector<std::pair<S32, std::string>> lines;
    S32                                      number = 0;
    for (size_t at = 0; at <= text.size();)
    {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        std::string line(text.substr(at, end - at));
        at = end + 1;
        ++number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '\\' && !lines.empty())
        {
            lines.back().second += line.substr(first + 1);
        }
        else
        {
            lines.emplace_back(number, std::move(line));
        }
    }
    for (auto& [line_number, line] : lines)
    {
        LLStringUtil::trim(line);
        while (!line.empty() && (line[0] == ':' || line[0] == ' ' || line[0] == '\t'))
        {
            line.erase(0, 1);
        }
        if (line.empty() || line[0] == '"')
        {
            continue;
        }
        bool quiet = false;
        for (const char* prefix : { "silent!", "sil!", "silent ", "sil " })
        {
            const size_t length = strlen(prefix);
            if (line.compare(0, length, prefix) == 0)
            {
                quiet = prefix[length - 1] == '!';
                line.erase(0, length);
                LLStringUtil::trim(line);
                break;
            }
        }
        size_t name_end = 0;
        while (name_end < line.size() && isNameChar(line[name_end]))
        {
            ++name_end;
        }
        std::string name = line.substr(0, name_end);
        std::string args = line.substr(name_end);
        if (!args.empty() && args[0] == '!')
        {
            name += '!';
            args.erase(0, 1);
        }
        LLStringUtil::trim(args);
        std::string error;
        std::string listing;
        if (!name.empty() && shared.mappings.command(name, args, true, listing, error))
        {
        }
        else if (name == "let")
        {
            if (!shared.mappings.let(args, error))
            {
                error = said("VimLetOnly", "Only mapleader and maplocalleader are set with :let");
            }
        }
        else if (isSetCommand(name))
        {
            // The mode's own options here; the host's, which the window
            // they are shown in keeps; each editor's own, for the host to
            // set on each.
            for (const std::string& word : optionWords(args))
            {
                const OptionSetting setting = optionSetting(word);
                std::string         shown;
                if (setSharedOption(shared, setting, word, shown, error))
                {
                    if (!error.empty())
                    {
                        break;
                    }
                }
                else if (!(host && host(word)))
                {
                    if (!isViewOption(setting.name))
                    {
                        error = said("VimUnknownOption", "E518: Unknown option: [OPTION]", { { "[OPTION]", word } });
                        break;
                    }
                    shared.viewOptions.push_back(word);
                }
            }
        }
        else if (name != "noh" && name != "nohlsearch")
        {
            error = said("VimNotACommand", "E492: Not an editor command: [LINE]", { { "[LINE]", line } });
        }
        if (!error.empty() && !quiet)
        {
            errors.push_back(
                said("VimrcLine", "line [NUMBER]: [ERROR]", { { "[NUMBER]", std::to_string(line_number) }, { "[ERROR]", error } }));
        }
    }
}

void ALVimKeymap::runCommand(ALTextView& view, const std::string& line_in)
{
    const ALTextDocument& d       = view.document();
    const bool            editing = !view.isReadOnly();
    std::string           line = line_in;
    LLStringUtil::trim(line);
    if (line.empty())
    {
        return;
    }
    // A range first: %, '<,'>, a number, ., $, 'x, or two of those with
    // a comma; then the command.
    S32    first = view.caret().line;
    S32    last  = first;
    bool   ranged = false;
    size_t at_   = 0;
    auto   lineNumber = [&](S32& out) { return lineAddress(view, line, at_, out); };
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
            // A line number alone goes there, a jump.
            const ALTextPos to(last, firstNonBlankColumn(d, last));
            if (to.line != view.caret().line)
            {
                noteJump(view, cursor(view));
            }
            moveTo(view, to);
        }
        return;
    }
    // The command's name: letters -- and after the first, underscores,
    // which a host's names have (go_to_line) and vim's do not -- or one
    // symbol.
    size_t name_end = 0;
    while (name_end < rest.size() && (isNameChar(rest[name_end]) || (name_end > 0 && rest[name_end] == '_')))
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
        // Doubled for two steps and so on; a count after it is how many
        // lines from the range's start.
        S32         steps = 1;
        std::string rest  = args;
        while (!rest.empty() && rest[0] == name[0])
        {
            ++steps;
            rest.erase(0, 1);
        }
        LLStringUtil::trim(rest);
        if (!rest.empty() && isDigit(rest[0]))
        {
            const S32 n = std::atoi(rest.c_str());
            first       = ranged ? last : first;
            last        = llmin(d.lineCount() - 1, first + llmax(1, n) - 1);
        }
        Span span;
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
        applyOperator(view, name[0], span, steps);
        finishCommand(true);
        return;
    }
    if (name == "m" || name == "move" || name == "t" || name == "co" || name == "copy")
    {
        // The lines below the line the address names -- 0 for the top --
        // moved there, or copied there.
        if (view.isReadOnly())
        {
            return;
        }
        size_t at  = 0;
        S32    to  = -1;
        if (args == "0")
        {
            at = 1;
        }
        else if (!lineAddress(view, args, at, to))
        {
            say(said("VimInvalidAddress", "E14: Invalid address"), true);
            return;
        }
        const bool move = name[0] == 'm';
        if (move && to >= first && to <= last)
        {
            if (to != last)
            {
                say(said("VimMoveIntoItself", "E134: Cannot move a range of lines into itself"), true);
            }
            return;
        }
        const std::string block = d.text(ALTextRange(d.lineStart(first), d.lineEnd(last)));
        const S32         count = last - first + 1;
        view.undoJournal().beginGroup();
        // Put first, then taken out, so that the addresses stay what
        // they were; the copy goes after line `to`, before line to+1.
        if (to < 0)
        {
            view.setCaret(d.lineStart(0));
            view.insertText(block + "\n");
        }
        else
        {
            view.setCaret(d.lineEnd(to));
            view.insertText("\n" + block);
        }
        S32 landed = to < 0 ? 0 : to + 1;
        if (move)
        {
            const S32 shift = to < first ? count : 0;
            const S32 f     = first + shift;
            const S32 l     = last + shift;
            ALTextRange whole;
            if (l + 1 < d.lineCount())
            {
                whole = ALTextRange(d.lineStart(f), d.lineStart(l + 1));
            }
            else
            {
                whole = ALTextRange(d.lineEnd(f - 1), d.lineEnd(l));
            }
            view.deleteRange(whole);
            if (to >= last)
            {
                landed -= count;
            }
        }
        view.undoJournal().endGroup();
        moveTo(view, ALTextPos(llclamp(landed + count - 1, 0, d.lineCount() - 1), 0));
        moveTo(view, ALTextPos(view.caret().line, firstNonBlankColumn(d, view.caret().line)));
        finishCommand(true);
        return;
    }
    if (name == "sor" || name == "sort" || name == "sor!" || name == "sort!")
    {
        // The lines of the range -- the whole text without one -- in
        // order: by their text, or by the first number in each with n;
        // without regard to case with i; each kept once with u; the
        // other way round with a bang.
        if (view.isReadOnly())
        {
            return;
        }
        if (!ranged)
        {
            first = 0;
            last  = d.lineCount() - 1;
        }
        // The empty line after a final line break is no line of the
        // text's, and does not sort.
        if (last == d.lineCount() - 1 && last > first && d.lineLength(last) == 0)
        {
            --last;
        }
        const bool reverse  = name.back() == '!';
        const bool numeric  = args.find('n') != std::string::npos;
        const bool ignore   = args.find('i') != std::string::npos;
        const bool unique   = args.find('u') != std::string::npos;
        std::vector<std::string> lines;
        for (S32 l = first; l <= last; ++l)
        {
            lines.push_back(d.line(l));
        }
        auto key = [&](const std::string& text) {
            if (!ignore)
            {
                return text;
            }
            return alRecased(text, 'u');
        };
        auto number = [](const std::string& text) {
            size_t at = text.find_first_of("0123456789");
            if (at == std::string::npos)
            {
                return std::pair<bool, S64>(false, 0);
            }
            const bool negative = at > 0 && text[at - 1] == '-';
            S64        n        = 0;
            while (at < text.size() && isDigit(text[at]))
            {
                n = n * 10 + (text[at++] - '0');
            }
            return std::pair<bool, S64>(true, negative ? -n : n);
        };
        auto before = [&](const std::string& a, const std::string& b) {
            if (numeric)
            {
                // Lines with no number come first, in their order.
                const auto na = number(a);
                const auto nb = number(b);
                if (na.first != nb.first)
                {
                    return !na.first;
                }
                return na.second < nb.second;
            }
            return key(a) < key(b);
        };
        std::stable_sort(lines.begin(), lines.end(), before);
        if (unique)
        {
            lines.erase(std::unique(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b) { return !before(a, b) && !before(b, a); }), lines.end());
        }
        if (reverse)
        {
            std::reverse(lines.begin(), lines.end());
        }
        std::string sorted;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
            {
                sorted += '\n';
            }
            sorted += lines[i];
        }
        view.replaceAll({ { ALTextRange(d.lineStart(first), d.lineEnd(last)), sorted } });
        moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
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
            if (!play(view, inputs, name.back() != '!'))
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
    if (name == "j" || name == "join" || name == "j!" || name == "join!")
    {
        // The range's lines, the last and one more where it is one line;
        // or, with a count, that many from the range's last.
        S32 from  = first;
        S32 until = last > first ? last : first + 1;
        if (const S32 lines = args.empty() ? 0 : std::atoi(args.c_str()); lines > 0)
        {
            from  = last;
            until = last + lines - 1;
        }
        if (editing)
        {
            if (const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(d, from, until, name.back() == '!'))
            {
                view.apply(*join);
                moveTo(view, join->caret);
            }
        }
        return;
    }
    if (name == "retab" || name == "ret" || name == "retab!" || name == "ret!")
    {
        // Every line's leading blanks, or the range's, as the tabs are set:
        // spaces where they are, else tabs; a width given sets it, the
        // blanks measured as the tabs were.
        const S32 was = view.getTabWidth();
        if (const S32 width = args.empty() ? 0 : std::atoi(args.c_str()); width > 0)
        {
            view.setTabWidth(llclamp(width, 1, 16));
        }
        if (editing)
        {
            view.convertIndentation(ranged ? first : 0, ranged ? last : d.lineCount() - 1, view.getSoftTabs(), was);
        }
        return;
    }
    if (name == "u" || name == "undo" || name == "red" || name == "redo")
    {
        view.perform(name[0] == 'u' ? ALEditorCommand::Undo : ALEditorCommand::Redo);
        moveTo(view, view.caret());
        return;
    }
    if (name == "pu" || name == "put" || name == "pu!" || name == "put!")
    {
        // A register's text as lines, whatever it was taken as: under the
        // range's last line, or above its first with !.
        const Register reg = fetch(args.empty() ? mRegister : args[0]);
        if (!editing || reg.text.empty())
        {
            return;
        }
        std::string text = reg.text;
        if (!text.empty() && text.back() == '\n')
        {
            text.pop_back();
        }
        const bool above = name.back() == '!';
        const S32  line  = above ? first : last;
        view.setCaret(above ? d.lineStart(line) : d.lineEnd(line));
        view.insertText(above ? text + "\n" : "\n" + text);
        const S32 put_at = above ? line : line + 1;
        moveTo(view, ALTextPos(put_at, firstNonBlankColumn(d, put_at)));
        return;
    }
    if (name == "ma" || name == "mark" || name == "k")
    {
        // A mark at the range's last line, as m would set it there.
        if (args.size() == 1 && ((args[0] >= 'a' && args[0] <= 'z') || args[0] == '\'' || args[0] == '`'))
        {
            mMarks[args[0]] = ALTextPos(last, 0);
        }
        else
        {
            say(said("VimBadMark", "E191: Argument must be a letter or forward/backward quote"), true);
        }
        return;
    }
    if (name == "le" || name == "left" || name == "ri" || name == "right" || name == "ce" || name == "center")
    {
        // The range's lines to the left with an indent, or to the right or
        // centred within a width: eighty, where none is given.
        if (!editing)
        {
            return;
        }
        const bool left   = name[0] == 'l';
        const S32  amount = args.empty() ? (left ? 0 : 80) : llmax(0, std::atoi(args.c_str()));
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (S32 line = first; line <= last; ++line)
        {
            std::string text = d.line(line);
            LLStringUtil::trim(text);
            if (text.empty())
            {
                continue;
            }
            const S32 size   = static_cast<S32>(text.size());
            const S32 indent = left ? amount : name[0] == 'r' ? llmax(0, amount - size) : llmax(0, (amount - size) / 2);
            edits.emplace_back(ALTextRange(d.lineStart(line), d.lineEnd(line)), std::string(static_cast<size_t>(indent), ' ') + text);
        }
        if (!edits.empty())
        {
            view.replaceAll(std::move(edits));
        }
        return;
    }
    if (name == "reg" || name == "registers" || name == "di" || name == "display")
    {
        listRegisters(view, args);
        return;
    }
    if (name == "marks")
    {
        listMarks(view, args);
        return;
    }
    if (name == "changes")
    {
        // As vim's :changes: each by how far back it is from where the caret
        // was last taken, with its line and column and the line's text; >
        // where that is.
        const std::vector<ALTextPos>& changes = view.changes();
        const S32                     at      = view.changeAt();
        std::string                   text    = "change line  col text";
        for (S32 i = 0; i < static_cast<S32>(changes.size()); ++i)
        {
            const ALTextPos p    = d.clamp(changes[static_cast<size_t>(i)]);
            std::string     line = d.line(p.line);
            LLStringUtil::trimHead(line);
            text += llformat("\n%c%5d %5d %4d ", i == at ? '>' : ' ', std::abs(at - i), p.line + 1, p.column) + line.substr(0, 60);
        }
        if (at >= static_cast<S32>(changes.size()))
        {
            text += "\n>";
        }
        list(view, text);
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
    {
        // The :map family, and the leaders by :let.
        std::string listing;
        std::string error;
        if (mShared->mappings.command(name, args, false, listing, error))
        {
            if (!error.empty())
            {
                say(error, true);
            }
            else if (listing.find('\n') == std::string::npos)
            {
                say(listing);
            }
            else
            {
                list(view, listing);
            }
            return;
        }
        if (name == "let")
        {
            if (!mShared->mappings.let(args, error))
            {
                error = said("VimLetOnly", "Only mapleader and maplocalleader are set with :let");
            }
            if (!error.empty())
            {
                say(error, true);
            }
            return;
        }
    }
    if (isSetCommand(name))
    {
        // Each option in turn: the mode's own, then the view's, then the
        // host's; what a query shows, said together at the end.
        std::string shown_all;
        bool        search_lit = false;
        for (const std::string& word : optionWords(args))
        {
            std::string shown;
            std::string error;
            if (!setSharedOption(*mShared, optionSetting(word), word, shown, error) && !setViewOption(view, word, shown, error) &&
                !(mHooks.command && mHooks.command(view, "set", word)))
            {
                error = said("VimUnknownOption", "E518: Unknown option: [OPTION]", { { "[OPTION]", word } });
            }
            if (!error.empty())
            {
                say(error, true);
                return;
            }
            if (!shown.empty())
            {
                shown_all += (shown_all.empty() ? "" : "  ") + shown;
            }
            const std::string option = optionSetting(word).name;
            search_lit               = search_lit || option == "hls" || option == "hlsearch";
        }
        if (search_lit && !mShared->highlightSearch)
        {
            if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
            {
                editor->clearHighlights();
            }
        }
        if (!shown_all.empty())
        {
            say(shown_all);
        }
        return;
    }
    if (mHooks.command && mHooks.command(view, name, args))
    {
        return;
    }
    say(said("VimNotACommand", "E492: Not an editor command: [LINE]", { { "[LINE]", line } }), true);
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
        say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        return false;
    }
    if (mInGlobal)
    {
        say(said("VimGlobalRecursive", "E147: Cannot do :global recursive"), true);
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
        say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
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
        say(said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", error } }), true);
        return false;
    }
    // The matches are in order: the lines walked beside them.
    std::vector<S32> lines;
    size_t           next = 0;
    for (S32 line = first; line <= last; ++line)
    {
        while (next < matches.size() && matches[next].begin.line < line)
        {
            ++next;
        }
        const bool hit = next < matches.size() && matches[next].begin.line == line;
        if (hit != invert)
        {
            lines.push_back(line);
        }
    }
    if (lines.empty())
    {
        say(said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        return false;
    }
    if (command.empty())
    {
        // Nothing to do to them: the last one is where the caret goes,
        // and how many there were is said.
        moveTo(view, ALTextPos(lines.back(), firstNonBlankColumn(d, lines.back())));
        say(alSaidCount("VimLinesPicked", static_cast<S32>(lines.size()), "1 line", "[COUNT] lines"));
        return true;
    }
    // One step to undo for the lot; an asking :s among the commands
    // gathers its edits here and the asking runs once, in order, after.
    view.undoJournal().beginGroup();
    mConfirming           = Confirming();
    mConfirming.gathering = true;
    mInGlobal             = true;
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
    mInGlobal             = false;
    mConfirming.gathering = false;
    if (mMessageError && !mConfirming.edits.empty())
    {
        // An error part way: what was gathered from the lines before it
        // is not asked about, as vim stops there too; said, since the
        // error alone would not say so.
        say(said("VimNothingSubstituted", "[MESSAGE] -- nothing substituted", { { "[MESSAGE]", mMessage } }), true);
    }
    if (!mConfirming.edits.empty() && !mMessageError)
    {
        // The group stays open for the asking to close.
        std::sort(mConfirming.edits.begin(), mConfirming.edits.end(),
                  [](const std::pair<ALTextRange, std::string>& a, const std::pair<ALTextRange, std::string>& b) { return a.first.begin < b.first.begin; });
        mMode = Mode::Confirm;
        askNext(view);
        return false;
    }
    mConfirming = Confirming();
    view.undoJournal().endGroup();
    return !mMessageError;
}

ALVimKeymap::Pattern ALVimKeymap::patternOf(const std::string& vim, std::optional<bool> force_case) const
{
    return ALVimPattern::of(vim, mLastReplacement, { mShared->ignoreCase, mShared->smartCase }, force_case);
}

ALVimPattern::Places ALVimKeymap::placesOf(const ALTextView& view) const
{
    // The last visual area, as a range: whole lines for a line-wise one,
    // the character under either end included otherwise; a block is the
    // lines between its ends and the columns between them.
    const ALTextDocument& d = view.document();
    ALVimPattern::Places  places;
    places.caret = view.caret();
    if (mVisualLast != Mode::Normal)
    {
        const ALTextPos a = mVisualLastAnchor < mVisualLastCaret ? mVisualLastAnchor : mVisualLastCaret;
        const ALTextPos b = mVisualLastAnchor < mVisualLastCaret ? mVisualLastCaret : mVisualLastAnchor;
        places.visual     = true;
        places.visualRange = mVisualLast == Mode::VisualLine ? ALTextRange(d.lineStart(a.line), d.lineEnd(b.line)) : ALTextRange(a, d.nextCluster(b));
        if (mVisualLast == Mode::VisualBlock)
        {
            places.blockLeft   = llmin(mVisualLastAnchor.column, mVisualLastCaret.column);
            places.blockRight  = llmax(mVisualLastAnchor.column, mVisualLastCaret.column);
            places.visualRange = ALTextRange(d.lineStart(a.line), d.lineEnd(b.line));
        }
    }
    return places;
}

std::vector<ALTextRange> ALVimKeymap::matchesOf(ALTextView& view, const Pattern& pattern, ALTextSearchOptions options, const ALTextRange* scope, std::string& error,
                                                std::vector<ALTextPos>& wholes) const
{
    return pattern.matchesIn(view.document(), options, scope, placesOf(view), error, wholes);
}

std::string ALVimKeymap::replacementOf(const std::string& with) const
{
    return ALVimPattern::replacementOf(with);
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
            say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
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
            say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
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
        say(said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", error } }), true);
        return false;
    }
    if (matches.empty())
    {
        if (!quiet)
        {
            say(said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
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
        say(matchesSaid(count, lines));
        return false;
    }
    if (asking && !view.isReadOnly())
    {
        if (mConfirming.gathering)
        {
            // A :g's line: the edits kept for the asking after the :g.
            for (auto& edit : edits)
            {
                mConfirming.edits.push_back(std::move(edit));
            }
            return false;
        }
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
        say(substitutionsSaid(count, lines));
    }
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

namespace
{
    // A register's or a line's text as vim lists it: a line break as ^J, a
    // tab as ^I, cut to what fits a row.
    std::string listed(const std::string& text, size_t most)
    {
        std::string out;
        for (char c : text)
        {
            if (out.size() >= most)
            {
                break;
            }
            out += c == '\n' ? std::string("^J") : c == '\t' ? std::string("^I") : std::string(1, c);
        }
        return out;
    }
}

void ALVimKeymap::listRegisters(ALTextView& view, const std::string& names)
{
    // Each that holds anything, as vim's :registers has them: its kind --
    // c characters, l lines, b a block -- its name, and what it holds; the
    // last : line and the last search after them.
    std::string text = "Type Name Content";
    auto        row  = [&](char kind, char name, const std::string& held) {
        if (!held.empty() && (names.empty() || names.find(name) != std::string::npos))
        {
            text += llformat("\n  %c  \"%c   ", kind, name) + listed(held, 70);
        }
    };
    for (const char* name = "\"0123456789abcdefghijklmnopqrstuvwxyz-"; *name; ++name)
    {
        // Lines are kept without the break that ends the last, which vim
        // shows.
        const Register held = fetch(*name);
        row(held.block ? 'b' : held.linewise ? 'l' : 'c', *name, held.linewise && !held.text.empty() ? held.text + "\n" : held.text);
    }
    row('c', ':', mShared->command.empty() ? std::string() : mShared->command.back());
    row('c', '/', mSearchPattern);
    list(view, text);
}

void ALVimKeymap::listMarks(ALTextView& view, const std::string& names)
{
    // Each set, as vim's :marks has them: the name, the line and column
    // it is at, and the text of that line.
    followDocument(view);
    const ALTextDocument& d    = view.document();
    std::string           text = "mark line  col file/text";
    for (const auto& [name, at] : mMarks)
    {
        if (!names.empty() && names.find(name) == std::string::npos)
        {
            continue;
        }
        const ALTextPos p = d.clamp(at);
        std::string     line = d.line(p.line);
        LLStringUtil::trimHead(line);
        text += llformat("\n %c %6d %4d ", name, p.line + 1, p.column) + listed(line, 60);
    }
    list(view, text);
}

void ALVimKeymap::list(ALTextView& view, const std::string& text)
{
    if (mHooks.listing)
    {
        mHooks.listing(view, text);
    }
    else
    {
        say(text.substr(0, text.find('\n')));
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
    // The view's, round past the ends, the count times.
    std::optional<ALTextRange> word;
    ALTextPos                  at = cursor(view);
    for (S32 n = 0; n < count; ++n)
    {
        const std::optional<ALTextRange> next = view.misspellingFrom(at, forward);
        if (!next)
        {
            break;
        }
        word = next;
        at   = next->begin;
    }
    if (!word)
    {
        say(said("VimNoMisspelling", "No misspelled words"), true);
        return false;
    }
    moveTo(view, word->begin);
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
            list += llformat("%s%d \"%s\"", i ? "  " : "", static_cast<int>(i + 1), items[i].c_str());
        }
        say(list);
        return;
    }
    // Picked later: the word may have changed by then, and is looked for
    // again at the caret.
    const LLHandle<LLUICtrl> handle = view.getHandle();
    mHooks.pick(view, view.document().text(word), items, [handle](size_t index) {
        ALTextView* again = dynamic_cast<ALTextView*>(handle.get());
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
    ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view);
    if (!editor)
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
                editor->unfoldAt(line);
                return true;
            case 'c':
            case 'C':
                editor->foldAt(line);
                return true;
            case 'a':
            case 'A':
                if (editor->isFolded(line))
                {
                    editor->unfoldAt(line);
                }
                else
                {
                    editor->foldAt(line);
                }
                return true;
            case 'R':
                editor->unfoldAll();
                return true;
            case 'M':
                editor->foldAll();
                return true;
            case 'j':
            case 'k':
            {
                // zj the start of the next block below; zk the end of the
                // last one above.
                S32 to = -1;
                for (const ALCodeEditor::FoldRegion& region : editor->foldRegions())
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
    const ALCodeEditor::FoldRegion* around = nullptr;
    for (const ALCodeEditor::FoldRegion& region : editor->foldRegions())
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

std::optional<ALTextRange> ALVimKeymap::matchNear(ALTextView& view, bool forward)
{
    if (mSearchPattern.empty())
    {
        say(said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        return std::nullopt;
    }
    ALTextSearchOptions options;
    options.regex         = !mSearchWholeWord;
    options.wholeWord     = mSearchWholeWord;
    const Pattern pattern = mSearchWholeWord ? Pattern{ mSearchPattern, !mShared->ignoreCase } : patternOf(mSearchPattern);
    options.caseSensitive = pattern.caseSensitive;
    std::string              error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = matchesOf(view, pattern, options, nullptr, error, wholes);
    if (!error.empty() || matches.empty())
    {
        say(said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", mSearchPattern } }), true);
        return std::nullopt;
    }
    // gn: the one the caret is in, or the next; gN the one it is in, or
    // the one before.
    const ALTextPos at = cursor(view);
    if (forward)
    {
        for (const ALTextRange& match : matches)
        {
            if (at < match.end)
            {
                return match;
            }
        }
        return matches.front();
    }
    for (auto it = matches.rbegin(); it != matches.rend(); ++it)
    {
        if (!(at < it->begin))
        {
            return *it;
        }
    }
    return matches.back();
}

bool ALVimKeymap::unmatchedBracket(const ALTextView& view, llwchar bracket, S32 count, ALTextPos& out) const
{
    // Out through the brackets of the other kind as well, counting pairs of
    // this kind only: [( from a(b(c)d|) is the second (.
    const ALTextDocument& d       = view.document();
    const bool            forward = bracket == ')' || bracket == '}';
    const char            open    = bracket == ')' || bracket == '(' ? '(' : '{';
    const char            close   = open == '(' ? ')' : '}';
    ALTextPos             at      = view.caret();
    S32                   depth   = 0;
    S32                   found   = 0;
    for (;;)
    {
        if (forward)
        {
            if (atLineEnd(d, at))
            {
                if (at.line + 1 >= d.lineCount())
                {
                    return false;
                }
                at = ALTextPos(at.line + 1, 0);
                if (d.lineLength(at.line) == 0)
                {
                    continue;
                }
            }
            else
            {
                at = d.nextCluster(at);
                if (atLineEnd(d, at))
                {
                    continue;
                }
            }
        }
        else
        {
            if (at.column == 0)
            {
                if (at.line == 0)
                {
                    return false;
                }
                at = ALTextPos(at.line - 1, d.lineLength(at.line - 1));
                continue;
            }
            at = d.prevCluster(at);
        }
        const char c = d.line(at.line)[static_cast<size_t>(at.column)];
        if (c == (forward ? open : close))
        {
            ++depth;
        }
        else if (c == (forward ? close : open))
        {
            if (depth > 0)
            {
                --depth;
            }
            else if (++found == count)
            {
                out = at;
                return true;
            }
        }
    }
}

// static
void ALVimKeymap::splitOffset(const std::string& line, llwchar kind, std::string& pattern, std::string& offset_text)
{
    // The pattern runs to the first `kind` not escaped; the rest is the
    // offset.
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '\\')
        {
            ++i;
            continue;
        }
        if (static_cast<llwchar>(static_cast<unsigned char>(line[i])) == kind)
        {
            pattern     = line.substr(0, i);
            offset_text = line.substr(i + 1);
            return;
        }
    }
    pattern = line;
    offset_text.clear();
}

// static
bool ALVimKeymap::parseOffset(const std::string& text, SearchOffset& out)
{
    // [+-]N lines, e[+-N] from the end, s[+-N] or b[+-N] from the start;
    // a sign alone is one.
    out = SearchOffset{};
    if (text.empty())
    {
        return true;
    }
    size_t at   = 0;
    char   kind = 'l';
    if (text[0] == 'e' || text[0] == 's' || text[0] == 'b')
    {
        kind = text[0] == 'e' ? 'e' : 's';
        at   = 1;
    }
    S32 sign = 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    const bool signed_alone = at == text.size() && at > 0 && (text[at - 1] == '+' || text[at - 1] == '-');
    S32        amount       = 0;
    for (; at < text.size(); ++at)
    {
        if (text[at] < '0' || text[at] > '9')
        {
            return false;
        }
        amount = llmin(amount * 10 + (text[at] - '0'), 100000);
    }
    out.kind   = kind;
    out.amount = sign * (signed_alone ? 1 : amount);
    return true;
}

ALTextPos ALVimKeymap::offsetFrom(const ALTextDocument& d, const ALTextRange& match, const SearchOffset& offset) const
{
    if (offset.kind == 'l')
    {
        return ALTextPos(llclamp(match.begin.line + offset.amount, 0, d.lineCount() - 1), 0);
    }
    ALTextPos at = offset.kind == 'e' ? d.prevCluster(match.end) : match.begin;
    for (S32 n = 0; n < std::abs(offset.amount); ++n)
    {
        at = offset.amount > 0 ? d.nextCluster(at) : d.prevCluster(at);
    }
    return offset.kind ? at : match.begin;
}

void ALVimKeymap::incrementalSearch(ALTextView& view)
{
    ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view);
    if (!mShared->incrementalSearch || !editor)
    {
        return;
    }
    std::string pattern;
    std::string offset_text;
    splitOffset(mLine, mLineKind, pattern, offset_text);
    mIncrementalShown = true;
    if (pattern.empty())
    {
        editor->clearHighlights();
        view.scrollToCaret();
        return;
    }
    // What is typed so far may not be a pattern yet: nothing lit then.
    ALTextSearchOptions options;
    options.regex             = true;
    const Pattern parsed      = patternOf(pattern);
    options.caseSensitive     = parsed.caseSensitive;
    std::string              error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = matchesOf(view, parsed, options, nullptr, error, wholes);
    if (!error.empty() || matches.empty())
    {
        editor->clearHighlights();
        view.scrollToCaret();
        return;
    }
    const bool forward = mLineKind == '/';
    const S32  index   = ALTextSearch::nearest(matches, forward ? view.document().nextCluster(cursor(view)) : cursor(view), forward);
    const ALTextRange next = matches[static_cast<size_t>(index < 0 ? 0 : index)];
    // Every match lit where hlsearch has them, else the next alone.
    editor->setHighlights(mShared->highlightSearch ? matches : std::vector<ALTextRange>{ next });
    view.scrollToLine(next.begin.line);
}

void ALVimKeymap::endIncremental(ALTextView& view)
{
    if (!mIncrementalShown)
    {
        return;
    }
    mIncrementalShown = false;
    if (ALCodeEditor* editor = ALViewType::as<ALCodeEditor>(&view))
    {
        editor->clearHighlights();
    }
    view.scrollToCaret();
}
