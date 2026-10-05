/**
 * @file aldiffsplice.h
 * @brief Two texts compared again only where they changed.
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

#ifndef AL_ALDIFFSPLICE_H
#define AL_ALDIFFSPLICE_H

#include "altextdiff.h"

#include <string>
#include <vector>

// Two texts compared again after one or both changed in a stretch -- a
// live comparison's right as it is typed in -- by the runs they had: what
// lies between the last line the same before the change and the first
// after it, on both sides, compared again on its own and put in place of
// what the runs said of it; the runs before kept, and those after moved
// on by as many lines as each side gained or lost. A stretch so cut may
// be found otherwise than the whole compared again would find it, as a
// stretch between anchors is.
//
// The whole is compared again, rather, where the stretch is more than
// MOST_SHARE of the texts, an anchor holds a line in it and one outside
// it, or comments are let go of (which a lexer reads whole texts for).
// Compared by structure, the runs are the lines' (Histogram), which
// ALStructuralDiff::read then reads as tokens.
namespace ALDiffSplice
{
    // As a share of both texts' lines.
    constexpr F32 MOST_SHARE = 0.5f;

    // The runs of `left` and `right`, from those of what they were; false,
    // leaving them as they were, where the whole must be compared again.
    // The options' anchors are of the texts as they are.
    bool splice(std::vector<ALTextDiff::Run>& runs, const std::vector<std::string>& left_was, const std::vector<std::string>& left,
                const std::vector<std::string>& right_was, const std::vector<std::string>& right, const ALTextDiff::Options& options);

    // How many lines the last splice compared again, on both sides: what a
    // test holds its cost to.
    S32 lastCompared();
}

#endif // AL_ALDIFFSPLICE_H
