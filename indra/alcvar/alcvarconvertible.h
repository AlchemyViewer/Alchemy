/**
 * @file alcvarconvertible.h
 * @brief Which changes of a setting's type a stored value follows by itself:
 *        the conversion table, as the store converts and the generator
 *        reports.
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

#include "alcvartype.h"

enum class ALCVarConvertible : U8
{
    // Nothing converts it: the value takes its default unless the
    // declaration says how.
    No,
    // Every value converts and back again.
    Exact,
    // Values convert where they fit: clamped, rounded, parsed or with a
    // component dropped; one that does not takes the default.
    Lossy,
};

namespace ALCVarConvert
{
    // Whether a value of `from` becomes a value of `to` by itself. The store's
    // conversions (alcvarconvert.cpp) do exactly what this says they can.
    constexpr ALCVarConvertible convertible(ALCVarType from, ALCVarType to)
    {
        using T = ALCVarType;
        using C = ALCVarConvertible;
        if (from == to)
        {
            return C::Exact;
        }
        // Through the type's LLSD form, where the shape fits.
        if (from == T::LLSD || to == T::LLSD)
        {
            return C::Lossy;
        }
        // Formatted, or parsed.
        if (from == T::String || to == T::String)
        {
            return C::Lossy;
        }
        const bool fromNumber = ALCVarTypes::isNumber(from) || from == T::Bool;
        const bool toNumber   = ALCVarTypes::isNumber(to) || to == T::Bool;
        if (fromNumber && toNumber)
        {
            if (from == T::Bool)
            {
                return C::Exact;
            }
            if (to == T::Bool)
            {
                return C::Lossy;
            }
            if (ALCVarTypes::isInteger(from) && ALCVarTypes::isFloat(to))
            {
                return C::Exact;
            }
            if (from == T::F32 && to == T::F64)
            {
                return C::Exact;
            }
            // Integers clamped to the narrower type, floats rounded to the
            // nearest integer.
            return C::Lossy;
        }
        // A number in every component, alpha 1.
        if (ALCVarTypes::isNumber(from) &&
            (to == T::Vec2 || to == T::Vec3 || to == T::Vec3d || to == T::Color3 || to == T::Color4))
        {
            return C::Exact;
        }
        if ((from == T::Color3 && to == T::Color4) || (from == T::Vec3 && to == T::Vec3d))
        {
            return C::Exact;
        }
        if ((from == T::Color4 && to == T::Color3) || (from == T::Vec3d && to == T::Vec3))
        {
            return C::Lossy;
        }
        // A one-element list.
        if ((from == T::UUID && to == T::UUIDList) || (from == T::Vec2 && to == T::Vec2List))
        {
            return C::Exact;
        }
        // Each element parsed, or formatted.
        if ((from == T::UUIDList && to == T::StringList) || (from == T::StringList && to == T::UUIDList))
        {
            return from == T::UUIDList ? C::Exact : C::Lossy;
        }
        return C::No;
    }
}
