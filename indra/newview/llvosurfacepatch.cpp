/**
 * @file llvosurfacepatch.cpp
 * @brief Viewer-object derived "surface patch", which is a piece of terrain
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llvosurfacepatch.h"

#include "lldrawpoolterrain.h"

#include "lldrawable.h"
#include "llface.h"
#include "llprimitive.h"
#include "llsky.h"
#include "llsurfacepatch.h"
#include "llsurface.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvlcomposition.h"
#include "llvovolume.h"
#include "pipeline.h"
#include "llspatialpartition.h"

F32 LLVOSurfacePatch::sLODFactor = 1.f;

LLVOSurfacePatch::LLVOSurfacePatch(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
    :   LLStaticViewerObject(id, pcode, regionp),
        mDirtiedPatch(false),
        mPool(NULL),
        mBaseComp(0),
        mPatchp(NULL),
        mDirtyTexture(false),
        mDirtyTerrain(false)
{
    // Terrain must draw during selection passes so it can block objects behind it.
    mbCanSelect = true;
    setScale(LLVector3(16.f, 16.f, 16.f)); // Hack for setting scale for bounding boxes/visibility.
}


LLVOSurfacePatch::~LLVOSurfacePatch()
{
    mPatchp = NULL;
}


void LLVOSurfacePatch::markDead()
{
    if (mPatchp)
    {
        mPatchp->clearVObj();
        mPatchp = NULL;
    }
    LLViewerObject::markDead();
}


bool LLVOSurfacePatch::isActive() const
{
    return false;
}


void LLVOSurfacePatch::setPixelAreaAndAngle(LLAgent &agent)
{
    mAppAngle = 50;
    mPixelArea = 500*500;
}


void LLVOSurfacePatch::updateTextures()
{
}


LLFacePool *LLVOSurfacePatch::getPool()
{
    mPool = (LLDrawPoolTerrain*) gPipeline.getPool(LLDrawPool::POOL_TERRAIN, mPatchp->getSurface()->getSTexture());

    return mPool;
}


LLDrawable *LLVOSurfacePatch::createDrawable(LLPipeline *pipeline)
{
    pipeline->allocDrawable(this);

    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_TERRAIN);

    mBaseComp = llfloor(mPatchp->getMinComposition());
    S32 min_comp, max_comp, range;
    min_comp = llfloor(mPatchp->getMinComposition());
    max_comp = llceil(mPatchp->getMaxComposition());
    range = (max_comp - min_comp);
    range++;
    if (range > 3)
    {
        if ((mPatchp->getMinComposition() - min_comp) > (max_comp - mPatchp->getMaxComposition()))
        {
            // The top side runs over more
            mBaseComp++;
        }
        range = 3;
    }

    LLFacePool *poolp = getPool();

    mDrawable->addFace(poolp, NULL);

    return mDrawable;
}


void LLVOSurfacePatch::updateGL()
{
    if (mPatchp)
    {
        LL_PROFILE_ZONE_SCOPED;
        mPatchp->updateGL();
    }
}

bool LLVOSurfacePatch::updateGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;

    dirtySpatialGroup();

    S32 min_comp, max_comp, range;
    min_comp = lltrunc(mPatchp->getMinComposition());
    max_comp = lltrunc(ceil(mPatchp->getMaxComposition()));
    range = (max_comp - min_comp);
    range++;
    S32 new_base_comp = lltrunc(mPatchp->getMinComposition());
    if (range > 3)
    {
        if ((mPatchp->getMinComposition() - min_comp) > (max_comp - mPatchp->getMaxComposition()))
        {
            // The top side runs over more
            new_base_comp++;
        }
        range = 3;
    }

    // Pick the two closest detail textures for this patch...
    // Then create the draw pool for it.
    // Actually, should get the average composition instead of the center.
    mBaseComp = new_base_comp;

    return true;
}

void LLVOSurfacePatch::updateFaceSize(S32 idx)
{
    LL_PROFILE_ZONE_SCOPED;
    if (idx != 0)
    {
        LL_WARNS() << "Terrain partition requested invalid face!!!" << LL_ENDL;
        return;
    }

    LLFace* facep = mDrawable->getFace(idx);
    if (facep)
    {
        facep->setSize(PATCH_CORNERS, PATCH_CORNERS);
    }
}

bool LLVOSurfacePatch::updateLOD()
{
    return true;
}

// One GL_PATCHES primitive per surface patch: its four corners, region-local,
// z left for the evaluation stage, wound counter-clockwise from above. The
// heights and everything derived from them come from the region's surface
// maps; nothing here changes when the terrain does.
void LLVOSurfacePatch::getTerrainGeometry(LLStrider<LLVector3> &verticesp, LLStrider<U16> &indicesp)
{
    LLFace* facep = mDrawable->getFace(0);
    if (!facep)
    {
        return;
    }

    const LLSurface* surfacep = mPatchp->getSurface();
    const F32 size = surfacep->getGridsPerPatchEdge() * surfacep->getMetersPerGrid();
    const LLVector3& origin = mPatchp->getOriginRegion();
    const F32 x0 = origin.mV[VX];
    const F32 y0 = origin.mV[VY];

    *verticesp++ = LLVector3(x0,        y0,        0.f);
    *verticesp++ = LLVector3(x0 + size, y0,        0.f);
    *verticesp++ = LLVector3(x0 + size, y0 + size, 0.f);
    *verticesp++ = LLVector3(x0,        y0 + size, 0.f);

    const U16 index_offset = (U16)facep->getGeomIndex();
    for (U16 i = 0; i < PATCH_CORNERS; ++i)
    {
        *indicesp++ = index_offset + i;
    }
}

void LLVOSurfacePatch::setPatch(LLSurfacePatch *patchp)
{
    mPatchp = patchp;

    dirtyPatch();
};


void LLVOSurfacePatch::dirtyPatch()
{
    mDirtiedPatch = true;
    dirtyGeom();
    mDirtyTerrain = true;
    LLVector3 center = mPatchp->getCenterRegion();
    LLSurface *surfacep = mPatchp->getSurface();

    setPositionRegion(center);

    F32 scale_factor = surfacep->getGridsPerPatchEdge() * surfacep->getMetersPerGrid();
    setScale(LLVector3(scale_factor, scale_factor, mPatchp->getMaxZ() - mPatchp->getMinZ()));
}

void LLVOSurfacePatch::dirtyGeom()
{
    if (mDrawable)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
        LLFace* facep = mDrawable->getFace(0);
        if (facep)
        {
            facep->setVertexBuffer(NULL);
        }
        mDrawable->movePartition();
    }
}

bool LLVOSurfacePatch::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end, S32 face, bool pick_transparent, bool pick_rigged, bool pick_unselectable, S32 *face_hitp,
                                      LLVector4a* intersection,LLVector2* tex_coord, LLVector4a* normal, LLVector4a* tangent)

{

    if (!lineSegmentBoundingBox(start, end))
    {
        return false;
    }

    LLVector4a da;
    da.setSub(end, start);
    LLVector3 delta(da.getF32ptr());

    LLVector3 pdelta = delta;
    pdelta.mV[2] = 0;

    F32 plength = pdelta.length();

    F32 tdelta = 1.f/plength;

    LLVector3 v_start(start.getF32ptr());

    LLVector3 origin = v_start - mRegionp->getOriginAgent();

    if (mRegionp->getLandHeightRegion(origin) > origin.mV[2])
    {
        //origin is under ground, treat as no intersection
        return false;
    }

    //step one meter at a time until intersection point found

    //VECTORIZE THIS
    const LLVector4a* exta = mDrawable->getSpatialExtents();

    LLVector3 ext[2];
    ext[0].set(exta[0].getF32ptr());
    ext[1].set(exta[1].getF32ptr());

    F32 rad = (delta*tdelta).magVecSquared();

    F32 t = 0.f;
    while ( t <= 1.f)
    {
        LLVector3 sample = origin + delta*t;

        if (AABBSphereIntersectR2(ext[0], ext[1], sample+mRegionp->getOriginAgent(), rad))
        {
            F32 height = mRegionp->getLandHeightRegion(sample);
            if (height > sample.mV[2])
            { //ray went below ground, positive intersection
                //quick and dirty binary search to get impact point
                tdelta = -tdelta*0.5f;
                F32 err_dist = 0.001f;
                F32 dist = fabsf(sample.mV[2] - height);

                while (dist > err_dist && tdelta*tdelta > 0.0f)
                {
                    t += tdelta;
                    sample = origin+delta*t;
                    height = mRegionp->getLandHeightRegion(sample);
                    if ((tdelta < 0 && height < sample.mV[2]) ||
                        (height > sample.mV[2] && tdelta > 0))
                    { //jumped over intersection point, go back
                        tdelta = -tdelta;
                    }
                    tdelta *= 0.5f;
                    dist = fabsf(sample.mV[2] - height);
                }

                if (intersection)
                {
                    F32 height = mRegionp->getLandHeightRegion(sample);
                    if (fabsf(sample.mV[2]-height) < delta.length()*tdelta)
                    {
                        sample.mV[2] = mRegionp->getLandHeightRegion(sample);
                    }
                    intersection->load3((sample + mRegionp->getOriginAgent()).mV);
                }

                if (normal)
                {
                    normal->load3((mRegionp->getLand().resolveNormalGlobal(mRegionp->getPosGlobalFromRegion(sample))).mV);
                }

                return true;
            }
        }

        t += tdelta;
        if (t > 1 && t < 1.f+tdelta*0.99f)
        { //make sure end point is checked (saves vertical lines coming up negative)
            t = 1.f;
        }
    }


    return false;
}

void LLVOSurfacePatch::updateSpatialExtents(LLVector4a& newMin, LLVector4a &newMax)
{
    LLVector3 posAgent = getPositionAgent();
    LLVector3 scale = getScale();
    //make z-axis scale at least 1 to avoid shadow artifacts on totally flat land
    scale.mV[VZ] = llmax(scale.mV[VZ], 1.f);
    newMin.load3( (posAgent-scale*0.5f).mV); // Changing to 2.f makes the culling a -little- better, but still wrong
    newMax.load3( (posAgent+scale*0.5f).mV);
    LLVector4a pos;
    pos.setAdd(newMin,newMax);
    pos.mul(0.5f);
    mDrawable->setPositionGroup(pos);
}

U32 LLVOSurfacePatch::getPartitionType() const
{
    return LLViewerRegion::PARTITION_TERRAIN;
}

LLTerrainPartition::LLTerrainPartition(LLViewerRegion* regionp)
: LLSpatialPartition(LLDrawPoolTerrain::VERTEX_DATA_MASK, false, regionp)
{
    mOcclusionEnabled = false;
    mInfiniteFarClip = true;
    mDrawableType = LLPipeline::RENDER_TYPE_TERRAIN;
    mPartitionType = LLViewerRegion::PARTITION_TERRAIN;
}

void LLTerrainPartition::getGeometry(LLSpatialGroup* group)
{
    LL_PROFILE_ZONE_SCOPED;

    LLVertexBuffer* buffer = group->mVertexBuffer;

    //get vertex buffer striders
    LLStrider<LLVector3> vertices;
    LLStrider<U16> indices;

    llassert_always(buffer->getVertexStrider(vertices));
    llassert_always(buffer->getIndexStrider(indices));

    U32 indices_index = 0;
    U32 index_offset = 0;

    for (std::vector<LLFace*>::iterator i = mFaceList.begin(); i != mFaceList.end(); ++i)
    {
        LLFace* facep = *i;

        facep->setIndicesIndex(indices_index);
        facep->setGeomIndex(index_offset);
        facep->setVertexBuffer(buffer);

        LLVOSurfacePatch* patchp = (LLVOSurfacePatch*) facep->getViewerObject();
        patchp->getTerrainGeometry(vertices, indices);

        indices_index += facep->getIndicesCount();
        index_offset += facep->getGeomCount();
    }

    mFaceList.clear();
}

