/**
 * @file llreflectionmapmanager.cpp
 * @brief LLReflectionMapManager class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llreflectionmapmanager.h"

#include <vector>

#include "llviewercamera.h"
#include "llspatialpartition.h"
#include "llviewerregion.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llstartup.h"
#include "llviewermenufile.h"
#include "llnotificationsutil.h"

#if LL_WINDOWS
#pragma warning (push)
#pragma warning (disable : 4702) // compiler complains unreachable code
#endif
#define TINYEXR_USE_MINIZ 0
#include <zlib.h>
#include <tinyexr.h>
#if LL_WINDOWS
#pragma warning (pop)
#endif

LLPointer<LLImageGL> gEXRImage;

void load_exr(const std::string& filename)
{
    // reset reflection maps when previewing a new HDRI
    gPipeline.mReflectionMapManager.reset();
    gPipeline.mReflectionMapManager.initReflectionMaps();

    float* out; // width * height * RGBA
    int width;
    int height;
    const char* err = NULL; // or nullptr in C++11

    int ret =  LoadEXRWithLayer(&out, &width, &height, filename.c_str(), /* layername */ nullptr, &err);
    if (ret == TINYEXR_SUCCESS)
    {
        U32 texName = 0;
        LLImageGL::generateTextures(1, &texName);

        gEXRImage = new LLImageGL(texName, 4, GL_TEXTURE_2D, GL_RGB16F, GL_RGBA, GL_FLOAT);
        gEXRImage->setHasMipMaps(true);
        gEXRImage->setUseMipMaps(true);
        gEXRImage->markStorageAllocated();

        gGL.getTextureSlot(0)->bind(gEXRImage);

        // Immutable storage with the full mip chain, upload and VRAM accounting in one
        // call -- this used to be a raw mutable glTexImage2D bypassing the allocator.
        LLImageGL::allocateTexture2D(GL_TEXTURE_2D, GL_RGB16F, width, height, GL_RGBA, GL_FLOAT, out,
                                     LLImageGL::calcMipLevelCount(width, height));

        free(out); // release memory of image data

        LLImageGL::generateMipmaps(GL_TEXTURE_2D);

        gGL.getTextureSlot(0)->unbind();

    }
    else
    {
        LLSD notif_args;
        notif_args["WHAT"] = filename;
        notif_args["REASON"] = "Unknown";
        if (err)
        {
            notif_args["REASON"] = std::string(err);
            FreeEXRErrorMessage(err); // release memory of error message.
        }
        LLNotificationsUtil::add("CannotLoad", notif_args);
    }
}

void hdri_preview()
{
    LLFilePickerReplyThread::startPicker(
        [](const std::vector<std::string>& filenames, LLFilePicker::ELoadFilter load_filter, LLFilePicker::ESaveFilter save_filter)
        {
            if (LLAppViewer::instance()->quitRequested())
            {
                return;
            }
            if (filenames.size() > 0)
            {
                load_exr(filenames[0]);
            }
        },
        LLFilePicker::FFLOAD_HDRI,
        true);
}

extern bool gCubeSnapshot;
extern bool gTeleportDisplay;

static U32 sUpdateCount = 0;

