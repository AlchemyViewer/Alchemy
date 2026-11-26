/**
 * @file alsettingcycle.h
 * @brief Flip or cycle a debug setting through values typed in chat.
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

#include "linden_common.h"

#include <string>
#include <string_view>
#include <vector>

class LLControlVariable;

namespace ALSettingCycle
{
enum class Result
{
    Written,      // the setting was flipped or moved to its next value
    BadValue,     // a value is not whole and of the setting's type; nothing written
    TooFewValues, // a setting that is not a boolean needs at least two values
    Unsupported,  // LLSD settings are not supported
};

// A boolean is flipped and the values are ignored. Any other setting takes at
// least two values: if it holds one of them it is set to the next, and if it
// holds the last or none of them it is set to the first. Vectors, colors,
// quaternions and rects are one comma-separated value each, such as 0,0,0.
// Every value is parsed before anything is written.
Result apply(LLControlVariable& control, const std::vector<std::string>& values);

// A whole decimal number: no prefix parse, and no inf, nan, hex or spaces.
bool parseNumber(std::string_view token, F64& out);

// The tail of /mapto when its last word is x|y or x|y|z. The region is
// everything before that word. False when the word has a '|' that is not
// two or three numbers, or when no region is left.
bool parseMapToCoordinates(const std::string& tail, std::string& region, F32& x, F32& y, bool& has_z, F32& z);
} // namespace ALSettingCycle
