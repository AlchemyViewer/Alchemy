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
// its own that cannot be changed, their lines lined up -- a line one side
// has and the other not stands beside an empty one, which has no number --
// and scrolled together; or inline, in one, what was taken out above what
// was put in, numbered as the right. Lines taken out are tinted one colour
// and lines put in another, and within a line changed into another, the
// words that changed are marked. Each side has a title over it, and over
// the titles a bar (ALDiffBar): which change the caret is in, of how many,
// the steps through them, inline or side by side, the sides swapped, and
// done.
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

    // How many changes there are: each run of lines taken out, put in, or
    // both, between lines the same.
    S32 changeCount() const { return static_cast<S32>(mChanges.size()); }
    // The change the caret of the side in front is in, counted from
    // nought; -1 where it is in none.
    S32 changeAtCaret() const;
    // The next change after the line the caret is on, or the one before;
    // the caret put at its first line. False where there is none that way.
    bool goToChange(bool forward);

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
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    void draw() override;

protected:
    friend class LLUICtrlFactory;
    explicit ALDiffView(const Params& p);

private:
    ALCodeEditor* makeSide(const ALCodeEditor::Params& side, const std::string& name);
    void          rebuild();
    void          arrange();
    // The rows of the side in front that stand for the right's lines, each
    // counted from one (nought where a row has none of it); and the side
    // whose rows are of the left's text instead, whose column is no guide.
    const std::vector<S32>& rightRowsOf(const ALCodeEditor* side) const;
    const ALCodeEditor*     notRightSide() const;
    // Where the caret is, to keep across a rebuild that changes the rows:
    // its line of the right's text and its column there, and how far down
    // the view its row is.
    struct Place
    {
        S32 line     = 0;
        S32 column   = 0;
        S32 belowTop = 0;
    };
    Place                   placeOfCaret();
    void                    restorePlace(const Place& place);
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
    // Where each change starts and where it ends, past its last, as lines
    // of what is shown: the same on both sides, which are lined up.
    std::vector<S32>      mChanges;
    std::vector<S32>      mChangeEnds;
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
