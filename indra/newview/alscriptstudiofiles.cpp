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

#include "alfloaterscriptstudio.h"

#include "alfilewrite.h"
#include "alscriptsnippets.h"
#include "alscriptstudiofileio.h"
#include "alscriptstudiovimrc.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llviewermenufile.h"

#include <algorithm>

using ALScriptFileIO::fileTooLarge;
using ALScriptFileIO::readWholeFile;
using ALScriptFileIO::StudioLiveFile;

// static
std::string ALFloaterScriptStudio::textSyntaxOf(const std::string& path)
{
    // A file that is no script: coloured where it is XML or JSON, which
    // the studio has grammars for -- its snippets are XML -- else text.
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    return extension == "xml" || extension == "xui" ? "xml" : extension == "json" ? "json" : "text";
}

// static
ALFloaterScriptStudio::FileLanguage ALFloaterScriptStudio::languageOfFile(const std::string& path, bool lua_hint)
{
    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    FileLanguage language;
    language.said   = !extension.empty();
    language.script = extension == "lsl" || extension == "lua" || extension == "luau" || lua_hint;
    language.lua    = extension == "lua" || extension == "luau" || (extension != "lsl" && lua_hint);
    return language;
}

void ALFloaterScriptStudio::watchFile(Doc& doc)
{
    if (doc.file.empty() || doc.external.watch)
    {
        return;
    }
    // The file watched for changes made outside, whoever makes them: an
    // editor the studio started, or anything else.
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         id     = doc.id;
    doc.external.watch               = std::make_unique<StudioLiveFile>(
        doc.file,
        [handle, id](const std::string& file) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChangedOutside(id, file);
            }
        },
        false);
}

void ALFloaterScriptStudio::fileChangedOutside(const std::string& id, const std::string& file)
{
    const size_t index = indexOf(id);
    if (index == NONE)
    {
        return;
    }
    Doc& doc = *mDocs[index];
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
        report(getString("FileChangedOutside", args), true, &doc);
        if (doc.askingReload)
        {
            return;
        }
        doc.askingReload = true;
        LLSD question;
        question["NAME"]                 = doc.name;
        const LLHandle<LLFloater> handle = getHandle();
        LLNotificationsUtil::add("ScriptStudioFileChanged", question, LLSD(), [handle, id](const LLSD& notification, const LLSD& response) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            const size_t           index  = studio ? studio->indexOf(id) : NONE;
            if (index == NONE)
            {
                return;
            }
            Doc& asked         = *studio->mDocs[index];
            asked.askingReload = false;
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
            {
                // What is on disk, as one step to undo, and clean.
                studio->revert(asked);
            }
        });
        return;
    }
    if (text == doc.editor->text())
    {
        return;
    }
    // Taken as one step to undo, and clean, since it is what the file is.
    doc.carriedText = text;
    takeCarriedText(doc);
    fileSettled(doc);
    report(getString("FileReloaded", args), false, &doc);
}

void ALFloaterScriptStudio::saveFile(Doc& doc)
{
    doc.save.done();
    LLStringUtil::format_map_t args;
    args["[PATH]"] = doc.file;
    if (!ALFileWrite::whole(doc.file, doc.editor->text()))
    {
        report(getString("SaveToFileFailed", args), true, &doc);
        mSaving.stopped(doc);
        return;
    }
    if (doc.external.watch)
    {
        // The watcher on the file: this write is not an outside change.
        doc.external.watch->seen();
    }
    report(getString("SavedToFile", args), false, &doc);
    fileSettled(doc);
    // The scripter's snippets, offered as saved from here on; the vimrc
    // read again at once.
    for (bool lua : { false, true })
    {
        if (doc.file == ALScriptSnippets::path(lua))
        {
            ALScriptSnippets::forget(lua);
        }
    }
    if (doc.file == ALScriptStudioVimrc::filePath())
    {
        ALScriptStudioVimrc::instance().check(true);
    }
}

