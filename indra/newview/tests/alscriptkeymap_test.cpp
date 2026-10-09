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
#include "../alscriptkeypresets.h"
// The settings the keymap is kept in, the viewer's group, which the test
// links (newview_test_settings.cpp).
#include "../llviewercontrol.h"

#include "llcontrol.h"
#include "llkeyboard.h"

#include "../test/lltut.h"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>

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
            if (!gSavedSettings.controlExists("ALScriptStudioKeymapPreset"))
            {
                gSavedSettings.declareString("ALScriptStudioKeymapPreset", "studio", "another editor's keys", LLControlVariable::PERSIST_NO);
            }
            ALScriptKeymap::setPreset(ALScriptKeyPresets::STANDARD);
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

    template<> template<>
    void alscriptkeymap_object::test<4>()
    {
        set_test_name("every item of the studio's menu bar can take keys: the editors' command, or one of the table's; named by where it is, and the only one so named");
        LLXMLNodePtr root;
        ensure("the studio's file", LLXMLNode::parseFile(std::string(LLUI_TEST_APP_DIR) + "/skins/default/xui/en/floater_script_studio.xml", root));
        const std::vector<ALScriptKeymap::MenuItem> items = ALScriptKeymap::menuItemsIn(root);
        ensure("its menus", items.size() > 50);
        std::set<std::string> ids;
        for (const ALScriptKeymap::MenuItem& item : items)
        {
            ensure("the only item so named: " + item.id, ids.insert(item.id).second);
            ensure("can take keys: " + item.id, alEditorCommandFromName(item.id).has_value() || ALScriptKeymap::isMenuCommand(item.id));
            ensure("named by its menus: " + item.id, item.path.find(" > ") != std::string::npos);
        }
        for (const std::string& id : ALScriptKeymap::menuIds())
        {
            ensure("the table's commands are the menus': " + id, ids.count(id) == 1);
        }
        const auto map = std::find_if(items.begin(), items.end(), [](const ALScriptKeymap::MenuItem& item) { return item.id == "map_left"; });
        ensure("as deep as it goes", map != items.end() && map->path == "View > Editor > Scrollbar > Map on the Left");
    }

    template<> template<>
    void alscriptkeymap_object::test<5>()
    {
        set_test_name("keys given take them from whatever had them: an editor's command by the first key, the menus' by the keys, their first, or the given's first");
        ALScriptKeymap::Owner revert;
        revert.menu = "revert";
        const auto named = [](const std::vector<ALScriptKeymap::Owner>& owners, const std::string& menu, C command = C::None) {
            return std::any_of(owners.begin(), owners.end(),
                               [&](const ALScriptKeymap::Owner& one) { return menu.empty() ? one.command == command : one.menu == menu; });
        };

        std::vector<ALScriptKeymap::Owner> from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'F', MASK_CONTROL }, false);
        ensure("Find's, the editor's", from.size() == 1 && named(from, "", C::Find));
        ensure("named, not taken", ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find);
        ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'F', MASK_CONTROL }, true);
        ensure("taken", ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::None);
        ALScriptKeymap::restoreAll();

        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'K', MASK_CONTROL }, true);
        ensure("every two keys it was the first of", named(from, "save_all") && named(from, "explorer") && named(from, "expanded"));
        ensure("which have none now", ALScriptKeymap::menuKeys("save_all").empty() && ALScriptKeymap::menuKeys("explorer").empty());
        ALScriptKeymap::restoreAll();

        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'X', MASK_NONE, 'P', MASK_CONTROL }, true);
        ensure("the key that is the first of the two given", from.size() == 1 && named(from, "quick_open"));
        ensure("which it had alone", ALScriptKeymap::menuKeys("quick_open").empty());
        ALScriptKeymap::restoreAll();

