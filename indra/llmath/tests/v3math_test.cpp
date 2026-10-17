/**
 * @file v3math_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief v3math test cases.
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
#include "llsd.h"

#include "../v3dmath.h"
#include "../m3math.h"
#include "../v4math.h"
#include "../v3math.h"
#include "../llquaternion.h"
#include "../llquantize.h"
#include "altestmatchers.h"


namespace
{
    TEST_CASE("v3math_h: constructors set every component", "[v3math_h]")
    {
        LLVector3 vec3;
        CHECK(vec3.mV[VX] == 0.f);
        CHECK(vec3.mV[VY] == 0.f);
        CHECK(vec3.mV[VZ] == 0.f);

        F32 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3 vec3a(x,y,z);
        CHECK(vec3a.mV[VX] == 2.32f);
        CHECK(vec3a.mV[VY] == 1.212f);
        CHECK(vec3a.mV[VZ] == -.12f);

        const F32 vec[3] = {1.2f ,3.2f, -4.2f};
        LLVector3 vec3b(vec);
        CHECK(vec3b.mV[VX] == 1.2f);
        CHECK(vec3b.mV[VY] == 3.2f);
        CHECK(vec3b.mV[VZ] == -4.2f);
    }

    TEST_CASE("v3math_h: conversion from LLVector3d and LLVector4", "[v3math_h]")
    {
        F32 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3 vec3(x,y,z);
        LLVector3d vector3d(vec3);
        LLVector3 vec3a(vector3d);
        CHECK(vec3 == vec3a);
        LLVector4 vector4(vec3);
        LLVector3 vec3b(vector4);
        CHECK(vec3 == vec3b);
    }

    TEST_CASE("v3math_h: LLSD constructor and getValue round trip", "[v3math_h]")
    {
        // An array of three: a vector reads sd[0], sd[1] and sd[2], so a
        // scalar LLSD would make every component zero.
        LLSD llsd;
        llsd[0] = 2.32;
        llsd[1] = 1.212;
        llsd[2] = -.12;
        LLVector3 vec3(llsd);
        CHECK(vec3.mV[VX] == 2.32f);
        CHECK(vec3.mV[VY] == 1.212f);
        CHECK(vec3.mV[VZ] == -.12f);
        LLSD sd = vec3.getValue();
        LLVector3 vec3a(sd);
        CHECK(vec3 == vec3a);
    }

    TEST_CASE("v3math_h: assignment copies every component", "[v3math_h]")
    {
        LLVector3 vec3(2.32f, 1.212f, -.12f), vec3a;
        vec3a = vec3;
        CHECK(vec3a.mV[VX] == 2.32f);
        CHECK(vec3a.mV[VY] == 1.212f);
        CHECK(vec3a.mV[VZ] == -.12f);
    }

    TEST_CASE("v3math_h: isFinite and the clearVec/setVec/zeroVec setters", "[v3math_h]")
    {
        F32 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3 vec3(x,y,z);
        CHECK(vec3.isFinite());//need more test cases:
        vec3.clearVec();
        CHECK(vec3.mV[VX] == 0.f);
        CHECK(vec3.mV[VY] == 0.f);
        CHECK(vec3.mV[VZ] == 0.f);

        vec3.setVec(x,y,z);
        CHECK(vec3.mV[VX] == 2.32f);
        CHECK(vec3.mV[VY] == 1.212f);
        CHECK(vec3.mV[VZ] == -.12f);

        vec3.zeroVec();
        CHECK(vec3.mV[VX] == 0.f);
        CHECK(vec3.mV[VY] == 0.f);
        CHECK(vec3.mV[VZ] == 0.f);
    }

    TEST_CASE("v3math_h: abs and the setVec overloads", "[v3math_h]")
    {
        F32 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3 vec3(x,y,z),vec3a;
        vec3.abs();
        CHECK(x == vec3.mV[VX]);
        CHECK(y == vec3.mV[VY]);
        CHECK(-z == vec3.mV[VZ]);

        vec3a.setVec(vec3);
        CHECK(vec3a == vec3);
        const F32 vec[3] = {1.2f ,3.2f, -4.2f};
        vec3.clearVec();
        vec3.setVec(vec);
        CHECK(vec3.mV[VX] == 1.2f);
        CHECK(vec3.mV[VY] == 3.2f);
        CHECK(vec3.mV[VZ] == -4.2f);

        vec3a.clearVec();
        LLVector3d vector3d(vec3);
        vec3a.setVec(vector3d);
        CHECK(vec3 == vec3a);
        LLVector4 vector4(vec3);
        vec3a.clearVec();
        vec3a.setVec(vector4);
        CHECK(vec3 == vec3a);
    }

    TEST_CASE("v3math_h: clamp limits components and reports a change", "[v3math_h]")
    {
        F32 x = 2.32f, y = 3.212f, z = -.12f;
        F32 min = 0.0001f, max = 3.0f;
        LLVector3 vec3(x,y,z);
        CHECK(vec3.clamp(min, max));
        CHECK(x == vec3.mV[VX]);
        CHECK(max == vec3.mV[VY]);
        CHECK(min == vec3.mV[VZ]);

        x = 1.f, y = 2.2f, z = 2.8f;
        vec3.setVec(x,y,z);
        CHECK_FALSE(vec3.clamp(min, max));
    }

    TEST_CASE("v3math_h: magVecSquared and magVec", "[v3math_h]")
    {
        F32 x = 2.32f, y = 1.212f, z = -.12f;
        LLVector3 vec3(x,y,z);
        CHECK(is_approx_equal(vec3.magVecSquared(), (x*x + y*y + z*z)));
        CHECK(is_approx_equal(vec3.magVec(), (F32) sqrt(x*x + y*y + z*z)));
    }

    TEST_CASE("v3math_h: abs then isNull on long and very short vectors", "[v3math_h]")
    {
        F32 x =-2.0f, y = -3.0f, z = 1.23f ;
        LLVector3 vec3(x,y,z);
        CHECK(vec3.abs());
        CHECK_FALSE(vec3.isNull());    //Returns true if vector has a _very_small_ length
        x =.00000001f, y = .000001001f, z = .000001001f;
        vec3.setVec(x,y,z);
        CHECK(vec3.isNull());
    }

    TEST_CASE("v3math_h: isExactlyZero and scaleVec", "[v3math_h]")
    {
        F32 x =-2.0f, y = -3.0f, z = 1.f ;
        LLVector3 vec3(x,y,z),vec3a;
        CHECK(vec3a.isExactlyZero());
        vec3a = vec3a.scaleVec(vec3);
        CHECK(vec3a.mV[VX] == 0.f);
        CHECK(vec3a.mV[VY] == 0.f);
        CHECK(vec3a.mV[VZ] == 0.f);

        vec3a.setVec(x,y,z);
        vec3a = vec3a.scaleVec(vec3);
        CHECK(vec3a.mV[VX] == 4);
        CHECK(vec3a.mV[VY] == 9);
        CHECK(vec3a.mV[VZ] == 1);

        CHECK_FALSE(vec3.isExactlyZero());
    }

    TEST_CASE("v3math_h: rotVec turns a vector by an angle about an axis", "[v3math_h]")
    {
        using Catch::Matchers::WithinAbs;

        // A quarter turn about z takes x to y.
        LLVector3 quarter(1.f, 0.f, 0.f);
        quarter.rotVec(F_PI_BY_TWO, LLVector3(0.f, 0.f, 1.f));
        CHECK_THAT(quarter.mV[VX], WithinAbs(0.f, 1e-6));
        CHECK_THAT(quarter.mV[VY], WithinAbs(1.f, 1e-6));
        CHECK_THAT(quarter.mV[VZ], WithinAbs(0.f, 1e-6));

        // Any angle about any axis, against Rodrigues' formula in double:
        // v cos a + (k x v) sin a + k (k . v)(1 - cos a), k the unit axis.
        const F64 angle = 100.0;
        const F64 axis_length = sqrt(20.0 * 20.0 + 30.0 * 30.0 + 15.0 * 15.0);
        const F64 k[3] = { 20.0 / axis_length, 30.0 / axis_length, 15.0 / axis_length };
        const F64 v[3] = { 1.0, 2.0, 3.0 };
        const F64 k_dot_v = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
        const F64 k_cross_v[3] = { k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0] };
        F64 expected[3];
        for (int i = 0; i < 3; ++i)
        {
            expected[i] = v[i] * cos(angle) + k_cross_v[i] * sin(angle) + k[i] * k_dot_v * (1.0 - cos(angle));
        }

        LLVector3 vec3((F32)v[0], (F32)v[1], (F32)v[2]);
        vec3.rotVec((F32)angle, LLVector3(20.f, 30.f, 15.f));
        CHECK_THAT(vec3.mV[VX], WithinAbs(expected[0], 1e-4));
        CHECK_THAT(vec3.mV[VY], WithinAbs(expected[1], 1e-4));
        CHECK_THAT(vec3.mV[VZ], WithinAbs(expected[2], 1e-4));

        // No turn about a zero axis, or by a zero angle.
        LLVector3 still(1.f, 2.f, 3.f);
        still.rotVec((F32)angle, LLVector3::zero);
        CHECK(still == LLVector3(1.f, 2.f, 3.f));
        still.rotVec(0.f, LLVector3(20.f, 30.f, 15.f));
        CHECK(still == LLVector3(1.f, 2.f, 3.f));
    }

    TEST_CASE("v3math_h: subscript operator reads components and returns references", "[v3math_h]")
    {
        F32 x =-2.0f, y = -3.0f, z = 1.f ;
        LLVector3 vec3(x,y,z);
        CHECK(x ==  vec3[0]);
        CHECK(y ==  vec3[1]);
        CHECK(z ==  vec3[2]);

        // The non-const subscript is a reference to the component itself.
        F32 &ref1 = vec3[0];
        CHECK(&ref1 == &vec3.mV[VX]);
        ref1 = 23.f;
        vec3[1] = -.2361f;
        vec3[2] = 3.25f;
        CHECK(vec3.mV[VX] == 23.f);
        CHECK(vec3.mV[VY] == -.2361f);
        CHECK(vec3.mV[VZ] == 3.25f);
    }

    TEST_CASE("v3math_h: operator+ adds componentwise", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val1, val2, val3;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2), vec3b;
        vec3b = vec3 + vec3a ;
        val1 = x1+x2;
        val2 = y1+y2;
        val3 = z1+z2;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);

        vec3.clearVec();
        vec3a.clearVec();
        vec3b.clearVec();
        x1 = -.235f, y1 = -24.32f,z1 = 2.13f,  x2 = -2.3f, y2 = 1.f, z2 = 34.21f;
        vec3.setVec(x1,y1,z1);
        vec3a.setVec(x2,y2,z2);
        vec3b = vec3 + vec3a;
        val1 = x1+x2;
        val2 = y1+y2;
        val3 = z1+z2;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator- subtracts componentwise", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val1, val2, val3;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2), vec3b;
        vec3b = vec3 - vec3a ;
        val1 = x1-x2;
        val2 = y1-y2;
        val3 = z1-z2;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);

        vec3.clearVec();
        vec3a.clearVec();
        vec3b.clearVec();
        x1 = -.235f, y1 = -24.32f,z1 = 2.13f,  x2 = -2.3f, y2 = 1.f, z2 = 34.21f;
        vec3.setVec(x1,y1,z1);
        vec3a.setVec(x2,y2,z2);
        vec3b = vec3 - vec3a;
        val1 = x1-x2;
        val2 = y1-y2;
        val3 = z1-z2;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator* as dot product and scalar product", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val1, val2, val3;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2);
        val1 = vec3 * vec3a;
        val2 = x1*x2 + y1*y2 + z1*z2;
        CHECK(val1 == val2);

        vec3a.clearVec();
        F32 mulVal = 4.332f;
        vec3a = vec3 * mulVal;
        val1 = x1*mulVal;
        val2 = y1*mulVal;
        val3 = z1*mulVal;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);

        vec3a.clearVec();
        vec3a = mulVal * vec3;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator% is the cross product", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val1, val2, val3;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2), vec3b;
        vec3b = vec3 % vec3a ;
        val1 = y1*z2 - y2*z1;
        val2 = z1*x2 -z2*x1;
        val3 = x1*y2-x2*y1;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);

        vec3.clearVec();
        vec3a.clearVec();
        vec3b.clearVec();
        x1 =112.f, y1 = 22.3f,z1 = 1.2f, x2 = -2.3f, y2 = 341.11f, z2 = 1234.234f;
        vec3.setVec(x1,y1,z1);
        vec3a.setVec(x2,y2,z2);
        vec3b = vec3 % vec3a ;
        val1 = y1*z2 - y2*z1;
        val2 = z1*x2 -z2*x1;
        val3 = x1*y2-x2*y1;
        CHECK(val1 == vec3b.mV[VX]);
        CHECK(val2 == vec3b.mV[VY]);
        CHECK(val3 == vec3b.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator/ divides by a scalar", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, div = 3.2f;
        F32 t = 1.f / div, val1, val2, val3;
        LLVector3 vec3(x1,y1,z1), vec3a;
        vec3a = vec3 / div;
        val1 = x1 * t;
        val2 = y1 * t;
        val3 = z1 *t;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);

        vec3a.clearVec();
        x1 = -.235f, y1 = -24.32f, z1 = .342f, div = -2.2f;
        t = 1.f / div;
        vec3.setVec(x1,y1,z1);
        vec3a = vec3 / div;
        val1 = x1 * t;
        val2 = y1 * t;
        val3 = z1 *t;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator== for equal vectors", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f;
        LLVector3 vec3(x1,y1,z1), vec3a(x1,y1,z1);
        CHECK(vec3 == vec3a);

        vec3a.clearVec();
        x1 = -.235f, y1 = -24.32f, z1 = .342f;
        vec3.clearVec();
        vec3a.clearVec();
        vec3.setVec(x1,y1,z1);
        vec3a.setVec(x1,y1,z1);
        CHECK(vec3 == vec3a);
    }

    TEST_CASE("v3math_h: operator!= for different and equal vectors", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 =112.f, y2 = 2.234f,z2 = 11.2f;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2);
        CHECK(vec3a != vec3);

        // One component apart is enough.
        vec3a.setVec(x1, y1, z2);
        CHECK(vec3a != vec3);

        vec3a.setVec(vec3);
        CHECK_FALSE(vec3a != vec3);
    }

    TEST_CASE("v3math_h: operator+= adds componentwise", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 =112.f, y2 = 2.2f,z2 = 11.2f;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2);
        vec3a += vec3;
        F32 val1, val2, val3;
        val1 = x1+x2;
        val2 = y1+y2;
        val3 = z1+z2;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator-= subtracts componentwise", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 =112.f, y2 = 2.2f,z2 = 11.2f;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2);
        vec3a -= vec3;
        F32 val1, val2, val3;
        val1 = x2-x1;
        val2 = y2-y1;
        val3 = z2-z1;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);
    }

    TEST_CASE("v3math_h: operator*= by a vector and by a scalar", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val1,val2,val3;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2);
        vec3a *= vec3;
        val1 = x1*x2;
        val2 = y1*y2;
        val3 = z1*z2;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);

        F32 mulVal = 4.332f;
        vec3 *=mulVal;
        val1 = x1*mulVal;
        val2 = y1*mulVal;
        val3 = z1*mulVal;
        CHECK(is_approx_equal(val1, vec3.mV[VX]));
        CHECK(is_approx_equal(val2, vec3.mV[VY]));
        CHECK(is_approx_equal(val3, vec3.mV[VZ]));
    }

    TEST_CASE("v3math_h: operator%= matches operator%", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, x2 = -2.3f, y2 = 1.11f, z2 = 1234.234f;
        LLVector3 vec3(x1,y1,z1), vec3a(x2,y2,z2),vec3b;
        vec3b = vec3a % vec3;
        vec3a %= vec3;
        CHECK(vec3a == vec3b);
    }

    TEST_CASE("v3math_h: operator/= divides by a scalar", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f, div = 3.2f;
        F32 t = 1.f / div, val1, val2, val3;
        LLVector3 vec3a(x1,y1,z1);
        vec3a /= div;
        val1 = x1 * t;
        val2 = y1 * t;
        val3 = z1 *t;
        CHECK(val1 == vec3a.mV[VX]);
        CHECK(val2 == vec3a.mV[VY]);
        CHECK(val3 == vec3a.mV[VZ]);
    }

    TEST_CASE("v3math_h: unary operator- negates", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f;
        LLVector3 vec3(x1,y1,z1), vec3a;
        vec3a = -vec3;
        CHECK(-vec3a == vec3);
    }

    TEST_CASE("v3math_h: operator<< prints equal vectors alike", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 1.2f;
        std::ostringstream stream1, stream2;
        LLVector3 vec3(x1,y1,z1), vec3a;
        stream1 << vec3;
        vec3a.setVec(x1,y1,z1);
        stream2 << vec3a;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v3math_h: operator< orders by x, then y, then z", "[v3math_h]")
    {
        // x decides wherever the two differ in it, whatever y and z say.
        CHECK(LLVector3(-2.3f, 2.f, 1.2f) < LLVector3(1.3f, 1.11f, 1234.234f));
        CHECK_FALSE(LLVector3(2.3f, 2.f, 1.2f) < LLVector3(1.3f, 2.f, 1234.234f));
        // Equal in x, y decides.
        CHECK(LLVector3(1.f, 2.f, 9.f) < LLVector3(1.f, 3.f, 0.f));
        CHECK_FALSE(LLVector3(1.f, 3.f, 0.f) < LLVector3(1.f, 2.f, 9.f));
        // Equal in x and y, z decides.
        CHECK(LLVector3(1.f, 2.f, 3.f) < LLVector3(1.f, 2.f, 4.f));
        CHECK_FALSE(LLVector3(1.f, 2.f, 4.f) < LLVector3(1.f, 2.f, 3.f));
        // Strict: no vector is less than itself.
        CHECK_FALSE(LLVector3(1.f, 2.f, 3.f) < LLVector3(1.f, 2.f, 3.f));
    }

    TEST_CASE("v3math_h: parseVector3 reads three numbers", "[v3math_h]")
    {
        F32 x1 =1.23f, y1 = 2.f,z1 = 4.f;
        std::string buf("1.23 2. 4");
        LLVector3 vec3, vec3a(x1,y1,z1);
        LLVector3::parseVector3(buf, &vec3);
        CHECK(vec3 == vec3a);
    }

    TEST_CASE("v3math_h: scaleVec multiplies componentwise", "[v3math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,z1 = 4.f;
        LLVector3 vec3(x1,y1,z1),vec3a,vec3b;
        vec3a.setVec(1,1,1);
        vec3a.scaleVec(vec3);
        CHECK(vec3 == vec3a);
        vec3a.clearVec();
        vec3a.setVec(x1,y1,z1);
        vec3a.scaleVec(vec3);
        CHECK(vec3a.mV[VX] == 1.f);
        CHECK(vec3a.mV[VY] == 4.f);
        CHECK(vec3a.mV[VZ] == 16.f);
    }

    TEST_CASE("v3math_h: lerp between two vectors", "[v3math_h]")
    {
        F32 x1 =-2.3f, y1 = 2.f,z1 = 1.2f, x2 = 1.3f, y2 = 1.11f, z2 = 1234.234f;
        F32 val = 2.3f,val1,val2,val3;
        val1 = x1 + (x2 - x1)* val;
        val2 = y1 + (y2 - y1)* val;
        val3 = z1 + (z2 - z1)* val;
        LLVector3 vec3(x1,y1,z1),vec3a(x2,y2,z2);
        LLVector3 vec3b = lerp(vec3,vec3a,val);
        // Within the rounding of the larger of the two terms each sums, which
        // the library's lerp and this working of it may round differently.
        auto terms = [&](F32 a, F32 b) { return llmax(fabsf(a), fabsf((b - a) * val)); };
        CHECK_THAT(vec3b.mV[VX], ll_test::WithinRounding(val1, terms(x1, x2)));
        CHECK_THAT(vec3b.mV[VY], ll_test::WithinRounding(val2, terms(y1, y2)));
        CHECK_THAT(vec3b.mV[VZ], ll_test::WithinRounding(val3, terms(z1, z2)));
    }

    TEST_CASE("v3math_h: dist_vec and its squared and 2D forms", "[v3math_h]")
    {
        F32 x1 =-2.3f, y1 = 2.f,z1 = 1.2f, x2 = 1.3f, y2 = 1.f, z2 = 1.f;
        F32 val1,val2;
        LLVector3 vec3(x1,y1,z1),vec3a(x2,y2,z2);
        val1 = dist_vec(vec3,vec3a);
        val2 = (F32) sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2) + (z1 - z2)* (z1 -z2));
        CHECK(val2 == val1);
        val1 = dist_vec_squared(vec3,vec3a);
        val2 =((x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2) + (z1 - z2)* (z1 -z2));
        CHECK(val2 == val1);
        val1 = dist_vec_squared2D(vec3, vec3a);
        val2 =(x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2);
        CHECK(val2 == val1);
    }

    TEST_CASE("v3math_h: normVec normalises and zeroes a tiny vector", "[v3math_h]")
    {
        F32 x =12.3524f, y = -342.f,z = 4.126341f;
        LLVector3 vec3(x,y,z);
        F32 mag = vec3.normVec();
        mag = 1.f/ mag;
        F32 val1 = x* mag, val2 = y* mag, val3 = z* mag;
        CHECK(is_approx_equal(val1, vec3.mV[VX]));
        CHECK(is_approx_equal(val2, vec3.mV[VY]));
        CHECK(is_approx_equal(val3, vec3.mV[VZ]));

        x = 0.000000001f, y = 0.f, z = 0.f;
        vec3.clearVec();
        vec3.setVec(x,y,z);
        mag = vec3.normVec();
        val1 = x* mag, val2 = y* mag, val3 = z* mag;
        CHECK(mag == 0.);
        CHECK(vec3.mV[VX] == 0.);
        CHECK(vec3.mV[VY] == 0.);
        CHECK(vec3.mV[VZ] == 0.);
    }

    TEST_CASE("v3math_h: snap rounds to the given decimal places", "[v3math_h]")
    {
        F32 x = -202.23412f, y = 123.2312f, z = -89.f;
        LLVector3 vec(x,y,z);
        vec.snap(2);
        CHECK(is_approx_equal(-202.23f, vec.mV[VX]));
        CHECK(is_approx_equal(123.23f, vec.mV[VY]));
        CHECK(is_approx_equal(-89.f, vec.mV[VZ]));
    }

    TEST_CASE("v3math_h: quantize16 and quantize8 match the U16 and U8 round trips", "[v3math_h]")
    {
        F32 x = 10.f, y = 20.f, z = -15.f;
        F32 x1, y1, z1;
        F32 lowerxy = 0.f, upperxy = 1.0f, lowerz = -1.0f, upperz = 1.f;
        LLVector3 vec3(x,y,z);
        vec3.quantize16(lowerxy,upperxy,lowerz,upperz);
        x1 = U16_to_F32(F32_to_U16(x, lowerxy, upperxy), lowerxy, upperxy);
        y1 = U16_to_F32(F32_to_U16(y, lowerxy, upperxy), lowerxy, upperxy);
        z1 = U16_to_F32(F32_to_U16(z, lowerz,  upperz),  lowerz,  upperz);
        CHECK(is_approx_equal(x1, vec3.mV[VX]));
        CHECK(is_approx_equal(y1, vec3.mV[VY]));
        CHECK(is_approx_equal(z1, vec3.mV[VZ]));

        LLVector3 vec3a(x,y,z);
        vec3a.quantize8(lowerxy,upperxy,lowerz,upperz);
        x1 = U8_to_F32(F32_to_U8(x, lowerxy, upperxy), lowerxy, upperxy);
        y1 = U8_to_F32(F32_to_U8(y, lowerxy, upperxy), lowerxy, upperxy);
        z1 = U8_to_F32(F32_to_U8(z, lowerz, upperz), lowerz, upperz);
        CHECK(is_approx_equal(x1, vec3a.mV[VX]));
        CHECK(is_approx_equal(y1, vec3a.mV[VY]));
        CHECK(is_approx_equal(z1, vec3a.mV[VZ]));
    }

    TEST_CASE("v3math_h: LLSD arrays shorter than three leave the rest zero", "[v3math_h]")
    {
        LLSD sd = LLSD::emptyArray();
        sd[0] = 1.f;

        LLVector3 parsed_1(sd);
        CHECK(is_approx_equal(parsed_1.mV[VX], 1.f));
        CHECK(is_approx_equal(parsed_1.mV[VY], 0.f));
        CHECK(is_approx_equal(parsed_1.mV[VZ], 0.f));

        sd[1] = 2.f;
        LLVector3 parsed_2(sd);
        CHECK(is_approx_equal(parsed_2.mV[VX], 1.f));
        CHECK(is_approx_equal(parsed_2.mV[VY], 2.f));
        CHECK(is_approx_equal(parsed_2.mV[VZ], 0.f));

        sd[2] = 3.f;
        LLVector3 parsed_3(sd);
        CHECK(is_approx_equal(parsed_3.mV[VX], 1.f));
        CHECK(is_approx_equal(parsed_3.mV[VY], 2.f));
        CHECK(is_approx_equal(parsed_3.mV[VZ], 3.f));
    }
}
