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

#include "alkeymap.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

namespace
{
    struct alkeymap_data
    {
        typedef ALEditorCommand C;
    };

    TEST_CASE_METHOD(alkeymap_data, "alkeymap: every command has a name of its own that finds it again", "[alkeymap]")
    {
        std::set<std::string> names;
        for (size_t i = 1; i < static_cast<size_t>(C::COUNT); ++i)
        {
            const C           command = static_cast<C>(i);
            const std::string name    = alEditorCommandName(command);
            CAPTURE(i, name);
            REQUIRE_FALSE(name.empty());
            REQUIRE(name != "none");
            UNSCOPED_INFO("a name no other command has");
            REQUIRE(names.insert(name).second);
            REQUIRE(alEditorCommandFromName(name) == command);
        }
        CHECK_FALSE(alEditorCommandFromName("frobnicate"));
        UNSCOPED_INFO("none is no command to find");
        CHECK_FALSE(alEditorCommandFromName("none"));
    }

    TEST_CASE_METHOD(alkeymap_data, "alkeymap: a key needs exactly its modifiers; a rebind replaces and an unbind frees", "[alkeymap]")
    {
        ALKeymap map;
        map.bind('K', MASK_CONTROL, C::DeleteLine);
        CHECK(map.lookup('K', MASK_CONTROL) == C::DeleteLine);
        UNSCOPED_INFO("not with Shift too");
        CHECK(map.lookup('K', MASK_CONTROL | MASK_SHIFT) == C::None);
        UNSCOPED_INFO("nor without Control");
        CHECK(map.lookup('K', MASK_NONE) == C::None);
        map.bind('K', MASK_CONTROL, C::Cut);
        {
            INFO("replaced");
            CHECK(map.lookup('K', MASK_CONTROL) == C::Cut);
            CHECK(map.bindings().size() == 1);
        }
        map.bind(KEY_F5, MASK_NONE, C::Cut);
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        {
            INFO("its first key");
            CHECK(map.keysFor(C::Cut, key, mask));
            CHECK(key == 'K');
            CHECK(mask == MASK_CONTROL);
        }
        map.unbind('K', MASK_CONTROL);
        CHECK(map.lookup('K', MASK_CONTROL) == C::None);
        {
            INFO("its other key now first");
            CHECK(map.keysFor(C::Cut, key, mask));
            CHECK(key == KEY_F5);
        }
        UNSCOPED_INFO("none for a command with none");
        CHECK_FALSE(map.keysFor(C::Paste, key, mask));
    }

    // The everyday keys, Backspace and Return under Shift as well, each key
    // bound once, and off the Mac no Control-Alt with a key that types.
    TEST_CASE_METHOD(alkeymap_data, "alkeymap: the standard keys bind the everyday commands and each key once", "[alkeymap]")
    {
        const ALKeymap map = ALKeymap::standard();
        CHECK(map.lookup('Z', MASK_CONTROL) == C::Undo);
        CHECK(map.lookup('F', MASK_CONTROL) == C::Find);
        CHECK(map.lookup(KEY_BACKSPACE, MASK_NONE) == C::DeleteLeft);
        CHECK(map.lookup(KEY_BACKSPACE, MASK_SHIFT) == C::DeleteLeft);
        CHECK(map.lookup(KEY_RETURN, MASK_NONE) == C::NewLine);
        CHECK(map.lookup(KEY_RETURN, MASK_SHIFT) == C::NewLine);
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        {
            INFO("the menu shows Backspace alone");
            CHECK(map.keysFor(C::DeleteLeft, key, mask));
            CHECK(mask == MASK_NONE);
        }
        std::set<std::pair<KEY, MASK>> seen;
        for (const ALKeymap::Binding& binding : map.bindings())
        {
            UNSCOPED_INFO("each key once");
            REQUIRE(seen.insert({ binding.key, binding.mask }).second);
#if !LL_DARWIN
            // Control and Alt together is AltGr on many a keyboard, which
            // types.
            const bool types = binding.key >= 0x20 && binding.key < 0x80;
            UNSCOPED_INFO("no Control-Alt " << static_cast<char>(binding.key));
            REQUIRE((!types || (binding.mask & (MASK_CONTROL | MASK_ALT)) != (MASK_CONTROL | MASK_ALT)));
#endif
        }
    }

