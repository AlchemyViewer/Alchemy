/**
 * @file llmatrix3a.inl
 * @brief LLMatrix3a inline definitions
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

#include "llmatrix3a.h"
#include "m3math.h"

inline LLMatrix3a::LLMatrix3a( const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2 )
{
    setRows( r0, r1, r2 );
}

inline void LLMatrix3a::loadu(const LLMatrix3& src)
{
    mRows[0].load3(src.mMatrix[0]);
    mRows[1].load3(src.mMatrix[1]);
    mRows[2].load3(src.mMatrix[2]);
}

inline void LLMatrix3a::setRows(const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2)
{
    mRows[0] = r0;
    mRows[1] = r1;
    mRows[2] = r2;
}

inline void LLMatrix3a::setColumns(const LLVector4a& c0, const LLVector4a& c1, const LLVector4a& c2)
{
    mRows[0] = c0;
    mRows[1] = c1;
    mRows[2] = c2;
    setTranspose( *this );
}

// Gathers each column of src into a row. The fourth lanes come from the
// third row, which is what the last shuffle of each row leaves there.
inline void LLMatrix3a::setTranspose(const LLMatrix3a& src)
{
    const LLQuad r0 = src.mRows[0];
    const LLQuad r1 = src.mRows[1];
    const LLQuad r2 = src.mRows[2];
    const LLQuad xy = alsimd::unpacklo(r0, r1);   // r0.x r1.x r0.y r1.y
    const LLQuad zw = alsimd::unpackhi(r0, r1);   // r0.z r1.z r0.w r1.w
    mRows[0] = alsimd::movelh(xy, r2);                 // r0.x r1.x r2.x r2.y
    mRows[1] = alsimd::shuffle2<2, 3, 1, 0>(xy, r2);   // r0.y r1.y r2.y r2.x
    mRows[2] = alsimd::shuffle2<0, 1, 2, 0>(zw, r2);   // r0.z r1.z r2.z r2.x
}

template<int N>
inline const LLVector4a& LLMatrix3a::getRow() const
{
    static_assert(N >= 0 && N < 3, "LLMatrix3a has three rows");
    return mRows[N];
}

inline const LLVector4a& LLMatrix3a::getRow(const U32 row) const
{
    llassert( row < 3 );
    return mRows[row];
}

// The rows weighted by the lanes of v
inline void LLMatrix3a::rotate(const LLVector4a& v, LLVector4a& res) const
{
    const LLQuad q = v;
    LLQuad r = alsimd::mul(alsimd::splat<0>(q), mRows[0]);
    r = alsimd::fmadd_lane<1>(mRows[1], q, r);
    res = alsimd::fmadd_lane<2>(mRows[2], q, r);
}

// Each row of the product is that row of a rotated by b.
inline void LLMatrix3a::setMul( const LLMatrix3a& a, const LLMatrix3a& b )
{
    const LLQuad b0 = b.mRows[0];
    const LLQuad b1 = b.mRows[1];
    const LLQuad b2 = b.mRows[2];

    for ( int i = 0; i < 3; i++ )
    {
        const LLQuad v = a.mRows[i];
        LLQuad result = alsimd::mul(alsimd::splat<0>(v), b0);
        result = alsimd::fmadd_lane<1>(b1, v, result);
        mRows[i] = alsimd::fmadd_lane<2>(b2, v, result);
    }
}

inline void LLMatrix3a::setLerp(const LLMatrix3a& a, const LLMatrix3a& b, F32 w)
{
    mRows[0].setLerp( a.mRows[0], b.mRows[0], w );
    mRows[1].setLerp( a.mRows[1], b.mRows[1], w );
    mRows[2].setLerp( a.mRows[2], b.mRows[2], w );
}

inline bool LLMatrix3a::isFinite() const
{
    return mRows[0].isFinite3() && mRows[1].isFinite3() && mRows[2].isFinite3();
}

inline void LLMatrix3a::getDeterminant( LLVector4a& dest ) const
{
    LLVector4a row1xrow2; row1xrow2.setCross3( mRows[1], mRows[2] );
    dest.setAllDot3( row1xrow2, mRows[0] );
}

inline LLSimdScalar LLMatrix3a::getDeterminant() const
{
    LLVector4a row1xrow2; row1xrow2.setCross3( mRows[1], mRows[2] );
    return row1xrow2.dot3( mRows[0] );
}

inline bool LLMatrix3a::isApproximatelyEqual( const LLMatrix3a& rhs, F32 tolerance /*= F_APPROXIMATELY_ZERO*/ ) const
{
    return rhs.mRows[0].equals3(mRows[0], tolerance)
        && rhs.mRows[1].equals3(mRows[1], tolerance)
        && rhs.mRows[2].equals3(mRows[2], tolerance);
}

inline const LLMatrix3a& LLMatrix3a::getIdentity()
{
    extern const LLMatrix3a LL_M3A_IDENTITY;
    return LL_M3A_IDENTITY;
}

inline bool LLRotation::isOkRotation() const
{
    LLMatrix3a transpose; transpose.setTranspose( *this );
    LLMatrix3a product; product.setMul( *this, transpose );

    LLSimdScalar detMinusOne = getDeterminant() - 1.f;

    return product.isApproximatelyEqual( LLMatrix3a::getIdentity() ) && (detMinusOne.getAbs() < F_APPROXIMATELY_ZERO);
}

inline void LLVector4a::setRotated( const LLRotation& rot, const LLVector4a& vec )
{
    rot.rotate( vec, *this );
}
