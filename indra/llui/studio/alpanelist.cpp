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

#include "alkeychord.h"

#include "alemptystate.h"

#include "llclipboard.h"
#include "llmenugl.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"
#include "llsdutil.h"
#include "lltooltip.h"
#include "llui.h"
#include "lluictrlfactory.h"

#include <utility>

#include <boost/container_hash/hash.hpp>

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

    // How many characters a string shows as: its code points, which is
    // near enough for a table read in a fixed face.
    S32 displayWidth(const std::string& text)
    {
        S32 count = 0;
        for (const char c : text)
        {
            count += ((U8)c & 0xC0) != 0x80;
        }
        return count;
    }

    // A cell with a newline or a tab in it would break the table it is
    // being written into.
    std::string oneLine(std::string text)
    {
        for (char& c : text)
        {
            if (c == '\n' || c == '\r' || c == '\t')
            {
                c = ' ';
            }
        }
        return text;
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

void ALPaneList::clearRows()
{
    mKeyed.clear();
    mKeyOf.clear();
    mOrder.clear();
    LLScrollListCtrl::clearRows();
}

// static
size_t ALPaneList::saidBy(const LLScrollListCell::Params& cell)
{
    size_t said = 0;
    boost::hash_combine(said, cell.type());
    boost::hash_combine(said, cell.column());
    boost::hash_combine(said, cell.value().asString());
    boost::hash_combine(said, cell.label.isProvided() ? cell.label() : std::string());
    boost::hash_combine(said, cell.tool_tip());
    boost::hash_combine(said, reinterpret_cast<uintptr_t>(cell.font()));
    boost::hash_combine(said, static_cast<S32>(cell.font_halign()));
    boost::hash_combine(said, cell.enabled());
    boost::hash_combine(said, cell.visible());
    for (const auto* color : { &cell.color, &cell.font_color })
    {
        boost::hash_combine(said, color->isProvided());
        for (F32 channel : (*color)().mV)
        {
            boost::hash_combine(said, channel);
        }
    }
    return said;
}

LLScrollListItem* ALPaneList::addKeyed(Row& row)
{
    LLScrollListItem::Params item;
    item.value   = row.value;
    item.enabled = row.enabled;
    Keyed keyed;
    keyed.said.reserve(row.cells.size());
    for (LLScrollListCell::Params& cell : row.cells)
    {
        keyed.said.push_back(saidBy(cell));
        item.columns.add(std::move(cell));
    }
    LLScrollListItem* made = addRow(item, ADD_BOTTOM);
    if (made)
    {
        keyed.item      = made;
        mKeyOf[made]    = row.key;
        mKeyed[row.key] = std::move(keyed);
    }
    return made;
}

void ALPaneList::rewrite(LLScrollListItem* item, Row& row, std::vector<size_t>& said)
{
    item->setValue(row.value);
    item->setEnabled(row.enabled);
    said.resize(row.cells.size(), 0);
    for (size_t i = 0; i < row.cells.size(); ++i)
    {
        LLScrollListCell::Params& cell = row.cells[i];
        const size_t              now  = saidBy(cell);
        const LLScrollListColumn* column = getColumn(cell.column());
        if (now == said[i] || !column)
        {
            continue;
        }
        // Made again, only this cell, as wide as its column.
        said[i] = now;
        if (!cell.width.isProvided())
        {
            cell.width = column->getWidth();
        }
        if (LLScrollListCell* made = LLScrollListCell::create(cell))
        {
            item->setColumn(column->mIndex, made);
        }
    }
}

void ALPaneList::setRows(std::vector<Row> rows)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    // Kept by key through it: the rows chosen, and the row at the top.
    updateSort();
    const item_list&         items = getItemList();
    std::vector<std::string> chosen;
    for (const LLScrollListItem* item : items)
    {
        if (item->getSelected())
        {
            chosen.push_back(keyOf(item));
        }
    }
    const S32         scrolled = getScrollPos();
    const std::string top      = scrolled >= 0 && scrolled < static_cast<S32>(items.size()) ? keyOf(items[scrolled]) : std::string();

    // The rows there now all this list's own, and each key given once:
    // else the list is made again.
    boost::unordered_flat_map<std::string_view, size_t> given;
    bool                                                in_place = mKeyed.size() == items.size();
    for (const LLScrollListItem* item : items)
    {
        in_place = in_place && mKeyOf.contains(item);
    }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        in_place = in_place && !rows[i].key.empty() && given.emplace(rows[i].key, i).second;
    }
    if (!in_place)
    {
        deleteAllItems();
    }
    else
    {
        // Those gone, taken out where they stand.
        for (const std::string& key : mOrder)
        {
            if (!given.contains(key))
            {
                const auto        gone = mKeyed.find(key);
                LLScrollListItem* item = gone->second.item;
                mKeyOf.erase(item);
                mKeyed.erase(gone);
                deleteSingleItem(item);
            }
        }
    }
    // The rest made again where a cell says something else, the new ones
    // made, and all of them put in the order given -- which a sort by a
    // column then sorts, as it would have.
    item_list order;
    mOrder.clear();
    mOrder.reserve(rows.size());
    for (Row& row : rows)
    {
        mOrder.push_back(row.key);
        LLScrollListItem* item = nullptr;
        if (const auto kept = mKeyed.find(row.key); in_place && kept != mKeyed.end())
        {
            item = kept->second.item;
            rewrite(item, row, kept->second.said);
        }
        else
        {
            item = addKeyed(row);
        }
        if (item)
        {
            order.push_back(item);
        }
    }
    if (in_place)
    {
        getItemList() = std::move(order);
        setNeedsSort();
        dirtyColumns();
    }

    // What was chosen chosen again, and the row at the top back there.
    updateSort();
    if (!in_place && !chosen.empty())
    {
        bool first = true;
        for (const std::string& key : chosen)
        {
            LLScrollListItem* item = rowWithKey(key);
            if (!item)
            {
                continue;
            }
            if (first)
            {
                selectNthItem(getItemIndex(item));
                first = false;
            }
            else
            {
                item->setSelected(true);
            }
        }
    }
    if (!top.empty())
    {
        if (LLScrollListItem* item = rowWithKey(top))
        {
            setScrollPos(getItemIndex(item));
        }
    }
}

