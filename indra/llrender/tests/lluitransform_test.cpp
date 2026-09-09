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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<lluitransform_data> lluitransform_test;
    typedef lluitransform_test::object     lluitransform_object;
    tut::lluitransform_test lluitransform_testgroup("lluitransform");

    // With nothing pushed, a local rect is where it says it is.
    template<> template<>
    void lluitransform_object::test<1>()
    {
        const LLRect local(10, 40, 110, 20);
        const LLRect screen = LLRender2D::toScreen(local);
        ensure_equals("untransformed, it is itself " + where(screen), where(screen), where(local));
    }

    // An offset moves it, which is all this ever did.
    template<> template<>
    void lluitransform_object::test<2>()
    {
        LLFontGL::sCurOrigin.set(7, 13);
        const LLRect screen = LLRender2D::toScreen(LLRect(10, 40, 110, 20));
        ensure_equals("moved by the offset", screen.mLeft, 17);
        ensure_equals("in both directions", screen.mBottom, 33);
        ensure_equals("keeping its width", screen.getWidth(), 100);
        ensure_equals("and its height", screen.getHeight(), 20);
    }

    // A scale grows it about the screen origin, and the offset is scaled with
    // it: a UI vertex is (local + offset) * scale, and this is the same
    // arithmetic the renderer does to every one of them. Clipping used to
    // apply the offset and not the scale, so a scissor sat where the drawing
    // would have been at a hundred per cent.
    template<> template<>
    void lluitransform_object::test<3>()
    {
        LLFontGL::sCurOrigin.set(10, 20);
        LLFontGL::sCurScaleX = 2.f;
        LLFontGL::sCurScaleY = 2.f;

        const LLRect screen = LLRender2D::toScreen(LLRect(0, 50, 100, 0));
        ensure_equals("the near edge is the scaled offset", screen.mLeft, 20);
        ensure_equals("and so is the bottom", screen.mBottom, 40);
        ensure_equals("the far edge is scaled too", screen.mRight, 220);
        ensure_equals("the rect is twice the size", screen.getWidth(), 200);
        ensure_equals("in both directions", screen.getHeight(), 100);
    }

    // The two axes are independent, because the renderer's are.
    template<> template<>
    void lluitransform_object::test<4>()
    {
        LLFontGL::sCurScaleX = 3.f;
        LLFontGL::sCurScaleY = 1.f;

        const LLRect screen = LLRender2D::toScreen(LLRect(0, 10, 10, 0));
        ensure_equals("across", screen.getWidth(), 30);
        ensure_equals("down is left alone", screen.getHeight(), 10);
    }

    // A point and a rect agree, since the rect is two points.
    template<> template<>
    void lluitransform_object::test<5>()
    {
        LLFontGL::sCurOrigin.set(4, 6);
        LLFontGL::sCurScaleX = 1.5f;
        LLFontGL::sCurScaleY = 2.5f;

        F32 x = 0.f;
        F32 y = 0.f;
        LLRender2D::toScreen(20.f, 30.f, x, y);
        ensure_equals("across", (S32)ll_round(x), 36);
        ensure_equals("down", (S32)ll_round(y), 90);

        const LLRect screen = LLRender2D::toScreen(LLRect(20, 30, 20, 30));
        ensure_equals("the rect's corner is the same point", screen.mLeft, 36);
        ensure_equals("and the same on the other axis", screen.mBottom, 90);
    }
}
