/**
 * @file aldiffmoves.h
 * @brief Blocks of lines moved from one place to another.
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

#ifndef AL_ALDIFFMOVES_H
#define AL_ALDIFFMOVES_H

#include "altextdiff.h"

#include <string>
#include <vector>

// Of the lines one text lost and the other gained, the blocks that are the
// same lines (as compared) -- a function cut from here and pasted there,
// which a diff by lines reads as all of it taken out and all of it put in.
// A block is the longest run of lines taken out, one after another, that
// is a run of lines put in, one after another, each line of it not yet in
// another; and it counts only where it holds LEAST_ALNUM letters and
// digits or more (git's), so that a brace and a blank line moved do not.
// Each line taken out or put in is in one block at most. A block moved and
// changed a little within is not one.
namespace ALDiffMoves
{
    // Lines `left` on of the left, taken out, that are lines `right` on of
    // the right, put in, `count` of each.
    struct Move
    {
        S32 left  = 0;
        S32 right = 0;
        S32 count = 0;

        bool operator==(const Move& other) const = default;
    };
    typedef std::vector<Move> moves_t;

    constexpr S32 LEAST_ALNUM = 20;
    // How many places a line put in may be tried at for a line taken out:
    // a brace is everywhere.
    constexpr S32 MOST_TRIED = 64;

    // The blocks moved, in order of their lines on the left, of the runs
    // between two texts (ALTextDiff::lines), lines told the same as the
    // options say.
    moves_t find(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& runs,
                 const ALTextDiff::Options& options = ALTextDiff::Options());
}

#endif // AL_ALDIFFMOVES_H
