/**
 * @file alscriptformatter.h
 * @brief A script's layout put right: indentation from its structure, spaces where the language reads better with them.
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

// A formatter over the tokens of a script rather than its tree, so that
// every comment, string and line break the author wrote is still there
// afterwards; what changes is the whitespace. Each line is indented by
// the depth of the blocks and brackets open at its start -- braces for
// LSL, `then`, `do`, `function`, `repeat` and their `end` for Luau,
// brackets and parentheses in both, and a statement hanging off an `if`
// or `else` without braces -- and within a line the spaces are put
// where the language reads best: one around a binary operator, none
// around a unary one or inside brackets, one after a comma, none
// before it, and inside Luau's braces as asked. What the author spaced deliberately is left alone: the
// gap before a trailing comment, a preprocessor line, anything inside
// a string or a comment. Runs of blank lines are shortened and trailing
// whitespace dropped, unless only some lines are asked for, in which
// case every line keeps its number. A line too long may be broken at
// the commas of a bracket on it; no other break is added.
class ALScriptFormatter
{
public:
    struct Options
    {
        bool lua = false;
        // How far each level goes in, in spaces; or one tab.
        S32  indent = 4;
        bool tabs   = false;
        // How many blank lines in a row may stay.
        S32  maxBlankLines = 2;
        // Whether the spaces within a line are touched at all, or only
        // the indentation.
        bool spacing = true;
        // Luau: a space inside a table's braces, and a table type's, as
        // StyLua writes them -- { 1, 2 }, { any } -- or none, {1, 2}.
        // Never inside {}, nor an interpolated string's.
        bool braceSpaces = true;
        // How many columns a line may take before it is broken, a tab as
        // wide as a level: at the commas of the widest bracket on it that
        // has any, or of the table or list a call ends with, which stays
        // on the call's line -- `f(a, {` and `})` -- each part on a line
        // of its own, a level further in, and broken again where it is
        // still too long. Never a vector or rotation, nor a line with a
        // comment inside it or a string or comment running on past it.
        // Nought for never. Not where only some lines are asked for of
        // formatLines, whose lines keep their numbers.
        S32  width = 0;
    };

    // The whole text.
    static std::string format(std::string_view text, const Options& options);
    // Only the lines from `first` to `last`, zero-based and inclusive,
    // changed; the rest as it was, and every line where it was.
    static std::string formatLines(std::string_view text, const Options& options, S32 first, S32 last);
    // The same, as each line of the text in order, one entry a line --
    // and an entry for the empty line after a final newline -- each of
    // those from `first` to `last` broken past the width, its entry
    // holding the breaks. A `first` below nought is the first line: no
    // line is taken out.
    static std::vector<std::string> formatEach(std::string_view text, const Options& options, S32 first, S32 last);

    // Which line breaks stand inside a string, one for each line of `text`:
    // true where the break that ends the line is part of a string that runs
    // on to the next -- a Luau long string, a string continued by a
    // backslash, an LSL string with a break written into it. The blanks
    // before such a break, and a blank line after it, are the string's and
    // not the layout's: nothing that tidies a script's lines may touch them.
    static std::vector<bool> breaksInStrings(std::string_view text, bool lua);
};