const std::string& ALPaneList::keyOf(const LLScrollListItem* item) const
{
    const auto found = mKeyOf.find(item);
    return found != mKeyOf.end() ? found->second : LLStringUtil::null;
}

LLScrollListItem* ALPaneList::rowWithKey(std::string_view key) const
{
    const auto found = mKeyed.find(key);
    return found != mKeyed.end() ? found->second.item : nullptr;
}

void ALPaneList::setEmpty(const std::string& headline, const std::string& sentence)
{
    if (!mEmpty)
    {
        ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
        ep.name               = "empty";
        ep.rect               = getItemListRect();
        ep.follows.flags      = FOLLOWS_ALL;
        ep.background_visible = false;
        ep.mouse_opaque       = false;
        mEmpty                = LLUICtrlFactory::create<ALEmptyState>(ep);
        addChild(mEmpty);
    }
    mEmptyHeadline = headline;
    mEmpty->say(headline, sentence);
    mEmpty->setVisible(saysEmpty());
}

bool ALPaneList::saysEmpty() const
{
    return mEmpty && !mEmptyHeadline.empty() && getItemCount() == 0;
}

void ALPaneList::setGo(std::function<void()> go)
{
    mGo = std::move(go);
    setDoubleClickCallback([this]()
    {
        if (mGo)
        {
            mWentByClick = true;
            mGo();
        }
    });
}

bool ALPaneList::handleUnicodeCharHere(llwchar uni_char)
{
    if (uni_char == ' ' && mSpace && getFirstSelected())
    {
        mSpace();
        return true;
    }
    return LLScrollListCtrl::handleUnicodeCharHere(uni_char);
}

