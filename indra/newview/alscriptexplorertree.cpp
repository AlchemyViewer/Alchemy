/**
 * @file alscriptexplorertree.cpp
 * @brief Script Studio's explorer shown as a tree, the inventory's folder view over the explorer's rows.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptexplorertree.h"

#include "alfolderfilter.h"

#include "llclipboard.h"
#include "llfolderview.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodel.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <optional>

static LLPanelInjector<ALScriptExplorerTree> t_script_studio_explorer_tree("script_studio_explorer_tree");

// --- a row, as the folder view holds it --------------------------------------------------

class ALScriptExplorerTree::Node final : public ALFilteredItem
{
public:
    Node(ALScriptExplorerTree& tree, LLFolderViewModelInterface& model, std::string key, bool folder)
    :   ALFilteredItem(model),
        mTree(tree),
        mKey(std::move(key)),
        mFolder(folder)
    {
    }

    // What it stands for and how it is shown; true where that changed.
    bool set(const Row& row, const Look& look)
    {
        const bool changed = look.label != mLabel || look.suffix != mSuffix || look.icon != mIconName || row.name != mName;
        mValue  = row.value;
        mName   = row.name;
        mLabel  = look.label;
        mSuffix = look.suffix;
        mKnown  = row.known;
        if (look.icon != mIconName)
        {
            mIconName = look.icon;
            mIcon     = look.icon.empty() ? LLPointer<LLUIImage>() : LLUI::getUIImage(look.icon);
        }
        return changed;
    }

    const std::string& key() const { return mKey; }
    const LLSD&        value() const { return mValue; }
    bool               folder() const { return mFolder; }
    bool               known() const { return mKnown; }
    const std::string& suffix() const { return mSuffix; }
    S32                mOrder = 0;
    // How the rows last said it was, open or folded: the folder is opened
    // or folded again only where that changed, leaving what a person did
    // meanwhile -- a folder shut while a filter looks stays shut.
    std::optional<bool> mRowFolded;
    LLFolderViewItem*   widget() const { return mFolderViewItem; }

    const std::string& getName() const override { return mName; }
    const std::string& getDisplayName() const override { return mLabel; }
    const std::string& getSearchableName() const override { return mLabel; }
    std::string        getSearchableDescription() const override { return std::string(); }
    std::string        getSearchableCreatorName() const override { return std::string(); }
    std::string        getSearchableUUIDString() const override { return std::string(); }

    LLPointer<LLUIImage> getIcon() const override { return mIcon; }
    LLFontGL::StyleFlags getLabelStyle() const override { return LLFontGL::NORMAL; }
    std::string          getLabelSuffix() const override { return mSuffix; }

    // A folder opened or folded by its arrow, a script or notecard opened by
    // a double-click; nothing for the tree's root.
    void openItem() override { mTree.opened(*this); }
    void closeItem() override { mTree.closed(*this); }
    void selectItem() override {}
    void navigateToFolder(bool new_window = false, bool change_mode = false) override {}

    bool isFavorite() const override { return false; }
    bool isItemRenameable() const override { return mValue.isDefined() && mTree.mHooks.renameable && mTree.mHooks.renameable(mValue); }
    bool renameItem(const std::string& new_name) override
    {
        if (mTree.mHooks.renamed)
        {
            mTree.mHooks.renamed(mValue, new_name);
        }
        return true;
    }
    // An item may be dragged out -- to the inventory, to another prim --
    // which the folder view starts only from a row that says so; what
    // goes is the owner's to say (Hooks::drag). A prim or an object is
    // not carried off.
    bool isItemMovable() const override { return !mFolder && mValue.isDefined(); }
    void move(LLFolderViewModelItem* parent_listener) override {}
    // Deleting is the explorer's, asked about first; the folder view's own
    // delete, the Edit menu's, finds nothing to delete here.
    bool isItemRemovable(bool check_worn = true) const override { return false; }
    bool isItemInTrash() const override { return false; }
    bool removeItem() override { return false; }
    void removeBatch(std::vector<LLFolderViewModelItem*>& batch) override {}
    // A copy is the names of what is chosen, all at once, whichever is asked.
    bool isItemCopyable(bool can_copy_as_link = true) const override { return mValue.isDefined(); }
    bool copyToClipboard() const override
    {
        mTree.copy();
        return true;
    }
    bool cutToClipboard() override { return false; }
    bool isClipboardPasteable() const override { return false; }
    void pasteFromClipboard() override {}
    void pasteLinkFromClipboard() override {}
    bool isAgentInventory() const override { return false; }
    bool isAgentInventoryRoot() const override { return false; }
    void buildContextMenu(LLMenuGL& menu, U32 flags) override {}
    bool potentiallyVisible() override { return true; }
    bool hasChildren() const override { return !mChildren.empty(); }

    bool dragOrDrop(MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, std::string& tooltip_msg) override
    {
        return mTree.mHooks.drop && mTree.mHooks.drop(mValue, mask, drop, cargo_type, cargo_data, tooltip_msg);
    }

private:
    ALScriptExplorerTree& mTree;
    const std::string     mKey;
    const bool            mFolder;
    LLSD                  mValue;
    std::string           mName;
    std::string           mLabel;
    std::string           mSuffix;
    std::string           mIconName;
    LLPointer<LLUIImage>  mIcon;
    bool                  mKnown = true;
};

// --- what the folder view is told of the rows ----------------------------------------------

// Every row passes; the filter's words are only lit. Its generation moves
// when the words do, so that each row is lit again.
class ALScriptExplorerTree::Filter final : public ALFolderFilter
{
public:
    Filter() : ALFolderFilter("script_studio_explorer") {}

    bool check(const LLFolderViewModelItem* item) override { return true; }
    // Nothing hidden, whatever is lit: the tree is as it would be.
    bool isActive() const override { return false; }
};

// The rows' own order: the pinned first, a linkset's prims as they are
// linked, the prims holding nothing after the rest.
class ALScriptExplorerTree::Sort
{
public:
    bool operator()(const Node* a, const Node* b) const { return a->mOrder < b->mOrder; }
};

class ALScriptExplorerTree::ViewModel final : public LLFolderViewModel<Sort, Node, Node, Filter>
{
public:
    explicit ViewModel(ALScriptExplorerTree& tree) : LLFolderViewModel<Sort, Node, Node, Filter>(new Sort(), new Filter()), mTree(tree) {}

    bool startDrag(std::vector<LLFolderViewModelItem*>& items) override { return mTree.mHooks.drag && mTree.mHooks.drag(); }
    // A prim whose contents the region has not said yet is not complete:
    // its arrow is drawn, for it to be opened and asked.
    bool isFolderComplete(LLFolderViewFolder* folder) override
    {
        const Node* node = folder ? static_cast<const Node*>(folder->getViewModelItem()) : nullptr;
        return !node || node->known();
    }

private:
    ALScriptExplorerTree& mTree;
};

// --- the panel -------------------------------------------------------------------------------

ALScriptExplorerTree::ALScriptExplorerTree(const LLPanel::Params& params) : LLPanel(params) {}

ALScriptExplorerTree::~ALScriptExplorerTree()
{
    if (LLEditMenuHandler::gEditMenuHandler == mFolderView && mFolderView)
    {
        LLEditMenuHandler::gEditMenuHandler = nullptr;
    }
    // The folder view goes while its view model and its rows' nodes are
    // still here to be let go of, not after, as a child would.
    deleteAllChildren();
    mFolderView = nullptr;
    mNodes.clear();
}

bool ALScriptExplorerTree::postBuild()
{
    mViewModel = std::make_unique<ViewModel>(*this);
    mRoot      = new Node(*this, *mViewModel, std::string(), true);

    LLFolderView::Params p(LLUICtrlFactory::getDefaultParams<LLFolderView>());
    p.name              = "explorer_folders";
    p.title             = std::string();
    p.rect              = LLRect(0, 0, getRect().getWidth(), 0);
    p.parent_panel      = this;
    p.listener          = mRoot;
    p.view_model        = mViewModel.get();
    p.root              = nullptr;
    p.use_ellipses      = true;
    p.use_label_suffix  = true;
    p.allow_multiselect = true;
    p.allow_drag        = true;
    // Never shown: the explorer's own menu is, from a right-click here.
    p.options_menu      = "menu_script_studio_explorer.xml";
    mFolderView         = LLUICtrlFactory::create<LLFolderView>(p);

    // A place for the keyboard, as the inventory's scroller is: given the
    // panel's, it passes the keys to the folder view, which is no control
    // and takes none itself.
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLFolderViewScrollContainer>());
    sp.rect     = getLocalRect();
    sp.tab_stop = true;
    LLScrollContainer* scroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(sp);
    scroller->setFollowsAll();
    addChild(scroller);
    scroller->addChild(mFolderView);
    mFolderView->setScrollContainer(scroller);
    mFolderView->setFollowsAll();
    mFolderView->addChild(mFolderView->mStatusTextBox);
    mViewModel->setFolderView(mFolderView);
    mFolderView->setOpen(true);
    mFolderView->setSelectCallback([this](const std::deque<LLFolderViewItem*>&, bool) {
        if (mHooks.chosen)
        {
            mHooks.chosen();
        }
    });
    return true;
}

void ALScriptExplorerTree::draw()
{
    // The folder view's own round: filtering, then laying out.
    if (mFolderView)
    {
        mFolderView->update();
    }
    LLPanel::draw();
}

void ALScriptExplorerTree::onFocusReceived()
{
    LLPanel::onFocusReceived();
    if (mFolderView)
    {
        LLEditMenuHandler::gEditMenuHandler = mFolderView;
    }
}

void ALScriptExplorerTree::onFocusLost()
{
    if (mFolderView && LLEditMenuHandler::gEditMenuHandler == mFolderView)
    {
        LLEditMenuHandler::gEditMenuHandler = nullptr;
    }
    LLPanel::onFocusLost();
}

bool ALScriptExplorerTree::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    // The row under the pointer is the choice, unless it is among what was
    // chosen already; below the rows, nothing is. Then the explorer's menu,
    // not the folder view's.
    if (!mFolderView)
    {
        return LLPanel::handleRightMouseDown(x, y, mask);
    }
    setFocus(true);
    LLFolderViewItem* hit = itemAt(x, y);
    if (hit && !hit->isSelected())
    {
        mFolderView->setSelection(hit, false, true);
    }
    else if (!hit)
    {
        mFolderView->clearSelection();
    }
    if (mHooks.menu)
    {
        mHooks.menu(x, y);
    }
    return true;
}

void ALScriptExplorerTree::showMenuAtChoice()
{
    S32 x = 8;
    S32 y = 8;
    if (LLFolderViewItem* item = mFolderView ? mFolderView->getCurSelectedItem() : nullptr)
    {
        mFolderView->scrollToShowSelection();
        // A folder's own row, at the top of the room its children take.
        item->localPointToOtherView(8, item->getRect().getHeight() - item->getItemHeight() / 2, &x, &y, this);
    }
    handleRightMouseDown(x, y, MASK_NONE);
}

LLFolderViewItem* ALScriptExplorerTree::itemAt(S32 x, S32 y) const
{
    S32 lx = 0, ly = 0;
    localPointToOtherView(x, y, &lx, &ly, mFolderView);
    for (LLView* view = mFolderView->childFromPoint(lx, ly, true); view && view != mFolderView; view = view->getParent())
    {
        if (LLFolderViewItem* item = dynamic_cast<LLFolderViewItem*>(view))
        {
            // A folder's own row, not the room its open children take.
            S32 ix = 0, iy = 0;
            localPointToOtherView(x, y, &ix, &iy, item);
            if (iy >= item->getRect().getHeight() - item->getItemHeight())
            {
                return item;
            }
        }
    }
    return nullptr;
}

// --- the rows --------------------------------------------------------------------------------

// static
const ALScriptExplorerTree::Node* ALScriptExplorerTree::parentOf(const Node* node)
{
    // Reached through the interface, where it is public.
    LLFolderViewModelItem* base = const_cast<Node*>(node);
    return static_cast<const Node*>(base->getParent());
}

// static
std::string ALScriptExplorerTree::keyOf(const LLSD& row)
{
    if (!row.isMap())
    {
        return std::string();
    }
    if (row.has("item"))
    {
        return "i:" + row["prim"].asString() + ":" + row["item"].asString();
    }
    if (row.has("empties"))
    {
        return "e:" + row["root"].asString();
    }
    if (row.has("prim"))
    {
        return "p:" + row["prim"].asString();
    }
    return "o:" + row["root"].asString();
}

const ALScriptExplorerTree::Entry* ALScriptExplorerTree::find(const LLSD& row) const
{
    const auto found = mNodes.find(keyOf(row));
    return found != mNodes.end() ? &found->second : nullptr;
}

ALScriptExplorerTree::Entry ALScriptExplorerTree::make(const Row& row, const std::string& key, const Entry& parent)
{
    const bool folder = row.kind != Row::Kind::Item;
    Entry      made;
    made.node = new Node(*this, *mViewModel, key, folder);

    LLFolderViewItem::Params params(LLUICtrlFactory::getDefaultParams<LLFolderViewItem>());
    params.name                 = row.name;
    params.root                 = mFolderView;
    params.listener             = made.node;
    params.font_color           = LLUIColorTable::instance().getColor("MenuItemEnabledColor", LLColor4::white);
    params.font_highlight_color = LLUIColorTable::instance().getColor("MenuItemHighlightColor", LLColor4::white);
    if (folder)
    {
        // Filled here, not fetched as the inventory's are: what a folder
        // holds is its children already, which it sorts, and which say
        // whether it is complete.
        LLFolderViewFolder* widget = LLUICtrlFactory::create<LLFolderViewFolder>(params);
        widget->setChildrenInited(true);
        made.widget = widget;
    }
    else
    {
        made.widget = LLUICtrlFactory::create<LLFolderViewItem>(params);
    }
    parent.node->addChild(made.node);
    made.widget->addToFolder(static_cast<LLFolderViewFolder*>(parent.widget));
    return made;
}

void ALScriptExplorerTree::forget(Node* node)
{
    for (auto child = node->getChildrenBegin(); child != node->getChildrenEnd(); ++child)
    {
        forget(static_cast<Node*>(child->get()));
    }
    mNodes.erase(node->key());
}

void ALScriptExplorerTree::drop(const std::string& key)
{
    const auto found = mNodes.find(key);
    if (found == mNodes.end())
    {
        return;
    }
    // Held while it goes: its entry, which held it, goes first.
    LLPointer<Node>   node   = found->second.node;
    LLFolderViewItem* widget = found->second.widget;
    forget(node);
    LLFolderViewModelItem* base = node.get();
    if (LLFolderViewModelItem* parent = const_cast<LLFolderViewModelItem*>(base->getParent()))
    {
        parent->removeChild(node);
    }
    if (widget)
    {
        widget->destroyView();
    }
}

void ALScriptExplorerTree::show(const std::vector<Row>& rows, const std::function<Look(const Row&)>& look, const std::string& filter, const std::string& empty)
{
    if (!mFolderView)
    {
        return;
    }
    mShowing = true;
    Filter& lit = static_cast<Filter&>(mViewModel->getFilter());
    lit.setWords(filter);
    lit.setEmptyLookupMessage(empty);

    // Each row's node, and what holds it: an object at the top; a prim in
    // its object, or with the prims holding nothing after that row; an item
    // in its prim, or in its object where that is one prim.
    const Entry                               root{ mRoot, mFolderView };
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> shown;
    Entry                                     object, empties, prim;
    bool                                      resort = false;
    S32                                       order  = 0;
    for (const Row& row : rows)
    {
        const std::string key = keyOf(row.value);
        const Entry*      parent = &root;
        switch (row.kind)
        {
            case Row::Kind::Object: break;
            case Row::Kind::Empties: parent = &object; break;
            case Row::Kind::Prim: parent = empties.node ? &empties : &object; break;
            case Row::Kind::Item: parent = row.many && prim.node ? &prim : &object; break;
        }
        if (!parent->node)
        {
            continue;
        }
        // Where it was held by another, it is made again under this one.
        auto found = mNodes.find(key);
        if (found != mNodes.end() && found->second.widget->getParentFolder() != parent->widget)
        {
            drop(key);
            found = mNodes.end();
        }
        const bool fresh = found == mNodes.end();
        if (fresh)
        {
            found = mNodes.emplace(key, make(row, key, *parent)).first;
        }
        const Entry entry = found->second;
        shown.insert(key);
        if (entry.node->set(row, look(row)) || fresh)
        {
            entry.widget->refresh();
        }
        if (entry.node->mOrder != order)
        {
            entry.node->mOrder = order;
            resort             = true;
        }
        ++order;
        if (entry.node->folder() && entry.node->mRowFolded != row.folded)
        {
            entry.node->mRowFolded = row.folded;
            static_cast<LLFolderViewFolder*>(entry.widget)->setOpen(!row.folded);
        }
        switch (row.kind)
        {
            case Row::Kind::Object:
                object  = entry;
                empties = Entry();
                prim    = Entry();
                break;
            case Row::Kind::Empties: empties = entry; break;
            case Row::Kind::Prim: prim = entry; break;
            case Row::Kind::Item: break;
        }
    }
    // What is no longer a row goes, with what it held.
    std::vector<std::string> gone;
    for (const auto& [key, entry] : mNodes)
    {
        if (!shown.contains(key))
        {
            gone.push_back(key);
        }
    }
    for (const std::string& key : gone)
    {
        drop(key);
    }
    if (resort)
    {
        mViewModel->requestSortAll();
    }
    mFolderView->arrangeAll();
    mShowing = false;
}

void ALScriptExplorerTree::opened(Node& node)
{
    if (mShowing || !node.value().isDefined())
    {
        return;
    }
    if (node.folder())
    {
        if (mHooks.folded)
        {
            mHooks.folded(node.value(), false);
        }
    }
    else if (mHooks.opened)
    {
        mHooks.opened(node.value());
    }
}

void ALScriptExplorerTree::closed(Node& node)
{
    if (mShowing || !node.value().isDefined() || !node.folder())
    {
        return;
    }
    if (mHooks.folded)
    {
        mHooks.folded(node.value(), true);
    }
}

// --- what is chosen ----------------------------------------------------------------------------

std::vector<LLSD> ALScriptExplorerTree::chosen() const
{
    std::vector<const Node*> nodes;
    if (mFolderView)
    {
        for (const LLFolderViewItem* item : mFolderView->getSelectionList())
        {
            if (const Node* node = static_cast<const Node*>(item->getViewModelItem()); node && node->value().isDefined())
            {
                nodes.push_back(node);
            }
        }
    }
    // In the tree's order: by what holds each, then among its own.
    const auto path = [this](const Node* node) {
        std::vector<S32> orders;
        for (const Node* up = node; up && up != mRoot.get(); up = parentOf(up))
        {
            orders.insert(orders.begin(), up->mOrder);
        }
        return orders;
    };
    std::sort(nodes.begin(), nodes.end(), [&path](const Node* a, const Node* b) { return path(a) < path(b); });
    std::vector<LLSD> rows;
    for (const Node* node : nodes)
    {
        rows.push_back(node->value());
    }
    return rows;
}

bool ALScriptExplorerTree::choose(const LLSD& row, bool focus)
{
    const Entry* entry = find(row);
    if (!entry || !mFolderView)
    {
        return false;
    }
    // What holds it opened, as a person opening it would: told, so that
    // what it now shows is asked for.
    if (LLFolderViewFolder* holder = entry->widget->getParentFolder(); holder && holder != mFolderView)
    {
        holder->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_UP);
    }
    mFolderView->setSelection(entry->widget, false, focus);
    mFolderView->arrangeAll();
    mFolderView->scrollToShowSelection();
    return true;
}

void ALScriptExplorerTree::chooseNone()
{
    if (mFolderView)
    {
        mFolderView->clearSelection();
    }
}

void ALScriptExplorerTree::toggle(const LLSD& row)
{
    const Entry* entry = find(row);
    if (entry && entry->node->folder())
    {
        static_cast<LLFolderViewFolder*>(entry->widget)->toggleOpen();
    }
}

void ALScriptExplorerTree::rename()
{
    if (mFolderView)
    {
        mFolderView->startRenamingSelectedItem();
    }
}

void ALScriptExplorerTree::copy() const
{
    std::string text;
    for (const LLSD& row : chosen())
    {
        if (const Entry* entry = find(row))
        {
            text += (text.empty() ? "" : "\n") + entry->node->getName();
        }
    }
    if (!text.empty())
    {
        LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
    }
}

// --- for a test --------------------------------------------------------------------------------

bool ALScriptExplorerTree::has(const LLSD& row) const
{
    return find(row) != nullptr;
}

std::string ALScriptExplorerTree::label(const LLSD& row) const
{
    const Entry* entry = find(row);
    return entry ? entry->node->getDisplayName() : std::string();
}

std::string ALScriptExplorerTree::suffix(const LLSD& row) const
{
    const Entry* entry = find(row);
    return entry ? entry->node->suffix() : std::string();
}

bool ALScriptExplorerTree::isOpen(const LLSD& row) const
{
    const Entry* entry = find(row);
    return entry && entry->widget->isOpen();
}

LLSD ALScriptExplorerTree::parentOf(const LLSD& row) const
{
    const Entry* entry = find(row);
    if (!entry)
    {
        return LLSD();
    }
    const Node* parent = parentOf(entry->node.get());
    return parent ? parent->value() : LLSD();
}
