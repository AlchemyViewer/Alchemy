/**
 * @file v3dmath_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief v3dmath test cases.
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
#include "llsd.h"
#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include "../m3math.h"
#include "../v4math.h"
#include "../v3dmath.h"
#include "../v3dmath.h"
#include "../llquaternion.h"

namespace
{
    TEST_CASE("v3dmath_h: constructors set every component", "[v3dmath_h]")
    {
        LLVector3d vec3D;
        CHECK(vec3D.mdV[VX] == 0);
        CHECK(vec3D.mdV[VY] == 0);
        CHECK(vec3D.mdV[VZ] == 0);

        F64 x = 2.32, y = 1.212, z = -.12;
        LLVector3d vec3Da(x,y,z);
        CHECK(vec3Da.mdV[VX] == 2.32);
        CHECK(vec3Da.mdV[VY] == 1.212);
        CHECK(vec3Da.mdV[VZ] == -.12);

        const F64 vec[3] = {1.2 ,3.2, -4.2};
        LLVector3d vec3Db(vec);
        CHECK(vec3Db.mdV[VX] == 1.2);
        CHECK(vec3Db.mdV[VY] == 3.2);
        CHECK(vec3Db.mdV[VZ] == -4.2);

        // From an LLVector3, each float widened exactly.
        LLVector3 vec3(2.32f, 1.212f, -.12f);
        LLVector3d vec3Dc(vec3);
        CHECK(vec3Dc.mdV[VX] == F64(2.32f));
        CHECK(vec3Dc.mdV[VY] == F64(1.212f));
        CHECK(vec3Dc.mdV[VZ] == F64(-.12f));
    }

    TEST_CASE("v3dmath_h: LLSD constructor and getValue round trip", "[v3dmath_h]")
    {
        // An array of three: a vector reads sd[0], sd[1] and sd[2], so a
        // scalar LLSD would make every component zero.
        LLSD llsd;
        llsd[0] = -235.0;
        llsd[1] = 232345521.411132;
        llsd[2] = -.12;
        LLVector3d vec3d(llsd);
        CHECK(vec3d.mdV[VX] == -235.0);
        CHECK(vec3d.mdV[VY] == 232345521.411132);
        CHECK(vec3d.mdV[VZ] == -.12);
        LLSD sd = vec3d.getValue();
        LLVector3d vec3da(sd);
        CHECK(vec3d == vec3da);
    }

    TEST_CASE("v3dmath_h: setValue and getValue round trip", "[v3dmath_h]")
    {
        LLSD llsd;
        llsd[0] = 232345521.411132;
        llsd[1] = -0.000001;
        llsd[2] = 12345.2343;
        LLVector3d vec3d;
        vec3d.setValue(llsd);
        CHECK(vec3d.mdV[VX] == 232345521.411132);
        CHECK(vec3d.mdV[VY] == -0.000001);
        CHECK(vec3d.mdV[VZ] == 12345.2343);
        LLSD sd = vec3d.getValue();
        LLVector3d vec3da;
        vec3da.setValue(sd);
        CHECK(vec3d == vec3da);
    }

    TEST_CASE("v3dmath_h: cast from an LLSD array copies each element", "[v3dmath_h]")
    {
        F64 a[3] = {222231.43222, 12345.2343, -434343.33222};
        LLSD llsd;
        llsd[0] = a[0];
        llsd[1] = a[1];
        llsd[2] = a[2];
        LLVector3d vec3D;
        vec3D = (LLVector3d)llsd;
        CHECK(llsd[0].asReal()== vec3D.mdV[VX]);
        CHECK(llsd[1].asReal() == vec3D.mdV[VY]);
        CHECK(llsd[2].asReal() == vec3D.mdV[VZ]);
    }

    TEST_CASE("v3dmath_h: clearVec/zeroVec and the setVec overloads", "[v3dmath_h]")
    {
        // Values a float holds, so the trip through an LLVector3 below is exact.
        F64 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3d vec3D(x,y,z);
        vec3D.clearVec();
        CHECK(vec3D.mdV[VX] == 0);
        CHECK(vec3D.mdV[VY] == 0);
        CHECK(vec3D.mdV[VZ] == 0);

        vec3D.setVec(x,y,z);
        CHECK(x == vec3D.mdV[VX]);
        CHECK(y == vec3D.mdV[VY]);
        CHECK(z == vec3D.mdV[VZ]);

        vec3D.zeroVec();
        CHECK(vec3D.mdV[VX] == 0);
        CHECK(vec3D.mdV[VY] == 0);
        CHECK(vec3D.mdV[VZ] == 0);

        vec3D.clearVec();
        LLVector3 vec3((F32)x,(F32)y,(F32)z);
        vec3D.setVec(vec3);
        CHECK(x == vec3D.mdV[VX]);
        CHECK(y == vec3D.mdV[VY]);
        CHECK(z == vec3D.mdV[VZ]);

        vec3D.clearVec();
        const F64 vec[3] = {x,y,z};
        vec3D.setVec(vec);
        CHECK(x == vec3D.mdV[VX]);
        CHECK(y == vec3D.mdV[VY]);
        CHECK(z == vec3D.mdV[VZ]);

        LLVector3d vec3Da;
        vec3Da.setVec(vec3D);
        CHECK(vec3D == vec3Da);
    }

    TEST_CASE("v3dmath_h: abs then isNull and isExactlyZero", "[v3dmath_h]")
    {
        F64 x = -2.32, y = 1.212, z = -.12;
        LLVector3d vec3D(x,y,z);
        vec3D.abs();
        CHECK(-x == vec3D.mdV[VX]);
        CHECK(y == vec3D.mdV[VY]);
        CHECK(-z == vec3D.mdV[VZ]);

        CHECK_FALSE(vec3D.isNull());
        vec3D.clearVec();
        x =.00000001, y = .000001001, z = .000001001;
        vec3D.setVec(x,y,z);
        CHECK(vec3D.isNull());
        CHECK_FALSE(vec3D.isExactlyZero());
        x =.0000000, y = .00000000, z = .00000000;
        vec3D.setVec(x,y,z);
        CHECK(vec3D.isExactlyZero());
    }

    TEST_CASE("v3dmath_h: subscript operator reads components and returns references", "[v3dmath_h]")
    {
        F64 x = -2.32, y = 1.212, z = -.12;
        LLVector3d vec3D(x,y,z);

        CHECK(x ==  vec3D[0]);
        CHECK(y ==  vec3D[1]);
        CHECK(z ==  vec3D[2]);
        // The non-const subscript is a reference to the component itself.
        F64 &ref1 = vec3D[0];
        CHECK(&ref1 == &vec3D.mdV[VX]);
        ref1 = 23.23;
        vec3D[1] = -.2361;
        vec3D[2] = 3.25;
        CHECK(vec3D.mdV[VX] == 23.23);
        CHECK(vec3D.mdV[VY] == -.2361);
        CHECK(vec3D.mdV[VZ] == 3.25);
    }

    TEST_CASE("v3dmath_h: assignment from LLVector4", "[v3dmath_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.f;
        LLVector4 vec4(x,y,z);
        LLVector3d vec3D;
        vec3D = vec4;
        CHECK(vec4.mV[VX] == vec3D.mdV[VX]);
        CHECK(vec4.mV[VY] == vec3D.mdV[VY]);
        CHECK(vec4.mV[VZ] == vec3D.mdV[VZ]);
    }

    TEST_CASE("v3dmath_h: operator+ and operator+=", "[v3dmath_h]")
    {
        F64 x1 = 1.78787878, y1 = 232322.2121, z1 = -12121.121212;
        F64 x2 = 1.2, y2 = 2.5, z2 = 1.;
        LLVector3d vec3D(x1,y1,z1),vec3Da(x2,y2,z2),vec3Db;
        vec3Db = vec3Da+ vec3D;
        CHECK(x1+x2 == vec3Db.mdV[VX]);
        CHECK(y1+y2 == vec3Db.mdV[VY]);
        CHECK(z1+z2 == vec3Db.mdV[VZ]);

        x1 = -2.45, y1 = 2.1, z1 = 3.0;
        vec3D.clearVec();
        vec3Da.clearVec();
        vec3D.setVec(x1,y1,z1);
        vec3Da += vec3D;
        CHECK(vec3Da == vec3D);
        vec3Da += vec3D;
        CHECK(2*x1 == vec3Da.mdV[VX]);
        CHECK(2*y1 == vec3Da.mdV[VY]);
        CHECK(2*z1 == vec3Da.mdV[VZ]);
    }

    TEST_CASE("v3dmath_h: operator- and operator-=", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        F64 x2 = 1.2, y2 = 2.5, z2 = 1.;
        LLVector3d vec3D(x1,y1,z1),vec3Da(x2,y2,z2),vec3Db;
        vec3Db = vec3Da - vec3D;
        CHECK(x2-x1 == vec3Db.mdV[VX]);
        CHECK(y2-y1 == vec3Db.mdV[VY]);
        CHECK(z2-z1 == vec3Db.mdV[VZ]);

        x1 = -2.45, y1 = 2.1, z1 = 3.0;
        vec3D.clearVec();
        vec3Da.clearVec();
        vec3D.setVec(x1,y1,z1);
        vec3Da -=vec3D;
        CHECK(vec3Da.mdV[VX] == 2.45);
        CHECK(vec3Da.mdV[VY] == -2.1);
        CHECK(vec3Da.mdV[VZ] == -3.0);

        vec3Da -= vec3D;
        CHECK(-2*x1 == vec3Da.mdV[VX]);
        CHECK(-2*y1 == vec3Da.mdV[VY]);
        CHECK(-2*z1 == vec3Da.mdV[VZ]);
    }

    TEST_CASE("v3dmath_h: operator* dot and scalar products and operator*=", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        F64 x2 = 1.2, y2 = 2.5, z2 = 1.;
        LLVector3d vec3D(x1,y1,z1),vec3Da(x2,y2,z2);
        F64 res = vec3D * vec3Da;
        CHECK_THAT(res, ll_test::WithinBits(x1*x2 + y1*y2 + z1*z2, 8));
        vec3Da.clearVec();
        F64 mulVal = 4.2;
        vec3Da = vec3D * mulVal;
        CHECK_THAT(vec3Da.mdV[VX], ll_test::WithinBits(x1*mulVal, 8));
        CHECK_THAT(vec3Da.mdV[VY], ll_test::WithinBits(y1*mulVal, 8));
        CHECK_THAT(vec3Da.mdV[VZ], ll_test::WithinBits(z1*mulVal, 8));
        vec3Da.clearVec();
        vec3Da = mulVal * vec3D;
        CHECK_THAT(vec3Da.mdV[VX], ll_test::WithinBits(x1*mulVal, 8));
        CHECK_THAT(vec3Da.mdV[VY], ll_test::WithinBits(y1*mulVal, 8));
        CHECK_THAT(vec3Da.mdV[VZ], ll_test::WithinBits(z1*mulVal, 8));
        vec3D *= mulVal;
        CHECK_THAT(vec3D.mdV[VX], ll_test::WithinBits(x1*mulVal, 8));
        CHECK_THAT(vec3D.mdV[VY], ll_test::WithinBits(y1*mulVal, 8));
        CHECK_THAT(vec3D.mdV[VZ], ll_test::WithinBits(z1*mulVal, 8));
    }

    TEST_CASE("v3dmath_h: operator% and operator%= cross products", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        F64 x2 = 1.2, y2 = 2.5, z2 = 1.;
        F64 val1, val2, val3;
        LLVector3d vec3D(x1,y1,z1),vec3Da(x2,y2,z2), vec3Db;
        vec3Db = vec3D % vec3Da;
        val1 = y1*z2 - y2*z1;
        val2 = z1*x2 -z2*x1;
        val3 = x1*y2-x2*y1;
        CHECK(val1 == vec3Db.mdV[VX]);
        CHECK(val2 == vec3Db.mdV[VY]);
        CHECK(val3 == vec3Db.mdV[VZ]);

        vec3D %= vec3Da;
        CHECK(is_approx_equal(vec3D.mdV[VX],vec3Db.mdV[VX]));
        CHECK(is_approx_equal(vec3D.mdV[VY],vec3Db.mdV[VY]));
        CHECK(is_approx_equal(vec3D.mdV[VZ],vec3Db.mdV[VZ]));
    }

    TEST_CASE("v3dmath_h: operator/ and operator/= divide by a scalar", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1,div = 4.2;
        F64 t = 1.f / div;
        LLVector3d vec3D(x1,y1,z1), vec3Da;
        vec3Da = vec3D/div;
        CHECK_THAT(vec3Da.mdV[VX], ll_test::WithinBits(x1*t, 8));
        CHECK_THAT(vec3Da.mdV[VY], ll_test::WithinBits(y1*t, 8));
        CHECK_THAT(vec3Da.mdV[VZ], ll_test::WithinBits(z1*t, 8));
        x1 = 1.23, y1 = 4., z1 = -2.32;
        vec3D.clearVec();
        vec3Da.clearVec();
        vec3D.setVec(x1,y1,z1);
        vec3Da = vec3D/div;
        CHECK_THAT(vec3Da.mdV[VX], ll_test::WithinBits(x1*t, 8));
        CHECK_THAT(vec3Da.mdV[VY], ll_test::WithinBits(y1*t, 8));
        CHECK_THAT(vec3Da.mdV[VZ], ll_test::WithinBits(z1*t, 8));
        vec3D /= div;
        CHECK_THAT(vec3D.mdV[VX], ll_test::WithinBits(x1*t, 8));
        CHECK_THAT(vec3D.mdV[VY], ll_test::WithinBits(y1*t, 8));
        CHECK_THAT(vec3D.mdV[VZ], ll_test::WithinBits(z1*t, 8));
    }

    TEST_CASE("v3dmath_h: operator== and operator!=", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        LLVector3d vec3D(x1,y1,z1), vec3Da;
        CHECK(vec3D !=vec3Da);
        vec3Da = vec3D;
        CHECK(vec3D ==vec3Da);
        vec3D.clearVec();
        vec3Da.clearVec();
        x1 = .211, y1 = 21.111, z1 = 23.22;
        vec3D.setVec(x1,y1,z1);
        vec3Da.setVec(x1,y1,z1);
        CHECK(vec3D ==vec3Da);
        CHECK_FALSE(vec3D !=vec3Da);
    }

    TEST_CASE("v3dmath_h: operator<< prints equal vectors alike", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        LLVector3d vec3D(x1,y1,z1), vec3Da;
        std::ostringstream stream1, stream2;
        stream1 << vec3D;
        vec3Da.setVec(x1,y1,z1);
        stream2 << vec3Da;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v3dmath_h: parseVector3d reads three numbers", "[v3dmath_h]")
    {
        F64 x1 = 1.23, y1 = 2.0, z1 = 4.;
        std::string buf("1.23 2. 4");
        LLVector3d vec3D, vec3Da(x1,y1,z1);
        LLVector3d::parseVector3d(buf, &vec3D);
        CHECK(vec3D == vec3Da);
    }

    TEST_CASE("v3dmath_h: unary operator- negates", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        LLVector3d vec3D(x1,y1,z1), vec3Da;
        vec3Da = -vec3D;
        CHECK(vec3D == - vec3Da);
    }

    TEST_CASE("v3dmath_h: magVecSquared and magVec", "[v3dmath_h]")
    {
        F64 x = 1., y = 2., z = -1.1;
        LLVector3d vec3D(x,y,z);
        F64 res = (x*x + y*y + z*z) - vec3D.magVecSquared();
        CHECK(-F_APPROXIMATELY_ZERO <= res);
        CHECK(res <=F_APPROXIMATELY_ZERO);

        res = (F32) sqrt(x*x + y*y + z*z) - vec3D.magVec();
        CHECK(-F_APPROXIMATELY_ZERO <= res);
        CHECK(res <=F_APPROXIMATELY_ZERO);
    }

    TEST_CASE("v3dmath_h: normVec normalises long and tiny vectors", "[v3dmath_h]")
    {
        F64 x = 1., y = 2., z = -1.1;
        LLVector3d vec3D(x,y,z);
        F64 mag = vec3D.normVec();
        mag = 1.f/ mag;
        CHECK_THAT(vec3D.mdV[VX], ll_test::WithinBits(x * mag, 8));
        CHECK_THAT(vec3D.mdV[VY], ll_test::WithinBits(y * mag, 8));
        CHECK_THAT(vec3D.mdV[VZ], ll_test::WithinBits(z * mag, 8));
        x = 0.000000001, y = 0.000000001, z = 0.000000001;
        vec3D.clearVec();
        vec3D.setVec(x,y,z);
        mag = vec3D.normVec();
        CHECK_THAT(vec3D.mdV[VX], ll_test::WithinBits(x * mag, 8));
        CHECK_THAT(vec3D.mdV[VY], ll_test::WithinBits(y * mag, 8));
        CHECK_THAT(vec3D.mdV[VZ], ll_test::WithinBits(z * mag, 8));
    }

    TEST_CASE("v3dmath_h: parseVector3d reads large values exactly", "[v3dmath_h]")
    {
        F64 x1 = 1111.232222;
        F64 y1 = 2222222222.22;
        F64 z1 = 422222222222.0;
        std::string buf("1111.232222 2222222222.22 422222222222");
        LLVector3d vec3Da, vec3Db(x1,y1,z1);
        LLVector3d::parseVector3d(buf, &vec3Da);
        CHECK(vec3Da == vec3Db);
    }

    TEST_CASE("v3dmath_h: lerp between two vectors", "[v3dmath_h]")
    {
        F64 x1 = 1., y1 = 2., z1 = -1.1;
        F64 x2 = 1.2, y2 = 2.5, z2 = 1.;
        F64 val = 2.3f,val1,val2,val3;
        val1 = x1 + (x2 - x1)* val;
        val2 = y1 + (y2 - y1)* val;
        val3 = z1 + (z2 - z1)* val;
        LLVector3d vec3Da(x1,y1,z1),vec3Db(x2,y2,z2);
        LLVector3d vec3d = lerp(vec3Da,vec3Db,val);
        CHECK(val1 ==vec3d.mdV[VX]);
        CHECK(val2 ==vec3d.mdV[VY]);
        CHECK(val3 ==vec3d.mdV[VZ]);
    }

    TEST_CASE("v3dmath_h: clamp reports a component out of range", "[v3dmath_h]")
    {
        F64 x = 2.32, y = 1.212, z = -.12;
        F64 min = 0.0001, max = 3.0;
        LLVector3d vec3d(x,y,z);
        CHECK(vec3d.clamp(min, max));
        x = 0.000001f, z = 5.3f;
        vec3d.setVec(x,y,z);
        CHECK(vec3d.clamp(min, max));
    }

    TEST_CASE("v3dmath_h: are_parallel for equal and non-parallel vectors", "[v3dmath_h]")
    {
        F64 x = 10., y = 20., z = -15.;
        F64 epsilon = .23425;
        LLVector3d vec3Da(x,y,z), vec3Db(x,y,z);
        CHECK(are_parallel(vec3Da,vec3Db,epsilon));
        F64 x1 = -12., y1 = -20., z1 = -100.;
        vec3Db.clearVec();
        vec3Db.setVec(x1,y1,z1);
        CHECK_FALSE(are_parallel(vec3Da,vec3Db,epsilon));
    }

    TEST_CASE("v3dmath_h: angle_between is zero for equal vectors and matches acos", "[v3dmath_h]")
    {
        F64 x = 10., y = 20., z = -15.;
        F64 angle1, angle2;
        LLVector3d vec3Da(x,y,z), vec3Db(x,y,z);
        angle1 = angle_between(vec3Da, vec3Db);
        CHECK(angle1 == 0);
        F64 x1 = -1., y1 = -20., z1 = -1.;
        vec3Da.clearVec();
        vec3Da.setVec(x1,y1,z1);
        angle2 = angle_between(vec3Da, vec3Db);
        vec3Db.normVec();
        vec3Da.normVec();
        F64 angle = vec3Db*vec3Da;
        angle = acos(angle);
        CHECK(angle == angle2);
    }
}
