/**
 * @file alkeymap_test.cpp
 * @brief The editor's commands, their names, and the standard keys for them.
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

#include "../alkeymap.h"

#include "../test/lltut.h"

#include <set>
#include <string>

namespace tut
{
    struct alkeymap_data
    {
        typedef ALEditorCommand C;
    };
    typedef test_group<alkeymap_data> alkeymap_group;
    typedef alkeymap_group::object    alkeymap_object;
    alkeymap_group                    alkeymap_instance("alkeymap");

    template<> template<>
    void alkeymap_object::test<1>()
    {
        set_test_name("every command has a name of its own, and is found again by it");
        std::set<std::string> names;
        for (size_t i = 1; i < static_cast<size_t>(C::COUNT); ++i)
        {
            const C           command = static_cast<C>(i);
            const std::string name    = alEditorCommandName(command);
            ensure("named", !name.empty() && name != "none");
            ensure("once", names.insert(name).second);
            ensure("found again", alEditorCommandFromName(name) == command);
        }
        ensure("a name no command has", !alEditorCommandFromName("frobnicate"));
        ensure("none is no command to find", !alEditorCommandFromName("none"));
    }

    template<> template<>
    void alkeymap_object::test<2>()
    {
        set_test_name("a key is looked up with exactly its modifiers; binding it again replaces; unbinding it frees it");
        ALKeymap map;
        map.bind('K', MASK_CONTROL, C::DeleteLine);
        ensure("found", map.lookup('K', MASK_CONTROL) == C::DeleteLine);
        ensure("not with Shift too", map.lookup('K', MASK_CONTROL | MASK_SHIFT) == C::None);
        ensure("nor without Control", map.lookup('K', MASK_NONE) == C::None);
        map.bind('K', MASK_CONTROL, C::Cut);
        ensure("replaced", map.lookup('K', MASK_CONTROL) == C::Cut && map.bindings().size() == 1);
        map.bind(KEY_F5, MASK_NONE, C::Cut);
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        ensure("its first key", map.keysFor(C::Cut, key, mask) && key == 'K' && mask == MASK_CONTROL);
        map.unbind('K', MASK_CONTROL);
        ensure("freed", map.lookup('K', MASK_CONTROL) == C::None);
        ensure("its other key now first", map.keysFor(C::Cut, key, mask) && key == KEY_F5);
        ensure("none for a command with none", !map.keysFor(C::Paste, key, mask));
    }

    template<> template<>
    void alkeymap_object::test<3>()
    {
        set_test_name("the standard keys: the everyday ones, Shift-Backspace and Shift-Return, and no Control-Alt with a key that types off the Mac");
        const ALKeymap map = ALKeymap::standard();
        ensure("undo", map.lookup('Z', MASK_CONTROL) == C::Undo);
        ensure("find", map.lookup('F', MASK_CONTROL) == C::Find);
        ensure("Backspace", map.lookup(KEY_BACKSPACE, MASK_NONE) == C::DeleteLeft);
        ensure("and under Shift", map.lookup(KEY_BACKSPACE, MASK_SHIFT) == C::DeleteLeft);
        ensure("Return", map.lookup(KEY_RETURN, MASK_NONE) == C::NewLine);
        ensure("and under Shift", map.lookup(KEY_RETURN, MASK_SHIFT) == C::NewLine);
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        ensure("the menu shows Backspace alone", map.keysFor(C::DeleteLeft, key, mask) && mask == MASK_NONE);
        std::set<std::pair<KEY, MASK>> seen;
        for (const ALKeymap::Binding& binding : map.bindings())
        {
            ensure("each key once", seen.insert({ binding.key, binding.mask }).second);
#if !LL_DARWIN
            // Control and Alt together is AltGr on many a keyboard, which
            // types.
            const bool types = binding.key >= 0x20 && binding.key < 0x80;
            ensure(std::string("no Control-Alt ") + static_cast<char>(binding.key),
                   !types || (binding.mask & (MASK_CONTROL | MASK_ALT)) != (MASK_CONTROL | MASK_ALT));
#endif
        }
    }
}
