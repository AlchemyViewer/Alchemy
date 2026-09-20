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

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gAccordionTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gAccordionTestAnonName;
}

namespace tut
{
    struct llaccordionctrltab_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    };

    typedef test_group<llaccordionctrltab_data> llaccordionctrltab_test;
    typedef llaccordionctrltab_test::object     llaccordionctrltab_object;
    tut::llaccordionctrltab_test llaccordionctrltab_testgroup("llaccordionctrltab");

    // The block calls it display_children and a file calls it `expanded`.
    // Both reach it now, so a file that guessed the other name is no longer
    // writing an attribute that is read and dropped without a word -- which
    // is what a comment in the viewer had taken for the value being ignored.
    //
    // Only the reading is tested here: building a tab measures the text of
    // its header, and text measurement is the one thing this fixture has no
    // fonts for.
    template<> template<>
    void llaccordionctrltab_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const auto reads = [](const std::string& attribute)
        {
            const std::string source =
                "<accordion_tab name=\"tab\" title=\"Tab\" " + attribute + "=\"false\"/>\n";
            LLXMLNodePtr node;
            ensure("parses", LLXMLNode::parseBuffer(source.data(), source.size(), node));

            LLAccordionCtrlTab::Params params;
            LLXUIParser parser;
            parser.readXUI(node, params, "accordion_test.xml");
            ensure(attribute + " was read", params.display_children.isProvided());
            ensure(attribute + " says what it says", !params.display_children());
        };

        reads("expanded");
        reads("display_children");
    }

    // And left out, a tab is open: a section that says nothing about itself
    // is one nobody has folded.
    template<> template<>
    void llaccordionctrltab_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLAccordionCtrlTab::Params params;
        ensure("open unless a file says otherwise", params.display_children());
        ensure("and nothing wrote it", !params.display_children.isProvided());
    }

    // A tab carries the panel it holds: resize the tab and the panel takes
    // the width, less the tab's padding. Worth pinning even though it holds,
    // because `hideScrollbar` returns early once the scrollbar is away and
    // that looks, reading it, exactly like a tab that stops placing its panel
    // -- twice now. It places it anyway, and this is what says so.
    template<> template<>
    void llaccordionctrltab_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the panel is as wide as the tab lets it be", wide,
                      300 - tab->getPaddingLeft() - tab->getPaddingRight());

        tab->reshape(200, 200);
        ensure_equals("and follows the tab in", rows->getRect().getWidth(), wide - 100);

        tab->reshape(300, 200);
        ensure_equals("and back out", rows->getRect().getWidth(), wide);

        tab->die();
    }

    // A fitted panel saying how tall it wants to be is how a tab learns its
    // height without anyone writing it down: the tab takes the height plus
    // its header and padding, the panel is squeezed to exactly the height it
    // asked for, what follows the panel's top stays where it was against it,
    // and a tab that is shut opens to the new height when it next opens.
    // The Lightbox sizes every one of its sections this way.
    template<> template<>
    void llaccordionctrltab_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the tab is the height asked for and its chrome", tab->getRect().getHeight(), 120 + chrome);
        ensure_equals("the panel is the height it asked for", rows->getRect().getHeight(), 120);
        ensure_equals("and the row is as far below its top as it was",
                      rows->getRect().getHeight() - row->getRect().mTop, below_top);

        tab->setDisplayChildren(false);
        rows->notifyParent(LLSD().with("action", "size_changes").with("height", 150));
        ensure("shut, the tab stays shut", tab->getRect().getHeight() < 150);
        tab->setDisplayChildren(true);
        ensure_equals("and opens to the height asked for while it was shut",
                      tab->getRect().getHeight(), 150 + chrome);

        tab->die();
    }

    // A hidden tab takes no room. Once its accordion is arranged, the tab
    // below closes up under the one above, and showing it and arranging again
    // puts it back between them. It is the arrange that moves them, not the
    // hiding, so a caller that hides a tab arranges afterwards. The Lightbox
    // hides whichever of its bloom and legacy glow sections the renderer is
    // not running this way.
    template<> template<>
    void llaccordionctrltab_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the middle tab is under the first", middle->getRect().mTop, above->getRect().mBottom);
        ensure_equals("and the last under it", below->getRect().mTop, middle->getRect().mBottom);

        middle->setVisible(false);
        accordion->arrange();
        ensure_equals("hidden, it leaves no gap", below->getRect().mTop, above->getRect().mBottom);

        middle->setVisible(true);
        accordion->arrange();
        ensure_equals("shown, it is back under the first", middle->getRect().mTop, above->getRect().mBottom);
        ensure_equals("and the last is under it again", below->getRect().mTop, middle->getRect().mBottom);

        accordion->die();
    }
}
