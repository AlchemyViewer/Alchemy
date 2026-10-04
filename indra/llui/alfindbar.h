/**
 * @file alfindbar.h
 * @brief The find and replace bar of a text view.
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

#pragma once

#include "altextsearch.h"
#include "llpanel.h"

#include <boost/signals2.hpp>

#include <string>

class ALFlatButton;
class LLLineEditor;
class LLTextBox;

// The bar over a text view's top right corner, as the modern editors put
// it: a field for what to find with its three ways of matching inside it,
// the count of what was found and the arrows through them, a toggle for
// the selection alone, and a way out; unfolded, a field for what to put
// instead, as wide as the first, keeping each match's case if asked,
// with the two ways of putting it. It
// holds the query and the options; the view holds the matches and tells
// the bar the count.
//
// Its buttons are flat glyphs, lit when on, as an editor's are: the
// glyphs stand in until the icons are drawn. Tab reaches each of them
// in turn, and Space or Return presses the one it is on; and while the
// bar has the keyboard, Alt with C, W, R or L -- Command and Option with
// them on a Mac -- turns case, whole words, patterns or the selection on
// or off, as the modern editors have it; Shift and Return goes back,
// Alt and Return -- Option and Return -- selects every match, each a
// selection of its own, and Control, Alt and Return -- Command, Option and
// Return -- replaces every one.
class ALFindBar : public LLPanel
{
public:
    AL_VIEW_TYPE(ALFindBar, LLPanel);

    struct Params : public LLInitParam::Block<Params, LLPanel::Params>
    {
        Params();
    };

    // Without `announce`, no change is said: what seeds the bar as it opens.

    void setQuery(const std::string& query, bool announce = true);
    const std::string& query() const { return mQuery; }
    void               setReplacement(const std::string& text);
    std::string        replacement() const;
    ALTextSearchOptions options() const;
    bool               inSelection() const;

    // The second row, shown or folded away; and whether it may be at all,
    // which a read-only view says no to.
    void setReplaceShown(bool shown);
    bool replaceShown() const { return mReplaceShown; }
    void setReplaceAllowed(bool allowed);

    // What the view found: which one is current, of how many; or what was
    // wrong with the pattern.
    // `capped`: the total is as many as were looked for, and there are more.
    // `wrapped`: the step to the current one went round the text's end --
    // on from the top (1) or back from the bottom (-1) -- said until the
    // count next changes.
    void setCount(S32 current, S32 total, const std::string& error, bool capped = false, S32 wrapped = 0);
    std::string countSaid() const;

    // The colours of the view the bar is over: its glyphs and its count
    // in the view's ink, its background a shade off the view's.
    void setColors(const LLColor4& background, const LLColor4& ink);

    // The height the bar wants for its state.
    S32  wantedHeight() const;
    void focusQuery();
    void focusReplacement();

    typedef boost::signals2::signal<void()> signal_t;
    boost::signals2::connection onChanged(const signal_t::slot_type& cb) { return mChanged.connect(cb); }
    boost::signals2::connection onNext(const signal_t::slot_type& cb) { return mNext.connect(cb); }
    boost::signals2::connection onPrevious(const signal_t::slot_type& cb) { return mPrevious.connect(cb); }
    boost::signals2::connection onReplace(const signal_t::slot_type& cb) { return mReplace.connect(cb); }
    boost::signals2::connection onReplaceAll(const signal_t::slot_type& cb) { return mReplaceAll.connect(cb); }
    boost::signals2::connection onSelectAll(const signal_t::slot_type& cb) { return mSelectAll.connect(cb); }
    boost::signals2::connection onClose(const signal_t::slot_type& cb) { return mClose.connect(cb); }

    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALFindBar(const Params& p);

private:
    ALFlatButton* flat(const std::string& name, const std::string& glyph, bool toggle, const std::string& tip);
    LLLineEditor* field(const std::string& name, const std::string& label, S32 pad_right);
    void          layout();

    std::string   mQuery;
    bool          mReplaceShown   = false;
    bool          mReplaceAllowed = true;
    LLUIColor     mBgColor;
    LLUIColor     mInkColor;

    ALFlatButton* mExpand       = nullptr;
    LLLineEditor* mFind         = nullptr;
    ALFlatButton* mCase         = nullptr;
    ALFlatButton* mWord         = nullptr;
    ALFlatButton* mRegex        = nullptr;
    LLTextBox*    mCount        = nullptr;
    ALFlatButton* mPrev         = nullptr;
    ALFlatButton* mNextButton   = nullptr;
    ALFlatButton* mSelection    = nullptr;
    ALFlatButton* mCloseButton  = nullptr;
    LLLineEditor* mReplaceField = nullptr;
    ALFlatButton* mPreserveCase = nullptr;
    ALFlatButton* mReplaceOne   = nullptr;
    ALFlatButton* mReplaceEvery = nullptr;

    signal_t mChanged, mNext, mPrevious, mReplace, mReplaceAll, mSelectAll, mClose;
};
