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

        const auto reads = [this](const std::string& attribute)
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
}
