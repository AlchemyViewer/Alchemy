/**
 * @file allinepairs.h
 * @brief Within a change, the lines taken out and put in that stand for each other.
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

#ifndef AL_ALLINEPAIRS_H
#define AL_ALLINEPAIRS_H

#include "altextdiff.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Within a change, the lines taken out and put in that stand for each
// other -- a line edited, beside what it became -- and those that stand
// alone: a line taken out in the middle of lines changed is not put
// beside the line after it, and nothing is paired with what is nothing
// like it.
namespace ALLinePairs
{
    // Each pair, as a place in the lines taken out and one in the lines put
    // in.
    typedef std::vector<std::pair<S32, S32>> pairs_t;
    // How alike a pair must be.
    constexpr F32 PAIR_LEAST = 0.5f;
    // How many pairs may be weighed, the lines taken out by those put in,
    // before each line is weighed against the one at its own place alone.
    constexpr S32 MOST_CELLS = 100000;

    // Of `gone`, lines of the left, and `made`, lines of the right, in
    // order, the pairs -- rising in both -- that are alike enough and alike
    // the most in all, the rest standing alone. A pair the options' anchors
    // keep beside each other is a pair however unlike.
    pairs_t pair(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<S32>& gone,
                 const std::vector<S32>& made, const ALTextDiff::Options& options = ALTextDiff::Options());
    // How alike two lines are: of the words both have -- not blanks --
    // the share in common (Dice's: twice those in common over all of
    // both), from nought to one; two lines of no words alike.
    F32 alike(std::string_view left, std::string_view right, const ALTextDiff::Options& options = ALTextDiff::Options());
}

#endif // AL_ALLINEPAIRS_H
