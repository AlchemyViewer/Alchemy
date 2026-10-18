/**
 * @file alwaterfield.cpp
 * @brief Where the water is drawn on the plane, and the phases its wave coordinates take from the camera.
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

#include "linden_common.h"

#include "alwaterfield.h"

#include "llmath.h"

#include <bit>
#include <cmath>
#include <cstdlib>

namespace ALWaterField
{

namespace
{
// Whether x is a finite number, from its bits: /fp:fast may take a comparison or isfinite to hold for every value.
bool finite(F64 x)
{
    constexpr U64 EXPONENT = 0x7ff0000000000000ull;
    return (std::bit_cast<U64>(x) & EXPONENT) != EXPONENT;
}

// The most rings the rim takes, 2^MAX_RINGS times the square's half-size: the rim never comes near it.
constexpr S32 MAX_RINGS = 40;

// The wave layers scroll at the wave direction times half the uptime times their rate: the big layer and the little
// map's zw read along wave 1, its xy read along wave 2.
constexpr F64 SCROLL_TIME = 0.5;
constexpr F64 BIG_RATE = 0.055;
constexpr F64 LITTLE_XY_RATE = 0.13;
constexpr F64 LITTLE_ZW_RATE = 0.1;

// In double: a turn rounded to float is off by 1.7e-7, once for every turn the sweep's phase is reduced by, and a
// camera a thousand kilometres from the grid's origin is ten thousand turns in.
constexpr F64 TWO_PI = 6.283185307179586476925;

F64 frac(F64 x)
{
    return x - std::floor(x);
}

F64 scroll(F32 direction, F64 time, F64 rate)
{
    return frac(direction * time * rate);
}

// x into [0, 2 pi).
F64 turn(F64 x)
{
    const F64 t = std::fmod(x, TWO_PI);
    return t < 0.0 ? t + TWO_PI : t;
}

// How many cells from `centre` the farther end of [first, first + count) lies.
S32 reachOf(S32 first, S32 count, S32 centre)
{
    return llmax(std::abs(first - centre), std::abs(first + count - 1 - centre));
}
}

S32 cellOf(F64 global, F64 width)
{
    const F64 cell = std::floor(global / width);
    if (!finite(cell))
    {
        return 0;
    }
    return (S32)llclamp(cell, -(F64)CELL_LIMIT, (F64)CELL_LIMIT);
}

F64 tessReach(F32 density)
{
    return llclamp(REACH_SPACING * (F64)density, MIN_REACH, MAX_REACH);
}

F64 reachAcross(F64 reach, F64 height)
{
    const F64 h = std::fabs(height);
    if (!finite(reach) || !finite(h) || reach <= h)
    {
        return 0.0;
    }
    return std::sqrt((reach - h) * (reach + h));
}

Layout layout(F64 eye_x, F64 eye_y, F64 width, F32 draw_distance, F64 stretch, const std::vector<Region>& regions,
              F64 reach)
{
    Layout out;
    if (!finite(eye_x) || !finite(eye_y) || !finite(width) || width <= 0.0)
    {
        return out;
    }
    // Past the cells' limit the camera is off any grid; it is held there, with the square it centres.
    const F64 limit = (F64)CELL_LIMIT * width;
    eye_x = llclamp(eye_x, -limit, limit);
    eye_y = llclamp(eye_y, -limit, limit);
    const F64 draw = finite(draw_distance) ? llclamp((F64)draw_distance, 0.0, (F64)MAX_CELLS * width) : 0.0;
    stretch = finite(stretch) ? llmax(stretch, 0.0) : 0.0;
    reach = finite(reach) ? llclamp(reach, 0.0, (F64)MAX_CELLS * width) : 0.0;

    out.mWidth = width;
    out.mCentreX = cellOf(eye_x, width);
    out.mCentreY = cellOf(eye_y, width);

    S32 n = (S32)std::ceil((draw + width) / width);
    for (const Region& region : regions)
    {
        n = llmax(n, reachOf(region.mCellX, region.mCells, out.mCentreX));
        n = llmax(n, reachOf(region.mCellY, region.mCells, out.mCentreY));
    }
    if (reach > 0.0)
    {
        n = llmax(n, (S32)std::ceil(reach / width) + 1);
    }
    n = llclamp(n, MIN_CELLS, MAX_CELLS);
    out.mCells = n;
    out.mHalf = ((F64)n + 0.5) * width;

    // Ring k ends at 2^(k + 1) S_0; the last one reaches the rim, where it is clamped.
    out.mRim = out.mHalf + stretch;
    out.mRings = 1;
    while (out.mRings < MAX_RINGS && std::ldexp(out.mHalf, out.mRings) < out.mRim)
    {
        ++out.mRings;
    }

    if (reach > 0.0)
    {
        // The camera is in the centre cell, n cells from the square's edge each way: a reach of n - 1 cells takes in no
        // cell past it, and its fade is 0 before the edge.
        out.mReach = llmin(reach, (F64)(n - 1) * width);
        // A cell is in when its nearest point is nearer than the reach: the cell holding eye - reach is, and the one
        // starting at eye + reach is not.
        out.mPatchMinX = cellOf(eye_x - out.mReach, width);
        out.mPatchMinY = cellOf(eye_y - out.mReach, width);
        // The ceiling of (eye + reach) / width, as minus the floor of its negation, held as cellOf holds a floor.
        out.mPatchMaxX = -cellOf(-(eye_x + out.mReach), width) - 1;
        out.mPatchMaxY = -cellOf(-(eye_y + out.mReach), width) - 1;
    }
    return out;
}

PatchRect patchRect(const Layout& layout)
{
    PatchRect out;
    if (layout.mPatchMaxX < layout.mPatchMinX || layout.mPatchMaxY < layout.mPatchMinY)
    {
        return out;
    }
    const S32 west = layout.mCentreX - layout.mCells;
    const S32 south = layout.mCentreY - layout.mCells;
    out.mMinX = layout.mPatchMinX - west;
    out.mMinY = layout.mPatchMinY - south;
    out.mEndX = layout.mPatchMaxX - west + 1;
    out.mEndY = layout.mPatchMaxY - south + 1;
    return out;
}

PatchOrder patchOrder(F32 at_x, F32 at_y)
{
    // The slow axis the one the view looks along most, run from the end behind the camera, and the fast axis likewise.
    const bool slow_x = std::fabs(at_x) > std::fabs(at_y);
    const F32 along = slow_x ? at_x : at_y;
    const F32 across = slow_x ? at_y : at_x;
    PatchOrder out;
    out.mOrder[0] = slow_x ? 1 : 0;
    out.mOrder[1] = along < 0.f ? 1 : 0;
    out.mOrder[2] = across < 0.f ? 1 : 0;
    return out;
}

FieldDraws fieldDraws(const Layout& layout, const PatchRect& patches)
{
    FieldDraws out;
    if (layout.mCells <= 0)
    {
        return out;
    }
    out.add({ Draw::CELLS, true, 4, layout.side() * layout.side() });
    out.add({ Draw::RINGS, false, 3 * RING_QUAD_TRIANGLES, RING_QUADS * layout.mRings });
    out.add({ Draw::ZIPPERS, false, 3 * zipperTriangles(layout), 4 });
    if (!patches.empty())
    {
        out.add({ Draw::FRINGE, false, 3 * FRINGE_TRIANGLES,
                  2 * ((patches.mEndX - patches.mMinX) + (patches.mEndY - patches.mMinY)) });
    }
    return out;
}

Rect cellRect(const Layout& layout, S32 i, S32 j)
{
    const F64 w = layout.mWidth;
    const F64 x = ((F64)(i - layout.mCells) - 0.5) * w;
    const F64 y = ((F64)(j - layout.mCells) - 0.5) * w;
    return { x, y, x + w, y + w };
}

Rect ringQuad(const Layout& layout, S32 ring, S32 quad)
{
    S32 col, row;
    if (quad < 4)
    {
        col = quad;
        row = 0;
    }
    else if (quad < 8)
    {
        col = (quad & 1) * 3;
        row = 1 + (quad - 4) / 2;
    }
    else
    {
        col = quad - 8;
        row = 3;
    }

    const F64 side = std::ldexp(layout.mHalf, ring);
    const F64 rim = layout.mRim;
    return { llclamp((F64)(col - 2) * side, -rim, rim), llclamp((F64)(row - 2) * side, -rim, rim),
             llclamp((F64)(col - 1) * side, -rim, rim), llclamp((F64)(row - 1) * side, -rim, rim) };
}

LLVector4 cellSquare(const Layout& layout, F64 origin_x, F64 origin_y)
{
    const F64 x = (F64)(layout.mCentreX - layout.mCells) * layout.mWidth;
    const F64 y = (F64)(layout.mCentreY - layout.mCells) * layout.mWidth;
    return LLVector4((F32)(x - origin_x), (F32)(y - origin_y), (F32)layout.mWidth, (F32)layout.side());
}

void fillCells(const Layout& layout, const std::vector<Region>& regions, F32 default_height, std::vector<F32>& texels)
{
    texels.resize((size_t)MAP_SIZE * MAP_SIZE * 2);
    for (size_t t = 0; t < (size_t)MAP_SIZE * MAP_SIZE; ++t)
    {
        texels[t * 2] = default_height;
        texels[t * 2 + 1] = 0.f;
    }
    // Each region stamps the cells of the map it covers, last listed first, so the first listed has a cell two claim.
    const S32 west = layout.mCentreX - layout.mCells;
    const S32 south = layout.mCentreY - layout.mCells;
    for (auto region = regions.rbegin(); region != regions.rend(); ++region)
    {
        const S32 x0 = llmax(region->mCellX - west, 0);
        const S32 y0 = llmax(region->mCellY - south, 0);
        const S32 x1 = llmin(region->mCellX + region->mCells - west, MAP_SIZE);
        const S32 y1 = llmin(region->mCellY + region->mCells - south, MAP_SIZE);
        for (S32 j = y0; j < y1; ++j)
        {
            F32* texel = texels.data() + ((size_t)j * MAP_SIZE + (size_t)x0) * 2;
            for (S32 i = x0; i < x1; ++i, texel += 2)
            {
                texel[0] = region->mWaterHeight;
                texel[1] = 1.f;
            }
        }
    }
}

WaveScrolls waveScrolls(F64 uptime, const LLVector2& wave1, const LLVector2& wave2)
{
    const F64 t = uptime * SCROLL_TIME;
    WaveScrolls out;
    out.mBig.set((F32)scroll(wave1.mV[VX], t, BIG_RATE), (F32)scroll(wave1.mV[VY], t, BIG_RATE));
    out.mLittle.set((F32)scroll(wave2.mV[VX], t, LITTLE_XY_RATE), (F32)scroll(wave2.mV[VY], t, LITTLE_XY_RATE),
                    (F32)scroll(wave1.mV[VX], t, LITTLE_ZW_RATE), (F32)scroll(wave1.mV[VY], t, LITTLE_ZW_RATE));
    return out;
}

WavePhases wavePhases(F64 eye_x, F64 eye_y, F64 uptime, const LLVector2& wave1, const LLVector2& wave2)
{
    const F64 t = uptime * SCROLL_TIME;
    WavePhases out;
    out.mBig.set((F32)frac(BIG_SCALE * eye_x + scroll(wave1.mV[VX], t, BIG_RATE)),
                 (F32)frac(BIG_SCALE * eye_y + scroll(wave1.mV[VY], t, BIG_RATE)));
    out.mLittle.set((F32)frac(LITTLE_XY_SCALE[0] * eye_x + scroll(wave2.mV[VX], t, LITTLE_XY_RATE)),
                    (F32)frac(LITTLE_XY_SCALE[1] * eye_y + scroll(wave2.mV[VY], t, LITTLE_XY_RATE)),
                    (F32)frac(LITTLE_ZW_SCALE[0] * eye_x + scroll(wave1.mV[VX], t, LITTLE_ZW_RATE)),
                    (F32)frac(LITTLE_ZW_SCALE[1] * eye_y + scroll(wave1.mV[VY], t, LITTLE_ZW_RATE)));
    out.mSweep.set((F32)turn(SWEEP_SCALE[0] * eye_x), (F32)turn(SWEEP_SCALE[1] * eye_y));
    return out;
}

namespace
{
// One layer's lattice place for a position already scaled to the layer: its skewed coordinates' floor, as the low 32
// bits a shader's ivec2 holds, and their fraction, held below 1 in float.
void latticePlace(F64 x, F64 y, S32* base, F32* fraction)
{
    const F64 q[2] = { HEX_GRID * (x + HEX_SKEW_XY * y), HEX_GRID * (HEX_SKEW_YY * y) };
    for (S32 i = 0; i < 2; ++i)
    {
        const F64 whole = std::floor(q[i]);
        base[i] = (S32)(U32)(U64)(S64)whole;
        fraction[i] = llmin((F32)(q[i] - whole), 0.99999994f);
    }
}
} // namespace

LatticePlaces latticePlaces(F64 eye_x, F64 eye_y)
{
    LatticePlaces out;
    F32 fraction[4];
    latticePlace(BIG_SCALE * eye_x, BIG_SCALE * eye_y, out.mBase, fraction);
    latticePlace(LITTLE_ZW_SCALE[0] * eye_x, LITTLE_ZW_SCALE[1] * eye_y, out.mBase + 2, fraction + 2);
    out.mFrac.set(fraction[0], fraction[1], fraction[2], fraction[3]);
    return out;
}

LLVector2 meanNormal(const U8* pixels, S32 width, S32 height, S32 components)
{
    if (!pixels || width <= 0 || height <= 0 || components < 3)
    {
        return LLVector2::zero;
    }
    U64 sum[2] = {};
    const size_t count = (size_t)width * (size_t)height;
    for (size_t i = 0; i < count; ++i)
    {
        sum[0] += pixels[i * components];
        sum[1] += pixels[i * components + 1];
    }
    const F64 scale = 2.0 / (255.0 * (F64)count);
    return LLVector2((F32)((F64)sum[0] * scale - 1.0), (F32)((F64)sum[1] * scale - 1.0));
}

} // namespace ALWaterField
