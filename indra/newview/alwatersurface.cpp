/**
 * @file alwatersurface.cpp
 * @brief The water's field for the frame: its layout around the camera and the map of cell water heights.
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

#include "alwatersurface.h"

#include "alfarplane.h"
#include "alwaterheightbake.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "llcamera.h"
#include "lldrawpoolwater.h"
#include "llglslshader.h"
#include "llimage.h"
#include "llimagegl.h"
#include "llregionhandle.h"
#include "llrender.h"
#include "llshadermgr.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llworld.h"
#include "pipeline.h"
#include "workqueue.h"

#include <cmath>
#include <memory>

ALWaterSurface::WaveSettings ALWaterSurface::waveSettings()
{
    static LLCachedControl<F32> wave_height(gSavedSettings, "AlchemyRenderWaterWaveHeight", 0.5f);
    static LLCachedControl<F32> tess_density(gSavedSettings, "AlchemyRenderWaterTessDensity", 32.f);
    WaveSettings out;
    out.mHeight = llclamp((F32)wave_height, 0.f, 4.f);
    out.mDensity = llclamp((F32)tess_density, 8.f, 256.f);
    return out;
}

bool ALWaterSurface::wavesWanted()
{
    static LLCachedControl<bool> waves(gSavedSettings, "AlchemyRenderWaterWaves", true);
    return waves && water_waves_supported();
}

void ALWaterSurface::update()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    static LLCachedControl<bool> render_water(gSavedSettings, "RenderWater", true);
    if (!render_water)
    {
        release();
        return;
    }

    LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region)
    {
        return;
    }

    mScratch.mRegions.clear();
    for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
    {
        U32 x = 0;
        U32 y = 0;
        from_region_handle(region->getHandle(), &x, &y);
        const S32 cells = llmax(1, (S32)(region->getWidth() / REGION_WIDTH_METERS));
        mScratch.mRegions.push_back({ (S32)(x / REGION_WIDTH_U32), (S32)(y / REGION_WIDTH_U32), cells, region->getWaterHeight() });
    }
    const F32 default_height = agent_region->getWaterHeight();
    F32 lowest = default_height;
    F32 highest = default_height;
    for (const ALWaterField::Region& region : mScratch.mRegions)
    {
        lowest = llmin(lowest, region.mWaterHeight);
        highest = llmax(highest, region.mWaterHeight);
    }

    // The maps the pool binds this frame, followed before anything is drawn, so the waves' bound is the bound maps'.
    // Their means are read only by the tiled reads.
    static LLCachedControl<U32> hextile(gSavedSettings, "AlchemyRenderWaterHextile", 1);
    const bool waves = wavesWanted() && gWaterWavesProgram.mProgramObject != 0;
    if (LLDrawPoolWater* pool = static_cast<LLDrawPoolWater*>(gPipeline.findPool(LLDrawPool::POOL_WATER)))
    {
        trackMaps(pool->getNormalMap(0), pool->getNormalMap(1), hextile >= 1, waves);
    }

    // The waves' bound, A: each map's dial times its rise times the most its layers move the surface, a tiled layer's by
    // up to sqrt(3) more, the blend bands' variance gain at its peak; the larger of the two maps'.
    const WaveSettings settings = waveSettings();
    F32 max_displacement = 0.f;
    if (waves)
    {
        const F32 big_gain = hextile >= 1 ? F_SQRT3 : 1.f;
        const F32 mid_gain = hextile >= 2 ? F_SQRT3 : 1.f;
        for (const LLUUID& id : mTracked)
        {
            const MapStats* stats = findMapStats(id);
            if (stats && stats->mHeights.notNull())
            {
                const F32 rise = (F32)llclamp((LLFrameTimer::getTotalSeconds() - stats->mLanded) / RAMP_SECONDS, 0.0, 1.0);
                max_displacement = llmax(max_displacement, settings.mHeight * rise * (big_gain * stats->mMaxBig + mid_gain * stats->mMaxMid));
            }
        }
        // The heights are read from halves rounded to nearest, up to 2^-11 past the field they were baked from, and
        // worked in float from there: the culls test against A strictly, so it is let out by more than both.
        if (max_displacement > 0.f)
        {
            max_displacement = max_displacement * (1.f + 1.f / 1024.f) + 0.01f;
        }
    }

    // The reach the waves are tessellated to, across the plane: as far as a point lies within the reach of the camera
    // itself, so none is tessellated from a height where its edges would be whole.
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    const LLVector3 eye_agent = camera->getOrigin();
    F64 reach = 0.0;
    if (waves && max_displacement > 0.f)
    {
        const F32 above = llmax(0.f, llmax(eye_agent.mV[VZ] - highest, lowest - eye_agent.mV[VZ]));
        reach = ALWaterField::reachAcross(ALWaterField::tessReach(settings.mDensity), (F64)above);
    }

    // The draw distance and the projection as the main view takes them this frame (display_update_camera): probe
    // captures and mirrors set the camera's own, and the field is laid out before they draw and before the main view's
    // are set.
    const LLVector3d eye = gAgent.getPosGlobalFromAgent(eye_agent);
    const F32 draw_distance = ALFarPlane::clampDrawDistance(gAgentCamera.mDrawDistance, LLRender::sReverseZ);
    static LLCachedControl<S32> force_projection(gSavedSettings, "AlchemyRenderFarPlaneForce", ALFarPlane::FORCE_AUTOMATIC);
    const bool infinite = ALFarPlane::isInfinite(
        ALFarPlane::projectionFar(LLRender::sReverseZ, false, force_projection(), gAgentCamera.mDrawDistance));
    const ALWaterField::Layout layout = ALWaterField::layout(eye.mdV[VX], eye.mdV[VY], REGION_WIDTH_METERS, draw_distance,
                                                             ALFarPlane::edgeWaterStretch(infinite), mScratch.mRegions, reach);
    if (layout.mCells == 0)
    {
        // A camera at no finite place lays out nothing; the last frame's field stays.
        return;
    }

    mLayout = layout;
    mEye = eye_agent;
    mVoidDrawn = gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_VOIDWATER);
    mMaxDisplacement = max_displacement;
    mWavesDrawn = reach > 0.0 && mLayout.mPatchMaxX >= mLayout.mPatchMinX && mLayout.mPatchMaxY >= mLayout.mPatchMinY;

    const LLVector3d agent_origin = agent_region->getOriginGlobal();
    mSquare = ALWaterField::cellSquare(mLayout, agent_origin.mdV[VX], agent_origin.mdV[VY]);
    mCentre.set(mSquare.mV[VX] + 0.5f * mSquare.mV[VW] * mSquare.mV[VZ], mSquare.mV[VY] + 0.5f * mSquare.mV[VW] * mSquare.mV[VZ]);
    mDefaultHeight = default_height;
    mLowest = lowest;
    mHighest = highest;

    mScratch.mCentreX = mLayout.mCentreX;
    mScratch.mCentreY = mLayout.mCentreY;
    mScratch.mCells = mLayout.mCells;
    mScratch.mDefaultHeight = mDefaultHeight;
    if (mCellMap.notNull() && mScratch == mKey)
    {
        return;
    }
    std::swap(mKey, mScratch);

    ALWaterField::fillCells(mLayout, mKey.mRegions, mKey.mDefaultHeight, mTexels);
    const U8* bytes = reinterpret_cast<const U8*>(mTexels.data());
    if (mCellMap.isNull())
    {
        mCellMap = new LLImageGL(ALWaterField::MAP_SIZE, ALWaterField::MAP_SIZE, 2, /*usemipmaps=*/false);
        mCellMap->setNeedsAlphaAndPickMask(false);
        mCellMap->setExplicitFormat(GL_RG32F, GL_RG, GL_FLOAT);
        mCellMap->createGLTexture(0, bytes);
    }
    else
    {
        mCellMap->setImage(bytes);
        gGL.getTextureSlot(0)->unbind();
    }
}

