/**
 * @file m3math_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief Test cases of m3math.h
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "../m3math.h"
#include "../v3math.h"
#include "../v4math.h"
#include "../m4math.h"
#include "../llquaternion.h"
#include "../v3dmath.h"
#include "altestmatchers.h"

#include <catch2/catch_test_macros.hpp>

#if LL_WINDOWS
// disable unreachable code warnings caused by usage of skip.
#pragma warning(disable: 4702)
#endif

namespace
{
    //test case for setIdentity() fn.
    TEST_CASE("m3math_h: setIdentity makes the identity matrix", "[m3math_h]")
    {
        LLMatrix3 llmat3_obj;
        llmat3_obj.setIdentity();
        CHECK(llmat3_obj.mMatrix[0][0] == 1.f);
        CHECK(llmat3_obj.mMatrix[0][1] == 0.f);
        CHECK(llmat3_obj.mMatrix[0][2] == 0.f);
        CHECK(llmat3_obj.mMatrix[1][0] == 0.f);
        CHECK(llmat3_obj.mMatrix[1][1] == 1.f);
        CHECK(llmat3_obj.mMatrix[1][2] == 0.f);
        CHECK(llmat3_obj.mMatrix[2][0] == 0.f);
        CHECK(llmat3_obj.mMatrix[2][1] == 0.f);
        CHECK(llmat3_obj.mMatrix[2][2] == 1.f);
    }

    //test case for LLMatrix3& setZero() fn.
    TEST_CASE("m3math_h: setZero clears every element", "[m3math_h]")
    {
        LLMatrix3 llmat3_obj;
        llmat3_obj.setZero();

        CHECK(llmat3_obj.setZero().mMatrix[0][0] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[0][1] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[0][2] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[1][0] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[1][1] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[1][2] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[2][0] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[2][1] == 0.f);
        CHECK(llmat3_obj.setZero().mMatrix[2][2] == 0.f);
    }

    //test case for setRows(const LLVector3 &x_axis, const LLVector3 &y_axis, const LLVector3 &z_axis) fns.
    TEST_CASE("m3math_h: setRows fills the matrix row by row", "[m3math_h]")
    {
        LLMatrix3 llmat3_obj;
        LLVector3 vect1(2, 1, 4);
        LLVector3 vect2(3, 5, 7);
        LLVector3 vect3(6, 9, 7);
        llmat3_obj.setRows(vect1, vect2, vect3);
        CHECK(llmat3_obj.mMatrix[0][0] == 2);
        CHECK(llmat3_obj.mMatrix[0][1] == 1);
        CHECK(llmat3_obj.mMatrix[0][2] == 4);
        CHECK(llmat3_obj.mMatrix[1][0] == 3);
        CHECK(llmat3_obj.mMatrix[1][1] == 5);
        CHECK(llmat3_obj.mMatrix[1][2] == 7);
        CHECK(llmat3_obj.mMatrix[2][0] == 6);
        CHECK(llmat3_obj.mMatrix[2][1] == 9);
        CHECK(llmat3_obj.mMatrix[2][2] == 7);
    }

    //test case for getFwdRow(), getLeftRow(), getUpRow() fns.
    TEST_CASE("m3math_h: getFwdRow/getLeftRow/getUpRow return the rows set", "[m3math_h]")
    {
        LLMatrix3 llmat3_obj;
        LLVector3 vect1(2, 1, 4);
        LLVector3 vect2(3, 5, 7);
        LLVector3 vect3(6, 9, 7);
        llmat3_obj.setRows(vect1, vect2, vect3);

        CHECK(vect1 == llmat3_obj.getFwdRow());
        CHECK(vect2 == llmat3_obj.getLeftRow());
        CHECK(vect3 == llmat3_obj.getUpRow());
    }

    //test case for operator*(const LLMatrix3 &a, const LLMatrix3 &b)
    TEST_CASE("m3math_h: operator* multiplies two matrices", "[m3math_h]")
    {
        LLMatrix3 llmat_obj1;
        LLMatrix3 llmat_obj2;
        LLMatrix3 llmat_obj3;

        LLVector3 llvec1(1, 3, 5);
        LLVector3 llvec2(3, 6, 1);
        LLVector3 llvec3(4, 6, 9);

        LLVector3 llvec4(1, 1, 5);
        LLVector3 llvec5(3, 6, 8);
        LLVector3 llvec6(8, 6, 2);

        LLVector3 llvec7(0, 0, 0);
        LLVector3 llvec8(0, 0, 0);
        LLVector3 llvec9(0, 0, 0);

        llmat_obj1.setRows(llvec1, llvec2, llvec3);
        llmat_obj2.setRows(llvec4, llvec5, llvec6);
        llmat_obj3.setRows(llvec7, llvec8, llvec9);
        llmat_obj3 = llmat_obj1 * llmat_obj2;
        CHECK(llmat_obj3.mMatrix[0][0] == 50);
        CHECK(llmat_obj3.mMatrix[0][1] == 49);
        CHECK(llmat_obj3.mMatrix[0][2] == 39);
        CHECK(llmat_obj3.mMatrix[1][0] == 29);
        CHECK(llmat_obj3.mMatrix[1][1] == 45);
        CHECK(llmat_obj3.mMatrix[1][2] == 65);
        CHECK(llmat_obj3.mMatrix[2][0] == 94);
        CHECK(llmat_obj3.mMatrix[2][1] == 94);
        CHECK(llmat_obj3.mMatrix[2][2] == 86);
    }


    //test case for operator*(const LLVector3 &a, const LLMatrix3 &b)
    TEST_CASE("m3math_h: operator* transforms an LLVector3 by a matrix", "[m3math_h]")
    {

        LLMatrix3 llmat_obj1;

        LLVector3 llvec(1, 3, 5);
        LLVector3 res_vec(0, 0, 0);
        LLVector3 llvec1(1, 3, 5);
        LLVector3 llvec2(3, 6, 1);
        LLVector3 llvec3(4, 6, 9);

        llmat_obj1.setRows(llvec1, llvec2, llvec3);
        res_vec = llvec * llmat_obj1;

        LLVector3 expected_result(30, 51, 53);

        CHECK(res_vec == expected_result);
    }

    //test case for operator*(const LLVector3d &a, const LLMatrix3 &b)
    TEST_CASE("m3math_h: operator* transforms an LLVector3d by a matrix", "[m3math_h]")
    {
        LLMatrix3 llmat_obj1;
        LLVector3d llvec3d1;
        LLVector3d llvec3d2(0, 3, 4);

        LLVector3 llvec1(1, 3, 5);
        LLVector3 llvec2(3, 2, 1);
        LLVector3 llvec3(4, 6, 0);

        llmat_obj1.setRows(llvec1, llvec2, llvec3);
        llvec3d1 = llvec3d2 * llmat_obj1;

        LLVector3d expected_result(25, 30, 3);

        CHECK(llvec3d1 == expected_result);
    }

    // test case for operator==(const LLMatrix3 &a, const LLMatrix3 &b)
    TEST_CASE("m3math_h: operator== and operator!= compare matrices", "[m3math_h]")
    {
        LLMatrix3 llmat_obj1;
        LLMatrix3 llmat_obj2;

        LLVector3 llvec1(1, 3, 5);
        LLVector3 llvec2(3, 6, 1);
        LLVector3 llvec3(4, 6, 9);

        llmat_obj1.setRows(llvec1, llvec2, llvec3);
        llmat_obj2.setRows(llvec1, llvec2, llvec3);
        CHECK(llmat_obj1 == llmat_obj2);

        llmat_obj2.setRows(llvec2, llvec2, llvec3);
        CHECK(llmat_obj1 != llmat_obj2);
    }

    //test case for quaternion() fn.
    TEST_CASE("m3math_h: quaternion converts the matrix to a quaternion", "[m3math_h]")
    {
        LLMatrix3 llmat_obj1;
        LLQuaternion llmat_quat;

        LLVector3 llmat1(2.0f, 1.0f, 6.0f);
        LLVector3 llmat2(1.0f, 1.0f, 3.0f);
        LLVector3 llmat3(1.0f, 7.0f, 5.0f);

        llmat_obj1.setRows(llmat1, llmat2, llmat3);
        llmat_quat = llmat_obj1.quaternion();
        // Within the rounding of terms no larger than 5, the rows' differences
        // the quaternion is worked from, which the tree's reciprocal and
        // reordered arithmetic may round a step or two either way.
        CHECK_THAT(llmat_quat.mQ[0], ll_test::WithinRounding(-0.66666669f, 5.f));
        CHECK_THAT(llmat_quat.mQ[1], ll_test::WithinRounding(-0.83333337f, 5.f));
        CHECK_THAT(llmat_quat.mQ[2], ll_test::WithinRounding(0.0f, 5.f));
        CHECK_THAT(llmat_quat.mQ[3], ll_test::WithinRounding(1.5f, 5.f));
    }

    //test case for transpose() fn.
    TEST_CASE("m3math_h: transpose swaps rows and columns", "[m3math_h]")
    {
        LLMatrix3 llmat_obj;

        LLVector3 llvec1(1, 2, 3);
        LLVector3 llvec2(3, 2, 1);
        LLVector3 llvec3(2, 2, 2);

        llmat_obj.setRows(llvec1, llvec2, llvec3);
        llmat_obj.transpose();

        LLVector3 resllvec1(1, 3, 2);
        LLVector3 resllvec2(2, 2, 2);
        LLVector3 resllvec3(3, 1, 2);
        LLMatrix3 expectedllmat_obj;
        expectedllmat_obj.setRows(resllvec1, resllvec2, resllvec3);

        CHECK(llmat_obj == expectedllmat_obj);
    }

    //test case for determinant() fn.
    TEST_CASE("m3math_h: determinant of a singular matrix is zero", "[m3math_h]")
    {
        LLMatrix3 llmat_obj1;

        LLVector3 llvec1(1, 2, 3);
        LLVector3 llvec2(3, 2, 1);
        LLVector3 llvec3(2, 2, 2);
        llmat_obj1.setRows(llvec1, llvec2, llvec3);
        CHECK(llmat_obj1.determinant() == 0.0f);
    }

    //test case for orthogonalize() fn.
    TEST_CASE("m3math_h: orthogonalize makes the rows orthonormal", "[m3math_h]")
    {
        LLMatrix3 llmat_obj;

        LLVector3 llvec1(1, 4, 3);
        LLVector3 llvec2(1, 2, 0);
        LLVector3 llvec3(2, 4, 2);

        SKIP("This test fails depending on architecture. Need to fix comparison operation, is_approx_equal, to work on more than one platform.");

        llmat_obj.setRows(llvec1, llvec2, llvec3);
        llmat_obj.orthogonalize();

        CHECK(is_approx_equal(0.19611614f, llmat_obj.mMatrix[0][0]));
        CHECK(is_approx_equal(0.78446454f, llmat_obj.mMatrix[0][1]));
        CHECK(is_approx_equal(0.58834841f, llmat_obj.mMatrix[0][2]));
        CHECK(is_approx_equal(0.47628204f, llmat_obj.mMatrix[1][0]));
        CHECK(is_approx_equal(0.44826545f, llmat_obj.mMatrix[1][1]));
        CHECK(is_approx_equal(-0.75644795f, llmat_obj.mMatrix[1][2]));
        CHECK(is_approx_equal(-0.85714286f, llmat_obj.mMatrix[2][0]));
        CHECK(is_approx_equal(0.42857143f, llmat_obj.mMatrix[2][1]));
        CHECK(is_approx_equal(-0.28571429f, llmat_obj.mMatrix[2][2]));
    }

    //test case for adjointTranspose() fn.
    TEST_CASE("m3math_h: adjointTranspose gives the expected cofactors", "[m3math_h]")
    {
        LLMatrix3 llmat_obj;

        LLVector3 llvec1(3, 2, 1);
        LLVector3 llvec2(6, 2, 1);
        LLVector3 llvec3(3, 6, 8);

        llmat_obj.setRows(llvec1, llvec2, llvec3);
        llmat_obj.adjointTranspose();

        CHECK(llmat_obj.mMatrix[0][0] == 10);
        CHECK(llmat_obj.mMatrix[1][0] == -45);
        CHECK(llmat_obj.mMatrix[2][0] == 30);
        CHECK(llmat_obj.mMatrix[0][1] == -10);
        CHECK(llmat_obj.mMatrix[1][1] == 21);
        CHECK(llmat_obj.mMatrix[2][1] == -12);
        CHECK(llmat_obj.mMatrix[0][2] == 0);
        CHECK(llmat_obj.mMatrix[1][2] == 3);
        CHECK(llmat_obj.mMatrix[2][2] == -6);
    }

    /* TBD: Need to add test cases for getEulerAngles() and setRot() functions */

    TEST_CASE("m3math_h: field-only operations of both matrices evaluate at compile time", "[m3math_h]")
    {
        constexpr LLMatrix3 product = []() constexpr
        {
            const F32 a[9] = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 10.f };
            const F32 b[9] = { 2.f, 0.f, 1.f, 1.f, 3.f, 0.f, 0.f, 1.f, 4.f };
            return LLMatrix3(a) * LLMatrix3(b);
        }();
        static_assert(product.mMatrix[0][0] == 4.f && product.mMatrix[0][1] == 9.f && product.mMatrix[0][2] == 13.f);
        static_assert(product.mMatrix[1][0] == 13.f && product.mMatrix[1][1] == 21.f && product.mMatrix[1][2] == 28.f);
        static_assert(product.mMatrix[2][0] == 22.f && product.mMatrix[2][1] == 34.f && product.mMatrix[2][2] == 47.f);

        constexpr F32 det = []() constexpr
        {
            const F32 a[9] = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 10.f };
            return LLMatrix3(a).determinant();
        }();
        static_assert(det == -3.f);

        constexpr LLMatrix3 transposed = []() constexpr
        {
            const F32 a[9] = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 10.f };
            LLMatrix3 m(a);
            m.transpose();
            m *= 2.f;
            return m;
        }();
        static_assert(transposed.mMatrix[0][1] == 8.f && transposed.mMatrix[1][0] == 4.f);
        static_assert(transposed != product && transposed == transposed);

        constexpr LLMatrix4 frame = []() constexpr
        {
            LLMatrix4 m;
            m.setTranslation(1.f, 2.f, 3.f);
            m.setFwdRow(LLVector3(0.f, 1.f, 0.f));
            m.setLeftRow(LLVector3(-1.f, 0.f, 0.f));
            return m;
        }();
        static_assert(frame.getTranslation() == LLVector3(1.f, 2.f, 3.f));
        static_assert((LLVector3(1.f, 0.f, 0.f) * frame) == LLVector3(1.f, 3.f, 3.f));
        static_assert(rotate_vector(LLVector3(1.f, 0.f, 0.f), frame) == LLVector3(0.f, 1.f, 0.f));
        static_assert(frame.determinant() == 1.f);
        static_assert(!frame.isIdentity() && frame != LLMatrix4());

        constexpr LLMatrix4 inverse = []() constexpr
        {
            LLMatrix4 m;
            m.setTranslation(LLVector3(1.f, 2.f, 3.f));
            m.invert();
            return m;
        }();
        static_assert(inverse.getTranslation() == LLVector3(-1.f, -2.f, -3.f));

        constexpr LLMatrix4 sum = []() constexpr
        {
            LLMatrix4 m, n;
            m.setZero();
            m += n;
            m -= n;
            m += n;
            m *= 3.f;
            m *= n;
            return m;
        }();
        static_assert(sum.mMatrix[0][0] == 3.f && sum.mMatrix[0][1] == 0.f && sum.mMatrix[3][3] == 3.f);

        {
            INFO("compile-time matrices agree at run time");
            CHECK(product.mMatrix[2][2] == 47.f);
            CHECK(inverse.getTranslation().mV[2] == -3.f);
        }
    }
}