#if !LL_RELEASE_FOR_DOWNLOAD
namespace
{
    // Register the tail of the ReflectionProbes block for debug validation at shader load.
    // Only the scalars are listed: the leading arrays are what fixes their offsets, so a
    // packing surprise anywhere ahead of them shows up here, and these are the members with no
    // other check -- the C++ struct's own size is the only thing pinning _ssrTailPad.
    const bool s_probe_layout_registered = []
    {
#define LL_PROBE_LAYOUT(m) \
        { -1, #m, (U32)offsetof(LLReflectionMapManager::ReflectionProbeData, m), false }
        LLGLSLShader::registerEngineBlockLayout("ReflectionProbes",
        {
            LL_PROBE_LAYOUT(refmapCount),
            LL_PROBE_LAYOUT(heroShape),
            LL_PROBE_LAYOUT(heroMipCount),
            LL_PROBE_LAYOUT(heroProbeCount),
            LL_PROBE_LAYOUT(iterationCount),
            LL_PROBE_LAYOUT(rayStep),
            LL_PROBE_LAYOUT(distanceBias),
            LL_PROBE_LAYOUT(depthRejectBias),
            LL_PROBE_LAYOUT(glossySampleCount),
            LL_PROBE_LAYOUT(adaptiveStepMultiplier),
        });
#undef LL_PROBE_LAYOUT
        return true;
    }();
}
#endif // !LL_RELEASE_FOR_DOWNLOAD

// get the next highest power of two of v (or v if v is already a power of two)
//defined in llvertexbuffer.cpp
extern U32 nhpo2(U32 v);

static void touch_default_probe(LLReflectionMap* probe)
{
    if (LLViewerCamera::getInstance())
    {
        LLVector3 origin = LLViewerCamera::getInstance()->getOrigin();
        origin.mV[2] += 64.f;

        probe->mOrigin.load3(origin.mV);
    }
}

LLReflectionMapManager::LLReflectionMapManager()
{
    mDynamicProbeCount = LL_MAX_REFLECTION_PROBE_COUNT;
    initCubeFree();
}

void LLReflectionMapManager::initCubeFree()
{
    // start at 1 because index 0 is reserved for mDefaultProbe
    for (U32 i = 1; i < mDynamicProbeCount; ++i)
    {
        mCubeFree.push_back(i);
    }
}

// Order probes for the cube-slot handout below: placed probes first, then by distance.
//
// This order decides which probes exist in VRAM at all, so it is where artist intent has to be
// honoured. Distance alone did the opposite: mDistance is (distance to origin - radius), so a
// large influence volume reads as nearer than a small one the camera is equally far inside --
// an automatic probe covering a 16m octree node has a radius of about 13.9, and a manual probe
// on a 2m prim standing in the same spot has 1. The placed probe lost the slot every time.
//
// A probe's radius is not a statement about its importance when the octree chose it and is
// exactly that when a person did, so priority leads and distance orders within each group.
// Automatic probes are a fallback for space nobody placed a probe in; they should be what gives
// when there is not enough room for everything.
struct CompareProbeDistance
{
    bool operator()(const LLPointer<LLReflectionMap>& lhs, const LLPointer<LLReflectionMap>& rhs) const
    {
        if (lhs->mPriority != rhs->mPriority)
        {
            return lhs->mPriority > rhs->mPriority;
        }

        return lhs->mDistance < rhs->mDistance;
    }
};

static F32 update_score(LLReflectionMap* p)
{
    return gFrameTimeSeconds - p->mLastUpdateTime  - p->mDistance*0.1f;
}

// return true if a is higher priority for an update than b
static bool check_priority(LLReflectionMap* a, LLReflectionMap* b)
{
    if (a->mCubeIndex == -1)
    { // not a candidate for updating
        return false;
    }
    else if (b->mCubeIndex == -1)
    { // b is not a candidate for updating, a is higher priority by default
        return true;
    }
    else if (!a->mComplete && !b->mComplete)
    { //neither probe is complete, generate the placed one first, then use distance
        if (a->mPriority != b->mPriority)
        {
            return a->mPriority > b->mPriority;
        }
        return a->mDistance < b->mDistance;
    }
    else if (a->mComplete && b->mComplete)
    { //both probes are complete, use update_score metric
        return update_score(a) > update_score(b);
    }

    // a or b is not complete,
    if (sUpdateCount % 3 == 0)
    { // every third update, allow complete probes to cut in line in front of non-complete probes to avoid spammy probe generators from deadlocking scheduler (SL-20258))
        return !b->mComplete;
    }

    // prioritize incomplete probe
    return b->mComplete;
}

// helper class to seed octree with probes
void LLReflectionMapManager::update()
{
    if (!LLPipeline::sReflectionProbesEnabled || gTeleportDisplay || LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LL_PROFILE_GPU_ZONE("reflection manager update");
    llassert(!gCubeSnapshot); // assert a snapshot is not in progress
    if (LLAppViewer::instance()->logoutRequestSent())
    {
        return;
    }

    if (mPaused && gFrameTimeSeconds > mResumeTime)
    {
        resume();
    }

    mResetFade = llmin((F32)(mResetFade + gFrameIntervalSeconds * 2.f), 1.f);

    {
        U32 probe_count_temp = mDynamicProbeCount;
        if (mRenderReflectionProbeDynamicAllocation > -1)
        {
            if (mRenderReflectionProbeLevel == 0)
            {
                mDynamicProbeCount = 1;
            }
            else if (mRenderReflectionProbeLevel == 1)
            {
                mDynamicProbeCount = (U32)mProbes.size();
            }
            else if (mRenderReflectionProbeLevel == 2)
            {
                mDynamicProbeCount = llmax((U32)mProbes.size(), 128);
            }
            else
            {
                mDynamicProbeCount = 256;
            }

            if (mRenderReflectionProbeDynamicAllocation > 1)
            {
                // Round mDynamicProbeCount to the nearest increment of 16
                mDynamicProbeCount = ((mDynamicProbeCount + mRenderReflectionProbeDynamicAllocation / 2) / mRenderReflectionProbeDynamicAllocation) * 16;
                mDynamicProbeCount = llclamp(mDynamicProbeCount, 1, mRenderReflectionProbeCount);
            }
            else
            {
                mDynamicProbeCount = llclamp(mDynamicProbeCount + mRenderReflectionProbeDynamicAllocation, 1, mRenderReflectionProbeCount);
            }
        }
        else
        {
            mDynamicProbeCount = mRenderReflectionProbeCount;
        }

        mDynamicProbeCount = llmin(mDynamicProbeCount, LL_MAX_REFLECTION_PROBE_COUNT);

        if (mDynamicProbeCount != probe_count_temp)
            mResetFade = 1.f;
    }

    initReflectionMaps();

    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    if (!mRenderTarget.isComplete())
    {
        U32 color_fmt = render_hdr ? GL_R11F_G11F_B10F : GL_RGB8;
        U32 targetRes = mProbeResolution * 4; // super sample
        mRenderTarget.allocate(targetRes, targetRes, color_fmt, true);
    }

    if (mMipChain.empty())
    {
        U32 res = mProbeResolution;
        U32 count = (U32)(log2((F32)res) + 0.5f);

        mMipChain.resize(count);
        for (U32 i = 0; i < count; ++i)
        {
            mMipChain[i].allocate(res, res, render_hdr ? GL_R11F_G11F_B10F : GL_RGB8);
            res /= 2;
        }
    }

    llassert(mProbes[0] == mDefaultProbe);

    LLVector4a camera_pos;
    camera_pos.load3(LLViewerCamera::instance().getOrigin().mV);

    // process kill list
    for (auto& probe : mKillList)
    {
        auto const & iter = std::find(mProbes.begin(), mProbes.end(), probe);
        if (iter != mProbes.end())
        {
            deleteProbe((U32)(iter - mProbes.begin()));
        }
    }

    mKillList.clear();

    // process create list
    for (auto& probe : mCreateList)
    {
        mProbes.push_back(probe);
    }

    mCreateList.clear();

    if (mProbes.empty())
    {
        return;
    }


    bool did_update = false;

    bool realtime = mRenderReflectionProbeDetail >= (S32)LLReflectionMapManager::DetailLevel::REALTIME;

    LLReflectionMap* closestDynamic = nullptr;

    LLReflectionMap* oldestProbe = nullptr;
    LLReflectionMap* oldestOccluded = nullptr;

    if (mUpdatingProbe != nullptr)
    {
        did_update = true;
        doProbeUpdate();
    }

    // Occlusion state only means anything for as long as the queries that produce it keep
    // running, and they stop whenever LLPipeline::sUseOcclusion drops out of its "query"
    // level -- the UseOcclusion setting, a feature-table veto, or simply turning on wireframe.
    // Nothing used to clear mOccluded when that happened, so every probe that was occluded at
    // that instant stayed occluded for the rest of the session: dropped by getReflectionMaps,
    // skipped by the neighbour packer, and unable to recover, because recovering needs a query
    // result and no query would ever run again. The one probe that escaped was whichever one
    // the camera stood inside, since that path answers false before issuing anything.
    if (LLPipeline::sUseOcclusion <= 1)
    {
        for (auto& probe : mProbes)
        {
            probe->mOccluded = false;
        }
    }

    // Make probes track the viewer objects they are attached to, then work out which automatic
    // probes a manual probe has made redundant.
    //
    // An automatic probe is a fallback for space nobody placed a probe in. Where a placed probe
    // has swallowed its whole influence volume it can no longer change a single pixel -- see
    // eclipses() for what "swallowed" has to mean for that to be true of each volume shape --
    // so the cube slot and the place in the update queue it holds are pure waste. Only probes
    // that are actually rendering get to evict anything, or a probe still generating would
    // leave a hole where the one it displaced used to be.
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmu - manual eclipse");

        // Slack, applied in whichever direction preserves the current state: a probe sitting on
        // the boundary of a manual probe that is moving would otherwise flip every frame, and
        // each flip costs it a full twelve-pass regeneration.
        const F32 ECLIPSE_HYSTERESIS = 0.5f;

        std::vector<LLReflectionMap*> manual_probes;

        for (auto& probe : mProbes)
        {
            probe->syncToViewerObject();

            if (probe != mDefaultProbe && probe->mViewerObject && probe->mComplete &&
                probe->mCubeIndex != -1 && probe->mFadeIn >= 1.f && probe->isRelevant())
            {
                manual_probes.push_back(probe);
            }
        }

        for (auto& probe : mProbes)
        {
            if (probe == mDefaultProbe || probe->mViewerObject)
            { // only automatic probes are ever displaced
                continue;
            }

            F32 margin = probe->mInsideManualProbe ? ECLIPSE_HYSTERESIS : -ECLIPSE_HYSTERESIS;

            bool eclipsed = false;
            for (auto* manual : manual_probes)
            {
                if (manual->eclipses(probe, margin))
                {
                    eclipsed = true;
                    break;
                }
            }

            probe->mInsideManualProbe = eclipsed;
        }
    }

    // Update distance to camera for all probes.
    //
    // This has to happen before the sort below, before the cube slot handout that follows it,
    // and before check_priority's tie-break -- all three consume mDistance. It used to be done
    // inside the scheduling loop further down, one frame after the sort that reads it, and only
    // for probes that loop did not skip: an irrelevant probe, or any probe at all while paused,
    // kept whatever value it last held and sorted on that. A newly created probe holds -1, which
    // is nearer than anything real.
    for (auto& probe : mProbes)
    {
        if (probe == mDefaultProbe)
        {
            // 64m for the purposes of prioritization once it has been generated once, and a
            // hard boost while it has not
            probe->mDistance = probe->mComplete ? 64.f : -4096.f;
            continue;
        }

        if (!probe->isRelevant())
        { // sorts to the very back, so the release pass below reclaims its cube slot -- that
          // pass only ever looks past mReflectionProbeCount, so a probe that stopped mattering
          // while close to the camera used to hold its slot indefinitely
            probe->mDistance = FLT_MAX;
            continue;
        }

        LLVector4a d;
        d.setSub(camera_pos, probe->mOrigin);
        probe->mDistance = d.getLength3().getF32() - probe->mRadius;
    }

    std::sort(mProbes.begin()+1, mProbes.end(), CompareProbeDistance());
    llassert(mProbes[0] == mDefaultProbe);
    llassert(mProbes[0]->mCubeArray == mTexture);
    llassert(mProbes[0]->mCubeIndex == 0);

    // make sure we're assigning cube slots to the closest probes

    // first free any cube indices for distant probes, and for probes that can no longer affect
    // anything. The second case was unreachable before: relevance had no bearing on the sort, so
    // an irrelevant probe near the camera sat inside the budget and held its slot indefinitely,
    // while still being complete enough for getReflectionMaps to keep handing it to the shader.
    // Index 0 is the default probe and is never released.
    for (U32 i = 1; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        llassert(probe != nullptr);

        if (probe && probe->mCubeIndex != -1 && mUpdatingProbe != probe &&
            (i >= mReflectionProbeCount || !probe->isRelevant()))
        { // free this index
            mCubeFree.push_back(probe->mCubeIndex);

            probe->mCubeArray = nullptr;
            probe->mCubeIndex = -1;
            probe->mComplete = false;
            probe->mFadeIn = 0;
            // Cleared with the rest of the render state. A probe that gets a slot back later
            // would otherwise start out culled by a query taken back when it still had
            // something to show, and need two more occlusion passes to say so -- on top of the
            // twelve it needs to regenerate.
            probe->mOccluded = false;
        }
    }

    // next distribute the free indices
    U32 count = llmin(mReflectionProbeCount, (U32)mProbes.size());

    for (U32 i = 1; i < count && !mCubeFree.empty(); ++i)
    {
        // find the closest probe that needs a cube index
        LLReflectionMap* probe = mProbes[i];

        // Relevance checked here as well as in the release pass above. When there are fewer
        // probes than slots every probe is inside the budget, so without this an irrelevant one
        // would be handed a slot and have it taken back on the next frame, forever.
        if (probe->mCubeIndex == -1 && probe->isRelevant())
        {
            S32 idx = allocateCubeIndex();
            llassert(idx > 0); //if we're still in this loop, mCubeFree should not be empty and allocateCubeIndex should be returning good indices
            probe->mCubeArray = mTexture;
            probe->mCubeIndex = idx;
        }
    }

    for (unsigned int i = 0; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        if (probe->getNumRefs() == 1)
        { // no references held outside manager, delete this probe
            deleteProbe(i);
            --i;
            continue;
        }

        if (probe != mDefaultProbe &&
            (!probe->isRelevant() || mPaused))
        { // skip irrelevant probes (or all non-default probes if paused)
            continue;
        }

        if (probe->mComplete)
        {
            probe->autoAdjustOrigin();
            probe->mFadeIn = llmin((F32) (probe->mFadeIn + gFrameIntervalSeconds), 1.f);

            // Rebuild the neighbour graph once the influence volume has drifted away from the
            // one it was built against.
            //
            // The shader stops its scan at the first probe covering a pixel and then considers
            // only that probe's neighbours, which is sound only while the graph is current: two
            // probes that both contain a point necessarily intersect, so the second is reachable
            // from the first. The graph was only ever rebuilt when a probe finished a full
            // twelve-pass update, so a probe riding a moving object described where it used to
            // be, and a pixel it covered could be skipped outright because whichever probe the
            // scan started from had never heard of it. Rebuilding here also fixes the other
            // side, since the search phase writes into both lists.
            if (probe->neighborsAreStale())
            {
                updateNeighbors(probe);
            }
        }
        if (probe->mOccluded && probe->mComplete)
        {
            if (oldestOccluded == nullptr)
            {
                oldestOccluded = probe;
            }
            else if (probe->mLastUpdateTime < oldestOccluded->mLastUpdateTime)
            {
                oldestOccluded = probe;
            }
        }
        else
        {
            if (!did_update &&
                i < mReflectionProbeCount &&
                (oldestProbe == nullptr ||
                    check_priority(probe, oldestProbe)))
            {
               oldestProbe = probe;
            }
        }

        if (realtime &&
            closestDynamic == nullptr &&
            probe->mCubeIndex != -1 &&
            probe->getIsDynamic())
        {
            closestDynamic = probe;
        }

        if (mRenderReflectionProbeLevel == 0)
        {
            // only update default probe when coverage is set to none
            llassert(probe == mDefaultProbe);
            break;
        }
    }

    if (realtime && closestDynamic != nullptr)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmu - realtime");
        // update the closest dynamic probe realtime
        // should do a full irradiance pass on "odd" frames and a radiance pass on "even" frames
        closestDynamic->autoAdjustOrigin();

        // store and override the value of "isRadiancePass" -- parts of the render pipe rely on "isRadiancePass" to set
        // lighting values etc
        bool radiance_pass = isRadiancePass();
        mRadiancePass = mRealtimeRadiancePass;
        for (U32 i = 0; i < 6; ++i)
        {
            updateProbeFace(closestDynamic, i);
        }
        mRealtimeRadiancePass = !mRealtimeRadiancePass;

        // restore "isRadiancePass"
        mRadiancePass = radiance_pass;
    }

