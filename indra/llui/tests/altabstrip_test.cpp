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

#include "../altabstrip.h"

#include "../lluictrlfactory.h"

#include "llfontfreetype.h"
#include "llfontgl.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gTabStripTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gTabStripTestAnonName;
}

namespace tut
{
    struct altabstrip_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

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

    typedef test_group<altabstrip_data> altabstrip_test;
    typedef altabstrip_test::object     altabstrip_object;
    tut::altabstrip_test altabstrip_testgroup("altabstrip");

    // The tabs run left to right in the order they were given, each inside
    // the strip and none over another, and the chosen one is the one asked.
    template<> template<>
    void altabstrip_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "b");
        ensure_equals("three tabs", strip->tabs().size(), 3u);
        ensure_equals("the chosen one", strip->chosen(), std::string("b"));

        S32 right = -1;
        for (size_t i = 0; i < 3; ++i)
        {
            const LLRect r = strip->rectOf(i);
            ensure("tab " + std::to_string(i) + " starts after the one before: "
                       + std::to_string(r.mLeft) + " > " + std::to_string(right),
                   r.mLeft > right);
            ensure("and ends inside the strip: " + std::to_string(r.mRight), r.mRight <= WIDTH);
            ensure("and is as tall as it", r.mTop == HEIGHT && r.mBottom == 0);
            ensure("and wide enough to read: " + std::to_string(r.getWidth()), r.getWidth() >= 64);
            right = r.mRight;
        }
        // A point in each tab answers that tab.
        for (size_t i = 0; i < 3; ++i)
        {
            const LLRect r = strip->rectOf(i);
            ensure_equals("the tab under its own middle", strip->at(r.getCenterX(), r.getCenterY()), (S32)i);
        }
        // Past the last tab, with room to be past it, nothing.
        strip->reshape(WIDTH * 2, HEIGHT);
        ensure_equals("and nothing past the last", strip->at(WIDTH * 2 - 1, HEIGHT / 2), -1);
        strip->die();
    }

    // A press on a tab chooses it and says so; a press on the way out says
    // that instead, and chooses nothing. The way out is at the right end of
    // the tab the pointer is over.
    template<> template<>
    void altabstrip_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        strip->setTabs(three(), "a");

        std::vector<std::string> chosen;
        std::vector<std::string> closed;
        strip->onChosen([&chosen](const std::string& value) { chosen.push_back(value); });
        strip->onClosed([&closed](const std::string& value) { closed.push_back(value); });

        const LLRect second = strip->rectOf(1);
        strip->handleHover(second.getCenterX(), second.getCenterY(), MASK_NONE);
        ensure("a press on a tab is handled",
               strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE));
        ensure_equals("and chooses it", strip->chosen(), std::string("b"));
        ensure_equals("saying so once", chosen.size(), 1u);
        ensure_equals("by value", chosen.front(), std::string("b"));
        ensure("and closes nothing", closed.empty());

        // Pressing the tab already chosen says nothing new.
        strip->handleMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        ensure_equals("chosen once is chosen", chosen.size(), 1u);

        // The way out of the first, which is inside the first tab and at
        // its right end.
        const LLRect out = strip->closeRectOf(0);
        ensure("the way out is inside its tab", strip->rectOf(0).contains(out));
        ensure("at the right end", out.mRight > strip->rectOf(0).getCenterX());
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        ensure("a press on it is handled",
               strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE));
        ensure("which asks nothing until it is let go of", closed.empty());
        // Slid off before letting go: taken back.
        strip->handleHover(out.mLeft - 20, out.getCenterY(), MASK_NONE);
        strip->handleMouseUp(out.mLeft - 20, out.getCenterY(), MASK_NONE);
        ensure("a press slid off closes nothing", closed.empty());
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseUp(out.getCenterX(), out.getCenterY(), MASK_NONE);
        ensure_equals("let go of over it, it asks for it to go", closed.size(), 1u);
        ensure_equals("by value", closed.front(), std::string("a"));
        ensure_equals("without choosing it", strip->chosen(), std::string("b"));

        // The middle button anywhere on a tab asks the same.
        const LLRect third = strip->rectOf(2);
        strip->handleMiddleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure_equals("the middle button closes", closed.size(), 2u);
        ensure_equals("that tab", closed.back(), std::string("c"));
        strip->die();
    }

    // Many tabs share the strip: they shrink together rather than run off
    // it, down to the least a tab may be, and their names are cut rather
    // than their neighbours covered.
    template<> template<>
    void altabstrip_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            ensure_equals("tab " + std::to_string(i) + " is as wide as the first", r.getWidth(), first);
            ensure("and inside the strip: " + std::to_string(r.mRight), r.mRight <= 400);
            ensure("and no narrower than the least: " + std::to_string(r.getWidth()), r.getWidth() >= 64);
        }
        ensure("and the share is less than a name wants", first < 220);

        // Narrower still, the least holds: the tabs run past the edge
        // rather than become slivers nobody can press.
        strip->reshape(200, HEIGHT);
        for (size_t i = 0; i < 5; ++i)
        {
            ensure("at the least: " + std::to_string(strip->rectOf(i).getWidth()),
                   strip->rectOf(i).getWidth() == 64);
        }
        ensure("and the last runs past the edge", strip->rectOf(4).mRight > 200);

        // Given the room, each takes what it wants and no more -- the
        // whole of its words, even a little past the usual most.
        strip->reshape(2000, HEIGHT);
        for (size_t i = 0; i < 5; ++i)
        {
            const S32 w = strip->rectOf(i).getWidth();
            ensure("wants its words: " + std::to_string(w), w > 64 && w < 300);
        }

        // A short name beside a long one: the short keeps its words, the
        // long gives way, where they do not both fit.
        strip->setTabs({ tab("a.lsl", "a"), tab("a_very_long_name_that_wants_a_great_deal_of_room.lsl", "b") }, "a");
        strip->reshape(200, HEIGHT);
        const S32 words = LLFontGL::getFontSansSerifSmall()->getWidth("a.lsl");
        ensure("the short one keeps what it wants: " + std::to_string(strip->rectOf(0).getWidth()), strip->rectOf(0).getWidth() < 200 / 2 && strip->rectOf(0).getWidth() >= words);
        ensure("the long one takes the rest: " + std::to_string(strip->rectOf(1).mRight), strip->rectOf(1).mRight <= 200 && strip->rectOf(1).getWidth() > strip->rectOf(0).getWidth());

        // A name past the usual most shows whole while the strip has the
        // room, up to half the strip.
        strip->reshape(2000, HEIGHT);
        const S32 long_words = LLFontGL::getFontSansSerifSmall()->getWidth("a_very_long_name_that_wants_a_great_deal_of_room.lsl");
        ensure("the long name runs on past the usual most: " + std::to_string(strip->rectOf(1).getWidth()), strip->rectOf(1).getWidth() > 220 && strip->rectOf(1).getWidth() > long_words);
        strip->reshape(500, HEIGHT);
        ensure("but to half the strip: " + std::to_string(strip->rectOf(1).getWidth()), strip->rectOf(1).getWidth() <= 250);
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
            ensure_equals("the tab is the drawing's sum", width, llmax(64, words + 42));
        }
        strip->die();

        // A name cut to fit keeps its extension and a few letters before
        // it, with an ellipsis in the middle.
        const LLFontGL*   font = LLFontGL::getFontSansSerifSmall();
        const std::string name = "a_very_long_name_that_wants_a_great_deal_of_room.lsl";
        ensure_equals("a name that fits is itself", ALTabStrip::shortened(font, name, 10000), name);
        const std::string cut = ALTabStrip::shortened(font, name, font->getWidth(name) / 2);
        ensure("shorter: " + cut, cut.size() < name.size() && font->getWidth(cut) <= font->getWidth(name) / 2);
        ensure("the end kept: " + cut, cut.size() >= 7 && cut.compare(cut.size() - 7, 7, "oom.lsl") == 0);
        ensure("the start kept: " + cut, cut.compare(0, 2, "a_") == 0);
        ensure("an ellipsis between: " + cut, cut.find("\xE2\x80\xA6") != std::string::npos);
        ensure_equals("no room for even the end: the whole, for the renderer to cut", ALTabStrip::shortened(font, name, 4), name);
    }

    // A preview tab has no way out: it is not being held, so there is
    // nothing to let go of.
    template<> template<>
    void altabstrip_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make();
        std::vector<ALTabStrip::Tab> tabs = three();
        tabs[2].preview = true;
        strip->setTabs(tabs, "c");

        S32 closed = 0;
        strip->onClosed([&closed](const std::string&) { ++closed; });
        const LLRect out = strip->closeRectOf(2);
        strip->handleHover(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMouseUp(out.getCenterX(), out.getCenterY(), MASK_NONE);
        strip->handleMiddleMouseDown(out.getCenterX(), out.getCenterY(), MASK_NONE);
        ensure_equals("nothing asks a preview to go", closed, 0);
        ensure_equals("and it stays chosen", strip->chosen(), std::string("c"));
        strip->die();
    }

    // A preview is set in italic, and italic is a face and not a slant put
    // on the upright one: asked for the style, the upright font hands it to
    // the face the registry has for it, which carries the style itself.
    template<> template<>
    void altabstrip_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLFontGL* upright = LLFontGL::getFontSansSerifSmall();
        ensure("the upright face is upright",
               !(upright->getFontFreetype()->getStyle() & LLFontGL::ITALIC));
        ensure("and asked for nothing, answers itself", upright->faceFor(LLFontGL::NORMAL) == upright);

        const LLFontGL* italic = upright->faceFor(LLFontGL::ITALIC);
        ensure("asked for italic, it hands over to another face", italic != upright);
        ensure("which carries the style itself",
               (italic->getFontFreetype()->getStyle() & LLFontGL::ITALIC) != 0);
        ensure("and has nothing further to hand over to", italic->faceFor(LLFontGL::ITALIC) == italic);
        ensure("asked twice, the answer is the same face", upright->faceFor(LLFontGL::ITALIC) == italic);
    }

    // More tabs than the strip holds at their least: it scrolls to keep the
    // chosen one in sight, the wheel moves along them, and a button at the
    // right end asks for the whole list.
    template<> template<>
    void altabstrip_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALTabStrip* strip = make(200);
        std::vector<ALTabStrip::Tab> many;
        for (S32 i = 0; i < 6; ++i)
        {
            many.push_back(tab("script_" + std::to_string(i) + ".lsl", std::to_string(i)));
        }
        strip->setTabs(many, "0");
        ensure("overflowing", strip->overflowing());
        const LLRect list = strip->listRect();
        ensure("a list button at the right end", list.notEmpty() && list.mRight == 200);
        ensure_equals("no tab under it", strip->at(list.getCenterX(), list.getCenterY()), -1);

        strip->choose("5");
        const LLRect last = strip->rectOf(5);
        ensure("the chosen tab brought into sight: " + std::to_string(last.mLeft) + ".." + std::to_string(last.mRight),
               last.mLeft >= 0 && last.mRight <= list.mLeft);
        ensure_equals("and pressable where it is drawn", strip->at(last.getCenterX(), last.getCenterY()), 5);

        strip->handleScrollWheel(10, HEIGHT / 2, LLScrollDelta(-10, -10.f));
        ensure_equals("the wheel back to the start", strip->rectOf(0).mLeft, 0);
        strip->handleScrollWheel(10, HEIGHT / 2, LLScrollDelta(100, 100.f));
        ensure("and no further than the end", strip->rectOf(5).mRight <= list.mLeft && strip->rectOf(5).mRight > list.mLeft - 8);

        S32 asked = 0;
        strip->onListAsked([&asked]() { ++asked; });
        strip->handleMouseDown(list.getCenterX(), list.getCenterY(), MASK_NONE);
        ensure_equals("the button asks for the list", asked, 1);

        strip->reshape(2000, HEIGHT);
        ensure("with room, nothing to list", !strip->overflowing() && strip->listRect().isEmpty());
        ensure_equals("and nothing scrolled", strip->rectOf(0).mLeft, 0);
        strip->die();
    }
}
