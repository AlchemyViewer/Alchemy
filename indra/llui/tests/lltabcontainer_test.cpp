/**
 * @file lltabcontainer_test.cpp
 * @brief That a tab says what it is, and what it has to say beyond that.
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

#include "../lltabcontainer.h"

#include "../llbutton.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gTabContainerTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gTabContainerTestAnonName;
}

namespace tut
{
    struct lltabcontainer_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLTabContainer* build(LLTabContainer::TabPosition position = LLTabContainer::TOP)
        {
            LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
            p.name = "tabs";
            p.rect = LLRect(0, 300, 400, 0);
            p.tab_position = position;
            return LLUICtrlFactory::create<LLTabContainer>(p);
        }

        // A page, with or without something written about it. Nothing to
        // measure: this fixture loads font metrics but no glyphs, so the
        // label stays empty, which is also what an icon strip asks for.
        static LLPanel* page(const std::string& name, const std::string& tool_tip)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = name;
            p.rect = LLRect(0, 260, 400, 0);
            if (!tool_tip.empty())
            {
                p.tool_tip = tool_tip;
            }
            return LLUICtrlFactory::create<LLPanel>(p);
        }

        // The button a container makes for a page. It names it after the page
        // and the strip it is in, which is the only way in from outside.
        static LLButton* tabButton(LLTabContainer* tabs, const std::string& name, bool vertical = false)
        {
            return tabs->findChild<LLButton>((vertical ? "vtab_" : "htab_") + name, true);
        }
    };

    typedef test_group<lltabcontainer_data> lltabcontainer_test;
    typedef lltabcontainer_test::object     lltabcontainer_object;
    tut::lltabcontainer_test lltabcontainer_testgroup("lltabcontainer");

    // What the file wrote about a page is what the button that selects it
    // says. A tab with an icon and no label has nothing else to say it.
    template<> template<>
    void lltabcontainer_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* described = page("described", "What this mode is for");
        tabs->addTabPanel(described);

        LLButton* button = tabButton(tabs, "described");
        ensure("the page has a button", button != nullptr);
        ensure_equals("the button says what the page said", button->getToolTip(),
                      std::string("What this mode is for"));
        tabs->die();
    }

    // A page that wrote nothing is left as it was: hundreds of files have
    // tabs whose label is the whole story, and a tooltip repeating it is
    // noise that nobody asked for.
    template<> template<>
    void lltabcontainer_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("plain", std::string()));

        LLButton* button = tabButton(tabs, "plain");
        ensure("the page has a button", button != nullptr);
        ensure("a page that said nothing leaves its button silent", button->getToolTip().empty());
        tabs->die();
    }

    // A count on the tab, and off it again. A tab with nothing to say looks
    // like one that never had anything.
    template<> template<>
    void lltabcontainer_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* counted = page("counted", std::string());
        tabs->addTabPanel(counted);

        LLButton* button = tabButton(tabs, "counted");
        ensure("the page has a button", button != nullptr);
        ensure("a tab starts with nothing to say", !button->hasBadge());

        tabs->setTabBadge(counted, "12");
        ensure("a count puts a badge on the tab", button->hasBadge());

        tabs->setTabBadge(counted, std::string());
        ensure("the badge stays made once it is made", button->hasBadge());
        tabs->die();
    }

    // Asking for nothing on a tab that has nothing makes nothing: a badge
    // is not built in order to be hidden straight away.
    template<> template<>
    void lltabcontainer_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* quiet = page("quiet", std::string());
        tabs->addTabPanel(quiet);

        tabs->setTabBadge(quiet, std::string());
        LLButton* button = tabButton(tabs, "quiet");
        ensure("the page has a button", button != nullptr);
        ensure("nothing to say builds nothing", !button->hasBadge());
        tabs->die();
    }

    // The strip down the side names its buttons differently, and a tooltip
    // written for a page reaches the button either way.
    template<> template<>
    void lltabcontainer_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build(LLTabContainer::LEFT);
        tabs->addTabPanel(page("sideways", "Down the side"));

        LLButton* button = tabButton(tabs, "sideways", true);
        ensure("the page has a button", button != nullptr);
        ensure_equals("the button says what the page said", button->getToolTip(),
                      std::string("Down the side"));
        tabs->die();
    }
}