void ALWaterSurface::release()
{
    mCellMap = nullptr;
    mKey = Key();
    mWavesDrawn = false;
    mMaxDisplacement = 0.f;
    // The maps' heights are GL textures; their texels stay, and are uploaded again when next wanted. A bake still
    // running lands on its map as before.
    for (MapStats& stats : mMaps)
    {
        stats.mHeights = nullptr;
    }
}

void ALWaterSurface::bind(LLGLSLShader& shader) const
{
    if (mCellMap.notNull())
    {
        const S32 channel = shader.enableTexture(LLShaderMgr::WATER_CELL_MAP);
        if (channel > -1)
        {
            gGL.getTextureSlot(channel)->bindSampled(mCellMap.get(), ALSamplers::PointClamp);
        }
        shader.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, mSquare.mV);
    }
    else
    {
        // No cells: the square has none a side, so every point is past it.
        const LLVector4 none(0.f, 0.f, REGION_WIDTH_METERS, 0.f);
        shader.uniform4fv(LLShaderMgr::WATER_CELL_SQUARE, 1, none.mV);
    }
    shader.uniform1f(LLShaderMgr::WATER_DEFAULT_HEIGHT, mDefaultHeight);

    shader.uniform4f(LLShaderMgr::WATER_RINGS, (F32)mLayout.mHalf, (F32)mLayout.mRings, (F32)mLayout.mRim, mVoidDrawn ? 1.f : 0.f);
}

