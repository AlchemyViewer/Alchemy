/**
 * @file m4math.h
 * @brief LLMatrix4 class header file.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_M4MATH_H
#define LL_M4MATH_H

#include "v3math.h"

class LLVector4;
class LLMatrix3;
class LLQuaternion;
class LLMatrix4a;

// NOTA BENE: Currently assuming a right-handed, x-forward, y-left, z-up universe

// Us versus OpenGL:

// Even though OpenGL uses column vectors and we use row vectors, we can plug our matrices
// directly into OpenGL.  This is because OpenGL numbers its matrices going columnwise:
//
// OpenGL indexing:          Our indexing:
// 0  4  8 12                [0][0] [0][1] [0][2] [0][3]
// 1  5  9 13                [1][0] [1][1] [1][2] [1][3]
// 2  6 10 14                [2][0] [2][1] [2][2] [2][3]
// 3  7 11 15                [3][0] [3][1] [3][2] [3][3]
//
// So when you're looking at OpenGL related matrices online, our matrices will be
// "transposed".  But our matrices can be plugged directly into OpenGL and work fine!
//

// We're using row vectors - [vx, vy, vz, vw]
//
// There are several different ways of thinking of matrices, if you mix them up, you'll get very confused.
//
// One way to think about it is a matrix that takes the origin frame A
// and rotates it into B': i.e. A*M = B
//
//      Vectors:
//      f - forward axis of B expressed in A
//      l - left axis of B expressed in A
//      u - up axis of B expressed in A
//
//      |  0: fx  1: fy  2: fz  3:0 |
//  M = |  4: lx  5: ly  6: lz  7:0 |
//      |  8: ux  9: uy 10: uz 11:0 |
//      | 12: 0  13: 0  14:  0 15:1 |
//
//
//
//
// Another way to think of matrices is matrix that takes a point p in frame A, and puts it into frame B:
// This is used most commonly for the modelview matrix.
//
// so p*M = p'
//
//      Vectors:
//      f - forward of frame B in frame A
//      l - left of frame B in frame A
//      u - up of frame B in frame A
//      o - origin of frame frame B in frame A
//
//      |  0: fx  1: lx  2: ux  3:0 |
//  M = |  4: fy  5: ly  6: uy  7:0 |
//      |  8: fz  9: lz 10: uz 11:0 |
//      | 12:-of 13:-ol 14:-ou 15:1 |
//
//      of, ol, and ou mean the component of the "global" origin o in the f axis, l axis, and u axis.
//

static const U32 NUM_VALUES_IN_MAT4 = 4;

class LLMatrix4
{
public:
    F32 mMatrix[NUM_VALUES_IN_MAT4][NUM_VALUES_IN_MAT4] {
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f, 1.f}
    };

    // Initializes Matrix to identity matrix
    constexpr LLMatrix4() noexcept = default;
    constexpr explicit LLMatrix4(const F32 *mat) noexcept;          // Initializes Matrix to values in mat
    explicit LLMatrix4(const LLMatrix3 &mat);                       // Initializes Matrix to values in mat and sets position to (0,0,0)
    explicit LLMatrix4(const LLQuaternion &q);                      // Initializes Matrix with rotation q and sets position to (0,0,0)
    explicit LLMatrix4(const LLMatrix4a& mat);

    LLMatrix4(const LLMatrix3 &mat, const LLVector4 &pos);  // Initializes Matrix to values in mat and pos

    // These are really, really, inefficient as implemented! - djs
    LLMatrix4(const LLQuaternion &q, const LLVector4 &pos); // Initializes Matrix with rotation q and position pos
    LLMatrix4(F32 angle,
              const LLVector4 &vec,
              const LLVector4 &pos);                        // Initializes Matrix with axis-angle and position
    LLMatrix4(F32 angle, const LLVector4 &vec);             // Initializes Matrix with axis-angle and sets position to (0,0,0)
    LLMatrix4(const F32 roll, const F32 pitch, const F32 yaw,
              const LLVector4 &pos);                        // Initializes Matrix with Euler angles
    LLMatrix4(const F32 roll, const F32 pitch, const F32 yaw);              // Initializes Matrix with Euler angles

    LLSD getValue() const;
    void setValue(const LLSD&);

    //////////////////////////////
    //
    // Matrix initializers - these replace any existing values in the matrix
    //

    void initRows(const LLVector4 &row0,
                  const LLVector4 &row1,
                  const LLVector4 &row2,
                  const LLVector4 &row3);

    // various useful matrix functions
    constexpr const LLMatrix4& setIdentity() noexcept; // Load identity matrix
    constexpr bool isIdentity() const noexcept;
    constexpr const LLMatrix4& setZero() noexcept;  // Clears matrix to all zeros.

    const LLMatrix4& initRotation(const F32 angle, const LLVector4 &axis);  // Calculate rotation matrix for rotating angle radians about vec
    const LLMatrix4& initRotation(const F32 roll, const F32 pitch, const F32 yaw);      // Calculate rotation matrix from Euler angles
    const LLMatrix4& initRotation(const LLQuaternion &q);           // Set with Quaternion and position

    // Position Only
    const LLMatrix4& initMatrix(const LLMatrix3 &mat); //
    const LLMatrix4& initMatrix(const LLMatrix3 &mat, const LLVector4 &translation);

    // These operation create a matrix that will rotate and translate by the
    // specified amounts.
    const LLMatrix4& initRotTrans(const F32 angle, const LLVector3 &axis, const LLVector3 &translation);     // Rotation from axis angle + translation
    const LLMatrix4& initRotTrans(const F32 roll, const F32 pitch, const F32 yaw, const LLVector4 &pos); // Rotation from Euler + translation
    const LLMatrix4& initRotTrans(const LLQuaternion &q, const LLVector4 &pos); // Set with Quaternion and position

    constexpr const LLMatrix4& initScale(const LLVector3 &scale) noexcept;

    // Set all
    const LLMatrix4& initAll(const LLVector3 &scale, const LLQuaternion &q, const LLVector3 &pos);


    ///////////////////////////
    //
    // Matrix setters - set some properties without modifying others
    //

    constexpr const LLMatrix4& setTranslation(const F32 x, const F32 y, const F32 z) noexcept; // Sets matrix to translate by (x,y,z)

    constexpr void setFwdRow(const LLVector3 &row) noexcept;
    constexpr void setLeftRow(const LLVector3 &row) noexcept;
    constexpr void setUpRow(const LLVector3 &row) noexcept;

    constexpr void setFwdCol(const LLVector3 &col) noexcept;
    constexpr void setLeftCol(const LLVector3 &col) noexcept;
    constexpr void setUpCol(const LLVector3 &col) noexcept;

    const LLMatrix4& setTranslation(const LLVector4 &translation);
    constexpr const LLMatrix4& setTranslation(const LLVector3 &translation) noexcept;

    // Convenience func for simplifying comparison-heavy code by
    // intentionally stomping values [-FLT_EPS,FLT_EPS] to 0.0
    //
    constexpr void condition() noexcept;

    ///////////////////////////
    //
    // Get properties of a matrix
    //

    constexpr F32 determinant() const noexcept;                 // Return determinant
    LLQuaternion quaternion(void) const;            // Returns quaternion

    LLVector4 getFwdRow4() const;
    LLVector4 getLeftRow4() const;
    LLVector4 getUpRow4() const;

    LLMatrix3 getMat3() const;

    // Return the translation row (row 3, xyz) as an LLVector3. Previously
    // this was returned by reference via `*(LLVector3*)&mMatrix[3][0]`,
    // which type-puns a 3-element interior of an F32[4] array as an
    // LLVector3 object. The two have the same byte layout but unrelated
    // types -- the LLVector3 object was never created at that address, so
    // accessing it through an LLVector3& is strict-aliasing UB and the
    // pattern LTO is permitted to miscompile. Return by value so callers
    // get a real LLVector3 from the three floats; no caller relied on the
    // aliased-reference lifetime (all known uses copy the result or bind
    // it to a short-lived `const LLVector3&` which the temporary still
    // satisfies).
    constexpr LLVector3 getTranslation() const noexcept { return LLVector3(mMatrix[3][0], mMatrix[3][1], mMatrix[3][2]); }

    ///////////////////////////
    //
    // Operations on an existing matrix
    //

    constexpr const LLMatrix4& transpose() noexcept;    // Transpose LLMatrix4
    constexpr const LLMatrix4& invert() noexcept;       // Invert LLMatrix4

    // Rotate existing matrix
    // These are really, really, inefficient as implemented! - djs
    const LLMatrix4& rotate(const F32 angle, const LLVector4 &vec);     // Rotate matrix by rotating angle radians about vec
    const LLMatrix4& rotate(const F32 roll, const F32 pitch, const F32 yaw);        // Rotate matrix by Euler angles
    const LLMatrix4& rotate(const LLQuaternion &q);             // Rotate matrix by Quaternion


    // Translate existing matrix
    constexpr const LLMatrix4& translate(const LLVector3 &vec) noexcept; // Translate matrix by (vec[VX], vec[VY], vec[VZ])


    ///////////////////////
    //
    // Operators
    //

    //  friend inline LLMatrix4 operator*(const LLMatrix4 &a, const LLMatrix4 &b);      // Return a * b
    friend LLVector4 operator*(const LLVector4 &a, const LLMatrix4 &b);     // Return transform of vector a by matrix b
    friend constexpr const LLVector3 operator*(const LLVector3 &a, const LLMatrix4 &b) noexcept;  // Return full transform of a by matrix b
    friend LLVector4 rotate_vector(const LLVector4 &a, const LLMatrix4 &b); // Rotates a but does not translate
    friend constexpr LLVector3 rotate_vector(const LLVector3 &a, const LLMatrix4 &b) noexcept;    // Rotates a but does not translate

    friend constexpr bool operator==(const LLMatrix4 &a, const LLMatrix4 &b) noexcept;    // Return a == b
    friend constexpr bool operator!=(const LLMatrix4 &a, const LLMatrix4 &b) noexcept;    // Return a != b
    friend constexpr bool operator<(const LLMatrix4 &a, const LLMatrix4& b) noexcept;     // Return a < b

    friend constexpr const LLMatrix4& operator+=(LLMatrix4 &a, const LLMatrix4 &b) noexcept;  // Return a + b
    friend constexpr const LLMatrix4& operator-=(LLMatrix4 &a, const LLMatrix4 &b) noexcept;  // Return a - b
    friend constexpr const LLMatrix4& operator*=(LLMatrix4 &a, const LLMatrix4 &b) noexcept;  // Return a * b
    friend constexpr const LLMatrix4& operator*=(LLMatrix4 &a, const F32 &b) noexcept;        // Return a * b

    friend std::ostream&     operator<<(std::ostream& s, const LLMatrix4 &a);   // Stream a
};

static_assert(std::is_trivially_copyable<LLMatrix4>::value, "LLMatrix4 must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLMatrix4>::value, "LLMatrix4 must be trivial move");
static_assert(std::is_standard_layout<LLMatrix4>::value, "LLMatrix4 must be a standard layout type");

constexpr LLMatrix4::LLMatrix4(const F32 *mat) noexcept
{
    mMatrix[0][0] = mat[0];
    mMatrix[0][1] = mat[1];
    mMatrix[0][2] = mat[2];
    mMatrix[0][3] = mat[3];

    mMatrix[1][0] = mat[4];
    mMatrix[1][1] = mat[5];
    mMatrix[1][2] = mat[6];
    mMatrix[1][3] = mat[7];

    mMatrix[2][0] = mat[8];
    mMatrix[2][1] = mat[9];
    mMatrix[2][2] = mat[10];
    mMatrix[2][3] = mat[11];

    mMatrix[3][0] = mat[12];
    mMatrix[3][1] = mat[13];
    mMatrix[3][2] = mat[14];
    mMatrix[3][3] = mat[15];
}

constexpr const LLMatrix4& LLMatrix4::setZero() noexcept
{
    mMatrix[0][0] = 0.f;
    mMatrix[0][1] = 0.f;
    mMatrix[0][2] = 0.f;
    mMatrix[0][3] = 0.f;

    mMatrix[1][0] = 0.f;
    mMatrix[1][1] = 0.f;
    mMatrix[1][2] = 0.f;
    mMatrix[1][3] = 0.f;

    mMatrix[2][0] = 0.f;
    mMatrix[2][1] = 0.f;
    mMatrix[2][2] = 0.f;
    mMatrix[2][3] = 0.f;

    mMatrix[3][0] = 0.f;
    mMatrix[3][1] = 0.f;
    mMatrix[3][2] = 0.f;
    mMatrix[3][3] = 0.f;
    return *this;
}

constexpr const LLMatrix4& LLMatrix4::transpose() noexcept
{
    LLMatrix4 mat;
    mat.mMatrix[0][0] = mMatrix[0][0];
    mat.mMatrix[1][0] = mMatrix[0][1];
    mat.mMatrix[2][0] = mMatrix[0][2];
    mat.mMatrix[3][0] = mMatrix[0][3];

    mat.mMatrix[0][1] = mMatrix[1][0];
    mat.mMatrix[1][1] = mMatrix[1][1];
    mat.mMatrix[2][1] = mMatrix[1][2];
    mat.mMatrix[3][1] = mMatrix[1][3];

    mat.mMatrix[0][2] = mMatrix[2][0];
    mat.mMatrix[1][2] = mMatrix[2][1];
    mat.mMatrix[2][2] = mMatrix[2][2];
    mat.mMatrix[3][2] = mMatrix[2][3];

    mat.mMatrix[0][3] = mMatrix[3][0];
    mat.mMatrix[1][3] = mMatrix[3][1];
    mat.mMatrix[2][3] = mMatrix[3][2];
    mat.mMatrix[3][3] = mMatrix[3][3];

    *this = mat;
    return *this;
}

constexpr F32 LLMatrix4::determinant() const noexcept
{
    F32 value =
        mMatrix[0][3] * mMatrix[1][2] * mMatrix[2][1] * mMatrix[3][0] -
        mMatrix[0][2] * mMatrix[1][3] * mMatrix[2][1] * mMatrix[3][0] -
        mMatrix[0][3] * mMatrix[1][1] * mMatrix[2][2] * mMatrix[3][0] +
        mMatrix[0][1] * mMatrix[1][3] * mMatrix[2][2] * mMatrix[3][0] +
        mMatrix[0][2] * mMatrix[1][1] * mMatrix[2][3] * mMatrix[3][0] -
        mMatrix[0][1] * mMatrix[1][2] * mMatrix[2][3] * mMatrix[3][0] -
        mMatrix[0][3] * mMatrix[1][2] * mMatrix[2][0] * mMatrix[3][1] +
        mMatrix[0][2] * mMatrix[1][3] * mMatrix[2][0] * mMatrix[3][1] +
        mMatrix[0][3] * mMatrix[1][0] * mMatrix[2][2] * mMatrix[3][1] -
        mMatrix[0][0] * mMatrix[1][3] * mMatrix[2][2] * mMatrix[3][1] -
        mMatrix[0][2] * mMatrix[1][0] * mMatrix[2][3] * mMatrix[3][1] +
        mMatrix[0][0] * mMatrix[1][2] * mMatrix[2][3] * mMatrix[3][1] +
        mMatrix[0][3] * mMatrix[1][1] * mMatrix[2][0] * mMatrix[3][2] -
        mMatrix[0][1] * mMatrix[1][3] * mMatrix[2][0] * mMatrix[3][2] -
        mMatrix[0][3] * mMatrix[1][0] * mMatrix[2][1] * mMatrix[3][2] +
        mMatrix[0][0] * mMatrix[1][3] * mMatrix[2][1] * mMatrix[3][2] +
        mMatrix[0][1] * mMatrix[1][0] * mMatrix[2][3] * mMatrix[3][2] -
        mMatrix[0][0] * mMatrix[1][1] * mMatrix[2][3] * mMatrix[3][2] -
        mMatrix[0][2] * mMatrix[1][1] * mMatrix[2][0] * mMatrix[3][3] +
        mMatrix[0][1] * mMatrix[1][2] * mMatrix[2][0] * mMatrix[3][3] +
        mMatrix[0][2] * mMatrix[1][0] * mMatrix[2][1] * mMatrix[3][3] -
        mMatrix[0][0] * mMatrix[1][2] * mMatrix[2][1] * mMatrix[3][3] -
        mMatrix[0][1] * mMatrix[1][0] * mMatrix[2][2] * mMatrix[3][3] +
        mMatrix[0][0] * mMatrix[1][1] * mMatrix[2][2] * mMatrix[3][3];

    return value;
}

// Only works for pure orthonormal, homogeneous transform matrices.
constexpr const LLMatrix4& LLMatrix4::invert() noexcept
{
    // transpose the rotation part
    F32 temp;
    temp = mMatrix[VX][VY]; mMatrix[VX][VY] = mMatrix[VY][VX]; mMatrix[VY][VX] = temp;
    temp = mMatrix[VX][VZ]; mMatrix[VX][VZ] = mMatrix[VZ][VX]; mMatrix[VZ][VX] = temp;
    temp = mMatrix[VY][VZ]; mMatrix[VY][VZ] = mMatrix[VZ][VY]; mMatrix[VZ][VY] = temp;

    // rotate the translation part by the new rotation
    // (temporarily store in empty column of matrix)
    U32 j;
    for (j=0; j<3; j++)
    {
        mMatrix[j][VW] =  mMatrix[VW][VX] * mMatrix[VX][j] +
                          mMatrix[VW][VY] * mMatrix[VY][j] +
                          mMatrix[VW][VZ] * mMatrix[VZ][j];
    }

    // negate and copy the temporary vector back to the tranlation row
    mMatrix[VW][VX] = -mMatrix[VX][VW];
    mMatrix[VW][VY] = -mMatrix[VY][VW];
    mMatrix[VW][VZ] = -mMatrix[VZ][VW];

    // zero the empty column again
    mMatrix[VX][VW] = mMatrix[VY][VW] = mMatrix[VZ][VW] = 0.0f;

    return *this;
}

constexpr void LLMatrix4::condition() noexcept
{
    U32 i;
    U32 j;
    for (i = 0; i < 3;i++)
        for (j = 0; j < 3;j++)
            mMatrix[i][j] = ((mMatrix[i][j] > -FLT_EPSILON)
                              && (mMatrix[i][j] < FLT_EPSILON)) ? 0.0f : mMatrix[i][j];
}

constexpr const LLMatrix4& LLMatrix4::initScale(const LLVector3 &scale) noexcept
{
    setIdentity();

    mMatrix[VX][VX] = scale.mV[VX];
    mMatrix[VY][VY] = scale.mV[VY];
    mMatrix[VZ][VZ] = scale.mV[VZ];

    return (*this);
}

constexpr const LLMatrix4& LLMatrix4::translate(const LLVector3 &vec) noexcept
{
    mMatrix[3][0] += vec.mV[0];
    mMatrix[3][1] += vec.mV[1];
    mMatrix[3][2] += vec.mV[2];
    return (*this);
}

constexpr void LLMatrix4::setFwdRow(const LLVector3 &row) noexcept
{
    mMatrix[VX][VX] = row.mV[VX];
    mMatrix[VX][VY] = row.mV[VY];
    mMatrix[VX][VZ] = row.mV[VZ];
}

constexpr void LLMatrix4::setLeftRow(const LLVector3 &row) noexcept
{
    mMatrix[VY][VX] = row.mV[VX];
    mMatrix[VY][VY] = row.mV[VY];
    mMatrix[VY][VZ] = row.mV[VZ];
}

constexpr void LLMatrix4::setUpRow(const LLVector3 &row) noexcept
{
    mMatrix[VZ][VX] = row.mV[VX];
    mMatrix[VZ][VY] = row.mV[VY];
    mMatrix[VZ][VZ] = row.mV[VZ];
}

constexpr void LLMatrix4::setFwdCol(const LLVector3 &col) noexcept
{
    mMatrix[VX][VX] = col.mV[VX];
    mMatrix[VY][VX] = col.mV[VY];
    mMatrix[VZ][VX] = col.mV[VZ];
}

constexpr void LLMatrix4::setLeftCol(const LLVector3 &col) noexcept
{
    mMatrix[VX][VY] = col.mV[VX];
    mMatrix[VY][VY] = col.mV[VY];
    mMatrix[VZ][VY] = col.mV[VZ];
}

constexpr void LLMatrix4::setUpCol(const LLVector3 &col) noexcept
{
    mMatrix[VX][VZ] = col.mV[VX];
    mMatrix[VY][VZ] = col.mV[VY];
    mMatrix[VZ][VZ] = col.mV[VZ];
}

constexpr const LLMatrix4& LLMatrix4::setTranslation(const F32 tx, const F32 ty, const F32 tz) noexcept
{
    mMatrix[VW][VX] = tx;
    mMatrix[VW][VY] = ty;
    mMatrix[VW][VZ] = tz;
    return (*this);
}

constexpr const LLMatrix4& LLMatrix4::setTranslation(const LLVector3 &translation) noexcept
{
    mMatrix[VW][VX] = translation.mV[VX];
    mMatrix[VW][VY] = translation.mV[VY];
    mMatrix[VW][VZ] = translation.mV[VZ];
    return (*this);
}

inline constexpr LLVector3 rotate_vector(const LLVector3 &a, const LLMatrix4 &b) noexcept
{
    // Rotates but does not translate
    // Operate "to the left" on row-vector a
    LLVector3   vec;
    vec.mV[VX] = a.mV[VX] * b.mMatrix[VX][VX] +
                 a.mV[VY] * b.mMatrix[VY][VX] +
                 a.mV[VZ] * b.mMatrix[VZ][VX];

    vec.mV[VY] = a.mV[VX] * b.mMatrix[VX][VY] +
                 a.mV[VY] * b.mMatrix[VY][VY] +
                 a.mV[VZ] * b.mMatrix[VZ][VY];

    vec.mV[VZ] = a.mV[VX] * b.mMatrix[VX][VZ] +
                 a.mV[VY] * b.mMatrix[VY][VZ] +
                 a.mV[VZ] * b.mMatrix[VZ][VZ];
    return vec;
}

inline constexpr bool operator==(const LLMatrix4 &a, const LLMatrix4 &b) noexcept
{
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            if (a.mMatrix[j][i] != b.mMatrix[j][i])
                return false;
        }
    }
    return true;
}

inline constexpr bool operator!=(const LLMatrix4 &a, const LLMatrix4 &b) noexcept
{
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            if (a.mMatrix[j][i] != b.mMatrix[j][i])
                return true;
        }
    }
    return false;
}

inline constexpr bool operator<(const LLMatrix4& a, const LLMatrix4 &b) noexcept
{
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            if (a.mMatrix[i][j] != b.mMatrix[i][j])
            {
                return a.mMatrix[i][j] < b.mMatrix[i][j];
            }
        }
    }

    return false;
}

constexpr const LLMatrix4& LLMatrix4::setIdentity() noexcept
{
    mMatrix[0][0] = 1.f;
    mMatrix[0][1] = 0.f;
    mMatrix[0][2] = 0.f;
    mMatrix[0][3] = 0.f;

    mMatrix[1][0] = 0.f;
    mMatrix[1][1] = 1.f;
    mMatrix[1][2] = 0.f;
    mMatrix[1][3] = 0.f;

    mMatrix[2][0] = 0.f;
    mMatrix[2][1] = 0.f;
    mMatrix[2][2] = 1.f;
    mMatrix[2][3] = 0.f;

    mMatrix[3][0] = 0.f;
    mMatrix[3][1] = 0.f;
    mMatrix[3][2] = 0.f;
    mMatrix[3][3] = 1.f;
    return (*this);
}

constexpr bool LLMatrix4::isIdentity() const noexcept
{
    return
        mMatrix[0][0] == 1.f &&
        mMatrix[0][1] == 0.f &&
        mMatrix[0][2] == 0.f &&
        mMatrix[0][3] == 0.f &&

        mMatrix[1][0] == 0.f &&
        mMatrix[1][1] == 1.f &&
        mMatrix[1][2] == 0.f &&
        mMatrix[1][3] == 0.f &&

        mMatrix[2][0] == 0.f &&
        mMatrix[2][1] == 0.f &&
        mMatrix[2][2] == 1.f &&
        mMatrix[2][3] == 0.f &&

        mMatrix[3][0] == 0.f &&
        mMatrix[3][1] == 0.f &&
        mMatrix[3][2] == 0.f &&
        mMatrix[3][3] == 1.f;
}


/*
inline LLMatrix4 operator*(const LLMatrix4 &a, const LLMatrix4 &b)
{
    U32     i, j;
    LLMatrix4   mat;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][0] * b.mMatrix[0][i] +
                                a.mMatrix[j][1] * b.mMatrix[1][i] +
                                a.mMatrix[j][2] * b.mMatrix[2][i] +
                                a.mMatrix[j][3] * b.mMatrix[3][i];
        }
    }
    return mat;
}
*/


