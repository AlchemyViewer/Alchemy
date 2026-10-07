/**
 * @file alscriptmasterupload.h
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

#pragma once

#include "alscriptdiskmasters.h"
#include "alwatchedfile.h"

#include <memory>
#include <optional>
#include <string>

class LLInventoryItem;

// A script sent from its master, step by step, each on the main thread as
// the one before it answers:
//  - the file read whole;
//  - the item found -- an object's asked of the region, which keeps what
//    a co-owner saved where the object's own copy does not;
//  - the file expanded as a file on disk is, its includes read from beside
//    it, with an upload header naming it where headers are on;
//  - the world asked what it holds, and, where that is not what the last
//    send left, its text read and hashed beside what would go up;
//  - what the plan says done (ALMasterPlan): the world's text kept in
//    History first where it is gone over, a send of the studio's own held
//    where the world moved under it;
//  - sent, through the workspace as every save goes, and the link moved on.
// What came of it goes to ALScriptDiskMasters::finished, which tells
// whoever listens. A save always goes up: what the preprocessor found is
// said with it, not a reason to keep it back.
class ALScriptMasterUpload : public std::enable_shared_from_this<ALScriptMasterUpload>
{
public:
    typedef ALScriptDiskMasters::Outcome Outcome;

    static void start(const ALMasterLink& link, ALMasterPlan::Send kind);

    ALScriptMasterUpload(const ALMasterLink& link, ALMasterPlan::Send kind);

private:
    void read();
    void found(LLInventoryItem* item);
    void prepared(const ALScriptPrepared& prepared);
    void worldHas(const LLUUID& asset);
    void worldText(const ALScriptLoaded& loaded);
    void decide();
    void upload();
    void uploaded(const ALScriptCompileResult& result);
    void end(Outcome::What what, const std::string& why = std::string(), std::optional<ALScriptCompileResult> result = std::nullopt);

    ALMasterLink       mLink;
    ALMasterLink       mUpdated;
    bool               mChanged = false;
    ALMasterPlan::Send mKind;
    ALScriptRef        mRef;
    std::string        mText;
    ALFileStamp        mStamp;
    std::string        mTarget;
    std::string        mName;
    LLUUID             mAsset;
    ALScriptPrepared   mPrepared;
    std::string        mOurs;
    bool               mWorldMoved = false;
    bool               mWorldSame  = false;
    std::string        mWorldText;
    bool               mKeptTheirs = false;
};
