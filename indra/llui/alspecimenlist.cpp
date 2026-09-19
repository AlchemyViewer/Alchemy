/**
 * @file alspecimenlist.cpp
 * @brief A list whose rows are the things themselves rather than words about them.
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

#include "alspecimenlist.h"

#include "alemptystate.h"
#include "alstringmatch.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "lliconctrl.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "lltextbox.h"
#include "lltooltip.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llui.h"

#include <algorithm>
#include <functional>

static LLDefaultChildRegistry::Register<ALSpecimenList> r("specimen_list");

namespace
{
    constexpr S32 HEADING_HEIGHT = 20;
    constexpr S32 INSET = 6;
    constexpr S32 GAP = 8;
    // A cell's own margins, and the strip its name sits in.
    constexpr S32 CELL_PAD = 4;
    constexpr S32 CAPTION_HEIGHT = 14;

    // Whether a press has moved far enough to be a drag.
    bool dragged(S32 x, S32 y, S32 from_x, S32 from_y)
    {
        const S32 dx = x - from_x;
        const S32 dy = y - from_y;
        return dx * dx + dy * dy > DRAG_N_DROP_DISTANCE_THRESHOLD * DRAG_N_DROP_DISTANCE_THRESHOLD;
    }
}

// One row: a label, and the thing itself beside it.
//
// The click is taken here rather than passed down. A specimen is a picture of
// a widget: a list of buttons is not a list of things to press, and a person
// choosing the row means the row.
class ALSpecimenList::Row final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALSpecimenList::Row, LLPanel);

    Row(const LLPanel::Params& p, std::string value, ALSpecimenList& list)
    :   LLPanel(p),
        mValue(std::move(value)),
        mList(list)
    {
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        // Not LLPanel::handleMouseDown, which would offer it to the specimen
        // first and let a button in the list be pressed.
        mList.choose(mValue);
        // Held from here, so that a press that goes on to move is seen
        // moving by this row and not by whatever it moved over.
        mPressX = x;
        mPressY = y;
        gFocusMgr.setMouseCapture(this);
        return true;
    }

    bool handleMouseUp(S32 x, S32 y, MASK mask) override
    {
        if (hasMouseCapture())
        {
            gFocusMgr.setMouseCapture(nullptr);
            return true;
        }
        return LLPanel::handleMouseUp(x, y, mask);
    }

    // A press that has moved far enough is a drag, and the row lets go of
    // the mouse so that the drag tool, which the caller starts, has it.
    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (!hasMouseCapture())
        {
            return LLPanel::handleHover(x, y, mask);
        }
        if (dragged(x, y, mPressX, mPressY))
        {
            gFocusMgr.setMouseCapture(nullptr);
            mList.startDrag(mValue);
        }
        return true;
    }

    void draw() override
    {
        if (mMarked)
        {
            static const LLUIColor mark =
                LLUIColorTable::instance().getColor("ScrollSelectedBGColor", LLColor4::blue);
            gl_rect_2d(getLocalRect(), mark.get(), true);
        }
        LLPanel::draw();
    }

    void setMarked(bool marked) { mMarked = marked; }
    const std::string& value() const { return mValue; }

private:
    std::string         mValue;
    ALSpecimenList&     mList;
    bool                mMarked = false;
    S32                 mPressX = 0;
    S32                 mPressY = 0;
};

// The pane the cells are drawn on: it draws them and takes the clicks, and
// the list owns the entries and the layout. Cells are drawn rather than
// built, since there are hundreds and a picture needs no widget.
class ALSpecimenList::Tiles final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALSpecimenList::Tiles, LLPanel);

    Tiles(const LLPanel::Params& p, ALSpecimenList& list) : LLPanel(p), mList(list) {}

    void draw() override
    {
        for (const Cell& cell : mList.mCells)
        {
            mList.drawCell(cell, cell.index >= 0 && cell.index == mHovered);
        }
        LLPanel::draw();
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        const S32 index = mList.cellAt(x, y);
        if (index < 0)
        {
            return LLPanel::handleMouseDown(x, y, mask);
        }
        // The keyboard comes with the click, so the arrows work from here;
        // the press is held, so one that goes on to move is a drag.
        mList.setFocus(true);
        mList.cellPressed(index, mask);
        mPressX = x;
        mPressY = y;
        gFocusMgr.setMouseCapture(this);
        return true;
    }

    bool handleMouseUp(S32 x, S32 y, MASK mask) override
    {
        if (hasMouseCapture())
        {
            gFocusMgr.setMouseCapture(nullptr);
            return true;
        }
        return LLPanel::handleMouseUp(x, y, mask);
    }

    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (hasMouseCapture())
        {
            if (dragged(x, y, mPressX, mPressY))
            {
                gFocusMgr.setMouseCapture(nullptr);
                mList.startDrag(mList.mChosen);
            }
            return true;
        }
        mHovered = mList.cellAt(x, y);
        return LLPanel::handleHover(x, y, mask);
    }

    void onMouseLeave(S32 x, S32 y, MASK mask) override
    {
        mHovered = -1;
        LLPanel::onMouseLeave(x, y, mask);
    }

    bool handleToolTip(S32 x, S32 y, MASK mask) override
    {
        const S32 index = mList.cellAt(x, y);
        if (index < 0 || mList.mSpecimens[index].toolTip.empty())
        {
            return LLPanel::handleToolTip(x, y, mask);
        }
        const auto cell = std::find_if(mList.mCells.begin(), mList.mCells.end(),
                                       [&](const Cell& c) { return c.index == index; });
        LLRect sticky;
        localRectToScreen(cell->rect, &sticky);
        LLToolTipMgr::instance().show(LLToolTip::Params().message(mList.mSpecimens[index].toolTip).sticky_rect(sticky));
        return true;
    }

private:
    ALSpecimenList& mList;
    S32             mHovered = -1;
    S32             mPressX = 0;
    S32             mPressY = 0;
};

ALSpecimenList::Params::Params()
:   row_height("row_height", 30),
    label_width("label_width", 120),
    cell_width("cell_width", 0),
    cell_height("cell_height", 0),
    empty_headline("empty_headline"),
    empty_sentence("empty_sentence")
{
}

ALSpecimenList::ALSpecimenList(const Params& p)
:   LLPanel(p),
    mEmptyHeadline(p.empty_headline),
    mEmptySentence(p.empty_sentence),
    mRowHeight(p.row_height),
    mLabelWidth(p.label_width),
    mCellWidth(p.cell_width),
    mCellHeight(p.cell_height)
{
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
    sp.name = "scroller";
    sp.rect = getLocalRect();
    sp.follows.flags = FOLLOWS_ALL;
    sp.reserve_scroll_corner = false;
    mScroller = LLUICtrlFactory::create<LLScrollContainer>(sp);
    addChild(mScroller);

    LLPanel::Params cp(LLUICtrlFactory::getDefaultParams<LLPanel>());
    cp.name = "content";
    cp.rect = LLRect(0, getRect().getHeight(), getRect().getWidth(), 0);
    cp.background_visible = false;
    cp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    mRowsPane = LLUICtrlFactory::create<LLPanel>(cp);
    cp.name = "tiles";
    mTiles = new Tiles(cp, *this);
    mTiles->initFromParams(cp);
    mContent = cells() ? static_cast<LLPanel*>(mTiles) : mRowsPane;
    mScroller->addChild(mContent);

    ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
    ep.name = "empty";
    ep.rect = getLocalRect();
    ep.follows.flags = FOLLOWS_ALL;
    ep.background_visible = false;
    mEmpty = LLUICtrlFactory::create<ALEmptyState>(ep);
    addChild(mEmpty);
    mEmpty->say(mEmptyHeadline, mEmptySentence);
    mEmpty->setVisible(false);
}

ALSpecimenList::~ALSpecimenList()
{
    // The views are children of a row or the tiles and go with them; a
    // view never placed is nobody's but this list's. The layout that is
    // not showing is nobody's child either.
    for (const Specimen& specimen : mSpecimens)
    {
        if (specimen.view && !specimen.view->getParent())
        {
            delete specimen.view;
        }
    }
    if (mRowsPane && !mRowsPane->getParent())
    {
        delete mRowsPane;
    }
    if (mTiles && !mTiles->getParent())
    {
        delete mTiles;
    }
}

void ALSpecimenList::setEmpty(const std::string& headline, const std::string& sentence)
{
    mEmptyHeadline = headline;
    mEmptySentence = sentence;
    mEmpty->say(headline, sentence);
}

// A view out of wherever it sits: a row is a home for a view, not its
// owner, and so are the tiles.
static void takeOut(LLView* view)
{
    if (view && view->getParent())
    {
        view->getParent()->removeChild(view);
    }
}

// The rows and the headings taken down, and every view out.
void ALSpecimenList::unbuild()
{
    for (const Specimen& specimen : mSpecimens)
    {
        takeOut(specimen.view);
    }
    for (Row* row : mRows)
    {
        mRowsPane->removeChild(row);
        delete row;
    }
    mRows.clear();
    for (LLTextBox* heading : mHeadings)
    {
        mRowsPane->removeChild(heading);
        delete heading;
    }
    mHeadings.clear();
    mRowsBuilt = false;
    mCells.clear();
}

void ALSpecimenList::setSpecimens(std::vector<Specimen> specimens)
{
    unbuild();
    for (const Specimen& specimen : mSpecimens)
    {
        delete specimen.view;
    }
    mGroups.clear();
    mImages.clear();
    mSpecimens = std::move(specimens);

    // The headings, in the order they first appear: which things belong
    // together is a fact about them and not about their names, so they are
    // not sorted.
    for (const Specimen& specimen : mSpecimens)
    {
        if (!specimen.group.empty()
            && std::find(mGroups.begin(), mGroups.end(), specimen.group) == mGroups.end())
        {
            mGroups.push_back(specimen.group);
        }
    }
    for (const Specimen& specimen : mSpecimens)
    {
        mImages.push_back(specimen.image.empty() ? LLPointer<LLUIImage>() : LLUI::getUIImage(specimen.image));
    }
    placeViews();
}

// The other layout: the views out of this one and into the other, and
// the container given the other to scroll. Both layouts stay built, so
// the switch costs the frame and nothing else.
void ALSpecimenList::setCellSize(S32 width, S32 height)
{
    const bool were_cells = cells();
    mCellWidth = llmax(0, width);
    mCellHeight = llmax(0, height);
    if (were_cells == cells())
    {
        layout();
        return;
    }
    for (const Specimen& specimen : mSpecimens)
    {
        takeOut(specimen.view);
    }
    mScroller->removeChild(mContent);
    mContent = cells() ? static_cast<LLPanel*>(mTiles) : mRowsPane;
    mScroller->addChild(mContent);
    placeViews();
}

// The views into whichever layout is showing: each into its cell, over
// the cell's own drawing, or into its row beside the label.
void ALSpecimenList::placeViews()
{
    if (cells())
    {
        for (const Specimen& specimen : mSpecimens)
        {
            if (specimen.view)
            {
                specimen.view->setVisible(false);
                mTiles->addChild(specimen.view);
            }
        }
        layout();
        return;
    }

    buildRows();
    for (size_t i = 0; i < mSpecimens.size() && i < mRows.size(); ++i)
    {
        LLView* view = mSpecimens[i].view;
        if (!view)
        {
            continue;
        }
        // Beside the label, at its own height where that fits: a view
        // that was in a cell a moment ago is placed afresh.
        const S32 tall = llmin(mRowHeight - 8, view->getRect().getHeight());
        const S32 bottom = (mRowHeight - tall) / 2;
        view->setShape(LLRect(INSET + mLabelWidth + 4, bottom + tall, getRect().getWidth() - INSET, bottom));
        view->setVisible(true);
        mRows[i]->addChild(view);
    }
    layout();
}

// One row per entry with its label, and a heading per group, made once
// for the entries held and kept through a change of layout.
void ALSpecimenList::buildRows()
{
    if (mRowsBuilt)
    {
        return;
    }
    mRowsBuilt = true;

    for (const std::string& group : mGroups)
    {
        LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        tp.name = "heading_" + group;
        tp.initial_value = group;
        tp.rect = LLRect(0, HEADING_HEIGHT, getRect().getWidth(), 0);
        tp.font = LLFontGL::getFontSansSerifSmallBold();
        LLTextBox* made = LLUICtrlFactory::create<LLTextBox>(tp);
        mRowsPane->addChild(made);
        mHeadings.push_back(made);
    }

    for (Specimen& specimen : mSpecimens)
    {
        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row_" + specimen.value;
        rp.rect = LLRect(0, mRowHeight, getRect().getWidth(), 0);
        rp.background_visible = false;
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
        rp.tool_tip = specimen.toolTip;
        const std::string value = specimen.value;
        Row* row = new Row(rp, value, *this);
        row->initFromParams(rp);

        LLTextBox::Params lp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        lp.name = "label";
        lp.initial_value = specimen.label;
        lp.rect = LLRect(INSET, mRowHeight - 8, INSET + mLabelWidth, 8);
        lp.font = LLFontGL::getFontSansSerifSmall();
        lp.use_ellipses = true;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(lp));

        if (!specimen.view && !specimen.image.empty())
        {
            // A picture is shown beside the label the way a view would be,
            // fitted to the row.
            LLIconCtrl::Params ip;
            ip.name = "picture";
            ip.image = LLUI::getUIImage(specimen.image);
            ip.rect = LLRect(0, mRowHeight - 4, mRowHeight * 2, 4);
            specimen.view = LLUICtrlFactory::create<LLIconCtrl>(ip);
        }
        mRowsPane->addChild(row);
        mRows.push_back(row);
    }
}

bool ALSpecimenList::passes(const Specimen& specimen) const
{
    return ALStringMatch::containsNoCase(specimen.label, mFilter)
        || ALStringMatch::containsNoCase(specimen.group, mFilter);
}

void ALSpecimenList::filter(const std::string& text)
{
    mFilter = text;
    layout();
}

void ALSpecimenList::choose(const std::string& value)
{
    setChosen(value);
    mChose(value);
}

void ALSpecimenList::setChosen(const std::string& value)
{
    mChosen = value;
    mSelection = value.empty() ? std::vector<std::string>() : std::vector<std::string>{ value };
    for (Row* row : mRows)
    {
        row->setMarked(row->value() == value);
    }
    showChosen();
    mSelectionChanged();
}

void ALSpecimenList::setSelection(std::vector<std::string> values)
{
    mSelection = std::move(values);
    if (!mChosen.empty() && std::find(mSelection.begin(), mSelection.end(), mChosen) == mSelection.end())
    {
        mSelection.push_back(mChosen);
    }
    mSelectionChanged();
}

// A plain press chooses, and the selection is that one thing; control
// adds or takes the pressed one out, leaving what is chosen alone unless
// it was the pressed one; shift takes the run from the chosen one to the
// pressed one, in the order shown.
void ALSpecimenList::cellPressed(S32 index, MASK mask)
{
    const std::string& value = mSpecimens[index].value;

    if (mask & MASK_CONTROL)
    {
        const auto found = std::find(mSelection.begin(), mSelection.end(), value);
        if (found != mSelection.end() && value != mChosen)
        {
            mSelection.erase(found);
        }
        else if (found == mSelection.end())
        {
            mSelection.push_back(value);
            if (mChosen.empty())
            {
                mChosen = value;
                mChose(value);
            }
        }
        mSelectionChanged();
        return;
    }

    if ((mask & MASK_SHIFT) && !mChosen.empty())
    {
        const std::vector<S32> order = shownOrder();
        const auto from = std::find_if(order.begin(), order.end(), [&](S32 i) { return mSpecimens[i].value == mChosen; });
        const auto to = std::find(order.begin(), order.end(), index);
        if (from != order.end() && to != order.end())
        {
            mSelection.clear();
            for (auto it = llmin(from, to); it <= llmax(from, to); ++it)
            {
                mSelection.push_back(mSpecimens[*it].value);
            }
            mSelectionChanged();
            return;
        }
    }

    choose(value);
}

// The chosen cell scrolled into view, in the pane's own coordinates,
// which is what the container scrolls in.
void ALSpecimenList::showChosen()
{
    const S32 cell = chosenCell();
    if (cell >= 0 && mScroller)
    {
        mScroller->scrollToShowRect(mCells[cell].rect);
    }
}

// The entries under no heading first, then each heading and what is under
// it, leaving out what the filter has narrowed away.
std::vector<S32> ALSpecimenList::shownOrder() const
{
    std::vector<S32> order;
    const auto sweep = [&](const std::string& group)
    {
        for (size_t i = 0; i < mSpecimens.size(); ++i)
        {
            if (mSpecimens[i].group == group && passes(mSpecimens[i]))
            {
                order.push_back((S32)i);
            }
        }
    };
    sweep(std::string());
    for (const std::string& group : mGroups)
    {
        sweep(group);
    }
    return order;
}

void ALSpecimenList::layout()
{
    if (cells())
    {
        layoutCells();
    }
    else
    {
        layoutRows();
    }
}

// Down the list: rows under no heading first, then each heading and what is
// under it. A heading the filter has left with nothing under it goes with
// what it held, and a row the filter has narrowed away is hidden rather than
// destroyed -- building a widget is the expensive part, and a filter is typed
// a letter at a time.
void ALSpecimenList::layoutRows()
{
    if (!mRowsPane || !mScroller || mContent != mRowsPane)
    {
        return;
    }
    const S32 width = mScroller->getContentWindowRect().getWidth();

    // What is shown, in the order it is shown, before anything is placed:
    // the content panel has to be as tall as the whole of it, and a row is
    // placed from the content's top.
    struct Placed { LLView* view = nullptr; S32 height = 0; bool heading = false; };
    std::vector<Placed> order;
    mShown = 0;

    const auto sweep = [&](const std::string& group, LLTextBox* heading)
    {
        bool any = false;
        for (size_t i = 0; i < mSpecimens.size() && i < mRows.size(); ++i)
        {
            if (mSpecimens[i].group != group)
            {
                continue;
            }
            const bool keep = passes(mSpecimens[i]);
            mRows[i]->setVisible(keep);
            if (!keep)
            {
                continue;
            }
            if (heading && !any)
            {
                order.push_back({ heading, HEADING_HEIGHT, true });
            }
            any = true;
            order.push_back({ mRows[i], mRowHeight, false });
            ++mShown;
        }
        if (heading)
        {
            heading->setVisible(any);
        }
    };

    sweep(std::string(), nullptr);
    for (size_t g = 0; g < mGroups.size(); ++g)
    {
        sweep(mGroups[g], g < mHeadings.size() ? mHeadings[g] : nullptr);
    }

    S32 total = GAP;
    for (const Placed& one : order)
    {
        total += one.height;
    }
    const S32 tall = llmax(total, mScroller->getContentWindowRect().getHeight());
    mRowsPane->reshape(width, tall);
    mRowsPane->setOrigin(0, 0);

    S32 top = tall;
    for (const Placed& one : order)
    {
        const S32 left = one.heading ? INSET : 0;
        one.view->setShape(LLRect(left, top, width, top - one.height));
        top -= one.height;
    }

    mEmpty->setVisible(mShown == 0);
    mScroller->setVisible(mShown > 0);
}

// Rows of cells from the top, a heading row wherever the heading changes,
// and the pane made as tall as the rows come to.
void ALSpecimenList::layoutCells()
{
    if (!mTiles || !mScroller || mContent != mTiles)
    {
        return;
    }
    const S32 width = llmax(1, mScroller->getContentWindowRect().getWidth());
    const S32 columns = llmax(1, width / mCellWidth);
    const std::vector<S32> order = shownOrder();

    mCells.clear();
    mShown = order.size();

    S32 y = 0;
    S32 column = 0;
    std::string heading;
    bool first = true;

    for (const S32 index : order)
    {
        if (first || mSpecimens[index].group != heading)
        {
            heading = mSpecimens[index].group;
            first = false;
            if (column != 0)
            {
                y += mCellHeight;
                column = 0;
            }
            if (!heading.empty())
            {
                Cell cell;
                cell.rect = LLRect(0, -y, width, -(y + HEADING_HEIGHT));
                cell.heading = heading;
                mCells.push_back(cell);
                y += HEADING_HEIGHT;
            }
        }

        Cell cell;
        const S32 left = column * mCellWidth;
        cell.rect = LLRect(left, -y, left + mCellWidth, -(y + mCellHeight));
        cell.index = index;
        mCells.push_back(cell);

        if (++column == columns)
        {
            column = 0;
            y += mCellHeight;
        }
    }

    if (column != 0)
    {
        y += mCellHeight;
    }

    const S32 height = llmax(y, mScroller->getContentWindowRect().getHeight());

    // Cells were laid out from the top down as negative offsets; the pane
    // is as tall as they come to, with its top at its own height.
    for (Cell& cell : mCells)
    {
        cell.rect.translate(0, height);
    }

    mTiles->reshape(width, height);
    mTiles->setOrigin(0, mScroller->getContentWindowRect().getHeight() - height);

    // A built view goes in its cell, at its own height where that fits,
    // over the strip its name sits in; one the filter narrowed away is
    // hidden.
    for (const Specimen& specimen : mSpecimens)
    {
        if (specimen.view)
        {
            specimen.view->setVisible(false);
        }
    }
    for (const Cell& cell : mCells)
    {
        LLView* view = cell.index >= 0 ? mSpecimens[cell.index].view : nullptr;
        if (!view)
        {
            continue;
        }
        const LLRect area(cell.rect.mLeft + 2 + CELL_PAD, cell.rect.mTop - 2 - CELL_PAD,
                          cell.rect.mRight - 2 - CELL_PAD, cell.rect.mBottom + 2 + CAPTION_HEIGHT + CELL_PAD);
        const S32 tall = llmin(area.getHeight(), view->getRect().getHeight());
        const S32 bottom = area.mBottom + (area.getHeight() - tall) / 2;
        view->setShape(LLRect(area.mLeft, bottom + tall, area.mRight, bottom));
        view->setVisible(true);
    }

    mEmpty->setVisible(mShown == 0);
    mScroller->setVisible(mShown > 0);
}

S32 ALSpecimenList::cellAt(S32 x, S32 y) const
{
    for (const Cell& cell : mCells)
    {
        if (cell.index >= 0 && cell.rect.pointInRect(x, y))
        {
            return cell.index;
        }
    }
    return -1;
}

S32 ALSpecimenList::chosenCell() const
{
    for (size_t i = 0; i < mCells.size(); ++i)
    {
        if (mCells[i].index >= 0 && mSpecimens[mCells[i].index].value == mChosen)
        {
            return (S32)i;
        }
    }
    return -1;
}

void ALSpecimenList::drawCell(const Cell& cell, bool hovered) const
{
    static const LLUIColor heading_color = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor caption_color = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
    static const LLUIColor selected_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
    static const LLUIColor hover_color = LLUIColorTable::instance().getColor("DkGray", LLColor4::grey);
    static const LLUIColor mark_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
    static const LLUIColor broken_color = LLUIColorTable::instance().getColor("LtOrange", LLColor4::red);

    const LLFontGL* small = LLFontGL::getFontSansSerifSmall();

    if (cell.index < 0)
    {
        small->renderUTF8(cell.heading, 0, F32(CELL_PAD), F32(cell.rect.mBottom) + (HEADING_HEIGHT - small->getLineHeight()) / 2.f,
                          heading_color.get(), LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::BOLD, LLFontGL::NO_SHADOW);
        gl_line_2d(CELL_PAD, cell.rect.mBottom + 1, cell.rect.mRight - CELL_PAD, cell.rect.mBottom + 1, heading_color.get() % 0.3f);
        return;
    }

    const Specimen& specimen = mSpecimens[cell.index];
    const bool chosen = specimen.value == mChosen;
    const bool selected = !chosen && std::find(mSelection.begin(), mSelection.end(), specimen.value) != mSelection.end();
    const LLRect box(cell.rect.mLeft + 2, cell.rect.mTop - 2, cell.rect.mRight - 2, cell.rect.mBottom + 2);

    if (chosen || selected || hovered)
    {
        gl_rect_2d(box, chosen ? selected_color.get() % 0.35f : selected ? selected_color.get() % 0.18f : hover_color.get(), true);
    }

    // The picture, over the strip the name sits in: stretched across the
    // cell the way chrome is drawn, or fitted the way an icon is.
    const LLRect area(box.mLeft + CELL_PAD, box.mTop - CELL_PAD, box.mRight - CELL_PAD, box.mBottom + CAPTION_HEIGHT + CELL_PAD);
    const LLPointer<LLUIImage>& image = mImages[cell.index];

    // A built view in the cell draws itself; a picture is drawn here.
    if (!specimen.view && image.notNull() && area.getWidth() > 0 && area.getHeight() > 0)
    {
        const S32 natural_width = llmax(1, image->getWidth());
        const S32 natural_height = llmax(1, image->getHeight());
        S32 width;
        S32 height;

        if (specimen.stretch)
        {
            width = area.getWidth();
            height = llmin(area.getHeight(), natural_height * 2);
        }
        else
        {
            const F32 scale = llmin(1.f, llmin(F32(area.getWidth()) / natural_width, F32(area.getHeight()) / natural_height));
            width = llmax(1, ll_round(natural_width * scale));
            height = llmax(1, ll_round(natural_height * scale));
        }

        const S32 left = area.mLeft + (area.getWidth() - width) / 2;
        const S32 bottom = area.mBottom + (area.getHeight() - height) / 2;

        gl_rect_2d_checkerboard(LLRect(left, bottom + height, left + width, bottom), 0.6f);
        image->draw(left, bottom, width, height);
    }

    if (specimen.marked)
    {
        gl_rect_2d(LLRect(box.mRight - 8, box.mTop - 3, box.mRight - 3, box.mTop - 8), mark_color.get(), true);
    }

    small->renderUTF8(specimen.label, 0, F32(box.mLeft + CELL_PAD), F32(box.mBottom + 2),
                      specimen.broken ? broken_color.get() : caption_color.get(),
                      LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX,
                      box.getWidth() - 2 * CELL_PAD, nullptr, true);
}

// Left and right are the next cell either way, headings skipped; up and
// down are the cell in the row above or below nearest across, which is
// the one under or over this one when the rows are full.
bool ALSpecimenList::handleKeyHere(KEY key, MASK mask)
{
    if (cells() && key == 'A' && mask == MASK_CONTROL)
    {
        std::vector<std::string> values;
        for (const S32 index : shownOrder())
        {
            values.push_back(mSpecimens[index].value);
        }
        if (!values.empty())
        {
            if (std::find(values.begin(), values.end(), mChosen) == values.end())
            {
                choose(values.front());
            }
            setSelection(std::move(values));
        }
        return true;
    }
    if (!cells() || mask != MASK_NONE || mCells.empty()
        || (key != KEY_LEFT && key != KEY_RIGHT && key != KEY_UP && key != KEY_DOWN))
    {
        return LLPanel::handleKeyHere(key, mask);
    }

    const S32 from = chosenCell();
    S32 to = -1;

    if (from < 0)
    {
        for (size_t i = 0; i < mCells.size() && to < 0; ++i)
        {
            to = mCells[i].index >= 0 ? (S32)i : -1;
        }
    }
    else if (key == KEY_LEFT || key == KEY_RIGHT)
    {
        const S32 step = key == KEY_LEFT ? -1 : 1;
        for (S32 i = from + step; i >= 0 && i < (S32)mCells.size(); i += step)
        {
            if (mCells[i].index >= 0)
            {
                to = i;
                break;
            }
        }
    }
    else
    {
        const LLRect& here = mCells[from].rect;
        S32 nearest = S32_MAX;
        for (size_t i = 0; i < mCells.size(); ++i)
        {
            const LLRect& there = mCells[i].rect;
            const bool that_way = key == KEY_UP ? there.mBottom >= here.mTop : there.mTop <= here.mBottom;
            if (mCells[i].index < 0 || !that_way)
            {
                continue;
            }
            // The nearest row first, and within it the nearest across.
            const S32 rows = std::abs(there.getCenterY() - here.getCenterY());
            const S32 across = std::abs(there.getCenterX() - here.getCenterX());
            const S32 distance = rows * 4096 + across;
            if (distance < nearest)
            {
                nearest = distance;
                to = (S32)i;
            }
        }
    }

    if (to >= 0)
    {
        choose(mSpecimens[mCells[to].index].value);
    }
    return true;
}

void ALSpecimenList::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}
