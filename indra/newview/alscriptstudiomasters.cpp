/**
 * @file alscriptstudiomasters.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiomasters.h"

#include "alcodeeditor.h"
#include "alfilewrite.h"
#include "almastermatch.h"
#include "alnotecardembedded.h"
#include "alscriptenvelope.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudioservices.h"
#include "alserialworker.h"
#include "alwatchedfile.h"
#include "lldir.h"
#include "llsingleton.h"
#include "workqueue.h"

namespace
{
    // A master named in what is said, by its file's name.
    std::string fileNameOf(const std::string& path)
    {
        return gDirUtilp ? gDirUtilp->getBaseFileName(path) : path;
    }

    // Who a change in the world came from, in the window's words.
    const char* whoSaid(ALScriptOrigin origin)
    {
        switch (origin)
        {
            case ALScriptOrigin::Bridge: return "SavedByBridge";
            case ALScriptOrigin::Editor: return "SavedByEditor";
            case ALScriptOrigin::Studio: return "SavedByStudio";
            default: return "SavedByQueue";
        }
    }

    // The thread every window's masters ask the disk on, whether the file a
    // script names is there: made with the first such look, closed as the
    // viewer goes. Owned by no window, so that one closing never waits on
    // a slow drive: what a look finds for a window gone finds nobody.
    class ALScriptMastersDisk final : public LLSingleton<ALScriptMastersDisk>
    {
        LLSINGLETON_EMPTY_CTOR(ALScriptMastersDisk);
        void cleanupSingleton() override
        {
            if (mThread)
            {
                mThread->close();
            }
        }

    public:
        bool post(std::function<void()> job)
        {
            if (!mThread)
            {
                mThread = std::make_unique<ALSerialWorker>("ScriptMastersDisk");
            }
            return mThread->post(std::move(job));
        }

    private:
        std::unique_ptr<ALSerialWorker> mThread;
    };
}

ALScriptStudioMasters::ALScriptStudioMasters(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window, DiskMasters& disk)
: mServices(services), mAnalysis(analysis), mWindow(window), mDiskMasters(disk)
{
    mOutcomeConnection = mDiskMasters.onOutcome([this](const DiskMasters::Outcome& outcome) { heard(outcome); });
    mChangedConnection = mDiskMasters.onChanged([this]() { lookAgain(); });
}

bool ALScriptStudioMasters::canLink(const Doc* doc) const
{
    return doc && doc->file.empty() && doc->loaded && doc->modifiable && !doc->ref.isNull() && !mDiskMasters.linkOf(doc->ref);
}

bool ALScriptStudioMasters::carriesItems(const Doc& doc)
{
    // A file holds text alone: a notecard's items would be lost to it, so
    // one that carries any is never linked, and is told why.
    if (!doc.notecard || !doc.items || doc.items->items().empty())
    {
        return false;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    mServices.report(mServices.words("MasterNotecardCarries", args), true, &doc);
    return true;
}

bool ALScriptStudioMasters::mastersAny(const Doc* doc) const
{
    return doc && !doc->file.empty() && mDiskMasters.masters(doc->file);
}

void ALScriptStudioMasters::linkToFile(Doc& doc)
{
    if (!canLink(&doc) || carriesItems(doc))
    {
        return;
    }
    const std::string id = doc.id;
    mWindow.pickMasterFile([this, id](const std::string& path) {
        Doc* found = mServices.findDoc(id);
        if (!found || !canLink(found) || !ofItsLanguage(*found, path))
        {
            return;
        }
        if (!found->editor->isDirty())
        {
            linkTo(*found, path, ALMasterLink::Made::Picked);
            return;
        }
        // What was typed here is in neither the world nor the file, and
        // the file's text is what the script will hold: the author asked,
        // the file named, whether it goes into the file, over what that
        // holds -- picking the file as the script's master is the consent
        // to that -- or is let go of. The tab found again by its id once
        // answered: it may have gone, or been linked, meanwhile.
        mWindow.askLinkUnsaved(*found, path, [this, id, path](Unsaved answer) {
            Doc* asked = answer == Unsaved::Cancel ? nullptr : mServices.findDoc(id);
            if (!asked || !canLink(asked) || !ofItsLanguage(*asked, path))
            {
                return;
            }
            if (answer == Unsaved::Discard)
            {
                // Linked as a tab with nothing typed is, the file held up to
                // the tab's text as last saved, which is the world's; what
                // was typed is set aside as the tab goes, as any thrown away
                // is.
                linkTo(*asked, path, ALMasterLink::Made::Picked);
                return;
            }
            const bool unsaved = asked->editor->isDirty();
            if (!ALFileWrite::whole(path, asked->editor->wholeText()))
            {
                LLStringUtil::format_map_t args;
                args["[NAME]"] = asked->name;
                args["[FILE]"] = fileNameOf(path);
                mServices.report(mServices.words("MasterNotWritten", args), true, asked);
                return;
            }
            // Safe in the file, which its tab opens holding, to be sent by
            // its save: nothing of it set aside as the tab goes. A write of
            // the studio's own, which the masters' watch hears as none:
            // whatever else the file masters already is sent now, as any
            // write of it sends it.
            mDiskMasters.wrote(path);
            asked->editor->resetDirty();
            linkTo(*asked, path, ALMasterLink::Made::Picked, unsaved);
        });
    });
}

bool ALScriptStudioMasters::ofItsLanguage(const Doc& doc, const std::string& path)
{
    const bool lua = doc.language.lua;
    if (ALDiskIncludes::extensionOf(path, doc.notecard ? ALMasterMatch::notecardExtensions() : ALDiskIncludes::scriptExtensions(lua)) > 0)
    {
        return true;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(path);
    mServices.report(mServices.words(doc.notecard ? "MasterNotNotecardFile" : lua ? "MasterNotSLuaFile" : "MasterNotLSLFile", args), true, &doc);
    return false;
}

void ALScriptStudioMasters::linkTo(Doc& doc, std::string path, ALMasterLink::Made made, bool written)
{
    if (!ofItsLanguage(doc, path) || carriesItems(doc))
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(path);
    // A notecard's text goes up as it is: no language, no target.
    const bool   lua = !doc.notecard && doc.language.lua;
    ALMasterLink link;
    link.object     = doc.ref.object;
    link.item       = doc.ref.item;
    link.master     = path;
    link.made       = made;
    link.lua        = lua;
    link.notecard   = doc.notecard;
    link.target     = doc.notecard ? std::string() : doc.language.compileTarget;
    link.base       = doc.assetId;
    link.itemName   = doc.name;
    link.objectName = doc.objectName;
    link.regionName = doc.regionName;
    link.linked     = LLDate::now();
    const ALScriptRef ref     = doc.ref;
    const bool        bridged = mWindow.heldByBridge(ref);
    // Whether the file is what the script is now: if not, the first save
    // of it sends it, what the world had kept in History. What was typed
    // here and written to it is not in the world; nor is what was typed
    // and is let go of, so the tab's text as last saved is held up to the
    // file -- and nothing is, where an undo past the save and an edit
    // after it left no saved text to step back to.
    const std::optional<std::string> held =
        doc.editor->isDirty() ? doc.editor->undoJournal().savedText() : std::optional<std::string>(doc.editor->wholeText());
    std::string on_disk;
    const bool  differs = written || !held || !ALFileRead::whole(path, on_disk, ALDiskIncludes::MAX_BYTES) || on_disk != *held;
    if (!differs)
    {
        // The file as the world has it: the link starts from it, as from a
        // send of it, so that nothing marks the file as changed since.
        link.stamp = ALFileStamp::of(path).time;
    }
    mDiskMasters.link(link);

    // The script is changed through its file from here: its tab gives way
    // to the file's.
    mWindow.closeTab(doc);
    mWindow.openMasterFile(path, lua);
    Doc* tab = masterTab(path);
    if (tab)
    {
        tab->master->offerFor = ref;
    }
    std::string said = mServices.words("MasterLinked", args);
    if (bridged)
    {
        said += " " + mServices.words("MasterLinkedBridge", args);
    }
    if (differs)
    {
        said += " " + mServices.words("MasterLinkDiffers", args);
    }
    mServices.report(said, bridged, tab, differs && tab ? std::vector<std::string>{ "master_send", "master_compare" } : std::vector<std::string>());
}

void ALScriptStudioMasters::unlink(Doc& doc)
{
    std::vector<ALScriptRef> refs;
    if (!doc.file.empty())
    {
        for (const ALMasterLink& one : mDiskMasters.mastering(doc.file))
        {
            refs.emplace_back(one.object, one.item);
        }
    }
    else if (mDiskMasters.linkOf(doc.ref))
    {
        refs.push_back(doc.ref);
    }
    for (const ALScriptRef& ref : refs)
    {
        const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref);
        if (!link)
        {
            continue;
        }
        mDiskMasters.unlink(ref);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = link->itemName;
        args["[FILE]"] = fileNameOf(link->master);
        mServices.report(mServices.words("MasterUnlinked", args), false, &doc);
    }
}

void ALScriptStudioMasters::sendFromFile(Doc& doc)
{
    if (!mastersAny(&doc))
    {
        return;
    }
    // What goes up is the file on disk: what was typed here, saved first.
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.editor->isDirty())
    {
        mServices.report(mServices.words("MasterSaveFileFirst", args), true, &doc);
        return;
    }
    const std::vector<ALMasterLink> links = mDiskMasters.mastering(doc.file);
    mServices.setStatus(mServices.counted("MasterSending", static_cast<S32>(links.size()), args));
    for (const ALMasterLink& one : links)
    {
        mDiskMasters.send(ALScriptRef(one.object, one.item), ALMasterPlan::Send::Derived);
    }
}

// static
bool ALScriptStudioMasters::openable(const ALMasterLink& link)
{
    return link.state != ALMasterLink::State::Suspended && ALFileStamp::of(link.master).exists;
}

bool ALScriptStudioMasters::openMaster(const ALScriptRef& ref, const std::string& name)
{
    const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref);
    if (!link || !openable(*link))
    {
        return false;
    }
    mWindow.openMasterFile(link->master, link->lua);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name.empty() ? link->itemName : name;
    args["[FILE]"] = fileNameOf(link->master);
    mServices.setStatus(mServices.words("MasterOpened", args));
    return true;
}

bool ALScriptStudioMasters::editMaster(const Doc& doc)
{
    const std::optional<ALMasterLink> link =
        doc.file.empty() && !doc.ref.isNull() ? mDiskMasters.linkOf(doc.ref) : std::nullopt;
    if (!link || !openable(*link))
    {
        return false;
    }
    // The script is changed through its file: the editor given that, where
    // it is, rather than a copy of what the world holds.
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(link->master);
    mServices.report(mServices.words("MasterEditedOutside", args), false, &doc);
    mWindow.editMasterFile(link->master, link->lua);
    return true;
}

void ALScriptStudioMasters::loaded(Doc& doc)
{
    // Linked while it loaded: looked at with the rest, once the load is
    // done with it.
    if (doc.file.empty() && !doc.ref.isNull() && mDiskMasters.linkOf(doc.ref))
    {
        lookAgain();
        return;
    }
    // A notecard names no file of its own: what it says is its readers'.
    if (!canLink(&doc) || doc.notecard)
    {
        return;
    }
    // A file the script names as its own, offered where a hint may reach
    // it: under the folders its includes may be read from, and nowhere
    // else, whatever the script says.
    const bool                       lua   = doc.language.lua;
    const std::string                head  = doc.envelope ? doc.envelope->header : std::string();
    const std::optional<std::string> named = ALMasterMatch::hintOf(doc.editor->wholeText(), head, lua);
    if (!named)
    {
        return;
    }
    // Which folders those are is said here, where the settings and the
    // configurations are read; whether the file is in one is asked of the
    // disk on its own thread, since a drive may be slow, or a share far
    // away. Nothing of the tab goes there: what is found comes back to the
    // tab found again by its id.
    ALDiskIncludes                                   blessed   = mDiskMasters.blessedFor(std::string(), lua);
    std::vector<std::pair<std::string, std::string>> aliases   = mDiskMasters.aliasesFor(std::string(), lua);
    const U32                                        look      = ++doc.master->hintLook;
    const std::string                                id        = doc.id;
    const LL::WorkQueue::ptr_t                       main_loop = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        // No main loop to hand it back to -- a test -- so looked for here.
        std::string why;
        hintFound(id, look, *named, lua, ALMasterMatch::resolve(*named, blessed, aliases, lua, why));
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    ALScriptMastersDisk::instance().post(
        [this, alive, main_loop, id, look, hint = *named, lua, blessed = std::move(blessed), aliases = std::move(aliases)]() {
            std::string                      why;
            const std::optional<std::string> found = ALMasterMatch::resolve(hint, blessed, aliases, lua, why);
            main_loop->post([this, alive, id, look, hint, lua, found]() {
                if (alive.lock())
                {
                    hintFound(id, look, hint, lua, found);
                }
            });
        });
}

void ALScriptStudioMasters::hintFound(const std::string& id, U32 look, const std::string& hint, bool lua, const std::optional<std::string>& found)
{
    Doc* doc = mServices.findDoc(id);
    if (!found || !doc || doc->master->hintLook != look || doc->language.lua != lua || !canLink(doc))
    {
        return;
    }
    // Still the script that named it: what was typed while the disk was
    // asked may have taken the hint out, or named another file.
    const std::string head = doc->envelope ? doc->envelope->header : std::string();
    if (ALMasterMatch::hintOf(doc->editor->wholeText(), head, lua) != hint)
    {
        return;
    }
    doc->master->hinted = *found;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc->name;
    args["[FILE]"] = *found;
    mServices.report(mServices.words("MasterHinted", args), false, doc, { "master_link_hint" });
}

void ALScriptStudioMasters::fileSaved(Doc& doc)
{
    if (doc.file.empty())
    {
        return;
    }
    if (mastersAny(&doc))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        mServices.setStatus(mServices.counted("MasterSending", static_cast<S32>(mDiskMasters.mastering(doc.file).size()), args));
    }
    // What it masters sent, and the scripts that include it sent again --
    // those no tab holds among them -- whether it masters anything or not.
    mDiskMasters.wrote(doc.file);
}

void ALScriptStudioMasters::saved(const ALScriptSaved& saved)
{
    // A tab here of a linked script kept as it was linked, for what was
    // typed in it, is clean once that is saved. The links change with
    // most such saves, which looks again; not with one of just what the
    // file last sent, which moves nothing else.
    Doc* doc = mServices.findDoc(saved.ref);
    if (doc && mDiskMasters.linkOf(saved.ref))
    {
        // Saved, whatever an undo had made of it: given way as a save's is.
        doc->master->toldClean = false;
        lookAgain();
    }
}

void ALScriptStudioMasters::sayUnheard()
{
    for (const DiskMasters::Outcome& outcome : mDiskMasters.takeUnheard())
    {
        heard(outcome);
    }
}

void ALScriptStudioMasters::offer(Doc& doc, const std::string& action)
{
    const ALScriptRef ref = doc.file.empty() ? doc.ref : doc.master->offerFor;
    if (action == "master_link_hint")
    {
        if (doc.master->hinted.empty() || !canLink(&doc))
        {
            return;
        }
        // What was typed since the offer is in neither the world nor the
        // file, and the tab giving way would set it aside unasked. Nothing
        // a script names is written to, so it is not put in the file
        // either, as a file picked may be: saved or reverted first.
        if (doc.editor->isDirty())
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"] = doc.name;
            args["[FILE]"] = fileNameOf(doc.master->hinted);
            mServices.report(mServices.words("MasterHintUnsaved", args), true, &doc, { "master_link_hint" });
            return;
        }
        linkTo(doc, doc.master->hinted, ALMasterLink::Made::Hint);
    }
    else if (action == "master_send" && !ref.isNull())
    {
        // Asked for outright: sent as a save of the master is, over a
        // change in the world, which is kept first.
        mDiskMasters.send(ref, ALMasterPlan::Send::Direct);
    }
    else if (action == "master_compare" && !ref.isNull())
    {
        compareWithWorld(doc, ref);
    }
    else if (action == "master_compare_file" && doc.file.empty() && !ref.isNull())
    {
        compareWithFile(doc, ref);
    }
    else if (action == "master_give_way" && doc.file.empty() && !ref.isNull())
    {
        // As a clean tab gives way as its script is linked; typed in again
        // since it was offered, told so again, and kept.
        const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref);
        if (!link || !openable(*link))
        {
            return;
        }
        if (doc.editor->isDirty())
        {
            doc.master->toldClean = false;
            tellKept(doc, *link);
            return;
        }
        giveWayTo(doc, *link);
    }
    else if (action == "master_open_file" && doc.file.empty() && !ref.isNull())
    {
        // The file's tab opened beside the item's, which is kept, for what
        // was typed in it to be copied over.
        if (const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref))
        {
            mWindow.openMasterFile(link->master, link->lua);
            if (Doc* tab = masterTab(link->master))
            {
                tab->master->offerFor = ref;
            }
        }
    }
    else if (action == "master_unlink" && !ref.isNull())
    {
        if (const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref))
        {
            mDiskMasters.unlink(ref);
            LLStringUtil::format_map_t args;
            args["[NAME]"] = link->itemName;
            args["[FILE]"] = fileNameOf(link->master);
            mServices.report(mServices.words("MasterUnlinked", args), false, &doc);
        }
    }
    else if (action == "master_find" && !ref.isNull())
    {
        // Its master found again where it went: the link kept, its file the
        // one picked.
        mWindow.pickMasterFile([this, ref](const std::string& path) {
            std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref);
            if (!link)
            {
                return;
            }
            link->master = path;
            link->state  = ALMasterLink::State::Active;
            mDiskMasters.link(*link);
            LLStringUtil::format_map_t args;
            args["[NAME]"] = link->itemName;
            args["[FILE]"] = fileNameOf(path);
            mWindow.openMasterFile(path, link->lua);
            mServices.report(mServices.words("MasterRelinked", args), false, masterTab(path));
        });
    }
}

void ALScriptStudioMasters::compareWithWorld(Doc& doc, const ALScriptRef& ref)
{
    // Answered later, the tab found again by its id: the window may have
    // closed meanwhile, or the tab.
    const std::string         id    = doc.id;
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.loadWorldText(ref, [this, alive, id](const ALScriptLoaded& loaded) {
        Doc* found = alive.lock() ? mServices.findDoc(id) : nullptr;
        if (!found || !loaded.error.empty())
        {
            return;
        }
        // The author's text as it is in the world, out of its envelope.
        std::string theirs = loaded.text;
        if (const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(loaded.text))
        {
            theirs = envelope->source;
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = loaded.name;
        mWindow.compare(*found, found->editor->wholeText(), theirs, mServices.words("CompareMasterFile", args),
                        mServices.words("CompareMasterWorld", args));
    });
}

void ALScriptStudioMasters::compareWithFile(Doc& doc, const ALScriptRef& ref)
{
    const std::optional<ALMasterLink> link = mDiskMasters.linkOf(ref);
    if (!link)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(link->master);
    // The file as it is on disk, which is what a save of it sends, beside
    // what was typed here.
    std::string on_disk;
    if (!ALFileRead::whole(link->master, on_disk, ALDiskIncludes::MAX_BYTES))
    {
        mServices.report(mServices.words("MasterNotRead", args), true, &doc);
        return;
    }
    mWindow.compare(doc, on_disk, doc.editor->wholeText(), mServices.words("CompareMasterOnDisk", args), mServices.words("CompareNow"));
}

void ALScriptStudioMasters::lookAgain()
{
    // Not deep in whoever changed the links -- a tab being linked, which
    // closes itself after; a save heard; a load -- but once that is done,
    // and once however often they changed meanwhile. With no main loop to
    // wait for -- a test -- not at all.
    if (mLookingAgain)
    {
        return;
    }
    const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mLookingAgain                   = main_loop->post([this, alive]() {
        if (alive.lock())
        {
            mLookingAgain = false;
            giveWay();
        }
    });
}

void ALScriptStudioMasters::giveWay()
{
    // By their ids, since a tab giving way goes from among them.
    std::vector<std::string> items;
    for (const Doc* doc : mServices.openDocs())
    {
        if (doc->file.empty() && !doc->ref.isNull())
        {
            items.push_back(doc->id);
        }
    }
    for (const std::string& id : items)
    {
        Doc* doc = mServices.findDoc(id);
        if (!doc)
        {
            continue;
        }
        const std::optional<ALMasterLink> link = mDiskMasters.linkOf(doc->ref);
        if (!link)
        {
            // Let go of, or never linked: told again, should it be linked.
            doc->master->toldLinked.clear();
            doc->master->toldClean = false;
            continue;
        }
        // Left as it is while it loads, or a save of it is on its way; and
        // where the file could not be opened in its place, as a script asked
        // for while linked is not.
        if (!doc->loaded || doc->saveUnderway() || !openable(*link))
        {
            continue;
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc->name;
        args["[FILE]"] = fileNameOf(link->master);
        if (doc->editor->isDirty())
        {
            // What was typed here is in neither the world nor the file: kept,
            // and the author told, once, on the tab -- with the file to set
            // beside it, to see what differs or to copy it over, and the
            // link to let go of for it to be saved from here again.
            if (doc->master->toldLinked == link->master)
            {
                continue;
            }
            doc->master->toldLinked = link->master;
            doc->master->toldClean  = false;
            mServices.report(mServices.words("MasterLinkedUnsaved", args), true, doc,
                             { "master_compare_file", "master_open_file", "master_unlink" });
            continue;
        }
        // Made clean again by an undo while kept: kept still, its notice
        // offering it to give way, rather than closed under the author.
        if (doc->master->toldClean && doc->master->toldLinked == link->master)
        {
            continue;
        }
        // Nothing typed here -- never, or no longer, once what was kept was
        // saved or reverted: the file's tab in its place, as a script asked
        // for while linked opens.
        giveWayTo(*doc, *link);
    }
}

void ALScriptStudioMasters::giveWayTo(Doc& doc, const ALMasterLink& link)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(link.master);
    // Saved from here, the world holds what the file does not: the file's
    // tab offers to send the file over it, or to compare the two, as a
    // change heard in the world does.
    const ALScriptRef ref = doc.ref;
    mWindow.closeTab(doc);
    mWindow.openMasterFile(link.master, link.lua);
    Doc*                     tab  = masterTab(link.master);
    std::string              said = mServices.words("MasterGaveWay", args);
    std::vector<std::string> offers;
    if (tab && link.state == ALMasterLink::State::Differing)
    {
        tab->master->offerFor = ref;
        said += " " + mServices.words("MasterLinkDiffers", args);
        offers = { "master_send", "master_compare" };
    }
    mServices.report(said, false, tab, offers);
}

void ALScriptStudioMasters::textChanged(Doc& doc)
{
    // Every edit of every tab comes here: only one kept as its script was
    // linked goes further.
    if (doc.master->toldLinked.empty() || !doc.file.empty())
    {
        return;
    }
    // Clean again by an undo or a redo, back to the text as saved, a step
    // either way from it. A text put in whole -- a revert, a load -- is
    // clean with no step either way: it is looked at again as it loads,
    // and gives way then, as a save does.
    const ALTextUndo& journal = doc.editor->undoJournal();
    const bool        clean   = !doc.editor->isDirty();
    const bool        whole   = !journal.canUndo() && !journal.canRedo();
    const bool        undone  = clean && !whole;
    if (undone == doc.master->toldClean)
    {
        return;
    }
    doc.master->toldClean = undone;
    if (clean && whole)
    {
        return;
    }
    const std::optional<ALMasterLink> link = mDiskMasters.linkOf(doc.ref);
    if (link && link->master == doc.master->toldLinked)
    {
        tellKept(doc, *link);
    }
}

void ALScriptStudioMasters::tellKept(Doc& doc, const ALMasterLink& link)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(link.master);
    if (doc.master->toldClean)
    {
        doc.offer = Doc::Offer{ mServices.words("MasterLinkedClean", args),
                                { "master_give_way", "master_compare_file", "master_open_file", "master_unlink" } };
    }
    else
    {
        doc.offer = Doc::Offer{ mServices.words("MasterLinkedUnsaved", args), { "master_compare_file", "master_open_file", "master_unlink" } };
    }
    mWindow.refreshNotice();
}

ALScriptStudioMasters::Doc* ALScriptStudioMasters::masterTab(const std::string& master) const
{
    return mServices.findDoc("disk:" + master);
}

void ALScriptStudioMasters::heard(const DiskMasters::Outcome& outcome)
{
    typedef DiskMasters::Outcome::What What;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = outcome.itemName;
    args["[FILE]"] = fileNameOf(outcome.master);
    args["[WHY]"]  = outcome.why;
    // Said on the master's tab where it is open, which its offers act on.
    Doc* tab = masterTab(outcome.master);
    if (tab)
    {
        tab->master->offerFor = outcome.ref;
    }
    const auto said = [&](const char* key, bool failure, std::vector<std::string> actions = {}) {
        mServices.report(mServices.words(key, args), failure, tab, tab ? actions : std::vector<std::string>());
    };
    switch (outcome.what)
    {
        case What::Sent:
        {
            const bool compiled = outcome.result && outcome.result->success;
            // What the compiler said, in the master's tab, through the map
            // the send went up with.
            if (tab && outcome.result)
            {
                tab->problems = Doc::compiledOf(outcome.result->diagnostics, outcome.result->sourceMap.get(), outcome.result->codeLine);
                const std::vector<Doc::Compiled> found = Doc::compiledOf(outcome.preprocessed, nullptr, 0);
                tab->problems.insert(tab->problems.end(), found.begin(), found.end());
                mAnalysis.refreshProblems(*tab);
            }
            std::string text = mServices.words(compiled ? "MasterSent" : "MasterNotCompiled", args);
            if (outcome.keptTheirs)
            {
                text += " " + mServices.words("MasterKeptTheirs", args);
            }
            mServices.report(text, !compiled, tab);
            return;
        }
        case What::Failed:
            // Not expanded: why, in the master's tab.
            if (tab && !outcome.preprocessed.empty())
            {
                tab->problems = Doc::compiledOf(outcome.preprocessed, nullptr, 0);
                mAnalysis.refreshProblems(*tab);
            }
            said("MasterSendFailed", true, { "master_send" });
            return;
        case What::Skipped: said("MasterSkipped", false); return;
        case What::Held: said("MasterHeld", true, { "master_send", "master_compare", "master_unlink" }); return;
        case What::Differing:
            args["[WHO]"] = mServices.words(whoSaid(outcome.by));
            said("MasterDiffering", true, { "master_send", "master_compare", "master_unlink" });
            return;
        case What::Suspended: said("MasterSuspended", true, { "master_find", "master_unlink" }); return;
        case What::Orphaned: said("MasterOrphaned", false, { "master_unlink" }); return;
        case What::Pending: said("MasterPending", false, { "master_send" }); return;
        // Of many scripts at once, in words already.
        case What::NotSent: mServices.report(outcome.why, false); return;
    }
}
