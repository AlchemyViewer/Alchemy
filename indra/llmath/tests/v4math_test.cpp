/**
 * @file v4math_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief v4math test cases.
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
#include "altestmatchers.h"
#include "llsd.h"

#include "../m4math.h"
#include "../v4math.h"
#include "../llquaternion.h"

namespace
{
    TEST_CASE("v4math_h: constructors set every component and default w to 1", "[v4math_h]")
    {
        LLVector4 vec4;
        CHECK(vec4.mV[VX] == 0);
        CHECK(vec4.mV[VY] == 0);
        CHECK(vec4.mV[VZ] == 0);
        CHECK(vec4.mV[VW] == 1.0f);
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4a(x,y,z);
        CHECK(x == vec4a.mV[VX]);
        CHECK(y == vec4a.mV[VY]);
        CHECK(z == vec4a.mV[VZ]);
        CHECK(vec4a.mV[VW] == 1.0f);
        LLVector4 vec4b(x,y,z,w);
        CHECK(x == vec4b.mV[VX]);
        CHECK(y == vec4b.mV[VY]);
        CHECK(z == vec4b.mV[VZ]);
        CHECK(w == vec4b.mV[VW]);
        const F32 vec[4] = {.112f ,23.2f, -4.2f, -.0001f};
        LLVector4 vec4c(vec);
        CHECK(vec[0] == vec4c.mV[VX]);
        CHECK(vec[1] == vec4c.mV[VY]);
        CHECK(vec[2] == vec4c.mV[VZ]);
        CHECK(vec[3] == vec4c.mV[VW]);
        LLVector3 vec3(-2.23f,1.01f,42.3f);
        LLVector4 vec4d(vec3);
        CHECK(vec3.mV[VX] == vec4d.mV[VX]);
        CHECK(vec3.mV[VY] == vec4d.mV[VY]);
        CHECK(vec3.mV[VZ] == vec4d.mV[VZ]);
        CHECK(vec4d.mV[VW] == 1.f);
        F32 w1 = -.234f;
        LLVector4 vec4e(vec3,w1);
        CHECK(vec3.mV[VX] == vec4e.mV[VX]);
        CHECK(vec3.mV[VY] == vec4e.mV[VY]);
        CHECK(vec3.mV[VZ] == vec4e.mV[VZ]);
        CHECK(w1 == vec4e.mV[VW]);
    }

    TEST_CASE("v4math_h: setVec sets components and clearVec/zeroVec reset them", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4;
        vec4.setVec(x,y,z);
        CHECK(x == vec4.mV[VX]);
        CHECK(y == vec4.mV[VY]);
        CHECK(z == vec4.mV[VZ]);
        CHECK(vec4.mV[VW] == 1.0f);
        vec4.clearVec();
        CHECK(vec4.mV[VX] == 0);
        CHECK(vec4.mV[VY] == 0);
        CHECK(vec4.mV[VZ] == 0);
        CHECK(vec4.mV[VW] == 1.0f);
        vec4.setVec(x,y,z,w);
        CHECK(x == vec4.mV[VX]);
        CHECK(y == vec4.mV[VY]);
        CHECK(z == vec4.mV[VZ]);
        CHECK(w == vec4.mV[VW]);
        vec4.zeroVec();
        CHECK(vec4.mV[VX] == 0);
        CHECK(vec4.mV[VY] == 0);
        CHECK(vec4.mV[VZ] == 0);
        CHECK(vec4.mV[VW] == 0);
        LLVector3 vec3(-2.23f,1.01f,42.3f);
        vec4.clearVec();
        vec4.setVec(vec3);
        CHECK(vec3.mV[VX] == vec4.mV[VX]);
        CHECK(vec3.mV[VY] == vec4.mV[VY]);
        CHECK(vec3.mV[VZ] == vec4.mV[VZ]);
        CHECK(vec4.mV[VW] == 1.f);
        F32 w1 = -.234f;
        vec4.zeroVec();
        vec4.setVec(vec3,w1);
        CHECK(vec3.mV[VX] == vec4.mV[VX]);
        CHECK(vec3.mV[VY] == vec4.mV[VY]);
        CHECK(vec3.mV[VZ] == vec4.mV[VZ]);
        CHECK(w1 == vec4.mV[VW]);
        const F32 vec[4] = {.112f ,23.2f, -4.2f, -.0001f};
        LLVector4 vec4a;
        vec4a.setVec(vec);
        CHECK(vec[0] == vec4a.mV[VX]);
        CHECK(vec[1] == vec4a.mV[VY]);
        CHECK(vec[2] == vec4a.mV[VZ]);
        CHECK(vec[3] == vec4a.mV[VW]);
    }

    TEST_CASE("v4math_h: magVec and magVecSquared", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f;
        LLVector4 vec4(x,y,z);
        CHECK(is_approx_equal(vec4.magVec(), (F32) sqrt(x*x + y*y + z*z)));
        CHECK(is_approx_equal(vec4.magVecSquared(), (x*x + y*y + z*z)));
    }

    TEST_CASE("v4math_h: normVec normalizes and zeroes a near-zero vector", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f;
        LLVector4 vec4(x,y,z);
        F32 mag = vec4.normVec();
        mag = 1.f/ mag;
        CHECK(is_approx_equal(mag*x,vec4.mV[VX]));
        CHECK(is_approx_equal(mag*y, vec4.mV[VY]));
        CHECK(is_approx_equal(mag*z, vec4.mV[VZ]));
        x = 0.000000001f, y = 0.000000001f, z = 0.000000001f;
        vec4.clearVec();
        vec4.setVec(x,y,z);
        mag = vec4.normVec();
        CHECK(is_approx_equal(mag*x,vec4.mV[VX]));
        CHECK(is_approx_equal(mag*y, vec4.mV[VY]));
        CHECK(is_approx_equal(mag*z, vec4.mV[VZ]));
    }

    TEST_CASE("v4math_h: abs drops signs; isExactlyClear and isExactlyZero", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4(x,y,z,w);
        vec4.abs();
        CHECK(x == vec4.mV[VX]);
        CHECK(-y == vec4.mV[VY]);
        CHECK(-z == vec4.mV[VZ]);
        CHECK(-w == vec4.mV[VW]);
        vec4.clearVec();
        CHECK(vec4.isExactlyClear());
        vec4.zeroVec();
        CHECK(vec4.isExactlyZero());
    }

    TEST_CASE("v4math_h: scaleVec multiplies component-wise", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4(x,y,z,w),vec4a;
        vec4a = vec4.scaleVec(vec4);
        CHECK(is_approx_equal(x*x, vec4a.mV[VX]));
        CHECK(is_approx_equal(y*y, vec4a.mV[VY]));
        CHECK(is_approx_equal(z*z, vec4a.mV[VZ]));
        CHECK(is_approx_equal(w*w, vec4a.mV[VW]));
    }

    TEST_CASE("v4math_h: subscript operator reads components and gives references", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4(x,y,z,w);
        CHECK(x ==  vec4[0]);
        CHECK(y ==  vec4[1]);
        CHECK(z ==  vec4[2]);
        CHECK(w ==  vec4[3]);
        // The non-const subscript is a reference to the component itself.
        F32 &ref1 = vec4[0];
        CHECK(&ref1 == &vec4.mV[VX]);
        ref1 = 23.f;
        vec4[1] = -.2361f;
        vec4[2] = 3.25f;
        vec4[3] = 7.5f;
        CHECK(vec4.mV[VX] == 23.f);
        CHECK(vec4.mV[VY] == -.2361f);
        CHECK(vec4.mV[VZ] == 3.25f);
        CHECK(vec4.mV[VW] == 7.5f);
    }

    TEST_CASE("v4math_h: rotVec by a matrix and by a quaternion", "[v4math_h]")
    {
        using Catch::Matchers::WithinAbs;

        // By a matrix: the row vector times the matrix, all four components.
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        const  F32 val[16] = {
            1.f,  2.f,   3.f,    0.f,
            .34f, .1f,   -.5f,   0.f,
            2.f,  1.23f, 1.234f, 0.f,
            .89f, 0.f,   0.f,    0.f
        };
        LLMatrix4 mat(val);
        LLVector4 vec4(x,y,z,w);
        vec4.rotVec(mat);
        const F64 v[4] = { x, y, z, w };
        for (int col = 0; col < 4; ++col)
        {
            F64 expected = 0.0;
            for (int row = 0; row < 4; ++row)
            {
                expected += v[row] * val[row * 4 + col];
            }
            CAPTURE(col);
            CHECK_THAT(vec4.mV[col], WithinAbs(expected, 1e-4));
        }

        // By a quaternion: x, y and z turn as an LLVector3 does, and w stays.
        LLVector4 quarter(1.f, 0.f, 0.f, 3.f);
        quarter.rotVec(LLQuaternion(F_PI_BY_TWO, LLVector3(0.f, 0.f, 1.f)));
        CHECK_THAT(quarter.mV[VX], WithinAbs(0.f, 1e-6));
        CHECK_THAT(quarter.mV[VY], WithinAbs(1.f, 1e-6));
        CHECK_THAT(quarter.mV[VZ], WithinAbs(0.f, 1e-6));
        CHECK(quarter.mV[VW] == 3.f);

        // A vector off the turn's axis, which the turn moves.
        const LLQuaternion q(1.3f, LLVector3(2.32f, -23.2f, -34.1112f));
        LLVector4 vec4b(1.f, 2.f, 3.f, 1.010112f);
        vec4b.rotVec(q);
        const LLVector3 turned = LLVector3(1.f, 2.f, 3.f) * q;
        CHECK(vec4b.mV[VX] == turned.mV[VX]);
        CHECK(vec4b.mV[VY] == turned.mV[VY]);
        CHECK(vec4b.mV[VZ] == turned.mV[VZ]);
        CHECK(vec4b.mV[VW] == 1.010112f);
    }

    TEST_CASE("v4math_h: operator<< prints equal vectors the same", "[v4math_h]")
    {
        F32 x = 10.f, y = -2.3f, z = -.023f, w = -2.0f;
        LLVector4 vec4(x,y,z,w),vec4a;;
        std::ostringstream stream1, stream2;
        stream1 << vec4;
        vec4a.setVec(x,y,z,w);
        stream2 << vec4a;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v4math_h: operator+ and operator+=", "[v4math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f, z1 = -1.1f, w1 = .23f;
        F32 x2 = 1.2f, y2 = 2.5f, z2 = 1.f, w2 = 1.3f;
        LLVector4 vec4(x1,y1,z1,w1),vec4a(x2,y2,z2,w2),vec4b;
        vec4b = vec4a + vec4;
        CHECK(is_approx_equal(x1+x2,vec4b.mV[VX]));
        CHECK(is_approx_equal(y1+y2,vec4b.mV[VY]));
        CHECK(is_approx_equal(z1+z2,vec4b.mV[VZ]));
        x1 = -2.45f, y1 = 2.1f, z1 = 3.0f;
        vec4.clearVec();
        vec4a.clearVec();
        vec4.setVec(x1,y1,z1);
        vec4a +=vec4;
        CHECK(vec4a == vec4);
        vec4a += vec4;
        CHECK(is_approx_equal(2*x1,vec4a.mV[VX]));
        CHECK(is_approx_equal(2*y1,vec4a.mV[VY]));
        CHECK(is_approx_equal(2*z1,vec4a.mV[VZ]));
    }
    TEST_CASE("v4math_h: operator- and operator-=", "[v4math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f, z1 = -1.1f, w1 = .23f;
        F32 x2 = 1.2f, y2 = 2.5f, z2 = 1.f, w2 = 1.3f;
        LLVector4 vec4(x1,y1,z1,w1),vec4a(x2,y2,z2,w2),vec4b;
        vec4b = vec4a - vec4;
        CHECK(is_approx_equal(x2-x1,vec4b.mV[VX]));
        CHECK(is_approx_equal(y2-y1,vec4b.mV[VY]));
        CHECK(is_approx_equal(z2-z1,vec4b.mV[VZ]));
        x1 = -2.45f, y1 = 2.1f, z1 = 3.0f;
        vec4.clearVec();
        vec4a.clearVec();
        vec4.setVec(x1,y1,z1);
        vec4a -=vec4;
        CHECK(vec4a == -vec4);
        vec4a -=vec4;
        CHECK(is_approx_equal(-2*x1,vec4a.mV[VX]));
        CHECK(is_approx_equal(-2*y1,vec4a.mV[VY]));
        CHECK(is_approx_equal(-2*z1,vec4a.mV[VZ]));
    }

    TEST_CASE("v4math_h: dot product and scalar multiplication", "[v4math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f, z1 = -1.1f;
        F32 x2 = 1.2f, y2 = 2.5f, z2 = 1.f;
        LLVector4 vec4(x1,y1,z1),vec4a(x2,y2,z2);
        F32 res = vec4 * vec4a;
        CHECK(is_approx_equal(res, x1*x2 + y1*y2 + z1*z2));
        vec4a.clearVec();
        F32 mulVal = 4.2f;
        vec4a = vec4 * mulVal;
        CHECK(is_approx_equal(x1*mulVal,vec4a.mV[VX]));
        CHECK(is_approx_equal(y1*mulVal, vec4a.mV[VY]));
        CHECK(is_approx_equal(z1*mulVal, vec4a.mV[VZ]));
        vec4a.clearVec();
        vec4a = mulVal *  vec4 ;
        CHECK(is_approx_equal(x1*mulVal, vec4a.mV[VX]));
        CHECK(is_approx_equal(y1*mulVal, vec4a.mV[VY]));
        CHECK(is_approx_equal(z1*mulVal, vec4a.mV[VZ]));
        vec4 *= mulVal;
        CHECK(is_approx_equal(x1*mulVal, vec4.mV[VX]));
        CHECK(is_approx_equal(y1*mulVal, vec4.mV[VY]));
        CHECK(is_approx_equal(z1*mulVal, vec4.mV[VZ]));
    }

    TEST_CASE("v4math_h: cross product operator% and operator%=", "[v4math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f, z1 = -1.1f;
        F32 x2 = 1.2f, y2 = 2.5f, z2 = 1.f;
        LLVector4 vec4(x1,y1,z1),vec4a(x2,y2,z2),vec4b;
        vec4b = vec4 % vec4a;
        CHECK(is_approx_equal(y1*z2 - y2*z1, vec4b.mV[VX]));
        CHECK(is_approx_equal(z1*x2 -z2*x1, vec4b.mV[VY]));
        CHECK(is_approx_equal(x1*y2-x2*y1, vec4b.mV[VZ]));
        vec4 %= vec4a;
        CHECK(vec4 == vec4b);
    }

    TEST_CASE("v4math_h: operator/ and operator/= by a scalar", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f,div = 4.2f;
        F32 t = 1.f / div;
        LLVector4 vec4(x,y,z), vec4a;
        vec4a = vec4/div;
        CHECK(is_approx_equal(x*t, vec4a.mV[VX]));
        CHECK(is_approx_equal(y*t, vec4a.mV[VY]));
        CHECK(is_approx_equal(z*t, vec4a.mV[VZ]));
        x = 1.23f, y = 4.f, z = -2.32f;
        vec4.clearVec();
        vec4a.clearVec();
        vec4.setVec(x,y,z);
        vec4a = vec4/div;
        CHECK(is_approx_equal(x*t, vec4a.mV[VX]));
        CHECK(is_approx_equal(y*t, vec4a.mV[VY]));
        CHECK(is_approx_equal(z*t, vec4a.mV[VZ]));
        vec4 /= div;
        CHECK(is_approx_equal(x*t, vec4.mV[VX]));
        CHECK(is_approx_equal(y*t, vec4.mV[VY]));
        CHECK(is_approx_equal(z*t, vec4.mV[VZ]));
    }

    TEST_CASE("v4math_h: operator== and operator!=", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f;
        LLVector4 vec4(x,y,z), vec4a;
        CHECK(vec4 != vec4a);
        vec4a = vec4;
        CHECK(vec4 ==vec4a);
    }

    TEST_CASE("v4math_h: unary minus negates", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f;
        LLVector4 vec4(x,y,z), vec4a;
        vec4a = - vec4;
        CHECK(vec4 == - vec4a);
    }

    TEST_CASE("v4math_h: are_parallel within an epsilon", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f,epsilon = .23425f;
        LLVector4 vec4(x,y,z), vec4a(x,y,z);
        CHECK(are_parallel(vec4a,vec4,epsilon));
        x = 21.f, y = 12.f, z = -123.1f;
        vec4a.clearVec();
        vec4a.setVec(x,y,z);
        CHECK_FALSE(are_parallel(vec4a,vec4,epsilon));
    }

    TEST_CASE("v4math_h: angle_between matches acos of the normalized dot product", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f;
        F32 angle1, angle2;
        LLVector4 vec4(x,y,z), vec4a(x,y,z);
        angle1 = angle_between(vec4, vec4a);
        vec4.normVec();
        vec4a.normVec();
        angle2 = acos(vec4 * vec4a);
        CHECK_THAT(angle1, ll_test::WithinBits(angle2, 8));
        F32 x1 = 21.f, y1 = 2.23f, z1 = -1.1f;
        LLVector4 vec4b(x,y,z), vec4c(x1,y1,z1);
        angle1 = angle_between(vec4b, vec4c);
        vec4b.normVec();
        vec4c.normVec();
        angle2 = acos(vec4b * vec4c);
        CHECK_THAT(angle1, ll_test::WithinBits(angle2, 8));
    }

    TEST_CASE("v4math_h: dist_vec and dist_vec_squared", "[v4math_h]")
    {
        F32 x1 =-2.3f, y1 = 2.f,z1 = 1.2f, x2 = 1.3f, y2 = 1.f, z2 = 1.f;
        F32 val1,val2;
        LLVector4 vec4(x1,y1,z1),vec4a(x2,y2,z2);
        val1 = dist_vec(vec4,vec4a);
        val2 = (F32) sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2) + (z1 - z2)* (z1 -z2));
        CHECK(val2 == val1);
        val1 = dist_vec_squared(vec4,vec4a);
        val2 =((x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2) + (z1 - z2)* (z1 -z2));
        CHECK(val2 == val1);
    }

    TEST_CASE("v4math_h: lerp interpolates every component", "[v4math_h]")
    {
        F32 x1 =-2.3f, y1 = 2.f,z1 = 1.2f, w1 = -.23f, x2 = 1.3f, y2 = 1.f, z2 = 1.f,w2 = .12f;
        F32 val = 2.3f,val1,val2,val3,val4;
        LLVector4 vec4(x1,y1,z1,w1),vec4a(x2,y2,z2,w2);
        val1 = x1 + (x2 - x1)* val;
        val2 = y1 + (y2 - y1)* val;
        val3 = z1 + (z2 - z1)* val;
        val4 = w1 + (w2 - w1)* val;
        LLVector4 vec4b = lerp(vec4,vec4a,val);
        LLVector4 check(val1, val2, val3, val4);
        CHECK(check == vec4b);
    }

    TEST_CASE("v4math_h: vec4to3 and vec3to4", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f;
        LLVector4 vec4(x,y,z);
        LLVector3 vec3 = vec4to3(vec4);
        CHECK(x == vec3.mV[VX]);
        CHECK(y == vec3.mV[VY]);
        CHECK(z == vec3.mV[VZ]);
        LLVector4 vec4a = vec3to4(vec3);
        CHECK(vec4a == vec4);
    }

    TEST_CASE("v4math_h: getValue round-trips through LLVector3", "[v4math_h]")
    {
        F32 x = 1.f, y = 2.f, z = -1.1f;
        LLVector4 vec4(x,y,z);
        LLSD llsd = vec4.getValue();
        LLVector3 vec3(llsd);
        LLVector4 vec4a = vec3to4(vec3);
        CHECK(vec4a == vec4);
    }
}
