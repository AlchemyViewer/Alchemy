/**
 * @file llsurfacepatch.h
 * @brief LLSurfacePatch class definition
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

#ifndef LL_LLSURFACEPATCH_H
#define LL_LLSURFACEPATCH_H

#include "v3math.h"
#include "v3dmath.h"
#include "llpointer.h"

class LLSurface;
class LLVOSurfacePatch;
class LLVector2;
class LLColor4U;
class LLAgent;

// The per-vertex random that dithers the composition alpha ramp, keyed on the
// world position so neighbouring regions agree along their border.
F32 terrain_composition_noise(F64 x_global, F64 y_global);

class LLSurfacePatch
{
public:
    LLSurfacePatch();
    ~LLSurfacePatch();

    void reset(const U32 id);
    void connectNeighbor(LLSurfacePatch *neighborp, const U32 direction);
    void disconnectNeighbor(LLSurface *surfacep);

    void setNeighborPatch(const U32 direction, LLSurfacePatch *neighborp);
    LLSurfacePatch *getNeighborPatch(const U32 direction) const;

    void colorPatch(const U8 r, const U8 g, const U8 b);

    bool updateTexture();

    void updateVerticalStats();
    void updateCompositionStats();

    void updateEastEdge();
    void updateNorthEdge();
    // The sample at (grids per patch edge, grids per patch edge) is the meeting
    // point of the east column and the north row, which neither edge update
    // writes: from the diagonal region when one is connected, from whichever
    // of north and east exists and has data, else the patch's own diagonal.
    void updateNorthEastCorner();

    void updateGL();

    void dirtyZ(); // Dirty the z values of this patch
    void setHasReceivedData();
    bool getHasReceivedData() const;

    F32 getMaxZ() const;
    F32 getMinZ() const;
    F32 getMeanComposition() const;
    F32 getMinComposition() const;
    F32 getMaxComposition() const;
    const LLVector3 &getCenterRegion() const;
    const U64 &getLastUpdateTime() const;
    LLSurface *getSurface() const { return mSurfacep; }
    LLVector3 getPointAgent(const U32 x, const U32 y) const; // get the point at the offset.
    LLVector2 getTexCoords(const U32 x, const U32 y) const;

    // A grid point of the patch: region-local position, and the composition
    // value with its alpha-ramp noise -- what the heightmap-with-noise paint
    // mode reads per vertex. The paint-map bake builds its full-resolution
    // mesh from this.
    void eval(const U32 x, const U32 y, LLVector3 *vertex, LLVector2 *tex1) const;

    LLVector3 getOriginAgent() const;
    const LLVector3d &getOriginGlobal() const;
    const LLVector3 &getOriginRegion() const        { return mOriginRegion; }
    void setOriginGlobal(const LLVector3d &origin_global);

    // connectivity -- each LLPatch points at 5 neighbors (or NULL)
    // +---+---+---+
    // |   | 2 | 5 |
    // +---+---+---+
    // | 3 | 0 | 1 |
    // +---+---+---+
    // | 6 | 4 |   |
    // +---+---+---+


    void setSurface(LLSurface *surfacep);
    void setDataZ(F32 *data_z)                  { mDataZ = data_z; }
    F32 *getDataZ() const                       { return mDataZ; }

    void dirty();           // Mark this surface patch as dirty...
    void clearDirty()                           { mDirty = false; }

    bool isHeightsGenerated() const { return mHeightsGenerated; }

    void clearVObj();

public:
    bool mHasReceivedData;  // has the patch EVER received height data?
    bool mSTexUpdate;       // Does the surface texture need to be updated?

protected:
    LLSurfacePatch *mNeighborPatches[8]; // Adjacent patches

    bool mDirty;
    bool mDirtyZStats;
    bool mHeightsGenerated;

    U32 mDataOffset;
    F32 *mDataZ;

    // Pointer to the LLVOSurfacePatch object which is used in the new renderer.
    LLPointer<LLVOSurfacePatch> mVObjp;

    // pointers to beginnings of patch data fields
    LLVector3d mOriginGlobal;
    LLVector3 mOriginRegion;


    // height field stats
    LLVector3 mCenterRegion; // Center in region-local coords
    F32 mMinZ, mMaxZ, mMeanZ;
    F32 mRadius;

    F32 mMinComposition;
    F32 mMaxComposition;
    F32 mMeanComposition;

    U8 mConnectedEdge;      // This flag is non-zero iff patch is on at least one edge
                            // of LLSurface that is "connected" to another LLSurface
    U64 mLastUpdateTime;    // Time patch was last updated

    LLSurface *mSurfacep; // Pointer to "parent" surface
};



#endif // LL_LLSURFACEPATCH_H
