/**
 * @file llsurfacepatch.cpp
 * @brief LLSurfacePatch class implementation
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

#include "llsurfacepatch.h"
#include "llviewerobjectlist.h"
#include "llvosurfacepatch.h"
#include "llsurface.h"
#include "pipeline.h"
#include "llagent.h"
#include "llsky.h"

// For getting composition values
#include "llviewerregion.h"
#include "llvlcomposition.h"
#include "lldrawpool.h"
#include "noise.h"

extern U64MicrosecondsImplicit gFrameTime;
extern LLPipeline gPipeline;

LLSurfacePatch::LLSurfacePatch()
:   mHasReceivedData(false),
    mSTexUpdate(false),
    mDirty(false),
    mDirtyZStats(true),
    mHeightsGenerated(false),
    mDataOffset(0),
    mDataZ(NULL),
    mVObjp(NULL),
    mOriginRegion(0.f, 0.f, 0.f),
    mCenterRegion(0.f, 0.f, 0.f),
    mMinZ(0.f),
    mMaxZ(0.f),
    mMeanZ(0.f),
    mRadius(0.f),
    mMinComposition(0.f),
    mMaxComposition(0.f),
    mMeanComposition(0.f),
    // This flag is used to communicate between adjacent surfaces and is
    // set to non-zero values by higher classes.
    mConnectedEdge(NO_EDGE),
    mLastUpdateTime(0),
    mSurfacep(NULL)
{
    S32 i;
    for (i = 0; i < 8; i++)
    {
        setNeighborPatch(i, NULL);
    }
}


LLSurfacePatch::~LLSurfacePatch()
{
    mVObjp = NULL;
}


void LLSurfacePatch::dirty()
{
    // These are outside of the loop in case we're still waiting for a dirty from the
    // texture being updated...
    if (mVObjp)
    {
        mVObjp->dirtyGeom();
    }
    else
    {
        LL_WARNS("Terrain") << "No viewer object for this surface patch!" << LL_ENDL;
    }

    mDirtyZStats = true;
    mHeightsGenerated = false;

    if (!mDirty)
    {
        mDirty = true;
        mSurfacep->dirtySurfacePatch(this);
    }
}


void LLSurfacePatch::setSurface(LLSurface *surfacep)
{
    mSurfacep = surfacep;
    if (mVObjp == (LLVOSurfacePatch *)NULL)
    {
        llassert(mSurfacep->mType == 'l');

        mVObjp = (LLVOSurfacePatch *)gObjectList.createObjectViewer(LLViewerObject::LL_VO_SURFACE_PATCH, mSurfacep->getRegion());
        mVObjp->setPatch(this);
        mVObjp->setPositionRegion(mCenterRegion);
        gPipeline.createObject(mVObjp);
    }
}

void LLSurfacePatch::disconnectNeighbor(LLSurface *surfacep)
{
    U32 i;
    for (i = 0; i < 8; i++)
    {
        if (getNeighborPatch(i))
        {
            if (getNeighborPatch(i)->mSurfacep == surfacep)
            {
                setNeighborPatch(i, NULL);
            }
        }
    }

    // Clean up connected edges
    if (getNeighborPatch(EAST))
    {
        if (getNeighborPatch(EAST)->mSurfacep == surfacep)
        {
            mConnectedEdge &= ~EAST_EDGE;
        }
    }
    if (getNeighborPatch(NORTH))
    {
        if (getNeighborPatch(NORTH)->mSurfacep == surfacep)
        {
            mConnectedEdge &= ~NORTH_EDGE;
        }
    }
    if (getNeighborPatch(WEST))
    {
        if (getNeighborPatch(WEST)->mSurfacep == surfacep)
        {
            mConnectedEdge &= ~WEST_EDGE;
        }
    }
    if (getNeighborPatch(SOUTH))
    {
        if (getNeighborPatch(SOUTH)->mSurfacep == surfacep)
        {
            mConnectedEdge &= ~SOUTH_EDGE;
        }
    }
}

LLVector3 LLSurfacePatch::getPointAgent(const U32 x, const U32 y) const
{
    U32 surface_stride = mSurfacep->getGridsPerEdge();
    U32 point_offset = x + y*surface_stride;
    LLVector3 pos;
    pos = getOriginAgent();
    pos.mV[VX] += x * mSurfacep->getMetersPerGrid();
    pos.mV[VY] += y * mSurfacep->getMetersPerGrid();
    pos.mV[VZ] = *(mDataZ + point_offset);
    return pos;
}

LLVector2 LLSurfacePatch::getTexCoords(const U32 x, const U32 y) const
{
    U32 surface_stride = mSurfacep->getGridsPerEdge();
    U32 point_offset = x + y*surface_stride;
    LLVector3 pos, rel_pos;
    pos = getOriginAgent();
    pos.mV[VX] += x * mSurfacep->getMetersPerGrid();
    pos.mV[VY] += y * mSurfacep->getMetersPerGrid();
    pos.mV[VZ] = *(mDataZ + point_offset);
    rel_pos = pos - mSurfacep->getOriginAgent();
    rel_pos *= 1.f/surface_stride;
    return LLVector2(rel_pos.mV[VX], rel_pos.mV[VY]);
}


void LLSurfacePatch::eval(const U32 x, const U32 y, LLVector3 *vertex, LLVector2 *tex1) const
{
    if (!mSurfacep || !mSurfacep->getRegion() || !mSurfacep->getGridsPerEdge())
    {
        return; // failsafe
    }
    llassert_always(vertex && tex1);

    const F32 meters_per_grid = mSurfacep->getMetersPerGrid();
    const F32 x_region = mOriginRegion.mV[VX] + x * meters_per_grid;
    const F32 y_region = mOriginRegion.mV[VY] + y * meters_per_grid;
    vertex->set(x_region, y_region, *(mDataZ + x + y * mSurfacep->getGridsPerEdge()));

    // The composition is a grid, the noise is a function of world metres.
    const S32 gx = ll_round(mOriginRegion.mV[VX] / meters_per_grid) + (S32)x;
    const S32 gy = ll_round(mOriginRegion.mV[VY] / meters_per_grid) + (S32)y;
    tex1->mV[0] = mSurfacep->getRegion()->getCompositionXY(gx, gy);
    tex1->mV[1] = terrain_composition_noise(mSurfacep->getOriginGlobal().mdV[VX] + x_region,
                                            mSurfacep->getOriginGlobal().mdV[VY] + y_region);
}

F32 terrain_composition_noise(F64 x_global, F64 y_global)
{
    const F32 xyScale = 4.9215f*7.f; //0.93284f;
    const F32 xyScaleInv = (1.f / xyScale)*(0.2222222222f);

    F32 vec[3] = {
                    (F32)fmod((F32)x_global*xyScaleInv, 256.f),
                    (F32)fmod((F32)y_global*xyScaleInv, 256.f),
                    0.f
                };
    return llclamp(noise2(vec)* 0.75f + 0.5f, 0.f, 1.f);
}


void LLSurfacePatch::updateVerticalStats()
{
    if (!mDirtyZStats)
    {
        return;
    }

    U32 grids_per_patch_edge = mSurfacep->getGridsPerPatchEdge();
    U32 grids_per_edge = mSurfacep->getGridsPerEdge();
    F32 meters_per_grid = mSurfacep->getMetersPerGrid();

    U32 i, j, k;
    F32 z, total;

    llassert(mDataZ);
    z = *(mDataZ);

    mMinZ = z;
    mMaxZ = z;

    k = 0;
    total = 0.0f;

    // Iterate to +1 because we need to do the edges correctly.
    for (j=0; j<(grids_per_patch_edge+1); j++)
    {
        for (i=0; i<(grids_per_patch_edge+1); i++)
        {
            z = *(mDataZ + i + j*grids_per_edge);

            if (z < mMinZ)
            {
                mMinZ = z;
            }
            if (z > mMaxZ)
            {
                mMaxZ = z;
            }
            total += z;
            k++;
        }
    }
    mMeanZ = total / (F32) k;
    mCenterRegion.mV[VZ] = 0.5f * (mMinZ + mMaxZ);

    LLVector3 diam_vec(meters_per_grid*grids_per_patch_edge,
                        meters_per_grid*grids_per_patch_edge,
                        mMaxZ - mMinZ);
    mRadius = diam_vec.magVec() * 0.5f;

    mSurfacep->mMaxZ = llmax(mMaxZ, mSurfacep->mMaxZ);
    mSurfacep->mMinZ = llmin(mMinZ, mSurfacep->mMinZ);
    mSurfacep->mHasZData = true;
    mSurfacep->getRegion()->calculateCenterGlobal();

    if (mVObjp)
    {
        mVObjp->dirtyPatch();
    }
    mDirtyZStats = false;
}


void LLSurfacePatch::updateNorthEastCorner()
{
    U32 grids_per_patch_edge = mSurfacep->getGridsPerPatchEdge();
    U32 grids_per_edge = mSurfacep->getGridsPerEdge();

    F32* corner = mDataZ + grids_per_patch_edge + grids_per_patch_edge*grids_per_edge;
    const F32 own_diagonal = *(mDataZ + grids_per_patch_edge - 1 + (grids_per_patch_edge - 1)*grids_per_edge);

    if (!getNeighborPatch(NORTHEAST))
    {
        if (!getNeighborPatch(NORTH))
        {
            if (!getNeighborPatch(EAST))
            {
                // No north or east neighbors.  Pull from the diagonal in your own patch.
                *corner = own_diagonal;
            }
            else
            {
                if (getNeighborPatch(EAST)->getHasReceivedData())
                {
                    // East, but not north.  Pull from your east neighbor's northwest point.
                    *corner = *(getNeighborPatch(EAST)->mDataZ + (grids_per_patch_edge - 1)*grids_per_edge);
                }
                else
                {
                    *corner = own_diagonal;
                }
            }
        }
        else
        {
            // We have a north.
            if (getNeighborPatch(EAST))
            {
                // North and east neighbors, but not northeast.
                // Pull from diagonal in your own patch.
                *corner = own_diagonal;
            }
            else
            {
                if (getNeighborPatch(NORTH)->getHasReceivedData())
                {
                    // North, but not east.  Pull from your north neighbor's southeast corner.
                    *corner = *(getNeighborPatch(NORTH)->mDataZ + (grids_per_patch_edge - 1));
                }
                else
                {
                    *corner = own_diagonal;
                }
            }
        }
    }
    else if (getNeighborPatch(NORTHEAST)->mSurfacep != mSurfacep)
    {
        if (
            (!getNeighborPatch(NORTH) || (getNeighborPatch(NORTH)->mSurfacep != mSurfacep))
            &&
            (!getNeighborPatch(EAST) || (getNeighborPatch(EAST)->mSurfacep != mSurfacep)))
        {
            *corner = *(getNeighborPatch(NORTHEAST)->mDataZ);
        }
    }
    else
    {
        // We've got a northeast patch in the same surface.
        // The z will be handled by that patch.
    }
}


void LLSurfacePatch::updateEastEdge()
{
    U32 grids_per_patch_edge = mSurfacep->getGridsPerPatchEdge();
    U32 grids_per_edge = mSurfacep->getGridsPerEdge();

    U32 j, k;
    F32 *west_surface, *east_surface;

    if (!getNeighborPatch(EAST))
    {
        west_surface = mDataZ + grids_per_patch_edge;
        east_surface = mDataZ + grids_per_patch_edge - 1;
    }
    else if (mConnectedEdge & EAST_EDGE)
    {
        west_surface = mDataZ + grids_per_patch_edge;
        east_surface = getNeighborPatch(EAST)->mDataZ;
    }
    else
    {
        return;
    }

    // If patchp is on the east edge of its surface, then we update the east
    // side buffer
    for (j=0; j < grids_per_patch_edge; j++)
    {
        k = j * grids_per_edge;
        *(west_surface + k) = *(east_surface + k);  // update buffer Z
    }
}


void LLSurfacePatch::updateNorthEdge()
{
    U32 grids_per_patch_edge = mSurfacep->getGridsPerPatchEdge();
    U32 grids_per_edge = mSurfacep->getGridsPerEdge();

    U32 i;
    F32 *south_surface, *north_surface;

    if (!getNeighborPatch(NORTH))
    {
        south_surface = mDataZ + grids_per_patch_edge*grids_per_edge;
        north_surface = mDataZ + (grids_per_patch_edge - 1) * grids_per_edge;
    }
    else if (mConnectedEdge & NORTH_EDGE)
    {
        south_surface = mDataZ + grids_per_patch_edge*grids_per_edge;
        north_surface = getNeighborPatch(NORTH)->mDataZ;
    }
    else
    {
        return;
    }

    // Update patchp's north edge ...
    for (i=0; i<grids_per_patch_edge; i++)
    {
        *(south_surface + i) = *(north_surface + i);    // update buffer Z
    }
}


bool LLSurfacePatch::updateTexture()
{
    if (mSTexUpdate)        //  Update texture as needed
    {
        F32 meters_per_grid = getSurface()->getMetersPerGrid();
        F32 grids_per_patch_edge = (F32)getSurface()->getGridsPerPatchEdge();

        if ((!getNeighborPatch(EAST) || getNeighborPatch(EAST)->getHasReceivedData())
            && (!getNeighborPatch(WEST) || getNeighborPatch(WEST)->getHasReceivedData())
            && (!getNeighborPatch(SOUTH) || getNeighborPatch(SOUTH)->getHasReceivedData())
            && (!getNeighborPatch(NORTH) || getNeighborPatch(NORTH)->getHasReceivedData()))
        {
            LLViewerRegion *regionp = getSurface()->getRegion();
            LLVector3d origin_region = getOriginGlobal() - getSurface()->getOriginGlobal();

            // Have to figure out a better way to deal with these edge conditions...
            LLVLComposition* comp = regionp->getComposition();
            if (!mHeightsGenerated)
            {
                F32 patch_size = meters_per_grid*(grids_per_patch_edge+1);
                if (comp->generateHeights((F32)origin_region[VX], (F32)origin_region[VY],
                                          patch_size, patch_size))
                {
                    mHeightsGenerated = true;
                }
                else
                {
                    return false;
                }
            }

            if (comp->generateComposition())
            {
                if (mVObjp)
                {
                    mVObjp->dirtyGeom();
                    gPipeline.markGLRebuild(mVObjp);
                    return !mSTexUpdate;
                }
            }
        }
        return false;
    }
    else
    {
        return true;
    }
}

void LLSurfacePatch::updateGL()
{
    LL_PROFILE_ZONE_SCOPED;
    updateCompositionStats();
    mSTexUpdate = false;
}

void LLSurfacePatch::dirtyZ()
{
    mSTexUpdate = true;

    // A neighbour's buffer edge, north-east corner and surface-map apron all
    // read this patch's heights.
    for (U32 i = 0; i < 8; i++)
    {
        if (getNeighborPatch(i))
        {
            getNeighborPatch(i)->dirty();
        }
    }

    dirty();
    mLastUpdateTime = gFrameTime;
}


const U64 &LLSurfacePatch::getLastUpdateTime() const
{
    return mLastUpdateTime;
}

F32 LLSurfacePatch::getMaxZ() const
{
    return mMaxZ;
}

F32 LLSurfacePatch::getMinZ() const
{
    return mMinZ;
}

void LLSurfacePatch::setOriginGlobal(const LLVector3d &origin_global)
{
    mOriginGlobal = origin_global;

    LLVector3 origin_region;
    origin_region.setVec(mOriginGlobal - mSurfacep->getOriginGlobal());

    mOriginRegion = origin_region;
    mCenterRegion.mV[VX] = origin_region.mV[VX] + 0.5f*mSurfacep->getGridsPerPatchEdge()*mSurfacep->getMetersPerGrid();
    mCenterRegion.mV[VY] = origin_region.mV[VY] + 0.5f*mSurfacep->getGridsPerPatchEdge()*mSurfacep->getMetersPerGrid();
}

void LLSurfacePatch::connectNeighbor(LLSurfacePatch *neighbor_patchp, const U32 direction)
{
    llassert(neighbor_patchp);

    setNeighborPatch(direction, neighbor_patchp);
    neighbor_patchp->setNeighborPatch(gDirOpposite[direction], this);

    if (EAST == direction)
    {
        mConnectedEdge |= EAST_EDGE;
        neighbor_patchp->mConnectedEdge |= WEST_EDGE;
    }
    else if (NORTH == direction)
    {
        mConnectedEdge |= NORTH_EDGE;
        neighbor_patchp->mConnectedEdge |= SOUTH_EDGE;
    }
    else if (WEST == direction)
    {
        mConnectedEdge |= WEST_EDGE;
        neighbor_patchp->mConnectedEdge |= EAST_EDGE;
    }
    else if (SOUTH == direction)
    {
        mConnectedEdge |= SOUTH_EDGE;
        neighbor_patchp->mConnectedEdge |= NORTH_EDGE;
    }
}

const LLVector3d &LLSurfacePatch::getOriginGlobal() const
{
    return mOriginGlobal;
}

LLVector3 LLSurfacePatch::getOriginAgent() const
{
    return gAgent.getPosAgentFromGlobal(mOriginGlobal);
}

void LLSurfacePatch::setHasReceivedData()
{
    mHasReceivedData = true;
}

bool LLSurfacePatch::getHasReceivedData() const
{
    return mHasReceivedData;
}

const LLVector3 &LLSurfacePatch::getCenterRegion() const
{
    return mCenterRegion;
}


void LLSurfacePatch::updateCompositionStats()
{
    LLViewerLayer *vlp = mSurfacep->getRegion()->getComposition();

    F32 x, y, width, height, mpg, min, mean, max;

    LLVector3 origin = getOriginAgent() - mSurfacep->getOriginAgent();
    mpg = mSurfacep->getMetersPerGrid();
    x = origin.mV[VX];
    y = origin.mV[VY];
    width = mpg*(mSurfacep->getGridsPerPatchEdge()+1);
    height = mpg*(mSurfacep->getGridsPerPatchEdge()+1);

    mean = 0.f;
    min = vlp->getValueScaled(x, y);
    max= min;
    U32 count = 0;
    F32 i, j;
    for (j = 0; j < height; j += mpg)
    {
        for (i = 0; i < width; i += mpg)
        {
            F32 comp = vlp->getValueScaled(x + i, y + j);
            mean += comp;
            min = llmin(min, comp);
            max = llmax(max, comp);
            count++;
        }
    }
    mean /= count;

    mMinComposition = min;
    mMeanComposition = mean;
    mMaxComposition = max;
}

F32 LLSurfacePatch::getMeanComposition() const
{
    return mMeanComposition;
}

F32 LLSurfacePatch::getMinComposition() const
{
    return mMinComposition;
}

F32 LLSurfacePatch::getMaxComposition() const
{
    return mMaxComposition;
}

void LLSurfacePatch::setNeighborPatch(const U32 direction, LLSurfacePatch *neighborp)
{
    mNeighborPatches[direction] = neighborp;
}

LLSurfacePatch *LLSurfacePatch::getNeighborPatch(const U32 direction) const
{
    return mNeighborPatches[direction];
}

void LLSurfacePatch::clearVObj()
{
    mVObjp = NULL;
}
