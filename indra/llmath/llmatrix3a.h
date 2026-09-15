/**
 * @file llmatrix3a.h
 * @brief LLMatrix3a class header file - memory aligned and vectorized 3x3 matrix
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

#ifndef LL_LLMATRIX3A_H
#define LL_LLMATRIX3A_H

// A 3x3 matrix in three row vectors, each a register; the fourth lane of
// each row is carried, not meant. Vectors are rows, as they are for
// LLMatrix3 and LLMatrix4a: rotate(v) is v * M, and setMul(a, b) is a then
// b. LLRotation is the same storage with the promise that it holds a
// rotation, and is what a rotation matrix should be held in.
class alignas(16) LLMatrix3a
{
public:

    // Utility function for quickly transforming an array of LLVector4a's
    // For transforming a single LLVector4a, see rotate and LLVector4a::setRotated
    static void batchTransform( const LLMatrix3a& xform, const LLVector4a* src, int numVectors, LLVector4a* dst );

    // Utility function to obtain the identity matrix
    static inline const LLMatrix3a& getIdentity();

    //////////////////////////
    // Ctors
    //////////////////////////

    LLMatrix3a() = default;

    // Ctor for setting by rows
    inline LLMatrix3a( const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2 );

    //////////////////////////
    // Get/Set
    //////////////////////////

    // Loads the rows of an LLMatrix3; the fourth lanes are zero
    inline void loadu(const LLMatrix3& src);

    // Set rows
    inline void setRows(const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2);

    // Set columns
    inline void setColumns(const LLVector4a& c0, const LLVector4a& c1, const LLVector4a& c2);

    // Read-only access to a row. Valid rows are 0-2, but the run-time form
    // is unchecked. You've been warned.
    template<int N> inline const LLVector4a& getRow() const;
    inline const LLVector4a& getRow(const U32 row) const;

    /////////////////////////
    // Transformation
    /////////////////////////

    // res = v * this; the fourth lane of v is ignored
    inline void rotate(const LLVector4a& v, LLVector4a& res) const;

    /////////////////////////
    // Matrix modification
    /////////////////////////

    // Set this matrix to a then b: rotating by the product is rotating by a
    // and then by b. Either operand may be this matrix.
    inline void setMul( const LLMatrix3a& a, const LLMatrix3a& b );

    // Set this matrix to the transpose of src
    inline void setTranspose(const LLMatrix3a& src);

    // Set this matrix to a*w + b*(1-w)
    inline void setLerp(const LLMatrix3a& a, const LLMatrix3a& b, F32 w);

    /////////////////////////
    // Matrix inspection
    /////////////////////////

    // Sets all 4 elements in 'dest' to the determinant of this matrix.
    // If you will be using the determinant in subsequent ops with LLVector4a, use this version
    inline void getDeterminant( LLVector4a& dest ) const;

    // Returns the determinant as an LLSimdScalar. Use this if you will be using the determinant
    // primary for scalar operations.
    inline LLSimdScalar getDeterminant() const;

    // Returns true if rows 0-2 and colums 0-2 contain no NaN or INF values. The fourth lanes are ignored
    inline bool isFinite() const;

    // Returns true if this matrix is equal to 'rhs' up to 'tolerance'
    inline bool isApproximatelyEqual( const LLMatrix3a& rhs, F32 tolerance = F_APPROXIMATELY_ZERO ) const;

protected:

    LLVector4a mRows[3];

};

static_assert(std::is_trivially_copyable<LLMatrix3a>::value && std::is_standard_layout<LLMatrix3a>::value, "LLMatrix3a is plain data");

class LLRotation : public LLMatrix3a
{
public:

    LLRotation() = default;

    // Returns true if this rotation is orthonormal with det ~= 1
    inline bool isOkRotation() const;
};

static_assert(std::is_trivially_copyable<LLRotation>::value && std::is_standard_layout<LLRotation>::value, "LLRotation is plain data");

#endif
