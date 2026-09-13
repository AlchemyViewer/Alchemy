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

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gViewTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gViewTestAnonName;
}

namespace tut
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

    typedef test_group<llview_data> llview_test;
    typedef llview_test::object     llview_object;
    tut::llview_test llview_testgroup("llview");

    // A view with no children owns no child list at all, so everything that
    // reads one has to answer from an empty one it does not own.
    template<> template<>
    void llview_object::test<1>()
    {
        std::unique_ptr<TestView> v(view("lonely"));

        ensure_equals("no children", v->getChildCount(), 0);
        ensure("no first child", v->getFirstChild() == nullptr);
        ensure("child list is readable", v->getChildList() != nullptr);
        ensure("child list is empty", v->getChildList()->empty());
        ensure("child iteration is empty", v->beginChild() == v->endChild());
        ensure("tab order is empty", v->getTabOrder().empty());
    }

    // A view that had children and lost them still owns the list, and the list
    // is empty: asking it for its front is asking an empty list for its front.
    template<> template<>
    void llview_object::test<2>()
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* child = view("child");
        v->addChild(child);
        ensure("first child is the child", v->getFirstChild() == child);

        v->removeChild(child);
        ensure_equals("no children left", v->getChildCount(), 0);
        ensure("no first child once emptied", v->getFirstChild() == nullptr);
        delete child;
    }

    // Children are added at the front, which is the front to draw last and the
    // first to be offered a mouse event.
    template<> template<>
    void llview_object::test<3>()
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* first = view("first");
        TestView* second = view("second");
        v->addChild(first);
        v->addChild(second);

        ensure_equals("both children", v->getChildCount(), 2);
        ensure("last added is at the front", v->getFirstChild() == second);

        v->sendChildToFront(first);
        ensure("sent to front", v->getFirstChild() == first);

        v->sendChildToBack(first);
        ensure("sent to back", v->getFirstChild() == second);
    }

    // A tab group is recorded for the child that was given one, and goes when
    // the child does. A child added without one is not in the order at all.
    template<> template<>
    void llview_object::test<4>()
    {
        std::unique_ptr<TestView> v(view("parent"));
        TestView* grouped = view("grouped");
        TestView* plain = view("plain");
        v->addChild(grouped, 7);
        v->addChild(plain);

        ensure_equals("one child in the tab order", v->getTabOrder().size(), size_t(1));
        ensure("the grouped one", v->getTabOrder().find(grouped) != v->getTabOrder().end());
        ensure_equals("with its group", v->getTabOrder().find(grouped)->second, 7);
        ensure("the ungrouped one is absent", v->getTabOrder().find(plain) == v->getTabOrder().end());

        v->removeChild(grouped);
        ensure("tab order empty once the child is gone", v->getTabOrder().empty());
        delete grouped;
    }

    // findChildView answers with a direct child before it looks inside any of
    // them, so a name that appears at both depths finds the near one.
    template<> template<>
    void llview_object::test<5>()
    {
        std::unique_ptr<TestView> root(view("root"));
        TestView* branch = view("branch");
        TestView* deep = view("target");
        TestView* near_child = view("target");
        branch->addChild(deep);
        root->addChild(branch);
        root->addChild(near_child);

        ensure("direct child wins", root->findChildView("target") == near_child);
        ensure("deep one is still reachable from its own parent",
               branch->findChildView("target") == deep);
        ensure("a name nobody has is not found",
               root->findChildView("absent") == nullptr);
        ensure("and not found deep either",
               root->findChildView("absent", true) == nullptr);
    }

    // The follows flags decide what a parent's resize does to a child. Both
    // edges means the child stretches; the far edge alone means it moves; the
    // near edge alone means it stays where it is.
    template<> template<>
    void llview_object::test<6>()
    {
        LLRect r = reshaped(FOLLOWS_LEFT | FOLLOWS_TOP, 50, 0);
        ensure_equals("left|top: width unchanged by a width change", r.getWidth(), 20);
        ensure_equals("left|top: left unchanged", r.mLeft, 10);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP, 50, 0);
        ensure_equals("left|right: stretches", r.getWidth(), 70);
        ensure_equals("left|right: left stays", r.mLeft, 10);

        r = reshaped(FOLLOWS_RIGHT | FOLLOWS_TOP, 50, 0);
        ensure_equals("right: width unchanged", r.getWidth(), 20);
        ensure_equals("right: moved with the edge", r.mLeft, 60);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_BOTTOM, 0, 50);
        ensure_equals("top|bottom: stretches", r.getHeight(), 70);

        r = reshaped(FOLLOWS_LEFT | FOLLOWS_BOTTOM, 0, 50);
        ensure_equals("bottom: height unchanged", r.getHeight(), 20);
        ensure_equals("bottom: bottom stays", r.mBottom, 10);
    }

    // A view's pathname is its ancestors' names, and a name holding the
    // separator has to say so without looking like another level.
    template<> template<>
    void llview_object::test<7>()
    {
        std::unique_ptr<TestView> root(view("root"));
        TestView* child = view("child");
        root->addChild(child);

        // The root is not in the path.
        ensure_equals("child under root", child->getPathname(), std::string("/child"));
        ensure_equals("root itself has no path", root->getPathname(), std::string(""));

        TestView* slashed = view("a/b");
        root->addChild(slashed);
        ensure_equals("separator in a name is escaped",
                      slashed->getPathname(), std::string("/a%2Fb"));

        ensure_equals("the static form handles no view at all",
                      LLView::getPathname(nullptr), std::string("NULL"));
    }

    // popVisible restores what pushVisible saved. A view that was never pushed
    // has nothing saved, and must not be given the value a view is constructed
    // with -- LLFloaterView::popVisibleAll walks the child list as it stands
    // when the pop runs, so it reaches views that were not there for the push.
    template<> template<>
    void llview_object::test<8>()
    {
        std::unique_ptr<TestView> never(view("never pushed"));
        ensure("visible to begin with", never->getVisible());
        never->popVisible();
        ensure("an unmatched pop leaves it alone", never->getVisible());

        std::unique_ptr<TestView> pushed(view("pushed"));
        pushed->pushVisible(false);
        ensure("hidden by the push", !pushed->getVisible());
        pushed->popVisible();
        ensure("and put back by the pop", pushed->getVisible());

        // A second pop has nothing left to restore.
        pushed->setVisible(false);
        pushed->popVisible();
        ensure("the second pop restores nothing", !pushed->getVisible());
    }

    // A hidden view keeps the transparency a floater tried to give it, rather
    // than the walk descending into a subtree nothing under it can be drawing.
    template<> template<>
    void llview_object::test<9>()
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
        ensure_equals("the walk stops at a hidden view",
                      LLView::sTransparencyViewsWalked, 2);

        LLView::sTransparencyViewsWalked = 0;
        hidden->setVisible(true);
        ensure_equals("showing it spends what it was holding",
                      LLView::sTransparencyViewsWalked, 2);
    }

    // childFromPoint offers the front-most child containing the point, and
    // never a hidden one.
    template<> template<>
    void llview_object::test<10>()
    {
        std::unique_ptr<TestView> root(view("root", LLRect(0, 100, 100, 0)));
        TestView* under = view("under", LLRect(0, 50, 50, 0));
        TestView* over = view("over", LLRect(0, 50, 50, 0));
        root->addChild(under);
        root->addChild(over);   // added last, so front-most

        ensure("front-most child wins", root->childFromPoint(10, 10) == over);

        over->setVisible(false);
        ensure("a hidden child is not offered", root->childFromPoint(10, 10) == under);

        ensure("a point outside every child finds none",
               root->childFromPoint(80, 80) == nullptr);
    }
}
