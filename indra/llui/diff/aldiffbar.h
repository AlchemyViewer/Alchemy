/**
 * @file aldiffbar.h
 * @brief The bar over a comparison: where the changes are, and the ways through them.
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

#ifndef AL_ALDIFFBAR_H
#define AL_ALDIFFBAR_H

#include "almenuslot.h"
#include "alviewtype.h"
#include "lluicolor.h"
#include "llpanel.h"

#include <boost/signals2.hpp>

#include <functional>
#include <string>

class ALFlatButton;
class LLTextBox;

// The thin bar over a comparison's titles (ALDiffView): which change the
// caret is in, of how many, and the arrows to the one before and the next;
// the change taken back, where whoever shows it can; what is the same
// folded away, what is let go of in telling lines the same -- a menu, lit
// while anything is -- the comparison inline or side by side, its sides
// swapped; and done, back to what it was made from. In the comparison's colours, as
// the find bar is in its view's.
//
// It holds what it shows and nothing else: the view tells it the count
// and the state of its toggles, and it says what was pressed.
class ALDiffBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALDiffBar, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Params();
    };

    // The change the caret is in, counted from nought, or -1 where it is
    // in none; of how many.
    void        setCount(S32 current, S32 total);
    // Whether the comparison, asked to compare by structure, compared a
    // change too large for it by lines instead: said after the count.
    void        setFellBack(bool fell_back);
    std::string countSaid() const;
    // Whether there is a change before the caret, and one after it.
    void        setSteps(bool previous, bool next);
    void        setFolded(bool folded);
    // Whether anything is let go of in telling lines the same; and what
    // the menu under the blanks button asks of the view: whether each, by
    // its name, is let go of and is offered, and turning one.
    void        setIgnoring(bool ignoring);
    struct Ignores
    {
        std::function<bool(const std::string&)> checked;
        std::function<bool(const std::string&)> offered;
        std::function<void(const std::string&)> toggle;
    };
    void        setIgnores(Ignores ignores) { mIgnores = std::move(ignores); }
    void        setInline(bool inline_view);
    void        setSwapped(bool swapped);
    // Whether there is anywhere to go back to.
    void        setDoneShown(bool shown);
    // Whether a change can be taken back at all, and the caret's now.
    void        setTakeBackShown(bool shown);
    void        setTakeBackEnabled(bool enabled);

    // The colours of the comparison it is over: its glyphs and its count
    // in the sides' ink, its ground a shade off their paper.
    void setColors(const LLColor4& background, const LLColor4& ink);

    // The height the bar wants.
    static S32 wantedHeight();

    typedef boost::signals2::signal<void()> signal_t;
    boost::signals2::connection onPrevious(const signal_t::slot_type& cb) { return mPrevious.connect(cb); }
    boost::signals2::connection onNext(const signal_t::slot_type& cb) { return mNext.connect(cb); }
    boost::signals2::connection onFold(const signal_t::slot_type& cb) { return mFold.connect(cb); }
    boost::signals2::connection onInline(const signal_t::slot_type& cb) { return mInline.connect(cb); }
    boost::signals2::connection onSwap(const signal_t::slot_type& cb) { return mSwap.connect(cb); }
    boost::signals2::connection onDone(const signal_t::slot_type& cb) { return mDone.connect(cb); }
    boost::signals2::connection onTakeBack(const signal_t::slot_type& cb) { return mTakeBack.connect(cb); }

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALDiffBar(const Params& p);

private:
    ALFlatButton* flat(const std::string& name, const std::string& glyph, bool toggle, const std::string& tip);
    void          layout();
    // The menu of what to let go of (menu_diff_ignore.xml), under its
    // button.
    void          showIgnoreMenu();

    LLUIColor     mBgColor;
    LLUIColor     mInkColor;
    S32           mCurrent = -2;
    S32           mTotal   = -1;
    bool          mFellBack = false;
    Ignores       mIgnores;
    ALMenuSlot    mIgnoreMenu;

    LLTextBox*    mCount          = nullptr;
    ALFlatButton* mPreviousButton = nullptr;
    ALFlatButton* mNextButton     = nullptr;
    ALFlatButton* mFoldButton     = nullptr;
    ALFlatButton* mIgnoreButton   = nullptr;
    ALFlatButton* mInlineButton   = nullptr;
    ALFlatButton* mSwapButton     = nullptr;
    ALFlatButton* mDoneButton     = nullptr;
    ALFlatButton* mTakeBackButton = nullptr;

    signal_t mPrevious, mNext, mFold, mInline, mSwap, mDone, mTakeBack;
};

#endif // AL_ALDIFFBAR_H
