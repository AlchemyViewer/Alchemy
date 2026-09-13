/**
 * @file lllayoutstack_test.cpp
 * @brief Tests for the layout stack: the panels it holds and how they leave
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#include "../lllayoutstack.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gLayoutStackTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gLayoutStackTestAnonName;
}

namespace tut
{
    struct lllayoutstack_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLLayoutStack* stack()
        {
            LLLayoutStack::Params p;
            p.name = "stack";
            p.rect = LLRect(0, 100, 300, 0);
            p.orientation = LLLayoutStack::HORIZONTAL;
            return LLUICtrlFactory::create<LLLayoutStack>(p);
        }

        static LLLayoutPanel* panel(const std::string& name)
        {
            LLLayoutPanel::Params p;
            p.name = name;
            p.rect = LLRect(0, 100, 100, 0);
            return LLUICtrlFactory::create<LLLayoutPanel>(p);
        }
    };

    typedef test_group<lllayoutstack_data> lllayoutstack_test;
    typedef lllayoutstack_test::object     lllayoutstack_object;
    tut::lllayoutstack_test lllayoutstack_testgroup("lllayoutstack");

    // A panel that leaves its stack by being deleted is gone from the stack's
    // list, the same as one that was removed first. The stack lays out from
    // that list every frame, so an entry that outlives its panel is a read of
    // freed memory on the next draw.
    template<> template<>
    void lllayoutstack_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLLayoutStack> s(stack());
        LLLayoutPanel* a = panel("a");
        LLLayoutPanel* b = panel("b");
        s->addPanel(a);
        s->addPanel(b);
        ensure_equals("two panels", s->getNumPanels(), 2);

        delete a;
        ensure_equals("a deleted panel has left the list", s->getNumPanels(), 1);

        s->removeChild(b);
        delete b;
        ensure_equals("a removed panel has left the list", s->getNumPanels(), 0);
    }
}
