/**
 * @file lluitransform_test.cpp
 * @brief Where a local coordinate lands on screen, which clipping has to agree with.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontgl.h"
#include "../llrender2dutils.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct lluitransform_data
    {
        // The shadow of the UI transform is global, so each test says what it
        // starts from rather than inheriting whatever ran before it.
        lluitransform_data()
        {
            LLFontGL::sCurOrigin.set(0, 0);
            LLFontGL::sCurDepth = 0.f;
            LLFontGL::sCurScaleX = 1.f;
            LLFontGL::sCurScaleY = 1.f;
            LLFontGL::sOriginStack.clear();
        }

        ~lluitransform_data()
        {
            LLFontGL::sCurOrigin.set(0, 0);
            LLFontGL::sCurScaleX = 1.f;
            LLFontGL::sCurScaleY = 1.f;
            LLFontGL::sOriginStack.clear();
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }
    };


    TEST_CASE_METHOD(lluitransform_data, "lluitransform: with nothing pushed a local rect is where it says it is", "[lluitransform]")
    {
        const LLRect local(10, 40, 110, 20);
        const LLRect screen = LLRender2D::toScreen(local);
        CHECK(where(screen) == where(local));
    }

    // An offset moves it, which is all this ever did.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: an offset moves a rect and keeps its size", "[lluitransform]")
    {
        LLFontGL::sCurOrigin.set(7, 13);
        const LLRect screen = LLRender2D::toScreen(LLRect(10, 40, 110, 20));
        CHECK(screen.mLeft == 17);
        CHECK(screen.mBottom == 33);
        CHECK(screen.getWidth() == 100);
        CHECK(screen.getHeight() == 20);
    }

    // A scale grows it about the screen origin, and the offset is scaled with
    // it: a UI vertex is (local + offset) * scale, and this is the same
    // arithmetic the renderer does to every one of them. Clipping used to
    // apply the offset and not the scale, so a scissor sat where the drawing
    // would have been at a hundred per cent.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: a scale grows a rect about the screen origin and scales the offset", "[lluitransform]")
    {
        LLFontGL::sCurOrigin.set(10, 20);
        LLFontGL::sCurScaleX = 2.f;
        LLFontGL::sCurScaleY = 2.f;

        const LLRect screen = LLRender2D::toScreen(LLRect(0, 50, 100, 0));
        {
            INFO("the near edges are the scaled offset");
            CHECK(screen.mLeft == 20);
            CHECK(screen.mBottom == 40);
        }
        UNSCOPED_INFO("the far edge is scaled too");
        CHECK(screen.mRight == 220);
        {
            INFO("the rect is twice the size");
            CHECK(screen.getWidth() == 200);
            CHECK(screen.getHeight() == 100);
        }
    }

    // The two axes are independent, because the renderer's are.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: the two axes scale independently", "[lluitransform]")
    {
        LLFontGL::sCurScaleX = 3.f;
        LLFontGL::sCurScaleY = 1.f;

        const LLRect screen = LLRender2D::toScreen(LLRect(0, 10, 10, 0));
        CHECK(screen.getWidth() == 30);
        UNSCOPED_INFO("down is left alone");
        CHECK(screen.getHeight() == 10);
    }

    // A point and a rect agree, since the rect is two points.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: a point and a rect's corner land in the same place", "[lluitransform]")
    {
        LLFontGL::sCurOrigin.set(4, 6);
        LLFontGL::sCurScaleX = 1.5f;
        LLFontGL::sCurScaleY = 2.5f;

        F32 x = 0.f;
        F32 y = 0.f;
        LLRender2D::toScreen(20.f, 30.f, x, y);
        CHECK((S32)ll_round(x) == 36);
        CHECK((S32)ll_round(y) == 90);

        const LLRect screen = LLRender2D::toScreen(LLRect(20, 30, 20, 30));
        {
            INFO("the rect's corner is the same point");
            CHECK(screen.mLeft == 36);
            CHECK(screen.mBottom == 90);
        }
    }

    // A scale keeps the origin it was pushed at: what is under it is drawn
    // larger where it already was, rather than being carried away from the
    // window's corner in proportion to how far along it had got.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: a scale keeps the origin it was pushed at", "[lluitransform]")
    {
        LLRender2D::pushMatrix();
        LLRender2D::translate(300.f, 200.f);
        LLRender2D::scale(2.f, 2.f);

        F32 x = 0.f;
        F32 y = 0.f;
        LLRender2D::toScreen(0.f, 0.f, x, y);
        {
            INFO("the corner it was pushed at is where it was");
            CHECK(ll_round(x) == 300);
            CHECK(ll_round(y) == 200);
        }

        LLRender2D::toScreen(50.f, 40.f, x, y);
        {
            INFO("what is on it is twice as far along");
            CHECK(ll_round(x) == 400);
            CHECK(ll_round(y) == 280);
        }

        LLRender2D::popMatrix();
    }

    // Taking the frame off takes the scale with it, and what it did to the
    // offset to keep the origin.
    TEST_CASE_METHOD(lluitransform_data, "lluitransform: popMatrix takes off the frame's scale and what it did to the offset", "[lluitransform]")
    {
        LLRender2D::pushMatrix();
        LLRender2D::translate(30.f, 40.f);
        LLRender2D::pushMatrix();
        LLRender2D::scale(4.f, 4.f);
        LLRender2D::popMatrix();

        F32 x = 0.f;
        F32 y = 0.f;
        LLRender2D::toScreen(10.f, 10.f, x, y);
        {
            INFO("the offset is the one the frame was pushed on");
            CHECK(ll_round(x) == 40);
            CHECK(ll_round(y) == 50);
        }
        {
            INFO("the scale is off with it");
            CHECK(LLFontGL::sCurScaleX == 1.f);
            CHECK(LLFontGL::sCurScaleY == 1.f);
        }

        LLRender2D::popMatrix();
    }
}