    static LLCachedControl<F32> sUpdatePeriod(gSavedSettings, "RenderDefaultProbeUpdatePeriod", 2.f);
    if ((gFrameTimeSeconds - mDefaultProbe->mLastUpdateTime) < sUpdatePeriod)
    {
        if (mRenderReflectionProbeLevel == 0)
        { // when probes are disabled don't update the default probe more often than the prescribed update period
            oldestProbe = nullptr;
        }
    }
    else if (mRenderReflectionProbeLevel > 0)
    { // when probes are enabled don't update the default probe less often than the prescribed update period
      oldestProbe = mDefaultProbe;
    }

    // switch to updating the next oldest probe
    if (!did_update && oldestProbe != nullptr)
    {
        LLReflectionMap* probe = oldestProbe;
        llassert(probe->mCubeIndex != -1);

        probe->autoAdjustOrigin();

        sUpdateCount++;
        mUpdatingProbe = probe;
        doProbeUpdate();
    }

    if (oldestOccluded)
    {
        // as far as this occluded probe is concerned, an origin/radius update is as good as a full update
        oldestOccluded->autoAdjustOrigin();
        oldestOccluded->mLastUpdateTime = gFrameTimeSeconds;
    }
}

void LLReflectionMapManager::refreshSettings()
{
    mRenderReflectionProbeDetail = gSavedSettings.getS32("RenderReflectionProbeDetail");
    mRenderReflectionProbeLevel = gSavedSettings.getS32("RenderReflectionProbeLevel");
    mRenderReflectionProbeCount = gSavedSettings.getU32("RenderReflectionProbeCount");
    mRenderReflectionProbeDynamicAllocation = gSavedSettings.getS32("RenderReflectionProbeDynamicAllocation");
    cleanupQueryPool();
}

