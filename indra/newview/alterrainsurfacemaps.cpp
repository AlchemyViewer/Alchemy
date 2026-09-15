/**
 * @file alterrainsurfacemaps.cpp
 * @brief A region's terrain as textures: the height field, and the
 *        composition with its noise, each with an apron from the
 *        neighbouring regions.
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

#include "alterrainsurfacemaps.h"

#include "llimagegl.h"
#include "llrender.h"
#include "llsurface.h"
#include "llsurfacepatch.h"
#include "llviewerregion.h"

ALTerrainSurfaceMaps::ALTerrainSurfaceMaps(LLSurface& surface) :
    mSurface(surface)
{
}

ALTerrainSurfaceMaps::~ALTerrainSurfaceMaps() = default;

void ALTerrainSurfaceMaps::ensureUploaded()
{
    if (!mDirty || !mSurface.hasZData())
    {
        return;
    }
    refresh();
    mDirty = false;
}

void ALTerrainSurfaceMaps::bind(S32 height_slot, S32 composition_slot)
{
    // A slot below zero is a program with no such sampler: the shadow stages
    // never read the composition.
    if (height_slot >= 0 && mHeight.notNull())
    {
        gGL.getTextureSlot(height_slot)->bindSampled(mHeight.get(), ALSamplers::PointClamp);
    }
    if (composition_slot >= 0 && mComposition.notNull())
    {
        gGL.getTextureSlot(composition_slot)->bindSampled(mComposition.get(), ALSamplers::BilinearClamp);
    }
}

void ALTerrainSurfaceMaps::refresh()
{
    LL_PROFILE_ZONE_SCOPED;
    const S32 grids_per_edge = mSurface.getGridsPerEdge();
    mSize = grids_per_edge + 2 * APRON;

    bool has_neighbor[8];
    for (U32 dir = 0; dir < 8; ++dir)
    {
        has_neighbor[dir] = mSurface.mNeighbors[dir] != nullptr;
    }
    fill(mHeightStaging, grids_per_edge, 1, [&](S32 gx, S32 gy, F32* texel)
    {
        texel[0] = mSurface.sampleZ(gx, gy, has_neighbor);
    });

    // The noise is a function of world position alone, so it is computed once
    // per region and kept in the staging buffer across refreshes.
    const bool noise_ready = mNoiseReady;
    fill(mCompositionStaging, grids_per_edge, 2, [&](S32 gx, S32 gy, F32* texel)
    {
        texel[0] = compositionAt(gx, gy);
        if (!noise_ready)
        {
            texel[1] = noiseAt(gx, gy);
        }
    });
    mNoiseReady = true;

    upload(mHeight, GL_R32F, GL_RED, 1, mHeightStaging);
    upload(mComposition, GL_RG16F, GL_RG, 2, mCompositionStaging);
}

F32 ALTerrainSurfaceMaps::compositionAt(S32 gx, S32 gy) const
{
    // The composition has no apron of its own; the map is only ever sampled
    // inside the region, and getCompositionXY already reaches east and north
    // for the buffer column and row.
    const LLViewerRegion* region = mSurface.getRegion();
    if (!region)
    {
        return 0.f;
    }
    const S32 last = mSurface.getGridsPerEdge() - 1;
    return region->getCompositionXY(llclamp(gx, 0, last), llclamp(gy, 0, last));
}

F32 ALTerrainSurfaceMaps::noiseAt(S32 gx, S32 gy) const
{
    const LLVector3d& origin = mSurface.getOriginGlobal();
    return terrain_composition_noise(origin.mdV[VX] + gx, origin.mdV[VY] + gy);
}

void ALTerrainSurfaceMaps::upload(LLPointer<LLImageGL>& image, S32 internal_format, U32 primary_format, U8 components, const std::vector<F32>& data)
{
    const U8* bytes = reinterpret_cast<const U8*>(data.data());
    if (image.isNull())
    {
        image = new LLImageGL(mSize, mSize, components, /*usemipmaps=*/false);
        image->setExplicitFormat(internal_format, primary_format, GL_FLOAT);
        image->createGLTexture(0, bytes);
    }
    else
    {
        image->setImage(bytes);
        gGL.getTextureSlot(0)->unbind();
    }
}
