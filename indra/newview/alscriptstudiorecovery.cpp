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

#include "alscriptstudiorecovery.h"

#include "alcodeeditor.h"
#include "alscriptnotecardtab.h"
#include "alscriptstudioservices.h"
#include "lldir.h"
#include "llfile.h"
#include "llnotificationsutil.h"
#include "lltimer.h"

#include <algorithm>
#include <memory>

namespace
{
    // How long after the first change since it was last written a tab's
    // unsaved text is written again, whatever is typed meanwhile; and how
    // long discarded text is kept before it goes for good.
    const F64 RECOVERY_DELAY = 1.5;
    const F64 DISCARDED_KEPT = 7.0 * 24.0 * 60.0 * 60.0;

    // A test's store, in place of the account's.
    ALScriptRecoveryStore* sGivenStore = nullptr;

    // The account's store, one for the process, made again where another
    // account has logged in since; let go of in the viewer's cleanup --
    // what it has waiting written first -- before the writer it writes on
    // stops.
    class AccountRecovery final : public LLSingleton<AccountRecovery>
    {
        LLSINGLETON(AccountRecovery);
        void cleanupSingleton() override { mStore.reset(); }

    public:
        ALScriptRecoveryStore* storeFor(const std::string& directory)
        {
            if (!mStore || mMadeFor != directory)
            {
                LLFile::mkdir(directory);
                mStore   = std::make_unique<ALScriptRecoveryStore>(directory, mSession);
                mMadeFor = directory;
                // What was discarded long ago goes for good.
                mStore->prune(DISCARDED_KEPT);
            }
            return mStore.get();
        }

    private:
        std::unique_ptr<ALScriptRecoveryStore> mStore;
        std::string                            mMadeFor;
        const std::string                      mSession = LLUUID::generateNewID().asString();
    };

    AccountRecovery::AccountRecovery()
    {
        // Asked for here, so that it is let go of after this.
        ALScriptRecoveryWriter::getInstance();
    }

    // What an entry is called in a list: its name, or its file's.
    std::string nameOf(const ALScriptRecoveryEntry& entry)
    {
        return entry.name.empty() ? gDirUtilp->getBaseFileName(entry.file) : entry.name;
    }

    // Whether a tab's text and history stand as they were last written,
    // as what it is asked to be written as: durably, where that is asked.
    bool writtenAlready(const ALScriptStudioDoc& doc, ALScriptRecoveryEntry::State state, bool durable)
    {
        const ALScriptStudioDoc::RecoveryWritten& was = doc.recoveryWritten;
        return was.valid && was.state == state && (was.durable || !durable) && was.text == doc.editor->document().version() &&
               was.history == doc.editor->undoJournal().revision();
    }

    void markWritten(ALScriptStudioDoc& doc, ALScriptRecoveryEntry::State state, bool durable)
    {
        ALScriptStudioDoc::RecoveryWritten& was = doc.recoveryWritten;
        was.valid                               = true;
        was.text                                = doc.editor->document().version();
        was.history                             = doc.editor->undoJournal().revision();
        was.state                               = state;
        was.durable                             = durable;
    }
}

ALScriptStudioRecovery::ALScriptStudioRecovery(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window) {}

// --- the store -----------------------------------------------------------------------

// static
ALScriptRecoveryStore* ALScriptStudioRecovery::store()
{
    if (sGivenStore)
    {
        return sGivenStore;
    }
    if (!gDirUtilp || gDirUtilp->getLindenUserDir().empty() || AccountRecovery::wasDeleted())
    {
        return nullptr;
    }
    return AccountRecovery::instance().storeFor(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "script_studio_recovery"));
}

// static
void ALScriptStudioRecovery::useStore(ALScriptRecoveryStore* store)
{
    sGivenStore = store;
}

// static
ALScriptStudioRecovery::Entry ALScriptStudioRecovery::entryOf(const Doc& doc)
{
    Entry entry;
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
    // The steps that led here, to be taken back next time too, as the
    // journal writes them -- from what each step was written as the last
    // time -- and where the caret stood.
    entry.historyWritten = doc.editor->undoJournal().asNotation();
    entry.caretLine   = doc.editor->caret().line;
    entry.caretColumn = doc.editor->caret().column;
    if (doc.notecard && doc.file.empty())
    {
        entry.embedded = ALScriptNotecardTab::asLLSD(doc.items ? doc.items->items() : ALScriptNotecardTab::items_t());
    }
    return entry;
}

// --- a tab's text kept -----------------------------------------------------------------

