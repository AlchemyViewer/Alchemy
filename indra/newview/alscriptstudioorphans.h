/**
 * @file alscriptstudioorphans.h
 * @brief Script Studio's orphans: tabs whose script is gone, out of reach or not loaded, and the notice over the editor.
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

#include "alscriptnoticebar.h"
#include "alscriptstudiodoc.h"
#include "alscripttypes.h"

#include <optional>
#include <string>

class ALScriptStudioSaves;
class ALScriptStudioRecovery;
class ALScriptStudioServices;
class ALScriptStudioTabs;
struct ALRecoveryEntry;

// A tab's part of where it stands with what holds it: what it is
// (Doc::Orphan), and the rest of what ALScriptStudioOrphans keeps of it.
struct ALScriptStudioDoc::Orphaned
{
    Orphan kind            = Orphan::None;
    bool   noticeDismissed = false;
    // A kept text with nothing loaded under it -- its item out of
    // reach as it was opened -- whose script is known only as the
    // entry said: loaded under it once the item is in reach, before
    // it is saved. How many loads have failed on the way since the
    // last that went through, and when the next may be tried: further
    // apart each time, and a few times only unless a person asks.
    bool   detached      = false;
    S32    reattachTries = 0;
    F64    nextReattach  = 0.0;
    // Since when its object has been out of sight, or zero: an object
    // at the edge of what is in view comes and goes, and is taken for
    // gone only once it has been gone a moment.
    F64    awaySince = 0.0;
};

// A Script Studio window's tabs as what holds them has them, the tab's part
// `doc.orphan` keeping it: its object out of sight, its item gone from the
// object or the inventory, the connection lost, the item in the Trash, the
// file gone from disk; or a kept text over a script that may no longer be
// changed, or that could not be loaded. Looked at again when what is in
// reach may have changed -- the window says when: the inventory, an object
// come or gone, a prim's contents, a file -- and when this asks to be; a
// tab loaded under what it holds once its item is back in reach, tried a
// few times further apart each time. And the notice over the editor, which
// says what the tab in front has to reckon with, and what its buttons do.
// What is in reach is the window's to say, since it is the world's.
class ALScriptStudioOrphans
{
public:
    typedef ALScriptStudioDoc Doc;
    typedef Doc::Orphan       Orphan;

    // What is in reach of a tab, as the world has it now.
    struct Reach
    {
        bool offline   = false;
        // A file's tab: its file on disk.
        bool fileThere = false;
        // An inventory item's tab: the item, and whether it is in the Trash.
        bool itemThere = false;
        bool trashed   = false;
        // An object's: the object in sight; and whether its prim holds the
        // item, where the region has said what the prim holds -- nothing
        // while it is asked, or where the prim is not known.
        bool                objectThere = false;
        std::optional<bool> heldByPrim;
    };
    // What a tab is, as what is in reach says: a kept text over a script
    // that may no longer be changed, or that could not be loaded, stays
    // that while there is an item, and an item gone stays gone while its
    // prim is asked about again.
    static Orphan seen(const Doc& doc, const Reach& reach);
    // What a load that failed makes a tab: not permitted, locked; not to
    // be read or fetched, unloaded; else gone, from the inventory or its
    // object, or its object out of sight.
    static Orphan failedAs(const Doc& doc, ALScriptLoaded::Failure failure, bool object_there);
    // What the notice says for a tab: a text kept from an earlier session
    // first, then what it is, unless it was hidden since that changed,
    // then what the last word about it offered to do; nothing for none.
    static ALScriptNoticeBar::Notice noticeFor(const Doc* doc, const ALScriptStudioServices& services);
    // A tab come to stand as `kind` with what holds it, said in the notice,
    // which is back in sight where it was hidden.
    static void become(Doc& doc, Orphan kind)
    {
        doc.orphan->kind            = kind;
        doc.orphan->noticeDismissed = false;
    }
    // A kept text taken up with nothing loaded under it, standing as `kind`:
    // detached where it is an item's, to be loaded under it once the item is
    // in reach.
    static void detach(Doc& doc, Orphan kind)
    {
        // A file on disk has nothing to be loaded under it.
        doc.orphan->detached = doc.file.empty();
        become(doc, kind);
    }
    // The notice back in sight where it was hidden, saying what the tab is.
    static void showNotice(Doc& doc) { doc.orphan->noticeDismissed = false; }
    // A load of a tab failed, to be tried again later, further apart each
    // time; and one gone through, after which a failure is tried again soon.
    static void loadFailed(Doc& doc);
    static void loadWentThrough(Doc& doc) { doc.orphan->reattachTries = 0; }

    // What the orphans ask of the window itself, beyond what they are given.
    class Window
    {
    public:
        // What is in reach of a tab; and what it is called and where it
        // is, while it is in sight, taken again.
        virtual Reach reach(const Doc& doc)    = 0;
        virtual void  refreshPlace(Doc& doc)   = 0;
        // A tab's script loaded again, the answer going where every load's
        // goes.
        virtual void loadScript(const ALScriptRef& ref) = 0;
        // The notice.
        virtual ALScriptNoticeBar* noticeBar() = 0;
        // The notice's actions: a copy in the inventory or on disk; a text
        // kept from an earlier session thrown away.
        virtual void saveCopyToInventory(Doc& doc)                                           = 0;
        // What the last word about a tab offered, done as its link in
        // Output does it (Doc::offer).
        virtual void takeOffer(Doc& doc, const std::string& action)                          = 0;
        virtual void saveCopyToFile()                                                        = 0;
        virtual void discardRecovery(const ALRecoveryEntry& entry)                     = 0;
        // Two texts compared in the tab's place, each with what it is; and
        // the tab's source back in front where it shows a comparison.
        virtual void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                             const std::string& right_title)                                  = 0;
        virtual void endCompare(Doc& doc)                                                    = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioOrphans(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, ALScriptStudioSaves& saves, ALScriptStudioRecovery& recovery, Window& window);

    // Every tab looked at again: what it is called and where, what it is
    // now -- an object out of sight a moment not yet gone -- said where it
    // changed, what was typed kept where it is lost; and each detached tab
    // whose item is in reach loaded under what it holds, where its next
    // try is due. When to look again with nothing else changed: when an
    // object out of sight a moment is gone, or a try is due; 0 for never.
    F64 check();
    // What a tab holds carried over what its item has, with its history, as
    // a kept text is taken up: the item loaded under it, so that what it is
    // saved as is what the item is.
    void reattach(Doc& doc);
    // The same, as a person asks for it: tried now, and a few more times
    // after if it fails.
    void retryLoad(Doc& doc);
    // The notice said again for the tab in front; and one of its buttons.
    void refreshNotice();
    void noticeAction(const std::string& action);

private:
    ALScriptStudioServices& mServices;
    ALScriptStudioTabs&     mTabs;
    ALScriptStudioSaves&    mSaves;
    ALScriptStudioRecovery& mRecovery;
    Window&                 mWindow;
};
