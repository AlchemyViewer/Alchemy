/**
 * @file aldiffmodel.h
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

#ifndef AL_ALDIFFMODEL_H
#define AL_ALDIFFMODEL_H

#include "aldiffmoves.h"
#include "altextdiff.h"
#include "altextdocument.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Two texts compared (ALTextDiff), laid out as a comparison shows them, two
// ways at once. Side by side, two columns, the left and the right, each
// its own text as it is, their lines lined up in rows: a row of nothing
// where a column has no line, which is a row of the gap above its next
// line; within a change, the lines taken out and put in that stand for
// each other (ALLinePairs) on one row, the rest alone beside
// rows of nothing. Inline, one column: what was taken out above what was
// put in. In
// both, a run of lines the same beyond FOLD_CONTEXT lines either side of
// a change -- none at the text's start or end -- where that leaves
// FOLD_LEAST or more, is folded away, with a row of its own after it that
// stands for it while it is folded and has no line in any column.
//
// The left shows the text given as the left, or swapped the right's, and
// the right the other: the texts are still the ones given, a change's
// lines are of them, and the right's text is the one an edit goes to.
//
// It says what each line of a column is -- taken out, put in, the same;
// its sign; the words that changed; the rows of nothing above it -- and
// answers every lookup a view makes: a line's row and a row's line, the
// change and the fold of a row or a line, the right's line at a row,
// where a step from a line goes. And the arithmetic of the right made
// anew, its lines carried to where they went, and of a change taken back.
// Pure: what ALDiffView shows, and what a test can hold it to, without a
// window.
class ALDiffModel
{
public:
    static constexpr S32 FOLD_CONTEXT = 3;
    static constexpr S32 FOLD_LEAST   = 8;

    typedef ALTextDiff::Kind Kind;

    // A column: the left and the right side by side, and the one inline.
    enum class Column : U8
    {
        Left,
        Right,
        Inline
    };
    // The rows a column is in.
    enum class Layout : U8
    {
        Sides,
        Inline
    };
    static Layout layoutOf(Column column) { return column == Column::Inline ? Layout::Inline : Layout::Sides; }

    // A line of a column: taken out (Removed), put in (Added) or the same;
    // its sign, '~' for a line changed into another, '-' taken out and '+'
    // put in, '>' taken out or put in as part of a block moved, nought for
    // the same; the number its gutter shows -- of its own text side by
    // side; inline, the right's, nought for a line taken out; the words of
    // it that changed; the rows of nothing above it, beside lines the
    // other column has; and the block moved it is in, -1 for none.
    struct Line
    {
        Kind                kind    = Kind::Same;
        char                sign    = 0;
        S32                 number  = 0;
        S32                 padding = 0;
        ALTextDiff::spans_t words;
        S32                 move    = -1;
    };

    // A change's lines in the texts as given, swapped or not: where they
    // start on the left and on the right, and how many each has.
    struct ChangeLines
    {
        S32 leftFirst  = 0;
        S32 leftCount  = 0;
        S32 rightFirst = 0;
        S32 rightCount = 0;
    };

    // Where each line of the right as it was went when it was made anew:
    // a line still there, to itself; one taken out or changed, to where the
    // right had got to there -- a line changed, to the line it became.
    struct LineMap
    {
        S32  line(S32 was) const;
        // Whether it is still there as it was, where a column on it holds.
        bool kept(S32 was) const;

        std::vector<S32>  to;
        std::vector<bool> same;
        S32               last = 0;
    };

    ALDiffModel();

    // --- what is compared --------------------------------------------------

    // The texts, the left the one taken from and the right the one made;
    // lined up where stretches of them are known to stand for each other
    // (ALTextDiff's ranges), however they differ: each range starting
    // beside its other, and what follows it level again. The runs folded
    // as foldsSame() says.
    void setTexts(std::string_view left, std::string_view right, const ALTextDiff::ranges_t& ranges = {});
    // The right made anew -- the text it is of, changed -- and compared
    // again, only where it changed (ALDiffSplice) where that is enough: the
    // ranges carried to where their lines of the right went, changed or
    // not, and the runs as open as they were, by the first line of the
    // right each hides. Where each of its lines went.
    LineMap setRightText(std::string_view right);
    // Swapped or not: the runs as open as they were.
    void    setSwapped(bool swapped);
    // How lines are told the same: the runs folded as foldsSame() says,
    // since they are others now.
    void    setLikeness(const ALTextDiff::Likeness& like);
    // Words that mean the same in the two texts (ALDiffSame), the whole
    // comparison's; a range may have its own besides.
    void    setSame(ALTextDiff::same_t same);
    // How the lines that stay are chosen (ALTextDiff::Algorithm).
    void    setAlgorithm(ALTextDiff::Algorithm algorithm);
    // How lines are cut into words -- a grammar's tokens, or none for their
    // bytes alone -- and so which lines of a change pair and what is
    // marked in them.
    void    setLexer(ALTextDiff::lexer_t lexer);

    const std::string&           leftText() const { return mLeftText; }
    const std::string&           rightText() const { return mRightText; }
    const ALTextDiff::ranges_t&  ranges() const { return mRanges; }
    const ALTextDiff::Likeness&  likeness() const { return mOptions.like; }
    // How the texts are compared, but for the anchors, which are the
    // ranges'.
    const ALTextDiff::Options&   options() const { return mOptions; }
    bool                         swapped() const { return mSwapped; }

    // --- a column's lines --------------------------------------------------

    const std::string& text(Column column) const { return of(column).text; }
    S32                lineCount(Column column) const { return static_cast<S32>(of(column).lines.size()); }
    // Its line; nothing, past its lines.
    const Line&        line(Column column, S32 line) const;
    // The rows of nothing below its last line.
    S32                endPadding(Column column) const { return of(column).endPadding; }
    // The column showing the right's text side by side: the right, or the
    // left, swapped. Inline shows the right's lines, and the left's taken
    // out of it.
    Column             rightColumn() const { return mSwapped ? Column::Left : Column::Right; }

    // --- rows ----------------------------------------------------------------

    S32  rowCount(Layout layout) const;
    // A row's line in a column, -1 for none; a line's row.
    S32  lineOfRow(Column column, S32 row) const;
    S32  rowOfLine(Column column, S32 line) const;
    // A row's line, or the line after it where it has none; one past the
    // last past them all, which is the gap below the text.
    S32  lineBelowRow(Column column, S32 row) const;
    // Where the caret goes for a row: its line, or the line under its gap,
    // or at the text's end the last.
    S32  caretLineOfRow(Column column, S32 row) const;
    // Whether a row with no line is drawn: a fold's own row only while its
    // run is folded.
    bool rowDrawn(Layout layout, S32 row) const;
    // The line of the right's text at a row, -1 for none; and the row
    // that has a line of it, -1 for none.
    S32  rightLineOfRow(Layout layout, S32 row) const;
    S32  rowOfRightLine(Layout layout, S32 line) const;
    // The line of the right's text a place in a column is at, and the
    // column there: a line of the right's own, as it is; else the nearest
    // line after it the right has, else before, from its start.
    std::pair<S32, S32> rightAt(Column column, S32 line, S32 at_column) const;

    // --- changes ---------------------------------------------------------------

    // Each run of lines taken out, put in, or both, between lines the same.
    S32                changeCount() const { return static_cast<S32>(mChanges.size()); }
    // Its first row, and the row past its last, the same in every column
    // of a layout.
    S32                changeFirst(Layout layout, S32 change) const;
    S32                changeEnd(Layout layout, S32 change) const;
    const ChangeLines& changeLines(S32 change) const;
    // The change a row is in; -1 for none.
    S32                changeOfRow(Layout layout, S32 row) const;
    // The change a line of a column is in, or, where the column has none
    // of a change's lines, the change whose gap it is under -- at the
    // text's end, the one below the last line; -1 for none.
    S32                changeOfLine(Column column, S32 line) const;
    // Whether a column has any line among a change's rows.
    bool               hasLinesIn(Column column, S32 change) const;
    // From a line of a column, the change after the one it is in, or the
    // one before; from between changes, the next after its row or the last
    // before. -1 where there is none that way.
    S32                changeStep(Column column, S32 line, bool forward) const;
    // A change taken back: the right's lines of it made the left's again,
    // as one edit of the right's text -- what stretch of it and what goes
    // there -- and the right's text as it will be. False for no change.
    bool               takeBack(S32 change, ALTextRange& range, std::string& text, std::string& made) const;

    // --- moves -------------------------------------------------------------------

    // Blocks of lines taken out in one place and put in at another, the
    // same (ALDiffMoves): each line of one signed '>', not paired with
    // another, and its move's.
    S32                    moveCount() const { return static_cast<S32>(mMoves.size()); }
    // The line at the other end of the move a line of a column is in --
    // side by side in the other column, inline in its own -- and that
    // column; -1 for the line where it is in none.
    std::pair<Column, S32> moveOtherEnd(Column column, S32 line) const;

    // --- ranges ------------------------------------------------------------------

    // A range's rows side by side in a column -- of the stretch of the text
    // the column shows -- from its first line's row to the row past its
    // last line's; nothing inline.
    std::pair<S32, S32> rangeRows(S32 range, Column column) const;
    // Whether a view brackets it: its first lines lined up beside each
    // other -- not one the diff could not keep in order, the SLua of a
    // state_entry written last -- and no other so lined up lying within
    // it on both sides: a statement rather than the block or the function
    // it is in, but a block whose statements were not lined up, an if on
    // one line of LSL written as three of SLua.
    bool                rangeBracketed(S32 range) const { return mBracketed[static_cast<size_t>(range)]; }

    // --- folds -------------------------------------------------------------------

    // Runs folded as they are found, or not; and every run folded so, or
    // opened.
    void              setFoldSame(bool fold);
    bool              foldsSame() const { return mFoldSame; }
    S32               foldCount() const { return static_cast<S32>(mFolds.size()); }
    S32               foldedCount() const;
    bool              foldOpen(S32 fold) const;
    void              setFoldOpen(S32 fold, bool open);
    std::vector<bool> foldsOpen() const;
    // How many lines it hides.
    S32               foldLines(S32 fold) const;
    // Its first row, and its own row, after the lines it hides.
    S32               foldFirst(Layout layout, S32 fold) const;
    S32               foldRow(Layout layout, S32 fold) const;
    // The fold whose own row a row is, or with `lines` whose lines it is
    // one of too; -1 for none.
    S32               foldOfRow(Layout layout, S32 row, bool lines) const;
    // The folded run whose own row is a column's gap above a line, or the
    // gap below the text; -1 for none.
    S32               foldOfGap(Column column, S32 line) const;
    // A column's first line it hides, and the line its own row is the gap
    // above -- one past the last, at the text's end.
    S32               foldFirstLine(Column column, S32 fold) const;
    S32               foldGapLine(Column column, S32 fold) const;

private:
    // One column as it is made: its text, its lines, each row's line and
    // each line's row, and the rows of nothing waiting for its next line.
    struct ColumnData
    {
        std::string       text;
        std::vector<Line> lines;
        std::vector<S32>  lineOf;
        std::vector<S32>  rowOf;
        S32               pending    = 0;
        S32               endPadding = 0;
    };
    struct Change
    {
        S32         first[2] = { 0, 0 };
        S32         end[2]   = { 0, 0 };
        ChangeLines lines;
    };
    // A move's lines as shown, and where its first line taken out and its
    // first put in are inline.
    struct Move
    {
        ALDiffMoves::Move lines;
        S32               inlineLeft  = -1;
        S32               inlineRight = -1;
    };
    struct Fold
    {
        S32  first[2] = { 0, 0 };
        S32  count    = 0;
        bool open     = false;
    };

    static size_t     index(Layout layout) { return layout == Layout::Sides ? 0 : 1; }
    const ColumnData& of(Column column) const { return mColumns[static_cast<size_t>(column)]; }
    ColumnData&       of(Column column) { return mColumns[static_cast<size_t>(column)]; }
    // A line added to a column, a row of nothing that is a row of the gap
    // above its next line, and a row with no line in any gap: each's row.
    S32               add(Column column, const std::string& text, S32 number, Kind kind, char sign = 0);
    S32               pad(Column column);
    S32               none(Column column);
    // Compared again and made again from the texts; the runs as open as
    // given, where there are as many as there were. And made again from
    // the runs as they are.
    void              build(const std::vector<bool>& open = {});
    void              layout(const std::vector<bool>& open = {});
    // The options the texts as shown are compared by: the ranges' anchors,
    // swapped where the texts are.
    ALTextDiff::Options shownOptions() const;
    // Which ranges are bracketed, worked out as each layout is made.
    void              findBracketed();

    std::string           mLeftText;
    std::string           mRightText;
    ALTextDiff::ranges_t  mRanges;
    std::vector<bool>     mBracketed;
    ALTextDiff::Options   mOptions;
    bool                  mSwapped  = false;
    bool                  mFoldSame = true;
    ColumnData            mColumns[3];
    // Each row's line of the right's text, side by side and inline.
    std::vector<S32>      mRightRows[2];
    // Each text's lines, and the runs the texts as shown were last found
    // to have: what a right made anew is compared again from.
    std::vector<std::string>     mLeftLines;
    std::vector<std::string>     mRightLines;
    std::vector<ALTextDiff::Run> mRuns;
    std::vector<Change>   mChanges;
    std::vector<Move>     mMoves;
    std::vector<Fold>     mFolds;
};

#endif // AL_ALDIFFMODEL_H
