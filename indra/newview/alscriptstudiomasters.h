/**
 * @file alscriptstudiomasters.h
 * @brief A Script Studio window's side of scripts whose master is a file on disk.
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

#include "alscriptdiskmasters.h"
#include "alscriptstudiodoc.h"

#include <boost/signals2.hpp>

#include <functional>
#include <string>
#include <vector>

class ALScriptStudioAnalysis;
class ALScriptStudioServices;

// A tab's part of its master on disk: a file its script names as its
// master (an @file hint) under the blessed folders, offered and never taken
// alone; and, a file's tab, the script the last word about its links was
// of, which the offers then act on.
struct ALScriptStudioDoc::Mastered
{
    std::string hinted;
    ALScriptRef offerFor;
};

// A Script Studio window's side of the scripts whose master is a file on
// disk (ALScriptDiskMasters): linking a script's tab to a file, picked or
// as the script names it -- the tab giving way to the file's, which is then
// where it is changed -- and letting go of it; a linked script opened as
// its file; the file's save sending what it masters; and what came of each
// send said in Output, with what can be done about it on the file's tab,
// and what the compiler said of it among that tab's problems. A link is
// made only by a person's click: the hint is offered.
class ALScriptStudioMasters
{
public:
    typedef ALScriptStudioDoc Doc;

    // What becomes of what was typed in a tab about to be linked to a file:
    // written into the file, over what it holds; let go of, the file's text
    // taken; or no link made.
    enum class Unsaved : U8
    {
        Write,
        Discard,
        Cancel
    };

    // What the masters ask of the window itself, beyond what they are given.
    class Window
    {
    public:
        // A file chosen; a file's tab opened, or brought forward; a tab let go
        // of, its text having gone to its file or being the world's.
        virtual void pickMasterFile(std::function<void(const std::string& path)> chosen) = 0;
        virtual void openMasterFile(const std::string& path, bool lua)                   = 0;
        virtual void closeTab(Doc& doc)                                                  = 0;
        // A tab with unsaved changes about to be linked to a file, asked what
        // becomes of them, the file named.
        virtual void askLinkUnsaved(const Doc& doc, const std::string& path, std::function<void(Unsaved answer)> answered) = 0;
        // Two texts compared in a tab's place, each with what it is.
        virtual void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                             const std::string& right_title)                             = 0;
        // Whether the VS Code bridge holds a script, which a link would give
        // two masters.
        virtual bool heldByBridge(const ALScriptRef& ref) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioMasters(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window);

    // Whether a tab may be linked to a file: an item's script, loaded, that
    // may be changed and is linked to none; and whether a tab is a file's
    // that masters scripts.
    static bool canLink(const Doc* doc);
    bool        mastersAny(const Doc* doc) const;
    // The commands: a tab linked to a file picked for it, what was typed in
    // it written to the file or let go of as the author says; a file's
    // scripts let go of; and sent from it now, as the studio's own send,
    // which a change in the world holds.
    void linkToFile(Doc& doc);
    void unlink(Doc& doc);
    void sendFromFile(Doc& doc);

    // A script asked for that is linked: its file opened in its place, said
    // so; false where it is not, or its file is gone.
    bool openMaster(const ALScriptRef& ref, const std::string& name);
    // An item's tab loaded: the file its script names, where one is found
    // under the blessed folders, offered.
    void loaded(Doc& doc);
    // A file's tab saved to disk: what it masters sent.
    void fileSaved(Doc& doc);
    // One of the offers about a tab, as its link in Output or its notice
    // does it.
    void offer(Doc& doc, const std::string& action);
    // What came of sends while no window was there to hear it, said now
    // the window's panes are built.
    void sayUnheard();

private:
    // Whether a file is a script of the tab's language, said where it is not.
    bool ofItsLanguage(const Doc& doc, const std::string& path);
    // `written`: what was typed in the tab has just been written to the
    // file, so the file is not what the world holds.
    void linkTo(Doc& doc, const std::string& path, ALMasterLink::Made made, bool written = false);
    void heard(const ALScriptDiskMasters::Outcome& outcome);
    // The tab of a master file, where one is open.
    Doc* masterTab(const std::string& master) const;
    // The world's text of a script beside its master's, in the master's tab.
    void compareWithWorld(Doc& doc, const ALScriptRef& ref);

    ALScriptStudioServices&            mServices;
    ALScriptStudioAnalysis&            mAnalysis;
    Window&                            mWindow;
    boost::signals2::scoped_connection mOutcomeConnection;
};