inline constexpr const LLMatrix4& operator*=(LLMatrix4 &a, const LLMatrix4 &b) noexcept
{
    U32     i, j;
    LLMatrix4   mat;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][0] * b.mMatrix[0][i] +
                                a.mMatrix[j][1] * b.mMatrix[1][i] +
                                a.mMatrix[j][2] * b.mMatrix[2][i] +
                                a.mMatrix[j][3] * b.mMatrix[3][i];
        }
    }
    a = mat;
    return a;
}

inline constexpr const LLMatrix4& operator*=(LLMatrix4 &a, const F32 &b) noexcept
{
    U32     i, j;
    LLMatrix4   mat;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][i] * b;
        }
    }
    a = mat;
    return a;
}

inline constexpr const LLMatrix4& operator+=(LLMatrix4 &a, const LLMatrix4 &b) noexcept
{
    LLMatrix4 mat;
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][i] + b.mMatrix[j][i];
        }
    }
    a = mat;
    return a;
}

inline constexpr const LLMatrix4& operator-=(LLMatrix4 &a, const LLMatrix4 &b) noexcept
{
    LLMatrix4 mat;
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT4; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT4; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][i] - b.mMatrix[j][i];
        }
    }
    a = mat;
    return a;
}

// Operates "to the left" on row-vector a
//
// When avatar vertex programs are off, this function is a hot spot in profiles
// due to software skinning in LLViewerJointMesh::updateGeometry().  JC
inline constexpr const LLVector3 operator*(const LLVector3 &a, const LLMatrix4 &b) noexcept
{
    // This is better than making a temporary LLVector3.  This eliminates an
    // unnecessary LLVector3() constructor and also helps the compiler to
    // realize that the output floats do not alias the input floats, hence
    // eliminating redundant loads of a.mV[0], etc.  JC
    return LLVector3(a.mV[VX] * b.mMatrix[VX][VX] +
                     a.mV[VY] * b.mMatrix[VY][VX] +
                     a.mV[VZ] * b.mMatrix[VZ][VX] +
                     b.mMatrix[VW][VX],

                     a.mV[VX] * b.mMatrix[VX][VY] +
                     a.mV[VY] * b.mMatrix[VY][VY] +
                     a.mV[VZ] * b.mMatrix[VZ][VY] +
                     b.mMatrix[VW][VY],

                     a.mV[VX] * b.mMatrix[VX][VZ] +
                     a.mV[VY] * b.mMatrix[VY][VZ] +
                     a.mV[VZ] * b.mMatrix[VZ][VZ] +
                     b.mMatrix[VW][VZ]);
}

#endif

