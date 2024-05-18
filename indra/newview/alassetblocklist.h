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

#ifndef LL_ASSETBLOCK_H
#define LL_ASSETBLOCK_H

#include "llsingleton.h"
#include "lluuid.h"
#include "llassettype.h"

#include "boost/unordered_map.hpp"
#include "boost/signals2.hpp"


// ============================================================================
// ALAssetBlocklist
//

class ALBlockedAsset
{
public:
    ALBlockedAsset() = default;
    ALBlockedAsset(const LLSD& sd) { fromLLSD(sd); }

    LLSD toLLSD() const;
    void fromLLSD(const LLSD& sd);

    std::string         mLocation;
    LLUUID              mOwnerID;
    LLDate              mDate;
    LLAssetType::EType  mAssetType = LLAssetType::AT_NONE;
    bool                mPersist = true;
};

class ALAssetBlocklist : public LLSingleton<ALAssetBlocklist>
{
    LLSINGLETON(ALAssetBlocklist);
protected:
    /*virtual*/ ~ALAssetBlocklist();

    void load();
    void save() const;

public:
    using entry_list_t = boost::unordered_map<LLUUID, ALBlockedAsset>;
    using change_signal_t = boost::signals2::signal<void()>;

    const entry_list_t& getEntries() const { return mEntries; }

    void addEntry(const LLUUID& asset_id, const LLUUID& avatar_id, const std::string& region, LLAssetType::EType type, bool persist = true);
    void removeEntry(const LLUUID& asset_id);
    void removeEntries(const uuid_vec_t& asset_ids);

    bool isBlocked(const LLUUID& asset_id);

    boost::signals2::connection setChangeCallback(const change_signal_t::slot_type& cb) { return mChangedSignal.connect(cb); }

protected:
    entry_list_t mEntries;
    change_signal_t mChangedSignal;
};

// ============================================================================

#endif // LL_ASSETBLOCK_H
