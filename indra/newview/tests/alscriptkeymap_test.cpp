/**
 * @file alscriptkeymap_test.cpp
 * @brief Script Studio's keys: the editors' keymap and the menus' keys, with a person's changes over them.
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

#include "../alscriptkeymap.h"

#include "llcontrol.h"
#include "llkeyboard.h"

#include "../test/lltut.h"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>

// The setting the keymap is kept in, in a group of the test's own.
LLControlGroup gSavedSettings("Global");

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it.
class LLAvatarName;
const std::string gKeymapTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gKeymapTestAnonName;
}

namespace tut
{
    struct alscriptkeymap_data
    {
        typedef ALEditorCommand C;

        alscriptkeymap_data()
        {
            LLKeyboard::setStringTranslatorFunc([](std::string_view name) { return std::string(name); });
            if (!gSavedSettings.controlExists("ALScriptStudioKeymap"))
            {
                gSavedSettings.declareLLSD("ALScriptStudioKeymap", LLSD::emptyMap(), "a person's keys", LLControlVariable::PERSIST_NO);
            }
            ALScriptKeymap::restoreAll();
        }

        static bool has(const ALScriptKeymap::chords_t& keys, const ALKeyChord& chord)
        {
            return std::find(keys.begin(), keys.end(), chord) != keys.end();
        }
    };
    typedef test_group<alscriptkeymap_data> alscriptkeymap_group;
    typedef alscriptkeymap_group::object    alscriptkeymap_object;
    alscriptkeymap_group                    alscriptkeymap_instance("alscriptkeymap");

    template<> template<>
    void alscriptkeymap_object::test<1>()
    {
        set_test_name("the editors' keymap: the standard's, a person's keys over it, built again only when the setting changes");
        const ALKeymap* first = &ALScriptKeymap::current();
        ensure("the standard's", first->lookup('F', MASK_CONTROL) == C::Find);
        ensure("the same keymap asked again", &ALScriptKeymap::current() == first);
        ALScriptKeymap::rebind(C::Find, { { 'Y', MASK_CONTROL | MASK_SHIFT } });
        ensure("the new key", ALScriptKeymap::current().lookup('Y', MASK_CONTROL | MASK_SHIFT) == C::Find);
        ensure("the old one gone", ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::None);
        ensure("said rebound", ALScriptKeymap::isRebound(C::Find) && !ALScriptKeymap::isRebound(C::Replace));
        // Set by anything else -- the preferences' Cancel putting it back.
        gSavedSettings.setLLSD("ALScriptStudioKeymap", LLSD::emptyMap());
        ensure("the standard's again", ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find);
        ALScriptKeymap::rebind(C::Find, {});
        ensure("none: unbound", ALScriptKeymap::keysOf(ALScriptKeymap::current(), C::Find).empty());
        ALScriptKeymap::restore(C::Find);
        ensure("restored", ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find && !ALScriptKeymap::isRebound(C::Find));
    }

    template<> template<>
    void alscriptkeymap_object::test<2>()
    {
        set_test_name("the menus' keys: several as standard, two in turn, a person's in their place, and the standard's again");
#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        const ALScriptKeymap::chords_t next = ALScriptKeymap::menuKeys("next_tab");
        ensure("next tab: Control-PgDn, which the menu shows", !next.empty() && next.front() == ALKeyChord{ KEY_PAGE_DOWN, MASK_CONTROL });
        ensure("and Control-Tab", has(next, ALKeyChord{ KEY_TAB, REAL_CONTROL }));
        ensure("save all: Control-K, then S", ALScriptKeymap::menuKey("save_all") == (ALKeyChord{ 'S', MASK_NONE, 'K', MASK_CONTROL }));
        ensure("a command with none", ALScriptKeymap::menuKeys("revert").empty() && ALScriptKeymap::isMenuCommand("revert"));
        ensure("no such command", !ALScriptKeymap::isMenuCommand("frobnicate") && ALScriptKeymap::menuKeys("frobnicate").empty());

        const ALKeyChord two{ 'T', MASK_NONE, 'K', MASK_CONTROL };
        ALScriptKeymap::rebindMenu("revert", { two, ALKeyChord{ KEY_F9, MASK_SHIFT } });
        const ALScriptKeymap::chords_t mine = ALScriptKeymap::menuKeys("revert");
        ensure("kept, two in turn among them", mine.size() == 2 && mine[0] == two && mine[1] == (ALKeyChord{ KEY_F9, MASK_SHIFT }));
        ensure("said rebound", ALScriptKeymap::isMenuRebound("revert"));
        ALScriptKeymap::rebindMenu("next_tab", {});
        ensure("none at all, in place of the standard's", ALScriptKeymap::menuKeys("next_tab").empty() && ALScriptKeymap::menuKey("next_tab").none());
        ALScriptKeymap::restoreMenu("next_tab");
        ensure("the standard's again", ALScriptKeymap::menuKeys("next_tab") == next && !ALScriptKeymap::isMenuRebound("next_tab"));
        ALScriptKeymap::restoreAll();
        ensure("all of them", ALScriptKeymap::menuKeys("revert").empty());
    }

    template<> template<>
    void alscriptkeymap_object::test<3>()
    {
        set_test_name("the menus' table: each command once in its ids, each key the standard's once, and no Control-Alt with a key that types");
        const std::vector<std::string>& ids = ALScriptKeymap::menuIds();
        ensure_equals("each once", std::set<std::string>(ids.begin(), ids.end()).size(), ids.size());
        std::set<std::tuple<KEY, MASK, KEY, MASK>> seen;
        for (const std::string& id : ids)
        {
            for (const ALKeyChord& chord : ALScriptKeymap::menuKeys(id))
            {
                ensure("one command's key: " + id + " " + chord.describe(), seen.insert({ chord.key, chord.mask, chord.leadKey, chord.leadMask }).second);
                for (const auto& [key, mask] : { std::pair{ chord.key, chord.mask }, std::pair{ chord.leadKey, chord.leadMask } })
                {
                    const bool types = key >= 0x20 && key < 0x80;
                    ensure("no Control-Alt: " + id, !types || (mask & (MASK_CONTROL | MASK_ALT)) != (MASK_CONTROL | MASK_ALT));
                }
            }
        }
    }
}
