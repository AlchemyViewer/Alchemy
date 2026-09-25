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

#include "llfontgl.h"
#include "lllineeditor.h"
#include "llscrolllistcell.h"
#include "llscrolllistitem.h"
#include "llsdutil.h"
#include "llui.h"

#include <utility>

static LLDefaultChildRegistry::Register<ALPaneList> r("pane_list");

namespace
{
    // Whether a row's value is the one asked for: as the list keeps them,
    // each scalar made words on the way in, so a key's words against the
    // key's words rather than type against type.
    bool sameValue(const LLSD& kept, const LLSD& asked)
    {
        if (asked.isMap())
        {
            if (!kept.isMap() || kept.size() != asked.size())
            {
                return false;
            }
            for (const auto& [key, value] : llsd::inMap(asked))
            {
                if (!kept.has(key) || !sameValue(kept[key], value))
                {
                    return false;
                }
            }
            return true;
        }
        return !kept.isMap() && kept.asString() == asked.asString();
    }
}

ALPaneList::Params::Params()
{
}

ALPaneList::ALPaneList(const Params& p)
:   LLScrollListCtrl(p),
    mPress(DRAG_N_DROP_DISTANCE_THRESHOLD)
{
}

ALPaneList::~ALPaneList()
{
    // Gone with the list, the field says nothing more of losing the
    // keyboard: what it would tell is going too.
    mEditorLost.disconnect();
}

bool ALPaneList::handleKeyHere(KEY key, MASK mask)
{
    // While a row's words are edited, return keeps them and escape leaves
    // them, and the keys that would walk the rows do nothing: the field is
    // over its row. The rest go on, the window's commands among them.
    if (editing())
    {
        switch (key)
        {
            case KEY_RETURN:
                endEditing(true);
                return true;
            case KEY_ESCAPE:
                endEditing(false);
                return true;
            case KEY_UP:
            case KEY_DOWN:
            case KEY_PAGE_UP:
            case KEY_PAGE_DOWN:
            case KEY_F2:
                return true;
            default:
                return false;
        }
    }
    if (mKeyHandler && mKeyHandler(key, mask))
    {
        return true;
    }
    return LLScrollListCtrl::handleKeyHere(key, mask);
}

void ALPaneList::onFocusReceived()
{
    LLScrollListCtrl::onFocusReceived();
    gEditMenuHandler = this;
}

void ALPaneList::onFocusLost()
{
    if (gEditMenuHandler == this)
    {
        gEditMenuHandler = nullptr;
    }
    LLScrollListCtrl::onFocusLost();
}

bool ALPaneList::handleMouseDown(S32 x, S32 y, MASK mask)
{
    mPressed.clear();
    // One of several chosen, pressed plainly: all of them stay chosen,
    // held for a drag, and the release chooses the row alone where no drag
    // came, as the list's own release does.
    if (mDragStarter && mask == MASK_NONE)
    {
        const LLScrollListItem* hit = hitItem(x, y);
        if (hit && hit->getSelected() && getNumSelected() > 1)
        {
            if (!childrenHandleMouseDown(x, y, mask))
            {
                setFocus(true);
                gFocusMgr.setMouseCapture(this);
                mPressed = hit->getValue();
                mPress.press(x, y);
            }
            return true;
        }
    }
    const bool handled = LLScrollListCtrl::handleMouseDown(x, y, mask);
    // A row pressed, which the list has chosen and holds the pointer for:
    // one that goes on to move may be a drag.
    if (mDragStarter && hasMouseCapture())
    {
        if (const LLScrollListItem* hit = hitItem(x, y))
        {
            mPressed = hit->getValue();
            mPress.press(x, y);
        }
    }
    return handled;
}

bool ALPaneList::handleMouseUp(S32 x, S32 y, MASK mask)
{
    mPress.release();
    mPressed.clear();
    return LLScrollListCtrl::handleMouseUp(x, y, mask);
}

bool ALPaneList::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (editing() && mEditor->getRect().pointInRect(x, y))
    {
        childrenHandleDoubleClick(x, y, mask);
        return true;
    }
    return LLScrollListCtrl::handleDoubleClick(x, y, mask);
}

bool ALPaneList::handleHover(S32 x, S32 y, MASK mask)
{
    if (mPress.pressed() && hasMouseCapture())
    {
        // Still, or within the dead zone: nothing yet, and no row chosen
        // by a hand that only trembled.
        if (!mPress.moved(x, y))
        {
            return true;
        }
        mPress.release();
        const LLSD pressed = std::exchange(mPressed, LLSD());
        // Begun, the drag tool has the pointer from here.
        if (mDragStarter(pressed))
        {
            return true;
        }
    }
    return LLScrollListCtrl::handleHover(x, y, mask);
}

