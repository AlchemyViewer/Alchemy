/**
 * @file alscriptoutlinepane.h
 * @brief Script Studio's outline: what a script declares, as a tree, filtered, sorted and folded, following the caret.
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

#include "alscriptstudiodoc.h"
#include "llpanel.h"

#include <optional>
#include <string>
#include <vector>

class ALPaneList;
class ALScriptStudioServices;
class LLComboBox;
class LLFilterEditor;

// The outline of a Script Studio window: what the tab in front declares, as
// the last check said, each symbol under what holds it -- or, through the
// filter, every one with its letters, flat -- in the order asked for under
// each holder: as written, by name, or by kind. A symbol's arrow folds what
// it holds, the fold kept by the names down to it, so that it outlives a
// check that numbers them anew. The symbol the caret is in chosen, as the
// bar at the bottom found it; one chosen here gone to, which is the
// window's.
class ALScriptOutlinePane : public LLPanel
{
public:
    AL_VIEW_TYPE(ALScriptOutlinePane, LLPanel);
    typedef ALScriptStudioDoc Doc;

    // What the outline asks of the window beyond its services.
    class Window
    {
    public:
        // A tab's outline shown: the bar at the bottom told, whose path
        // the outline then follows.
        virtual void outlineShown(Doc& doc) = 0;
        // A symbol chosen: gone to in the tab's script.
        virtual void outlineChosen(Doc& doc, const ALScriptOutlineEntry& entry, bool to_editor) = 0;
        // How it is sorted changed, which the window's state keeps.
        virtual void outlineSortChanged() = 0;

    protected:
        ~Window() = default;
    };

    explicit ALScriptOutlinePane(const LLPanel::Params& params = getDefaultParams());
    bool postBuild() override;

    // The tab's outline listed, where it is the tab in front: made again
    // only where what the rows say changed, and then with its scroll kept.
    // Out of sight, only the window told; listed once it is seen (pump).
    void show(Doc& doc);
    // The innermost symbol the caret is in chosen, of the tab in front;
    // the nearest one shown that holds it where it is folded away. Out of
    // sight, once it is seen.
    void followCaret(Doc& doc);
    void pump();
    // A symbol's fold turned, or opened or shut; left on one with nothing
    // to fold, to what holds it, which is gone to as a row walked to is.
    void fold(size_t index, std::optional<bool> folded = std::nullopt);
    // The row chosen gone to.
    void choose(bool to_editor);
    // No tab: nothing listed.
    void forget();
    ALPaneList* list() const { return mList; }
    // How it is sorted -- order, name or kind -- as the window's state
    // keeps it.
    std::string sortOrder() const;
    void        setSortOrder(const std::string& order);

private:
    // Whether a point across a row is on its arrow.
    bool        arrowAt(const LLScrollListItem* item, S32 x) const;
    std::string kindName(ALScriptSymbolKind kind) const;

    ALScriptStudioServices*  mServices = nullptr;
    Window*                  mWindow   = nullptr;
    ALPaneList*              mList     = nullptr;
    LLFilterEditor*          mFilter   = nullptr;
    LLComboBox*              mSort     = nullptr;
    // What the rows last said, and whose they were: a check that changes
    // none of it leaves the list as it is.
    std::vector<std::string> mSaid;
    // Each entry's key -- the names down to it -- and whether it holds
    // others, as the rows were last made.
    std::vector<std::string> mKeys;
    std::vector<bool>        mParents;
    // Shown or followed out of sight: the tab in front's listed once it is
    // seen.
    bool                     mUnseen = false;
};
