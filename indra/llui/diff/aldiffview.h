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
#include "aldiffmodel.h"
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

// Two texts compared (ALTextDiff), as ALDiffModel lays them out. Side by
// side, each in a code editor of its own that cannot be changed and holds
// its text as it is, their lines lined up -- a line one side has and the
// other not stands beside rows of nothing, a gap in the layout
// (ALTextLayout) -- and scrolled together; or inline, in one, what was
// taken out above what was put in, numbered as the right. Lines taken out
// are tinted one colour and lines put in another, and within a line
// changed into another, the words that changed are marked. Runs of lines
// the same are folded away beyond a few lines of context, each to a row
// of nothing saying how many, which a click, Return on it, or unfolding
// beside it opens. Each side has a title over it, and over the titles a
// bar (ALDiffBar): which change the caret is in, of how many, the steps
// through them, folding, inline or side by side, the sides swapped, and
// done.
//
// What is shown, row by row, and every lookup between rows, lines, changes
// and folds is the model's; this fills an editor for each of its columns,
// draws what is over them, and takes the keys and the mouse. A change, a
// fold and the place kept across a rebuild are rows; a side's caret and
// its layout are of its own lines.
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
    // what each is; lined up where stretches of them are known to stand for
    // each other (ALTextDiff's ranges), however they differ, and side by
    // side each range lined up bracketed to its other across the gap -- an
    // LSL statement to the SLua lines written of it.
    void setTexts(std::string_view left, std::string_view right, const ALTextDiff::ranges_t& ranges = {});
    // The right made anew -- the text it is of, changed -- and compared
    // again: the caret kept on its line of the right wherever that went,
    // the runs folded as open as they were, and the anchors carried to
    // where their lines of the right now are, changed or not.
    void setRightText(std::string_view right);
    const std::string& leftText() const { return mModel.leftText(); }
    const std::string& rightText() const { return mModel.rightText(); }
    // What is compared, laid out: what a host or a test reads of it.
    const ALDiffModel& model() const { return mModel; }
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
    bool isSwapped() const { return mModel.swapped(); }

    // Lines the same folded away: a run of them beyond FOLD_CONTEXT lines
    // either side of a change -- none at the text's start or end -- where
    // that leaves FOLD_LEAST or more, both sides at once, to one row of
    // nothing after it that says how many, which the caret stops on going
    // up or down. Each opened by a click on its row, Return on it,
    // unfolding on a line beside it (ALEditorCommand::Unfold), or the
    // caret landing in it; all of them opened, or folded again, by turning
    // this off or on. On unless asked; never where nothing changed.
    static constexpr S32 FOLD_CONTEXT = ALDiffModel::FOLD_CONTEXT;
    static constexpr S32 FOLD_LEAST   = ALDiffModel::FOLD_LEAST;
    void setFoldSame(bool fold);
    bool foldsSame() const { return mModel.foldsSame(); }
    // Lines told the same with their blanks let go of -- re-indented or
    // re-spaced -- or their case (ALTextDiff::Likeness), and shown as they
    // are; off unless asked. Letting case go is on the bar only where the
    // host offers it: for prose, not code.
    void setIgnoreWhitespace(bool ignore);
    bool ignoresWhitespace() const { return mModel.likeness().ignoreWhitespace; }
    void setIgnoreCase(bool ignore);
    bool ignoresCase() const { return mModel.likeness().ignoreCase; }
    void setOffersIgnoreCase(bool offers);
    // Words that mean the same in the two texts (ALDiffSame) -- an LSL
    // function and the SLua it became -- left unmarked; compared again.
    void                  setSame(ALTextDiff::same_t same);
    // How the lines that stay are chosen (ALTextDiff::Algorithm), compared
    // again, the caret kept; Histogram unless asked.
    void                  setAlgorithm(ALTextDiff::Algorithm algorithm);
    ALTextDiff::Algorithm algorithm() const { return mModel.options().algorithm; }
    // How many runs there are to fold, and how many are folded.
    S32  foldCount() const { return mModel.foldCount(); }
    S32  foldedCount() const { return mModel.foldedCount(); }

    // How many changes there are: each run of lines taken out, put in, or
    // both, between lines the same.
    S32 changeCount() const { return mModel.changeCount(); }
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
    typedef ALDiffModel::Column Column;
    typedef ALDiffModel::Layout Layout;

    ALCodeEditor* makeSide(const ALCodeEditor::Params& side, const std::string& name);
    // Each editor filled from the model -- its text, what is said of each
    // line, the words marked -- and the folds applied.
    void          fill();
    void          arrange();
    // The column of the model an editor shows, and the rows it is in.
    Column        columnOf(const ALCodeEditor* side) const;
    Layout        layoutOf(const ALCodeEditor* side) const { return ALDiffModel::layoutOf(columnOf(side)); }
    // Where the caret is, to keep across a rebuild that changes the rows:
    // its line of the right's text and its column there, and how far down
    // the view its line is.
    struct Place
    {
        S32 line     = 0;
        S32 column   = 0;
        S32 belowTop = 0;
    };
    Place         placeOfCaret();
    void          restorePlace(const Place& place);
    // Compared again as lines are now told the same, the caret kept.
    void          setLikeness(const ALTextDiff::Likeness& like);
    // Words cut by a grammar's tokens, where it is one of code; compared
    // again, the caret kept.
    void          compareBy(const std::shared_ptr<const ALSyntaxGrammar>& grammar);
    // A row's top down a side's text: its line's, or as far down the gap
    // it is in as there are rows of it drawn before it. Past the last
    // row, the bottom of the text.
    S32           topOfRow(ALCodeEditor* side, S32 row) const;
    // The gap between the sides: wider where it holds the arrows that take
    // a change back, or bands between ranges.
    S32           gap() const;
    // Each range the model brackets, in sight, joined to its other across
    // the gap: a band from the rows it has on the left to those on the
    // right; but where those are one row each and level, which says
    // nothing the rows beside each other do not.
    void          drawRanges();
    // The change whose arrow is under a point of the view; -1 for none.
    S32           arrowAtPoint(S32 x, S32 y);
    void          drawArrows();
    // Each side's lines hidden and shown as the folds are, each folded
    // run's row a gap above the line after it, which the caret stops on,
    // and a caret on a line hidden put beside the run.
    void          applyFolds();
    void          openFold(S32 fold);
    // The side under a point of the view and the fold whose row is there,
    // folded; -1 for none.
    S32           foldAtPoint(S32 x, S32 y, ALCodeEditor** side = nullptr);
    void          drawFoldRows();
    // A line round the change the caret is in, on each side shown.
    void          drawCurrentChange();

    // The bar's count and steps, as the caret of the side in front has
    // them: told when a caret moves or the keyboard goes from one side to
    // the other, not looked at each frame.
    void          refreshBar();
    // Side by side, the other side scrolled where one is, there and then.
    void          followScroll(ALCodeEditor* from);
    // The colours drawn each frame, and the bar's, taken again only when
    // the colour table changes.
    void          refreshColors();
    // A step from the bar: the keyboard given to the side in front.
    void          stepFromBar(bool forward);

    ALDiffModel               mModel;
    std::string               mLeftTitle;
    std::string               mRightTitle;
    bool                      mInline    = false;
    ALCodeEditor*             mLeft      = nullptr;
    ALCodeEditor*             mRight     = nullptr;
    ALCodeEditor*             mInlined   = nullptr;
    ALDiffBar*                mBar       = nullptr;
    LLTextBox*                mLeftHead  = nullptr;
    LLTextBox*                mRightHead = nullptr;
    take_back_t               mTakeBack;
    // The change whose arrow the mouse is over, as last drawn.
    S32                       mArrowHover = -1;
    // A side scrolled to follow the other, which does not lead it back.
    bool                      mFollowing = false;
    // The colour table's generation the colours were last taken for, and
    // those drawn each frame.
    U32                       mColorsGeneration = U32_MAX;
    LLColor4                  mCurrentColor;
    LLColor4                  mDividerColor;
    // The grammar words are cut by: none for prose.
    std::shared_ptr<const ALSyntaxGrammar> mLexedBy;
    // What each folded row says, and the ranges drawn as bands, worked out
    // as the comparison is filled.
    std::vector<std::string>  mFoldSaid;
    std::vector<S32>          mBands;
    // The sides' signals, let go of before the sides are: a side losing
    // the keyboard as it goes would tell a comparison already gone.
    std::vector<boost::signals2::scoped_connection> mConnections;
    std::function<void()>     mEscape;
    std::function<void(bool)> mOnInline;
    edit_t                    mEdit;
};

#endif // AL_ALDIFFVIEW_H
