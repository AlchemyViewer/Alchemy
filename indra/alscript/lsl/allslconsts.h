/**
 * @file allslconsts.h
 * @brief The const keyword: what a script declares constant, checked and folded.
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

#include "allsloptimizer.h"
#include "alscriptproblem.h"

#include <string>
#include <string_view>
#include <vector>

// What an LSL script declared `const` -- the preprocessor takes the word off
// and says where each name it declared is (ALPreprocessor::Result::consts)
// -- held to what the word promises, over the expanded text:
// - a const variable is set where it is declared and nowhere else, and a
//   global or a local one is given a value there;
// - a const function works out what it gives back from its arguments
//   alone: it sets no global, reads none the script changes, calls nothing
//   that does more than work out a value, and changes no state.
// A const global's value may be any expression that can be worked out
// before the script runs -- LSL takes only a literal there -- and is
// worked out with the target's arithmetic (ALLSLArithmetic) into one.
// A text that does not parse, or whose types do not agree, is the
// compiler's to say what is wrong with: nothing is said of it here.
class ALLSLConsts
{
public:
    // A name declared const, where its declaration names it in the text,
    // zero-based; and whether it is a function's.
    struct Declared
    {
        std::string name;
        S32         line     = 0;
        S32         column   = 0;
        bool        function = false;
    };

    // A const global's value as the literal it comes to, where the text's
    // is not one LSL takes there: the stretch of the text to replace,
    // zero-based and its end past its last character, and what with.
    using Value = ALScriptEdit;

    struct Result
    {
        // Errors, each where the promise is broken, in the text's places.
        ALScriptProblems   problems;
        // In the order they are in the text.
        std::vector<Value> values;
    };

    // Needs the builtins loaded through ALLSLService, as the optimizer
    // does; without them nothing is said.
    static Result run(std::string_view text, const std::vector<Declared>& declared, ALLSLOptimizer::Target target);
};
