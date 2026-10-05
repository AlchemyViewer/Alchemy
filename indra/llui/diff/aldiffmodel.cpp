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
#include <limits>

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
    build();
}

ALDiffModel::LineMap ALDiffModel::setRightText(std::string_view right)
{
    // Each line of the right as it was, where it now is.
    std::vector<std::string>        now = ALTextDiff::split(right);
    const std::vector<std::string>& was = mRightLines;
    const LineMap                   map = ALDiffSplice::lineMap(was, now);
    // A range goes with its lines, changed or not: an edit of the SLua a
    // statement became still stands for the statement. Two come to one
    // line where one was taken out; lines() keeps those it can.
    ALTextDiff::ranges_t ranges;
    for (ALTextDiff::Range range : mRanges)
    {
        if (range.rightFirst >= 0 && range.rightFirst < static_cast<S32>(was.size()))
        {
            range.rightFirst = map.line(range.rightFirst);
            range.rightLast  = llmax(range.rightFirst, map.line(range.rightLast));
            ranges.push_back(range);
        }
    }
    // The runs open, by the first line of the right each hides.
    std::vector<S32>        opened;
    const std::vector<S32>& rows = mRightRows[index(Layout::Sides)];
    for (const Fold& fold : mFolds)
    {
        const S32 first = fold.first[index(Layout::Sides)];
        if (fold.open && first < static_cast<S32>(rows.size()) && rows[static_cast<size_t>(first)] >= 0)
        {
            opened.push_back(map.line(rows[static_cast<size_t>(first)]));
        }
    }
    // Compared again where it changed (ALDiffSplice), else all of it.
    mRegions.reset();
    std::vector<std::string> before = std::move(mRightLines);
    mRightText                      = ALLineBreaks::withLineFeeds(right);
    mRightLines                     = std::move(now);
    if (mMerge)
    {
        mMerge->setOurs(mRightLines);
    }
    mRanges                         = std::move(ranges);
    const ALTextDiff::Options options = shownOptions();
    const bool spliced = mSwapped ? ALDiffSplice::splice(mRuns, before, mRightLines, mLeftLines, mLeftLines, options)
                                  : ALDiffSplice::splice(mRuns, mLeftLines, mLeftLines, before, mRightLines, options);
    if (spliced && options.algorithm == ALTextDiff::Algorithm::Structural)
    {
        // Its changes read as tokens again, the lines' runs as spliced.
        readTokens();
        layout();
    }
    else if (spliced)
    {
        layout();
    }
    else
    {
        build();
    }
    const std::vector<S32>& now_rows = mRightRows[index(Layout::Sides)];
    for (Fold& fold : mFolds)
    {
        const S32 first = fold.first[index(Layout::Sides)];
        const S32 line  = first < static_cast<S32>(now_rows.size()) ? now_rows[static_cast<size_t>(first)] : -1;
        if (std::find(opened.begin(), opened.end(), line) != opened.end())
        {
            fold.open = true;
        }
    }
    return map;
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
    build();
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
        mOptions.algorithm = algorithm;
        build();
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
        if (!c.lines.empty())
        {
            mInlineText += '\n';
        }
        mInlineText += text;
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
        layout(open);
        return;
    }
    // By structure: the lines' runs, then their changes read as tokens.
    ALTextDiff::Options by_lines = options;
    by_lines.algorithm           = ALTextDiff::Algorithm::Histogram;
    mRuns                        = ALTextDiff::lines(left, right, by_lines);
    readTokens();
    layout(open);
}

void ALDiffModel::readTokens()
{
    const auto [left_regions, right_regions] = shownRegions();
    ALStructuralDiff::Result by_tokens       = ALStructuralDiff::read(shownLeft(), shownRight(), std::move(mRuns), mOptions, left_regions, right_regions);
    mRuns        = std::move(by_tokens.runs);
    mFellBack    = by_tokens.tooLarge;
    mMarks[0]    = std::move(by_tokens.leftMarks);
    mMarks[1]    = std::move(by_tokens.rightMarks);
    mByTokens[0] = std::move(by_tokens.leftByTokens);
    mByTokens[1] = std::move(by_tokens.rightByTokens);
}