bool ALPaneList::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept,
                                   std::string& tooltip)
{
    if (!mDropHandler)
    {
        return LLScrollListCtrl::handleDragAndDrop(x, y, mask, drop, type, cargo, accept, tooltip);
    }
    // At the top or the bottom of the rows, the list moves on a row at a
    // time, for one out of sight.
    constexpr F32 SCROLL_STEP = 0.1f;
    constexpr S32 EDGE        = 10;
    const LLRect  rows        = getItemListRect();
    if (!drop && rows.pointInRect(x, y) && (y > rows.mTop - EDGE || y < rows.mBottom + EDGE) && mDropScroll.getElapsedTimeF32() > SCROLL_STEP)
    {
        mDropScroll.reset();
        setScrollPos(llmax(0, getScrollPos() + (y < rows.mBottom + EDGE ? 1 : -1)));
    }
    LLScrollListItem* row = hitItem(x, y);
    *accept               = ACCEPT_NO;
    const LLSD target     = mDropHandler(row ? row->getValue() : LLSD(), mask, drop, type, cargo, accept, tooltip);
    // The row it would go to lit, not the one under the pointer: an item's
    // row may stand for what holds it.
    S32 lit = -1;
    if (!drop && target.isDefined())
    {
        S32 index = 0;
        for (LLScrollListItem* item : getAllData())
        {
            if (sameValue(item->getValue(), target))
            {
                lit = index;
                break;
            }
            ++index;
        }
    }
    mouseOverHighlightNthItem(lit);
    mDropLit = lit >= 0;
    return true;
}

bool ALPaneList::handleToolTip(S32 x, S32 y, MASK mask)
{
    if (!hitItem(x, y) && getItemListRect().pointInRect(x, y))
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    return LLScrollListCtrl::handleToolTip(x, y, mask);
}

void ALPaneList::draw()
{
    followEdit();
    // A drag gone elsewhere tells the list nothing: its light goes when
    // the pointer is no longer over it.
    if (mDropLit)
    {
        S32 x = 0, y = 0;
        LLUI::getInstance()->getMousePositionLocal(this, &x, &y);
        if (!pointInView(x, y))
        {
            mouseOverHighlightNthItem(-1);
            mDropLit = false;
        }
    }
    LLScrollListCtrl::draw();
}

void ALPaneList::onMouseCaptureLost()
{
    mPress.cancel();
    mPressed.clear();
    LLScrollListCtrl::onMouseCaptureLost();
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

S32 ALPaneList::indexOf(const LLSD& value) const
{
    S32 index = 0;
    for (const LLScrollListItem* item : getAllData())
    {
        if (sameValue(item->getValue(), value))
        {
            return index;
        }
        ++index;
    }
    return -1;
}

bool ALPaneList::editRow(const LLSD& value, Edit edit)
{
    endEditing(true);
    const S32 index = indexOf(value);
    if (index < 0 || !getColumn(edit.column))
    {
        return false;
    }
    // In sight first: a row off the page has no place for the field.
    const S32 lines = llmax(1, getLinesPerPage());
    if (index < getScrollPos())
    {
        setScrollPos(index);
    }
    else if (index >= getScrollPos() + lines)
    {
        setScrollPos(index - lines + 1);
    }
    if (!mEditor)
    {
        LLLineEditor::Params params;
        params.name("row_editor");
        params.font(LLFontGL::getFontSansSerifSmall());
        params.rect(LLRect(0, 20, 100, 0));
        params.follows.flags(FOLLOWS_NONE);
        params.commit_on_focus_lost(false);
        params.visible(false);
        mEditor = LLUICtrlFactory::create<LLLineEditor>(params);
        addChild(mEditor);
        // The keyboard gone elsewhere -- a click on another row, in the
        // world, in another window -- keeps what was typed, as a tree of
        // files does.
        mEditorLost = mEditor->setFocusLostCallback([this](LLFocusableElement*) { endEditing(true); });
    }
    mEditing = value;
    mEdit    = std::move(edit);
    mEditor->setMaxTextLength(mEdit->maxBytes > 0 ? mEdit->maxBytes : 1024);
    mEditor->setText(mEdit->text);
    placeEditor(indexOf(mEditing));
    mEditor->setVisible(true);
    mEditor->setFocus(true);
    mEditor->selectAll();
    return true;
}

void ALPaneList::followEdit()
{
    // Gone, there is nothing to name; out of sight, it is done with.
    if (!editing())
    {
        return;
    }
    const S32 index = indexOf(mEditing);
    if (index < 0)
    {
        endEditing(false);
    }
    else if (!placeEditor(index))
    {
        endEditing(true);
    }
}

bool ALPaneList::placeEditor(S32 index)
{
    const LLScrollListColumn* column = getColumn(mEdit->column);
    if (!column || index < getScrollPos() || index >= getScrollPos() + llmax(1, getLinesPerPage()))
    {
        return false;
    }
    // Its words where the row's were, less the field's own margin.
    S32 pad_left = 0, pad_right = 0;
    mEditor->getTextPadding(&pad_left, &pad_right);
    const LLRect cell  = getCellRect(index, column->mIndex);
    const S32    left  = llmin(cell.mLeft + llmax(0, mEdit->indent - pad_left), cell.mRight - 20);
    const S32    right = llmin(cell.mRight, getItemListRect().mRight);
    mEditor->setShape(LLRect(left, cell.mTop, llmax(left + 20, right), cell.mBottom));
    return true;
}

void ALPaneList::endEditing(bool keep)
{
    if (!editing())
    {
        return;
    }
    // Over before anything is told, so that the keyboard coming back here
    // does not end it again.
    const Edit        edit = std::exchange(mEdit, std::nullopt).value();
    const std::string text = mEditor->getText();
    mEditing.clear();
    // The keyboard back with the rows where the field had it, rather than
    // with nothing, where the arrows would walk the avatar.
    if (mEditor->hasFocus())
    {
        setFocus(true);
    }
    mEditor->setVisible(false);
    if (keep && text != edit.text && edit.done)
    {
        edit.done(text);
    }
}
