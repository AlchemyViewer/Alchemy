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

#include "aldiffsame.h"
#include "allinepairs.h"

#include <algorithm>

// --- LineMap -------------------------------------------------------------------

S32 ALDiffModel::LineMap::line(S32 was) const
{
    if (to.empty())
    {
        return 0;
    }
    return llclamp(to[static_cast<size_t>(llclamp(was, 0, static_cast<S32>(to.size()) - 1))], 0, last);
}

bool ALDiffModel::LineMap::kept(S32 was) const
{
    return was >= 0 && was < static_cast<S32>(same.size()) && same[static_cast<size_t>(was)];
}

// --- what is compared ---------------------------------------------------------

ALDiffModel::ALDiffModel()
{
    build();
}

void ALDiffModel::setTexts(std::string_view left, std::string_view right, const ALTextDiff::ranges_t& ranges)
{
    mLeftText  = std::string(left);
    mRightText = std::string(right);
    mRanges    = ranges;
    build();
}

ALDiffModel::LineMap ALDiffModel::setRightText(std::string_view right)
{
    // Each line of the right as it was, where it now is; and which are
    // still there as they were.
    const std::vector<std::string> was = ALTextDiff::split(mRightText);
    const std::vector<std::string> now = ALTextDiff::split(right);
    LineMap                        map;
    map.to.assign(was.size() + 1, static_cast<S32>(now.size()));
    map.same.assign(was.size(), false);
    map.last = llmax(0, static_cast<S32>(now.size()) - 1);
    for (const ALTextDiff::Run& run : ALTextDiff::lines(was, now))
    {
        for (S32 n = 0; n < run.count && run.kind != Kind::Added; ++n)
        {
            const size_t line = static_cast<size_t>(run.left + n);
            map.to[line]      = run.kind == Kind::Same ? run.right + n : run.right;
            map.same[line]    = run.kind == Kind::Same;
        }
    }
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
    const std::vector<S32>& rows = mRightLines[index(Layout::Sides)];
    for (const Fold& fold : mFolds)
    {
        const S32 first = fold.first[index(Layout::Sides)];
        if (fold.open && first < static_cast<S32>(rows.size()) && rows[static_cast<size_t>(first)] >= 0)
        {
            opened.push_back(map.line(rows[static_cast<size_t>(first)]));
        }
    }
    mRightText = std::string(right);
    mRanges    = std::move(ranges);
    build();
    const std::vector<S32>& now_rows = mRightLines[index(Layout::Sides)];
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
    build(foldsOpen());
}

// --- made --------------------------------------------------------------------------

