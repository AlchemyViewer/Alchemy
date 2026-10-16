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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>

namespace
{
    // What names keys as a person reads them, which LLKeyboard keeps
    // without saying.
    struct KeyTranslator : LLKeyboard
    {
        static LLKeyStringTranslatorFunc* current() { return mStringTranslator; }
    };
}

namespace
{
    struct alscriptkeymap_data
    {
        typedef ALEditorCommand C;

        // What the fixture sets, as it was: put back as each test ends, for
        // whatever runs after it.
        LLKeyStringTranslatorFunc* translatorWas = KeyTranslator::current();
        LLSD                       keysWas;
        std::string                presetWas;

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
            keysWas   = gSavedSettings.getLLSD("ALScriptStudioKeymap");
            presetWas = gSavedSettings.getString("ALScriptStudioKeymapPreset");
            ALScriptKeymap::setPreset(ALScriptKeyPresets::STANDARD);
            ALScriptKeymap::restoreAll();
        }
        ~alscriptkeymap_data()
        {
            // The keymap hears its settings change, and builds again from
            // them when next asked.
            gSavedSettings.setString("ALScriptStudioKeymapPreset", presetWas);
            gSavedSettings.setLLSD("ALScriptStudioKeymap", keysWas);
            LLKeyboard::setStringTranslatorFunc(translatorWas);
        }

