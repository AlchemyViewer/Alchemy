/**
 * @file alregionretirement.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alregionretirement.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "lltexturefetch.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "message.h"
#include "llviewerregion.h"
#include "llvertexbuffer.h"
#include "llviewertexturelist.h"
#include "llworld.h"

namespace
{
    // What a frame spends on the retiring region's objects, killing or passing over them, beyond a floor of one looked
    // at, so every frame advances: a kill takes a linkset, or an avatar and everything it wears.
    constexpr F64 KILL_BUDGET_SECONDS = 0.0005;
    // How long the dead objects may take to leave the object list, a frame's budget at a time, before the region is
    // removed anyway and removeRegion destroys the rest.
    constexpr F64 MAX_DRAIN_WAIT_SECONDS = 5.0;

    void collect_local_ids(const LLViewerObject* objectp, std::vector<U32>& ids)
    {
        ids.push_back(objectp->getLocalID());
        for (const LLViewerObject* childp : objectp->getChildren())
        {
            collect_local_ids(childp, ids);
        }
    }

    bool kept(LLViewerRegion* regionp)
    {
        LLViewerRegion* agent_region = gAgent.getRegion();
        if (!agent_region)
        {
            return true;
        }
        std::vector<LLViewerRegion*> neighbours;
        agent_region->getNeighboringRegions(neighbours);
        const bool neighbour = std::find(neighbours.begin(), neighbours.end(), regionp) != neighbours.end();
        // Near the agent, or near the camera, which can be a draw distance away from it and see as far again.
        const LLVector3d origin = regionp->getOriginGlobal();
        const F64 width = regionp->getWidth();
        const F64 draw_distance = gAgentCamera.mDrawDistance;
        return ALRegionRetirement::keeps(regionp == agent_region, neighbour, origin, width, gAgent.getPositionGlobal(),
                                         draw_distance)
            || ALRegionRetirement::keeps(false, false, origin, width, gAgentCamera.getCameraPositionGlobal(),
                                         draw_distance);
    }
}

ALRegionRetirement::ALRegionRetirement() = default;

ALRegionRetirement::~ALRegionRetirement() = default;

void ALRegionRetirement::update()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    static LLCachedControl<bool> retire_departed(gSavedSettings, "AlchemyRetireDepartedRegions", true);

    watchArrival();
    if (mTeleporting)
    {
        // The teleport may lead back to the region being retired: its arrival must find it gone, not half killed.
        if (mRegionp)
        {
            finish();
        }
        mQueue.clear();
        return;
    }

    if (mArrivalPending && settled(LLFrameTimer::getTotalSeconds() - mArrivalTime,
                                   LLAppViewer::getTextureFetch() ? LLAppViewer::getTextureFetch()->getNumRequests() : 0,
                                   (S32)gTextureList.mCreateTextureList.size()))
    {
        mArrivalPending = false;
        // What the teleport freed waits in the buffer pool otherwise. Not before now: the arrival's own geometry, built
        // meanwhile, takes those stores back.
        LLVertexBuffer::trimPoolSoon();
        if (retire_departed)
        {
            chooseRegions();
        }
    }

    if (!retire_departed)
    {
        // the region being retired, if any, is finished; no other is started
        mQueue.clear();
    }

    if (!mRegionp)
    {
        startNext();
    }
    if (mRegionp)
    {
        killSome();
    }
}

void ALRegionRetirement::forget(LLViewerRegion* regionp)
{
    if (regionp == mRegionp)
    {
        mRegionp = nullptr;
        mObjects.clear();
        mKilledIds.clear();
    }
}

void ALRegionRetirement::watchArrival()
{
    if (gAgent.getTeleportState() != LLAgent::TELEPORT_NONE)
    {
        mTeleporting = true;
        mArrivalPending = false;
        return;
    }
    if (mTeleporting)
    {
        mTeleporting = false;
        mArrivalPending = true;
        mArrivalTime = LLFrameTimer::getTotalSeconds();
    }
}

void ALRegionRetirement::chooseRegions()
{
    for (LLViewerRegion* regionp : LLWorld::getInstance()->getRegionList())
    {
        if (!kept(regionp) && std::find(mQueue.begin(), mQueue.end(), regionp->getHandle()) == mQueue.end())
        {
            mQueue.push_back(regionp->getHandle());
        }
    }
}

void ALRegionRetirement::startNext()
{
    while (!mRegionp && !mQueue.empty())
    {
        const U64 handle = mQueue.front();
        mQueue.pop_front();

        // The agent may have moved since the regions were chosen.
        LLViewerRegion* regionp = LLWorld::getInstance()->getRegionFromHandle(handle);
        if (!regionp || kept(regionp))
        {
            continue;
        }

        mRegionp = regionp;
        mRegionp->setRetiring(true);
        mObjects.clear();
        mKilledIds.clear();
        gObjectList.getRegionObjects(mRegionp, mObjects);
        mObjectCount = mObjects.size();
        mStartTime = LLFrameTimer::getTotalSeconds();
        mKilledTime = mStartTime;
        mFrames = 0;
        LL_INFOS("RegionRetirement") << "Retiring " << mRegionp->getName() << " (" << mRegionp->getHost() << "), "
                                     << mObjectCount << " objects" << LL_ENDL;
    }
}

void ALRegionRetirement::killSome()
{
    // The agent or the camera came near it, or draw distance grew, while it was being retired. What was killed comes
    // back: the cached objects from the region's cache as its idle update resumes (a cached object's drawable hands
    // its entry back as it dies), and every one from the simulator, asked again as a cache miss is.
    if (kept(mRegionp))
    {
        LL_WARNS("RegionRetirement") << mRegionp->getName() << " is near the view again while being retired; keeping it"
                                     << " and asking again for the " << mKilledIds.size() << " objects killed" << LL_ENDL;
        mRegionp->setRetiring(false);
        for (U32 local_id : mKilledIds)
        {
            mRegionp->addCacheMissFull(local_id);
        }
        mRegionp = nullptr;
        mObjects.clear();
        mKilledIds.clear();
        return;
    }

    ++mFrames;
    if (!mObjects.empty())
    {
        const F64 start = LLTimer::getTotalSeconds();
        bool examined = false;
        while (!mObjects.empty() && (!examined || LLTimer::getTotalSeconds() - start < KILL_BUDGET_SECONDS))
        {
            examined = true;
            LLPointer<LLViewerObject> objectp = std::move(mObjects.back());
            mObjects.pop_back();
            // Roots only: a root's death takes its children and its control avatar in the same call, so no frame sees
            // a live linkset with dead parts. What no root here takes, removeRegion's killObjects does.
            const LLVOAvatar* avatarp = objectp->asAvatar();
            if (!objectp->isDead() && objectp->getRegion() == mRegionp && !objectp->getParent()
                && !(avatarp && avatarp->isControlAvatar()))
            {
                collect_local_ids(objectp, mKilledIds);
                gObjectList.noteReleasedTextures(objectp, true);
                gObjectList.killObject(objectp);
            }
        }
        if (mObjects.empty())
        {
            mKilledTime = LLFrameTimer::getTotalSeconds();
        }
        return;
    }

    if (gObjectList.getNumDeadObjects() == 0 || LLFrameTimer::getTotalSeconds() - mKilledTime > MAX_DRAIN_WAIT_SECONDS)
    {
        finish();
    }
}

void ALRegionRetirement::finish()
{
    LLViewerRegion* regionp = mRegionp;
    mRegionp = nullptr;
    mObjects.clear();
    mKilledIds.clear();
    if (!regionp || regionp == gAgent.getRegion())
    {
        if (regionp)
        {
            regionp->setRetiring(false);
        }
        return;
    }

    const std::string name = regionp->getName();
    const LLHost host = regionp->getHost();
    const F64 began = LLTimer::getTotalSeconds();
    regionp->saveObjectCache();
    const F64 saved = LLTimer::getTotalSeconds();
    LLWorld::getInstance()->removeRegion(host);
    // As DisableSimulator does: what the simulator still sends is dropped by the message system, not handled for a
    // region that is gone.
    gMessageSystem->disableCircuit(host);
    const F64 removed = LLTimer::getTotalSeconds();

    LL_INFOS("RegionRetirement") << "Retired " << name << ": " << mObjectCount << " objects retired over " << mFrames
                                 << " frames, removed " << (LLFrameTimer::getTotalSeconds() - mStartTime)
                                 << " s after it began; object cache saved in " << (saved - began) * 1000.0
                                 << " ms, region removed in " << (removed - saved) * 1000.0 << " ms" << LL_ENDL;
}
