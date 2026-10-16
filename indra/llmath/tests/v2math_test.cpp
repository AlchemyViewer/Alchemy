/**
 * @file v2math_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief v2math test cases.
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

#include "../v2math.h"


namespace
{
    TEST_CASE("v2math_h: constructors and clearVec/zeroVec", "[v2math_h]")
    {
        LLVector2 vec2;
        CHECK(vec2.mV[VX] == 0.f);
        CHECK(vec2.mV[VY] == 0.f);

        F32 x =2.0f, y = 3.2f ;
        LLVector2 vec3(x,y);
        CHECK(x == vec3.mV[VX]);
        CHECK(y == vec3.mV[VY]);

        const F32 vec[2] = {3.2f, 4.5f};
        LLVector2 vec4(vec);
        CHECK(vec[0] == vec4.mV[VX]);
        CHECK(vec[1] == vec4.mV[VY]);

        vec4.clearVec();
        CHECK(vec4.mV[VX] == 0.f);
        CHECK(vec4.mV[VY] == 0.f);

        vec3.zeroVec();
        CHECK(vec3.mV[VX] == 0.f);
        CHECK(vec3.mV[VY] == 0.f);
    }

    TEST_CASE("v2math_h: the setVec overloads", "[v2math_h]")
    {
        F32 x = 123.356f, y = 2387.453f;
        LLVector2 vec2,vec3;
        vec2.setVec(x, y);
        CHECK(x == vec2.mV[VX]);
        CHECK(y == vec2.mV[VY]);

        vec3.setVec(vec2);
        CHECK(vec2 == vec3);

        vec3.zeroVec();
        const F32 vec[2] = {3.24653f, 457653.4f};
        vec3.setVec(vec);
        CHECK(vec[0] == vec3.mV[VX]);
        CHECK(vec[1] == vec3.mV[VY]);
    }

    TEST_CASE("v2math_h: magVecSquared and magVec", "[v2math_h]")
    {
        F32 x = 2.2345f, y = 3.5678f ;
        LLVector2 vec2(x,y);
        CHECK(is_approx_equal(vec2.magVecSquared(), (x*x + y*y)));
        CHECK(is_approx_equal(vec2.magVec(), (F32) sqrt(x*x + y*y)));
    }

    TEST_CASE("v2math_h: abs then isNull on long and very short vectors", "[v2math_h]")
    {
        F32 x =-2.0f, y = -3.0f ;
        LLVector2 vec2(x,y);
        CHECK(vec2.abs());
        CHECK(is_approx_equal(vec2.mV[VX], 2.f));
        CHECK(is_approx_equal(vec2.mV[VY], 3.f));

        CHECK_FALSE(vec2.isNull());   //Returns true if vector has a _very_small_ length

        x =.00000001f, y = .000001001f;
        vec2.setVec(x, y);
        CHECK(vec2.isNull());
    }

    TEST_CASE("v2math_h: scaleVec and isExactlyZero", "[v2math_h]")
    {
        F32 x =1.f, y = 2.f;
        LLVector2 vec2(x, y), vec3;
        vec3 = vec3.scaleVec(vec2);
        CHECK(vec3.mV[VX] == 0.);
        CHECK(vec3.mV[VY] == 0.);

        CHECK(vec3.isExactlyZero());

        vec3.setVec(2.f, 1.f);
        vec3 = vec3.scaleVec(vec2);
        CHECK(vec3.mV[VX] == 2.f);
        CHECK(vec3.mV[VY] == 2.f);

        CHECK_FALSE(vec3.isExactlyZero());
    }

    TEST_CASE("v2math_h: operator+ adds componentwise", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f, x2 = -2.3f, y2 = 1.11f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2), vec4;
        vec4 = vec2 + vec3 ;
        val1 = x1+x2;
        val2 = y1+y2;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);

        vec2.clearVec();
        vec3.clearVec();
        x1 = -.235f, y1 = -24.32f,  x2 = -2.3f, y2 = 1.f;
        vec2.setVec(x1, y1);
        vec3.setVec(x2, y2);
        vec4 = vec2 + vec3;
        val1 = x1+x2;
        val2 = y1+y2;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);
    }

    TEST_CASE("v2math_h: operator- subtracts componentwise", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,  x2 = -2.3f, y2 = 1.11f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2), vec4;
        vec4 = vec2 - vec3 ;
        val1 = x1-x2;
        val2 = y1-y2;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);

        vec2.clearVec();
        vec3.clearVec();
        vec4.clearVec();
        x1 = -.235f, y1 = -24.32f,  x2 = -2.3f, y2 = 1.f;
        vec2.setVec(x1, y1);
        vec3.setVec(x2, y2);
        vec4 = vec2 - vec3;
        val1 = x1-x2;
        val2 = y1-y2;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);
    }

    TEST_CASE("v2math_h: operator* as dot product and scalar product", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,  x2 = -2.3f, y2 = 1.11f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2);
        val1 = vec2 * vec3;
        val2 = x1*x2 + y1*y2;
        CHECK(val1 == val2);

        vec3.clearVec();
        F32 mulVal = 4.332f;
        vec3 = vec2 * mulVal;
        val1 = x1*mulVal;
        val2 = y1*mulVal;
        CHECK(val1 == vec3.mV[VX]);
        CHECK(val2 == vec3.mV[VY]);

        vec3.clearVec();
        vec3 = mulVal * vec2;
        CHECK(val1 == vec3.mV[VX]);
        CHECK(val2 == vec3.mV[VY]);
    }

    TEST_CASE("v2math_h: operator/ divides by a scalar", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f, div = 3.2f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3;
        vec3 = vec2 / div;
        val1 = x1 / div;
        val2 = y1 / div;
        CHECK(is_approx_equal(val1, vec3.mV[VX]));
        CHECK(is_approx_equal(val2, vec3.mV[VY]));

        vec3.clearVec();
        x1 = -.235f, y1 = -24.32f, div = -2.2f;
        vec2.setVec(x1, y1);
        vec3 = vec2 / div;
        val1 = x1 / div;
        val2 = y1 / div;
        CHECK(is_approx_equal(val1, vec3.mV[VX]));
        CHECK(is_approx_equal(val2, vec3.mV[VY]));
    }

    TEST_CASE("v2math_h: operator% is the cross product", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,  x2 = -2.3f, y2 = 1.11f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2), vec4;
        vec4 = vec2 % vec3;
        val1 = x1*y2 - x2*y1;
        val2 = y1*x2 - y2*x1;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);

        vec2.clearVec();
        vec3.clearVec();
        vec4.clearVec();
        x1 = -.235f, y1 = -24.32f,  x2 = -2.3f, y2 = 1.f;
        vec2.setVec(x1, y1);
        vec3.setVec(x2, y2);
        vec4 = vec2 % vec3;
        val1 = x1*y2 - x2*y1;
        val2 = y1*x2 - y2*x1;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);
    }

    TEST_CASE("v2math_h: operator== for equal vectors", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        LLVector2 vec2(x1, y1), vec3(x1, y1);
        CHECK(vec2 == vec3);

        vec2.clearVec();
        vec3.clearVec();
        x1 = -.235f, y1 = -24.32f;
        vec2.setVec(x1, y1);
        vec3.setVec(vec2);
        CHECK(vec2 == vec3);
    }

    TEST_CASE("v2math_h: operator!= for different and equal vectors", "[v2math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f,x2 = 2.332f, y2 = -1.23f;
        LLVector2 vec2(x1, y1), vec3(x2, y2);
        CHECK(vec2 != vec3);

        vec2.clearVec();
        vec3.clearVec();
        vec2.setVec(x1, y1);
        vec3.setVec(vec2);
        CHECK_FALSE(vec2 != vec3);
    }

    TEST_CASE("v2math_h: operator+= and operator-=", "[v2math_h]")
    {
        F32 x1 = 1.f, y1 = 2.f,x2 = 2.332f, y2 = -1.23f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2);
        vec2 +=vec3;
        val1 = x1+x2;
        val2 = y1+y2;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);

        vec2.setVec(x1, y1);
        vec2 -=vec3;
        val1 = x1-x2;
        val2 = y1-y2;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);

        vec2.clearVec();
        vec3.clearVec();
        x1 = -21.000466f, y1 = 2.98382f,x2 = 0.332f, y2 = -01.23f;
        vec2.setVec(x1, y1);
        vec3.setVec(x2, y2);
        vec2 +=vec3;
        val1 = x1+x2;
        val2 = y1+y2;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);

        vec2.setVec(x1, y1);
        vec2 -=vec3;
        val1 = x1-x2;
        val2 = y1-y2;
        CHECK(is_approx_equal(val1, vec2.mV[VX]));
        CHECK(is_approx_equal(val2, vec2.mV[VY]));
    }

    TEST_CASE("v2math_h: operator/= divides by a scalar", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        F32 val1, val2, mulVal = 4.332f;
        LLVector2 vec2(x1, y1);
        vec2 /=mulVal;
        val1 = x1 / mulVal;
        val2 = y1 / mulVal;
        CHECK(is_approx_equal(val1, vec2.mV[VX]));
        CHECK(is_approx_equal(val2, vec2.mV[VY]));

        vec2.clearVec();
        x1 = .213f, y1 = -2.34f, mulVal = -.23f;
        vec2.setVec(x1, y1);
        vec2 /=mulVal;
        val1 = x1 / mulVal;
        val2 = y1 / mulVal;
        CHECK(is_approx_equal(val1, vec2.mV[VX]));
        CHECK(is_approx_equal(val2, vec2.mV[VY]));
    }

    TEST_CASE("v2math_h: operator*= multiplies by a scalar", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        F32 val1, val2, mulVal = 4.332f;
        LLVector2 vec2(x1, y1);
        vec2 *=mulVal;
        val1 = x1*mulVal;
        val2 = y1*mulVal;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);

        vec2.clearVec();
        x1 = .213f, y1 = -2.34f, mulVal = -.23f;
        vec2.setVec(x1, y1);
        vec2 *=mulVal;
        val1 = x1*mulVal;
        val2 = y1*mulVal;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);
    }

    TEST_CASE("v2math_h: operator%= matches the cross product", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f,  x2 = -2.3f, y2 = 1.11f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1), vec3(x2, y2);
        vec2 %= vec3;
        val1 = x1*y2 - x2*y1;
        val2 = y1*x2 - y2*x1;
        CHECK(val1 == vec2.mV[VX]);
        CHECK(val2 == vec2.mV[VY]);
    }

    TEST_CASE("v2math_h: unary operator- negates", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        LLVector2 vec2(x1, y1),vec3;
        vec3 = -vec2;
        CHECK(-vec3 == vec2);
    }

    TEST_CASE("v2math_h: operator<< prints equal vectors alike", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        std::ostringstream stream1, stream2;
        LLVector2 vec2(x1, y1),vec3;
        stream1 << vec2;
        vec3.setVec(x1, y1);
        stream2 << vec3;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v2math_h: operator< orders by x then y", "[v2math_h]")
    {
        F32 x1 =1.0f, y1 = 2.0f, x2 = -.32f, y2 = .2234f;
        LLVector2 vec2(x1, y1),vec3(x2, y2);
        CHECK(vec3 < vec2);

        x1 = 1.0f, y1 = 2.0f, x2 = 1.0f, y2 = 3.2234f;
        vec2.setVec(x1, y1);
        vec3.setVec(x2, y2);
        CHECK_FALSE(vec3 < vec2);
    }

    TEST_CASE("v2math_h: subscript operator reads each component", "[v2math_h]")
    {
        F32 x1 =1.0f, y1 = 2.0f;
        LLVector2 vec2(x1, y1);
        CHECK(x1 ==  vec2[0]);
        CHECK(y1 ==  vec2[1]);

        vec2.clearVec();
        x1 = 23.0f, y1 = -.2361f;
        vec2.setVec(x1, y1);
        F32 ref1 = vec2[0];
        CHECK(ref1 ==  x1);
        F32 ref2 = vec2[1];
        CHECK(ref2 ==  y1);
    }

    TEST_CASE("v2math_h: dist_vec and its squared and 2D forms", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f, x2 = -.32f, y2 = .2234f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1),vec3(x2, y2);
        val1 = dist_vec_squared2D(vec2, vec3);
        val2 = (x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2);
        CHECK(val2 == val1);

        val1 = dist_vec_squared(vec2, vec3);
        CHECK(val2 == val1);

        val1 =  dist_vec(vec2, vec3);
        val2 = (F32) sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)* (y1 - y2));
        CHECK(val2 == val1);
    }

    TEST_CASE("v2math_h: lerp between two vectors", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f, x2 = -.32f, y2 = .2234f,fVal = .0121f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1),vec3(x2, y2);
        LLVector2 vec4 = lerp(vec2, vec3, fVal);
        val1 = x1 + (x2 - x1) * fVal;
        val2 = y1 + (y2 - y1) * fVal;
        CHECK(val1 == vec4.mV[VX]);
        CHECK(val2 == vec4.mV[VY]);
    }

    TEST_CASE("v2math_h: normVec normalises and zeroes a tiny vector", "[v2math_h]")
    {
        F32 x1 =1.f, y1 = 2.f;
        F32 val1, val2;
        LLVector2 vec2(x1, y1);

        F32 vecMag = vec2.normVec();
        F32 mag = (F32) sqrt(x1*x1 + y1*y1);

        F32 oomag = 1.f / mag;
        val1 = x1 * oomag;
        val2 = y1 * oomag;

        CHECK(is_approx_equal(val1, vec2.mV[VX]));
        CHECK(is_approx_equal(val2, vec2.mV[VY]));
        CHECK(is_approx_equal(vecMag, mag));

        x1 =.00000001f, y1 = 0.f;

        vec2.setVec(x1, y1);
        vecMag = vec2.normVec();
        CHECK(vec2.mV[VX] == 0.);
        CHECK(vec2.mV[VY] == 0.);
        CHECK(vecMag == 0.);
    }
}