LLReflectionMap* LLReflectionMapManager::addProbe(LLSpatialGroup* group)
{
    if (gGLManager.mGLVersion < 4.05f || !LLPipeline::sReflectionProbesEnabled)
    {
        return nullptr;
    }

    LLReflectionMap* probe = new LLReflectionMap();
    probe->mGroup = group;

    if (mDefaultProbe.isNull())
    {  //safety check to make sure default probe is always first probe added
        mDefaultProbe = new LLReflectionMap();
        mProbes.push_back(mDefaultProbe);
    }

    llassert(mProbes[0] == mDefaultProbe);

    if (group)
    {
        probe->mOrigin = group->getOctreeNode()->getCenter();
    }

    if (gCubeSnapshot)
    { //snapshot is in progress, mProbes is being iterated over, defer insertion until next update
        mCreateList.push_back(probe);
    }
    else
    {
        mProbes.push_back(probe);
    }

    return probe;
}

U32 LLReflectionMapManager::probeCount()
{
    return mDynamicProbeCount;
}

U32 LLReflectionMapManager::probeMemory()
{
    // The SH coefficient strip is 9 x probes x RGBA16F -- tens of kilobytes, below the
    // resolution this reports in.
    return (mDynamicProbeCount * 6 * (mProbeResolution * mProbeResolution) * 4) / 1024 / 1024;
}

GLuint LLReflectionMapManager::allocateQuery()
{
    if (mQueryPool.empty())
    {
        GLuint query;
        glGenQueries(1, &query);
        return query;
    }

    GLuint query = mQueryPool.front();
    mQueryPool.pop_front();
    return query;
}

void LLReflectionMapManager::recycleQuery(GLuint query)
{
    mQueryPool.push_back(query);
}

struct CompareProbeDepth
{
    bool operator()(const LLReflectionMap* lhs, const LLReflectionMap* rhs)
    {
        return lhs->mMinDepth < rhs->mMinDepth;
    }
};

void LLReflectionMapManager::getReflectionMaps(std::vector<LLReflectionMap*>& maps)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    // Occlusion is measured from the main camera, so it says nothing about what a probe capture
    // rendering from somewhere else can see. llviewerdisplay stops the queries for the duration
    // of a snapshot for exactly that reason -- "don't read or write it during cube snapshots" --
    // but only the write half was ever honoured, and this is the read. Without it every capture
    // omits whatever the player happens not to be looking at, and bakes that into the probe.
    const bool honor_occlusion = !gCubeSnapshot;

    U32 count = 0;
    U32 lastIdx = 0;
    for (U32 i = 0; count < maps.size() && i < mProbes.size(); ++i)
    {
        // Every probe that does not make the list gets its index cleared, not just the ones
        // without a cube slot. A probe that is occluded or not yet complete used to keep the
        // index it held on some earlier frame, and that slot now belongs to a different probe:
        // the neighbour packer rejects -1 but has no way to tell a stale index from a live one,
        // so a neighbour entry could point at an unrelated probe.
        if (mProbes[i]->mCubeIndex != -1 && mProbes[i]->mComplete &&
            !(honor_occlusion && mProbes[i]->mOccluded))
        {
            maps[count++] = mProbes[i];
            modelview.affineTransform(mProbes[i]->mOrigin, oa);
            mProbes[i]->mMinDepth = -oa.getF32ptr()[2] - mProbes[i]->mRadius;
            mProbes[i]->mMaxDepth = -oa.getF32ptr()[2] + mProbes[i]->mRadius;
        }
        else
        {
            mProbes[i]->mProbeIndex = -1;
        }
        lastIdx = i;
    }

    // set remaining probe indices to -1
    for (U32 i = lastIdx+1; i < mProbes.size(); ++i)
    {
        mProbes[i]->mProbeIndex = -1;
    }

    if (count > 1)
    {
        std::sort(maps.begin(), maps.begin() + count, CompareProbeDepth());
    }

    for (U32 i = 0; i < count; ++i)
    {
        maps[i]->mProbeIndex = i;
    }

    // null terminate list
    if (count < maps.size())
    {
        maps[count] = nullptr;
    }
}

LLReflectionMap* LLReflectionMapManager::registerSpatialGroup(LLSpatialGroup* group)
{
    if (!group)
    {
        return nullptr;
    }
    LLSpatialPartition* part = group->getSpatialPartition();
    if (!part || part->mPartitionType != LLViewerRegion::PARTITION_VOLUME)
    {
        return nullptr;
    }
    OctreeNode* node = group->getOctreeNode();
    F32 size = node->getSize().getF32ptr()[0];
    if (size < 15.f || size > 17.f)
    {
        return nullptr;
    }
    return addProbe(group);
}

LLReflectionMap* LLReflectionMapManager::registerViewerObject(LLViewerObject* vobj)
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return nullptr;
    }

    llassert(vobj != nullptr);

    LLReflectionMap* probe = new LLReflectionMap();
    probe->mViewerObject = vobj;
    // A placed probe, and it is one from the moment it exists. This used to be derived inside
    // autoAdjustOrigin, which is a placement pass and does not run on a probe until it has
    // already won a cube slot -- so a new manual probe carried an automatic probe's priority
    // through the sort that hands slots out, and in a region with none free it could never
    // reach the code that would have told the sort otherwise.
    probe->mPriority = 1;
    probe->mOrigin.load3(vobj->getPositionAgent().mV);

    if (gCubeSnapshot)
    { //snapshot is in progress, mProbes is being iterated over, defer insertion until next update
        mCreateList.push_back(probe);
    }
    else
    {
        mProbes.push_back(probe);
    }

    return probe;
}

S32 LLReflectionMapManager::allocateCubeIndex()
{
    if (!mCubeFree.empty())
    {
        S32 ret = mCubeFree.front();
        mCubeFree.pop_front();
        return ret;
    }

    return -1;
}

void LLReflectionMapManager::deleteProbe(U32 i)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LLReflectionMap* probe = mProbes[i];

    llassert(probe != mDefaultProbe);

    if (probe->mCubeIndex != -1)
    { // mark the cube index used by this probe as being free
        mCubeFree.push_back(probe->mCubeIndex);
    }
    if (mUpdatingProbe == probe)
    {
        mUpdatingProbe = nullptr;
        mUpdatingFace = 0;
    }

    // remove from any Neighbors lists
    for (auto& other : probe->mNeighbors)
    {
        auto const & iter = std::find(other->mNeighbors.begin(), other->mNeighbors.end(), probe);
        llassert(iter != other->mNeighbors.end());
        other->mNeighbors.erase(iter);
    }

    // Probes are distance sorted, order matters.
    mProbes.erase(mProbes.begin() + i);
}


void LLReflectionMapManager::doProbeUpdate()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(mUpdatingProbe != nullptr);

    updateProbeFace(mUpdatingProbe, mUpdatingFace);

    bool debug_updates = gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_PROBE_UPDATES) && mUpdatingProbe->mViewerObject;

    if (++mUpdatingFace == 6)
    {
        if (debug_updates)
        {
            mUpdatingProbe->mViewerObject->setDebugText(llformat("%.1f", (F32)gFrameTimeSeconds), LLColor4(1, 1, 1, 1));
        }
        updateNeighbors(mUpdatingProbe);
        mUpdatingFace = 0;
        if (isRadiancePass())
        {
            mUpdatingProbe->mComplete = true;
            mUpdatingProbe = nullptr;
            mRadiancePass = false;
        }
        else
        {
            mRadiancePass = true;
        }
    }
    else if (debug_updates)
    {
        mUpdatingProbe->mViewerObject->setDebugText(llformat("%.1f", (F32)gFrameTimeSeconds), LLColor4(1, 1, 0, 1));
    }
}

