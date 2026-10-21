/**
 * @file alhexlattice.h
 * @brief The hex lattice hexLatticeUtil.glsl tiles on, placed in the world on the CPU.
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

#include "llmath.h"
#include "stdtypes.h"

#include <cmath>

namespace ALHexLattice
{
/// The lattice's skew, hex_skew: (x, y) to (x - y / sqrt(3), 2 y / sqrt(3)), where every triangle is half of a unit
/// square.
constexpr F64 SKEW_XY = -0.57735026918962576;
constexpr F64 SKEW_YY = 1.1547005383792515;

/// A point's place on the lattice of `grid` points to the unit: skew(grid * (x, y)), split into the low 32 bits of its
/// floor, as a shader's ivec2 holds them, and its fraction, held below 1 in float. A shader adds the skewed offset of a
/// point from this one to the fraction, and the floor of that to the bits, so a cell keeps its id from wherever it is
/// reached, and no large number reaches the GPU.
inline void place(F64 grid, F64 x, F64 y, S32* base, F32* fraction)
{
    const F64 q[2] = { grid * (x + SKEW_XY * y), grid * (SKEW_YY * y) };
    for (S32 i = 0; i < 2; ++i)
    {
        const F64 whole = std::floor(q[i]);
        base[i] = (S32)(U32)(U64)(S64)whole;
        fraction[i] = llmin((F32)(q[i] - whole), 0.99999994f);
    }
}
} // namespace ALHexLattice
