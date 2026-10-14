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

#include <catch2/catch_test_macros.hpp>

namespace
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

    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: one crumb per step in order with an arrow where a step has siblings", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        CHECK(crumbsIn(bar) == 4);
        const LLView* first = bar->findChild<LLView>("crumb_0", false);
        REQUIRE(first != nullptr);
        UNSCOPED_INFO("a step with nowhere else to be is a plain button");
        CHECK(first->as<LLButton>() != nullptr);
        UNSCOPED_INFO("and one with siblings offers them");
        REQUIRE(bar->getChild<LLView>("crumb_2")->as<LLFlyoutButton>() != nullptr);

        // Left to right, in the order the path reads.
        CHECK(bar->getChild<LLView>("crumb_0")->getRect().mRight
                        <= bar->getChild<LLView>("crumb_1")->getRect().mLeft);
        delete bar;
    }

    // Pressing a crumb goes there; picking one of its siblings goes there
    // instead. One signal, because they are one gesture with two answers.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: pressing a crumb goes to it and picking a sibling goes there instead", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        std::vector<std::pair<size_t, std::string> > chosen;
        bar->onChose([&chosen](size_t at, const std::string& value)
        {
            chosen.emplace_back(at, value);
        });

        bar->getChild<LLButton>("crumb_1")->onCommit();
        REQUIRE(chosen.size() == 1u);
        CHECK(chosen.back().first == 1u);
        UNSCOPED_INFO("the crumb's own value");
        CHECK(chosen.back().second == std::string("body"));

        // Sideways: the sibling picked is what is answered, not the crumb.
        LLFlyoutButton* sideways = bar->getChild<LLView>("crumb_3")->as<LLFlyoutButton>();
        REQUIRE(sideways != nullptr);
        sideways->setSelectedByValue(LLSD("help_btn"), true);
        sideways->onCommit();
        REQUIRE(chosen.size() == 2u);
        UNSCOPED_INFO("on the same crumb");
        CHECK(chosen.back().first == 3u);
        UNSCOPED_INFO("with the sibling chosen");
        CHECK(chosen.back().second == std::string("help_btn"));
        delete bar;
    }

    // A path longer than the room folds from the front, because the end of a
    // path says more about where you are than the start does -- and the fold
    // is a crumb too, offering what it swallowed.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: a path longer than the room folds from the front", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make(600);
        bar->setPath(path());
        UNSCOPED_INFO("all four fit");
        CHECK(crumbsIn(bar) == 4);

        bar->reshape(150, 22);
        CHECK(crumbsIn(bar) < 4);
        UNSCOPED_INFO("the last step is still one of them");
        REQUIRE(bar->findChild<LLView>("crumb_3", true) != nullptr);
        UNSCOPED_INFO("and the first is not");
        CHECK(bar->findChild<LLView>("crumb_0", true) == nullptr);

        // Everything shown is inside the room there is.
        for (LLView* child : *bar->getChildList())
        {
            if (child->getName().rfind("crumb_", 0) == 0)
            {
                INFO("inside the bar: " << child->getName());
                REQUIRE(child->getRect().mRight <= 150);
            }
        }

        bar->reshape(600, 22);
        UNSCOPED_INFO("all four back when there is room");
        CHECK(crumbsIn(bar) == 4);
        delete bar;
    }

    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: the trailer past the end of the path is not something to press", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        UNSCOPED_INFO("nothing said by default");
        CHECK(bar->findChild<LLView>("trailer", true) == nullptr);

        bar->setTrailer("default/en");
        const LLView* trailer = bar->findChild<LLView>("trailer", true);
        REQUIRE(trailer != nullptr);
        UNSCOPED_INFO("past the end of the path");
        CHECK(trailer->getRect().mLeft >= bar->getChild<LLView>("crumb_3")->getRect().mRight);
        CHECK(trailer->as<LLButton>() == nullptr);
        delete bar;
    }

    // Going somewhere is a new path, and a caller sets it from inside the
    // press on the crumb that chose it. The crumb is still on the stack, so
    // the path is built again once the press is over.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: a path set from inside a crumb's press is built once the press is over", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        REQUIRE(went.size() == 1u);
        CHECK(went.front() == std::string("body"));
        UNSCOPED_INFO("the path is still the old one while the press is over");
        CHECK(crumbsIn(bar) == 4);

        gIdleCallbacks.callFunctions();
        UNSCOPED_INFO("and then the new one");
        CHECK(crumbsIn(bar) == 2);
        UNSCOPED_INFO("of which the last is where it went");
        REQUIRE(bar->findChild<LLView>("crumb_1") != nullptr);
        delete bar;
    }

    // What is said past the path may be in pieces, and a piece with a value
    // is a way somewhere: pressing it says the value. A piece without one is
    // only said, and the same pieces said again build nothing again.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: a trailer piece with a value says it when pressed; same pieces are kept", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        REQUIRE(line);
        REQUIRE(gap);
        REQUIRE(problems);
        UNSCOPED_INFO("in the order said");
        CHECK(line->getRect().mRight <= problems->getRect().mLeft);
        UNSCOPED_INFO("past the end of the path");
        CHECK(bar->getChild<LLView>("trailer")->getRect().mLeft >= bar->getChild<LLView>("crumb_3")->getRect().mRight);

        const auto press = [](LLTextBox* piece) {
            const S32 x = piece->getRect().getWidth() / 2;
            const S32 y = piece->getRect().getHeight() / 2;
            piece->handleMouseDown(x, y, MASK_NONE);
            piece->handleMouseUp(x, y, MASK_NONE);
        };
        press(problems);
        REQUIRE(chosen.size() == 1u);
        CHECK(chosen.back() == std::string("problems"));
        press(gap);
        UNSCOPED_INFO("a piece with no value says nothing");
        CHECK(chosen.size() == 1u);
        press(line);
        CHECK(chosen.back() == std::string("line"));

        const LLView* before = bar->findChild<LLView>("trailer", true);
        bar->setTrailer(parts);
        UNSCOPED_INFO("the same pieces again build nothing again");
        CHECK(bar->findChild<LLView>("trailer", true) == before);

        // Other words in the same pieces -- the caret moving -- change the
        // words where they are: nothing of the path is built again.
        const LLView* crumb = bar->findChild<LLView>("crumb_0", true);
        std::vector<ALJumpBar::TrailerPart> moved = parts;
        moved[0].text = "Ln 120, Col 14";
        bar->setTrailer(moved);
        CHECK(bar->findChild<LLView>("trailer", true) == before);
        CHECK(bar->findChild<LLView>("crumb_0", true) == crumb);
        LLTextBox* again = bar->findChild<LLTextBox>("trailer_0", true);
        REQUIRE(again == line);
        CHECK(again->getText() == std::string("Ln 120, Col 14"));
        const LLTextBox* last = bar->findChild<LLTextBox>("trailer_2", true);
        REQUIRE(last != nullptr);
        UNSCOPED_INFO("still in order");
        CHECK(again->getRect().mRight <= last->getRect().mLeft);
        UNSCOPED_INFO("against the far edge");
        CHECK(last->getRect().mRight <= bar->getChild<LLView>("trailer")->getRect().getWidth());

        // Another piece, and the trailer is built again.
        moved.push_back({ "1 warning", "problems", "The problems" });
        bar->setTrailer(moved);
        REQUIRE(bar->findChild<LLTextBox>("trailer_3", true) != nullptr);
        delete bar;
    }
    // The same path said again is the same bar: a caller says it whenever
    // its reasons for asking move -- a check come back -- and building it
    // again would close a crumb's list under the person choosing from it.
    // Another path is built afresh.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: the same path said again keeps the bar and another rebuilds it", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        LLView* crumb = bar->findChild<LLView>("crumb_3", false);
        REQUIRE(crumb != nullptr);
        const LLHandle<LLView> held = crumb->getHandle();

        bar->setPath(path());
        {
            INFO("said again, the crumb is the one it was");
            CHECK_FALSE(held.isDead());
            CHECK(bar->findChild<LLView>("crumb_3", false) == crumb);
        }

        std::vector<ALJumpBar::Crumb> other = path();
        other[3].alternatives.push_back({ "ok_btn", "ok_btn" });
        bar->setPath(other);
        UNSCOPED_INFO("another path, built again");
        CHECK(held.isDead());
        CHECK(crumbsIn(bar) == 4);
        delete bar;
    }
    // A piece may be said in a colour of its own -- a warning's -- and the
    // colour changing builds the piece again in the new one.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: a trailer piece takes a colour of its own and drops it again", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        std::vector<ALJumpBar::TrailerPart> parts = { { "Ln 3, Col 1", "line", "Go to a line" }, { "200 KB of 256 KB", std::string(), "The size" } };
        bar->setTrailer(parts);
        LLTextBox* size = bar->findChild<LLTextBox>("trailer_1", true);
        REQUIRE(size != nullptr);
        const LLColor4 quiet = size->getColor().get();

        parts[1].color = LLColor4::yellow;
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        {
            INFO("in its colour");
            REQUIRE(size);
            CHECK(size->getColor().get() == LLColor4::yellow);
        }
        const LLTextBox* line = bar->findChild<LLTextBox>("trailer_0", true);
        REQUIRE(line != nullptr);
        UNSCOPED_INFO("the others as they were");
        CHECK(line->getColor().get() == quiet);

        parts[1].color = LLColor4::red;
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        {
            INFO("in another");
            REQUIRE(size);
            CHECK(size->getColor().get() == LLColor4::red);
        }

        parts[1].color.reset();
        bar->setTrailer(parts);
        size = bar->findChild<LLTextBox>("trailer_1", true);
        {
            INFO("and back in the quiet ink");
            REQUIRE(size);
            CHECK(size->getColor().get() == quiet);
        }
        delete bar;
    }
    // Too narrow for all of the trailer, the pieces that matter least go
    // first, a separator with them; one that never goes stays; widened,
    // all are said again. The path keeps its last step.
    TEST_CASE_METHOD(aljumpbar_data, "aljumpbar: a narrow trailer drops the pieces that matter least and a separator each", "[aljumpbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        const auto said = [&](size_t i) {
            const LLTextBox* piece = bar->findChild<LLTextBox>("trailer_" + std::to_string(i), true);
            REQUIRE(piece != nullptr);
            return piece->getVisible();
        };
        {
            INFO("the place and the errors, one separator between");
            CHECK(said(0));
            CHECK(said(1));
            CHECK(said(4));
            CHECK_FALSE(said(3));
            CHECK_FALSE(said(5));
        }
        {
            INFO("the indentation and the view gone");
            CHECK_FALSE(said(2));
            CHECK_FALSE(said(6));
        }
        const LLTextBox* errors = bar->findChild<LLTextBox>("trailer_4", true);
        REQUIRE(errors != nullptr);
        UNSCOPED_INFO("against the far edge");
        CHECK(errors->getRect().mRight <= bar->getChild<LLView>("trailer")->getRect().getWidth());

        bar->reshape(two * 4, 22);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            INFO("widened, all said");
            CAPTURE(i);
            REQUIRE(said(i));
        }

        bar->reshape(two + 12 + 6 + 4, 22);
        bar->setPath(path());
        UNSCOPED_INFO("with a path, its last step kept");
        REQUIRE(bar->findChild<LLView>("crumb_3", false) != nullptr);
        {
            INFO("and the place, which never goes");
            CHECK(said(0));
            CHECK_FALSE(said(4));
            CHECK_FALSE(said(1));
        }
        delete bar;
    }
}
