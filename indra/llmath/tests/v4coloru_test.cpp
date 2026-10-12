/**
 * @file v4coloru_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief v4coloru test cases.
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

#include "llsd.h"

#include "../v4coloru.h"


namespace
{
    TEST_CASE("v4coloru_h: constructors set components and default alpha to 255", "[v4coloru_h]")
    {
        LLColor4U llcolor4u;
        CHECK(llcolor4u.mV[VRED] == 0);
        CHECK(llcolor4u.mV[VGREEN] == 0);
        CHECK(llcolor4u.mV[VBLUE] == 0);
        CHECK(llcolor4u.mV[VALPHA] == 255);

        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 0x23;
        LLColor4U llcolor4u1(r,g,b);
        CHECK(r == llcolor4u1.mV[VRED]);
        CHECK(g == llcolor4u1.mV[VGREEN]);
        CHECK(b == llcolor4u1.mV[VBLUE]);
        CHECK(llcolor4u1.mV[VALPHA] == 255);

        LLColor4U llcolor4u2(r,g,b,a);
        CHECK(r == llcolor4u2.mV[VRED]);
        CHECK(g == llcolor4u2.mV[VGREEN]);
        CHECK(b == llcolor4u2.mV[VBLUE]);
        CHECK(a == llcolor4u2.mV[VALPHA]);

        const U8 vec[4] = {0x12,0xFF,0xAF,0x23};
        LLColor4U llcolor4u3(vec);
        CHECK(vec[0] == llcolor4u3.mV[VRED]);
        CHECK(vec[1] == llcolor4u3.mV[VGREEN]);
        CHECK(vec[2] == llcolor4u3.mV[VBLUE]);
        CHECK(vec[3] == llcolor4u3.mV[VALPHA]);

        LLSD sd = llcolor4u3.getValue();
        LLColor4U llcolor4u4(sd);
        CHECK(llcolor4u4 == llcolor4u3);
    }

    TEST_CASE("v4coloru_h: setValue reads back what getValue wrote", "[v4coloru_h]")
    {
        LLColor4U llcolor4ua(1, 2, 3, 4);
        LLSD sd = llcolor4ua.getValue();
        LLColor4U llcolor4u;
        llcolor4u.setValue(sd);
        CHECK(llcolor4u == llcolor4ua);
    }

    TEST_CASE("v4coloru_h: setToBlack and setToWhite leave alpha at 255", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 0x23;
        LLColor4U llcolor4u(r,g,b,a);
        llcolor4u.setToBlack();
        CHECK(llcolor4u.mV[VRED] == 0);
        CHECK(llcolor4u.mV[VGREEN] == 0);
        CHECK(llcolor4u.mV[VBLUE] == 0);
        CHECK(llcolor4u.mV[VALPHA] == 255);

        llcolor4u.setToWhite();
        CHECK(llcolor4u.mV[VRED] == 255);
        CHECK(llcolor4u.mV[VGREEN] == 255);
        CHECK(llcolor4u.mV[VBLUE] == 255);
        CHECK(llcolor4u.mV[VALPHA] == 255);
    }

    TEST_CASE("v4coloru_h: cast from LLSD reads back what getValue wrote", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 0x23;
        LLColor4U llcolor4ua(r,g,b,a);
        LLSD sd = llcolor4ua.getValue();
        LLColor4U llcolor4u = (LLColor4U)sd;
        CHECK(llcolor4u == llcolor4ua);
    }

    TEST_CASE("v4coloru_h: setVec from values or another color or an array", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 0x23;
        LLColor4U llcolor4u;
        llcolor4u.setVec(r,g,b,a);
        CHECK(r == llcolor4u.mV[VRED]);
        CHECK(g == llcolor4u.mV[VGREEN]);
        CHECK(b == llcolor4u.mV[VBLUE]);
        CHECK(a == llcolor4u.mV[VALPHA]);

        llcolor4u.setToBlack();
        llcolor4u.setVec(r,g,b);
        CHECK(r == llcolor4u.mV[VRED]);
        CHECK(g == llcolor4u.mV[VGREEN]);
        CHECK(b == llcolor4u.mV[VBLUE]);
        CHECK(llcolor4u.mV[VALPHA] == 255);

        LLColor4U llcolor4u1;
        llcolor4u1.setVec(llcolor4u);
        CHECK(llcolor4u1 == llcolor4u);

        const U8 vec[4] = {0x12,0xFF,0xAF,0x23};
        LLColor4U llcolor4u2;
        llcolor4u2.setVec(vec);
        CHECK(vec[0] == llcolor4u2.mV[VRED]);
        CHECK(vec[1] == llcolor4u2.mV[VGREEN]);
        CHECK(vec[2] == llcolor4u2.mV[VBLUE]);
        CHECK(vec[3] == llcolor4u2.mV[VALPHA]);
    }

    TEST_CASE("v4coloru_h: setAlpha sets the alpha", "[v4coloru_h]")
    {
        U8 alpha = 0x12;
        LLColor4U llcolor4u;
        llcolor4u.setAlpha(alpha);
        CHECK(alpha == llcolor4u.mV[VALPHA]);
    }

    TEST_CASE("v4coloru_h: magVecSquared and magVec", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF;
        LLColor4U llcolor4u(r,g,b);
        CHECK(is_approx_equal(llcolor4u.magVecSquared(), (F32)(r*r + g*g + b*b)));
        CHECK(is_approx_equal(llcolor4u.magVec(), (F32) sqrt((F32) (r*r + g*g + b*b))));
    }

    TEST_CASE("v4coloru_h: operator<< prints equal colors the same", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF;
        std::ostringstream stream1, stream2;
        LLColor4U llcolor4u1(r,g,b),llcolor4u2;
        stream1 << llcolor4u1;
        llcolor4u2.setVec(r,g,b);
        stream2 << llcolor4u2;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v4coloru_h: operator+ and operator+= wrap modulo 256", "[v4coloru_h]")
    {
        U8 r1 = 0x12, g1 = 0xFF, b1 = 0xAF;
        U8 r2 = 0x1C, g2 = 0x9A, b2 = 0x1B;
        LLColor4U llcolor4u1(r1,g1,b1), llcolor4u2(r2,g2,b2),llcolor4u3;
        llcolor4u3 = llcolor4u1 + llcolor4u2;
        CHECK(llcolor4u3.mV[VRED] == (U8)(r1+r2));
        CHECK(llcolor4u3.mV[VGREEN] == (U8)(g1+g2));
        CHECK(llcolor4u3.mV[VBLUE] == (U8)(b1+b2));

        llcolor4u2 += llcolor4u1;
        CHECK(llcolor4u2.mV[VRED] == (U8)(r1+r2));
        CHECK(llcolor4u2.mV[VGREEN] == (U8)(g1+g2));
        CHECK(llcolor4u2.mV[VBLUE] == (U8)(b1+b2));
    }

    TEST_CASE("v4coloru_h: operator- and operator-= wrap modulo 256", "[v4coloru_h]")
    {
        U8 r1 = 0x12, g1 = 0xFF, b1 = 0xAF;
        U8 r2 = 0x1C, g2 = 0x9A, b2 = 0x1B;
        LLColor4U llcolor4u1(r1,g1,b1), llcolor4u2(r2,g2,b2),llcolor4u3;
        llcolor4u3 = llcolor4u1 - llcolor4u2;
        CHECK(llcolor4u3.mV[VRED] == (U8)(r1-r2));
        CHECK(llcolor4u3.mV[VGREEN] == (U8)(g1-g2));
        CHECK(llcolor4u3.mV[VBLUE] == (U8)(b1-b2));

        llcolor4u1 -= llcolor4u2;
        CHECK(llcolor4u1.mV[VRED] == (U8)(r1-r2));
        CHECK(llcolor4u1.mV[VGREEN] == (U8)(g1-g2));
        CHECK(llcolor4u1.mV[VBLUE] == (U8)(b1-b2));
    }

    TEST_CASE("v4coloru_h: operator* by a color and operator*= by a scalar wrap modulo 256", "[v4coloru_h]")
    {
        U8 r1 = 0x12, g1 = 0xFF, b1 = 0xAF;
        U8 r2 = 0x1C, g2 = 0x9A, b2 = 0x1B;
        LLColor4U llcolor4u1(r1,g1,b1), llcolor4u2(r2,g2,b2),llcolor4u3;
        llcolor4u3 = llcolor4u1 * llcolor4u2;
        CHECK(llcolor4u3.mV[VRED] == (U8)(r1*r2));
        CHECK(llcolor4u3.mV[VGREEN] == (U8)(g1*g2));
        CHECK(llcolor4u3.mV[VBLUE] == (U8)(b1*b2));

        U8 mulVal = 123;
        llcolor4u1 *= mulVal;
        CHECK(llcolor4u1.mV[VRED] == (U8)(r1*mulVal));
        CHECK(llcolor4u1.mV[VGREEN] == (U8)(g1*mulVal));
        CHECK(llcolor4u1.mV[VBLUE] == (U8)(b1*mulVal));
    }

    TEST_CASE("v4coloru_h: operator== and operator!=", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF;
        LLColor4U llcolor4u(r,g,b),llcolor4u1;
        llcolor4u1 = llcolor4u;
        CHECK(llcolor4u1 == llcolor4u);
        llcolor4u1.setToBlack();
        CHECK(llcolor4u1 != llcolor4u);
    }

    TEST_CASE("v4coloru_h: operator%= multiplies the alpha", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 12;
        LLColor4U llcolor4u(r,g,b,a);
        U8 modVal = 45;
        llcolor4u %= modVal;
        CHECK(llcolor4u.mV[VALPHA] == (U8)(a * modVal));
    }

    TEST_CASE("v4coloru_h: parseColor4U reads four values and rejects fewer", "[v4coloru_h]")
    {
        U8 r = 0x12, g = 0xFF, b = 0xAF, a = 12;
        LLColor4U llcolor4u1(r,g,b,a);
        std::string color("12, 23, 132, 50");
        LLColor4U::parseColor4U(color, &llcolor4u1);
        CHECK(llcolor4u1.mV[VRED] == 12);
        CHECK(llcolor4u1.mV[VGREEN] == 23);
        CHECK(llcolor4u1.mV[VBLUE] == 132);
        CHECK(llcolor4u1.mV[VALPHA] == 50);

        color = "12, 23, 132";
        CHECK_FALSE(LLColor4U::parseColor4U(color, &llcolor4u1));

        color = "12";
        CHECK_FALSE(LLColor4U::parseColor4U(color, &llcolor4u1));
    }

    TEST_CASE("v4coloru_h: multAll rounds and clamps each channel", "[v4coloru_h]")
    {
        U8 r = 12, g = 123, b = 3, a = 2;
        LLColor4U llcolor4u(r,g,b,a),llcolor4u1;
        const F32 fVal = 3.f;
        llcolor4u1 = llcolor4u.multAll(fVal);
        // Each channel multiplied by fVal then rounded and clamped to [0,255].
        // Previously the test used (U8)ll_round(...) directly, which wraps
        // modulo 256 for products > 255 (e.g. 123 * 3 = 369 -> 113), locking
        // in the wrap bug. multAll now clamps, so green saturates at 255.
        CHECK((U8)llclampb(ll_round(r * fVal)) == llcolor4u1.mV[VRED]);
        CHECK((U8)llclampb(ll_round(g * fVal)) == llcolor4u1.mV[VGREEN]);
        CHECK((U8)llclampb(ll_round(b * fVal)) == llcolor4u1.mV[VBLUE]);
        CHECK((U8)llclampb(ll_round(a * fVal)) == llcolor4u1.mV[VALPHA]);
    }

    TEST_CASE("v4coloru_h: addClampMax adds and clamps each channel at 255", "[v4coloru_h]")
    {
        U8 r1 = 12, g1 = 123, b1 = 3, a1 = 2;
        U8 r2 = 23, g2 = 230, b2 = 124, a2 = 255;
        LLColor4U llcolor4u(r1,g1,b1,a1),llcolor4u1(r2,g2,b2,a2);
        llcolor4u1 = llcolor4u1.addClampMax(llcolor4u);
        CHECK(r1+r2 == llcolor4u1.mV[VRED]);
        CHECK(llcolor4u1.mV[VGREEN] == 255);
        CHECK(b1+b2 == llcolor4u1.mV[VBLUE]);
        CHECK(llcolor4u1.mV[VALPHA] == 255);

        r1 = 132, g1 = 3, b1 = 3, a1 = 2;
        r2 = 123, g2 = 230, b2 = 154, a2 = 25;
        LLColor4U llcolor4u2(r1,g1,b1,a1),llcolor4u3(r2,g2,b2,a2);
        llcolor4u3 = llcolor4u3.addClampMax(llcolor4u2);
        CHECK(llcolor4u3.mV[VRED] == 255);
        CHECK(g1+g2 == llcolor4u3.mV[VGREEN]);
        CHECK(b1+b2 == llcolor4u3.mV[VBLUE]);
        CHECK(a1+a2 == llcolor4u3.mV[VALPHA]);
    }

    TEST_CASE("v4coloru_h: setVecScaleClamp scales the brightest channel to 255 and clamps at 0", "[v4coloru_h]")
    {
        F32 r = 23.f, g = 12.32f, b = -12.3f;
        LLColor3 color3(r,g,b);
        LLColor4U llcolor4u;
        llcolor4u.setVecScaleClamp(color3);
        const S32 MAX_COLOR = 255;
        F32 color_scale_factor = MAX_COLOR/r;
        S32 r2 = ll_round(r * color_scale_factor);
        S32 g2 = ll_round(g * color_scale_factor);
        CHECK(r2 == llcolor4u.mV[VRED]);
        CHECK(g2 == llcolor4u.mV[VGREEN]);
        CHECK(llcolor4u.mV[VBLUE] == 0);
        CHECK(llcolor4u.mV[VALPHA] == 255);
    }
}
