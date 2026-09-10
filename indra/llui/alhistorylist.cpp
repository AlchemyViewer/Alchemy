/**
 * @file alhistorylist.cpp
 * @brief A buffered document's undo stack as a place, rather than a keystroke.
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

#include "linden_common.h"

#include "alhistorylist.h"

#include "alemptystate.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALHistoryList> r("history_list");

namespace
{
    constexpr S32 MARK_WIDTH = 18;
    constexpr S32 WHERE_WIDTH = 120;
}

ALHistoryList::Params::Params()
:   empty_headline("empty_headline"),
    empty_sentence("empty_sentence")
{
}

ALHistoryList::ALHistoryList(const Params& p)
:   LLPanel(p),
    mEmptyHeadline(p.empty_headline),
    mEmptySentence(p.empty_sentence)
{
    LLScrollListCtrl::Params lp(LLUICtrlFactory::getDefaultParams<LLScrollListCtrl>());
    lp.name = "steps";
    lp.rect = getLocalRect();
    lp.follows.flags = FOLLOWS_ALL;
    lp.draw_heading = false;
    lp.multi_select = false;
    lp.column_padding = 0;
    mList = LLUICtrlFactory::create<LLScrollListCtrl>(lp);
    addChild(mList);

    // The mark is a column of its own so that the text of every step starts
    // in the same place: a line drawn by indenting one row would move the
    // words on it.
    LLScrollListColumn::Params mark;
    mark.name = "mark";
    mark.width.pixel_width = MARK_WIDTH;
    mList->addColumn(mark);
    LLScrollListColumn::Params what;
    what.name = "what";
    what.width.dynamic_width = true;
    mList->addColumn(what);
    LLScrollListColumn::Params where;
    where.name = "where";
    where.width.pixel_width = WHERE_WIDTH;
    mList->addColumn(where);

    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback(boost::bind(&ALHistoryList::onRowChosen, this));
    mList->setDoubleClickCallback(boost::bind(&ALHistoryList::goToSelected, this));

    ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
    ep.name = "empty";
    ep.rect = getLocalRect();
    ep.follows.flags = FOLLOWS_ALL;
    ep.background_visible = false;
    mEmpty = LLUICtrlFactory::create<ALEmptyState>(ep);
    addChild(mEmpty);
    mEmpty->say(mEmptyHeadline, mEmptySentence);
}

void ALHistoryList::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    if (mList)
    {
        mList->reshape(width, height);
    }
    if (mEmpty)
    {
        mEmpty->reshape(width, height);
    }
}

void ALHistoryList::setSteps(std::vector<Step> steps, size_t in_force)
{
    mSteps = std::move(steps);
    mInForce = llmin(in_force, mSteps.size());
    fill();
}

void ALHistoryList::fill()
{
    // What was selected is an index into the steps, and the steps are what
    // just changed: a document that has grown by one has the same first
    // steps, so keeping the row keeps a person's place in the list.
    const LLScrollListItem* was = mList->getFirstSelected();
    const S32 kept = was ? was->getValue().asInteger() : -1;

    mList->deleteAllItems();

    // The column that says which document, only where some step names
    // one: a caller with one document open has nothing to put in it.
    const bool any_where = std::any_of(mSteps.begin(), mSteps.end(),
                                       [](const Step& step) { return !step.where.empty(); });
    if (LLScrollListColumn* where = mList->getColumn("where"))
    {
        const S32 width = any_where ? WHERE_WIDTH : 0;
        if (where->getWidth() != width)
        {
            where->setWidth(width);
            mList->updateColumns(true);
        }
    }

    // Newest first: the thing most likely to be undone is the thing most
    // recently done, and a list a person reaches for Control-Z instead of
    // should not need scrolling to reach it.
    for (size_t i = mSteps.size(); i-- > 0;)
    {
        const bool done = i < mInForce;
        const bool present = i + 1 == mInForce;

        LLSD row;
        row["value"] = (S32)i;
        LLSD& columns = row["columns"];
        columns[0]["column"] = "mark";
        columns[0]["value"] = present ? "\xe2\x96\xb8" : "";   // a small right-pointing triangle
        columns[1]["column"] = "what";
        columns[1]["value"] = mSteps[i].what;
        columns[2]["column"] = "where";
        columns[2]["value"] = mSteps[i].where;

        // Steps that have been put back are drawn in the quiet ink: they
        // are still there and they are not in force, and a list that drew
        // them like the rest would be saying that they were.
        LLScrollListItem* item = mList->addElement(row);
        if (item && !done)
        {
            static const LLUIColor undone =
                LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
            for (S32 cell = 0; cell < item->getNumColumns(); ++cell)
            {
                if (LLScrollListCell* text = item->getColumn(cell))
                {
                    text->setColor(undone.get());
                }
            }
        }
    }

    // Kept without being chosen again: the row is where it was, and a
    // caller told it had been pointed at would go and look at it.
    if (kept >= 0 && kept < (S32)mSteps.size())
    {
        mList->setCommitOnSelectionChange(false);
        mList->setSelectedByValue(LLSD(kept), true);
        mList->setCommitOnSelectionChange(true);
    }

    const bool anything = !mSteps.empty();
    mList->setVisible(anything);
    mEmpty->setVisible(!anything);
}

void ALHistoryList::onRowChosen()
{
    if (const LLScrollListItem* item = mList->getFirstSelected())
    {
        mChose((size_t)item->getValue().asInteger());
    }
}

// Choosing a step is asking for the document to be as it was just after that
// step: the count in force is one more than its index. Choosing the one that
// is already the present asks for nothing.
void ALHistoryList::goToSelected()
{
    const LLScrollListItem* item = mList->getFirstSelected();
    if (!item)
    {
        return;
    }
    const size_t want = (size_t)item->getValue().asInteger() + 1;
    if (want != mInForce)
    {
        mGoTo(want);
    }
}
