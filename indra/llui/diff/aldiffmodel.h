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

#include "aldiffmerge.h"
#include "aldiffmoves.h"
#include "aldiffsplice.h"
#include "alstructuraldiff.h"
#include "altextdiff.h"
#include "altextdocument.h"

#include <optional>
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

    // Where each line of the right as it was went when it was made anew.
    typedef ALDiffSplice::LineMap LineMap;

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
    // The left made another -- another version of it -- and compared again
    // as the right made anew is, the runs as open as they were: what stood
    // for what, what was said of the left and a merge with it, which were
    // the other's, let go of.
    void    setLeftText(std::string_view left);
    // Stretches of the texts as given that stand for each other by what
    // they are -- a function, an event or a state of the same name on each
    // side -- by their first and last lines: the texts lined up there, each
    // beside its other, so that what changed in one is compared with what
    // it was and not with another's lines alike. Said of nothing else: no
    // band, no link. Carried with their lines as either text is made
    // anew; let go of with new texts; not used where there are ranges,
    // which line the texts up more finely. False where nothing changed.
    bool                        setPairs(ALTextDiff::ranges_t pairs);
    const ALTextDiff::ranges_t& pairs() const { return mPairs; }
    // What the last rebuild laid out again, by column: all of it; or the
    // lines from `first`, `now` of them in place of `was`, and those after
    // moved along, their numbers by `numbered` and the first of them with
    // the rows of nothing now waiting above it -- a layout made again after
    // a splice keeps the groups of runs before the change and moves those
    // after it, rather than laying out every row: what a view fills again.
    struct Relaid
    {
        bool whole       = true;
        S32  first[3]    = { 0, 0, 0 };
        S32  was[3]      = { 0, 0, 0 };
        S32  now[3]      = { 0, 0, 0 };
        S32  numbered[3] = { 0, 0, 0 };
    };
    const Relaid& relaid() const { return mRelaid; }
    // How many layouts have been made: relaid() is of the last, from the
    // one before.
    U32           layouts() const { return mLayouts; }
    // Whether a rebuild after a splice keeps what it can of the one before
    // -- the layout, by structure the changes read as tokens, and the
    // lines' ids the moves are found by; on unless asked. Off, every row is
    // laid out, every change read and every line keyed again: what a test
    // holds what is kept to.
    void          setKeepsLayout(bool keeps) { mKeepsLayout = keeps; }
    // Swapped or not: the runs as open as they were.
    void    setSwapped(bool swapped);
    // How lines are told the same: the runs folded as foldsSame() says,
    // since they are others now.
    void    setLikeness(const ALTextDiff::Likeness& like);
    // Words that mean the same in the two texts (ALDiffSame), the whole
    // comparison's; a range may have its own besides.
    void    setSame(ALTextDiff::same_t same);
    // How the lines that stay are chosen (ALTextDiff::Algorithm). By
    // structure, a changed line's words are its tokens not kept, and a
    // change of nothing but formatting is none; a change too large for it
    // is compared as lines are, which fellBack() says.
    void    setAlgorithm(ALTextDiff::Algorithm algorithm);
    bool    fellBack() const { return mFellBack; }
    // How lines are cut into words -- a grammar's tokens, or none for their
    // bytes alone -- and so which lines of a change pair and what is
    // marked in them. Without one nothing says where a comment is: none
    // let go of. And the merge's, a lexer of its own over the same grammar
    // (mergeOptions): a lexer holds the last two texts it read, which are
    // the comparison's, and a merge reads three. Without it, the merge
    // reads by the comparison's.
    void    setLexer(ALTextDiff::lexer_t lexer, ALTextDiff::lexer_t merging = ALTextDiff::lexer_t());

    // The texts as given, their line endings LF as an editor reads them.
    const std::string&           leftText() const { return mLeftText; }
    const std::string&           rightText() const { return mRightText; }
    const ALTextDiff::ranges_t&  ranges() const { return mRanges; }
    const ALTextDiff::Likeness&  likeness() const { return mOptions.like; }
    // How the texts are compared, but for the anchors, which are the
    // ranges'.
    const ALTextDiff::Options&   options() const { return mOptions; }
    bool                         swapped() const { return mSwapped; }

    // --- a column's lines --------------------------------------------------

    // Its lines joined: side by side, the whole text it shows, as given;
    // inline, the lines of both as they are laid out.
    std::string_view text(Column column) const
    {
        if (column == Column::Inline)
        {
            // Each line kept ended by its break: the last's is not the text's.
            return std::string_view(mInlineText).substr(0, mInlineText.empty() ? 0 : mInlineText.size() - 1);
        }
        return (column == Column::Left) != mSwapped ? mLeftText : mRightText;
    }
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
    // How many rows drawn there are from a row to the line below it, or to
    // the text's end: how far up the gap above that line the row is.
    // Nought for a row with a line.
    S32  gapRowsFrom(Column column, S32 row) const;
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
    // The change a row is in; -1 for none. And the first change whose
    // first row is a row or after it, the count past them all: where a view
    // drawing the rows in sight starts.
    S32                changeOfRow(Layout layout, S32 row) const;
    S32                changeFrom(Layout layout, S32 row) const;
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
    // A change's lines in the text given as the left, or as the right,
    // each ended by a line break: what a copy of it takes. None where it
    // has none there.
    std::string        changeText(S32 change, bool given_left) const;

    // --- a merge -----------------------------------------------------------------

    // The left saved elsewhere and the right made here, each from a text
    // both were (the base): merged as the right changes (ALDiffMerge), and
    // each change that has a line in a conflict of it said so. Given after
    // the texts, which let it go; nothing for none.
    void                                 setMergeBase(std::optional<std::string_view> base);
    bool                                 merging() const { return mMerge.has_value(); }
    // How a merge is found, and begun (ALDiffMerge::start): as the texts
    // are compared, read by the merge's own lexer where it was given one.
    ALTextDiff::Options                  mergeOptions() const;
    // How many conflicts there are, and whether a change is in one.
    S32                                  conflictCount() const;
    bool                                 changeConflicts(S32 change) const;
    // The conflicts a change is in settled (ALDiffMerge::settle); nothing
    // where it is in none.
    std::optional<ALDiffMerge::Settling> settle(S32 change, ALTextMerge::Take take) const;
    // A settling kept (ALDiffMerge::settled) before the edit it makes, and
    // the conflicts found again: the right made anew after, where it makes
    // one, finds the merge again then.
    void                                 settled(const ALDiffMerge::Settling& settling);

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
    // The narrowest range a line of a column side by side is in -- of the
    // text the column shows; of two as narrow, the wider on the other side,
    // which says all the line became: an LSL line holding an if and the
    // call in it, the if's SLua -- -1 for none, and inline: what both sides
    // outline when the caret is on it.
    S32                 rangeAt(Column column, S32 line) const;

    // --- notes -------------------------------------------------------------------

    // Words about a line of the text given as the left, said beside it
    // wherever it is shown -- the converter's notes beside the LSL they
    // are about. A new text lets them go; the right made anew keeps them.
    struct Note
    {
        S32         line = 0;
        std::string text;
        std::string tip;
    };
    void                     setNotes(std::vector<Note> notes);
    const std::vector<Note>& notes() const { return mNotes; }
    // The line of a column showing a line of the text given as the left,
    // or as the right; -1 where it shows none.
    S32                      lineShowing(Column column, bool given_left, S32 line) const;
    // The notes on a column's lines, each at its line there, those on one
    // line said together.
    std::vector<Note>        notesIn(Column column) const;

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
    // The first fold whose own row is a row or after it; the count past
    // them all.
    S32               foldFrom(Layout layout, S32 row) const;
    // The folded run whose own row is a column's gap above a line, or the
    // gap below the text; -1 for none.
    S32               foldOfGap(Column column, S32 line) const;
    // A column's first line it hides, and the line its own row is the gap
    // above -- one past the last, at the text's end.
    S32               foldFirstLine(Column column, S32 fold) const;
    S32               foldGapLine(Column column, S32 fold) const;