void ALWaterSurface::draw(LLGLSLShader& shader, bool patched) const
{
    if (mCellMap.isNull())
    {
        return;
    }
    // The fringe only where the patches are drawn with the field, bindWaves having set their rectangle.
    const ALWaterField::PatchRect rect = patched && mWavesDrawn ? ALWaterField::patchRect(mLayout) : ALWaterField::PatchRect();
    for (const ALWaterField::Draw& draw : ALWaterField::fieldDraws(mLayout, rect))
    {
        if (draw.mInstances <= 0)
        {
            continue;
        }
        shader.uniform1i(LLShaderMgr::WATER_LAYOUT, (S32)draw.mPart);
        gGL.drawArraysInstanced(draw.mStrip ? LLRender::TRIANGLE_STRIP : LLRender::TRIANGLES, 0, (U32)draw.mVertices,
                                (U32)draw.mInstances);
        ++gPipeline.mWaterDraws;
    }
}

bool ALWaterSurface::rim(const LLVector3& eye, LLVector2& lo, LLVector2& hi) const
{
    if (mCellMap.isNull())
    {
        return false;
    }
    const F32 rim = (F32)mLayout.mRim;
    lo.set(mCentre.mV[VX] - rim - eye.mV[VX], mCentre.mV[VY] - rim - eye.mV[VY]);
    hi.set(mCentre.mV[VX] + rim - eye.mV[VX], mCentre.mV[VY] + rim - eye.mV[VY]);
    return true;
}

bool ALWaterSurface::inView(LLCamera& camera) const
{
    if (mCellMap.isNull())
    {
        return false;
    }
    // The waves reach A above the highest water and below the lowest.
    const F32 rim = (F32)mLayout.mRim;
    const LLVector4a centre(mCentre.mV[VX], mCentre.mV[VY], 0.5f * (mLowest + mHighest));
    const LLVector4a radius(rim, rim, 0.5f * (mHighest - mLowest) + maxDisplacement() + 0.01f);
    return camera.AABBInFrustumNoFarClip(centre, radius) != 0;
}

bool ALWaterSurface::patchesInView(LLCamera& camera) const
{
    if (!mWavesDrawn || mCellMap.isNull())
    {
        return false;
    }
    const ALWaterField::PatchRect rect = ALWaterField::patchRect(mLayout);
    const F32 width = mSquare.mV[VZ];
    const F32 x0 = mSquare.mV[VX] + (F32)rect.mMinX * width;
    const F32 y0 = mSquare.mV[VY] + (F32)rect.mMinY * width;
    const F32 x1 = mSquare.mV[VX] + (F32)rect.mEndX * width;
    const F32 y1 = mSquare.mV[VY] + (F32)rect.mEndY * width;
    const LLVector4a centre(0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (mLowest + mHighest));
    const LLVector4a radius(0.5f * (x1 - x0), 0.5f * (y1 - y0), 0.5f * (mHighest - mLowest) + mMaxDisplacement + 0.01f);
    return camera.AABBInFrustumNoFarClip(centre, radius) != 0;
}

