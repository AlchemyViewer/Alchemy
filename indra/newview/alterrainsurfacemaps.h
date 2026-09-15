/**
 * @file alterrainsurfacemaps.h
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

#pragma once

#include "lldefs.h"
#include "llpointer.h"

#include <functional>
#include <vector>

class LLImageGL;
class LLSurface;

// The terrain surface of one region, uploaded for the GPU to reconstruct.
//
// Two maps of (grids per edge + 2 * APRON) texels a side, texel (u, v)
// holding grid (u - APRON, v - APRON): the height field as R32F, and the
// composition value with the alpha-ramp noise as RG16F. Grid coordinates
// inside the region come from the surface itself; the apron comes from the
// neighbouring surfaces where they are connected and repeats the nearest own
// sample where they are not -- the reach and the clamp of
// LLSurfacePatch::calcNormal, whose two-grid stencil is why the apron is two
// texels. The maps are rebuilt whole and re-uploaded at most once per frame,
// when anything marked them dirty.
class ALTerrainSurfaceMaps
{
public:
    static constexpr S32 APRON = 2;

    explicit ALTerrainSurfaceMaps(LLSurface& surface);
    ~ALTerrainSurfaceMaps();

    void markDirty() { mDirty = true; }
    // Rebuild and upload both maps if anything marked them dirty since the
    // last call. Needs the GL context; the terrain pool calls it before its
    // first draw of the frame.
    void ensureUploaded();
    void bind(S32 height_slot, S32 composition_slot);
    S32  getSize() const { return mSize; }

    // Texel writer for fill(); grid coordinates run [-APRON, grids_per_edge + APRON).
    using Sampler = std::function<void(S32 gx, S32 gy, F32* texel)>;

    // Fill a map's staging buffer, `channels` floats per texel, rows along y.
    // A buffer already the right size keeps whatever channels the sampler
    // leaves alone.
    static void fill(std::vector<F32>& out, S32 grids_per_edge, U32 channels, const Sampler& sample);

    // Which surface a grid coordinate that may lie outside [0, grids_per_edge)
    // is read from: MIDDLE for this one, else the neighbour's direction, with
    // gx and gy rewritten into that surface's grid. A missing neighbour clamps
    // the axis that needed it. x resolves first and y from where x landed,
    // the order LLSurfacePatch::calcNormal walks patches in.
    static U32 resolve(S32& gx, S32& gy, S32 grids_per_edge, const bool (&has_neighbor)[8]);

private:
    static U32 direction(S32 dx, S32 dy);

    void refresh();
    F32  heightAt(S32 gx, S32 gy, const bool (&has_neighbor)[8]) const;
    F32  compositionAt(S32 gx, S32 gy) const;
    F32  noiseAt(S32 gx, S32 gy) const;
    void upload(LLPointer<LLImageGL>& image, S32 internal_format, U32 primary_format, U8 components, const std::vector<F32>& data);

    LLSurface&           mSurface;
    LLPointer<LLImageGL> mHeight;
    LLPointer<LLImageGL> mComposition;
    std::vector<F32>     mHeightStaging;
    std::vector<F32>     mCompositionStaging;
    S32                  mSize = 0;
    bool                 mNoiseReady = false;
    bool                 mDirty = true;
};

inline void ALTerrainSurfaceMaps::fill(std::vector<F32>& out, S32 grids_per_edge, U32 channels, const Sampler& sample)
{
    const S32 size = grids_per_edge + 2 * APRON;
    out.resize(static_cast<size_t>(size) * size * channels);
    F32* texel = out.data();
    for (S32 gy = -APRON; gy < grids_per_edge + APRON; ++gy)
    {
        for (S32 gx = -APRON; gx < grids_per_edge + APRON; ++gx, texel += channels)
        {
            sample(gx, gy, texel);
        }
    }
}

inline U32 ALTerrainSurfaceMaps::direction(S32 dx, S32 dy)
{
    for (U32 dir = 0; dir < 8; ++dir)
    {
        if (gDirAxes[dir][0] == dx && gDirAxes[dir][1] == dy)
        {
            return dir;
        }
    }
    return MIDDLE;
}

inline U32 ALTerrainSurfaceMaps::resolve(S32& gx, S32& gy, S32 grids_per_edge, const bool (&has_neighbor)[8])
{
    // A neighbour's grid 0 is this surface's last column, so a step across
    // the border is one less than the edge.
    const S32 span = grids_per_edge - 1;
    S32 dx = gx < 0 ? -1 : (gx >= grids_per_edge ? 1 : 0);
    S32 dy = gy < 0 ? -1 : (gy >= grids_per_edge ? 1 : 0);
    if (dx != 0 && !has_neighbor[direction(dx, 0)])
    {
        gx = llclamp(gx, 0, span);
        dx = 0;
    }
    if (dy != 0 && !has_neighbor[direction(dx, dy)])
    {
        gy = llclamp(gy, 0, span);
        dy = 0;
    }
    gx -= dx * span;
    gy -= dy * span;
    return direction(dx, dy);
}
