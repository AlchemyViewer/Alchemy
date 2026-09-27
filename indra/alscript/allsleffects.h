/**
 * @file allsleffects.h
 * @brief What LSL code may change, and the order it runs in.
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

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <vector>

namespace Tailslide
{
    class LSLASTNode;
    class LSLScript;
    class LSLSymbol;
}

// What a stretch of LSL may change, and which parts of a statement run
// before which, over Tailslide's tree once its symbols are resolved: what
// moving code needs to know to move it without anything seen differently.
//
// LSL's order is the same under LSO and Mono: a binary operator's right
// operand runs before its left, `&&` and `||` included and neither
// stopping early; a call's arguments run left to right, then the call; an
// assignment's value runs before its store. The elements of a list, a
// vector and a rotation are taken as if in no known order.
class ALLSLEffects
{
public:
    // What running some code may change: the variables it assigns, and
    // whether it does anything past that -- calls a library function that
    // is not pure, which may change the world or read what changes.
    struct Writes
    {
        boost::unordered_flat_set<Tailslide::LSLSymbol*> variables;
        bool                                             impure = false;

        bool writes(Tailslide::LSLSymbol* variable) const { return variables.contains(variable); }
        void add(const Writes& more);
    };

    // Each of the script's functions' writes, through the functions it
    // calls, worked out once.
    explicit ALLSLEffects(Tailslide::LSLScript* script);

    // What calling a function may change: the globals it assigns, itself
    // and through what it calls, and whether anything it does is impure.
    // One the script does not define is taken as impure.
    const Writes& ofFunction(Tailslide::LSLSymbol* function) const;
    // What running a node may change: its assignments and increments,
    // and what the calls in it may.
    Writes of(Tailslide::LSLASTNode* node) const;

    // The subtrees of `root` that run before `node` does, in no order.
    // `node` must be within `root`.
    static std::vector<Tailslide::LSLASTNode*> before(Tailslide::LSLASTNode* root, Tailslide::LSLASTNode* node);

    // Whether `node` may be run ahead of the rest of `root` -- moved out
    // before its statement -- with nothing seen differently: all that
    // runs before it changes nothing, calls nothing but pure library
    // functions, and reads nothing it may change.
    bool mayRunFirst(Tailslide::LSLASTNode* root, Tailslide::LSLASTNode* node) const;

private:
    boost::unordered_flat_map<Tailslide::LSLSymbol*, Writes> mFunctions;
    Writes                                                  mUnknown;
};
