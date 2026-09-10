/**
 * @file alpopover_test.cpp
 * @brief Where it lands, and the three ways out of it.
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

#include "../alpopover.h"

#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gPopoverTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gPopoverTestAnonName;
}

namespace tut
{
    struct alpopover_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

        // Something to hang one off, placed well inside the screen.
        static LLPanel* anchor(S32 left = 200, S32 bottom = 300)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "anchor";
            p.rect = LLRect(left, bottom + 20, left + 120, bottom);
            LLPanel* view = LLUICtrlFactory::create<TestPanel>(p);
            gFloaterView->addChild(view);
            return view;
        }

        static LLPanel* content(S32 width = 160, S32 height = 90)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "content";
            p.rect = LLRect(0, height, width, 0);
            return LLUICtrlFactory::create<TestPanel>(p);
        }
    };

    typedef test_group<alpopover_data> alpopover_test;
    typedef alpopover_test::object     alpopover_object;
    tut::alpopover_test alpopover_testgroup("alpopover");

    // Under the control, its left edge with the control's, and the size the
    // content asked for: what is in it decides how big it is.
    template<> template<>
    void alpopover_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel* over = anchor();
        ALPopover* popover = ALPopover::show(over, content(160, 90));
        ensure("opens", popover != nullptr);
        ensure("and is shown", popover->getVisible());

        const LLRect screen = over->calcScreenRect();
        const LLRect where = popover->getRect();
        ensure_equals("the size the content asked for", where.getWidth(), 160);
        ensure_equals("in both directions", where.getHeight(), 90);
        ensure_equals("left edges line up", where.mLeft, screen.mLeft);
        ensure_equals("and it hangs from the bottom of the control",
                      where.mTop, screen.mBottom);

        ensure("the content is in it", popover->getChild<LLPanel>("content", true) != nullptr);
        popover->closeFloater();
        over->die();
    }

    // A popover that would hang off the bottom of the screen goes above the
    // control instead: one a person cannot see the whole of has not opened.
    template<> template<>
    void alpopover_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel* over = anchor(200, 10);
        ALPopover* popover = ALPopover::show(over, content(160, 90));
        ensure("opens", popover != nullptr);
        ensure("and is on the screen", popover->getRect().mBottom >= 0);
        ensure("above the control rather than below it",
               popover->getRect().mBottom >= over->calcScreenRect().mTop);
        popover->closeFloater();
        over->die();
    }

    // Three ways out, and a caller applying on close has to tell them apart.
    template<> template<>
    void alpopover_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Settled: what was chosen stands.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            popover->settle();
            ensure_equals("said once", said.size(), 1u);
            ensure("and said it was not escaped", !said.front());
            over->die();
        }

        // Escaped: the caller keeps what it had.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            ensure("escape is taken", popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            ensure_equals("said once", said.size(), 1u);
            ensure("and said it was escaped", said.front());
            over->die();
        }

        // Looking somewhere else is the usual way out, and it settles: a
        // person who has chosen and moved on has finished.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            popover->onFocusLost();
            ensure_equals("said once", said.size(), 1u);
            ensure("and said it was not escaped", !said.front());
            over->die();
        }
    }

    // Nothing to hang it off is nothing to open, and the content given is not
    // leaked over it.
    template<> template<>
    void alpopover_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure("no anchor, no popover", ALPopover::show(nullptr, content()) == nullptr);
        LLPanel* over = anchor();
        ensure("and no content, none either", ALPopover::show(over, nullptr) == nullptr);
        over->die();
    }
}
