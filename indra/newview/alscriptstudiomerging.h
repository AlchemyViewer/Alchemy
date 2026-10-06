/**
 * @file alscriptstudiomerging.h
 * @brief A Script Studio window's merges: a save that came up against another, settled conflict by conflict.
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
#include "altextmerge.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

class ALScriptStudioServices;

// A Script Studio window's merges: a save that came up against another --
// saved elsewhere while the tab had changes, or the world's item moved on
// since the tab had it -- settled conflict by conflict, rather than one
// side taken whole.
//
// Where what both were is known -- the text the tab last loaded or saved,
// which its undo still reaches -- every change only the other side made is
// put into the tab as one step to undo, and the tab is compared with the
// other side as a merge (ALDiffView::setMergeBase): what conflicts is
// marked, and settled from the comparison's bar, each an edit of the tab;
// then the tab is saved as any other. Where it is not known, or the tab
// may not be changed, the two are compared as they are, and a change is
// taken back from the other side as in any comparison.
class ALScriptStudioMerging
{
public:
    typedef ALScriptStudioDoc Doc;

    // What a merge asks of the window.
    class Window
    {
    public:
        // Another text beside the tab's own, which the comparison follows.
        virtual void compareWithTab(Doc& doc, const std::string& theirs, const std::string& their_title, const std::string& own_title,
                                    const ALTextDiff::ranges_t& ranges) = 0;
        // The text the world's item holds now, and its asset, once loaded;
        // nothing where it could not be, which is said, or the tab has
        // gone.
        virtual void loadWorld(Doc& doc, std::function<void(Doc& doc, const std::string& text, const LLUUID& asset)> loaded) = 0;
        // The notice over the editor said again, for the tab in front.
        virtual void refreshNotice() = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioMerging(ALScriptStudioServices& services, Window& window);

    // Whether the tab can be merged into: loaded, and changeable.
    static bool canMerge(const Doc& doc);
    // What was saved elsewhere while the tab had changes, merged in.
    void mergeSaved(Doc& doc);
    // What the world's item holds now, once loaded, merged in: the tab
    // made from it from here on, so that a save no longer stops for it.
    void mergeWorld(Doc& doc);
    // The tab's text changed: a merge stepped back before it was saved
    // puts back what it set aside -- what was saved elsewhere, the asset
    // the tab was made from -- so that a save stops for the other version
    // again, and says so. Once put back, a step forward again does not
    // take it away; merging again does.
    void textChanged(Doc& doc);
    // A merge's conflict settled from the keyboard, as the comparison's bar
    // settles one: the one the caret is in, where the tab's comparison is
    // a merge in front. Whether it was.
    static bool canSettle(const Doc& doc);
    static bool settle(Doc& doc, ALTextMerge::Take take);

private:
    // Merged, where it can be, and said; else compared as they are. Whether
    // it was merged.
    bool merge(Doc& doc, const std::string& theirs, const std::string& title);
    // The merge just put in the tab kept, with what it set aside.
    static void remember(Doc& doc, Doc::Merged merged);
    // The tab's text made another as one step to undo: the lines between
    // those the two share at the start and at the end.
    static bool becomes(Doc& doc, const std::string& text);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Whether this is still here, for what a load calls back.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};
