/**
 * @file aljumpbar_test.cpp
 * @brief The path, what folds when it does not fit, and what each step offers.
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

#include "../aljumpbar.h"

#include "../llbutton.h"
#include "../llflyoutbutton.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gJumpTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gJumpTestAnonName;
}

namespace tut
{
    struct aljumpbar_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALJumpBar* make(S32 width = 600)
        {
            ALJumpBar::Params p(LLUICtrlFactory::getDefaultParams<ALJumpBar>());
            p.rect = LLRect(0, 22, width, 0);
            return LLUICtrlFactory::create<ALJumpBar>(p);
        }

        // root > body > buttons > close_btn, with siblings on the last two.
        static std::vector<ALJumpBar::Crumb> path()
        {
            std::vector<ALJumpBar::Crumb> crumbs;
            for (const char* name : { "root", "body", "buttons", "close_btn" })
            {
                ALJumpBar::Crumb crumb;
                crumb.label = name;
                crumb.value = name;
                crumbs.push_back(crumb);
            }
            crumbs[2].alternatives = { { "buttons", "buttons" }, { "footer", "footer" } };
            crumbs[3].alternatives = { { "close_btn", "close_btn" }, { "help_btn", "help_btn" } };
            return crumbs;
        }

        static S32 crumbsIn(const ALJumpBar* bar)
        {
            S32 n = 0;
            for (LLView* child : *bar->getChildList())
            {
                n += child->getName().rfind("crumb_", 0) == 0;
            }
            return n;
        }
    };

    typedef test_group<aljumpbar_data> aljumpbar_test;
    typedef aljumpbar_test::object     aljumpbar_object;
    tut::aljumpbar_test aljumpbar_testgroup("aljumpbar");

    // One crumb per step, in order, and a step with somewhere else to be is
    // the one with the arrow on it.
    template<> template<>
    void aljumpbar_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        ensure_equals("one each", crumbsIn(bar), 4);
        ensure("a step with nowhere else to be is a plain button",
               bar->getChild<LLView>("crumb_0")->as<LLFlyoutButton>() == nullptr);
        ensure("and one with siblings offers them",
               bar->getChild<LLView>("crumb_2")->as<LLFlyoutButton>() != nullptr);

        // Left to right, in the order the path reads.
        ensure("in order", bar->getChild<LLView>("crumb_0")->getRect().mRight
                        <= bar->getChild<LLView>("crumb_1")->getRect().mLeft);
        delete bar;
    }

    // Pressing a crumb goes there; picking one of its siblings goes there
    // instead. One signal, because they are one gesture with two answers.
    template<> template<>
    void aljumpbar_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());

        std::vector<std::pair<size_t, std::string> > chosen;
        bar->onChose([&chosen](size_t at, const std::string& value)
        {
            chosen.emplace_back(at, value);
        });

        bar->getChild<LLButton>("crumb_1")->onCommit();
        ensure_equals("said once", chosen.size(), 1u);
        ensure_equals("which crumb", chosen.back().first, 1u);
        ensure_equals("and its own value", chosen.back().second, std::string("body"));

        // Sideways: the sibling picked is what is answered, not the crumb.
        LLFlyoutButton* sideways = bar->getChild<LLView>("crumb_3")->as<LLFlyoutButton>();
        ensure("the last step offers its siblings", sideways != nullptr);
        sideways->setSelectedByValue(LLSD("help_btn"), true);
        sideways->onCommit();
        ensure_equals("said again", chosen.size(), 2u);
        ensure_equals("on the same crumb", chosen.back().first, 3u);
        ensure_equals("with the sibling chosen", chosen.back().second, std::string("help_btn"));
        delete bar;
    }

    // A path longer than the room folds from the front, because the end of a
    // path says more about where you are than the start does -- and the fold
    // is a crumb too, offering what it swallowed.
    template<> template<>
    void aljumpbar_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make(600);
        bar->setPath(path());
        ensure_equals("all four fit", crumbsIn(bar), 4);

        bar->reshape(150, 22);
        ensure("fewer than four are shown", crumbsIn(bar) < 4);
        ensure("the last step is still one of them",
               bar->findChild<LLView>("crumb_3", true) != nullptr);
        ensure("and the first is not",
               bar->findChild<LLView>("crumb_0", true) == nullptr);

        // Everything shown is inside the room there is.
        for (LLView* child : *bar->getChildList())
        {
            if (child->getName().rfind("crumb_", 0) == 0)
            {
                ensure("inside the bar: " + child->getName(), child->getRect().mRight <= 150);
            }
        }

        bar->reshape(600, 22);
        ensure_equals("and back again when there is room", crumbsIn(bar), 4);
        delete bar;
    }

    // What is said past the end of the path is not a step, so it is not
    // something to press.
    template<> template<>
    void aljumpbar_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALJumpBar* bar = make();
        bar->setPath(path());
        ensure("nothing said by default", bar->findChild<LLView>("trailer", true) == nullptr);

        bar->setTrailer("default/en");
        const LLView* trailer = bar->findChild<LLView>("trailer", true);
        ensure("said", trailer != nullptr);
        ensure("past the end of the path",
               trailer->getRect().mLeft >= bar->getChild<LLView>("crumb_3")->getRect().mRight);
        ensure("and it is not a button", trailer->as<LLButton>() == nullptr);
        delete bar;
    }
}
