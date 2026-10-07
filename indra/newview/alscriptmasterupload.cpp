/**
 * @file alscriptmasterupload.cpp
 * @brief One in-world script sent from the file on disk that is its master.
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

#include "alscriptmasterupload.h"

#include "alfilewrite.h"
#include "alrecovery.h"
#include "alrecoverystore.h"
#include "alsavehistory.h"
#include "alscriptenvelope.h"
#include "alscriptworkspace.h"
#include "alserialworker.h"
#include "alsourcemap.h"
#include "aluploadheader.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "llsingleton.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "workqueue.h"

namespace
{
    // The target a notecard's text is hashed under.
    constexpr const char* NOTECARD = "notecard";
    // How many of an item's newest saves in History a world that moved is
    // looked for among, as a text this viewer kept already.
    constexpr size_t      HISTORY_NEWEST = 10;

    // A master as one look at the disk found it: whether it is there, when
    // it was written, and its text, where it read whole.
    struct MasterRead
    {
        ALFileStamp stamp;
        std::string text;
        bool        whole = false;
    };

    // Safe on any thread.
    MasterRead readMaster(const std::string& path)
    {
        MasterRead read;
        read.stamp = ALFileStamp::of(path);
        read.whole = read.stamp.exists && ALFileRead::whole(path, read.text, ALDiskIncludes::MAX_BYTES);
        return read;
    }

    // The thread every send and probe reads its master on, one after
    // another: made with the first, closed as the viewer goes. A master may
    // be on a slow drive or a share far away, and as large as a file a
    // script may read, and the main thread waits on neither. Owned by no
    // send, so that what one reads finds it still there.
    class ALScriptMasterReads final : public LLSingleton<ALScriptMasterReads>
    {
        LLSINGLETON_EMPTY_CTOR(ALScriptMasterReads);
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
                mThread = std::make_unique<ALSerialWorker>("ScriptMasterReads");
            }
            return mThread->post(std::move(job));
        }

    private:
        std::unique_ptr<ALSerialWorker> mThread;
    };

    // What a text hashes to beside what a send would make of the master:
    // an envelope's halves, or the text where it is none.
    std::string hashOfText(const std::string& text, const std::string& target)
    {
        if (const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
        {
            return ALUploadHeader::hashOf(envelope->compileTarget.empty() ? target : envelope->compileTarget, envelope->source,
                                          envelope->expanded);
        }
        return ALUploadHeader::hashOfPlain(target, text);
    }

    // The files on disk an expansion read, the master's includes and
    // theirs, as a link keeps them (`disk:<path>`): whose change sends it
    // again. The map names each as the search found it, which is where it
    // stands once its links are followed -- a file is read only as the
    // folders admit it (ALDiskIncludes::admits), which follows them, and
    // its identity is made from that -- so nothing is asked of the disk
    // here to follow them again.
    std::vector<std::string> usesOf(const ALScriptPrepared& prepared)
    {
        std::vector<std::string> uses;
        if (prepared.map)
        {
            const std::vector<ALSourceMap::File>& files = prepared.map->files();
            for (size_t i = 1; i < files.size(); ++i)
            {
                if (files[i].path.rfind("disk:", 0) == 0)
                {
                    uses.push_back(files[i].path);
                }
            }
        }
        return uses;
    }
}

// static
void ALScriptMasterUpload::start(const ALMasterLink& link, ALMasterPlan::Send kind)
{
    std::make_shared<ALScriptMasterUpload>(link, kind)->read();
}

// static
void ALScriptMasterUpload::probe(const ALMasterLink& link, probed_t done, bool world, bool refetch)
{
    std::shared_ptr<ALScriptMasterUpload> probing = std::make_shared<ALScriptMasterUpload>(link, ALMasterPlan::Send::Derived);
    probing->mProbed                              = std::move(done);
    probing->mProbeWorld                          = world;
    probing->mRefetch                             = refetch;
    probing->read();
}

ALScriptMasterUpload::ALScriptMasterUpload(const ALMasterLink& link, ALMasterPlan::Send kind)
: mLink(link), mUpdated(link), mKind(kind), mRef(link.object, link.item)
{
}

std::string ALScriptMasterUpload::hashOf(const std::string& text) const
{
    return mLink.notecard ? ALUploadHeader::hashOfPlain(NOTECARD, text) : hashOfText(text, mTarget);
}

void ALScriptMasterUpload::read()
{
    // Looked at and read on the masters' thread; what it found handed back
    // to this send on the main thread, which alone goes on with it. With no
    // main loop to hand it back to -- a test -- or the thread closed as the
    // viewer goes, read here.
    if (const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop"))
    {
        std::shared_ptr<ALScriptMasterUpload> self   = shared_from_this();
        const std::string                     master = mLink.master;
        if (ALScriptMasterReads::instance().post([self, main_loop, master]() mutable {
                // Shared, not copied, on its way: the queue copies what it
                // is given, and the text may be large.
                const std::shared_ptr<MasterRead> read = std::make_shared<MasterRead>(readMaster(master));
                main_loop->post([self = std::move(self), read]() { self->masterRead(read->stamp, std::move(read->text), read->whole); });
            }))
        {
            return;
        }
    }
    MasterRead read = readMaster(mLink.master);
    masterRead(read.stamp, std::move(read.text), read.whole);
}

void ALScriptMasterUpload::masterRead(const ALFileStamp& stamp, std::string text, bool whole)
{
    mStamp = stamp;
    if (!mStamp.exists)
    {
        mUpdated.state = ALMasterLink::State::Suspended;
        mChanged       = true;
        end(Outcome::What::Suspended, LLTrans::getString("ScriptMasterGone"));
        return;
    }
    if (!whole)
    {
        end(Outcome::What::Failed, LLTrans::getString("ScriptMasterUnreadable"));
        return;
    }
    mText = std::move(text);
    find();
}

void ALScriptMasterUpload::find()
{
    if (mRef.inInventory())
    {
        found(ALScriptDiskMasters::itemOf(mRef));
        return;
    }
    // An object keeps its copy of what it holds until it is selected or
    // asked, and hears nothing of a co-owner's save: the region asked. A
    // probe told not to ask again -- of contents just fetched, for a whole
    // object's scripts -- takes the copy the object holds, where it holds
    // one it has not heard has changed: what is asked of the region then is
    // only what is not there.
    LLViewerObject* object = gObjectList.findObject(mRef.object);
    if (!object)
    {
        mUpdated.state = ALMasterLink::State::Pending;
        mChanged       = true;
        end(Outcome::What::Pending, LLTrans::getString("ScriptMasterOutOfReach"));
        return;
    }
    // What RLVa keeps from being seen or changed -- the object's contents,
    // as it keeps the build floater's -- is listed as holding nothing, and
    // would look gone: whether the item is there is not known, and nothing
    // is sent into it. The send waits to be made by hand, the link kept.
    if (!ALScriptWorkspace::rlvRefusal(object, LLAssetType::AT_NONE, ALScriptRlvUse::Change).empty())
    {
        mUpdated.state = ALMasterLink::State::Pending;
        mChanged       = true;
        end(Outcome::What::Pending, LLTrans::getString("ScriptMasterRlvHeld"));
        return;
    }
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    ALScriptWorkspace::instance().listContents(
        mRef.object,
        [self](const ALScriptContents& contents) {
            if (!contents.fetched)
            {
                self->unanswered();
                return;
            }
            self->found(ALScriptDiskMasters::itemOf(self->mRef));
        },
        /*from_region*/ mRefetch);
}

