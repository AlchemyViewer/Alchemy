/**
 * @file allslcosts.cpp
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

#include "linden_common.h"

#include "allslcosts.h"

#include <algorithm>

namespace
{
    ALLSLCosts lso()
    {
        ALLSLCosts c;
        // An integer where a float is wanted is converted as the code runs.
        c.integerForFloat  = false;
        c.lengthAsNotEqual = true;
        c.listAsSum        = true;
        c.elementForList   = true;

        c.complementForNotMinusOne     = true;
        c.notComplementForMinusOne     = true;
        c.negateComplementForIncrement = true;
        c.complementNegateForDecrement = true;
        c.preForPost                   = true;
        // A float's, a key's, a vector's and a rotation's default loads
        // smaller than what writes it.
        c.incrementForAssign           = false;
        c.dropIntegerDefault           = false;
        c.dropFloatDefault             = true;
        c.dropKeyDefault               = true;
        c.dropVectorDefault            = true;
        c.dropRotationDefault          = true;
        c.dropIntegerGlobalDefault     = false;
        c.castForNegative              = false;
        c.emptyForNullKey              = true;
        c.comparisonForNot             = true;
        c.xorForNotEqual               = true;
        c.strictForInclusive           = false;
        c.bitOrForOr                   = false;
        c.emptyForLength               = true;
        c.castForDump                  = true;
        c.castForDetail                = true;
        c.castForWholeFloat            = false;
        c.bitTestsMerged               = true;
        // An && costs what an & does; two ifs one jump more than one; and
        // a do's check one jump less than a for's.
        c.bitAndForAnd                 = false;
        c.plusForMinus                 = false;
        c.productForShift              = false;
        c.complementForMinusOneLess    = true;
        c.sumForDouble                 = false;
        c.productForQuotient           = false;
        c.remainderForOddTest          = false;
        c.differenceForNotEqual        = false;
        c.quotientForShift             = false;
        c.andForNestedIf               = true;
        c.bitAndForNestedTruths        = true;
        c.arithmeticForSelect          = true;
        c.doForKnownFirst              = true;

        c.local         = 11;
        c.localFrame    = 0;
        c.jump          = 5;
        c.function      = 16;
        c.functionChar  = 0;
        c.parameter     = 3;
        c.call          = 21;
        c.argument      = 5;
        c.reference     = 0;
        c.referenceChar = 0;

        // The image holds every value where it is used.
        c.integer       = { 10, 0, 0 };
        c.floating      = { 10, 0, 0 };
        c.wholeFloating = { 10, 0, 0 };
        c.vector        = { 18, 10, 10 };
        c.wholeVector   = { 18, 10, 10 };
        c.rotation      = { 22, 15, 15 };
        c.wholeRotation = { 22, 15, 15 };
        c.string         = { 18, -3, -3 };
        c.stringChar     = { 1, 1, 1 };
        c.stringHeld     = 0;
        c.stringHeldChar = 0;
        return c;
    }

    ALLSLCosts mono()
    {
        ALLSLCosts c;
        // An integer the script writes loads in five bytes, as LL's compiler
        // writes every one and the grid keeps it, and a float in nine.
        c.integerForFloat  = true;
        c.lengthAsNotEqual = false;
        c.listAsSum        = true;
        c.elementForList   = true;
        c.listShapeLeast   = 5;
        c.listHelperMost   = 26;

        c.complementForNotMinusOne     = true;
        c.notComplementForMinusOne     = true;
        c.negateComplementForIncrement = true;
        c.complementNegateForDecrement = true;
        c.preForPost                   = true;
        // Mono loads an integer the script writes by ldc.i4, five bytes:
        // ++x adds the one it makes up itself in one; an integer's default
        // is ldc.i4.0; and the cast of a negative number is one constant,
        // where the number negated is two instructions.
        c.incrementForAssign           = true;
        c.dropIntegerDefault           = true;
        c.dropFloatDefault             = false;
        c.dropKeyDefault               = false;
        c.dropVectorDefault            = false;
        c.dropRotationDefault          = false;
        c.dropIntegerGlobalDefault     = true;
        c.castForNegative              = true;
        c.emptyForNullKey              = true;
        c.comparisonForNot             = true;
        c.xorForNotEqual               = true;
        c.strictForInclusive           = true;
        c.bitOrForOr                   = true;
        c.emptyForLength               = false;
        c.castForDump                  = true;
        // A byte a place, against the helper to cast a list to a string a
        // script may not have.
        c.castForDetail                = false;
        c.castForWholeFloat            = true;
        c.bitTestsMerged               = true;
        // An && is a branch, where & is one instruction; a subtraction, a
        // shift and a division by a float each a call into the helpers,
        // where an add, a product and a product are one; a constant five
        // bytes, a local's read two; and a do's check one jump less.
        c.bitAndForAnd                 = true;
        c.plusForMinus                 = true;
        c.productForShift              = true;
        c.complementForMinusOneLess    = true;
        c.sumForDouble                 = true;
        c.productForQuotient           = true;
        c.remainderForOddTest          = false;
        c.differenceForNotEqual        = false;
        c.quotientForShift             = false;
        c.andForNestedIf               = false;
        c.bitAndForNestedTruths        = true;
        c.arithmeticForSelect          = true;
        c.doForKnownFirst              = true;

        c.local         = 6;
        c.localFrame    = 31;
        c.jump          = 5;
        c.function      = 35;
        c.functionChar  = 1;
        c.parameter     = 14;
        c.call          = 6;
        c.argument      = 2;
        c.reference     = 17;
        c.referenceChar = 1;

        // A global is a field, set in the constructor; a string is held
        // once, however many places load it.
        c.integer       = { 43, -1, -1 };
        c.floating      = { 47, 3, 3 };
        c.wholeFloating = { 44, 0, 0 };
        c.vector        = { 70, 26, 26 };
        c.wholeVector   = { 61, 17, 17 };
        c.rotation      = { 111, 35, 35 };
        c.wholeRotation = { 99, 23, 23 };
        c.string         = { 45, -1, -1 };
        c.stringChar     = { 2, 0, 0 };
        c.stringHeld     = 2;
        c.stringHeldChar = 2;
        return c;
    }

    ALLSLCosts luau()
    {
        ALLSLCosts c;
        c.integerForFloat  = false;
        c.lengthAsNotEqual = false;
        c.listAsSum           = false;
        c.elementForList      = true;
        c.elementsForListMost = 2;

        // Luau's bitwise operators are calls into its bit32 library, which
        // a script pays for once, more than a few uses of ~ save.
        c.complementForNotMinusOne     = false;
        c.notComplementForMinusOne     = false;
        c.negateComplementForIncrement = false;
        c.complementNegateForDecrement = false;
        c.preForPost                   = false;
        // A key's default loads smaller than the empty string it is.
        c.incrementForAssign           = false;
        c.dropIntegerDefault           = false;
        c.dropFloatDefault             = false;
        c.dropKeyDefault               = true;
        c.dropVectorDefault            = false;
        c.dropRotationDefault          = false;
        c.dropIntegerGlobalDefault     = false;
        c.castForNegative              = false;
        c.emptyForNullKey              = true;
        c.comparisonForNot             = true;
        c.xorForNotEqual               = false;
        c.strictForInclusive           = false;
        c.bitOrForOr                   = false;
        // Nothing a place: only what the first place takes away, which
        // depends on what else the script calls.
        c.emptyForLength               = false;
        c.castForDump                  = false;
        c.castForDetail                = false;
        c.castForWholeFloat            = false;
        c.bitTestsMerged               = true;
        // Its bitwise operators are calls into bit32, its arithmetic one
        // instruction each.
        c.bitAndForAnd                 = false;
        c.plusForMinus                 = false;
        c.productForShift              = true;
        c.complementForMinusOneLess    = false;
        c.sumForDouble                 = true;
        c.productForQuotient           = false;
        c.remainderForOddTest          = true;
        c.differenceForNotEqual        = true;
        c.quotientForShift             = true;
        c.andForNestedIf               = false;
        c.bitAndForNestedTruths        = false;
        c.arithmeticForSelect          = false;
        c.doForKnownFirst              = false;

        c.local         = 4;
        c.localFrame    = 0;
        c.jump          = 92;
        c.function      = 37;
        c.functionChar  = 1;
        c.parameter     = 0;
        c.call          = 14;
        c.argument      = 4;
        c.reference     = 2;
        c.referenceChar = 0;

        // A function keeps each constant once, and loads it for less than
        // a global.
        c.integer       = { 24, -1, -4 };
        c.floating      = { 19, 3, -4 };
        c.wholeFloating = { 28, 3, -4 };
        c.vector        = { 36, 11, -4 };
        c.wholeVector   = { 19, 11, -4 };
        c.rotation      = { 76, 36, 22 };
        c.wholeRotation = { 103, 63, 22 };
        c.string         = { 22, -4, -4 };
        c.stringChar     = { 1, 0, 0 };
        c.stringHeld     = 3;
        c.stringHeldChar = 1;
        return c;
    }
}

S32 ALLSLCosts::inlined(const Inlining& f) const
{
    const S32 own    = function + functionChar * S32(f.name) + f.params * parameter;
    const S32 held   = f.strings * stringHeld + f.chars * stringHeldChar;
    const S32 body   = std::max(0, f.bytes - own - held);
    const S32 copy   = body + f.locals * local + f.jumps * jump;
    const S32 before = f.bytes + reference + referenceChar * S32(f.name) + f.calls * (call + f.params * argument);
    return f.calls * copy + held - before;
}

// static
const ALLSLCosts& ALLSLCosts::of(ALLSLOptimizer::Target target)
{
    static const ALLSLCosts LSO  = lso();
    static const ALLSLCosts MONO = mono();
    static const ALLSLCosts LUAU = luau();
    switch (target)
    {
        case ALLSLOptimizer::Target::LSO:
            return LSO;
        case ALLSLOptimizer::Target::Luau:
            return LUAU;
        default:
            return MONO;
    }
}
