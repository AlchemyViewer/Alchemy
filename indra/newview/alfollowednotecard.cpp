/**
 * @file alfollowednotecard.cpp
 * @brief A notecard in inventory followed for its text, kept on disk while its asset is unchanged.
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

#include "alfollowednotecard.h"

#include "alscriptworkspace.h"
#include "llfile.h"
#include "llinventorymodel.h"
#include "llsdserialize.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"

ALFollowedNotecard::ALFollowedNotecard(std::string setting, std::string cache_file, std::string not_notecard)
    : mSetting(std::move(setting)), mCacheFile(std::move(cache_file)), mNotNotecard(std::move(not_notecard))
{
    if (LLControlVariable* control = gSavedPerAccountSettings.getControl(mSetting))
    {
        mSettingChanged = control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) {
            mFailed.setNull();
            mMoving = true;
            refresh();
            mMoving = false;
            mChanged(true);
        });
    }
    refresh();
}

ALFollowedNotecard::~ALFollowedNotecard() = default;

LLUUID ALFollowedNotecard::item() const
{
    const LLControlVariable* control = gSavedPerAccountSettings.getControl(mSetting);
    return control ? LLUUID(control->getValue().asString()) : LLUUID::null;
}

std::string ALFollowedNotecard::name() const
{
    const LLUUID followed = item();
    if (followed.isNull())
    {
        return std::string();
    }
    if (const LLViewerInventoryItem* held = gInventory.getItem(followed))
    {
        return held->getName();
    }
    return mCacheItem == followed ? mCacheName : std::string();
}

void ALFollowedNotecard::use(const LLUUID& followed)
{
    gSavedPerAccountSettings.setString(mSetting, followed.isNull() ? std::string() : followed.asString());
}

void ALFollowedNotecard::check()
{
    const LLUUID followed = item();
    if (followed.isNull())
    {
        return;
    }
    const LLViewerInventoryItem* held = gInventory.getItem(followed);
    if (held && (mCacheItem != followed || held->getAssetUUID() != mCacheAsset) && held->getAssetUUID() != mFetching &&
        held->getAssetUUID() != mFailed)
    {
        refresh();
    }
}

void ALFollowedNotecard::refresh()
{
    const LLUUID followed = item();
    if (followed.isNull())
    {
        take(std::string(), std::string());
        return;
    }
    loadCache();
    const bool                   kept = mCacheItem == followed;
    const LLViewerInventoryItem* held = gInventory.getItem(followed);
    // The copy while it is this notecard's: the notecard as it stands
    // where the item's asset is the one kept, or the last known while
    // inventory has not come as far as the item; standing in, where the
    // asset is another, until that comes.
    take(kept ? mCacheText : std::string(), std::string());
    if (held && (!kept || held->getAssetUUID() != mCacheAsset))
    {
        fetch(followed);
    }
}

void ALFollowedNotecard::fetch(const LLUUID& followed)
{
    const LLViewerInventoryItem* held = gInventory.getItem(followed);
    if (!held || held->getAssetUUID() == mFetching)
    {
        return;
    }
    mFetching                 = held->getAssetUUID();
    const LLUUID        asset = mFetching;
    std::weak_ptr<bool> alive = mAlive;
    ALScriptWorkspace::getInstance()->load(ALScriptRef(LLUUID::null, followed), [this, alive, followed, asset](const ALScriptLoaded& loaded) {
        if (alive.expired())
        {
            return;
        }
        mFetching.setNull();
        if (item() != followed)
        {
            // Another since.
            return;
        }
        if (!loaded.error.empty() || !loaded.notecard)
        {
            // What was there stays; why it is not the notecard is said, and
            // the asset is not asked for again.
            mFailed = asset;
            take(mText, loaded.error.empty() ? LLTrans::getString(mNotNotecard) : loaded.error);
            return;
        }
        mCacheItem  = followed;
        mCacheAsset = loaded.assetId;
        mCacheName  = loaded.name;
        mCacheText  = loaded.text;
        saveCache();
        take(loaded.text, std::string());
    });
}

std::string ALFollowedNotecard::cachePath() const
{
    return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, mCacheFile);
}

void ALFollowedNotecard::loadCache()
{
    if (mCacheLoaded)
    {
        return;
    }
    mCacheLoaded = true;
    llifstream in(cachePath());
    LLSD       kept;
    if (!in.is_open() || LLSDSerialize::fromXML(kept, in) <= 0 || !kept.isMap())
    {
        return;
    }
    mCacheItem  = kept["item"].asUUID();
    mCacheAsset = kept["asset"].asUUID();
    mCacheName  = kept["name"].asString();
    mCacheText  = kept["text"].asString();
}

void ALFollowedNotecard::saveCache()
{
    LLSD kept;
    kept["item"]  = mCacheItem;
    kept["asset"] = mCacheAsset;
    kept["name"]  = mCacheName;
    kept["text"]  = mCacheText;
    llofstream out(cachePath());
    if (out.is_open())
    {
        LLSDSerialize::toPrettyXML(kept, out);
    }
}

void ALFollowedNotecard::take(const std::string& text, const std::string& error)
{
    if (text == mText && error == mError)
    {
        return;
    }
    mText  = text;
    mError = error;
    if (!mMoving)
    {
        mChanged(false);
    }
}