void ALScriptMasterUpload::unanswered()
{
    // No answer is not an answer that the item is gone: the region slow, or
    // the object gone out of sight before it said. Whether the item is
    // there is not known, and the send waits to be made by hand, as one to
    // an object out of reach does; only an answer that came without it
    // orphans the link.
    const bool in_sight = gObjectList.findObject(mRef.object) != nullptr;
    mUpdated.state      = ALMasterLink::State::Pending;
    mChanged            = true;
    end(Outcome::What::Pending, LLTrans::getString(in_sight ? "ScriptMasterNoContents" : "ScriptMasterOutOfReach"));
}

void ALScriptMasterUpload::found(LLInventoryItem* item)
{
    if (!item)
    {
        // Gone from where it was, the inventory or an object that said what
        // it holds; or, an object's, out of reach again.
        const bool gone = mRef.inInventory() || gObjectList.findObject(mRef.object);
        mUpdated.state  = gone ? ALMasterLink::State::Orphaned : ALMasterLink::State::Pending;
        if (gone)
        {
            mUpdated.orphanedSince = LLDate::now();
        }
        mChanged = true;
        end(gone ? Outcome::What::Orphaned : Outcome::What::Pending,
            LLTrans::getString(gone ? "ScriptMasterItemGone" : "ScriptMasterOutOfReach"));
        return;
    }
    mName  = item->getName();
    mAsset = item->getAssetUUID();
    if (mUpdated.itemName != mName)
    {
        mUpdated.itemName = mName;
        mChanged          = true;
    }
    // A notecard's master sends a notecard, and a script's a script.
    const bool notecard = item->getType() == LLAssetType::AT_NOTECARD;
    if (notecard != mLink.notecard)
    {
        end(Outcome::What::Failed, LLTrans::getString(mLink.notecard ? "ScriptMasterNotNotecard" : "ScriptMasterNotScript"));
        return;
    }
    if (notecard)
    {
        // Its text as the file has it: nothing expanded, nothing compiled.
        mTarget = NOTECARD;
        ALScriptPrepared as_is;
        as_is.text = mText;
        prepared(as_is);
        return;
    }
    // The target: the last one sent with, or what the script compiles for
    // now. A script is in one language, its master's, and only compiles
    // for that language's targets.
    const bool lua = item->getInventorySubType() == SST_LUA || item->getRuntime() == "luau";
    if (lua != mLink.lua)
    {
        end(Outcome::What::Failed, LLTrans::getString(mLink.lua ? "ScriptMasterNotSLua" : "ScriptMasterNotLSL"));
        return;
    }
    mTarget = mLink.target;
    if (mTarget.empty() || (mTarget == "luau") != lua)
    {
        mTarget = item->getRuntime();
        if (mTarget.empty() || (mTarget == "luau") != lua)
        {
            mTarget = lua ? "luau" : "mono";
        }
    }
    // Expanded as a file on disk is, its includes read from beside it, and
    // named in the upload header from the folder it is under.
    ALScriptWorkspace::From from;
    from.path = "disk:" + mLink.master;
    from.file = ALScriptDiskMasters::fileLabel(mLink.master, ALScriptDiskMasters::blessedFor(mLink.master, lua));
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    ALScriptWorkspace::instance().prepare(
        mRef, mName, mAsset, mText, lua, mTarget, [self](const ALScriptPrepared& prepared) { self->prepared(prepared); }, /*anyway*/ true,
        from);
}

