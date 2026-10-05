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

// How one text becomes another, by lines: each stretch the two share whose
// lines are rarest in them kept, and what is either side of it found the
// same way (a histogram diff, as git's), so that a function's braces and
// blank lines, which are everywhere, stay with their own function; and
// where nothing shared is rare enough, the fewest lines taken out and put
// in (Myers, in space as much as the texts). Within a line changed into
// another, the fewest words. Pure: what a diff view shows, and what a test
// can hold it to.
//
// A text's lines are its own to split; a line holds no line break.
namespace ALTextDiff
{
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

    // How two lines, or two words, are told the same: as they are; or with
    // their blanks let go of -- trimmed, a run of them as one -- and a
    // word of blanks nothing; or their case. What is shown is the text as
    // it is either way.
    struct Likeness
    {
        bool ignoreWhitespace = false;
        bool ignoreCase       = false;

        bool any() const { return ignoreWhitespace || ignoreCase; }
    };

    // Lines known to stand for each other -- an LSL statement and the SLua
    // it was written as -- each a line of the left and one of the right,
    // counted from nought. Lined up whatever they say: each pair kept beside
    // each other, and the stretches between them compared on their own.
    // Only pairs in order on both sides can be kept: of those given, the
    // most that are, and none outside either text. A pair that differs is a
    // change of its own, the first lines of it the pair's; a Same run of no
    // lines goes before it, to part it from a change just before.
    typedef std::vector<std::pair<S32, S32>> anchors_t;

    // How the lines that stay are chosen: each stretch the two share whose
    // lines are rarest kept, and the rest found the same way (Histogram).
    enum class Algorithm : U8
    {
        Histogram
    };

    // How two texts are compared: by what way, what is let go of, and
    // where they are known to line up.
    struct Options
    {
        Algorithm algorithm = Algorithm::Histogram;
        Likeness  like;
        anchors_t anchors;
    };

    // The runs that make the left the right, compared as `options` says.
    std::vector<Run> lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options = Options());
    // Stretches known to stand for each other -- an LSL statement and the
    // SLua lines written of it -- each the first and the last line of the
    // left and of the right, counted from nought.
    struct Range
    {
        S32 leftFirst  = 0;
        S32 leftLast   = 0;
        S32 rightFirst = 0;
        S32 rightLast  = 0;

        bool operator==(const Range& other) const = default;
    };
    typedef std::vector<Range> ranges_t;
    // The anchors that line ranges up: each's first lines, and the lines
    // after each's last, so that a range starts beside its other and what
    // follows it starts level again. Ranges inside others -- a block and
    // the statements in it -- may ask one line to be beside two: an end
    // that would put a line inside a range beside one outside it is let
    // go of; of two on one line of the left, the one with the earlier line
    // of the right is kept, a block's head before the statement written
    // on its line; of two on one line of the right, the one with the later
    // line of the left, the node that wrote it before the one around it
    // that wrote nothing of its own.
    anchors_t anchorsOf(const ranges_t& ranges);

    // A text's lines, for lines().
    std::vector<std::string> split(std::string_view text);

    // Within one line changed into another, the stretches of each, as
    // [begin, end) in bytes, that are not the other's: by words --
    // identifiers and numbers, runs of blanks, each other character.
    typedef std::vector<std::pair<S32, S32>> spans_t;
    void words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out, const Options& options = Options());
    // A line or a word as it is compared, told the same so.
    std::string likenessOf(std::string_view text, const Likeness& like);
}

#endif // AL_ALTEXTDIFF_H
