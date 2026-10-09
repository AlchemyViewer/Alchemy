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

#include "../test/lltut.h"

namespace tut
{
    struct almenuslot_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        LLMenuHolderGL*      holder = nullptr;

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
            LLMenuGL::sMenuContainer = nullptr;
            LLMortician::updateClass();
            delete holder;
        }

        // A menu from a file the tests' skin has, its items' callbacks
        // registered as a maker would.
        LLContextMenu* make(ALMenuSlot& slot)
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
            LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
            commit.add("PaneList.Copy", [](LLUICtrl*, const LLSD&) {});
            enable.add("PaneList.CopyEnabled", [](LLUICtrl*, const LLSD&) { return true; });
            return slot.make("menu_pane_list.xml");
        }
    };

    typedef test_group<almenuslot_data> almenuslot_group;
    typedef almenuslot_group::object     almenuslot_object;
    tut::almenuslot_group                almenuslot_test_group("ALMenuSlot");

    template<> template<>
    void almenuslot_object::test<1>()
    {
        set_test_name("a menu made is the slot's, in the holder; another made lets go of the first");
        ALMenuSlot     slot;
        LLContextMenu* first = make(slot);
        ensure("made", first && slot.get() == first && first->getParent() == holder);
        ensure("alive", !first->isDead());
        LLContextMenu* second = make(slot);
        ensure("the second is the slot's", second && second != first && slot.get() == second);
        ensure("the first let go of", first->isDead() && !second->isDead());
    }

    template<> template<>
    void almenuslot_object::test<2>()
    {
        set_test_name("closed, the menu is out of sight and gone; a slot going takes its open menu with it");
        LLContextMenu* open = nullptr;
        {
            ALMenuSlot slot;
            LLContextMenu* menu = make(slot);
            menu->setVisible(true);
            slot.close();
            ensure("closed: hidden and gone", !menu->getVisible() && menu->isDead() && slot.get() == nullptr);
            open = make(slot);
            open->setVisible(true);
        }
        ensure("the slot gone, its menu is too", !open->getVisible() && open->isDead());
    }

    template<> template<>
    void almenuslot_object::test<3>()
    {
        set_test_name("without a menu holder, nothing is made");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMenuGL::sMenuContainer = nullptr;
        ALMenuSlot slot;
        ensure("nothing", slot.make("menu_pane_list.xml") == nullptr && slot.get() == nullptr);
        LLMenuGL::sMenuContainer = holder;
    }
}
