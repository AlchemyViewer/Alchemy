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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class LLInventoryItem;

// A script sent from its master, step by step, each on the main thread as
// the one before it answers:
//  - the file looked at and read whole, on a thread of its own, since a
//    drive may be slow or a share far away;
//  - the item found -- an object's asked of the region, which keeps what
//    a co-owner saved where the object's own copy does not;
//  - the file expanded as a file on disk is, its includes read from beside
//    it, with an upload header naming it where headers are on;
//  - the world asked what it holds, and, where that is not what the last
//    send left or nothing went up through the link yet, its text read and
//    hashed beside what would go up; where it moved to another text,
//    History asked whether this viewer kept that one already as it went
//    up, which is no change of anybody else's;
//  - what the plan says done (ALMasterPlan): the world's text kept in
//    History first where it is gone over -- by the first send through a
//    link too, which the plan sees as no move -- a send of the studio's
//    own held where the world moved under it;
//  - sent, through the workspace as every save goes, and the link moved on.
// What came of it goes to ALScriptDiskMasters::finished, which tells
// whoever listens. A save always goes up: what the preprocessor found is
// said with it, not a reason to keep it back.
class ALScriptMasterUpload : public std::enable_shared_from_this<ALScriptMasterUpload>
{
public:
    typedef ALScriptDiskMasters::Outcome Outcome;

    static void start(const ALMasterLink& link, ALMasterPlan::Send kind);

    // What a send would find, nothing sent: the master read and expanded as
    // it would go up, and, where `world`, the world's text read and hashed
    // beside it, whether or not it moved. Nothing is said, and the link is
    // left as it is -- it need not be one yet: a file proposed for an item
    // is probed through a link made up for it. An object's contents are
    // asked of the region again, as a send asks them, unless not `refetch`:
    // then the copy the object holds is taken, where it has one, for
    // contents just fetched -- every script of an object probed, say,
    // which would otherwise ask the region again for each.
    struct Probe
    {
        ALScriptRef ref;
        // Where a send could not have gone up -- the master gone or not to
        // be read, the item gone, out of reach or of the other language --
        // what it would have come to, and why; Sent where it could.
        Outcome::What what = Outcome::What::Sent;
        std::string   why;
        std::string   itemName;
        // What would go up, hashed as the link keeps it; whether that is
        // what went up last; whether the world's text was read, and holds
        // it already; whether the world moved since the link's base, to
        // a text this viewer did not keep already in History.
        std::string ours;
        bool        unchanged  = false;
        bool        worldRead  = false;
        bool        worldSame  = false;
        bool        worldMoved = false;
        // What the preprocessor found.
        std::vector<ALScriptDiagnostic> preprocessed;
        // What a link made from it keeps, as a send's would: the master's
        // stamp as this read found it (ALFileStamp::time); the files on disk
        // the expansion read, as `disk:<path>` identities; and whether the
        // expansion had a problem -- an include not found, say.
        S64                      stamp = 0;
        std::vector<std::string> uses;
        bool                     missed = false;
    };
    typedef std::function<void(const Probe&)> probed_t;
    static void probe(const ALMasterLink& link, probed_t done, bool world = true, bool refetch = true);

    ALScriptMasterUpload(const ALMasterLink& link, ALMasterPlan::Send kind);

private:
    // The master looked at and read, off the main thread; what was found;
    // and the item looked for.
    void read();
    void masterRead(const ALFileStamp& stamp, std::string text, bool whole);
    void find();
    void found(LLInventoryItem* item);
    // An object's contents not said by the region in time.
    void unanswered();
    void prepared(const ALScriptPrepared& prepared);
    void worldHas(const LLUUID& asset);
    void worldText(const ALScriptLoaded& loaded);
    // Whether History kept the world's text already, as a save of this
    // viewer's went up: asked off the main thread; and what it said.
    void askHistory();
    void historyHas(bool kept);
    void decide();
    // What the world holds kept in History, before it is gone over.
    void keepTheirs();
    void upload();
    void uploaded(const ALScriptCompileResult& result);
    void end(Outcome::What what, const std::string& why = std::string(), std::optional<ALScriptCompileResult> result = std::nullopt);
    // What a text hashes to as the link keeps it: a script's as the
    // envelope's halves, a notecard's as its text.
    std::string hashOf(const std::string& text) const;
    // Whether nothing has gone up through the link yet: its base is what
    // the item held when it was linked, not what a send left.
    bool firstSend() const { return mLink.hash.empty(); }

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
    bool               mWorldRead  = false;
    // Moved to a text History kept already: no change of anybody else's.
    bool               mWorldOurs  = false;
    // A notecard that carries items in the world.
    bool               mWorldCarries = false;
    // Told what was found, where this is a probe and sends nothing; and
    // whether it reads the world's text. Whether an object's contents are
    // asked of the region again: a send's always are.
    probed_t           mProbed;
    bool               mProbeWorld = true;
    bool               mRefetch    = true;
};
