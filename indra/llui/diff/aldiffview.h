/**
 * @file aldiffview.h
 * @brief Two texts compared, side by side or inline.
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

#ifndef AL_ALDIFFVIEW_H
#define AL_ALDIFFVIEW_H

#include "alcodeeditor.h"
#include "altextdiff.h"
#include "alviewtype.h"
#include "lluictrl.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ALDiffBar;
class LLTextBox;

// Two texts compared (ALTextDiff). Side by side, each in a code editor of
// its own that cannot be changed and holds its text as it is, their lines
// lined up -- a line one side has and the other not stands beside rows of
// nothing, a gap in the layout (ALTextLayout) -- and scrolled together; or
// inline, in one, what was taken out above what was put in, numbered as
// the right. Lines taken out are tinted one colour and lines put in
// another, and within a line changed into another, the words that changed
// are marked. Runs of lines the same are folded away beyond a few lines of
// context, each to a row of nothing saying how many, which a click, or
// unfolding beside it, opens. Each side has a title over it, and over the
// titles a bar (ALDiffBar): which change the caret is in, of how many,
// the steps through them, folding, inline or side by side, the sides
// swapped, and done.
//
// Its rows are the lines of both sides lined up, a row of nothing where a
// side has no line -- a gap's row, above the next line it has -- and a
// row for each run folded, after the run, drawn while it is folded. A
// change, a fold and the place kept across a rebuild are rows; a side's
// caret and its layout are of its own lines.
//
// F7 or Alt-Down goes to the next change and Shift-F7 or Alt-Up to the one
// before; Escape tells whoever shows it, to put back what was there. What
// is typed in it, which it cannot take, goes to whoever shows it, where it
// says.
class ALDiffView : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALDiffView, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        // The grammar both sides are coloured by.
        Optional<std::string>          syntax;
        Optional<bool>                 inline_view;
        // What each side's editor is made with -- a host's colours and
        // font -- but for its name, its place, and its being read only.
        Optional<ALCodeEditor::Params> side;
        Params();
    };

    ~ALDiffView() override;

    // The texts, the left the one taken from, the right the one made, and
    // what each is; lined up where lines are known to stand for each other
    // (ALTextDiff's anchors), however they differ.
    void setTexts(std::string_view left, std::string_view right, const ALTextDiff::anchors_t& anchors = {});
    // The right made anew -- the text it is of, changed -- and compared
    // again: the caret kept on its line of the right wherever that went,
    // the runs folded as open as they were, and the anchors carried to
    // where their lines of the right now are, changed or not.
    void setRightText(std::string_view right);
    const std::string& leftText() const { return mLeftText; }
    const std::string& rightText() const { return mRightText; }
    void setTitles(const std::string& left, const std::string& right);
    void setSyntax(const std::string& syntax);
    // The grammar both sides are coloured by, as another view has it.
    void setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar);
    void setFont(const LLFontGL* font);
    void setInline(bool inline_view);
    bool isInline() const { return mInline; }
    // The right shown on the left and the left on the right: what was put
    // in shown as taken out, and the other way. The texts are still the
    // ones given as left and right, and an edit still goes to the right's.
    void setSwapped(bool swapped);
    bool isSwapped() const { return mSwapped; }

    // Lines the same folded away: a run of them beyond FOLD_CONTEXT lines
    // either side of a change -- none at the text's start or end -- where
    // that leaves FOLD_LEAST or more, both sides at once, to one row of
    // nothing after it that says how many, which the caret stops on going
    // up or down. Each opened by a click on its row, Return on it,
    // unfolding on a line beside it (ALEditorCommand::Unfold), or the
    // caret landing in it; all of them opened, or folded again, by turning
    // this off or on. On unless asked; never where nothing changed.
    static constexpr S32 FOLD_CONTEXT = 3;
    static constexpr S32 FOLD_LEAST   = 8;
    void setFoldSame(bool fold);
    bool foldsSame() const { return mFoldSame; }
    // Lines told the same with their blanks let go of -- re-indented or
    // re-spaced -- or their case (ALTextDiff::Likeness), and shown as they
    // are; off unless asked. Letting case go is on the bar only where the
    // host offers it: for prose, not code.
    void setIgnoreWhitespace(bool ignore);
    bool ignoresWhitespace() const { return mLike.ignoreWhitespace; }
    void setIgnoreCase(bool ignore);
    bool ignoresCase() const { return mLike.ignoreCase; }
    void setOffersIgnoreCase(bool offers);
    // How many runs there are to fold, and how many are folded.
    S32  foldCount() const { return static_cast<S32>(mFolds.size()); }
    S32  foldedCount() const;

    // How many changes there are: each run of lines taken out, put in, or
    // both, between lines the same.
    S32 changeCount() const { return static_cast<S32>(mChanges.size()); }
    // The change the caret of the side in front is in, counted from
    // nought; -1 where it is in none. Where the side has none of a
    // change's lines, the caret on the line under its gap is in it.
    S32 changeAtCaret() const;
    // The change after the one the caret is in, or the one before; from
    // between changes, the next after its line or the last before. The
    // caret put at its first line, or under its gap. False where there is
    // none that way.
    bool goToChange(bool forward);

    // A change taken back: the right's lines of it made the left's again,
    // an edit of the right's text that whoever shows it makes, told what
    // stretch of the right's text to put what in, and answering whether it
    // did; the comparison then compared again (setRightText). Offered -- an
    // arrow in the gap between the sides at each change, a button on the
    // bar -- only where there is someone to make it.
    typedef std::function<bool(const ALTextRange& range, const std::string& text)> take_back_t;
    void setOnTakeBack(take_back_t take);
    bool canTakeBack() const { return mTakeBack != nullptr; }
    // The change, counted from nought, taken back; false where it was not.
    bool takeBack(S32 change);

    // The side the keyboard is in, else the right, or the one inline:
    // what a find or a copy works on.
    ALCodeEditor* shown() const;
    ALCodeEditor* left() const { return mLeft; }
    ALCodeEditor* right() const { return mRight; }
    ALCodeEditor* inlined() const { return mInlined; }
    ALDiffBar*    bar() const { return mBar; }

    // Told when Escape is pressed in it, or its bar's done; without it,
    // the bar has no done.
    void setOnEscape(std::function<void()> escape);
    // Told when its bar turns it inline or side by side, as it now is.
    void setOnInline(std::function<void(bool)> inlined) { mOnInline = std::move(inlined); }
    // Told when what is typed in it would change the right's text -- a
    // character, a line broken or joined, a paste -- with where, as a line
    // and column of the right's text; answered with the view to type in
    // instead, or nothing where the typing goes nowhere.
    typedef std::function<LLView*(S32 line, S32 column)> edit_t;
    void setOnEdit(edit_t edit) { mEdit = std::move(edit); }
    // The line of the right's text the caret of the side in front is on,
    // and its column there: the row's, or where the row has none -- a
    // line taken out, a gap -- the nearest line after it, else before.
    // The text given as the right, wherever it is shown.
    std::pair<S32, S32> rightAtCaret() const;

    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    void draw() override;

protected:
    friend class LLUICtrlFactory;
    explicit ALDiffView(const Params& p);

private:
    ALCodeEditor* makeSide(const ALCodeEditor::Params& side, const std::string& name);
    // Made again from the texts; the folds as open as given, where there
    // are as many as there were.
    void          rebuild(const std::vector<bool>& open = {});
    void          arrange();
    // The rows of the side in front that stand for the right's lines, each
    // counted from one (nought where a row has none of it); and the side
    // whose rows are of the left's text instead, whose column is no guide.
    const std::vector<S32>& rightRowsOf(const ALCodeEditor* side) const;
    const ALCodeEditor*     notRightSide() const;
    // Where the caret is, to keep across a rebuild that changes the rows:
    // its line of the right's text and its column there, and how far down
    // the view its line is.
    struct Place
    {
        S32 line     = 0;
        S32 column   = 0;
        S32 belowTop = 0;
    };
    Place                   placeOfCaret();
    // Made again as lines are now told the same, the caret kept.
    void                    rebuildLikeness();
    void                    restorePlace(const Place& place);
    // A run of lines the same, folded away or not: its first row, side by
    // side and inline, and how many; its own row is the one after it.
    struct Fold
    {
        S32  first       = 0;
        S32  inlineFirst = 0;
        S32  count       = 0;
        bool open        = false;
    };
    // Each change's lines in the texts as given, swapped or not: where
    // they start on the left and on the right, and how many each has.
    struct ChangeLines
    {
        S32 leftFirst  = 0;
        S32 leftCount  = 0;
        S32 rightFirst = 0;
        S32 rightCount = 0;
    };
    // The change a row of what is shown is in; -1 for none.
    S32               changeOfRow(S32 row) const;
    // The change a line of a side is in, or is under the gap of, as the
    // caret's is (changeAtCaret); -1 for none.
    S32               changeOfLine(const ALCodeEditor* side, S32 line) const;
    // The change a step from the caret goes to (goToChange); -1 for none.
    S32               changeStep(bool forward) const;
    // Each row's line of the text an editor shows -- the left's or the
    // right's, or the one inline -- or -1 for a row of nothing there,
    // which is in the gap above the next line that has one; and each
    // line's row.
    struct Lines
    {
        std::vector<S32> lineOf;
        std::vector<S32> rowOf;
    };
    const Lines&      linesOf(const ALCodeEditor* side) const;
    S32               rowOfLine(const ALCodeEditor* side, S32 line) const;
    // A row's line, or the line after it where it has none; one past the
    // last past them all, which is the gap below the text.
    S32               lineBelowRow(const ALCodeEditor* side, S32 row) const;
    // Where the caret goes for a row: its line, or the line under its
    // gap, or at the text's end the last.
    S32               caretLineOfRow(const ALCodeEditor* side, S32 row) const;
    // A row's top down a side's text: its line's, or as far down the gap
    // it is in as there are rows of it drawn before it. Past the last
    // row, the bottom of the text.
    S32               topOfRow(ALCodeEditor* side, S32 row) const;
    // Whether a row of nothing is drawn: a fold's own row only while its
    // run is folded.
    bool              rowDrawn(const ALCodeEditor* side, S32 row) const;
    // Whether a side has any line among a change's rows.
    bool              hasLinesIn(const ALCodeEditor* side, S32 change) const;
    // The gap between the sides: wider where it holds the arrows that take
    // a change back.
    S32               gap() const;
    // The change whose arrow is under a point of the view; -1 for none.
    S32               arrowAtPoint(S32 x, S32 y);
    void              drawArrows();
    // Each side's lines hidden and shown as the folds are, each folded
    // run's row a gap above the line after it, and a caret on a line
    // hidden put beside the run.
    void              applyFolds();
    // The fold whose own row a row of a side is, or with `lines` whose
    // lines it is one of too; -1 for none.
    S32               foldOfRow(const ALCodeEditor* side, S32 row, bool lines) const;
    // The folded run whose own row is a side's gap above a line, or the
    // one below the text; -1 for none.
    S32               foldOfGap(const ALCodeEditor* side, S32 line) const;
    S32               firstOfFold(const ALCodeEditor* side, S32 fold) const;
    S32               rowOfFold(const ALCodeEditor* side, S32 fold) const;
    void              openFold(S32 fold);
    std::vector<bool> foldsOpen() const;
    // The side under a point of the view and the fold whose row is there,
    // folded; -1 for none.
    S32               foldAtPoint(S32 x, S32 y, ALCodeEditor** side = nullptr);
    void              drawFoldRows();
    // A line round the change the caret is in, on each side shown.
    void              drawCurrentChange();

    // The bar's count and steps, as the caret of the side in front has them.
    void                    refreshBar();
    // A step from the bar: the keyboard given to the side in front.
    void                    stepFromBar(bool forward);

    std::string           mLeftText;
    std::string           mRightText;
    ALTextDiff::anchors_t mAnchors;
    // Each row's line of what is shown on the left and on the right, side
    // by side and inline, counted from one; nought where the row has none.
    std::vector<S32>      mLeftRows;
    std::vector<S32>      mRightRows;
    std::vector<S32>      mInlineLeftRows;
    std::vector<S32>      mInlineRows;
    // Each editor's lines by row, and rows by line.
    Lines                 mLeftLines;
    Lines                 mRightLines;
    Lines                 mInlineLines;
    std::string           mLeftTitle;
    std::string           mRightTitle;
    bool                  mInline  = false;
    bool                  mSwapped = false;
    ALCodeEditor*         mLeft    = nullptr;
    ALCodeEditor*         mRight   = nullptr;
    ALCodeEditor*         mInlined = nullptr;
    ALDiffBar*            mBar       = nullptr;
    LLTextBox*            mLeftHead  = nullptr;
    LLTextBox*            mRightHead = nullptr;
    // Where each change starts and where it ends, past its last, as rows
    // of what is shown: the same on both sides, which are lined up.
    std::vector<S32>      mChanges;
    std::vector<S32>      mChangeEnds;
    std::vector<ChangeLines> mChangeLines;
    take_back_t           mTakeBack;
    // The change whose arrow the mouse is over, as last drawn.
    S32                   mArrowHover = -1;
    std::vector<Fold>     mFolds;
    bool                  mFoldSame = true;
    ALTextDiff::Likeness  mLike;
    // Where the two sides were last scrolled to, to follow the one moved.
    S32                   mScrolledY = 0;
    F32                   mScrolledX = 0.f;
    // The colour table's generation the bar was last coloured for.
    U32                   mBarColors = U32_MAX;
    std::function<void()> mEscape;
    std::function<void(bool)> mOnInline;
    edit_t                mEdit;
};

#endif // AL_ALDIFFVIEW_H
