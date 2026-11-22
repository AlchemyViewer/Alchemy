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
#include "lldrawable.h"
#include "lltexturefetch.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "llvocache.h"
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
    // How long the dead objects may take to leave the object list, a frame's budget at a time, before the retirement
    // ends anyway.
    constexpr F64 MAX_DRAIN_WAIT_SECONDS = 5.0;

    // Whether the object's death hands its entry back to its region's object cache, which makes it again once the
    // region's idle update resumes (LLDrawable::removeFromOctree, LLViewerRegion::removeActiveCacheEntry).
    bool returns_to_cache(LLViewerObject* objectp)
    {
        LLDrawable* drawablep = objectp->mDrawable;
        LLViewerOctreeEntry* entryp = drawablep ? drawablep->getEntry() : nullptr;
        if (!entryp || !entryp->hasVOCacheEntry())
        {
            return false;
        }
        LLVOCacheEntry* cache_entryp = (LLVOCacheEntry*)entryp->getVOCacheEntry();
        return cache_entryp->isValid() && cache_entryp->isState(LLVOCacheEntry::ACTIVE);
    }

    // Notes each object of a linkset, or of an avatar and what it wears, that its region's object cache will not make
    // again.
    void note_lost(LLViewerObject* objectp, ALRetiredObjects& lost)
    {
        if (!returns_to_cache(objectp))
        {
            lost.lost(objectp->getLocalID());
        }
        for (LLViewerObject* childp : objectp->getChildren())
        {
            note_lost(childp, lost);
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
        // The teleport may lead back to the region being retired, whose simulator still holds the agent there: it
        // stays as it stands, to be revived as the teleport arrives at it, or retired further once an arrival
        // elsewhere has settled.
        pause();
        mQueue.clear();
        return;
    }

    reviveNear();

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
    }
}

void ALRegionRetirement::revive(LLViewerRegion* regionp, const char* why)
{
    if (regionp == mRegionp)
    {
        mRegionp = nullptr;
        mObjects.clear();
    }
    regionp->setRetiring(false);
    const std::vector<U32> lost = regionp->retiredObjects().take();
    for (U32 local_id : lost)
    {
        regionp->addCacheMissFull(local_id);
    }
    LL_INFOS("RegionRetirement") << "Reviving " << regionp->getName() << " (" << regionp->getHost() << "), " << why
                                 << ": its object cache makes again what it holds, and the " << lost.size()
                                 << " objects it lost besides are asked for again" << LL_ENDL;
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

void ALRegionRetirement::reviveNear()
{
    // The agent or the camera came near it, draw distance grew, or the agent stands in it.
    for (LLViewerRegion* regionp : LLWorld::getInstance()->getRegionList())
    {
        if (regionp->isRetiring() && kept(regionp))
        {
            revive(regionp, "near the view again");
        }
    }
}

void ALRegionRetirement::pause()
{
    if (mRegionp)
    {
        LL_INFOS("RegionRetirement") << "Pausing the retirement of " << mRegionp->getName() << " for a teleport, "
                                     << mObjects.size() << " objects left to look at" << LL_ENDL;
        mRegionp = nullptr;
        mObjects.clear();
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

        std::vector<LLPointer<LLViewerObject>> objects;
        gObjectList.getRegionObjects(regionp, objects);
        const bool resumed = regionp->isRetiring();
        if (resumed && objects.empty())
        {
            // retired already, and waiting for its simulator to let the agent go there
            continue;
        }

        mRegionp = regionp;
        mRegionp->setRetiring(true);
        mObjects = std::move(objects);
        mObjectCount = mObjects.size();
        mStartTime = LLFrameTimer::getTotalSeconds();
        mKilledTime = mStartTime;
        mFrames = 0;
        LL_INFOS("RegionRetirement") << (resumed ? "Resuming the retirement of " : "Retiring ") << mRegionp->getName()
                                     << " (" << mRegionp->getHost() << "), " << mObjectCount << " objects" << LL_ENDL;
    }
}

void ALRegionRetirement::killSome()
{
    ++mFrames;
    if (!mObjects.empty())
    {
        ALRetiredObjects& lost = mRegionp->retiredObjects();
        const F64 start = LLTimer::getTotalSeconds();
        bool examined = false;
        while (!mObjects.empty() && (!examined || LLTimer::getTotalSeconds() - start < KILL_BUDGET_SECONDS))
        {
            examined = true;
            LLPointer<LLViewerObject> objectp = std::move(mObjects.back());
            mObjects.pop_back();
            // Roots only: a root's death takes its children and its control avatar in the same call, so no frame sees
            // a live linkset with dead parts.
            const LLVOAvatar* avatarp = objectp->asAvatar();
            if (!objectp->isDead() && objectp->getRegion() == mRegionp && !objectp->getParent()
                && !(avatarp && avatarp->isControlAvatar()))
            {
                // before the kill, while the objects made from the cache still hold their entries
                note_lost(objectp, lost);
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
    if (!regionp)
    {
        return;
    }

    const std::string name = regionp->getName();
    const F64 seconds = LLFrameTimer::getTotalSeconds() - mStartTime;
    static LLCachedControl<bool> close_circuit(gSavedSettings, "AlchemyRetireDepartedRegionsCloseCircuit", false);
    if (!close_circuit || regionp == gAgent.getRegion())
    {
        // Kept, with its land, its handshake and its object cache, for as long as its simulator holds the agent there:
        // DisableSimulator removes it as it would without retirement, and a teleport back revives it.
        LL_INFOS("RegionRetirement") << "Retired " << name << ": " << mObjectCount << " objects retired over " << mFrames
                                     << " frames, " << seconds << " s after it began; "
                                     << regionp->retiredObjects().size()
                                     << " to ask for again if it revives, the region kept until its simulator disables it"
                                     << LL_ENDL;
        return;
    }

    // The simulator is told, with CloseCircuit, that the circuit is closed.
    const LLHost host = regionp->getHost();
    const F64 began = LLTimer::getTotalSeconds();
    regionp->saveObjectCache();
    const F64 saved = LLTimer::getTotalSeconds();
    LLWorld::getInstance()->removeRegion(host);
    gMessageSystem->closeCircuit(host);
    const F64 removed = LLTimer::getTotalSeconds();

    LL_INFOS("RegionRetirement") << "Retired " << name << ": " << mObjectCount << " objects retired over " << mFrames
                                 << " frames, removed " << seconds << " s after it began; object cache saved in "
                                 << (saved - began) * 1000.0 << " ms, region removed and its circuit closed in "
                                 << (removed - saved) * 1000.0 << " ms" << LL_ENDL;
}