// Do the reflection map update render passes.
// For every 12 calls of this function, one complete reflection probe radiance map and irradiance map is generated
// First six passes render the scene with direct lighting only into a scratch space cube map at the end of the cube map array and generate
// a simple mip chain (not convolution filter).
// At the end of these passes, an irradiance map is generated for this probe and placed into the irradiance cube map array at the index for this probe
// The next six passes render the scene with both radiance and irradiance into the same scratch space cube map and generate a simple mip chain.
// At the end of these passes, a radiance map is generated for this probe and placed into the radiance cube map array at the index for this probe.
// In effect this simulates single-bounce lighting.
void LLReflectionMapManager::updateProbeFace(LLReflectionMap* probe, U32 face)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LL_PROFILE_GPU_ZONE("probe update");
    // hacky hot-swap of camera specific render targets
    gPipeline.mRT = &gPipeline.mAuxillaryRT;

    mLightScale = 1.f;
    static LLCachedControl<F32> max_local_light_ambiance(gSavedSettings, "RenderReflectionProbeMaxLocalLightAmbiance", 8.f);
    if (!isRadiancePass() && probe->getAmbiance() > max_local_light_ambiance)
    {
        mLightScale = max_local_light_ambiance / probe->getAmbiance();
    }

    if (probe == mDefaultProbe)
    {
        touch_default_probe(probe);

        gPipeline.pushRenderTypeMask();

        //only render sky, water, terrain, and clouds
        gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY, LLPipeline::RENDER_TYPE_WL_SKY,
            LLPipeline::RENDER_TYPE_WATER, LLPipeline::RENDER_TYPE_VOIDWATER, LLPipeline::RENDER_TYPE_CLOUDS, LLPipeline::RENDER_TYPE_TERRAIN, LLPipeline::END_RENDER_TYPES);

        probe->update(mRenderTarget.getWidth(), face);

        gPipeline.popRenderTypeMask();
    }
    else
    {
        llassert(mRenderReflectionProbeLevel > 0); // should never update a probe that's not the default probe if reflection coverage is none
        probe->update(mRenderTarget.getWidth(), face);
    }

    gPipeline.mRT = &gPipeline.mMainRT;

    S32 sourceIdx = mReflectionProbeCount;

    if (probe != mUpdatingProbe)
    { // this is the "realtime" probe that's updating every frame, use the secondary scratch space channel
        sourceIdx += 1;
    }

    gGL.setColorMask(true, true);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable blend(GL_BLEND);

    // downsample to placeholder map
    {
        gGL.matrixMode(gGL.MM_MODELVIEW);
        gGL.pushMatrix();
        gGL.loadIdentity();

        gGL.matrixMode(gGL.MM_PROJECTION);
        gGL.pushMatrix();
        gGL.loadIdentity();

        gGL.flush();
        U32 res = mProbeResolution * 2;

        LLRenderTarget* screen_rt = &gPipeline.mAuxillaryRT.screen;

        // perform a gaussian blur on the super sampled render before downsampling
        {
            gGaussianProgram.bind();
            gGaussianProgram.uniform1f(LLShaderMgr::RES_SCALE, 1.f / (mProbeResolution * 2));
            S32 diffuseChannel = gGaussianProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);

            // horizontal
            gGaussianProgram.uniform2f(LLShaderMgr::DIRECTION, 1.f, 0.f);
            gGL.getTextureSlot(diffuseChannel)->bind(screen_rt);
            mRenderTarget.bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            mRenderTarget.flush();

            // vertical
            gGaussianProgram.uniform2f(LLShaderMgr::DIRECTION, 0.f, 1.f);
            gGL.getTextureSlot(diffuseChannel)->bind(&mRenderTarget);
            screen_rt->bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            screen_rt->flush();
        }


        S32 mips = (S32)(log2((F32)mProbeResolution) + 0.5f);

        gReflectionMipProgram.bind();
        S32 diffuseChannel = gReflectionMipProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);

        for (int i = 0; i < mMipChain.size(); ++i)
        {
            LL_PROFILE_GPU_ZONE("probe mip");
            mMipChain[i].bindTarget();
            if (i == 0)
            {
                gGL.getTextureSlot(diffuseChannel)->bind(screen_rt);
            }
            else
            {
                gGL.getTextureSlot(diffuseChannel)->bind(&(mMipChain[i - 1]));
            }


            gReflectionMipProgram.uniform1f(LLShaderMgr::RES_SCALE, 1.f/(mProbeResolution*2));

            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            res /= 2;

            GLint mip = i - (static_cast<GLint>(mMipChain.size()) - mips);

            if (mip >= 0)
            {
                LL_PROFILE_GPU_ZONE("probe mip copy");
                mTexture->bind(0);
                //glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0, probe->mCubeIndex * 6 + face, 0, 0, res, res);
                mTexture->copyFaceFromFramebuffer(mip, sourceIdx, face, res);
                //if (i == 0)
                //{
                    //glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0, probe->mCubeIndex * 6 + face, 0, 0, res, res);
                //}
                mTexture->unbind();
            }
            mMipChain[i].flush();
        }

        gGL.popMatrix();
        gGL.matrixMode(gGL.MM_MODELVIEW);
        gGL.popMatrix();

        gGL.getTextureSlot(diffuseChannel)->unbind();
        gReflectionMipProgram.unbind();
    }

    if (face == 5)
    {
        if (isRadiancePass())
        {
            mMipChain[0].bindTarget();

            //generate radiance map (even if this is not the irradiance map, we need the mip chain for the irradiance map)
            gRadianceGenProgram.bind();
            mVertexBuffer->setBuffer();

            S32 channel = gRadianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES);
            mTexture->bind(channel);
            gRadianceGenProgram.uniform1i(LLShaderMgr::SOURCE_IDX, sourceIdx);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_STRENGTH, 1.f);

            U32 res = mMipChain[0].getWidth();

            for (int i = 0; i < mMipChain.size(); ++i)
            {
                LL_PROFILE_GPU_ZONE("probe radiance gen");

                gRadianceGenProgram.uniform1f(LLShaderMgr::ROUGHNESS, (F32)i / (F32)(mMipChain.size() - 1));
                gRadianceGenProgram.uniform1f(LLShaderMgr::MIP_LEVEL, (GLfloat)i);
                gRadianceGenProgram.uniform1i(LLShaderMgr::U_WIDTH, mProbeResolution);

                for (int cf = 0; cf < 6; ++cf)
                { // for each cube face
                    LLCoordFrame frame;
                    frame.lookAt(LLVector3(0, 0, 0), LLCubeMapArray::sClipToCubeLookVecs[cf], LLCubeMapArray::sClipToCubeUpVecs[cf]);

                    F32 mat[16];
                    frame.getOpenGLRotation(mat);
                    gGL.loadMatrix(mat);

                    mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

                    mTexture->copyFaceFromFramebuffer(i, probe->mCubeIndex, cf, res);
                }

                if (i != mMipChain.size() - 1)
                {
                    res /= 2;
                    glViewport(0, 0, res, res);
                }
            }

            gRadianceGenProgram.unbind();
            mMipChain[0].flush();
        }
        else
        {
            LL_PROFILE_GPU_ZONE("probe sh project");

            // Nine coefficients replace the 16x16x6 irradiance cubemap this used to convolve.
            // The old pass re-integrated the environment once per output texel; this integrates
            // it once, into the only nine numbers irradiance actually has.
            //
            // Integrate a mip whose faces are mSHProjectionRes, not mip 0. Averaging texels
            // before an integral does not change the integral, and the target basis cannot
            // represent anything the finer mip would add.
            S32 sh_mip = 0;
            U32 sh_res = mProbeResolution;
            while (sh_res > mSHProjectionRes && (size_t)(sh_mip + 1) < mMipChain.size())
            {
                sh_res >>= 1;
                ++sh_mip;
            }

            gSHProjectionProgram.bind();
            S32 channel = gSHProjectionProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES);
            mTexture->bind(channel);

            gSHProjectionProgram.uniform1i(LLShaderMgr::SOURCE_IDX, sourceIdx);
            gSHProjectionProgram.uniform1f(LLShaderMgr::MIP_LEVEL, (GLfloat)sh_mip);
            gSHProjectionProgram.uniform1i(LLShaderMgr::U_WIDTH, (S32)sh_res);

            mSHCoeffs.bindTarget();
            // This probe owns one row and must not disturb any other, so the viewport is the
            // write mask -- the target is never cleared.
            glViewport(0, probe->mCubeIndex, LL_SH_COEFF_COUNT, 1);

            mVertexBuffer->setBuffer();
            mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

            mSHCoeffs.flush();

            gSHProjectionProgram.unbind();
        }
    }
}

