/**
 * @file alscriptregionusage.h
 * @brief What the region reserves for the scripts of the objects in hand.
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

#include "lldate.h"
#include "llsd.h"
#include "lluuid.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <vector>

// What the region reserves for the scripts of the objects a studio has in
// hand, as it tells the Script Limits and My Scripts floaters: memory and
// URLs for each object, by its root. Never for each script, and never what
// a script uses: the region counts a Mono script at its limit, 64 KB unless
// llSetMemoryLimit says less. Time it tells only an estate manager.
//
// An attachment of the agent's is asked of AttachmentResources, which
// answers for all of them at once; an object on land, of LandResources,
// by way of its parcel. Each object is asked at most once a minute however
// often it is asked for, and what an answer said is kept with when it said
// it. Nothing of the world here: what an object is and the asking are the
// World's, which the workspace gives it and a test fakes.
class ALScriptRegionUsage
{
public:
    struct Usage
    {
        // Bytes.
        S64    memory = 0;
        S32    urls   = 0;
        LLDate when;
    };

    struct World
    {
        // What an object is, by its root: out of sight or not the agent's to
        // ask about; an attachment of the agent's own; or on land in the
        // agent's region.
        enum class Kind : U8
        {
            None,
            Attachment,
            Land
        };
        std::function<Kind(const LLUUID& root)> kindOf;
        // The agent's attachments asked about; `told` with the answer, once,
        // or not at all where the asking failed.
        std::function<void(std::function<void(const LLSD& answer)> told)> askAttachments;
        // The parcel an object stands on asked about, its details' answer
        // to `told` the same way.
        std::function<void(const LLUUID& root, std::function<void(const LLSD& answer)> told)> askLand;
    };

    // How long an answer stands before an object is asked about again.
    static constexpr F64 ASK_EVERY = 60.0;

    explicit ALScriptRegionUsage(World world);

    // What the region last said an object reserves, where it has said.
    const Usage* usageOf(const LLUUID& root) const;
    // Objects asked about that are due, `now` in LLTimer's seconds: the
    // attachments among them once for all, each other one by its parcel.
    void ask(const std::vector<LLUUID>& roots, F64 now);

    // Answers read: AttachmentResources's, and a parcel's
    // ScriptResourceDetails; each object with what it reserves, by id.
    typedef boost::unordered_flat_map<LLUUID, Usage> usages_t;
    static void readAttachments(const LLSD& answer, const LLDate& when, usages_t& out);
    static void readDetails(const LLSD& answer, const LLDate& when, usages_t& out);

    // What an answer said kept, and heard: the objects it named.
    void heard(const usages_t& usages);
    typedef boost::signals2::signal<void()> heard_signal_t;
    boost::signals2::connection onHeard(const heard_signal_t::slot_type& slot) { return mHeard.connect(slot); }

private:
    World    mWorld;
    usages_t mUsages;
    // When each object, and the attachments, were last asked about.
    boost::unordered_flat_map<LLUUID, F64> mAsked;
    F64                                    mAttachmentsAsked = -ASK_EVERY;
    // The clock as the last asking said it, which an answer is counted by.
    F64                                    mNow = 0.0;
    heard_signal_t                         mHeard;
    // Whether this is still here, for what an answer calls back.
    std::shared_ptr<bool>                  mAlive = std::make_shared<bool>(true);
};