S32 ALDiffModel::add(Column column, const std::string& text, S32 number, Kind kind, char sign)
{
    ColumnData& c = of(column);
    if (!c.lines.empty())
    {
        c.text += '\n';
    }
    c.text += text;
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

void ALDiffModel::build(const std::vector<bool>& open)
{
    // What is shown on the left and on the right: the texts as given, or
    // swapped, and the pairs that line them up with them.
    const std::vector<std::string> left  = ALTextDiff::split(mSwapped ? mRightText : mLeftText);
    const std::vector<std::string> right = ALTextDiff::split(mSwapped ? mLeftText : mRightText);
    ALTextDiff::anchors_t          anchors = ALTextDiff::anchorsOf(mRanges);
    if (mSwapped)
    {
        for (auto& [from, to] : anchors)
        {
            std::swap(from, to);
        }
    }
    ALTextDiff::Options options = mOptions;
    options.anchors             = std::move(anchors);
    const std::vector<ALTextDiff::Run> runs = ALTextDiff::lines(left, right, options);
    // Each text's lines' regions, where a grammar cuts their words.
    const std::vector<ALTextDiff::regions_t>* left_regions  = nullptr;
    const std::vector<ALTextDiff::regions_t>* right_regions = nullptr;
    if (mOptions.lexer)
    {
        left_regions  = &mOptions.lexer(left);
        right_regions = &mOptions.lexer(right);
        if (left_regions->size() != left.size() || right_regions->size() != right.size())
        {
            left_regions  = nullptr;
            right_regions = nullptr;
        }
    }
    const auto regionsOf = [](const std::vector<ALTextDiff::regions_t>* regions, S32 line) {
        return regions ? &(*regions)[static_cast<size_t>(line)] : nullptr;
    };
    // The words that mean the same in a pair: the innermost range's that
    // holds both its lines and has a table of its own, with the whole
    // comparison's; else the whole's. By each line of the text given as
    // the left, the ranges with tables over it, the narrowest first.
    std::vector<std::vector<S32>> tabled;
    std::vector<ALTextDiff::same_t> joined(mRanges.size());
    for (size_t n = 0; n < mRanges.size(); ++n)
    {
        const ALTextDiff::Range& range = mRanges[n];
        if (!range.same)
        {
            continue;
        }
        tabled.resize(std::max(tabled.size(), static_cast<size_t>(std::max(range.leftLast + 1, 0))));
        for (S32 line = std::max(range.leftFirst, 0); line <= range.leftLast; ++line)
        {
            tabled[static_cast<size_t>(line)].push_back(static_cast<S32>(n));
        }
    }
    for (std::vector<S32>& over : tabled)
    {
        std::sort(over.begin(), over.end(), [this](S32 a, S32 b) {
            const ALTextDiff::Range& x = mRanges[static_cast<size_t>(a)];
            const ALTextDiff::Range& y = mRanges[static_cast<size_t>(b)];
            return x.leftLast - x.leftFirst + x.rightLast - x.rightFirst < y.leftLast - y.leftFirst + y.rightLast - y.rightFirst;
        });
    }
    ALTextDiff::Options with = mOptions;
    const auto          optionsOf = [&](S32 shown_left, S32 shown_right) -> const ALTextDiff::Options& {
        const S32 given_left  = mSwapped ? shown_right : shown_left;
        const S32 given_right = mSwapped ? shown_left : shown_right;
        if (given_left >= 0 && given_left < static_cast<S32>(tabled.size()))
        {
            for (const S32 n : tabled[static_cast<size_t>(given_left)])
            {
                const ALTextDiff::Range& range = mRanges[static_cast<size_t>(n)];
                if (given_right >= range.rightFirst && given_right <= range.rightLast)
                {
                    ALTextDiff::same_t& table = joined[static_cast<size_t>(n)];
                    if (!table)
                    {
                        table = ALDiffSame::joined(mOptions.same, range.same);
                    }
                    with.same = table;
                    return with;
                }
            }
        }
        return mOptions;
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
        c = ColumnData();
    }
    mChanges.clear();
    std::vector<Fold> folds;
    // Each inline row's line of the left and of the right as shown.
    std::vector<S32>  inline_left;
    std::vector<S32>  inline_right;
    // The last run that is a change: the runs the same after it are at
    // the text's end. Where none is, nothing is folded.
    size_t last_change = runs.size();
    for (size_t i = runs.size(); i-- > 0;)
    {
        if (runs[i].kind != Kind::Same)
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
        // A change: the lines taken out and put in between two the same,
        // side by side, the first taken out beside the first put in. Where
        // it starts on each side, whichever it begins with.
        Change change;
        change.lines.leftFirst  = runs[i].left;
        change.lines.rightFirst = runs[i].right;
        std::vector<S32> gone;
        std::vector<S32> made;
        for (; i < runs.size() && runs[i].kind != Kind::Same; ++i)
        {
            const bool out = runs[i].kind == Kind::Removed;
            for (S32 n = 0; n < runs[i].count; ++n)
            {
                (out ? gone : made).push_back((out ? runs[i].left : runs[i].right) + n);
            }
        }
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
        ALLinePairs::pairs_t pairs = ALLinePairs::pair(left, right, gone_free, made_free, options, left_regions, right_regions);
        for (auto& [g, d] : pairs)
        {
            g = gone_at[static_cast<size_t>(g)];
            d = made_at[static_cast<size_t>(d)];
        }
        // A line taken out or put in alone, signed as its own or as moved.
        const auto alone = [&](Column column, const std::string& text, S32 line, Kind kind, S32 move) {
            add(column, text, line + 1, kind, move >= 0 ? '>' : kind == Kind::Removed ? '-' : '+');
            of(column).lines.back().move = move;
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
                alone(Column::Left, left[static_cast<size_t>(gone[g])], gone[g], Kind::Removed, left_move[static_cast<size_t>(gone[g])]);
                pad(Column::Right);
            }
            for (; d < to_made; ++d)
            {
                pad(Column::Left);
                alone(Column::Right, right[static_cast<size_t>(made[d])], made[d], Kind::Added, right_move[static_cast<size_t>(made[d])]);
            }
            if (p == pairs.size())
            {
                break;
            }
            add(Column::Left, left[static_cast<size_t>(gone[g])], gone[g] + 1, Kind::Removed, '~');
            add(Column::Right, right[static_cast<size_t>(made[d])], made[d] + 1, Kind::Added, '~');
            auto& [lspans, rspans] = paired.emplace_back();
            ALTextDiff::words(left[static_cast<size_t>(gone[g])], right[static_cast<size_t>(made[d])], lspans, rspans, optionsOf(gone[g], made[d]),
                              regionsOf(left_regions, gone[g]), regionsOf(right_regions, made[d]));
            of(Column::Left).lines.back().words  = lspans;
            of(Column::Right).lines.back().words = rspans;
            ++g;
            ++d;
        }
        // Inline: what was taken out, unnumbered, above what was put in.
        const S32 first_out = lineCount(Column::Inline);
        for (const S32 line : gone)
        {
            const S32 move = left_move[static_cast<size_t>(line)];
            if (move >= 0 && mMoves[static_cast<size_t>(move)].lines.left == line)
            {
                mMoves[static_cast<size_t>(move)].inlineLeft = lineCount(Column::Inline);
            }
            add(Column::Inline, left[static_cast<size_t>(line)], 0, Kind::Removed, move >= 0 ? '>' : '-');
            of(Column::Inline).lines.back().move = move;
            inline_left.push_back(line);
            inline_right.push_back(-1);
        }
        const S32 first_in = lineCount(Column::Inline);
        for (const S32 line : made)
        {
            const S32 move = right_move[static_cast<size_t>(line)];
            if (move >= 0 && mMoves[static_cast<size_t>(move)].lines.right == line)
            {
                mMoves[static_cast<size_t>(move)].inlineRight = lineCount(Column::Inline);
            }
            add(Column::Inline, right[static_cast<size_t>(line)], line + 1, Kind::Added, move >= 0 ? '>' : '+');
            of(Column::Inline).lines.back().move = move;
            inline_left.push_back(-1);
            inline_right.push_back(line);
        }
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
    // The right's lines by row: side by side, the column showing it;
    // inline, the right's as shown, or swapped the left's.
    mRightLines[index(Layout::Sides)] = of(rightColumn()).lineOf;
    mRightLines[index(Layout::Inline)] = mSwapped ? inline_left : inline_right;
    if (open.size() == folds.size())
    {
        for (size_t n = 0; n < folds.size(); ++n)
        {
            folds[n].open = open[n];
        }
    }
    mFolds = std::move(folds);
    findBracketed();
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
    const ColumnData& c    = of(column);
    const S32         rows = static_cast<S32>(c.lineOf.size());
    for (S32 at = llmax(0, row); at < rows; ++at)
    {
        if (c.lineOf[static_cast<size_t>(at)] >= 0)
        {
            return c.lineOf[static_cast<size_t>(at)];
        }
    }
    return static_cast<S32>(c.lines.size());
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

S32 ALDiffModel::rightLineOfRow(Layout layout, S32 row) const
{
    const std::vector<S32>& rows = mRightLines[index(layout)];
    return row >= 0 && row < static_cast<S32>(rows.size()) ? rows[static_cast<size_t>(row)] : -1;
}

S32 ALDiffModel::rowOfRightLine(Layout layout, S32 line) const
{
    const std::vector<S32>& rows = mRightLines[index(layout)];
    const auto              at   = std::find(rows.begin(), rows.end(), line);
    return line >= 0 && at != rows.end() ? static_cast<S32>(at - rows.begin()) : -1;
}

std::pair<S32, S32> ALDiffModel::rightAt(Column column, S32 line, S32 at_column) const
{
    const std::vector<S32>& rows = mRightLines[index(layoutOf(column))];
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
    const Layout layout = layoutOf(column);
    for (S32 row = changeFirst(layout, change); row < changeEnd(layout, change); ++row)
    {
        if (lineOfRow(column, row) >= 0)
        {
            return true;
        }
    }
    return false;
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
    // The right's lines of it in the left's place: the lines between, a
    // line's break with the lines taken out or put in, at the text's end
    // the one before them.
    const ChangeLines&             c     = mChanges[static_cast<size_t>(change)].lines;
    const std::vector<std::string> left  = ALTextDiff::split(mLeftText);
    const std::vector<std::string> right = ALTextDiff::split(mRightText);
    std::string                    lines;
    for (S32 n = 0; n < c.leftCount; ++n)
    {
        lines += (n ? "\n" : "") + left[static_cast<size_t>(c.leftFirst + n)];
    }
    const S32  last = static_cast<S32>(right.size()) - 1;
    const auto ends = [&](S32 line) { return ALTextPos(line, static_cast<S32>(right[static_cast<size_t>(line)].size())); };
    text.clear();
    if (c.rightCount > 0 && c.leftCount > 0)
    {
        range = ALTextRange(ALTextPos(c.rightFirst, 0), ends(c.rightFirst + c.rightCount - 1));
        text  = lines;
    }
    else if (c.rightCount > 0)
    {
        const S32 after = c.rightFirst + c.rightCount;
        range           = after <= last          ? ALTextRange(ALTextPos(c.rightFirst, 0), ALTextPos(after, 0))
                          : c.rightFirst > 0 ? ALTextRange(ends(c.rightFirst - 1), ends(last))
                                             : ALTextRange(ALTextPos(0, 0), ends(last));
    }
    else
    {
        range = c.rightFirst <= last ? ALTextRange(ALTextPos(c.rightFirst, 0), ALTextPos(c.rightFirst, 0)) : ALTextRange(ends(last), ends(last));
        text  = c.rightFirst <= last ? lines + "\n" : "\n" + lines;
    }
    // The right as it will be, by where each line of it starts in the text.
    std::vector<size_t> starts{ 0 };
    for (size_t at = mRightText.find('\n'); at != std::string::npos; at = mRightText.find('\n', at + 1))
    {
        starts.push_back(at + 1);
    }
    const auto offset = [&](const ALTextPos& pos) { return starts[static_cast<size_t>(pos.line)] + static_cast<size_t>(pos.column); };
    made              = mRightText.substr(0, offset(range.begin)) + text + mRightText.substr(offset(range.end));
    return true;
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
