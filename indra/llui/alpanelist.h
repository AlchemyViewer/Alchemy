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

    bool handleKeyHere(KEY key, MASK mask) override;

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
};
