/**
 * @file alscriptstudiovimrc.cpp
 * @brief The vimrc Script Studio's vim reads: a file in the settings folder, or a notecard kept on disk while its asset is unchanged.
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

#include "alscriptstudiovimrc.h"

#include "alfilewrite.h"
#include "alscriptworkspace.h"
#include "alwatchedfile.h"
#include "llfile.h"
#include "llinventorymodel.h"
#include "llsdserialize.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"

namespace
{
    // More than any vimrc: a file this size is not read.
    constexpr S64 MOST_VIMRC_BYTES = 256 * 1024;
}

ALScriptStudioVimrc::~ALScriptStudioVimrc() = default;

ALScriptStudioVimrc::ALScriptStudioVimrc()
{
    if (LLControlVariable* control = gSavedPerAccountSettings.getControl(SETTING))
    {
        mSettingChanged = control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) {
            refresh();
            // Where it comes from changed, whatever the text.
            mChanged();
        });
    }
    refresh();
}

// static
std::string ALScriptStudioVimrc::filePath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "vimrc");
}

// static
std::string ALScriptStudioVimrc::cachePath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "script_studio_vimrc.xml");
}

LLUUID ALScriptStudioVimrc::notecard() const
{
    const LLControlVariable* control = gSavedPerAccountSettings.getControl(SETTING);
    return control ? LLUUID(control->getValue().asString()) : LLUUID::null;
}

std::string ALScriptStudioVimrc::notecardName() const
{
    const LLUUID item = notecard();
    if (item.isNull())
    {
        return std::string();
    }
    if (const LLViewerInventoryItem* held = gInventory.getItem(item))
    {
        return held->getName();
    }
    return mCacheItem == item ? mCacheName : std::string();
}

void ALScriptStudioVimrc::useNotecard(const LLUUID& item)
{
    gSavedPerAccountSettings.setString(SETTING, item.isNull() ? std::string() : item.asString());
}

void ALScriptStudioVimrc::check(bool now)
{
    if (!now && mSinceCheck.getElapsedTimeF32() < 1.f)
    {
        return;
    }
    mSinceCheck.reset();
    const LLUUID item = notecard();
    if (item.isNull())
    {
        // Watched, and read as it changes; read now where asked, or where
        // it has yet to be watched.
        if (now || !mWatch)
        {
            readFile();
        }
        return;
    }
    const LLViewerInventoryItem* held = gInventory.getItem(item);
    if (held && (mCacheItem != item || held->getAssetUUID() != mCacheAsset) && held->getAssetUUID() != mFetching)
    {
        refresh();
    }
}

void ALScriptStudioVimrc::refresh()
{
    const LLUUID item = notecard();
    if (item.isNull())
    {
        readFile();
        return;
    }
    // Not the vimrc while the notecard is.
    mWatch.reset();
    loadCache();
    const bool                   kept = mCacheItem == item;
    const LLViewerInventoryItem* held = gInventory.getItem(item);
    // The copy while it is this notecard's: the notecard as it stands
    // where the item's asset is the one kept, or the last known while
    // inventory has not come as far as the item; standing in, where the
    // asset is another, until that comes.
    take(kept ? mCacheText : std::string(), std::string());
    if (held && (!kept || held->getAssetUUID() != mCacheAsset))
    {
        fetch(item);
    }
}

void ALScriptStudioVimrc::readFile()
{
    const std::string path = filePath();
    // Watched from what is read here: a change after it is heard, and
    // read in turn.
    if (!mWatch || mWatch->path() != path)
    {
        mWatch = std::make_unique<ALWatchedFile>(path, [this](const std::string&) { readFile(); });
        mWatch->poll(1.f);
    }
    else
    {
        mWatch->seen();
    }
    const ALFileStamp stamp = ALFileStamp::of(path);
    if (stamp.size > static_cast<std::uintmax_t>(MOST_VIMRC_BYTES))
    {
        take(std::string(), LLTrans::getString("VimrcTooLarge"));
        return;
    }
    std::string text;
    if (!ALFileRead::whole(path, text, static_cast<std::uintmax_t>(MOST_VIMRC_BYTES)))
    {
        text.clear();
    }
    take(text, std::string());
}

void ALScriptStudioVimrc::fetch(const LLUUID& item)
{
    const LLViewerInventoryItem* held = gInventory.getItem(item);
    if (!held || held->getAssetUUID() == mFetching)
    {
        return;
    }
    mFetching = held->getAssetUUID();
    ALScriptWorkspace::getInstance()->load(ALScriptRef(LLUUID::null, item), [item](const ALScriptWorkspace::Loaded& loaded) {
        if (!ALScriptStudioVimrc::instanceExists())
        {
            return;
        }
        ALScriptStudioVimrc& self = ALScriptStudioVimrc::instance();
        self.mFetching.setNull();
        if (self.notecard() != item)
        {
            // Another since.
            return;
        }
        if (!loaded.error.empty() || !loaded.notecard)
        {
            // What was there stays; why it is not the notecard is said.
            self.take(self.mText, loaded.error.empty() ? LLTrans::getString("VimrcNotNotecard") : loaded.error);
            return;
        }
        self.mCacheItem  = item;
        self.mCacheAsset = loaded.assetId;
        self.mCacheName  = loaded.name;
        self.mCacheText  = loaded.text;
        self.saveCache();
        self.take(loaded.text, std::string());
    });
}

void ALScriptStudioVimrc::loadCache()
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

void ALScriptStudioVimrc::saveCache()
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

void ALScriptStudioVimrc::take(const std::string& text, const std::string& error)
{
    if (text == mText && error == mError)
    {
        return;
    }
    mText  = text;
    mError = error;
    mChanged();
}
