/**
 * @file alvimmappings.h
 * @brief Vim's key mappings: the :map family's table, vim's key notation, and which mapping keys typed make.
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

#include "indra_constants.h"
#include "llpreprocessor.h"
#include "stdtypes.h"

#include <string>
#include <string_view>
#include <vector>

// One thing typed to vim: a character, or a key with its modifiers.
struct ALVimInput
{
    // The Control of vim's chords: the Mac's own Control key there, where
    // MASK_CONTROL is Command and belongs to the menus -- Command-C copies
    // and Command-V pastes in every mode -- and Control everywhere else.
#if LL_DARWIN
    static constexpr MASK CONTROL = MASK_MAC_CONTROL;
#else
    static constexpr MASK CONTROL = MASK_CONTROL;
#endif
    bool    isChar = false;
    llwchar ch     = 0;
    KEY     key    = KEY_NONE;
    MASK    mask   = MASK_NONE;

    static ALVimInput character(llwchar c)
    {
        ALVimInput in;
        in.isChar = true;
        in.ch     = c;
        return in;
    }
    static ALVimInput keyOf(KEY k, MASK m = MASK_NONE)
    {
        ALVimInput in;
        in.key  = k;
        in.mask = m;
        return in;
    }
    // The same thing typed: the same character, or the same key with the
    // same modifiers.
    bool sameAs(const ALVimInput& other) const
    {
        return isChar == other.isChar &&
               (isChar ? ch == other.ch : key == other.key && (mask & MASK_MODIFIERS) == (other.mask & MASK_MODIFIERS));
    }
};

// Vim's key mappings, as the :map family makes them: keys typed in a mode
// that stand for other keys, fed on as though typed -- mapped again where
// the mapping was made with :map, not where it was made with :noremap.
// Normal, visual and operator-pending mode have their own, as do insert
// mode and the : and / lines; :map is the first three, :map! the last two.
// The table and vim's key notation are here; the keys held while they may
// yet be the start of one, and feeding what one stands for, are the
// keymap's.
class ALVimMappings
{
public:
    // The modes a mapping is made for, as bits.
    enum ModeBits : U8
    {
        NORMAL       = 1 << 0,
        VISUAL       = 1 << 1,
        OPERATOR     = 1 << 2,
        INSERT       = 1 << 3,
        COMMAND_LINE = 1 << 4
    };

    struct Mapping
    {
        std::vector<ALVimInput> from;
        std::vector<ALVimInput> to;
        U8                      modes   = 0;
        // Fed on unmapped: made with :noremap and its kin.
        bool                    noremap = false;
        // Taken at once, though a longer mapping starts with it.
        bool                    nowait  = false;
        // Made by the vimrc, which takes its own away before it is read again.
        bool                    vimrc   = false;
    };

    // Keys in vim's notation, as a mapping reads them: characters as they
    // are, and in angle brackets, whatever the case, <Esc> <CR> <Enter>
    // <Return> <NL> <Tab> <BS> <Del> <Insert> <Up> <Down> <Left> <Right>
    // <Home> <End> <PageUp> <PageDown> <F1>-<F12>, a key or a character
    // with C- S- A- or M- before it, <Space> <lt> <Bar> <Bslash>, <Leader>
    // and <LocalLeader> as the leaders are set now, and <Nop> as nothing.
    // A name it does not know is the characters it is written with.
    std::vector<ALVimInput> keysOf(std::string_view text) const;
    // Keys spelt in the notation, as :map lists them.
    static std::string shown(const std::vector<ALVimInput>& keys);
    // Keys as a register holds them, what q records and @ plays: the
    // characters as they are but for < as <lt>, every other key by its name
    // in the notation, F1 to F12 included; and read back, the notation's
    // names taken whatever their case, <Leader> not among them.
    static std::string             written(const std::vector<ALVimInput>& keys);
    static std::vector<ALVimInput> keysWritten(std::string_view text);

    // One of the :map family, by its name -- a bang part of it -- and what
    // followed it: map, noremap, unmap and mapclear, each for the modes
    // its first letter says, n x v o i c, or with a bang insert and the
    // : line; with <nowait>, <unique>, <silent> or <buffer> before the keys.
    // With keys and what they stand for, a mapping made; with keys alone,
    // or none, the mappings that start with them listed in `listing`.
    // False where the name is none of the family; `error` says what went
    // wrong where anything did. `vimrc`: made by the vimrc.
    bool command(const std::string& name, const std::string& args, bool vimrc, std::string& listing, std::string& error);
    // :let mapleader and maplocalleader, and with g: before them: the
    // value in double quotes, where \<Space> is a key and \\ a backslash,
    // or in single quotes as it is. False where the name is another;
    // `error` says where the value could not be read.
    bool let(const std::string& args, std::string& error);

    // What keys typed in a mode are: the longest mapping they begin with,
    // whole; and whether they are the start of a longer one, where more may
    // be typed after them.
    struct Match
    {
        const Mapping* full   = nullptr;
        bool           longer = false;
    };
    Match match(U8 mode, const std::vector<ALVimInput>& keys, bool more_may_come) const;
    // Whether a mapping in the mode starts with this key.
    bool  starts(U8 mode, const ALVimInput& key) const;
    // The most keys any mapping in the mode is made of: as many as a
    // match need look at. None where the mode has none.
    size_t longest(U8 mode) const;
    bool  empty() const { return mMappings.empty(); }
    const std::vector<Mapping>& mappings() const { return mMappings; }

    const std::string& leader() const { return mLeader; }
    const std::string& localLeader() const { return mLocalLeader; }

    // What the vimrc made taken away, and the leaders back to vim's, for
    // it to be read again.
    void forgetVimrc();

private:
    // A name in angle brackets as keys, added to `out`; false where it is
    // no name the notation knows. namedKey() knows every name but the
    // leaders and <Nop>, which are the table's.
    bool        named(const std::string& name, std::vector<ALVimInput>& out) const;
    static bool namedKey(const std::string& name, std::vector<ALVimInput>& out);
    // The mappings in `modes` whose keys start with `from`, listed.
    std::string list(U8 modes, const std::vector<ALVimInput>& from) const;

    std::vector<Mapping> mMappings;
    // As they were set, in the notation.
    std::string          mLeader      = "\\";
    std::string          mLocalLeader = "\\";
};
