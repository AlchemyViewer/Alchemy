/**
 * @file aldiffmodel.cpp
 * @brief Two texts compared, laid out as a comparison shows them.
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

#include "linden_common.h"

#include "aldiffmodel.h"

#include "aldiffedit.h"
#include "aldiffrangesame.h"
#include "allinebreaks.h"
#include "allinepairs.h"
#include "alstructuraldiff.h"

#include <algorithm>
#include <boost/container_hash/hash.hpp>

#include <iterator>
#include <limits>

namespace
{
    // The lines of a text, with LF, that was a text and these lines of it:
    // where the two first differ and how far they end alike, by their bytes,
    // and so the lines wholly before and wholly after, by the breaks there
    // -- those taken from the lines as they were rather than made again,
    // only the lines between cut from the text. What lies between of the
    // lines as they were is left where it was. A line the same beside the
    // bytes that differ may be counted among those between, which only
    // reads it again.
    std::vector<std::string> linesAgain(std::vector<std::string>& was, std::string_view was_text, std::string_view text, ALDiffEdit::Edges& edges)
    {
        const size_t most   = std::min(was_text.size(), text.size());
        const size_t before = static_cast<size_t>(std::mismatch(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(most), was_text.begin()).first - text.begin());
        const size_t after  = static_cast<size_t>(
            std::mismatch(text.rbegin(), text.rbegin() + static_cast<std::ptrdiff_t>(most - before), was_text.rbegin()).first - text.rbegin());
        const std::string_view ending = text.substr(text.size() - after);
        edges.head = static_cast<S32>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(before), '\n'));
        edges.tail = static_cast<S32>(std::count(ending.begin(), ending.end(), '\n'));
        // The lines between: from after the last break before, to the first
        // break of the lines after.
        const size_t from = edges.head > 0 ? text.rfind('\n', before - 1) + 1 : 0;
        const size_t to   = edges.tail > 0 ? text.size() - after + ending.find('\n') : text.size();
        std::vector<std::string> now;
        now.reserve(was.size() + 8);
        std::move(was.begin(), was.begin() + edges.head, std::back_inserter(now));
        for (const std::string_view line : ALLineBreaks::views(text.substr(from, to - from)))
        {
            now.emplace_back(line);
        }
        std::move(was.end() - edges.tail, was.end(), std::back_inserter(now));
        return now;
    }

    size_t hashOf(const ALTextDiff::regions_t& regions)
    {
        size_t hash = regions.size();
        for (const ALTextDiff::Piece& piece : regions)
        {
            boost::hash_combine(hash, piece.begin);
            boost::hash_combine(hash, piece.end);
            boost::hash_combine(hash, static_cast<U8>(piece.region));
        }
        return hash;
    }
}

// --- what is compared ---------------------------------------------------------

ALDiffModel::ALDiffModel()
:   mLeftLines(ALTextDiff::split("")),
    mRightLines(ALTextDiff::split(""))
{
    build();
}

void ALDiffModel::setTexts(std::string_view left, std::string_view right, const ALTextDiff::ranges_t& ranges)
{
    mNotes.clear();
    mMerge.reset();
    mLeftText   = ALLineBreaks::withLineFeeds(left);
    mRightText  = ALLineBreaks::withLineFeeds(right);
    mLeftLines  = ALTextDiff::split(mLeftText);
    mRightLines = ALTextDiff::split(mRightText);
    mRanges     = ranges;
    mPairs.clear();
    mMoveFinder.forget();
    build();
}

bool ALDiffModel::setPairs(ALTextDiff::ranges_t pairs)
{
    if (pairs == mPairs)
    {
        return false;
    }
    mPairs = std::move(pairs);
    if (!mRanges.empty())
    {
        return false;
    }
    build(foldsOpen());
    return true;
}

ALTextDiff::ranges_t ALDiffModel::carried(const ALTextDiff::ranges_t& ranges, bool left, S32 was, const LineMap& map)
{
    ALTextDiff::ranges_t out;
    out.reserve(ranges.size());
    for (ALTextDiff::Range range : ranges)
    {
        S32& first = left ? range.leftFirst : range.rightFirst;
        S32& last  = left ? range.leftLast : range.rightLast;
        if (first >= 0 && first < was)
        {
            first = map.line(first);
            last  = llmax(first, map.line(last));
            out.push_back(std::move(range));
        }
    }
    return out;
}

ALDiffModel::LineMap ALDiffModel::setRightText(std::string_view right)
{
    // Each line of the right as it was, where it now is: the lines the
    // two share at either end taken from the right as it was, of which what
    // lies between is still there to compare.
    std::string                     text = ALLineBreaks::withLineFeeds(right);
    ALDiffEdit::Edges               edges;
    std::vector<std::string>        now  = linesAgain(mRightLines, mRightText, text, edges);
    const std::vector<std::string>& was  = mRightLines;
    const LineMap                   map  = ALDiffSplice::lineMap(was, now, edges);
    // A range goes with its lines, changed or not: an edit of the SLua a
    // statement became still stands for the statement. Two come to one
    // line where one was taken out; lines() keeps those it can. So does a
    // pair: a function typed in is still the function.
    ALTextDiff::ranges_t ranges = carried(mRanges, false, static_cast<S32>(was.size()), map);
    mPairs                      = carried(mPairs, false, static_cast<S32>(was.size()), map);
    // The runs open, by the first line of the right each hides.
    const std::vector<S32> opened = openedLines(&map);
    mRightText                    = std::move(text);
    mRanges                       = std::move(ranges);
    resplice(false, std::move(now), edges);
    reopen(opened);
    return map;
}

void ALDiffModel::setLeftText(std::string_view left)
{
    // What was said of the left, and a merge with it, were the other's.
    mNotes.clear();
    mMerge.reset();
    std::string              text = ALLineBreaks::withLineFeeds(left);
    ALDiffEdit::Edges        edges;
    std::vector<std::string> lines = linesAgain(mLeftLines, mLeftText, text, edges);
    mLeftText                      = std::move(text);
    mPairs = carried(mPairs, true, static_cast<S32>(mLeftLines.size()), ALDiffSplice::lineMap(mLeftLines, lines, edges));
    if (!mRanges.empty())
    {
        // Nor what stood for what: lined up otherwise, compared afresh.
        mRanges.clear();
        mMoveFinder.edited(true, edges.head, static_cast<S32>(mLeftLines.size()) - edges.tail, static_cast<S32>(lines.size()) - edges.tail);
        mLeftLines = std::move(lines);
        build();
        return;
    }
    resplice(true, std::move(lines), edges);
}

void ALDiffModel::resplice(bool given_left, std::vector<std::string> lines, const ALDiffEdit::Edges& edges)
{
    // Compared again where it changed (ALDiffSplice), else all of it: the
    // side changed by where it differs, the other the same throughout.
    std::vector<std::string>& side       = given_left ? mLeftLines : mRightLines;
    const S32                 was        = static_cast<S32>(side.size());
    const S32                 moved      = static_cast<S32>(lines.size()) - was;
    const bool                shown_left = given_left != mSwapped;
    const size_t              shown      = shown_left ? 0 : 1;
    // Where a grammar says how lines read, the regions the lines of the
    // changes after the edit were read in, before it reads the side again.
    const bool                           regioned = static_cast<bool>(mOptions.lexer);
    const bool                           had      = regioned && (shown ? shownRegions().second : shownRegions().first);
    const std::vector<std::pair<S32, size_t>> read_before = had ? readFrom(shown, was - edges.tail) : std::vector<std::pair<S32, size_t>>();
    // The moves' ids of the lines edited let go of, those after moved along.
    mMoveFinder.edited(given_left, edges.head, was - edges.tail, static_cast<S32>(lines.size()) - edges.tail);
    side = std::move(lines);
    mRegions.reset();
    if (mMerge && !given_left)
    {
        mMerge->setOurs(mRightLines);
    }
    const ALTextDiff::Options options = shownOptions();
    Relayout                  again;
    again.runs                           = mRuns;
    (shown_left ? again.left : again.right) = moved;
    again.side                           = shown_left ? 0 : 1;
    again.head                           = edges.head;
    again.tail                           = edges.tail;
    again.lines                          = was;
    const std::vector<std::string>& other = given_left ? mRightLines : mLeftLines;
    const ALDiffSplice::Side        changed{ side, was, edges };
    const ALDiffSplice::Side        same{ other, static_cast<S32>(other.size()), ALDiffEdit::Edges{ static_cast<S32>(other.size()), 0 } };
    const bool spliced = shown_left ? ALDiffSplice::splice(mRuns, changed, same, options) : ALDiffSplice::splice(mRuns, same, changed, options);
    if (!spliced)
    {
        build();
        return;
    }
    // The edit reaches as far as the lines of changes after it now read
    // otherwise -- a block comment opened or closed -- whose words and
    // tokens are cut otherwise: laid out and read again to the last of
    // them, or to the end where the grammar's regions are missing before
    // or after.
    if (regioned)
    {
        const line_regions_t now   = shown ? shownRegions().second : shownRegions().first;
        S32                  reach = had && now ? -1 : static_cast<S32>(side.size());
        for (const auto& [line, hash] : read_before)
        {
            if (reach < static_cast<S32>(side.size()) && hashOf((*now)[static_cast<size_t>(line + moved)]) != hash)
            {
                reach = line + moved + 1;
            }
        }
        if (reach >= 0)
        {
            again.tail = std::min(again.tail, static_cast<S32>(side.size()) - reach);
        }
    }
    if (options.algorithm == ALTextDiff::Algorithm::Structural)
    {
        // Its changes read as tokens again, the lines' runs as spliced:
        // those outside the lines edited, as they were, kept.
        ALStructuralDiff::Edited edited;
        edited.edges[shown]     = ALDiffEdit::Edges{ again.head, again.tail };
        edited.was[shown]       = was;
        edited.edges[1 - shown] = ALDiffEdit::Edges{ static_cast<S32>(other.size()), 0 };
        edited.was[1 - shown]   = static_cast<S32>(other.size());
        readTokens(&again.runs, &edited);
    }
    layout(options, {}, &again);
}

std::vector<std::pair<S32, size_t>> ALDiffModel::readFrom(size_t side, S32 from) const
{
    std::vector<std::pair<S32, size_t>> out;
    const line_regions_t                regions = side ? shownRegions().second : shownRegions().first;
    const Kind                          own     = side ? Kind::Added : Kind::Removed;
    for (const ALTextDiff::Run& run : mRuns)
    {
        const S32 start = side ? run.right : run.left;
        for (S32 line = std::max(start, from); run.kind == own && regions && line < start + run.count; ++line)
        {
            out.emplace_back(line, hashOf((*regions)[static_cast<size_t>(line)]));
        }
    }
    return out;
}

std::vector<S32> ALDiffModel::openedLines(const LineMap* map) const
{
    std::vector<S32>        opened;
    const std::vector<S32>& rows = rightRows(Layout::Sides);
    for (const Fold& fold : mFolds)
    {
        const S32 first = fold.first[index(Layout::Sides)];
        if (fold.open && first < static_cast<S32>(rows.size()) && rows[static_cast<size_t>(first)] >= 0)
        {
            const S32 line = rows[static_cast<size_t>(first)];
            opened.push_back(map ? map->line(line) : line);
        }
    }
    // In order already, as the folds are; a line taken back may not be.
    if (!std::is_sorted(opened.begin(), opened.end()))
    {
        std::sort(opened.begin(), opened.end());
    }
    return opened;
}

void ALDiffModel::reopen(const std::vector<S32>& opened)
{
    if (opened.empty())
    {
        return;
    }
    const std::vector<S32>& rows = rightRows(Layout::Sides);
    for (Fold& fold : mFolds)
    {
        const S32 first = fold.first[index(Layout::Sides)];
        const S32 line  = first < static_cast<S32>(rows.size()) ? rows[static_cast<size_t>(first)] : -1;
        if (line >= 0 && std::binary_search(opened.begin(), opened.end(), line))
        {
            fold.open = true;
        }
    }
}

void ALDiffModel::setSwapped(bool swapped)
{
    if (mSwapped != swapped)
    {
        mSwapped = swapped;
        build(foldsOpen());
    }
}

void ALDiffModel::setLikeness(const ALTextDiff::Likeness& like)
{
    mOptions.like = like;
    if (mMerge)
    {
        mMerge->setOptions(mOptions);
    }
    // The runs are others now, but one hiding the same first line of the
    // right as one the reader opened is the same to the reader: open.
    const std::vector<S32> opened = openedLines();
    build();
    reopen(opened);
}

void ALDiffModel::setSame(ALTextDiff::same_t same)
{
    mOptions.same = std::move(same);
    build(foldsOpen());
}

void ALDiffModel::setAlgorithm(ALTextDiff::Algorithm algorithm)
{
    if (mOptions.algorithm != algorithm)
    {
        mOptions.algorithm            = algorithm;
        const std::vector<S32> opened = openedLines();
        build();
        reopen(opened);
    }
}

void ALDiffModel::setLexer(ALTextDiff::lexer_t lexer)
{
    mOptions.lexer = std::move(lexer);
    if (!mOptions.lexer && mOptions.like.ignoreComments)
    {
        // Lines told the same otherwise now: as setLikeness.
        ALTextDiff::Likeness like = mOptions.like;
        like.ignoreComments       = false;
        setLikeness(like);
        return;
    }
    build(foldsOpen());
}

// --- made --------------------------------------------------------------------------

S32 ALDiffModel::add(Column column, const std::string& text, S32 number, Kind kind, char sign)
{
    ColumnData& c = of(column);
    if (column == Column::Inline)
    {
        mInlineText += text;
        mInlineText += '\n';
    }
    Line& line   = c.lines.emplace_back();
    line.kind    = kind;
    line.sign    = sign;
    line.number  = number;
    line.padding = c.pending;
    c.pending    = 0;
    c.rowOf.push_back(static_cast<S32>(c.lineOf.size()));
    c.lineOf.push_back(static_cast<S32>(c.lines.size()) - 1);
    return static_cast<S32>(c.lineOf.size()) - 1;
}

S32 ALDiffModel::pad(Column column)
{
    ++of(column).pending;
    return none(column);
}

S32 ALDiffModel::none(Column column)
{
    ColumnData& c = of(column);
    c.lineOf.push_back(-1);
    return static_cast<S32>(c.lineOf.size()) - 1;
}

ALTextDiff::Options ALDiffModel::shownOptions() const
{
    // The pairs that line the texts up, as they are shown: the ranges'
    // anchors, swapped where the texts are.
    ALTextDiff::Options options = mOptions;
    options.anchors             = ALTextDiff::anchorsOf(mRanges);
    if (mRanges.empty())
    {
        // Each pair's first lines and its last, which close what it is.
        for (const ALTextDiff::Range& pair : mPairs)
        {
            options.anchors.emplace_back(pair.leftFirst, pair.rightFirst);
            options.anchors.emplace_back(pair.leftLast, pair.rightLast);
        }
    }
    if (mSwapped)
    {
        for (auto& [from, to] : options.anchors)
        {
            std::swap(from, to);
        }
    }
    return options;
}

std::pair<ALDiffModel::line_regions_t, ALDiffModel::line_regions_t> ALDiffModel::shownRegions() const
{
    if (mRegions)
    {
        return *mRegions;
    }
    mRegions.emplace(nullptr, nullptr);
    if (mOptions.lexer)
    {
        // Read in turn: the lexer holds the last two it read.
        const line_regions_t left  = &mOptions.lexer(shownLeft());
        const line_regions_t right = &mOptions.lexer(shownRight());
        if (left->size() == shownLeft().size() && right->size() == shownRight().size())
        {
            mRegions.emplace(left, right);
        }
    }
    return *mRegions;
}

void ALDiffModel::build(const std::vector<bool>& open)
{
    mRegions.reset();
    const std::vector<std::string>& left    = shownLeft();
    const std::vector<std::string>& right   = shownRight();
    const ALTextDiff::Options       options = shownOptions();
    for (size_t side = 0; side < 2; ++side)
    {
        mMarks[side].clear();
        mByTokens[side].clear();
    }
    mFellBack = false;
    if (options.algorithm != ALTextDiff::Algorithm::Structural)
    {
        mRuns = ALTextDiff::lines(left, right, options);
        layout(options, open);
        return;
    }
    // By structure: the lines' runs, then their changes read as tokens.
    ALTextDiff::Options by_lines = options;
    by_lines.algorithm           = ALTextDiff::Algorithm::Histogram;
    mRuns                        = ALTextDiff::lines(left, right, by_lines);
    readTokens();
    layout(options, open);
}

void ALDiffModel::readTokens(const std::vector<ALTextDiff::Run>* was, const ALStructuralDiff::Edited* edited)
{
    const auto [left_regions, right_regions] = shownRegions();
    // After an edit, what was read of the texts as they were standing --
    // none of it too large -- only the changes that are not as they were.
    if (was && edited && mKeepsLayout && !mFellBack && static_cast<S32>(mMarks[0].size()) == edited->was[0] && static_cast<S32>(mMarks[1].size()) == edited->was[1] &&
        mByTokens[0].size() == mMarks[0].size() && mByTokens[1].size() == mMarks[1].size())
    {
        ALStructuralDiff::Result read;
        read.leftMarks     = std::move(mMarks[0]);
        read.rightMarks    = std::move(mMarks[1]);
        read.leftByTokens  = std::move(mByTokens[0]);
        read.rightByTokens = std::move(mByTokens[1]);
        ALStructuralDiff::readAgain(shownLeft(), shownRight(), *was, mRuns, *edited, mOptions, left_regions, right_regions, read);
        mMarks[0]    = std::move(read.leftMarks);
        mMarks[1]    = std::move(read.rightMarks);
        mByTokens[0] = std::move(read.leftByTokens);
        mByTokens[1] = std::move(read.rightByTokens);
        mFellBack    = read.tooLarge;
        return;
    }
    ALStructuralDiff::Result by_tokens       = ALStructuralDiff::read(shownLeft(), shownRight(), std::move(mRuns), mOptions, left_regions, right_regions);
    mRuns        = std::move(by_tokens.runs);
    mFellBack    = by_tokens.tooLarge;
    mMarks[0]    = std::move(by_tokens.leftMarks);
    mMarks[1]    = std::move(by_tokens.rightMarks);
    mByTokens[0] = std::move(by_tokens.leftByTokens);
    mByTokens[1] = std::move(by_tokens.rightByTokens);
}

void ALDiffModel::layout(const ALTextDiff::Options& options, const std::vector<bool>& open, const Relayout* again)
{
    ++mLayouts;
    // What is shown on the left and on the right: the texts as given, or
    // swapped, and their runs.
    const std::vector<std::string>&     left    = shownLeft();
    const std::vector<std::string>&     right   = shownRight();
    const std::vector<ALTextDiff::Run>& runs    = mRuns;
    // Each text's lines' regions, where a grammar cuts their words.
    const auto [left_regions, right_regions] = shownRegions();
    const auto regionsOf = [](line_regions_t regions, S32 line) {
        return regions ? &(*regions)[static_cast<size_t>(line)] : nullptr;
    };
    // The words that mean the same in a pair, by the ranges its lines are
    // in (of the texts as given).
    ALDiffRangeSame     same(mRanges, mOptions.same);
    // The anchors by their lines of the left, of which a change's pairing
    // is told only those within it: a converted script has one or two a
    // line, which ALLinePairs would look through for every pair it weighs.
    ALTextDiff::anchors_t by_left = options.anchors;
    if (!std::is_sorted(by_left.begin(), by_left.end()))
    {
        std::sort(by_left.begin(), by_left.end());
    }
    ALTextDiff::Options pairing = options;
    const auto          pairingOf = [&](const std::vector<S32>& gone_lines) -> const ALTextDiff::Options& {
        pairing.anchors.clear();
        if (!gone_lines.empty())
        {
            const auto from = std::lower_bound(by_left.begin(), by_left.end(), std::make_pair(gone_lines.front(), std::numeric_limits<S32>::min()));
            const auto to   = std::upper_bound(from, by_left.end(), std::make_pair(gone_lines.back(), std::numeric_limits<S32>::max()));
            pairing.anchors.assign(from, to);
        }
        return pairing;
    };
    ALTextDiff::Options with      = mOptions;
    const auto          optionsOf = [&](S32 shown_left, S32 shown_right) -> const ALTextDiff::Options& {
        with.same = mSwapped ? same.at(shown_right, shown_left) : same.at(shown_left, shown_right);
        return with;
    };
    // The blocks moved, and which each line of either side is in.
    if (!mKeepsLayout)
    {
        mMoveFinder.forget();
    }
    const ALDiffMoves::moves_t moves = mMoveFinder.find(mLeftLines, mRightLines, runs, options, mSwapped);
    std::vector<S32>           left_move(left.size(), -1);
    std::vector<S32>           right_move(right.size(), -1);
    for (size_t n = 0; n < moves.size(); ++n)
    {
        for (S32 k = 0; k < moves[n].count; ++k)
        {
            left_move[static_cast<size_t>(moves[n].left + k)]   = static_cast<S32>(n);
            right_move[static_cast<size_t>(moves[n].right + k)] = static_cast<S32>(n);
        }
    }
    // Which changes are none, as lines are told the same: each of their
    // lines blank, or a comment, where those are let go of. By each of
    // their runs.
    const ALTextDiff::Likeness& like = mOptions.like;
    std::vector<bool>           ignored(runs.size(), false);
    // By tokens, a change whose lines have none not kept is formatting
    // alone: no change either. A line's change read as tokens, and its
    // tokens not kept.
    const bool by_tokens = !mByTokens[0].empty() || !mByTokens[1].empty();
    const auto tokened   = [this](bool out, S32 line) {
        const std::vector<bool>& flags = mByTokens[out ? 0 : 1];
        return line >= 0 && line < static_cast<S32>(flags.size()) && flags[static_cast<size_t>(line)];
    };
    const auto marksOf = [this](bool out, S32 line) -> const ALTextDiff::spans_t& {
        static const ALTextDiff::spans_t none;
        const std::vector<ALTextDiff::spans_t>& marks = mMarks[out ? 0 : 1];
        return line >= 0 && line < static_cast<S32>(marks.size()) ? marks[static_cast<size_t>(line)] : none;
    };
    if (like.ignoreBlankLines || like.ignoreComments || by_tokens)
    {
        for (size_t i = 0; i < runs.size();)
        {
            if (runs[i].kind == Kind::Same)
            {
                ++i;
                continue;
            }
            bool   none = true;
            size_t j    = i;
            for (; j < runs.size() && runs[j].kind != Kind::Same; ++j)
            {
                const bool out = runs[j].kind == Kind::Removed;
                for (S32 n = 0; n < runs[j].count && none; ++n)
                {
                    const S32 line = (out ? runs[j].left : runs[j].right) + n;
                    none = (tokened(out, line) && marksOf(out, line).empty()) ||
                           ALTextDiff::ignorable(out ? left[static_cast<size_t>(line)] : right[static_cast<size_t>(line)], like,
                                                 regionsOf(out ? left_regions : right_regions, line));
                }
            }
            for (; i < j; ++i)
            {
                ignored[i] = none;
            }
        }
    }
    // The last run that is a change: the runs the same after it are at
    // the text's end. Where none is, nothing is folded.
    size_t last_change = runs.size();
    for (size_t i = runs.size(); i-- > 0;)
    {
        if (runs[i].kind != Kind::Same && !ignored[i])
        {
            last_change = i;
            break;
        }
    }
    // What of the layout before stands, after a splice; and where it does,
    // the groups after the change set aside to be moved along, the rest
    // after the groups kept let go of. Else all of it made again.
    const std::optional<Reuse> reuse = again && mKeepsLayout ? reusable(*again, moves, last_change) : std::nullopt;
    std::vector<Fold>          folds;
    const Mark                 kept  = reuse ? mGroups[reuse->kept] : Mark();
    const Mark                 moved = reuse ? mGroups[reuse->moved] : Mark();
    std::vector<Line>          after_lines[3];
    std::vector<S32>           after_line_of[3];
    std::vector<S32>           after_row_of[3];
    S32                        end_pending[3] = { 0, 0, 0 };
    std::vector<S32>           after_inline[2];
    std::vector<Change>        after_changes;
    std::vector<Fold>          after_folds;
    std::vector<Mark>          after_marks;
    std::string                after_text;
    if (reuse)
    {
        for (size_t c = 0; c < 3; ++c)
        {
            ColumnData&  column = mColumns[c];
            const size_t rows   = static_cast<size_t>(moved.rows[c == 2 ? 1 : 0]);
            after_lines[c].assign(std::make_move_iterator(column.lines.begin() + moved.lines[c]), std::make_move_iterator(column.lines.end()));
            after_line_of[c].assign(column.lineOf.begin() + static_cast<std::ptrdiff_t>(rows), column.lineOf.end());
            after_row_of[c].assign(column.rowOf.begin() + moved.lines[c], column.rowOf.end());
            end_pending[c] = column.endPadding;
            column.lines.resize(static_cast<size_t>(kept.lines[c]));
            column.lineOf.resize(static_cast<size_t>(kept.rows[c == 2 ? 1 : 0]));
            column.rowOf.resize(static_cast<size_t>(kept.lines[c]));
            column.pending    = kept.pending[c];
            column.endPadding = 0;
        }
        for (size_t side = 0; side < 2; ++side)
        {
            after_inline[side].assign(mInlineRows[side].begin() + moved.rows[1], mInlineRows[side].end());
            mInlineRows[side].resize(static_cast<size_t>(kept.rows[1]));
        }
        after_text = mInlineText.substr(moved.text);
        mInlineText.resize(kept.text);
        after_changes.assign(mChanges.begin() + static_cast<std::ptrdiff_t>(moved.changes), mChanges.end());
        mChanges.resize(kept.changes);
        after_folds.assign(mFolds.begin() + static_cast<std::ptrdiff_t>(moved.folds), mFolds.end());
        folds.assign(mFolds.begin(), mFolds.begin() + static_cast<std::ptrdiff_t>(kept.folds));
        after_marks.assign(mGroups.begin() + static_cast<std::ptrdiff_t>(reuse->moved), mGroups.end());
        mGroups.resize(reuse->kept);
    }
    else
    {
        for (ColumnData& c : mColumns)
        {
            c.lines.clear();
            c.lineOf.clear();
            c.rowOf.clear();
            c.pending    = 0;
            c.endPadding = 0;
        }
        of(Column::Left).lines.reserve(left.size());
        of(Column::Right).lines.reserve(right.size());
        of(Column::Inline).lines.reserve(right.size());
        mInlineText.clear();
        mInlineText.reserve((mSwapped ? mLeftText : mRightText).size());
        mChanges.clear();
        mInlineRows[0].clear();
        mInlineRows[1].clear();
        mGroups.clear();
        mMoves.assign(moves.size(), Move());
    }
    // Where each move's inline lines were: kept, moved along, or laid out
    // again below.
    std::vector<std::pair<bool, bool>> moved_ends(reuse ? mMoves.size() : 0);
    for (size_t n = 0; n < moved_ends.size(); ++n)
    {
        moved_ends[n] = { mMoves[n].inlineLeft >= moved.lines[2], mMoves[n].inlineRight >= moved.lines[2] };
    }
    for (size_t n = 0; n < moves.size(); ++n)
    {
        mMoves[n].lines = moves[n];
    }
    // Where each group begins to be laid out.
    const auto stateNow = [&](size_t run) {
        Mark here;
        here.run     = run;
        here.rows[0] = rowCount(Layout::Sides);
        here.rows[1] = rowCount(Layout::Inline);
        for (size_t c = 0; c < 3; ++c)
        {
            here.lines[c]   = static_cast<S32>(mColumns[c].lines.size());
            here.pending[c] = mColumns[c].pending;
        }
        here.changes = mChanges.size();
        here.folds   = folds.size();
        here.text    = mInlineText.size();
        return here;
    };
    const auto mark = [&](size_t run) { mGroups.push_back(stateNow(run)); };
    for (size_t i = reuse ? reuse->from : 0; i < (reuse ? reuse->to : runs.size());)
    {
        mark(i);
        const ALTextDiff::Run& run = runs[i];
        if (run.kind == Kind::Same)
        {
            const auto same = [&](S32 n) {
                const S32 l = run.left + n;
                const S32 r = run.right + n;
                add(Column::Left, left[static_cast<size_t>(l)], l + 1, Kind::Same);
                add(Column::Right, right[static_cast<size_t>(r)], r + 1, Kind::Same);
                add(Column::Inline, right[static_cast<size_t>(r)], r + 1, Kind::Same);
                mInlineRows[0].push_back(l);
                mInlineRows[1].push_back(r);
            };
            // Context kept beside a change, none at either end; what is
            // left folded away, where it is enough, and after it a row of
            // its own that stands for it while it is.
            const S32 before = rowCount(Layout::Sides) == 0 ? 0 : FOLD_CONTEXT;
            const S32 after  = i > last_change ? 0 : FOLD_CONTEXT;
            const S32 hidden = last_change == runs.size() ? 0 : run.count - before - after;
            S32       n      = 0;
            if (hidden >= FOLD_LEAST)
            {
                for (; n < before; ++n)
                {
                    same(n);
                }
                Fold fold;
                fold.first[index(Layout::Sides)]  = rowCount(Layout::Sides);
                fold.first[index(Layout::Inline)] = rowCount(Layout::Inline);
                fold.count                        = hidden;
                fold.open                         = !mFoldSame;
                for (; n < before + hidden; ++n)
                {
                    same(n);
                }
                none(Column::Left);
                none(Column::Right);
                none(Column::Inline);
                mInlineRows[0].push_back(-1);
                mInlineRows[1].push_back(-1);
                folds.push_back(fold);
            }
            for (; n < run.count; ++n)
            {
                same(n);
            }
            ++i;
            continue;
        }
        // The lines taken out and put in between two the same, and where
        // they start on each side, whichever they begin with.
        const bool       told_same = ignored[i];
        Change           change;
        std::vector<S32> gone;
        std::vector<S32> made;
        change.lines.leftFirst  = runs[i].left;
        change.lines.rightFirst = runs[i].right;
        i                       = ALTextDiff::changeAt(runs, i, gone, made);
        if (told_same)
        {
            // No change: its lines as the same are shown, side by side
            // beside each other as they fall, the rest beside nothing;
            // inline, the right's alone.
            for (size_t n = 0; n < std::max(gone.size(), made.size()); ++n)
            {
                if (n < gone.size())
                {
                    add(Column::Left, left[static_cast<size_t>(gone[n])], gone[n] + 1, Kind::Same);
                }
                else
                {
                    pad(Column::Left);
                }
                if (n < made.size())
                {
                    add(Column::Right, right[static_cast<size_t>(made[n])], made[n] + 1, Kind::Same);
                }
                else
                {
                    pad(Column::Right);
                }
            }
            for (const S32 line : made)
            {
                add(Column::Inline, right[static_cast<size_t>(line)], line + 1, Kind::Same);
                mInlineRows[0].push_back(-1);
                mInlineRows[1].push_back(line);
            }
            continue;
        }
        // A change: side by side, the first taken out beside the first put
        // in.
        change.first[index(Layout::Sides)]  = rowCount(Layout::Sides);
        change.first[index(Layout::Inline)] = rowCount(Layout::Inline);
        change.lines.leftCount              = static_cast<S32>(gone.size());
        change.lines.rightCount             = static_cast<S32>(made.size());
        if (mSwapped)
        {
            std::swap(change.lines.leftFirst, change.lines.rightFirst);
            std::swap(change.lines.leftCount, change.lines.rightCount);
        }
        // Side by side: the lines that stand for each other (ALTextDiff's
        // pairs) changed, beside each other, their words that differ found
        // once for both ways of showing; the rest taken out or put in alone,
        // beside a row of nothing, what was taken out before what was put
        // in between two pairs.
        // Lines of a block moved stand alone: paired, of the rest.
        std::vector<S32> gone_free;
        std::vector<S32> made_free;
        std::vector<S32> gone_at;
        std::vector<S32> made_at;
        for (size_t n = 0; n < gone.size(); ++n)
        {
            if (left_move[static_cast<size_t>(gone[n])] < 0)
            {
                gone_free.push_back(gone[n]);
                gone_at.push_back(static_cast<S32>(n));
            }
        }
        for (size_t n = 0; n < made.size(); ++n)
        {
            if (right_move[static_cast<size_t>(made[n])] < 0)
            {
                made_free.push_back(made[n]);
                made_at.push_back(static_cast<S32>(n));
            }
        }
        ALLinePairs::pairs_t pairs = ALLinePairs::pair(left, right, gone_free, made_free, pairingOf(gone_free), left_regions, right_regions);
        for (auto& [g, d] : pairs)
        {
            g = gone_at[static_cast<size_t>(g)];
            d = made_at[static_cast<size_t>(d)];
        }
        // A line taken out or put in alone, signed as its own or as moved;
        // by tokens, marked where its tokens were not kept.
        const auto alone = [&](Column column, bool out, S32 line, S32 number) {
            const S32 move = (out ? left_move : right_move)[static_cast<size_t>(line)];
            add(column, (out ? left : right)[static_cast<size_t>(line)], number, out ? Kind::Removed : Kind::Added, move >= 0 ? '>' : out ? '-' : '+');
            Line& one = of(column).lines.back();
            one.move  = move;
            if (move < 0 && tokened(out, line))
            {
                one.words = marksOf(out, line);
            }
        };
        std::vector<std::pair<ALTextDiff::spans_t, ALTextDiff::spans_t>> paired;
        size_t                                                           g = 0;
        size_t                                                           d = 0;
        for (size_t p = 0; p <= pairs.size(); ++p)
        {
            const size_t to_gone = p < pairs.size() ? static_cast<size_t>(pairs[p].first) : gone.size();
            const size_t to_made = p < pairs.size() ? static_cast<size_t>(pairs[p].second) : made.size();
            for (; g < to_gone; ++g)
            {
                alone(Column::Left, true, gone[g], gone[g] + 1);
                pad(Column::Right);
            }
            for (; d < to_made; ++d)
            {
                pad(Column::Left);
                alone(Column::Right, false, made[d], made[d] + 1);
            }
            if (p == pairs.size())
            {
                break;
            }
            add(Column::Left, left[static_cast<size_t>(gone[g])], gone[g] + 1, Kind::Removed, '~');
            add(Column::Right, right[static_cast<size_t>(made[d])], made[d] + 1, Kind::Added, '~');
            auto& [lspans, rspans] = paired.emplace_back();
            if (tokened(true, gone[g]) && tokened(false, made[d]))
            {
                lspans = marksOf(true, gone[g]);
                rspans = marksOf(false, made[d]);
            }
            else
            {
                ALTextDiff::words(left[static_cast<size_t>(gone[g])], right[static_cast<size_t>(made[d])], lspans, rspans, optionsOf(gone[g], made[d]),
                                  regionsOf(left_regions, gone[g]), regionsOf(right_regions, made[d]));
            }
            of(Column::Left).lines.back().words  = lspans;
            of(Column::Right).lines.back().words = rspans;
            ++g;
            ++d;
        }
        // Inline: what was taken out, unnumbered, above what was put in;
        // the inline line a block moved starts on, at either end.
        const auto inlined = [&](bool out, const std::vector<S32>& lines) {
            for (const S32 line : lines)
            {
                if (const S32 move = (out ? left_move : right_move)[static_cast<size_t>(line)]; move >= 0)
                {
                    Move& block = mMoves[static_cast<size_t>(move)];
                    if ((out ? block.lines.left : block.lines.right) == line)
                    {
                        (out ? block.inlineLeft : block.inlineRight) = lineCount(Column::Inline);
                    }
                }
                alone(Column::Inline, out, line, out ? 0 : line + 1);
                mInlineRows[0].push_back(out ? line : -1);
                mInlineRows[1].push_back(out ? -1 : line);
            }
        };
        const S32 first_out = lineCount(Column::Inline);
        inlined(true, gone);
        const S32 first_in = lineCount(Column::Inline);
        inlined(false, made);
        for (size_t n = 0; n < pairs.size(); ++n)
        {
            of(Column::Inline).lines[static_cast<size_t>(first_out + pairs[n].first)].words = std::move(paired[n].first);
            of(Column::Inline).lines[static_cast<size_t>(first_in + pairs[n].second)].words = std::move(paired[n].second);
        }
        change.end[index(Layout::Sides)]  = rowCount(Layout::Sides);
        change.end[index(Layout::Inline)] = rowCount(Layout::Inline);
        mChanges.push_back(change);
    }
    if (reuse)
    {
        // The groups after the change moved along: by as many rows and
        // lines as laid out again gained or lost, their lines' numbers by
        // as many as each side's lines moved; the rows of nothing above a
        // column's first line there those now waiting for it, not those
        // that were; and what each mark said of them likewise.
        const Mark  here       = stateNow(reuse->to);
        const S32   rows[2]    = { here.rows[0] - moved.rows[0], here.rows[1] - moved.rows[1] };
        const S32   shift[3]   = { again->left, again->right, again->right };
        const S32   given_left = mSwapped ? again->right : again->left;
        const S32   given_right = mSwapped ? again->left : again->right;
        S32         lines[3];
        S32         pending[3];
        for (size_t c = 0; c < 3; ++c)
        {
            ColumnData& column = mColumns[c];
            lines[c]           = here.lines[c] - moved.lines[c];
            pending[c]         = here.pending[c] - moved.pending[c];
            for (size_t n = 0; n < after_lines[c].size(); ++n)
            {
                Line& line = after_lines[c][n];
                // Inline, a line taken out has no number.
                if (line.number > 0)
                {
                    line.number += shift[c];
                }
                if (n == 0)
                {
                    line.padding += pending[c];
                }
                column.lines.push_back(std::move(line));
            }
            for (S32 line : after_line_of[c])
            {
                column.lineOf.push_back(line >= 0 ? line + lines[c] : line);
            }
            for (const S32 row : after_row_of[c])
            {
                column.rowOf.push_back(row + rows[c == 2 ? 1 : 0]);
            }
            // The rows of nothing below the last line: as they were, where
            // the column has a line after the change; else those waiting.
            column.pending = after_lines[c].empty() ? end_pending[c] + pending[c] : end_pending[c];
        }
        for (size_t side = 0; side < 2; ++side)
        {
            for (const S32 line : after_inline[side])
            {
                mInlineRows[side].push_back(line >= 0 ? line + (side == 0 ? again->left : again->right) : line);
            }
        }
        // The inline text after the change, each line its own break.
        const S32 text = static_cast<S32>(here.text) - static_cast<S32>(moved.text);
        mInlineText += after_text;
        for (Change& change : after_changes)
        {
            for (size_t l = 0; l < 2; ++l)
            {
                change.first[l] += rows[l];
                change.end[l] += rows[l];
            }
            change.lines.leftFirst += given_left;
            change.lines.rightFirst += given_right;
            mChanges.push_back(change);
        }
        for (Fold& fold : after_folds)
        {
            fold.first[0] += rows[0];
            fold.first[1] += rows[1];
            folds.push_back(fold);
        }
        // The moves' ends after the change: their inline lines moved along.
        for (size_t n = 0; n < mMoves.size(); ++n)
        {
            mMoves[n].inlineLeft += moved_ends[n].first ? lines[2] : 0;
            mMoves[n].inlineRight += moved_ends[n].second ? lines[2] : 0;
        }
        const S32    run_moved     = static_cast<S32>(runs.size()) - static_cast<S32>(again->runs.size());
        const size_t changes_moved = mChanges.size() - after_changes.size() - moved.changes;
        const size_t folds_moved   = folds.size() - after_folds.size() - moved.folds;
        for (size_t m = 0; m + 1 < after_marks.size(); ++m)
        {
            Mark mark_after = after_marks[m];
            mark_after.run  = static_cast<size_t>(static_cast<S32>(mark_after.run) + run_moved);
            for (size_t l = 0; l < 2; ++l)
            {
                mark_after.rows[l] += rows[l];
            }
            for (size_t c = 0; c < 3; ++c)
            {
                // Rows of nothing waiting as they were, where a line of the
                // column after the change came first.
                if (mark_after.lines[c] == moved.lines[c])
                {
                    mark_after.pending[c] += pending[c];
                }
                mark_after.lines[c] += lines[c];
            }
            mark_after.changes += changes_moved;
            mark_after.folds += folds_moved;
            mark_after.text = static_cast<size_t>(static_cast<S32>(mark_after.text) + text);
            mGroups.push_back(mark_after);
        }
        // What a view fills again: the lines laid out again, each column's.
        mRelaid.whole = false;
        for (size_t c = 0; c < 3; ++c)
        {
            mRelaid.first[c]    = kept.lines[c];
            mRelaid.was[c]      = moved.lines[c] - kept.lines[c];
            mRelaid.now[c]      = here.lines[c] - kept.lines[c];
            mRelaid.numbered[c] = shift[c];
        }
    }
    else
    {
        mRelaid = Relaid();
    }
    mark(runs.size());
    mLastChange = last_change;
    for (ColumnData& c : mColumns)
    {
        c.endPadding = c.pending;
        c.pending    = 0;
    }
    // Each line of either side's inline line, by the rows it is on.
    for (size_t side = 0; side < 2; ++side)
    {
        mInlineOf[side].assign(side == 0 ? left.size() : right.size(), -1);
        const std::vector<S32>& by_row = mInlineRows[side];
        const ColumnData&       column = of(Column::Inline);
        for (size_t row = 0; row < by_row.size() && row < column.lineOf.size(); ++row)
        {
            if (by_row[row] >= 0 && column.lineOf[row] >= 0)
            {
                mInlineOf[side][static_cast<size_t>(by_row[row])] = column.lineOf[row];
            }
        }
    }
    if (open.size() == folds.size())
    {
        for (size_t n = 0; n < folds.size(); ++n)
        {
            folds[n].open = open[n];
        }
    }
    mFolds = std::move(folds);
    findBracketed();
    orderRanges();
    findConflicts();
}

std::optional<ALDiffModel::Reuse> ALDiffModel::reusable(const Relayout& again, const ALDiffMoves::moves_t& moves, size_t last_change) const
{
    const std::vector<ALTextDiff::Run>& was = again.runs;
    const std::vector<ALTextDiff::Run>& now = mRuns;
    if (mGroups.empty() || mGroups.back().run != was.size())
    {
        return std::nullopt;
    }
    // The runs before the change as they were, and those after it moved
    // along by as many lines as each side gained or lost: where an edit
    // inside a change left every run as it was, both all of them, the lines
    // changed parting what is kept from what is moved.
    size_t before = 0;
    while (before < was.size() && before < now.size() && was[before] == now[before])
    {
        ++before;
    }
    size_t after = 0;
    while (after < was.size() && after < now.size())
    {
        ALTextDiff::Run run = was[was.size() - 1 - after];
        run.left += again.left;
        run.right += again.right;
        if (!(run == now[now.size() - 1 - after]))
        {
            break;
        }
        ++after;
    }
    // The groups wholly before the change; and the first wholly after it,
    // at a group's edge in the runs as they now are too: a change after
    // the change begins a group only after a run the same...
    size_t kept = 0;
    while (kept + 1 < mGroups.size() && mGroups[kept + 1].run <= before && mGroups[kept + 1].lines[again.side] <= again.head)
    {
        ++kept;
    }
    // ...and not after the last change as it was, or as it is, where that
    // is another: a run the same is folded to the end where no change
    // comes after it, and not at all where none is, so the runs from the
    // last change before are folded otherwise now. A change after none,
    // or none after one, lays out all of it again.
    const bool had_none = mLastChange == was.size();
    const bool has_none = last_change == now.size();
    if (had_none != has_none)
    {
        return std::nullopt;
    }
    while (!had_none && last_change != mLastChange && kept > 0 && mGroups[kept].run > std::min(mLastChange, last_change))
    {
        --kept;
    }
    // ...ending at a group's edge in the runs as they now are: a change
    // the runs now carry on past it is laid out again whole.
    while (kept > 0 && mGroups[kept].run < now.size() &&
           (now[mGroups[kept].run - 1].kind == Kind::Same) == (now[mGroups[kept].run].kind == Kind::Same))
    {
        --kept;
    }
    const S32 delta = static_cast<S32>(now.size()) - static_cast<S32>(was.size());
    size_t    moved = kept;
    while (moved + 1 < mGroups.size() && (mGroups[moved].run < was.size() - after || mGroups[moved].lines[again.side] < again.lines - again.tail))
    {
        ++moved;
    }
    for (; moved + 1 < mGroups.size(); ++moved)
    {
        const size_t at = static_cast<size_t>(static_cast<S32>(mGroups[moved].run) + delta);
        if (at == 0 || (now[at - 1].kind == Kind::Same) != (now[at].kind == Kind::Same))
        {
            break;
        }
    }
    const size_t from = mGroups[kept].run;
    const size_t to   = static_cast<size_t>(static_cast<S32>(mGroups[moved].run) + delta);
    // The first group stays the first, or not: its context is none.
    if (to < from || (mGroups[moved].run == 0) != (to == 0))
    {
        return std::nullopt;
    }
    // The blocks moved those there were, in the same order, each end kept
    // or moved along where it was, or laid out again: their lines' signs
    // and their moves stand.
    const Mark& k = mGroups[kept];
    const Mark& g = mGroups[moved];
    if (moves.size() != mMoves.size())
    {
        return std::nullopt;
    }
    for (size_t n = 0; n < moves.size(); ++n)
    {
        const ALDiffMoves::Move& block     = mMoves[n].lines;
        const ALDiffMoves::Move& now_block = moves[n];
        // An end where it was, where it was moved along to, or anywhere
        // within what is laid out again; one over an edge of that stretch
        // stands as its lines outside it do, which keep their marks.
        const auto ends = [&](S32 was_at, S32 now_at, size_t column, S32 shift) {
            const S32 from = k.lines[column];
            const S32 to   = g.lines[column];
            if (was_at + block.count <= from)
            {
                return now_at == was_at;
            }
            if (was_at >= to)
            {
                return now_at == was_at + shift;
            }
            if (now_at >= from && now_at + block.count <= to + shift)
            {
                return true;
            }
            const bool before = was_at < from;
            const bool after  = was_at + block.count > to;
            if (before && after)
            {
                return shift == 0 && now_at == was_at;
            }
            return before ? now_at == was_at : now_at == was_at + shift;
        };
        if (block.count != now_block.count || !ends(block.left, now_block.left, 0, again.left) || !ends(block.right, now_block.right, 1, again.right))
        {
            return std::nullopt;
        }
    }
    return Reuse{ kept, moved, from, to };
}

// --- a column's lines ----------------------------------------------------------

const ALDiffModel::Line& ALDiffModel::line(Column column, S32 line) const
{
    static const Line nothing;
    const ColumnData& c = of(column);
    return line >= 0 && line < static_cast<S32>(c.lines.size()) ? c.lines[static_cast<size_t>(line)] : nothing;
}

// --- rows -------------------------------------------------------------------------

S32 ALDiffModel::rowCount(Layout layout) const
{
    return static_cast<S32>(of(layout == Layout::Sides ? Column::Left : Column::Inline).lineOf.size());
}

S32 ALDiffModel::lineOfRow(Column column, S32 row) const
{
    const ColumnData& c = of(column);
    return row >= 0 && row < static_cast<S32>(c.lineOf.size()) ? c.lineOf[static_cast<size_t>(row)] : -1;
}

S32 ALDiffModel::rowOfLine(Column column, S32 line) const
{
    const ColumnData& c = of(column);
    if (c.rowOf.empty())
    {
        return 0;
    }
    return c.rowOf[static_cast<size_t>(llclamp(line, 0, static_cast<S32>(c.rowOf.size()) - 1))];
}

S32 ALDiffModel::lineBelowRow(Column column, S32 row) const
{
    // The first line whose row is it or after it: a column's lines are on
    // its rows in order.
    const ColumnData& c = of(column);
    return static_cast<S32>(std::lower_bound(c.rowOf.begin(), c.rowOf.end(), row) - c.rowOf.begin());
}

S32 ALDiffModel::caretLineOfRow(Column column, S32 row) const
{
    return llmin(lineBelowRow(column, row), llmax(0, lineCount(column) - 1));
}

bool ALDiffModel::rowDrawn(Layout layout, S32 row) const
{
    const S32 fold = foldOfRow(layout, row, false);
    return fold < 0 || !mFolds[static_cast<size_t>(fold)].open;
}

S32 ALDiffModel::gapRowsFrom(Column column, S32 row) const
{
    // The rows from it to the next line's, all drawn but a fold's own row
    // while its run is open, which is the only row of its gap.
    const Layout layout = layoutOf(column);
    const S32    at     = llclamp(row, 0, rowCount(layout));
    const S32    line   = lineBelowRow(column, at);
    const S32    below  = line < lineCount(column) ? rowOfLine(column, line) : rowCount(layout);
    return below - at - (below > at && !rowDrawn(layout, below - 1) ? 1 : 0);
}

S32 ALDiffModel::rightLineOfRow(Layout layout, S32 row) const
{
    const std::vector<S32>& rows = rightRows(layout);
    return row >= 0 && row < static_cast<S32>(rows.size()) ? rows[static_cast<size_t>(row)] : -1;
}

S32 ALDiffModel::rowOfRightLine(Layout layout, S32 line) const
{
    // The row of the line showing it: side by side, in the column showing
    // the right's text; inline, in the one.
    const Column column = layout == Layout::Sides ? rightColumn() : Column::Inline;
    const S32    shown  = lineShowing(column, false, line);
    return shown >= 0 ? of(column).rowOf[static_cast<size_t>(shown)] : -1;
}

std::pair<S32, S32> ALDiffModel::rightAt(Column column, S32 line, S32 at_column) const
{
    const std::vector<S32>& rows = rightRows(layoutOf(column));
    if (rows.empty())
    {
        return { 0, 0 };
    }
    const S32 row = rowOfLine(column, line);
    if (rows[static_cast<size_t>(row)] >= 0)
    {
        // The column where it stands in the right's own text: the column
        // showing the right's, or a line inline that the right has.
        const bool own = column == Column::Inline || column == rightColumn();
        return { rows[static_cast<size_t>(row)], own ? at_column : 0 };
    }
    // The right's lines are on rows in order: the first whose row is after
    // it, found by halves, else the one before that.
    const Layout layout = layoutOf(column);
    S32          after  = 0;
    for (S32 end = static_cast<S32>(mRightLines.size()); after < end;)
    {
        const S32 mid = after + (end - after) / 2;
        if (rowOfRightLine(layout, mid) <= row)
        {
            after = mid + 1;
        }
        else
        {
            end = mid;
        }
    }
    if (after < static_cast<S32>(mRightLines.size()))
    {
        return { after, 0 };
    }
    return { llmax(0, after - 1), 0 };
}

// --- changes -----------------------------------------------------------------------

S32 ALDiffModel::changeFirst(Layout layout, S32 change) const
{
    return mChanges[static_cast<size_t>(change)].first[index(layout)];
}

S32 ALDiffModel::changeEnd(Layout layout, S32 change) const
{
    return mChanges[static_cast<size_t>(change)].end[index(layout)];
}

const ALDiffModel::ChangeLines& ALDiffModel::changeLines(S32 change) const
{
    return mChanges[static_cast<size_t>(change)].lines;
}

S32 ALDiffModel::changeFrom(Layout layout, S32 row) const
{
    const size_t at = index(layout);
    return static_cast<S32>(std::lower_bound(mChanges.begin(), mChanges.end(), row, [at](const Change& change, S32 r) { return change.first[at] < r; }) -
                            mChanges.begin());
}

S32 ALDiffModel::changeOfRow(Layout layout, S32 row) const
{
    // The last change that starts at or before it, where it ends after it.
    const size_t at   = index(layout);
    const auto   next = std::upper_bound(mChanges.begin(), mChanges.end(), row, [at](S32 r, const Change& change) { return r < change.first[at]; });
    if (next == mChanges.begin())
    {
        return -1;
    }
    const size_t change = static_cast<size_t>(next - mChanges.begin()) - 1;
    return row < mChanges[change].end[at] ? static_cast<S32>(change) : -1;
}

S32 ALDiffModel::changeOfLine(Column column, S32 line) const
{
    // The change its row is in; or, where the column has none of a
    // change's lines -- the other's alone, a gap in this one -- the change
    // whose gap it is under, or at the text's end the one below the last.
    const Layout layout = layoutOf(column);
    const S32    row    = rowOfLine(column, line);
    if (const S32 change = changeOfRow(layout, row); change >= 0)
    {
        return change;
    }
    if (const S32 above = changeOfRow(layout, row - 1); above >= 0 && !hasLinesIn(column, above))
    {
        return above;
    }
    if (line == lineCount(column) - 1)
    {
        if (const S32 below = changeOfRow(layout, row + 1); below >= 0 && !hasLinesIn(column, below))
        {
            return below;
        }
    }
    return -1;
}

bool ALDiffModel::hasLinesIn(Column column, S32 change) const
{
    // Its lines of the text the column shows; inline, of either.
    const ChangeLines& c = changeLines(change);
    if (column == Column::Inline)
    {
        return c.leftCount + c.rightCount > 0;
    }
    return ((column == Column::Left) != mSwapped ? c.leftCount : c.rightCount) > 0;
}

S32 ALDiffModel::changeStep(Column column, S32 line, bool forward) const
{
    // From the change the line is in, the next or the one before; from
    // between changes, the first after its row or the last before.
    const S32 count = changeCount();
    if (const S32 in = changeOfLine(column, line); in >= 0)
    {
        const S32 to = in + (forward ? 1 : -1);
        return to >= 0 && to < count ? to : -1;
    }
    const size_t at  = index(layoutOf(column));
    const S32    row = rowOfLine(column, line);
    if (forward)
    {
        const auto next = std::upper_bound(mChanges.begin(), mChanges.end(), row, [at](S32 r, const Change& change) { return r < change.first[at]; });
        return next == mChanges.end() ? -1 : static_cast<S32>(next - mChanges.begin());
    }
    const auto next = std::lower_bound(mChanges.begin(), mChanges.end(), row, [at](const Change& change, S32 r) { return change.first[at] < r; });
    return next == mChanges.begin() ? -1 : static_cast<S32>(next - mChanges.begin()) - 1;
}

bool ALDiffModel::takeBack(S32 change, ALTextRange& range, std::string& text, std::string& made) const
{
    if (change < 0 || change >= changeCount())
    {
        return false;
    }
    // The right's lines of it in the left's place, as one edit of the
    // right's text (ALDiffEdit).
    const ChangeLines&             c = mChanges[static_cast<size_t>(change)].lines;
    const std::vector<std::string> left(mLeftLines.begin() + c.leftFirst, mLeftLines.begin() + c.leftFirst + c.leftCount);
    return ALDiffEdit::replaceLines(mRightLines, c.rightFirst, c.rightCount, left, range, text, made);
}

std::string ALDiffModel::changeText(S32 change, bool given_left) const
{
    if (change < 0 || change >= changeCount())
    {
        return std::string();
    }
    const ChangeLines&              c     = mChanges[static_cast<size_t>(change)].lines;
    const std::vector<std::string>& lines = given_left ? mLeftLines : mRightLines;
    const S32                       first = given_left ? c.leftFirst : c.rightFirst;
    const S32                       count = given_left ? c.leftCount : c.rightCount;
    std::string                     text;
    for (S32 n = 0; n < count; ++n)
    {
        text.append(lines[static_cast<size_t>(first + n)]).push_back('\n');
    }
    return text;
}

// --- a merge ---------------------------------------------------------------------------

void ALDiffModel::setMergeBase(std::optional<std::string_view> base)
{
    mMerge.reset();
    if (base)
    {
        // Theirs the left as given, ours the right.
        mMerge.emplace(ALTextDiff::split(*base), mLeftLines, mOptions);
        mMerge->setOurs(mRightLines);
    }
    findConflicts();
}

void ALDiffModel::findConflicts()
{
    mConflicted.assign(mChanges.size(), false);
    for (size_t i = 0; mMerge && i < mChanges.size(); ++i)
    {
        const ChangeLines& c = mChanges[i].lines;
        mConflicted[i]       = mMerge->inConflict(c.leftFirst, c.leftCount, c.rightFirst, c.rightCount);
    }
}

S32 ALDiffModel::conflictCount() const
{
    return mMerge ? mMerge->conflictCount() : 0;
}

bool ALDiffModel::changeConflicts(S32 change) const
{
    return change >= 0 && change < static_cast<S32>(mConflicted.size()) && mConflicted[static_cast<size_t>(change)];
}

std::optional<ALDiffMerge::Settling> ALDiffModel::settle(S32 change, ALTextMerge::Take take) const
{
    if (!changeConflicts(change))
    {
        return std::nullopt;
    }
    const ChangeLines& c = mChanges[static_cast<size_t>(change)].lines;
    return mMerge->settle(mMerge->conflictsIn(c.leftFirst, c.leftCount, c.rightFirst, c.rightCount), take);
}

void ALDiffModel::settled(const ALDiffMerge::Settling& settling)
{
    if (mMerge)
    {
        mMerge->settled(settling);
        findConflicts();
    }
}

// --- moves ---------------------------------------------------------------------------

std::pair<ALDiffModel::Column, S32> ALDiffModel::moveOtherEnd(Column column, S32 line) const
{
    const Line& one = this->line(column, line);
    if (one.move < 0)
    {
        return { column, -1 };
    }
    // A line taken out stands for the line put in as far into the block.
    const Move& move = mMoves[static_cast<size_t>(one.move)];
    if (column == Column::Inline)
    {
        return one.kind == Kind::Removed ? std::make_pair(column, move.inlineRight + line - move.inlineLeft)
                                         : std::make_pair(column, move.inlineLeft + line - move.inlineRight);
    }
    return column == Column::Left ? std::make_pair(Column::Right, move.lines.right + line - move.lines.left)
                                  : std::make_pair(Column::Left, move.lines.left + line - move.lines.right);
}

// --- ranges --------------------------------------------------------------------------

void ALDiffModel::findBracketed()
{
    // Those whose first lines are on one row, side by side; then, in order
    // of their first lines on the left, each against the lined up ones
    // that start within it there, which are the only ones that can lie
    // within it on both sides.
    const size_t count = mRanges.size();
    mBracketed.assign(count, false);
    std::vector<size_t> order;
    for (size_t n = 0; n < count; ++n)
    {
        const auto [lf, le] = rangeRows(static_cast<S32>(n), mSwapped ? Column::Right : Column::Left);
        const auto [rf, re] = rangeRows(static_cast<S32>(n), mSwapped ? Column::Left : Column::Right);
        if (le > lf && re > rf && lf == rf)
        {
            mBracketed[n] = true;
            order.push_back(n);
        }
    }
    const auto by_first = [this](size_t a, size_t b) { return mRanges[a].leftFirst < mRanges[b].leftFirst; };
    if (!std::is_sorted(order.begin(), order.end(), by_first))
    {
        std::sort(order.begin(), order.end(), by_first);
    }
    for (size_t i = 0; i < order.size(); ++i)
    {
        const ALTextDiff::Range& outer = mRanges[order[i]];
        // Back over those starting on the same line, then on while they
        // start inside it.
        size_t from = i;
        while (from > 0 && mRanges[order[from - 1]].leftFirst == outer.leftFirst)
        {
            --from;
        }
        for (size_t j = from; j < order.size() && mRanges[order[j]].leftFirst <= outer.leftLast; ++j)
        {
            const ALTextDiff::Range& inner = mRanges[order[j]];
            if (j != i && !(inner == outer) && inner.leftLast <= outer.leftLast && inner.rightFirst >= outer.rightFirst && inner.rightLast <= outer.rightLast)
            {
                mBracketed[order[i]] = false;
                break;
            }
        }
    }
}

void ALDiffModel::orderRanges()
{
    for (size_t side = 0; side < 2; ++side)
    {
        const auto first_of = [&](S32 n) { return side ? mRanges[static_cast<size_t>(n)].rightFirst : mRanges[static_cast<size_t>(n)].leftFirst; };
        std::vector<S32>& order = mRangeOrder[side];
        order.resize(mRanges.size());
        for (size_t n = 0; n < order.size(); ++n)
        {
            order[n] = static_cast<S32>(n);
        }
        const auto by_first = [&](S32 a, S32 b) { return first_of(a) < first_of(b); };
        if (!std::is_sorted(order.begin(), order.end(), by_first))
        {
            std::stable_sort(order.begin(), order.end(), by_first);
        }
        std::vector<S32>& reach = mRangeReach[side];
        reach.resize(order.size());
        S32 most = std::numeric_limits<S32>::min();
        for (size_t i = 0; i < order.size(); ++i)
        {
            const ALTextDiff::Range& r = mRanges[static_cast<size_t>(order[i])];
            most                       = llmax(most, side ? r.rightLast : r.leftLast);
            reach[i]                   = most;
        }
    }
}

S32 ALDiffModel::rangeAt(Column column, S32 line) const
{
    if (column == Column::Inline || line < 0)
    {
        return -1;
    }
    // The left shows the text given as the left, but swapped.
    const bool              given    = (column == Column::Left) != mSwapped;
    const std::vector<S32>& order    = mRangeOrder[given ? 0 : 1];
    const std::vector<S32>& reach    = mRangeReach[given ? 0 : 1];
    if (order.size() != mRanges.size())
    {
        return -1;
    }
    const auto              first_of = [&](S32 n) { return given ? mRanges[static_cast<size_t>(n)].leftFirst : mRanges[static_cast<size_t>(n)].rightFirst; };
    // Back from the last starting at or before the line, found by halves:
    // until none before reaches it, or any before would be wider there than
    // the narrowest found.
    S32 i     = static_cast<S32>(std::upper_bound(order.begin(), order.end(), line, [&](S32 l, S32 n) { return l < first_of(n); }) - order.begin());
    S32 best  = -1;
    S32 span  = 0;
    S32 other = 0;
    while (--i >= 0 && reach[static_cast<size_t>(i)] >= line)
    {
        const S32                n     = order[static_cast<size_t>(i)];
        const ALTextDiff::Range& r     = mRanges[static_cast<size_t>(n)];
        const S32                first = given ? r.leftFirst : r.rightFirst;
        const S32                last  = given ? r.leftLast : r.rightLast;
        if (best >= 0 && line - first > span)
        {
            break;
        }
        if (line > last)
        {
            continue;
        }
        const S32 mine   = last - first;
        const S32 theirs = given ? r.rightLast - r.rightFirst : r.leftLast - r.leftFirst;
        if (best < 0 || mine < span || (mine == span && (theirs > other || (theirs == other && n < best))))
        {
            best  = n;
            span  = mine;
            other = theirs;
        }
    }
    return best;
}

std::pair<S32, S32> ALDiffModel::rangeRows(S32 range, Column column) const
{
    if (column == Column::Inline || range < 0 || range >= static_cast<S32>(mRanges.size()) || lineCount(column) == 0)
    {
        return { 0, 0 };
    }
    // The left shows the text given as the left, but swapped.
    const ALTextDiff::Range& r     = mRanges[static_cast<size_t>(range)];
    const bool               given = (column == Column::Left) != mSwapped;
    const S32                last  = lineCount(column) - 1;
    const S32                first = llclamp(given ? r.leftFirst : r.rightFirst, 0, last);
    const S32                end   = llclamp(given ? r.leftLast : r.rightLast, first, last);
    return { rowOfLine(column, first), rowOfLine(column, end) + 1 };
}

// --- notes ---------------------------------------------------------------------------

void ALDiffModel::setNotes(std::vector<Note> notes)
{
    mNotes = std::move(notes);
}

S32 ALDiffModel::lineShowing(Column column, bool given_left, S32 line) const
{
    // Shown on the left is the text given as the left, but swapped.
    const bool left_shown = given_left != mSwapped;
    if (column == Column::Inline)
    {
        const std::vector<S32>& of = mInlineOf[left_shown ? 0 : 1];
        return line >= 0 && line < static_cast<S32>(of.size()) ? of[static_cast<size_t>(line)] : -1;
    }
    if ((column == Column::Left) != left_shown)
    {
        return -1;
    }
    return line >= 0 && line < lineCount(column) ? line : -1;
}

std::vector<ALDiffModel::Note> ALDiffModel::notesIn(Column column) const
{
    std::vector<Note> out;
    for (const Note& note : mNotes)
    {
        const S32 line = lineShowing(column, true, note.line);
        if (line < 0)
        {
            continue;
        }
        const auto same = std::find_if(out.begin(), out.end(), [line](const Note& one) { return one.line == line; });
        if (same == out.end())
        {
            out.push_back(Note{ line, note.text, note.tip });
        }
        else
        {
            same->text += " \xC2\xB7 " + note.text;
            same->tip += "\n" + note.tip;
        }
    }
    return out;
}

// --- folds ---------------------------------------------------------------------------

void ALDiffModel::setFoldSame(bool fold)
{
    mFoldSame = fold;
    for (Fold& each : mFolds)
    {
        each.open = !fold;
    }
}

S32 ALDiffModel::foldedCount() const
{
    return static_cast<S32>(std::count_if(mFolds.begin(), mFolds.end(), [](const Fold& fold) { return !fold.open; }));
}

bool ALDiffModel::foldOpen(S32 fold) const
{
    return mFolds[static_cast<size_t>(fold)].open;
}

void ALDiffModel::setFoldOpen(S32 fold, bool open)
{
    mFolds[static_cast<size_t>(fold)].open = open;
}

std::vector<bool> ALDiffModel::foldsOpen() const
{
    std::vector<bool> open;
    open.reserve(mFolds.size());
    for (const Fold& fold : mFolds)
    {
        open.push_back(fold.open);
    }
    return open;
}

S32 ALDiffModel::foldLines(S32 fold) const
{
    return mFolds[static_cast<size_t>(fold)].count;
}

S32 ALDiffModel::foldFirst(Layout layout, S32 fold) const
{
    return mFolds[static_cast<size_t>(fold)].first[index(layout)];
}

S32 ALDiffModel::foldRow(Layout layout, S32 fold) const
{
    return foldFirst(layout, fold) + foldLines(fold);
}

S32 ALDiffModel::foldFrom(Layout layout, S32 row) const
{
    // By their own rows, which are in order as their runs are.
    const size_t at = index(layout);
    return static_cast<S32>(
        std::lower_bound(mFolds.begin(), mFolds.end(), row, [at](const Fold& fold, S32 r) { return fold.first[at] + fold.count < r; }) - mFolds.begin());
}

S32 ALDiffModel::foldOfRow(Layout layout, S32 row, bool lines) const
{
    // The last fold whose run starts at or before it, in order of their rows.
    const size_t at    = index(layout);
    const auto   after = std::upper_bound(mFolds.begin(), mFolds.end(), row, [at](S32 r, const Fold& fold) { return r < fold.first[at]; });
    if (after == mFolds.begin())
    {
        return -1;
    }
    const S32 fold  = static_cast<S32>(after - mFolds.begin()) - 1;
    const S32 first = foldFirst(layout, fold);
    const S32 own   = foldRow(layout, fold);
    return row == own || (lines && row >= first && row < own) ? fold : -1;
}

S32 ALDiffModel::foldOfGap(Column column, S32 line) const
{
    // The row before the line the gap is above, or the last row for the
    // gap below the text: a folded run's own, where it is one.
    if (line < 0)
    {
        return -1;
    }
    const ColumnData& c    = of(column);
    const S32         row  = (line < static_cast<S32>(c.rowOf.size()) ? c.rowOf[static_cast<size_t>(line)] : static_cast<S32>(c.lineOf.size())) - 1;
    const S32         fold = foldOfRow(layoutOf(column), row, false);
    return fold >= 0 && !mFolds[static_cast<size_t>(fold)].open ? fold : -1;
}

S32 ALDiffModel::foldFirstLine(Column column, S32 fold) const
{
    return lineBelowRow(column, foldFirst(layoutOf(column), fold));
}

S32 ALDiffModel::foldGapLine(Column column, S32 fold) const
{
    return lineBelowRow(column, foldRow(layoutOf(column), fold));
}
