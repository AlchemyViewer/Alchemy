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
#include "../llfocusmgr.h"
#include "../lliconctrl.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "llframetimer.h"
#include "lltexture.h"
#include "lltimer.h"
#include "lluiimage.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct lltabcontainer_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

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

        // A texture with a size and nothing else: what a UI image needs to
        // exist here, where no image provider makes any.
        struct SizedTexture : public LLTexture
        {
            S32 getWidth(S32) const override { return 16; }
            S32 getHeight(S32) const override { return 16; }
        };
    };

    // What the file wrote about a page is what the button that selects it
    // says. A tab with an icon and no label has nothing else to say it.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a page's tooltip is what its tab button says", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* described = page("described", "What this mode is for");
        tabs->addTabPanel(described);

        LLButton* button = tabButton(tabs, "described");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("the button says what the page said");
        CHECK(button->getToolTip() ==
                      std::string("What this mode is for"));
        tabs->die();
    }

    // A page that wrote nothing is left as it was: hundreds of files have
    // tabs whose label is the whole story, and a tooltip repeating it is
    // noise that nobody asked for.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a page with no tooltip leaves its tab button without one", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("plain", std::string()));

        LLButton* button = tabButton(tabs, "plain");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("a page that said nothing leaves its button silent");
        REQUIRE(button->getToolTip().empty());
        tabs->die();
    }

    // A count on the tab, and off it again. A tab with nothing to say looks
    // like one that never had anything.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: setTabBadge makes a badge and keeps it once the count is cleared", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* counted = page("counted", std::string());
        tabs->addTabPanel(counted);

        LLButton* button = tabButton(tabs, "counted");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("a tab starts with nothing to say");
        CHECK_FALSE(button->hasBadge());

        tabs->setTabBadge(counted, "12");
        UNSCOPED_INFO("a count puts a badge on the tab");
        CHECK(button->hasBadge());

        tabs->setTabBadge(counted, std::string());
        UNSCOPED_INFO("the badge stays made once it is made");
        CHECK(button->hasBadge());
        tabs->die();
    }

    // Asking for nothing on a tab that has nothing makes nothing: a badge
    // is not built in order to be hidden straight away.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: clearing the badge of a tab that has none makes no badge", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* quiet = page("quiet", std::string());
        tabs->addTabPanel(quiet);

        tabs->setTabBadge(quiet, std::string());
        LLButton* button = tabButton(tabs, "quiet");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("nothing to say builds nothing");
        CHECK_FALSE(button->hasBadge());
        tabs->die();
    }

    // The strip down the side names its buttons differently, and a tooltip
    // written for a page reaches the button either way.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a side strip's tab button carries the page's tooltip too", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build(LLTabContainer::LEFT);
        tabs->addTabPanel(page("sideways", "Down the side"));

        LLButton* button = tabButton(tabs, "sideways", true);
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("the button says what the page said");
        CHECK(button->getToolTip() ==
                      std::string("Down the side"));
        tabs->die();
    }

    // Hiding a tab that is not the one being read leaves the reader where
    // they are. A container that hides a tab as the selection changes threw
    // them back to the first tab every time it did.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: hiding a tab other than the current one keeps the selection", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        LLPanel* third = page("third", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->addTabPanel(third);

        tabs->selectTabPanel(third);
        UNSCOPED_INFO("reading the third");
        REQUIRE(tabs->getCurrentPanel() == third);

        tabs->setTabVisibility(second, false);
        UNSCOPED_INFO("hiding another leaves it there");
        REQUIRE(tabs->getCurrentPanel() == third);

        // The one being read is the one that went: somewhere else, then.
        tabs->setTabVisibility(third, false);
        UNSCOPED_INFO("hiding this one moves to the first left");
        REQUIRE(tabs->getCurrentPanel() == first);
        UNSCOPED_INFO("and the container is still worth showing");
        CHECK(tabs->getVisible());

        tabs->setTabVisibility(first, false);
        UNSCOPED_INFO("a container with nothing to show does not");
        CHECK_FALSE(tabs->getVisible());
        tabs->die();
    }

    // The arrows at the right end of the strip move in by the difference
    // between one offset and the next, both of them, and an offset asked
    // for twice moves nothing the second time.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: the right arrows move by the change in setRightTabBtnOffset", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLButton* next = tabs->findChild<LLButton>("Right Arrow", true);
        LLButton* jump = tabs->findChild<LLButton>("Jump Right Arrow", true);
        {
            INFO("the strip has its arrows");
            REQUIRE(next);
            REQUIRE(jump);
        }
        const S32 next_right = next->getRect().mRight;
        const S32 jump_right = jump->getRect().mRight;

        tabs->setRightTabBtnOffset(10);
        UNSCOPED_INFO("the arrow moved in");
        CHECK(next->getRect().mRight == next_right - 10);
        UNSCOPED_INFO("and the jump beside it");
        CHECK(jump->getRect().mRight == jump_right - 10);

        tabs->setRightTabBtnOffset(10);
        UNSCOPED_INFO("the same offset again moves nothing");
        CHECK(next->getRect().mRight == next_right - 10);

        tabs->setRightTabBtnOffset(0);
        UNSCOPED_INFO("no offset puts it back");
        CHECK(next->getRect().mRight == next_right);
        UNSCOPED_INFO("both of them");
        CHECK(jump->getRect().mRight == jump_right);
        tabs->die();
    }

    // Deleting every tab takes with it everything the strip knew about them:
    // the width they took, the ones that were locked in place. A lock left
    // over would put the next tab added at the front past the end of an
    // empty list.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: deleteAllTabs forgets the tabs' locks and widths", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("a", std::string()));
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(page("c", std::string()));
        tabs->lockTabs();
        UNSCOPED_INFO("three locked");
        CHECK(tabs->getNumLockedTabs() == 3);
        UNSCOPED_INFO("three widths");
        CHECK(tabs->getTotalTabWidth() > 0);

        tabs->deleteAllTabs();
        UNSCOPED_INFO("none locked");
        CHECK(tabs->getNumLockedTabs() == 0);
        UNSCOPED_INFO("no width");
        CHECK(tabs->getTotalTabWidth() == 0);
        UNSCOPED_INFO("nothing read");
        CHECK(tabs->getCurrentPanelIndex() == -1);

        LLPanel* front = page("front", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(front).insert_at(LLTabContainer::START));
        UNSCOPED_INFO("a tab at the front of an empty strip");
        REQUIRE(tabs->getPanelByIndex(0) == front);
        tabs->die();
    }

    // Next and previous try each other tab once. With nothing else to
    // select they leave the strip as it is, without telling it again about
    // the tab it is on; with nothing selected and nothing selectable they
    // come back, rather than going round forever.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: next and previous with nothing else selectable change nothing", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->enableTabButton(1, false);
        tabs->selectTabPanel(first);

        S32 commits = 0;
        tabs->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        tabs->selectNextTab();
        UNSCOPED_INFO("still on the first");
        REQUIRE(tabs->getCurrentPanel() == first);
        UNSCOPED_INFO("and not told about it again");
        CHECK(commits == 0);
        tabs->selectPrevTab();
        UNSCOPED_INFO("either way");
        CHECK(commits == 0);

        tabs->deleteAllTabs();
        tabs->addTabPanel(page("third", std::string()));
        tabs->addTabPanel(page("fourth", std::string()));
        tabs->enableTabButton(0, false);
        tabs->enableTabButton(1, false);
        UNSCOPED_INFO("nothing selected");
        CHECK(tabs->getCurrentPanelIndex() == -1);
        tabs->selectNextTab();
        tabs->selectPrevTab();
        UNSCOPED_INFO("and nothing selectable leaves it so");
        CHECK(tabs->getCurrentPanelIndex() == -1);
        tabs->die();
    }

    // A tab retitled is measured in the strip's own font, the one its title
    // is drawn in, not in whichever font a tab is drawn in by default.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a retitled tab is measured in the strip's own font", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.font = LLFontGL::getFontSansSerifBig();
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        tabs->addTabPanel(page("titled", std::string()));

        const std::string title("A title to measure");
        tabs->setPanelTitle(0, title);
        LLButton* button = tabButton(tabs, "titled");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        const S32 in_own_font = llclamp(LLFontGL::getFontSansSerifBig()->getWidth(title),
                                        tabs->getMinTabWidth(), tabs->getMaxTabWidth());
        UNSCOPED_INFO("sized to the title in the strip's font");
        CHECK(button->getRect().getWidth() == in_own_font);
        tabs->die();
    }

    // A click on a tab the container would not switch to -- one a validate
    // callback refused -- does not take the keyboard into a panel that is
    // not showing.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a click on a refused tab does not focus its hidden panel", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);
        tabs->setValidateBeforeCommit([](const LLSD&) { return false; });

        tabs->onTabBtn(LLSD(), second);
        UNSCOPED_INFO("refused, so still on the first");
        REQUIRE(tabs->getCurrentPanel() == first);
        UNSCOPED_INFO("the keyboard did not go into the refused panel");
        CHECK_FALSE(gFocusMgr.childHasKeyboardFocus(second));
        UNSCOPED_INFO("which is not showing");
        CHECK_FALSE(second->getVisible());
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // A drag passing over the strip opens the tab under it, and a hidden
    // tab is not under it: its button is not shown for the occasion, nor
    // its panel opened.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a drag over a hidden tab neither shows nor opens it", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.open_tabs_on_drag_and_drop = true;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);
        tabs->setTabVisibility(second, false);
        LLButton* hidden = tabButton(tabs, "second");
        UNSCOPED_INFO("the hidden page has a button");
        REQUIRE(hidden != nullptr);
        UNSCOPED_INFO("which is not shown");
        CHECK_FALSE(hidden->getVisible());

        // Before the strip is drawn its buttons all sit at the left, so a
        // point in the strip's band is over the hidden one.
        const S32 x = hidden->getRect().mLeft + 3;
        const S32 y = hidden->getRect().mBottom + 3;
        EAcceptance accept = ACCEPT_NO;
        std::string tip;
        LLFrameTimer::updateFrameTime();
        tabs->handleDragAndDrop(x, y, 0, false, DAD_NONE, nullptr, &accept, tip);
        ms_sleep(600);
        LLFrameTimer::updateFrameTime();
        tabs->handleDragAndDrop(x, y, 0, false, DAD_NONE, nullptr, &accept, tip);

        UNSCOPED_INFO("the hidden tab's button stays hidden");
        CHECK_FALSE(hidden->getVisible());
        UNSCOPED_INFO("and the drag did not open it");
        REQUIRE(tabs->getCurrentPanel() == first);
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // A strip built with no height yet has its arrows all the same; their
    // follows flags carry them when it is given one. Without them the first
    // draw had nothing to show or hide.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a strip built with no height still has its arrows", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 0, 400, 0);
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        UNSCOPED_INFO("a strip with no height has its arrows");
        REQUIRE(tabs->findChild<LLButton>("Left Arrow", true) != nullptr);
        UNSCOPED_INFO("all of them");
        REQUIRE(tabs->findChild<LLButton>("Jump Right Arrow", true) != nullptr);
        tabs->die();
    }

    // The strip down the side puts its lower arrow at its own bottom, not
    // at where the strip sits in whatever holds it.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a side strip puts its lower arrow at its own bottom", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 350, 400, 50);
        p.tab_position = LLTabContainer::LEFT;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLButton* down = tabs->findChild<LLButton>("Down Arrow", true);
        UNSCOPED_INFO("the strip has its lower arrow");
        REQUIRE(down != nullptr);
        UNSCOPED_INFO("at the strip's own bottom");
        CHECK(down->getRect().mBottom == 0);
        tabs->die();
    }

    // A strip down the side with more tabs than fit scrolls to the one
    // selected, and says it selected it, as the strip along the top does.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a side strip with more tabs than fit selects any of them", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build(LLTabContainer::LEFT);
        for (S32 i = 0; i < 20; ++i)
        {
            tabs->addTabPanel(page("page" + std::to_string(i), std::string()));
        }
        UNSCOPED_INFO("the last tab is selected");
        CHECK(tabs->selectTab(19));
        UNSCOPED_INFO("and is the one being read");
        CHECK(tabs->getCurrentPanelIndex() == 19);
        UNSCOPED_INFO("the first again");
        CHECK(tabs->selectTab(0));
        UNSCOPED_INFO("and read");
        CHECK(tabs->getCurrentPanelIndex() == 0);
        tabs->die();
    }

    // The next arrow with nothing selected selects the first tab, rather
    // than comparing nothing to the count as an unsigned and doing nothing.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: the next arrow with nothing selected selects the first tab", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("first", std::string()));
        tabs->addTabPanel(page("second", std::string()));
        UNSCOPED_INFO("nothing selected");
        CHECK(tabs->getCurrentPanelIndex() == -1);
        tabs->onNextBtn(LLSD());
        UNSCOPED_INFO("next from nothing is the first");
        CHECK(tabs->getCurrentPanelIndex() == 0);
        tabs->die();
    }

    // A tab's flash image comes with the rest of its position's images,
    // however it was put in the strip. A tab put in the middle never
    // passed through the step that gave the first and last tabs theirs.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a tab put in the middle gets the middle tab's flash image", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLPointer<LLUIImage> flash = new LLUIImage(std::string("flash"), LLPointer<LLTexture>(new SizedTexture()));
        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.middle_tab.tab_top_image_flash(flash.get());
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLPanel* a = page("a", std::string());
        tabs->addTabPanel(a);
        tabs->addTabPanel(page("b", std::string()));
        tabs->selectTabPanel(a);
        LLPanel* between = page("between", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(between).insert_at(LLTabContainer::RIGHT_OF_CURRENT));
        UNSCOPED_INFO("put in the middle");
        CHECK(tabs->getIndexForPanel(between) == 1);

        LLButton* button = tabButton(tabs, "between");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("with the middle tab's flash image");
        REQUIRE(button->getImageFlash().get() == flash.get());
        tabs->die();
    }

    // A tab added and selected in the same breath is the one selected,
    // wherever in the strip it was put; it used to be whichever was last.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a tab added and selected at once is selected wherever it is put", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* a = page("a", std::string());
        tabs->addTabPanel(a);
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(page("c", std::string()));
        tabs->selectTabPanel(a);

        LLPanel* added = page("added", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(added).select_tab(true).insert_at(LLTabContainer::RIGHT_OF_CURRENT));
        UNSCOPED_INFO("put after the first");
        CHECK(tabs->getIndexForPanel(added) == 1);
        UNSCOPED_INFO("and the one selected");
        REQUIRE(tabs->getCurrentPanel() == added);
        tabs->die();
    }

    // Removing a tab before the one being read leaves that one being read.
    // It used to move the reading to the tab after it, which is what the
    // old index came to point at once the list closed up.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: removing a tab before the current one keeps the current one", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* a = page("a", std::string());
        LLPanel* c = page("c", std::string());
        LLPanel* d = page("d", std::string());
        tabs->addTabPanel(a);
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(c);
        tabs->addTabPanel(d);
        tabs->selectTabPanel(c);

        S32 commits = 0;
        tabs->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        tabs->removeTabPanel(a);
        a->die();
        UNSCOPED_INFO("still reading the third");
        REQUIRE(tabs->getCurrentPanel() == c);
        UNSCOPED_INFO("at its new place");
        CHECK(tabs->getCurrentPanelIndex() == 1);
        UNSCOPED_INFO("with nothing to announce");
        CHECK(commits == 0);
        {
            INFO("and it is the one showing");
            CHECK(c->getVisible());
            CHECK_FALSE(d->getVisible());
        }

        // The one being read is the one that goes: what took its place.
        tabs->removeTabPanel(c);
        c->die();
        UNSCOPED_INFO("reading what took its place");
        REQUIRE(tabs->getCurrentPanel() == d);
        UNSCOPED_INFO("announced");
        CHECK(commits == 1);
        tabs->die();
    }

    // What a strip is set to, as a value, is what it reads back as one.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a strip's value reads back as it was set", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("first", std::string()));
        tabs->addTabPanel(page("second", std::string()));
        tabs->setValue(1);
        UNSCOPED_INFO("the value set is the value read");
        CHECK(tabs->getValue().asInteger() == 1);
        tabs->die();
    }

    // A picture on a tab sits inside the button that selects it, in the
    // button's own coordinates. It used to be placed by the button's
    // position in the strip, which put it as far above the button as the
    // button was above the strip's bottom.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a tab's picture sits inside its button in the button's coordinates", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.use_custom_icon_ctrl = true;
        p.tab_icon_ctrl_pad = 2;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLPanel* pictured = page("pictured", std::string());
        tabs->addTabPanel(pictured);

        LLIconCtrl::Params ip;
        ip.name = "picture";
        LLIconCtrl* icon = LLUICtrlFactory::create<LLIconCtrl>(ip);
        tabs->setTabImage(pictured, icon);

        LLButton* button = tabButton(tabs, "pictured");
        UNSCOPED_INFO("the page has a button");
        REQUIRE(button != nullptr);
        UNSCOPED_INFO("the picture is the button's");
        REQUIRE(icon->getParent() == button);
        const LLRect& in_button = icon->getRect();
        const LLRect box = button->getLocalRect();
        {
            INFO("and sits inside it");
            CHECK(in_button.mLeft >= box.mLeft);
            CHECK(in_button.mRight <= box.mRight);
            CHECK(in_button.mBottom >= box.mBottom);
            CHECK(in_button.mTop <= box.mTop);
        }
        UNSCOPED_INFO("padded off the top");
        CHECK(in_button.mTop == box.mTop - 2);
        UNSCOPED_INFO("and the left");
        CHECK(in_button.mLeft == 2);
        UNSCOPED_INFO("as tall as the room leaves it");
        CHECK(in_button.getHeight() == box.getHeight() - 4);
        tabs->die();
    }

    // Where the strip is scrolled to, and how far it can go, count the
    // tabs that show. A hidden tab is not a place to scroll to and takes
    // no room; it used to be counted into how many fit from the right and
    // into where a selected tab sits, so the strip scrolled to the wrong
    // place whenever a hidden tab lay before the one selected.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: scrolling counts only the tabs that show", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        for (const char* name : { "a", "b", "c", "d", "e" })
        {
            tabs->addTabPanel(page(name, std::string()));
        }
        LLPanel* wide = page("f", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(wide)
                          .label("A label long enough to be widened to the strip's maximum"));
        tabs->addTabPanel(page("g", std::string()));
        LLPanel* last = page("h", std::string());
        tabs->addTabPanel(last);
        UNSCOPED_INFO("the wide tab is as wide as a tab may be");
        CHECK(tabButton(tabs, "f")->getRect().getWidth() == tabs->getMaxTabWidth());

        tabs->setTabVisibility(wide, false);
        UNSCOPED_INFO("seven tabs of sixty in four hundred: one more than fit");
        CHECK(tabs->getMaxScrollPos() == 1);

        tabs->selectTabPanel(last);
        UNSCOPED_INFO("scrolled one tab to show the last");
        CHECK(tabs->getScrollPos() == 1);
        tabs->die();
    }

    // Arrow keys on a tab button switch tabs on their own. With a modifier
    // held they are somebody else's key, and the strip says so.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: arrow keys switch tabs only with no modifier held", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);
        tabButton(tabs, "first")->setFocus(true);

        UNSCOPED_INFO("control-right is not the strip's");
        CHECK_FALSE(tabs->handleKeyHere(KEY_RIGHT, MASK_CONTROL));
        UNSCOPED_INFO("and moved nothing");
        REQUIRE(tabs->getCurrentPanel() == first);
        UNSCOPED_INFO("right on its own is");
        CHECK(tabs->handleKeyHere(KEY_RIGHT, MASK_NONE));
        UNSCOPED_INFO("and moved to the next");
        REQUIRE(tabs->getCurrentPanel() == second);
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // An indent asked for on a tab is added to the padding its label has
    // along the top; it used to be written and then written over.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a tab's indent is added to its label's padding", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* plain = page("plain", std::string());
        LLPanel* indented = page("indented", std::string());
        tabs->addTabPanel(plain);
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(indented).indent(10));
        const S32 pad = tabButton(tabs, "plain")->getLeftHPad();
        UNSCOPED_INFO("the indent is added to the label's padding");
        CHECK(tabButton(tabs, "indented")->getLeftHPad() == pad + 10);
        tabs->die();
    }

    // The container hides when its last tab is hidden and shows when one
    // comes back, and between those its visibility is its owner's: hiding
    // one tab among others does not show a container its owner has hidden.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: hiding every tab hides the container and showing one shows it", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);

        tabs->setVisible(false);
        tabs->setTabVisibility(second, false);
        UNSCOPED_INFO("hiding a tab among others leaves the container as its owner left it");
        CHECK_FALSE(tabs->getVisible());
        tabs->setTabVisibility(second, true);
        UNSCOPED_INFO("and so does showing one while another showed");
        CHECK_FALSE(tabs->getVisible());

        tabs->setVisible(true);
        tabs->setTabVisibility(first, false);
        tabs->setTabVisibility(second, false);
        UNSCOPED_INFO("nothing left to show hides it");
        CHECK_FALSE(tabs->getVisible());
        tabs->setTabVisibility(first, true);
        UNSCOPED_INFO("something to show again shows it");
        CHECK(tabs->getVisible());
        tabs->die();
    }

    // The strip sits flush with the pages: its outer arrows stand where a
    // page's sides are, so nothing is a pixel or two short of the content
    // under it, whichever way the pages are inset.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: the strip's outer arrows stand at the page's sides", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        for (bool offset : { false, true })
        {
            LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
            p.name = "tabs";
            p.rect = LLRect(0, 300, 400, 0);
            p.use_tab_offset = offset;
            LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
            LLPanel* only = page("only", std::string());
            tabs->addTabPanel(only);
            LLButton* jump_left = tabs->findChild<LLButton>("Jump Left Arrow", true);
            LLButton* jump_right = tabs->findChild<LLButton>("Jump Right Arrow", true);
            {
                INFO("the strip has its jump arrows");
                REQUIRE(jump_left);
                REQUIRE(jump_right);
            }
            UNSCOPED_INFO("the strip starts where the page does");
            REQUIRE(jump_left->getRect().mLeft == only->getRect().mLeft);
            UNSCOPED_INFO("and ends where it does");
            REQUIRE(jump_right->getRect().mRight == only->getRect().mRight);
            tabs->die();
        }
    }

    // A strip asked to fill its width shares what is left over among the
    // tabs that show, so it runs the whole way across its pages; a strip
    // whose tabs do not fit leaves each its own width and scrolls.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: fill_width shares the room among the tabs that show", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.fill_width = true;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLPanel* a = page("a", std::string());
        tabs->addTabPanel(a);
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(page("c", std::string()));
        tabs->addTabPanel(page("d", std::string()));

        // Four tabs of sixty in the three hundred and ninety-eight between
        // the page's sides: each gets its sixty and a fourth of the rest.
        const S32 room = 398;
        S32 total = 0;
        for (const char* name : { "a", "b", "c", "d" })
        {
            const S32 width = tabButton(tabs, name)->getRect().getWidth();
            UNSCOPED_INFO("each tab is wider than it wants");
            REQUIRE(width > 60);
            total += width;
        }
        UNSCOPED_INFO("and together they are the strip");
        CHECK(total == room);
        UNSCOPED_INFO("the odd pixels go to the first");
        CHECK(tabButton(tabs, "a")->getRect().getWidth() ==
                      tabButton(tabs, "d")->getRect().getWidth() + 1);

        // A hidden tab takes no share.
        tabs->setTabVisibility(a, false);
        total = 0;
        for (const char* name : { "b", "c", "d" })
        {
            total += tabButton(tabs, name)->getRect().getWidth();
        }
        UNSCOPED_INFO("three share it");
        CHECK(total == room);
        UNSCOPED_INFO("the hidden one is its own width");
        CHECK(tabButton(tabs, "a")->getRect().getWidth() == 60);
        tabs->setTabVisibility(a, true);

        // More than fit: their own widths, and a scroll.
        for (const char* name : { "e", "f", "g" })
        {
            tabs->addTabPanel(page(name, std::string()));
        }
        UNSCOPED_INFO("seven of sixty do not fit, so each is sixty");
        CHECK(tabButton(tabs, "a")->getRect().getWidth() == 60);
        UNSCOPED_INFO("and the strip scrolls");
        CHECK(tabs->getMaxScrollPos() > 0);
        tabs->die();
    }

    // A strip can keep its right end for buttons of its own. fill_width
    // shares out only what tab_padding_right leaves; a child that is not a
    // panel stays a child rather than becoming a tab; and a button in the
    // kept room gets its own clicks, because the tabs' mouse capture stops
    // where that room begins. The Lightbox keeps its pop-out button there.
    TEST_CASE_METHOD(lltabcontainer_data, "lltabcontainer: a strip can keep its right end for buttons of its own", "[lltabcontainer]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        constexpr S32 KEPT = 80;
        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 300, 400, 0);
        p.fill_width = true;
        p.tab_padding_right = KEPT;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        // The way the XUI factory adds them, pages and button alike.
        for (const char* name : { "a", "b", "c", "d" })
        {
            tabs->addChild(page(name, std::string()));
        }
        LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
        bp.name = "strip_button";
        bp.rect = LLRect(400 - 26, 298, 400 - 6, 278);
        bp.follows.flags = FOLLOWS_TOP | FOLLOWS_RIGHT;
        LLButton* button = LLUICtrlFactory::create<LLButton>(bp);
        S32 clicks = 0;
        button->setCommitCallback([&clicks](LLUICtrl*, const LLSD&) { ++clicks; });
        tabs->addChild(button);

        UNSCOPED_INFO("the button is not a tab");
        CHECK(tabs->getTabCount() == 4);
        UNSCOPED_INFO("it is the container's own child");
        CHECK(button->getParent() == tabs);

        // The 398 between the pages' sides, less the 80 kept.
        S32 total = 0;
        for (const char* name : { "a", "b", "c", "d" })
        {
            total += tabButton(tabs, name)->getRect().getWidth();
        }
        UNSCOPED_INFO("the tabs fill what is left");
        CHECK(total == 398 - KEPT);
        UNSCOPED_INFO("and stop short of the button");
        CHECK(1 + total <= button->getRect().mLeft);

        // Pressed and released on it: its own click, and the strip does not
        // take the mouse from it in between.
        const S32 x = button->getRect().getCenterX();
        const S32 y = button->getRect().getCenterY();
        tabs->handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("the button holds the mouse");
        CHECK(gFocusMgr.getMouseCapture() == button);
        button->handleMouseUp(x - button->getRect().mLeft, y - button->getRect().mBottom, MASK_NONE);
        UNSCOPED_INFO("and is clicked");
        CHECK(clicks == 1);

        // Over the tabs, the strip takes the mouse, and would take it from a
        // button put there.
        tabs->handleMouseDown(10, y, MASK_NONE);
        UNSCOPED_INFO("the strip holds the mouse over the tabs");
        CHECK(gFocusMgr.getMouseCapture() == tabs);
        gFocusMgr.setMouseCapture(nullptr);
        tabs->die();
    }
}
