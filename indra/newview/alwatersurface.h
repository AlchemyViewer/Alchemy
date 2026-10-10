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

class LLGLSLShader;
class LLImageGL;

/// The water's field for the frame: ALWaterField's layout around the main camera, and the cell map the water's shaders
/// read the heights from, ALWaterField::MAP_SIZE texels a side of RG32F (height, flags) as fillCells lays them out.
/// The map is refilled when what it holds changes -- the loaded regions, their water heights, the agent region's
/// height that empty cells take, or the square under the camera -- which a key of those, compared each frame, finds
/// without a trigger at each place they change.
///
/// Owned by LLPipeline. Its texture goes with the pipeline's other GL objects and is made again on the next update.
class ALWaterSurface
{
public:
    /// Lays the field out around the main camera and refills the map if its key moved. Once a main frame, at its start,
    /// before anything draws or fits to the water, with the context current; without an agent region there is no field
    /// and nothing changes.
    void update();

    /// Releases the map's texture.
    void release();

    /// Binds the cell map and sets the uniforms environment/waterSurface.glsl reads on `shader`, bound. Before the
    /// first update with an agent region, every point takes the agent region's height.
    void bind(LLGLSLShader& shader) const;

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
    F32                  mDefaultHeight = DEFAULT_WATER_HEIGHT;
    Key                  mKey;
    Key                  mScratch;
    std::vector<F32>     mTexels;
    LLPointer<LLImageGL> mCellMap;
};