void ALScriptMasterUpload::prepared(const ALScriptPrepared& prepared)
{
    mPrepared = prepared;
    mOurs     = hashOf(mPrepared.text);
    worldHas(mAsset);
}

void ALScriptMasterUpload::worldHas(const LLUUID& asset)
{
    // Moved in the world since the last send from the master: what it holds
    // read, to tell a real change from the same text gone up another way.
    mWorldMoved = asset.notNull() && mLink.base.notNull() && asset != mLink.base;
    // A probe asked of it reads it whatever the base: what the world holds
    // is what it is asked. One that is not reads it where it moved, and
    // where nothing was ever sent through the link: its base is what the
    // item held when it was linked, so nothing has moved, and yet the
    // first send goes over what the world has -- which may be a co-owner's
    // work, and is kept first where it is not what goes up. A notecard's is
    // read always, for the items it may have come to carry.
    const bool read = mProbed ? mProbeWorld && asset.notNull() : asset.notNull() && (mWorldMoved || firstSend() || mLink.notecard);
    if (!read)
    {
        decide();
        return;
    }
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    ALScriptWorkspace::instance().load(mRef, [self](const ALScriptLoaded& loaded) { self->worldText(loaded); });
}

void ALScriptMasterUpload::worldText(const ALScriptLoaded& loaded)
{
    if (loaded.error.empty())
    {
        mWorldText    = loaded.text;
        mWorldRead    = true;
        mWorldSame    = hashOf(mWorldText) == mOurs;
        mWorldCarries = loaded.notecard && !loaded.embedded.empty();
    }
    // Moved, to a text that is not what goes up: a change made in the
    // world, unless it is a text this viewer kept already as it went up --
    // a send of this link's whose moving the base on was lost, the index
    // written a moment after a send ends and a crash coming first.
    if (mWorldRead && mWorldMoved && !mWorldSame && !mWorldCarries)
    {
        askHistory();
        return;
    }
    decide();
}

