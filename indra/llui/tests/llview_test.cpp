/**
 * @file llview_test.cpp
 * @brief Tests for the view tree: children, tab order, reshape and visibility
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

#include "../llview.h"
#include "../llfocusmgr.h"

#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
        // LLView's constructor is protected, for LLUICtrlFactory. A test builds
        // views the same way a widget does.
        struct TestView : public LLView
        {
            TestView(const LLView::Params& p) : LLView(p) {}
        };

    struct llview_data
    {
        // A view of the given name and rect. Views are parented into a tree by
        // the tests and deleted with their root.
        static TestView* view(const std::string& name,
                              const LLRect& rect = LLRect(0, 10, 10, 0),
                              U32 follows = FOLLOWS_LEFT | FOLLOWS_TOP)
        {
            LLView::Params p;
            p.name = name;
            p.rect = rect;
            p.follows.flags = follows;
            return new TestView(p);
        }

        // The child rect a reshape of the parent leaves behind, for a child
        // that starts at (10,10)-(30,30) inside a 100x100 parent.
        static LLRect reshaped(U32 follows, S32 delta_width, S32 delta_height)
        {
            std::unique_ptr<TestView> parent(view("parent", LLRect(0, 100, 100, 0)));
            TestView* child = view("child", LLRect(10, 30, 30, 10), follows);
            parent->addChild(child);

            parent->reshape(100 + delta_width, 100 + delta_height);
            return child->getRect();
        }
    };

    // A view with no children owns no child list at all, so everything that
    // reads one has to answer from an empty one it does not own.
    TEST_CASE_METHOD(llview_data, "llview: a view with no children answers from an empty child list", "[llview]")
    {
        std::unique_ptr<TestView> v(view("lonely"));

        CHECK(v->getChildCount() == 0);
        CHECK(v->getFirstChild() == nullptr);
        REQUIRE(v->getChildList() != nullptr);
        CHECK(v->getChildList()->empty());
        CHECK(v->beginChild() == v->endChild());
        CHECK(v->getTabOrder().empty());
    }

    // A view that had children and lost them still owns the list, and the list
    // is empty: asking it for its front is asking an empty list for its front.
    TEST_CASE_METHOD(llview_data, "llview: a view that lost its children has no first child", "[llview]")
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* child = view("child");
        v->addChild(child);
        CHECK(v->getFirstChild() == child);

        v->removeChild(child);
        CHECK(v->getChildCount() == 0);
        CHECK(v->getFirstChild() == nullptr);
        delete child;
    }

    // Children are added at the front, which is the front to draw last and the
    // first to be offered a mouse event.
    TEST_CASE_METHOD(llview_data, "llview: children are added at the front and can be sent to front or back", "[llview]")
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* first = view("first");
        TestView* second = view("second");
        v->addChild(first);
        v->addChild(second);

        CHECK(v->getChildCount() == 2);
        CHECK(v->getFirstChild() == second);

        v->sendChildToFront(first);
        CHECK(v->getFirstChild() == first);

        v->sendChildToBack(first);
        CHECK(v->getFirstChild() == second);
    }

    // A tab group is recorded for the child that was given one, and goes when
    // the child does. A child added without one is not in the order at all.
    TEST_CASE_METHOD(llview_data, "llview: a child's tab group is recorded and goes when the child does", "[llview]")
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* grouped = view("grouped");
        TestView* plain = view("plain");
        v->addChild(grouped, 7);
        v->addChild(plain);

        CHECK(v->getTabOrder().size() == size_t(1));
        REQUIRE(v->getTabOrder().find(grouped) != v->getTabOrder().end());
        CHECK(v->getTabOrder().find(grouped)->second == 7);
        CHECK(v->getTabOrder().find(plain) == v->getTabOrder().end());

        v->removeChild(grouped);
        CHECK(v->getTabOrder().empty());
        delete grouped;
    }

    // findChildView answers with a direct child before it looks inside any of
    // them, so a name that appears at both depths finds the near one.
    TEST_CASE_METHOD(llview_data, "llview: findChildView finds a direct child before looking deeper", "[llview]")
    {
        std::unique_ptr<TestView> root(view("root"));
        TestView* branch = view("branch");
        TestView* deep = view("target");
        TestView* near_child = view("target");
        branch->addChild(deep);
        root->addChild(branch);
        root->addChild(near_child);

        CHECK(root->findChildView("target") == near_child);
        CHECK(branch->findChildView("target") == deep);
        CHECK(root->findChildView("absent") == nullptr);
        CHECK(root->findChildView("absent", true) == nullptr);
    }

    // The follows flags decide what a parent's resize does to a child. Both
    // edges means the child stretches; the far edge alone means it moves; the
    // near edge alone means it stays where it is.
    TEST_CASE_METHOD(llview_data, "llview: the follows flags decide what a parent's resize does to a child", "[llview]")
    {
        LLRect r = reshaped(FOLLOWS_LEFT | FOLLOWS_TOP, 50, 0);
        UNSCOPED_INFO("left|top: width unchanged by a width change");
        CHECK(r.getWidth() == 20);
        UNSCOPED_INFO("left|top: left unchanged");
        CHECK(r.mLeft == 10);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP, 50, 0);
        UNSCOPED_INFO("left|right: stretches");
        CHECK(r.getWidth() == 70);
        UNSCOPED_INFO("left|right: left stays");
        CHECK(r.mLeft == 10);

        r = reshaped(FOLLOWS_RIGHT | FOLLOWS_TOP, 50, 0);
        UNSCOPED_INFO("right: width unchanged");
        CHECK(r.getWidth() == 20);
        UNSCOPED_INFO("right: moved with the edge");
        CHECK(r.mLeft == 60);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_BOTTOM, 0, 50);
        UNSCOPED_INFO("top|bottom: stretches");
        CHECK(r.getHeight() == 70);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_BOTTOM, 0, 50);
        UNSCOPED_INFO("bottom: height unchanged");
        CHECK(r.getHeight() == 20);
        UNSCOPED_INFO("bottom: bottom stays");
        CHECK(r.mBottom == 10);
    }

    // A view's pathname is its ancestors' names, and a name holding the
    // separator has to say so without looking like another level.
    TEST_CASE_METHOD(llview_data, "llview: a view's pathname is its ancestors' names with the separator escaped", "[llview]")
    {
        std::unique_ptr<TestView> root(view("root"));
        TestView* child = view("child");
        root->addChild(child);

        // The root is not in the path.
        CHECK(child->getPathname() == std::string("/child"));
        CHECK(root->getPathname() == std::string(""));

        TestView* slashed = view("a/b");
        root->addChild(slashed);
        CHECK(slashed->getPathname() == std::string("/a%2Fb"));

        CHECK(LLView::getPathname(nullptr) == std::string("NULL"));
    }

    // popVisible restores what pushVisible saved. A view that was never pushed
    // has nothing saved, and must not be given the value a view is constructed
    // with -- LLFloaterView::popVisibleAll walks the child list as it stands
    // when the pop runs, so it reaches views that were not there for the push.
    TEST_CASE_METHOD(llview_data, "llview: popVisible restores what pushVisible saved and nothing else", "[llview]")
    {
        std::unique_ptr<TestView> never(view("never pushed"));
        CHECK(never->getVisible());
        never->popVisible();
        UNSCOPED_INFO("an unmatched pop leaves it alone");
        CHECK(never->getVisible());

        std::unique_ptr<TestView> pushed(view("pushed"));
        pushed->pushVisible(false);
        CHECK_FALSE(pushed->getVisible());
        pushed->popVisible();
        CHECK(pushed->getVisible());

        // A second pop has nothing left to restore.
        pushed->setVisible(false);
        pushed->popVisible();
        UNSCOPED_INFO("the second pop restores nothing");
        CHECK_FALSE(pushed->getVisible());
    }

    // A hidden view keeps the transparency a floater tried to give it, rather
    // than the walk descending into a subtree nothing under it can be drawing.
    TEST_CASE_METHOD(llview_data, "llview: applyTransparencyType does not walk into a hidden view", "[llview]")
    {
        std::unique_ptr<TestView> root(view("root"));
        TestView* shown = view("shown");
        TestView* hidden = view("hidden");
        TestView* under_hidden = view("under hidden");
        hidden->addChild(under_hidden);
        root->addChild(shown);
        root->addChild(hidden);
        hidden->setVisible(false);

        LLView::sTransparencyViewsWalked = 0;
        root->applyTransparencyType(1);

        // root and shown; the hidden one holds the value and is not descended
        // into, so the view under it is never reached.
        CHECK(LLView::sTransparencyViewsWalked == 2);

        LLView::sTransparencyViewsWalked = 0;
        hidden->setVisible(true);
        UNSCOPED_INFO("showing it spends what it was holding");
        CHECK(LLView::sTransparencyViewsWalked == 2);
    }

    // childFromPoint offers the front-most child containing the point, and
    // never a hidden one.
    TEST_CASE_METHOD(llview_data, "llview: childFromPoint offers the front-most shown child at the point", "[llview]")
    {
        std::unique_ptr<TestView> root(view("root", LLRect(0, 100, 100, 0)));
        TestView* under = view("under", LLRect(0, 50, 50, 0));
        TestView* over = view("over", LLRect(0, 50, 50, 0));
        root->addChild(under);
        root->addChild(over);   // added last, so front-most

        CHECK(root->childFromPoint(10, 10) == over);

        over->setVisible(false);
        CHECK(root->childFromPoint(10, 10) == under);

        CHECK(root->childFromPoint(80, 80) == nullptr);
    }

    // followsAll is the four edges together, which is what its name says and
    // what a caller reaching for it would be asking.
    TEST_CASE_METHOD(llview_data, "llview: followsAll is true for the four edges together only", "[llview]")
    {
        std::unique_ptr<TestView> v(view("follower", LLRect(0, 10, 10, 0), FOLLOWS_LEFT));
        CHECK_FALSE(v->followsAll());

        v->setFollows(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP);
        CHECK_FALSE(v->followsAll());

        v->setFollowsAll();
        CHECK(v->followsAll());

        v->setFollowsNone();
        CHECK_FALSE(v->followsAll());
    }

    // A message sent to the children is sent to all of them. One child saying
    // it handled the message is the answer to give back, not a reason to stop:
    // a panel telling each of its accordions to store its state has as many
    // handlers as it has accordions.
        struct CountingView : public LLView
        {
            CountingView(const LLView::Params& p) : LLView(p) {}
            bool notifyChildren(const LLSD& info) override
            {
                ++mHeard;
                return true;
            }
            S32 mHeard = 0;
        };

    TEST_CASE_METHOD(llview_data, "llview: notifyChildren reaches every child after one has handled it", "[llview]")
    {
        std::unique_ptr<TestView> root(view("root", LLRect(0, 100, 100, 0)));
        std::vector<CountingView*> counters;
        for (S32 i = 0; i < 3; ++i)
        {
            LLView::Params p;
            p.name = "counter";
            p.rect = LLRect(0, 10, 10, 0);
            counters.push_back(new CountingView(p));
            root->addChild(counters.back());
        }

        UNSCOPED_INFO("a child handled it");
        CHECK(root->notifyChildren(LLSD().with("action", "store_state")));
        for (CountingView* counter : counters)
        {
            REQUIRE(counter->mHeard == 1);
        }
    }

    // Two views with no ancestor in common have no offset between them, and
    // that is what the answer says. The walk up from the far view ends at a
    // null, and so does the walk up from this one -- comparing those two nulls
    // is how every pair of views looked related.
    TEST_CASE_METHOD(llview_data, "llview: views with no ancestor in common have no offset between them", "[llview]")
    {
        std::unique_ptr<TestView> tree_a(view("a", LLRect(0, 100, 100, 0)));
        std::unique_ptr<TestView> tree_b(view("b", LLRect(0, 100, 100, 0)));
        TestView* leaf = view("leaf", LLRect(10, 30, 30, 10));
        tree_a->addChild(leaf);

        S32 x = 0, y = 0;
        CHECK_FALSE(leaf->localPointToOtherView(1, 1, &x, &y, tree_b.get()));

        LLRect rect;
        CHECK_FALSE(leaf->localRectToOtherView(leaf->getLocalRect(), &rect, tree_b.get()));

        // And a view that does share a root still answers, with the offset.
        TestView* sibling = view("sibling", LLRect(50, 80, 70, 60));
        tree_a->addChild(sibling);
        CHECK(leaf->localPointToOtherView(0, 0, &x, &y, sibling));
        UNSCOPED_INFO("offset by the difference of their origins");
        CHECK(x == 10 - 50);
        CHECK(y == 10 - 60);
    }

    // childHasKeyboardFocus asks about a child of this view. A name is not a
    // tree, so the same name under somebody else is not an answer, and neither
    // is this view holding the focus itself.
    TEST_CASE_METHOD(llview_data, "llview: childHasKeyboardFocus asks only about this view's own children", "[llview]")
    {
        std::unique_ptr<TestView> mine(view("mine", LLRect(0, 100, 100, 0)));
        std::unique_ptr<TestView> theirs(view("theirs", LLRect(0, 100, 100, 0)));
        TestView* my_combo = view("combo");
        TestView* their_combo = view("combo");
        mine->addChild(my_combo);
        theirs->addChild(their_combo);
        // Let go of before the views it is given to are deleted, a failed
        // check included: a plain view does not let go of it itself.
        ll_test::FocusScope focus;

        gFocusMgr.setKeyboardFocus(my_combo);
        CHECK(mine->childHasKeyboardFocus("combo"));
        CHECK_FALSE(theirs->childHasKeyboardFocus("combo"));
        CHECK_FALSE(mine->childHasKeyboardFocus("absent"));

        gFocusMgr.setKeyboardFocus(mine.get());
        CHECK_FALSE(mine->childHasKeyboardFocus("mine"));

        gFocusMgr.setKeyboardFocus(nullptr);
    }
}
