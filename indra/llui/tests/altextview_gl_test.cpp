/**
 * @file altextview_gl_test.cpp
 * @brief What the text view draws, read back from llrender's hidden window
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

#include "../alcodeeditor.h"
#include "../altextview.h"
#include "../llui.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "llrender2dutils.h"

#include "../test/lltut.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <string>
#include <vector>

class LLAvatarName;
const std::string gTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gTestAnonName;
}

namespace
{
    // The drawing the text view does, from outside it.
    struct Painter : public ALTextView
    {
        using ALTextView::drawSquiggle;
    };
}

namespace tut
{
    struct altextview_gl_data
    {
        static constexpr S32 W = ll_test::HeadlessGL::WIDTH;
        static constexpr S32 H = ll_test::HeadlessGL::HEIGHT;

        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(true, true, true, /*needs_render=*/true);
            return instance;
        }

        altextview_gl_data()
        {
            gl();
            ll_test::installWhiteTexture();
        }

        // White drawn over black, blended by its alpha, and read back: how
        // much of each pixel it covered, 0 to 255, bottom row first.
        static std::vector<S32> coverage(const std::function<void()>& draw)
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

        // The squiggle as it was drawn before it was a texture: a ribbon
        // through a point at each pixel from x0 to x1.
        static void ribbon(F32 x0, F32 x1, S32 y)
        {
            std::vector<LLVector2> points;
            for (F32 x = x0; x <= x1; x += 1.f)
            {
                points.emplace_back(x, static_cast<F32>(y) + sinf((x - x0) * (2.f * F_PI / 5.f)));
            }
            gl_polyline_2d(points, LLColor4::white, 1.f);
        }

        struct Difference
        {
            S32 most     = 0;  // the most any one pixel differs by
            S64 covered  = 0;  // what the first covers, all told
            S64 compared = 0;  // what the second does
        };

        static Difference differ(const std::vector<S32>& a, const std::vector<S32>& b, S32 left, S32 right)
        {
            Difference d;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = left; x < right; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    d.most         = llmax(d.most, std::abs(a[i] - b[i]));
                    d.covered += a[i];
                    d.compared += b[i];
                }
            }
            return d;
        }

        static std::string said(const Difference& d)
        {
            return " (most " + std::to_string(d.most) + ", covered " + std::to_string(d.covered) + " against " +
                   std::to_string(d.compared) + ")";
        }
    };

    typedef test_group<altextview_gl_data> altextview_gl_test;
    typedef altextview_gl_test::object     altextview_gl_object;
    tut::altextview_gl_test                altextview_gl_testcase("altextview_gl");

    // A squiggle is one quad over a texture of one wave, and covers what
    // the ribbon of triangles it replaced did: pixel for pixel near enough,
    // and as much of them all told.
    template<> template<>
    void altextview_gl_object::test<1>()
    {
        const LLRect           all(0, H, W, 0);
        const std::vector<S32> was = coverage([] { ribbon(40.f, 140.f, 128); });
        const std::vector<S32> now = coverage([&] { Painter::drawSquiggle(40.f, 140.f, 128, LLColor4::white, all); });
        // Its ends aside, where the ribbon's caps reach past the last point.
        const Difference d = differ(was, now, 42, 138);
        ensure("the ribbon drew something" + said(d), d.covered > 0);
        ensure("no pixel far from the ribbon's" + said(d), d.most <= 64);
        ensure("as much covered as the ribbon" + said(d), std::abs(d.covered - d.compared) * 16 <= d.covered);
        // Nothing above or below the band it was drawn in.
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                if (std::abs(y - 128) > 3 && now[static_cast<size_t>(y) * W + x] != 0)
                {
                    fail("drawn at " + std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
    }

    // What is outside the clip is not drawn, and what is inside is drawn as
    // it would have been: the wave stays where the squiggle began.
    template<> template<>
    void altextview_gl_object::test<2>()
    {
        const std::vector<S32> whole   = coverage([] { Painter::drawSquiggle(33.f, 200.f, 100, LLColor4::white, LLRect(0, H, W, 0)); });
        const std::vector<S32> clipped = coverage([] { Painter::drawSquiggle(33.f, 200.f, 100, LLColor4::white, LLRect(80, H, 120, 0)); });
        S64 inside = 0;
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                const size_t i = static_cast<size_t>(y) * W + x;
                if (x < 80 || x >= 120)
                {
                    ensure_equals("nothing outside the clip at " + std::to_string(x) + "," + std::to_string(y), clipped[i], 0);
                }
                else
                {
                    ensure("the same wave inside the clip at " + std::to_string(x) + "," + std::to_string(y), std::abs(clipped[i] - whole[i]) <= 1);
                    inside += clipped[i];
                }
            }
        }
        ensure("something inside the clip", inside > 0);
    }

    // Where the UI is scaled, the wave is made with as many texels to a
    // point as the scale has pixels, and covers what the ribbon does there.
    template<> template<>
    void altextview_gl_object::test<3>()
    {
        LLVector2& scale = LLUI::getScaleFactor();
        const LLVector2 was_scale = scale;
        scale.set(2.f, 2.f);
        const auto scaled = [](const std::function<void()>& draw) {
            return coverage([&] {
                gGL.pushUIMatrix();
                gGL.loadUIIdentity();
                gGL.scaleUI(2.f, 2.f, 1.f);
                draw();
                gGL.popUIMatrix();
            });
        };
        const std::vector<S32> was = scaled([] { ribbon(10.f, 110.f, 64); });
        const std::vector<S32> now = scaled([] { Painter::drawSquiggle(10.f, 110.f, 64, LLColor4::white, LLRect(0, H, W, 0)); });
        scale = was_scale;
        const Difference d = differ(was, now, 24, 216);
        ensure("the ribbon drew something" + said(d), d.covered > 0);
        ensure("no pixel far from the ribbon's" + said(d), d.most <= 64);
        ensure("as much covered as the ribbon" + said(d), std::abs(d.covered - d.compared) * 16 <= d.covered);
    }
    // The map draws each line's runs from what it read last, and reads a
    // line again when it changes -- a space made a tab moves the runs after
    // it, though no token does -- and not otherwise.
    template<> template<>
    void altextview_gl_object::test<4>()
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 40; ++i)
        {
            text += "word word word word\n";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "view";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = text;
        ALTextView* view = LLUICtrlFactory::create<ALTextView>(p);
        view->setFont(LLFontGL::getFontMonospace());
        view->setScrollMap(true);
        // The map's columns, at the right of the view.
        const S32  map_left = W - view->scrollMapWidth();
        // Every channel of the map's pixels, as the view draws them.
        const auto map_of = [&]() {
            gl().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            view->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            const std::vector<U8> rgba = ll_test::readFramebufferRGBA(W, H);
            std::vector<U8>       map;
            for (S32 y = 0; y < H; ++y)
            {
                map.insert(map.end(), rgba.begin() + (static_cast<size_t>(y) * W + map_left) * 4, rgba.begin() + (static_cast<size_t>(y) * W + W) * 4);
            }
            return map;
        };
        const std::vector<U8> first = map_of();
        ensure("the map draws its lines", std::adjacent_find(first.begin(), first.end(), std::not_equal_to<U8>()) != first.end());
        ensure("the same map when nothing changed", map_of() == first);
        view->document().replace(ALTextRange(ALTextPos(3, 4), ALTextPos(3, 5)), "\t");
        const std::vector<U8> tabbed = map_of();
        ensure("a space made a tab changes the map", tabbed != first);
        ensure("and it stays as it now is", map_of() == tabbed);
        view->die();
    }
    // The code editor draws its rows in passes -- what is behind the text,
    // every row's glyphs in one call, then what is over it -- and each
    // thing still lands on its own line: a selection, a colour over part
    // of a line and a squiggle each change only the pixels of the line they
    // are on.
    // And the frame is a handful of draws.
    template<> template<>
    void altextview_gl_object::test<5>()
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 12; ++i)
        {
            text += "integer value = f(x, [y, z]);\n";
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name         = "editor";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = text;
        p.syntax       = "lsl";
        ALCodeEditor* editor = LLUICtrlFactory::create<ALCodeEditor>(p);
        editor->setFont(LLFontGL::getFontMonospace());
        // With the keyboard, so that a selection is drawn as the one being
        // worked in; read-only, so that no caret blinks between frames.
        editor->setReadOnly(true);
        editor->setFocus(true);
        // The skin's colours are not loaded here: the selection's is given.
        editor->setSelectionColor(LLUIColor(LLColor4(0.2f, 0.4f, 0.9f, 0.6f)));

        const auto frame = [&]() {
            gl().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            editor->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // Every pixel that differs between two frames lies within a line's
        // band, a few pixels either way; and some do.
        const auto only_on = [&](const std::string& what, const std::vector<U8>& a, const std::vector<U8>& b, S32 line) {
            const S32 top    = editor->textRect().mTop - (editor->layout().lineTop(line) - editor->scrollY()) + 3;
            const S32 bottom = top - 3 - editor->layout().lineHeight(line) - 3;
            bool      some   = false;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                    if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
                    {
                        some = true;
                        ensure(what + ": changed at " + std::to_string(x) + "," + std::to_string(y) + ", off line " + std::to_string(line),
                               y <= top && y >= bottom);
                    }
                }
            }
            ensure(what + ": something changed", some);
        };

        const std::vector<U8> plain = frame();
        ensure("the same frame again", frame() == plain);

        editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 20)));
        only_on("a selection", plain, frame(), 2);
        editor->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        ensure("the selection gone", frame() == plain);

        // A colour of its own over part of a line: each row's glyphs carry
        // their own colours into the one call.
        ALTextView::Style style;
        style.range = ALTextRange(ALTextPos(5, 8), ALTextPos(5, 13));
        style.color = LLColor4::red;
        editor->setStyles({ style });
        only_on("a colour of its own", plain, frame(), 5);
        editor->clearStyles();
        ensure("the colour gone", frame() == plain);

        ALCodeEditor::Decoration d;
        d.range = ALTextRange(ALTextPos(7, 8), ALTextPos(7, 13));
        d.color = LLColor4::red;
        editor->setDecorations({ d });
        only_on("a squiggle", plain, frame(), 7);

        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        editor->draw();
        gGL.flush();
        gGL.endList();
        glFinish();
        ensure("a handful of draws: " + std::to_string(capture.size()), capture.size() <= 25);
        gFocusMgr.setKeyboardFocus(nullptr);
        editor->die();
    }
}
