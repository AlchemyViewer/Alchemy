/**
 * @file alpanelist.cpp
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

#include "linden_common.h"

#include "alpanelist.h"

#include "llscrolllistcell.h"
#include "llscrolllistitem.h"

static LLDefaultChildRegistry::Register<ALPaneList> r("pane_list");

ALPaneList::Params::Params()
{
}

ALPaneList::ALPaneList(const Params& p)
:   LLScrollListCtrl(p)
{
}

bool ALPaneList::handleKeyHere(KEY key, MASK mask)
{
    if (mKeyHandler && mKeyHandler(key, mask))
    {
        return true;
    }
    return LLScrollListCtrl::handleKeyHere(key, mask);
}

void ALPaneList::setGrouping(group_t grouping)
{
    mGrouping = std::move(grouping);
    connectSort();
}

void ALPaneList::setComparison(compare_t comparison)
{
    mComparison = std::move(comparison);
    connectSort();
}

void ALPaneList::connectSort()
{
    if (mSortConnection.connected())
    {
        return;
    }
    mSortConnection = setSortCallback([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) { return compare(column, a, b); });
}

S32 ALPaneList::compare(S32 column, const LLScrollListItem* a, const LLScrollListItem* b)
{
    // The list turns what this says the other way for a column sorted
    // down; what orders the groups, and a heading over its rows, is turned
    // first so that it comes out the same whichever way.
    if (mGrouping)
    {
        const S32 way = getSortAscending() ? 1 : -1;
        S32       group_a = 0, group_b = 0;
        bool      heading_a = false, heading_b = false;
        mGrouping(a, group_a, heading_a);
        mGrouping(b, group_b, heading_b);
        if (group_a != group_b)
        {
            return way * (group_a < group_b ? -1 : 1);
        }
        if (heading_a != heading_b)
        {
            return way * (heading_a ? -1 : 1);
        }
    }
    if (mComparison)
    {
        return mComparison(column, a, b);
    }
    const LLScrollListCell* cell_a = a->getColumn(column);
    const LLScrollListCell* cell_b = b->getColumn(column);
    return cell_a && cell_b ? LLStringUtil::compareDict(cell_a->getValue().asString(), cell_b->getValue().asString()) : 0;
}
