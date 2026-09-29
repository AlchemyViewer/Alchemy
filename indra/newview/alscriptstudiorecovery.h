/**
 * @file alscriptstudiorecovery.h
 * @brief Script Studio's unsaved texts kept against a crash, and offered back.
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

#include "alquickopen.h"
#include "alrecoverystore.h"
#include "alscriptstudiodoc.h"
#include "alscriptworkspace.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's unsaved texts kept against the viewer going
// before they were saved -- a crash, a lost connection, a quit -- in the
// account's recovery store, and offered back: at login, what a session
// that ended left; from File > Recover Unsaved Changes, that and what was
// thrown away lately. A kept text is taken up over its script's tab as one
// step to undo, with its history and its caret, or held in a tab of its own
// where its script is gone.
//
// The store is the process's, one for the account logged in, which every
// studio window shares; the rest is a window's, over its tabs. What a kept
// text's tab turns into where its script is gone -- an orphan -- and the
// notice that says so are the window's.
class ALScriptStudioRecovery
{
public:
    typedef ALScriptStudioDoc     Doc;
    typedef ALRecoveryEntry Entry;

    // What recovery asks of the window beyond its services.
    class Window
    {
    public:
        // A kept text whose script or file is open in another of the
        // studio's windows, taken up there -- two tabs of one script would
        // each save over the other; false where no other window has it.
        virtual bool recoverElsewhere(const Entry& entry) = 0;
        // A file on disk opened in a tab where it is; null where it could
        // not be.
        virtual Doc* openFileTab(const std::string& path, bool lua) = 0;
        // Whether a script can be had to put a kept text over: its item in
        // the inventory, or its object in sight.
        virtual bool scriptInHand(const ALScriptRef& ref) const = 0;
        // A tab brought to the front.
        virtual void activate(Doc& doc) = 0;
        // A kept text in a tab of its own, its script gone; a tab made one;
        // and what a load failing makes one.
        virtual void        openOrphan(const Entry& entry, Doc::Orphan orphan)                  = 0;
        virtual void        becomeOrphan(Doc& doc, const Entry& entry, Doc::Orphan orphan)      = 0;
        virtual Doc::Orphan failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const = 0;
        // Text carried in put in place of the server's, as one step to undo.
        virtual void takeCarriedText(Doc& doc) = 0;
        // The notice over the editor said again; and the tabs and the
        // toolbar, a tab's text having changed under them.
        virtual void refreshNotice() = 0;
        virtual void tabsChanged()   = 0;
        // A list to pick from, over the editors: what is chosen, and what
        // Shift-Return is pressed on.
        virtual void pick(std::vector<ALQuickOpen::Candidate> candidates, const std::string& placeholder, const std::string& title,
                          std::function<void(const std::string& value)> chosen, std::function<void(const std::string& value)> dropped) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioRecovery(ALScriptStudioServices& services, Window& window);

    // A tab as an entry, from the tab alone -- nothing of the world's asked
    // -- since it is written as the viewer goes as well, after the world
    // may have.
    static Entry entryOf(const Doc& doc);

    // --- a tab's text kept -----------------------------------------------------------

    // Written now: an unsaved text as it stands, or, saved or never
    // changed, this session's entry for it forgotten; and the entry the tab
    // took up let go of, its own from here. A write that fails is said once,
    // not at every pause in typing. False where it could not be kept.
    bool keep(Doc& doc, Entry::State state = Entry::State::Unsaved);
    // keep for each of several tabs at once, the unsaved texts written on
    // the writer's thread, each forced out to the disk there, and waited
    // on once for the lot: what the viewer going asks. False where any
    // could not be kept, which is said as keep says it.
    bool keepAll(const std::vector<Doc*>& docs, Entry::State state = Entry::State::Unsaved);
    // The same, as typing asks: an unsaved text written on the writer's
    // thread; anything else now, as keep does.
    void keepSoon(Doc& doc);
    // What a tab throws away set aside a while all the same, and its entry
    // forgotten once that is written.
    bool setAside(Doc& doc);
    // A moment after the first change since it was last written: typing on
    // writes it that often, not only once the typing stops.
    void schedule(Doc& doc);
    // Each frame: the tabs whose moment has come, written.
    void pump();

    // --- a kept text taken up --------------------------------------------------------

    // What is kept of a tab's script, for its notice to offer as the tab
    // opens: where no tab holds the script elsewhere, this session's own --
    // what a window that went wrote of the tab as it went -- set aside;
    // else, or failing that, what an earlier session left.
    void offerFor(Doc& doc, bool held_elsewhere);
    // Put in over its tab, as one step to undo, once the tab has loaded; a
    // notecard's items with it. Over a tab nothing can be saved from, the
    // tab holds it on its own.
    void takeUp(Doc& doc, const Entry& entry);
    // Over a tab holding nothing of its own: the text with the history that
    // led to it, and the caret. False where the history is not this text's.
    bool restoreHistory(Doc& doc, const Entry& entry);
    // Opened where it belongs -- its tab, here or in another window; its
    // file; a tab of its own where its script is gone -- and taken up.
    void recover(const Entry& entry);
    // File > Recover Unsaved Changes: what earlier sessions left and what
    // was thrown away lately, to pick one to take up, or Shift-Return to
    // throw it away -- or, thrown away already, to let it go for good.
    void show();

private:
    // An entry a listing read only the start of, read whole; false, and
    // said, where it cannot be.
    bool wholeOf(Entry& entry);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Held while this is, for what answers later to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};
