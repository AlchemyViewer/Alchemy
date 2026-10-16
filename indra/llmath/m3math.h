/**
 * @file m3math.h
 * @brief LLMatrix3 class header file.
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

#ifndef LL_M3MATH_H
#define LL_M3MATH_H

#include "llerror.h"
#include "stdtypes.h"
#include "lldefs.h"

class LLVector4;
class LLVector3;
class LLVector3d;
class LLQuaternion;

// NOTA BENE: Currently assuming a right-handed, z-up universe

//               ji
// LLMatrix3 = | 00 01 02 |
//             | 10 11 12 |
//             | 20 21 22 |

// LLMatrix3 = | fx fy fz | forward-axis
//             | lx ly lz | left-axis
//             | ux uy uz | up-axis

// NOTE: The world of computer graphics uses column-vectors and matricies that
// "operate to the left".


static const U32 NUM_VALUES_IN_MAT3 = 3;
class LLMatrix3
{
    public:
        F32 mMatrix[NUM_VALUES_IN_MAT3][NUM_VALUES_IN_MAT3] {
            {1.f, 0.f, 0.f},
            {0.f, 1.f, 0.f},
            {0.f, 0.f, 1.f}
        };

        constexpr LLMatrix3() noexcept = default;           // Initializes Matrix to identity matrix
        constexpr explicit LLMatrix3(const F32 *mat) noexcept; // Initializes Matrix to values in mat
        explicit LLMatrix3(const LLQuaternion &q);          // Initializes Matrix with rotation q

        LLMatrix3(const F32 angle, const LLVector3 &vec);   // Initializes Matrix with axis angle
        LLMatrix3(const F32 angle, const LLVector3d &vec);  // Initializes Matrix with axis angle
        LLMatrix3(const F32 angle, const LLVector4 &vec);   // Initializes Matrix with axis angle
        LLMatrix3(const F32 roll, const F32 pitch, const F32 yaw);  // Initializes Matrix with Euler angles

        //////////////////////////////
        //
        // Matrix initializers - these replace any existing values in the matrix
        //

        // various useful matrix functions
        constexpr const LLMatrix3& setIdentity() noexcept;  // Load identity matrix
        constexpr const LLMatrix3& clear() noexcept;        // Clears Matrix to zero
        constexpr const LLMatrix3& setZero() noexcept;      // Clears Matrix to zero

        ///////////////////////////
        //
        // Matrix setters - set some properties without modifying others
        //

        // These functions take Rotation arguments
        const LLMatrix3& setRot(const F32 angle, const LLVector3 &vec); // Calculate rotation matrix for rotating angle radians about vec
        const LLMatrix3& setRot(const F32 roll, const F32 pitch, const F32 yaw);    // Calculate rotation matrix from Euler angles
        const LLMatrix3& setRot(const LLQuaternion &q);         // Transform matrix by Euler angles and translating by pos

        const LLMatrix3& setRows(const LLVector3 &x_axis, const LLVector3 &y_axis, const LLVector3 &z_axis);
        const LLMatrix3& setRow( U32 rowIndex, const LLVector3& row );
        const LLMatrix3& setCol( U32 colIndex, const LLVector3& col );


        ///////////////////////////
        //
        // Get properties of a matrix
        //
        LLQuaternion quaternion() const;        // Returns quaternion from mat
        void getEulerAngles(F32 *roll, F32 *pitch, F32 *yaw) const; // Returns Euler angles, in radians

        // Axis extraction routines
        LLVector3 getFwdRow() const;
        LLVector3 getLeftRow() const;
        LLVector3 getUpRow() const;
        constexpr F32 determinant() const noexcept; // Return determinant


        ///////////////////////////
        //
        // Operations on an existing matrix
        //
        constexpr const LLMatrix3& transpose() noexcept;        // Transpose MAT4
        const LLMatrix3& orthogonalize();   // Orthogonalizes X, then Y, then Z
        void invert();          // Invert MAT4
        constexpr const LLMatrix3& adjointTranspose() noexcept; // returns transpose of matrix adjoint, for multiplying normals


        // Rotate existing matrix
        // Note: the two lines below are equivalent:
        //  foo.rotate(bar)
        //  foo = foo * bar
        // That is, foo.rotate(bar) multiplies foo by bar FROM THE RIGHT
        const LLMatrix3& rotate(const F32 angle, const F32 x, const F32 y, const F32 z);    // Rotate matrix by rotating angle radians about (x, y, z)
        const LLMatrix3& rotate(const F32 angle, const LLVector3 &vec);                     // Rotate matrix by rotating angle radians about vec
        const LLMatrix3& rotate(const F32 roll, const F32 pitch, const F32 yaw);            // Rotate matrix by roll (about x), pitch (about y), and yaw (about z)
        const LLMatrix3& rotate(const LLQuaternion &q);         // Transform matrix by Euler angles and translating by pos

        constexpr void add(const LLMatrix3& other_matrix) noexcept;    // add other_matrix to this one

// This operator is misleading as to operation direction
//      friend LLVector3 operator*(const LLMatrix3 &a, const LLVector3 &b);         // Apply rotation a to vector b

        friend LLVector3 operator*(const LLVector3 &a, const LLMatrix3 &b);         // Apply rotation b to vector a
        friend LLVector3d operator*(const LLVector3d &a, const LLMatrix3 &b);           // Apply rotation b to vector a
        friend constexpr LLMatrix3 operator*(const LLMatrix3 &a, const LLMatrix3 &b) noexcept;  // Return a * b

        friend constexpr bool operator==(const LLMatrix3 &a, const LLMatrix3 &b) noexcept;      // Return a == b
        friend constexpr bool operator!=(const LLMatrix3 &a, const LLMatrix3 &b) noexcept;      // Return a != b

        friend constexpr const LLMatrix3& operator*=(LLMatrix3 &a, const LLMatrix3 &b) noexcept;    // Return a * b
        friend constexpr const LLMatrix3& operator*=(LLMatrix3 &a, F32 scalar ) noexcept;           // Return a * scalar

        friend std::ostream&     operator<<(std::ostream& s, const LLMatrix3 &a);   // Stream a
};

static_assert(std::is_trivially_copyable<LLMatrix3>::value, "LLMatrix3 must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLMatrix3>::value, "LLMatrix3 must be trivial move");
static_assert(std::is_standard_layout<LLMatrix3>::value, "LLMatrix3 must be a standard layout type");

constexpr LLMatrix3::LLMatrix3(const F32 *mat) noexcept
{
    mMatrix[0][0] = mat[0];
    mMatrix[0][1] = mat[1];
    mMatrix[0][2] = mat[2];

    mMatrix[1][0] = mat[3];
    mMatrix[1][1] = mat[4];
    mMatrix[1][2] = mat[5];

    mMatrix[2][0] = mat[6];
    mMatrix[2][1] = mat[7];
    mMatrix[2][2] = mat[8];
}

constexpr const LLMatrix3& LLMatrix3::setIdentity() noexcept
{
    mMatrix[0][0] = 1.f;
    mMatrix[0][1] = 0.f;
    mMatrix[0][2] = 0.f;

    mMatrix[1][0] = 0.f;
    mMatrix[1][1] = 1.f;
    mMatrix[1][2] = 0.f;

    mMatrix[2][0] = 0.f;
    mMatrix[2][1] = 0.f;
    mMatrix[2][2] = 1.f;
    return (*this);
}

constexpr const LLMatrix3& LLMatrix3::clear() noexcept
{
    mMatrix[0][0] = 0.f;
    mMatrix[0][1] = 0.f;
    mMatrix[0][2] = 0.f;

    mMatrix[1][0] = 0.f;
    mMatrix[1][1] = 0.f;
    mMatrix[1][2] = 0.f;

    mMatrix[2][0] = 0.f;
    mMatrix[2][1] = 0.f;
    mMatrix[2][2] = 0.f;
    return (*this);
}

constexpr const LLMatrix3& LLMatrix3::setZero() noexcept
{
    mMatrix[0][0] = 0.f;
    mMatrix[0][1] = 0.f;
    mMatrix[0][2] = 0.f;

    mMatrix[1][0] = 0.f;
    mMatrix[1][1] = 0.f;
    mMatrix[1][2] = 0.f;

    mMatrix[2][0] = 0.f;
    mMatrix[2][1] = 0.f;
    mMatrix[2][2] = 0.f;
    return (*this);
}

constexpr const LLMatrix3& LLMatrix3::transpose() noexcept
{
    // transpose the matrix
    F32 temp;
    temp = mMatrix[VX][VY]; mMatrix[VX][VY] = mMatrix[VY][VX]; mMatrix[VY][VX] = temp;
    temp = mMatrix[VX][VZ]; mMatrix[VX][VZ] = mMatrix[VZ][VX]; mMatrix[VZ][VX] = temp;
    temp = mMatrix[VY][VZ]; mMatrix[VY][VZ] = mMatrix[VZ][VY]; mMatrix[VZ][VY] = temp;
    return *this;
}

constexpr F32 LLMatrix3::determinant() const noexcept
{
    // Is this a useful method when we assume the matrices are valid rotation
    // matrices throughout this implementation?
    return  mMatrix[0][0] * (mMatrix[1][1] * mMatrix[2][2] - mMatrix[1][2] * mMatrix[2][1]) +
            mMatrix[0][1] * (mMatrix[1][2] * mMatrix[2][0] - mMatrix[1][0] * mMatrix[2][2]) +
            mMatrix[0][2] * (mMatrix[1][0] * mMatrix[2][1] - mMatrix[1][1] * mMatrix[2][0]);
}

// does not assume a rotation matrix, and does not divide by determinant, assuming results will be renormalized
constexpr const LLMatrix3& LLMatrix3::adjointTranspose() noexcept
{
    LLMatrix3 adjoint_transpose;
    adjoint_transpose.mMatrix[VX][VX] = mMatrix[VY][VY] * mMatrix[VZ][VZ] - mMatrix[VY][VZ] * mMatrix[VZ][VY] ;
    adjoint_transpose.mMatrix[VY][VX] = mMatrix[VY][VZ] * mMatrix[VZ][VX] - mMatrix[VY][VX] * mMatrix[VZ][VZ] ;
    adjoint_transpose.mMatrix[VZ][VX] = mMatrix[VY][VX] * mMatrix[VZ][VY] - mMatrix[VY][VY] * mMatrix[VZ][VX] ;
    adjoint_transpose.mMatrix[VX][VY] = mMatrix[VZ][VY] * mMatrix[VX][VZ] - mMatrix[VZ][VZ] * mMatrix[VX][VY] ;
    adjoint_transpose.mMatrix[VY][VY] = mMatrix[VZ][VZ] * mMatrix[VX][VX] - mMatrix[VZ][VX] * mMatrix[VX][VZ] ;
    adjoint_transpose.mMatrix[VZ][VY] = mMatrix[VZ][VX] * mMatrix[VX][VY] - mMatrix[VZ][VY] * mMatrix[VX][VX] ;
    adjoint_transpose.mMatrix[VX][VZ] = mMatrix[VX][VY] * mMatrix[VY][VZ] - mMatrix[VX][VZ] * mMatrix[VY][VY] ;
    adjoint_transpose.mMatrix[VY][VZ] = mMatrix[VX][VZ] * mMatrix[VY][VX] - mMatrix[VX][VX] * mMatrix[VY][VZ] ;
    adjoint_transpose.mMatrix[VZ][VZ] = mMatrix[VX][VX] * mMatrix[VY][VY] - mMatrix[VX][VY] * mMatrix[VY][VX] ;

    *this = adjoint_transpose;
    return *this;
}

constexpr void LLMatrix3::add(const LLMatrix3& other_matrix) noexcept
{
    for (S32 i = 0; i < 3; ++i)
    {
        for (S32 j = 0; j < 3; ++j)
        {
            mMatrix[i][j] += other_matrix.mMatrix[i][j];
        }
    }
}

inline constexpr LLMatrix3 operator*(const LLMatrix3 &a, const LLMatrix3 &b) noexcept
{
    U32     i, j;
    LLMatrix3   mat;
    for (i = 0; i < NUM_VALUES_IN_MAT3; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT3; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][0] * b.mMatrix[0][i] +
                                a.mMatrix[j][1] * b.mMatrix[1][i] +
                                a.mMatrix[j][2] * b.mMatrix[2][i];
        }
    }
    return mat;
}

inline constexpr bool operator==(const LLMatrix3 &a, const LLMatrix3 &b) noexcept
{
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT3; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT3; j++)
        {
            if (a.mMatrix[j][i] != b.mMatrix[j][i])
                return false;
        }
    }
    return true;
}

inline constexpr bool operator!=(const LLMatrix3 &a, const LLMatrix3 &b) noexcept
{
    U32     i, j;
    for (i = 0; i < NUM_VALUES_IN_MAT3; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT3; j++)
        {
            if (a.mMatrix[j][i] != b.mMatrix[j][i])
                return true;
        }
    }
    return false;
}

inline constexpr const LLMatrix3& operator*=(LLMatrix3 &a, const LLMatrix3 &b) noexcept
{
    U32     i, j;
    LLMatrix3   mat;
    for (i = 0; i < NUM_VALUES_IN_MAT3; i++)
    {
        for (j = 0; j < NUM_VALUES_IN_MAT3; j++)
        {
            mat.mMatrix[j][i] = a.mMatrix[j][0] * b.mMatrix[0][i] +
                                a.mMatrix[j][1] * b.mMatrix[1][i] +
                                a.mMatrix[j][2] * b.mMatrix[2][i];
        }
    }
    a = mat;
    return a;
}

inline constexpr const LLMatrix3& operator*=(LLMatrix3 &a, F32 scalar) noexcept
{
    for( U32 i = 0; i < NUM_VALUES_IN_MAT3; ++i )
    {
        for( U32 j = 0; j < NUM_VALUES_IN_MAT3; ++j )
        {
            a.mMatrix[i][j] *= scalar;
        }
    }

    return a;
}

#endif


// Rotation matrix hints...

// Inverse of Rotation Matrices
// ----------------------------
// If R is a rotation matrix that rotate vectors from Frame-A to Frame-B,
// then the transpose of R will rotate vectors from Frame-B to Frame-A.


// Creating Rotation Matricies From Object Axes
// --------------------------------------------
// Suppose you know the three axes of some object in some "absolute-frame".
// If you take those three vectors and throw them into the rows of
// a rotation matrix what do you get?
//
// R = | X0  X1  X2 |
//     | Y0  Y1  Y2 |
//     | Z0  Z1  Z2 |
//
// Yeah, but what does it mean?
//
// Transpose the matrix and have it operate on a vector...
//
// V * R_transpose = [ V0  V1  V2 ] * | X0  Y0  Z0 |
//                                    | X1  Y1  Z1 |
//                                    | X2  Y2  Z2 |
//
//                 = [ V*X  V*Y  V*Z ]
//
//                 = components of V that are parallel to the three object axes
//
//                 = transformation of V into object frame
//
// Since the transformation of a rotation matrix is its inverse, then
// R must rotate vectors from the object-frame into the absolute-frame.



