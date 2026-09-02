/**
 * @file alviewkind_test.cpp
 * @brief Tests for asking a view what it is
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

#include "../llfloater.h"
#include "../lllayoutstack.h"
#include "../lluictrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gViewKindTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gViewKindTestAnonName;
}

namespace tut
{
    struct TestView : public LLView
    {
        TestView(const LLView::Params& p) : LLView(p) {}
    };

    struct TestCtrl : public LLUICtrl
    {
        TestCtrl(const LLUICtrl::Params& p) : LLUICtrl(p) {}
    };

    struct TestFloater : public LLFloater
    {
        TestFloater(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
    };

    // A parent that asks what a child is as it leaves. ~LLView removes a
    // child from its parent after every derived destructor has run.
    struct AskingView : public LLView
    {
        AskingView(const LLView::Params& p) : LLView(p) {}

        bool mLeaverWasCtrl { false };

        void removeChild(LLView* child) override
        {
            mLeaverWasCtrl = child->as<LLUICtrl>() != nullptr;
            LLView::removeChild(child);
        }
    };

    struct alviewkind_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static TestView* view(const std::string& name)
        {
            LLView::Params p;
            p.name = name;
            p.rect = LLRect(0, 10, 10, 0);
            return new TestView(p);
        }

        static TestCtrl* ctrl(const std::string& name)
        {
            LLUICtrl::Params p;
            p.name = name;
            p.rect = LLRect(0, 10, 10, 0);
            return new TestCtrl(p);
        }

        static LLFloaterView* floaterView()
        {
            LLFloaterView::Params p;
            p.name = "floater_view";
            p.rect = LLRect(0, 600, 800, 0);
            return LLUICtrlFactory::create<LLFloaterView>(p);
        }

        static TestFloater* floater()
        {
            LLFloater::Params p;
            p.name = "floater";
            p.rect = LLRect(0, 100, 200, 0);
            return new TestFloater(p);
        }

        static LLLayoutStack* stack()
        {
            LLLayoutStack::Params p;
            p.name = "stack";
            p.rect = LLRect(0, 100, 300, 0);
            p.orientation = LLLayoutStack::HORIZONTAL;
            return LLUICtrlFactory::create<LLLayoutStack>(p);
        }

        static LLLayoutPanel* panel()
        {
            LLLayoutPanel::Params p;
            p.name = "panel";
            p.rect = LLRect(0, 100, 100, 0);
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        }
    };

    typedef test_group<alviewkind_data> alviewkind_test;
    typedef alviewkind_test::object     alviewkind_object;
    tut::alviewkind_test alviewkind_testgroup("alviewkind");

    // Each class answers for itself and every base that is a kind, and for
    // nothing else. A type with no kind is still answered, the slow way.
    template<> template<>
    void alviewkind_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();
        TestFloater* f = floater();     // parents itself into gFloaterView

        ensure("a floater is a floater", f->as<LLFloater>() == f);
        ensure("a floater is a panel", f->as<LLPanel>() == f);
        ensure("a floater is a control", f->as<LLUICtrl>() == f);
        ensure("a floater is a view", f->as<LLView>() == f);
        ensure("a floater is not a floater view", f->as<LLFloaterView>() == nullptr);

        ensure("a floater view is a floater view", fv->as<LLFloaterView>() == fv.get());
        ensure("a floater view is a control", fv->as<LLUICtrl>() == fv.get());
        ensure("a floater view is not a panel", fv->as<LLPanel>() == nullptr);
        ensure("a floater view is not a floater", fv->as<LLFloater>() == nullptr);

        std::unique_ptr<TestCtrl> c(ctrl("ctrl"));
        ensure("a control is a control", c->as<LLUICtrl>() == c.get());
        ensure("a control is not a panel", c->as<LLPanel>() == nullptr);
        ensure("its own type has no kind and is still found", c->as<TestCtrl>() == c.get());

        std::unique_ptr<TestView> v(view("plain"));
        ensure("a plain view is a view", v->as<LLView>() == v.get());
        ensure("a plain view is not a control", v->as<LLUICtrl>() == nullptr);
        ensure("nor any type with no kind that it is not", v->as<TestCtrl>() == nullptr);

        const LLView* cf = f;
        ensure("a const view answers the same", cf->as<LLFloater>() == f);

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* lp = panel();
        s->addPanel(lp);
        ensure("a layout stack is a layout stack", s->as<LLLayoutStack>() == s.get());
        ensure("a layout stack is not a control", s->as<LLUICtrl>() == nullptr);
        ensure("a layout panel is a layout panel", lp->as<LLLayoutPanel>() == lp);
        ensure("a layout panel is a panel", lp->as<LLPanel>() == lp);
        ensure("a layout panel is not a layout stack", lp->as<LLLayoutStack>() == nullptr);
        ensure("its parent is its stack", lp->getParentAs<LLLayoutStack>() == s.get());

        fv.reset();
        gFloaterView = nullptr;
    }

    // The parent as a kind, and the nearest ancestor of a kind.
    template<> template<>
    void alviewkind_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();
        TestFloater* f = floater();
        TestView* inner = view("inner");
        f->addChild(inner);

        ensure("the floater's parent is the floater view", f->getParentAs<LLFloaterView>() == fv.get());
        ensure("the floater view has no parent", fv->getParentAs<LLFloater>() == nullptr);
        ensure("the inner view's parent is not a floater view", inner->getParentAs<LLFloaterView>() == nullptr);
        ensure("the inner view's parent is the floater", inner->getParentAs<LLFloater>() == f);
        ensure("the nearest floater above the inner view", inner->getParentByType<LLFloater>() == f);
        ensure("the nearest floater view above it, through the floater", inner->getParentByType<LLFloaterView>() == fv.get());
        ensure("no ancestor is a type with no kind", inner->getParentByType<TestCtrl>() == nullptr);

        fv.reset();
        gFloaterView = nullptr;
    }

    // A view being destroyed answers for the base that is left, as
    // dynamic_cast does: by the time ~LLView takes it out of its parent, a
    // control is no longer one. A member bit would still say it was.
    template<> template<>
    void alviewkind_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLView::Params p;
        p.name = "asking";
        p.rect = LLRect(0, 10, 10, 0);
        std::unique_ptr<AskingView> parent(new AskingView(p));

        TestCtrl* c = ctrl("ctrl");
        parent->addChild(c);
        parent->removeChild(c);
        ensure("a live control leaving is a control", parent->mLeaverWasCtrl);

        parent->addChild(c);
        delete c;
        ensure("a dying control leaving is not", !parent->mLeaverWasCtrl);
    }
}