void LLReflectionMapManager::reset()
{
    mReset = true;
}

void LLReflectionMapManager::pause(F32 duration)
{
    mPaused = true;
    mResumeTime = gFrameTimeSeconds + duration;
}

void LLReflectionMapManager::resume()
{
    mPaused = false;
}

void LLReflectionMapManager::shift(const LLVector4a& offset)
{
    for (auto& probe : mProbes)
    {
        probe->mOrigin.add(offset);

        // Carried along, because a rigid translation of every probe at once leaves the neighbour
        // graph describing exactly the same geometry. Without this a region crossing would mark
        // all of them stale on the same frame and rebuild the whole graph against itself.
        if (probe->mNeighborRadius >= 0.f)
        {
            probe->mNeighborOrigin.add(offset);
        }
    }
}

void LLReflectionMapManager::updateNeighbors(LLReflectionMap* probe)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    // Recorded whether or not there is a list to build. The default probe never has neighbours,
    // and without this it would report itself stale on every frame forever.
    probe->mNeighborOrigin = probe->mOrigin;
    probe->mNeighborRadius = probe->mRadius;

    if (mDefaultProbe == probe)
    {
        return;
    }

    //remove from existing neighbors
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmun - clear");

        for (auto& other : probe->mNeighbors)
        {
            auto const & iter = std::find(other->mNeighbors.begin(), other->mNeighbors.end(), probe);
            llassert(iter != other->mNeighbors.end()); // <--- bug davep if this ever happens, something broke badly
            other->mNeighbors.erase(iter);
        }

        probe->mNeighbors.clear();
    }

    // search for new neighbors
    if (probe->isRelevant())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmun - search");
        for (auto& other : mProbes)
        {
            if (other != mDefaultProbe && other != probe)
            {
                if (other->isRelevant() && probe->intersects(other))
                {
                    probe->mNeighbors.push_back(other);
                    other->mNeighbors.push_back(probe);
                }
            }
        }
    }
}

void LLReflectionMapManager::updateUniforms()
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LL_PROFILE_GPU_ZONE("rmmu - uniforms")


    // Have active manual probes live-track the object they're associated with, BEFORE anything
    // derives from their origin or radius.
    //
    // getReflectionMaps takes mMinDepth/mMaxDepth from these values and sorts the array on
    // mMinDepth, and refBucket -- which is the shader's lower bound for its scan -- is only
    // valid while that sort holds. Refreshing afterwards, as this used to, described where the
    // probe was in the bucket and uploaded where it is in refSphere, and a probe whose bucket
    // entry no longer covers it is one the shader scans straight past. Manual probes are the
    // only ones that get this refresh, so they were the only ones that could vanish from it.
    for (auto& probe : mProbes)
    {
        probe->syncToViewerObject();
    }

    mReflectionMaps.resize(mReflectionProbeCount);
    getReflectionMaps(mReflectionMaps);

    F32 minDepth[256];

    for (int i = 0; i < 256; ++i)
    {
        mProbeData.refBucket[i][0] = mReflectionProbeCount;
        mProbeData.refBucket[i][1] = mReflectionProbeCount;
        mProbeData.refBucket[i][2] = mReflectionProbeCount;
        mProbeData.refBucket[i][3] = mReflectionProbeCount;
        minDepth[i] = FLT_MAX;
    }

    // load modelview matrix into matrix 4a
    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    S32 count = 0;
    U32 nc = 0; // neighbor "cursor" - index into refNeighbor to start writing the next probe's list of neighbors

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    F32 minimum_ambiance = psky->getReflectionProbeAmbiance(should_auto_adjust);

    bool is_ambiance_pass = gCubeSnapshot && !isRadiancePass();
    F32 ambscale = is_ambiance_pass ? 0.f : 1.f;
    ambscale *= mResetFade;
    ambscale = llmax(0, ambscale);
    F32 radscale = is_ambiance_pass ? 0.5f : 1.f;
    radscale *= mResetFade;
    radscale = llmax(0, radscale);

    for (auto* refmap : mReflectionMaps)
    {
        if (refmap == nullptr)
        {
            break;
        }

        if (refmap != mDefaultProbe)
        {
            // bucket search data
            // theory of operation:
            //      1. Determine minimum and maximum depth of each influence volume and store in mDepth (done in getReflectionMaps)
            //      2. Sort by minimum depth
            //      3. Prepare a bucket for each 1m of depth out to 256m
            //      4. For each bucket, store the index of the nearest probe that might influence pixels in that bucket
            //      5. In the shader, lookup the bucket for the pixel depth to get the index of the first probe that could possibly influence
            //          the current pixel.
            unsigned int depth_min = llclamp(llfloor(refmap->mMinDepth), 0, 255);
            unsigned int depth_max = llclamp(llfloor(refmap->mMaxDepth), 0, 255);
            for (U32 i = depth_min; i <= depth_max; ++i)
            {
                if (refmap->mMinDepth < minDepth[i])
                {
                    minDepth[i] = refmap->mMinDepth;
                    mProbeData.refBucket[i][0] = refmap->mProbeIndex;
                }
            }
        }

        llassert(refmap->mProbeIndex == count);
        llassert(mReflectionMaps[refmap->mProbeIndex] == refmap);

        llassert(refmap->mCubeIndex >= 0); // should always be  true, if not, getReflectionMaps is bugged

        modelview.affineTransform(refmap->mOrigin, oa);
        mProbeData.refSphere[count].set(oa.getF32ptr());
        mProbeData.refSphere[count].mV[3] = refmap->mRadius;

        mProbeData.refIndex[count][0] = refmap->mCubeIndex;
        llassert(nc % 4 == 0);
        mProbeData.refIndex[count][1] = nc / 4;
        mProbeData.refIndex[count][3] = refmap->mPriority;

        // for objects that are reflection probes, use the volume as the influence volume of the probe
        // only possibile influence volumes are boxes and spheres, so detect boxes and treat everything else as spheres
        if (refmap->getBox(mProbeData.refBox[count]))
        { // negate priority to indicate this probe has a box influence volume
            // Negated from at least 1, because -0 and 0 are the same integer: a box probe that
            // reached here with priority 0 would be read back as an automatic SPHERE, losing
            // both its shape and its precedence. A box influence volume only exists on a probe
            // attached to a viewer object, which is a manual probe by definition.
            mProbeData.refIndex[count][3] = -llmax(mProbeData.refIndex[count][3], 1);
        }

        mProbeData.refParams[count].set(
            llmax(minimum_ambiance, refmap->getAmbiance())*ambscale, // ambiance scale
            radscale, // radiance scale
            refmap->mFadeIn, // fade in weight
            0.f); // unused

        S32 ni = nc; // neighbor ("index") - index into refNeighbor to write indices for current reflection probe's neighbors
        {
            //LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmsu - refNeighbors");
            //pack neghbor list
            const U32 max_neighbors = 64;
            U32 neighbor_count = 0;

            for (auto& neighbor : refmap->mNeighbors)
            {
                if (ni >= 4096)
                { // out of space
                    break;
                }

                // mProbeIndex alone decides this. getReflectionMaps clears it on every probe it
                // leaves out, so it already carries occlusion, completeness and the cube slot,
                // and it is the only one of those that agrees with the list the shader will
                // actually index. Re-testing mOccluded here would disagree with it during a
                // cube snapshot, where occlusion measured from the main camera is ignored:
                // the probe would be in the scan list and unreachable as a neighbour.
                GLint idx = neighbor->mProbeIndex;
                if (idx == -1)
                {
                    continue;
                }

                // this neighbor may be sampled
                mProbeData.refNeighbor[ni++] = idx;

                neighbor_count++;
                if (neighbor_count >= max_neighbors)
                {
                    break;
                }
            }
        }

        if (nc == ni)
        {
            //no neighbors, tag as empty
            mProbeData.refIndex[count][1] = -1;
        }
        else
        {
            mProbeData.refIndex[count][2] = ni - nc;

            // move the cursor forward
            nc = ni;
            if (nc % 4 != 0)
            { // jump to next power of 4 for compatibility with ivec4
                nc += 4 - (nc % 4);
            }
        }


        count++;
    }