        static bool has(const ALScriptKeymap::chords_t& keys, const ALKeyChord& chord)
        {
            return std::find(keys.begin(), keys.end(), chord) != keys.end();
        }
    };

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: a person's editor keys replace the standard's and follow the setting", "[alscriptkeymap]")
    {
        const ALKeymap* first = &ALScriptKeymap::current();
        UNSCOPED_INFO("the standard's");
        CHECK(first->lookup('F', MASK_CONTROL) == C::Find);
        UNSCOPED_INFO("the same keymap asked again");
        CHECK(&ALScriptKeymap::current() == first);
        ALScriptKeymap::rebind(C::Find, { { 'Y', MASK_CONTROL | MASK_SHIFT } });
        UNSCOPED_INFO("the new key");
        CHECK(ALScriptKeymap::current().lookup('Y', MASK_CONTROL | MASK_SHIFT) == C::Find);
        UNSCOPED_INFO("the old one gone");
        CHECK(ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::None);
        {
            INFO("said rebound");
            CHECK(ALScriptKeymap::isRebound(C::Find));
            CHECK_FALSE(ALScriptKeymap::isRebound(C::Replace));
        }
        // Set by anything else -- the preferences' Cancel putting it back.
        gSavedSettings.setLLSD("ALScriptStudioKeymap", LLSD::emptyMap());
        UNSCOPED_INFO("the standard's again");
        CHECK(ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find);
        ALScriptKeymap::rebind(C::Find, {});
        UNSCOPED_INFO("none: unbound");
        REQUIRE(ALScriptKeymap::keysOf(ALScriptKeymap::current(), C::Find).empty());
        ALScriptKeymap::restore(C::Find);
        {
            INFO("restored");
            CHECK(ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find);
            CHECK_FALSE(ALScriptKeymap::isRebound(C::Find));
        }
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: menu keys may be several or two in turn and restore to the standard's", "[alscriptkeymap]")
    {
#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        const ALScriptKeymap::chords_t next = ALScriptKeymap::menuKeys("next_tab");
        {
            INFO("next tab: Control-PgDn, which the menu shows");
            REQUIRE_FALSE(next.empty());
            CHECK(next.front() == ALKeyChord{ KEY_PAGE_DOWN, MASK_CONTROL });
        }
        UNSCOPED_INFO("and Control-Tab");
        CHECK(has(next, ALKeyChord{ KEY_TAB, REAL_CONTROL }));
        UNSCOPED_INFO("save all: Control-K, then S");
        CHECK(ALScriptKeymap::menuKey("save_all") == (ALKeyChord{ 'S', MASK_NONE, 'K', MASK_CONTROL }));
        {
            INFO("a command with none");
            REQUIRE(ALScriptKeymap::menuKeys("revert").empty());
            CHECK(ALScriptKeymap::isMenuCommand("revert"));
        }
        {
            INFO("no such command");
            CHECK_FALSE(ALScriptKeymap::isMenuCommand("frobnicate"));
            REQUIRE(ALScriptKeymap::menuKeys("frobnicate").empty());
        }

        const ALKeyChord two{ 'T', MASK_NONE, 'K', MASK_CONTROL };
        ALScriptKeymap::rebindMenu("revert", { two, ALKeyChord{ KEY_F9, MASK_SHIFT } });
        const ALScriptKeymap::chords_t mine = ALScriptKeymap::menuKeys("revert");
        {
            INFO("kept, two in turn among them");
            REQUIRE(mine.size() == 2);
            CHECK(mine[0] == two);
            CHECK(mine[1] == (ALKeyChord{ KEY_F9, MASK_SHIFT }));
        }
        UNSCOPED_INFO("said rebound");
        CHECK(ALScriptKeymap::isMenuRebound("revert"));
        ALScriptKeymap::rebindMenu("next_tab", {});
        {
            INFO("none at all, in place of the standard's");
            REQUIRE(ALScriptKeymap::menuKeys("next_tab").empty());
            CHECK(ALScriptKeymap::menuKey("next_tab").none());
        }
        ALScriptKeymap::restoreMenu("next_tab");
        {
            INFO("the standard's again");
            CHECK(ALScriptKeymap::menuKeys("next_tab") == next);
            CHECK_FALSE(ALScriptKeymap::isMenuRebound("next_tab"));
        }
        ALScriptKeymap::restoreAll();
        UNSCOPED_INFO("all of them");
        REQUIRE(ALScriptKeymap::menuKeys("revert").empty());
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: the menus' table has unique ids and keys and no Control-Alt typing key", "[alscriptkeymap]")
    {
        const std::vector<std::string>& ids = ALScriptKeymap::menuIds();
        UNSCOPED_INFO("each once");
        REQUIRE(std::set<std::string>(ids.begin(), ids.end()).size() == ids.size());
        std::set<std::tuple<KEY, MASK, KEY, MASK>> seen;
        for (const std::string& id : ids)
        {
            for (const ALKeyChord& chord : ALScriptKeymap::menuKeys(id))
            {
                UNSCOPED_INFO("one command's key: " << id << " " << chord.describe());
                REQUIRE(seen.insert({ chord.key, chord.mask, chord.leadKey, chord.leadMask }).second);
                for (const auto& [key, mask] : { std::pair{ chord.key, chord.mask }, std::pair{ chord.leadKey, chord.leadMask } })
                {
                    const bool types = key >= 0x20 && key < 0x80;
                    UNSCOPED_INFO("no Control-Alt: " << id);
                    REQUIRE((!types || (mask & (MASK_CONTROL | MASK_ALT)) != (MASK_CONTROL | MASK_ALT)));
                }
            }
        }
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: every studio menu bar item can take keys and has a unique id and a path", "[alscriptkeymap]")
    {
        LLXMLNodePtr root;
        UNSCOPED_INFO("the studio's file");
        REQUIRE(LLXMLNode::parseFile(std::string(LLUI_TEST_APP_DIR) + "/skins/default/xui/en/floater_script_studio.xml", root));
        const std::vector<ALScriptKeymap::MenuItem> items = ALScriptKeymap::menuItemsIn(root);
        UNSCOPED_INFO("its menus");
        REQUIRE(items.size() > 50);
        std::set<std::string> ids;
        for (const ALScriptKeymap::MenuItem& item : items)
        {
            UNSCOPED_INFO("the only item so named: " << item.id);
            REQUIRE(ids.insert(item.id).second);
            UNSCOPED_INFO("can take keys: " << item.id);
            REQUIRE((alEditorCommandFromName(item.id).has_value() || ALScriptKeymap::isMenuCommand(item.id)));
            UNSCOPED_INFO("named by its menus: " << item.id);
            REQUIRE(item.path.find(" > ") != std::string::npos);
        }
        for (const std::string& id : ALScriptKeymap::menuIds())
        {
            UNSCOPED_INFO("the table's commands are the menus': " << id);
            REQUIRE(ids.count(id) == 1);
        }
        const auto map = std::find_if(items.begin(), items.end(), [](const ALScriptKeymap::MenuItem& item) { return item.id == "map_left"; });
        {
            INFO("as deep as it goes");
            REQUIRE(map != items.end());
            CHECK(map->path == "View > Editor > Scrollbar > Map on the Left");
        }
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: takeKeys takes a chord from whichever commands held it", "[alscriptkeymap]")
    {
        ALScriptKeymap::Owner revert;
        revert.menu = "revert";
        const auto named = [](const std::vector<ALScriptKeymap::Owner>& owners, const std::string& menu, C command = C::None) {
            return std::any_of(owners.begin(), owners.end(),
                               [&](const ALScriptKeymap::Owner& one) { return menu.empty() ? one.command == command : one.menu == menu; });
        };

        std::vector<ALScriptKeymap::Owner> from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'F', MASK_CONTROL }, false);
        {
            INFO("Find's, the editor's");
            REQUIRE(from.size() == 1);
            CHECK(named(from, "", C::Find));
        }
        UNSCOPED_INFO("named, not taken");
        CHECK(ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::Find);
        ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'F', MASK_CONTROL }, true);
        UNSCOPED_INFO("taken");
        CHECK(ALScriptKeymap::current().lookup('F', MASK_CONTROL) == C::None);
        ALScriptKeymap::restoreAll();

        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'K', MASK_CONTROL }, true);
        {
            INFO("every two keys it was the first of");
            CHECK(named(from, "save_all"));
            CHECK(named(from, "explorer"));
            CHECK(named(from, "expanded"));
        }
        {
            INFO("which have none now");
            REQUIRE(ALScriptKeymap::menuKeys("save_all").empty());
            REQUIRE(ALScriptKeymap::menuKeys("explorer").empty());
        }
        ALScriptKeymap::restoreAll();

        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ 'X', MASK_NONE, 'P', MASK_CONTROL }, true);
        {
            INFO("the key that is the first of the two given");
            REQUIRE(from.size() == 1);
            CHECK(named(from, "quick_open"));
        }
        UNSCOPED_INFO("which it had alone");
        REQUIRE(ALScriptKeymap::menuKeys("quick_open").empty());
        ALScriptKeymap::restoreAll();

