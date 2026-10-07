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

#include <functional>
#include <memory>
#include <optional>
#include <span>
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
// This is what is compared and how, and the ways in; the work is done
// beside it, a part a file: the ways lines are found (ALLineDiff), the
// words a line is cut into (ALDiffTokens), the words of two lines
// compared (ALWordDiff), and which lines of a change stand for each other
// (ALLinePairs).
//
class ALDiffSame;

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
    // Besides: blanks at a line's end alone let go of; blank lines let go
    // of -- each nothing, and a change of nothing else no change; comments
    // let go of, where a grammar says where they are (the options' lexer)
    // -- a line's comment not compared, and a line of nothing else as a
    // blank line is. Where the grammar says where strings are, a string's
    // blanks and case are its own whatever is let go of: "a  b" is not
    // "a b", nor "Hi" "hi".
    struct Likeness
    {
        bool ignoreWhitespace = false;
        bool ignoreCase       = false;
        bool ignoreTrailing   = false;
        bool ignoreBlankLines = false;
        bool ignoreComments   = false;

        bool any() const { return ignoreWhitespace || ignoreCase || ignoreTrailing || ignoreBlankLines || ignoreComments; }
        // Whether a line's regions change how it is told the same: where
        // comments are let go of, or blanks or case, which a string keeps.
        bool byRegions() const { return ignoreWhitespace || ignoreCase || ignoreComments; }
        bool operator==(const Likeness& other) const = default;
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
    // lines are rarest kept, and the rest found the same way (Histogram,
    // the default); the lines that come once in each kept, and the rest
    // found the same way (Patience); the fewest lines taken out and put
    // in, whatever they are (Minimal); or by the tokens of code, whatever
    // lines they are on (Structural, ALStructuralDiff).
    enum class Algorithm : U8
    {
        Histogram,
        Patience,
        Minimal,
        Structural
    };
    // Each by a name a setting can hold: "histogram", "patience",
    // "minimal", "structural"; a name of none, nothing.
    const char*              algorithmName(Algorithm algorithm);
    std::optional<Algorithm> algorithmFromName(std::string_view name);

    // What a stretch of a line is, as its words are cut: code, cut as a
    // language's tokens are; or text written to be read -- a string's, a
    // comment's -- cut as prose is.
    enum class Region : U8
    {
        Code,
        String,
        Comment
    };
    struct Piece
    {
        S32    begin  = 0;
        S32    end    = 0;
        Region region = Region::Code;

        bool operator==(const Piece& other) const = default;
    };
    // A line's stretches, in order, covering it; none, all of it code.
    typedef std::vector<Piece> regions_t;
    // A text's lines, the stretches of each, by a grammar: what a
    // comparison of code is given by whoever knows its language. Without
    // one, a line's words are cut by their bytes alone (prose, notecards).
    // What it answers stays its own until it is asked again.
    typedef std::function<const std::vector<regions_t>&(const std::vector<std::string>& lines)> lexer_t;

    // Words that mean the same in the two texts though written otherwise
    // (ALDiffSame): none, or a table made once and shared.
    typedef std::shared_ptr<const ALDiffSame> same_t;

    // How two texts are compared: by what way, what is let go of, where
    // they are known to line up, how their lines are cut into words, and
    // which words mean the same.
    struct Options
    {
        Algorithm algorithm = Algorithm::Histogram;
        Likeness  like;
        anchors_t anchors;
        lexer_t   lexer;
        same_t    same;
    };

    // The options by lines alone, as told the same: no anchors, no words
    // alike, a grammar only to say where comments and strings are where
    // that changes what is told the same (Likeness::byRegions), and lines
    // found as Histogram finds them where structure was asked for -- what a
    // merge and a unified diff compare by.
    Options linesOnly(const Options& options);

    // Two texts' lines' regions, by the options' lexer where they are
    // `needed` and it has one: asked of it in turn, the left then the
    // right, so both are of it until it is asked again -- it holds the last
    // two texts it read; and none of either where it answers other than a
    // line each.
    typedef std::pair<const std::vector<regions_t>*, const std::vector<regions_t>*> both_regions_t;
    both_regions_t lexed(const Options& options, const std::vector<std::string>& left, const std::vector<std::string>& right, bool needed);

    // The runs that make the left the right, compared as `options` says.
    std::vector<Run> lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options = Options());
    // As lines(), each line's regions given rather than asked of the
    // options' lexer, which is not asked: a stretch of two texts, its
    // regions cut from those the whole texts were read in -- read on its
    // own, a stretch is read from its grammar's first state, the lines
    // inside a block comment as code. Regions of other than every line of
    // both, none. Lines found as Histogram finds them where structure was
    // asked for.
    std::vector<Run> lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options,
                           std::span<const regions_t> left_regions, std::span<const regions_t> right_regions);
    // As lines(), found the way `algorithm` says whatever the options say:
    // the lines a structural comparison reads its changes from.
    std::vector<Run> linesBy(Algorithm algorithm, const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options);
    // A change: from a run that is not the same, the lines taken out and
    // those put in until one that is -- a parting of none ends it too --
    // each in order; and where that run is, or the end.
    size_t           changeAt(const std::vector<Run>& runs, size_t from, std::vector<S32>& gone, std::vector<S32>& made);
    // Stretches known to stand for each other -- an LSL statement and the
    // SLua lines written of it -- each the first and the last line of the
    // left and of the right, counted from nought; and the words that mean
    // the same within it, beside the whole comparison's, where it has its
    // own.
    struct Range
    {
        S32    leftFirst  = 0;
        S32    leftLast   = 0;
        S32    rightFirst = 0;
        S32    rightLast  = 0;
        same_t same;

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
    // Of anchors, those lines() keeps: within both texts, of so many lines
    // each, the most that are in order on both, in that order.
    anchors_t keptAnchors(const anchors_t& anchors, S32 left_lines, S32 right_lines);

    // A text's lines, for lines(), read as a document reads them
    // (ALLineBreaks): CRLF and a lone CR as LF, so that a line here is a
    // line of the editor the text is shown in.
    std::vector<std::string> split(std::string_view text);

    // Within one line changed into another, the stretches of each, as
    // [begin, end) in bytes, that are not the other's: by words, each
    // line's stretches cut as their regions say (ALDiffTokens), the fewest
    // changed, then cleaned up (ALWordDiff).
    typedef std::vector<std::pair<S32, S32>> spans_t;
    void words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out, const Options& options = Options(),
               const regions_t* left_regions = nullptr, const regions_t* right_regions = nullptr);
    // A line or a word as it is compared, told the same so; a line's
    // comments, by its regions, left out where they are let go of, and its
    // strings as they are.
    std::string likenessOf(std::string_view text, const Likeness& like, const regions_t* regions = nullptr);
    // Whether a line taken out or put in is no change, as lines are told
    // the same: blank, where blank lines are let go of; a comment and
    // blanks, where comments are. A change of nothing but such lines is
    // none.
    bool        ignorable(std::string_view line, const Likeness& like, const regions_t* regions = nullptr);
}

#endif // AL_ALTEXTDIFF_H
