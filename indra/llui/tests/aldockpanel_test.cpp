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

#include "../aldockpanel.h"

#include "../llbutton.h"
#include "../lllayoutstack.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gDockTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gDockTestAnonName;
}

namespace tut
{
    struct aldockpanel_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A window with a stack of two panes across it, the way a tool is
        // laid out: a side pane of a fixed width and a middle that takes
        // whatever is left.
        struct Window
        {
            LLFloater*      floater = nullptr;
            LLLayoutStack*  stack = nullptr;
            LLLayoutPanel*  side = nullptr;
            LLLayoutPanel*  middle = nullptr;
            LLButton*       inside = nullptr;   // something to look for by name
        };

        static Window build(S32 width = 600, S32 height = 400, S32 side_width = 200)
        {
            Window w;
            LLFloater::Params fp(LLFloater::getDefaultParams());
            fp.name = "tool";
            fp.rect = LLRect(0, height, width, 0);
            fp.save_rect = false;
            fp.save_visibility = false;
            w.floater = new LLFloater(LLSD(), fp);

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

    typedef test_group<aldockpanel_data> aldockpanel_test;
    typedef aldockpanel_test::object     aldockpanel_object;
    tut::aldockpanel_test aldockpanel_testgroup("aldockpanel");

    // Wrapping gains the tree a level and moves nothing that anybody was
    // looking for: a window given panes that can come out does not have to
    // be declared again.
    template<> template<>
    void aldockpanel_object::test<1>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        ensure("there is a pane", pane != nullptr);
        ensure_equals("it is in the layout panel", pane->getParent(), (LLView*)w.side);
        ensure_equals("and what was there is in it", w.inside->getParent(), (LLView*)pane);
        ensure_equals("the window still finds it by name",
                      w.floater->findChild<LLButton>("in_the_side", true), w.inside);
        w.floater->die();
    }

    // Out: the pane is in a window of its own, and the room it left goes to
    // what is still in the stack.
    template<> template<>
    void aldockpanel_object::test<2>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        const S32 was = w.middle->getRect().getWidth();

        pane->popOut();
        ensure("it says it is out", pane->poppedOut());
        ensure("it is not in the window any more", pane->getParent() != (LLView*)w.side);
        ensure("it is in a window of its own",
               pane->getParentByType<ALPanelFloater>() != nullptr);
        ensure("the pane it came out of is folded away", w.side->isCollapsed());

        w.stack->updateLayout();
        ensure("and the rest of the stack took the room " + std::to_string(w.middle->getRect().getWidth()),
               w.middle->getRect().getWidth() > was);
        w.floater->die();
    }

    // Back: where it was is remembered here and nowhere else, so it goes
    // back to the same parent at the same size, and the stack gives it the
    // room it had.
    template<> template<>
    void aldockpanel_object::test<3>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        const S32 dim = w.side->getTargetDim();

        pane->popOut();
        // The frame the pane is out for: a collapsed panel is laid out at no
        // size, and a panel that does not resize itself takes the size it is
        // laid out at as the size it wants. That is where the number goes.
        w.stack->updateLayout();
        pane->dock();
        w.stack->updateLayout();

        ensure("it says it is back", !pane->poppedOut());
        ensure_equals("in the parent it came from", pane->getParent(), (LLView*)w.side);
        ensure("and the room came back with it", !w.side->isCollapsed());
        ensure_equals("at the size it had", w.side->getTargetDim(), dim);
        ensure_equals("the window finds what is in it again",
                      w.floater->findChild<LLButton>("in_the_side", true), w.inside);
        w.floater->die();
    }

    // Closing the window a pane is borrowing puts the pane back rather than
    // taking it down: the window is the borrower, and the pane belongs to
    // the tool.
    template<> template<>
    void aldockpanel_object::test<4>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        pane->popOut();

        ALPanelFloater* window = pane->getParentByType<ALPanelFloater>();
        ensure("there is a window", window != nullptr);
        window->closeFloater();

        ensure("the pane came back", !pane->poppedOut());
        ensure_equals("to where it was", pane->getParent(), (LLView*)w.side);
        ensure("and it is still alive", !pane->isDead());
        w.floater->die();
    }

    // Popping twice is popping once, and docking one that is already home
    // does nothing at all.
    template<> template<>
    void aldockpanel_object::test<5>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");

        pane->dock();
        ensure_equals("still where it was declared", pane->getParent(), (LLView*)w.side);

        pane->popOut();
        LLView* window = pane->getParent();
        pane->popOut();
        ensure_equals("one window, not two", pane->getParent(), window);
        w.floater->die();
    }

    // A pane out of its window is a different focused floater, so the keys
    // pressed over it go home to the window it came from -- otherwise every
    // accelerator quietly stops working the moment a pane is popped.
    template<> template<>
    void aldockpanel_object::test<6>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        pane->popOut();

        ALPanelFloater* window = pane->getParentByType<ALPanelFloater>();
        ensure("there is a window", window != nullptr);
        ensure_equals("and it knows where home is", window->home(), w.floater);
        ensure("it asks before the viewer's own menu does", window->hasAccelerators());
        w.floater->die();
    }

    // A pane goes under the window's title bar, not over it. A pane given
    // the whole of a floater covers the title, the buttons and the handle it
    // is dragged by -- and whatever the pane draws along its own top shares
    // a band with them.
    template<> template<>
    void aldockpanel_object::test<8>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        const S32 was = pane->getRect().getHeight();
        pane->popOut();

        LLFloater* window = pane->getParentByType<ALPanelFloater>();
        ensure("there is a window", window != nullptr);
        ensure("the title bar has a band of its own", window->getHeaderHeight() > 0);
        ensure_equals("and the pane starts below it",
                      pane->getRect().mTop, window->getRect().getHeight() - window->getHeaderHeight());
        ensure_equals("the pane still starts at the left", pane->getRect().mLeft, 0);
        ensure_equals("and reaches the right", pane->getRect().mRight, window->getRect().getWidth());

        // The window is the pane and the bar, so the pane keeps the height
        // it had rather than losing the bar's band out of it.
        ensure_equals("the pane kept its height", pane->getRect().getHeight(), was);
        w.floater->die();
    }

    // Where it is while it is out survives going home and coming out again,
    // so a developer who put it on the other monitor finds it there.
    template<> template<>
    void aldockpanel_object::test<9>()
    {
        Window w = build();
        ALDockPanel* pane = ALDockPanel::wrap(w.side, "Side");
        pane->popOut();

        LLFloater* window = pane->getParentByType<ALPanelFloater>();
        const LLRect moved(300, 700, 800, 300);
        window->setShape(moved);
        pane->dock();

        ensure_equals("where it was is remembered", pane->floatingRect().getWidth(), moved.getWidth());
        pane->popOut();
        ensure_equals("and it comes back out there",
                      pane->getParentByType<ALPanelFloater>()->getRect().getWidth(), moved.getWidth());
        w.floater->die();
    }
}
