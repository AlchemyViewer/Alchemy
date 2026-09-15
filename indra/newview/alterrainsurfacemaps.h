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

#include <cmath>
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

    // The tangent at the middle sample of three, limited so the cubic between
    // it and either neighbour stays monotone (Fritsch-Carlson).
    static F32 limitedSlope(F32 left, F32 mid, F32 right);

    // Cubic Hermite basis at t -- value weights of the two ends, tangent
    // weights -- and its derivative.
    static void hermite(F32 t, F32 (&h)[4], F32 (&d)[4]);

    // The monotone bicubic through the grid samples around (x, y), in grid
    // units, and its gradient per grid: a Hermite patch per cell from the
    // corner values and their limited x and y tangents, held to the corners'
    // range. sample(gx, gy) supplies the heights and owns the reach past the
    // region. The GPU's twin, terrain_height_smooth in terrainSurface.glsl,
    // is this arithmetic in this order.
    template <class Sample>
    static F32 smoothHeight(F32 x, F32 y, const Sample& sample, F32* dzdx, F32* dzdy)
    {
        const S32 ix = (S32)floorf(x);
        const S32 iy = (S32)floorf(y);
        const F32 u = x - (F32)ix;
        const F32 v = y - (F32)iy;

        F32 r0[4], r1[4];
        for (S32 k = 0; k < 4; ++k)
        {
            r0[k] = sample(ix + k - 1, iy);
            r1[k] = sample(ix + k - 1, iy + 1);
        }
        const F32 c0m = sample(ix,     iy - 1);
        const F32 c0p = sample(ix,     iy + 2);
        const F32 c1m = sample(ix + 1, iy - 1);
        const F32 c1p = sample(ix + 1, iy + 2);

        const F32 f00 = r0[1], f10 = r0[2], f01 = r1[1], f11 = r1[2];
        const F32 fx00 = limitedSlope(r0[0], f00, f10);
        const F32 fx10 = limitedSlope(f00, f10, r0[3]);
        const F32 fx01 = limitedSlope(r1[0], f01, f11);
        const F32 fx11 = limitedSlope(f01, f11, r1[3]);
        const F32 fy00 = limitedSlope(c0m, f00, f01);
        const F32 fy01 = limitedSlope(f00, f01, c0p);
        const F32 fy10 = limitedSlope(c1m, f10, f11);
        const F32 fy11 = limitedSlope(f10, f11, c1p);

        F32 hu[4], du[4], hv[4], dv[4];
        hermite(u, hu, du);
        hermite(v, hv, dv);

        const F32 h   = hu[0] * (hv[0] * f00 + hv[1] * f01) + hu[1] * (hv[0] * f10 + hv[1] * f11)
                      + hu[2] * (hv[0] * fx00 + hv[1] * fx01) + hu[3] * (hv[0] * fx10 + hv[1] * fx11)
                      + hu[0] * (hv[2] * fy00 + hv[3] * fy01) + hu[1] * (hv[2] * fy10 + hv[3] * fy11);
        if (dzdx)
        {
            *dzdx = du[0] * (hv[0] * f00 + hv[1] * f01) + du[1] * (hv[0] * f10 + hv[1] * f11)
                  + du[2] * (hv[0] * fx00 + hv[1] * fx01) + du[3] * (hv[0] * fx10 + hv[1] * fx11)
                  + du[0] * (hv[2] * fy00 + hv[3] * fy01) + du[1] * (hv[2] * fy10 + hv[3] * fy11);
        }
        if (dzdy)
        {
            *dzdy = hu[0] * (dv[0] * f00 + dv[1] * f01) + hu[1] * (dv[0] * f10 + dv[1] * f11)
                  + hu[2] * (dv[0] * fx00 + dv[1] * fx01) + hu[3] * (dv[0] * fx10 + dv[1] * fx11)
                  + hu[0] * (dv[2] * fy00 + dv[3] * fy01) + hu[1] * (dv[2] * fy10 + dv[3] * fy11);
        }
        const F32 lo = llmin(llmin(f00, f10), llmin(f01, f11));
        const F32 hi = llmax(llmax(f00, f10), llmax(f01, f11));
        return llclamp(h, lo, hi);
    }

private:
    static U32 direction(S32 dx, S32 dy);

    void refresh();
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

inline F32 ALTerrainSurfaceMaps::limitedSlope(F32 left, F32 mid, F32 right)
{
    const F32 dl = mid - left;
    const F32 dr = right - mid;
    if (dl * dr <= 0.f)
    {
        return 0.f;
    }
    const F32 m = 0.5f * (dl + dr);
    const F32 bound = 3.f * llmin(fabsf(dl), fabsf(dr));
    return llclamp(m, -bound, bound);
}

inline void ALTerrainSurfaceMaps::hermite(F32 t, F32 (&h)[4], F32 (&d)[4])
{
    const F32 t2 = t * t;
    const F32 t3 = t2 * t;
    h[0] =  2.f * t3 - 3.f * t2 + 1.f;
    h[1] = -2.f * t3 + 3.f * t2;
    h[2] =        t3 - 2.f * t2 + t;
    h[3] =        t3 -       t2;
    d[0] =  6.f * t2 - 6.f * t;
    d[1] = -6.f * t2 + 6.f * t;
    d[2] =  3.f * t2 - 4.f * t + 1.f;
    d[3] =  3.f * t2 - 2.f * t;
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
