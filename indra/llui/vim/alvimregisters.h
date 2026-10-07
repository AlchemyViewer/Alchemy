/**
 * @file alvimregisters.h
 * @brief Vim's registers: what a yank or a delete keeps, and what a put or a macro reads back.
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

#include <functional>
#include <map>
#include <string>

// Vim's registers: what a yank or a delete keeps, and in which. A named
// register, a to z, keeps what is put in it, and its capital adds to it. A
// yank with none named goes in 0; a delete of a line or more in 1, whichever
// is named, the eight before it moving along to 9, as does a smaller one
// over a search or a jump, which vim keeps there however little it takes; a
// smaller one with none named in -; _ keeps nothing, and gives nothing
// back, the clipboard's no more than any other's. The unnamed register is
// the last put in any, and is the clipboard where `clipboard=unnamed` says
// so -- a named one leaves the clipboard alone -- while + and * are the
// clipboard whatever it says. What a macro records is its register's alone.
class ALVimRegisters
{
public:
    struct Register
    {
        std::string text;
        bool        linewise = false;
        bool        block    = false;
    };
    // The clipboard: the viewer's, unless a test gives one of its own.
    typedef std::function<void(const std::string& text)> copy_t;
    typedef std::function<bool(std::string& text)>        paste_t;

    ALVimRegisters();
    void setClipboard(copy_t copy, paste_t paste);

    // What a yank (`yanked`) or a delete took, kept in the register named,
    // or where the kind of thing it was says with none (0): a yank in 0, a
    // delete less than a line in -. A delete goes in 1 as well, whichever
    // register is named, where it is a line or more, or `register_one`
    // says -- a delete over a search or a jump, which vim keeps there
    // however little it takes. The clipboard is the unnamed register where
    // `unnamed_clipboard` says so.
    void store(char name, std::string text, bool linewise, bool block, bool yanked, bool unnamed_clipboard, bool register_one = false);
    // What a register gives back: a named or numbered one what it keeps;
    // "" what was last put in any, as is none named where the clipboard is
    // not the unnamed register; + and *, and none named where it is, what
    // the clipboard holds -- with how it was taken, where it is what the
    // studio put there, an empty line too though the clipboard says it
    // holds nothing, and as characters otherwise. One never set holds no
    // text and is no line; so is what _ gives back, and what any other
    // name does.
    Register fetch(char name, bool unnamed_clipboard) const;
    // Keys a macro recorded, into its register as characters; its capital
    // adds to it. Neither the unnamed register nor the clipboard is
    // touched: what was typed is not what was taken.
    void record(char name, const std::string& keys);

private:
    std::map<char, Register> mRegisters;
    Register                 mUnnamed;
    copy_t                   mCopy;
    paste_t                  mPaste;
};
