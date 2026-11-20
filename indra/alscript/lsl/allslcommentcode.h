/**
 * @file allslcommentcode.h
 * @brief LSL a comment switched off, converted to SLua where it stands and kept commented.
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

#include "allslcomments.h"
#include "allsltoslua.h"

#include <string_view>
#include <vector>

// The code a script's comments switched off, written as SLua, still
// commented, where each comment stood (ALLSLToSLua::Options::commentCode).
//
// Each comment that may be code (ALLSLComments::units) is tried in place:
// the script is converted again with it uncommented, its markers blanked,
// so that its code resolves against the script around it and takes the
// converter's inferences. Where that parses with no error, gives the
// comment's code lines of its own, and leaves every other line as the plain
// conversion writes it, the comment is written as those lines, commented
// as it was: line comments as --, a block as --[[ ]], the toggle as
// --[[ ... --]]. Anywhere else it stays as the LSL had it; nothing else of
// the script is ever written otherwise.
class ALLSLCommentCode
{
public:
    // Why a unit stayed as the LSL had it, or that it did not.
    enum class Outcome : U8
    {
        Converted,
        // Uncommented, the script does not parse or resolve.
        Refused,
        // Its code is no statement of its own: part of an expression, or
        // nothing the converter writes.
        NoStatement,
        // Its code reaches past it, or takes in the script's.
        Spills,
        // Uncommenting it changes how other lines are written.
        Changes,
        // Its SLua cannot stand where it was: more than a line after code.
        Unplaced,
        // Past the trials one conversion makes.
        Capped,
    };
    struct Tried
    {
        ALLSLComments::Unit unit;
        Outcome             outcome = Outcome::Refused;
        // What it was written as, where it was converted: its lines past
        // the first as deep as the first, which the writer indents.
        std::string         text;
    };
    // What one conversion tried, for the corpus counts: each unit's last
    // outcome, a run's pieces after it, and how many conversions it took.
    struct Report
    {
        std::vector<Tried> tried;
        S32                conversions = 0;
    };

    // The most trial conversions one conversion makes before it leaves the
    // rest as they are.
    static constexpr S32 MAX_TRIALS = 64;

    static ALLSLToSLua::Result convert(std::string_view lsl, const ALLSLToSLua::Options& options, Report* report = nullptr);
    static const char*         nameOf(Outcome outcome);
};
