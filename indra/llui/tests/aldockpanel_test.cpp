/**
 * @file aldockpanel_test.cpp
 * @brief A pane out of its window, and back, and what it takes with it.
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

#include "aldockpanel.h"

#include "../llbutton.h"
#include "../lllayoutstack.h"
#include "../llpanel.h"
#include "../lltabcontainer.h"
#include "../lluictrlfactory.h"

#include "llmortician.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <string>

namespace
{
    struct aldockpanel_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // A pane still out is put back, its window closed as the case on
        // closing a pane's window shows that does, and each test's dead views
        // reaped as it ends, as the next frame would: the quitting case
        // empties the graveyard itself, and then finds only its own views
        // there.
        ~aldockpanel_data()
        {
            if (!ui.ok())
            {
                return;
            }
            std::vector<LLHandle<LLFloater>> out;
            for (LLView* child : *gFloaterView->getChildList())
            {
                if (ALPanelFloater* window = child->as<ALPanelFloater>(); window && !window->isDead())
                {
                    out.push_back(window->getHandle());
                }
            }
            for (const LLHandle<LLFloater>& handle : out)
            {
                if (LLFloater* window = handle.get())
                {
                    window->closeFloater();
                }
            }
            LLMortician::updateClass();
        }

        // A window that says which keys it was asked about, and takes only
        // Control-F.
        struct Home : public LLFloater
        {
            Home(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
            bool handleKeyHere(KEY key, MASK mask) override
            {
                heard.push_back(key);
                return key == 'F' && mask == MASK_CONTROL;
            }
            std::vector<KEY> heard;
        };

        struct Window
        {
            LLFloater*      floater = nullptr;
            LLLayoutStack*  stack = nullptr;
            LLLayoutPanel*  side = nullptr;
            LLLayoutPanel*  middle = nullptr;
            LLButton*       inside = nullptr;   // something to look for by name
        };

        // A window with a stack of two panes across it, the way a tool is
        // laid out: a side pane of a fixed width and a middle that takes
        // whatever is left.
        Window build(S32 width = 600, S32 height = 400, S32 side_width = 200)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            Window w;
            LLFloater::Params fp(LLFloater::getDefaultParams());
            fp.name = "tool";
            fp.rect = LLRect(0, height, width, 0);
            fp.save_rect = false;
            fp.save_visibility = false;
            w.floater = new Home(fp);

            LLLayoutStack::Params sp(LLUICtrlFactory::getDefaultParams<LLLayoutStack>());
            sp.name = "stack";
            sp.rect = LLRect(0, height, width, 0);
            sp.orientation = "horizontal";
            sp.animate = false;
            w.stack = LLUICtrlFactory::create<LLLayoutStack>(sp);
            w.floater->addChild(w.stack);

            LLLayoutPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            pp.name = "side";
            pp.rect = LLRect(0, height, side_width, 0);
            pp.auto_resize = false;
            pp.min_dim = 40;
            w.side = LLUICtrlFactory::create<LLLayoutPanel>(pp);
            w.stack->addChild(w.side);

            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = "in_the_side";
            bp.rect = LLRect(0, 20, 80, 0);
            w.inside = LLUICtrlFactory::create<LLButton>(bp);
            w.side->addChild(w.inside);

            LLLayoutPanel::Params mp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            mp.name = "middle";
            mp.rect = LLRect(0, height, width - side_width, 0);
            mp.auto_resize = true;
            w.middle = LLUICtrlFactory::create<LLLayoutPanel>(mp);
            w.stack->addChild(w.middle);

            w.stack->updateLayout();
            return w;
        }
    };

    // Wrapping gains the tree a level and moves nothing that anybody was
    // looking for: a window given panes that can come out does not have to
    // be declared again.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: wrap adds a level and the window still finds what was inside", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        UNSCOPED_INFO("it is in the layout panel");
        CHECK(pane->getParent() == (LLView*)w.side);
        UNSCOPED_INFO("and what was there is in it");
        CHECK(w.inside->getParent() == (LLView*)pane);
        UNSCOPED_INFO("the window still finds it by name");
        CHECK(w.floater->findChild<LLButton>("in_the_side", true) == w.inside);
        w.floater->die();
    }

    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: popOut puts the pane in a window of its own and gives its room away", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        const S32 was = w.middle->getRect().getWidth();

        pane->popOut();
        CHECK(pane->poppedOut());
        UNSCOPED_INFO("it is not in the window any more");
        CHECK(pane->getParent() != (LLView*)w.side);
        UNSCOPED_INFO("it is in a window of its own");
        CHECK(pane->getParentByType<ALPanelFloater>() != nullptr);
        UNSCOPED_INFO("the pane it came out of is folded away");
        CHECK(w.side->isCollapsed());

        w.stack->updateLayout();
        UNSCOPED_INFO("and the rest of the stack took the room");
        CHECK(w.middle->getRect().getWidth() > was);
        w.floater->die();
    }

    // Back: where it was is remembered here and nowhere else, so it goes
    // back to the same parent at the same size, and the stack gives it the
    // room it had.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: dock puts the pane back in its parent at the size it had", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        const S32 dim = w.side->getTargetDim();

        pane->popOut();
        // The frame the pane is out for: a collapsed panel is laid out at no
        // size, and a panel that does not resize itself takes the size it is
        // laid out at as the size it wants. That is where the number goes.
        w.stack->updateLayout();
        pane->dock();
        w.stack->updateLayout();

        CHECK_FALSE(pane->poppedOut());
        UNSCOPED_INFO("in the parent it came from");
        CHECK(pane->getParent() == (LLView*)w.side);
        UNSCOPED_INFO("and the room came back with it");
        CHECK_FALSE(w.side->isCollapsed());
        UNSCOPED_INFO("at the size it had");
        CHECK(w.side->getTargetDim() == dim);
        UNSCOPED_INFO("the window finds what is in it again");
        CHECK(w.floater->findChild<LLButton>("in_the_side", true) == w.inside);
        w.floater->die();
    }

    // Closing the window a pane is borrowing puts the pane back rather than
    // taking it down: the window is the borrower, and the pane belongs to
    // the tool.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: closing a pane's window docks the pane rather than killing it", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        pane->popOut();

        ALPanelFloater* window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        window->closeFloater();

        UNSCOPED_INFO("the pane came back");
        CHECK_FALSE(pane->poppedOut());
        UNSCOPED_INFO("to where it was");
        CHECK(pane->getParent() == (LLView*)w.side);
        UNSCOPED_INFO("and it is still alive");
        CHECK_FALSE(pane->isDead());
        w.floater->die();
    }

    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: popping out twice is once and docking a docked pane does nothing", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);

        pane->dock();
        UNSCOPED_INFO("still where it was declared");
        CHECK(pane->getParent() == (LLView*)w.side);

        pane->popOut();
        LLView* window = pane->getParent();
        pane->popOut();
        UNSCOPED_INFO("one window, not two");
        CHECK(pane->getParent() == window);
        w.floater->die();
    }

    // A pane out of its window is a different focused floater, so the keys
    // pressed over it go home to the window it came from -- otherwise every
    // accelerator quietly stops working the moment a pane is popped.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: keys over a popped-out pane go to the window it came from", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        pane->popOut();

        ALPanelFloater* window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        UNSCOPED_INFO("and it knows where home is");
        CHECK(window->home() == w.floater);
        UNSCOPED_INFO("it asks before the viewer's own menu does");
        CHECK(window->hasAccelerators());
        const std::vector<KEY>& heard = static_cast<Home*>(w.floater)->heard;
        UNSCOPED_INFO("a key home takes is taken");
        CHECK(window->handleKeyHere('F', MASK_CONTROL));
        UNSCOPED_INFO("home heard it");
        CHECK(heard == std::vector<KEY>{ 'F' });
        UNSCOPED_INFO("a key home declines is declined");
        CHECK_FALSE(window->handleKeyHere('Q', MASK_CONTROL));
        UNSCOPED_INFO("having been asked");
        CHECK(heard == std::vector<KEY>{ 'F', 'Q' });
        UNSCOPED_INFO("a key the window takes itself is taken");
        CHECK(window->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        UNSCOPED_INFO("and never reaches home");
        CHECK(heard == std::vector<KEY>{ 'F', 'Q' });
        w.floater->die();
    }

    // A pane goes under the window's title bar, not over it. A pane given
    // the whole of a floater covers the title, the buttons and the handle it
    // is dragged by -- and whatever the pane draws along its own top shares
    // a band with them.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: a popped-out pane sits under its window's title bar", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        const S32 was = pane->getRect().getHeight();
        pane->popOut();

        LLFloater* window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        UNSCOPED_INFO("the title bar has a band of its own");
        CHECK(window->getHeaderHeight() > 0);
        UNSCOPED_INFO("and the pane starts below it");
        CHECK(pane->getRect().mTop == window->getRect().getHeight() - window->getHeaderHeight());
        UNSCOPED_INFO("the pane still starts at the left");
        CHECK(pane->getRect().mLeft == 0);
        UNSCOPED_INFO("and reaches the right");
        CHECK(pane->getRect().mRight == window->getRect().getWidth());

        // The window is the pane and the bar, so the pane keeps the height
        // it had rather than losing the bar's band out of it.
        UNSCOPED_INFO("the pane kept its height");
        CHECK(pane->getRect().getHeight() == was);
        w.floater->die();
    }

    // Where it is while it is out survives going home and coming out again,
    // so a developer who put it on the other monitor finds it there.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: a pane pops out again where its window was last moved to", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        pane->popOut();

        LLFloater* window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        const LLRect moved(300, 700, 800, 300);
        window->setShape(moved);
        pane->dock();

        UNSCOPED_INFO("where it was is remembered");
        CHECK(pane->floatingRect().getWidth() == moved.getWidth());
        pane->popOut();
        window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        UNSCOPED_INFO("and it comes back out there");
        CHECK(window->getRect().getWidth() == moved.getWidth());
        w.floater->die();
    }

    // Back after the window changed size while the pane was out: the pane
    // fills the room it comes back to as it is now, not as it was, since
    // it follows every later resize from wherever it was put.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: docked after its window resized a pane fills its room as it is now", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);

        pane->popOut();
        w.stack->updateLayout();
        w.floater->reshape(600, 500);
        w.stack->reshape(600, 500);
        w.stack->updateLayout();
        pane->dock();
        w.stack->updateLayout();

        UNSCOPED_INFO("in the parent it came from");
        CHECK(pane->getParent() == (LLView*)w.side);
        UNSCOPED_INFO("and as tall as it is now");
        CHECK(pane->getRect() == w.side->getLocalRect());
        w.floater->die();
    }

    // A tab container's page, which is a place a pane is wanted as much as a
    // layout panel is: everything on the page goes out and comes back, the
    // page is filled again when it does, and the tab strip never notices --
    // the page it holds stays the page it holds, out or in.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: a tab page pops out and back while the tab strip keeps the page", "[aldockpanel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLFloater::Params fp(LLFloater::getDefaultParams());
        fp.name = "tabbed";
        fp.rect = LLRect(0, 300, 400, 0);
        fp.save_rect = false;
        fp.save_visibility = false;
        LLFloater* floater = new LLFloater(LLSD(), fp);

        LLTabContainer::Params tp(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        tp.name = "tabs";
        tp.rect = LLRect(0, 280, 400, 0);
        tp.tab_position = LLTabContainer::TOP;
        tp.follows.flags = FOLLOWS_ALL;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(tp);
        floater->addChild(tabs);

        LLPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        pp.rect = LLRect(0, 260, 400, 0);
        pp.name = "page_one";
        pp.label = "One";
        LLPanel* one = LLUICtrlFactory::create<LLPanel>(pp);
        pp.name = "page_two";
        pp.label = "Two";
        LLPanel* two = LLUICtrlFactory::create<LLPanel>(pp);

        LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
        bp.name = "on_the_page";
        bp.rect = LLRect(10, 40, 90, 20);
        LLButton* on_page = LLUICtrlFactory::create<LLButton>(bp);
        one->addChild(on_page);

        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(one).select_tab(true));
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(two));

        ALDockPanel* pane = ALDockPanel::wrap(one, "One");
        UNSCOPED_INFO("what was on the page is in the pane");
        REQUIRE(on_page->getParent() == (LLView*)pane);

        pane->popOut();
        CHECK(pane->poppedOut());
        UNSCOPED_INFO("in a window of its own");
        CHECK(pane->getParentByType<ALPanelFloater>() != nullptr);
        UNSCOPED_INFO("and the strip still has both pages");
        CHECK(tabs->getTabCount() == 2);
        UNSCOPED_INFO("the page it left is still a page");
        CHECK(tabs->getIndexForPanel(one) >= 0);

        tabs->selectTabPanel(two);
        UNSCOPED_INFO("the tabs still change while it is out");
        CHECK(tabs->getCurrentPanel() == two);
        tabs->selectTabPanel(one);
        UNSCOPED_INFO("including back to the empty page");
        CHECK(tabs->getCurrentPanel() == one);

        pane->dock();
        CHECK_FALSE(pane->poppedOut());
        UNSCOPED_INFO("on the page it came from");
        CHECK(pane->getParent() == (LLView*)one);
        UNSCOPED_INFO("filling it");
        CHECK(pane->getRect() == one->getLocalRect());
        UNSCOPED_INFO("with what was on it");
        CHECK(floater->findChild<LLButton>("on_the_page", true) == on_page);
        UNSCOPED_INFO("and its tab shows it");
        CHECK(tabs->getCurrentPanel() == one);
        floater->die();
    }

    // The viewer quitting closes every window, the one a pane is borrowing
    // among them, and a window asking about unsaved work may hold the quit
    // up or call it off: the pane goes home all the same, and outlives the
    // window it was in. It remembers it was out, for the next session, and
    // forgets that as soon as it is moved for any other reason.
    TEST_CASE_METHOD(aldockpanel_data, "aldockpanel: quitting docks a popped-out pane and outAtQuit says it was out", "[aldockpanel]")
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        REQUIRE(pane != nullptr);
        const LLHandle<LLView> held = pane->getHandle();
        pane->popOut();

        ALPanelFloater* window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        const LLHandle<LLFloater> window_held = window->getHandle();
        window->closeFloater(true);
        LLMortician::updateClass();

        UNSCOPED_INFO("the window is gone");
        CHECK(window_held.isDead());
        UNSCOPED_INFO("the pane is not");
        CHECK_FALSE(held.isDead());
        UNSCOPED_INFO("it is home");
        CHECK(pane->getParent() == (LLView*)w.side);
        UNSCOPED_INFO("no longer out");
        CHECK_FALSE(pane->poppedOut());
        UNSCOPED_INFO("but out when the viewer went");
        CHECK(pane->outAtQuit());
        UNSCOPED_INFO("and the window still finds what is in it");
        CHECK(w.floater->findChild<LLButton>("in_the_side", true) == w.inside);

        pane->popOut();
        {
            INFO("out again, it is simply out");
            REQUIRE(pane->poppedOut());
            CHECK_FALSE(pane->outAtQuit());
        }
        window = pane->getParentByType<ALPanelFloater>();
        REQUIRE(window != nullptr);
        window->closeFloater();
        {
            INFO("closed by hand, it is simply home");
            CHECK_FALSE(pane->poppedOut());
            CHECK_FALSE(pane->outAtQuit());
        }
        w.floater->die();
    }
}