void ALWaterSurface::addExtents(LLVector3& min, LLVector3& max) const
{
    if (mCellMap.isNull())
    {
        return;
    }
    const F32 rim = (F32)mLayout.mRim;
    min.mV[VX] = llmin(min.mV[VX], mCentre.mV[VX] - rim);
    min.mV[VY] = llmin(min.mV[VY], mCentre.mV[VY] - rim);
    min.mV[VZ] = llmin(min.mV[VZ], mLowest - maxDisplacement());
    max.mV[VX] = llmax(max.mV[VX], mCentre.mV[VX] + rim);
    max.mV[VY] = llmax(max.mV[VY], mCentre.mV[VY] + rim);
    max.mV[VZ] = llmax(max.mV[VZ], mHighest + maxDisplacement());
}

void ALWaterSurface::bindWaves(LLGLSLShader& shader, bool drawn, bool patches, const LLViewerTexture* a, const LLViewerTexture* b) const
{
    drawn = drawn && mWavesDrawn;
    // The patch rectangle in the cell map's cells, its max past the end; all 0, empty, without waves.
    const ALWaterField::PatchRect rect = drawn ? ALWaterField::patchRect(mLayout) : ALWaterField::PatchRect();
    const S32 rect_uniform[4] = { rect.mMinX, rect.mMinY, rect.mEndX, rect.mEndY };
    shader.uniform4iv(LLShaderMgr::WATER_PATCH_RECT, 1, rect_uniform);
    if (!patches || !drawn)
    {
        return;
    }

    const WaveSettings settings = waveSettings();
    shader.uniform1f(LLShaderMgr::WATER_TESS_DENSITY, settings.mDensity);
    shader.uniform1f(LLShaderMgr::WATER_MAX_DISPLACEMENT, mMaxDisplacement);
    shader.uniform2f(LLShaderMgr::WATER_WAVE_REACH, (F32)mLayout.mReach, mMaxDisplacement);
    shader.uniform3fv(LLShaderMgr::WATER_WAVE_EYE, 1, mEye.mV);

    // The view's planes but the far one: the patches lie within the reach, which the draw distance does not bound.
    // Read at bind, from whichever camera this view is drawn from.
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    constexpr S32 CULL_PLANES = 5;
    const S32 planes[CULL_PLANES] = { LLCamera::AGENT_PLANE_LEFT, LLCamera::AGENT_PLANE_RIGHT, LLCamera::AGENT_PLANE_NEAR,
                                 LLCamera::AGENT_PLANE_BOTTOM, LLCamera::AGENT_PLANE_TOP };
    F32 cull[CULL_PLANES * 4];
    for (S32 i = 0; i < CULL_PLANES; ++i)
    {
        const LLPlane& plane = camera->getAgentPlane(planes[i]);
        for (S32 c = 0; c < 4; ++c)
        {
            cull[i * 4 + c] = plane[c];
        }
    }
    shader.uniform4fv(LLShaderMgr::WATER_CULL_PLANES, CULL_PLANES, cull);

    // The patches front to back, as nearly as rows of them allow. The water shades only what the depth test lets
    // through, so a crest drawn first spares the troughs it hides behind it.
    const LLVector3 at = camera->getAtAxis();
    const ALWaterField::PatchOrder order = ALWaterField::patchOrder(at.mV[VX], at.mV[VY]);
    shader.uniform4iv(LLShaderMgr::WATER_PATCH_ORDER, 1, order.mOrder);

    // Each map's heights on their own trilinear sampler, whatever RenderWaterMipNormal puts the normal maps on, and
    // its wave height; a map not baked has none, and its sampler reads black rather than whatever the unit held.
    F32 amp[2] = { 0.f, 0.f };
    const U32 units[2] = { LLShaderMgr::WATER_HEIGHT_MAP, LLShaderMgr::WATER_HEIGHT_MAP2 };
    const LLViewerTexture* maps[2] = { a, b };
    for (S32 i = 0; i < 2; ++i)
    {
        const Heights heights = mapHeights(maps[i]);
        const S32 channel = shader.enableTexture(units[i]);
        if (channel < 0)
        {
            continue;
        }
        if (heights.mTexture)
        {
            gGL.getTextureSlot(channel)->bindSampled(heights.mTexture, ALSamplers::TrilinearWrap);
            amp[i] = settings.mHeight * heights.mRise;
        }
        else if (LLViewerTexture::sBlackImagep.notNull() && LLViewerTexture::sBlackImagep->getGLTexture())
        {
            gGL.getTextureSlot(channel)->bindSampled(LLViewerTexture::sBlackImagep->getGLTexture(), ALSamplers::PointWrap);
        }
    }
    shader.uniform2f(LLShaderMgr::WATER_WAVE_AMP, amp[0], amp[1]);
}

