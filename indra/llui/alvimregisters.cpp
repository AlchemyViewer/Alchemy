/**
 * @file alvimregisters.cpp
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

#include "linden_common.h"

#include "alvimregisters.h"

#include "llclipboard.h"

#include <cctype>

ALVimRegisters::ALVimRegisters()
:   mCopy([](const std::string& text) { LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size())); }),
    mPaste([](std::string& text) { return LLClipboard::instance().pasteFromClipboard(text); })
{
}

void ALVimRegisters::setClipboard(copy_t copy, paste_t paste)
{
    mCopy  = std::move(copy);
    mPaste = std::move(paste);
}

void ALVimRegisters::store(char name, std::string text, bool linewise, bool block, bool yanked, bool unnamed_clipboard)
{
    Register reg;
    reg.text     = std::move(text);
    reg.linewise = linewise;
    reg.block    = block;
    if (name == '_')
    {
        return;
    }
    // A register named keeps it, and "" says it; the clipboard is not
    // touched, as vim's clipboard=unnamed leaves a named one alone.
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
        return;
    }
    if (name >= 'a' && name <= 'z')
    {
        mRegisters[name] = reg;
        mUnnamed         = reg;
        return;
    }
    // "+ and "* are the clipboard, whatever the setting says.
    if (name == '+' || name == '*')
    {
        mUnnamed = reg;
        mCopy(reg.text);
        return;
    }
    if (yanked)
    {
        mRegisters['0'] = reg;
    }
    else if (linewise || reg.text.find('\n') != std::string::npos)
    {
        // A delete of a line or more: the last nine kept, newest first.
        for (char n = '9'; n > '1'; --n)
        {
            const auto older = mRegisters.find(static_cast<char>(n - 1));
            if (older != mRegisters.end())
            {
                mRegisters[n] = older->second;
            }
        }
        mRegisters['1'] = reg;
    }
    else
    {
        // A smaller delete.
        mRegisters['-'] = reg;
    }
    // The unnamed register: the clipboard, which the world shares, where
    // the setting says so; the editor's own otherwise.
    mUnnamed = reg;
    if (unnamed_clipboard)
    {
        mCopy(reg.text);
    }
}

ALVimRegisters::Register ALVimRegisters::fetch(char name, bool unnamed_clipboard) const
{
    if (name >= 'A' && name <= 'Z')
    {
        name = static_cast<char>(name - 'A' + 'a');
    }
    if ((name >= 'a' && name <= 'z') || (name >= '0' && name <= '9') || name == '-')
    {
        const auto it = mRegisters.find(name);
        return it == mRegisters.end() ? Register() : it->second;
    }
    // "" by name: what was last put in a register, whichever it was. With
    // no register named, the same where the clipboard is not the unnamed
    // register's.
    if (name == '"' || (name == 0 && !unnamed_clipboard))
    {
        return mUnnamed;
    }
    // What the clipboard holds now: ours, with how it was taken, or
    // somebody else's, taken as characters.
    Register    reg;
    std::string text;
    if (mPaste(text))
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

void ALVimRegisters::record(char name, const std::string& keys)
{
    const bool append = name >= 'A' && name <= 'Z';
    Register&  reg    = mRegisters[static_cast<char>(std::tolower(name))];
    reg.text          = append ? reg.text + keys : keys;
    reg.linewise      = false;
    reg.block         = false;
}
