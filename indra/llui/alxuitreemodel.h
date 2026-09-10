/**
 * @file alxuitreemodel.h
 * @brief Folder-view model over a built view tree for XUI Studio's hierarchy pane.
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

#include "alxuiselection.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodel.h"
#include "llpointer.h"
#include "lltimer.h"

#include <functional>
#include <string>

#include <boost/unordered_map.hpp>

class ALXUISourceMap;
class ALXUITreeItem;

// Rows keep the order their views were created in, which is the order the
// file lists them.
class ALXUITreeSort
{
public:
    bool operator()(const ALXUITreeItem* a, const ALXUITreeItem* b) const;
};

// Type-to-filter over a row's name and tag, and the switch that hides the
// views the file did not create.
class ALXUITreeFilter final : public LLFolderViewFilter
{
public:
    ALXUITreeFilter();
    ~ALXUITreeFilter() override = default;

    void setFilterSubString(const std::string& text);
    void setShowCodeBuilt(bool show);
    bool getShowCodeBuilt() const { return mShowCodeBuilt; }

    bool check(const LLFolderViewModelItem* item) override;
    bool checkFolder(const LLFolderViewModelItem* folder) const override { return true; }

    void setEmptyLookupMessage(const std::string& message) override { mEmptyLookupMessage = message; }
    std::string getEmptyLookupMessage(bool is_empty_folder = false) const override { return mEmptyLookupMessage; }
    bool showAllResults() const override { return false; }
    Match getFilterMatch(LLFolderViewModelItem* item) const override;

    bool isActive() const override { return !mSubString.empty() || !mShowCodeBuilt; }
    bool isModified() const override { return mModified; }
    void clearModified() override { mModified = false; }
    const std::string& getName() const override { return mName; }
    const std::string& getFilterText() override { return mSubString; }
    void setModified(EFilterModified behavior = FILTER_RESTART) override;

    void resetTime(S32 timeout) override
    {
        mFilterTime.reset();
        mFilterTime.setTimerExpirySec((F32)timeout / 1000.f);
    }
    bool isTimedOut() override { return mFilterTime.hasExpired(); }

    bool isDefault() const override { return !isActive(); }
    bool isNotDefault() const override { return isActive(); }
    void markDefault() override {}
    void resetDefault() override {}

    S32 getCurrentGeneration() const override { return mGeneration; }
    S32 getFirstSuccessGeneration() const override { return mGeneration; }
    S32 getFirstRequiredGeneration() const override { return mGeneration; }

private:
    std::string mName;
    std::string mEmptyLookupMessage;
    std::string mSubString;         // lowercased
    LLTimer     mFilterTime;
    S32         mGeneration = 1;
    bool        mModified = false;
    bool        mShowCodeBuilt = true;
};

class ALXUITreeModel final
:   public LLFolderViewModel<ALXUITreeSort, ALXUITreeItem, ALXUITreeItem, ALXUITreeFilter>
{
public:
    typedef LLFolderViewModel<ALXUITreeSort, ALXUITreeItem, ALXUITreeItem, ALXUITreeFilter> base_t;
    typedef std::function<void(ALXUITreeItem& item, LLMenuGL& menu, U32 flags)> context_menu_fn_t;

    ALXUITreeModel();
    ~ALXUITreeModel() override;

    // One item per view under the root, the root included, in creation
    // order; the source map says which of them the file created. Replaces
    // whatever was built before.
    ALXUITreeItem* build(LLView* root, const ALXUISourceMap& source_map);
    void clear();

    ALXUITreeItem* rootItem() const { return mRoot.get(); }
    ALXUITreeItem* itemFor(const ALXUISelection::path_t& path) const;
    ALXUITreeItem* itemFor(const LLView* view) const;
    size_t count() const { return mByView.size(); }

    // A subtree built again, and the rows that were showing the old one
    // pointed at what replaced it. This works because the document did not
    // change shape: every row still answers to the path it did, and what
    // changed is which view is at that path. False where a row's path names
    // nothing any more, which leaves that row pointing at a view that has
    // gone -- the caller has to make the rows again, and has to be told.
    bool rebind(const ALXUISelection::path_t& path, LLView* root);

    void setContextMenuHandler(context_menu_fn_t fn) { mContextMenu = std::move(fn); }
    void buildContextMenu(ALXUITreeItem& item, LLMenuGL& menu, U32 flags);

    // How many findings sit on a row and everything below it, which is
    // what a row shows beside its name. Null until something says.
    typedef std::function<S32(const ALXUISelection::path_t& path)> badge_fn_t;
    void setBadgeProvider(badge_fn_t fn) { mBadge = std::move(fn); }
    S32 badgeFor(const ALXUISelection::path_t& path) const { return mBadge ? mBadge(path) : 0; }

    // Hover runs both ways: the row the canvas is over is drawn marked, and
    // the row the mouse is over is reported, null when it leaves the rows.
    typedef std::function<void(const ALXUITreeItem* item)> hover_fn_t;
    void setHoverHandler(hover_fn_t fn) { mHover = std::move(fn); }
    void rowHovered(const ALXUITreeItem* item);
    void setCanvasHover(const ALXUITreeItem* item) { mCanvasHover = item; }
    const ALXUITreeItem* canvasHover() const { return mCanvasHover; }

    bool startDrag(std::vector<LLFolderViewModelItem*>& items) override { return false; }

private:
    ALXUITreeItem* buildItem(LLView* view, const ALXUISourceMap& source_map,
                             const ALXUISelection::path_t& path, S32 order);

    LLPointer<ALXUITreeItem>                            mRoot;
    boost::unordered_map<std::string, ALXUITreeItem*>   mByPath;
    boost::unordered_map<const LLView*, ALXUITreeItem*> mByView;
    context_menu_fn_t                                   mContextMenu;
    hover_fn_t                                          mHover;
    badge_fn_t                                          mBadge;
    const ALXUITreeItem*                                mCanvasHover = nullptr;
    const ALXUITreeItem*                                mRowHover = nullptr;
};

// A row: the view, the tag it was built as, whether the file created it,
// and the view's visibility as authored so a session-only flip can be told
// from what the file says.
class ALXUITreeItem final : public LLFolderViewModelItemCommon
{
public:
    ALXUITreeItem(LLView* view, std::string tag, bool from_xml, S32 order,
                  ALXUISelection::path_t path, ALXUITreeModel& model);

    LLView* getView() const { return mView; }
    // Pointed at what replaced it, when the element this row is about was
    // built again. The model keeps the other way round and does both.
    void setView(LLView* view) { mView = view; }
    const std::string& getTag() const { return mTag; }
    bool isFromXML() const { return mFromXML; }
    S32 getOrder() const { return mOrder; }
    const ALXUISelection::path_t& getPath() const { return mPath; }
    ALXUITreeModel& getModel() const { return mModel; }

    // Visibility the tool flipped for the session, against what the file
    // built. Nothing here touches a document.
    bool isAuthoredVisible() const { return mAuthoredVisible; }
    bool isShown() const;
    void setShown(bool shown);
    void toggleShown() { setShown(!isShown()); }

    // LLFolderViewModelItem
    const std::string& getName() const override { return mName; }
    const std::string& getDisplayName() const override { return mName; }
    const std::string& getSearchableName() const override { return mSearchable; }
    std::string getSearchableDescription() const override { return mTag; }
    std::string getSearchableCreatorName() const override { return LLStringUtil::null; }
    std::string getSearchableUUIDString() const override { return LLStringUtil::null; }

    LLPointer<LLUIImage> getIcon() const override { return nullptr; }
    LLFontGL::StyleFlags getLabelStyle() const override;
    std::string getLabelSuffix() const override;

    void openItem() override {}
    void closeItem() override {}
    void selectItem() override {}
    void navigateToFolder(bool new_window = false, bool change_mode = false) override {}

    bool isFavorite() const override { return false; }
    bool isItemRenameable() const override { return false; }
    bool renameItem(const std::string& new_name) override { return false; }
    bool isItemMovable() const override { return false; }
    void move(LLFolderViewModelItem* parent_listener) override {}
    bool isItemRemovable(bool check_worn = true) const override { return false; }
    bool isItemInTrash() const override { return false; }
    bool removeItem() override { return false; }
    void removeBatch(std::vector<LLFolderViewModelItem*>& batch) override {}
    bool isItemCopyable(bool can_copy_as_link = true) const override { return false; }
    bool copyToClipboard() const override { return false; }
    bool cutToClipboard() override { return false; }
    bool isClipboardPasteable() const override { return false; }
    void pasteFromClipboard() override {}
    void pasteLinkFromClipboard() override {}
    bool isAgentInventory() const override { return false; }
    bool isAgentInventoryRoot() const override { return false; }
    void buildContextMenu(LLMenuGL& menu, U32 flags) override;
    bool hasChildren() const override { return getChildrenCount() > 0; }
    bool dragOrDrop(MASK mask, bool drop, EDragAndDropType cargo_type,
                    void* cargo_data, std::string& tooltip_msg) override { return false; }

    bool filter(LLFolderViewFilter& filter) override;
    void setPassedFilter(bool passed, S32 filter_generation,
                         std::string::size_type string_offset = std::string::npos,
                         std::string::size_type string_size = 0) override;

private:
    LLView*                 mView;
    std::string             mTag;
    std::string             mName;
    std::string             mSearchable;    // lowercased name and tag
    ALXUISelection::path_t  mPath;
    ALXUITreeModel&         mModel;
    S32                     mOrder;
    bool                    mFromXML;
    bool                    mAuthoredVisible;
    bool                    mPrevPassedAllFilters = false;
};

// The rows' widgets. Each draws an eye at its right edge that flips the
// view's visibility for the session, and a click there goes to the eye
// rather than the selection.
class ALXUITreeFolder final : public LLFolderViewFolder
{
public:
    AL_VIEW_TYPE(ALXUITreeFolder, LLFolderViewFolder);

    typedef LLFolderViewFolder::Params Params;
    ALXUITreeFolder(const Params& p) : LLFolderViewFolder(p) {}
    ~ALXUITreeFolder() override = default;

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
};

class ALXUITreeRow final : public LLFolderViewItem
{
public:
    AL_VIEW_TYPE(ALXUITreeRow, LLFolderViewItem);

    typedef LLFolderViewItem::Params Params;
    ALXUITreeRow(const Params& p) : LLFolderViewItem(p) {}
    ~ALXUITreeRow() override = default;

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
};

// Shared by both widgets: the eye's rectangle within a row, its drawing,
// whether a click landed on it, and the mark for the row the canvas is over.
namespace ALXUITreeEye
{
    constexpr S32 WIDTH = 18;

    LLRect rectIn(const LLFolderViewItem& row);
    void draw(const LLFolderViewItem& row);
    bool hit(const LLFolderViewItem& row, S32 x, S32 y);
    void drawCanvasHover(const LLFolderViewItem& row);
    void reportHover(const LLFolderViewItem& row, S32 y, bool inside);
}
