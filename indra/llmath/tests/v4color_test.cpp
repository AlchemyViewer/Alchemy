/**
 * @file v4color_test.cpp
 * @author Adroit
 * @date 2007-03
 * @brief v4color test cases.
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

#include "../v4coloru.h"
#include "llsd.h"
#include "../v3color.h"
#include "../v4color.h"


namespace
{
    TEST_CASE("v4color_h: constructors from values or an array or LLColor3 or LLSD or LLColor4U", "[v4color_h]")
    {
        LLColor4 llcolor4;
        CHECK(llcolor4.mV[VRED] == 0);
        CHECK(llcolor4.mV[VGREEN] == 0);
        CHECK(llcolor4.mV[VBLUE] == 0);
        CHECK(llcolor4.mV[VALPHA] == 1.0f);

        F32 r = 0x20, g = 0xFFFF, b = 0xFF, a = 0xAF;
        LLColor4 llcolor4a(r,g,b);
        CHECK(r == llcolor4a.mV[VRED]);
        CHECK(g == llcolor4a.mV[VGREEN]);
        CHECK(b == llcolor4a.mV[VBLUE]);
        CHECK(llcolor4a.mV[VALPHA] == 1.0f);

        LLColor4 llcolor4b(r,g,b,a);
        CHECK(r == llcolor4b.mV[VRED]);
        CHECK(g == llcolor4b.mV[VGREEN]);
        CHECK(b == llcolor4b.mV[VBLUE]);
        CHECK(a == llcolor4b.mV[VALPHA]);

        const F32 vec[4] = {.112f ,23.2f, -4.2f, -.0001f};
        LLColor4 llcolor4c(vec);
        CHECK(vec[0] == llcolor4c.mV[VRED]);
        CHECK(vec[1] == llcolor4c.mV[VGREEN]);
        CHECK(vec[2] == llcolor4c.mV[VBLUE]);
        CHECK(vec[3] == llcolor4c.mV[VALPHA]);

        LLColor3 llcolor3(-2.23f,1.01f,42.3f);
        F32 val = -.1f;
        LLColor4 llcolor4d(llcolor3,val);
        CHECK(llcolor3.mV[VRED] == llcolor4d.mV[VRED]);
        CHECK(llcolor3.mV[VGREEN] == llcolor4d.mV[VGREEN]);
        CHECK(llcolor3.mV[VBLUE] == llcolor4d.mV[VBLUE]);
        CHECK(val == llcolor4d.mV[VALPHA]);

        LLSD sd = llcolor4d.getValue();
        LLColor4 llcolor4e(sd);
        CHECK(llcolor4d == llcolor4e);

        U8 r1 = 0xF2, g1 = 0xFA, b1 = 0xBF;
        LLColor4U color4u(r1,g1,b1);
        LLColor4 llcolor4g(color4u);
        const F32 SCALE = 1.f/255.f;
        F32 r2 = r1*SCALE, g2 = g1* SCALE, b2 = b1* SCALE;
        CHECK(r2 == llcolor4g.mV[VRED]);
        CHECK(g2 == llcolor4g.mV[VGREEN]);
        CHECK(b2 == llcolor4g.mV[VBLUE]);
    }

    TEST_CASE("v4color_h: setValue and getValue round-trip through LLSD", "[v4color_h]")
    {
        LLColor4 llcolor(1.0, 2.0, 3.0, 4.0);
        LLSD llsd = llcolor.getValue();
        LLColor4 llcolor4(llsd), llcolor4a;
        llcolor4a.setValue(llsd);
        CHECK(llcolor4 == llcolor4a);
        LLSD sd = llcolor4a.getValue();
        LLColor4 llcolor4b(sd);
        CHECK(llcolor4b == llcolor4a);
    }

    TEST_CASE("v4color_h: setToBlack and setToWhite leave alpha at 1", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF,a = 0xAF;
        LLColor4 llcolor4(r,g,b,a);
        llcolor4.setToBlack();
        CHECK(llcolor4.mV[VRED] == 0);
        CHECK(llcolor4.mV[VGREEN] == 0);
        CHECK(llcolor4.mV[VBLUE] == 0);
        CHECK(llcolor4.mV[VALPHA] == 1.0f);

        llcolor4.setToWhite();
        CHECK(llcolor4.mV[VRED] == 1.f);
        CHECK(llcolor4.mV[VGREEN] == 1.f);
        CHECK(llcolor4.mV[VBLUE] == 1.f);
        CHECK(llcolor4.mV[VALPHA] == 1.0f);
    }

    TEST_CASE("v4color_h: setVec from values or colors or an array or LLColor4U", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF, a = 0xAF;
        LLColor4 llcolor4;
        llcolor4.setVec(r,g,b);
        CHECK(r == llcolor4.mV[VRED]);
        CHECK(g == llcolor4.mV[VGREEN]);
        CHECK(b == llcolor4.mV[VBLUE]);
        CHECK(llcolor4.mV[VALPHA] == 1.f);

        llcolor4.setVec(r,g,b,a);
        CHECK(r == llcolor4.mV[VRED]);
        CHECK(g == llcolor4.mV[VGREEN]);
        CHECK(b == llcolor4.mV[VBLUE]);
        CHECK(a == llcolor4.mV[VALPHA]);

        LLColor4 llcolor4a;
        llcolor4a.setVec(llcolor4);
        CHECK(llcolor4a == llcolor4);

        LLColor3 llcolor3(-2.23f,1.01f,42.3f);
        llcolor4a.setVec(llcolor3);
        CHECK(llcolor3.mV[VRED] == llcolor4a.mV[VRED]);
        CHECK(llcolor3.mV[VGREEN] == llcolor4a.mV[VGREEN]);
        CHECK(llcolor3.mV[VBLUE] == llcolor4a.mV[VBLUE]);

        F32 val = -.33f;
        llcolor4a.setVec(llcolor3,val);
        CHECK(llcolor3.mV[VRED] == llcolor4a.mV[VRED]);
        CHECK(llcolor3.mV[VGREEN] == llcolor4a.mV[VGREEN]);
        CHECK(llcolor3.mV[VBLUE] == llcolor4a.mV[VBLUE]);
        CHECK(val == llcolor4a.mV[VALPHA]);

        const F32 vec[4] = {.112f ,23.2f, -4.2f, -.0001f};
        LLColor4 llcolor4c;
        llcolor4c.setVec(vec);
        CHECK(vec[0] == llcolor4c.mV[VRED]);
        CHECK(vec[1] == llcolor4c.mV[VGREEN]);
        CHECK(vec[2] == llcolor4c.mV[VBLUE]);
        CHECK(vec[3] == llcolor4c.mV[VALPHA]);

        U8 r1 = 0xF2, g1 = 0xFA, b1= 0xBF;
        LLColor4U color4u(r1,g1,b1);
        llcolor4.setVec(color4u);
        const F32 SCALE = 1.f/255.f;
        F32 r2 = r1*SCALE, g2 = g1* SCALE, b2 = b1* SCALE;
        CHECK(r2 == llcolor4.mV[VRED]);
        CHECK(g2 == llcolor4.mV[VGREEN]);
        CHECK(b2 == llcolor4.mV[VBLUE]);
    }

    TEST_CASE("v4color_h: setAlpha sets the alpha", "[v4color_h]")
    {
        F32 alpha = 0xAF;
        LLColor4 llcolor4;
        llcolor4.setAlpha(alpha);
        CHECK(alpha == llcolor4.mV[VALPHA]);
    }

    TEST_CASE("v4color_h: magVecSquared and magVec", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        LLColor4 llcolor4(r,g,b);
        CHECK(is_approx_equal(llcolor4.magVecSquared(), (r*r + g*g + b*b)));
        CHECK(is_approx_equal(llcolor4.magVec(), (F32) sqrt(r*r + g*g + b*b)));
    }

    TEST_CASE("v4color_h: normVec normalizes the color and returns its magnitude", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        LLColor4 llcolor4(r,g,b);
        F32 vecMag = llcolor4.normVec();
        F32 mag = (F32) sqrt(r*r + g*g + b*b);
        F32 oomag = 1.f / mag;
        F32 val1 = r * oomag, val2 = g * oomag, val3 = b * oomag;
        CHECK(is_approx_equal(val1, llcolor4.mV[0]));
        CHECK(is_approx_equal(val2, llcolor4.mV[1]));
        CHECK(is_approx_equal(val3, llcolor4.mV[2]));
        CHECK(is_approx_equal(vecMag, mag));
    }

    TEST_CASE("v4color_h: isOpaque only when alpha is exactly 1", "[v4color_h]")
    {
        LLColor4 llcolor4;
        CHECK(llcolor4.isOpaque() == 1);
        F32 r = 0x20, g = 0xFFFF, b = 0xFF,a = 1.f;
        llcolor4.setVec(r,g,b,a);
        CHECK(llcolor4.isOpaque() == 1);
        a = 2.f;
        llcolor4.setVec(r,g,b,a);
        CHECK(llcolor4.isOpaque() == 0);
    }

    TEST_CASE("v4color_h: subscript operator reads components and gives references", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        LLColor4 llcolor4(r,g,b);
        CHECK(r ==  llcolor4[0]);
        CHECK(g ==  llcolor4[1]);
        CHECK(b ==  llcolor4[2]);

        // The non-const subscript is a reference to the component itself.
        F32 &ref1 = llcolor4[0];
        CHECK(&ref1 == &llcolor4.mV[VRED]);
        ref1 = 0xA20;
        llcolor4[1] = 0xFBFF;
        llcolor4[2] = 0xFFF;
        CHECK(llcolor4.mV[VRED] == 0xA20);
        CHECK(llcolor4.mV[VGREEN] == 0xFBFF);
        CHECK(llcolor4.mV[VBLUE] == 0xFFF);
    }

    TEST_CASE("v4color_h: assignment from LLColor3 and construction from LLSD", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        LLColor3 llcolor3(r,g,b);
        LLColor4 llcolor4a,llcolor4b;
        llcolor4a = llcolor3;
        CHECK(llcolor3.mV[0] == llcolor4a.mV[VRED]);
        CHECK(llcolor3.mV[1] == llcolor4a.mV[VGREEN]);
        CHECK(llcolor3.mV[2] == llcolor4a.mV[VBLUE]);
        LLSD sd = llcolor4a.getValue();
        llcolor4b = LLColor4(sd);
        CHECK(llcolor4a == llcolor4b);
    }

    TEST_CASE("v4color_h: operator<< prints equal colors the same", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        std::ostringstream stream1, stream2;
        LLColor4 llcolor4a(r,g,b),llcolor4b;
        stream1 << llcolor4a;
        llcolor4b.setVec(r,g,b);
        stream2 << llcolor4b;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("v4color_h: operator+ and operator+=", "[v4color_h]")
    {
        F32 r1 = 0x20, g1 = 0xFFFF, b1 = 0xFF;
        F32 r2 = 0xABF, g2 = 0xFB, b2 = 0xFFF;
        LLColor4 llcolor4a(r1,g1,b1),llcolor4b(r2,g2,b2),llcolor4c;
        llcolor4c = llcolor4b + llcolor4a;
        CHECK(is_approx_equal(r1+r2,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1+g2,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1+b2,llcolor4c.mV[VBLUE]));

        llcolor4b += llcolor4a;
        CHECK(is_approx_equal(r1+r2,llcolor4b.mV[VRED]));
        CHECK(is_approx_equal(g1+g2,llcolor4b.mV[VGREEN]));
        CHECK(is_approx_equal(b1+b2,llcolor4b.mV[VBLUE]));
    }

    TEST_CASE("v4color_h: operator- and operator-=", "[v4color_h]")
    {
        F32 r1 = 0x20, g1 = 0xFFFF, b1 = 0xFF;
        F32 r2 = 0xABF, g2 = 0xFB, b2 = 0xFFF;
        LLColor4 llcolor4a(r1,g1,b1),llcolor4b(r2,g2,b2),llcolor4c;
        llcolor4c = llcolor4a - llcolor4b;
        CHECK(is_approx_equal(r1-r2,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1-g2,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1-b2,llcolor4c.mV[VBLUE]));

        llcolor4a -= llcolor4b;
        CHECK(is_approx_equal(r1-r2,llcolor4a.mV[VRED]));
        CHECK(is_approx_equal(g1-g2,llcolor4a.mV[VGREEN]));
        CHECK(is_approx_equal(b1-b2,llcolor4a.mV[VBLUE]));
    }

    TEST_CASE("v4color_h: operator* and operator*= by a color and by a scalar", "[v4color_h]")
    {
        F32 r1 = 0x20, g1 = 0xFFFF, b1 = 0xFF;
        F32 r2 = 0xABF, g2 = 0xFB, b2 = 0xFFF;
        LLColor4 llcolor4a(r1,g1,b1),llcolor4b(r2,g2,b2),llcolor4c;
        llcolor4c = llcolor4a * llcolor4b;
        CHECK(is_approx_equal(r1*r2,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1*g2,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1*b2,llcolor4c.mV[VBLUE]));

        F32 mulVal = 3.33f;
        llcolor4c = llcolor4a * mulVal;
        CHECK(is_approx_equal(r1*mulVal,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1*mulVal,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1*mulVal,llcolor4c.mV[VBLUE]));
        llcolor4c = mulVal * llcolor4a;
        CHECK(is_approx_equal(r1*mulVal,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1*mulVal,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1*mulVal,llcolor4c.mV[VBLUE]));

        llcolor4a *= mulVal;
        CHECK(is_approx_equal(r1*mulVal,llcolor4a.mV[VRED]));
        CHECK(is_approx_equal(g1*mulVal,llcolor4a.mV[VGREEN]));
        CHECK(is_approx_equal(b1*mulVal,llcolor4a.mV[VBLUE]));

        LLColor4 llcolor4d(r1,g1,b1),llcolor4e(r2,g2,b2);
        llcolor4e *= llcolor4d;
        CHECK(is_approx_equal(r1*r2,llcolor4e.mV[VRED]));
        CHECK(is_approx_equal(g1*g2,llcolor4e.mV[VGREEN]));
        CHECK(is_approx_equal(b1*b2,llcolor4e.mV[VBLUE]));
    }

    TEST_CASE("v4color_h: operator% and operator%= scale only the alpha", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF,a = 0x30;
        F32 div = 12.345f;
        LLColor4 llcolor4a(r,g,b,a),llcolor4b;
        llcolor4b = llcolor4a % div;//chnage only alpha value nor r,g,b;
        CHECK(is_approx_equal(r,llcolor4b.mV[VRED]));
        CHECK(is_approx_equal(g,llcolor4b.mV[VGREEN]));
        CHECK(is_approx_equal(b,llcolor4b.mV[VBLUE]));
        CHECK(is_approx_equal(div*a,llcolor4b.mV[VALPHA]));

        llcolor4b = div % llcolor4a;
        CHECK(is_approx_equal(r,llcolor4b.mV[VRED]));
        CHECK(is_approx_equal(g,llcolor4b.mV[VGREEN]));
        CHECK(is_approx_equal(b,llcolor4b.mV[VBLUE]));
        CHECK(is_approx_equal(div*a,llcolor4b.mV[VALPHA]));

        llcolor4a %= div;
        CHECK(is_approx_equal(a*div,llcolor4a.mV[VALPHA]));
    }

    TEST_CASE("v4color_h: operator== and operator!= against LLColor4 and LLColor3", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF,a = 0x30;
        LLColor4 llcolor4a(r,g,b,a),llcolor4b;
        llcolor4b = llcolor4a;
        CHECK(llcolor4b == llcolor4a);
        F32 r1 = 0x2, g1 = 0xFF, b1 = 0xFA;
        LLColor3 llcolor3(r1,g1,b1);
        llcolor4b = llcolor3;
        CHECK(llcolor4b == llcolor3);
        CHECK(llcolor4a != llcolor3);
    }

    TEST_CASE("v4color_h: vec4to3 and vec3to4", "[v4color_h]")
    {
        F32 r = 0x20, g = 0xFFFF, b = 0xFF;
        LLColor4 llcolor4a(r,g,b),llcolor4b;
        LLColor3 llcolor3 = vec4to3(llcolor4a);
        CHECK(is_approx_equal(llcolor3.mV[VRED],llcolor4a.mV[VRED]));
        CHECK(is_approx_equal(llcolor3.mV[VGREEN],llcolor4a.mV[VGREEN]));
        CHECK(is_approx_equal(llcolor3.mV[VBLUE],llcolor4a.mV[VBLUE]));
        llcolor4b = vec3to4(llcolor3);
        CHECK(llcolor4b == llcolor4a);
    }

    TEST_CASE("v4color_h: lerp interpolates every component", "[v4color_h]")
    {
        F32 r1 = 0x20, g1 = 0xFFFF, b1 = 0xFF, val = 0x20;
        F32 r2 = 0xABF, g2 = 0xFB, b2 = 0xFFF;
        LLColor4 llcolor4a(r1,g1,b1),llcolor4b(r2,g2,b2),llcolor4c;
        llcolor4c = lerp(llcolor4a,llcolor4b,val);
        CHECK(is_approx_equal(r1 + (r2 - r1)* val,llcolor4c.mV[VRED]));
        CHECK(is_approx_equal(g1 + (g2 - g1)* val,llcolor4c.mV[VGREEN]));
        CHECK(is_approx_equal(b1 + (b2 - b1)* val,llcolor4c.mV[VBLUE]));
    }

    TEST_CASE("v4color_h: parseColor reads color names and 0 to 255 components", "[v4color_h]")
    {
        F32 r = 12.0f, g = -2.3f, b = 1.32f, a = 5.0f;
        LLColor4 llcolor4a(r,g,b,a),llcolor4b;
        std::string color("red");
        LLColor4::parseColor(color, &llcolor4b);
        CHECK(llcolor4b == LLColor4::red);

        color = "12.0, -2.3, 1.32, 5.0";
        LLColor4::parseColor(color, &llcolor4b);
        llcolor4a = llcolor4a * (1.f / 255.f);
        CHECK(llcolor4a == llcolor4b);

        color = "yellow5";
        llcolor4a.setVec(r,g,b);
        LLColor4::parseColor(color, &llcolor4a);
        CHECK(llcolor4a == LLColor4::yellow5);
    }

    TEST_CASE("v4color_h: parseColor4 reads four floats", "[v4color_h]")
    {
        F32 r = 12.0f, g = -2.3f, b = 1.32f, a = 5.0f;
        LLColor4 llcolor4a(r,g,b,a),llcolor4b;
        std::string color("12.0, -2.3, 1.32, 5.0");
        LLColor4::parseColor4(color, &llcolor4b);
        CHECK(llcolor4a == llcolor4b);
    }
}