S32 ALWaterSurface::patchCount() const
{
    if (!mWavesDrawn)
    {
        return 0;
    }
    const S32 cells = (mLayout.mPatchMaxX - mLayout.mPatchMinX + 1) * (mLayout.mPatchMaxY - mLayout.mPatchMinY + 1);
    return cells * ALWaterField::PATCHES_PER_CELL * ALWaterField::PATCHES_PER_CELL;
}

void ALWaterSurface::drawPatches(LLGLSLShader& shader) const
{
    if (!mWavesDrawn || mCellMap.isNull())
    {
        return;
    }
    gGL.setPatchVertices(4);
    gGL.drawArraysInstanced(LLRender::PATCHES, 0, 4, (U32)patchCount());
    gPipeline.mWaterDraws += 1;
}

void ALWaterSurface::trackMaps(LLViewerTexture* current, LLViewerTexture* next, bool means, bool heights)
{
    mTracked[0] = current ? current->getID() : LLUUID::null;
    mTracked[1] = next ? next->getID() : LLUUID::null;
    for (LLViewerTexture* map : { current, next })
    {
        if (!map)
        {
            continue;
        }
        MapStats& stats = mapStats(map->getID());
        stats.mUsed = gFrameCount;
        if (heights && stats.mTexels && stats.mHeights.isNull())
        {
            uploadHeights(stats);
        }
        const bool wanted = (means && !stats.mKnown) || (heights && !stats.mHeightsBaked);
        if (!wanted || stats.mBaking)
        {
            continue;
        }
        LLViewerFetchedTexture* fetched = LLViewerTextureManager::staticCastToFetchedTexture(map);
        if (!fetched)
        {
            continue;
        }
        // The full-resolution pixels, saved by the fetch when asked for, read where they are. The bake holds its own
        // reference, so the texture's is let go at once rather than kept to the texture list's sweep, unless another
        // user waits on the texture's loads or asked to keep them (LLViewerFetchedTexture::destroySavedRawImage).
        if (fetched->hasSavedRawImage() && fetched->getSavedRawImageLevel() == 0)
        {
            bakeMap(stats, fetched->getSavedRawImage(), heights);
            if (!fetched->hasCallbacks())
            {
                fetched->destroySavedRawImage();
            }
        }
        else
        {
            fetched->forceToSaveRawImage(0);
        }
    }
}

struct ALWaterSurface::Baked
{
    LLVector2                               mMean;
    bool                                    mHeightsWanted = false;
    std::shared_ptr<const std::vector<U16>> mTexels;
    S32                                     mWidth = 0;
    S32                                     mHeight = 0;
    F32                                     mMaxBig = 0.f;
    F32                                     mMaxMid = 0.f;
    F64                                     mRmsBig = 0.0;
    F64                                     mRmsMid = 0.0;
    F64                                     mSeconds = 0.0;
};

