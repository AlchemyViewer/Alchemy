/**
 * @file alwatersurface.h
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

#pragma once

#include "alwaterfield.h"

#include "indra_constants.h"
#include "llpointer.h"

#include <vector>

class LLCamera;
class LLGLSLShader;
class LLImageGL;
class LLVector3;

/// The water's field for the frame: ALWaterField's layout around the main camera, and the cell map the water's shaders
/// read the heights from, ALWaterField::MAP_SIZE texels a side of RG32F (height, flags) as fillCells lays them out.
/// The water is drawn from these alone (draw), with no vertex buffer, object or spatial partition.
/// The map is refilled when what it holds changes -- the loaded regions, their water heights, the agent region's
/// height that empty cells take, or the square under the camera -- which a key of those, compared each frame, finds
/// without a trigger at each place they change.
///
/// Owned by LLPipeline. Its texture goes with the pipeline's other GL objects and is made again on the next update.
class ALWaterSurface
{
public:
    /// Lays the field out around the main camera and refills the map if its key moved. Once a main frame, at its start,
    /// before anything draws or fits to the water, with the context current; without an agent region nothing changes,
    /// and with RenderWater off there is no field.
    void update();

    /// Whether there is a field to draw.
    bool exists() const { return mCellMap.notNull(); }

    /// Releases the map's texture.
    void release();

    /// Binds the cell map and sets the uniforms environment/waterSurface.glsl reads on `shader`, bound: the square,
    /// the rings, the agent region's height, and whether the void is drawn (RENDER_TYPE_VOIDWATER). Before the first
    /// update with an agent region, every point takes the agent region's height.
    void bind(LLGLSLShader& shader) const;

    /// Draws the field with `shader`, bound and bound to the field (bind): ALWaterField::fieldDraws, instances whose
    /// vertices the vertex stage places (water_vertex). Nothing before the first update.
    void draw(LLGLSLShader& shader) const;

    /// The field's outer rectangle, horizontally, relative to `eye` in agent space: the rim each way from the square's
    /// centre. False before the first update.
    bool rim(const LLVector3& eye, LLVector2& lo, LLVector2& hi) const;

    /// Whether `camera`'s frustum meets the field at all: its slab, from rim to rim and from its lowest water to its
    /// highest. False only where the view misses the whole sea, as looking up.
    bool inView(LLCamera& camera) const;

    /// Grows `min` and `max` (agent space) to hold the field: its rim, and its lowest and highest water.
    void addExtents(LLVector3& min, LLVector3& max) const;

    const ALWaterField::Layout& getLayout() const { return mLayout; }

    /// The cell map, null until the first update with an agent region.
    LLImageGL* getCellMap() const { return mCellMap; }

private:
    struct Key
    {
        S32                               mCentreX = 0;
        S32                               mCentreY = 0;
        S32                               mCells = 0;
        F32                               mDefaultHeight = 0.f;
        std::vector<ALWaterField::Region> mRegions;

        bool operator==(const Key&) const = default;
    };

    ALWaterField::Layout mLayout;
    LLVector4            mSquare;
    LLVector2            mCentre;
    F32                  mDefaultHeight = DEFAULT_WATER_HEIGHT;
    F32                  mLowest = DEFAULT_WATER_HEIGHT;
    F32                  mHighest = DEFAULT_WATER_HEIGHT;
    // RENDER_TYPE_VOIDWATER as the frame began: the passes that draw the water run under render-type masks that
    // leave it out.
    bool                 mVoidDrawn = true;
    Key                  mKey;
    Key                  mScratch;
    std::vector<F32>     mTexels;
    LLPointer<LLImageGL> mCellMap;
};
