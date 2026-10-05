/**
 * @file allinediff.h
 * @brief Which lines of two texts stay and which change: the ways of finding it.
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

#ifndef AL_ALLINEDIFF_H
#define AL_ALLINEDIFF_H

#include "altextdiff.h"

#include <string>
#include <vector>

// The ways ALTextDiff finds which lines of two texts stay, over the texts'
// lines as ids (one for each line as compared): the runs each says, which
// together cover both.
namespace ALLineDiff
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;

    // How much walking a diff may do, in diagonals stepped along, before what
    // is left is answered as all of it taken out and all of it put in.
    constexpr S64 MOST_WORK = 50000000;

    // A stretch added after the last, joined to it where it is of the same
    // kind and carries straight on from it.
    void push(std::vector<Run>& out, Kind kind, S32 left, S32 right, S32 count);

    // The fewest taken out and put in (Myers), in space as much as the
    // texts; all of each where the walking runs past `work`.
    std::vector<Run> myers(const std::vector<S32>& a, const std::vector<S32>& b, S64 work = MOST_WORK);
    // Each stretch the two share whose lines are rarest kept, and the rest
    // found the same way (a histogram diff, as git's); the fewest changes
    // where nothing shared is rare enough.
    std::vector<Run> histogram(const std::vector<S32>& a, const std::vector<S32>& b);

    // Lines that come once in each text kept, the longest run of them in
    // order, and the stretches between found the same way (patience, as
    // git's); the fewest changes where none comes once in each.
    std::vector<Run> patience(const std::vector<S32>& a, const std::vector<S32>& b);
    // The fewest taken out and put in, and nothing else: Myers alone, with
    // ten times the walking.
    std::vector<Run> minimal(const std::vector<S32>& a, const std::vector<S32>& b);

    // Each change that could as well stand a line up or down put where it
    // reads as one thing, by the lines' text.
    void slide(std::vector<Run>& runs, const std::vector<S32>& a, const std::vector<S32>& b, const std::vector<std::string>& left,
               const std::vector<std::string>& right);
}

#endif // AL_ALLINEDIFF_H