void ALWaterSurface::bakeMap(MapStats& stats, LLPointer<LLImageRaw> raw, bool heights)
{
    if (raw.isNull())
    {
        return;
    }
    // The saved pixels are the texture's own copy, never written once saved, and counted thread-safely: the worker
    // reads them under the image's shared lock, and nothing is copied here.
    // Anything thrown on the worker would be thrown again on the main thread as it lands (LL::WorkQueue::postTo), so
    // nothing leaves: ALWaterHeightBake::bake catches its own, and an allocation failing around it lands as a failure.
    const auto bake = [raw, heights]() -> std::shared_ptr<Baked>
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("water height bake");
        try
        {
            auto baked = std::make_shared<Baked>();
            LLTimer timer;
            LLImageDataSharedLock lock(raw);
            const U8* data = raw->getData();
            const S32 width = raw->getWidth();
            const S32 height = raw->getHeight();
            const S32 components = raw->getComponents();
            baked->mMean = ALWaterField::meanNormal(data, width, height, components);
            baked->mHeightsWanted = heights;
            if (heights)
            {
                // A map past the bake's size is halved until it fits, first.
                std::vector<U8> fitted;
                const U8* pixels = data;
                size_t size = (size_t)raw->getDataSize();
                S32 bake_width = width;
                S32 bake_height = height;
                if ((width > ALWaterHeightBake::MAX_BAKE_SIZE || height > ALWaterHeightBake::MAX_BAKE_SIZE) &&
                    ALWaterHeightBake::fitToSize(data, width, height, components, ALWaterHeightBake::MAX_BAKE_SIZE, fitted,
                                                 bake_width, bake_height))
                {
                    pixels = fitted.data();
                    size = fitted.size();
                }
                const ALWaterHeightBake::Heights field = ALWaterHeightBake::bake(pixels, size, bake_width, bake_height, components);
                if (field.valid())
                {
                    baked->mTexels = std::make_shared<const std::vector<U16>>(ALWaterHeightBake::halfTexels(field));
                    baked->mWidth = field.mWidth;
                    baked->mHeight = field.mHeight;
                    baked->mMaxBig = field.mMaxBig;
                    baked->mMaxMid = field.mMaxMid;
                    const size_t count = (size_t)field.mWidth * (size_t)field.mHeight;
                    F64 square[2] = {};
                    for (size_t n = 0; n < count; ++n)
                    {
                        square[0] += (F64)field.mBig[n] * field.mBig[n];
                        square[1] += (F64)field.mMid[n] * field.mMid[n];
                    }
                    baked->mRmsBig = ALWaterHeightBake::BIG_METRES * std::sqrt(square[0] / (F64)count);
                    baked->mRmsMid = ALWaterHeightBake::MID_METRES * std::sqrt(square[1] / (F64)count);
                }
            }
            baked->mSeconds = timer.getElapsedTimeF64();
            return baked;
        }
        catch (const std::exception& e)
        {
            LL_WARNS("Water") << "A wave height bake failed: " << e.what() << LL_ENDL;
        }
        return nullptr;
    };

    stats.mBaking = true;
    const LLUUID id = stats.mID;
    LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
    // Landing looks the map up again: the cache may have let it go meanwhile.
    if (!main_queue || !general_queue ||
        !main_queue->postTo(general_queue, bake, [id](std::shared_ptr<Baked> baked) { gPipeline.mWaterSurface.land(id, baked.get()); }))
    {
        // No queue to take it, as the viewer shuts down: the map is left as it was.
        stats.mBaking = false;
    }
}

void ALWaterSurface::land(const LLUUID& id, const Baked* baked)
{
    MapStats* stats = findMapStats(id);
    if (!stats || !stats->mBaking)
    {
        return;
    }
    stats->mBaking = false;
    stats->mKnown = true;
    if (!baked)
    {
        // Not tried again: the map's mean stays neutral and its waves flat.
        stats->mMean.setZero();
        stats->mHeightsBaked = true;
        return;
    }
    stats->mMean = baked->mMean;
    if (!baked->mHeightsWanted)
    {
        return;
    }
    // Baked or not, the map has given what heights it has; without, its waves stay flat.
    stats->mHeightsBaked = true;
    if (!baked->mTexels)
    {
        return;
    }
    // Uploaded at the next track, on the main thread with the context current.
    stats->mTexels = baked->mTexels;
    stats->mTexelsWidth = baked->mWidth;
    stats->mTexelsHeight = baked->mHeight;
    stats->mHeights = nullptr;
    stats->mMaxBig = baked->mMaxBig;
    stats->mMaxMid = baked->mMaxMid;
    stats->mLanded = LLFrameTimer::getTotalSeconds();
    LL_INFOS("Water") << "Wave heights of " << id << ", " << baked->mWidth << "x" << baked->mHeight << ", baked in "
                      << baked->mSeconds * 1000.0 << " ms: big layer rms " << baked->mRmsBig << " m, max " << baked->mMaxBig
                      << " m; mid layer rms " << baked->mRmsMid << " m, max " << baked->mMaxMid << " m" << LL_ENDL;
}

