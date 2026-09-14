/**
 * @file llsimdtypes.h
 * @brief Declaration of basic SIMD math related types
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

#ifndef LL_SIMD_TYPES_H
#define LL_SIMD_TYPES_H

#include "llmath.h"
#include "alsimd.h"

// The register every vector type here wraps.
typedef alsimd::f32x4 LLQuad;

// A scalar kept in a vector register, for a value that came out of vector
// math and is going back into it: a dot product, a length, a lane. The
// value is lane 0; the other three lanes are unspecified. The comparisons
// are the scalar ones, with whatever the build's floating-point mode makes
// of a NaN.
class LLSimdScalar
{
public:
    LLSimdScalar() = default;

    LLSimdScalar(LLQuad q) : mQ(q) {}

    LLSimdScalar(F32 f) : mQ(alsimd::set1(f)) {}

    static inline LLSimdScalar getZero()
    {
        return LLSimdScalar(alsimd::zero());
    }

    inline F32 getF32() const;

    inline bool isApproximatelyEqual(const LLSimdScalar& rhs, F32 tolerance = F_APPROXIMATELY_ZERO) const;

    inline LLSimdScalar getAbs() const;

    inline void setMax(const LLSimdScalar& a, const LLSimdScalar& b);

    inline void setMin(const LLSimdScalar& a, const LLSimdScalar& b);

    inline LLSimdScalar& operator=(F32 rhs);

    inline LLSimdScalar& operator+=(const LLSimdScalar& rhs);

    inline LLSimdScalar& operator-=(const LLSimdScalar& rhs);

    inline LLSimdScalar& operator*=(const LLSimdScalar& rhs);

    inline LLSimdScalar& operator/=(const LLSimdScalar& rhs);

    inline operator LLQuad() const
    {
        return mQ;
    }

    inline const LLQuad& getQuad() const
    {
        return mQ;
    }

private:
    LLQuad mQ;
};

static_assert(std::is_trivially_copyable<LLSimdScalar>::value && std::is_standard_layout<LLSimdScalar>::value, "LLSimdScalar is plain data");
static_assert(sizeof(LLSimdScalar) == 16 && alignof(LLSimdScalar) == 16, "LLSimdScalar is one register");

#endif //LL_SIMD_TYPES_H
