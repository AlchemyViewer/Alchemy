/**
 * @file alpanefolds_test.cpp
 * @brief ALPaneFolds: regions of a window that fold, and that come out into windows of their own
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

#include "../alpanefolds.h"

#include "../aldockpanel.h"
#include "../llbutton.h"
#include "../lllayoutstack.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gPaneFoldsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gPaneFoldsTestAnonName;
}

namespace tut
{
    struct alpanefolds_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A window with a stack of two layout panels, and beside it a plain
        // panel that is in no stack: the two kinds of region.
        struct Window
        {
            LLFloater*      floater = nullptr;
            LLLayoutPanel*  side = nullptr;
            LLButton*       fold = nullptr;
            LLPanel*        page = nullptr;
            LLButton*       in_page = nullptr;
        };

        static Window build()
        {
            Window w;
            LLFloater::Params fp(LLFloater::getDefaultParams());
            fp.name = "tool";
            fp.rect = LLRect(0, 400, 600, 0);
            fp.save_rect = false;
            fp.save_visibility = false;
            w.floater = new LLFloater(LLSD(), fp);

            LLLayoutStack::Params sp(LLUICtrlFactory::getDefaultParams<LLLayoutStack>());
            sp.name = "stack";
            sp.rect = LLRect(0, 300, 600, 0);
            sp.orientation = "horizontal";
            sp.animate = false;
            LLLayoutStack* stack = LLUICtrlFactory::create<LLLayoutStack>(sp);
            w.floater->addChild(stack);

            LLLayoutPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            pp.name = "side";
            pp.rect = LLRect(0, 300, 200, 0);
            pp.auto_resize = false;
            pp.min_dim = 40;
            w.side = LLUICtrlFactory::create<LLLayoutPanel>(pp);
            stack->addChild(w.side);

            LLLayoutPanel::Params mp(LLUICtrlFactory::getDefaultParams<LLLayoutPanel>());
            mp.name = "middle";
            mp.rect = LLRect(0, 300, 400, 0);
            mp.auto_resize = true;
            stack->addChild(LLUICtrlFactory::create<LLLayoutPanel>(mp));
            stack->updateLayout();

            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = "fold_side";
            bp.rect = LLRect(0, 320, 40, 300);
            w.fold = LLUICtrlFactory::create<LLButton>(bp);
            w.floater->addChild(w.fold);

            LLPanel::Params gp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            gp.name = "page";
            gp.rect = LLRect(0, 400, 600, 320);
            w.page = LLUICtrlFactory::create<LLPanel>(gp);
            w.floater->addChild(w.page);

            LLButton::Params ip(LLUICtrlFactory::getDefaultParams<LLButton>());
            ip.name = "in_the_page";
            ip.rect = LLRect(0, 20, 80, 0);
            w.in_page = LLUICtrlFactory::create<LLButton>(ip);
            w.page->addChild(w.in_page);

            return w;
        }

        static std::vector<ALPaneFolds::Pane> panes()
        {
            return { { "side", "side", "fold_side", "Side" },
                     { "page", "page", "", "Page" } };
        }
    };

    typedef test_group<alpanefolds_data> alpanefolds_test;
    typedef alpanefolds_test::object     alpanefolds_object;
    tut::alpanefolds_test alpanefolds_testgroup("alpanefolds");

    // A layout panel folds and has a size; a plain panel does neither, and
    // asking does nothing rather than something wrong.
    template<> template<>
    void alpanefolds_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        ensure("the side is open", !folds.collapsed("side"));
        folds.toggle("side");
        ensure("and folds", folds.collapsed("side"));
        ensure("and its button says so", !w.fold->getToggleState());
        ensure_equals("it has a size", folds.dim("side"), 200);

        ensure("the page is never folded", !folds.collapsed("page"));
        folds.toggle("page");
        ensure("nor can it be", !folds.collapsed("page"));
        ensure_equals("and has no size", folds.dim("page"), 0);
        w.floater->die();
    }

    // Either kind comes out into a window of its own and goes back, with
    // what it holds still where it was.
    template<> template<>
    void alpanefolds_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Window w = build();
        ALPaneFolds folds;
        folds.bind(w.floater, panes());

        ensure("home to start", !folds.out("page") && folds.window("page") == nullptr);
        folds.toggleOut("page");
        ensure("out", folds.out("page"));
        LLFloater* window = folds.window("page");
        ensure("in a window of its own", window != nullptr && window != w.floater);
        ensure("with what it held", w.in_page->getParentByType<LLFloater>() == window);
        ensure("and the page itself still on the window", w.page->getParent() == w.floater);

        folds.toggleOut("side");
        ensure("the layout panel comes out too", folds.out("side"));

        folds.dockAll();
        ensure("and both go home", !folds.out("page") && !folds.out("side"));
        ensure("the button back under its page", w.in_page->getParentByType<LLFloater>() == w.floater);
        w.floater->die();
    }

    // Written down: a fold and a size for the layout panel only, and for
    // both whether it is out and where its window was. Read back, a region
    // that was out comes out again where it was.
    template<> template<>
    void alpanefolds_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLSD state;
        {
            Window w = build();
            ALPaneFolds folds;
            folds.bind(w.floater, panes());
            folds.toggleOut("page");
            LLFloater* window = folds.window("page");
            window->setRect(LLRect(700, 500, 1000, 300));
            folds.save(state);
            ensure("the side's fold is written", state.has("fold_side"));
            ensure("and its size", state.has("dim_side"));
            ensure("the page has no fold to write", !state.has("fold_page"));
            ensure("nor a size", !state.has("dim_page"));
            ensure("but it is written as out", state["out_page"].asBoolean());
            ensure("with its window's rect", state.has("rect_page"));
            w.floater->die();
        }
        {
            Window w = build();
            ALPaneFolds folds;
            folds.bind(w.floater, panes());
            folds.load(state);
            ensure("read back, the page is out again", folds.out("page"));
            LLFloater* window = folds.window("page");
            ensure("in a window", window != nullptr);
            ensure_equals("as wide as it was", window->getRect().getWidth(), 300);
            ensure("and the side is home", !folds.out("side"));
            w.floater->die();
        }
    }
}
