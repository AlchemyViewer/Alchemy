/**
 * @file altabstrip.h
 * @brief A row of tabs over what a window shows, one per thing it holds.
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

#include "lluictrl.h"

#include <string>
#include <vector>

#include <boost/signals2.hpp>

// The documents a window holds, as a row of tabs over the one it shows: the
// shown one marked, a dot on the ones with work unsaved, and a way to close
// each. A tab container is a set of pages of one thing; this is a set of
// things in one place, which is what an editor with several files open is.
//
// The tabs are drawn rather than made of buttons, because their widths are
// one decision over all of them: they share the strip, shrink together when
// there are many, and cut their names short before they overlap. A name too
// long for its tab ends in an ellipsis, and the whole of it is the tab's tip.
//
// A tab may be a preview: what is being looked at without being held, which
// the next look replaces. It is drawn in the other face so that a reader
// can tell which tabs will still be there after the next click, and it
// offers no way to close, since it is not being held. Whether a tab is one
// is the caller's to say, and so is what a tab is called.
class ALTabStrip : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALTabStrip, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        // A tab is as wide as its name wants, between these two. Past the
        // strip's width, every tab takes an equal share down to the least.
        Optional<S32>           min_tab_width;
        Optional<S32>           max_tab_width;
        Optional<S32>           gap;
        // The mark on a tab with unsaved work, before its name.
        Optional<std::string>   dirty_mark;
        Params();
    };

    struct Tab
    {
        // The name on the tab.
        std::string label;
        // Said after it in the quieter ink, where the caller has two tabs
        // of one name to tell apart. Empty says nothing.
        std::string detail;
        // What the caller calls it, answered back when it is chosen or
        // closed.
        std::string value;
        std::string toolTip;
        bool        dirty = false;
        bool        preview = false;
    };

    // The tabs in order, and which of them is shown, by value.
    void setTabs(std::vector<Tab> tabs, const std::string& chosen);
    const std::vector<Tab>& tabs() const { return mTabs; }

    void choose(const std::string& value);
    const std::string& chosen() const { return mChosen; }

    // Where each tab is, in the strip's coordinates, and where its way out
    // is. A tab past the right edge is where the arithmetic puts it and is
    // drawn cut off there; the strip does not scroll.
    LLRect rectOf(size_t index) const;
    LLRect closeRectOf(size_t index) const;
    // The tab under a point, or -1.
    S32 at(S32 x, S32 y) const;

    typedef boost::signals2::signal<void(const std::string&)> tab_signal_t;
    // A tab pressed, by its value. Not sent for the tab already chosen.
    boost::signals2::connection onChosen(const tab_signal_t::slot_type& cb)
    {
        return mChosenSignal.connect(cb);
    }
    // A tab's way out pressed, or the tab pressed with the middle button.
    // Whether it goes is the caller's: this holds nothing and closes nothing.
    boost::signals2::connection onClosed(const tab_signal_t::slot_type& cb)
    {
        return mClosedSignal.connect(cb);
    }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMiddleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALTabStrip(const Params& p);

private:
    // Every tab's width, decided together.
    void layout();
    std::string textOf(const Tab& tab) const;

    std::vector<Tab>    mTabs;
    std::vector<S32>    mWidths;
    std::string         mChosen;
    S32                 mHover = -1;
    S32                 mMinTabWidth;
    S32                 mMaxTabWidth;
    S32                 mGap;
    std::string         mDirtyMark;
    tab_signal_t        mChosenSignal;
    tab_signal_t        mClosedSignal;
};
