/**
 * @file alworddiff.h
 * @brief Within a line changed into another, the words that changed.
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

#ifndef AL_ALWORDDIFF_H
#define AL_ALWORDDIFF_H

#include "altextdiff.h"

#include <string_view>

// Within a line changed into another, the words of each that are not the
// other's, as a person reads the change: the fewest words changed (Myers
// over ALDiffTokens' words), then cleaned up -- a match no longer than
// the changes on both sides of it, a lone bracket or dot or blank between
// two changes, folded into them, so a change reads as one mark and not as
// chaff -- and a word changed into one like it, a name with a letter more
// or another number at its end, marked by the characters that differ.
namespace ALWordDiff
{
    // As ALTextDiff::words.
    void diff(std::string_view left, std::string_view right, ALTextDiff::spans_t& left_out, ALTextDiff::spans_t& right_out,
              const ALTextDiff::Options& options, const ALTextDiff::regions_t* left_regions, const ALTextDiff::regions_t* right_regions);
}

#endif // AL_ALWORDDIFF_H
