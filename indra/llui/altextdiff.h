/**
 * @file altextdiff.h
 * @brief How two texts differ: by lines, and within a changed line by words.
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

#ifndef AL_ALTEXTDIFF_H
#define AL_ALTEXTDIFF_H

#include "stdtypes.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// How one text becomes another: the fewest lines taken out and put in
// (Myers), the lines the two share at either end first set aside; and,
// within a line changed into another, the words that differ, by the same
// means. Pure: what a diff view shows, and what a test can hold it to.
//
// A text's lines are its own to split; a line holds no line break.
class ALTextDiff
{
public:
    enum class Kind : U8
    {
        Same,
        Removed,
        Added
    };

    // A stretch of lines, each counted from nought in its own text: the
    // same in both, `count` of them from `left` and from `right`; taken out
    // of the left, from `left`, where the right has reached `right`; or
    // put in on the right, from `right`, where the left has reached `left`.
    // In order, and together covering both texts.
    struct Run
    {
        Kind kind  = Kind::Same;
        S32  left  = 0;
        S32  right = 0;
        S32  count = 0;

        bool operator==(const Run& other) const
        {
            return kind == other.kind && left == other.left && right == other.right && count == other.count;
        }
    };

    // Past this many lines taken out and put in, what is between the lines
    // the two share at either end is answered as all of it taken out and
    // all of it put in: finding the fewest costs the square of this.
    static constexpr S32 MOST_CHANGES = 1000;

    static std::vector<Run> lines(const std::vector<std::string>& left, const std::vector<std::string>& right);

    // A text's lines, for lines().
    static std::vector<std::string> split(std::string_view text);

    // Within one line changed into another, the stretches of each, as
    // [begin, end) in bytes, that are not the other's: by words --
    // identifiers and numbers, runs of blanks, each other character.
    typedef std::vector<std::pair<S32, S32>> spans_t;
    static void words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out);
};

#endif // AL_ALTEXTDIFF_H
