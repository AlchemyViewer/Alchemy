/**
 * @file aldiffrangesame.h
 * @brief The words that mean the same in a pair of lines, by the ranges they are in.
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

#ifndef AL_ALDIFFRANGESAME_H
#define AL_ALDIFFRANGESAME_H

#include "altextdiff.h"

#include <vector>

// Which table of words that mean the same (ALDiffSame) a pair of lines is
// compared by: the narrowest range that holds both its lines and has a
// table of its own, that table joined with the whole comparison's; else
// the whole's. Made once for a comparison's ranges, each joined table made
// the first time a pair asks for it.
class ALDiffRangeSame
{
public:
    ALDiffRangeSame(const ALTextDiff::ranges_t& ranges, ALTextDiff::same_t whole);

    // For a line of the text given as the left and one of the right.
    const ALTextDiff::same_t& at(S32 left, S32 right);

private:
    const ALTextDiff::ranges_t&     mRanges;
    ALTextDiff::same_t              mWhole;
    // By each line of the left, the ranges with tables over it, the
    // narrowest first; and each range's table joined, as made.
    std::vector<std::vector<S32>>   mOver;
    std::vector<ALTextDiff::same_t> mJoined;
};

#endif // AL_ALDIFFRANGESAME_H
