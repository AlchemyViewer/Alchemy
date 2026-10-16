/**
 * @file alscriptstudiofiles.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiofiles.h"

#include "alcodeeditor.h"
#include "alfilewrite.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudiofileio.h"
#include "alscriptstudiosaves.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiotabs.h"
#include "lldir.h"
#include "llfile.h"
#include "llmenugl.h"
#include "llsd.h"
#include "lluictrlfactory.h"

#include <algorithm>

using ALScriptFileIO::fileTooLarge;
using ALScriptFileIO::readWholeFile;
using ALScriptFileIO::StudioLiveFile;

namespace
{
    // How many of each the recent lists keep.
    constexpr size_t MOST_RECENT = 10;
}

ALScriptStudioFiles::ALScriptStudioFiles(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, ALScriptStudioAnalysis& analysis, ALScriptStudioSaves& saves, Window& window) : mServices(services), mTabs(tabs), mAnalysis(analysis), mSaves(saves), mWindow(window)
{
}

// static
std::string ALScriptStudioFiles::textSyntaxOf(const std::string& path)
{
    // A file that is no script: coloured by the grammar whose file names
    // its extension -- XML, JSON, a notecard of settings -- else as text.
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    const std::shared_ptr<const ALSyntaxGrammar> grammar = ALTextView::syntaxLibrary().forExtension(extension);
    return grammar ? grammar->name() : std::string("text");
}

// static
ALScriptStudioFiles::Language ALScriptStudioFiles::languageOf(const std::string& path, bool lua_hint)
{
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    Language language;
    language.said   = !extension.empty();
    language.script = extension == "lsl" || extension == "lua" || extension == "luau" || lua_hint;
    language.lua    = extension == "lua" || extension == "luau" || (extension != "lsl" && lua_hint);
    return language;
}

// --- watching and writing ---------------------------------------------------------------

void ALScriptStudioFiles::watch(Doc& doc)
{
    if (doc.file.empty() || doc.watch)
    {
        return;
    }
    // The file watched for changes made outside, whoever makes them: an
    // editor the studio started, or anything else.
    const std::weak_ptr<bool> alive = mAlive;
    const std::string         id    = doc.id;
    doc.watch                       = std::make_unique<StudioLiveFile>(
        doc.file,
        [this, alive, id](const std::string& file) {
            if (alive.lock())
            {
                changedOutside(id, file);
            }
        },
        nullptr);
}

void ALScriptStudioFiles::changedOutside(const std::string& id, const std::string& file)
{
    Doc* found = mServices.findDoc(id);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    mWindow.reachChanged();
    // Gone, or not to be read: nothing to take. Deleted, the tab keeps
    // what it holds and the check on what holds each tab says the file
    // is gone; mid-save by something that writes it in two steps, it is
    // heard again once it is back.
    std::string text;
    if (!readWholeFile(file, text))
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.editor->isDirty())
    {
        // What is typed here is not thrown away for it: the author is told,
        // and asked which to keep -- once, however often it changes while
        // the question is up.
        mServices.report(mServices.words("FileChangedOutside", args), true, &doc);
        if (doc.askingReload)
        {
            return;
        }
        doc.askingReload                = true;
        const std::weak_ptr<bool> alive = mAlive;
        mWindow.askReload(doc, [this, alive, id](bool reload) {
            Doc* asked = alive.lock() ? mServices.findDoc(id) : nullptr;
            if (!asked)
            {
                return;
            }
            asked->askingReload = false;
            if (reload)
            {
                // What is on disk, as one step to undo, and clean.
                mTabs.revert(*asked);
            }
        });
        return;
    }
    if (text == doc.editor->wholeText())
    {
        return;
    }
    // Taken as one step to undo, and clean, since it is what the file is.
    doc.carriedText = text;
    mTabs.takeCarriedText(doc);
    mWindow.fileSettled(doc);
    mServices.report(mServices.words("FileReloaded", args), false, &doc);
}

void ALScriptStudioFiles::write(Doc& doc)
{
    doc.save.done();
    LLStringUtil::format_map_t args;
    args["[PATH]"] = doc.file;
    if (!ALFileWrite::whole(doc.file, doc.editor->wholeText()))
    {
        mServices.report(mServices.words("SaveToFileFailed", args), true, &doc);
        mSaves.stopped(doc);
        return;
    }
    if (doc.watch)
    {
        // The watcher on the file: this write is not an outside change.
        doc.watch->seen();
    }
    mServices.report(mServices.words("SavedToFile", args), false, &doc);
    // The scripter's snippets, offered as saved from here on; the vimrc
    // read again at once; what it masters sent. Before it is settled,
    // which may close it.
    mWindow.fileWritten(doc);
    mWindow.fileSettled(doc);
}

// --- the File menu -----------------------------------------------------------------------

void ALScriptStudioFiles::openFromDisk()
{
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.pickFilesToOpen(true, [this, alive](const std::vector<std::string>& files) {
        if (alive.lock())
        {
            for (const std::string& file : files)
            {
                mTabs.openFileTab(file, false);
            }
        }
    });
}

void ALScriptStudioFiles::load(bool insert)
{
    const Doc* doc = mServices.frontDoc();
    if (!doc)
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.pickFilesToOpen(false, [this, alive, id = doc->id, insert](const std::vector<std::string>& files) {
        if (alive.lock())
        {
            chosenToLoad(id, files, insert);
        }
    });
}

void ALScriptStudioFiles::chosenToLoad(const std::string& id, const std::vector<std::string>& files, bool insert)
{
    Doc* found = mServices.findDoc(id);
    if (!found || files.empty())
    {
        return;
    }
    Doc& doc = *found;
    // Over the text as loaded, where it may be changed: over one still
    // loading, the load would put its own back over it.
    if (!doc.loaded || !doc.modifiable)
    {
        return;
    }
    std::string text;
    if (!readWholeFile(files.front(), text))
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = files.front();
        mServices.setStatus(mServices.words(fileTooLarge(files.front()) ? "FileTooLarge" : "LoadFromFileFailed", args), true);
        return;
    }
    if (text.empty())
    {
        return;
    }
    // In place of the text, or where the caret is, as one step to undo.
    mTabs.activate(doc);
    if (!insert)
    {
        doc.editor->selectAll();
    }
    doc.editor->insertText(text);
}

void ALScriptStudioFiles::saveCopy()
{
    const Doc* doc = mServices.frontDoc();
    if (!doc)
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.pickFileToSave(doc->name, [this, alive, id = doc->id](const std::vector<std::string>& files) {
        if (alive.lock())
        {
            chosenToSave(id, files);
        }
    });
}

void ALScriptStudioFiles::chosenToSave(const std::string& id, const std::vector<std::string>& files)
{
    Doc* doc = mServices.findDoc(id);
    if (!doc || files.empty())
    {
        return;
    }
    const bool                 written = ALFileWrite::whole(files.front(), doc->editor->wholeText());
    LLStringUtil::format_map_t args;
    args["[PATH]"] = files.front();
    mServices.report(mServices.words(written ? "SavedToFile" : "SaveToFileFailed", args), !written, doc);
}

void ALScriptStudioFiles::saveAs()
{
    const Doc* doc = mServices.frontDoc();
    if (!doc || doc->file.empty())
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.pickFileToSave(doc->name, [this, alive, id = doc->id](const std::vector<std::string>& files) {
        if (alive.lock())
        {
            chosenToSaveAs(id, files);
        }
    });
}

void ALScriptStudioFiles::chosenToSaveAs(const std::string& id, const std::vector<std::string>& files)
{
    Doc* doc = mServices.findDoc(id);
    if (!doc || doc->file.empty() || files.empty())
    {
        return;
    }
    const std::string path = files.front();
    if (path == doc->file)
    {
        write(*doc);
        return;
    }
    LLStringUtil::format_map_t args;
    args["[PATH]"] = path;
    if (mServices.findDoc("disk:" + path))
    {
        // Open in another tab already: that tab is the file, not this one.
        args["[FILE]"] = path;
        mServices.setStatus(mServices.words("FileOpenElsewhere", args), true);
        return;
    }
    if (!ALFileWrite::whole(path, doc->editor->wholeText()))
    {
        mServices.report(mServices.words("SaveToFileFailed", args), true, doc);
        return;
    }
    // The tab is the new file from here on: keyed by it, named after
    // it, watched for changes to it, its problems its own, and in the
    // language its name says.
    doc->watch.reset();
    mWindow.becomeFile(*doc, path);
    watch(*doc);
    noteFile(path);
    mServices.report(mServices.words("SavedToFile", args), false, doc);
    mWindow.fileWritten(*doc);
    mWindow.fileSettled(*doc);
    mAnalysis.scheduleAnalysis(*doc, true);
}

// --- the recent lists ---------------------------------------------------------------------

void ALScriptStudioFiles::noteFile(const std::string& path)
{
    mRecentFiles.erase(std::remove(mRecentFiles.begin(), mRecentFiles.end(), path), mRecentFiles.end());
    mRecentFiles.insert(mRecentFiles.begin(), path);
    if (mRecentFiles.size() > MOST_RECENT)
    {
        mRecentFiles.resize(MOST_RECENT);
    }
    fillMenu();
    mWindow.recentChanged();
}

void ALScriptStudioFiles::noteScript(const Doc& doc)
{
    // A script or a notecard from the world or the inventory: a file is
    // noted as a file.
    if (!doc.file.empty() || doc.ref.isNull())
    {
        return;
    }
    const auto same = [&doc](const Recent& one) { return one.ref == doc.ref; };
    mRecentScripts.erase(std::remove_if(mRecentScripts.begin(), mRecentScripts.end(), same), mRecentScripts.end());
    mRecentScripts.insert(mRecentScripts.begin(), Recent{ doc.ref, doc.name });
    if (mRecentScripts.size() > MOST_RECENT)
    {
        mRecentScripts.resize(MOST_RECENT);
    }
    fillMenu();
    mWindow.recentChanged();
}

bool ALScriptStudioFiles::pruneRecent(const std::function<bool(const ALScriptRef&)>& gone)
{
    const size_t scripts = mRecentScripts.size();
    const size_t files   = mRecentFiles.size();
    mRecentScripts.erase(std::remove_if(mRecentScripts.begin(), mRecentScripts.end(), [&gone](const Recent& one) { return gone(one.ref); }),
                         mRecentScripts.end());
    mRecentFiles.erase(std::remove_if(mRecentFiles.begin(), mRecentFiles.end(), [](const std::string& path) { return !LLFile::isfile(path); }),
                       mRecentFiles.end());
    const bool pruned = mRecentScripts.size() != scripts || mRecentFiles.size() != files;
    if (pruned)
    {
        fillMenu();
        mWindow.recentChanged();
    }
    return pruned;
}

void ALScriptStudioFiles::clearRecent()
{
    mRecentFiles.clear();
    mRecentScripts.clear();
    fillMenu();
    mWindow.recentChanged();
}

void ALScriptStudioFiles::fillMenu()
{
    LLMenuGL* menu = mWindow.recentMenu();
    if (!menu)
    {
        return;
    }
    menu->empty();
    // The scripts and notecards opened lately, then the files, each under
    // a word saying which where there are both.
    const std::weak_ptr<bool> alive   = mAlive;
    const auto                heading = [menu](const std::string& label) {
        LLMenuItemCallGL::Params p;
        p.name                 = "recent_heading_" + label;
        p.label                = label;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setEnabled(false);
        menu->addChild(item);
    };
    if (!mRecentScripts.empty())
    {
        if (!mRecentFiles.empty())
        {
            heading(mServices.words("RecentScripts"));
        }
        for (const Recent& one : mRecentScripts)
        {
            LLMenuItemCallGL::Params p;
            p.name                 = "recent_" + one.ref.id();
            p.label                = one.name;
            LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
            const ALScriptRef ref  = one.ref;
            const std::string name = one.name;
            item->setClickCallback([this, alive, ref, name](LLUICtrl*, const LLSD&) {
                if (alive.lock())
                {
                    mServices.openScript(ref, name);
                }
            });
            menu->addChild(item);
        }
        if (!mRecentFiles.empty())
        {
            LLMenuItemSeparatorGL::Params sep;
            menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
            heading(mServices.words("RecentFiles"));
        }
    }
    if (mRecentFiles.empty() && mRecentScripts.empty())
    {
        LLMenuItemCallGL::Params none;
        none.name              = "no_recent";
        none.label             = mServices.words("NoRecentFiles");
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(none);
        item->setEnabled(false);
        menu->addChild(item);
        return;
    }
    // Each file by its name, the folder after it where two share a name;
    // the callback bound here rather than looked up by name, since the
    // registry answers for whichever studio registered last.
    for (const std::string& path : mRecentFiles)
    {
        const std::string name  = gDirUtilp->getBaseFileName(path);
        const auto        named = [&name](const std::string& other) { return gDirUtilp->getBaseFileName(other) == name; };
        const bool        twice = std::count_if(mRecentFiles.begin(), mRecentFiles.end(), named) > 1;
        LLMenuItemCallGL::Params p;
        p.name                 = "recent_" + path;
        p.label                = twice ? name + "  (" + gDirUtilp->getDirName(path) + ")" : name;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setClickCallback([this, alive, path](LLUICtrl*, const LLSD&) {
            if (alive.lock())
            {
                mTabs.openFileTab(path, false);
            }
        });
        menu->addChild(item);
    }
    LLMenuItemSeparatorGL::Params sep;
    menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
    LLMenuItemCallGL::Params clear;
    clear.name             = "clear_recent";
    clear.label            = mServices.words("ClearRecentFiles");
    LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(clear);
    item->setClickCallback([this, alive](LLUICtrl*, const LLSD&) {
        if (alive.lock())
        {
            clearRecent();
        }
    });
    menu->addChild(item);
}

void ALScriptStudioFiles::writeState(LLSD& state) const
{
    LLSD recent = LLSD::emptyArray();
    for (const std::string& path : mRecentFiles)
    {
        recent.append(path);
    }
    state["recent_files"] = recent;
    LLSD scripts = LLSD::emptyArray();
    for (const Recent& one : mRecentScripts)
    {
        LLSD entry;
        entry["object"] = one.ref.object;
        entry["item"]   = one.ref.item;
        entry["name"]   = one.name;
        scripts.append(entry);
    }
    state["recent_scripts"] = scripts;
}

void ALScriptStudioFiles::readState(const LLSD& state)
{
    if (state.has("recent_scripts"))
    {
        mRecentScripts.clear();
        for (LLSD::array_const_iterator it = state["recent_scripts"].beginArray(); it != state["recent_scripts"].endArray(); ++it)
        {
            const ALScriptRef ref((*it)["object"].asUUID(), (*it)["item"].asUUID());
            if (!ref.isNull())
            {
                mRecentScripts.push_back(Recent{ ref, (*it)["name"].asString() });
            }
        }
    }
    if (state.has("recent_files"))
    {
        mRecentFiles.clear();
        for (LLSD::array_const_iterator it = state["recent_files"].beginArray(); it != state["recent_files"].endArray(); ++it)
        {
            const std::string path = it->asString();
            if (!path.empty() && std::find(mRecentFiles.begin(), mRecentFiles.end(), path) == mRecentFiles.end())
            {
                mRecentFiles.push_back(path);
            }
        }
    }
    fillMenu();
}
