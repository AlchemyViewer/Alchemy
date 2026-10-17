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

#include <catch2/catch_test_macros.hpp>

namespace
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

    // Keyboard focus on a view is found from every ancestor of it and from
    // no one else, and the view is answered as a view without a walk back
    // from the element.
    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: keyboard focus on a view is found from its ancestors and no one else", "[llfocusmgr]")
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestView* child = view("child");
        parent->addChild(child);
        std::unique_ptr<TestView> other(view("other"));

        gFocusMgr.setKeyboardFocus(child);
        CHECK(gFocusMgr.getKeyboardFocus() == child);
        CHECK(gFocusMgr.getKeyboardFocusView() == child);
        UNSCOPED_INFO("a plain view is not a control");
        CHECK(gFocusMgr.getKeyboardFocusCtrl() == nullptr);
        CHECK(gFocusMgr.childHasKeyboardFocus(parent.get()));
        CHECK(gFocusMgr.childHasKeyboardFocus(child));
        CHECK_FALSE(gFocusMgr.childHasKeyboardFocus(other.get()));

        gFocusMgr.removeKeyboardFocusWithoutCallback(child);
        CHECK(gFocusMgr.getKeyboardFocus() == nullptr);
        CHECK(gFocusMgr.getKeyboardFocusView() == nullptr);
        CHECK_FALSE(gFocusMgr.childHasKeyboardFocus(parent.get()));
    }

    // A focusable that is not a view holds focus, has no view, and no view
    // claims it.
    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: a focusable that is not a view holds focus and no view claims it", "[llfocusmgr]")
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestFocusable element;

        gFocusMgr.setKeyboardFocus(&element);
        CHECK(gFocusMgr.getKeyboardFocus() == &element);
        CHECK(gFocusMgr.getKeyboardFocusView() == nullptr);
        CHECK(gFocusMgr.getKeyboardFocusCtrl() == nullptr);
        CHECK_FALSE(gFocusMgr.childHasKeyboardFocus(parent.get()));
        CHECK_FALSE(gFocusMgr.keyboardFocusHasAccelerators());

        gFocusMgr.setKeyboardFocus(nullptr);
        CHECK(gFocusMgr.getKeyboardFocus() == nullptr);
        CHECK(gFocusMgr.getKeyboardFocusView() == nullptr);
    }

    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: the focus view follows the element when focus moves", "[llfocusmgr]")
    {
        std::unique_ptr<TestView> a(view("a"));
        std::unique_ptr<TestView> b(view("b"));

        gFocusMgr.setKeyboardFocus(a.get());
        gFocusMgr.setKeyboardFocus(b.get());
        CHECK(gFocusMgr.getKeyboardFocus() == b.get());
        CHECK(gFocusMgr.getKeyboardFocusView() == b.get());
        CHECK(gFocusMgr.getLastKeyboardFocus() == a.get());
        CHECK_FALSE(gFocusMgr.childHasKeyboardFocus(a.get()));
        CHECK(gFocusMgr.childHasKeyboardFocus(b.get()));
    }

    // Mouse capture has the same shape, and a captor that dies lets go.
    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: mouse capture is found from ancestors and a dying captor lets go", "[llfocusmgr]")
    {
        std::unique_ptr<TestView> parent(view("parent"));
        TestView* child = view("child");
        parent->addChild(child);
        std::unique_ptr<TestView> other(view("other"));

        gFocusMgr.setMouseCapture(child);
        CHECK(gFocusMgr.getMouseCapture() == child);
        CHECK(gFocusMgr.getMouseCaptureView() == child);
        CHECK(gFocusMgr.childHasMouseCapture(parent.get()));
        CHECK_FALSE(gFocusMgr.childHasMouseCapture(other.get()));

        gFocusMgr.setMouseCapture(nullptr);
        CHECK(gFocusMgr.getMouseCapture() == nullptr);
        CHECK(gFocusMgr.getMouseCaptureView() == nullptr);

        gFocusMgr.setMouseCapture(child);
        gFocusMgr.removeMouseCaptureWithoutCallback(child);
        CHECK(gFocusMgr.getMouseCaptureView() == nullptr);

        gFocusMgr.setMouseCapture(child);
        parent.reset();     // takes the child with it
        CHECK(gFocusMgr.getMouseCapture() == nullptr);
        CHECK(gFocusMgr.getMouseCaptureView() == nullptr);
    }

    // A view that counts what it is told of its focus.
    struct CountingView : public TestView
    {
        explicit CountingView(const LLView::Params& p) : TestView(p) {}
        void onFocusLost() override { ++lost; TestView::onFocusLost(); }
        S32 lost = 0;

        static CountingView* make(const std::string& name)
        {
            LLView::Params p;
            p.name = name;
            p.rect = LLRect(0, 10, 10, 0);
            return new CountingView(p);
        }
    };

    // Focus taken without callbacks leaves nothing as having it: a view whose
    // focus was taken so is not told later that it lost it, nor are the views
    // above it, when focus goes somewhere else.
    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: focus taken without callbacks tells no one later that they lost it", "[llfocusmgr]")
    {
        std::unique_ptr<CountingView> parent(CountingView::make("parent"));
        CountingView* child = CountingView::make("child");
        parent->addChild(child);
        std::unique_ptr<TestView> other(view("other"));

        gFocusMgr.setKeyboardFocus(child);
        gFocusMgr.removeKeyboardFocusWithoutCallback(child);
        gFocusMgr.setKeyboardFocus(other.get());
        CHECK(child->lost == 0);
        CHECK(parent->lost == 0);
    }

    // A view that died with focus leaves no trace in what the next change
    // compares: focus moving between two children of one parent leaves the
    // parent holding it, and the parent is not told it lost it.
    TEST_CASE_METHOD(llfocusmgr_data, "llfocusmgr: after a view dies holding focus a parent keeps focus its children pass", "[llfocusmgr]")
    {
        {
            std::unique_ptr<TestView> gone(view("gone"));
            gFocusMgr.setKeyboardFocus(gone.get());
        }

        std::unique_ptr<CountingView> parent(CountingView::make("parent"));
        TestView* first = view("first");
        TestView* second = view("second");
        parent->addChild(first);
        parent->addChild(second);

        gFocusMgr.setKeyboardFocus(first);
        gFocusMgr.setKeyboardFocus(second);
        CHECK(parent->lost == 0);
        CHECK(gFocusMgr.childHasKeyboardFocus(parent.get()));
    }
}
