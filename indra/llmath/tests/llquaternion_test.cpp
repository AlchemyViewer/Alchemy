/**
 * @file llquaternion_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief Test cases of llquaternion.h
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
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string_view>

#include "../v4math.h"
#include "../v3math.h"
#include "../v3dmath.h"
#include "../m4math.h"
#include "../m3math.h"
#include "../llquaternion.h"
#include "altestmatchers.h"

namespace
{
    //test case for LLQuaternion::LLQuaternion(void) fn.
    TEST_CASE("LLQuaternion: default constructor gives the identity quaternion", "[LLQuaternion]")
    {
        LLQuaternion llquat;
        CHECK(llquat.mQ[0] == 0.f);
        CHECK(llquat.mQ[1] == 0.f);
        CHECK(llquat.mQ[2] == 0.f);
        CHECK(llquat.mQ[3] == 1.f);
    }

    // The rotation of a matrix comes back as the quaternion it was made from,
    // or its negation, which is the same rotation.
    void require_same_rotation(const LLQuaternion& actual, const LLQuaternion& expected)
    {
        using Catch::Matchers::WithinAbs;
        INFO("expected " << expected << ", got " << actual);
        const F32 sign = dot(actual, expected) < 0.f ? -1.f : 1.f;
        for (int i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            REQUIRE_THAT(actual.mQ[i], WithinAbs(sign * expected.mQ[i], 1e-6));
        }
    }

    //test case for explicit LLQuaternion(const LLMatrix4 &mat) fn.
    TEST_CASE("LLQuaternion: from an LLMatrix4, the matrix's rotation, its translation ignored", "[LLQuaternion]")
    {
        CHECK(LLQuaternion(LLMatrix4()) == LLQuaternion::DEFAULT);

        const LLQuaternion turn(1.1f, LLVector3(2.f, -1.f, 3.f));
        require_same_rotation(LLQuaternion(LLMatrix4(turn)), turn);
        require_same_rotation(LLQuaternion(LLMatrix4(turn, LLVector4(5.f, -6.f, 7.f, 1.f))), turn);

        // Past a half turn, where w is negative.
        const LLQuaternion wide(4.f, LLVector3(-1.f, 0.5f, 0.25f));
        require_same_rotation(LLQuaternion(LLMatrix4(wide)), wide);
    }

    //test case for explicit LLQuaternion(const LLMatrix3 &mat) fn.
    TEST_CASE("LLQuaternion: from an LLMatrix3, the matrix's rotation", "[LLQuaternion]")
    {
        CHECK(LLQuaternion(LLMatrix3()) == LLQuaternion::DEFAULT);

        const LLQuaternion turn(1.1f, LLVector3(2.f, -1.f, 3.f));
        require_same_rotation(LLQuaternion(LLMatrix3(turn)), turn);
        const LLQuaternion wide(4.f, LLVector3(-1.f, 0.5f, 0.25f));
        require_same_rotation(LLQuaternion(LLMatrix3(wide)), wide);
    }

    TEST_CASE("LLQuaternion: LLMatrix4 initRows fills the matrix row by row", "[LLQuaternion]")
    {
        LLMatrix4 llmat;
        LLVector4 vector1(2.0f, 1.0f, 3.0f, 6.0f);
        LLVector4 vector2(5.0f, 6.0f, 0.0f, 1.0f);
        LLVector4 vector3(2.0f, 1.0f, 2.0f, 9.0f);
        LLVector4 vector4(3.0f, 8.0f, 1.0f, 5.0f);

        llmat.initRows(vector1, vector2, vector3, vector4);
        CHECK(llmat.mMatrix[0][0] == 2.0f);
        CHECK(llmat.mMatrix[0][1] == 1.0f);
        CHECK(llmat.mMatrix[0][2] == 3.0f);
        CHECK(llmat.mMatrix[0][3] == 6.0f);
        CHECK(llmat.mMatrix[1][0] == 5.0f);
        CHECK(llmat.mMatrix[1][1] == 6.0f);
        CHECK(llmat.mMatrix[1][2] == 0.0f);
        CHECK(llmat.mMatrix[1][3] == 1.0f);
        CHECK(llmat.mMatrix[2][0] == 2.0f);
        CHECK(llmat.mMatrix[2][1] == 1.0f);
        CHECK(llmat.mMatrix[2][2] == 2.0f);
        CHECK(llmat.mMatrix[2][3] == 9.0f);
        CHECK(llmat.mMatrix[3][0] == 3.0f);
        CHECK(llmat.mMatrix[3][1] == 8.0f);
        CHECK(llmat.mMatrix[3][2] == 1.0f);
        CHECK(llmat.mMatrix[3][3] == 5.0f);
    }

    TEST_CASE("LLQuaternion: LLMatrix3 setRows keeps each float exactly", "[LLQuaternion]")
    {
        LLMatrix3 llmat;

        LLVector3 vect1(3.4028234660000000f , 234.56f, 4234.442234f);
        LLVector3 vect2(741.434f, 23.00034f, 6567.223423f);
        LLVector3 vect3(566.003034f, 12.98705f, 234.764423f);
        llmat.setRows(vect1, vect2, vect3);

        CHECK(llmat.mMatrix[0][0] == 3.4028234660000000f);
        CHECK(llmat.mMatrix[0][1] == 234.56f);
        CHECK(llmat.mMatrix[0][2] == 4234.442234f);
        CHECK(llmat.mMatrix[1][0] == 741.434f);
        CHECK(llmat.mMatrix[1][1] == 23.00034f);
        CHECK(llmat.mMatrix[1][2] == 6567.223423f);
        CHECK(llmat.mMatrix[2][0] == 566.003034f);
        CHECK(llmat.mMatrix[2][1] == 12.98705f);
        CHECK(llmat.mMatrix[2][2] == 234.764423f);
    }

    //test case for LLQuaternion(F32 x, F32 y, F32 z, F32 w), setQuatInit() and normQuat() fns.
    TEST_CASE("LLQuaternion: normQuat normalises and turns a zero quaternion into identity", "[LLQuaternion]")
    {
        F32 x_val = 3.0f;
        F32 y_val = 2.0f;
        F32 z_val = 6.0f;
        F32 w_val = 1.0f;

        LLQuaternion res_quat;
        res_quat.setQuatInit(x_val, y_val, z_val, w_val);
        res_quat.normQuat();

        CHECK(is_approx_equal(0.42426407f, res_quat.mQ[0]));
        CHECK(is_approx_equal(0.28284273f, res_quat.mQ[1]));
        CHECK(is_approx_equal(0.84852815f, res_quat.mQ[2]));
        CHECK(is_approx_equal(0.14142136f, res_quat.mQ[3]));

        x_val = 0.0f;
        y_val = 0.0f;
        z_val = 0.0f;
        w_val = 0.0f;

        res_quat.setQuatInit(x_val, y_val, z_val, w_val);
        res_quat.normQuat();

        CHECK(is_approx_equal(0.0f, res_quat.mQ[0]));
        CHECK(is_approx_equal(0.0f, res_quat.mQ[1]));
        CHECK(is_approx_equal(0.0f, res_quat.mQ[2]));
        CHECK(is_approx_equal(1.0f, res_quat.mQ[3]));
    }

    //test case for conjQuat() and transQuat() fns.
    TEST_CASE("LLQuaternion: conjQuat and transQuat agree on the vector part", "[LLQuaternion]")
    {
        F32 x_val = 3.0f;
        F32 y_val = 2.0f;
        F32 z_val = 6.0f;
        F32 w_val = 1.0f;

        LLQuaternion res_quat;
        LLQuaternion result, result1;
        result1 = result = res_quat.setQuatInit(x_val, y_val, z_val, w_val);

        result.conjQuat();
        result1.transQuat();

        CHECK(is_approx_equal(result1.mQ[0], result.mQ[0]));
        CHECK(is_approx_equal(result1.mQ[1], result.mQ[1]));
        CHECK(is_approx_equal(result1.mQ[2], result.mQ[2]));

    }

    //test case for dot(const LLQuaternion &a, const LLQuaternion &b) fn.
    TEST_CASE("LLQuaternion: dot sums the products of the four components", "[LLQuaternion]")
    {
        LLQuaternion quat1(3.0f, 2.0f, 6.0f, 0.0f), quat2(1.0f, 1.0f, 1.0f, 1.0f);
        CHECK(ll_round(12.000000f, 2) == ll_round(dot(quat1, quat2), 2));

        LLQuaternion quat0(3.0f, 9.334f, 34.5f, 23.0f), quat(34.5f, 23.23f, 2.0f, 45.5f);
        CHECK(ll_round(1435.828807f, 2) == ll_round(dot(quat0, quat), 2));
    }

    //test case for LLQuaternion &LLQuaternion::constrain(F32 radians) fn.
    TEST_CASE("LLQuaternion: constrain clamps only rotations wider than the cone angle", "[LLQuaternion]")
    {
        F32 radian = 60.0f;
        LLQuaternion quat(3.0f, 2.0f, 6.0f, 0.0f);
        LLQuaternion quat1;
        quat1 = quat.constrain(radian);
        CHECK(is_approx_equal_fraction(-0.423442f, quat1.mQ[0], 8));
        CHECK(is_approx_equal_fraction(-0.282295f, quat1.mQ[1], 8));
        CHECK(is_approx_equal_fraction(-0.846884f, quat1.mQ[2], 8));
        CHECK(is_approx_equal_fraction(0.154251f, quat1.mQ[3], 8));


        radian = 30.0f;
        LLQuaternion quat0(37.50f, 12.0f, 86.023f, 40.32f);
        quat1 = quat0.constrain(radian);

        CHECK(is_approx_equal_fraction(37.500000f, quat1.mQ[0], 8));
        CHECK(is_approx_equal_fraction(12.0000f, quat1.mQ[1], 8));
        CHECK(is_approx_equal_fraction(86.0230f, quat1.mQ[2], 8));
        CHECK(is_approx_equal_fraction(40.320000f, quat1.mQ[3], 8));
    }

    TEST_CASE("LLQuaternion: lerp and slerp and nlerp overloads interpolate", "[LLQuaternion]")
    {
        F32 value1 = 15.0f;
        LLQuaternion quat1(1.0f, 2.0f, 4.0f, 1.0f);
        LLQuaternion quat2(4.0f, 3.0f, 6.5f, 9.7f);
        LLQuaternion res_lerp, res_slerp, res_nlerp;

        //test case for lerp(F32 t, const LLQuaternion &q) fn.
        // value1 = 15, quat1 = (1, 2, 4, 1). lerp from identity to quat1:
        //   pre-normalize r = (15, 30, 60, 1)  (W = 1 + 15*(1 - 1) = 1)
        //   |r| = sqrt(4726) ~= 68.7459
        //   r/|r| ~= (0.218195, 0.436390, 0.872779, 0.014546)
        // Previous "expected" values were derived from a typo that read
        // q.mQ[VZ] for W instead of q.mQ[VW].
        res_lerp = lerp(value1, quat1);
        CHECK(is_approx_equal_fraction(0.218195f, res_lerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.436390f, res_lerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.872779f, res_lerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.014546f, res_lerp.mQ[3], 16));

        //test case for lerp(F32 t, const LLQuaternion &p, const LLQuaternion &q) fn.
        res_lerp = lerp(value1, quat1, quat2);
        CHECK(is_approx_equal_fraction(0.314306f, res_lerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.116156f, res_lerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.283559f, res_lerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.898506f, res_lerp.mQ[3], 16));

        //test case for slerp( F32 u, const LLQuaternion &a, const LLQuaternion &b ) fn.
        res_slerp = slerp(value1, quat1, quat2);
        CHECK(is_approx_equal_fraction(46.000f, res_slerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(17.00f, res_slerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(41.5f, res_slerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(131.5f, res_slerp.mQ[3], 16));

        //test case for nlerp(F32 t, const LLQuaternion &a, const LLQuaternion &b) fn.
        res_nlerp = nlerp(value1, quat1, quat2);
        CHECK(is_approx_equal_fraction(0.314306f, res_nlerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.116157f, res_nlerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.283559f, res_nlerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.898506f, res_nlerp.mQ[3], 16));

        //test case for slerp(F32 u, const LLQuaternion &q) fn.
        res_slerp = slerp(value1, quat1);
        CHECK(is_approx_equal_fraction(1.0f, res_slerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(2.0f, res_slerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(4.0000f, res_slerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(1.000f, res_slerp.mQ[3], 16));

        LLQuaternion quat3(2.0f, 1.0f, 5.5f, 10.5f);
        LLQuaternion res_nlerp1;
        value1 = 100.0f;
        res_nlerp1 = nlerp(value1, quat3);
        // nlerp delegates to lerp(t, q) when q.W >= 0; quat3.W = 10.5 > 0 so
        // this exercises the same fix. value1 = 100, quat3 = (2, 1, 5.5, 10.5):
        //   pre-normalize r = (200, 100, 550, 951)  (W = 1 + 100*(10.5 - 1) = 951)
        //   |r| = sqrt(1256901) ~= 1121.116
        //   r/|r| ~= (0.178394, 0.089197, 0.490583, 0.848262)
        CHECK(is_approx_equal_fraction(0.178394f, res_nlerp1.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.089197f, res_nlerp1.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.490583f, res_nlerp1.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.848262f, res_nlerp1.mQ[3], 16));

        //test case for lerp(F32 t, const LLQuaternion &q) fn.
        // value1 = 100, quat2 = (4, 3, 6.5, 9.7):
        //   pre-normalize r = (400, 300, 650, 871)  (W = 1 + 100*(9.7 - 1) = 871)
        //   |r| = sqrt(1431141) ~= 1196.303
        //   r/|r| ~= (0.334363, 0.250773, 0.543341, 0.728076)
        res_lerp = lerp(value1, quat2);
        CHECK(is_approx_equal_fraction(0.334363f, res_lerp.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.250773f, res_lerp.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.543341f, res_lerp.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.728076f, res_lerp.mQ[3], 16));

    }

    TEST_CASE("LLQuaternion: operator* and operator+ and operator- on two quaternions", "[LLQuaternion]")
    {
        //test case for LLQuaternion operator*(const LLQuaternion &a, const LLQuaternion &b) fn
        LLQuaternion quat1(1.0f, 2.5f, 3.5f, 5.5f);
        LLQuaternion quat2(4.0f, 3.0f, 5.0f, 1.0f);
        LLQuaternion result = quat1 *  quat2;
        CHECK(result.mQ[0] == 21.0f);
        CHECK(result.mQ[1] == 10.0f);
        CHECK(result.mQ[2] == 38.0f);
        CHECK(result.mQ[3] == -23.5f);

        LLQuaternion quat3(2341.340f, 2352.345f, 233.25f, 7645.5f);
        LLQuaternion quat4(674.067f, 893.0897f, 578.0f, 231.0f);
        result = quat3 * quat4;
        // Products in the millions: the build contracts and reassociates
        // the arithmetic, so the last bit or two of each is the
        // machine's, not the one the constants were written on.
        CHECK(is_approx_equal_fraction(4543086.5f, result.mQ[0], 2));
        CHECK(is_approx_equal_fraction(8567578.0f, result.mQ[1], 2));
        CHECK(is_approx_equal_fraction(3967591.25f, result.mQ[2], 2));
        CHECK(is_approx_equal_fraction(-2047783.25f, result.mQ[3], 2));

        //inline LLQuaternion operator+(const LLQuaternion &a, const LLQuaternion &b)fn.
        result = quat1 + quat2;
        CHECK(result.mQ[0] == 5.0f);
        CHECK(result.mQ[1] == 5.5f);
        CHECK(result.mQ[2] == 8.5f);
        CHECK(result.mQ[3] == 6.5f);

        result = quat3 + quat4;
        CHECK(is_approx_equal(3015.407227f, result.mQ[0]));
        CHECK(is_approx_equal(3245.434570f, result.mQ[1]));
        CHECK(result.mQ[2] == 811.25f);
        CHECK(result.mQ[3] == 7876.5f);

        //inline LLQuaternion operator-(const LLQuaternion &a, const LLQuaternion &b) fn
        result = quat1 - quat2;
        CHECK(result.mQ[0] == -3.0f);
        CHECK(result.mQ[1] == -0.5f);
        CHECK(result.mQ[2] == -1.5f);
        CHECK(result.mQ[3] == 4.5f);

        result = quat3 - quat4;
        CHECK(is_approx_equal(1667.273071f, result.mQ[0]));
        CHECK(is_approx_equal(1459.255249f, result.mQ[1]));
        CHECK(result.mQ[2] == -344.75f);
        CHECK(result.mQ[3] == 7414.50f);
    }

    //test case for LLVector4 operator*(const LLVector4 &a, const LLQuaternion &rot) fn.
    TEST_CASE("LLQuaternion: operator* rotates an LLVector4 by a quaternion", "[LLQuaternion]")
    {
        LLVector4 vect(12.0f, 5.0f, 60.0f, 75.1f);
        LLQuaternion quat(2323.034f, 23.5f, 673.23f, 57667.5f);
        LLVector4 result = vect * quat;
        // Each the exact rotation rounded to a float, within the rounding of
        // the largest of the four terms it sums, which the order the compiler
        // sums them in moves: 3.98e10, 9.06e9 and 2.00e11.
        CHECK_THAT(result.mV[0], ll_test::WithinRounding(39928406016.0f, 3.98e10f));
        CHECK_THAT(result.mV[1], ll_test::WithinRounding(1457802240.0f, 9.06e9f));
        CHECK_THAT(result.mV[2], ll_test::WithinRounding(200580612096.0f, 2.00e11f));
        CHECK(result.mV[3] == 75.099998f);

        LLVector4 vect1(22.0f, 45.0f, 40.0f, 78.1f);
        LLQuaternion quat1(2.034f, 45.5f, 37.23f, 7.5f);
        result = vect1 * quat1;
        CHECK(is_approx_equal(-58153.5390f, result.mV[0]));
        CHECK(is_approx_equal(183787.8125f, result.mV[1]));
        CHECK(is_approx_equal(116864.164063f, result.mV[2]));
        CHECK(is_approx_equal(78.099998f, result.mV[3]));
    }

    //test case for LLVector3 operator*(const LLVector3 &a, const LLQuaternion &rot) fn.
    TEST_CASE("LLQuaternion: operator* rotates an LLVector3 by a quaternion", "[LLQuaternion]")
    {
        LLVector3 vect(12.0f, 5.0f, 60.0f);
        LLQuaternion quat(23.5f, 6.5f, 3.23f, 56.5f);
        LLVector3 result = vect * quat;
        CHECK(is_approx_equal(97182.953125f,result.mV[0]));
        CHECK(is_approx_equal(-135405.640625f, result.mV[1]));
        CHECK(is_approx_equal(162986.140f, result.mV[2]));

        LLVector3 vect1(5.0f, 40.0f, 78.1f);
        LLQuaternion quat1(2.034f, 45.5f, 37.23f, 7.5f);
        result = vect1 * quat1;
        CHECK(is_approx_equal(33217.703f, result.mV[0]));
        CHECK(is_approx_equal(295383.8125f, result.mV[1]));
        CHECK(is_approx_equal(84718.140f, result.mV[2]));
    }

    //test case for LLVector3d operator*(const LLVector3d &a, const LLQuaternion &rot) fn.
    TEST_CASE("LLQuaternion: operator* rotates an LLVector3d by a quaternion", "[LLQuaternion]")
    {
        LLVector3d vect(-2.0f, 5.0f, -6.0f);
        LLQuaternion quat(-3.5f, 4.5f, 3.5f, 6.5f);
        LLVector3d result = vect * quat;
        CHECK(result.mdV[0] == -633.0f);
        CHECK(result.mdV[1] == -300.0f);
        CHECK(result.mdV[2] == -36.0f);

        LLVector3d vect1(5.0f, -4.5f, 8.21f);
        LLQuaternion quat1(2.0f, 4.5f, -7.2f, 9.5f);
        result = vect1 * quat1;
        CHECK(is_approx_equal_fraction(-120.29f, (F32) result.mdV[0], 8));
        CHECK(is_approx_equal_fraction(-1683.958f, (F32) result.mdV[1], 8));
        CHECK(is_approx_equal_fraction(516.56f, (F32) result.mdV[2], 8));

        LLVector3d vect2(2.0f, 3.5f, 1.1f);
        LLQuaternion quat2(1.0f, 4.0f, 2.0f, 5.0f);
        result = vect2 * quat2;
        CHECK(is_approx_equal_fraction(18.400001f, (F32) result.mdV[0], 8));
        CHECK(is_approx_equal_fraction(188.6f, (F32) result.mdV[1], 8));
        CHECK(is_approx_equal_fraction(32.20f, (F32) result.mdV[2], 8));
    }

    //test case for inline LLQuaternion operator-(const LLQuaternion &a) fn.
    TEST_CASE("LLQuaternion: unary operator- negates every component", "[LLQuaternion]")
    {
        LLQuaternion quat(23.5f, 34.5f, 16723.4f, 324.7f);
        LLQuaternion result = -quat;
        CHECK(result.mQ[0] == -23.5f);
        CHECK(result.mQ[1] == -34.5f);
        CHECK(result.mQ[2] == -16723.4f);
        CHECK(result.mQ[3] == -324.7f);

        LLQuaternion quat1(-3.5f, -34.5f, -16.4f, -154.7f);
        result = -quat1;
        CHECK(result.mQ[0] == 3.5f);
        CHECK(result.mQ[1] == 34.5f);
        CHECK(result.mQ[2] == 16.4f);
        CHECK(result.mQ[3] == 154.7f);
    }

    //test case for inline LLQuaternion operator*(F32 a, const LLQuaternion &q) and
    //inline LLQuaternion operator*(F32 a, const LLQuaternion &q) fns.
    TEST_CASE("LLQuaternion: scalar operator* gives the same on either side", "[LLQuaternion]")
    {
        LLQuaternion quat_value(9.0f, 8.0f, 7.0f, 6.0f);
        F32 a =3.5f;
        LLQuaternion result = a * quat_value;
        LLQuaternion result1 = quat_value * a;

        CHECK(result.mQ[0] == result1.mQ[0]);
        CHECK(result.mQ[1] == result1.mQ[1]);
        CHECK(result.mQ[2] == result1.mQ[2]);
        CHECK(result.mQ[3] == result1.mQ[3]);


        LLQuaternion quat_val(9454.0f, 43568.3450f, 456343247.0343f, 2346.03434f);
        a =-3324.3445f;
        result = a * quat_val;
        result1 = quat_val * a;

        CHECK(result.mQ[0] == result1.mQ[0]);
        CHECK(result.mQ[1] == result1.mQ[1]);
        CHECK(result.mQ[2] == result1.mQ[2]);
        CHECK(result.mQ[3] == result1.mQ[3]);
    }

    TEST_CASE("LLQuaternion: operator~ conjugates and operator== matches equal quaternions", "[LLQuaternion]")
    {
        // test cases for inline LLQuaternion operator~(const LLQuaternion &a)
        LLQuaternion quat_val(2323.634f, -43535.4f, 3455.88f, -32232.45f);
        LLQuaternion result = ~quat_val;
        CHECK(result.mQ[0] == -2323.634f);
        CHECK(result.mQ[1] == 43535.4f);
        CHECK(result.mQ[2] == -3455.88f);
        CHECK(result.mQ[3] == -32232.45f);

        //test case for inline bool LLQuaternion::operator==(const LLQuaternion &b) const
        LLQuaternion quat_val1(2323.634f, -43535.4f, 3455.88f, -32232.45f);
        LLQuaternion quat_val2(2323.634f, -43535.4f, 3455.88f, -32232.45f);
        CHECK(quat_val1 == quat_val2);
    }

    TEST_CASE("LLQuaternion: operator!= tells apart quaternions differing in one component", "[LLQuaternion]")
    {
        //test case for inline bool LLQuaternion::operator!=(const LLQuaternion &b) const
        LLQuaternion quat_val1(2323.634f, -43535.4f, 3455.88f, -32232.45f);
        LLQuaternion quat_val2(0, -43535.4f, 3455.88f, -32232.45f);
        CHECK(quat_val1 != quat_val2);
    }

    TEST_CASE("LLQuaternion: mayaQ builds the rotation for each axis order", "[LLQuaternion]")
    {
        //test case for LLQuaternion mayaQ(F32 xRot, F32 yRot, F32 zRot, LLQuaternion::Order order)
        F32 x = 2.0f;
        F32 y = 1.0f;
        F32 z = 3.0f;

        LLQuaternion result = mayaQ(x, y, z, LLQuaternion::XYZ);
        CHECK(is_approx_equal_fraction(0.0172174f, result.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.009179f, result.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026020f, result.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999471f, result.mQ[3], 16));

        LLQuaternion result1 = mayaQ(x, y, z, LLQuaternion::YZX);
        CHECK(is_approx_equal_fraction(0.017217f, result1.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.008265f, result1.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026324f, result1.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999471f, result1.mQ[3], 16));

        LLQuaternion result2 = mayaQ(x, y, z, LLQuaternion::ZXY);
        CHECK(is_approx_equal_fraction(0.017674f, result2.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.008265f, result2.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026020f, result2.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999471f, result2.mQ[3], 16));

        LLQuaternion result3 = mayaQ(x, y, z, LLQuaternion::XZY);
        CHECK(is_approx_equal_fraction(0.017674f, result3.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.009179f, result3.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026020f, result3.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999463f, result3.mQ[3], 16));

        LLQuaternion result4 = mayaQ(x, y, z, LLQuaternion::YXZ);
        CHECK(is_approx_equal_fraction(0.017217f, result4.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.009179f, result4.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026324f, result4.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999463f, result4.mQ[3], 16));

        LLQuaternion result5 = mayaQ(x, y, z, LLQuaternion::ZYX);
        CHECK(is_approx_equal_fraction(0.017674f, result5.mQ[0], 16));
        CHECK(is_approx_equal_fraction(0.008265f, result5.mQ[1], 16));
        CHECK(is_approx_equal_fraction(0.026324f, result5.mQ[2], 16));
        CHECK(is_approx_equal_fraction(0.999463f, result5.mQ[3], 16));
    }

    TEST_CASE("LLQuaternion: operator<< prints the components in braces", "[LLQuaternion]")
    {
        // test case for friend std::ostream& operator<<(std::ostream &s, const LLQuaternion &a) fn
        LLQuaternion a(1.0f, 1.0f, 1.0f, 1.0f);
        std::ostringstream result_value;
        result_value << a;
        CHECK(result_value.str() == "{ 1, 1, 1, 1 }");

        LLQuaternion b(-31.034f, 231.2340f, 3451.344320f, -341.0f);
        std::ostringstream result_value1;
        result_value1 << b;
        CHECK(result_value1.str() == "{ -31.034, 231.234, 3451.34, -341 }");

        LLQuaternion c(1.0f, 2.2f, 3.3f, 4.4f);
        result_value << c;
        CHECK(result_value.str() == "{ 1, 1, 1, 1 }{ 1, 2.2, 3.3, 4.4 }");

    }

    TEST_CASE("LLQuaternion: OrderToString names each axis order", "[LLQuaternion]")
    {
        //test case for const char *OrderToString( const LLQuaternion::Order order ) fn
        const char* result = OrderToString(LLQuaternion::XYZ);
        CHECK(std::string_view(result) == "XYZ");

        result = OrderToString(LLQuaternion::YZX);
        CHECK(std::string_view(result) == "YZX");

        result = OrderToString(LLQuaternion::ZXY);
        CHECK(std::string_view(result) == "ZXY");
        CHECK(std::string_view(result) != "XYZ");
        CHECK(std::string_view(result) != "YZX");
        CHECK(std::string_view(result) != "XZY");
        CHECK(std::string_view(result) != "YXZ");
        CHECK(std::string_view(result) != "ZYX");

        result = OrderToString(LLQuaternion::XZY);
        CHECK(std::string_view(result) == "XZY");

        result = OrderToString(LLQuaternion::ZYX);
        CHECK(std::string_view(result) == "ZYX");

        result = OrderToString(LLQuaternion::YXZ);
        CHECK(std::string_view(result) == "YXZ");
    }

    TEST_CASE("LLQuaternion: StringToOrder maps each name to its axis order", "[LLQuaternion]")
    {
        //test case for LLQuaternion::Order StringToOrder( const char *str ) fn
        int result = StringToOrder("XYZ");
        CHECK(result == 0);

        result = StringToOrder("YZX");
        CHECK(result == 1);

        result = StringToOrder("ZXY");
        CHECK(result == 2);

        result = StringToOrder("XZY");
        CHECK(result == 3);

        result = StringToOrder("YXZ");
        CHECK(result == 4);

        result = StringToOrder("ZYX");
        CHECK(result == 5);

    }

    TEST_CASE("LLQuaternion: getAngleAxis recovers the angle and the normalised axis", "[LLQuaternion]")
    {
        //void LLQuaternion::getAngleAxis(F32* angle, LLVector3 &vec) const fn
        F32 angle_value = 90.0f;
        LLVector3 vect(12.0f, 4.0f, 1.0f);
        LLQuaternion llquat(angle_value, vect);
        llquat.getAngleAxis(&angle_value, vect);
        CHECK(is_approx_equal_fraction(2.035406f, angle_value, 16));
        CHECK(is_approx_equal_fraction(0.315244f, vect.mV[1], 16));
        CHECK(is_approx_equal_fraction(0.078811f, vect.mV[2], 16));
        CHECK(is_approx_equal_fraction(0.945733f, vect.mV[0], 16));
    }

    TEST_CASE("LLQuaternion: getEulerAngles gives back the angles setEulerAngles took", "[LLQuaternion]")
    {
        //test case for void LLQuaternion::getEulerAngles(F32 *roll, F32 *pitch, F32 *yaw) const fn
        using Catch::Matchers::WithinAbs;

        // The identity has no angles, whatever the outputs held before.
        F32 roll = -12.0f;
        F32 pitch = -22.43f;
        F32 yaw = 11.0f;
        LLQuaternion llquat;
        llquat.getEulerAngles(&roll, &pitch, &yaw);
        CHECK_THAT(roll, WithinAbs(0.f, 1e-6));
        CHECK_THAT(pitch, WithinAbs(0.f, 1e-6));
        CHECK_THAT(yaw, WithinAbs(0.f, 1e-6));

        // Angles within range, pitch short of a quarter turn, round trip.
        const F32 angles[][3] = { { 0.3f, -0.2f, 1.1f }, { -2.5f, 1.2f, -0.4f }, { 1.f, 0.f, 0.f } };
        for (const auto& set : angles)
        {
            CAPTURE(set[0], set[1], set[2]);
            LLQuaternion turned;
            turned.setEulerAngles(set[0], set[1], set[2]);
            turned.getEulerAngles(&roll, &pitch, &yaw);
            CHECK_THAT(roll, WithinAbs(set[0], 1e-5));
            CHECK_THAT(pitch, WithinAbs(set[1], 1e-5));
            CHECK_THAT(yaw, WithinAbs(set[2], 1e-5));
        }
    }

    TEST_CASE("LLQuaternion: product and dot evaluate at compile time", "[LLQuaternion]")
    {
        constexpr LLQuaternion half_turn(0.f, 0.f, 1.f, 0.f);
        constexpr LLQuaternion full_turn = half_turn * half_turn;
        static_assert(full_turn == LLQuaternion(0.f, 0.f, 0.f, -1.f));
        static_assert(dot(half_turn, half_turn) == 1.f);
        static_assert(dot(half_turn, full_turn) == 0.f);
        UNSCOPED_INFO("compile-time quaternion agrees at run time");
        CHECK(full_turn.mQ[VW] == -1.f);
    }

}
