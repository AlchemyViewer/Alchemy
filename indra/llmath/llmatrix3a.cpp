/**
 * @file llvector4a.cpp
 * @brief SIMD vector implementation
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

// Build the identity LLMatrix3a column-by-column rather than reinterpreting
// an F32 array: LLMatrix3a stores LLVector4a columns (each wrapping __m128),
// so casting F32[12] to LLMatrix3a is strict-aliasing UB that GCC flags at
// -Wstrict-aliasing=2.
extern const LLMatrix3a LL_M3A_IDENTITY(
    LLVector4a(1.f, 0.f, 0.f, 0.f),
    LLVector4a(0.f, 1.f, 0.f, 0.f),
    LLVector4a(0.f, 0.f, 1.f, 0.f));

void LLMatrix3a::setMul( const LLMatrix3a& lhs, const LLMatrix3a& rhs )
{
    const LLVector4a col0 = lhs.getColumn(0);
    const LLVector4a col1 = lhs.getColumn(1);
    const LLVector4a col2 = lhs.getColumn(2);

    for ( int i = 0; i < 3; i++ )
    {
        // Value-copy the column so the splats access it through LLVector4a
        // (its real type) instead of via _mm_load_ss((F32*)getF32ptr()),
        // which puns __m128 → F32* and is unsafe on sse2neon AArch64.
        const LLVector4a v = rhs.mColumns[i];

        LLVector4a xxxx; xxxx.splat<0>(v); xxxx.mul(col0);
        LLVector4a yyyy; yyyy.splat<1>(v); yyyy.mul(col1);
        LLVector4a zzzz; zzzz.splat<2>(v); zzzz.mul(col2);

        xxxx.add(yyyy);
        xxxx.add(zzzz);

        mColumns[i] = xxxx;
    }
}

/*static */void LLMatrix3a::batchTransform( const LLMatrix3a& xform, const LLVector4a* src, int numVectors, LLVector4a* dst )
{
    const LLVector4a col0 = xform.getColumn(0);
    const LLVector4a col1 = xform.getColumn(1);
    const LLVector4a col2 = xform.getColumn(2);
    const LLVector4a* maxAddr = src + numVectors;

    // Each iteration value-copies the source LLVector4a(s) and splats the
    // x/y/z lanes via splat<N>, which goes through LLVector4a's real type
    // (no F32* pun of __m128 storage — unsafe under sse2neon on AArch64).

    if ( numVectors & 0x1 )
    {
        const LLVector4a v = *src;

        LLVector4a x; x.splat<0>(v); x.mul(col0);
        LLVector4a y; y.splat<1>(v); y.mul(col1);
        LLVector4a z; z.splat<2>(v); z.mul(col2);
        x.add(y);
        x.add(z);

        *dst = x;
        src++;
        dst++;
    }

    while ( src < maxAddr )
    {
        _mm_prefetch( (const char*)(src + 32), _MM_HINT_NTA );
        _mm_prefetch( (const char*)(dst + 32), _MM_HINT_NTA );

        const LLVector4a v0 = src[0];
        const LLVector4a v1 = src[1];

        LLVector4a x0, y0, z0;
        LLVector4a x1, y1, z1;
        x0.splat<0>(v0); y0.splat<1>(v0); z0.splat<2>(v0);
        x1.splat<0>(v1); y1.splat<1>(v1); z1.splat<2>(v1);
        x0.mul(col0); y0.mul(col1); z0.mul(col2);
        x1.mul(col0); y1.mul(col1); z1.mul(col2);
        x0.add(y0); x0.add(z0);
        x1.add(y1); x1.add(z1);

        dst[0] = x0;
        dst[1] = x1;
        src += 2;
        dst += 2;
    }
}