bool ALPaneList::handleKeyHere(KEY key, MASK mask)
{
    // Shift-F10 or the Menu key: what a right click on the row chosen
    // would do -- a menu of the list's, or its owner's.
    if (ALKeyChords::isContextMenuKey(key, mask))
    {
        if (LLScrollListItem* item = getFirstSelected())
        {
            scrollToShowSelected();
            const LLRect cell = getCellRect(getItemIndex(item), 0);
            handleRightMouseDown(cell.mLeft + 4, cell.getCenterY(), MASK_NONE);
            return true;
        }
    }
    if (mask == MASK_NONE && (key == KEY_LEFT || key == KEY_RIGHT) && mFold)
    {
        if (const LLScrollListItem* item = getFirstSelected())
        {
            mFold(item->getValue(), key == KEY_LEFT);
            return true;
        }
    }
    if (mask == MASK_NONE && key == KEY_RETURN && mGo)
    {
        mGo();
        return true;
    }
    if (mask == MASK_NONE && key == KEY_ESCAPE && mBack)
    {
        mBack();
        return true;
    }
    // What the menu's Copy would, rather than the comma-separated rows the
    // edit menu would reach.
    if (mCopyable && key == 'C' && mask == MASK_CONTROL)
    {
        copyRows(getAllSelected());
        return true;
    }
    return LLScrollListCtrl::handleKeyHere(key, mask);
}

bool ALPaneList::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    if (!mCopyable)
    {
        return LLScrollListCtrl::handleRightMouseDown(x, y, mask);
    }
    // The right button does not choose, so Copy would have the wrong rows,
    // or none at all on the first click. The row under it, unless the click
    // landed inside a choice already made.
    LLScrollListItem* hit = hitItem(x, y);
    if (hit && !hit->getSelected())
    {
        selectItemAt(x, y, MASK_NONE);
    }
    mMenuCell.clear();
    if (hit)
    {
        if (const LLScrollListCell* cell = hit->getColumn(getColumnIndexFromOffset(x)))
        {
            mMenuCell = cell->getValue().asString();
        }
    }
    if (!LLMenuGL::sMenuContainer)
    {
        return true;
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    const LLHandle<LLUICtrl> self = getHandle();
    commit.add("PaneList.Copy", [self](LLUICtrl*, const LLSD& action)
    {
        if (ALPaneList* list = ALViewType::as<ALPaneList>(self.get()))
        {
            list->copyAction(action.asString());
        }
    });
    enable.add("PaneList.CopyEnabled", [self](LLUICtrl*, const LLSD& action)
    {
        const ALPaneList* list = ALViewType::as<ALPaneList>(self.get());
        return list && list->copyActionEnabled(action.asString());
    });
    if (mCopyMenu.make("menu_pane_list.xml"))
    {
        mCopyMenu.show(this, x, y);
    }
    return true;
}

void ALPaneList::copyAction(const std::string& action)
{
    if (action == "copy_cell")
    {
        LLClipboard::instance().copyToClipboard(mMenuCell, 0, (S32)mMenuCell.size());
    }
    else if (action == "copy")
    {
        copyRows(getAllSelected());
    }
    else if (action == "copy_all")
    {
        copyRows(getAllData());
    }
    else if (action == "select_all")
    {
        selectAll();
    }
}

bool ALPaneList::copyActionEnabled(const std::string& action) const
{
    if (action == "copy_cell")
    {
        return !mMenuCell.empty();
    }
    if (action == "copy_all" || action == "select_all")
    {
        return getFirstData() != nullptr;
    }
    return getFirstSelected() != nullptr;
}

