/**
 * @file alregionretirement.h
 * @brief ALRegionRetirement: removing the regions a teleport left behind, once the arrival has settled
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

#include "llpointer.h"
#include "v3dmath.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

class LLViewerObject;
class LLViewerRegion;

// The objects a retiring region lost, by local ID: those killed that its object cache will not make again, and those
// its simulator sent while it retired, dropped. The simulator holds all of them as sent for as long as it holds the
// agent there, so a revival asks for each again.
class ALRetiredObjects
{
public:
    void lost(U32 local_id) { mIds.insert(local_id); }
    // Its simulator killed it: there is nothing left to ask for.
    void killed(U32 local_id) { mIds.erase(local_id); }
    bool empty() const { return mIds.empty(); }
    size_t size() const { return mIds.size(); }

    // Every local ID lost, once, in ascending order, leaving none.
    std::vector<U32> take()
    {
        std::vector<U32> ids(mIds.begin(), mIds.end());
        mIds.clear();
        std::sort(ids.begin(), ids.end());
        return ids;
    }

private:
    boost::unordered_flat_set<U32> mIds;
};

// After a teleport the region left behind stays until its simulator sends DisableSimulator, about a minute later,
// with its objects, avatars, terrain and probes. Once the arrival has settled, every region the agent is neither in
// nor near is retired: marked so that its simulator creates no new objects, and its objects killed a frame's budget at
// a time. The region itself stays, with its land, handshake and object cache, until its simulator closes the circuit
// with DisableSimulator: until then the simulator holds the agent there, counts everything it sent the viewer as sent,
// and sends none of it again. One that comes near the view again, or that a teleport arrives at, is revived: what its
// object cache holds is made again from it, and every other object it lost is asked for again.
// AlchemyRetireDepartedRegionsCloseCircuit instead removes a retired region at once and closes its circuit, the
// simulator told so with CloseCircuit.
class ALRegionRetirement
{
public:
    // An arrival has settled this long after it at the earliest, and this long at the latest whatever the queues.
    static constexpr F64 MIN_SETTLE_SECONDS = 5.0;
    static constexpr F64 MAX_SETTLE_SECONDS = 15.0;
    // Below these the texture fetcher and the create queue count as calm.
    static constexpr S32 CALM_FETCH_REQUESTS = 32;
    static constexpr S32 CALM_CREATE_QUEUE = 8;

    // A region stays when it is the agent's, a neighbour connected to it, or when its square comes within draw
    // distance plus one region width of the agent in the ground plane.
    static bool keeps(bool agent_region, bool connected_neighbour, const LLVector3d& region_origin, F64 region_width,
                      const LLVector3d& agent_pos, F64 draw_distance)
    {
        if (agent_region || connected_neighbour)
        {
            return true;
        }
        const F64 dx = std::max({ region_origin.mdV[VX] - agent_pos.mdV[VX], 0.0,
                                  agent_pos.mdV[VX] - (region_origin.mdV[VX] + region_width) });
        const F64 dy = std::max({ region_origin.mdV[VY] - agent_pos.mdV[VY], 0.0,
                                  agent_pos.mdV[VY] - (region_origin.mdV[VY] + region_width) });
        const F64 reach = std::max(draw_distance, 0.0) + region_width;
        return dx * dx + dy * dy <= reach * reach;
    }

    static bool settled(F64 seconds_since_arrival, S32 fetch_requests, S32 create_queue)
    {
        if (seconds_since_arrival < MIN_SETTLE_SECONDS)
        {
            return false;
        }
        return seconds_since_arrival >= MAX_SETTLE_SECONDS
            || (fetch_requests <= CALM_FETCH_REQUESTS && create_queue <= CALM_CREATE_QUEUE);
    }

    // What LLWorld::addRegion does with the region it already has for the handle a simulator arrives at.
    enum class EArrival
    {
        REUSE,   // the region as it stands
        REVIVE,  // a retiring region whose simulator still holds the agent there, revived
        REPLACE, // a new region in its place
    };

    static EArrival arrivalAt(bool same_host, bool alive, bool retiring)
    {
        if (!same_host || !alive)
        {
            return EArrival::REPLACE;
        }
        return retiring ? EArrival::REVIVE : EArrival::REUSE;
    }

    ALRegionRetirement();
    ~ALRegionRetirement();
    ALRegionRetirement(const ALRegionRetirement&) = delete;
    ALRegionRetirement& operator=(const ALRegionRetirement&) = delete;

    // Once a frame, before the regions' own updates.
    void update();

    // regionp is about to be deleted, by whatever path.
    void forget(LLViewerRegion* regionp);

    // regionp, retiring, is wanted again: its object cache makes again what it holds as the region's idle update
    // resumes, and every other object it lost is asked of its simulator. why ends the log line.
    void revive(LLViewerRegion* regionp, const char* why);

private:
    void watchArrival();
    // Revives the retiring regions the agent or the camera came near again.
    void reviveNear();
    // Leaves the region being retired as it stands, still retiring, for a teleport that may lead back to it.
    void pause();
    void chooseRegions();
    void startNext();
    void killSome();
    // Ends the retirement of the region being retired, once nothing of it is left to kill.
    void finish();

    bool mTeleporting = false;
    bool mArrivalPending = false;
    F64 mArrivalTime = 0.0;

    // handles of the regions waiting their turn
    std::deque<U64> mQueue;

    // the region being retired, and what of it is left to kill
    LLViewerRegion* mRegionp = nullptr;
    std::vector<LLPointer<LLViewerObject>> mObjects;
    size_t mObjectCount = 0;
    F64 mStartTime = 0.0;
    F64 mKilledTime = 0.0;
    U32 mFrames = 0;
};
