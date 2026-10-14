/**
 * @file alpanefolds_test.cpp
 * @brief ALPaneFolds: regions of a window that fold, and that come out into windows of their own
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

#include "alpanefolds.h"

#include "aldockpanel.h"
#include "../llbutton.h"
#include "../lllayoutstack.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct alpanefolds_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A window with a stack of two layout panels, and beside it a plain
        // panel that is in no stack: the two kinds of region.
        struct Window
        {
            LLFloater*      floater = nullptr;
            LLLayoutPanel*  side = nullptr;
            LLButton*       fold = nullptr;
            LLPanel*        page = nullptr;
            LLButton*       in_page = nullptr;
        };

        static Window build()
        {
            Window w;
            LLFloater::Params fp(LLFloater::getDefaultParams());
            fp.name = "tool";
            fp.rect = LLRect(0, 400, 600, 0);
            fp.save_rect = false;
            fp.save_visibility = false;
            w.floater = new LLFloater(LLSD(), fp);

            LLLayoutStack::Params sp(LLUICtrlFactory::getDefaultParams<LLLayoutStack>());
            sp.name = "stack";
            sp.rect = LLRect(0, 300, 600, 0);
            sp.orientation = "horizontal";
            sp.animate = false;
            LLLayoutStack* stack = LLUICtrlFactory::create<LLLayoutStack>(sp);
            w.floater->addChild(stack);

            LLLayoutPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            pp.name = "side";
            pp.rect = LLRect(0, 300, 200, 0);
            pp.auto_resize = false;
            pp.min_dim = 40;
            w.side = LLUICtrlFactory::create<LLLayoutPanel>(pp);
            stack->addChild(w.side);

            LLLayoutPanel::Params mp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            mp.name = "middle";
            mp.rect = LLRect(0, 300, 400, 0);
            mp.auto_resize = true;
            stack->addChild(LLUICtrlFactory::create<LLLayoutPanel>(mp));
            stack->updateLayout();

            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = "fold_side";
            bp.rect = LLRect(0, 320, 40, 300);
            w.fold = LLUICtrlFactory::create<LLButton>(bp);
            w.floater->addChild(w.fold);

            LLPanel::Params gp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            gp.name = "page";
            gp.rect = LLRect(0, 400, 600, 320);
            w.page = LLUICtrlFactory::create<LLPanel>(gp);
            w.floater->addChild(w.page);

            LLButton::Params ip(LLUICtrlFactory::getDefaultParams<LLButton>());
            ip.name = "in_the_page";
            ip.rect = LLRect(0, 20, 80, 0);
            w.in_page = LLUICtrlFactory::create<LLButton>(ip);
            w.page->addChild(w.in_page);

            return w;
        }

        static std::vector<ALPaneFolds::Pane> panes()
        {
            return { { "side", "side", "fold_side", "Side" },
                     { "page", "page", "", "Page" } };
        }
    };

    // A layout panel folds and has a size; a plain panel does neither, and
    // asking does nothing rather than something wrong.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: a layout panel folds and has a size; a plain panel does neither", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        UNSCOPED_INFO("the side is open");
        CHECK_FALSE(folds.collapsed("side"));
        folds.toggle("side");
        CHECK(folds.collapsed("side"));
        UNSCOPED_INFO("its button says it is folded");
        CHECK_FALSE(w.fold->getToggleState());
        CHECK(folds.dim("side") == 200);

        UNSCOPED_INFO("the page is never folded");
        CHECK_FALSE(folds.collapsed("page"));
        folds.toggle("page");
        UNSCOPED_INFO("nor can it be");
        CHECK_FALSE(folds.collapsed("page"));
        CHECK(folds.dim("page") == 0);
        w.floater->die();
    }

    // Either kind comes out into a window of its own and goes back, with
    // what it holds still where it was.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: either kind comes out into a window of its own and goes back whole", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        {
            INFO("home to start");
            CHECK_FALSE(folds.out("page"));
            CHECK(folds.window("page") == nullptr);
        }
        folds.toggleOut("page");
        CHECK(folds.out("page"));
        LLFloater* window = folds.window("page");
        {
            INFO("in a window of its own");
            REQUIRE(window != nullptr);
            CHECK(window != w.floater);
        }
        UNSCOPED_INFO("with what it held");
        CHECK(w.in_page->getParentByType<LLFloater>() == window);
        UNSCOPED_INFO("and the page itself still on the window");
        CHECK(w.page->getParent() == w.floater);

        folds.toggleOut("side");
        UNSCOPED_INFO("the layout panel comes out too");
        CHECK(folds.out("side"));

        folds.dockAll();
        {
            INFO("and both go home");
            CHECK_FALSE(folds.out("page"));
            CHECK_FALSE(folds.out("side"));
        }
        UNSCOPED_INFO("the button back under its page");
        CHECK(w.in_page->getParentByType<LLFloater>() == w.floater);
        w.floater->die();
    }

    // Written down: a fold and a size for the layout panel only, and for
    // both whether it is out and where its window was. Read back, a region
    // that was out comes out again where it was.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: a save writes folds and sizes for layout panels only and what is out", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLSD state;
        {
            Window w = build();
            ALPaneFolds folds;
            folds.bind(w.floater, panes());
            folds.toggleOut("page");
            LLFloater* window = folds.window("page");
            REQUIRE(window != nullptr);
            window->setRect(LLRect(700, 500, 1000, 300));
            folds.save(state);
            UNSCOPED_INFO("the side's fold is written");
            CHECK(state.has("fold_side"));
            UNSCOPED_INFO("and its size");
            CHECK(state.has("dim_side"));
            UNSCOPED_INFO("the page has no fold to write");
            CHECK_FALSE(state.has("fold_page"));
            UNSCOPED_INFO("nor a size");
            CHECK_FALSE(state.has("dim_page"));
            UNSCOPED_INFO("but it is written as out");
            CHECK(state["out_page"].asBoolean());
            UNSCOPED_INFO("with its window's rect");
            CHECK(state.has("rect_page"));
            w.floater->die();
        }
        {
            Window w = build();
            ALPaneFolds folds;
            folds.bind(w.floater, panes());
            folds.load(state);
            UNSCOPED_INFO("read back, the page is out again");
            CHECK(folds.out("page"));
            LLFloater* window = folds.window("page");
            REQUIRE(window != nullptr);
            UNSCOPED_INFO("as wide as it was");
            CHECK(window->getRect().getWidth() == 300);
            UNSCOPED_INFO("and the side is home");
            CHECK_FALSE(folds.out("side"));
            w.floater->die();
        }
    }
    // A region out in a window of its own is showing, whatever became of
    // the room it left: asked to unfold, the studio's own Show Problems,
    // it does not open that room as an empty band, and folding it puts it
    // back, folded. Its button says it shows while it is out.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: a region out in a window of its own shows though its room is folded", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        folds.toggleOut("side");
        CHECK(folds.out("side"));
        UNSCOPED_INFO("the room it left is folded");
        CHECK(w.side->isCollapsed());
        UNSCOPED_INFO("but the region is showing");
        CHECK_FALSE(folds.collapsed("side"));
        UNSCOPED_INFO("and its button says so");
        CHECK(w.fold->getToggleState());

        folds.setCollapsed("side", false);
        UNSCOPED_INFO("unfolding it leaves it out");
        CHECK(folds.out("side"));
        UNSCOPED_INFO("and the room it left folded");
        CHECK(w.side->isCollapsed());

        folds.setCollapsed("side", true);
        UNSCOPED_INFO("folding it puts it back");
        CHECK_FALSE(folds.out("side"));
        UNSCOPED_INFO("into its room");
        REQUIRE(w.side->findChild<ALDockPanel>("side_pane", false) != nullptr);
        {
            INFO("folded");
            CHECK(folds.collapsed("side"));
            CHECK(w.side->isCollapsed());
        }
        UNSCOPED_INFO("and its button says that");
        CHECK_FALSE(w.fold->getToggleState());

        folds.toggleOut("side");
        folds.toggle("side");
        {
            INFO("the button's own press does the same");
            CHECK_FALSE(folds.out("side"));
            CHECK(folds.collapsed("side"));
        }
        w.floater->die();
    }

    // A region put back only because the viewer was quitting is written
    // down as out, so that the next session puts it out again; and where
    // the quit is called off, it goes out again now.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: a region put back only by quitting is saved as out and can go out again", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        folds.toggleOut("page");
        LLFloater* window = folds.window("page");
        REQUIRE(window != nullptr);
        window->closeFloater(true);
        CHECK_FALSE(folds.out("page"));

        LLSD state;
        folds.save(state);
        UNSCOPED_INFO("written as out all the same");
        CHECK(state["out_page"].asBoolean());

        folds.putBackOut();
        UNSCOPED_INFO("the quit called off, it is out again");
        CHECK(folds.out("page"));
        folds.dockAll();
        LLSD after;
        folds.save(after);
        UNSCOPED_INFO("put back by hand, it is written as home");
        CHECK_FALSE(after["out_page"].asBoolean());
        folds.putBackOut();
        UNSCOPED_INFO("and stays there");
        CHECK_FALSE(folds.out("page"));
        w.floater->die();
    }

    // In sight: shown up to its window, in no folded panel, its window not
    // minimized; out in a window of its own, whatever became of its room.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: inSight needs it shown up to an unminimized window and in no fold", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window          w = build();
        LLPanel::Params ip(LLUICtrlFactory::getDefaultParams<LLPanel>());
        ip.name          = "in_the_side";
        ip.rect          = LLRect(0, 100, 100, 0);
        LLPanel* in_side = LLUICtrlFactory::create<LLPanel>(ip);
        w.side->addChild(in_side);
        ALPaneFolds folds;
        folds.bind(w.floater, panes());
        gFloaterView->addChild(w.floater);
        w.floater->setVisible(true);

        CHECK_FALSE(ALPaneFolds::inSight(nullptr));
        CHECK(ALPaneFolds::inSight(w.in_page));
        CHECK(ALPaneFolds::inSight(in_side));
        folds.toggle("side");
        {
            INFO("the side folded: not");
            CHECK_FALSE(ALPaneFolds::inSight(in_side));
            CHECK(ALPaneFolds::inSight(w.in_page));
        }
        folds.toggleOut("side");
        {
            INFO("out in a window of its own: it is");
            CHECK(folds.out("side"));
            CHECK(ALPaneFolds::inSight(in_side));
        }
        folds.dockAll();
        UNSCOPED_INFO("home again, as its fold says");
        CHECK(ALPaneFolds::inSight(in_side) == !folds.collapsed("side"));
        folds.setCollapsed("side", true);
        UNSCOPED_INFO("folded at home: not");
        CHECK_FALSE(ALPaneFolds::inSight(in_side));
        folds.setCollapsed("side", false);
        CHECK(ALPaneFolds::inSight(in_side));

        w.page->setVisible(false);
        UNSCOPED_INFO("its page hidden: not");
        CHECK_FALSE(ALPaneFolds::inSight(w.in_page));
        w.page->setVisible(true);
        w.floater->setMinimized(true);
        {
            INFO("the window minimized: not");
            CHECK_FALSE(ALPaneFolds::inSight(w.in_page));
            CHECK_FALSE(ALPaneFolds::inSight(in_side));
        }
        w.floater->setMinimized(false);
        CHECK(ALPaneFolds::inSight(w.in_page));
        w.floater->setVisible(false);
        UNSCOPED_INFO("the window hidden: not");
        CHECK_FALSE(ALPaneFolds::inSight(w.in_page));
        gFloaterView->removeChild(w.floater);
        w.floater->die();
    }
    // Whether the regions are the sizes given is said with nothing made to
    // say it, and says what comparing dims() would.
    TEST_CASE_METHOD(alpanefolds_data, "alpanefolds: dimsAre answers as comparing dims would", "[alpanefolds]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());
        const std::vector<S32> was = folds.dims();
        CHECK(folds.dimsAre(was));
        folds.setDim("side", 240);
        UNSCOPED_INFO("not after one changes");
        CHECK_FALSE(folds.dimsAre(was));
        CHECK(folds.dimsAre(folds.dims()));
        UNSCOPED_INFO("nor fewer sizes than regions");
        CHECK_FALSE(folds.dimsAre({}));
        w.floater->die();
    }
}