private:
    // One column as it is made: its lines, each row's line and each line's
    // row, and the rows of nothing waiting for its next line. Cleared, not
    // made anew, as the comparison is laid out again: what it holds is as
    // long as it was.
    struct ColumnData
    {
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
    // The texts as shown on the left and on the right: as given, or
    // swapped.
    const std::vector<std::string>& shownLeft() const { return mSwapped ? mRightLines : mLeftLines; }
    const std::vector<std::string>& shownRight() const { return mSwapped ? mLeftLines : mRightLines; }
    // Each line's regions of the texts as shown, where a grammar cuts their
    // words and answers a line each; none else. Asked of the lexer once a
    // rebuild, which reads and lays out by them as many times as it needs:
    // a text it holds is compared whole to be known again.
    typedef const std::vector<ALTextDiff::regions_t>* line_regions_t;
    std::pair<line_regions_t, line_regions_t> shownRegions() const;
    const ColumnData& of(Column column) const { return mColumns[static_cast<size_t>(column)]; }
    ColumnData&       of(Column column) { return mColumns[static_cast<size_t>(column)]; }
    // A line added to a column, a row of nothing that is a row of the gap
    // above its next line, and a row with no line in any gap: each's row.
    S32               add(Column column, const std::string& text, S32 number, Kind kind, char sign = 0);
    S32               pad(Column column);
    S32               none(Column column);
    // What a layout made again after a splice may keep of the one before:
    // the runs it was made from; how far the lines after the change moved,
    // each side as shown; and the side changed, as shown, its lines from
    // `head` to all but its last `tail` changed, of `lines` before.
    struct Relayout
    {
        std::vector<ALTextDiff::Run> runs;
        S32                          left  = 0;
        S32                          right = 0;
        size_t                       side  = 0;
        S32                          head  = 0;
        S32                          tail  = 0;
        S32                          lines = 0;
    };
    // Where each group of runs -- a run the same, or the runs of a change --
    // began to be laid out, and where the last ended: the rows of each
    // layout, the lines and the rows of nothing waiting of each column, and
    // the changes, folds and inline text made before it.
    struct Mark
    {
        size_t run        = 0;
        S32    rows[2]    = { 0, 0 };
        S32    lines[3]   = { 0, 0, 0 };
        S32    pending[3] = { 0, 0, 0 };
        size_t changes    = 0;
        size_t folds      = 0;
        size_t text       = 0;
    };
    // What of the layout before stands: the groups [0, kept) as they were,
    // those from `moved` on moved along, and the runs [from, to) between
    // them laid out again.
    struct Reuse
    {
        size_t kept  = 0;
        size_t moved = 0;
        size_t from  = 0;
        size_t to    = 0;
    };
    // Where the layout before may be kept, after a splice: the runs before
    // the change as they were and those after it moved along, at the edges
    // of groups, and none of their lines among those changed -- a line
    // edited in a change leaves its runs as they were; the last change where
    // it was, before or after the change; and the blocks moved those there
    // were, outside the change. Nothing where all of it is to be laid out
    // again.
    std::optional<Reuse> reusable(const Relayout& again, const ALDiffMoves::moves_t& moves, size_t last_change) const;
    // Compared again and made again from the texts; the runs as open as
    // given, where there are as many as there were. And made again from
    // the runs as they are, by the options shown (worked out once a
    // rebuild): after a splice, only where it must be.
    void              build(const std::vector<bool>& open = {});
    void              layout(const ALTextDiff::Options& options, const std::vector<bool>& open = {}, const Relayout* again = nullptr);
    // A side's lines made anew, its text already so: compared again only
    // where it changed (ALDiffSplice) where that is enough, and laid out
    // again only there -- there taking in the lines after it that now read
    // otherwise by its grammar, as far as the last of them.
    void              resplice(bool given_left, std::vector<std::string> lines, const ALDiffEdit::Edges& edges);
    // The rows' lines of the right's text: side by side the column showing
    // it, inline the right's as shown, or swapped the left's.
    const std::vector<S32>& rightRows(Layout layout) const { return layout == Layout::Sides ? of(rightColumn()).lineOf : mInlineRows[mSwapped ? 0 : 1]; }
    // By structure, the runs' changes read as tokens (ALStructuralDiff):
    // after an edit, given the runs before it, only the changes that are
    // not as they were.
    void              readTokens(const std::vector<ALTextDiff::Run>* was = nullptr, const ALStructuralDiff::Edited* edited = nullptr);
    // The lines of a side's changes from a line on, or with `every` each
    // of its lines from it, each with its regions as read (by a hash of
    // them): what an edit before them may have made read otherwise -- a
    // block comment opened or closed -- which cuts their words and tokens,
    // and where lines are told the same by their regions, which of them
    // are the same.
    std::vector<std::pair<S32, size_t>> readFrom(size_t side, S32 from, bool every = false) const;
    // Each pair's or range's lines on one side, where they now are: those
    // of a pair or range whose first line went taken back.
    static ALTextDiff::ranges_t carried(const ALTextDiff::ranges_t& ranges, bool left, S32 was, const LineMap& map);
    // The options the texts as shown are compared by: the ranges' anchors,
    // or the pairs', swapped where the texts are.
    ALTextDiff::Options shownOptions() const;
    // Which ranges are bracketed, worked out as each layout is made.
    void              findBracketed();
    // The ranges in order of their first lines on each side, worked out as
    // each layout is made: what rangeAt finds a line's by halves.
    void              orderRanges();
    // The first line of the right each open run hides, in order -- carried
    // where the right was made anew -- and the runs hiding those opened
    // again after a rebuild: a run the same to the reader, though the runs
    // are others.
    std::vector<S32>  openedLines(const LineMap* map = nullptr) const;
    void              reopen(const std::vector<S32>& opened);
    // Which changes are in a conflict of the merge, worked out as each
    // layout is made and each conflict settled.
    void              findConflicts();
    // After a merge is found by the comparison's lexer, where it has none
    // of its own, which may hold the merge's texts now in the place of
    // those shown: the regions of those shown read again, while their
    // lines are whole -- an edit takes the lines it keeps out of the side
    // as it was before it reads that side's regions.
    void              mergeRead();
    // The merge, where there is one, found again by the options as they now
    // are (mergeOptions): what lines are told the same by, how the lines
    // that stay are chosen, and the grammar, each as it is set.
    void              refreshMerge();

    std::string           mLeftText;
    std::string           mRightText;
    ALTextDiff::ranges_t  mRanges;
    ALTextDiff::ranges_t  mPairs;
    // The search for blocks moved, which keeps each line's id between
    // rebuilds, told of every edit.
    ALDiffMoves::Finder   mMoveFinder;
    std::vector<bool>     mBracketed;
    // The ranges by their first lines of the text given as the left and as
    // the right, and the furthest last line among those up to each.
    std::vector<S32>      mRangeOrder[2];
    std::vector<S32>      mRangeReach[2];
    ALTextDiff::Options   mOptions;
    // The lexer a merge reads by, where it has one of its own.
    ALTextDiff::lexer_t   mMergeLexer;
    bool                  mSwapped  = false;
    bool                  mFoldSame = true;
    ColumnData            mColumns[3];
    // The inline column's lines, each ended by a line break, so that a
    // stretch of them moves along as it is; a side's is the text it shows.
    std::string           mInlineText;
    // The inline line showing each line of the left as shown, and of the
    // right; -1 for none.
    std::vector<S32>      mInlineOf[2];
    // Each inline row's line of the left and of the right as shown; -1 for
    // none.
    std::vector<S32>      mInlineRows[2];
    // Where each group of runs began to be laid out, and the last ended;
    // the run the last change ends at, the runs' count where none is; and
    // what the last rebuild laid out again.
    std::vector<Mark>     mGroups;
    size_t                mLastChange = 0;
    Relaid                mRelaid;
    U32                   mLayouts = 0;
    bool                  mKeepsLayout = true;
    std::vector<Note>     mNotes;
    // Each text's lines, and the runs the texts as shown were last found
    // to have: what a right made anew is compared again from.
    std::vector<std::string>     mLeftLines;
    std::vector<std::string>     mRightLines;
    std::vector<ALTextDiff::Run> mRuns;
    // Compared by structure: each line's tokens of the left and the right
    // as shown not kept, and whether its change was read so; and whether a
    // change was too large to be.
    std::vector<ALTextDiff::spans_t> mMarks[2];
    std::vector<bool>                mByTokens[2];
    bool                             mFellBack = false;
    std::vector<Change>   mChanges;
    // The merge, where there is one, and whether each change is in a
    // conflict of it.
    std::optional<ALDiffMerge> mMerge;
    std::vector<bool>     mConflicted;
    std::vector<Move>     mMoves;
    std::vector<Fold>     mFolds;
    // The regions asked for this rebuild, let go of as the next begins, and
    // read again as a merge reading by the same lexer is found.
    mutable std::optional<std::pair<line_regions_t, line_regions_t>> mRegions;
};

#endif // AL_ALDIFFMODEL_H
