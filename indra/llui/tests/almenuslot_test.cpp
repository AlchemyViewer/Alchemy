/**
 * @file almenuslot_test.cpp
 * @brief Tests for ALMenuSlot: a context menu let go of as another is made, and with its view.
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

#include "almenuslot.h"

#include "../llmenugl.h"
#include "../lluictrl.h"
#include "../lluictrlfactory.h"

#include "llmortician.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct almenuslot_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        LLMenuHolderGL*      holder = nullptr;
        // Put back when the test ends, rather than left null for the next.
        LLMenuHolderGL*      container_before = LLMenuGL::sMenuContainer;

        almenuslot_data()
        {
            if (!ui.ok())
            {
                return;
            }
            // A menu holder, as the viewer's window has one.
            LLMenuHolderGL::Params p;
            p.name         = "menu_holder";
            p.rect         = LLRect(0, 1080, 1920, 0);
            p.mouse_opaque = false;
            holder         = LLUICtrlFactory::create<LLMenuHolderGL>(p);
            LLMenuGL::sMenuContainer = holder;
        }

        ~almenuslot_data()
        {
            LLMenuGL::sMenuContainer = container_before;
            LLMortician::updateClass();
            delete holder;
        }

        // A menu from a file the tests' skin has, its items' callbacks
        // registered as a maker would.
        LLContextMenu* make(ALMenuSlot& slot)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
            LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
            commit.add("PaneList.Copy", [](LLUICtrl*, const LLSD&) {});
            enable.add("PaneList.CopyEnabled", [](LLUICtrl*, const LLSD&) { return true; });
            return slot.make("menu_pane_list.xml");
        }
    };

    TEST_CASE_METHOD(almenuslot_data, "ALMenuSlot: a menu made is the slot's in the holder and another made replaces it", "[ALMenuSlot]")
    {
        ALMenuSlot     slot;
        LLContextMenu* first = make(slot);
        REQUIRE(first);
        CHECK(slot.get() == first);
        CHECK(first->getParent() == holder);
        CHECK_FALSE(first->isDead());
        LLContextMenu* second = make(slot);
        {
            INFO("the second is the slot's");
            REQUIRE(second);
            CHECK(second != first);
            CHECK(slot.get() == second);
        }
        {
            INFO("the first let go of");
            CHECK(first->isDead());
            CHECK_FALSE(second->isDead());
        }
    }

    TEST_CASE_METHOD(almenuslot_data, "ALMenuSlot: closing hides and kills the menu and so does the slot going", "[ALMenuSlot]")
    {
        LLContextMenu* open = nullptr;
        {
            ALMenuSlot slot;
            LLContextMenu* menu = make(slot);
            REQUIRE(menu);
            menu->setVisible(true);
            slot.close();
            {
                INFO("closed: hidden and gone");
                CHECK_FALSE(menu->getVisible());
                CHECK(menu->isDead());
                CHECK(slot.get() == nullptr);
            }
            open = make(slot);
            REQUIRE(open);
            open->setVisible(true);
        }
        {
            INFO("the slot gone, its menu is too");
            CHECK_FALSE(open->getVisible());
            CHECK(open->isDead());
        }
    }

    TEST_CASE_METHOD(almenuslot_data, "ALMenuSlot: without a menu holder nothing is made", "[ALMenuSlot]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMenuGL::sMenuContainer = nullptr;
        ALMenuSlot slot;
        CHECK(slot.make("menu_pane_list.xml") == nullptr);
        CHECK(slot.get() == nullptr);
        LLMenuGL::sMenuContainer = holder;
    }
}
