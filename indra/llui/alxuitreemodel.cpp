/**
 * @file alxuitreemodel.cpp
 * @brief Folder-view model over a built view tree for the XUI tool's hierarchy pane.
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

#include "alxuitreemodel.h"

#include "alxuisourcemap.h"
#include "llrender2dutils.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llview.h"

// ---------------------------------------------------------------------------
// ALXUITreeSort
// ---------------------------------------------------------------------------
bool ALXUITreeSort::operator()(const ALXUITreeItem* a, const ALXUITreeItem* b) const
{
    return a->getOrder() < b->getOrder();
}

// ---------------------------------------------------------------------------
// ALXUITreeFilter
// ---------------------------------------------------------------------------
ALXUITreeFilter::ALXUITreeFilter()
:   mName("xui_tree")
{
}

void ALXUITreeFilter::setFilterSubString(const std::string& text)
{
    const std::string lower = utf8str_tolower(text);
    if (lower == mSubString)
    {
        return;
    }
    mSubString = lower;
    setModified();
}

void ALXUITreeFilter::setShowCodeBuilt(bool show)
{
    if (show == mShowCodeBuilt)
    {
        return;
    }
    mShowCodeBuilt = show;
    setModified();
}

void ALXUITreeFilter::setModified(EFilterModified behavior)
{
    // The tree is a few hundred rows: every change starts over.
    ++mGeneration;
    mModified = true;
}

bool ALXUITreeFilter::check(const LLFolderViewModelItem* item)
{
    const ALXUITreeItem* row = static_cast<const ALXUITreeItem*>(item);
    if (!mShowCodeBuilt && !row->isFromXML())
    {
        return false;
    }
    return mSubString.empty() || row->getSearchableName().find(mSubString) != std::string::npos;
}

LLFolderViewFilter::Match ALXUITreeFilter::getFilterMatch(LLFolderViewModelItem* item) const
{
    Match match;
    if (mSubString.empty())
    {
        return match;
    }
    const std::string& label = item->getDisplayName();
    const std::string lower = utf8str_tolower(label);
    const std::string::size_type at = lower.find(mSubString);
    if (at == std::string::npos)
    {
        return match;
    }
    if (utf8str_is_ascii(label) && utf8str_is_ascii(mSubString))
    {
        match.mOffset = at;
        match.mLength = mSubString.size();
        return match;
    }
    match.mOffset = utf8str_bytes_from_cased_bytes(label, at, false);
    const size_t end = utf8str_bytes_from_cased_bytes(label, at + mSubString.size(), false);
    match.mLength = end > match.mOffset ? end - match.mOffset : 0;
    return match;
}

// ---------------------------------------------------------------------------
// ALXUITreeModel
// ---------------------------------------------------------------------------
ALXUITreeModel::ALXUITreeModel()
:   base_t(new ALXUITreeSort(), new ALXUITreeFilter())
{
}

ALXUITreeModel::~ALXUITreeModel()
{
    clear();
}

void ALXUITreeModel::clear()
{
    // The items are reference counted and the folder view holds them as
    // long as its widgets live; releasing the maps releases this side.
    mRoot = nullptr;
    mByPath.clear();
    mByView.clear();
}

ALXUITreeItem* ALXUITreeModel::build(LLView* root, const ALXUISourceMap& source_map)
{
    clear();
    if (!root)
    {
        return nullptr;
    }
    mRoot = buildItem(root, source_map, ALXUISelection::path_t(), 0);
    return mRoot.get();
}

ALXUITreeItem* ALXUITreeModel::buildItem(LLView* view, const ALXUISourceMap& source_map,
                                         const ALXUISelection::path_t& path, S32 order)
{
    const ALXUISourceMap::Origin* origin = source_map.find(view);
    std::string tag;
    if (origin)
    {
        tag = origin->tag;
    }
    else if (const std::string* registered = LLUICtrlFactory::widgetTag(view->viewType()))
    {
        tag = *registered;
    }
    else
    {
        tag = view->viewType()->mName;
    }

    ALXUITreeItem* item = new ALXUITreeItem(view, tag, origin != nullptr, order, path, *this);
    mByPath.emplace(ALXUISelection::toString(path), item);
    mByView.emplace(view, item);

    // Children in creation order, which is the reverse of the list; a
    // repeated name among them takes its ordinal in that order.
    const LLView::child_list_t& children = *view->getChildList();
    std::unordered_map<std::string, S32> seen;
    S32 child_order = 0;
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        LLView* child = *it;
        ALXUISelection::path_t child_path(path);
        child_path.push_back(ALXUISelection::step(child->getName(), seen[child->getName()]++));
        item->addChild(buildItem(child, source_map, child_path, child_order++));
    }
    return item;
}

ALXUITreeItem* ALXUITreeModel::itemFor(const ALXUISelection::path_t& path) const
{
    auto it = mByPath.find(ALXUISelection::toString(path));
    return it == mByPath.end() ? nullptr : it->second;
}

ALXUITreeItem* ALXUITreeModel::itemFor(const LLView* view) const
{
    auto it = mByView.find(view);
    return it == mByView.end() ? nullptr : it->second;
}

void ALXUITreeModel::buildContextMenu(ALXUITreeItem& item, LLMenuGL& menu, U32 flags)
{
    if (mContextMenu)
    {
        mContextMenu(item, menu, flags);
    }
}

void ALXUITreeModel::rowHovered(const ALXUITreeItem* item)
{
    if (item == mRowHover)
    {
        return;
    }
    mRowHover = item;
    if (mHover)
    {
        mHover(item);
    }
}

// ---------------------------------------------------------------------------
// ALXUITreeItem
// ---------------------------------------------------------------------------
ALXUITreeItem::ALXUITreeItem(LLView* view, std::string tag, bool from_xml, S32 order,
                             ALXUISelection::path_t path, ALXUITreeModel& model)
:   LLFolderViewModelItemCommon(model),
    mView(view),
    mTag(std::move(tag)),
    mName(view->getName()),
    mPath(std::move(path)),
    mModel(model),
    mOrder(order),
    mFromXML(from_xml),
    mAuthoredVisible(view->getVisible())
{
    mSearchable = utf8str_tolower(mName) + " " + mTag;
}

bool ALXUITreeItem::isShown() const
{
    return mView->getVisible();
}

void ALXUITreeItem::setShown(bool shown)
{
    mView->setVisible(shown);
}

LLFontGL::StyleFlags ALXUITreeItem::getLabelStyle() const
{
    return mFromXML ? LLFontGL::NORMAL : LLFontGL::ITALIC;
}

std::string ALXUITreeItem::getLabelSuffix() const
{
    const LLRect& rect = mView->getRect();
    std::string suffix;
    // The findings on this row and everything below it, first, so it sits
    // against the name rather than at the end of the line.
    if (const S32 findings = mModel.badgeFor(mPath))
    {
        suffix += "  [" + std::to_string(findings) + "]";
    }
    suffix += "  " + mTag + "  " + std::to_string(rect.getWidth()) + "x" + std::to_string(rect.getHeight());
    if (!isShown())
    {
        suffix += isAuthoredVisible() ? "  hidden here" : "  hidden";
    }
    if (!mView->getEnabled())
    {
        suffix += "  disabled";
    }
    return suffix;
}

void ALXUITreeItem::buildContextMenu(LLMenuGL& menu, U32 flags)
{
    mModel.buildContextMenu(*this, menu, flags);
}

bool ALXUITreeItem::filter(LLFolderViewFilter& filter)
{
    const S32 generation = filter.getCurrentGeneration();
    const S32 required = filter.getFirstRequiredGeneration();

    if (getLastFilterGeneration() >= required
        && getLastFolderFilterGeneration() >= required
        && !passedFilter(required))
    {
        setPassedFilter(false, generation);
        setPassedFolderFilter(false, generation);
        return true;
    }

    setPassedFolderFilter(true, generation);

    bool keep_going = true;
    if (!mChildren.empty()
        && (getLastFilterGeneration() < required || descendantsPassedFilter(required)))
    {
        for (auto& childp : mChildren)
        {
            ALXUITreeItem* child = static_cast<ALXUITreeItem*>(childp.get());
            if (child->getLastFilterGeneration() < generation)
            {
                keep_going = child->filter(filter);
            }
            if (child->passedFilter())
            {
                ALXUITreeItem* up = this;
                while (up && up->mMostFilteredDescendantGeneration < generation)
                {
                    up->mMostFilteredDescendantGeneration = generation;
                    up = static_cast<ALXUITreeItem*>(up->mParent);
                }
            }
            if (!keep_going)
            {
                return false;
            }
        }
    }

    if (filter.isTimedOut())
    {
        return false;
    }

    const bool passed = filter.check(this);
    const LLFolderViewFilter::Match match = filter.getFilterMatch(this);
    setPassedFilter(passed, generation, match.mOffset, match.mLength);
    return true;
}

void ALXUITreeItem::setPassedFilter(bool passed, S32 filter_generation,
                                    std::string::size_type string_offset,
                                    std::string::size_type string_size)
{
    // Only an arrange applies pass and fail to the widgets, and nothing
    // else asks for one when a filter changes.
    const S32 last_generation = mLastFilterGeneration;
    LLFolderViewModelItemCommon::setPassedFilter(passed, filter_generation, string_offset, string_size);
    const bool before = mPrevPassedAllFilters;
    mPrevPassedAllFilters = passedFilter(filter_generation);
    if (before != mPrevPassedAllFilters
        || (mPrevPassedAllFilters && last_generation < mRootViewModel.getFilter().getFirstSuccessGeneration()))
    {
        LLFolderViewFolder* parent_folder = mFolderViewItem ? mFolderViewItem->getParentFolder() : nullptr;
        if (parent_folder)
        {
            parent_folder->requestArrange();
        }
    }
}

// ---------------------------------------------------------------------------
// The eye
// ---------------------------------------------------------------------------
namespace
{
    LLRect eyeRect(S32 row_width, S32 row_height, S32 item_height)
    {
        const S32 right = row_width - 2;
        const S32 top = row_height - 1;
        return LLRect(right - ALXUITreeEye::WIDTH, top, right, top - item_height + 2);
    }

    void drawEye(const LLRect& rect, bool shown, bool selected)
    {
        static const LLUIImagePtr on = LLUI::getUIImage("Profile_Group_Visibility_On");
        static const LLUIImagePtr off = LLUI::getUIImage("Profile_Group_Visibility_Off");
        const LLUIImagePtr& image = shown ? on : off;
        if (image.isNull())
        {
            return;
        }
        const S32 size = llmin(rect.getWidth(), rect.getHeight()) - 2;
        const S32 x = rect.mLeft + (rect.getWidth() - size) / 2;
        const S32 y = rect.mBottom + (rect.getHeight() - size) / 2;
        LLColor4 color = LLColor4::white;
        if (!shown && !selected)
        {
            color.mV[VALPHA] = 0.5f;
        }
        image->draw(x, y, size, size, color);
    }
}

// static
LLRect ALXUITreeEye::rectIn(const LLFolderViewItem& row)
{
    return eyeRect(row.getRect().getWidth(), row.getRect().getHeight(), row.getItemHeight());
}

// static
void ALXUITreeEye::draw(const LLFolderViewItem& row)
{
    const ALXUITreeItem* item = static_cast<const ALXUITreeItem*>(row.getViewModelItem());
    if (!item)
    {
        return;
    }
    drawEye(rectIn(row), item->isShown(), row.isSelected());
}

// static
bool ALXUITreeEye::hit(const LLFolderViewItem& row, S32 x, S32 y)
{
    return rectIn(row).pointInRect(x, y);
}

// static
void ALXUITreeEye::drawCanvasHover(const LLFolderViewItem& row)
{
    const ALXUITreeItem* item = static_cast<const ALXUITreeItem*>(row.getViewModelItem());
    if (!item || item->getModel().canvasHover() != item)
    {
        return;
    }
    static const LLUIColor color = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4(0.5f, 0.5f, 0.5f, 0.3f));
    const S32 top = row.getRect().getHeight();
    LLColor4 fill = color.get();
    fill.mV[VALPHA] *= 0.5f;
    gl_rect_2d(0, top, row.getRect().getWidth(), top - row.getItemHeight(), fill);
}

// static
void ALXUITreeEye::reportHover(const LLFolderViewItem& row, S32 y, bool inside)
{
    const ALXUITreeItem* item = static_cast<const ALXUITreeItem*>(row.getViewModelItem());
    if (!item)
    {
        return;
    }
    // A folder's rectangle holds its children; only the strip at the top
    // is the row itself.
    const bool on_row = inside && y > row.getRect().getHeight() - row.getItemHeight();
    item->getModel().rowHovered(on_row ? item : nullptr);
}

// ---------------------------------------------------------------------------
// ALXUITreeFolder, ALXUITreeRow
// ---------------------------------------------------------------------------
void ALXUITreeFolder::draw()
{
    ALXUITreeEye::drawCanvasHover(*this);
    LLFolderViewFolder::draw();
    ALXUITreeEye::draw(*this);
}

bool ALXUITreeFolder::handleHover(S32 x, S32 y, MASK mask)
{
    const bool handled = LLFolderViewFolder::handleHover(x, y, mask);
    if (!isOpen() || y > getRect().getHeight() - getItemHeight())
    {
        ALXUITreeEye::reportHover(*this, y, true);
    }
    return handled;
}

void ALXUITreeFolder::onMouseLeave(S32 x, S32 y, MASK mask)
{
    LLFolderViewFolder::onMouseLeave(x, y, mask);
    ALXUITreeEye::reportHover(*this, y, false);
}

bool ALXUITreeFolder::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (ALXUITreeEye::hit(*this, x, y))
    {
        static_cast<ALXUITreeItem*>(getViewModelItem())->toggleShown();
        return true;
    }
    return LLFolderViewFolder::handleMouseDown(x, y, mask);
}

bool ALXUITreeFolder::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (ALXUITreeEye::hit(*this, x, y))
    {
        return true;
    }
    return LLFolderViewFolder::handleDoubleClick(x, y, mask);
}

void ALXUITreeRow::draw()
{
    ALXUITreeEye::drawCanvasHover(*this);
    LLFolderViewItem::draw();
    ALXUITreeEye::draw(*this);
}

bool ALXUITreeRow::handleHover(S32 x, S32 y, MASK mask)
{
    const bool handled = LLFolderViewItem::handleHover(x, y, mask);
    ALXUITreeEye::reportHover(*this, y, true);
    return handled;
}

void ALXUITreeRow::onMouseLeave(S32 x, S32 y, MASK mask)
{
    LLFolderViewItem::onMouseLeave(x, y, mask);
    ALXUITreeEye::reportHover(*this, y, false);
}

bool ALXUITreeRow::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (ALXUITreeEye::hit(*this, x, y))
    {
        static_cast<ALXUITreeItem*>(getViewModelItem())->toggleShown();
        return true;
    }
    return LLFolderViewItem::handleMouseDown(x, y, mask);
}
