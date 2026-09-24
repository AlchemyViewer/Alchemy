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

#include "aldraggesture.h"
#include "lluictrl.h"
#include "lluiimage.h"

class LLFontGL;

#include <string>
#include <vector>

#include <boost/signals2.hpp>

// The documents a window holds, as a row of tabs over the one it shows: the
// shown one marked, a dot on the ones with work unsaved, and a way to close
// each. A tab container is a set of pages of one thing; this is a set of
// things in one place, which is what an editor with several files open is.
//
// The tabs are drawn rather than made of buttons, because their widths are
// one decision over all of them: they share the strip, a long name taking
// the spare room while there is any, the wide ones giving way first when
// there is not, and cut their names short before they overlap. A name too
// long for its tab is cut in the middle, its end -- the extension -- kept,
// and the whole of it is the tab's tip.
//
// A tab may be a preview: what is being looked at without being held, which
// the next look replaces. It is drawn in the other face so that a reader
// can tell which tabs will still be there after the next click; it closes
// as any tab does, by its way out or the middle button, since a reader
// done looking should not have to look at something else to be rid of
// it. Whether a tab is one is the caller's to say, and so is what a tab
// is called.
//
// With the keyboard, once the strip has it: the arrows choose the tab
// beside, Home and End the first and the last; Shift with an arrow moves
// the chosen tab along; Delete closes it, Return or Space holds it, Shift
// and F10 ask for its menu, and the down arrow for the list of them all.
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
        // A colour for the dot at the tab's left, where the caller has
        // something to say about the tab -- a problem in it -- and no
        // alpha where it has not. A dirty tab's dot is the ink unless a
        // badge says otherwise.
        LLColor4    badge = LLColor4::transparent;
        // What the tab holds, as a mark before its name: a script, a
        // notecard, a file. None for no mark.
        LLUIImagePtr image;
    };

    // The tabs in order, and which of them is shown, by value.
    // The tabs afresh. What a tab keeps of what it last drew -- its
    // name as cut for the room it had -- is kept where the tab is the
    // same tab with the same name, since a host that fills the strip on
    // every keystroke would otherwise have every name measured again
    // every frame.
    void setTabs(std::vector<Tab> tabs, const std::string& chosen);
    const std::vector<Tab>& tabs() const { return mTabs; }

    void choose(const std::string& value);
    const std::string& chosen() const { return mChosen; }

    // Where each tab is, in the strip's coordinates, and where its way out
    // is. Where the tabs are wider than the strip even at their least,
    // the strip scrolls -- the chosen tab always in sight, the wheel
    // moving along them -- and a button at its right end lists them all.
    LLRect rectOf(size_t index) const;
    LLRect closeRectOf(size_t index) const;
    // The tab under a point, or -1; none under the list button.
    S32 at(S32 x, S32 y) const;
    // Whether the tabs run past the strip, and where the button that
    // lists them is while they do.
    bool   overflowing() const;
    LLRect listRect() const;
    S32    scrollOffset() const { return mScroll; }

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
    // The tabs dragged into a new order, given as their values in it:
    // the caller keeps its own list in that order.
    // A tab double-clicked: a preview held, as it is anywhere a preview
    // is -- the caller says it is one no longer.
    boost::signals2::connection onHeld(const tab_signal_t::slot_type& cb)
    {
        return mHeldSignal.connect(cb);
    }
    typedef boost::signals2::signal<void(const std::vector<std::string>&)> order_signal_t;
    boost::signals2::connection onReordered(const order_signal_t::slot_type& cb)
    {
        return mReorderedSignal.connect(cb);
    }
    // A tab pressed with the right button, by its value and where: for
    // a menu about it.
    typedef boost::signals2::signal<void(const std::string&, S32, S32)> tab_menu_signal_t;
    boost::signals2::connection onMenu(const tab_menu_signal_t::slot_type& cb)
    {
        return mMenuSignal.connect(cb);
    }
    // The list button pressed, while the tabs run past the strip, or the
    // down arrow: for the caller to offer every tab to choose from.
    typedef boost::signals2::signal<void()> list_signal_t;
    boost::signals2::connection onListAsked(const list_signal_t::slot_type& cb)
    {
        return mListSignal.connect(cb);
    }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    void onMouseCaptureLost() override;
    bool handleMiddleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    bool handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALTabStrip(const Params& p);

public:
    // A name cut to fit a width, in the middle, its end kept. Public for
    // the test of it.
    static std::string shortened(const LLFontGL* font, const std::string& label, S32 room);

private:
    // Every tab's width, decided together.
    void layout();
    // The tabs' whole width, and the room they are shown in.
    S32  contentWidth() const;
    S32  shownWidth() const;
    // The scroll kept within the tabs, and moved so the chosen tab is in
    // sight.
    void clampScroll();
    void showChosen();
    // The tab with a value, or -1.
    S32  indexOf(const std::string& value) const;
    // The order the tabs were dragged into, told.
    void sayOrder();
    // Two tabs change places, each with what it last drew.
    void trade(size_t a, size_t b);
    // A tab chosen from the keyboard, which the strip keeps.
    void chooseKeyed(size_t index);
    std::string textOf(const Tab& tab) const;
    // A preview is set in italic. The face that draws it is the one the
    // font hands the style to, which is what the words are measured in.
    static const LLFontGL* fontFor(const Tab& tab);
    static U8 styleOf(const Tab& tab);

    std::vector<Tab>    mTabs;
    std::vector<S32>    mWidths;
    // Each tab's name as last drawn: cut for a room, kept while the room,
    // the face -- italic for a preview, regular once held -- and the
    // name are the same, since the cutting measures the words.
    struct Shown
    {
        S32             room = -1;
        const LLFontGL* font = nullptr;
        std::string     label;
        std::string     text;
    };
    std::vector<Shown>  mShown;
    std::string         mChosen;
    S32                 mHover = -1;
    S32                 mHoverX = 0;
    S32                 mHoverY = 0;
    S32                 mMinTabWidth;
    S32                 mMaxTabWidth;
    S32                 mGap;
    tab_signal_t        mChosenSignal;
    tab_signal_t        mClosedSignal;
    tab_signal_t        mHeldSignal;
    tab_menu_signal_t   mMenuSignal;
    order_signal_t      mReorderedSignal;
    list_signal_t       mListSignal;
    // How far along the tabs the strip is scrolled, in pixels; a wheel's
    // fractions kept until they make a pixel.
    S32                 mScroll = 0;
    F32                 mScrollRemainder = 0.f;
    // A tab whose way out was pressed: it goes if the press is let go of
    // over it, and not if the mouse slid off first.
    //
    // Both presses follow their tab, not its place: the host fills the
    // strip afresh whenever a fact about a tab moves -- a check done, a
    // save come back -- and may while a press is held, and a press whose
    // tab has gone is let go of.
    S32                 mPressedClose = -1;
    // A tab pressed and perhaps being dragged along the strip: which, and
    // the press, which is a drag once it has gone more than four pixels
    // along the strip.
    S32                 mPressed  = -1;
    ALDragGesture       mDrag{ 4, ALDragGesture::Zone::Across };
};
