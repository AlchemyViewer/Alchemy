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

extern const LLQuad F_ZERO_4A       = { 0, 0, 0, 0 };
extern const LLQuad F_APPROXIMATELY_ZERO_4A = {
    F_APPROXIMATELY_ZERO,
    F_APPROXIMATELY_ZERO,
    F_APPROXIMATELY_ZERO,
    F_APPROXIMATELY_ZERO
};

// Construct from the LLQuad (i.e. __m128) value directly. The previous
// reinterpret_cast<const LLVector4a&>(LLQuad) was strict-aliasing UB; the
// LLQuad ctor is a plain value copy of the __m128 into mQ.
extern const LLVector4a LL_V4A_ZERO(F_ZERO_4A);
extern const LLVector4a LL_V4A_EPSILON(F_APPROXIMATELY_ZERO_4A);

/*static */void LLVector4a::memcpyNonAliased16(F32* __restrict dst, const F32* __restrict src, size_t bytes)
{
        ll_memcpy_nonaliased_aligned_16((char*)dst, (char*)src, bytes);
}

void LLVector4a::setRotated( const LLRotation& rot, const LLVector4a& vec )
{
    const LLVector4a col0 = rot.getColumn(0);
    const LLVector4a col1 = rot.getColumn(1);
    const LLVector4a col2 = rot.getColumn(2);

    LLVector4a result = _mm_load_ss( vec.getF32ptr() );
    result.splat<0>( result );
    result.mul( col0 );

    {
        LLVector4a yyyy = _mm_load_ss( vec.getF32ptr() +  1 );
        yyyy.splat<0>( yyyy );
        yyyy.mul( col1 );
        result.add( yyyy );
    }

    {
        LLVector4a zzzz = _mm_load_ss( vec.getF32ptr() +  2 );
        zzzz.splat<0>( zzzz );
        zzzz.mul( col2 );
        result.add( zzzz );
    }

    *this = result;
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

void LLVector4a::quantize8( const LLVector4a& low, const LLVector4a& high )
{
    LLVector4a val(mQ);
    LLVector4a delta; delta.setSub( high, low );

    // Guard against low==high in any channel: 1/delta would be +Inf and
    // val * Inf below produces NaN (val - low is 0 for that channel after
    // clamp, so we hit 0 * Inf). Substituting 1 for zero-delta deltas in
    // the reciprocal step gives 0/1 = 0 there; the later `val.mul(delta)`
    // step still uses the real delta, which is 0, so the zero-delta
    // channels collapse to `low` (= high) -- the only sensible answer for
    // a degenerate range. Under -ffast-math the compiler is allowed to
    // assume no NaN/Inf, so this fix is also a correctness barrier.
    const LLVector4Logical zeroDelta = delta.equal(LLVector4a::getZero());
    LLVector4a safeDelta;
    LLVector4a one; one.splat(1.f);
    safeDelta.setSelectWithMask(zeroDelta, one, delta);

    // Load the F32[4] constants through LLVector4a::load4a instead of
    // reinterpret_cast<const LLVector4a*>: F_U8MAX_4A is a plain F32 array
    // and the SSE intrinsic-ABI alias exemption only covers __m128, not
    // LLVector4a, so the cast trips -Wstrict-aliasing=2.
    LLVector4a vU8Max;   vU8Max.load4a(F_U8MAX_4A);
    LLVector4a vOOU8Max; vOOU8Max.load4a(F_OOU8MAX_4A);

    {
        val.clamp(low, high);
        val.sub(low);

        // 8-bit quantization means we can do with just 12 bits of reciprocal accuracy
        const LLVector4a oneOverDelta = _mm_rcp_ps(safeDelta.mQ);
//      {
//          alignas(16) static const F32 F_TWO_4A[4] = { 2.f, 2.f, 2.f, 2.f };
//          LLVector4a two; two.load4a( F_TWO_4A );
//
//          // Here we use _mm_rcp_ps plus one round of newton-raphson
//          // We wish to find 'x' such that x = 1/delta
//          // As a first approximation, we take x0 = _mm_rcp_ps(delta)
//          // Then x1 = 2 * x0 - a * x0^2 or x1 = x0 * ( 2 - a * x0 )
//          // See Intel AP-803 http://ompf.org/!/Intel_application_note_AP-803.pdf
//          const LLVector4a recipApprox = _mm_rcp_ps(delta.mQ);
//          oneOverDelta.setMul( delta, recipApprox );
//          oneOverDelta.setSub( two, oneOverDelta );
//          oneOverDelta.mul( recipApprox );
//      }

        val.mul(oneOverDelta);
        val.mul(vU8Max);
    }

    val = _mm_cvtepi32_ps(_mm_cvtps_epi32( val.mQ ));

    {
        val.mul(vOOU8Max);
        val.mul(delta);
        val.add(low);
    }

    {
        LLVector4a maxError; maxError.setMul(delta, vOOU8Max);
        LLVector4a absVal; absVal.setAbs( val );
        setSelectWithMask( absVal.lessThan( maxError ), F_ZERO_4A, val );
    }
}

void LLVector4a::quantize16( const LLVector4a& low, const LLVector4a& high )
{
    LLVector4a val(mQ);
    LLVector4a delta; delta.setSub( high, low );

    // See quantize8 for the rationale: substitute 1 for zero-delta channels
    // so 1/delta is finite, then the later `val.mul(delta)` collapses those
    // channels back to `low`.
    const LLVector4Logical zeroDelta = delta.equal(LLVector4a::getZero());
    LLVector4a safeDelta;
    LLVector4a one; one.splat(1.f);
    safeDelta.setSelectWithMask(zeroDelta, one, delta);

    // Load the F32[4] constants through LLVector4a::load4a -- see quantize8.
    LLVector4a vU16Max;   vU16Max.load4a(F_U16MAX_4A);
    LLVector4a vOOU16Max; vOOU16Max.load4a(F_OOU16MAX_4A);

    {
        val.clamp(low, high);
        val.sub(low);

        // 16-bit quantization means we need a round of Newton-Raphson
        LLVector4a oneOverDelta;
        {
            LLVector4a two;
            two.splat(2.f);

            // Here we use _mm_rcp_ps plus one round of newton-raphson
            // We wish to find 'x' such that x = 1/delta
            // As a first approximation, we take x0 = _mm_rcp_ps(delta)
            // Then x1 = 2 * x0 - a * x0^2 or x1 = x0 * ( 2 - a * x0 )
            // See Intel AP-803 http://ompf.org/!/Intel_application_note_AP-803.pdf
            const LLVector4a recipApprox = _mm_rcp_ps(safeDelta.mQ);
            oneOverDelta.setMul( safeDelta, recipApprox );
            oneOverDelta.setSub( two, oneOverDelta );
            oneOverDelta.mul( recipApprox );
        }

        val.mul(oneOverDelta);
        val.mul(vU16Max);
    }

    val = _mm_cvtepi32_ps(_mm_cvtps_epi32( val.mQ ));

    {
        val.mul(vOOU16Max);
        val.mul(delta);
        val.add(low);
    }

    {
        LLVector4a maxError; maxError.setMul(delta, vOOU16Max);
        LLVector4a absVal; absVal.setAbs( val );
        setSelectWithMask( absVal.lessThan( maxError ), F_ZERO_4A, val );
    }
}
