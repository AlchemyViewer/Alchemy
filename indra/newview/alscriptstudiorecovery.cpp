/**
 * @file alscriptstudiorecovery.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterscriptstudio.h"

#include "alcodeeditor.h"
#include "alquickopen.h"
#include "alscriptnotecardtab.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "llnotificationsutil.h"
#include "lltimer.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <memory>

namespace
{
    // How long after the first change since it was last written a tab's
    // unsaved text is written again, whatever is typed meanwhile; and how
    // long discarded text is kept before it goes for good.
    const F64 RECOVERY_DELAY = 1.5;
    const F64 DISCARDED_KEPT = 7.0 * 24.0 * 60.0 * 60.0;
}

// --- recovery ------------------------------------------------------------------------

// static
ALScriptRecoveryStore* ALFloaterScriptStudio::recoveryStore()
{
    // One for the account logged in, under its own folder; made again where
    // another has logged in since. Every entry this process writes is under
    // one session, which is how the next session tells what this one left.
    static std::unique_ptr<ALScriptRecoveryStore> store;
    static std::string                            made_for;
    static const std::string                      session = LLUUID::generateNewID().asString();
    if (gDirUtilp->getLindenUserDir().empty())
    {
        return nullptr;
    }
    const std::string directory = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "script_studio_recovery");
    if (!store || made_for != directory)
    {
        LLFile::mkdir(directory);
        store    = std::make_unique<ALScriptRecoveryStore>(directory, session);
        made_for = directory;
        // What was discarded long ago goes for good.
        store->prune(DISCARDED_KEPT);
    }
    return store.get();
}

ALScriptRecoveryEntry ALFloaterScriptStudio::recoveryEntryOf(const Doc& doc) const
{
    // From the tab alone -- nothing of the world's asked -- since it is
    // written as the viewer goes as well, after the world may have.
    ALScriptRecoveryEntry entry;
    entry.key           = doc.recoveryKey;
    entry.object        = doc.file.empty() ? doc.ref.object : LLUUID::null;
    entry.item          = doc.file.empty() ? doc.ref.item : LLUUID::null;
    entry.file          = doc.file;
    entry.name          = doc.name;
    entry.objectName    = doc.objectName;
    entry.region        = doc.regionName;
    entry.lua           = doc.language.lua;
    entry.notecard      = doc.notecard;
    entry.wrapped       = doc.envelope.has_value();
    entry.compileTarget = doc.language.compileTarget;
    entry.baseAsset     = doc.assetId;
    entry.text          = doc.editor->text();
    // The steps that led here, to be taken back next time too, and where
    // the caret stood.
    entry.history     = doc.editor->undoJournal().asLLSD(512 * 1024);
    entry.caretLine   = doc.editor->caret().line;
    entry.caretColumn = doc.editor->caret().column;
    if (doc.notecard && doc.file.empty())
    {
        entry.embedded = ALScriptNotecardTab::asLLSD(doc.items ? doc.items->items() : ALScriptNotecardTab::items_t());
    }
    return entry;
}

bool ALFloaterScriptStudio::keepForRecovery(Doc& doc, ALScriptRecoveryEntry::State state)
{
    doc.recoveryDue              = 0.0;
    ALScriptRecoveryStore* store = recoveryStore();
    if (!store)
    {
        // Nowhere to keep it, which matters only where there is something
        // unsaved to keep.
        return !(doc.loaded && doc.modifiable && doc.editor->isDirty());
    }
    // Nothing to keep of a tab still loading, one that may not be changed,
    // or one whose kept text has not been put in yet.
    if (doc.recoveryKey.empty() || !doc.loaded || !doc.modifiable || doc.carriedText)
    {
        return true;
    }
    if (!doc.editor->isDirty())
    {
        // Saved, or never changed: nothing of this session's to keep, and
        // nothing of another's once it was taken in.
        store->forget(doc.recoveryKey);
    }
    else
    {
        ALScriptRecoveryEntry entry = recoveryEntryOf(doc);
        entry.state                 = state;
        if (!store->write(entry))
        {
            // Said once, not at every pause in typing.
            if (!doc.recoveryFailed)
            {
                doc.recoveryFailed = true;
                LLStringUtil::format_map_t args;
                args["[NAME]"] = doc.name;
                report(getString("RecoveryWriteFailed", args), true, &doc);
            }
            return false;
        }
        doc.recoveryFailed = false;
    }
    // What this tab took up is its own to keep from here.
    if (doc.recovering)
    {
        store->remove(*doc.recovering);
        doc.recovering.reset();
    }
    return true;
}

void ALFloaterScriptStudio::keepForRecoverySoon(Doc& doc)
{
    ALScriptRecoveryStore* store = recoveryStore();
    // What the last of these could not write, said once, as a write here
    // says it.
    if (store)
    {
        for (const std::string& key : store->takeFailures())
        {
            for (std::unique_ptr<Doc>& each : mDocs)
            {
                if (each->recoveryKey == key && !each->recoveryFailed)
                {
                    each->recoveryFailed = true;
                    LLStringUtil::format_map_t args;
                    args["[NAME]"] = each->name;
                    report(getString("RecoveryWriteFailed", args), true, each.get());
                }
            }
        }
    }
    // Anything but an unsaved text to write -- nothing to keep, an entry
    // to let go of once it is written -- as it always is.
    if (!store || doc.recoveryKey.empty() || !doc.loaded || !doc.modifiable || doc.carriedText || !doc.editor->isDirty() || doc.recovering)
    {
        keepForRecovery(doc);
        return;
    }
    doc.recoveryDue             = 0.0;
    ALScriptRecoveryEntry entry = recoveryEntryOf(doc);
    entry.state                 = ALScriptRecoveryEntry::State::Unsaved;
    store->writeSoon(std::move(entry));
}

bool ALFloaterScriptStudio::setAside(Doc& doc)
{
    ALScriptRecoveryStore* store = recoveryStore();
    if (!store || doc.recoveryKey.empty() || !store->setAside(recoveryEntryOf(doc)))
    {
        return false;
    }
    store->forget(doc.recoveryKey);
    return true;
}

void ALFloaterScriptStudio::scheduleRecovery(Doc& doc)
{
    if (doc.recoveryKey.empty())
    {
        return;
    }
    if (!doc.editor->isDirty())
    {
        keepForRecovery(doc);
        return;
    }
    // A moment after the first change since it was last written: typing
    // on writes it that often, not only once the typing stops.
    if (doc.recoveryDue <= 0.0)
    {
        doc.recoveryDue = LLTimer::getTotalSeconds() + RECOVERY_DELAY;
    }
}

void ALFloaterScriptStudio::takeUpEntry(Doc& doc, const ALScriptRecoveryEntry& entry)
{
    // The kept text put in over what is there, as one step to undo; its
    // entry let go of once this tab's own is written. A notecard's items
    // come with it, since its text says them by their places.
    if (doc.recoverable && doc.recoverable->path == entry.path)
    {
        doc.recoverable.reset();
    }
    using Failure = ALScriptWorkspace::Loaded::Failure;
    if (doc.loadFailure != Failure::None || (doc.loaded && !doc.modifiable))
    {
        // Nothing it could be saved over: the script could not be loaded,
        // or may no longer be changed. The tab holds the text on its own,
        // to copy or export, rather than waiting on a load that is not
        // coming or holding it where nothing can be done with it.
        if (doc.loadFailure == Failure::None)
        {
            doc.loadFailure = Failure::NotPermitted;
        }
        const bool locked = doc.loadFailure == Failure::NotPermitted;
        doc.carriedText.reset();
        doc.carriedEmbedded.reset();
        becomeOrphan(doc, entry, failedAs(doc, doc.loadFailure));
        if (locked)
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"] = doc.name;
            report(getString("OrphanLockedKept", args), true, &doc, { "copy", "export" });
        }
        return;
    }
    doc.recovering  = entry;
    doc.carriedText = entry.text;
    if (entry.notecard && doc.file.empty())
    {
        doc.carriedEmbedded = ALScriptNotecardTab::fromLLSD(entry.embedded);
    }
    if (!doc.loaded)
    {
        // Put in once it has loaded.
        return;
    }
    if (doc.carriedEmbedded && doc.items)
    {
        doc.items->take(std::move(*doc.carriedEmbedded));
    }
    doc.carriedEmbedded.reset();
    takeCarriedText(doc);
    // After, since putting the text in opens the editor to take it.
    doc.editor->setReadOnly(!doc.modifiable);
    if (doc.items)
    {
        doc.items->place();
    }
    keepForRecovery(doc);
    refreshNotice();
}

bool ALFloaterScriptStudio::restoreHistory(Doc& doc, const ALScriptRecoveryEntry& entry)
{
    // The kept tab as it was: its text, the steps that led to it to take
    // back and forward, and its caret -- as an editor's history outlives
    // its window. Its saved mark holds where the text it stands at is the
    // one the item holds now, as far as this tab knows; otherwise nothing
    // the history reaches was saved.
    //
    // Only over a tab that holds nothing of its own: the history takes the
    // place of the tab's, and what was typed there would go with it.
    if (!entry.history.isMap() || doc.editor->isDirty())
    {
        return false;
    }
    const std::optional<std::string> standing = doc.orphan == Doc::Orphan::None ? doc.editor->undoJournal().savedText() : std::nullopt;
    if (!doc.editor->setTextWithHistory(entry.text, entry.history))
    {
        // Not the history of this text: the tab is as it was.
        return false;
    }
    const std::optional<std::string> saved   = doc.editor->undoJournal().savedText();
    const bool                       trusted = standing && saved && *saved == *standing;
    if (!trusted)
    {
        doc.editor->markUnsaved();
    }
    // The gutter's bars on the lines that differ from what the item holds,
    // where that is known; every line, where it is not.
    if (standing)
    {
        doc.editor->barChangesSince(*standing);
    }
    if (entry.caretLine >= 0)
    {
        doc.editor->goTo(doc.editor->document().clamp(ALTextPos(entry.caretLine, entry.caretColumn)));
    }
    if (doc.items)
    {
        // The text came in whole, which takes the items' buttons with it.
        doc.items->place();
    }
    fillTabs();
    refreshToolbar();
    scheduleRecovery(doc);
    return true;
}

void ALFloaterScriptStudio::recoverEntry(const ALScriptRecoveryEntry& entry)
{
    // Open in another window: put in there, since two tabs of one script
    // would each save over the other.
    if (ALFloaterScriptStudio* holder = holderOf(ALScriptRef(entry.object, entry.item), entry.file); holder && holder != this)
    {
        holder->openFloater(holder->getKey());
        holder->setFocus(true);
        holder->recoverEntry(entry);
        return;
    }
    // A file: opened where it is and the kept text put over it; where it is
    // gone, a tab of its own that writes it again when saved.
    if (!entry.file.empty())
    {
        size_t index = indexOf("disk:" + entry.file);
        if (index == NONE && LLFile::isfile(entry.file))
        {
            openFile(entry.file, entry.lua);
            index = indexOf("disk:" + entry.file);
        }
        if (index != NONE)
        {
            activate(index);
            takeUpEntry(*mDocs[index], entry);
        }
        else
        {
            openOrphan(entry, Doc::Orphan::FileGone);
        }
        return;
    }
    // A script or a notecard: its tab, opened where it is not and it can
    // be had -- the inventory's item there, the object in sight -- with the
    // kept text put in as it loads; a tab of its own otherwise.
    const ALScriptRef ref(entry.object, entry.item);
    size_t            index = indexOf(ref);
    if (index == NONE)
    {
        LLViewerObject* object  = ref.inInventory() ? nullptr : gObjectList.findObject(ref.object);
        const bool      present = ref.inInventory() ? gInventory.getItem(ref.item) != nullptr : object && !object->isDead();
        if (!present)
        {
            openOrphan(entry, ref.inInventory() ? Doc::Orphan::Removed : Doc::Orphan::Away);
            return;
        }
        openScript(ref, entry.name);
        index = indexOf(ref);
        if (index == NONE)
        {
            return;
        }
    }
    activate(index);
    takeUpEntry(*mDocs[index], entry);
}

void ALFloaterScriptStudio::showRecovery()
{
    ALScriptRecoveryStore* store = recoveryStore();
    if (!store)
    {
        return;
    }
    // What earlier sessions left, unsaved or kept, and what was discarded
    // lately, this session's too; not this session's own unsaved, which
    // are its tabs.
    std::vector<ALScriptRecoveryEntry> offered;
    for (ALScriptRecoveryEntry& entry : store->list())
    {
        if (entry.state == ALScriptRecoveryEntry::State::Discarded || entry.session != store->session())
        {
            offered.push_back(std::move(entry));
        }
    }
    if (offered.empty())
    {
        setStatus(getString("RecoverNone"));
        return;
    }
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = 0; i < offered.size(); ++i)
    {
        const ALScriptRecoveryEntry& entry = offered[i];
        const char* state = entry.state == ALScriptRecoveryEntry::State::Kept        ? "RecoverKept"
                            : entry.state == ALScriptRecoveryEntry::State::Discarded ? "RecoverDiscarded"
                                                                                     : "RecoverUnsaved";
        ALQuickOpen::Candidate one;
        one.label  = entry.name.empty() ? gDirUtilp->getBaseFileName(entry.file) : entry.name;
        one.detail = getString(state) + ", " + entry.whenSaid();
        one.also   = !entry.file.empty() ? entry.file : entry.objectName + " " + entry.region;
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    const LLHandle<LLFloater> handle = getHandle();
    quickOpen(
        std::move(candidates), getString("RecoverPlaceholder"), getString("RecoverTitle"),
        [handle, offered](const std::string& value) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            const size_t           index  = static_cast<size_t>(atoi(value.c_str()));
            if (studio && index < offered.size())
            {
                studio->recoverEntry(offered[index]);
            }
        },
        mEditorHost, 0, 0, {},
        [handle, offered](const std::string& value) {
            // Shift-Return: discarded, or, discarded already, gone for good.
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            ALScriptRecoveryStore* store  = recoveryStore();
            const size_t           index  = static_cast<size_t>(atoi(value.c_str()));
            if (!studio || !store || index >= offered.size())
            {
                return;
            }
            const ALScriptRecoveryEntry& entry = offered[index];
            LLStringUtil::format_map_t   args;
            args["[NAME]"] = entry.name;
            if (entry.state == ALScriptRecoveryEntry::State::Discarded)
            {
                store->remove(entry);
                studio->setStatus(studio->getString("RecoveryGone", args));
            }
            else
            {
                store->discard(entry);
                studio->setStatus(studio->getString("RecoveryDiscarded", args));
            }
            // Its tab, if one offers it, offers it no longer.
            for (std::unique_ptr<Doc>& doc : studio->mDocs)
            {
                if (doc->recoverable && doc->recoverable->path == entry.path)
                {
                    doc->recoverable.reset();
                }
            }
            studio->refreshNotice();
        });
}

// static
void ALFloaterScriptStudio::offerRecovery()
{
    // What a session that ended before saving left: offered once the
    // world is in, to open now, later, or not at all. What was kept on
    // purpose at a quit opens with the studio, and is not asked about.
    ALScriptRecoveryStore* store = recoveryStore();
    if (!store)
    {
        return;
    }
    std::vector<ALScriptRecoveryEntry> unsaved;
    for (ALScriptRecoveryEntry& entry : store->left())
    {
        if (entry.state == ALScriptRecoveryEntry::State::Unsaved)
        {
            unsaved.push_back(std::move(entry));
        }
    }
    if (unsaved.empty())
    {
        return;
    }
    std::string names;
    for (size_t i = 0; i < unsaved.size() && i < 5; ++i)
    {
        names += (names.empty() ? "" : ", ") + (unsaved[i].name.empty() ? gDirUtilp->getBaseFileName(unsaved[i].file) : unsaved[i].name);
    }
    if (unsaved.size() > 5)
    {
        names += ", ...";
    }
    LLSD args;
    args["COUNT"] = static_cast<S32>(unsaved.size());
    args["NAMES"] = names;
    LLNotificationsUtil::add(unsaved.size() == 1 ? "ScriptStudioRecoveredOne" : "ScriptStudioRecovered", args, LLSD(), [](const LLSD& notification, const LLSD& response) {
        const S32              option = LLNotificationsUtil::getSelectedOption(notification, response);
        ALScriptRecoveryStore* store  = recoveryStore();
        if (!store || option == 1)
        {
            return;
        }
        // As they are now, not as they were when asked.
        std::vector<ALScriptRecoveryEntry> now;
        for (ALScriptRecoveryEntry& entry : store->left())
        {
            if (entry.state == ALScriptRecoveryEntry::State::Unsaved)
            {
                now.push_back(std::move(entry));
            }
        }
        if (option == 2)
        {
            for (const ALScriptRecoveryEntry& entry : now)
            {
                store->discard(entry);
            }
            return;
        }
        ALFloaterScriptStudio* studio = LLFloaterReg::showTypedInstance<ALFloaterScriptStudio>("script_studio", LLSD(), TAKE_FOCUS_YES);
        if (!studio)
        {
            return;
        }
        for (const ALScriptRecoveryEntry& entry : now)
        {
            studio->recoverEntry(entry);
        }
    });
}
