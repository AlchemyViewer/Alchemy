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
#include "../lluictrlfactory.h"

#include "llcriticaldamp.h"
#include "llframetimer.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gLayoutStackTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gLayoutStackTestAnonName;
}

namespace tut
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

    typedef test_group<lllayoutstack_data> lllayoutstack_test;
    typedef lllayoutstack_test::object     lllayoutstack_object;
    tut::lllayoutstack_test lllayoutstack_testgroup("lllayoutstack");

    // A panel that leaves its stack by being deleted is gone from the stack's
    // list, the same as one that was removed first. The stack lays out from
    // that list every frame, so an entry that outlives its panel is a read of
    // freed memory on the next draw.
    template<> template<>
    void lllayoutstack_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* a = panel("a");
        LLLayoutPanel* b = panel("b");
        s->addPanel(a);
        s->addPanel(b);
        ensure_equals("two panels", s->getNumPanels(), 2);

        delete a;
        ensure_equals("a deleted panel has left the list", s->getNumPanels(), 1);

        s->removeChild(b);
        delete b;
        ensure_equals("a removed panel has left the list", s->getNumPanels(), 0);
    }

    // A panel moved from one stack to another arrives with a resize bar, and
    // the bar belongs to the stack now holding it. Joining a stack is what
    // builds the bar and leaving one is what takes it away, so the two have to
    // happen in that order.
    template<> template<>
    void lllayoutstack_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> a(stack());
        std::unique_ptr<LLLayoutStack> b(stack());
        LLLayoutPanel* p = panel("p");

        a->addPanel(p);
        ensure("a panel added to a stack has a resize bar", p->getResizeBar() != nullptr);
        ensure("of that stack's", p->getResizeBar()->getParent() == a.get());

        b->addChild(p);
        ensure_equals("the panel left the first stack", a->getNumPanels(), 0);
        ensure_equals("and joined the second", b->getNumPanels(), 1);
        ensure("the moved panel has a resize bar", p->getResizeBar() != nullptr);
        ensure("of the stack that holds it now", p->getResizeBar()->getParent() == b.get());

        // Reads the bar of every panel on the list.
        b->updateLayout();
    }

    // The same question asked by adding a panel a stack already holds: the
    // removal that arrives from inside must not take the new entry back off.
    template<> template<>
    void lllayoutstack_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");

        s->addPanel(p);
        s->addChild(p);
        ensure_equals("the panel is on the list once", s->getNumPanels(), 1);
        ensure("and still has a resize bar", p->getResizeBar() != nullptr);

        s->updateLayout();
    }

    // A stack owns the resize bars, not the panels. One whose panel has gone is
    // deleted rather than left with no parent and nobody to free it -- and
    // mouse capture is a thing a view gives up on its way out, so the focus
    // manager can say which of the two happened.
    template<> template<>
    void lllayoutstack_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");
        s->addPanel(p);

        LLResizeBar* barp = p->getResizeBar();
        ensure("the panel has a resize bar", barp != nullptr);

        gFocusMgr.setMouseCapture(barp);
        ensure("the bar holds the mouse", gFocusMgr.getMouseCapture() == barp);

        s->removeChild(p);
        ensure("the bar went with the panel it belonged to",
               gFocusMgr.getMouseCapture() == nullptr);
        ensure("and the panel no longer names it", p->getResizeBar() == nullptr);

        delete p;
    }

    // The gap between panels is not owed after the last one that is showing,
    // and once a trailing panel is hidden that is no longer the last one on the
    // list. A gap charged for and never given back is space the stack loses.
    template<> template<>
    void lllayoutstack_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack(10));
        LLLayoutPanel* shown = panel("shown");
        LLLayoutPanel* hidden = hiddenPanel("hidden");
        s->addPanel(shown);
        s->addPanel(hidden);

        s->updateLayout();
        ensure_equals("the only panel showing fills the stack",
                      shown->getRect().getWidth(), 300);

        hidden->setVisible(true);
        s->updateLayout();
        ensure_equals("with both showing, one gap sits between them",
                      shown->getRect().getWidth() + hidden->getRect().getWidth(), 290);
    }

    // Ignoring reshapes is the caller's answer about its own work, not the
    // layout pass's about its own: a panel told to ignore them is still
    // ignoring them on the far side of a layout.
    template<> template<>
    void lllayoutstack_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("a reshape the panel was told to ignore leaves its target alone",
                      p->getTargetDim(), target);
    }

    // A frame's worth of animation, once a frame. Layout runs from updateClass
    // at the top of the frame and again from draw, while the interpolant is
    // computed once a frame -- so a second pass that moved a panel again would
    // move it twice as far as the frame is worth.
    template<> template<>
    void lllayoutstack_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const F32 TIME_CONSTANT = 1.f;
        if (partialInterpolant(TIME_CONSTANT) <= 0.f)
        {
            skip("the frame clock did not move");
        }

        std::unique_ptr<LLLayoutStack> s(stack(0, true, TIME_CONSTANT));
        LLLayoutPanel* p = panel("p");
        s->addPanel(p, LLLayoutStack::ANIMATE);

        LLFrameTimer::updateFrameCount();
        LLLayoutStack::updateClass();       // the frame's animation pass
        F32 after_frame = p->getVisibleAmount();
        ensure("the frame opened the panel part way",
               after_frame > 0.f && after_frame < 1.f);

        s->updateLayout();                  // what draw does, same frame
        ensure_equals("a second pass in the same frame moves it no further",
                      p->getVisibleAmount(), after_frame);

        LLFrameTimer::updateFrameCount();
        s->updateLayout();
        ensure("the next frame moves it again", p->getVisibleAmount() > after_frame);
    }

    // Every panel of a stack gets the frame's animation, not just the first one
    // that wanted it: panels that open together open together.
    template<> template<>
    void lllayoutstack_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the first panel opened", a->getVisibleAmount(), 1.f);
        ensure_equals("so did the second", b->getVisibleAmount(), 1.f);
    }

    // A panel that never declared a minimum has one of zero, collapsed or not.
    // -1 is how the parameter says it was not given, and a dimension of -1
    // travels out of the space arithmetic and into a clip rect.
    template<> template<>
    void lllayoutstack_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = panel("p");
        s->addPanel(p);

        ensure_equals("expanded, its minimum is zero", p->getRelevantMinDim(), 0);

        s->collapsePanel(p, true);
        s->updateLayout();
        ensure_equals("collapsed, its minimum is still zero", p->getRelevantMinDim(), 0);
        ensure_equals("and it takes no room", p->getVisibleDim(), 0);
    }

    // Resize bars sit in front of the panels. A bar overlaps the panel on
    // either side of it by resize_bar_overlap, and a click in that overlap
    // belongs to the bar; whichever panel joined last is still behind it.
    template<> template<>
    void lllayoutstack_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
                ensure("every resize bar is in front of every panel", !seen_panel);
            }
        }
        ensure("the stack has panels to be in front of", seen_panel);
    }

    // setTargetDim asks the stack for a size the panel will hold. Below the
    // panel's minimum, the minimum is what it asks for.
    template<> template<>
    void lllayoutstack_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* p = fixedPanel("fixed", 100);   // its minimum comes from that rect
        s->addPanel(p);
        s->updateLayout();
        ensure_equals("the panel starts at its authored size", p->getTargetDim(), 100);

        p->setTargetDim(10);
        ensure_equals("a target under the minimum is the minimum", p->getTargetDim(), 100);
    }
}
