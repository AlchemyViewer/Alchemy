/**
 * @file alnotecardembedded.h
 * @brief A notecard's items in its Script Studio tab: buttons in the text, dropped in, saved, opened and copied out.
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

#include "altextview.h"
#include "llinventory.h"
#include "llpointer.h"
#include "llsd.h"
#include "llui.h"
#include "lluuid.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ALScriptRef;
struct ALScriptStudioDoc;
class ALScriptStudioServices;

// What a notecard's tab carries: the items the notecard holds, each a
// button in the text where its character stands (ALNotecardItems) that
// opens the item or offers a copy of it; taken in from the inventory by a
// drop; sent back with the text on a save, only those the text still
// stands, numbered afresh. A script's tab has none, and neither has a text
// file's. Held by the tab (ALScriptStudioDoc::items), for as long as it is
// a notecard's.
class ALNotecardEmbedded : public std::enable_shared_from_this<ALNotecardEmbedded>
{
public:
    typedef std::vector<LLPointer<LLInventoryItem>> items_t;

    // What the tab asks of the viewer beyond the window: the side of an
    // item that is the world's. The viewer's own is `viewer()`; a test
    // fakes it.
    class World
    {
    public:
        // The image an item's button shows, by the item's kind.
        virtual std::string iconOf(const LLInventoryItem& item) const = 0;
        // Whether what is being dragged comes out of a notecard, which the
        // drag tool will not put into another.
        virtual bool draggedFromNotecard() const = 0;
        // Whether a notecard may carry environment settings.
        virtual bool carriesSettings() const = 0;
        // Whether the agent may copy an item: carrying one is copying it.
        virtual bool mayCopy(const LLInventoryItem& item) const = 0;
        // The frame being drawn. Several items dropped together come one
        // call each, in the same frame.
        virtual U32 frame() const = 0;
        // An item opened as its kind opens: a texture or a material in its
        // preview, with the notecard named so that a save from there can
        // reach it; a landmark's place, `copy` taking a copy of it into its
        // folder first where the inventory has none for the place; a calling
        // card's profile. True where that is all. A sound played, and every
        // other kind, false: a copy is offered.
        virtual bool open(const LLPointer<LLInventoryItem>& item, const ALScriptRef& notecard,
                          std::function<void(const LLUUID& folder, U32 callback_id)> copy) = 0;
        // Whether to take a copy, asked; `yes` where the answer is yes.
        virtual void confirmCopy(std::function<void()> yes) = 0;
        // A copy of an item into a folder, or the one the server picks,
        // asked of the region the notecard is in, or the agent's. False
        // where it could not be asked; `refused` with the region's words
        // where it says no.
        virtual bool askCopy(const ALScriptRef& notecard, const LLUUID& item, const LLUUID& folder, U32 callback_id,
                             std::function<void(const std::string& error)> refused) = 0;

    protected:
        ~World() = default;
    };
    // The viewer's (alnotecardworld.cpp).
    static World& viewer();

    ALNotecardEmbedded(ALScriptStudioDoc& doc, ALScriptStudioServices& services, World& world);

    // The items the notecard was loaded with, which its asset carries and
    // the server can copy out of it.
    void loaded(items_t items);
    // Items put in place of those: kept, or carried from another window,
    // which the text is read against by their places in the list. Only
    // what the asset was loaded carrying can be copied out still.
    void take(items_t items);
    const items_t& items() const { return mItems; }
    // Saved: the asset carries what was sent, and nothing else.
    void saved(const std::vector<LLUUID>& sent);

    // From the moment the text is the notecard's: what is dragged onto
    // the text heard, and a placeholder put in by an edit -- a drop, an
    // undo, a redo, a paste -- given its button as the edit lands.
    void wire();
    // A button for each item the text stands; and for those on a stretch
    // of lines that have none yet.
    void place();
    void place(S32 first_line, S32 last_line);

    // The text and the items as a save sends them: each item the text
    // still stands, numbered afresh in the order it first stands them;
    // any other left behind. The editor's own text and list are left as
    // they are, so that a placeholder undone back into the text still
    // names its item.
    void forSave(std::string& text, items_t& items) const;

    // An inventory item dragged onto the text, taken where it is dropped
    // as the legacy notecard takes one: of a kind a notecard may carry,
    // that the next owner may have whole and the agent may copy; never
    // one out of another notecard. False to let the drag go elsewhere.
    bool drop(S32 x, S32 y, bool dropping, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip);
    // An item's button pressed: opened as its kind opens, or a copy of it
    // offered.
    void open(LLPointer<LLInventoryItem> item);
    // A copy taken into the inventory by the server: false, said why, for
    // an item the saved asset does not carry, which it could not find.
    bool copy(LLPointer<LLInventoryItem> item, const LLUUID& folder, U32 callback_id = 0);

    // Items as a kept text keeps them, and back: each in its place, a
    // missing one as nothing, since the text says an item by its place.
    static LLSD    asLLSD(const items_t& items);
    static items_t fromLLSD(const LLSD& items);
    // What a tab's items are carried as into another tab -- in another
    // window, or loaded again under a kept text: its items, where it is a
    // notecard's.
    static void carry(const ALScriptStudioDoc& from, ALScriptStudioDoc& to);

private:
    ALTextView::Atom atomFor(const ALTextPos& at, size_t index);

    ALScriptStudioDoc&                 mDoc;
    ALScriptStudioServices&            mServices;
    World&                             mWorld;
    items_t                            mItems;
    // The items the saved asset carries, by id: what the server can copy
    // out of it. An item dropped since is only here once a save has taken
    // it.
    boost::unordered_flat_set<LLUUID>  mInAsset;
    boost::signals2::scoped_connection mEdits;
    // Where the last drop's placeholder ended, and in which frame: several
    // items dropped at once come one call each, at one point, and each
    // goes after the one before.
    ALTextPos                          mDropEnd{ -1, -1 };
    U32                                mDropFrame = 0;
};
