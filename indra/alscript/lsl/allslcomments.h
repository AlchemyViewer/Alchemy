/**
 * @file allslcomments.h
 * @brief The comments of an LSL script read from its source alone, and those that may hold code.
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

// The comments of an LSL script, read from its source alone, past its
// strings: each line comment and block comment, where it begins and ends,
// and the //* ... //*/ toggle's markers paired as the converter writes them
// (ALLSLToSLua's keepComments). And of those, the units that may be code
// switched off -- a run of line comments alone on their lines, a block
// comment, a toggle turned off, a line comment after code -- which the
// converter tries uncommented (ALLSLCommentCode). Nothing here parses LSL.
class ALLSLComments
{
public:
    struct Comment
    {
        enum class Kind : U8
        {
            Line,
            Block
        };
        Kind   kind  = Kind::Line;
        // From its first slash to one past its last byte, a line comment's
        // break not in it.
        size_t begin = 0;
        size_t end   = 0;
        // Where it begins and the line it ends on, each from one.
        S32    line    = 1;
        S32    column  = 1;
        S32    endLine = 1;
        // Only blanks before it on its first line, and after it on its last.
        bool   alone   = false;
        // A block comment's */ found.
        bool   closed  = true;
        // //*, over code a later closer pairs it with: a / taken out of it
        // makes a block comment of what is between. The closer's index.
        bool   opener   = false;
        size_t closedBy = 0;
        // //*/, the toggle's closer, about the code over it.
        bool   closer  = false;
        // /* closed by //*/ on another line: the toggle turned off.
        bool   toggled = false;
    };
    static std::vector<Comment> scan(std::string_view source);

    // A stretch of comments that may be code switched off.
    struct Unit
    {
        enum class Shape : U8
        {
            // Line comments alone on their lines, one under another.
            Lines,
            // A block comment alone on its lines.
            Block,
            // /* ... //*/, alone on its lines.
            Toggle,
            // A line comment after code on its line.
            Trailing
        };
        Shape  shape = Shape::Lines;
        // The comments it is, by their index in scan()'s: first to last.
        size_t first = 0;
        size_t last  = 0;
        // From its first comment's first slash to one past its last's end.
        size_t begin = 0;
        size_t end   = 0;
        // Its first comment's line and column, and its last's line, from one.
        S32    line    = 1;
        S32    column  = 1;
        S32    endLine = 1;
    };
    // The units, in the order written: each run of line comments alone on
    // consecutive lines, each block comment and toggle alone on their lines,
    // and each line comment after code. Not a toggle's markers, a rule (a
    // comment beginning with a slash or a star), a comment whose code would
    // run past it once uncommented (an unclosed block, a string not closed
    // on its line, a /* or */ inside), nor one that cannot be code: with no
    // line that may be (mayBeCode), or nothing that ends code (endsCode). A
    // run is one unit; runOf splits it.
    static std::vector<Unit> units(std::string_view source, const std::vector<Comment>& comments);
    // A run split at its lines that cannot be code, each stretch between
    // them a unit; or each of its lines that may be, one a unit. Each that
    // ends code.
    static std::vector<Unit> runOf(const Unit& run, std::string_view source, const std::vector<Comment>& comments, bool lines);

    // What a comment holds, past its markers: a line comment's text after
    // //, a block comment's between /* and */ (and the toggle's //).
    static std::string_view body(std::string_view source, const Comment& comment);
    // Whether text may be LSL code: it has a ;, a brace or a bracket a call
    // opens. Words alone are prose.
    static bool mayBeCode(std::string_view text);
    // Whether text may be whole LSL code: every statement, declaration,
    // function, handler and state ends with a ; or a }.
    static bool endsCode(std::string_view text);
    // The source with the unit's markers blanked to spaces, so that what
    // they held is code where it stands, every line and column where it was.
    static std::string uncommented(std::string_view source, const std::vector<Comment>& comments, const Unit& unit);
    static void        uncomment(std::string& source, const std::vector<Comment>& comments, const Unit& unit);

    // A byte the source has none of, below a space and not a tab or a line's
    // break, for marking what is written of it: the lowest such, past
    // `after`; nought where it has every one.
    static char freeByte(std::string_view source, char after = 0);
    // Several such, the highest first, none of them `but`; fewer where the
    // source has fewer.
    static std::vector<char> freeBytes(std::string_view source, size_t count, char but);
};
