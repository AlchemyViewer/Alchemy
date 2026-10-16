/**
 * @file llvector4a.inl
 * @brief LLVector4a inline function implementations
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

////////////////////////////////////
// LOAD/STORE
////////////////////////////////////

inline void LLVector4a::load4a(const F32* src)
{
    mQ = alsimd::load(src);
}

inline void LLVector4a::loadua(const F32* src)
{
    mQ = alsimd::loadu(src);
}

inline void LLVector4a::load3(const F32* src)
{
    mQ = alsimd::load3(src);
}

inline void LLVector4a::store4a(F32* dst) const
{
    alsimd::store(dst, mQ);
}

////////////////////////////////////
// BASIC GET/SET
////////////////////////////////////

// The register type aliases float under every compiler here: GCC gives a
// vector type its element's alias set, Clang gives it the character type's,
// and MSVC declares it as a union of the lanes.
F32* LLVector4a::getF32ptr()
{
    return reinterpret_cast<F32*>(&mQ);
}

const F32* LLVector4a::getF32ptr() const
{
    return reinterpret_cast<const F32*>(&mQ);
}

inline F32 LLVector4a::operator[](const S32 idx) const
{
    return alsimd::lane(mQ, idx);
}

inline LLSimdScalar LLVector4a::getScalarAt(const S32 idx) const
{
    switch (idx)
    {
        case 0:
            return mQ;
        case 1:
            return alsimd::splat<1>(mQ);
        case 2:
            return alsimd::splat<2>(mQ);
        case 3:
        default:
            return alsimd::splat<3>(mQ);
    }
}

template <int N> LL_FORCE_INLINE LLSimdScalar LLVector4a::getScalarAt() const
{
    return alsimd::splat<N>(mQ);
}

template<> LL_FORCE_INLINE LLSimdScalar LLVector4a::getScalarAt<0>() const
{
    return mQ;
}

inline void LLVector4a::set(F32 x, F32 y, F32 z, F32 w)
{
    mQ = alsimd::set(x, y, z, w);
}

inline void LLVector4a::clear()
{
    mQ = alsimd::zero();
}

inline void LLVector4a::splat(const F32 x)
{
    mQ = alsimd::set1(x);
}

inline void LLVector4a::splat(const LLSimdScalar& x)
{
    mQ = alsimd::splat<0>(x.getQuad());
}

template <int N> void LLVector4a::splat(const LLVector4a& src)
{
    mQ = alsimd::splat<N>(src.mQ);
}

inline void LLVector4a::splat(const LLVector4a& v, U32 i)
{
    switch (i)
    {
        case 0:
            mQ = alsimd::splat<0>(v.mQ);
            break;
        case 1:
            mQ = alsimd::splat<1>(v.mQ);
            break;
        case 2:
            mQ = alsimd::splat<2>(v.mQ);
            break;
        case 3:
            mQ = alsimd::splat<3>(v.mQ);
            break;
    }
}

inline void LLVector4a::setSelectWithMask( const LLVector4Logical& mask, const LLVector4a& sourceIfTrue, const LLVector4a& sourceIfFalse )
{
    mQ = alsimd::select(mask, sourceIfTrue.mQ, sourceIfFalse.mQ);
}

////////////////////////////////////
// ALGEBRAIC
////////////////////////////////////

inline void LLVector4a::setAdd(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::add(a.mQ, b.mQ);
}

inline void LLVector4a::setSub(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::sub(a.mQ, b.mQ);
}

inline void LLVector4a::setMul(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::mul(a.mQ, b.mQ);
}

inline void LLVector4a::setDiv(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::div(a.mQ, b.mQ);
}

inline void LLVector4a::setAbs(const LLVector4a& src)
{
    mQ = alsimd::abs(src.mQ);
}

inline void LLVector4a::setNeg(const LLVector4a& src)
{
    mQ = alsimd::neg(src.mQ);
}

inline void LLVector4a::add(const LLVector4a& rhs)
{
    mQ = alsimd::add(mQ, rhs.mQ);
}

inline void LLVector4a::sub(const LLVector4a& rhs)
{
    mQ = alsimd::sub(mQ, rhs.mQ);
}

inline void LLVector4a::mul(const LLVector4a& rhs)
{
    mQ = alsimd::mul(mQ, rhs.mQ);
}

inline void LLVector4a::div(const LLVector4a& rhs)
{
    mQ = alsimd::div(mQ, rhs.mQ);
}

inline void LLVector4a::mul(const F32 x)
{
    mQ = alsimd::mul(mQ, alsimd::set1(x));
}

inline void LLVector4a::negate()
{
    mQ = alsimd::neg(mQ);
}

inline void LLVector4a::setCross3(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::cross3(a.mQ, b.mQ);
}

inline void LLVector4a::setAllDot3(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::dot3(a.mQ, b.mQ);
}

inline void LLVector4a::setAllDot4(const LLVector4a& a, const LLVector4a& b)
{
    mQ = alsimd::dot4(a.mQ, b.mQ);
}

inline LLSimdScalar LLVector4a::dot3(const LLVector4a& b) const
{
    return alsimd::dot3(mQ, b.mQ);
}

inline LLSimdScalar LLVector4a::dot4(const LLVector4a& b) const
{
    return alsimd::dot4(mQ, b.mQ);
}

inline void LLVector4a::normalize3()
{
    mQ = alsimd::mul(mQ, alsimd::rsqrt(alsimd::dot3(mQ, mQ)));
}

inline void LLVector4a::normalize4()
{
    mQ = alsimd::mul(mQ, alsimd::rsqrt(alsimd::dot4(mQ, mQ)));
}

inline LLSimdScalar LLVector4a::normalize3withLength()
{
    const LLQuad lenSqrd = alsimd::dot3(mQ, mQ);
    mQ = alsimd::mul(mQ, alsimd::rsqrt(lenSqrd));
    return alsimd::sqrt(lenSqrd);
}

inline void LLVector4a::normalize3fast()
{
    mQ = alsimd::mul(mQ, alsimd::rsqrt_fast(alsimd::dot3(mQ, mQ)));
}

inline void LLVector4a::normalize3fast_checked(LLVector4a* d)
{
    if (!isFinite3())
    {
        *this = d ? *d : LLVector4a(0,1,0,1);
        return;
    }

    const LLQuad lenSqrd = alsimd::dot3(mQ, mQ);

    if (alsimd::lane<0>(lenSqrd) <= FLT_EPSILON)
    {
        *this = d ? *d : LLVector4a(0,1,0,1);
        return;
    }

    mQ = alsimd::mul(mQ, alsimd::rsqrt_fast(lenSqrd));
}

// Convert an absolute length tolerance to the equivalent absolute lensq
// tolerance. We have lensq = a.a so |lensq - 1| = |(len-1)(len+1)| =
// |len - 1| * (len + 1) for len >= 0. The check we want -- |len - 1| <=
// tolerance -- is equivalent to |lensq - 1| <= tolerance * (len + 1), and
// the upper-boundary case len = 1 + tolerance gives the largest safe
// threshold: tolerance * (2 + tolerance) = 2*tolerance + tolerance^2.
static LL_FORCE_INLINE F32 lensq_tolerance_from_len(F32 lenTol)
{
    return lenTol * (2.f + lenTol);
}

inline bool LLVector4a::isNormalized3( F32 tolerance ) const
{
    const LLQuad lenSquared = alsimd::dot3(mQ, mQ);
    const F32 off = alsimd::lane<0>(alsimd::abs(alsimd::sub(lenSquared, alsimd::set1(1.f))));
    return off <= lensq_tolerance_from_len(tolerance);
}

inline bool LLVector4a::isNormalized4( F32 tolerance ) const
{
    const LLQuad lenSquared = alsimd::dot4(mQ, mQ);
    const F32 off = alsimd::lane<0>(alsimd::abs(alsimd::sub(lenSquared, alsimd::set1(1.f))));
    return off <= lensq_tolerance_from_len(tolerance);
}

inline void LLVector4a::setAllLength3( const LLVector4a& v )
{
    mQ = alsimd::sqrt(alsimd::dot3(v.mQ, v.mQ));
}

inline LLSimdScalar LLVector4a::getLength3() const
{
    return alsimd::sqrt(alsimd::dot3(mQ, mQ));
}

inline void LLVector4a::setMin(const LLVector4a& lhs, const LLVector4a& rhs)
{
    mQ = alsimd::min(lhs.mQ, rhs.mQ);
}

inline void LLVector4a::setMax(const LLVector4a& lhs, const LLVector4a& rhs)
{
    mQ = alsimd::max(lhs.mQ, rhs.mQ);
}

inline void LLVector4a::setLerp(const LLVector4a& lhs, const LLVector4a& rhs, F32 c)
{
    mQ = alsimd::fmadd(alsimd::sub(rhs.mQ, lhs.mQ), alsimd::set1(c), lhs.mQ);
}

inline bool LLVector4a::isFinite3() const
{
    return !alsimd::any3(alsimd::nonfinite(mQ));
}

inline bool LLVector4a::isFinite4() const
{
    return !alsimd::any(alsimd::nonfinite(mQ));
}

inline void LLVector4a::setRotatedInv( const LLRotation& rot, const LLVector4a& vec )
{
    LLRotation inv; inv.setTranspose( rot );
    setRotated( inv, vec );
}

inline void LLVector4a::setRotatedInv( const LLQuaternion2& quat, const LLVector4a& vec )
{
    LLQuaternion2 invRot; invRot.setConjugate( quat );
    setRotated(invRot, vec);
}

inline void LLVector4a::clamp( const LLVector4a& low, const LLVector4a& high )
{
    mQ = alsimd::select(alsimd::cmpgt(mQ, high.mQ), high.mQ, mQ);
    mQ = alsimd::select(alsimd::cmplt(mQ, low.mQ), low.mQ, mQ);
}


////////////////////////////////////
// LOGICAL
////////////////////////////////////

inline LLVector4Logical LLVector4a::greaterThan(const LLVector4a& rhs) const
{
    return alsimd::cmpgt(mQ, rhs.mQ);
}

inline LLVector4Logical LLVector4a::lessThan(const LLVector4a& rhs) const
{
    return alsimd::cmplt(mQ, rhs.mQ);
}

inline LLVector4Logical LLVector4a::greaterEqual(const LLVector4a& rhs) const
{
    return alsimd::cmpge(mQ, rhs.mQ);
}

inline LLVector4Logical LLVector4a::lessEqual(const LLVector4a& rhs) const
{
    return alsimd::cmple(mQ, rhs.mQ);
}

inline LLVector4Logical LLVector4a::equal(const LLVector4a& rhs) const
{
    return alsimd::cmpeq(mQ, rhs.mQ);
}

inline bool LLVector4a::equals4(const LLVector4a& rhs, F32 tolerance ) const
{
    const LLQuad diff = alsimd::abs(alsimd::sub(mQ, rhs.mQ));
    return alsimd::all(alsimd::cmplt(diff, alsimd::set1(tolerance)));
}

inline bool LLVector4a::equals3(const LLVector4a& rhs, F32 tolerance ) const
{
    const LLQuad diff = alsimd::abs(alsimd::sub(mQ, rhs.mQ));
    return alsimd::all3(alsimd::cmplt(diff, alsimd::set1(tolerance)));
}

////////////////////////////////////
// OPERATORS
////////////////////////////////////

inline const LLVector4a& LLVector4a::operator= ( const LLQuad& rhs )
{
    mQ = rhs;
    return *this;
}

inline LLVector4a::operator LLQuad() const
{
    return mQ;
}
