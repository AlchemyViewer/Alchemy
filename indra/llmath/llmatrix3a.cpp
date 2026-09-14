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

extern const LLMatrix3a LL_M3A_IDENTITY(
    LLVector4a(1.f, 0.f, 0.f, 0.f),
    LLVector4a(0.f, 1.f, 0.f, 0.f),
    LLVector4a(0.f, 0.f, 1.f, 0.f));

/*static */void LLMatrix3a::batchTransform( const LLMatrix3a& xform, const LLVector4a* src, int numVectors, LLVector4a* dst )
{
    const LLQuad col0 = xform.getColumn(0);
    const LLQuad col1 = xform.getColumn(1);
    const LLQuad col2 = xform.getColumn(2);
    const LLVector4a* maxAddr = src + numVectors;

    if ( numVectors & 0x1 )
    {
        const LLQuad v = *src;
        LLQuad x = alsimd::mul(alsimd::splat<0>(v), col0);
        x = alsimd::fmadd_lane<1>(col1, v, x);
        *dst = alsimd::fmadd_lane<2>(col2, v, x);
        src++;
        dst++;
    }

    while ( src < maxAddr )
    {
        alsimd::prefetch_nta(src + 32);

        const LLQuad v0 = src[0];
        const LLQuad v1 = src[1];

        LLQuad x0 = alsimd::mul(alsimd::splat<0>(v0), col0);
        LLQuad x1 = alsimd::mul(alsimd::splat<0>(v1), col0);
        x0 = alsimd::fmadd_lane<1>(col1, v0, x0);
        x1 = alsimd::fmadd_lane<1>(col1, v1, x1);
        dst[0] = alsimd::fmadd_lane<2>(col2, v0, x0);
        dst[1] = alsimd::fmadd_lane<2>(col2, v1, x1);
        src += 2;
        dst += 2;
    }
}
