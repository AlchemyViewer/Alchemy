/**
 * @file allsloptimizer.h
 * @brief The LSL optimizer over Tailslide's tree.
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

#include <map>
#include <string>
#include <string_view>

// The simulator's compilers optimise nothing, so every byte a script
// saves is saved before upload: this runs over Tailslide's tree after
// symbol resolution and type checking, folds what is constant with the
// target's arithmetic, simplifies what can be simplified, removes what
// can never run or is never used, and prints the result. The passes have
// the names scripters already know for them, and the same defaults.
//
// A fold gives what the VM would have given, or is left alone: floats
// are rounded to single precision at every step for the LSL targets,
// integers wrap, a division by zero stays where the author wrote it,
// and a library call is folded only when the definitions say it has no
// side effects and its arguments are in hand. What was done is reported
// as notes, so removed code is never a mystery.
class ALLSLOptimizer
{
public:
    enum class Target : U8
    {
        Mono,
        LSO,
        // LSL compiled to Luau: doubles, so nothing is rounded to single.
        Luau
    };

    struct Options
    {
        Target target = Target::Mono;
        // Folding and simplification, on by default.
        bool constfold  = true;
        bool optsigns   = true;
        bool optfloats  = true;
        bool listadd    = true;
        bool listlength = true;
        bool ifelseswap = true;
        // Two literals joined make a new entry in the constant pool, which
        // can cost more than it saves; off by default.
        bool addstrings = false;
        // Removal, on by default.
        bool dcr = true;
        // Size, off by default: every name the script owns shortened.
        bool shrinknames = false;
        // The compiler turns a tab in a literal into spaces, so a fold that
        // would put one there is refused unless this says otherwise.
        bool foldtabs = false;
        // Whether every change made is noted. A caller that reads the
        // notes -- the studio's problems pane -- leaves this on; one
        // that only wants the text turns it off, and the run does not
        // print the before and after of every fold to say so.
        bool notes = true;
        // What one run may visit, over all its rounds: a script large
        // enough that the passes keep finding work would otherwise hold
        // whoever asked for as long as it liked. A run that reaches it
        // stops where it is and says so; what it has done so far stands.
        size_t visitBudget = 40u * 1000u * 1000u;
        // A user function called from one place put in that place, and a
        // small one returning an expression in every place, before the
        // rest; off by default. The functions named here are put in
        // place wherever they are called, whatever their size: what the
        // script marked `inline`.
        bool                     inlining = false;
        std::vector<std::string> inlineNames;
    };

    struct Result
    {
        // The optimized script, or the source as it was when nothing
        // could be done.
        std::string      text;
        // Errors that stopped the run, and notes on every change made.
        ALScriptProblems problems;
        // From the result's positions back to the source's, for what
        // was kept.
        ALSourceMap      map;
        // Original name to new, when names were shrunk.
        std::map<std::string, std::string> renamed;
        size_t sizeBefore = 0;
        size_t sizeAfter  = 0;
        bool   optimized  = false;
        // The run reached its budget and stopped early; what it did
        // stands, and there may have been more to do.
        bool   stoppedEarly = false;
    };

    // Needs the builtins loaded through ALLSLService first; without them
    // the run says so and does nothing.
    static Result run(std::string_view source, const Options& options);
};
