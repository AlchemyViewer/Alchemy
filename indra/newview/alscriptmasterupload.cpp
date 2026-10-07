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

ALScriptMasterUpload::ALScriptMasterUpload(const ALMasterLink& link, ALMasterPlan::Send kind)
: mLink(link), mUpdated(link), mKind(kind), mRef(link.object, link.item)
{
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
    mOurs     = hashOfText(mPrepared.text, mTarget);
    worldHas(mAsset);
}

void ALScriptMasterUpload::worldHas(const LLUUID& asset)
{
    // Moved in the world since the last send from the master: what it holds
    // read, to tell a real change from the same text gone up another way.
    mWorldMoved = asset.notNull() && mLink.base.notNull() && asset != mLink.base;
    if (!mWorldMoved)
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
        mWorldText = loaded.text;
        mWorldSame = hashOfText(mWorldText, mTarget) == mOurs;
    }
    decide();
}

void ALScriptMasterUpload::decide()
{
    static LLCachedControl<bool> skip_unchanged(gSavedSettings, "ALScriptMastersSkipUnchanged", true);
    const bool unchanged = !mLink.hash.empty() && mOurs == mLink.hash;
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
    ALScriptSaveOptions options;
    options.compileTarget = mTarget;
    options.sender        = ALScriptSender(ALScriptOrigin::Disk);
    options.sourceMap     = mPrepared.map;
    options.codeLine      = mPrepared.codeLine;
    std::shared_ptr<ALScriptMasterUpload> self = shared_from_this();
    std::string                            error;
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
    mUpdated.hash   = mOurs;
    mUpdated.stamp  = mStamp.time;
    mUpdated.target = mTarget;
    mUpdated.state  = ALMasterLink::State::Active;
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
