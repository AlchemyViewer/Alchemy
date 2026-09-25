/**
 * @file alpanelist.h
 * @brief A list whose rows are places: the keys that go to one and back.
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
#include "llframetimer.h"
#include "llscrolllistctrl.h"

#include <functional>

// A studio pane's list -- problems, references, places found, an outline --
// whose rows are places to go. Choosing a row shows its place and the arrow
// keys walk on through them; these are the keys past that: return goes to
// the place chosen, escape goes back to where the typing was, and left and
// right fold and open a row that holds others. A panel above the list takes
// escape to mean nothing is to have the keyboard before its window hears
// of it, so the list is asked first.
//
// Rows in groups, under a heading each: a sort by a column sorts within
// each group, the headings staying over their own, where the caller says
// which group a row is of.
class ALPaneList : public LLScrollListCtrl
{
public:
    AL_VIEW_TYPE(ALPaneList, LLScrollListCtrl);

    struct Params : public LLInitParam::Block<Params, LLScrollListCtrl::Params>
    {
        Params();
    };

    // A key offered before the list does anything with it; true takes it.
    typedef std::function<bool(KEY, MASK)> key_t;
    void setKeyHandler(key_t handler) { mKeyHandler = std::move(handler); }

    // Which group a row is of, and whether it is the group's heading, for
    // a sort that keeps them together; nothing, and the list sorts as any
    // other. A group's number orders the groups whichever way a column
    // sorts.
    typedef std::function<void(const LLScrollListItem*, S32& group, bool& heading)> group_t;
    void setGrouping(group_t grouping);
    // How two rows of a group compare by a column: below zero for the
    // first before the second, as the column ascends. Nothing compares the
    // cells' words.
    typedef std::function<S32(S32 column, const LLScrollListItem*, const LLScrollListItem*)> compare_t;
    void setComparison(compare_t comparison);

    // A press on a row that moves on with the button held, past the
    // viewer's dead zone for a drag: offered as a drag of what is chosen,
    // with the value of the row pressed -- the rows may be made again
    // while the button is held -- which the starter begins with the viewer's
    // drag tool and says it did. Not begun, or with no starter, the list
    // chooses the rows the pointer passes, as any list does. With a
    // starter, a plain press on one of several rows chosen keeps them all
    // chosen, for a drag of them all; let go without one, it chooses its
    // row alone, as a press would have.
    typedef std::function<bool(const LLSD& pressed)> drag_t;
    void setDragStarter(drag_t starter) { mDragStarter = std::move(starter); }

    // What the viewer's drag tool carries over a row, or drops on it: the
    // handler is given the row's value -- undefined over no row -- says in
    // `accept` what it would do, and does it on the drop; and it answers
    // the value of the row the drop would go to, which is lit, or an
    // undefined one for none. Near the list's top or bottom the list
    // scrolls, for a row out of sight; the light goes with the pointer.
    typedef std::function<LLSD(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)>
        drop_t;
    void setDropHandler(drop_t handler) { mDropHandler = std::move(handler); }

    bool handleKeyHere(KEY key, MASK mask) override;
    // The Edit menu's commands are the list's while it has the keyboard,
    // as they are a text's: else they are whatever last took them, and
    // with a prim chosen in the build tools that is the world's selection,
    // whose Delete -- on the plain key, which the menu hears before the
    // list does -- deletes the prim.
    void onFocusReceived() override;
    void onFocusLost() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseCaptureLost() override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept,
                           std::string& tooltip) override;
    // Over the rows, a row's own tip; over none of them, the list's own,
    // which a plain list only ever shows as its hidden column heading's.
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    void draw() override;

protected:
    friend class LLUICtrlFactory;
    ALPaneList(const Params& p);

private:
    S32 compare(S32 column, const LLScrollListItem* a, const LLScrollListItem* b);
    void connectSort();

    key_t                       mKeyHandler;
    group_t                     mGrouping;
    compare_t                   mComparison;
    boost::signals2::connection mSortConnection;
    drag_t                      mDragStarter;
    ALDragGesture               mPress;
    LLSD                        mPressed;
    drop_t                      mDropHandler;
    // A row a step, and no faster, while a drag rests at an edge; and
    // whether a row is lit for a drop, until the pointer leaves.
    LLFrameTimer                mDropScroll;
    bool                        mDropLit = false;
};
