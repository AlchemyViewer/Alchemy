/**
 * @file v3color_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief v3color test cases.
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

#include "../v3color.h"
#include "altestmatchers.h"


namespace
{
    TEST_CASE("v3color_h: constructors take nothing or components or an array or a hex string", "[v3color_h]")
    {
        LLColor3 llcolor3;
        CHECK(llcolor3.mV[0] == 0.0f);
        CHECK(llcolor3.mV[1] == 0.0f);
        CHECK(llcolor3.mV[2] == 0.0f);
        F32 r = 2.0f, g = 3.2f, b = 1.f;
        F32 v1,v2,v3;
        LLColor3 llcolor3a(r,g,b);
        CHECK(llcolor3a.mV[0] == 2.0f);
        CHECK(llcolor3a.mV[1] == 3.2f);
        CHECK(llcolor3a.mV[2] == 1.f);

        const F32 vec[3] = {2.0f, 3.2f,1.f};
        LLColor3 llcolor3b(vec);
        CHECK(llcolor3b.mV[0] == 2.0f);
        CHECK(llcolor3b.mV[1] == 3.2f);
        CHECK(llcolor3b.mV[2] == 1.f);
        const char* str = "561122";
        LLColor3 llcolor3c(str);
        v1 = (F32)86.0f/255.0f; // 0x56 = 86
        v2 = (F32)17.0f/255.0f; // 0x11 = 17
        v3 = (F32)34.0f/255.f;  // 0x22 = 34
        CHECK(is_approx_equal(v1, llcolor3c.mV[0]));
        CHECK(is_approx_equal(v2, llcolor3c.mV[1]));
        CHECK(is_approx_equal(v3, llcolor3c.mV[2]));
    }

    TEST_CASE("v3color_h: setToBlack and setToWhite", "[v3color_h]")
    {
        LLColor3 llcolor3;
        llcolor3.setToBlack();
        CHECK(llcolor3.mV[0] == 0.f);
        CHECK(llcolor3.mV[1] == 0.f);
        CHECK(llcolor3.mV[2] == 0.f);
        llcolor3.setToWhite();
        CHECK(llcolor3.mV[0] == 1.f);
        CHECK(llcolor3.mV[1] == 1.f);
        CHECK(llcolor3.mV[2] == 1.f);
    }

    TEST_CASE("v3color_h: setVec from components or another color or an array", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = 1231.f, b = 4.7849321232f;
        LLColor3 llcolor3, llcolor3a;
        llcolor3.setVec(r,g,b);
        CHECK(r == llcolor3.mV[0]);
        CHECK(g == llcolor3.mV[1]);
        CHECK(b == llcolor3.mV[2]);
        llcolor3a.setVec(llcolor3);
        CHECK(llcolor3 == llcolor3a);
        F32 vec[3] = {1.2324f, 2.45634f, .234563f};
        llcolor3.setToBlack();
        llcolor3.setVec(vec);
        CHECK(vec[0] == llcolor3.mV[0]);
        CHECK(vec[1] == llcolor3.mV[1]);
        CHECK(vec[2] == llcolor3.mV[2]);
    }

    TEST_CASE("v3color_h: magVecSquared and magVec", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = 1231.f, b = 4.7849321232f;
        LLColor3 llcolor3(r,g,b);
        CHECK(is_approx_equal(llcolor3.magVecSquared(), (r*r + g*g + b*b)));
        CHECK(is_approx_equal(llcolor3.magVec(), (F32) sqrt(r*r + g*g + b*b)));
    }

    TEST_CASE("v3color_h: normVec normalizes and leaves black at zero", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = 1231.f, b = 4.7849321232f;
        F32 val1, val2,val3;
        LLColor3 llcolor3(r,g,b);
        F32 vecMag = llcolor3.normVec();
        F32 mag = (F32) sqrt(r*r + g*g + b*b);
        F32 oomag = 1.f / mag;
        val1 = r * oomag;
        val2 = g * oomag;
        val3 = b * oomag;
        CHECK(is_approx_equal(val1, llcolor3.mV[0]));
        CHECK(is_approx_equal(val2, llcolor3.mV[1]));
        CHECK(is_approx_equal(val3, llcolor3.mV[2]));
        CHECK(is_approx_equal(vecMag, mag));
        r = .000000000f, g = 0.f, b = 0.0f;
        llcolor3.setVec(r,g,b);
        vecMag = llcolor3.normVec();
        CHECK(llcolor3.mV[0] == 0.);
        CHECK(llcolor3.mV[1] == 0.);
        CHECK(llcolor3.mV[2] == 0.);
        CHECK(vecMag == 0.);
    }

    TEST_CASE("v3color_h: operator<< prints equal colors the same", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = -1231.f, b = .7849321232f;
        std::ostringstream stream1, stream2;
        LLColor3 llcolor3(r,g,b),llcolor3a;
        stream1 << llcolor3;
        llcolor3a.setVec(r,g,b);
        stream2 << llcolor3a;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v3color_h: assignment gives an equal color", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = -1231.f, b = .7849321232f;
        LLColor3 llcolor3(r,g,b),llcolor3a;
        llcolor3a = llcolor3;
        CHECK(llcolor3a == llcolor3);
    }

    TEST_CASE("v3color_h: operator+ adds component-wise", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2),llcolor3b;
        llcolor3b = llcolor3 + llcolor3a ;
        CHECK(is_approx_equal(r1+r2 ,llcolor3b.mV[0]));
        CHECK(is_approx_equal(g1+g2,llcolor3b.mV[1]));
        CHECK(is_approx_equal(b1+b2,llcolor3b.mV[2]));
        r1 = -.235f, g1 = -24.32f, b1 = 2.13f,  r2 = -2.3f, g2 = 1.f, b2 = 34.21f;
        llcolor3.setVec(r1,g1,b1);
        llcolor3a.setVec(r2,g2,b2);
        llcolor3b = llcolor3 + llcolor3a;
        CHECK(is_approx_equal(r1+r2 ,llcolor3b.mV[0]));
        CHECK(is_approx_equal(g1+g2,llcolor3b.mV[1]));
        CHECK(is_approx_equal(b1+b2,llcolor3b.mV[2]));
    }

    TEST_CASE("v3color_h: operator- subtracts component-wise", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2),llcolor3b;
        llcolor3b = llcolor3 - llcolor3a ;
        CHECK(is_approx_equal(r1-r2 ,llcolor3b.mV[0]));
        CHECK(is_approx_equal(g1-g2,llcolor3b.mV[1]));
        CHECK(is_approx_equal(b1-b2,llcolor3b.mV[2]));
        r1 = -.235f, g1 = -24.32f, b1 = 2.13f,  r2 = -2.3f, g2 = 1.f, b2 = 34.21f;
        llcolor3.setVec(r1,g1,b1);
        llcolor3a.setVec(r2,g2,b2);
        llcolor3b = llcolor3 - llcolor3a;
        CHECK(is_approx_equal(r1-r2 ,llcolor3b.mV[0]));
        CHECK(is_approx_equal(g1-g2,llcolor3b.mV[1]));
        CHECK(is_approx_equal(b1-b2,llcolor3b.mV[2]));
    }

    TEST_CASE("v3color_h: operator* by a color and by a scalar on either side", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2),llcolor3b;
        llcolor3b = llcolor3 * llcolor3a;
        CHECK(is_approx_equal(r1*r2 ,llcolor3b.mV[0]));
        CHECK(is_approx_equal(g1*g2,llcolor3b.mV[1]));
        CHECK(is_approx_equal(b1*b2,llcolor3b.mV[2]));
        llcolor3a.setToBlack();
        F32 mulVal = 4.332f;
        llcolor3a = llcolor3 * mulVal;
        CHECK(is_approx_equal(r1*mulVal ,llcolor3a.mV[0]));
        CHECK(is_approx_equal(g1*mulVal,llcolor3a.mV[1]));
        CHECK(is_approx_equal(b1*mulVal,llcolor3a.mV[2]));
        llcolor3a.setToBlack();
        llcolor3a = mulVal * llcolor3;
        CHECK(is_approx_equal(r1*mulVal ,llcolor3a.mV[0]));
        CHECK(is_approx_equal(g1*mulVal,llcolor3a.mV[1]));
        CHECK(is_approx_equal(b1*mulVal,llcolor3a.mV[2]));
    }

    TEST_CASE("v3color_h: unary minus negates", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = 1231.f, b = 4.7849321232f;
        LLColor3 llcolor3(r,g,b),llcolor3a;
        llcolor3a = -llcolor3;
        CHECK(-llcolor3a == llcolor3);
    }

    TEST_CASE("v3color_h: operator== on equal colors", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = 1231.f, b = 4.7849321232f;
        LLColor3 llcolor3(r,g,b),llcolor3a(r,g,b);
        CHECK(llcolor3a == llcolor3);
        r = 13.3436212f, g = -11.f, b = .7849321232f;
        llcolor3.setVec(r,g,b);
        llcolor3a.setVec(r,g,b);
        CHECK(llcolor3a == llcolor3);
    }

    TEST_CASE("v3color_h: operator!= on different and equal colors", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        CHECK(llcolor3 != llcolor3a);
        llcolor3.setToBlack();
        llcolor3a.setVec(llcolor3);
        CHECK_FALSE(llcolor3a != llcolor3);
    }

    TEST_CASE("v3color_h: operator+= adds component-wise", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        llcolor3a += llcolor3;
        CHECK(is_approx_equal(r1+r2 ,llcolor3a.mV[0]));
        CHECK(is_approx_equal(g1+g2,llcolor3a.mV[1]));
        CHECK(is_approx_equal(b1+b2,llcolor3a.mV[2]));

        // Again on other values, and it hands back the color it changed.
        r1 = -.235f, g1 = -24.32f, b1 = 2.13f, r2 = .5f, g2 = 1.f, b2 = -34.21f;
        llcolor3.setVec(r1,g1,b1);
        llcolor3a.setVec(r2,g2,b2);
        const LLColor3& sum = (llcolor3a += llcolor3);
        CHECK(&sum == &llcolor3a);
        CHECK(is_approx_equal(r1+r2 ,llcolor3a.mV[0]));
        CHECK(is_approx_equal(g1+g2,llcolor3a.mV[1]));
        CHECK(is_approx_equal(b1+b2,llcolor3a.mV[2]));
    }

    TEST_CASE("v3color_h: operator-= subtracts component-wise", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        llcolor3a -= llcolor3;
        CHECK(is_approx_equal(r2-r1, llcolor3a.mV[0]));
        CHECK(is_approx_equal(g2-g1, llcolor3a.mV[1]));
        CHECK(is_approx_equal(b2-b1, llcolor3a.mV[2]));

        // Again on other values, and it hands back the color it changed.
        r1 = -.235f, g1 = -24.32f, b1 = 2.13f, r2 = .5f, g2 = 1.f, b2 = -34.21f;
        llcolor3.setVec(r1,g1,b1);
        llcolor3a.setVec(r2,g2,b2);
        const LLColor3& difference = (llcolor3a -= llcolor3);
        CHECK(&difference == &llcolor3a);
        CHECK(is_approx_equal(r2-r1, llcolor3a.mV[0]));
        CHECK(is_approx_equal(g2-g1, llcolor3a.mV[1]));
        CHECK(is_approx_equal(b2-b1, llcolor3a.mV[2]));
    }

    TEST_CASE("v3color_h: operator*= by a color and by a scalar", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        llcolor3a *= llcolor3;
        CHECK(is_approx_equal(r1*r2 ,llcolor3a.mV[0]));
        CHECK(is_approx_equal(g1*g2,llcolor3a.mV[1]));
        CHECK(is_approx_equal(b1*b2,llcolor3a.mV[2]));
        F32 mulVal = 4.332f;
        llcolor3 *=mulVal;
        CHECK(is_approx_equal(r1*mulVal ,llcolor3.mV[0]));
        CHECK(is_approx_equal(g1*mulVal,llcolor3.mV[1]));
        CHECK(is_approx_equal(b1*mulVal,llcolor3.mV[2]));
    }

    TEST_CASE("v3color_h: clamp limits components to the 0 to 1 range", "[v3color_h]")
    {
        F32 r = 2.3436212f, g = -1231.f, b = .7849321232f;
        LLColor3 llcolor3(r,g,b);
        llcolor3.clamp();
        CHECK(llcolor3.mV[0] == 1.0f);
        CHECK(llcolor3.mV[1] == 0.f);
        CHECK(b == llcolor3.mV[2]);
        r = -2.3436212f, g = -1231.f, b = 67.7849321232f;
        llcolor3.setVec(r,g,b);
        llcolor3.clamp();
        CHECK(llcolor3.mV[0] == 0.f);
        CHECK(llcolor3.mV[1] == 0.f);
        CHECK(llcolor3.mV[2] == 1.f);
    }

    TEST_CASE("v3color_h: lerp interpolates every component", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        F32 val = 2.3f,val1,val2,val3;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        val1 = r1 + (r2 - r1)* val;
        val2 = g1 + (g2 - g1)* val;
        val3 = b1 + (b2 - b1)* val;
        LLColor3 llcolor3b = lerp(llcolor3,llcolor3a,val);
        // Within the rounding of the larger of the two terms each sums, which
        // the library's lerp and this working of it may round differently.
        auto terms = [&](F32 a, F32 b) { return llmax(fabsf(a), fabsf((b - a) * val)); };
        CHECK_THAT(llcolor3b.mV[0], ll_test::WithinRounding(val1, terms(r1, r2)));
        CHECK_THAT(llcolor3b.mV[1], ll_test::WithinRounding(val2, terms(g1, g2)));
        CHECK_THAT(llcolor3b.mV[2], ll_test::WithinRounding(val3, terms(b1, b2)));
    }

    TEST_CASE("v3color_h: distVec and distVec_squared", "[v3color_h]")
    {
        F32 r1 =1.f, g1 = 2.f,b1 = 1.2f, r2 = -2.3f, g2 = 1.11f, b2 = 1234.234f;
        LLColor3 llcolor3(r1,g1,b1),llcolor3a(r2,g2,b2);
        F32 val = distVec(llcolor3,llcolor3a);
        CHECK(is_approx_equal((F32) sqrt((r1-r2)*(r1-r2) + (g1-g2)*(g1-g2) + (b1-b2)*(b1-b2)) ,val));

        F32 val1 = distVec_squared(llcolor3,llcolor3a);
        CHECK(is_approx_equal(((r1-r2)*(r1-r2) + (g1-g2)*(g1-g2) + (b1-b2)*(b1-b2)) ,val1));
    }

    TEST_CASE("v3color_h: getValue round-trips through setValue and the LLSD constructor", "[v3color_h]")
    {
        F32 r1 = 1.02223f, g1 = 22222.212f, b1 = 122222.00002f;
        LLColor3 llcolor31(r1,g1,b1);

        LLSD sd = llcolor31.getValue();
        LLColor3 llcolor32;
        llcolor32.setValue(sd);
        CHECK(llcolor31 == llcolor32);

        LLColor3 llcolor33(sd);
        CHECK(llcolor31 == llcolor33);
    }
}
