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
    // What SLua makes of a function: its `ll` answers an index from one,
    // or nil for none, where LSL answers one from nought, or -1
    // (SluaIndexResult); takes an index from one where LSL takes one from
    // nought, in the arguments `sluaIndexArgs` says (SluaIndexArgs); answers
    // a boolean where LSL answers 1 or 0 (SluaBool), or gives booleans in
    // the list it answers where LSL gave 1 or 0 (SluaBoolList); or lacks it,
    // leaving it to `llcompat` alone (SluaRemoved), or has it but deprecates
    // it (SluaDeprecated). `llcompat` has every function as LSL has it, but
    // those SLua has nowhere (SluaAbsent).
    enum Slua : U8
    {
        SluaIndexResult = 1 << 0,
        SluaIndexArgs   = 1 << 1,
        SluaBool        = 1 << 2,
        SluaRemoved     = 1 << 3,
        SluaBoolList    = 1 << 4,
        SluaAbsent      = 1 << 5,
        SluaDeprecated  = 1 << 6,
        SluaIndex       = SluaIndexResult | SluaIndexArgs,
    };
    struct Trait
    {
        const char* name;
        bool        pure;
        bool        mustUse;
        bool        native;
        U8          slua;
        // The arguments that are an index, a bit for each by its place; and
        // those SLua takes text for, a string or a uuid, likewise.
        U16         sluaIndexArgs;
        U16         sluaTextArgs;
        // What SLua would use in its stead, where it says: a library
        // function of its own, an operator; or null. And why, where it says.
        const char* sluaUse;
        const char* sluaReason;
    };
    // The function's row, or null for a name the definitions lack.
    static const Trait* of(const char* name);
    static bool         pure(const char* name);
    // A constant LSL types a string that SLua types a uuid: NULL_KEY, the
    // TEXTURE_ and IMG_USE_BAKED_ ones, COMBAT_LOG_ID.
    static bool uuidConstant(std::string_view name);
    // An event's parameter, by its place from nought, that LSL types a key
    // and SLua passes as a string: link_message's id.
    static bool eventTextParam(std::string_view event, int index);
    // Whether text is a UUID as LSL writes one: 8-4-4-4-12 hexadecimal.
    static bool isUuid(std::string_view text);

    // What an expression can be dropped, or read at another time, without
    // losing anything: no assignment, no call but to a function the
    // definitions call pure, no print. A null node is nothing.
    static bool sideEffectFree(Tailslide::LSLASTNode* node);
    // What an expression can be dropped without losing anything, though
    // not read at another time: as sideEffectFree, but a call may also be
    // to one whose result is the only point of calling it -- llGetPos,
    // llGetTime -- which reads what changes and changes nothing.
    static bool changesNothing(Tailslide::LSLASTNode* node);

    // A function whose answer is an index or -1, never below: what finds
    // something, and INVENTORY_NONE. What the definitions do not say, so
    // kept by hand.
    static bool atLeastMinusOne(const char* name);
};
