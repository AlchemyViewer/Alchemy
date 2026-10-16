/**
 * @file llaccordionctrltab_test.cpp
 * @brief Whether a tab starts open is one parameter with two spellings.
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

#include "../llaccordionctrltab.h"

#include "../llaccordionctrl.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"

#include "alheadlessui_fixture.h"

#include "llxmlnode.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct llaccordionctrltab_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    };

    // The block calls it display_children and a file calls it `expanded`.
    // Both reach it now, so a file that guessed the other name is no longer
    // writing an attribute that is read and dropped without a word -- which
    // is what a comment in the viewer had taken for the value being ignored.
    //
    // Only the reading is tested here: building a tab measures the text of
    // its header, and text measurement is the one thing this fixture has no
    // fonts for.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: both expanded and display_children say whether a tab is open", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const auto require_read = [](const std::string& attribute)
        {
            CAPTURE(attribute);
            const std::string source =
                "<accordion_tab name=\"tab\" title=\"Tab\" " + attribute + "=\"false\"/>\n";
            LLXMLNodePtr node;
            REQUIRE(LLXMLNode::parseBuffer(source.data(), source.size(), node));

            LLAccordionCtrlTab::Params params;
            LLXUIParser parser;
            parser.readXUI(node, params, "accordion_test.xml");
            REQUIRE(params.display_children.isProvided());
            REQUIRE_FALSE(params.display_children());
        };

        require_read("expanded");
        require_read("display_children");
    }

    // And left out, a tab is open: a section that says nothing about itself
    // is one nobody has folded.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a tab is open when a file leaves display_children out", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLAccordionCtrlTab::Params params;
        UNSCOPED_INFO("open unless a file says otherwise");
        CHECK(params.display_children());
        UNSCOPED_INFO("and nothing wrote it");
        CHECK_FALSE(params.display_children.isProvided());
    }

    // A tab carries the panel it holds: resize the tab and the panel takes
    // the width, less the tab's padding. Worth pinning even though it holds,
    // because `hideScrollbar` returns early once the scrollbar is away and
    // that looks, reading it, exactly like a tab that stops placing its panel
    // -- twice now. It places it anyway, and this is what says so.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a tab's panel takes the tab's width less padding as it resizes", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
        tp.name = "tab";
        tp.title = "Tab";
        tp.display_children = true;
        // The branch that owns a scrollbar. A fitted tab always places its
        // panel and never reached the placing this is about.
        tp.fit_panel = false;
        tp.rect = LLRect(0, 200, 300, 0);
        LLAccordionCtrlTab* tab = LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);

        // Short enough to fit, so the tab's own scrollbar stays away.
        LLPanel::Params pp;
        pp.name = "rows";
        pp.rect = LLRect(0, 40, 300, 0);
        LLPanel* rows = LLUICtrlFactory::create<LLPanel>(pp);
        tab->setAccordionView(rows);

        tab->reshape(300, 200);
        const S32 wide = rows->getRect().getWidth();
        UNSCOPED_INFO("the panel is as wide as the tab lets it be");
        CHECK(wide ==
                      300 - tab->getPaddingLeft() - tab->getPaddingRight());

        tab->reshape(200, 200);
        UNSCOPED_INFO("and follows the tab in");
        CHECK(rows->getRect().getWidth() == wide - 100);

        tab->reshape(300, 200);
        UNSCOPED_INFO("and back out");
        CHECK(rows->getRect().getWidth() == wide);

        tab->die();
    }

    // A fitted panel saying how tall it wants to be is how a tab learns its
    // height without anyone writing it down: the tab takes the height plus
    // its header and padding, the panel is squeezed to exactly the height it
    // asked for, what follows the panel's top stays where it was against it,
    // and a tab that is shut opens to the new height when it next opens.
    // The Lightbox sizes every one of its sections this way.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a fitted panel's asked-for height sizes its tab even while shut", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
        tp.name = "tab";
        tp.title = "Tab";
        tp.display_children = true;
        tp.fit_panel = true;
        tp.rect = LLRect(0, 109, 300, 0);
        LLAccordionCtrlTab* tab = LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);

        LLPanel::Params pp;
        pp.name = "rows";
        pp.rect = LLRect(0, 80, 300, 0);
        LLPanel* rows = LLUICtrlFactory::create<LLPanel>(pp);
        LLPanel::Params cp;
        cp.name = "row";
        cp.rect = LLRect(8, 72, 100, 56);
        cp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        LLPanel* row = LLUICtrlFactory::create<LLPanel>(cp);
        rows->addChild(row);
        tab->setAccordionView(rows);

        const S32 chrome = tab->getHeaderHeight() + tab->getPaddingTop() + tab->getPaddingBottom();
        const S32 below_top = rows->getRect().getHeight() - row->getRect().mTop;

        rows->notifyParent(LLSD().with("action", "size_changes").with("height", 120));
        UNSCOPED_INFO("the tab is the height asked for and its chrome");
        CHECK(tab->getRect().getHeight() == 120 + chrome);
        UNSCOPED_INFO("the panel is the height it asked for");
        CHECK(rows->getRect().getHeight() == 120);
        UNSCOPED_INFO("and the row is as far below its top as it was");
        CHECK(rows->getRect().getHeight() - row->getRect().mTop == below_top);

        tab->setDisplayChildren(false);
        rows->notifyParent(LLSD().with("action", "size_changes").with("height", 150));
        UNSCOPED_INFO("shut, the tab stays shut");
        CHECK(tab->getRect().getHeight() < 150);
        tab->setDisplayChildren(true);
        UNSCOPED_INFO("and opens to the height asked for while it was shut");
        CHECK(tab->getRect().getHeight() == 150 + chrome);

        tab->die();
    }

    // A hidden tab takes no room. Once its accordion is arranged, the tab
    // below closes up under the one above, and showing it and arranging again
    // puts it back between them. It is the arrange that moves them, not the
    // hiding, so a caller that hides a tab arranges afterwards. The Lightbox
    // hides whichever of its bloom and legacy glow sections the renderer is
    // not running this way.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a hidden tab takes no room once its accordion is arranged", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLAccordionCtrl::Params ap(LLUICtrlFactory::getDefaultParams<LLAccordionCtrl>());
        ap.name = "accordion";
        ap.rect = LLRect(0, 400, 300, 0);
        ap.single_expansion = false;
        LLAccordionCtrl* accordion = LLUICtrlFactory::create<LLAccordionCtrl>(ap);

        // Open, and as tall as their rects say: 180 of the 400, so the
        // accordion has no scrollbar to move them by.
        const auto add_tab = [accordion](const std::string& name, S32 height)
        {
            LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
            tp.name = name;
            tp.title = name;
            tp.display_children = true;
            tp.fit_panel = false;
            tp.rect = LLRect(0, height, 300, 0);
            LLAccordionCtrlTab* tab = LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);
            LLPanel::Params pp;
            pp.name = name + "_rows";
            pp.rect = LLRect(0, 10, 300, 0);
            tab->setAccordionView(LLUICtrlFactory::create<LLPanel>(pp));
            accordion->addCollapsibleCtrl(tab);
            return tab;
        };
        LLAccordionCtrlTab* above = add_tab("above", 50);
        LLAccordionCtrlTab* middle = add_tab("middle", 60);
        LLAccordionCtrlTab* below = add_tab("below", 70);
        accordion->arrange();
        UNSCOPED_INFO("the middle tab is under the first");
        CHECK(middle->getRect().mTop == above->getRect().mBottom);
        UNSCOPED_INFO("and the last under it");
        CHECK(below->getRect().mTop == middle->getRect().mBottom);

        middle->setVisible(false);
        accordion->arrange();
        UNSCOPED_INFO("hidden, it leaves no gap");
        CHECK(below->getRect().mTop == above->getRect().mBottom);

        middle->setVisible(true);
        accordion->arrange();
        UNSCOPED_INFO("shown, it is back under the first");
        CHECK(middle->getRect().mTop == above->getRect().mBottom);
        UNSCOPED_INFO("and the last is under it again");
        CHECK(below->getRect().mTop == middle->getRect().mBottom);

        accordion->die();
    }

    // What a section is for is said over its header and nowhere else: the
    // tab carries no tooltip of its own, since a parent's is offered before
    // its children's and would pop up over everything in the panel. A title
    // the header cuts short is said in full ahead of it. The Lightbox gives
    // every one of its sections one.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a section's tool tip is on its header with a cut title ahead of it", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const std::string source =
            "<accordion_tab name=\"tab\" title=\"Tab\" header_tool_tip=\"What it does\"/>\n";
        LLXMLNodePtr node;
        REQUIRE(LLXMLNode::parseBuffer(source.data(), source.size(), node));
        LLAccordionCtrlTab::Params read;
        LLXUIParser parser;
        parser.readXUI(node, read, "accordion_test.xml");
        UNSCOPED_INFO("a file can say it");
        CHECK(read.header_tool_tip() == std::string("What it does"));

        const auto build = [](const std::string& title, const std::string& tip)
        {
            LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
            tp.name = "tab";
            tp.title = title;
            if (!tip.empty())
            {
                tp.header_tool_tip = tip;
            }
            tp.rect = LLRect(0, 100, 300, 0);
            return LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);
        };
        const auto require_header = [](LLAccordionCtrlTab* tab)
        {
            LLView* found = tab->findChild<LLView>("dd_header", false);
            INFO("the tab has its header");
            REQUIRE(found != nullptr);
            return found;
        };

        LLAccordionCtrlTab* tab = build("Tab", "What it does");
        tab->reshape(300, 100);
        {
            INFO("the header says it");
            CHECK(require_header(tab)->getToolTip() == std::string("What it does"));
        }
        UNSCOPED_INFO("and the tab does not");
        CHECK(tab->getToolTip().empty());

        const std::string title = "A title far too long for the header it is in";
        tab->setTitle(title);
        tab->reshape(60, 100);
        {
            INFO("cut short, the title comes first");
            CHECK(require_header(tab)->getToolTip() ==
                          title + "\nWhat it does");
        }
        tab->setTitle("Tab");
        tab->reshape(300, 100);
        {
            INFO("and goes once it fits");
            CHECK(require_header(tab)->getToolTip() == std::string("What it does"));
        }
        tab->die();

        LLAccordionCtrlTab* plain = build("Tab", "");
        plain->reshape(300, 100);
        {
            INFO("without one, a title that fits says nothing");
            CHECK(require_header(plain)->getToolTip().empty());
        }
        plain->setTitle(title);
        plain->reshape(60, 100);
        {
            INFO("and one cut short says itself");
            CHECK(require_header(plain)->getToolTip() == title);
        }
        plain->die();
    }

    // And the header says it over itself and no further. The tab used to hand
    // the header half a header more than it is tall, which is the gap under
    // it and the top of the first row: with every Lightbox section saying
    // what it is for, that band said so over the controls instead of them.
    TEST_CASE_METHOD(llaccordionctrltab_data, "llaccordionctrltab: a header's tool tip covers the header and no further", "[llaccordionctrltab]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
        tp.name = "tab";
        tp.title = "Tab";
        tp.header_tool_tip = "What it does";
        tp.display_children = true;
        tp.fit_panel = false;
        tp.rect = LLRect(0, 200, 300, 0);
        LLAccordionCtrlTab* tab = LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);
        LLPanel::Params pp;
        pp.name = "rows";
        pp.rect = LLRect(0, 150, 300, 0);
        tab->setAccordionView(LLUICtrlFactory::create<LLPanel>(pp));
        tab->reshape(300, 200);

        const S32 header_bottom = 200 - tab->getHeaderHeight();
        UNSCOPED_INFO("over the header, the header answers");
        CHECK(tab->handleToolTip(10, header_bottom + 4, MASK_NONE));
        UNSCOPED_INFO("just under it, nothing does");
        CHECK_FALSE(tab->handleToolTip(10, header_bottom - 4, MASK_NONE));

        tab->die();
    }
}
