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
#include "alscriptenvelope.h"
#include "alscriptmodules.h"
#include "alscriptworkspace.h"
#include "aluploadheader.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

namespace
{
    // The target a notecard's text is hashed under.
    constexpr const char* NOTECARD = "notecard";

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
}

// static
void ALScriptMasterUpload::start(const ALMasterLink& link, ALMasterPlan::Send kind)
{
    std::make_shared<ALScriptMasterUpload>(link, kind)->read();
}

// static
void ALScriptMasterUpload::probe(const ALMasterLink& link, probed_t done, bool world)
{
    std::shared_ptr<ALScriptMasterUpload> probing = std::make_shared<ALScriptMasterUpload>(link, ALMasterPlan::Send::Derived);
    probing->mProbed                              = std::move(done);
    probing->mProbeWorld                          = world;
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
    mStamp = ALFileStamp::of(mLink.master);
    if (!mStamp.exists)
    {
        mUpdated.state = ALMasterLink::State::Suspended;
        mChanged       = true;
        end(Outcome::What::Suspended, LLTrans::getString("ScriptMasterGone"));
        return;
    }
    if (!ALFileRead::whole(mLink.master, mText, ALDiskIncludes::MAX_BYTES))
    {
        end(Outcome::What::Failed, LLTrans::getString("ScriptMasterUnreadable"));
        return;
    }
    if (mRef.inInventory())
    {
        found(ALScriptDiskMasters::itemOf(mRef));
        return;
    }
    // An object keeps its copy of what it holds until it is selected or
    // asked, and hears nothing of a co-owner's save: the region asked.
    if (!gObjectList.findObject(mRef.object))
    {
        mUpdated.state = ALMasterLink::State::Pending;
        mChanged       = true;
        end(Outcome::What::Pending, LLTrans::getString("ScriptMasterOutOfReach"));
        return;
    }
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    ALScriptWorkspace::instance().listContents(
        mRef.object,
        [self](const ALScriptContents& contents) {
            self->found(contents.fetched ? ALScriptDiskMasters::itemOf(self->mRef) : nullptr);
        },
        true);
}

void ALScriptMasterUpload::found(LLInventoryItem* item)
{
    if (!item)
    {
        // Gone from where it was; or, an object's, out of reach again.
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
    // is what it is asked. One that is not reads it never. A notecard's is
    // read always, for the items it may have come to carry.
    const bool read = mProbed ? mProbeWorld && asset.notNull() : mWorldMoved || (mLink.notecard && asset.notNull());
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
        found.worldMoved   = mWorldMoved;
        found.preprocessed = mPrepared.errors;
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
    switch (ALMasterPlan::decide(mKind, mWorldMoved, mWorldSame, unchanged, skip_unchanged))
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
            // What the world had, in History before it is gone over, under
            // the item, as every save keeps what it sends.
            if (!mWorldText.empty())
            {
                ALScriptSaved theirs;
                theirs.ref    = mRef;
                theirs.kind   = ALScriptKind::Script;
                theirs.text   = mWorldText;
                theirs.asset  = mAsset;
                theirs.sender = ALScriptSender(ALScriptOrigin::Disk);
                ALScriptWorkspace::instance().keepInHistory(theirs);
                mKeptTheirs = true;
            }
            upload();
            return;
        case ALMasterPlan::Do::Send:
            upload();
            return;
    }
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
    mUpdated.uses.clear();
    if (mPrepared.map)
    {
        const std::vector<ALSourceMap::File>& files = mPrepared.map->files();
        for (size_t i = 1; i < files.size(); ++i)
        {
            if (files[i].path.rfind("disk:", 0) == 0)
            {
                mUpdated.uses.push_back(ALScriptModules::identity(files[i].path));
            }
        }
    }
    mChanged = true;
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
