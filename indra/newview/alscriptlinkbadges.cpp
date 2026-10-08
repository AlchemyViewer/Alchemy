/**
 * @file alscriptlinkbadges.cpp
 * @brief How each in-world script linked to a file on disk stands, for the studio's windows to mark it by.
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

#include "alscriptlinkbadges.h"

#include "alscriptdiskmasters.h"
#include "alserialworker.h"
#include "workqueue.h"

#include <boost/unordered/unordered_flat_set.hpp>

namespace
{
    typedef ALScriptLinkBadges::Badge Badge;

    // How a link stands, by its state and its master's stamp as last
    // looked at: none where it has not been looked at yet.
    Badge badgeOf(const ALMasterLink& link, const ALFileStamp* stamp)
    {
        typedef ALMasterLink::State State;
        if (link.state == State::Suspended || (stamp && !stamp->exists))
        {
            return Badge::Suspended;
        }
        if (link.state == State::Differing)
        {
            return Badge::Differing;
        }
        if (link.state == State::Pending)
        {
            return Badge::Pending;
        }
        if (stamp && stamp->time > link.stamp)
        {
            // A link with no stamp has never had a send from its file.
            return link.stamp == 0 ? Badge::Unsent : Badge::Newer;
        }
        return Badge::Linked;
    }
}

ALScriptLinkBadges::ALScriptLinkBadges()
{
    // Made again as the links change: made, let go of, or sent.
    mLinksConnection = ALScriptDiskMasters::instance().onChanged([this]() { refresh(); });
}

ALScriptLinkBadges::~ALScriptLinkBadges() = default;

void ALScriptLinkBadges::initSingleton()
{
    // The links as they stand when the first window asks.
    refresh();
}

void ALScriptLinkBadges::cleanupSingleton()
{
    if (mWorker)
    {
        mWorker->close();
    }
}

std::optional<ALScriptLinkBadges::Mark> ALScriptLinkBadges::markOf(const ALScriptRef& ref) const
{
    const auto found = mMarks.find(ref);
    return found != mMarks.end() ? std::optional<Mark>(found->second) : std::nullopt;
}

S32 ALScriptLinkBadges::linkedIn(const LLUUID& prim) const
{
    const auto found = mByPrim.find(prim);
    return found != mByPrim.end() ? found->second : 0;
}

S32 ALScriptLinkBadges::masteredBy(const std::string& path) const
{
    if (path.empty() || mByMaster.empty())
    {
        return 0;
    }
    const auto found = mByMaster.find(ALMasterLinks::keyOf(path));
    return found != mByMaster.end() ? found->second : 0;
}

void ALScriptLinkBadges::refresh()
{
    // The links as they are now, which costs nothing of the disk; then the
    // stamps, which do.
    mLinks = ALScriptDiskMasters::instance().all();
    remark();
    if (mLooking)
    {
        mLookAgain = true;
        return;
    }
    look();
}

void ALScriptLinkBadges::remark()
{
    boost::unordered_flat_map<ALScriptRef, Mark>                                 marks;
    boost::unordered_flat_map<LLUUID, S32>                                       by_prim;
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> by_master;
    for (const ALMasterLink& link : mLinks)
    {
        // An orphan's item has left the world: no row shows it, and no save
        // of its file sends it.
        if (link.state == ALMasterLink::State::Orphaned)
        {
            continue;
        }
        const auto stamp = mStamps.find(link.master);
        marks[ALScriptRef(link.object, link.item)] = { badgeOf(link, stamp != mStamps.end() ? &stamp->second : nullptr), link.master };
        ++by_prim[link.object];
        ++by_master[ALMasterLinks::keyOf(link.master)];
    }
    if (marks == mMarks && by_master == mByMaster)
    {
        mByPrim = std::move(by_prim);
        return;
    }
    mMarks    = std::move(marks);
    mByPrim   = std::move(by_prim);
    mByMaster = std::move(by_master);
    mChanged();
}

void ALScriptLinkBadges::look()
{
    // Each master once, but those held as gone: a file that came back is
    // the scripter's to find again.
    std::vector<std::string>                                                 masters;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const ALMasterLink& link : mLinks)
    {
        if (link.state != ALMasterLink::State::Orphaned && link.state != ALMasterLink::State::Suspended && seen.insert(link.master).second)
        {
            masters.push_back(link.master);
        }
    }
    if (masters.empty())
    {
        if (!mStamps.empty())
        {
            mStamps.clear();
            remark();
        }
        return;
    }
    // Where there is no main loop to hand what was found back to -- a test
    // -- looked at here.
    const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        Stamps stamps;
        for (const std::string& master : masters)
        {
            stamps[master] = ALFileStamp::of(master);
        }
        looked(std::move(stamps));
        return;
    }
    if (!mWorker)
    {
        mWorker = std::make_unique<ALSerialWorker>("ScriptLinkBadges");
    }
    mLooking                         = true;
    const std::weak_ptr<bool> alive  = mAlive;
    const ALSerialWorker*     worker = mWorker.get();
    const bool                posted = mWorker->post([this, alive, main_loop, worker, masters = std::move(masters)]() {
        // Given up as the viewer quits, which waits on this: a master on a
        // share out of reach may take a while over each look.
        Stamps stamps;
        for (const std::string& master : masters)
        {
            if (worker->closing())
            {
                return;
            }
            stamps[master] = ALFileStamp::of(master);
        }
        main_loop->post([this, alive, stamps = std::move(stamps)]() mutable {
            if (alive.lock())
            {
                looked(std::move(stamps));
            }
        });
    });
    if (!posted)
    {
        mLooking = false;
    }
}

void ALScriptLinkBadges::looked(Stamps stamps)
{
    mLooking = false;
    mStamps  = std::move(stamps);
    remark();
    if (std::exchange(mLookAgain, false))
    {
        look();
    }
}