#if 0
    {
        // fill in gaps in refBucket
        S32 probe_idx = mReflectionProbeCount;

        for (int i = 0; i < 256; ++i)
        {
            if (i < count)
            { // for debugging, store depth of mReflectionsMaps[i]
                rpd.refBucket[i][1] = (S32) (mReflectionMaps[i]->mDepth * 10);
            }

            if (rpd.refBucket[i][0] == mReflectionProbeCount)
            {
                rpd.refBucket[i][0] = probe_idx;
            }
            else
            {
                probe_idx = rpd.refBucket[i][0];
            }
        }
    }
#endif

    mProbeData.refmapCount = count;

    gPipeline.mHeroProbeManager.updateUniforms();

    // Get the hero data.

    mProbeData.heroBox = gPipeline.mHeroProbeManager.mHeroData.heroBox;
    mProbeData.heroSphere = gPipeline.mHeroProbeManager.mHeroData.heroSphere;
    mProbeData.heroShape  = gPipeline.mHeroProbeManager.mHeroData.heroShape;
    mProbeData.heroMipCount   = gPipeline.mHeroProbeManager.mHeroData.heroMipCount;
    mProbeData.heroProbeCount = gPipeline.mHeroProbeManager.mHeroData.heroProbeCount;

    // SSR march parameters ride along in this block rather than being re-pushed loose on every
    // bindReflectionProbes; they are constant for the frame.
    mProbeData.iterationCount         = (F32)LLPipeline::RenderScreenSpaceReflectionIterations;
    mProbeData.rayStep                = LLPipeline::RenderScreenSpaceReflectionRayStep;
    mProbeData.distanceBias           = LLPipeline::RenderScreenSpaceReflectionDistanceBias;
    mProbeData.depthRejectBias        = LLPipeline::RenderScreenSpaceReflectionDepthRejectBias;
    mProbeData.glossySampleCount      = (F32)LLPipeline::RenderScreenSpaceReflectionGlossySamples;
    mProbeData.adaptiveStepMultiplier = LLPipeline::RenderScreenSpaceReflectionAdaptiveStepMultiplier;

    //copy mProbeData into uniform buffer object
    mUBO.update(&mProbeData, sizeof(ReflectionProbeData));

#if 0
    if (!gCubeSnapshot)
    {
        for (auto& probe : mProbes)
        {
            LLViewerObject* vobj = probe->mViewerObject;
            if (vobj)
            {
                F32 time = (F32)gFrameTimeSeconds - probe->mLastUpdateTime;
                vobj->setDebugText(llformat("%d/%d/%d/%.1f - %.1f/%.1f", probe->mCubeIndex, probe->mProbeIndex, (U32) probe->mNeighbors.size(), probe->mMinDepth, probe->mMaxDepth, time), time > 1.f ? LLColor4::white : LLColor4::green);
            }
        }
    }
#endif
}

void LLReflectionMapManager::setUniforms()
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return;
    }

    if (!mUBO.allocated())
    {
        updateUniforms();
    }
    mUBO.bind(LLGLSLShader::UB_REFLECTION_PROBES);
}


void renderReflectionProbe(LLReflectionMap* probe)
{
    if (probe->isRelevant())
    {
        F32* po = probe->mOrigin.getF32ptr();

        //draw orange line from probe to neighbors
        gGL.flush();
        gGL.diffuseColor4f(1, 0.5f, 0, 1);
        gGL.begin(gGL.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                continue;
            }

            gGL.vertex3fv(po);
            gGL.vertex3fv(neighbor->mOrigin.getF32ptr());
        }
        gGL.end();
        gGL.flush();

        gGL.diffuseColor4f(1, 1, 0, 1);
        gGL.begin(gGL.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                gGL.vertex3fv(po);
                gGL.vertex3fv(neighbor->mOrigin.getF32ptr());
            }
        }
        gGL.end();
        gGL.flush();
    }

#if 0
    LLSpatialGroup* group = probe->mGroup;
    if (group)
    { // draw lines from corners of object aabb to reflection probe

        const LLVector4a* bounds = group->getBounds();
        LLVector4a o = bounds[0];

        gGL.flush();
        gGL.diffuseColor4f(0, 0, 1, 1);
        F32* c = o.getF32ptr();

        const F32* bc = bounds[0].getF32ptr();
        const F32* bs = bounds[1].getF32ptr();

        // daaw blue lines from corners to center of node
        gGL.begin(gGL.LINES);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] + bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] + bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] - bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] - bs[1], bc[2] + bs[2]);

        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] + bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] + bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] - bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] - bs[1], bc[2] - bs[2]);
        gGL.end();

        //draw yellow line from center of node to reflection probe origin
        gGL.flush();
        gGL.diffuseColor4f(1, 1, 0, 1);
        gGL.begin(gGL.LINES);
        gGL.vertex3fv(c);
        gGL.vertex3fv(po);
        gGL.end();
        gGL.flush();
    }
#endif
}

void LLReflectionMapManager::renderDebug()
{
    gDebugProgram.bind();

    for (auto& probe : mProbes)
    {
        renderReflectionProbe(probe);
    }

    gDebugProgram.unbind();
}

