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

#include "llglheaders.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include "alcodeeditor.h"
#include "aldiffview.h"
#include "../llfocusmgr.h"
#include "../llui.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

class LLAvatarName;
const std::string gDiffGLTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gDiffGLTestAnonName;
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

    // The view drawn over black, read back: RGBA, bottom row first.
    std::vector<U8> drawn(LLView& view)
    {
        gl().clearFramebuffer();
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

namespace tut
{
    struct aldiffview_gl_data
    {
        ALDiffView* view = nullptr;

        aldiffview_gl_data()
        {
            gl();
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

        // Forty lines, the third changed and the thirty-first, a line put
        // in after the fifth: the run between folded. The sides in colours
        // of their own, which the headless colour table has none of.
        ALDiffView& make()
        {
            // The fonts' glyphs in textures, once there is a context.
            if (!ll_test::HeadlessUI::get(/*gl_textures=*/true).ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            std::string left;
            std::string right;
            for (S32 i = 0; i < 40; ++i)
            {
                left += "line " + std::to_string(i) + "\n";
                right += (i == 2 ? std::string("two") : i == 30 ? std::string("thirty") : "line " + std::to_string(i)) + "\n";
                if (i == 4)
                {
                    right += "added\n";
                }
            }
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
    typedef test_group<aldiffview_gl_data> aldiffview_gl_group;
    typedef aldiffview_gl_group::object    aldiffview_gl_object;
    aldiffview_gl_group                    aldiffview_gl_instance("aldiffview_gl");

    template<> template<>
    void aldiffview_gl_object::test<1>()
    {
        set_test_name("the change the caret is in is outlined on each side, across the text; none while it is in none");
        ALDiffView& d = make();
        d.right()->setFocus(true);
        d.goToChange(true);
        ensure_equals("in the first change", d.changeAtCaret(), 0);
        const std::vector<U8> outlined = drawn(d);
        for (ALCodeEditor* side : { d.left(), d.right() })
        {
            const Band b     = band(side, 2, 2);
            const S32  width = b.right - b.left - 8;
            const S32  edge  = llmax(count(outlined, b.left + 4, b.right - 4, b.top - 2, b.top + 1, yellow),
                                     count(outlined, b.left + 4, b.right - 4, b.bottom - 1, b.bottom + 2, yellow));
            ensure("its edge drawn across the text, on " + side->getName(), edge >= width / 2);
        }

        d.right()->goTo(ALTextPos(0, 0));
        ensure_equals("in none", d.changeAtCaret(), -1);
        const std::vector<U8> plain = drawn(d);
        const Band            b     = band(d.right(), 2, 2);
        ensure("no edge", count(plain, b.left + 4, b.right - 4, b.top - 2, b.top + 1, yellow) < (b.right - b.left) / 4);
    }

    template<> template<>
    void aldiffview_gl_object::test<2>()
    {
        set_test_name("a folded run's row, a gap above the line after it, has how many it stands for said over it; opened, it is gone");
        ALDiffView& d = make();
        ensure_equals("one run folded", d.foldedCount(), 1);
        // The right's lines: 0-1, the change at 2, 3-4, the line put in at
        // 5, context 6-8, the run 9-27, and its row the gap above 28.
        const S32      under  = 28;
        ALTextLayout&  layout = d.right()->layout();
        ensure("the row a gap", layout.gapRows(under) == 1 && layout.hidden(9) && layout.hidden(27));
        Band b      = band(d.right(), under, under);
        b.bottom    = b.top;
        b.top       = b.top + layout.gapHeight(under);
        const std::vector<U8> folded = drawn(d);
        ensure("words over it", count(folded, b.left, b.right, b.bottom + 1, b.top - 1, inked) > 20);
        d.right()->goTo(ALTextPos(12, 0));
        ensure_equals("opened", d.foldedCount(), 0);
        ensure("its row gone", layout.gapRows(under) == 0 && !layout.hidden(12));
    }

    template<> template<>
    void aldiffview_gl_object::test<3>()
    {
        set_test_name("ranges bracketed across a wider gap: a band from a line on the left to the three on the right it became; none for a pair level line for line");
        ALDiffView& d = make();
        const S32   narrow = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        // An LSL line that became three of SLua, then one that became one.
        d.setTexts("default\n{\n    if (a) b();\n    c();\n}", "if a then\n    b()\nend\nc()", { { 2, 2, 0, 2 }, { 3, 3, 3, 3 } });
        const S32 gap = d.right()->getRect().mLeft - d.left()->getRect().mRight;
        ensure("the gap wider", gap > narrow);
        const std::vector<U8> drawn_now = drawn(d);
        // The gap's columns, inside its edges, down the rows of each range.
        const S32  from   = d.left()->getRect().mRight + 1;
        const S32  to     = d.right()->getRect().mLeft - 1;
        // The band's ground is faint over the black it is drawn on.
        const auto lit    = [](U8 r, U8 g, U8 b) { return r > 8 || g > 8 || b > 8; };
        const Band block  = band(d.right(), 0, 2);
        const Band single = band(d.right(), 3, 3);
        ensure("a band beside the if's three lines", count(drawn_now, from, to, block.bottom + 2, block.top - 2, lit) > (to - from) * 4);
        ensure("nothing beside the call level with its own", count(drawn_now, from, to, single.bottom + 2, single.top - 2, lit) == 0);
        d.setTexts("a();\nb();", "a()\nb()", { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } });
        ensure("ranges all level line for line: no bands, the gap as narrow as ever", d.right()->getRect().mLeft - d.left()->getRect().mRight == narrow);
    }

    template<> template<>
    void aldiffview_gl_object::test<4>()
    {
        set_test_name("the caret on an LSL line washes the SLua it became, an edge down its left, and the line's own rows; none where it is in no range");
        ALDiffView& d = make();
        d.setTexts("default\n{\n    if (a) b();\n    c();\n}", "if a then\n    b()\nend\nc()", { { 2, 2, 0, 2 }, { 3, 3, 3, 3 } });
        d.left()->setFocus(true);
        d.left()->goTo(ALTextPos(2, 0));
        ensure_equals("linked to the if", d.linkedRange(), 0);
        const std::vector<U8> linked = drawn(d);
        // The edge's blue over the sides' dark ground.
        const auto blue  = [](U8 r, U8 g, U8 b) { return b > 150 && b > r + 40; };
        const Band block = band(d.right(), 0, 2);
        const Band after = band(d.right(), 3, 3);
        // Inside the change's outline, which the caret's line is also in.
        const S32 edge = block.left + ALDiffView::LINKED_INSET;
        ensure("an edge down the three SLua lines", count(linked, edge, edge + ALDiffView::LINKED_EDGE, block.bottom + 1, block.top - 1, blue) >= (block.top - block.bottom - 2));
        ensure("none beside the call after", count(linked, edge, edge + ALDiffView::LINKED_EDGE, after.bottom + 1, after.top - 1, blue) == 0);
        const Band own = band(d.left(), 2, 2);
        ensure("and down the LSL line's own row", count(linked, own.left + ALDiffView::LINKED_INSET, own.left + ALDiffView::LINKED_INSET + ALDiffView::LINKED_EDGE,
                                                        own.bottom + 1, own.top - 1, blue) > 0);
        d.left()->goTo(ALTextPos(0, 0));
        ensure_equals("in none", d.linkedRange(), -1);
        const std::vector<U8> plain = drawn(d);
        ensure("no edge", count(plain, edge, edge + ALDiffView::LINKED_EDGE, block.bottom + 1, block.top - 1, blue) == 0);
    }

    template<> template<>
    void aldiffview_gl_object::test<5>()
    {
        set_test_name("a merge's conflict edged down its rows on each side; ours's own change not");
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
        ensure("no edge before a merge", count(drawn(d), right.left, right.left + edge, right.bottom + 1, right.top - 1, orange) == 0);
        d.setMergeBase(base);
        const std::vector<U8> merged = drawn(d);
        ensure("down the conflict on the right", count(merged, right.left, right.left + edge, right.bottom + 1, right.top - 1, orange) >= right.top - right.bottom - 2);
        ensure("and on the left", count(merged, left.left, left.left + edge, left.bottom + 1, left.top - 1, orange) >= left.top - left.bottom - 2);
        ensure("not down ours's own change", count(merged, own.left, own.left + edge, own.bottom + 1, own.top - 1, orange) == 0);
    }

    template<> template<>
    void aldiffview_gl_object::test<6>()
    {
        set_test_name("the bands in sight drawn in two batches however many there are: their grounds in one, their edges in the other");
        ALDiffView& d = make();
        // Eight lines the same on each side, ranges of two lines each, level:
        // a band each, which line up nothing the lines would not, so the
        // sides are drawn alike whatever bands there are.
        std::string text;
        for (S32 n = 0; n < 8; ++n)
        {
            text += (n ? "\n" : "") + ("same " + std::to_string(n));
        }
        // How many draws the comparison makes, drawn once already: what is
        // put in a texture the first time it is drawn, put in.
        const auto draws = [&](const ALTextDiff::ranges_t& ranges) {
            d.setTexts(text, text, ranges);
            drawn(d);
            gGL.pushUIMatrix();
            const U32 before = LLRender::sUICalls;
            drawn(d);
            const U32 made = LLRender::sUICalls - before;
            gGL.popUIMatrix();
            return made;
        };
        const U32 two  = draws({ { 0, 1, 0, 1 }, { 4, 5, 4, 5 } });
        const U32 four = draws({ { 0, 1, 0, 1 }, { 2, 3, 2, 3 }, { 4, 5, 4, 5 }, { 6, 7, 6, 7 } });
        ensure("drawn", two > 0);
        ensure_equals("as many draws for four bands as for two", four, two);
    }
}
