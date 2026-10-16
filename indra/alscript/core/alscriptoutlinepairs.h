/**
 * @file alscriptoutlinepairs.h
 * @brief Two outlines' functions, events and states paired by what they are.
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

#include "alscriptsymbol.h"
#include "stdtypes.h"

#include <vector>

// Two outlines -- two versions of one script -- paired by what their
// symbols are: each function, event and state by its kind, its name and
// those of what holds it -- an event by its state, a function by the
// function it is in -- the first so called on one side with the first on
// the other, the second with the second. What a comparison lines the two
// texts up by (ALDiffModel::setPairs), so that a function is compared with
// what it was and not with another's lines alike. Nothing else is paired:
// a global is a line, which a comparison lines up well enough alone.
namespace ALScriptOutlinePairs
{
    // A symbol's lines on each side, from its first to its last.
    struct Pair
    {
        S32 leftFirst  = 0;
        S32 leftLast   = 0;
        S32 rightFirst = 0;
        S32 rightLast  = 0;

        bool operator==(const Pair& other) const = default;
    };

    // In the left's order.
    std::vector<Pair> pair(const std::vector<ALScriptOutlineEntry>& left, const std::vector<ALScriptOutlineEntry>& right);
}
