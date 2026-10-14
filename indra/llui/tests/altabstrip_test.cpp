/**
 * @file altabstrip_test.cpp
 * @brief A row of tabs over what a window shows, one per thing it holds.
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

#include "altabstrip.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "llfontfreetype.h"
#include "llfontgl.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct altabstrip_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        static constexpr S32 WIDTH = 300;
        static constexpr S32 HEIGHT = 24;

        static ALTabStrip* make(S32 width = WIDTH)
        {
            ALTabStrip::Params p(LLUICtrlFactory::getDefaultParams<ALTabStrip>());
            p.name = "tabs";
            p.rect = LLRect(0, HEIGHT, width, 0);
            return LLUICtrlFactory::create<ALTabStrip>(p);
        }

        static ALTabStrip::Tab tab(const std::string& label, const std::string& value)
        {
            ALTabStrip::Tab t;
            t.label = label;
            t.value = value;
            return t;
        }

        static std::vector<ALTabStrip::Tab> three()
        {
            return { tab("floater_a.xml", "a"), tab("floater_b.xml", "b"), tab("panel_c.xml", "c") };
        }
    };

    // The tabs run left to right in the order they were given, each inside
    // the strip and none over another, and the chosen one is the one asked.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: tabs run left to right in their order and inside the strip", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "b");
        REQUIRE(strip->tabs().size() == 3u);
        CHECK(strip->chosen() == std::string("b"));

        S32 right = -1;
        for (size_t i = 0; i < 3; ++i)
        {
            const LLRect r = strip->rectOf(i);
            CAPTURE(i);
            UNSCOPED_INFO("starts after the one before");
            REQUIRE(r.mLeft > right);
            UNSCOPED_INFO("and ends inside the strip");
            REQUIRE(r.mRight <= WIDTH);
            {
                INFO("and is as tall as it");
                REQUIRE(r.mTop == HEIGHT);
                REQUIRE(r.mBottom == 0);
            }
            UNSCOPED_INFO("and wide enough to read");
            REQUIRE(r.getWidth() >= 64);
            right = r.mRight;
        }
        // A point in each tab answers that tab.
        for (size_t i = 0; i < 3; ++i)
        {
            const LLRect r = strip->rectOf(i);
            CAPTURE(i);
            UNSCOPED_INFO("the tab under its own middle");
            REQUIRE(strip->at(r.getCenterX(), r.getCenterY()) == (S32)i);
        }
        // Past the last tab, with room to be past it, nothing.
        strip->reshape(WIDTH * 2, HEIGHT);
        UNSCOPED_INFO("and nothing past the last");
        CHECK(strip->at(WIDTH * 2 - 1, HEIGHT / 2) == -1);
        strip->die();
    }

    // A press on a tab chooses it and says so; a press on the way out says
    // that instead, and chooses nothing. The way out is at the right end of
    // the tab the pointer is over.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a press chooses a tab; one on its way out asks for it to close", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "a");

        std::vector<std::string> chosen;
        std::vector<std::string> closed;
        strip->onChosen([&chosen](const std::string& value) { chosen.push_back(value); });
        strip->onClosed([&closed](const std::string& value) { closed.push_back(value); });

        const LLRect second = strip->rectOf(1);
        strip->handleHover(second.getCenterX(), second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a press on a tab is handled");
        CHECK(strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE));
        UNSCOPED_INFO("and chooses it");
        CHECK(strip->chosen() == std::string("b"));
        UNSCOPED_INFO("saying so once");
        REQUIRE(chosen.size() == 1u);
        CHECK(chosen.front() == std::string("b"));
        UNSCOPED_INFO("and closes nothing");
        REQUIRE(closed.empty());

        // Pressing the tab already chosen says nothing new.
        strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("chosen once is chosen");
        REQUIRE(chosen.size() == 1u);

        // The way out of the first, which is inside the first tab and at
        // its right end.
        const LLRect out = strip->closeRectOf(0);
        UNSCOPED_INFO("the way out is inside its tab");
        CHECK(strip->rectOf(0).contains(out));
        UNSCOPED_INFO("at the right end");
        CHECK(out.mRight > strip->rectOf(0).getCenterX());
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a press on it is handled");
        CHECK(strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE));
        UNSCOPED_INFO("which asks nothing until it is let go of");
        REQUIRE(closed.empty());
        // Slid off before letting go: taken back.
        strip->handleHover(out.mLeft - 20, out.getCenterY(), MASK_NONE);
        strip->handleMouseUp(out.mLeft - 20, out.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a press slid off closes nothing");
        REQUIRE(closed.empty());
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseUp(out.getCenterX(), out.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("let go of over it, it asks for it to go");
        REQUIRE(closed.size() == 1u);
        CHECK(closed.front() == std::string("a"));
        UNSCOPED_INFO("without choosing it");
        CHECK(strip->chosen() == std::string("b"));

        // The middle button anywhere on a tab asks the same.
        const LLRect third = strip->rectOf(2);
        strip->handleMiddleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("the middle button closes");
        REQUIRE(closed.size() == 2u);
        CHECK(closed.back() == std::string("c"));
        strip->die();
    }

    // Many tabs share the strip: they shrink together rather than run off
    // it, down to the least a tab may be, and their names are cut rather
    // than their neighbours covered.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: many tabs shrink together to a least width and names are cut to fit", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make(400);
        std::vector<ALTabStrip::Tab> many;
        for (S32 i = 0; i < 5; ++i)
        {
            many.push_back(tab("floater_with_a_long_name_" + std::to_string(i) + ".xml", std::to_string(i)));
        }
        strip->setTabs(many, "0");

        // Five names that want more than the strip has get an equal share.
        const S32 first = strip->rectOf(0).getWidth();
        for (size_t i = 0; i < 5; ++i)
        {
            const LLRect r = strip->rectOf(i);
            CAPTURE(i);
            UNSCOPED_INFO("as wide as the first");
            REQUIRE(r.getWidth() == first);
            UNSCOPED_INFO("and inside the strip");
            REQUIRE(r.mRight <= 400);
            UNSCOPED_INFO("and no narrower than the least");
            REQUIRE(r.getWidth() >= 64);
        }
        UNSCOPED_INFO("and the share is less than a name wants");
        CHECK(first < 220);

        // Narrower still, the least holds: the tabs run past the edge
        // rather than become slivers nobody can press.
        strip->reshape(200, HEIGHT);
        for (size_t i = 0; i < 5; ++i)
        {
            CAPTURE(i);
            UNSCOPED_INFO("at the least");
            REQUIRE(strip->rectOf(i).getWidth() == 64);
        }
        UNSCOPED_INFO("and the last runs past the edge");
        CHECK(strip->rectOf(4).mRight > 200);

        // Given the room, each takes what it wants and no more -- the
        // whole of its words, even a little past the usual most.
        strip->reshape(2000, HEIGHT);
        for (size_t i = 0; i < 5; ++i)
        {
            const S32 w = strip->rectOf(i).getWidth();
            CAPTURE(i);
            INFO("wants its words");
            REQUIRE(w > 64);
            REQUIRE(w < 300);
        }

        // A short name beside a long one: the short keeps its words, the
        // long gives way, where they do not both fit.
        strip->setTabs({ tab("a.lsl", "a"), tab("a_very_long_name_that_wants_a_great_deal_of_room.lsl", "b") }, "a");
        strip->reshape(200, HEIGHT);
        const S32 words = LLFontGL::getFontSansSerifSmall()->getWidth("a.lsl");
        {
            INFO("the short one keeps what it wants");
            REQUIRE(strip->rectOf(0).getWidth() < 200 / 2);
            CHECK(strip->rectOf(0).getWidth() >= words);
        }
        {
            INFO("the long one takes the rest");
            REQUIRE(strip->rectOf(1).mRight <= 200);
            CHECK(strip->rectOf(1).getWidth() > strip->rectOf(0).getWidth());
        }

        // A name past the usual most shows whole while the strip has the
        // room, up to half the strip.
        strip->reshape(2000, HEIGHT);
        const S32 long_words = LLFontGL::getFontSansSerifSmall()->getWidth("a_very_long_name_that_wants_a_great_deal_of_room.lsl");
        {
            INFO("the long name runs on past the usual most");
            REQUIRE(strip->rectOf(1).getWidth() > 220);
            CHECK(strip->rectOf(1).getWidth() > long_words);
        }
        strip->reshape(500, HEIGHT);
        UNSCOPED_INFO("but to half the strip");
        CHECK(strip->rectOf(1).getWidth() <= 250);
        strip->die();

        // One short name alone in a wide strip is shown whole: the tab is
        // as wide as the drawing needs, the half pad before the way out
        // included.
        strip = make(600);
        strip->setTabs({ tab("New Script", "n") }, "n");
        {
            const S32 words = LLFontGL::getFontSansSerifSmall()->getWidth("New Script");
            const S32 width = strip->rectOf(0).getWidth();
            // PAD 8 + MARK 10 + words + PAD/2 4 + CLOSE 12 + PAD 8
            UNSCOPED_INFO("the tab is the drawing's sum");
            CHECK(width == llmax(64, words + 42));
        }
        strip->die();

        // A name cut to fit keeps its extension and a few letters before
        // it, with an ellipsis in the middle.
        const LLFontGL*   font = LLFontGL::getFontSansSerifSmall();
        const std::string name = "a_very_long_name_that_wants_a_great_deal_of_room.lsl";
        UNSCOPED_INFO("a name that fits is itself");
        CHECK(ALTabStrip::shortened(font, name, 10000) == name);
        const std::string cut = ALTabStrip::shortened(font, name, font->getWidth(name) / 2);
        {
            INFO("shorter: " << cut);
            REQUIRE(cut.size() < name.size());
            CHECK(font->getWidth(cut) <= font->getWidth(name) / 2);
        }
        {
            INFO("the end kept: " << cut);
            REQUIRE(cut.size() >= 7);
            REQUIRE(cut.compare(cut.size() - 7, 7, "oom.lsl") == 0);
        }
        UNSCOPED_INFO("the start kept: " << cut);
        CHECK(cut.compare(0, 2, "a_") == 0);
        UNSCOPED_INFO("an ellipsis between: " << cut);
        REQUIRE(cut.find("\xE2\x80\xA6") != std::string::npos);
        UNSCOPED_INFO("no room for even the end: the whole, for the renderer to cut");
        CHECK(ALTabStrip::shortened(font, name, 4) == name);
    }

    // A preview tab closes as any tab does: by its way out, let go of over
    // it, and by the middle button.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a preview tab closes by its way out or the middle button", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        std::vector<ALTabStrip::Tab> tabs = three();
        tabs[2].preview = true;
        strip->setTabs(tabs, "c");

        std::vector<std::string> closed;
        strip->onClosed([&closed](const std::string& value) { closed.push_back(value); });
        const LLRect out = strip->closeRectOf(2);
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("not on the press");
        REQUIRE(closed.empty());
        strip->handleMouseUp(out.getCenterX(), out.getCenterY(), MASK_NONE);
        {
            INFO("the way out asks the preview to go");
            REQUIRE(closed.size() == 1);
            CHECK(closed.back() == "c");
        }
        strip->handleMiddleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        {
            INFO("and so does the middle button");
            REQUIRE(closed.size() == 2);
            CHECK(closed.back() == "c");
        }
        strip->die();
    }

    // A preview is set in italic, and italic is a face and not a slant put
    // on the upright one: asked for the style, the upright font hands it to
    // the face the registry has for it, which carries the style itself.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a preview's italic is a face of its own and not a slanted upright", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLFontGL* upright = LLFontGL::getFontSansSerifSmall();
        UNSCOPED_INFO("the upright face is upright");
        CHECK_FALSE(upright->getFontFreetype()->getStyle() & LLFontGL::ITALIC);
        UNSCOPED_INFO("and asked for nothing, answers itself");
        CHECK(upright->faceFor(LLFontGL::NORMAL) == upright);

        const LLFontGL* italic = upright->faceFor(LLFontGL::ITALIC);
        UNSCOPED_INFO("asked for italic, it hands over to another face");
        CHECK(italic != upright);
        UNSCOPED_INFO("which carries the style itself");
        CHECK((italic->getFontFreetype()->getStyle() & LLFontGL::ITALIC) != 0);
        UNSCOPED_INFO("and has nothing further to hand over to");
        CHECK(italic->faceFor(LLFontGL::ITALIC) == italic);
        UNSCOPED_INFO("asked twice, the answer is the same face");
        CHECK(upright->faceFor(LLFontGL::ITALIC) == italic);
    }

    // More tabs than the strip holds at their least: it scrolls to keep the
    // chosen one in sight, the wheel moves along them, and a button at the
    // right end asks for the whole list.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: more tabs than fit scroll to the chosen one and offer a list", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make(200);
        std::vector<ALTabStrip::Tab> many;
        for (S32 i = 0; i < 6; ++i)
        {
            many.push_back(tab("script_" + std::to_string(i) + ".lsl", std::to_string(i)));
        }
        strip->setTabs(many, "0");
        CHECK(strip->overflowing());
        const LLRect list = strip->listRect();
        {
            INFO("a list button at the right end");
            CHECK(list.notEmpty());
            CHECK(list.mRight == 200);
        }
        UNSCOPED_INFO("no tab under it");
        CHECK(strip->at(list.getCenterX(), list.getCenterY()) == -1);

        strip->choose("5");
        const LLRect last = strip->rectOf(5);
        {
            INFO("the chosen tab brought into sight");
            CHECK(last.mLeft >= 0);
            CHECK(last.mRight <= list.mLeft);
        }
        UNSCOPED_INFO("and pressable where it is drawn");
        CHECK(strip->at(last.getCenterX(), last.getCenterY()) == 5);

        strip->handleScrollWheel(10, HEIGHT / 2, LLScrollDelta(-10, -10.f));
        UNSCOPED_INFO("the wheel back to the start");
        CHECK(strip->rectOf(0).mLeft == 0);
        strip->handleScrollWheel(10, HEIGHT / 2, LLScrollDelta(100, 100.f));
        {
            INFO("and no further than the end");
            REQUIRE(strip->rectOf(5).mRight <= list.mLeft);
            CHECK(strip->rectOf(5).mRight > list.mLeft - 8);
        }

        S32 asked = 0;
        strip->onListAsked([&asked]() { ++asked; });
        strip->handleMouseDown(list.getCenterX(), list.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("the button asks for the list");
        CHECK(asked == 1);

        strip->reshape(2000, HEIGHT);
        {
            INFO("with room, nothing to list");
            REQUIRE_FALSE(strip->overflowing());
            CHECK(strip->listRect().isEmpty());
        }
        UNSCOPED_INFO("and nothing scrolled");
        CHECK(strip->rectOf(0).mLeft == 0);
        strip->die();
    }

    // A preview is held by a double-click on it, as a preview is anywhere:
    // the strip says which, and the caller says it is a preview no longer.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a double-click on a preview tab holds it", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip*                  strip = make();
        std::vector<ALTabStrip::Tab> tabs  = three();
        tabs[2].preview                    = true;
        strip->setTabs(tabs, "c");
        std::vector<std::string> held;
        strip->onHeld([&held](const std::string& value) { held.push_back(value); });
        const LLRect preview = strip->rectOf(2);
        UNSCOPED_INFO("a double-click on a tab is taken");
        CHECK(strip->handleDoubleClick(preview.getCenterX(), preview.getCenterY(), MASK_NONE));
        UNSCOPED_INFO("said once");
        REQUIRE(held.size() == 1u);
        UNSCOPED_INFO("of the tab under it");
        CHECK(held.back() == std::string("c"));
        UNSCOPED_INFO("nowhere, nothing");
        REQUIRE((!strip->handleDoubleClick(2000, 2000, MASK_NONE) || held.size() == 1u));
        strip->die();
    }
    // A tab dragged past its neighbour's middle moves, and the host hears
    // the new order when it is let go of -- told before letting go forgets
    // the drag, which it did before this was said, the host never hearing
    // and filling the strip back in the old order on the next keystroke.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a tab dragged past its neighbour moves; the new order told on release", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "a");
        std::vector<std::vector<std::string>> orders;
        strip->onReordered([&orders](const std::vector<std::string>& order) { orders.push_back(order); });

        const LLRect first  = strip->rectOf(0);
        const LLRect second = strip->rectOf(1);
        strip->handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        strip->handleHover(second.getCenterX() + 4, second.getCenterY(), MASK_NONE);
        REQUIRE(strip->tabs().size() == 3u);
        UNSCOPED_INFO("dragged past its neighbour, it moved");
        CHECK(strip->tabs()[1].value == "a");
        UNSCOPED_INFO("nothing said while it is held");
        REQUIRE(orders.empty());
        strip->handleMouseUp(second.getCenterX() + 4, second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("let go of, the order is told");
        REQUIRE(orders.size() == (size_t)1);
        UNSCOPED_INFO("as it now is");
        CHECK(orders.back() == std::vector<std::string>({ "b", "a", "c" }));

        // A press that never travelled is a press, and says no order.
        const LLRect third = strip->rectOf(2);
        strip->handleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        strip->handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a press is not a drag");
        REQUIRE(orders.size() == (size_t)1);

        // Taken away mid-drag, the tabs are where the drag left them, and
        // the host is told so.
        strip->setTabs(three(), "a");
        strip->handleMouseDown(strip->rectOf(0).getCenterX(), HEIGHT / 2, MASK_NONE);
        strip->handleHover(strip->rectOf(1).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        gFocusMgr.setMouseCapture(nullptr);
        UNSCOPED_INFO("a drag cut short is told");
        REQUIRE(orders.size() == (size_t)2);
        UNSCOPED_INFO("where it got to");
        CHECK(orders.back() == std::vector<std::string>({ "b", "a", "c" }));
        strip->die();
    }

    // The host fills the strip afresh whenever a fact about a tab moves,
    // and may while a press is held: the press follows its tab, not its
    // place. A drag goes on with the tab it began with; a way out pressed
    // closes the tab it was pressed on or nothing; and a press whose tab
    // has gone is let go of.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a press held while the host refills the strip follows its tab", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        std::vector<std::vector<std::string>> orders;
        std::vector<std::string>              closed;
        strip->onReordered([&orders](const std::vector<std::string>& order) { orders.push_back(order); });
        strip->onClosed([&closed](const std::string& value) { closed.push_back(value); });

        // A drag of "a", the host filling the strip in its own order mid-way
        // -- a check come back, a badge on "c".
        strip->setTabs(three(), "a");
        strip->handleMouseDown(strip->rectOf(0).getCenterX(), HEIGHT / 2, MASK_NONE);
        strip->handleHover(strip->rectOf(1).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        std::vector<ALTabStrip::Tab> refill = three();
        refill[2].badge = LLColor4::red;
        strip->setTabs(refill, "a");
        strip->handleHover(strip->rectOf(2).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        strip->handleMouseUp(strip->rectOf(2).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        UNSCOPED_INFO("told once");
        REQUIRE(orders.size() == (size_t)1);
        UNSCOPED_INFO("the tab dragged is the one pressed, to the end");
        CHECK(orders.back() == std::vector<std::string>({ "b", "c", "a" }));

        // The way out of "b" pressed, then "a" gone from the strip: "c" now
        // sits where "b" was. Let go of there, nothing closes -- the press
        // was on "b" -- and let go of over "b"'s way out, "b" does.
        strip->setTabs(three(), "a");
        const LLRect out = strip->closeRectOf(1);
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->setTabs({ tab("floater_b.xml", "b"), tab("panel_c.xml", "c") }, "b");
        strip->handleMouseUp(out.getCenterX(), out.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("not the tab that came to sit there");
        REQUIRE(closed.empty());
        const LLRect moved = strip->closeRectOf(0);
        strip->handleHover(moved.getCenterX(), moved.getCenterY(), MASK_NONE);
        strip->handleMouseDown(moved.getCenterX(), moved.getCenterY(), MASK_NONE);
        strip->setTabs({ tab("floater_b.xml", "b"), tab("panel_c.xml", "c") }, "b");
        strip->handleMouseUp(moved.getCenterX(), moved.getCenterY(), MASK_NONE);
        {
            INFO("the tab pressed, followed");
            REQUIRE(closed.size() == 1);
            CHECK(closed.back() == "b");
        }

        // A drag whose tab goes: let go of, nothing moves after, nothing
        // is told.
        strip->setTabs(three(), "a");
        strip->handleMouseDown(strip->rectOf(0).getCenterX(), HEIGHT / 2, MASK_NONE);
        strip->handleHover(strip->rectOf(1).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        orders.clear();
        strip->setTabs({ tab("floater_b.xml", "b"), tab("panel_c.xml", "c") }, "b");
        UNSCOPED_INFO("the press let go of");
        CHECK_FALSE(strip->hasMouseCapture());
        strip->handleHover(strip->rectOf(1).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        strip->handleMouseUp(strip->rectOf(1).getCenterX() + 4, HEIGHT / 2, MASK_NONE);
        {
            INFO("nothing moved");
            REQUIRE(strip->tabs().size() == 2u);
            REQUIRE(strip->tabs()[0].value == "b");
            CHECK(strip->tabs()[1].value == "c");
        }
        UNSCOPED_INFO("nothing told");
        REQUIRE(orders.empty());
        strip->die();
    }

    // Whoever hears of a choice fills the strip afresh, as the studio does:
    // the value it heard stays the value, and the menu asked for by the
    // right button is about the tab pressed.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a host that refills the strip on a choice keeps the value it heard", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "a");
        std::vector<std::string> heard;
        std::vector<std::string> menus;
        strip->onChosen([&](const std::string& value) {
            strip->setTabs(altabstrip_data::three(), value);
            heard.push_back(value);
        });
        strip->onMenu([&](const std::string& value, S32, S32) { menus.push_back(value); });

        const LLRect second = strip->rectOf(1);
        strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        strip->handleMouseUp(second.getCenterX(), second.getCenterY(), MASK_NONE);
        {
            INFO("heard as it was chosen");
            REQUIRE(heard.size() == 1);
            CHECK(heard.back() == "b");
        }
        UNSCOPED_INFO("and chosen");
        CHECK(strip->chosen() == std::string("b"));

        const LLRect third = strip->rectOf(2);
        strip->handleRightMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        {
            INFO("the right button chooses");
            REQUIRE(heard.size() == 2);
            CHECK(heard.back() == "c");
        }
        {
            INFO("and asks for a menu about it");
            REQUIRE(menus.size() == 1);
            CHECK(menus.back() == "c");
        }
        strip->die();
    }
    // With the keyboard: the arrows walk the tabs and the strip keeps the
    // keyboard while they do, Shift with an arrow moves the chosen one,
    // and what else the mouse does has a key.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: the arrows walk the tabs and what the mouse does has a key", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        // A host that keeps its own order, fills the strip on every choice,
        // and puts the keyboard in what it shows.
        std::vector<ALTabStrip::Tab>          kept = three();
        std::vector<std::string>              chosen, closed, held, menus;
        std::vector<std::vector<std::string>> orders;
        S32                                   lists = 0;
        strip->setTabs(kept, "a");
        strip->onReordered([&](const std::vector<std::string>& order) {
            orders.push_back(order);
            std::vector<ALTabStrip::Tab> now;
            for (const std::string& value : order)
            {
                for (const ALTabStrip::Tab& one : kept)
                {
                    if (one.value == value)
                    {
                        now.push_back(one);
                    }
                }
            }
            kept = now;
        });
        strip->onChosen([&](const std::string& value) {
            chosen.push_back(value);
            gFocusMgr.setKeyboardFocus(nullptr);
            strip->setTabs(kept, value);
        });
        strip->onClosed([&](const std::string& value) { closed.push_back(value); });
        strip->onHeld([&](const std::string& value) { held.push_back(value); });
        strip->onMenu([&](const std::string& value, S32, S32) { menus.push_back(value); });
        strip->onListAsked([&]() { ++lists; });

        UNSCOPED_INFO("reached by Tab");
        CHECK(strip->hasTabStop());
        UNSCOPED_INFO("the right arrow is taken");
        CHECK(strip->handleKeyHere(KEY_RIGHT, MASK_NONE));
        {
            INFO("and chooses the next");
            REQUIRE(chosen.size() == 1);
            CHECK(chosen.back() == "b");
            CHECK(strip->chosen() == "b");
        }
        UNSCOPED_INFO("and the strip keeps the keyboard");
        CHECK(strip->hasFocus());
        strip->handleKeyHere(KEY_END, MASK_NONE);
        UNSCOPED_INFO("End, the last");
        CHECK(strip->chosen() == std::string("c"));
        strip->handleKeyHere(KEY_RIGHT, MASK_NONE);
        {
            INFO("past the last, nothing more");
            REQUIRE(chosen.size() == 2);
            CHECK(strip->chosen() == "c");
        }
        strip->handleKeyHere(KEY_HOME, MASK_NONE);
        UNSCOPED_INFO("Home, the first");
        CHECK(strip->chosen() == std::string("a"));

        UNSCOPED_INFO("Shift and right is taken");
        CHECK(strip->handleKeyHere(KEY_RIGHT, MASK_SHIFT));
        {
            INFO("and moves the chosen tab along, told");
            REQUIRE(orders.size() == 1);
            CHECK(orders.back() == std::vector<std::string>({ "b", "a", "c" }));
        }
        {
            INFO("which is still chosen, where it went");
            REQUIRE(strip->chosen() == "a");
            REQUIRE(strip->tabs().size() == 3u);
            CHECK(strip->tabs()[1].value == "a");
        }
        strip->handleKeyHere(KEY_LEFT, MASK_SHIFT);
        strip->handleKeyHere(KEY_LEFT, MASK_SHIFT);
        {
            INFO("back, and no further than the first");
            REQUIRE(orders.size() == 2);
            REQUIRE(strip->tabs().size() == 3u);
            CHECK(strip->tabs()[0].value == "a");
        }

        strip->handleKeyHere(KEY_DELETE, MASK_NONE);
        UNSCOPED_INFO("Delete closes the chosen");
        CHECK(closed == std::vector<std::string>({ "a" }));
        strip->handleKeyHere(KEY_RETURN, MASK_NONE);
        strip->handleKeyHere(' ', MASK_NONE);
        UNSCOPED_INFO("Return and Space hold it");
        CHECK(held == std::vector<std::string>({ "a", "a" }));
        strip->handleKeyHere(KEY_F10, MASK_SHIFT);
        UNSCOPED_INFO("Shift-F10 asks for its menu");
        CHECK(menus == std::vector<std::string>({ "a" }));
        strip->handleKeyHere(KEY_DOWN, MASK_NONE);
        UNSCOPED_INFO("the down arrow for the list");
        CHECK(lists == 1);
        UNSCOPED_INFO("anything else is not the strip's");
        CHECK_FALSE(strip->handleKeyHere('Q', MASK_NONE));
        gFocusMgr.setKeyboardFocus(nullptr);
        strip->die();
    }

    // A tab pulled well off the strip and let go of is torn out: said by
    // its value and where on the screen, and the order left as it was; let
    // go of on the strip, it is not.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a tab pulled well off the strip and let go of is torn out", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "a");
        std::vector<std::string>              torn;
        std::vector<std::pair<S32, S32>>      where;
        std::vector<std::vector<std::string>> orders;
        strip->onTorn([&](const std::string& value, S32 x, S32 y) {
            torn.push_back(value);
            where.emplace_back(x, y);
        });
        strip->onReordered([&](const std::vector<std::string>& order) { orders.push_back(order); });
        const LLRect second = strip->rectOf(1);
        strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        strip->handleHover(second.getCenterX(), second.getCenterY() - 10, MASK_NONE);
        UNSCOPED_INFO("a little off the strip is still along it");
        CHECK_FALSE(strip->tearing());
        strip->handleHover(second.getCenterX() + 5, -120, MASK_NONE);
        UNSCOPED_INFO("well off it, torn");
        CHECK(strip->tearing());
        strip->handleMouseUp(second.getCenterX() + 5, -120, MASK_NONE);
        UNSCOPED_INFO("said once");
        REQUIRE(torn.size() == size_t(1));
        CHECK(torn.front() == std::string("b"));
        S32 screen_x = 0, screen_y = 0;
        strip->localPointToScreen(second.getCenterX() + 5, -120, &screen_x, &screen_y);
        REQUIRE(where.size() == size_t(1));
        UNSCOPED_INFO("where, on the screen");
        CHECK(where.front() == std::make_pair(screen_x, screen_y));
        UNSCOPED_INFO("no longer tearing");
        CHECK_FALSE(strip->tearing());
        UNSCOPED_INFO("and the order as it was");
        REQUIRE((orders.empty() || orders.back() == std::vector<std::string>({ "a", "b", "c" })));

        // Pulled off and brought back: moved along the strip, not torn.
        strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        strip->handleHover(second.getCenterX(), -120, MASK_NONE);
        strip->handleHover(strip->rectOf(2).getCenterX() + 10, second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("back on the strip");
        CHECK_FALSE(strip->tearing());
        strip->handleMouseUp(strip->rectOf(2).getCenterX() + 10, second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("not torn");
        REQUIRE(torn.size() == size_t(1));
        {
            INFO("moved along instead");
            REQUIRE_FALSE(orders.empty());
            CHECK(orders.back() == std::vector<std::string>({ "a", "c", "b" }));
        }
        strip->die();
    }

    // With no tab at all, an arrow walks nowhere and is still the strip's:
    // passed on, it would walk the avatar.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: with no tabs an arrow key is still the strip's", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs({}, "");
        for (KEY key : { KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END })
        {
            CAPTURE(key);
            UNSCOPED_INFO("kept");
            REQUIRE(strip->handleKeyHere(key, MASK_NONE));
        }
        UNSCOPED_INFO("another key not");
        CHECK_FALSE(strip->handleKeyHere('X', MASK_NONE));
        strip->setTabs(three(), "");
        {
            INFO("with tabs, the walk starts");
            REQUIRE(strip->handleKeyHere(KEY_RIGHT, MASK_NONE));
            CHECK(strip->chosen() == "a");
        }
        strip->die();
    }

    // One tab made again by its value: its dot and its tip change where it
    // is, and nothing moves; a longer name lays the tabs out again. A value
    // no tab has changes nothing.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: setTab changes one tab in place by its value", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make(WIDTH * 2);
        strip->setTabs(three(), "b");
        const LLRect    before = strip->rectOf(2);
        ALTabStrip::Tab dirty  = tab("floater_b.xml", "b");
        dirty.dirty            = true;
        dirty.toolTip          = "unsaved";
        dirty.badge            = LLColor4::red;
        UNSCOPED_INFO("found");
        CHECK(strip->setTab(dirty));
        REQUIRE(strip->tabs().size() == 3u);
        {
            INFO("changed in place");
            REQUIRE(strip->tabs()[1].dirty);
            REQUIRE(strip->tabs()[1].toolTip == "unsaved");
            CHECK(strip->tabs()[1].badge == LLColor4::red);
        }
        {
            INFO("the others as they were");
            REQUIRE(strip->tabs()[0].label == "floater_a.xml");
            CHECK_FALSE(strip->tabs()[2].dirty);
        }
        {
            INFO("nothing moved");
            REQUIRE(strip->rectOf(2) == before);
            CHECK(strip->chosen() == "b");
        }

        UNSCOPED_INFO("a longer name");
        CHECK(strip->setTab(tab("floater_b_with_a_much_longer_name.xml", "b")));
        UNSCOPED_INFO("the tabs after it moved along");
        CHECK(strip->rectOf(2).mLeft > before.mLeft);
        {
            INFO("no such tab");
            REQUIRE_FALSE(strip->setTab(tab("gone.xml", "z")));
            REQUIRE(strip->tabs().size() == 3);
        }
        strip->die();
    }
    // Where each tab starts is summed once as they are laid out: forty tabs
    // of every width run edge to edge a gap apart, each found under its own
    // middle, scrolled or not, and again once their names change.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: forty tabs lie a gap apart and each is found under its middle", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        const auto  many  = [](const char* stem) {
            std::vector<ALTabStrip::Tab> tabs;
            for (S32 i = 0; i < 40; ++i)
            {
                tabs.push_back(altabstrip_data::tab(std::string(stem) + std::string(static_cast<size_t>(1 + (i * 7) % 23), 'x') + ".xml", std::to_string(i)));
            }
            return tabs;
        };
        const auto require_laid_out = [&](const std::string& what) {
            INFO(what);
            const S32 gap = strip->rectOf(1).mLeft - strip->rectOf(0).mRight;
            for (size_t i = 0; i + 1 < 40; ++i)
            {
                CAPTURE(i + 1);
                UNSCOPED_INFO("a gap after the one before");
                REQUIRE(strip->rectOf(i + 1).mLeft - strip->rectOf(i).mRight == gap);
            }
            for (size_t i = 0; i < 40; ++i)
            {
                const LLRect r = strip->rectOf(i);
                if (r.getCenterX() >= 0 && r.getCenterX() < strip->getRect().getWidth() - 40)
                {
                    CAPTURE(i);
                    UNSCOPED_INFO("the tab under its own middle");
                    REQUIRE(strip->at(r.getCenterX(), r.getCenterY()) == (S32)i);
                }
            }
        };
        strip->setTabs(many("panel_"), "0");
        require_laid_out("from the start");
        strip->choose("39");
        UNSCOPED_INFO("scrolled to the last");
        CHECK(strip->rectOf(0).mLeft < 0);
        require_laid_out("scrolled");
        strip->setTabs(many("a_much_longer_name_"), "39");
        require_laid_out("renamed");
        strip->die();
    }

    // A name cut to fit is cut between characters as a reader sees them: a
    // letter kept before the extension keeps the mark on it, and the
    // ellipsis goes before the two rather than between them.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a name cut to fit is cut between characters as a reader sees them", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLFontGL*   font = LLFontGL::getFontSansSerifSmall();
        // An e with a combining dot below and a combining acute, two
        // letters before .lsl.
        const std::string name = "a_very_long_name_that_wants_a_great_deal_of_room_xe\xCC\xA3\xCC\x81yz.lsl";
        const std::string cut  = ALTabStrip::shortened(font, name, font->getWidth(name) / 2);
        {
            INFO("cut, with an ellipsis: " << cut);
            REQUIRE(cut.size() < name.size());
            REQUIRE(cut.find("\xE2\x80\xA6") != std::string::npos);
        }
        UNSCOPED_INFO("the extension kept: " << cut);
        REQUIRE(cut.ends_with(".lsl"));
        UNSCOPED_INFO("no mark after the ellipsis, parted from its e: " << cut);
        REQUIRE(cut.find("\xE2\x80\xA6\xCC") == std::string::npos);
        UNSCOPED_INFO("the e kept with both its marks: " << cut);
        REQUIRE(cut.find("e\xCC\xA3\xCC\x81") != std::string::npos);
    }

    // What a cut name keeps after its ellipsis: the extension and three
    // letters before it, or, where there is no extension, its last four
    // letters.
    TEST_CASE_METHOD(altabstrip_data, "altabstrip: a cut name keeps its extension or last letters after the ellipsis", "[altabstrip]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLFontGL*   font     = LLFontGL::getFontSansSerifSmall();
        const std::string ellipsis = "\xE2\x80\xA6";
        const auto        kept     = [&](const std::string& name) {
            const std::string cut = ALTabStrip::shortened(font, name, font->getWidth(name) / 2);
            const size_t      at  = cut.rfind(ellipsis);
            return at == std::string::npos ? std::string() : cut.substr(at + ellipsis.size());
        };
        UNSCOPED_INFO("three letters and the extension");
        CHECK(kept("a_very_long_name_that_wants_a_great_deal_of_room.lsl") == std::string("oom.lsl"));
        UNSCOPED_INFO("four letters, with no extension");
        CHECK(kept("a_very_long_name_that_wants_a_great_deal_of_room") == std::string("room"));
    }
}
