/**
 * @file alfuzzymatch.h
 * @brief How well a few letters typed answer a name: one matcher for every list that narrows as it is typed at.
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

#include "stdtypes.h"

#include <string>
#include <string_view>
#include <vector>

// How well what was typed answers a name, by the kind of answer it is:
// the name's start, a run from where one of its parts begins, a letter at
// the start of each of several parts, a run anywhere, its letters merely
// in order. The parts begin after an underscore, a dot, a colon, a dash,
// a slash or a blank; at a capital after a small letter, and at the last
// capital of a run before a small one; and at a digit. Letters match in
// either case, ASCII's.
//
// What each kind is worth is the caller's: completion takes the first four
// and ranks a part's run above the parts' letters, Quick Open takes all of
// them and ranks the letters of a name's words above a run inside one. The
// rules of what matches, and as what, are the same for both.
//
// A name matched many times -- a list's every entry at every key -- is
// prepared once (Target): lowered, and where its parts begin found.
class ALFuzzyMatch
{
public:
    enum class Tier : S8
    {
        None = -1,
        // Its start, in the case typed; in either case.
        Prefix,
        PrefixAnyCase,
        // A run from where one of its parts begins: `Say` in `llSay`.
        PartRun,
        // A letter at the start of each of several parts, runs of each
        // after: `sp` or `spos` in `llSetPos`, `flbuy` in `floater_buy`.
        Parts,
        // A run anywhere: `oater` in `floater`.
        Run,
        // Its letters in order, anywhere.
        Scattered,
    };

    struct Match
    {
        Tier tier = Tier::None;
        // Where a run begins; for scattered letters, how far in the last
        // of them is.
        size_t at = 0;
        explicit operator bool() const { return tier != Tier::None; }
    };

    // A name made ready to be matched many times.
    struct Target
    {
        std::string       text;
        std::string       lowered;
        std::vector<bool> parts;
    };
    static Target prepare(std::string_view text);

    // How the name answers what was typed, no worse than `worst`: a match
    // of a lesser kind is none.
    static Match match(const Target& target, std::string_view typed, Tier worst = Tier::Scattered);
    static Match match(std::string_view name, std::string_view typed, Tier worst = Tier::Scattered);

    // Whether a part of a name begins at a byte of it.
    static bool partAt(std::string_view name, size_t at);
    static char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
};
