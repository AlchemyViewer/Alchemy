/**
 * @file llfocusmgr_test.cpp
 * @brief Tests for the focus manager: what holds focus and the mouse, and who can tell
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

#include "../llfocusmgr.h"
#include "../llview.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gFocusMgrTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFocusMgrTestAnonName;
}

namespace tut
{
    struct TestView : public LLView
    {
        TestView(const LLView::Params& p) : LLView(p) {}
    };

    // A focusable that is not a view. The viewer has one: media focus.
    struct TestFocusable : public LLFocusableElement
    {
    };

    struct llfocusmgr_data
    {
        static TestView* view(const std::string& name)
        {
            LLView::Params p;
            p.name = name;
            p.rect = LLRect(0, 10, 10, 0);
            return new TestView(p);
        }

        // Whatever a test left holding focus or the mouse is let go before
        // the next one, and before the views it pointed at are freed.
        ~llfocusmgr_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            gFocusMgr.setMouseCapture(nullptr);
        }
    };

    typedef test_group<llfocusmgr_data> llfocusmgr_test;
    typedef llfocusmgr_test::object     llfocusmgr_object;
    tut::llfocusmgr_test llfocusmgr_testgroup("llfocusmgr");

    // Keyboard focus on a view is found from every ancestor of it and from
    // no one else, and the view is answered as a view without a walk back
    // from the element.
    template<> template<>
    void llfocusmgr_object::test<1>()
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestView* child = view("child");
        parent->addChild(child);
        std::unique_ptr<TestView> other(view("other"));

        gFocusMgr.setKeyboardFocus(child);
        ensure("the element is the child", gFocusMgr.getKeyboardFocus() == child);
        ensure("the view is the child", gFocusMgr.getKeyboardFocusView() == child);
        ensure("a plain view is not a control", gFocusMgr.getKeyboardFocusCtrl() == nullptr);
        ensure("the parent has it", gFocusMgr.childHasKeyboardFocus(parent.get()));
        ensure("the child has it", gFocusMgr.childHasKeyboardFocus(child));
        ensure("an unrelated view does not", !gFocusMgr.childHasKeyboardFocus(other.get()));

        gFocusMgr.removeKeyboardFocusWithoutCallback(child);
        ensure("released: no element", gFocusMgr.getKeyboardFocus() == nullptr);
        ensure("released: no view", gFocusMgr.getKeyboardFocusView() == nullptr);
        ensure("released: the parent no longer has it", !gFocusMgr.childHasKeyboardFocus(parent.get()));
    }

    // A focusable that is not a view holds focus, has no view, and no view
    // claims it.
    template<> template<>
    void llfocusmgr_object::test<2>()
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestFocusable element;

        gFocusMgr.setKeyboardFocus(&element);
        ensure("the element has focus", gFocusMgr.getKeyboardFocus() == &element);
        ensure("but there is no view", gFocusMgr.getKeyboardFocusView() == nullptr);
        ensure("and no control", gFocusMgr.getKeyboardFocusCtrl() == nullptr);
        ensure("no view has it", !gFocusMgr.childHasKeyboardFocus(parent.get()));
        ensure("no accelerators to find", !gFocusMgr.keyboardFocusHasAccelerators());

        gFocusMgr.setKeyboardFocus(nullptr);
        ensure("released", gFocusMgr.getKeyboardFocus() == nullptr && gFocusMgr.getKeyboardFocusView() == nullptr);
    }

    // The view follows the element when focus moves.
    template<> template<>
    void llfocusmgr_object::test<3>()
    {
        std::unique_ptr<TestView> a(view("a"));
        std::unique_ptr<TestView> b(view("b"));

        gFocusMgr.setKeyboardFocus(a.get());
        gFocusMgr.setKeyboardFocus(b.get());
        ensure("the element moved", gFocusMgr.getKeyboardFocus() == b.get());
        ensure("the view moved with it", gFocusMgr.getKeyboardFocusView() == b.get());
        ensure("the last focus is remembered", gFocusMgr.getLastKeyboardFocus() == a.get());
        ensure("a no longer has it", !gFocusMgr.childHasKeyboardFocus(a.get()));
        ensure("b has it", gFocusMgr.childHasKeyboardFocus(b.get()));
    }

    // Mouse capture has the same shape, and a captor that dies lets go.
    template<> template<>
    void llfocusmgr_object::test<4>()
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestView* child = view("child");
        parent->addChild(child);
        std::unique_ptr<TestView> other(view("other"));

        gFocusMgr.setMouseCapture(child);
        ensure("the handler is the child", gFocusMgr.getMouseCapture() == child);
        ensure("the view is the child", gFocusMgr.getMouseCaptureView() == child);
        ensure("the parent has it", gFocusMgr.childHasMouseCapture(parent.get()));
        ensure("an unrelated view does not", !gFocusMgr.childHasMouseCapture(other.get()));

        gFocusMgr.setMouseCapture(nullptr);
        ensure("released: no handler", gFocusMgr.getMouseCapture() == nullptr);
        ensure("released: no view", gFocusMgr.getMouseCaptureView() == nullptr);

        gFocusMgr.setMouseCapture(child);
        gFocusMgr.removeMouseCaptureWithoutCallback(child);
        ensure("removed: no view", gFocusMgr.getMouseCaptureView() == nullptr);

        gFocusMgr.setMouseCapture(child);
        parent.reset();     // takes the child with it
        ensure("a dead captor let go", gFocusMgr.getMouseCapture() == nullptr);
        ensure("and took its view with it", gFocusMgr.getMouseCaptureView() == nullptr);
    }
}
