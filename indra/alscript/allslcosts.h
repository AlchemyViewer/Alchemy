/**
 * @file allslcosts.h
 * @brief What LSL forms cost on each target, as Tailslide's compilers make them.
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

// What forms of LSL cost on each target, in bytes of the code its compiler
// makes of them: Tailslide's LSO image, its Mono IL as ALScriptWeigh sizes
// it, and the Luau bytecode the SLua fork's compiler makes of LSL. The
// optimizer asks here wherever a choice is smaller on one target and larger
// on another -- which is most of them: a form that saves under Mono can
// cost under LSO, and more under Luau -- rather than following a rule of
// thumb made for one of them.
//
// Every answer is measured: tests/allslcosts_test.cpp compiles pairs of
// snippets with ALScriptWeigh and fails when a compiler changes and an
// answer here should change with it. A cost is what one more use adds;
// what a target spends once for a whole script, as Mono's reference to a
// helper it calls, is left out unless it is named, since a script of any
// size has paid it already.
struct ALLSLCosts
{
    // ---- rewrites, each true where the rewritten form is smaller

    // A float literal whose value is whole written as an integer, where LSL
    // converts one: `2` for `2.0`, and in a vector's or rotation's parts.
    bool integerForFloat  = false;
    // llGetListLength(l) as `l != []`.
    bool lengthAsNotEqual = false;
    // A list `[a, b]` as `(list)a + b`.
    bool listAsSum        = false;
    // A list's element added bare: `l + [a]` as `l + a`, `[a] + l` as
    // `a + l`, `l + [a, b]` as `l + a + b`, `l + (list)x` as `l + x`; and
    // the most elements of a literal that are, where there is a most --
    // Luau's sum of many holds a register for each, where its literal is
    // one table.
    bool elementForList      = false;
    S32  elementsForListMost = 0;
    // Where a list's operations are calls to helpers the assembly
    // references once for the whole script (Mono): the least any of a
    // list's shapes saves at a place, and the most a helper new to the
    // script costs. Where the places come to more than the helpers, the
    // shapes are smaller without a weighing to say so.
    S32  listShapeLeast = 0;
    S32  listHelperMost = 0;
    // `x != -1` as `~x` where only whether it is true counts -- and, of a
    // value never below -1, `x > -1` and `x >= 0` too.
    bool complementForNotMinusOne = false;
    // `x == -1` as `!~x` -- and, of a value never below -1, `x < 0`.
    bool notComplementForMinusOne = false;
    // `x + 1` as `-~x`, `x + 2` as `-~-~x`.
    bool negateComplementForIncrement = false;
    // `x - 1` as `~-x`, `x - 2` as `~-~-x`.
    bool complementNegateForDecrement = false;
    // `x++` as `++x`, and `x--` as `--x`, where the value goes unused.
    bool preForPost = false;

    // ---- functions, for whether putting one in place saves

    // A local declared and set, then read, against the value used where it
    // is read.
    S32 local         = 0;
    // A jump and the label it goes to.
    S32 jump          = 0;
    // A function taking and returning nothing, with an empty body, and what
    // each character of its name adds to it.
    S32 function      = 0;
    S32 functionChar  = 0;
    // What each parameter adds to a function.
    S32 parameter     = 0;
    // A call of a function taking nothing, as a statement, and what each
    // argument adds to it past its value's own code.
    S32 call          = 0;
    S32 argument      = 0;
    // What a function's first call costs past the others -- under Mono, the
    // reference to its method -- and what each character of its name adds.
    S32 reference     = 0;
    S32 referenceChar = 0;

    // ---- globals never assigned, for whether their value goes where they are read

    // A global's value written where it is read, against the global.
    struct Held
    {
        // The global, its value included.
        S32 global = 0;
        // The value written out, against the global read: at the first
        // place, and at each place after, where a target keeps a constant
        // once and loads it again for less.
        S32 first  = 0;
        S32 each   = 0;

        // Whether writing the value at all `reads` places costs less than
        // keeping the global.
        bool writeOut(S32 reads) const { return reads > 0 && first + (reads - 1) * each < global; }
    };
    Held integer;
    // A float, a vector and a rotation whose parts are all whole, and those
    // with any part that is not: the one may be written as integers
    // (integerForFloat).
    Held floating;
    Held wholeFloating;
    Held vector;
    Held wholeVector;
    Held rotation;
    Held wholeRotation;
    // A string, and what each of its characters adds to each of its costs.
    Held string;
    Held stringChar;

    Held stringOf(S32 length) const
    {
        return Held{ string.global + length * stringChar.global, string.first + length * stringChar.first, string.each + length * stringChar.each };
    }

    // What a string literal costs once however many places write it -- its
    // entry in what the code shares: Mono's user strings, a Luau function's
    // constants -- and what each of its characters adds to that.
    S32 stringHeld     = 0;
    S32 stringHeldChar = 0;

    // What putting a function in place spends, less what it saves: each of
    // `calls` of it, passing `params` arguments, goes as a copy of its body
    // -- its `bytes` less what the function as such costs, and less what
    // its `strings` distinct string literals of `chars` characters in all
    // are held once for -- with `locals` declared in each copy for its
    // parameters and its value, and `jumps` for its returns. Negative is
    // smaller. `name` is the length of the function's name, which some of
    // its costs are counted over.
    struct Inlining
    {
        S32    bytes   = 0;
        S32    calls   = 0;
        S32    params  = 0;
        S32    locals  = 0;
        S32    jumps   = 0;
        S32    strings = 0;
        S32    chars   = 0;
        size_t name    = 0;
    };
    S32 inlined(const Inlining& f) const;

    static const ALLSLCosts& of(ALLSLOptimizer::Target target);
};
