/**
 * @file allsltraits.h
 * @brief What the definitions say of each library function.
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

namespace Tailslide
{
    class LSLASTNode;
}

// What lsl_definitions.yaml says of each library function -- whether it
// has no side effects, whether its result is the point of calling it, and
// whether it needs a native implementation off LSO -- read into a table
// the library compiles in, for the optimizer and the inliner to consult
// with one answer between them.
class ALLSLTraits
{
public:
    struct Trait
    {
        const char* name;
        bool        pure;
        bool        mustUse;
        bool        native;
    };
    // The function's row, or null for a name the definitions lack.
    static const Trait* of(const char* name);
    static bool         pure(const char* name);

    // What an expression can be dropped, or read at another time, without
    // losing anything: no assignment, no call but to a function the
    // definitions call pure, no print. A null node is nothing.
    static bool sideEffectFree(Tailslide::LSLASTNode* node);
    // What an expression can be dropped without losing anything, though
    // not read at another time: as sideEffectFree, but a call may also be
    // to one whose result is the only point of calling it -- llGetPos,
    // llGetTime -- which reads what changes and changes nothing.
    static bool changesNothing(Tailslide::LSLASTNode* node);
};