void ALWaterSurface::uploadHeights(MapStats& stats)
{
    if (!stats.mTexels || stats.mTexelsWidth <= 0 || stats.mTexelsHeight <= 0)
    {
        return;
    }
    // The two fields side by side in a texel, and every mip, box-averaged as GL generates them: a layer seen far off
    // reads its heights averaged toward the plane.
    LLPointer<LLImageGL> texture = new LLImageGL(stats.mTexelsWidth, stats.mTexelsHeight, 2, true);
    texture->setNeedsAlphaAndPickMask(false);
    texture->setExplicitFormat(GL_RG16F, GL_RG, GL_HALF_FLOAT);
    if (!texture->createGLTexture(0, reinterpret_cast<const U8*>(stats.mTexels->data())))
    {
        // Not tried again: the map's waves stay flat.
        LL_WARNS("Water") << "The wave heights of " << stats.mID << " did not upload" << LL_ENDL;
        stats.mTexels.reset();
        return;
    }
    stats.mHeights = texture;
}

ALWaterSurface::Heights ALWaterSurface::mapHeights(const LLViewerTexture* map) const
{
    Heights out;
    const MapStats* stats = map ? findMapStats(map->getID()) : nullptr;
    if (stats && stats->mHeights.notNull())
    {
        out.mTexture = stats->mHeights;
        out.mMaxBig = stats->mMaxBig;
        out.mMaxMid = stats->mMaxMid;
        out.mRise = (F32)llclamp((LLFrameTimer::getTotalSeconds() - stats->mLanded) / RAMP_SECONDS, 0.0, 1.0);
    }
    return out;
}

LLVector4 ALWaterSurface::mapMeans(const LLViewerTexture* a, const LLViewerTexture* b) const
{
    LLVector4 means(0.f, 0.f, 0.f, 0.f);
    const LLViewerTexture* maps[2] = { a, b };
    for (S32 i = 0; i < 2; ++i)
    {
        const MapStats* stats = maps[i] ? findMapStats(maps[i]->getID()) : nullptr;
        if (stats && stats->mKnown)
        {
            means.mV[2 * i] = stats->mMean.mV[VX];
            means.mV[2 * i + 1] = stats->mMean.mV[VY];
        }
    }
    return means;
}

ALWaterSurface::MapStats& ALWaterSurface::mapStats(const LLUUID& id)
{
    for (MapStats& stats : mMaps)
    {
        if (stats.mID == id)
        {
            return stats;
        }
    }
    if (mMaps.size() < MAP_CACHE)
    {
        mMaps.emplace_back();
        mMaps.back().mID = id;
        return mMaps.back();
    }
    // The least recently tracked map makes way, but not one still baking while another can: tracked again before its
    // bake landed, it would be baked a second time and one of the two bakes thrown away.
    MapStats* oldest = nullptr;
    for (MapStats& stats : mMaps)
    {
        if (!oldest || (oldest->mBaking && !stats.mBaking) ||
            (oldest->mBaking == stats.mBaking && stats.mUsed < oldest->mUsed))
        {
            oldest = &stats;
        }
    }
    *oldest = MapStats();
    oldest->mID = id;
    return *oldest;
}

ALWaterSurface::MapStats* ALWaterSurface::findMapStats(const LLUUID& id)
{
    for (MapStats& stats : mMaps)
    {
        if (stats.mID == id)
        {
            return &stats;
        }
    }
    return nullptr;
}

const ALWaterSurface::MapStats* ALWaterSurface::findMapStats(const LLUUID& id) const
{
    for (const MapStats& stats : mMaps)
    {
        if (stats.mID == id)
        {
            return &stats;
        }
    }
    return nullptr;
}