bool ALScriptStudioRecovery::keep(Doc& doc, Entry::State state)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    doc.recoveryDue             = 0.0;
    ALScriptRecoveryStore* kept = store();
    if (!kept)
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
        kept->forget(doc.recoveryKey);
        doc.recoveryWritten.valid = false;
    }
    else if (!writtenAlready(doc, state, /*durable*/ true))
    {
        Entry entry = entryOf(doc);
        entry.state = state;
        if (!kept->write(entry))
        {
            // Said once, not at every pause in typing.
            if (!doc.recoveryFailed)
            {
                doc.recoveryFailed = true;
                LLStringUtil::format_map_t args;
                args["[NAME]"] = doc.name;
                mServices.report(mServices.words("RecoveryWriteFailed", args), true, &doc);
            }
            doc.recoveryWritten.valid = false;
            return false;
        }
        doc.recoveryFailed = false;
        markWritten(doc, state, /*durable*/ true);
    }
    // What this tab took up is its own to keep from here.
    if (doc.recovering)
    {
        kept->remove(*doc.recovering);
        doc.recovering.reset();
    }
    return true;
}

bool ALScriptStudioRecovery::keepAll(const std::vector<Doc*>& docs, Entry::State state)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALScriptRecoveryStore* kept = store();
    // Each unsaved text handed to the writer, forced out to the disk
    // there, and waited on once for the lot; the rest as keep does them.
    std::vector<Doc*> written;
    bool              all = true;
    for (Doc* doc : docs)
    {
        if (!kept || doc->recoveryKey.empty() || !doc->loaded || !doc->modifiable || doc->carriedText || !doc->editor->isDirty())
        {
            all = keep(*doc, state) && all;
            continue;
        }
        Entry entry = entryOf(*doc);
        entry.state = state;
        kept->writeSoon(std::move(entry), /*durable*/ true);
        written.push_back(doc);
    }
    if (written.empty())
    {
        return all;
    }
    kept->flush();
    const std::vector<std::string> failed = kept->takeFailures();
    for (Doc* doc : written)
    {
        doc->recoveryDue = 0.0;
        if (std::find(failed.begin(), failed.end(), doc->recoveryKey) != failed.end())
        {
            all                        = false;
            doc->recoveryWritten.valid = false;
            if (!doc->recoveryFailed)
            {
                doc->recoveryFailed = true;
                LLStringUtil::format_map_t args;
                args["[NAME]"] = doc->name;
                mServices.report(mServices.words("RecoveryWriteFailed", args), true, doc);
            }
            continue;
        }
        doc->recoveryFailed = false;
        markWritten(*doc, state, /*durable*/ true);
        // What this tab took up is its own to keep from here.
        if (doc->recovering)
        {
            kept->remove(*doc->recovering);
            doc->recovering.reset();
        }
    }
    return all;
}

void ALScriptStudioRecovery::keepSoon(Doc& doc)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALScriptRecoveryStore* kept = store();
    // What the last of these could not write, said once, as a write here
    // says it.
    if (kept)
    {
        for (const std::string& key : kept->takeFailures())
        {
            for (Doc* each : mServices.openDocs())
            {
                if (each->recoveryKey == key)
                {
                    // Not written after all: written again next time.
                    each->recoveryWritten.valid = false;
                }
                if (each->recoveryKey == key && !each->recoveryFailed)
                {
                    each->recoveryFailed = true;
                    LLStringUtil::format_map_t args;
                    args["[NAME]"] = each->name;
                    mServices.report(mServices.words("RecoveryWriteFailed", args), true, each);
                }
            }
        }
    }
    // Anything but an unsaved text to write -- nothing to keep, an entry
    // to let go of once it is written -- as it always is.
    if (!kept || doc.recoveryKey.empty() || !doc.loaded || !doc.modifiable || doc.carriedText || !doc.editor->isDirty() || doc.recovering)
    {
        keep(doc);
        return;
    }
    doc.recoveryDue = 0.0;
    if (writtenAlready(doc, Entry::State::Unsaved, /*durable*/ false))
    {
        // Neither the text nor its history has moved since.
        return;
    }
    Entry entry = entryOf(doc);
    entry.state = Entry::State::Unsaved;
    kept->writeSoon(std::move(entry));
    markWritten(doc, Entry::State::Unsaved, /*durable*/ false);
}

bool ALScriptStudioRecovery::setAside(Doc& doc)
{
    ALScriptRecoveryStore* kept = store();
    if (!kept || doc.recoveryKey.empty() || !kept->setAside(entryOf(doc)))
    {
        return false;
    }
    kept->forget(doc.recoveryKey);
    doc.recoveryWritten.valid = false;
    return true;
}

void ALScriptStudioRecovery::schedule(Doc& doc)
{
    if (doc.recoveryKey.empty())
    {
        return;
    }
    if (!doc.editor->isDirty())
    {
        keep(doc);
        return;
    }
    if (doc.recoveryDue <= 0.0)
    {
        doc.recoveryDue = LLTimer::getTotalSeconds() + RECOVERY_DELAY;
    }
}

