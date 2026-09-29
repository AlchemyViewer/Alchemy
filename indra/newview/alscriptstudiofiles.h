/**
 * @file alscriptstudiofiles.h
 * @brief Script Studio's files on disk: their languages, watched for changes outside, written, the File menu's pickers, the recent lists.
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

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ALScriptStudioServices;
class ALScriptStudioTabs;
class LLMenuGL;
class LLSD;

// A Script Studio window's files on disk, apart from opening one in a tab,
// which is the window's: what a file's name says it holds; a tab's file
// watched, and what changes it outside taken, or asked about where the
// tab has changes of its own; a tab's file written; the File menu's Open,
// Load, Insert, Save to File and Save As, through the viewer's pickers;
// and the files, scripts and notecards opened lately, under Open Recent
// and kept with the window's state.
class ALScriptStudioFiles
{
public:
    typedef ALScriptStudioDoc Doc;

    // What a file's name says it holds: an LSL or an SLua script, or, with
    // neither extension, what it was asked for as, else plain text.
    struct Language
    {
        bool script = false;
        bool lua    = false;
        // Whether the name said anything: it has an extension.
        bool said   = false;
    };
    static Language languageOf(const std::string& path, bool lua_hint);
    // The grammar a file that is no script is coloured by: XML or JSON,
    // which the studio has, else text.
    static std::string textSyntaxOf(const std::string& path);

    // A script or notecard opened lately, by what holds it.
    struct Recent
    {
        ALScriptRef ref;
        std::string name;
    };

    // What the files ask of the window beyond its services and its tabs.
    class Window
    {
    public:
        // A tab checked again.
        virtual void scheduleAnalysis(Doc& doc, bool now) = 0;
        // The viewer's pickers, which answer later: files to open or load,
        // several or one; where to save one, its name to start from.
        virtual void pickFilesToOpen(bool several, std::function<void(const std::vector<std::string>& files)> chosen)            = 0;
        virtual void pickFileToSave(const std::string& name, std::function<void(const std::vector<std::string>& files)> chosen) = 0;
        // Whether to read a tab's file again, changed outside, over what is
        // typed in the tab: `answered` with true to read it.
        virtual void askReload(const Doc& doc, std::function<void(bool reload)> answered) = 0;
        // A tab's file written and taken as saved: the window's part -- the
        // weights and recovery told, the scripts that include it expanded
        // again, a close that waited on it done.
        virtual void fileSettled(Doc& doc) = 0;
        // A save of a tab's file that did not go.
        virtual void saveStopped(Doc& doc) = 0;
        // A file written that the studio reads something from: the
        // snippets, the vimrc.
        virtual void fileWritten(const std::string& path) = 0;
        // A tab's file changed on disk, or went: what is in reach of the
        // tabs to be looked at again (ALScriptStudioOrphans).
        virtual void reachChanged() = 0;
        // A tab that is a file, the file at `path` from here on: keyed by
        // it, named after it, its problems and what recovery kept of it
        // forgotten, in the language its name says.
        virtual void becomeFile(Doc& doc, const std::string& path) = 0;
        // Open Recent's menu; and the lists changed, to be kept with the
        // window's state.
        virtual LLMenuGL* recentMenu()   = 0;
        virtual void      recentChanged() = 0;

    protected:
        ~Window() = default;
    };

    ALScriptStudioFiles(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, Window& window);

    // A tab's file watched for changes made to it outside, whoever makes
    // them: an editor the studio started, or anything else. Taken where
    // the tab is clean; asked about, once while the question is up, where
    // it is not.
    void watch(Doc& doc);
    void changedOutside(const std::string& id, const std::string& file);
    // A tab's text written to its file, whole or not at all.
    void write(Doc& doc);

    // The File menu's: files opened, several at once; one loaded into the
    // tab in front, in place of its text or at the caret; the tab in front
    // saved to a file as a copy, or, where it is a file, as another file,
    // which it is from then on. Each picker answers for the tab it was
    // asked from, by its id: the tab in front when the answer comes may be
    // another, or none.
    void openFromDisk();
    void load(bool insert);
    void chosenToLoad(const std::string& id, const std::vector<std::string>& files, bool insert);
    void saveCopy();
    void chosenToSave(const std::string& id, const std::vector<std::string>& files);
    void saveAs();
    void chosenToSaveAs(const std::string& id, const std::vector<std::string>& files);

    // The files opened from disk lately, newest first, ten at most, each
    // once; and the scripts and notecards opened from the world and the
    // inventory, the same. Open Recent filled from them, and the window
    // told.
    void noteFile(const std::string& path);
    void noteScript(const Doc& doc);
    void clearRecent();
    // What cannot be had any more, let go of: a script `gone` says of, and
    // a file no longer on disk; the window told where anything went.
    // Whether anything did.
    bool pruneRecent(const std::function<bool(const ALScriptRef&)>& gone);
    void fillMenu();
    const std::vector<std::string>& recentFiles() const { return mRecentFiles; }
    const std::vector<Recent>&      recentScripts() const { return mRecentScripts; }
    // The lists kept with the window's state, and read back from it.
    void writeState(LLSD& state) const;
    void readState(const LLSD& state);

private:
    ALScriptStudioServices&  mServices;
    ALScriptStudioTabs&      mTabs;
    Window&                  mWindow;
    std::vector<std::string> mRecentFiles;
    std::vector<Recent>      mRecentScripts;
    // Held while this is, for a watch, a picker or a menu item to know it
    // still is.
    std::shared_ptr<bool>    mAlive = std::make_shared<bool>(true);
};