    // The Mac's conventions there, and the others' elsewhere; a key that
    // needs no Fn is the one shown first.
    TEST_CASE_METHOD(alkeymap_data, "alkeymap: find next and replace and folding keep each platform's conventions", "[alkeymap]")
    {
        const ALKeymap map = ALKeymap::standard();
        KEY            key  = KEY_NONE;
        MASK           mask = MASK_NONE;
#if LL_DARWIN
        UNSCOPED_INFO("Command-G the next match");
        CHECK(map.lookup('G', MASK_CONTROL) == C::FindNext);
        UNSCOPED_INFO("Shift-Command-G the one before");
        CHECK(map.lookup('G', MASK_CONTROL | MASK_SHIFT) == C::FindPrevious);
        UNSCOPED_INFO("Command-H left to the system");
        CHECK(map.lookup('H', MASK_CONTROL) == C::None);
        UNSCOPED_INFO("Command-Option-F replaces");
        CHECK(map.lookup('F', MASK_CONTROL | MASK_ALT) == C::Replace);
        UNSCOPED_INFO("Command-Option-[ folds");
        CHECK(map.lookup('[', MASK_CONTROL | MASK_ALT) == C::Fold);
        UNSCOPED_INFO("Command-Shift-[ left to the tabs");
        CHECK(map.lookup('[', MASK_CONTROL | MASK_SHIFT) == C::None);
        {
            INFO("a definition shown as Control-Command-J");
            CHECK(map.keysFor(C::GoToDefinition, key, mask));
            CHECK(key == 'J');
            CHECK(mask == (MASK_CONTROL | MASK_MAC_CONTROL));
        }
        UNSCOPED_INFO("F12 still");
        CHECK(map.lookup(KEY_F12, MASK_NONE) == C::GoToDefinition);
        UNSCOPED_INFO("Command-semicolon the next misspelling");
        CHECK(map.lookup(';', MASK_CONTROL) == C::NextMisspelling);
        UNSCOPED_INFO("Option with an arrow a word");
        CHECK(map.lookup(KEY_LEFT, MASK_ALT) == C::MoveWordLeft);
#else
        UNSCOPED_INFO("Alt with an arrow left to whoever holds the editor, for back");
        CHECK(map.lookup(KEY_LEFT, MASK_ALT) == C::None);
        UNSCOPED_INFO("Control-H replaces");
        CHECK(map.lookup('H', MASK_CONTROL) == C::Replace);
        UNSCOPED_INFO("Control-Shift-[ folds");
        CHECK(map.lookup('[', MASK_CONTROL | MASK_SHIFT) == C::Fold);
        UNSCOPED_INFO("F3 the next match, shown");
        CHECK((map.keysFor(C::FindNext, key, mask) && key == KEY_F3));
#endif
    }

    TEST_CASE_METHOD(alkeymap_data, "alkeymap: the keypad's slash KEY_DIVIDE looks up and binds as the slash key", "[alkeymap]")
    {
        ALKeymap map = ALKeymap::standard();
        CHECK(map.lookup('/', MASK_CONTROL) == C::ToggleComment);
        CHECK(map.lookup(KEY_DIVIDE, MASK_CONTROL) == C::ToggleComment);
        CHECK(map.lookup(KEY_DIVIDE, MASK_NONE) == C::None);

        map.unbind(KEY_DIVIDE, MASK_CONTROL);
        UNSCOPED_INFO("unbound as either");
        CHECK(map.lookup('/', MASK_CONTROL) == C::None);
        map.unbind('7', MASK_CONTROL | MASK_SHIFT);
        map.bind(KEY_DIVIDE, MASK_CONTROL | MASK_ALT, C::ToggleComment);
        KEY  key  = KEY_NONE;
        MASK mask = MASK_NONE;
        {
            INFO("bound as KEY_DIVIDE, kept and shown as '/'");
            CHECK(map.keysFor(C::ToggleComment, key, mask));
            CHECK(key == '/');
            CHECK(mask == (MASK_CONTROL | MASK_ALT));
        }
    }
}
