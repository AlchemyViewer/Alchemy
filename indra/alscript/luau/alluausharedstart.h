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

#include <string>
#include <string_view>
#include <vector>

// What keeping the start of several strings once makes of an SLua script
// (ALScriptWeigh::sharedStarts): each literal of one of them in the script
// made the start, kept in a local of its own, with the rest joined on --
// `"You have touched the door"` becoming `sharedStart .. "door"`, one that
// is the start itself the local alone -- and the local declared at the top,
// past the script's `--!` comments, given its value apart from its
// declaration, so that Luau, which joins constant strings as it compiles,
// a local never assigned again among them, keeps it apart. What a literal
// stands in brackets where `..` would bind otherwise than the literal did.
// Constant strings joined, which Luau folds into one, have their first piece
// keep the start. A table's key, a require's path and an interpolated
// string's pieces are left as they are.
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
    // False, with why, where the source does not parse, or holds no
    // literal of the strings.
    bool rewrite(std::string_view source, const std::string& start, const std::vector<std::string>& strings, Rewrite& out, std::string& error);

    // A string written as SLua reads it back the same: quoted, a backslash,
    // a quote and every control byte escaped.
    std::string quoted(std::string_view text);
}
