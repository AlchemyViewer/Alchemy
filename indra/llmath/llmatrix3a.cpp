/**
 * @file llmatrix3a.cpp
 * @brief LLMatrix3a class implementation - memory aligned and vectorized 3x3 matrix
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include "linden_common.h"

#include "llmath.h"
#include "alsimdkernels.h"

extern const LLMatrix3a LL_M3A_IDENTITY(
    LLVector4a(1.f, 0.f, 0.f, 0.f),
    LLVector4a(0.f, 1.f, 0.f, 0.f),
    LLVector4a(0.f, 0.f, 1.f, 0.f));

// The direction kernel weights the rows of a 4x4 by x, y and z, which is
// rotate() with a fourth row it never reads.
/*static */void LLMatrix3a::batchTransform( const LLMatrix3a& xform, const LLVector4a* src, int numVectors, LLVector4a* dst )
{
    LLMatrix4a rows;
    rows.setRows(xform.getRow<0>(), xform.getRow<1>(), xform.getRow<2>());
    rows.setRow<3>(LLVector4a::getZero());
    alsimd::transform_directions(rows, src, dst, static_cast<size_t>(numVectors));
}
