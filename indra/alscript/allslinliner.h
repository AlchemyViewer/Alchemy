/**
 * @file allslinliner.h
 * @brief A user function called once put where it is called, in the text.
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
#include "alsourcemap.h"

#include <string>
#include <string_view>
#include <vector>

// A user function called from one place is put in that place, and the
// function goes: a call is a frame the script pays for, and a function
// with one caller is a block that was given a name. A small function
// returning an expression goes in every place it is called, since the
// expression costs about what the call did; and a function the script
// marked `inline` goes in every place whatever its size. Done in the
// text, from what the parse says of it, so that the script the
// optimizer then reads is an ordinary script with ordinary scopes.
//
// Two shapes are taken. A function returning nothing, called as a
// statement, becomes a block: each parameter a local of the block set
// to its argument, then the body's statements, a return in it a jump to
// a label that ends the block, its own labels renamed, provided it changes
// no state. A function whose body is one `return expression;` becomes
// that expression wherever it is called, its parameters replaced by the
// arguments -- constants, names, or expressions that change nothing,
// in parentheses; one a parameter reads more than once goes into a
// temporary before the statement. A name the body declares that is
// already visible where the call is gets a new one, since LSL has no
// shadowing. A function that reaches itself through calls is never put
// in place. Anything else is left as the author wrote it.
class ALLSLInliner
{
public:
    struct Result
    {
        std::string      text;
        // From the text's positions back to the source's.
        ALSourceMap      map;
        // A note for each function put in place.
        ALScriptProblems notes;
        // How many were.
        S32              inlined = 0;
    };

    // Needs the builtins loaded through ALLSLService, as the optimizer
    // does; the source must parse, or it is returned as it is. The
    // functions named are put in place wherever they are called.
    static Result run(std::string_view source, const std::vector<std::string>& marked = {});
};
