/**
 * @file alscriptexplorertree.h
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

#pragma once

#include "alscriptexplorermodel.h"
#include "llpanel.h"
#include "llpointer.h"
#include "llstl.h"
#include "llui.h"

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class LLFolderView;
class LLFolderViewItem;

// The explorer's rows as a tree, in the inventory's folder view: objects,
// their prims and what each holds, each opened and folded by its arrow,
// renamed where it stands, dragged out and dropped on as the inventory's
// are, with its state after its name. The rows come from the explorer's
// model; a row is kept as the same node by what it stands for, so the tree
// can be told its rows again as answers come in without losing what is
// chosen, what is open, or a name being typed. What a person does with it
// goes to its owner.
//
// The panel holds the keyboard while the tree has it, as the folder view
// wants of its panel, and passes on every key the tree does not take --
// not taking escape, as a panel does, to leave nothing with the keyboard.
class ALScriptExplorerTree : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptExplorerTree, LLPanel);
    typedef ALScriptExplorerModel::Row Row;

    // What the tree asks of its owner. A row is its value.
    struct Hooks
    {
        // A script or notecard opened: double-clicked.
        std::function<void(const LLSD& row)> opened;
        // An object, a prim or the prims holding nothing folded or opened.
        std::function<void(const LLSD& row, bool folded)> folded;
        // Whether a row may be renamed; its new name.
        std::function<bool(const LLSD& row)>                          renameable;
        std::function<void(const LLSD& row, const std::string& name)> renamed;
        // What is carried over a row, or dropped on it; over no row, an
        // undefined one. True where it would go, with why not otherwise.
        std::function<bool(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, std::string& tooltip)> drop;
        // What is chosen dragged out; true where the drag began.
        std::function<bool()> drag;
        // What is chosen changed.
        std::function<void()> chosen;
        // The menu, at a point of the panel, what is under it chosen first.
        std::function<void(S32 x, S32 y)> menu;
    };
    // How a row is shown: its words, what follows them, its icon's name.
    struct Look
    {
        std::string label;
        std::string suffix;
        std::string icon;
        // Drawn over the icon: a mark beside what the row is, such as a pin.
        std::string overlay;
    };

    // Built by the skin (class="script_studio_explorer_tree").
    explicit ALScriptExplorerTree(const LLPanel::Params& params = getDefaultParams());
    ~ALScriptExplorerTree() override;
    bool postBuild() override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override { return false; }
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    // The menu a right click on the row chosen would open, or on the space
    // below the rows where none is: Shift-F10 and the Menu key's.
    void showMenuAtChoice();
    // The Edit menu's commands are the tree's while it has the keyboard:
    // else they are whatever last took them, which may delete a prim.
    void onFocusReceived() override;
    void onFocusLost() override;

    void setHooks(Hooks hooks) { mHooks = std::move(hooks); }

    // The rows shown, in their order, each as `look` says; a row's node
    // opened or folded as the row says where it is new or that changed;
    // what the filter looks for lit in each; and `empty` said where there
    // is nothing to show.
    void show(const std::vector<Row>& rows, const std::function<Look(const Row&)>& look, const std::string& filter, const std::string& empty);

    // The rows chosen, in the tree's order.
    std::vector<LLSD> chosen() const;
    // A row chosen alone, what holds it opened, and in view; the keyboard
    // with it where asked. False where there is no such row.
    bool choose(const LLSD& row, bool focus);
    void chooseNone();
    // A folder opened where it is folded, folded where it is open.
    void toggle(const LLSD& row);
    // The row chosen renamed where it stands.
    void rename();
    // The names of what is chosen, one a line, on the clipboard.
    void copy() const;

    // For a test: whether a row is shown, what it is shown as, whether it
    // is open, and what holds it; and the folder view.
    bool          has(const LLSD& row) const;
    std::string   label(const LLSD& row) const;
    std::string   suffix(const LLSD& row) const;
    bool          isOpen(const LLSD& row) const;
    LLSD          parentOf(const LLSD& row) const;
    LLFolderView* folderView() const { return mFolderView; }

    class Node;
    class Filter;
    class Sort;
    class ViewModel;

private:
    friend class Node;
    friend class ViewModel;
    struct Entry
    {
        LLPointer<Node>   node;
        LLFolderViewItem* widget = nullptr;
    };
    // A row's identity: what kind of row, and the ids it is known by.
    struct Key
    {
        char   kind = 0;
        LLUUID first;
        LLUUID second;
        bool   operator==(const Key&) const = default;
        friend size_t hash_value(const Key& key) noexcept
        {
            size_t seed = static_cast<size_t>(key.kind);
            boost::hash_combine(seed, hash_value(key.first));
            boost::hash_combine(seed, hash_value(key.second));
            return seed;
        }
    };
    static Key         keyOf(const LLSD& row);
    static const Node* parentOf(const Node* node);
    const Entry*       find(const LLSD& row) const;
    Entry              make(const Row& row, const Key& key, const Entry& parent);
    void               drop(const Key& key);
    void               forget(Node* node);
    // The node under a point of the panel; null for none.
    LLFolderViewItem* itemAt(S32 x, S32 y) const;

    // What the nodes tell the tree.
    void opened(Node& node);
    void closed(Node& node);

    Hooks                                                 mHooks;
    std::unique_ptr<ViewModel>                            mViewModel;
    LLPointer<Node>                                       mRoot;
    LLFolderView*                                         mFolderView = nullptr;
    boost::unordered_flat_map<Key, Entry>                  mNodes;
    // While the rows are being put in, the nodes opened and folded say
    // nothing of it: it is the rows' doing, not a person's.
    bool                                                  mShowing = false;
    // The filter and the words over no rows as last laid out: the rows
    // the same and these too, nothing is laid out again.
    std::string                                           mShownFilter;
    std::string                                           mShownEmpty;
    // How many times the rows were put in, and what was chosen as last
    // asked, by the widgets chosen then and in the tree's order: the
    // buttons, the menu and a click each ask, and the rows are the same.
    U32                                                   mShows = 0;
    mutable U32                                           mChosenOf = 0;
    mutable std::vector<const LLFolderViewItem*>          mChosenItems;
    mutable std::vector<LLSD>                             mChosen;
};
