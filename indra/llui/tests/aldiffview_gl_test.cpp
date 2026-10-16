/**
 * @file aldiffview_gl_test.cpp
 * @brief What a comparison draws over its sides, read back from llrender's hidden window.
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

#include "llfonttextcache.h"
#include "llglheaders.h"

#include "aluigl_fixture.h"

#include "alcodeeditor.h"
#include "aldiffview.h"
#include "../llfocusmgr.h"
#include "../llui.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr S32 W = ll_test::HeadlessGL::WIDTH;
    constexpr S32 H = ll_test::HeadlessGL::HEIGHT;

    // The view drawn over black, read back: RGBA, bottom row first.
    std::vector<U8> drawn(LLView& view)
    {
        ll_test::uiGL().clearFramebuffer();
        glEnable(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        view.draw();
        gGL.flush();
        glDisable(GL_BLEND);
        glFinish();
        return ll_test::readFramebufferRGBA(W, H);
    }

    // How many pixels of a band, rows [bottom, top] and columns [left,
    // right), are as asked.
    template <class Test>
    S32 count(const std::vector<U8>& rgba, S32 left, S32 right, S32 bottom, S32 top, Test test)
    {
        S32 found = 0;
        for (S32 y = llmax(0, bottom); y <= llmin(H - 1, top); ++y)
        {
            for (S32 x = llmax(0, left); x < llmin(W, right); ++x)
            {
                const U8* px = &rgba[(static_cast<size_t>(y) * W + x) * 4];
                found += test(px[0], px[1], px[2]) ? 1 : 0;
            }
        }
        return found;
    }

    // The outline's yellow, over the sides' dark ground and a change's
    // tint; the text's grey.
    bool yellow(U8 r, U8 g, U8 b) { return r > 150 && g > 150 && b < 90; }
    bool inked(U8 r, U8 g, U8 b) { return r > 110 && g > 110 && b > 110; }
}

namespace
{
    struct aldiffview_gl_data
    {
        ALDiffView* view = nullptr;

        aldiffview_gl_data()
        {
            ll_test::uiGL();
            // What a rect without a texture of its own is drawn with.
            ll_test::installWhiteTexture();
        }

        ~aldiffview_gl_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            if (view)
            {
                view->die();
            }
        }

        // Forty lines, the third changed and the thirty-first -- or the one
        // asked for -- and a line put in after the fifth: the run between
        // folded.
        static std::pair<std::string, std::string> texts(S32 changed = 30)
        {
            std::string left;
            std::string right;
            for (S32 i = 0; i < 40; ++i)
            {
                left += "line " + std::to_string(i) + "\n";
                right += (i == 2 ? std::string("two") : i == changed ? std::string("thirty") : "line " + std::to_string(i)) + "\n";
                if (i == 4)
                {
                    right += "added\n";
                }
            }
            return { left, right };
        }

        // The texts above, the sides in colours of their own, which the
        // headless colour table has none of.
        ALDiffView& make()
        {
            // The fonts' glyphs in textures, once there is a context.
            if (!ll_test::HeadlessUI::get(/*gl_textures=*/true).ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            const auto [left, right] = texts();
            ALCodeEditor::Params side(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            const LLColor4       ink(0.85f, 0.85f, 0.85f, 1.f);
            const LLColor4       ground(0.12f, 0.12f, 0.14f, 1.f);
            side.text_color          = ink;
            side.text_readonly_color = ink;
            side.bg_color            = ground;
            side.bg_readonly_color   = ground;
            side.bg_focus_color      = ground;
            side.cursor_color        = LLColor4(1.f, 1.f, 0.f, 1.f);
            side.gutter_color        = LLColor4(0.16f, 0.16f, 0.18f, 1.f);
            side.line_number_color   = LLColor4(0.5f, 0.5f, 0.55f, 1.f);
            side.word_wrap           = false;
            ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
            p.name   = "diff";
            p.rect   = LLRect(0, H, W, 0);
            p.syntax = "lsl";
            p.side   = side;
            view     = LLUICtrlFactory::create<ALDiffView>(p);
            view->setFont(LLFontGL::getFontMonospace());
            view->setTexts(left, right);
            return *view;
        }

        // A row of a side, in the window: its top and its bottom, and the
        // text's left and right.
        struct Band
        {
            S32 top    = 0;
            S32 bottom = 0;
            S32 left   = 0;
            S32 right  = 0;
        };
        static Band band(ALCodeEditor* side, S32 first, S32 last)
        {
            const LLRect frame = side->getRect();
            const LLRect text  = side->textRect();
            const S32    top   = frame.mBottom + text.mTop;
            Band         b;
            b.top    = top - (side->layout().lineTop(first) - side->scrollY());
            b.bottom = top - (side->layout().lineTop(last) + side->layout().lineHeight(last) - side->scrollY());
            b.left   = frame.mLeft + text.mLeft;
            b.right  = frame.mLeft + text.mRight;
            return b;
        }
    };

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: the change holding the caret is outlined across each side", "[aldiffview_gl]")
    {
        ALDiffView& d = make();
        d.right()->setFocus(true);
        d.goToChange(true);
        UNSCOPED_INFO("in the first change");
        CHECK(d.changeAtCaret() == 0);
        const std::vector<U8> outlined = drawn(d);
        for (ALCodeEditor* side : { d.left(), d.right() })
        {
            const Band b     = band(side, 2, 2);
            const S32  width = b.right - b.left - 8;
            const S32  edge  = llmax(count(outlined, b.left + 4, b.right - 4, b.top - 2, b.top + 1, yellow),
                                     count(outlined, b.left + 4, b.right - 4, b.bottom - 1, b.bottom + 2, yellow));
            UNSCOPED_INFO("its edge drawn across the text on " << side->getName());
            REQUIRE(edge >= width / 2);
        }

        d.right()->goTo(ALTextPos(0, 0));
        UNSCOPED_INFO("in none");
        CHECK(d.changeAtCaret() == -1);
        const std::vector<U8> plain = drawn(d);
        const Band            b     = band(d.right(), 2, 2);
        UNSCOPED_INFO("no edge");
        CHECK(count(plain, b.left + 4, b.right - 4, b.top - 2, b.top + 1, yellow) < (b.right - b.left) / 4);
    }

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: a folded run's gap row says how many lines it stands for and goes once the run opens", "[aldiffview_gl]")
    {
        ALDiffView& d = make();
        CHECK(d.foldedCount() == 1);
        // The right's lines: 0-1, the change at 2, 3-4, the line put in at
        // 5, context 6-8, the run 9-27, and its row the gap above 28.
        const S32      under  = 28;
        ALTextLayout&  layout = d.right()->layout();
        {
            INFO("the row a gap");
            CHECK(layout.gapRows(under) == 1);
            CHECK(layout.hidden(9));
            CHECK(layout.hidden(27));
        }
        Band b      = band(d.right(), under, under);
        b.bottom    = b.top;
        b.top       = b.top + layout.gapHeight(under);
        const std::vector<U8> folded = drawn(d);
        UNSCOPED_INFO("words over it");
        CHECK(count(folded, b.left, b.right, b.bottom + 1, b.top - 1, inked) > 20);

        // The run a line longer, its row the gap above the line after it:
        // the words over that row say another count, so they differ.
        const S32 lower = under + 1;
        {
            const auto [left, right] = texts(31);
            d.setTexts(left, right);
        }
        CHECK(layout.gapRows(lower) == 1);
        Band c   = band(d.right(), lower, lower);
        c.bottom = c.top;
        c.top    = c.top + layout.gapHeight(lower);
        const std::vector<U8> longer = drawn(d);
        // Pixel for pixel up the two rows from their bottoms.
        S32 differing = 0;
        for (S32 up = 1; up < b.top - b.bottom; ++up)
        {
            const S32 was = b.bottom + up;
            const S32 now = c.bottom + up;
            if (was < 0 || was >= H || now < 0 || now >= H)
            {
                continue;
            }
            for (S32 x = llmax(0, b.left); x < llmin(W, b.right); ++x)
            {
                const size_t i = (static_cast<size_t>(was) * W + x) * 4;
                const size_t j = (static_cast<size_t>(now) * W + x) * 4;
                differing += folded[i] != longer[j] || folded[i + 1] != longer[j + 1] || folded[i + 2] != longer[j + 2] ? 1 : 0;
            }
        }
        UNSCOPED_INFO("another count over a run a line longer");
        CHECK(differing > 0);
        {
            const auto [left, right] = texts();
            d.setTexts(left, right);
        }

        d.right()->goTo(ALTextPos(12, 0));
        UNSCOPED_INFO("opened");
        CHECK(d.foldedCount() == 0);
        {
            INFO("its row gone");
            CHECK(layout.gapRows(under) == 0);
            CHECK_FALSE(layout.hidden(12));
        }
    }

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: a bracketed range draws a band across a wider gap between the sides", "[aldiffview_gl]")
    {
        ALDiffView& d = make();
        const S32   narrow = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        // An LSL line that became three of SLua, then one that became one.
        d.setTexts("default\n{\n    if (a) b();\n    c();\n}", "if a then\n    b()\nend\nc()", { { 2, 2, 0, 2 }, { 3, 3, 3, 3 } });
        const S32 gap = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        CHECK(gap > narrow);
        const std::vector<U8> drawn_now = drawn(d);
        // The gap's columns, inside its edges, down the rows of each range.
        const S32  from   = d.left()->getRect().mRight + 1;
        const S32  to     = d.right()->getRect().mLeft - 1;
        // The band's ground is faint over the black it is drawn on.
        const auto lit    = [](U8 r, U8 g, U8 b) { return r > 8 || g > 8 || b > 8; };
        const Band block  = band(d.right(), 0, 2);
        const Band single = band(d.right(), 3, 3);
        UNSCOPED_INFO("a band beside the if's three lines");
        CHECK(count(drawn_now, from, to, block.bottom + 2, block.top - 2, lit) > (to - from) * 4);
        UNSCOPED_INFO("nothing beside the call level with its own");
        CHECK(count(drawn_now, from, to, single.bottom + 2, single.top - 2, lit) == 0);
        d.setTexts("a();\nb();", "a()\nb()", { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } });
        UNSCOPED_INFO("ranges all level line for line: no bands, the gap as narrow as ever");
        CHECK(d.right()->getRect().mLeft - d.left()->getRect().mRight == narrow);
    }

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: the caret on an LSL line edges the SLua it became and its own row", "[aldiffview_gl]")
    {
        ALDiffView& d = make();
        d.setTexts("default\n{\n    if (a) b();\n    c();\n}", "if a then\n    b()\nend\nc()", { { 2, 2, 0, 2 }, { 3, 3, 3, 3 } });
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(2, 0));
        UNSCOPED_INFO("linked to the if");
        CHECK(d.linkedRange() == 0);
        const std::vector<U8> linked = drawn(d);
        // The edge's blue over the sides' dark ground.
        const auto blue  = [](U8 r, U8 g, U8 b) { return b > 150 && b > r + 40; };
        const Band block = band(d.right(), 0, 2);
        const Band after = band(d.right(), 3, 3);
        // Inside the change's outline, which the caret's line is also in.
        const S32 edge = block.left + ALDiffView::LINKED_INSET;
        UNSCOPED_INFO("an edge down the three SLua lines");
        CHECK(count(linked, edge, edge + ALDiffView::LINKED_EDGE, block.bottom + 1, block.top - 1, blue) >= (block.top - block.bottom - 2));
        UNSCOPED_INFO("none beside the call after");
        CHECK(count(linked, edge, edge + ALDiffView::LINKED_EDGE, after.bottom + 1, after.top - 1, blue) == 0);
        const Band own = band(d.left(), 2, 2);
        UNSCOPED_INFO("and down the LSL line's own row");
        CHECK(count(linked, own.left + ALDiffView::LINKED_INSET, own.left + ALDiffView::LINKED_INSET + ALDiffView::LINKED_EDGE,
                                                        own.bottom + 1, own.top - 1, blue) > 0);
        d.left()->goTo(ALTextPos(0, 0));
        UNSCOPED_INFO("in none");
        CHECK(d.linkedRange() == -1);
        const std::vector<U8> plain = drawn(d);
        UNSCOPED_INFO("no edge");
        CHECK(count(plain, edge, edge + ALDiffView::LINKED_EDGE, block.bottom + 1, block.top - 1, blue) == 0);
    }

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: a merge conflict is edged on each side but ours's own change is not", "[aldiffview_gl]")
    {
        ALDiffView&       d      = make();
        const std::string base   = "zero\none\ntwo\nthree\nfour\nfive\nsix\nseven";
        const std::string theirs = "zero\none\ntwo\nthree\nfour\nfive\nsix theirs\nseven";
        const std::string ours   = "zero\none\ntwo mine\nthree\nfour\nfive\nsix mine\nseven";
        d.setFoldSame(false);
        d.setTexts(theirs, ours);
        d.right()->setFocus(true);
        d.right()->goTo(ALTextPos(0, 0));
        const auto orange = [](U8 r, U8 g, U8 b) { return r > 200 && g > 110 && g < 180 && b < 70; };
        const Band right  = band(d.right(), 6, 6);
        const Band left   = band(d.left(), 6, 6);
        const Band own    = band(d.right(), 2, 2);
        const S32  edge   = ALDiffView::CONFLICT_EDGE;
        UNSCOPED_INFO("no edge before a merge");
        CHECK(count(drawn(d), right.left, right.left + edge, right.bottom + 1, right.top - 1, orange) == 0);
        d.setMergeBase(base);
        const std::vector<U8> merged = drawn(d);
        UNSCOPED_INFO("down the conflict on the right");
        CHECK(count(merged, right.left, right.left + edge, right.bottom + 1, right.top - 1, orange) >= right.top - right.bottom - 2);
        UNSCOPED_INFO("and on the left");
        CHECK(count(merged, left.left, left.left + edge, left.bottom + 1, left.top - 1, orange) >= left.top - left.bottom - 2);
        UNSCOPED_INFO("not down ours's own change");
        CHECK(count(merged, own.left, own.left + edge, own.bottom + 1, own.top - 1, orange) == 0);
    }

    TEST_CASE_METHOD(aldiffview_gl_data, "aldiffview_gl: range bands draw in the same number of batches however many there are", "[aldiffview_gl]")
    {
        ALDiffView& d      = make();
        const S32   narrow = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        // Eight lines the same on each side, ranges of two lines each, level:
        // a band each, which line up nothing the lines would not, so the
        // sides are drawn alike whatever bands there are.
        std::string text;
        for (S32 n = 0; n < 8; ++n)
        {
            text += (n ? "\n" : "") + ("same " + std::to_string(n));
        }
        // How many draws a frame of the comparison makes, and what it drew.
        std::vector<U8> shown;
        const auto      frame = [&]() {
            gGL.pushUIMatrix();
            const U32 before = LLRender::sUICalls;
            shown            = drawn(d);
            const U32 made   = LLRender::sUICalls - before;
            gGL.popUIMatrix();
            return made;
        };
        // How many a frame makes once no text in it is put together anew:
        // drawn until a frame rebuilds none, at most eight times. A frame
        // that adds glyphs to the font's cache moves it under every text
        // drawn before them, and each of those is rebuilt on the next frame,
        // so a frame can make as many draws as the one before and still not
        // be the frame that settles.
        const auto draws = [&](const ALTextDiff::ranges_t& ranges) {
            d.setTexts(text, text, ranges);
            for (S32 tries = 0; tries < 8; ++tries)
            {
                const U64 rebuilt = LLFontTextCache::regenCount();
                const U32 made    = frame();
                if (LLFontTextCache::regenCount() == rebuilt)
                {
                    return made;
                }
            }
            FAIL("no frame drew without rebuilding a text");
            return 0U;
        };
        // The bands drawn in the frame counted: the gap widened for them, and
        // its columns lit beside each band's rows.
        const auto lit    = [](U8 r, U8 g, U8 b) { return r > 8 || g > 8 || b > 8; };
        const auto banded = [&](const ALTextDiff::ranges_t& ranges) {
            UNSCOPED_INFO("the gap widened for the bands");
            CHECK(d.right()->getRect().mLeft - d.left()->getRect().mRight > narrow);
            const S32 from = d.left()->getRect().mRight + 1;
            const S32 to   = d.right()->getRect().mLeft - 1;
            for (const ALTextDiff::Range& range : ranges)
            {
                const Band rows = band(d.right(), range.rightFirst, range.rightLast);
                UNSCOPED_INFO("a band beside the right's lines from " << range.rightFirst);
                CHECK(count(shown, from, to, rows.bottom + 2, rows.top - 2, lit) > (to - from) * 4);
            }
        };
        const ALTextDiff::ranges_t two_bands  = { { 0, 1, 0, 1 }, { 4, 5, 4, 5 } };
        const ALTextDiff::ranges_t four_bands = { { 0, 1, 0, 1 }, { 2, 3, 2, 3 }, { 4, 5, 4, 5 }, { 6, 7, 6, 7 } };
        const U32                  two        = draws(two_bands);
        banded(two_bands);
        const U32 four = draws(four_bands);
        banded(four_bands);
        CHECK(two > 0);
        UNSCOPED_INFO("as many draws for four bands as for two");
        CHECK(four == two);
    }
}
