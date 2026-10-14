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

#include "aluigl_fixture.h"

#include "alcodeeditor.h"
#include "altextruler.h"
#include "altextview.h"
#include "../llui.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <ostream>
#include <string>
#include <vector>

namespace
{
    // The drawing the text view does, from outside it.
    struct Painter : public ALTextView
    {
        using ALTextView::drawSquiggle;
        using ALTextView::squiggleMiddle;
    };

    constexpr S32 W = ll_test::HeadlessGL::WIDTH;
    constexpr S32 H = ll_test::HeadlessGL::HEIGHT;

    // White drawn over black, blended by its alpha, and read back: how
    // much of each pixel it covered, 0 to 255, bottom row first.
    std::vector<S32> coverage(const std::function<void()>& draw)
    {
        ll_test::uiGL().clearFramebuffer();
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

    // Where two frames, or two strips of them, part: how many pixels differ
    // and the first of them, so that a failure names a place rather than
    // printing both whole. A row is width pixels of channels bytes, bottom
    // row first; frames of different sizes differ by what the longer has
    // over the shorter.
    struct FrameDiff
    {
        size_t pixels = 0;
        S32    x      = -1;
        S32    y      = -1;

        bool operator==(const FrameDiff&) const = default;
    };

    std::ostream& operator<<(std::ostream& out, const FrameDiff& d)
    {
        if (d.pixels == 0)
        {
            return out << "no pixel";
        }
        return out << d.pixels << " pixels, the first at (" << d.x << ", " << d.y << ")";
    }

    FrameDiff frameDiff(const std::vector<U8>& a, const std::vector<U8>& b, S32 width = W, S32 channels = 4)
    {
        FrameDiff    d;
        const size_t step   = static_cast<size_t>(channels);
        const size_t common = std::min(a.size(), b.size());
        for (size_t i = 0; i + step <= common; i += step)
        {
            if (!std::equal(a.begin() + i, a.begin() + i + step, b.begin() + i))
            {
                if (d.pixels++ == 0)
                {
                    d.x = static_cast<S32>((i / step) % static_cast<size_t>(width));
                    d.y = static_cast<S32>((i / step) / static_cast<size_t>(width));
                }
            }
        }
        d.pixels += (std::max(a.size(), b.size()) - common + step - 1) / step;
        return d;
    }

    // A keymap that is always typing the same line, and counts how often
    // the view asks it what the line is.
    struct TypingKeymap : public ALModalKeymap
    {
        S32* asked = nullptr;

        bool        handleKey(ALTextView&, KEY, MASK) override { return false; }
        bool        handleChar(ALTextView&, llwchar) override { return false; }
        bool        inserting() const override { return false; }
        std::string status() const override { return "NORMAL"; }
        U32         generation() const override { return 1; }
        bool        typingLine(std::string& line, S32& caret) const override
        {
            ++*asked;
            line  = ":set number";
            caret = 4;
            return true;
        }
    };

    // A view that is a white box: an atom's view, to see where it is drawn.
    struct WhiteBox : public LLView
    {
        explicit WhiteBox(const LLView::Params& p) : LLView(p) {}
        void draw() override { gl_rect_2d(getLocalRect(), LLColor4::white, true); }
    };

    // A view drawn where it is in a window, not at the window's corner:
    // what is drawn past its edges lands where it can be seen.
    std::vector<U8> drawnAt(LLView& view)
    {
        ll_test::uiGL().clearFramebuffer();
        glEnable(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        LLUI::pushMatrix();
        LLUI::translate(static_cast<F32>(view.getRect().mLeft), static_cast<F32>(view.getRect().mBottom));
        view.draw();
        LLUI::popMatrix();
        gGL.flush();
        glDisable(GL_BLEND);
        glFinish();
        return ll_test::readFramebufferRGBA(W, H);
    }

    // How many pixels of a stretch of the window, rows [bottom, top) and
    // columns [left, right), anything was drawn in.
    S32 drawnIn(const std::vector<U8>& rgba, S32 left, S32 right, S32 bottom, S32 top)
    {
        S32 found = 0;
        for (S32 y = llmax(0, bottom); y < llmin(H, top); ++y)
        {
            for (S32 x = llmax(0, left); x < llmin(W, right); ++x)
            {
                const U8* px = &rgba[(static_cast<size_t>(y) * W + x) * 4];
                found += (px[0] | px[1] | px[2]) != 0 ? 1 : 0;
            }
        }
        return found;
    }
}

namespace
{
    struct altextview_gl_data
    {
        // Focus as the case found it, whatever it leaves it on.
        ll_test::FocusScope  focus;
        // The views a case made, dying when it ends however it ends: a
        // failed REQUIRE skips whatever comes after it.
        std::vector<LLView*> views;

        altextview_gl_data()
        {
            ll_test::uiGL();
            ll_test::installWhiteTexture();
        }

        ~altextview_gl_data()
        {
            for (LLView* view : views)
            {
                view->die();
            }
        }

        template <class T>
        T* keep(T* view)
        {
            views.push_back(view);
            return view;
        }

        // What a squiggle from x0 at height y covers of a pixel, worked out
        // afresh: how near the wave comes to the pixel's middle, the line
        // solid for three quarters of a point either side and fading over
        // a point more -- a wave a point high either way and six long --
        // with as many pixels to a point as `scale`.
        static S32 expected(F32 x0, S32 y, F32 scale, S32 px, S32 py)
        {
            constexpr F32 AMPLITUDE = 1.f, WAVE = 6.f, HALF = 0.75f, FEATHER = 1.f;
            const F32     x         = (static_cast<F32>(px) + 0.5f) / scale - x0;
            const F32     dy        = (static_cast<F32>(py) + 0.5f) / scale - static_cast<F32>(y);
            F32           nearest   = F32_MAX;
            for (F32 along = x - WAVE; along <= x + WAVE; along += 0.005f)
            {
                const F32 ax = x - along;
                const F32 ay = dy - AMPLITUDE * sinf(along * 2.f * F_PI / WAVE);
                nearest      = llmin(nearest, ax * ax + ay * ay);
            }
            const F32 cover = llclamp((HALF + FEATHER - sqrtf(nearest)) / FEATHER, 0.f, 1.f);
            return static_cast<S32>(llround(cover * 255.f));
        }
        static std::vector<S32> expectedAll(F32 x0, F32 x1, S32 y, F32 scale)
        {
            std::vector<S32> out(static_cast<size_t>(W) * H, 0);
            for (S32 py = 0; py < H; ++py)
            {
                for (S32 px = static_cast<S32>(x0 * scale); px < static_cast<S32>(x1 * scale) && px < W; ++px)
                {
                    out[static_cast<size_t>(py) * W + px] = expected(x0, y, scale, px, py);
                }
            }
            return out;
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
    };

    // A squiggle is one quad over a texture of one wave, and covers what
    // the wave works out to: pixel for pixel, each texel's middle falling
    // on a pixel's, and as much of them all told.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a squiggle covers what its wave works out to pixel for pixel", "[altextview_gl]")
    {
        const LLRect           all(0, H, W, 0);
        const std::vector<S32> was = expectedAll(40.f, 140.f, 128, 1.f);
        const std::vector<S32> now = coverage([&] { Painter::drawSquiggle(40.f, 140.f, 128, LLColor4::white, all); });
        const Difference       d   = differ(was, now, 40, 140);
        {
            CAPTURE(d.most, d.covered, d.compared);
            UNSCOPED_INFO("the squiggle covers something");
            CHECK(d.compared > 0);
            UNSCOPED_INFO("no pixel far from the wave's");
            CHECK(d.most <= 6);
            UNSCOPED_INFO("as much covered as the wave");
            CHECK(std::abs(d.covered - d.compared) * 64 <= d.covered);
        }
        // Nothing above or below the band it was drawn in.
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                if (std::abs(y - 128) > 3 && now[static_cast<size_t>(y) * W + x] != 0)
                {
                    CAPTURE(x, y);
                    FAIL("drawn outside the wave's band");
                }
            }
        }
    }

    // What is outside the clip is not drawn, and what is inside is drawn as
    // it would have been: the wave stays where the squiggle began.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a squiggle's clip cuts it without moving the wave inside", "[altextview_gl]")
    {
        const std::vector<S32> whole   = coverage([] { Painter::drawSquiggle(33.f, 200.f, 100, LLColor4::white, LLRect(0, H, W, 0)); });
        const std::vector<S32> clipped = coverage([] { Painter::drawSquiggle(33.f, 200.f, 100, LLColor4::white, LLRect(80, H, 120, 0)); });
        S64 inside = 0;
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                const size_t i = static_cast<size_t>(y) * W + x;
                CAPTURE(x, y);
                if (x < 80 || x >= 120)
                {
                    UNSCOPED_INFO("nothing outside the clip");
                    REQUIRE(clipped[i] == 0);
                }
                else
                {
                    UNSCOPED_INFO("the same wave inside the clip");
                    REQUIRE(std::abs(clipped[i] - whole[i]) <= 1);
                    inside += clipped[i];
                }
            }
        }
        UNSCOPED_INFO("something inside the clip");
        CHECK(inside > 0);
    }

    // Where the UI is scaled, the wave is made with as many texels to a
    // point as the scale has pixels, and covers what the wave works out to
    // there.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: under a UI scale a squiggle covers what its wave works out to", "[altextview_gl]")
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
        const std::vector<S32> was = expectedAll(10.f, 110.f, 64, 2.f);
        const std::vector<S32> now = scaled([] { Painter::drawSquiggle(10.f, 110.f, 64, LLColor4::white, LLRect(0, H, W, 0)); });
        scale = was_scale;
        const Difference d = differ(was, now, 20, 220);
        CAPTURE(d.most, d.covered, d.compared);
        UNSCOPED_INFO("the squiggle covers something");
        CHECK(d.compared > 0);
        UNSCOPED_INFO("no pixel far from the wave's");
        CHECK(d.most <= 6);
        UNSCOPED_INFO("as much covered as the wave");
        CHECK(std::abs(d.covered - d.compared) * 64 <= d.covered);
    }
    // The map draws each line's runs from what it read last, and reads a
    // line again when it changes -- a space made a tab moves the runs after
    // it, though no token does -- and not otherwise.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: the scroll map reads a line again only when it changes", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        view->setScrollMap(true);
        // The map's columns, at the right of the view.
        const S32  map_left = W - view->scrollMapWidth();
        // Every channel of the map's pixels, as the view draws them.
        const auto map_of = [&]() {
            ll_test::uiGL().clearFramebuffer();
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
        UNSCOPED_INFO("the map draws its lines");
        REQUIRE(std::adjacent_find(first.begin(), first.end(), std::not_equal_to<U8>()) != first.end());
        UNSCOPED_INFO("the same map when nothing changed");
        CHECK(frameDiff(map_of(), first, W - map_left) == FrameDiff());
        view->document().replace(ALTextRange(ALTextPos(3, 4), ALTextPos(3, 5)), "\t");
        const std::vector<U8> tabbed = map_of();
        UNSCOPED_INFO("a space made a tab changes the map");
        CHECK(frameDiff(tabbed, first, W - map_left).pixels > 0);
        UNSCOPED_INFO("and it stays as it now is");
        CHECK(frameDiff(map_of(), tabbed, W - map_left) == FrameDiff());
    }
    // The code editor draws its rows in passes -- what is behind the text,
    // every row's glyphs in one call, then what is over it -- and each
    // thing still lands on its own line: a selection, a colour over part
    // of a line and a squiggle each change only the pixels of the line they
    // are on.
    // And the frame is a handful of draws.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: the code editor draws its rows in passes each landing on its own line", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ALCodeEditor* editor = keep(LLUICtrlFactory::create<ALCodeEditor>(p));
        editor->setFont(LLFontGL::getFontMonospace());
        // With the keyboard, so that a selection is drawn as the one being
        // worked in; read-only, so that no caret blinks between frames.
        editor->setReadOnly(true);
        editor->setFocus(true);
        // The skin's colours are not loaded here: the selection's is given.
        editor->setSelectionColor(LLUIColor(LLColor4(0.2f, 0.4f, 0.9f, 0.6f)));

        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            editor->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // Every pixel that differs between two frames lies within a line's
        // band, a few pixels either way -- `below` more under it -- and
        // some do.
        const auto require_only_on = [&](const std::string& what, const std::vector<U8>& a, const std::vector<U8>& b, S32 line, S32 below = 0) {
            INFO(what);
            const S32 top    = editor->textRect().mTop - (editor->layout().lineTop(line) - editor->scrollY()) + 3;
            const S32 bottom = top - 3 - editor->layout().lineHeight(line) - 3 - below;
            bool      some   = false;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                    if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
                    {
                        some = true;
                        CAPTURE(x, y, line);
                        INFO("changed off the line");
                        REQUIRE(y <= top);
                        REQUIRE(y >= bottom);
                    }
                }
            }
            UNSCOPED_INFO("something changed");
            REQUIRE(some);
        };

        const std::vector<U8> plain = frame();
        UNSCOPED_INFO("the same frame again");
        CHECK(frameDiff(frame(), plain) == FrameDiff());

        editor->setSelection(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 20)));
        require_only_on("a selection", plain, frame(), 2);
        editor->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        UNSCOPED_INFO("the selection gone");
        CHECK(frameDiff(frame(), plain) == FrameDiff());

        // A colour of its own over part of a line: each row's glyphs carry
        // their own colours into the one call.
        ALTextView::Style style;
        style.range = ALTextRange(ALTextPos(5, 8), ALTextPos(5, 13));
        style.color = LLColor4::red;
        editor->setStyles({ style });
        require_only_on("a colour of its own", plain, frame(), 5);
        editor->clearStyles();
        UNSCOPED_INFO("the colour gone");
        CHECK(frameDiff(frame(), plain) == FrameDiff());

        ALCodeEditor::Decoration d;
        d.range = ALTextRange(ALTextPos(7, 8), ALTextPos(7, 13));
        d.color = LLColor4::red;
        editor->setDecorations({ d });
        // Under the text, where the line's descenders reach: past the row
        // where the font's lines are closer than its letters are tall.
        const LLFontGL* mono     = LLFontGL::getFontMonospace();
        const S32       overhang = llmax(0, ll_round(mono->getDescenderHeight()) - (mono->getLineSpacing() - ll_round(mono->getAscenderHeight())));
        require_only_on("a squiggle", plain, frame(), 7, overhang);

        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        editor->draw();
        gGL.flush();
        gGL.endList();
        glFinish();
        UNSCOPED_INFO("the frame is a handful of draws");
        CHECK(capture.size() <= 25);
    }

    // The preview of the lines under the mouse on the map is drawn over the
    // text beside the map: to its left where the map is on the right, to its
    // right where it is on the left -- not off the view past it.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: the map's preview is drawn over the text beside the map on either side", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += "word word word word\n";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name           = "view";
        p.rect           = LLRect(0, H, W, 0);
        p.default_text   = text;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        view->setScrollMap(true);
        view->setScrollMapWidth(60);
        view->setBackgroundColor(LLColor4(0.1f, 0.1f, 0.12f, 1.f));
        view->setTextColor(LLColor4(0.9f, 0.9f, 0.9f, 1.f));
        ALTextRuler* map = view->findChild<ALTextRuler>("ruler");
        REQUIRE(map != nullptr);
        // Every pixel of the view, as it draws.
        const auto drawn = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            view->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // How many pixels between two columns differ between two frames.
        const auto changed = [&](const std::vector<U8>& a, const std::vector<U8>& b, S32 from, S32 to) {
            S32 count = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = from; x < to; ++x)
                {
                    const size_t at = (static_cast<size_t>(y) * W + x) * 4;
                    count += a[at] != b[at] || a[at + 1] != b[at + 1] || a[at + 2] != b[at + 2];
                }
            }
            return count;
        };
        for (const bool left : { false, true })
        {
            view->setScrollMapOnLeft(left);
            map->onMouseLeave(0, 0, MASK_NONE);
            const std::vector<U8> plain = drawn();
            map->handleHover(30, H / 2, MASK_NONE);
            const std::vector<U8> shown = drawn();
            const S32 text_from = left ? 60 : 0;
            const S32 text_to   = left ? W : W - 60;
            UNSCOPED_INFO("the preview over the text, the map on the " << (left ? "left" : "right"));
            REQUIRE(changed(plain, shown, text_from, text_to) > 1000);
        }
    }

    // A row's squiggle is under its text: nothing at or above the baseline,
    // where a period's foot stands, and only the fade in the pixel under
    // it; the line itself lower still.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a row's squiggle draws nothing at or above its text's baseline", "[altextview_gl]")
    {
        const S32              text_top = 150;
        const S32              ascent   = 12;
        const S32              baseline = text_top - ascent;
        const S32              middle   = Painter::squiggleMiddle(text_top, ascent);
        const std::vector<S32> now      = coverage([&] { Painter::drawSquiggle(40.f, 140.f, middle, LLColor4::white, LLRect(0, H, W, 0)); });
        S32                    under    = 0;
        S32                    drawn    = 0;
        for (S32 x = 40; x < 140; ++x)
        {
            for (S32 y = baseline; y < text_top; ++y)
            {
                if (now[static_cast<size_t>(y) * W + x] != 0)
                {
                    CAPTURE(x, y);
                    FAIL("drawn over the text");
                }
            }
            under = llmax(under, now[static_cast<size_t>(baseline - 1) * W + x]);
            for (S32 y = 0; y < baseline; ++y)
            {
                drawn = llmax(drawn, now[static_cast<size_t>(y) * W + x]);
            }
        }
        UNSCOPED_INFO("drawn under the text");
        CHECK(drawn == 255);
        UNSCOPED_INFO("only the fade just under the baseline");
        CHECK(under < 128);
    }

    // The carets besides the main one are drawn as it is, blinking with it:
    // a frame with the main caret on one line and another on a second is
    // the frame with the two the other way round. A selection besides the
    // main one is washed as it is, on its own line only. And every caret
    // in sight is the one draw.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: other carets and selections are drawn as the main one is in one draw", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 12; ++i)
        {
            text += "integer value = f(x, [y, z]);\n";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "view";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = text;
        // The skin's colours are not loaded here: the caret's is given.
        p.cursor_color = LLUIColor(LLColor4::white);
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        view->setSelectionColor(LLUIColor(LLColor4(0.2f, 0.4f, 0.9f, 0.6f)));
        // Shown, and still from frame to frame.
        view->setCaretBlink(false);
        view->setFocus(true);

        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            view->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // Every pixel that differs between two frames lies within a line's
        // band, and some do.
        const auto require_only_on = [&](const std::string& what, const std::vector<U8>& a, const std::vector<U8>& b, S32 line) {
            INFO(what);
            const S32 top    = view->textRect().mTop - (view->layout().lineTop(line) - view->scrollY()) + 3;
            const S32 bottom = top - 3 - view->layout().lineHeight(line) - 3;
            bool      some   = false;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                    if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
                    {
                        some = true;
                        CAPTURE(x, y, line);
                        INFO("changed off the line");
                        REQUIRE(y <= top);
                        REQUIRE(y >= bottom);
                    }
                }
            }
            UNSCOPED_INFO("something changed");
            REQUIRE(some);
        };
        const auto at = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };

        view->setSelection(at(4, 0));
        const std::vector<U8> one = frame();
        view->addSelection(at(2, 5));
        const std::vector<U8> two = frame();
        require_only_on("another caret", one, two, 2);
        view->setSelections(at(2, 5), { at(4, 0) });
        UNSCOPED_INFO("the two the other way round, the same frame");
        CHECK(frameDiff(frame(), two) == FrameDiff());

        const ALTextRange      span(ALTextPos(7, 2), ALTextPos(7, 10));
        view->setSelections(at(4, 0), { span });
        const std::vector<U8> other_span = frame();
        require_only_on("another selection, its caret at its end", one, other_span, 7);
        view->setSelections(span, { at(4, 0) });
        UNSCOPED_INFO("the same as the main selection");
        CHECK(frameDiff(frame(), other_span) == FrameDiff());

        // A caret on every line: the carets are one draw, not one each.
        std::vector<ALTextRange> many;
        for (S32 line = 1; line < 12; ++line)
        {
            many.push_back(at(line, line));
        }
        view->setSelections(at(0, 0), many);
        std::list<LLVertexBufferData> capture;
        gGL.beginList(&capture);
        view->draw();
        gGL.flush();
        gGL.endList();
        glFinish();
        const size_t with_many = capture.size();
        capture.clear();
        view->singleSelection();
        gGL.beginList(&capture);
        view->draw();
        gGL.flush();
        gGL.endList();
        glFinish();
        UNSCOPED_INFO("twelve carets cost what one does");
        CHECK(with_many == capture.size());
    }
    // A gap: its tint across the text and nothing else in it, the line
    // under it drawn as far down as the gap is tall, and the rows below
    // the text drawn too.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a gap is tinted across the text and moves the lines below it down", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "view";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = "MMMMMMMM\nMMMMMMMM\nMMMMMMMM";
        // The skin's colours are not loaded here: the text's is given, on
        // black.
        p.text_color   = LLUIColor(LLColor4::white);
        p.bg_visible   = false;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            view->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // How many pixels in a band of the view's rows, from a row's top
        // down so many rows, are lit in a channel over a floor.
        const S32  row_h  = view->layout().rowHeight();
        const LLRect text = view->textRect();
        const auto lit    = [&](const std::vector<U8>& rgba, S32 from_row, S32 rows, S32 channel, U8 floor) {
            S32 count = 0;
            for (S32 y = text.mTop - (from_row + rows) * row_h; y < text.mTop - from_row * row_h; ++y)
            {
                for (S32 x = text.mLeft; x < text.mRight; ++x)
                {
                    count += rgba[(static_cast<size_t>(y) * W + x) * 4 + channel] > floor ? 1 : 0;
                }
            }
            return count;
        };
        const std::vector<U8> before = frame();
        UNSCOPED_INFO("the second line's ink in its row");
        CHECK(lit(before, 1, 1, 0, 128) > 20);

        std::vector<ALTextView::LineAnnotation> lines(4);
        lines[1].gap     = 2;
        lines[1].gapTint = LLColor4(0.f, 0.f, 1.f, 1.f);
        lines[3].gap     = 1;
        lines[3].gapTint = LLColor4(0.f, 1.f, 0.f, 1.f);
        view->setLineAnnotations(lines);
        const std::vector<U8> after = frame();
        const S32             width = text.getWidth();
        UNSCOPED_INFO("the gap tinted across the text");
        CHECK(lit(after, 1, 2, 2, 200) >= width * (2 * row_h - 2));
        UNSCOPED_INFO("and nothing written in it");
        CHECK(lit(after, 1, 2, 0, 128) == 0);
        {
            INFO("the line under it as far down");
            CHECK(lit(after, 3, 1, 0, 128) > 20);
            CHECK(lit(after, 3, 1, 0, 128) == lit(before, 1, 1, 0, 128));
        }
        UNSCOPED_INFO("the first line where it was");
        CHECK(lit(after, 0, 1, 0, 128) == lit(before, 0, 1, 0, 128));
        {
            INFO("the rows below the text tinted");
            CHECK(lit(after, 5, 1, 1, 200) >= width * (row_h - 2));
            CHECK(lit(after, 6, 1, 1, 200) == 0);
        }
    }

    // What goes by row is cut at the text's edge: a code editor scrolled
    // half a row has its top row's number in the gutter cut there, not
    // drawn above the editor; and an atom's view on that row cut too.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a row's gutter number and atom view are cut at the text's top edge", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name              = "editor";
        p.rect              = LLRect(0, H / 2, W, 20);
        p.show_line_numbers = true;
        p.text_color        = LLUIColor(LLColor4::white);
        p.bg_visible        = false;
        std::string text;
        for (S32 n = 0; n < 40; ++n)
        {
            text += "MMMM " + std::to_string(n) + "\n";
        }
        ALCodeEditor* editor = keep(LLUICtrlFactory::create<ALCodeEditor>(p));
        editor->setFont(LLFontGL::getFontMonospace());
        editor->setText(text);
        const S32 row_h = editor->layout().rowHeight();
        const S32 above = editor->getRect().mTop;
        editor->setScrollY(row_h / 2);
        const std::vector<U8> frame = drawnAt(*editor);
        UNSCOPED_INFO("the gutter drawn");
        CHECK(drawnIn(frame, 0, W, editor->getRect().mBottom, above) > 0);
        UNSCOPED_INFO("nothing of it above the editor");
        CHECK(drawnIn(frame, 0, W, above, H) == 0);

        // An atom's view on the first line, which is half out of sight.
        LLView::Params bp(LLUICtrlFactory::getDefaultParams<LLView>());
        bp.name = "box";
        bp.rect = LLRect(0, row_h, 40, 0);
        ALTextView::Atom atom;
        atom.at    = ALTextPos(0, 0);
        atom.width = 40;
        atom.view  = new WhiteBox(bp);
        editor->setAtoms({ atom });
        editor->setScrollY(row_h / 2);
        editor->placeAtomViews();
        const std::vector<U8> with_atom = drawnAt(*editor);
        const LLRect          area      = editor->textRect();
        const S32             x0        = editor->getRect().mLeft + area.mLeft;
        UNSCOPED_INFO("the atom drawn, in what of its row is in sight");
        CHECK(drawnIn(with_atom, x0, x0 + 40, editor->getRect().mBottom + area.mTop - row_h / 2, editor->getRect().mBottom + area.mTop) > 0);
        // A clip takes in the row its top edge names, as a rect is drawn.
        UNSCOPED_INFO("nothing of it above the text");
        CHECK(drawnIn(with_atom, x0, x0 + 40, editor->getRect().mBottom + area.mTop + 1, H) == 0);
    }

    // The ruler's marks cost what one does, however many lines have one: a
    // comparison marks every line it changed, and a gap's mark with them.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: the ruler's marks cost what one does however many lines have one", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += "word word word word\n";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name           = "view";
        p.rect           = LLRect(0, H, W, 0);
        p.default_text   = text;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        ALTextRuler* ruler = view->findChild<ALTextRuler>("ruler");
        // A frame's draws, the ruler put in its place by a frame before it.
        const auto draws = [&]() {
            view->draw();
            gGL.flush();
            std::list<LLVertexBufferData> capture;
            gGL.beginList(&capture);
            view->draw();
            gGL.flush();
            gGL.endList();
            glFinish();
            return capture.size();
        };
        const LLColor4 changed(0.9f, 0.6f, 0.1f, 1.f);
        // The same gap in both, so that only the marks differ.
        std::vector<ALTextView::LineAnnotation> lines(200);
        lines[100].gap     = 2;
        lines[5].rulerTint = changed;
        view->setLineAnnotations(lines);
        const size_t one = draws();
        {
            INFO("the ruler beside the text");
            REQUIRE(ruler);
            CHECK(ruler->getVisible());
        }
        for (ALTextView::LineAnnotation& line : lines)
        {
            line.rulerTint = changed;
        }
        lines[100].gapRulerTint = changed;
        view->setLineAnnotations(lines);
        const size_t every = draws();
        UNSCOPED_INFO("two hundred marks and a gap's cost what one does");
        CHECK(every == one);
    }

    // A tint behind every line in sight and every gap costs what one does,
    // as a line under a link and a style on every line costs what one of
    // each does: a comparison's side tints most of its lines, and a log may
    // underline a name on each.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: tints and underlines on every line cost what one line's do", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 12; ++i)
        {
            text += "integer value = f(x, [y, z]);\n";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name           = "view";
        p.rect           = LLRect(0, H, W, 0);
        p.default_text   = text;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        // A frame's draws, laid out by a frame before it.
        const auto draws = [&]() {
            view->draw();
            gGL.flush();
            std::list<LLVertexBufferData> capture;
            gGL.beginList(&capture);
            view->draw();
            gGL.flush();
            gGL.endList();
            glFinish();
            return capture.size();
        };
        const LLColor4 tint(0.2f, 0.6f, 0.2f, 0.5f);
        // The same gaps in both, so that only the tints differ.
        std::vector<ALTextView::LineAnnotation> lines(12);
        lines[3].gap  = 1;
        lines[8].gap  = 1;
        lines[0].tint = tint;
        view->setLineAnnotations(lines);
        const size_t one_tint = draws();
        for (ALTextView::LineAnnotation& line : lines)
        {
            line.tint = tint;
        }
        lines[3].gapTint = tint;
        lines[8].gapTint = tint;
        view->setLineAnnotations(lines);
        const size_t every_tint = draws();
        UNSCOPED_INFO("a tint on every line and gap costs what one does");
        CHECK(every_tint == one_tint);
        view->setLineAnnotations({});

        // A style that underlines the first word, and a link always
        // underlined on the name after it.
        const auto underlined = [](S32 line, std::vector<ALTextView::Style>& styles, std::vector<ALTextView::Substitution>& links) {
            ALTextView::Style style;
            style.range = ALTextRange(ALTextPos(line, 0), ALTextPos(line, 7));
            style.flags = LLFontGL::UNDERLINE;
            styles.push_back(style);
            ALTextView::Substitution link;
            link.range     = ALTextRange(ALTextPos(line, 8), ALTextPos(line, 13));
            link.link      = true;
            link.underline = ALTextView::Substitution::Underline::Always;
            links.push_back(link);
        };
        std::vector<ALTextView::Style>        styles;
        std::vector<ALTextView::Substitution> links;
        underlined(0, styles, links);
        view->setStyles(styles);
        view->setSubstitutions(links);
        const size_t one_line = draws();
        for (S32 line = 1; line < 12; ++line)
        {
            underlined(line, styles, links);
        }
        view->setStyles(styles);
        view->setSubstitutions(links);
        const size_t every_line = draws();
        UNSCOPED_INFO("underlines on every line cost what one line's do");
        CHECK(every_line == one_line);
    }

    // The blocks the gutter draws its markers by are found again once the
    // text is lexed to its end: an edit that changes how every line after
    // it starts -- a string left open at the top -- has the lines in sight
    // lexed in the frame after it, and of the rest a slice, not the whole.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: fold blocks are found again once the text is lexed to its end", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text = "default\n{\n";
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "    integer x = 1;\n";
        }
        text += "}\n";
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name              = "editor";
        p.rect              = LLRect(0, H, W, 0);
        p.syntax            = "lsl";
        p.show_fold_markers = true;
        ALCodeEditor* editor = keep(LLUICtrlFactory::create<ALCodeEditor>(p));
        editor->setFont(LLFontGL::getFontMonospace());
        editor->setText(text);
        const S32  last  = editor->document().lineCount() - 1;
        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            editor->draw();
            gGL.flush();
            glFinish();
        };
        editor->highlighter().tokens(last);
        frame();
        UNSCOPED_INFO("the block");
        REQUIRE(editor->foldRegions().size() == size_t(1));

        editor->document().insert(ALTextPos(0, 0), "\"");
        frame();
        editor->highlighter().tokens(last);
        UNSCOPED_INFO("the frame after the edit left most of the text to lex");
        CHECK(editor->highlighter().lastLexed() > last / 2);
        frame();
        UNSCOPED_INFO("lexed to its end: the block gone");
        CHECK(editor->foldRegions().empty());
    }

    // What is being composed is underlined under its own text on each row
    // its line wraps it over: on the first, from where it starts to the
    // row's end; on the next, from the row's start to where it ends.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: preedit text is underlined under itself on each row it wraps over", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string words;
        for (S32 i = 0; i < 16; ++i)
        {
            words += "MMMM ";
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "view";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = words;
        // The skin's colours are not loaded here: the text's is given, on
        // black.
        p.text_color   = LLUIColor(LLColor4::white);
        p.bg_visible   = false;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        view->setWordWrap(true);
        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            view->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // Laid out by a frame, as it is drawn.
        frame();
        const std::vector<ALTextLayout::Row> rows = view->layout().line(0).rows;
        {
            INFO("the line wraps, three words or more to its first row");
            REQUIRE(rows.size() >= 2);
            CHECK(rows[0].end > 15);
        }
        // From the third word of the first row to the end of the first word
        // of the second.
        const S32 begin = 10;
        const S32 end   = rows[1].begin + 4;
        UNSCOPED_INFO("it ends inside the second row");
        CHECK(end < rows[1].end);
        const std::vector<U8> plain = frame();
        view->preeditor().markAsPreedit(begin, end - begin);
        CHECK(view->hasPreedit());
        const std::vector<U8> composed = frame();

        // The columns of a row's band in which anything changed between the
        // two frames, and how many of them lie in [from, to).
        const LLRect text    = view->textRect();
        const auto   columns = [&](size_t r) {
            std::vector<bool> out(static_cast<size_t>(W), false);
            const S32         top = text.mTop - (view->layout().lineTop(0) + rows[r].top - view->scrollY());
            for (S32 y = llmax(0, top - rows[r].height); y < llmin(H, top); ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
                    if (plain[i] != composed[i] || plain[i + 1] != composed[i + 1] || plain[i + 2] != composed[i + 2])
                    {
                        out[static_cast<size_t>(x)] = true;
                    }
                }
            }
            return out;
        };
        const auto changed_in = [](const std::vector<bool>& cols, S32 from, S32 to) {
            S32 found = 0;
            for (S32 x = llmax(0, from); x < llmin(W, to); ++x)
            {
                found += cols[static_cast<size_t>(x)] ? 1 : 0;
            }
            return found;
        };

        const S32               first_from = text.mLeft + static_cast<S32>(view->layout().xOf(0, begin));
        const S32               first_to   = text.mLeft + static_cast<S32>(rows[0].width);
        const std::vector<bool> first      = columns(0);
        UNSCOPED_INFO("nothing on the first row left of where it starts");
        CHECK(changed_in(first, 0, first_from) == 0);
        UNSCOPED_INFO("under most of its text to the first row's end");
        CHECK(changed_in(first, first_from, first_to) * 4 >= (first_to - first_from) * 3);
        UNSCOPED_INFO("nothing past the first row's end");
        CHECK(changed_in(first, first_to, W) == 0);

        const S32               second_from = text.mLeft + static_cast<S32>(view->layout().xOf(0, rows[1].begin));
        const S32               second_to   = text.mLeft + static_cast<S32>(view->layout().xOf(0, end));
        const std::vector<bool> second      = columns(1);
        UNSCOPED_INFO("under most of its text on the second row");
        CHECK(changed_in(second, second_from, second_to) * 4 >= (second_to - second_from) * 3);
        UNSCOPED_INFO("nothing on the second row left of its start");
        CHECK(changed_in(second, 0, second_from) == 0);
        UNSCOPED_INFO("nothing on the second row past where it ends");
        CHECK(changed_in(second, second_to, W) == 0);
    }

    // The preview of the lines under the mouse on the map numbers them as
    // the view does, from its first line's number: a notecard's from 0, an
    // expansion's from past its envelope.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: the map's preview numbers its lines from the view's line number base", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += "word word word word\n";
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name               = "editor";
        p.rect               = LLRect(0, H, W, 0);
        p.default_text       = text;
        ALCodeEditor* editor = keep(LLUICtrlFactory::create<ALCodeEditor>(p));
        editor->setFont(LLFontGL::getFontMonospace());
        // No gutter, so that only the preview says a line's number; read
        // only, so that no caret blinks between frames.
        editor->setShowLineNumbers(false);
        editor->setShowFoldMarkers(false);
        editor->setReadOnly(true);
        editor->setScrollMap(true);
        editor->setScrollMapWidth(60);
        editor->setBackgroundColor(LLColor4(0.1f, 0.1f, 0.12f, 1.f));
        editor->setTextColor(LLColor4(0.9f, 0.9f, 0.9f, 1.f));
        ALTextRuler* map = editor->findChild<ALTextRuler>("ruler");
        REQUIRE(map != nullptr);
        const auto drawn = [&]() {
            ll_test::uiGL().clearFramebuffer();
            glEnable(GL_BLEND);
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            editor->draw();
            gGL.flush();
            glDisable(GL_BLEND);
            glFinish();
            return ll_test::readFramebufferRGBA(W, H);
        };
        // How many pixels left of the map, where the preview is, differ
        // between two frames.
        const auto changed = [&](const std::vector<U8>& a, const std::vector<U8>& b) {
            S32 count = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W - 60; ++x)
                {
                    const size_t at = (static_cast<size_t>(y) * W + x) * 4;
                    count += a[at] != b[at] || a[at + 1] != b[at + 1] || a[at + 2] != b[at + 2];
                }
            }
            return count;
        };
        drawn();
        map->handleHover(30, H / 2, MASK_NONE);
        const std::vector<U8> from_one = drawn();
        editor->setLineNumberBase(1000);
        const std::vector<U8> from_base = drawn();
        UNSCOPED_INFO("numbered past a thousand, the preview says so");
        CHECK(changed(from_one, from_base) > 0);
        editor->setLineNumberBase(0);
        UNSCOPED_INFO("and from one again as before");
        CHECK(changed(from_one, drawn()) == 0);
    }

    // A gap put between the lines moves the map's rows below it down, each
    // line drawn with its own runs of text: the map is the one a view with
    // that gap from the start draws, though the text, and so the lines in
    // sight, are as they were. A text with no grammar, whose lines' tokens
    // never change.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a gap put between the lines moves the map's rows below it down", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // Lines of many lengths, so that one drawn with another's runs is
        // seen to be.
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += std::string(static_cast<size_t>(i % 9 + 1) * 4, 'x') + "\n";
        }
        std::vector<ALTextView::LineAnnotation> gapped(200);
        gapped[1].gap = 2;
        const auto make = [&]() {
            ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
            p.name           = "view";
            p.rect           = LLRect(0, H, W, 0);
            p.default_text   = text;
            ALTextView* made = keep(LLUICtrlFactory::create<ALTextView>(p));
            made->setFont(LLFontGL::getFontMonospace());
            made->setScrollMap(true);
            made->setScrollMapWidth(60);
            made->setBackgroundColor(LLColor4(0.1f, 0.1f, 0.12f, 1.f));
            made->setTextColor(LLColor4(0.9f, 0.9f, 0.9f, 1.f));
            return made;
        };
        // The map's columns of a frame of a view, drawn twice so that its
        // layout has settled.
        const auto map_of = [&](ALTextView* view) {
            std::vector<U8> map;
            for (S32 pass = 0; pass < 2; ++pass)
            {
                ll_test::uiGL().clearFramebuffer();
                glEnable(GL_BLEND);
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                view->draw();
                gGL.flush();
                glDisable(GL_BLEND);
                glFinish();
                const std::vector<U8> rgba = ll_test::readFramebufferRGBA(W, H);
                map.clear();
                for (S32 y = 0; y < H; ++y)
                {
                    for (S32 x = W - 60; x < W; ++x)
                    {
                        const size_t at = (static_cast<size_t>(y) * W + x) * 4;
                        map.insert(map.end(), rgba.begin() + static_cast<std::ptrdiff_t>(at), rgba.begin() + static_cast<std::ptrdiff_t>(at + 3));
                    }
                }
            }
            return map;
        };
        ALTextView* drawn_first = make();
        const std::vector<U8> before = map_of(drawn_first);
        drawn_first->setLineAnnotations(gapped);
        const std::vector<U8> after = map_of(drawn_first);
        UNSCOPED_INFO("the gap moved the map");
        CHECK(frameDiff(after, before, 60, 3).pixels > 0);
        ALTextView* gapped_first = make();
        gapped_first->setLineAnnotations(gapped);
        UNSCOPED_INFO("as a view with the gap from the start draws it");
        CHECK(frameDiff(after, map_of(gapped_first), 60, 3) == FrameDiff());
    }

    // The code editor's words beside the text -- an inlay's in its pill, a
    // line's note -- and the arrows for its tabs cost what one line's do,
    // however many lines have them: a script with a name before every
    // argument has a hint on most rows.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: hints and notes and tab arrows on every line cost what one line's do", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name   = "editor";
        p.rect   = LLRect(0, H, W, 0);
        p.syntax = "lsl";
        ALCodeEditor* editor = keep(LLUICtrlFactory::create<ALCodeEditor>(p));
        editor->setFont(LLFontGL::getFontMonospace());
        // Read-only, so that no caret blinks between frames.
        editor->setReadOnly(true);
        editor->setShowWhitespace(ALCodeEditor::Whitespace::All);
        // A frame's draws, laid out by a frame before it.
        const auto draws = [&]() {
            editor->draw();
            gGL.flush();
            std::list<LLVertexBufferData> capture;
            gGL.beginList(&capture);
            editor->draw();
            gGL.flush();
            gGL.endList();
            glFinish();
            return capture.size();
        };
        // Twelve calls, the first so many of them tabbed in, each of those
        // with a name before both its arguments and a note after it; the
        // words short, so that all of them are one batch of glyphs.
        const auto fill = [&](S32 tabbed) {
            std::string text;
            for (S32 line = 0; line < 12; ++line)
            {
                text += std::string(line < tabbed ? "\t" : "") + "f(0, v);\n";
            }
            editor->setText(text);
            std::vector<ALCodeEditor::InlayHint> hints;
            std::vector<ALCodeEditor::LineNote>  notes;
            for (S32 line = 0; line < tabbed; ++line)
            {
                ALCodeEditor::InlayHint first;
                first.at   = ALTextPos(line, 3);
                first.text = "c:";
                ALCodeEditor::InlayHint second;
                second.at   = ALTextPos(line, 6);
                second.text = "m:";
                hints.push_back(first);
                hints.push_back(second);
                notes.push_back({ line, "9b", "" });
            }
            editor->setInlayHints(hints);
            editor->setLineNotes(notes);
        };
        fill(1);
        const size_t one = draws();
        fill(12);
        const size_t every = draws();
        UNSCOPED_INFO("hints, notes and tabs on every line cost what one line's do");
        CHECK(every == one);
    }

    // A selection that goes on past a line's end is drawn a little past the
    // line's last glyph, as a match that does is: as far however far in the
    // text's first line starts.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a selection past a line's end is drawn a little past its last glyph", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "view";
        p.rect         = LLRect(0, H, W, 0);
        p.default_text = "MMMM\nMMMM\nMMMM";
        // The skin's colours are not loaded here: the text's is given, on
        // black.
        p.text_color   = LLUIColor(LLColor4::white);
        p.bg_visible   = false;
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        view->setSelectionColor(LLUIColor(LLColor4(0.f, 0.f, 1.f, 1.f)));
        // The first line in from the edge, as a log's are.
        view->layout().setIndentProvider([](S32 line) {
            ALTextLayout::Indent indent;
            indent.first = line == 0 ? 60.f : 0.f;
            return indent;
        });
        view->setSelection(ALTextRange(ALTextPos(1, 2), ALTextPos(2, 0)));
        ll_test::uiGL().clearFramebuffer();
        glEnable(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        view->draw();
        gGL.flush();
        glDisable(GL_BLEND);
        glFinish();
        const std::vector<U8> rgba = ll_test::readFramebufferRGBA(W, H);
        // Across the middle of the second line's row, from its last glyph
        // on: how many columns the band reaches.
        const LLRect text = view->textRect();
        const S32    y    = text.mTop - (view->layout().lineTop(1) - view->scrollY()) - view->layout().rowHeight() / 2;
        const S32    end  = text.mLeft + static_cast<S32>(std::ceil(view->layout().line(1).width));
        S32          past = 0;
        for (S32 x = end; x < W; ++x)
        {
            past += rgba[(static_cast<size_t>(y) * W + x) * 4 + 2] > 128 ? 1 : 0;
        }
        UNSCOPED_INFO("drawn past the line's end");
        CHECK(past > 0);
        UNSCOPED_INFO("a little, not as far again as the first line is in");
        CHECK(past <= 10);
    }

    // The band a modal keymap has under the text is read and measured once
    // while nothing moves, and again once the fonts are loaded again under
    // the same font: its caret and its choices are measured in the faces
    // its text is drawn in.
    TEST_CASE_METHOD(altextview_gl_data, "altextview_gl: a modal keymap's band is read once and again after a font reload", "[altextview_gl]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get(/*gl_textures=*/true);
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name           = "view";
        p.rect           = LLRect(0, H, W, 0);
        p.default_text   = "text";
        ALTextView* view = keep(LLUICtrlFactory::create<ALTextView>(p));
        view->setFont(LLFontGL::getFontMonospace());
        S32  asked  = 0;
        auto keymap = std::make_unique<TypingKeymap>();
        keymap->asked = &asked;
        view->setModalKeymap(std::move(keymap));
        const auto frame = [&]() {
            ll_test::uiGL().clearFramebuffer();
            view->draw();
            gGL.flush();
            glFinish();
        };
        frame();
        const S32 once = asked;
        UNSCOPED_INFO("read for the first frame");
        CHECK(once > 0);
        frame();
        UNSCOPED_INFO("not again while nothing moved");
        CHECK(asked == once);
        // What a font reload does: the faces swapped under the same font;
        // put back however the case ends, for the views of the cases after.
        struct Generation
        {
            const S32 was = LLFontGL::sResolutionGeneration;
            ~Generation() { LLFontGL::sResolutionGeneration = was; }
        } generation;
        ++LLFontGL::sResolutionGeneration;
        frame();
        UNSCOPED_INFO("read again once the fonts were loaded again");
        CHECK(asked > once);
    }
}
