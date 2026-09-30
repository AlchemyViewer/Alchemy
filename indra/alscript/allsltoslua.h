/**
 * @file allsltoslua.h
 * @brief An LSL script written again as SLua, where the two languages differ noted rather than guessed.
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

#include "alscriptproblem.h"

#include <string>
#include <string_view>
#include <vector>

// An LSL script written again as SLua, from Tailslide's tree of it: its
// globals and functions as locals, its states as tables of handlers put on
// LLEvents as each is entered, its expressions in Luau's operators and
// precedence, and each library call to SLua's `ll`, or to `llcompat` where
// `ll` means something else -- an index from one, a boolean for 1 or 0 --
// or lacks the function, so that what the script does stays what it did.
//
// Where the two languages mean different things and the text cannot say the
// same -- integer division rounding down, a jump SLua has no goto for,
// LSL's lists compared by their lengths -- the place is noted rather than
// guessed: a `-- LSL:` comment over the line, and a note of the same words
// at the LSL's place. Where SLua has a way of its own -- LLTimers, the
// detected table, indexing -- the note says so too.
class ALLSLToSLua
{
public:
    struct Result
    {
        bool             converted = false;
        std::string      text;
        // Where the two languages differ, each where it stands in the LSL.
        ALScriptProblems notes;
        // Why nothing was converted: the LSL does not parse, or the
        // definitions are not loaded.
        ALScriptProblems problems;
    };

    static Result convert(std::string_view lsl);
};