#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        ALScriptKeymap::chords_t rest = ALScriptKeymap::menuKeys("next_tab");
        rest.erase(std::remove(rest.begin(), rest.end(), ALKeyChord{ KEY_TAB, REAL_CONTROL }), rest.end());
        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ KEY_TAB, REAL_CONTROL }, true);
        ensure("one of several keys: the rest kept", named(from, "next_tab") && !rest.empty() && ALScriptKeymap::menuKeys("next_tab") == rest);
        ALScriptKeymap::restoreAll();

        ALScriptKeymap::Owner save_all;
        save_all.menu = "save_all";
        ensure("its own keys given again: nobody's", ALScriptKeymap::takeKeys(save_all, ALKeyChord{ 'S', MASK_NONE, 'K', MASK_CONTROL }, false).empty());
        ALScriptKeymap::Owner find;
        find.command = C::Find;
        ensure("nor an editor's own", ALScriptKeymap::takeKeys(find, ALKeyChord{ 'F', MASK_CONTROL }, false).empty());
    }

    template<> template<>
    void alscriptkeymap_object::test<6>()
    {
        set_test_name("the text's size on Control-=, Control-minus and Control-0, as everywhere; back and forward off them");
        const auto has = [](const char* id, const ALKeyChord& chord) {
            const ALScriptKeymap::chords_t keys = ALScriptKeymap::menuKeys(id);
            return std::find(keys.begin(), keys.end(), chord) != keys.end();
        };
        ensure("zoom in, the '=' key as Windows and SDL say it", has("zoom_in", ALKeyChord{ '=', MASK_CONTROL }) && has("zoom_in", ALKeyChord{ KEY_EQUALS, MASK_CONTROL }));
        ensure("and Control-plus", has("zoom_in", ALKeyChord{ '=', MASK_CONTROL | MASK_SHIFT }) && has("zoom_in", ALKeyChord{ KEY_ADD, MASK_CONTROL }));
        ensure("zoom out", has("zoom_out", ALKeyChord{ '-', MASK_CONTROL }));
        ensure("the size chosen", has("zoom_reset", ALKeyChord{ '0', MASK_CONTROL }));
        ensure("back not on Control-minus", !has("back", ALKeyChord{ '-', MASK_CONTROL }));
#if LL_DARWIN
        ensure("back on the Mac's Control-minus", has("back", ALKeyChord{ '-', MASK_MAC_CONTROL }));
#else
        ensure("back on Alt-Left", has("back", ALKeyChord{ KEY_LEFT, MASK_ALT }) && has("forward", ALKeyChord{ KEY_RIGHT, MASK_ALT }));
#endif
    }

    template<> template<>
    void alscriptkeymap_object::test<7>()
    {
        set_test_name("every preset names the studio's commands, each once, an editor's command with keys of one key each");
        ensure("the studio's own first, changing nothing", ALScriptKeyPresets::all().front().id == ALScriptKeyPresets::STANDARD &&
                                                           ALScriptKeyPresets::all().front().bindings.empty());
        ensure("four", ALScriptKeyPresets::all().size() == 4);
        for (const ALScriptKeyPresets::Preset& preset : ALScriptKeyPresets::all())
        {
            std::set<std::string> named;
            for (const ALScriptKeyPresets::Binding& binding : preset.bindings)
            {
                const std::string what = preset.id + ": " + binding.id;
                ensure("once: " + what, named.insert(binding.id).second);
                ensure("keys: " + what, !binding.keys.empty());
                const bool editor = alEditorCommandFromName(binding.id).has_value();
                ensure("a command: " + what, editor || ALScriptKeymap::isMenuCommand(binding.id));
                for (const ALKeyChord& chord : binding.keys)
                {
                    ensure("one key at a time for an editor's: " + what, !editor || !chord.twoKeys());
                }
            }
        }
    }

    template<> template<>
    void alscriptkeymap_object::test<8>()
    {
        set_test_name("with each preset in force, every key it gives is its command's, and no key is two commands'");
        for (const ALScriptKeyPresets::Preset& preset : ALScriptKeyPresets::all())
        {
            ALScriptKeymap::setPreset(preset.id);
            ensure_equals("in force", ALScriptKeymap::preset(), preset.id);
            const ALKeymap& map = ALScriptKeymap::current();
            for (const ALScriptKeyPresets::Binding& binding : preset.bindings)
            {
                const std::string what = preset.id + ": " + binding.id;
                if (const std::optional<ALEditorCommand> command = alEditorCommandFromName(binding.id))
                {
                    for (const ALKeyChord& chord : binding.keys)
                    {
                        ensure("the editor's key: " + what + " " + chord.describe(), map.lookup(chord.key, chord.mask) == *command);
                    }
                }
                else
                {
                    ensure("the menu's keys: " + what + " " + ALScriptKeymap::describe(ALScriptKeymap::menuKeys(binding.id)),
                           ALScriptKeymap::menuKeys(binding.id) == binding.keys);
                }
            }
            // No menu's key the first key of an editor's command, nor
            // another menu's, nor the first of another's two.
            for (const std::string& id : ALScriptKeymap::menuIds())
            {
                for (const ALKeyChord& chord : ALScriptKeymap::menuKeys(id))
                {
                    const std::string what = preset.id + ": " + id + " " + chord.describe();
                    const KEY         first_key  = chord.twoKeys() ? chord.leadKey : chord.key;
                    const MASK        first_mask = chord.twoKeys() ? chord.leadMask : chord.mask;
                    ensure("not an editor's key: " + what, map.lookup(first_key, first_mask) == ALEditorCommand::None);
                    for (const std::string& other : ALScriptKeymap::menuIds())
                    {
                        if (other == id)
                        {
                            continue;
                        }
                        for (const ALKeyChord& theirs : ALScriptKeymap::menuKeys(other))
                        {
                            const bool clash = theirs == chord || (!chord.twoKeys() && theirs.ledBy(chord.key, chord.mask)) ||
                                               (chord.twoKeys() && !theirs.twoKeys() && theirs.key == chord.leadKey && theirs.mask == chord.leadMask);
                            ensure("not " + other + "'s too: " + what, !clash);
                        }
                    }
                }
            }
        }
    }

    template<> template<>
    void alscriptkeymap_object::test<9>()
    {
        set_test_name("a person's own keys stay over another preset and win; restoring one goes back to the preset's, and all keeps the preset");
#if LL_DARWIN
        const std::string other_preset = "xcode";
#else
        const std::string other_preset = "visual_studio";
#endif
        ALScriptKeymap::setPreset("jetbrains");
        ensure("JetBrains' Control-D duplicates", ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::DuplicateLine);
        ALScriptKeymap::takeKeys(ALScriptKeymap::Owner{ C::JoinLines, "" }, ALKeyChord{ 'D', MASK_CONTROL }, true);
        ALScriptKeymap::rebind(C::JoinLines, { { 'D', MASK_CONTROL } });
        ensure("taken", ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::JoinLines && ALScriptKeymap::isRebound(C::JoinLines));
        ALScriptKeymap::setPreset(other_preset);
        ensure("kept over another preset", ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::JoinLines);
        ensure("and the person's still", ALScriptKeymap::anyRebound());
        ALScriptKeymap::restore(C::JoinLines);
        ALScriptKeymap::restore(C::DuplicateLine);
        ensure("restored: the preset's own", ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::DuplicateLine);
        ALScriptKeymap::restoreAll();
        ensure("all restored, the preset kept", ALScriptKeymap::preset() == other_preset && !ALScriptKeymap::anyRebound());
        ALScriptKeymap::setPreset("nothing of the kind");
        ensure("one not offered: the standard", ALScriptKeymap::preset() == ALScriptKeyPresets::STANDARD);
    }

    template<> template<>
    void alscriptkeymap_object::test<10>()
    {
        set_test_name("what the comparison's bar does has keys too, the first letter of each word; peeking at a change on Alt-F3, beside Alt-F5's step");
        const std::pair<const char*, ALKeyChord> keys[] = {
            { "compare_settle_theirs", ALKeyChord{ 'T', MASK_ALT } }, { "compare_settle_mine", ALKeyChord{ 'M', MASK_ALT } },
            { "compare_settle_both", ALKeyChord{ 'B', MASK_ALT } },   { "compare_fold", ALKeyChord{ 'F', MASK_ALT } },
            { "compare_swap", ALKeyChord{ 'S', MASK_ALT } },          { "compare_older", ALKeyChord{ ',', MASK_ALT } },
            { "compare_newer", ALKeyChord{ '.', MASK_ALT } },         { "peek_change", ALKeyChord{ KEY_F3, MASK_ALT } },
        };
        for (const auto& [id, chord] : keys)
        {
            ensure(std::string("a menu command: ") + id, ALScriptKeymap::isMenuCommand(id));
            ensure(std::string("its key: ") + id + " " + chord.describe(), ALScriptKeymap::menuKey(id) == chord);
            ensure(std::string("no editor's: ") + id, ALScriptKeymap::current().lookup(chord.key, chord.mask) == C::None);
        }
        ensure("the step beside it", ALScriptKeymap::menuKey("next_difference") == (ALKeyChord{ KEY_F5, MASK_ALT }));
    }

    template<> template<>
    void alscriptkeymap_object::test<11>()
    {
        set_test_name("keys kept for a menu's command the table no longer has, renamed or gone, take nothing from the commands that have them");
        // Go > Next Change was next_change until it was told from Next Edit
        // Location, the editor's command of that name.
        ensure("no menu command now", !ALScriptKeymap::isMenuCommand("next_change"));
        ALScriptKeymap::rebindMenu("next_change", { ALKeyChord{ KEY_F5, MASK_ALT }, ALKeyChord{ KEY_F8, MASK_NONE } });
        ensure("the step's key its own still", ALScriptKeymap::menuKey("next_difference") == (ALKeyChord{ KEY_F5, MASK_ALT }));
        ensure("and a problem's", has(ALScriptKeymap::menuKeys("next_problem"), ALKeyChord{ KEY_F8, MASK_NONE }));
        ensure("the editor's command of that name untouched", ALScriptKeymap::keysOf(ALScriptKeymap::current(), C::NextChange).empty() ==
                                                                  ALScriptKeymap::keysOf(ALKeymap::standard(), C::NextChange).empty());
        ALScriptKeymap::restoreAll();
    }
}