void ALFloaterScriptStudio::openFileFromDisk()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                for (const std::string& file : files)
                {
                    studio->openFile(file, false);
                }
            }
        },
        LLFilePicker::FFLOAD_SCRIPT, true);
}

// Each picker answers for the tab it was asked from, by its id: the tab
// in front when the answer comes may be another -- a script handed over,
// a tab a pane opened -- or none, the one asked from closed meanwhile.

void ALFloaterScriptStudio::loadFromFile(bool insert)
{
    const Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, id = doc->id, insert](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToLoad(id, files, insert);
            }
        },
        LLFilePicker::FFLOAD_SCRIPT, false);
}

void ALFloaterScriptStudio::fileChosenToLoad(const std::string& id, const std::vector<std::string>& files, bool insert)
{
    const size_t index = indexOf(id);
    if (index == NONE || files.empty())
    {
        return;
    }
    Doc& doc = *mDocs[index];
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
        setStatus(getString(fileTooLarge(files.front()) ? "FileTooLarge" : "LoadFromFileFailed", args), true);
        return;
    }
    if (text.empty())
    {
        return;
    }
    // In place of the text, or where the caret is, as one step to undo.
    activate(index);
    if (!insert)
    {
        doc.editor->selectAll();
    }
    doc.editor->insertText(text);
}

void ALFloaterScriptStudio::saveToFile()
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, id = doc->id](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSave(id, files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSave(const std::string& id, const std::vector<std::string>& files)
{
    const size_t index = indexOf(id);
    Doc*         doc   = index == NONE ? nullptr : mDocs[index].get();
    if (!doc || files.empty())
    {
        return;
    }
    const bool                 written = ALFileWrite::whole(files.front(), doc->editor->text());
    LLStringUtil::format_map_t args;
    args["[PATH]"] = files.front();
    report(getString(written ? "SavedToFile" : "SaveToFileFailed", args), !written, doc);
}

void ALFloaterScriptStudio::saveFileAs()
{
    Doc* doc = active();
    if (!doc || doc->file.empty())
    {
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, id = doc->id](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->fileChosenToSaveAs(id, files);
            }
        },
        LLFilePicker::FFSAVE_SCRIPT, doc->name);
}

void ALFloaterScriptStudio::fileChosenToSaveAs(const std::string& id, const std::vector<std::string>& files)
{
    const size_t index = indexOf(id);
    Doc*         doc   = index == NONE ? nullptr : mDocs[index].get();
    if (!doc || doc->file.empty() || files.empty())
    {
        return;
    }
    const std::string path = files.front();
    if (path == doc->file)
    {
        saveFile(*doc);
        return;
    }
    LLStringUtil::format_map_t args;
    args["[PATH]"] = path;
    if (indexOf("disk:" + path) != NONE)
    {
        // Open in another tab already: that tab is the file, not this one.
        args["[FILE]"] = path;
        setStatus(getString("FileOpenElsewhere", args), true);
        return;
    }
    if (!ALFileWrite::whole(path, doc->editor->text()))
    {
        report(getString("SaveToFileFailed", args), true, doc);
        return;
    }
    // The tab is the new file from here on: keyed by it, named after
    // it, watched for changes to it, its problems its own, and in the
    // language its name says.
    mProblemsPane->forget(doc->id);
    doc->external.watch.reset();
    if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store(); store && !doc->recoveryKey.empty())
    {
        store->forget(doc->recoveryKey);
    }
    doc->recoveryKey = ALScriptRecoveryStore::keyOf(LLUUID::null, LLUUID::null, path);
    doc->file = path;
    doc->name = gDirUtilp->getBaseFileName(path);
    rekeyDoc(*doc, "disk:" + path);
    if (const FileLanguage said = languageOfFile(path, false); said.said)
    {
        speakFileLanguage(*doc, said);
    }
    watchFile(*doc);
    noteRecentFile(path);
    report(getString("SavedToFile", args), false, doc);
    fileSettled(*doc);
    scheduleAnalysis(*doc, true);
}

