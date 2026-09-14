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

inline LLMatrix3a::LLMatrix3a( const LLVector4a& c0, const LLVector4a& c1, const LLVector4a& c2 )
{
    setColumns( c0, c1, c2 );
}

inline void LLMatrix3a::loadu(const LLMatrix3& src)
{
    mColumns[0].load3(src.mMatrix[0]);
    mColumns[1].load3(src.mMatrix[1]);
    mColumns[2].load3(src.mMatrix[2]);
}

inline void LLMatrix3a::setRows(const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2)
{
    mColumns[0] = r0;
    mColumns[1] = r1;
    mColumns[2] = r2;
    setTranspose( *this );
}

inline void LLMatrix3a::setColumns(const LLVector4a& c0, const LLVector4a& c1, const LLVector4a& c2)
{
    mColumns[0] = c0;
    mColumns[1] = c1;
    mColumns[2] = c2;
}

// Gathers each row of src into a column. The fourth lanes come from the
// third column, which is what the last shuffle of each row leaves there.
inline void LLMatrix3a::setTranspose(const LLMatrix3a& src)
{
    const LLQuad c0 = src.mColumns[0];
    const LLQuad c1 = src.mColumns[1];
    const LLQuad c2 = src.mColumns[2];
    const LLQuad xy = alsimd::unpacklo(c0, c1);   // c0.x c1.x c0.y c1.y
    const LLQuad zw = alsimd::unpackhi(c0, c1);   // c0.z c1.z c0.w c1.w
    mColumns[0] = alsimd::movelh(xy, c2);                 // c0.x c1.x c2.x c2.y
    mColumns[1] = alsimd::shuffle2<2, 3, 1, 0>(xy, c2);   // c0.y c1.y c2.y c2.x
    mColumns[2] = alsimd::shuffle2<0, 1, 2, 0>(zw, c2);   // c0.z c1.z c2.z c2.x
}

inline const LLVector4a& LLMatrix3a::getColumn(const U32 column) const
{
    llassert( column < 3 );
    return mColumns[column];
}

// Each column of the product is the columns of lhs weighted by the lanes of
// the matching column of rhs.
inline void LLMatrix3a::setMul( const LLMatrix3a& lhs, const LLMatrix3a& rhs )
{
    const LLQuad col0 = lhs.mColumns[0];
    const LLQuad col1 = lhs.mColumns[1];
    const LLQuad col2 = lhs.mColumns[2];

    for ( int i = 0; i < 3; i++ )
    {
        const LLQuad v = rhs.mColumns[i];
        LLQuad result = alsimd::mul(alsimd::splat<0>(v), col0);
        result = alsimd::fmadd_lane<1>(col1, v, result);
        mColumns[i] = alsimd::fmadd_lane<2>(col2, v, result);
    }
}

inline void LLMatrix3a::setLerp(const LLMatrix3a& a, const LLMatrix3a& b, F32 w)
{
    mColumns[0].setLerp( a.mColumns[0], b.mColumns[0], w );
    mColumns[1].setLerp( a.mColumns[1], b.mColumns[1], w );
    mColumns[2].setLerp( a.mColumns[2], b.mColumns[2], w );
}

inline bool LLMatrix3a::isFinite() const
{
    return mColumns[0].isFinite3() && mColumns[1].isFinite3() && mColumns[2].isFinite3();
}

inline void LLMatrix3a::getDeterminant( LLVector4a& dest ) const
{
    LLVector4a col1xcol2; col1xcol2.setCross3( mColumns[1], mColumns[2] );
    dest.setAllDot3( col1xcol2, mColumns[0] );
}

inline LLSimdScalar LLMatrix3a::getDeterminant() const
{
    LLVector4a col1xcol2; col1xcol2.setCross3( mColumns[1], mColumns[2] );
    return col1xcol2.dot3( mColumns[0] );
}

inline bool LLMatrix3a::isApproximatelyEqual( const LLMatrix3a& rhs, F32 tolerance /*= F_APPROXIMATELY_ZERO*/ ) const
{
    return rhs.getColumn(0).equals3(mColumns[0], tolerance)
        && rhs.getColumn(1).equals3(mColumns[1], tolerance)
        && rhs.getColumn(2).equals3(mColumns[2], tolerance);
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
