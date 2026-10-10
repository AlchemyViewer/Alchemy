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
#include "llglslshader.h"
#include "llimage.h"
#include "llimagegl.h"
#include "llregionhandle.h"
#include "llrender.h"
#include "llshadermgr.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "llviewertexture.h"
#include "llworld.h"
#include "pipeline.h"
#include "workqueue.h"

#include <cmath>
#include <memory>

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

    // The draw distance as the main view takes it: probe captures and mirrors set the camera's own far plane, and the
    // field is laid out before they draw.
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    const LLVector3d eye = gAgent.getPosGlobalFromAgent(camera->getOrigin());
    const F32 draw_distance = ALFarPlane::clampDrawDistance(gAgentCamera.mDrawDistance, LLRender::sReverseZ);
    const ALWaterField::Layout layout = ALWaterField::layout(eye.mdV[VX], eye.mdV[VY], REGION_WIDTH_METERS, draw_distance,
                                                             ALFarPlane::edgeWaterStretch(camera->isMainViewInfinite()),
                                                             mScratch.mRegions, 0.0);
    if (layout.mCells == 0)
    {
        // A camera at no finite place lays out nothing; the last frame's field stays.
        return;
    }
    mLayout = layout;
    mVoidDrawn = gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_VOIDWATER);

    const LLVector3d agent_origin = agent_region->getOriginGlobal();
    mSquare = ALWaterField::cellSquare(mLayout, agent_origin.mdV[VX], agent_origin.mdV[VY]);
    mCentre.set(mSquare.mV[VX] + 0.5f * mSquare.mV[VW] * mSquare.mV[VZ], mSquare.mV[VY] + 0.5f * mSquare.mV[VW] * mSquare.mV[VZ]);
    mDefaultHeight = agent_region->getWaterHeight();
    mLowest = mDefaultHeight;
    mHighest = mDefaultHeight;
    for (const ALWaterField::Region& region : mScratch.mRegions)
    {
        mLowest = llmin(mLowest, region.mWaterHeight);
        mHighest = llmax(mHighest, region.mWaterHeight);
    }

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

void ALWaterSurface::draw(LLGLSLShader& shader) const
{
    if (mCellMap.isNull())
    {
        return;
    }
    for (const ALWaterField::Draw& draw : ALWaterField::fieldDraws(mLayout))
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
    const F32 rim = (F32)mLayout.mRim;
    const LLVector4a centre(mCentre.mV[VX], mCentre.mV[VY], 0.5f * (mLowest + mHighest));
    const LLVector4a radius(rim, rim, 0.5f * (mHighest - mLowest) + 0.01f);
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
    min.mV[VZ] = llmin(min.mV[VZ], mLowest);
    max.mV[VX] = llmax(max.mV[VX], mCentre.mV[VX] + rim);
    max.mV[VY] = llmax(max.mV[VY], mCentre.mV[VY] + rim);
    max.mV[VZ] = llmax(max.mV[VZ], mHighest);
}

void ALWaterSurface::trackMaps(LLViewerTexture* current, LLViewerTexture* next)
{
    for (LLViewerTexture* map : { current, next })
    {
        if (!map)
        {
            continue;
        }
        MapStats& stats = mapStats(map->getID());
        stats.mUsed = gFrameCount;
        if (stats.mTexels && stats.mHeights.isNull())
        {
            uploadHeights(stats);
        }
        if (stats.mKnown || stats.mBaking)
        {
            continue;
        }
        LLViewerFetchedTexture* fetched = LLViewerTextureManager::staticCastToFetchedTexture(map);
        if (!fetched)
        {
            continue;
        }
        // The full-resolution pixels, saved by the fetch when asked for, read where they are.
        if (fetched->hasSavedRawImage() && fetched->getSavedRawImageLevel() == 0)
        {
            bakeMap(stats, fetched->getSavedRawImage());
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
    std::shared_ptr<const std::vector<U16>> mTexels;
    S32                                     mWidth = 0;
    S32                                     mHeight = 0;
    F32                                     mMaxBig = 0.f;
    F32                                     mMaxMid = 0.f;
    F64                                     mRmsBig = 0.0;
    F64                                     mRmsMid = 0.0;
    F64                                     mSeconds = 0.0;
};

void ALWaterSurface::bakeMap(MapStats& stats, LLPointer<LLImageRaw> raw)
{
    if (raw.isNull())
    {
        return;
    }
    // The saved pixels are the texture's own copy, never written once saved, and counted thread-safely: the worker
    // reads them under the image's shared lock, and nothing is copied here.
    const auto bake = [raw]()
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("water height bake");
        auto baked = std::make_shared<Baked>();
        LLTimer timer;
        LLImageDataSharedLock lock(raw);
        const U8* data = raw->getData();
        const S32 width = raw->getWidth();
        const S32 height = raw->getHeight();
        const S32 components = raw->getComponents();
        baked->mMean = ALWaterField::meanNormal(data, width, height, components);
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
        baked->mSeconds = timer.getElapsedTimeF64();
        return baked;
    };

    stats.mBaking = true;
    const LLUUID id = stats.mID;
    LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
    // Landing looks the map up again: the cache may have let it go meanwhile.
    if (!main_queue || !general_queue ||
        !main_queue->postTo(general_queue, bake, [id](std::shared_ptr<Baked> baked) { gPipeline.mWaterSurface.land(id, *baked); }))
    {
        // No queue to take it, as the viewer shuts down: the map is left as it was.
        stats.mBaking = false;
    }
}

void ALWaterSurface::land(const LLUUID& id, Baked& baked)
{
    MapStats* stats = findMapStats(id);
    if (!stats || !stats->mBaking)
    {
        return;
    }
    stats->mBaking = false;
    stats->mKnown = true;
    stats->mMean = baked.mMean;
    // Without heights the map's waves stay flat.
    if (!baked.mTexels)
    {
        return;
    }
    // Uploaded at the next track, on the main thread with the context current.
    stats->mTexels = std::move(baked.mTexels);
    stats->mTexelsWidth = baked.mWidth;
    stats->mTexelsHeight = baked.mHeight;
    stats->mHeights = nullptr;
    stats->mMaxBig = baked.mMaxBig;
    stats->mMaxMid = baked.mMaxMid;
    stats->mLanded = LLFrameTimer::getTotalSeconds();
    LL_INFOS("Water") << "Wave heights of " << id << ", " << baked.mWidth << "x" << baked.mHeight << ", baked in "
                      << baked.mSeconds * 1000.0 << " ms: big layer rms " << baked.mRmsBig << " m, max " << baked.mMaxBig
                      << " m; mid layer rms " << baked.mRmsMid << " m, max " << baked.mMaxMid << " m" << LL_ENDL;
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
    // The least recently tracked map makes way.
    MapStats* oldest = &mMaps.front();
    for (MapStats& stats : mMaps)
    {
        if (stats.mUsed < oldest->mUsed)
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
