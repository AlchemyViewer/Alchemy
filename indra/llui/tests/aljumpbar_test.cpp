/**
 * @file aljumpbar_test.cpp
 * @brief The path, what folds when it does not fit, and what each step offers.
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

#include "aljumpbar.h"
#include "../lltextbox.h"
#include "llfontgl.h"

#include "../llbutton.h"
#include "../llflyoutbutton.h"
#include "../lluictrlfactory.h"

#include "llcallbacklist.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

namespace tut
{
    struct aljumpbar_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALJumpBar* make(S32 width = 600)
        {
            ALJumpBar::Params p(LLUICtrlFactory::getDefaultParams<ALJumpBar>());
            p.rect = LLRect(0, 22, width, 0);
            return LLUICtrlFactory::create<ALJumpBar>(p);
        }

        // root > body > buttons > close_btn, with siblings on the last two.
        static std::vector<ALJumpBar::Crumb> path()
        {
            std::vector<ALJumpBar::Crumb> crumbs;
            for (const char* name : { "root", "body", "buttons", "close_btn" })
            {
                ALJumpBar::Crumb crumb;
                crumb.label = name;
                crumb.value = name;
                crumbs.push_back(crumb);
            }
            crumbs[2].alternatives = { { "buttons", "buttons" }, { "footer", "footer" } };
            crumbs[3].alternatives = { { "close_btn", "close_btn" }, { "help_btn", "help_btn" } };
            return crumbs;
        }

        static S32 crumbsIn(const ALJumpBar* bar)
        {
            S32 n = 0;
            for (LLView* child : *bar->getChildList())
            {
                n += child->getName().rfind("crumb_", 0) == 0;
            }
            return n;
        }
    };

    typedef test_group<aljumpbar_data> aljumpbar_test;
    typedef aljumpbar_test::object     aljumpbar_object;
    tut::aljumpbar_test aljumpbar_testgroup("aljumpbar");

    // One crumb per step, in order, and a step with somewhere else to be is
    // the one with the arrow on it.
    template<> template<>
    void aljumpbar_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        ensure_equals("one each", crumbsIn(bar), 4);
        ensure("a step with nowhere else to be is a plain button",
               bar->getChild<LLView>("crumb_0")->as<LLFlyoutButton>() == nullptr);
        ensure("and one with siblings offers them",
               bar->getChild<LLView>("crumb_2")->as<LLFlyoutButton>() != nullptr);

        // Left to right, in the order the path reads.
        ensure("in order", bar->getChild<LLView>("crumb_0")->getRect().mRight
                        <= bar->getChild<LLView>("crumb_1")->getRect().mLeft);
        delete bar;
    }

    // Pressing a crumb goes there; picking one of its siblings goes there
    // instead. One signal, because they are one gesture with two answers.
    template<> template<>
    void aljumpbar_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        std::vector<std::pair<size_t, std::string> > chosen;
        bar->onChose([&chosen](size_t at, const std::string& value)
        {
            chosen.emplace_back(at, value);
        });

        bar->getChild<LLButton>("crumb_1")->onCommit();
        ensure_equals("said once", chosen.size(), 1u);
        ensure_equals("which crumb", chosen.back().first, 1u);
        ensure_equals("and its own value", chosen.back().second, std::string("body"));

        // Sideways: the sibling picked is what is answered, not the crumb.
        LLFlyoutButton* sideways = bar->getChild<LLView>("crumb_3")->as<LLFlyoutButton>();
        ensure("the last step offers its siblings", sideways != nullptr);
        sideways->setSelectedByValue(LLSD("help_btn"), true);
        sideways->onCommit();
        ensure_equals("said again", chosen.size(), 2u);
        ensure_equals("on the same crumb", chosen.back().first, 3u);
        ensure_equals("with the sibling chosen", chosen.back().second, std::string("help_btn"));
        delete bar;
    }

    // A path longer than the room folds from the front, because the end of a
    // path says more about where you are than the start does -- and the fold
    // is a crumb too, offering what it swallowed.
    template<> template<>
    void aljumpbar_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make(600);
        bar->setPath(path());
        ensure_equals("all four fit", crumbsIn(bar), 4);

        bar->reshape(150, 22);
        ensure("fewer than four are shown", crumbsIn(bar) < 4);
        ensure("the last step is still one of them",
               bar->findChild<LLView>("crumb_3", true) != nullptr);
        ensure("and the first is not",
               bar->findChild<LLView>("crumb_0", true) == nullptr);

        // Everything shown is inside the room there is.
        for (LLView* child : *bar->getChildList())
        {
            if (child->getName().rfind("crumb_", 0) == 0)
            {
                ensure("inside the bar: " + child->getName(), child->getRect().mRight <= 150);
            }
        }

        bar->reshape(600, 22);
        ensure_equals("and back again when there is room", crumbsIn(bar), 4);
        delete bar;
    }

    // What is said past the end of the path is not a step, so it is not
    // something to press.
    template<> template<>
    void aljumpbar_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        ensure("nothing said by default", bar->findChild<LLView>("trailer", true) == nullptr);

        bar->setTrailer("default/en");
        const LLView* trailer = bar->findChild<LLView>("trailer", true);
        ensure("said", trailer != nullptr);
        ensure("past the end of the path",
               trailer->getRect().mLeft >= bar->getChild<LLView>("crumb_3")->getRect().mRight);
        ensure("and it is not a button", trailer->as<LLButton>() == nullptr);
        delete bar;
    }

    // Going somewhere is a new path, and a caller sets it from inside the
    // press on the crumb that chose it. The crumb is still on the stack, so
    // the path is built again once the press is over.
    template<> template<>
    void aljumpbar_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        std::vector<std::string> went;
        bar->onChose([bar, &went](size_t at, const std::string& value)
        {
            went.push_back(value);
            // Where a crumb goes is a shorter path: up to it.
            std::vector<ALJumpBar::Crumb> shorter = aljumpbar_data::path();
            shorter.resize(at + 1);
            bar->setPath(std::move(shorter));
        });

        LLButton* body = bar->getChild<LLButton>("crumb_1");
        const S32 x = body->getRect().getWidth() / 2;
        const S32 y = body->getRect().getHeight() / 2;
        body->handleMouseDown(x, y, MASK_NONE);
        body->handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the crumb was chosen", went.size(), 1u);
        ensure_equals("and said where", went.front(), std::string("body"));
        ensure_equals("the path is still the old one while the press is over", crumbsIn(bar), 4);

        gIdleCallbacks.callFunctions();
        ensure_equals("and then the new one", crumbsIn(bar), 2);
        ensure("of which the last is where it went", bar->findChild<LLView>("crumb_1") != nullptr);
        delete bar;
    }

    // What is said past the path may be in pieces, and a piece with a value
    // is a way somewhere: pressing it says the value. A piece without one is
    // only said, and the same pieces said again build nothing again.
    template<> template<>
    void aljumpbar_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        std::vector<std::string> chosen;
        bar->onTrailerChosen([&chosen](const std::string& value) { chosen.push_back(value); });
        const std::vector<ALJumpBar::TrailerPart> parts = { { "Ln 3, Col 1", "line", "Go to a line" },
                                                            { "   ", std::string(), std::string() },
                                                            { "2 errors", "problems", "The problems" } };
        bar->setTrailer(parts);
        LLTextBox* line     = bar->findChild<LLTextBox>("trailer_0", true);
        LLTextBox* gap      = bar->findChild<LLTextBox>("trailer_1", true);
        LLTextBox* problems = bar->findChild<LLTextBox>("trailer_2", true);
        ensure("each piece", line && gap && problems);
        ensure("in the order said", line->getRect().mRight <= problems->getRect().mLeft);
        ensure("past the end of the path", bar->getChild<LLView>("trailer")->getRect().mLeft >= bar->getChild<LLView>("crumb_3")->getRect().mRight);

        const auto press = [](LLTextBox* piece) {
            const S32 x = piece->getRect().getWidth() / 2;
            const S32 y = piece->getRect().getHeight() / 2;
            piece->handleMouseDown(x, y, MASK_NONE);
            piece->handleMouseUp(x, y, MASK_NONE);
        };
        press(problems);
        ensure_equals("said once", chosen.size(), 1u);
        ensure_equals("with the piece's value", chosen.back(), std::string("problems"));
        press(gap);
        ensure_equals("a piece with no value says nothing", chosen.size(), 1u);
        press(line);
        ensure_equals("and the other says its own", chosen.back(), std::string("line"));

        const LLView* before = bar->findChild<LLView>("trailer", true);
        bar->setTrailer(parts);
        ensure("the same pieces again are the same pieces", bar->findChild<LLView>("trailer", true) == before);

        // Other words in the same pieces -- the caret moving -- change the
        // words where they are: nothing of the path is built again.
        const LLView* crumb = bar->findChild<LLView>("crumb_0", true);
        std::vector<ALJumpBar::TrailerPart> moved = parts;
        moved[0].text = "Ln 120, Col 14";
        bar->setTrailer(moved);
        ensure("the same trailer", bar->findChild<LLView>("trailer", true) == before);
        ensure("the same path", bar->findChild<LLView>("crumb_0", true) == crumb);
        LLTextBox* again = bar->findChild<LLTextBox>("trailer_0", true);
        ensure("the same piece", again == line);
        ensure_equals("with the new words", again->getText(), std::string("Ln 120, Col 14"));
        ensure("still in order", again->getRect().mRight <= bar->findChild<LLTextBox>("trailer_2", true)->getRect().mLeft);
        ensure("and against the far edge", bar->findChild<LLTextBox>("trailer_2", true)->getRect().mRight <= bar->getChild<LLView>("trailer")->getRect().getWidth());

        // Another piece, and the trailer is built again.
        moved.push_back({ "1 warning", "problems", "The problems" });
        bar->setTrailer(moved);
        ensure("a fourth piece", bar->findChild<LLTextBox>("trailer_3", true) != nullptr);
        delete bar;
    }
    // The same path said again is the same bar: a caller says it whenever
    // its reasons for asking move -- a check come back -- and building it
    // again would close a crumb's list under the person choosing from it.
    // Another path is built afresh.
    template<> template<>
    void aljumpbar_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        LLView* crumb = bar->findChild<LLView>("crumb_3", false);
        ensure("a crumb", crumb != nullptr);
        const LLHandle<LLView> held = crumb->getHandle();

        bar->setPath(path());
        ensure("said again, the crumb is the one it was", !held.isDead() && bar->findChild<LLView>("crumb_3", false) == crumb);

        std::vector<ALJumpBar::Crumb> other = path();
        other[3].alternatives.push_back({ "ok_btn", "ok_btn" });
        bar->setPath(other);
        ensure("another path, built again", held.isDead());
        ensure_equals("with as many crumbs", crumbsIn(bar), 4);
        delete bar;
    }
    // A piece may be said in a colour of its own -- a warning's -- and the
    // colour changing builds the piece again in the new one.
    template<> template<>
    void aljumpbar_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        std::vector<ALJumpBar::TrailerPart> parts = { { "Ln 3, Col 1", "line", "Go to a line" }, { "200 KB of 256 KB", std::string(), "The size" } };
        bar->setTrailer(parts);
        LLTextBox* size = bar->findChild<LLTextBox>("trailer_1", true);
        ensure("a piece", size != nullptr);
        const LLColor4 quiet = size->getColor().get();

        parts[1].color = LLColor4::yellow;
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        ensure("in its colour", size && size->getColor().get() == LLColor4::yellow);
        ensure("the others as they were", bar->findChild<LLTextBox>("trailer_0", true)->getColor().get() == quiet);

        parts[1].color = LLColor4::red;
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        ensure("in another", size && size->getColor().get() == LLColor4::red);

        parts[1].color.reset();
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        ensure("and back in the quiet ink", size && size->getColor().get() == quiet);
        delete bar;
    }
    // Too narrow for all of the trailer, the pieces that matter least go
    // first, a separator with them; one that never goes stays; widened,
    // all are said again. The path keeps its last step.
    template<> template<>
    void aljumpbar_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const std::string                 dot = "   \xC2\xB7   ";
        ALJumpBar::TrailerPart            sep{ dot, std::string(), std::string() };
        sep.between = true;
        std::vector<ALJumpBar::TrailerPart> parts = { { "Ln 3, Col 1", "line", "Go to a line" }, sep, { "Spaces: 4", "indent", "" }, sep,
                                                      { "2 errors", "problems", "" },            sep, { "Source", "expanded", "" } };
        parts[2].drop = 3;
        parts[4].drop = 1;
        parts[6].drop = 2;
        const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
        const S32       two  = font->getWidth(parts[0].text) + font->getWidth(dot) + font->getWidth(parts[4].text);
        ALJumpBar*      bar  = make(two + 12 + 6 + 4);
        bar->setTrailer(parts);
        const auto said = [&](size_t i) { return bar->findChild<LLTextBox>("trailer_" + std::to_string(i), true)->getVisible(); };
        ensure("the place and the errors, one separator between", said(0) && said(1) && said(4) && !said(3) && !said(5));
        ensure("the indentation and the view gone", !said(2) && !said(6));
        ensure("against the far edge", bar->findChild<LLTextBox>("trailer_4", true)->getRect().mRight <= bar->getChild<LLView>("trailer")->getRect().getWidth());

        bar->reshape(two * 4, 22);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            ensure("widened, all said", said(i));
        }

        bar->reshape(two + 12 + 6 + 4, 22);
        bar->setPath(path());
        ensure("with a path, its last step kept", bar->findChild<LLView>("crumb_3", false) != nullptr);
        ensure("and the place, which never goes", said(0) && !said(4) && !said(1));
        delete bar;
    }
}