void LLReflectionMapManager::initReflectionMaps()
{
    static LLCachedControl<U32> ref_probe_res(gSavedSettings, "RenderReflectionProbeResolution", 128U);
    U32 probe_resolution = nhpo2(llclamp(ref_probe_res(), (U32)64, (U32)512));
    // No irradiance resolution to size any more: irradiance is nine SH coefficients whatever
    // the environment looks like, so the setting that used to pick a cubemap edge length is gone.
    if (mTexture.isNull() || mReflectionProbeCount != mDynamicProbeCount || mProbeResolution != probe_resolution ||
        !mSHCoeffs.isComplete() || mReset)
    {
        if(mProbeResolution != probe_resolution)
        {
            mRenderTarget.release();
            mMipChain.clear();
        }

        gEXRImage = nullptr;
        mReset = false;
        mReflectionProbeCount = mDynamicProbeCount;
        mProbeResolution = probe_resolution;
        mMaxProbeLOD = log2f((F32)mProbeResolution) - 1.f; // number of mips - 1

        // Signed storage: the linear and quadratic bands are negative over half the sphere, so
        // none of the unsigned float formats the radiance chain uses can hold these.
        mSHCoeffs.release();
        mSHCoeffs.allocate(LL_SH_COEFF_COUNT, mReflectionProbeCount + 2, GL_RGBA16F);

        if (mTexture.isNull() ||
            mTexture->getWidth() != mProbeResolution ||
            mReflectionProbeCount + 2 != mTexture->getCount())
        {
#if 0 // Cubemap copy critically flawed and overflows
            if (mTexture)
            {
                mTexture = new LLCubeMapArray(*mTexture, mProbeResolution, mReflectionProbeCount + 2);
            }
            else
#endif
            {
                mTexture = new LLCubeMapArray();

                static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

                // store mReflectionProbeCount+2 cube maps, final two cube maps are used for render target and radiance map generation
                // source)
                mTexture->allocate(mProbeResolution, 3, mReflectionProbeCount + 2, true, render_hdr);
            }
        }

        // reset probe state
        mUpdatingFace = 0;
        mUpdatingProbe = nullptr;
        mRadiancePass = false;
        mRealtimeRadiancePass = false;

        // if default probe already exists, remember whether or not it's complete (SL-20498)
        bool default_complete = mDefaultProbe.isNull() ? false : mDefaultProbe->mComplete;

        for (auto& probe : mProbes)
        {
            probe->mLastUpdateTime = 0.f;
            probe->mComplete = false;
            probe->mProbeIndex = -1;
            probe->mCubeArray = nullptr;
            probe->mCubeIndex = -1;
            probe->mNeighbors.clear();
            probe->mNeighborRadius = -1.f; // list cleared, so it describes nothing
            probe->mFadeIn = 0;
            // Both of these decide whether a probe is allowed to render, so a reset that left
            // them standing could not clear a probe that was stuck on either -- and this is the
            // path a teleport, a sky change or a probe-count change goes through, which is
            // exactly where someone would expect to get their probes back.
            probe->mOccluded = false;
            probe->mInsideManualProbe = false;
        }

        mCubeFree.clear();
        initCubeFree();

        if (mDefaultProbe.isNull())
        {
            llassert(mProbes.empty()); // default probe MUST be the first probe created
            mDefaultProbe = new LLReflectionMap();
            mProbes.push_back(mDefaultProbe);
        }

        llassert(mProbes[0] == mDefaultProbe);

        mDefaultProbe->mCubeIndex = 0;
        mDefaultProbe->mCubeArray = mTexture;
        mDefaultProbe->mDistance = 64.f;
        mDefaultProbe->mRadius = 4096.f;
        mDefaultProbe->mProbeIndex = 0;
        mDefaultProbe->mComplete = default_complete;

        touch_default_probe(mDefaultProbe);
    }

    if (mVertexBuffer.isNull())
    {
        U32 mask = LLVertexBuffer::MAP_VERTEX;
        LLPointer<LLVertexBuffer> buff = new LLVertexBuffer(mask);
        buff->allocateBuffer(4, 0);

        LLStrider<LLVector3> v;

        buff->getVertexStrider(v);

        // NOTE: z=-1 is load-bearing. radianceGenV/irradianceGenV use this position for BOTH
        // the clip-space gl_Position AND (rotated by modelview) the cubemap SAMPLE DIRECTION,
        // so z is the forward axis of the convolution -- do NOT flatten it. The clip-volume
        // conflict under reverse-Z (z=-1 is outside ZERO_TO_ONE) is resolved in the shaders,
        // which remap only the output clip z while keeping the direction from this raw z=-1.
        v[0] = LLVector3(-1, -1, -1);
        v[1] = LLVector3(1, -1, -1);
        v[2] = LLVector3(-1, 1, -1);
        v[3] = LLVector3(1, 1, -1);

        buff->unmapBuffer();

        mVertexBuffer = buff;
    }
}

void LLReflectionMapManager::cleanup()
{
    mVertexBuffer = nullptr;
    mRenderTarget.release();

    mMipChain.clear();

    mTexture = nullptr;
    mSHCoeffs.release();

    mProbes.clear();
    mKillList.clear();
    mCreateList.clear();

    mReflectionMaps.clear();
    mUpdatingFace = 0;

    mDefaultProbe = nullptr;
    mUpdatingProbe = nullptr;

    mUBO.release();

    cleanupQueryPool();

    // note: also called on teleport (not just shutdown), so make sure we're in a good "starting" state
    initCubeFree();
}

void LLReflectionMapManager::cleanupQueryPool()
{
    if (!mQueryPool.empty())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("cleanup query pool");
        std::vector<GLuint> queries(mQueryPool.begin(), mQueryPool.end());
        glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        mQueryPool.clear();
    }
}

void LLReflectionMapManager::doOcclusion()
{
    LLVector4a eye;
    eye.load3(LLViewerCamera::instance().getOrigin().mV);

    for (auto& probe : mProbes)
    {
        if (probe.isNull() || probe == mDefaultProbe)
        {
            continue;
        }

        // A probe holding no cube data, or one relevance has switched off, cannot reach the
        // frame whatever a query says about it -- and each query costs a box draw, so this was
        // up to a couple of hundred of them per frame spent on probes with nothing to show.
        // Cleared rather than left to go stale: a probe that becomes eligible again should be
        // culled by a query taken while it had something to show, not by one taken before.
        if (probe->mCubeIndex == -1 || !probe->isRelevant())
        {
            probe->mOccluded = false;
            continue;
        }

        probe->doOcclusion(eye);
    }
}

void LLReflectionMapManager::forceDefaultProbeAndUpdateUniforms(bool force)
{
    // Saved against the probe itself rather than against its position in mProbes. The list can
    // gain or lose entries between the two calls -- the create and kill lists are drained in
    // update(), and a probe can be dropped the moment nothing outside the manager holds it --
    // and an index-keyed restore then hands one probe's occlusion state to another. The
    // LLPointer also keeps every saved probe alive until the restore.
    static std::vector<std::pair<LLPointer<LLReflectionMap>, bool> > sSavedOcclusion;

    if (force)
    {
        llassert(sSavedOcclusion.empty());

        for (auto& probe : mProbes)
        {
            sSavedOcclusion.emplace_back(probe, probe->mOccluded);
            if (probe != mDefaultProbe)
            {
                probe->mOccluded = true;
            }
        }

        updateUniforms();
    }
    else
    {
        for (auto& saved : sSavedOcclusion)
        {
            saved.first->mOccluded = saved.second;
        }
        sSavedOcclusion.clear();
        sSavedOcclusion.shrink_to_fit();
    }
}