void ALDiffModel::layout(const std::vector<bool>& open)
{
    // What is shown on the left and on the right: the texts as given, or
    // swapped, and their runs.
    const std::vector<std::string>&     left    = shownLeft();
    const std::vector<std::string>&     right   = shownRight();
    const std::vector<ALTextDiff::Run>& runs    = mRuns;
    const ALTextDiff::Options           options = shownOptions();
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
    std::sort(by_left.begin(), by_left.end());
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
    const ALDiffMoves::moves_t moves = ALDiffMoves::find(left, right, runs, options);
    std::vector<S32>           left_move(left.size(), -1);
    std::vector<S32>           right_move(right.size(), -1);
    mMoves.assign(moves.size(), Move());
    for (size_t n = 0; n < moves.size(); ++n)
    {
        mMoves[n].lines = moves[n];
        for (S32 k = 0; k < moves[n].count; ++k)
        {
            left_move[static_cast<size_t>(moves[n].left + k)]   = static_cast<S32>(n);
            right_move[static_cast<size_t>(moves[n].right + k)] = static_cast<S32>(n);
        }
    }
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
    std::vector<Fold> folds;
    // Each inline row's line of the left and of the right as shown.
    std::vector<S32>  inline_left;
    std::vector<S32>  inline_right;
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
    for (size_t i = 0; i < runs.size();)
    {
        const ALTextDiff::Run& run = runs[i];
        if (run.kind == Kind::Same)
        {
            const auto same = [&](S32 n) {
                const S32 l = run.left + n;
                const S32 r = run.right + n;
                add(Column::Left, left[static_cast<size_t>(l)], l + 1, Kind::Same);
                add(Column::Right, right[static_cast<size_t>(r)], r + 1, Kind::Same);
                add(Column::Inline, right[static_cast<size_t>(r)], r + 1, Kind::Same);
                inline_left.push_back(l);
                inline_right.push_back(r);
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
                inline_left.push_back(-1);
                inline_right.push_back(-1);
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
                inline_left.push_back(-1);
                inline_right.push_back(line);
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
                inline_left.push_back(out ? line : -1);
                inline_right.push_back(out ? -1 : line);
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
    for (ColumnData& c : mColumns)
    {
        c.endPadding = c.pending;
        c.pending    = 0;
    }
    // Each line of either side's inline line, by the rows it is on.
    for (size_t side = 0; side < 2; ++side)
    {
        mInlineOf[side].assign(side == 0 ? left.size() : right.size(), -1);
        const std::vector<S32>& by_row = side == 0 ? inline_left : inline_right;
        const ColumnData&       column = of(Column::Inline);
        for (size_t row = 0; row < by_row.size() && row < column.lineOf.size(); ++row)
        {
            if (by_row[row] >= 0 && column.lineOf[row] >= 0)
            {
                mInlineOf[side][static_cast<size_t>(by_row[row])] = column.lineOf[row];
            }
        }
    }
    // The right's lines by row: side by side, the column showing it;
    // inline, the right's as shown, or swapped the left's.
    mRightRows[index(Layout::Sides)] = of(rightColumn()).lineOf;
    mRightRows[index(Layout::Inline)] = mSwapped ? inline_left : inline_right;
    if (open.size() == folds.size())
    {
        for (size_t n = 0; n < folds.size(); ++n)
        {
            folds[n].open = open[n];
        }
    }
    mFolds = std::move(folds);
    findBracketed();
    findConflicts();
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
    const std::vector<S32>& rows = mRightRows[index(layout)];
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
    const std::vector<S32>& rows = mRightRows[index(layoutOf(column))];
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
    for (S32 n = row + 1; n < static_cast<S32>(rows.size()); ++n)
    {
        if (rows[static_cast<size_t>(n)] >= 0)
        {
            return { rows[static_cast<size_t>(n)], 0 };
        }
    }
    for (S32 n = row - 1; n >= 0; --n)
    {
        if (rows[static_cast<size_t>(n)] >= 0)
        {
            return { rows[static_cast<size_t>(n)], 0 };
        }
    }
    return { 0, 0 };
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
        text += lines[static_cast<size_t>(first + n)] + "\n";
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
        mConflicted[i]       = !mMerge->conflictsIn(c.leftFirst, c.leftCount, c.rightFirst, c.rightCount).empty();
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

void ALDiffModel::settled(ALDiffMerge::lines_t base)
{
    if (mMerge)
    {
        mMerge->settled(std::move(base));
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
    std::sort(order.begin(), order.end(), [this](size_t a, size_t b) { return mRanges[a].leftFirst < mRanges[b].leftFirst; });
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

S32 ALDiffModel::rangeAt(Column column, S32 line) const
{
    if (column == Column::Inline || line < 0)
    {
        return -1;
    }
    // The left shows the text given as the left, but swapped.
    const bool given = (column == Column::Left) != mSwapped;
    S32        best  = -1;
    S32        span  = 0;
    S32        other = 0;
    for (size_t n = 0; n < mRanges.size(); ++n)
    {
        const ALTextDiff::Range& r     = mRanges[n];
        const S32                first = given ? r.leftFirst : r.rightFirst;
        const S32                last  = given ? r.leftLast : r.rightLast;
        if (line < first || line > last)
        {
            continue;
        }
        const S32 mine   = last - first;
        const S32 theirs = given ? r.rightLast - r.rightFirst : r.leftLast - r.leftFirst;
        if (best < 0 || mine < span || (mine == span && theirs > other))
        {
            best  = static_cast<S32>(n);
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
