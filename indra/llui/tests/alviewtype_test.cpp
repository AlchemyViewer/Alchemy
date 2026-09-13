/**
 * @file alviewtype_test.cpp
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

#include "../llbutton.h"
#include "../llcheckboxctrl.h"
#include "../llcombobox.h"
#include "../llfloater.h"
#include "../llfolderview.h"
#include "../lllayoutstack.h"
#include "../lllineeditor.h"
#include "../llmenugl.h"
#include "../llscrolllistctrl.h"
#include "../llspinctrl.h"
#include "../lltabcontainer.h"
#include "../lltextbox.h"
#include "../lluictrl.h"
#include "../lluictrlfactory.h"

#include <type_traits>

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
        AL_VIEW_TYPE(TestCtrl, LLUICtrl);
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

    struct alviewtype_data
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

    typedef test_group<alviewtype_data> alviewtype_test;
    typedef alviewtype_test::object     alviewtype_object;
    tut::alviewtype_test alviewtype_testgroup("alviewtype");

    // Each class answers for itself and every base that is a kind, and for
    // nothing else. A type with no kind is still answered, the slow way.
    template<> template<>
    void alviewtype_object::test<1>()
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
        ensure("a class declared in a test is found like any other", c->as<TestCtrl>() == c.get());

        std::unique_ptr<TestView> v(view("plain"));
        ensure("a plain view is a view", v->as<LLView>() == v.get());
        ensure("a plain view is not a control", v->as<LLUICtrl>() == nullptr);
        ensure("nor the test's own class", v->as<TestCtrl>() == nullptr);

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
    void alviewtype_object::test<2>()
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
    void alviewtype_object::test<3>()
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

    // The menu kinds: a bar and a context menu are menus, a separator is an
    // item, and a menu's parent item is held as one.
    template<> template<>
    void alviewtype_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLMenuGL::Params mp;
        mp.name = "menu";
        std::unique_ptr<LLMenuGL> menu(LLUICtrlFactory::create<LLMenuGL>(mp));
        LLMenuBarGL::Params bp;
        bp.name = "bar";
        std::unique_ptr<LLMenuBarGL> bar(LLUICtrlFactory::create<LLMenuBarGL>(bp));
        LLContextMenu::Params cp;
        cp.name = "context";
        std::unique_ptr<LLContextMenu> context(LLUICtrlFactory::create<LLContextMenu>(cp));
        LLMenuItemCallGL::Params ip;
        ip.name = "item";
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(ip);
        LLMenuItemSeparatorGL::Params sp;
        sp.name = "separator";
        std::unique_ptr<LLMenuItemSeparatorGL> separator(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(sp));

        ensure("a menu is a menu", menu->as<LLMenuGL>() == menu.get());
        ensure("a menu is not a bar", menu->as<LLMenuBarGL>() == nullptr);
        ensure("a menu is not a context menu", menu->as<LLContextMenu>() == nullptr);
        ensure("a menu is not an item", menu->as<LLMenuItemGL>() == nullptr);
        ensure("a bar is a menu", bar->as<LLMenuGL>() == bar.get());
        ensure("a bar is a bar", bar->as<LLMenuBarGL>() == bar.get());
        ensure("a context menu is a menu", context->as<LLMenuGL>() == context.get());
        ensure("a context menu is a context menu", context->as<LLContextMenu>() == context.get());
        ensure("a context menu is not a bar", context->as<LLMenuBarGL>() == nullptr);
        ensure("an item is an item", item->as<LLMenuItemGL>() == item);
        ensure("an item is not a separator", item->as<LLMenuItemSeparatorGL>() == nullptr);
        ensure("an item is not a menu", item->as<LLMenuGL>() == nullptr);
        ensure("a separator is an item", separator->as<LLMenuItemGL>() == separator.get());
        ensure("a separator is a separator", separator->as<LLMenuItemSeparatorGL>() == separator.get());

        menu->setParentMenuItem(item);
        ensure("the parent item is held as one", menu->getParentMenuItem() == item);
        delete item;
        ensure("and let go when it dies", menu->getParentMenuItem() == nullptr);
    }

    // Each class's place in the hierarchy is fixed at compile time, and a
    // class that made no declaration of its own is known not to have one.
    static_assert(LLView::sViewType.mDepth == 0);
    static_assert(LLUICtrl::sViewType.mDepth == 1);
    static_assert(LLPanel::sViewType.mDepth == 2);
    static_assert(LLFloater::sViewType.mDepth == 3);
    static_assert(LLFolderView::sViewType.mDepth == 3);
    static_assert(LLSpinCtrl::sViewType.mDepth == 3);
    static_assert(ALViewTypeOf<LLButton>::declared);
    static_assert(ALViewTypeOf<TestCtrl>::declared);
    static_assert(!ALViewTypeOf<AskingView>::declared);   // and would not compile in as<>()

    // The ancestor chain is the one the class heads declare, and the
    // is-a test reads it the same way at compile time as at run time.
    static_assert(LLPanel::sViewType.mAncestors[0] == &LLView::sViewType);
    static_assert(LLPanel::sViewType.mAncestors[1] == &LLUICtrl::sViewType);
    static_assert(LLFloater::sViewType.mAncestors[2] == &LLPanel::sViewType);
    static_assert(LLFloater::sViewType.isA(LLPanel::sViewType));
    static_assert(LLFloater::sViewType.isA(LLFloater::sViewType));
    static_assert(!LLPanel::sViewType.isA(LLFloater::sViewType));
    static_assert(!LLFloaterView::sViewType.isA(LLPanel::sViewType));

    // The widget types the name lookups ask for most: each is itself, a
    // control, and none of the others. A check box, a combo box and a scroll
    // list measure text as they are built, which the fixture's fonts cannot
    // answer without GL textures, so those three are not built here; each
    // of their overrides names LLUICtrl as its immediate base.
    template<> template<>
    void alviewtype_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLButton::Params bp;         bp.name = "button";
        LLTextBox::Params tp;        tp.name = "text";
        LLLineEditor::Params lp;     lp.name = "line";
        LLSpinCtrl::Params spp;      spp.name = "spin";
        LLTabContainer::Params tcp;  tcp.name = "tabs";

        std::unique_ptr<LLButton>         button(LLUICtrlFactory::create<LLButton>(bp));
        std::unique_ptr<LLTextBox>        text(LLUICtrlFactory::create<LLTextBox>(tp));
        std::unique_ptr<LLLineEditor>     line(LLUICtrlFactory::create<LLLineEditor>(lp));
        std::unique_ptr<LLSpinCtrl>       spin(LLUICtrlFactory::create<LLSpinCtrl>(spp));
        std::unique_ptr<LLTabContainer>   tabs(LLUICtrlFactory::create<LLTabContainer>(tcp));

        ensure("a button is a button", button->as<LLButton>() == button.get());
        ensure("a text box is a text box", text->as<LLTextBox>() == text.get());
        ensure("a line editor is a line editor", line->as<LLLineEditor>() == line.get());
        ensure("a spin control is a spin control", spin->as<LLSpinCtrl>() == spin.get());
        ensure("a tab container is a tab container", tabs->as<LLTabContainer>() == tabs.get());

        ensure("a button is a control", button->as<LLUICtrl>() == button.get());
        ensure("a spin control is a control, through its float base", spin->as<LLUICtrl>() == spin.get());
        ensure("a tab container is a panel", tabs->as<LLPanel>() == tabs.get());
        ensure("a text box is a text base", text->as<LLTextBase>() == text.get());

        ensure("a button is not a text box", button->as<LLTextBox>() == nullptr);
        ensure("a text box is not a line editor", text->as<LLLineEditor>() == nullptr);
        ensure("a line editor is not a spin control", line->as<LLSpinCtrl>() == nullptr);
        ensure("a spin control is not a tab container", spin->as<LLTabContainer>() == nullptr);
        ensure("a tab container is not a button", tabs->as<LLButton>() == nullptr);
        ensure("a button is not a panel", button->as<LLPanel>() == nullptr);
        ensure("none of them is a check box", button->as<LLCheckBoxCtrl>() == nullptr && line->as<LLCheckBoxCtrl>() == nullptr);
        ensure("none of them is a combo box or a scroll list", spin->as<LLComboBox>() == nullptr && tabs->as<LLScrollListCtrl>() == nullptr);
    }
}