void ALFloaterScriptStudio::noteRecentFile(const std::string& path)
{
    const size_t MOST = 10;
    mRecentFiles.erase(std::remove(mRecentFiles.begin(), mRecentFiles.end(), path), mRecentFiles.end());
    mRecentFiles.insert(mRecentFiles.begin(), path);
    if (mRecentFiles.size() > MOST)
    {
        mRecentFiles.resize(MOST);
    }
    fillRecentMenu();
    saveState();
}

void ALFloaterScriptStudio::noteRecentScript(const Doc& doc)
{
    // A script or a notecard from the world or the inventory: a file is
    // noted as a file.
    if (!doc.file.empty() || doc.ref.isNull())
    {
        return;
    }
    const size_t MOST = 10;
    mRecentScripts.erase(std::remove_if(mRecentScripts.begin(), mRecentScripts.end(), [&doc](const Recent& one) { return one.ref == doc.ref; }),
                         mRecentScripts.end());
    mRecentScripts.insert(mRecentScripts.begin(), Recent{ doc.ref, doc.name });
    if (mRecentScripts.size() > MOST)
    {
        mRecentScripts.resize(MOST);
    }
    fillRecentMenu();
    saveState();
}

void ALFloaterScriptStudio::fillRecentMenu()
{
    LLMenuGL* menu = menuBar() ? menuBar()->findChild<LLMenuGL>("open_recent") : nullptr;
    if (!menu)
    {
        return;
    }
    menu->empty();
    // The scripts and notecards opened lately, then the files, each under
    // a word saying which where there are both.
    const LLHandle<LLFloater> handle = getHandle();
    const auto heading = [menu](const std::string& label) {
        LLMenuItemCallGL::Params p;
        p.name  = "recent_heading_" + label;
        p.label = label;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setEnabled(false);
        menu->addChild(item);
    };
    if (!mRecentScripts.empty())
    {
        if (!mRecentFiles.empty())
        {
            heading(getString("RecentScripts"));
        }
        for (const Recent& one : mRecentScripts)
        {
            LLMenuItemCallGL::Params p;
            p.name  = "recent_" + one.ref.id();
            p.label = one.name;
            LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
            const ALScriptRef ref  = one.ref;
            const std::string name = one.name;
            item->setClickCallback([handle, ref, name](LLUICtrl*, const LLSD&) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->openScript(ref, name);
                }
            });
            menu->addChild(item);
        }
        if (!mRecentFiles.empty())
        {
            LLMenuItemSeparatorGL::Params sep;
            menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
            heading(getString("RecentFiles"));
        }
    }
    if (mRecentFiles.empty() && mRecentScripts.empty())
    {
        LLMenuItemCallGL::Params none;
        none.name  = "no_recent";
        none.label = getString("NoRecentFiles");
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
        const bool        twice = std::count_if(mRecentFiles.begin(), mRecentFiles.end(),
                                                [&name](const std::string& other) { return gDirUtilp->getBaseFileName(other) == name; }) > 1;
        LLMenuItemCallGL::Params p;
        p.name  = "recent_" + path;
        p.label = twice ? name + "  (" + gDirUtilp->getDirName(path) + ")" : name;
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setClickCallback([handle, path](LLUICtrl*, const LLSD&) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->openFile(path, false);
            }
        });
        menu->addChild(item);
    }
    LLMenuItemSeparatorGL::Params sep;
    menu->addChild(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sep));
    LLMenuItemCallGL::Params clear;
    clear.name  = "clear_recent";
    clear.label = getString("ClearRecentFiles");
    LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(clear);
    item->setClickCallback([handle](LLUICtrl*, const LLSD&) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->mCommands.run("clear_recent");
        }
    });
    menu->addChild(item);
}
