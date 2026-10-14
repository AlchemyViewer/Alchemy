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
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
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

    // Each class answers for itself and every base that is a kind, and for
    // nothing else; a class a test declares answers as the library's do.
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: as<> finds a view's own class and each base kind but no other", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());
        TestFloater* f = floater();     // parents itself into gFloaterView

        CHECK(f->as<LLFloater>() == f);
        CHECK(f->as<LLPanel>() == f);
        CHECK(f->as<LLUICtrl>() == f);
        CHECK(f->as<LLView>() == f);
        CHECK(f->as<LLFloaterView>() == nullptr);

        CHECK(fv->as<LLFloaterView>() == fv.get());
        CHECK(fv->as<LLUICtrl>() == fv.get());
        CHECK(fv->as<LLPanel>() == nullptr);
        CHECK(fv->as<LLFloater>() == nullptr);

        std::unique_ptr<TestCtrl> c(ctrl("ctrl"));
        CHECK(c->as<LLUICtrl>() == c.get());
        CHECK(c->as<LLPanel>() == nullptr);
        UNSCOPED_INFO("a class declared in a test is found like any other");
        CHECK(c->as<TestCtrl>() == c.get());

        std::unique_ptr<TestView> v(view("plain"));
        CHECK(v->as<LLView>() == v.get());
        CHECK(v->as<LLUICtrl>() == nullptr);
        CHECK(v->as<TestCtrl>() == nullptr);

        const LLView* cf = f;
        UNSCOPED_INFO("a const view answers the same");
        CHECK(cf->as<LLFloater>() == f);

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* lp = panel();
        s->addPanel(lp);
        CHECK(s->as<LLLayoutStack>() == s.get());
        CHECK(s->as<LLUICtrl>() == nullptr);
        CHECK(lp->as<LLLayoutPanel>() == lp);
        CHECK(lp->as<LLPanel>() == lp);
        CHECK(lp->as<LLLayoutStack>() == nullptr);
        CHECK(lp->getParentAs<LLLayoutStack>() == s.get());

        fv.reset();
    }

    // The parent as a kind, and the nearest ancestor of a kind.
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: getParentAs and getParentByType find a parent or ancestor by kind", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());
        TestFloater* f = floater();
        TestView* inner = view("inner");
        f->addChild(inner);

        CHECK(f->getParentAs<LLFloaterView>() == fv.get());
        UNSCOPED_INFO("the floater view has no parent");
        CHECK(fv->getParentAs<LLFloater>() == nullptr);
        CHECK(inner->getParentAs<LLFloaterView>() == nullptr);
        CHECK(inner->getParentAs<LLFloater>() == f);
        CHECK(inner->getParentByType<LLFloater>() == f);
        UNSCOPED_INFO("the nearest floater view above it, through the floater");
        CHECK(inner->getParentByType<LLFloaterView>() == fv.get());
        UNSCOPED_INFO("its ancestors are controls, but none a TestCtrl");
        CHECK(inner->getParentByType<TestCtrl>() == nullptr);

        fv.reset();
    }

    // A view being destroyed answers for the base that is left, as
    // dynamic_cast does: by the time ~LLView takes it out of its parent, a
    // control is no longer one. A member bit would still say it was.
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: a control being destroyed no longer answers as a control", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLView::Params p;
        p.name = "asking";
        p.rect = LLRect(0, 10, 10, 0);
        std::unique_ptr<AskingView> parent(new AskingView(p));

        TestCtrl* c = ctrl("ctrl");
        parent->addChild(c);
        parent->removeChild(c);
        UNSCOPED_INFO("a live control leaving is a control");
        CHECK(parent->mLeaverWasCtrl);

        parent->addChild(c);
        delete c;
        UNSCOPED_INFO("a dying control leaving is not");
        CHECK_FALSE(parent->mLeaverWasCtrl);
    }

    // The menu kinds: a bar and a context menu are menus, a separator is an
    // item, and a menu's parent item is held as one.
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: bars and context menus are menus and a separator is an item", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        CHECK(menu->as<LLMenuGL>() == menu.get());
        CHECK(menu->as<LLMenuBarGL>() == nullptr);
        CHECK(menu->as<LLContextMenu>() == nullptr);
        CHECK(menu->as<LLMenuItemGL>() == nullptr);
        CHECK(bar->as<LLMenuGL>() == bar.get());
        CHECK(bar->as<LLMenuBarGL>() == bar.get());
        CHECK(context->as<LLMenuGL>() == context.get());
        CHECK(context->as<LLContextMenu>() == context.get());
        CHECK(context->as<LLMenuBarGL>() == nullptr);
        CHECK(item->as<LLMenuItemGL>() == item);
        CHECK(item->as<LLMenuItemSeparatorGL>() == nullptr);
        CHECK(item->as<LLMenuGL>() == nullptr);
        CHECK(separator->as<LLMenuItemGL>() == separator.get());
        CHECK(separator->as<LLMenuItemSeparatorGL>() == separator.get());

        menu->setParentMenuItem(item);
        UNSCOPED_INFO("the parent item is held as one");
        CHECK(menu->getParentMenuItem() == item);
        delete item;
        UNSCOPED_INFO("and let go when it dies");
        CHECK(menu->getParentMenuItem() == nullptr);
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
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: the common widget types are themselves and controls and not each other", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        CHECK(button->as<LLButton>() == button.get());
        CHECK(text->as<LLTextBox>() == text.get());
        CHECK(line->as<LLLineEditor>() == line.get());
        CHECK(spin->as<LLSpinCtrl>() == spin.get());
        CHECK(tabs->as<LLTabContainer>() == tabs.get());

        CHECK(button->as<LLUICtrl>() == button.get());
        UNSCOPED_INFO("a spin control is a control, through its float base");
        CHECK(spin->as<LLUICtrl>() == spin.get());
        CHECK(tabs->as<LLPanel>() == tabs.get());
        CHECK(text->as<LLTextBase>() == text.get());

        CHECK(button->as<LLTextBox>() == nullptr);
        CHECK(text->as<LLLineEditor>() == nullptr);
        CHECK(line->as<LLSpinCtrl>() == nullptr);
        CHECK(spin->as<LLTabContainer>() == nullptr);
        CHECK(tabs->as<LLButton>() == nullptr);
        CHECK(button->as<LLPanel>() == nullptr);
        CHECK(button->as<LLCheckBoxCtrl>() == nullptr);
        CHECK(line->as<LLCheckBoxCtrl>() == nullptr);
        CHECK(spin->as<LLComboBox>() == nullptr);
        CHECK(tabs->as<LLScrollListCtrl>() == nullptr);
    }

    // Bound by on_check, a binding the item's own params do not name.
    TEST_CASE_METHOD(alviewtype_data, "alviewtype: a check item bound by on_check stays bound and shows the setting", "[alviewtype]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlGroup* config = LLUI::getInstance()->mSettingGroups["config"];
        if (!config->controlExists("ViewTypeCheck"))
        {
            config->declareBOOL("ViewTypeCheck", true, "A setting a check item shows");
        }
        config->setBOOL("ViewTypeCheck", true);
        LLMenuItemCheckGL::Params cp;
        cp.name                  = "check";
        cp.label                 = "Check";
        cp.on_check.control_name = "ViewTypeCheck";
        std::unique_ptr<LLMenuItemCheckGL> item(LLUICtrlFactory::create<LLMenuItemCheckGL>(cp));
        REQUIRE(item->getControlVariable() != nullptr);
        item->buildDrawLabel();
        UNSCOPED_INFO("checked while it is on");
        CHECK(item->getValue().asBoolean());
        config->setBOOL("ViewTypeCheck", false);
        item->buildDrawLabel();
        UNSCOPED_INFO("not checked once it is off");
        CHECK_FALSE(item->getValue().asBoolean());
        config->setBOOL("ViewTypeCheck", true);
        item->buildDrawLabel();
        UNSCOPED_INFO("and checked again");
        CHECK(item->getValue().asBoolean());
    }
}