void ALScriptStudioRecovery::pump()
{
    const F64 now = LLTimer::getTotalSeconds();
    for (Doc* doc : mServices.openDocs())
    {
        if (doc->recoveryDue > 0.0 && now >= doc->recoveryDue)
        {
            keepSoon(*doc);
        }
    }
}

// --- a kept text taken up ----------------------------------------------------------------

void ALScriptStudioRecovery::offerFor(Doc& doc, bool held_elsewhere)
{
    ALScriptRecoveryStore* kept = store();
    if (!kept || doc.recoveryKey.empty())
    {
        return;
    }
    // Where another tab holds the script -- a tab on its way here from
    // another window -- this session's entry is that tab's, and stays.
    if (!held_elsewhere)
    {
        doc.recoverable = kept->reclaim(doc.recoveryKey);
    }
    if (!doc.recoverable)
    {
        doc.recoverable = kept->leftFor(doc.recoveryKey);
    }
}

bool ALScriptStudioRecovery::wholeOf(Entry& entry)
{
    // A listing reads what it shows; the text and what goes with it are
    // read as it is taken up.
    ALScriptRecoveryStore* kept = store();
    if (entry.whole || (kept && kept->load(entry)))
    {
        return true;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = nameOf(entry);
    mServices.report(mServices.words("RecoveryReadFailed", args), true);
    return false;
}

void ALScriptStudioRecovery::takeUp(Doc& doc, const Entry& listed)
{
    // The kept text put in over what is there, as one step to undo; its
    // entry let go of once this tab's own is written. A notecard's items
    // come with it, since its text says them by their places.
    Entry entry = listed;
    if (!wholeOf(entry))
    {
        return;
    }
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
        mWindow.becomeOrphan(doc, entry, mWindow.failedAs(doc, doc.loadFailure));
        if (locked)
        {
            LLStringUtil::format_map_t args;
            args["[NAME]"] = doc.name;
            mServices.report(mServices.words("OrphanLockedKept", args), true, &doc, { "copy", "export" });
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
    mWindow.takeCarriedText(doc);
    // After, since putting the text in opens the editor to take it.
    doc.editor->setReadOnly(!doc.modifiable);
    if (doc.items)
    {
        doc.items->place();
    }
    keep(doc);
    mWindow.refreshNotice();
}

bool ALScriptStudioRecovery::restoreHistory(Doc& doc, const Entry& entry)
{
    // The kept tab as it was: its text, the steps that led to it to take
    // back and forward, and its caret -- as an editor's history outlives
    // its window. Its saved mark holds where the text it stands at is the
    // one the item holds now, as far as this tab knows; otherwise nothing
    // the history reaches was saved.
    //
    // Only over a tab that holds nothing of its own: the history takes the
    // place of the tab's, and what was typed there would go with it.
    const LLSD history = entry.historyOf();
    if (!history.isMap() || doc.editor->isDirty())
    {
        return false;
    }
    const std::optional<std::string> standing = doc.orphan.kind == Doc::Orphan::None ? doc.editor->undoJournal().savedText() : std::nullopt;
    if (!doc.editor->setTextWithHistory(entry.text, history))
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
    mWindow.tabsChanged();
    schedule(doc);
    return true;
}

void ALScriptStudioRecovery::recover(const Entry& listed)
{
    Entry entry = listed;
    if (!wholeOf(entry))
    {
        return;
    }
    // Open in another window: put in there, since two tabs of one script
    // would each save over the other.
    if (mWindow.recoverElsewhere(entry))
    {
        return;
    }
    // A file: opened where it is and the kept text put over it; where it is
    // gone, a tab of its own that writes it again when saved.
    if (!entry.file.empty())
    {
        Doc* doc = mServices.findDoc("disk:" + entry.file);
        if (!doc && LLFile::isfile(entry.file))
        {
            doc = mWindow.openFileTab(entry.file, entry.lua);
        }
        if (doc)
        {
            mWindow.activate(*doc);
            takeUp(*doc, entry);
        }
        else
        {
            mWindow.openOrphan(entry, Doc::Orphan::FileGone);
        }
        return;
    }
    // A script or a notecard: its tab, opened where it is not and it can
    // be had -- the inventory's item there, the object in sight -- with the
    // kept text put in as it loads; a tab of its own otherwise.
    const ALScriptRef ref(entry.object, entry.item);
    Doc*              doc = mServices.findDoc(ref);
    if (!doc)
    {
        if (!mWindow.scriptInHand(ref))
        {
            mWindow.openOrphan(entry, ref.inInventory() ? Doc::Orphan::Removed : Doc::Orphan::Away);
            return;
        }
        mServices.openScript(ref, entry.name);
        doc = mServices.findDoc(ref);
        if (!doc)
        {
            return;
        }
    }
    mWindow.activate(*doc);
    takeUp(*doc, entry);
}

void ALScriptStudioRecovery::show()
{
    ALScriptRecoveryStore* kept = store();
    if (!kept)
    {
        return;
    }
    // What earlier sessions left, unsaved or kept, and what was discarded
    // lately, this session's too; not this session's own unsaved, which
    // are its tabs.
    std::vector<Entry> offered;
    for (Entry& entry : kept->list())
    {
        if (entry.state == Entry::State::Discarded || entry.session != kept->session())
        {
            offered.push_back(std::move(entry));
        }
    }
    if (offered.empty())
    {
        mServices.setStatus(mServices.words("RecoverNone"));
        return;
    }
    std::vector<ALQuickOpen::Candidate> candidates;
    for (size_t i = 0; i < offered.size(); ++i)
    {
        const Entry& entry = offered[i];
        const char*  state = entry.state == Entry::State::Kept        ? "RecoverKept"
                             : entry.state == Entry::State::Discarded ? "RecoverDiscarded"
                                                                      : "RecoverUnsaved";
        ALQuickOpen::Candidate one;
        one.label  = nameOf(entry);
        one.detail = mServices.words(state) + ", " + entry.whenSaid();
        one.also   = !entry.file.empty() ? entry.file : entry.objectName + " " + entry.region;
        one.value  = std::to_string(i);
        candidates.push_back(std::move(one));
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.pick(
        std::move(candidates), mServices.words("RecoverPlaceholder"), mServices.words("RecoverTitle"),
        [this, alive, offered](const std::string& value) {
            const size_t index = static_cast<size_t>(atoi(value.c_str()));
            if (alive.lock() && index < offered.size())
            {
                recover(offered[index]);
            }
        },
        [this, alive, offered](const std::string& value) {
            // Shift-Return: discarded, or, discarded already, gone for good.
            ALScriptRecoveryStore* kept  = store();
            const size_t           index = static_cast<size_t>(atoi(value.c_str()));
            if (!alive.lock() || !kept || index >= offered.size())
            {
                return;
            }
            const Entry&               entry = offered[index];
            LLStringUtil::format_map_t args;
            args["[NAME]"] = entry.name;
            if (entry.state == Entry::State::Discarded)
            {
                kept->remove(entry);
                mServices.setStatus(mServices.words("RecoveryGone", args));
            }
            else
            {
                kept->discard(entry);
                mServices.setStatus(mServices.words("RecoveryDiscarded", args));
            }
            // Its tab, if one offers it, offers it no longer.
            for (Doc* doc : mServices.openDocs())
            {
                if (doc->recoverable && doc->recoverable->path == entry.path)
                {
                    doc->recoverable.reset();
                }
            }
            mWindow.refreshNotice();
        });
}

// static
void ALScriptStudioRecovery::offer(std::function<ALScriptStudioRecovery*()> studio)
{
    ALScriptRecoveryStore* kept = store();
    if (!kept)
    {
        return;
    }
    std::vector<Entry> unsaved;
    for (Entry& entry : kept->left())
    {
        if (entry.state == Entry::State::Unsaved)
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
        names += (names.empty() ? "" : ", ") + nameOf(unsaved[i]);
    }
    if (unsaved.size() > 5)
    {
        names += ", ...";
    }
    // Offered now: left alone, they go a while after, as the discarded
    // do, rather than being kept for ever.
    kept->markOffered(unsaved);
    LLSD args;
    args["COUNT"] = static_cast<S32>(unsaved.size());
    args["NAMES"] = names;
    LLNotificationsUtil::add(unsaved.size() == 1 ? "ScriptStudioRecoveredOne" : "ScriptStudioRecovered", args, LLSD(),
                             [studio](const LLSD& notification, const LLSD& response) {
                                 const S32              option = LLNotificationsUtil::getSelectedOption(notification, response);
                                 ALScriptRecoveryStore* kept   = store();
                                 if (!kept || option == 1)
                                 {
                                     return;
                                 }
                                 // As they are now, not as they were when asked.
                                 std::vector<Entry> now;
                                 for (Entry& entry : kept->left())
                                 {
                                     if (entry.state == Entry::State::Unsaved)
                                     {
                                         now.push_back(std::move(entry));
                                     }
                                 }
                                 if (option == 2)
                                 {
                                     for (const Entry& entry : now)
                                     {
                                         kept->discard(entry);
                                     }
                                     return;
                                 }
                                 ALScriptStudioRecovery* recovery = studio ? studio() : nullptr;
                                 if (!recovery)
                                 {
                                     return;
                                 }
                                 for (const Entry& entry : now)
                                 {
                                     recovery->recover(entry);
                                 }
                             });
}
