/**
 * @file alwaterheightbake.h
 * @brief The heights of a water normal map, integrated from its slopes.
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

#include <cstddef>
#include <vector>

/// The heights the water's waves are displaced by, integrated from the slopes of its normal map: no GL and no world, so
/// a worker can bake a map and a test can check one.
///
/// A normal map holds slopes. Its texel decodes as the water shaders decode it, n = rgb * 2 - 1, and the slopes along
/// the map's two axes are g = (-n.x / n.z, -n.y / n.z). The height whose gradient is nearest those slopes in the least
/// squares sense is found in the Fourier domain (Frankot and Chellappa): H(k) = -i (k . G(k)) / (2 pi |k|^2), with k in
/// cycles per repeat on each axis. Its mean is 0, since slopes say nothing of it. Heights are in repeats: a slope of 1
/// held across a whole repeat climbs 1.
///
/// Every field is row-major with x along a row: texel (i, j) is at index j * width + i, at u = i / width and
/// v = j / height. Rows are in the order GL uploads them, so a row's index grows with v, as LLImageRaw holds a texture.
namespace ALWaterHeightBake
{

/// The fewest texels along either axis, and the fewest components a texel has.
constexpr S32 MIN_SIZE = 4;
constexpr S32 MIN_COMPONENTS = 3;

/// A decoded normal's z is held at least this, so a texel lying in the map's plane is a slope of 10, not of infinity.
constexpr F32 MIN_NORMAL_Z = 0.1f;

/// The water's two anisotropic layers read the map twice as often along v as along u (0.1 x 0.2 and 0.45 x 0.9 repeats
/// per metre): in the world, a field's gradient along v counts MID_ASPECT times its gradient along u in the map's units,
/// while the shading tilts by the map's slopes as they are.
constexpr F32 MID_ASPECT = 2.f;

/// Metres of water height per unit of each field: a layer's weight in the water shaders over its repeats per metre
/// along u. The big layer weighs 0.5 at 0.04, the mid layer 0.3 at 0.1.
constexpr F32 BIG_METRES = 12.5f;
constexpr F32 MID_METRES = 3.f;

/// A map's two height fields, width x height each.
struct Heights
{
    S32 mWidth = 0;
    S32 mHeight = 0;
    /// H1, from the slopes as they are: the isotropic big layer's.
    std::vector<F32> mBig;
    /// H2, the anisotropic layers': the field whose gradient in the world, (H_u, MID_ASPECT H_v), is nearest the
    /// slopes, the error counted as the world sees it, H(k) = -i (kx Gx + a ky Gy) / (2 pi (kx^2 + a^2 ky^2)).
    std::vector<F32> mMid;
    /// max |BIG_METRES * H1| and max |MID_METRES * H2|, in metres. 0 where the map was not baked.
    F32 mMaxBig = 0.f;
    F32 mMaxMid = 0.f;

    bool valid() const { return !mBig.empty(); }
};

/// The heights of a normal map: `size` bytes of width x height texels, `components` bytes each, the first three r, g
/// and b, rows packed. A map smaller than MIN_SIZE along either axis, with fewer than MIN_COMPONENTS, shorter than its
/// texels or whose transform fails is logged and left unbaked: the result is empty and its maxima 0. Nothing is thrown.
Heights bake(const U8* pixels, size_t size, S32 width, S32 height, S32 components);

/// The heights of two slope fields given directly, along u and along v, width x height each. False, with `out` empty,
/// wherever bake would leave a map unbaked or a field is not width x height.
bool integrate(const std::vector<F32>& slope_x, const std::vector<F32>& slope_y, S32 width, S32 height, Heights& out);

/// A field taken to its half spectrum and back with the transforms the integration makes: r2c over both axes, then c2r
/// scaled by 1 / (width * height), which returns the field. False, with `field` as it was, where they cannot be made.
bool roundTrip(std::vector<F32>& field, S32 width, S32 height);

/// The most texels a map is baked at along either axis: a larger one is halved, a box of its texels each, until it
/// fits. The waves are read no finer than a vertex step, half a metre at the nearest, which spans five of these texels
/// on the big layer and more on the mid: a finer bake would fill only mips the waves never read.
constexpr S32 MAX_BAKE_SIZE = 256;

/// `pixels`, width x height of `components` bytes each, halved by boxes of two by two until neither side is past
/// `max_size`, into `out`, its size in `out_width` and `out_height`. A side that is odd keeps its last row or column
/// whole in its last box. False, with `out` empty, for no pixels or a size below 1; a map that already fits is copied.
bool fitToSize(const U8* pixels, S32 width, S32 height, S32 components, S32 max_size, std::vector<U8>& out,
               S32& out_width, S32& out_height);

/// The two fields as an RG16F texture takes them, as half floats: H1 then H2 for each texel, rounded to the nearest.
/// Empty for heights not baked.
std::vector<U16> halfTexels(const Heights& heights);

/// A float as a half float, rounded to the nearest, ties to even; past the half's range, infinity, and a NaN stays
/// one.
U16 halfOf(F32 value);

} // namespace ALWaterHeightBake
