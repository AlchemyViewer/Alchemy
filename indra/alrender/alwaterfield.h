/**
 * @file alwaterfield.h
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

#pragma once

#include "stdtypes.h"
#include "v2math.h"
#include "v4math.h"

#include <vector>

/// The water's layout on the plane and the phases of its wave coordinates, worked from the camera's global position
/// alone: no GL and no world, so the water's draws and their tests share one definition.
///
/// A square of region-sized cells, 2n + 1 a side, is centred on the camera's cell, one quad a cell at that cell's
/// water height. Around it, rings of twelve quads double in size out to the rim: ring k is the 4x4 grid of quads of
/// side S_k = 2^k S_0 around the square's centre without its middle 2x2, S_0 being the square's half-size, and the last
/// ring is clamped to the rim, S_0 plus the edge stretch. With waves on, the cells within the tessellation reach of the
/// camera are drawn as patches instead.
///
/// Cell coordinates are global, region-grid integers: the camera's moving recentres the square and moves nothing else.
namespace ALWaterField
{

/// The fewest and most cells each side of the centre. The most fits the cell map, MAP_SIZE a side.
constexpr S32 MIN_CELLS = 2;
constexpr S32 MAX_CELLS = 15;
constexpr S32 MAP_SIZE = 32;
static_assert(2 * MAX_CELLS + 1 <= MAP_SIZE, "the square must fit the cell map");

/// Quads in a ring: the 4x4 grid without its middle 2x2.
constexpr S32 RING_QUADS = 12;

/// The waves' tessellation patches along a cell's side (waterSurface.glsl's WATER_PATCHES): 32 m of a 256 m cell.
constexpr S32 PATCHES_PER_CELL = 8;

/// The tessellation reach is where the vertex spacing, distance over density, reaches half the big layer's
/// wavelength, held to this range.
constexpr F64 REACH_SPACING = 12.5;
constexpr F64 MIN_REACH = 256.0;
constexpr F64 MAX_REACH = 4096.0;

/// Wave coordinates per metre of each layer, as the water shaders scale the position: the big layer, then the little
/// map's two reads, xy and zw.
constexpr F64 BIG_SCALE = 0.04;
constexpr F64 LITTLE_XY_SCALE[2] = { 0.45, 0.9 };
constexpr F64 LITTLE_ZW_SCALE[2] = { 0.1, 0.2 };

/// LL's sweep bends x by SWEEP_AMPLITUDE * (cos(x * SWEEP_SCALE[0]) + sin(y * SWEEP_SCALE[1])) before the layers scale it.
constexpr F64 SWEEP_SCALE[2] = { 0.08, 0.02 };
constexpr F64 SWEEP_AMPLITUDE = 6.0;

/// A loaded region: the global cell of its south-west corner, the cells along a side, and its water height.
struct Region
{
    S32 mCellX = 0;
    S32 mCellY = 0;
    S32 mCells = 1;
    F32 mWaterHeight = 0.f;

    bool operator==(const Region&) const = default;
};

/// A rectangle on the plane, in metres from the square's centre.
struct Rect
{
    F64 mMinX = 0.0;
    F64 mMinY = 0.0;
    F64 mMaxX = 0.0;
    F64 mMaxY = 0.0;
};

/// Where everything is for one camera position.
struct Layout
{
    F64 mWidth = 0.0;   ///< a cell's side
    S32 mCentreX = 0;   ///< the camera's cell, global
    S32 mCentreY = 0;
    S32 mCells = 0;     ///< n, the cells each side of the centre
    F64 mHalf = 0.0;    ///< S_0, the square's half-size
    S32 mRings = 0;     ///< K
    F64 mRim = 0.0;     ///< where the outermost ring ends, each way from the square's centre
    F64 mReach = 0.0;   ///< the tessellation reach; 0 with waves off
    /// The cells drawn as patches, global and inclusive: those whose nearest point is nearer the camera than the reach
    /// along both axes. Empty (min past max) with waves off.
    S32 mPatchMinX = 0;
    S32 mPatchMinY = 0;
    S32 mPatchMaxX = -1;
    S32 mPatchMaxY = -1;

    /// The square's centre, global metres.
    F64 centreX() const { return ((F64)mCentreX + 0.5) * mWidth; }
    F64 centreY() const { return ((F64)mCentreY + 0.5) * mWidth; }

    /// The square's side in cells.
    S32 side() const { return 2 * mCells + 1; }

    bool operator==(const Layout&) const = default;
};

/// The global cell holding a global coordinate, held to [-CELL_LIMIT, CELL_LIMIT]: far past any grid, and far enough
/// inside S32 that a square around it does not overflow.
constexpr S32 CELL_LIMIT = 1 << 28;
S32 cellOf(F64 global, F64 width);

/// The reach of waves at a tessellation density, before the layout holds it inside the square.
F64 tessReach(F32 density);

/// The reach across the plane of a camera `height` above (or below) the water: the points of the plane within `reach`
/// of the camera itself, as the tessellation's levels measure distance, lie within this of the point under it. 0 where
/// the camera is `reach` or more from the plane: no point is near enough for a wave to be tessellated.
F64 reachAcross(F64 reach, F64 height);

/// The layout around a camera at global (eye_x, eye_y), for cells `width` wide, a draw distance, an edge stretch, the
/// loaded regions and a tessellation reach (0 with waves off). The square takes in the draw distance, every loaded
/// region and, with waves, the reach and a cell past it, up to MAX_CELLS; the reach is then held to (n - 1) cells, so
/// it ends inside the square whichever way it is measured. A camera or width that is not a finite number, or a width
/// that is not positive, lays out nothing: an empty layout, `width` 0 and no cells or rings.
Layout layout(F64 eye_x, F64 eye_y, F64 width, F32 draw_distance, F64 stretch, const std::vector<Region>& regions,
              F64 reach);

/// The cells drawn as the waves' patches, counted as the cell map counts them (waterSurface.glsl's waterPatchRect):
/// min x, min y, then max x and max y plus one. All 0, empty, without waves.
struct PatchRect
{
    S32 mMinX = 0;
    S32 mMinY = 0;
    S32 mEndX = 0;
    S32 mEndY = 0;

    bool empty() const { return mEndX <= mMinX || mEndY <= mMinY; }
    bool operator==(const PatchRect&) const = default;
};
PatchRect patchRect(const Layout& layout);

/// The order the patches are drawn in for a camera looking along (at_x, at_y) (waterSurface.glsl's waterPatchOrder):
/// the slow axis the one it looks along most, x (1) or y (0), then whether the slow and the fast axis run from their
/// high end, so the patches come front to back as nearly as rows of them allow.
struct PatchOrder
{
    S32 mOrder[4] = { 0, 0, 0, 0 };
};
PatchOrder patchOrder(F32 at_x, F32 at_y);

/// One of the field's draws (waterSurface.glsl's water_vertex): which part of it (waterLayout), whether its vertices
/// make a strip or triangles, and how many vertices each of how many instances has. Every vertex shared between two
/// parts, or between a part and the waves' patches, is the same point in both, worked from the same exact multiples,
/// and lies on no edge it is not a corner of: the field has no T-junction a crack could open along.
struct Draw
{
    enum EPart : S32
    {
        CELLS = 0,   ///< the square's cells, a quad each, but those beside the patches: a four-vertex strip
        RINGS = 1,   ///< the rings' quads, RING_QUAD_TRIANGLES each: those along a finer ring split at the edge's middle
        ZIPPERS = 2, ///< ring 0's four sides, each fanned onto the square's cell corners
        FRINGE = 3,  ///< the cells beside the patches, each fanned onto the patch corners along their shared edge
    };
    EPart mPart = CELLS;
    bool mStrip = false;
    S32 mVertices = 0;
    S32 mInstances = 0;
};

/// A ring quad's triangles: three, the third empty for a quad no finer ring's edge runs along.
constexpr S32 RING_QUAD_TRIANGLES = 3;
/// A fringe cell's: one for each patch along its shared edge, and one more.
constexpr S32 FRINGE_TRIANGLES = PATCHES_PER_CELL + 1;
/// A side of ring 0's: one for each cell along the square's side, and two more.
inline S32 zipperTriangles(const Layout& layout) { return layout.side() + 2; }

/// The field's draws, in order, and how many there are.
struct FieldDraws
{
    Draw mDraws[4];
    S32  mCount = 0;

    void add(const Draw& draw) { mDraws[mCount++] = draw; }
    const Draw* begin() const { return mDraws; }
    const Draw* end() const { return mDraws + mCount; }
};
/// The draws of the field laid out as `layout`, its cells beside `patches` (empty, none) fanned onto their corners.
/// None for an empty layout.
FieldDraws fieldDraws(const Layout& layout, const PatchRect& patches);

/// Cell `i`, `j` of the square, counted from its south-west corner.
Rect cellRect(const Layout& layout, S32 i, S32 j);

/// Quad `quad` of ring `ring`: quads 0 to 3 are the bottom row west to east, 4 to 7 the west and east quads of the two
/// middle rows, bottom first, and 8 to 11 the top row.
Rect ringQuad(const Layout& layout, S32 ring, S32 quad);

/// What waterSurface.glsl's waterCellSquare holds: the square's south-west corner relative to a global origin (the
/// agent region's, for agent space), a cell's width, and the cells a side.
LLVector4 cellSquare(const Layout& layout, F64 origin_x, F64 origin_y);

/// The cell map: MAP_SIZE x MAP_SIZE texels of (water height, flags), rows along y, texel (i, j) the square's cell
/// (i, j). Flag bit 0 says a region is there; a cell with none takes `default_height`, the agent region's. Where two
/// regions claim a cell the first listed has it. Texels past the square are filled the same way and never read.
void fillCells(const Layout& layout, const std::vector<Region>& regions, F32 default_height, std::vector<F32>& texels);

/// How far each wave layer has scrolled at `uptime` seconds, as a fraction of its repeat, for the wave directions an
/// environment sets: the big layer, and the little map's xy and zw reads.
struct WaveScrolls
{
    LLVector2 mBig;
    LLVector4 mLittle;
};
WaveScrolls waveScrolls(F64 uptime, const LLVector2& wave1, const LLVector2& wave2);

/// The phases the wave coordinates take from a camera at global (eye_x, eye_y): each layer's scroll plus the camera's
/// position in that layer, kept to the fraction, and the sweep's phases, kept to a turn. A shader given a position
/// relative to the camera adds these to what it works from that alone, so no large number reaches it and a point keeps
/// its coordinates wherever the camera and the agent's region are.
struct WavePhases
{
    LLVector2 mBig;
    LLVector4 mLittle;
    LLVector2 mSweep;
};
WavePhases wavePhases(F64 eye_x, F64 eye_y, F64 uptime, const LLVector2& wave1, const LLVector2& wave2);

/// The hex lattice a wave layer is tiled on (waterSurface.glsl's WATER_HEX_GRID): its points one repeat of the layer
/// apart, so a cell holds about one whole tile of the author's map. The lattice lies in the layer's scaled coordinates
/// before the sweep and the scroll, still in the world while the waves scroll through it.
constexpr F64 HEX_GRID = 1.0;

/// The lattice's skew, hexLatticeUtil.glsl's hex_skew: (x, y) to (x - y / sqrt(3), 2 y / sqrt(3)), where every
/// triangle is half of a unit square.
constexpr F64 HEX_SKEW_XY = -0.57735026918962576;
constexpr F64 HEX_SKEW_YY = 1.1547005383792515;

/// The camera's place on the lattices of the tiled layers, the big layer and the mid one (the little map's zw read):
/// skew(position scaled to the layer, times HEX_GRID), split into the low 32 bits of its floor and its fraction. A shader
/// adds the skewed offset of a point from the camera to the fraction, and the floor of that to the bits, so a point of
/// the sea keeps its cell's id however far the camera goes, and no large number reaches it.
struct LatticePlaces
{
    S32       mBase[4] = {}; ///< big x, y, mid x, y: the floor's low 32 bits, as a shader's ivec4 holds them
    LLVector4 mFrac;         ///< big x, y, mid x, y: the fraction, below 1
};
LatticePlaces latticePlaces(F64 eye_x, F64 eye_y);

/// The mean of a normal map's decoded x and y (each channel / 255 * 2 - 1), over `width` x `height` pixels of
/// `components` bytes, x the first channel and y the second. Zero for fewer than three components or no pixels.
LLVector2 meanNormal(const U8* pixels, S32 width, S32 height, S32 components);

} // namespace ALWaterField
