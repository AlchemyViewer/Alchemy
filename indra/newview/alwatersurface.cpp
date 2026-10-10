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
#include "llagent.h"
#include "llagentcamera.h"
#include "llcamera.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llregionhandle.h"
#include "llrender.h"
#include "llshadermgr.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "pipeline.h"

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
