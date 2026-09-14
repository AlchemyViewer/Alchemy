/**
 * @file llsimdtypes.inl
 * @brief Inlined definitions of basic SIMD math related types
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

//////////////////
// LLSimdScalar
//////////////////

inline LLSimdScalar operator+(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return alsimd::add(a, b);
}

inline LLSimdScalar operator-(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return alsimd::sub(a, b);
}

inline LLSimdScalar operator*(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return alsimd::mul(a, b);
}

inline LLSimdScalar operator/(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return alsimd::div(a, b);
}

inline LLSimdScalar operator-(const LLSimdScalar& a)
{
    return alsimd::neg(a);
}

inline bool operator==(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() == b.getF32();
}

inline bool operator!=(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() != b.getF32();
}

inline bool operator<(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() < b.getF32();
}

inline bool operator<=(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() <= b.getF32();
}

inline bool operator>(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() > b.getF32();
}

inline bool operator>=(const LLSimdScalar& a, const LLSimdScalar& b)
{
    return a.getF32() >= b.getF32();
}

inline bool LLSimdScalar::isApproximatelyEqual(const LLSimdScalar& rhs, F32 tolerance /* = F_APPROXIMATELY_ZERO */) const
{
    return alsimd::lane<0>(alsimd::abs(alsimd::sub(mQ, rhs.mQ))) <= tolerance;
}

inline void LLSimdScalar::setMax(const LLSimdScalar& a, const LLSimdScalar& b)
{
    mQ = alsimd::max(a, b);
}

inline void LLSimdScalar::setMin(const LLSimdScalar& a, const LLSimdScalar& b)
{
    mQ = alsimd::min(a, b);
}

inline LLSimdScalar& LLSimdScalar::operator=(F32 rhs)
{
    mQ = alsimd::set1(rhs);
    return *this;
}

inline LLSimdScalar& LLSimdScalar::operator+=(const LLSimdScalar& rhs)
{
    mQ = alsimd::add(mQ, rhs);
    return *this;
}

inline LLSimdScalar& LLSimdScalar::operator-=(const LLSimdScalar& rhs)
{
    mQ = alsimd::sub(mQ, rhs);
    return *this;
}

inline LLSimdScalar& LLSimdScalar::operator*=(const LLSimdScalar& rhs)
{
    mQ = alsimd::mul(mQ, rhs);
    return *this;
}

inline LLSimdScalar& LLSimdScalar::operator/=(const LLSimdScalar& rhs)
{
    mQ = alsimd::div(mQ, rhs);
    return *this;
}

inline LLSimdScalar LLSimdScalar::getAbs() const
{
    return alsimd::abs(mQ);
}

inline F32 LLSimdScalar::getF32() const
{
    return alsimd::lane<0>(mQ);
}
