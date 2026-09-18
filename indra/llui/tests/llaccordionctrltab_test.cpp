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
}