#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        ALScriptKeymap::chords_t rest = ALScriptKeymap::menuKeys("next_tab");
        rest.erase(std::remove(rest.begin(), rest.end(), ALKeyChord{ KEY_TAB, REAL_CONTROL }), rest.end());
        from = ALScriptKeymap::takeKeys(revert, ALKeyChord{ KEY_TAB, REAL_CONTROL }, true);
        {
            INFO("one of several keys: the rest kept");
            CHECK(named(from, "next_tab"));
            REQUIRE_FALSE(rest.empty());
            CHECK(ALScriptKeymap::menuKeys("next_tab") == rest);
        }
        ALScriptKeymap::restoreAll();

        ALScriptKeymap::Owner save_all;
        save_all.menu = "save_all";
        UNSCOPED_INFO("its own keys given again: nobody's");
        REQUIRE(ALScriptKeymap::takeKeys(save_all, ALKeyChord{ 'S', MASK_NONE, 'K', MASK_CONTROL }, false).empty());
        ALScriptKeymap::Owner find;
        find.command = C::Find;
        UNSCOPED_INFO("nor an editor's own");
        REQUIRE(ALScriptKeymap::takeKeys(find, ALKeyChord{ 'F', MASK_CONTROL }, false).empty());
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: the zoom keys are the usual Control ones and back and forward avoid them", "[alscriptkeymap]")
    {
        const auto has = [](const char* id, const ALKeyChord& chord) {
            const ALScriptKeymap::chords_t keys = ALScriptKeymap::menuKeys(id);
            return std::find(keys.begin(), keys.end(), chord) != keys.end();
        };
        {
            INFO("zoom in, the '=' key as Windows and SDL say it");
            CHECK(has("zoom_in", ALKeyChord{ '=', MASK_CONTROL }));
            CHECK(has("zoom_in", ALKeyChord{ KEY_EQUALS, MASK_CONTROL }));
        }
        {
            INFO("and Control-plus");
            CHECK(has("zoom_in", ALKeyChord{ '=', MASK_CONTROL | MASK_SHIFT }));
            CHECK(has("zoom_in", ALKeyChord{ KEY_ADD, MASK_CONTROL }));
        }
        UNSCOPED_INFO("zoom out");
        CHECK(has("zoom_out", ALKeyChord{ '-', MASK_CONTROL }));
        UNSCOPED_INFO("the size chosen");
        CHECK(has("zoom_reset", ALKeyChord{ '0', MASK_CONTROL }));
        UNSCOPED_INFO("back not on Control-minus");
        CHECK_FALSE(has("back", ALKeyChord{ '-', MASK_CONTROL }));
#if LL_DARWIN
        UNSCOPED_INFO("back on the Mac's Control-minus");
        CHECK(has("back", ALKeyChord{ '-', MASK_MAC_CONTROL }));
#else
        UNSCOPED_INFO("back on Alt-Left");
        CHECK(has("back", ALKeyChord{ KEY_LEFT, MASK_ALT }));
        UNSCOPED_INFO("forward on Alt-Right");
        CHECK(has("forward", ALKeyChord{ KEY_RIGHT, MASK_ALT }));
#endif
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: every preset binds studio commands once and editor ones to single keys", "[alscriptkeymap]")
    {
        {
            INFO("the studio's own first, changing nothing");
            CHECK(ALScriptKeyPresets::all().front().id == ALScriptKeyPresets::STANDARD);
            REQUIRE(ALScriptKeyPresets::all().front().bindings.empty());
        }
        REQUIRE(ALScriptKeyPresets::all().size() == 4);
        for (const ALScriptKeyPresets::Preset& preset : ALScriptKeyPresets::all())
        {
            std::set<std::string> named;
            for (const ALScriptKeyPresets::Binding& binding : preset.bindings)
            {
                const std::string what = preset.id + ": " + binding.id;
                UNSCOPED_INFO("once: " << what);
                REQUIRE(named.insert(binding.id).second);
                UNSCOPED_INFO("keys: " << what);
                REQUIRE_FALSE(binding.keys.empty());
                const bool editor = alEditorCommandFromName(binding.id).has_value();
                UNSCOPED_INFO("a command: " << what);
                REQUIRE((editor || ALScriptKeymap::isMenuCommand(binding.id)));
                for (const ALKeyChord& chord : binding.keys)
                {
                    UNSCOPED_INFO("one key at a time for an editor's: " << what);
                    REQUIRE((!editor || !chord.twoKeys()));
                }
            }
        }
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: under each preset every key reaches its command and none is shared", "[alscriptkeymap]")
    {
        for (const ALScriptKeyPresets::Preset& preset : ALScriptKeyPresets::all())
        {
            ALScriptKeymap::setPreset(preset.id);
            UNSCOPED_INFO("in force");
            REQUIRE(ALScriptKeymap::preset() == preset.id);
            const ALKeymap& map = ALScriptKeymap::current();
            for (const ALScriptKeyPresets::Binding& binding : preset.bindings)
            {
                const std::string what = preset.id + ": " + binding.id;
                if (const std::optional<ALEditorCommand> command = alEditorCommandFromName(binding.id))
                {
                    for (const ALKeyChord& chord : binding.keys)
                    {
                        UNSCOPED_INFO("the editor's key: " << what << " " << chord.describe());
                        REQUIRE(map.lookup(chord.key, chord.mask) == *command);
                    }
                }
                else
                {
                    UNSCOPED_INFO("the menu's keys: " << what << " " << ALScriptKeymap::describe(ALScriptKeymap::menuKeys(binding.id)));
                    REQUIRE(ALScriptKeymap::menuKeys(binding.id) == binding.keys);
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
                    UNSCOPED_INFO("not an editor's key: " << what);
                    REQUIRE(map.lookup(first_key, first_mask) == ALEditorCommand::None);
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
                            UNSCOPED_INFO("not " << other << "'s too: " << what);
                            REQUIRE_FALSE(clash);
                        }
                    }
                }
            }
        }
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: a person's keys win over any preset and restoring keeps the preset", "[alscriptkeymap]")
    {
#if LL_DARWIN
        const std::string other_preset = "xcode";
#else
        const std::string other_preset = "visual_studio";
#endif
        ALScriptKeymap::setPreset("jetbrains");
        UNSCOPED_INFO("JetBrains' Control-D duplicates");
        CHECK(ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::DuplicateLine);
        ALScriptKeymap::takeKeys(ALScriptKeymap::Owner{ C::JoinLines, "" }, ALKeyChord{ 'D', MASK_CONTROL }, true);
        ALScriptKeymap::rebind(C::JoinLines, { { 'D', MASK_CONTROL } });
        {
            INFO("taken");
            CHECK(ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::JoinLines);
            CHECK(ALScriptKeymap::isRebound(C::JoinLines));
        }
        ALScriptKeymap::setPreset(other_preset);
        UNSCOPED_INFO("kept over another preset");
        CHECK(ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::JoinLines);
        UNSCOPED_INFO("and the person's still");
        CHECK(ALScriptKeymap::anyRebound());
        ALScriptKeymap::restore(C::JoinLines);
        ALScriptKeymap::restore(C::DuplicateLine);
        UNSCOPED_INFO("restored: the preset's own");
        CHECK(ALScriptKeymap::current().lookup('D', MASK_CONTROL) == C::DuplicateLine);
        ALScriptKeymap::restoreAll();
        {
            INFO("all restored, the preset kept");
            CHECK(ALScriptKeymap::preset() == other_preset);
            CHECK_FALSE(ALScriptKeymap::anyRebound());
        }
        ALScriptKeymap::setPreset("nothing of the kind");
        UNSCOPED_INFO("one not offered: the standard");
        CHECK(ALScriptKeymap::preset() == ALScriptKeyPresets::STANDARD);
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: comparison bar commands are Alt with a first letter and peeking is Alt-F3", "[alscriptkeymap]")
    {
        const std::pair<const char*, ALKeyChord> keys[] = {
            { "compare_settle_theirs", ALKeyChord{ 'T', MASK_ALT } }, { "compare_settle_mine", ALKeyChord{ 'M', MASK_ALT } },
            { "compare_settle_both", ALKeyChord{ 'B', MASK_ALT } },   { "compare_fold", ALKeyChord{ 'F', MASK_ALT } },
            { "compare_swap", ALKeyChord{ 'S', MASK_ALT } },          { "compare_older", ALKeyChord{ ',', MASK_ALT } },
            { "compare_newer", ALKeyChord{ '.', MASK_ALT } },         { "peek_change", ALKeyChord{ KEY_F3, MASK_ALT } },
        };
        for (const auto& [id, chord] : keys)
        {
            UNSCOPED_INFO("a menu command: " << id);
            REQUIRE(ALScriptKeymap::isMenuCommand(id));
            UNSCOPED_INFO("its key: " << id << " " << chord.describe());
            REQUIRE(ALScriptKeymap::menuKey(id) == chord);
            UNSCOPED_INFO("no editor's: " << id);
            REQUIRE(ALScriptKeymap::current().lookup(chord.key, chord.mask) == C::None);
        }
        UNSCOPED_INFO("the step beside it");
        CHECK(ALScriptKeymap::menuKey("next_difference") == (ALKeyChord{ KEY_F5, MASK_ALT }));
    }

    TEST_CASE_METHOD(alscriptkeymap_data, "alscriptkeymap: keys kept for a menu command now gone take nothing from others", "[alscriptkeymap]")
    {
        // Go > Next Change was next_change until it was told from Next Edit
        // Location, the editor's command of that name.
        UNSCOPED_INFO("no menu command now");
        CHECK_FALSE(ALScriptKeymap::isMenuCommand("next_change"));
        ALScriptKeymap::rebindMenu("next_change", { ALKeyChord{ KEY_F5, MASK_ALT }, ALKeyChord{ KEY_F8, MASK_NONE } });
        UNSCOPED_INFO("the step's key its own still");
        CHECK(ALScriptKeymap::menuKey("next_difference") == (ALKeyChord{ KEY_F5, MASK_ALT }));
        UNSCOPED_INFO("and a problem's");
        CHECK(has(ALScriptKeymap::menuKeys("next_problem"), ALKeyChord{ KEY_F8, MASK_NONE }));
        UNSCOPED_INFO("the editor's command of that name untouched");
        CHECK(ALScriptKeymap::keysOf(ALScriptKeymap::current(), C::NextChange) ==
              ALScriptKeymap::keysOf(ALKeymap::standard(), C::NextChange));
        ALScriptKeymap::restoreAll();
    }
}
