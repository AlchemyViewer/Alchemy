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

#include "llmemory.h"
#include "llmath.h"
#include "llquantize.h"
#include "llthread.h"

extern const LLVector4a LL_V4A_ZERO(0.f, 0.f, 0.f, 0.f);
extern const LLVector4a LL_V4A_EPSILON(F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO);

/*static */void LLVector4a::initClass()
{
    set_thread_fp_mode();
}

/*static */void LLVector4a::memcpyNonAliased16(F32* __restrict dst, const F32* __restrict src, size_t bytes)
{
        ll_memcpy_nonaliased_aligned_16((char*)dst, (char*)src, bytes);
}

void LLVector4a::setRotated( const LLQuaternion2& quat, const LLVector4a& vec )
{
    const LLVector4a& quatVec = quat.getVector4a();
    LLVector4a temp; temp.setCross3(quatVec, vec);
    temp.add( temp );

    const LLVector4a realPart( quatVec.getScalarAt<3>() );
    LLVector4a tempTimesReal; tempTimesReal.setMul( temp, realPart );

    mQ = vec;
    add( tempTimesReal );

    LLVector4a imagCrossTemp; imagCrossTemp.setCross3( quatVec, temp );
    add(imagCrossTemp);
}

// Both quantizers map [low, high] onto the integers 0..max and back, so the
// result is the value the stored integer will read back as. A channel with
// low == high has nothing to quantize: its reciprocal is taken of 1 instead
// of 0, and the multiply by the real, zero delta lands it on low.
void LLVector4a::quantize8( const LLVector4a& low, const LLVector4a& high )
{
    LLVector4a val(mQ);
    LLVector4a delta; delta.setSub( high, low );

    const LLVector4Logical zeroDelta = delta.equal(LLVector4a::getZero());
    LLVector4a safeDelta;
    safeDelta.setSelectWithMask(zeroDelta, LLVector4a(1.f), delta);

    const LLVector4a vU8Max(255.f);
    const LLVector4a vOOU8Max(OOU8MAX);

    {
        val.clamp(low, high);
        val.sub(low);

        // Eight bits of result need eleven of reciprocal
        const LLVector4a oneOverDelta(alsimd::rcp_fast(safeDelta.mQ));

        val.mul(oneOverDelta);
        val.mul(vU8Max);
    }

    val = alsimd::round(val.mQ);

    {
        val.mul(vOOU8Max);
        val.mul(delta);
        val.add(low);
    }

    {
        LLVector4a maxError; maxError.setMul(delta, vOOU8Max);
        LLVector4a absVal; absVal.setAbs( val );
        setSelectWithMask( absVal.lessThan( maxError ), LLVector4a::getZero(), val );
    }
}

void LLVector4a::quantize16( const LLVector4a& low, const LLVector4a& high )
{
    LLVector4a val(mQ);
    LLVector4a delta; delta.setSub( high, low );

    const LLVector4Logical zeroDelta = delta.equal(LLVector4a::getZero());
    LLVector4a safeDelta;
    safeDelta.setSelectWithMask(zeroDelta, LLVector4a(1.f), delta);

    const LLVector4a vU16Max(65535.f);
    const LLVector4a vOOU16Max(OOU16MAX);

    {
        val.clamp(low, high);
        val.sub(low);

        // Sixteen bits of result need the refined reciprocal
        const LLVector4a oneOverDelta(alsimd::rcp(safeDelta.mQ));

        val.mul(oneOverDelta);
        val.mul(vU16Max);
    }

    val = alsimd::round(val.mQ);

    {
        val.mul(vOOU16Max);
        val.mul(delta);
        val.add(low);
    }

    {
        LLVector4a maxError; maxError.setMul(delta, vOOU16Max);
        LLVector4a absVal; absVal.setAbs( val );
        setSelectWithMask( absVal.lessThan( maxError ), LLVector4a::getZero(), val );
    }
}
