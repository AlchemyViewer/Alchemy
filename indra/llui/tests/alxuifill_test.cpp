/**
 * @file alxuifill_test.cpp
 * @brief That a container fills what it is in, at the size it was authored.
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

#include "../llfloater.h"
#include "../lllayoutstack.h"
#include "../lltabcontainer.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "llxmlnode.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gFillTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFillTestAnonName;
}

namespace tut
{
    struct alxuifill_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A file built the way the studio builds one to look at it: as a
        // child of a panel, so nothing it does reaches the floater view.
        static LLFloater* build(const std::string& file, LLPanel*& stage)
        {
            LLXMLNodePtr node;
            if (!LLUICtrlFactory::getLayeredXMLNode(file, node))
            {
                return nullptr;
            }

            LLPanel::Params sp(LLUICtrlFactory::getDefaultParams<LLPanel>());
            sp.name = "stage";
            sp.rect = LLRect(0, 1080, 1920, 0);
            stage = LLUICtrlFactory::create<LLPanel>(sp);

            LLFloater* floater = new LLFloater(LLSD(), LLFloater::getDefaultParams());
            stage->addChild(floater);
            if (!floater->initFloaterXML(node, stage, file))
            {
                return nullptr;
            }
            // At the size the FILE asks for, not the one the floater ended
            // up at: a file with save_rect="true" applies a remembered rect,
            // and in a test that setting does not exist, so the window comes
            // out a couple of hundred pixels short of what it says it is.
            // What is being asked here is whether the layout holds at the
            // size the file names.
            S32 want_w = 0;
            S32 want_h = 0;
            node->getAttributeS32("width", want_w);
            node->getAttributeS32("height", want_h);
            if (want_w > 0 && want_h > 0)
            {
                floater->reshape(want_w, want_h);
            }
            if (!ll_test::settle(floater))
            {
                return nullptr;
            }
            return floater;
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }

        // What a layout panel's contents owe it, and what a stack owes
        // whatever holds it.
        //
        // **Fit.** Anything in a layout panel must be inside it. A panel is
        // sized by its stack, so a child authored against a width the panel
        // used to have is a child hanging off the edge -- and the part that
        // hangs off is not clipped into view later, it is gone. The tab strip
        // of an inspector and the last two buttons of a toolbar went that way.
        //
        // **Fill**, of a layout stack that is the only thing in its parent. A
        // stack authored to a size its parent no longer has keeps that gap for
        // ever: `follows` preserves an offset, it never closes one, and dead
        // space down one side of a window is exactly that.
        //
        // Only stacks are asked to fill. An ordinary widget sits inset in its
        // row on purpose -- a twenty-two pixel field centred in a row of
        // twenty-six is not a defect -- and a stack beside other children is
        // exempt too, because a window puts one between its menu bar and its
        // status line deliberately.
        static void ensureFills(const std::string& file, LLView* view, std::vector<std::string>& gaps)
        {
            for (LLView* child : *view->getChildList())
            {
                const bool is_stack = child->as<LLLayoutStack>() != nullptr;
                const bool in_panel = view->as<LLLayoutPanel>() != nullptr;
                // A tab page holds one thing and hands it the whole page, so
                // it owes exactly what a layout panel owes.
                const bool in_page = view->getParent() && view->getParent()->as<LLTabContainer>();
                if (child->getVisible() && (is_stack || in_panel || in_page))
                {
                    const LLRect want = view->getLocalRect();
                    const LLRect got = child->getRect();
                    const bool fits = want.contains(got);
                    const bool fills = got.getWidth() == want.getWidth()
                                    && got.getHeight() == want.getHeight();
                    const bool must_fill = (is_stack || in_page) && view->getChildCount() == 1;
                    if (!fits || (must_fill && !fills))
                    {
                        std::string bands;
                        for (LLView* band : *child->getChildList())
                        {
                            bands += "\n      " + band->getName() + " " + where(band->getRect());
                        }
                        gaps.push_back(file + ": " + child->getName() + " is "
                                       + where(got) + (fits ? " and does not fill " : " and does not fit ")
                                       + view->getName() + ", which is " + where(want) + bands);
                    }
                }
                ensureFills(file, child, gaps);
            }
        }
    };

    typedef test_group<alxuifill_data> alxuifill_test;
    typedef alxuifill_test::object     alxuifill_object;
    tut::alxuifill_test alxuifill_testgroup("alxuifill");

    // The tool's own window. It is the file this rule was learnt on: a
    // stack authored at the width the window used to be, in a window that
    // had grown, left four hundred pixels of nothing down one side -- and
    // no size the developer dragged it to would ever take that back.
    template<> template<>
    void alxuifill_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLPanel* stage = nullptr;
        LLFloater* floater = build("floater_xui_studio.xml", stage);
        ensure("the studio's own window builds", floater != nullptr);

        std::vector<std::string> gaps;
        ensureFills("floater_xui_studio.xml", floater, gaps);

        std::string report;
        for (const std::string& gap : gaps)
        {
            report += "\n  " + gap;
        }
        ensure("every stack fills what it is in:" + report, gaps.empty());
        stage->die();
    }
}
