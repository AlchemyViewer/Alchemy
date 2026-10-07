/**
 * @file alvimcommandline.h
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


#pragma once

#include "alvimmappings.h"

#include <string>
#include <vector>

class ALTextView;
class ALVimKeymap;

// Vim's : line and search line, for the vim keymap (ALVimKeymap): what is
// typed on them and where the cursor is, the lines entered before --
// : and search apart, in the keymap's shared state -- walked by Up and
// Down, and Tab completing the word at the cursor. Entering a line is the
// keymap's: running a : command, searching.
class ALVimCommandLine
{
public:
    explicit ALVimCommandLine(ALVimKeymap& vim) : mVim(vim) {}

    // A key on the line.
    bool commandLine(ALTextView& view, const ALVimInput& input);
    // Tab on the : line: the word at the cursor completed from what the
    // keymap and the host know -- a command's name, or what follows
    // one -- the next of them on each Tab, the one before on Shift-Tab,
    // and the word as typed again past the last, as vim's wildmenu
    // walks. Any other key keeps what is on the line and drops the rest.
    void complete(ALTextView& view, bool forward);
    void dropCompletion();
    // The history of a line kind, and the line entered into it.
    std::vector<std::string>& historyOf(llwchar kind);
    void                      remember(llwchar kind, const std::string& line);

    // The : or / line being typed, and which; where Up has walked to in
    // its history, with what was typed before it was pressed, which Down
    // comes back to, -1 while not walking; and where in the line the next
    // character goes, in bytes.
    std::string line;
    llwchar     kind          = ':';
    S32         historyAt     = -1;
    std::string historyPrefix;
    size_t      cursor        = 0;
    // The completions Tab found for the word at the cursor, which of
    // them is on the line (-1 for the word as typed), where the word
    // begins, the word as typed and what followed the cursor.
    struct Completion
    {
        std::vector<std::string> items;
        S32                      at        = -1;
        size_t                   wordStart = 0;
        std::string              typed;
        std::string              tail;
    };
    Completion completion;

private:
    // The keymap out of the line, back in the mode it was opened from:
    // normal mode, or the visual mode a search was opened over.
    void backFromLine();

    ALVimKeymap& mVim;
};