void ALPaneList::copyRows(const std::vector<LLScrollListItem*>& rows)
{
    const std::string text = asText(rows);
    if (!text.empty())
    {
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
}

std::string ALPaneList::asText(const std::vector<LLScrollListItem*>& all)
{
    std::vector<const LLScrollListItem*> rows;
    for (const LLScrollListItem* item : all)
    {
        if (item->getEnabled())
        {
            rows.push_back(item);
        }
    }
    const S32 columns = getNumColumns();
    if (columns <= 0 || rows.empty())
    {
        return std::string();
    }

    // A column draws no heading when it needs none, which leaves its name
    // to stand for it here.
    std::vector<std::string> heading((size_t)columns);
    for (S32 i = 0; i < columns; ++i)
    {
        const LLScrollListColumn* column = getColumn(i);
        if (!column)
        {
            continue;
        }
        heading[i] = column->mLabel.getString();
        if (heading[i].empty())
        {
            heading[i] = column->mName;
            if (!heading[i].empty())
            {
                heading[i][0] = (char)toupper((U8)heading[i][0]);
            }
        }
    }

    std::vector<std::vector<std::string>> cells;
    std::vector<bool> used((size_t)columns, false);
    cells.reserve(rows.size());
    for (const LLScrollListItem* item : rows)
    {
        std::vector<std::string> line((size_t)columns);
        for (S32 i = 0; i < columns; ++i)
        {
            const LLScrollListCell* cell = item->getColumn(i);
            if (!cell)
            {
                continue;
            }
            line[i] = oneLine(cell->getValue().asString());
            used[i] = used[i] || !line[i].empty();
        }
        cells.push_back(std::move(line));
    }

    S32 last = -1;
    std::vector<S32> width((size_t)columns, 0);
    for (S32 i = 0; i < columns; ++i)
    {
        if (!used[i])
        {
            continue;
        }
        last = i;
        width[i] = displayWidth(heading[i]);
        for (const std::vector<std::string>& line : cells)
        {
            width[i] = llmax(width[i], displayWidth(line[i]));
        }
        // One long value -- a tool tip, a translated label -- would push
        // every other row's remaining columns out past reading distance,
        // so it is the one that steps out of line instead.
        width[i] = llmin(width[i], 48);
    }
    if (last < 0)
    {
        return std::string();
    }

    std::string text = mCopyCaption ? mCopyCaption() : std::string();
    if (!text.empty())
    {
        text += "\n\n";
    }
    auto append = [&](const std::vector<std::string>& line)
    {
        for (S32 i = 0; i <= last; ++i)
        {
            if (!used[i])
            {
                continue;
            }
            text += line[i];
            if (i != last)
            {
                text.append((size_t)llmax(0, width[i] - displayWidth(line[i])) + 2, ' ');
            }
        }
        text += '\n';
    };
    append(heading);
    for (const std::vector<std::string>& line : cells)
    {
        append(line);
    }
    return text;
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
    mWentByClick = false;
    // An arrow pressed turns its row's fold, before the list takes the
    // press as a choice.
    if (mask == MASK_NONE && mFold && mArrowAt)
    {
        if (const LLScrollListItem* hit = hitItem(x, y); hit && mArrowAt(hit, x))
        {
            mFold(hit->getValue(), std::nullopt);
            return true;
        }
    }
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
    if (std::exchange(mWentByClick, false))
    {
        // The release of a double-click that went where its row goes: the
        // first click chose the row, and choosing it again now would take
        // the keyboard back from where it went.
        if (hasMouseCapture())
        {
            gFocusMgr.setMouseCapture(nullptr);
        }
        return true;
    }
    return LLScrollListCtrl::handleMouseUp(x, y, mask);
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
    LLScrollListItem* item = hitItem(x, y);
    if (!item && getItemListRect().pointInRect(x, y))
    {
        return LLUICtrl::handleToolTip(x, y, mask);
    }
    // The row's own, made now.
    const std::string tip = item && mRowTip ? mRowTip(item) : std::string();
    if (!tip.empty())
    {
        const S32 index = getItemIndex(item);
        LLRect    sticky;
        localRectToScreen(getCellRect(index, getColumnIndexFromOffset(x)), &sticky);
        LLToolTipMgr::instance().show(LLToolTip::Params()
                                          .message(tip)
                                          .font(LLFontGL::getFontSansSerifSmall())
                                          .pos(LLCoordGL(sticky.mLeft - 5, sticky.mTop + 6))
                                          .delay_time(0.2f)
                                          .sticky_rect(sticky));
        return true;
    }
    return LLScrollListCtrl::handleToolTip(x, y, mask);
}

void ALPaneList::draw()
{
    // Shown while there is nothing, whatever emptied the list.
    if (mEmpty)
    {
        mEmpty->setVisible(saysEmpty());
    }
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
