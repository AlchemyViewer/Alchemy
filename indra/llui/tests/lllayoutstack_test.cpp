/**
 * @file lllayoutstack_test.cpp
 * @brief Tests for the layout stack: the panels it holds and how they leave
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#include "../lllayoutstack.h"

#include "../llresizebar.h"
#include "../lluictrlfactory.h"

#include "llcriticaldamp.h"
#include "llframetimer.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct lllayoutstack_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A horizontal stack 300 across. The gap between panels and the
        // animation constants are what the tests vary: a time constant of zero
        // makes one pass the whole of an animation, which is what lets a test
        // say where a panel ended up without asking the clock.
        static LLLayoutStack* stack(S32 spacing = 0, bool animate = false, F32 time_constant = 0.f)
        {
            LLLayoutStack::Params p;
            p.name = "stack";
            p.rect = LLRect(0, 100, 300, 0);
            p.orientation = LLLayoutStack::HORIZONTAL;
            p.border_size = spacing;
            p.animate = animate;
            p.open_time_constant = time_constant;
            p.close_time_constant = time_constant;
            return LLUICtrlFactory::create<LLLayoutStack>(p);
        }

        // An ordinary panel: the stack sizes it, and it declares no minimum.
        static LLLayoutPanel* panel(const std::string& name)
        {
            LLLayoutPanel::Params p;
            p.name = name;
            p.rect = LLRect(0, 100, 100, 0);
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        }

        static LLLayoutPanel* hiddenPanel(const std::string& name)
        {
            LLLayoutPanel::Params p;
            p.name = name;
            p.rect = LLRect(0, 100, 100, 0);
            p.visible = false;
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        }

        // A panel the stack does not size, which is the one kind whose target
        // dimension a reshape writes.
        static LLLayoutPanel* fixedPanel(const std::string& name, S32 width)
        {
            LLLayoutPanel::Params p;
            p.name = name;
            p.rect = LLRect(0, 100, width, 0);
            p.auto_resize = false;
            p.user_resize = true;
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        }

        // A frame's worth of interpolant, taken off the clock the way a frame
        // takes it, and small enough to leave room for a second helping. The
        // clock a frame timer reads only moves when a frame moves it, so a test
        // wanting time to pass has to say so. Zero when it did not pass, which
        // no test can read anything into.
        static F32 partialInterpolant(F32 time_constant)
        {
            for (S32 tries = 0; tries < 1000; ++tries)
            {
                LLFrameTimer::updateFrameTime();
                LLSmoothInterpolation::updateInterpolants();
                F32 interpolant = LLSmoothInterpolation::getInterpolant(time_constant);
                if (interpolant > 0.f && interpolant < 0.5f)
                {
                    return interpolant;
                }
            }
            return 0.f;
        }
    };

    // A panel that leaves its stack by being deleted is gone from the stack's
    // list, the same as one that was removed first. The stack lays out from
    // that list every frame, so an entry that outlives its panel is a read of
    // freed memory on the next draw.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a deleted panel leaves the stack's list as a removed one does", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* a = panel("a");
        LLLayoutPanel* b = panel("b");
        s->addPanel(a);
        s->addPanel(b);
        CHECK(s->getNumPanels() == 2);

        delete a;
        CHECK(s->getNumPanels() == 1);

        s->removeChild(b);
        delete b;
        CHECK(s->getNumPanels() == 0);
    }

    // A panel moved from one stack to another arrives with a resize bar, and
    // the bar belongs to the stack now holding it. Joining a stack is what
    // builds the bar and leaving one is what takes it away, so the two have to
    // happen in that order.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a panel moved to another stack has a resize bar of that stack", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> a(stack());
        std::unique_ptr<LLLayoutStack> b(stack());
        LLLayoutPanel* p = panel("p");

        a->addPanel(p);
        REQUIRE(p->getResizeBar() != nullptr);
        CHECK(p->getResizeBar()->getParent() == a.get());

        b->addChild(p);
        CHECK(a->getNumPanels() == 0);
        CHECK(b->getNumPanels() == 1);
        REQUIRE(p->getResizeBar() != nullptr);
        CHECK(p->getResizeBar()->getParent() == b.get());

        // Reads the bar of every panel on the list.
        b->updateLayout();
    }

    // The same question asked by adding a panel a stack already holds: the
    // removal that arrives from inside must not take the new entry back off.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: adding a panel the stack already holds keeps it on the list once", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");

        s->addPanel(p);
        s->addChild(p);
        CHECK(s->getNumPanels() == 1);
        REQUIRE(p->getResizeBar() != nullptr);

        s->updateLayout();
    }

    // A stack owns the resize bars, not the panels. One whose panel has gone is
    // deleted rather than left with no parent and nobody to free it -- and
    // mouse capture is a thing a view gives up on its way out, so the focus
    // manager can say which of the two happened.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a resize bar is deleted when its panel leaves the stack", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");
        s->addPanel(p);

        LLResizeBar* barp = p->getResizeBar();
        REQUIRE(barp != nullptr);

        gFocusMgr.setMouseCapture(barp);
        CHECK(gFocusMgr.getMouseCapture() == barp);

        s->removeChild(p);
        UNSCOPED_INFO("the bar was deleted, so it let go of the mouse");
        CHECK(gFocusMgr.getMouseCapture() == nullptr);
        CHECK(p->getResizeBar() == nullptr);

        delete p;
    }

    // The gap between panels is not owed after the last one that is showing,
    // and once a trailing panel is hidden that is no longer the last one on the
    // list. A gap charged for and never given back is space the stack loses.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: no gap is owed after the last panel showing", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack(10));
        LLLayoutPanel* shown = panel("shown");
        LLLayoutPanel* hidden = hiddenPanel("hidden");
        s->addPanel(shown);
        s->addPanel(hidden);

        s->updateLayout();
        UNSCOPED_INFO("the only panel showing fills the stack");
        CHECK(shown->getRect().getWidth() == 300);

        hidden->setVisible(true);
        s->updateLayout();
        UNSCOPED_INFO("with both showing, one gap sits between them");
        CHECK(shown->getRect().getWidth() + hidden->getRect().getWidth() == 290);
    }

    // Ignoring reshapes is the caller's answer about its own work, not the
    // layout pass's about its own: a panel told to ignore them is still
    // ignoring them on the far side of a layout.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a panel told to ignore reshapes still ignores them after a layout", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = fixedPanel("fixed", 100);
        s->addPanel(p);
        s->updateLayout();

        p->setIgnoreReshape(true);
        s->reshape(400, 100);   // asks for another pass
        s->updateLayout();

        S32 target = p->getTargetDim();
        p->reshape(target + 50, p->getRect().getHeight());
        CHECK(p->getTargetDim() == target);
    }

    // A frame's worth of animation, once a frame. Layout runs from updateClass
    // at the top of the frame and again from draw, while the interpolant is
    // computed once a frame -- so a second pass that moved a panel again would
    // move it twice as far as the frame is worth.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a second layout in the same frame moves an animating panel no further", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const F32 TIME_CONSTANT = 1.f;
        if (partialInterpolant(TIME_CONSTANT) <= 0.f)
        {
            SKIP("the frame clock did not move");
        }

        std::unique_ptr<LLLayoutStack> s(stack(0, true, TIME_CONSTANT));
        LLLayoutPanel* p = panel("p");
        s->addPanel(p, LLLayoutStack::ANIMATE);

        LLFrameTimer::updateFrameCount();
        LLLayoutStack::updateClass();       // the frame's animation pass
        F32 after_frame = p->getVisibleAmount();
        CHECK(after_frame > 0.f);
        CHECK(after_frame < 1.f);

        s->updateLayout();                  // what draw does, same frame
        UNSCOPED_INFO("a second pass in the same frame moves it no further");
        CHECK(p->getVisibleAmount() == after_frame);

        LLFrameTimer::updateFrameCount();
        s->updateLayout();
        UNSCOPED_INFO("the next frame moves it again");
        CHECK(p->getVisibleAmount() > after_frame);
    }

    // Every panel of a stack gets the frame's animation, not just the first one
    // that wanted it: panels that open together open together.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: every animating panel gets the frame's animation not just the first", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // A time constant of zero is a whole animation in one pass.
        std::unique_ptr<LLLayoutStack> s(stack(0, true, 0.f));
        LLLayoutPanel* a = panel("a");
        LLLayoutPanel* b = panel("b");
        s->addPanel(a, LLLayoutStack::ANIMATE);
        s->addPanel(b, LLLayoutStack::ANIMATE);

        // Building the stack laid it out once, and that spent this frame.
        // Nothing moves the frame on in a test but the test.
        LLFrameTimer::updateFrameCount();

        s->updateLayout();
        CHECK(a->getVisibleAmount() == 1.f);
        CHECK(b->getVisibleAmount() == 1.f);
    }

    // A panel that never declared a minimum has one of zero, collapsed or not.
    // -1 is how the parameter says it was not given, and a dimension of -1
    // travels out of the space arithmetic and into a clip rect.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a panel that declares no minimum has a minimum of zero even collapsed", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");
        s->addPanel(p);

        UNSCOPED_INFO("expanded");
        CHECK(p->getRelevantMinDim() == 0);

        s->collapsePanel(p, true);
        s->updateLayout();
        UNSCOPED_INFO("collapsed");
        CHECK(p->getRelevantMinDim() == 0);
        CHECK(p->getVisibleDim() == 0);
    }

    // Resize bars sit in front of the panels. A bar overlaps the panel on
    // either side of it by resize_bar_overlap, and a click in that overlap
    // belongs to the bar; whichever panel joined last is still behind it.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: resize bars sit in front of the panels", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        s->addPanel(panel("a"));
        s->addPanel(panel("b"));
        s->addPanel(panel("c"));

        // Front to back, which is the order a click is offered them in.
        bool seen_panel = false;
        for (LLView* child : *s->getChildList())
        {
            if (child->as<LLLayoutPanel>())
            {
                seen_panel = true;
            }
            else
            {
                UNSCOPED_INFO("every resize bar is in front of every panel");
                REQUIRE_FALSE(seen_panel);
            }
        }
        UNSCOPED_INFO("the stack has panels to be in front of");
        CHECK(seen_panel);
    }

    // setTargetDim asks the stack for a size the panel will hold. Below the
    // panel's minimum, the minimum is what it asks for.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: setTargetDim below the panel's minimum asks for the minimum", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = fixedPanel("fixed", 100);   // its minimum comes from that rect
        s->addPanel(p);
        s->updateLayout();
        UNSCOPED_INFO("the panel starts at its authored size");
        CHECK(p->getTargetDim() == 100);

        p->setTargetDim(10);
        CHECK(p->getTargetDim() == 100);
    }

    // A resize bar sits between two panels and needs both of them to be able
    // to give room. Putting a panel that is neither auto- nor user-resizable
    // between the two that were resizing against each other takes the bar
    // away, and it goes silently: the drag area is simply not there any more.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: a panel that cannot resize takes the resize bar from between two", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // A panel the stack does not size and nobody may drag: a toolbar.
        const auto band = [](const std::string& name, S32 width)
        {
            LLLayoutPanel::Params p;
            p.name = name;
            p.rect = LLRect(0, 100, width, 0);
            p.auto_resize = false;
            p.user_resize = false;
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        };

        {
            LLLayoutStack* s = stack();
            LLLayoutPanel* grows = panel("grows");
            LLLayoutPanel* dragged = fixedPanel("dragged", 80);
            s->addPanel(grows);
            s->addPanel(dragged);
            s->updateLayout();
            REQUIRE(grows->getResizeBar() != nullptr);
            UNSCOPED_INFO("the pair can resize, so there is somewhere to drag");
            CHECK(grows->getResizeBar()->getVisible());
            s->die();
        }

        {
            LLLayoutStack* s = stack();
            LLLayoutPanel* grows = panel("grows");
            LLLayoutPanel* between = band("between", 26);
            LLLayoutPanel* dragged = fixedPanel("dragged", 80);
            s->addPanel(grows);
            s->addPanel(between);
            s->addPanel(dragged);
            s->updateLayout();
            REQUIRE(between->getResizeBar() != nullptr);
            REQUIRE(grows->getResizeBar() != nullptr);
            UNSCOPED_INFO("a band that cannot give room has no bar of its own");
            CHECK_FALSE(between->getResizeBar()->getVisible());
            UNSCOPED_INFO("and it takes the bar off the panel behind it too");
            CHECK_FALSE(grows->getResizeBar()->getVisible());
            s->die();
        }
    }

    // A divider is furniture: worth finding when a hand is going for it, and
    // worth nothing the rest of the time. A stack that says so draws its
    // handles only near the mouse -- the bar itself is where it always was
    // and takes the mouse the same way; what appears is the mark saying so.
    TEST_CASE_METHOD(lllayoutstack_data, "lllayoutstack: drag handles shown on hover are drawn only near the mouse", "[lllayoutstack]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLLayoutStack::Params p;
        p.name = "stack";
        p.rect = LLRect(0, 100, 300, 0);
        p.orientation = LLLayoutStack::HORIZONTAL;
        p.border_size = 12;
        p.animate = false;
        p.show_drag_handle = true;
        p.drag_handle_on_hover = true;
        p.drag_handle_reach = 10;
        LLLayoutStack* stack = LLUICtrlFactory::create<LLLayoutStack>(p);

        stack->addChild(panel("left"));
        stack->addChild(panel("right"));
        stack->updateLayout();

        LLView* handle = stack->findChild<LLView>("resize_handle_bg_panel", true);
        REQUIRE(handle != nullptr);
        UNSCOPED_INFO("not drawn until a hand goes for it");
        CHECK_FALSE(handle->getVisible());

        // Where the bar is, which is where a hand going for it would be.
        const LLResizeBar* bar = stack->findChild<LLResizeBar>("resize", true);
        REQUIRE(bar != nullptr);
        const LLRect on_it = bar->getRect();

        stack->showDragHandlesNear(on_it.getCenterX(), on_it.getCenterY());
        UNSCOPED_INFO("a hand on it brings it out");
        CHECK(handle->getVisible());

        // Near it counts too, because a bar is a few pixels wide and a hand
        // is not that accurate.
        stack->showDragHandlesNear(on_it.mRight + 6, on_it.getCenterY());
        UNSCOPED_INFO("and so does a hand beside it");
        CHECK(handle->getVisible());

        stack->showDragHandlesNear(on_it.mRight + 60, on_it.getCenterY());
        UNSCOPED_INFO("a hand somewhere else does not");
        CHECK_FALSE(handle->getVisible());

        // A stack that draws its handles always is not affected by where the
        // mouse is: saying so is what turns this on.
        LLLayoutStack::Params always(p);
        always.drag_handle_on_hover = false;
        LLLayoutStack* other = LLUICtrlFactory::create<LLLayoutStack>(always);
        other->addChild(panel("a"));
        other->addChild(panel("b"));
        other->updateLayout();
        LLView* shown = other->findChild<LLView>("resize_handle_bg_panel", true);
        {
            INFO("drawn from the start");
            REQUIRE(shown != nullptr);
            CHECK(shown->getVisible());
        }
        other->showDragHandlesNear(-100, -100);
        UNSCOPED_INFO("and still drawn wherever the mouse is");
        CHECK(shown->getVisible());

        delete stack;
        delete other;
    }
}
