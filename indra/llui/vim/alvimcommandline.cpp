/**
 * @file alvimcommandline.cpp
 * @brief Vim's : and search lines: typing on them, Tab completing, and their histories.
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

#include "alvimcommandline.h"

#include "altextchars.h"
#include "alvimexcommands.h"
#include "alvimkeymap.h"
#include "alvimtext.h"

#include <algorithm>

using namespace ALVimText;

std::vector<std::string>& ALVimCommandLine::historyOf(llwchar which)
{
    return which == ':' ? mVim.mShared->command : mVim.mShared->search;
}

bool ALVimCommandLine::commandLine(ALTextView& view, const ALVimInput& input)
{
    // The cursor moves by whole characters.
    auto back = [this](size_t at) {
        while (at > 0 && (static_cast<unsigned char>(line[--at]) & 0xC0) == 0x80) {}
        return at;
    };
    auto forward = [this](size_t at) {
        if (at < line.size())
        {
            ++at;
            while (at < line.size() && (static_cast<unsigned char>(line[at]) & 0xC0) == 0x80)
            {
                ++at;
            }
        }
        return at;
    };
    cursor = llmin(cursor, line.size());
    // Tab walks the completions; anything else keeps what it put on the
    // line and lets the rest go -- but Escape with them up only lets
    // them go, the word as typed back on the line.
    if (!input.isChar && input.key == KEY_TAB && !(input.mask & (ALVimInput::CONTROL | MASK_CONTROL | MASK_ALT)))
    {
        complete(view, !(input.mask & MASK_SHIFT));
        return true;
    }
    if (!input.isChar && input.key == KEY_ESCAPE && !completion.items.empty())
    {
        line       = line.substr(0, completion.wordStart) + completion.typed + completion.tail;
        cursor = completion.wordStart + completion.typed.size();
        dropCompletion();
        return true;
    }
    dropCompletion();
    if (!input.isChar)
    {
        // Vim's own editing of the line: Control-B and Control-E to the
        // ends, Control-W a word back, Control-U to the start, Control-H
        // a character back.
        if ((input.mask & ALVimInput::CONTROL) && !(input.mask & MASK_ALT))
        {
            switch (input.key)
            {
                case KEY_LEFT:
                case KEY_RIGHT:
                {
                    // A WORD, as with shift.
                    ALVimInput as_shift = input;
                    as_shift.mask  = MASK_SHIFT;
                    return commandLine(view, as_shift);
                }
                case 'B': cursor = 0; return true;
                case 'E': cursor = line.size(); return true;
                case 'U':
                    line.erase(0, cursor);
                    cursor = 0;
                    historyAt  = -1;
                    return true;
                case 'W':
                {
                    // Blanks before the cursor, then the word or the run
                    // of other characters before them.
                    size_t at = cursor;
                    while (at > 0 && line[at - 1] == ' ')
                    {
                        --at;
                    }
                    if (at > 0)
                    {
                        const bool word = alWordByte(line[at - 1]);
                        while (at > 0 && line[at - 1] != ' ' && alWordByte(line[at - 1]) == word)
                        {
                            --at;
                        }
                    }
                    line.erase(at, cursor - at);
                    cursor = at;
                    historyAt  = -1;
                    return true;
                }
                case 'H':
                {
                    ALVimInput as_key;
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
                line.clear();
                cursor = 0;
                backFromLine(view);
                // What waited on the line goes with it: a count, and an
                // operator the search was to be the motion of.
                mVim.clearPending();
                if (mVim.mMode == ALVimKeymap::Mode::Normal)
                {
                    mVim.moveTo(view, view.caret());
                }
                return true;
            case KEY_LEFT:
            case KEY_RIGHT:
            {
                // A character, or with shift a WORD -- what stands between
                // blanks -- as vim's <S-Left> and <S-Right> have it.
                const bool left = input.key == KEY_LEFT;
                if (!(input.mask & MASK_SHIFT))
                {
                    cursor = left ? back(cursor) : forward(cursor);
                    return true;
                }
                size_t at = cursor;
                if (left)
                {
                    while (at > 0 && line[at - 1] == ' ') { --at; }
                    while (at > 0 && line[at - 1] != ' ') { --at; }
                }
                else
                {
                    while (at < line.size() && line[at] != ' ') { ++at; }
                    while (at < line.size() && line[at] == ' ') { ++at; }
                }
                cursor = at;
                return true;
            }
            case KEY_HOME: cursor = 0; return true;
            case KEY_END: cursor = line.size(); return true;
            case KEY_DELETE:
                if (cursor < line.size())
                {
                    line.erase(cursor, forward(cursor) - cursor);
                    historyAt = -1;
                }
                return true;
            case KEY_BACKSPACE:
                historyAt = -1;
                if (line.empty())
                {
                    // Let go of as Escape lets it go, and what waited on
                    // it with it.
                    backFromLine(view);
                    mVim.clearPending();
                }
                else if (cursor > 0)
                {
                    // The character before the cursor, whole.
                    const size_t cut = back(cursor);
                    line.erase(cut, cursor - cut);
                    cursor = cut;
                }
                return true;
            case KEY_UP:
            case KEY_DOWN:
            {
                // The lines entered before that start as this one does,
                // older with Up and newer with Down, back to this one
                // past the newest.
                const std::vector<std::string>& history = historyOf(kind);
                const S32                       n       = static_cast<S32>(history.size());
                if (historyAt < 0)
                {
                    historyPrefix = line;
                    historyAt     = n;
                }
                S32 at = historyAt;
                while (true)
                {
                    at += input.key == KEY_UP ? -1 : 1;
                    if (at < 0)
                    {
                        return true;
                    }
                    if (at >= n)
                    {
                        historyAt  = -1;
                        line       = historyPrefix;
                        cursor = line.size();
                        return true;
                    }
                    if (history[static_cast<size_t>(at)].compare(0, historyPrefix.size(), historyPrefix) == 0)
                    {
                        historyAt  = at;
                        line       = history[static_cast<size_t>(at)];
                        cursor = line.size();
                        return true;
                    }
                }
            }
            case KEY_RETURN:
            {
                const std::string entered = line;
                const llwchar     which   = kind;
                line.clear();
                cursor = 0;
                backFromLine(view);
                historyAt  = -1;
                remember(which, entered);
                if (which == ':')
                {
                    mVim.mEx->runCommand(view, entered);
                }
                else
                {
                    // /pattern/offset: an empty pattern the last one, with
                    // the offset given, or the last one too where none is.
                    std::string pattern;
                    std::string offset_text;
                    ALVimSearch::splitOffset(entered, which, pattern, offset_text);
                    ALVimSearch::Offset offset{};
                    const bool   has_offset = entered.size() > pattern.size();
                    if (has_offset && !ALVimSearch::parseOffset(offset_text, offset))
                    {
                        mVim.say(ALVimKeymap::said("VimBadOffset", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", entered } }), true);
                        mVim.finishCommand(false);
                        return true;
                    }
                    if (!pattern.empty())
                    {
                        mVim.mSearch.pattern   = pattern;
                        mVim.mSearch.wholeWord = false;
                        mVim.mSearch.offset    = offset;
                    }
                    else if (has_offset)
                    {
                        mVim.mSearch.offset = offset;
                    }
                    mVim.mSearch.forward = which == '/';
                    if (mVim.mOperator)
                    {
                        // The motion of the operator waiting on the line:
                        // the operator over the stretch to where it goes.
                        return mVim.searchMotion(view, mVim.mSearch.forward);
                    }
                    if (!mVim.mSearch.pattern.empty())
                    {
                        // A count typed before the line is the match that
                        // many on: 3/foo goes to the third.
                        mVim.mSearch.search(view, mVim.mSearch.pattern, mVim.mSearch.forward, countOr(mVim.mCount), mVim.mSearch.wholeWord, mVim.mSearch.offset);
                    }
                }
                if (mVim.mMode == ALVimKeymap::Mode::Normal)
                {
                    mVim.moveTo(view, view.caret());
                }
                mVim.finishCommand(false);
                return true;
            }
            default:
                // A plain key's character follows; a control chord is
                // nobody's while the line is being typed.
                return (input.mask & (ALVimInput::CONTROL | MASK_CONTROL | MASK_ALT)) != 0;
        }
    }
    if (input.ch == '\r' || input.ch == '\n')
    {
        ALVimInput as_key;
        as_key.key = KEY_RETURN;
        return commandLine(view, as_key);
    }
    const std::string typed = utf8Of(input.ch);
    line.insert(cursor, typed);
    cursor += typed.size();
    historyAt = -1;
    return true;
}

void ALVimCommandLine::backFromLine(ALTextView& view)
{
    // A search typed over a visual selection goes back to it, for what it
    // finds to move the visual caret; anything else to normal mode. What
    // the line lit as it was typed goes out with it (setMode).
    mVim.setMode(view, kind == ':' ? ALVimKeymap::Mode::Normal : mVim.mSearchVisual);
}

void ALVimCommandLine::dropCompletion()
{
    if (!completion.items.empty())
    {
        completion = Completion();
        mVim.bump();
    }
}

void ALVimCommandLine::complete(ALTextView& view, bool forward)
{
    if (mVim.mMode != ALVimKeymap::Mode::Command)
    {
        return;
    }
    if (completion.items.empty())
    {
        // The word at the cursor: the command's name where the cursor is
        // in it -- past any range -- else the word after the last blank,
        // which is the command's to complete.
        size_t at = 0;
        while (at < line.size() && (isDigit(line[at]) || line[at] == '%' || line[at] == '.' || line[at] == '$' || line[at] == ',' || line[at] == '+' ||
                                     line[at] == '-' || line[at] == '\'' || line[at] == '<' || line[at] == '>' || line[at] == ' '))
        {
            ++at;
        }
        const size_t name_start = at;
        while (at < line.size() && (isNameChar(line[at]) || (at > name_start && line[at] == '_')))
        {
            ++at;
        }
        const size_t name_end = at;
        if (at < line.size() && line[at] == '!')
        {
            ++at;
        }
        std::string              command;
        size_t                   word_start = name_start;
        std::vector<std::string> found;
        if (cursor <= name_end)
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
            command    = line.substr(name_start, name_end - name_start);
            word_start = line.rfind(' ', cursor > 0 ? cursor - 1 : 0);
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
        const std::string typed = line.substr(word_start, cursor - word_start);
        if (mVim.mHooks.complete)
        {
            mVim.mHooks.complete(view, command, typed, found);
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
        completion.items     = std::move(items);
        completion.at        = forward ? 0 : static_cast<S32>(completion.items.size()) - 1;
        completion.wordStart = word_start;
        completion.typed     = typed;
        completion.tail      = line.substr(cursor);
    }
    else
    {
        // On to the next, or back; the word as typed stands past either
        // end, as vim's wildmenu has it.
        const S32 n    = static_cast<S32>(completion.items.size());
        completion.at = completion.at + (forward ? 1 : -1);
        if (completion.at >= n)
        {
            completion.at = -1;
        }
        else if (completion.at < -1)
        {
            completion.at = n - 1;
        }
    }
    const std::string& word = completion.at < 0 ? completion.typed : completion.items[static_cast<size_t>(completion.at)];
    line                   = line.substr(0, completion.wordStart) + word + completion.tail;
    cursor             = completion.wordStart + word.size();
    historyAt              = -1;
    if (completion.items.size() == 1)
    {
        // One answer is the answer; nothing to walk.
        completion = Completion();
    }
    mVim.bump();
}

void ALVimCommandLine::remember(llwchar kind, const std::string& line)
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