void ALScriptMasterUpload::askHistory()
{
    const std::shared_ptr<ALSaveHistory> history = ALRecovery::history();
    if (!history)
    {
        decide();
        return;
    }
    // Looked for as History keeps a save: under the item, a script by its
    // envelope's source half, a notecard by its whole text.
    const std::string key  = ALRecoveryStore::keyOf(mRef.object, mRef.item, std::string());
    auto              text = std::make_shared<std::string>(mWorldText);
    if (!mLink.notecard)
    {
        if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(mWorldText))
        {
            *text = std::move(envelope->source);
        }
    }
    const LLUUID asset = mAsset;
    // Read on the thread History is written on, after every save it was
    // given to keep before this; what it found handed back to the main
    // thread. With no main loop to hand it back to -- a test -- read here;
    // with the thread stopped, as the viewer quits, not read at all.
    const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        historyHas(history->holds(key, asset, *text, HISTORY_NEWEST));
        return;
    }
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    if (!ALRecoveryWriter::instance().post([self, main_loop, history, key, asset, text]() mutable {
            const bool kept = history->holds(key, asset, *text, HISTORY_NEWEST);
            main_loop->post([self = std::move(self), kept]() { self->historyHas(kept); });
        }))
    {
        decide();
    }
}

void ALScriptMasterUpload::historyHas(bool kept)
{
    mWorldOurs = kept;
    decide();
}

void ALScriptMasterUpload::decide()
{
    static LLCachedControl<bool> skip_unchanged(gSavedSettings, "ALScriptMastersSkipUnchanged", true);
    const bool unchanged = !mLink.hash.empty() && mOurs == mLink.hash;
    // A notecard that came to carry items in the world is never sent over
    // from a file, which cannot hold them, whoever asks.
    if (mWorldCarries)
    {
        end(Outcome::What::Failed, LLTrans::getString("ScriptMasterNotecardCarries"));
        return;
    }
    if (mProbed)
    {
        Probe found;
        found.ref          = mRef;
        found.itemName     = mName;
        found.ours         = mOurs;
        found.unchanged    = unchanged;
        found.worldRead    = mWorldRead;
        found.worldSame    = mWorldSame;
        found.worldMoved   = mWorldMoved && !mWorldOurs;
        found.preprocessed = mPrepared.errors;
        found.stamp        = mStamp.time;
        found.uses         = usesOf(mPrepared);
        found.missed       = !mPrepared.errors.empty();
        mProbed(found);
        return;
    }
    // A send of the studio's own -- an include's users, Send from Files --
    // never puts up a script its preprocessor could not expand, an include
    // gone in a checkout say: what the world has is better than that. A
    // save of the master goes up all the same, as every save does, and says
    // what was found.
    if (mKind == ALMasterPlan::Send::Derived && !mPrepared.errors.empty())
    {
        end(Outcome::What::Failed, LLTrans::getString("ScriptMasterExpandFailed"));
        return;
    }
    // Moved to a text this viewer kept already: nothing of anybody else's
    // is there, and nothing would be lost. Not moved, for the plan; kept
    // already, so nothing kept again, nor said to be; and the link of what
    // the world holds now, as a skip leaves it.
    if (mWorldOurs)
    {
        mUpdated.base = mAsset;
        mChanged      = true;
    }
    ALMasterPlan::Do plan = ALMasterPlan::decide(mKind, mWorldMoved && !mWorldOurs, mWorldSame, unchanged, skip_unchanged);
    if (plan == ALMasterPlan::Do::Send && firstSend() && mAsset.notNull() && !mWorldSame && !mWorldOurs)
    {
        // The first send through a link goes over what the world held when
        // it was linked, which the plan sees as no move: it is kept first,
        // as what a move brought is. A send of the studio's own that could
        // not read it, to keep it, goes no further: a save of the master
        // goes up all the same, as every save does.
        if (!mWorldRead && mKind == ALMasterPlan::Send::Derived)
        {
            end(Outcome::What::Failed, LLTrans::getString("ScriptMasterWorldUnread"));
            return;
        }
        plan = ALMasterPlan::Do::SendKeepingTheirs;
    }
    switch (plan)
    {
        case ALMasterPlan::Do::Skip:
            // Nothing to send: where the world moved, it holds what the
            // master makes, and the link is of that now.
            if (mWorldMoved)
            {
                mUpdated.base = mAsset;
            }
            mUpdated.state = ALMasterLink::State::Active;
            mChanged       = true;
            end(Outcome::What::Skipped);
            return;
        case ALMasterPlan::Do::Hold:
            mUpdated.state = ALMasterLink::State::Differing;
            mChanged       = true;
            end(Outcome::What::Held);
            return;
        case ALMasterPlan::Do::SendKeepingTheirs:
            keepTheirs();
            upload();
            return;
        case ALMasterPlan::Do::Send:
            upload();
            return;
    }
}

