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

        // A texture with a size and nothing else: what a UI image needs to
        // exist here, where no image provider makes any.
        struct SizedTexture : public LLTexture
        {
            S32 getWidth(S32) const override { return 16; }
            S32 getHeight(S32) const override { return 16; }
        };
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

    // Hiding a tab that is not the one being read leaves the reader where
    // they are. A container that hides a tab as the selection changes threw
    // them back to the first tab every time it did.
    template<> template<>
    void lltabcontainer_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        LLPanel* third = page("third", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->addTabPanel(third);

        tabs->selectTabPanel(third);
        ensure_equals("reading the third", tabs->getCurrentPanel(), third);

        tabs->setTabVisibility(second, false);
        ensure_equals("hiding another leaves it there", tabs->getCurrentPanel(), third);

        // The one being read is the one that went: somewhere else, then.
        tabs->setTabVisibility(third, false);
        ensure_equals("hiding this one moves to the first left", tabs->getCurrentPanel(), first);
        ensure("and the container is still worth showing", tabs->getVisible());

        tabs->setTabVisibility(first, false);
        ensure("a container with nothing to show does not", !tabs->getVisible());
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

    // The arrows at the right end of the strip move in by the difference
    // between one offset and the next, both of them, and an offset asked
    // for twice moves nothing the second time.
    template<> template<>
    void lltabcontainer_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLButton* next = tabs->findChild<LLButton>("Right Arrow", true);
        LLButton* jump = tabs->findChild<LLButton>("Jump Right Arrow", true);
        ensure("the strip has its arrows", next && jump);
        const S32 next_right = next->getRect().mRight;
        const S32 jump_right = jump->getRect().mRight;

        tabs->setRightTabBtnOffset(10);
        ensure_equals("the arrow moved in", next->getRect().mRight, next_right - 10);
        ensure_equals("and the jump beside it", jump->getRect().mRight, jump_right - 10);

        tabs->setRightTabBtnOffset(10);
        ensure_equals("the same offset again moves nothing", next->getRect().mRight, next_right - 10);

        tabs->setRightTabBtnOffset(0);
        ensure_equals("no offset puts it back", next->getRect().mRight, next_right);
        ensure_equals("both of them", jump->getRect().mRight, jump_right);
        tabs->die();
    }

    // Deleting every tab takes with it everything the strip knew about them:
    // the width they took, the ones that were locked in place. A lock left
    // over would put the next tab added at the front past the end of an
    // empty list.
    template<> template<>
    void lltabcontainer_object::test<8>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("a", std::string()));
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(page("c", std::string()));
        tabs->lockTabs();
        ensure_equals("three locked", tabs->getNumLockedTabs(), 3);
        ensure("three widths", tabs->getTotalTabWidth() > 0);

        tabs->deleteAllTabs();
        ensure_equals("none locked", tabs->getNumLockedTabs(), 0);
        ensure_equals("no width", tabs->getTotalTabWidth(), 0);
        ensure_equals("nothing read", tabs->getCurrentPanelIndex(), -1);

        LLPanel* front = page("front", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(front).insert_at(LLTabContainer::START));
        ensure_equals("a tab at the front of an empty strip", tabs->getPanelByIndex(0), front);
        tabs->die();
    }

    // Next and previous try each other tab once. With nothing else to
    // select they leave the strip as it is, without telling it again about
    // the tab it is on; with nothing selected and nothing selectable they
    // come back, rather than going round forever.
    template<> template<>
    void lltabcontainer_object::test<9>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("still on the first", tabs->getCurrentPanel(), first);
        ensure_equals("and not told about it again", commits, 0);
        tabs->selectPrevTab();
        ensure_equals("either way", commits, 0);

        tabs->deleteAllTabs();
        tabs->addTabPanel(page("third", std::string()));
        tabs->addTabPanel(page("fourth", std::string()));
        tabs->enableTabButton(0, false);
        tabs->enableTabButton(1, false);
        ensure_equals("nothing selected", tabs->getCurrentPanelIndex(), -1);
        tabs->selectNextTab();
        tabs->selectPrevTab();
        ensure_equals("and nothing selectable leaves it so", tabs->getCurrentPanelIndex(), -1);
        tabs->die();
    }

    // A tab retitled is measured in the strip's own font, the one its title
    // is drawn in, not in whichever font a tab is drawn in by default.
    template<> template<>
    void lltabcontainer_object::test<10>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the page has a button", button != nullptr);
        const S32 in_own_font = llclamp(LLFontGL::getFontSansSerifBig()->getWidth(title),
                                        tabs->getMinTabWidth(), tabs->getMaxTabWidth());
        ensure_equals("sized to the title in the strip's font", button->getRect().getWidth(), in_own_font);
        tabs->die();
    }

    // A click on a tab the container would not switch to -- one a validate
    // callback refused -- does not take the keyboard into a panel that is
    // not showing.
    template<> template<>
    void lltabcontainer_object::test<11>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);
        tabs->setValidateBeforeCommit([](const LLSD&) { return false; });

        tabs->onTabBtn(LLSD(), second);
        ensure_equals("refused, so still on the first", tabs->getCurrentPanel(), first);
        ensure("the keyboard did not go into the refused panel", !gFocusMgr.childHasKeyboardFocus(second));
        ensure("which is not showing", !second->getVisible());
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // A drag passing over the strip opens the tab under it, and a hidden
    // tab is not under it: its button is not shown for the occasion, nor
    // its panel opened.
    template<> template<>
    void lltabcontainer_object::test<12>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the hidden page has a button", hidden != nullptr);
        ensure("which is not shown", !hidden->getVisible());

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

        ensure("the hidden tab's button stays hidden", !hidden->getVisible());
        ensure_equals("and the drag did not open it", tabs->getCurrentPanel(), first);
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // A strip built with no height yet has its arrows all the same; their
    // follows flags carry them when it is given one. Without them the first
    // draw had nothing to show or hide.
    template<> template<>
    void lltabcontainer_object::test<13>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 0, 400, 0);
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        ensure("a strip with no height has its arrows", tabs->findChild<LLButton>("Left Arrow", true) != nullptr);
        ensure("all of them", tabs->findChild<LLButton>("Jump Right Arrow", true) != nullptr);
        tabs->die();
    }

    // The strip down the side puts its lower arrow at its own bottom, not
    // at where the strip sits in whatever holds it.
    template<> template<>
    void lltabcontainer_object::test<14>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer::Params p(LLUICtrlFactory::getDefaultParams<LLTabContainer>());
        p.name = "tabs";
        p.rect = LLRect(0, 350, 400, 50);
        p.tab_position = LLTabContainer::LEFT;
        LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(p);
        LLButton* down = tabs->findChild<LLButton>("Down Arrow", true);
        ensure("the strip has its lower arrow", down != nullptr);
        ensure_equals("at the strip's own bottom", down->getRect().mBottom, 0);
        tabs->die();
    }

    // A strip down the side with more tabs than fit scrolls to the one
    // selected, and says it selected it, as the strip along the top does.
    template<> template<>
    void lltabcontainer_object::test<15>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build(LLTabContainer::LEFT);
        for (S32 i = 0; i < 20; ++i)
        {
            tabs->addTabPanel(page("page" + std::to_string(i), std::string()));
        }
        ensure("the last tab is selected", tabs->selectTab(19));
        ensure_equals("and is the one being read", tabs->getCurrentPanelIndex(), 19);
        ensure("the first again", tabs->selectTab(0));
        ensure_equals("and read", tabs->getCurrentPanelIndex(), 0);
        tabs->die();
    }

    // The next arrow with nothing selected selects the first tab, rather
    // than comparing nothing to the count as an unsigned and doing nothing.
    template<> template<>
    void lltabcontainer_object::test<16>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("first", std::string()));
        tabs->addTabPanel(page("second", std::string()));
        ensure_equals("nothing selected", tabs->getCurrentPanelIndex(), -1);
        tabs->onNextBtn(LLSD());
        ensure_equals("next from nothing is the first", tabs->getCurrentPanelIndex(), 0);
        tabs->die();
    }

    // A tab's flash image comes with the rest of its position's images,
    // however it was put in the strip. A tab put in the middle never
    // passed through the step that gave the first and last tabs theirs.
    template<> template<>
    void lltabcontainer_object::test<17>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("put in the middle", tabs->getIndexForPanel(between), 1);

        LLButton* button = tabButton(tabs, "between");
        ensure("the page has a button", button != nullptr);
        ensure_equals("with the middle tab's flash image", button->getImageFlash().get(), flash.get());
        tabs->die();
    }

    // A tab added and selected in the same breath is the one selected,
    // wherever in the strip it was put; it used to be whichever was last.
    template<> template<>
    void lltabcontainer_object::test<18>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* a = page("a", std::string());
        tabs->addTabPanel(a);
        tabs->addTabPanel(page("b", std::string()));
        tabs->addTabPanel(page("c", std::string()));
        tabs->selectTabPanel(a);

        LLPanel* added = page("added", std::string());
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(added).select_tab(true).insert_at(LLTabContainer::RIGHT_OF_CURRENT));
        ensure_equals("put after the first", tabs->getIndexForPanel(added), 1);
        ensure_equals("and the one selected", tabs->getCurrentPanel(), added);
        tabs->die();
    }

    // Removing a tab before the one being read leaves that one being read.
    // It used to move the reading to the tab after it, which is what the
    // old index came to point at once the list closed up.
    template<> template<>
    void lltabcontainer_object::test<19>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("still reading the third", tabs->getCurrentPanel(), c);
        ensure_equals("at its new place", tabs->getCurrentPanelIndex(), 1);
        ensure_equals("with nothing to announce", commits, 0);
        ensure("and it is the one showing", c->getVisible() && !d->getVisible());

        // The one being read is the one that goes: what took its place.
        tabs->removeTabPanel(c);
        c->die();
        ensure_equals("reading what took its place", tabs->getCurrentPanel(), d);
        ensure_equals("announced", commits, 1);
        tabs->die();
    }

    // What a strip is set to, as a value, is what it reads back as one.
    template<> template<>
    void lltabcontainer_object::test<20>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        tabs->addTabPanel(page("first", std::string()));
        tabs->addTabPanel(page("second", std::string()));
        tabs->setValue(1);
        ensure_equals("the value set is the value read", tabs->getValue().asInteger(), 1);
        tabs->die();
    }

    // A picture on a tab sits inside the button that selects it, in the
    // button's own coordinates. It used to be placed by the button's
    // position in the strip, which put it as far above the button as the
    // button was above the strip's bottom.
    template<> template<>
    void lltabcontainer_object::test<21>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the page has a button", button != nullptr);
        ensure_equals("the picture is the button's", icon->getParent(), button);
        const LLRect& in_button = icon->getRect();
        const LLRect box = button->getLocalRect();
        ensure("and sits inside it", in_button.mLeft >= box.mLeft && in_button.mRight <= box.mRight
                                     && in_button.mBottom >= box.mBottom && in_button.mTop <= box.mTop);
        ensure_equals("padded off the top", in_button.mTop, box.mTop - 2);
        ensure_equals("and the left", in_button.mLeft, 2);
        ensure_equals("as tall as the room leaves it", in_button.getHeight(), box.getHeight() - 4);
        tabs->die();
    }

    // Where the strip is scrolled to, and how far it can go, count the
    // tabs that show. A hidden tab is not a place to scroll to and takes
    // no room; it used to be counted into how many fit from the right and
    // into where a selected tab sits, so the strip scrolled to the wrong
    // place whenever a hidden tab lay before the one selected.
    template<> template<>
    void lltabcontainer_object::test<22>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the wide tab is as wide as a tab may be",
                      tabButton(tabs, "f")->getRect().getWidth(), tabs->getMaxTabWidth());

        tabs->setTabVisibility(wide, false);
        ensure_equals("seven tabs of sixty in four hundred: one more than fit", tabs->getMaxScrollPos(), 1);

        tabs->selectTabPanel(last);
        ensure_equals("scrolled one tab to show the last", tabs->getScrollPos(), 1);
        tabs->die();
    }

    // Arrow keys on a tab button switch tabs on their own. With a modifier
    // held they are somebody else's key, and the strip says so.
    template<> template<>
    void lltabcontainer_object::test<23>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);
        tabButton(tabs, "first")->setFocus(true);

        ensure("control-right is not the strip's", !tabs->handleKeyHere(KEY_RIGHT, MASK_CONTROL));
        ensure_equals("and moved nothing", tabs->getCurrentPanel(), first);
        ensure("right on its own is", tabs->handleKeyHere(KEY_RIGHT, MASK_NONE));
        ensure_equals("and moved to the next", tabs->getCurrentPanel(), second);
        gFocusMgr.setKeyboardFocus(nullptr);
        tabs->die();
    }

    // An indent asked for on a tab is added to the padding its label has
    // along the top; it used to be written and then written over.
    template<> template<>
    void lltabcontainer_object::test<24>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* plain = page("plain", std::string());
        LLPanel* indented = page("indented", std::string());
        tabs->addTabPanel(plain);
        tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(indented).indent(10));
        const S32 pad = tabButton(tabs, "plain")->getLeftHPad();
        ensure_equals("the indent is added to the label's padding",
                      tabButton(tabs, "indented")->getLeftHPad(), pad + 10);
        tabs->die();
    }

    // The container hides when its last tab is hidden and shows when one
    // comes back, and between those its visibility is its owner's: hiding
    // one tab among others does not show a container its owner has hidden.
    template<> template<>
    void lltabcontainer_object::test<25>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLTabContainer* tabs = build();
        LLPanel* first = page("first", std::string());
        LLPanel* second = page("second", std::string());
        tabs->addTabPanel(first);
        tabs->addTabPanel(second);
        tabs->selectTabPanel(first);

        tabs->setVisible(false);
        tabs->setTabVisibility(second, false);
        ensure("hiding a tab among others leaves the container as its owner left it", !tabs->getVisible());
        tabs->setTabVisibility(second, true);
        ensure("and so does showing one while another showed", !tabs->getVisible());

        tabs->setVisible(true);
        tabs->setTabVisibility(first, false);
        tabs->setTabVisibility(second, false);
        ensure("nothing left to show hides it", !tabs->getVisible());
        tabs->setTabVisibility(first, true);
        ensure("something to show again shows it", tabs->getVisible());
        tabs->die();
    }

    // The strip sits flush with the pages: its outer arrows stand where a
    // page's sides are, so nothing is a pixel or two short of the content
    // under it, whichever way the pages are inset.
    template<> template<>
    void lltabcontainer_object::test<26>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            ensure("the strip has its jump arrows", jump_left && jump_right);
            ensure_equals("the strip starts where the page does", jump_left->getRect().mLeft, only->getRect().mLeft);
            ensure_equals("and ends where it does", jump_right->getRect().mRight, only->getRect().mRight);
            tabs->die();
        }
    }

    // A strip asked to fill its width shares what is left over among the
    // tabs that show, so it runs the whole way across its pages; a strip
    // whose tabs do not fit leaves each its own width and scrolls.
    template<> template<>
    void lltabcontainer_object::test<27>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            ensure("each tab is wider than it wants", width > 60);
            total += width;
        }
        ensure_equals("and together they are the strip", total, room);
        ensure_equals("the odd pixels go to the first", tabButton(tabs, "a")->getRect().getWidth(),
                      tabButton(tabs, "d")->getRect().getWidth() + 1);

        // A hidden tab takes no share.
        tabs->setTabVisibility(a, false);
        total = 0;
        for (const char* name : { "b", "c", "d" })
        {
            total += tabButton(tabs, name)->getRect().getWidth();
        }
        ensure_equals("three share it", total, room);
        ensure_equals("the hidden one is its own width", tabButton(tabs, "a")->getRect().getWidth(), 60);
        tabs->setTabVisibility(a, true);

        // More than fit: their own widths, and a scroll.
        for (const char* name : { "e", "f", "g" })
        {
            tabs->addTabPanel(page(name, std::string()));
        }
        ensure_equals("seven of sixty do not fit, so each is sixty", tabButton(tabs, "a")->getRect().getWidth(), 60);
        ensure("and the strip scrolls", tabs->getMaxScrollPos() > 0);
        tabs->die();
    }

    // A strip can keep its right end for buttons of its own. fill_width
    // shares out only what tab_padding_right leaves; a child that is not a
    // panel stays a child rather than becoming a tab; and a button in the
    // kept room gets its own clicks, because the tabs' mouse capture stops
    // where that room begins. The Lightbox keeps its pop-out button there.
    template<> template<>
    void lltabcontainer_object::test<28>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        ensure_equals("the button is not a tab", tabs->getTabCount(), 4);
        ensure("it is the container's own child", button->getParent() == tabs);

        // The 398 between the pages' sides, less the 80 kept.
        S32 total = 0;
        for (const char* name : { "a", "b", "c", "d" })
        {
            total += tabButton(tabs, name)->getRect().getWidth();
        }
        ensure_equals("the tabs fill what is left", total, 398 - KEPT);
        ensure("and stop short of the button", 1 + total <= button->getRect().mLeft);

        // Pressed and released on it: its own click, and the strip does not
        // take the mouse from it in between.
        const S32 x = button->getRect().getCenterX();
        const S32 y = button->getRect().getCenterY();
        tabs->handleMouseDown(x, y, MASK_NONE);
        ensure("the button holds the mouse", gFocusMgr.getMouseCapture() == button);
        button->handleMouseUp(x - button->getRect().mLeft, y - button->getRect().mBottom, MASK_NONE);
        ensure_equals("and is clicked", clicks, 1);

        // Over the tabs, the strip takes the mouse, and would take it from a
        // button put there.
        tabs->handleMouseDown(10, y, MASK_NONE);
        ensure("the strip holds the mouse over the tabs", gFocusMgr.getMouseCapture() == tabs);
        gFocusMgr.setMouseCapture(nullptr);
        tabs->die();
    }
}
