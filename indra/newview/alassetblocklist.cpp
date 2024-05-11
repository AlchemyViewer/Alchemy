/**
 * @file alassetblocklist.h
 * @brief
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2024, Rye Mutt <rye@alchemyviewer.org>
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
 *
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alassetblocklist.h"

#include "llagent.h"
#include "llsdserialize.h"
#include "llselectmgr.h"
#include "lltrans.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvocache.h"
#include "llvoavatarself.h"
#include "llworld.h"
#include "pipeline.h"
#include "llxorcipher.h"

 // ============================================================================
 // ALAssetBlocklist
 //

LLSD ALBlockedAsset::toLLSD() const
{
    LLSD data;
    data["owner_id"] = mOwnerID;
    data["location"] = mLocation;
    data["type"] = mAssetType;
    data["date"] = mDate;
    return data;
}

void ALBlockedAsset::fromLLSD(const LLSD& data)
{
    mOwnerID = data["owner_id"].asUUID();
    mLocation = data["location"].asString();
    mAssetType = (LLAssetType::EType)data["type"].asInteger();
    mDate = data["date"].asDate();
    mPersist = true;
}

// ============================================================================
// ALAssetBlocklist
//

#define BLOCK_FILE "asset_blocklist.llsd"
const LLUUID PERSIST_ID("348da8de-abca-a407-8881-bc6f9b583388");

ALAssetBlocklist::ALAssetBlocklist()
{
    load();
}

ALAssetBlocklist::~ALAssetBlocklist()
{
}

void ALAssetBlocklist::load()
{
    llifstream fileDerender(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, BLOCK_FILE));
    if (!fileDerender.is_open())
    {
        LL_WARNS() << "Can't open asset blocklist file \"" << BLOCK_FILE << "\" for reading" << LL_ENDL;
        return;
    }

    mEntries.clear();

    LLSD inSD;
    S32 ret = LLSDSerialize::fromNotation(inSD, fileDerender, LLSDSerialize::SIZE_UNLIMITED);
    fileDerender.close();
    if (ret == LLSDParser::PARSE_FAILURE || !inSD.isMap())
    {
        return;
    }

    for (const auto& entry : inSD.asMap())
    {
        LLUUID shadow_id{ entry.first };
        LLXORCipher cipher(PERSIST_ID.mData, UUID_BYTES);
        cipher.decrypt(shadow_id.mData, UUID_BYTES);
        mEntries.emplace(shadow_id, entry.second);
    }
}

void ALAssetBlocklist::save() const
{
    llofstream fileDerender(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, BLOCK_FILE));
    if (!fileDerender.is_open())
    {
        LL_WARNS() << "Can't open asset blocklist file \"" << BLOCK_FILE << "\" for writing" << LL_ENDL;
        return;
    }

    LLSD outSD;
    for (const auto&[key, value] : mEntries)
    {
        if (value.mPersist)
        {
            LLUUID shadow_id{ key };
            LLXORCipher cipher(PERSIST_ID.mData, UUID_BYTES);
            cipher.encrypt(shadow_id.mData, UUID_BYTES);
            outSD[shadow_id.asString()] = value.toLLSD();
        }
    }

    LLSDSerialize::toNotation(outSD, fileDerender);
}

void ALAssetBlocklist::addEntry(const LLUUID& asset_id, const LLUUID& avatar_id, const std::string& region, LLAssetType::EType type, bool persist/* = true*/)
{
    if (isBlocked(asset_id))
    {
        return;
    }

    ALBlockedAsset new_entry;
    new_entry.mOwnerID = avatar_id;
    new_entry.mLocation = region;
    new_entry.mAssetType = type;
    new_entry.mDate = LLDate(time_corrected());
    new_entry.mPersist = persist;

    mEntries.emplace(asset_id, new_entry);

    if (persist)
        save();

    mChangedSignal();
}

void ALAssetBlocklist::removeEntry(const LLUUID& asset_id)
{
    auto it = mEntries.find(asset_id);
    if (it != mEntries.end())
    {
        bool permanent = it->second.mPersist;
        mEntries.erase(it);

        if (permanent)
            save();

        mChangedSignal();
    }
}

void ALAssetBlocklist::removeEntries(const uuid_vec_t& asset_ids)
{
    bool permanent = false;
    bool changed = false;
    for(const auto& id : asset_ids)
    {
        auto it = mEntries.find(id);
        if (it != mEntries.end())
        {
            if(!permanent)
            {
                permanent = it->second.mPersist;
            }

            mEntries.erase(it);

            if (!changed) 
            {
                changed = true;
            }
        }
    }
    if (permanent)
        save();

    if(changed)
        mChangedSignal();
}

bool ALAssetBlocklist::isBlocked(const LLUUID& asset_id)
{
    return mEntries.contains(asset_id);
}

// ============================================================================