void ALScriptMasterUpload::keepTheirs()
{
    // What the world had, in History before it is gone over, under the
    // item, as every save keeps what it sends: a notecard's as a notecard.
    if (mWorldText.empty())
    {
        return;
    }
    ALScriptSaved theirs;
    theirs.ref    = mRef;
    theirs.kind   = mLink.notecard ? ALScriptKind::Notecard : ALScriptKind::Script;
    theirs.text   = mWorldText;
    theirs.asset  = mAsset;
    theirs.sender = ALScriptSender(ALScriptOrigin::Disk);
    ALScriptWorkspace::instance().keepInHistory(theirs);
    mKeptTheirs = true;
}

void ALScriptMasterUpload::upload()
{
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    if (mLink.notecard)
    {
        // With no items: one that carries any is never sent from here.
        std::string error;
        if (!ALScriptWorkspace::instance().saveNotecard(
                mRef, mPrepared.text, {}, [self](const ALScriptCompileResult& result) { self->uploaded(result); }, error,
                ALScriptSender(ALScriptOrigin::Disk)))
        {
            end(Outcome::What::Failed, error);
        }
        return;
    }
    ALScriptSaveOptions options;
    options.compileTarget = mTarget;
    options.sender        = ALScriptSender(ALScriptOrigin::Disk);
    options.sourceMap     = mPrepared.map;
    options.codeLine      = mPrepared.codeLine;
    std::string error;
    if (!ALScriptWorkspace::instance().save(
            mRef, mPrepared.text, options, [self](const ALScriptCompileResult& result) { self->uploaded(result); }, error))
    {
        end(Outcome::What::Failed, error);
    }
}

void ALScriptMasterUpload::uploaded(const ALScriptCompileResult& result)
{
    if (!result.error.empty())
    {
        end(Outcome::What::Failed, result.error, result);
        return;
    }
    // Gone up, compiled or not: the link is of what it holds now. The
    // includes it read are what an include's save sends it again for.
    if (result.newAssetId.notNull())
    {
        mUpdated.base = result.newAssetId;
    }
    // Saved as another item, one that could not be changed in place: the
    // link is that item's now.
    if (result.newItemId.notNull())
    {
        mUpdated.item = result.newItemId;
    }
    mUpdated.hash   = mOurs;
    mUpdated.stamp  = mStamp.time;
    mUpdated.target = mTarget;
    mUpdated.state  = ALMasterLink::State::Active;
    // An include it could not find may be any file saved from here on.
    mUpdated.missed = !mPrepared.errors.empty();
    mUpdated.uses   = usesOf(mPrepared);
    mChanged        = true;
    end(Outcome::What::Sent, std::string(), result);
}

void ALScriptMasterUpload::end(Outcome::What what, const std::string& why, std::optional<ALScriptCompileResult> result)
{
    if (mProbed)
    {
        // A probe goes no further than finding why it could not go up.
        Probe found;
        found.ref      = mRef;
        found.what     = what;
        found.why      = why;
        found.itemName = mName.empty() ? mLink.itemName : mName;
        mProbed(found);
        return;
    }
    Outcome outcome;
    outcome.what         = what;
    outcome.ref          = mRef;
    outcome.master       = mLink.master;
    outcome.itemName     = mName.empty() ? mLink.itemName : mName;
    outcome.direct       = mKind == ALMasterPlan::Send::Direct;
    outcome.keptTheirs   = mKeptTheirs;
    outcome.result       = std::move(result);
    outcome.preprocessed = mPrepared.errors;
    outcome.why          = why;
    ALScriptDiskMasters::instance().finished(outcome, mChanged ? std::optional<ALMasterLink>(mUpdated) : std::nullopt);
}
