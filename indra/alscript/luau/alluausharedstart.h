/**
 * @file alluausharedstart.h
 * @brief Strings that start alike in an SLua script, their start kept once.
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

#include <boost/unordered/unordered_flat_map.hpp>

#include <string>
#include <string_view>
#include <vector>

// What keeping the start of several strings once makes of an SLua script
// (ALScriptWeigh::sharedStarts): each literal of one of them in the script
// made the start, kept in a local of its own, with the rest joined on --
// `"You have touched the door"` becoming `sharedStart .. "door"`, one that
// is the start itself the local alone -- and the local declared before the
// first of the script that is not a comment, since Luau reads a `--!`
// comment as the script's own only before that, given its value apart from
// its declaration, so that Luau, which joins constant strings as it
// compiles, a local never assigned again among them, keeps it apart. What a
// literal stands in brackets where `..` would bind otherwise than the
// literal did. Constant strings joined, which Luau folds into one, have
// their first piece keep the start, and the pieces after it bracketed with
// the rest, which Luau would otherwise load one by one; but not where they
// are what a `..` that is not folded goes on to, which Luau joins piece by
// piece already, each a literal of its own. Left as they are: a table's
// key, a require's path, an interpolated string's pieces and what is filled
// into it, a literal Luau makes no string of -- `#"..."`, `not "..."`, two
// constants compared -- or that `and` or `or` decides on, one the script's
// own types name as a singleton (`"idle" | "busy"`, which `string` is not;
// the definitions' are not known here), and the one value of a local
// declared bare and given it once after: a start kept once already.
namespace ALLuauSharedStart
{
    // A stretch of the source and what is put there, zero-based, columns
    // in bytes; an insertion where it ends where it begins.
    struct Edit
    {
        S32         line      = 0;
        S32         column    = 0;
        S32         endLine   = 0;
        S32         endColumn = 0;
        std::string text;
    };
    struct Rewrite
    {
        // In the order of the text, none overlapping.
        std::vector<Edit> edits;
        // The local's name: `sharedStart`, numbered where the script has
        // that name.
        std::string name;
        size_t      literals = 0;
    };
    // False, with why, where the source does not parse; where any of the
    // strings is not written out in it as a literal that could give the
    // start, since the table would keep that one whole whatever is made of
    // the rest; or where what it would make does not parse, as a call's
    // string given without brackets on a line of its own does not once it
    // is bracketed. The edits are made over a copy and parsed first.
    bool rewrite(std::string_view source, const std::string& start, const std::vector<std::string>& strings, Rewrite& out, std::string& error);

    // Each string the source writes out as rewrite() would make a start
    // and the rest of, by what it comes to, and how long a start every
    // literal of it could give up: its own length where it is written
    // whole, the first piece's where Luau joins constant strings into it.
    // A string Luau makes as it compiles -- an interpolated string's
    // pattern, strings joined on a local's constant value -- is none of
    // them. Nothing where the source does not parse.
    boost::unordered_flat_map<std::string, size_t> written(std::string_view source);

    // A string written as SLua reads it back the same: quoted, a backslash,
    // a quote and every control byte escaped, and every byte that is no
    // part of a whole UTF-8 character, which the editor's text cannot hold.
    std::string quoted(std::string_view text);
}
