/**
 * @file alsurface_gl_test.cpp
 * @brief A surface's ground and frame, and the tab strip's tabs, read back from llrender's hidden window.
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

#include "llglheaders.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include "../alsurface.h"
#include "../altabstrip.h"
#include "../llui.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <functional>
#include <string>
#include <vector>

class LLAvatarName;
const std::string gSurfaceGLTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSurfaceGLTestAnonName;
}

namespace
{
    constexpr S32 W = ll_test::HeadlessGL::WIDTH;
    constexpr S32 H = ll_test::HeadlessGL::HEIGHT;

    ll_test::HeadlessGL& gl()
    {
        static ll_test::HeadlessGL instance(true, true, true, /*needs_render=*/true);
        return instance;
    }

    // What is drawn over black, read back: each pixel's red, bottom row
    // first.
    std::vector<S32> drawn(const std::function<void()>& draw)
    {
        gl().clearFramebuffer();
        glEnable(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        draw();
        gGL.flush();
        glDisable(GL_BLEND);
        glFinish();
        const std::vector<U8> rgba = ll_test::readFramebufferRGBA(W, H);
        std::vector<S32>      out(static_cast<size_t>(W) * H);
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = rgba[i * 4];
        }
        return out;
    }

    S32 at(const std::vector<S32>& pixels, S32 x, S32 y) { return pixels[static_cast<size_t>(y) * W + x]; }
}

namespace tut
{
    struct alsurface_gl_data
    {
        alsurface_gl_data()
        {
            gl();
            ll_test::installWhiteTexture();
        }
    };

    typedef test_group<alsurface_gl_data> alsurface_gl_test;
    typedef alsurface_gl_test::object     alsurface_gl_object;
    tut::alsurface_gl_test                alsurface_gl_testcase("alsurface_gl");

    template<> template<>
    void alsurface_gl_object::test<1>()
    {
        set_test_name("a surface is its ground inside its rect and its frame on the pixels just inside the edges, at the window's corner too");
        for (const LLRect& rect : { LLRect(0, 10, 20, 0), LLRect(30, 40, 50, 20) })
        {
            const std::vector<S32> pixels = drawn([&] { ALSurface::draw(rect, LLColor4::black, LLColor4::white, 1.f); });
            // Its frame and its ground, where both are sure to be.
            const S32 frame  = at(pixels, rect.mLeft + 3, rect.mTop - 1);
            const S32 ground = at(pixels, rect.mLeft + 3, rect.mBottom + 3);
            ensure("the frame and the ground tell apart", frame != ground && ground != 0);
            for (S32 y = 0; y < 50; ++y)
            {
                for (S32 x = 0; x < 60; ++x)
                {
                    const bool inside = x >= rect.mLeft && x < rect.mRight && y >= rect.mBottom && y < rect.mTop;
                    const bool edge   = inside && (x == rect.mLeft || x == rect.mRight - 1 || y == rect.mBottom || y == rect.mTop - 1);
                    const S32  wanted = edge ? frame : inside ? ground : 0;
                    if (at(pixels, x, y) != wanted)
                    {
                        fail("at " + std::to_string(x) + "," + std::to_string(y) + " of the surface at " + std::to_string(rect.mLeft) + "," +
                             std::to_string(rect.mBottom) + ": " + std::to_string(at(pixels, x, y)) + " for " + std::to_string(wanted));
                    }
                }
            }
        }
    }

    template<> template<>
    void alsurface_gl_object::test<2>()
    {
        set_test_name("the first tab of a strip is framed at its left edge as at its right, not left without a line there");
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip::Params p(LLUICtrlFactory::getDefaultParams<ALTabStrip>());
        p.name                 = "tabs";
        p.rect                 = LLRect(0, 24, W, 0);
        ALTabStrip*                  strip = LLUICtrlFactory::create<ALTabStrip>(p);
        std::vector<ALTabStrip::Tab> tabs(2);
        tabs[0].label = "first";
        tabs[0].value = "a";
        tabs[1].label = "second";
        tabs[1].value = "b";
        strip->setTabs(tabs, "b");
        const std::vector<S32> pixels = drawn([&] { strip->draw(); });
        const LLRect           first  = strip->rectOf(0);
        const S32              middle = first.getCenterY();
        // Its frame and its ground, where both are sure to be: the top
        // line, and a blank inside it.
        const S32 frame  = at(pixels, first.mLeft + 3, first.mTop - 1);
        const S32 ground = at(pixels, first.mLeft + 3, middle);
        ensure("the frame and the ground tell apart", frame != ground);
        ensure_equals("framed at its left edge", at(pixels, first.mLeft, middle), frame);
        ensure_equals("and at its right", at(pixels, first.mRight - 1, middle), frame);
        ensure_equals("its ground just inside", at(pixels, first.mLeft + 1, middle), ground);
        strip->die();
    }

    template<> template<>
    void alsurface_gl_object::test<3>()
    {
        set_test_name("a strip attached to what it shows opens its shown tab into it, with no bottom edge; the tabs behind, and an unattached "
                      "strip's shown tab, keep theirs");
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        for (const bool attached : { true, false })
        {
            ALTabStrip::Params p(LLUICtrlFactory::getDefaultParams<ALTabStrip>());
            p.name                 = "tabs";
            p.rect                 = LLRect(0, 24, W, 0);
            p.attached             = attached;
            ALTabStrip*                  strip = LLUICtrlFactory::create<ALTabStrip>(p);
            std::vector<ALTabStrip::Tab> tabs(2);
            tabs[0].label = "first";
            tabs[0].value = "a";
            tabs[1].label = "second";
            tabs[1].value = "b";
            strip->setTabs(tabs, "b");
            const std::vector<S32> pixels = drawn([&] { strip->draw(); });
            const LLRect           shown  = strip->rectOf(1);
            const LLRect           behind = strip->rectOf(0);
            const S32              frame  = at(pixels, shown.mLeft + 3, shown.mTop - 1);
            const S32              ground = at(pixels, shown.mLeft + 3, shown.getCenterY());
            ensure("the frame and the shown tab's ground tell apart", frame != ground);
            ensure_equals(attached ? "the shown tab open at its bottom" : "unattached, the shown tab closed at its bottom",
                          at(pixels, shown.mLeft + 3, shown.mBottom), attached ? ground : frame);
            ensure_equals("its sides still framed", at(pixels, shown.mLeft, shown.getCenterY()), frame);
            ensure_equals("a tab behind closed at its bottom", at(pixels, behind.mLeft + 3, behind.mBottom), frame);
            strip->die();
        }
    }
}
