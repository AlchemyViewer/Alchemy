/**
 * @file alplace.h
 * @brief Where a box goes beside something: under or over it, or beside it.
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

#include "llrect.h"

// Where a box goes beside something already on the screen or in a view: a
// popover under its control, a list under a word, a card under what it is
// about, the documentation beside a list. One rule, so each flips and
// keeps inside the same way. Every rect is in the bounds' coordinates.
struct ALPlace
{
    // Under the anchor, left edges lined up and `gap` clear of it; over it
    // where under would run out of the bounds and over would not. Across,
    // moved in to keep inside the bounds as far as it is narrower than them.
    static LLRect under(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap = 0);
    // The same the other way up: over it first, and under it only where
    // over would run out of the bounds and under would not.
    static LLRect over(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap = 0);
    // Beside the anchor, tops lined up: on its right where there is room,
    // else on its left; else under or over it, as under() puts it. Up and
    // down, moved in to keep inside the bounds, its top kept where it is
    // taller than they are.
    static LLRect beside(const LLRect& anchor, S32 width, S32 height, const LLRect& bounds, S32 gap = 0);
};
