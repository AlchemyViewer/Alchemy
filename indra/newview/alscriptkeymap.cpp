/**
 * @file alscriptkeymap.cpp
 * @brief The keys Script Studio's editors answer to: the standard keymap with a person's changes over it, kept in a setting.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptkeymap.h"

#include "llkeyboard.h"
#include "llviewercontrol.h"

#include <algorithm>

namespace
{
    const char* const SETTING = "ALScriptStudioKeymap";
    // A menu item's key is kept under its name after this, beside the
    // editor's commands, which have no such prefix.
    const std::string MENU_PREFIX = "menu:";

    // The setting: a map from a command's name to its keys, each a map
    // of key and mask.
    LLSD rebound() { return gSavedSettings.getLLSD(SETTING); }

    void store(const LLSD& map) { gSavedSettings.setLLSD(SETTING, map); }

    ALScriptKeymap::keys_t keysFrom(const LLSD& list)
    {
        ALScriptKeymap::keys_t keys;
        for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
        {
            keys.emplace_back(static_cast<KEY>((*it)["key"].asInteger()), static_cast<MASK>((*it)["mask"].asInteger()));
        }
        return keys;
    }

    // A menu item's key as the setting keeps it: the list's first, and
    // the key pressed before it where it is two. None for an empty list.
    ALKeyChord chordFrom(const LLSD& list)
    {
        if (!list.isArray() || list.size() == 0)
        {
            return {};
        }
        const LLSD& one   = list[0];
        ALKeyChord  chord = { static_cast<KEY>(one["key"].asInteger()), static_cast<MASK>(one["mask"].asInteger()) };
        if (one.has("lead_key"))
        {
            chord.leadKey  = static_cast<KEY>(one["lead_key"].asInteger());
            chord.leadMask = static_cast<MASK>(one["lead_mask"].asInteger());
        }
        return chord;
    }

    LLSD chordTo(const ALKeyChord& chord)
    {
        LLSD list = LLSD::emptyArray();
        if (chord.none())
        {
            return list;
        }
        LLSD one;
        one["key"]  = static_cast<S32>(chord.key);
        one["mask"] = static_cast<S32>(chord.mask);
        if (chord.twoKeys())
        {
            one["lead_key"]  = static_cast<S32>(chord.leadKey);
            one["lead_mask"] = static_cast<S32>(chord.leadMask);
        }
        list.append(one);
        return list;
    }

    LLSD keysTo(const ALScriptKeymap::keys_t& keys)
    {
        LLSD list = LLSD::emptyArray();
        for (const auto& [key, mask] : keys)
        {
            LLSD one;
            one["key"]  = static_cast<S32>(key);
            one["mask"] = static_cast<S32>(mask);
            list.append(one);
        }
        return list;
    }
}

namespace ALScriptKeymap
{
    ALKeymap current()
    {
        ALKeymap   map   = ALKeymap::standard();
        const LLSD bound = rebound();
        if (!bound.isMap())
        {
            return map;
        }
        for (LLSD::map_const_iterator it = bound.beginMap(); it != bound.endMap(); ++it)
        {
            const std::optional<ALEditorCommand> command = alEditorCommandFromName(it->first);
            if (!command)
            {
                continue;
            }
            for (const auto& [key, mask] : keysOf(map, *command))
            {
                map.unbind(key, mask);
            }
            for (const auto& [key, mask] : keysFrom(it->second))
            {
                map.bind(key, mask, *command);
            }
        }
        return map;
    }

    keys_t keysOf(const ALKeymap& map, ALEditorCommand command)
    {
        keys_t keys;
        for (const ALKeymap::Binding& binding : map.bindings())
        {
            if (binding.command == command)
            {
                keys.emplace_back(binding.key, binding.mask);
            }
        }
        return keys;
    }

    bool isRebound(ALEditorCommand command)
    {
        const LLSD bound = rebound();
        return bound.isMap() && bound.has(alEditorCommandName(command));
    }

    void rebind(ALEditorCommand command, const keys_t& keys)
    {
        LLSD bound = rebound();
        if (!bound.isMap())
        {
            bound = LLSD::emptyMap();
        }
        bound[alEditorCommandName(command)] = keysTo(keys);
        store(bound);
    }

    void restore(ALEditorCommand command)
    {
        LLSD bound = rebound();
        if (bound.isMap() && bound.has(alEditorCommandName(command)))
        {
            bound.erase(alEditorCommandName(command));
            store(bound);
        }
    }

    void restoreAll() { store(LLSD::emptyMap()); }

    const std::vector<MenuCommand>& menuCommands()
    {
        // As the studio's menus give them, which this table overrides, so
        // that the two cannot drift: the menus are told these at every
        // open. Back and forward are the Control key's on a Mac too, as
        // an editor of code has them there, Command-minus being the
        // view's zoom elsewhere.
#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        // Control-K, then a key: for what would otherwise hold Control
        // and Alt together, which is AltGr on many a keyboard -- a German
        // } or a Polish s typed would fold a pane or save every script.
        const auto after_k = [](const char* id, KEY key) { return MenuCommand{ id, key, MASK_NONE, true, 'K', MASK_CONTROL }; };
        static const std::vector<MenuCommand> commands{
            { "new_script", 'N', MASK_CONTROL },
            { "new_lua_script", KEY_NONE, MASK_NONE },
            { "save", 'S', MASK_CONTROL },
            after_k("save_all", 'S'),
            { "revert", KEY_NONE, MASK_NONE },
            { "open_file", KEY_NONE, MASK_NONE },
            { "close", 'W', MASK_CONTROL },
            { "close_others", KEY_NONE, MASK_NONE },
            { "close_saved", KEY_NONE, MASK_NONE },
            { "close_all", KEY_NONE, MASK_NONE },
            { "insert_file", KEY_NONE, MASK_NONE },
            { "preferences", KEY_NONE, MASK_NONE },
            { "format", 'F', MASK_SHIFT | MASK_ALT },
            { "indent_spaces", KEY_NONE, MASK_NONE },
            { "indent_tabs", KEY_NONE, MASK_NONE },
            { "find_in_files", 'F', MASK_CONTROL | MASK_SHIFT },
            { "insert_snippet", 'I', MASK_CONTROL | MASK_SHIFT },
            { "back", '-', REAL_CONTROL },
            { "forward", '-', REAL_CONTROL | MASK_SHIFT },
            { "go_to_line", 'G', MASK_CONTROL },
            { "go_to_symbol", 'O', MASK_CONTROL | MASK_SHIFT },
            { "next_problem", KEY_F8, MASK_NONE },
            { "previous_problem", KEY_F8, MASK_SHIFT },
            { "next_tab", KEY_PAGE_DOWN, MASK_CONTROL },
            { "previous_tab", KEY_PAGE_UP, MASK_CONTROL },
            { "last_tab", '6', REAL_CONTROL },
            { "all_tabs", KEY_NONE, MASK_NONE },
            { "move_tab_left", KEY_PAGE_UP, MASK_CONTROL | MASK_SHIFT },
            { "move_tab_right", KEY_PAGE_DOWN, MASK_CONTROL | MASK_SHIFT },
            { "focus_tabs", KEY_NONE, MASK_NONE },
            { "command_palette", 'P', MASK_CONTROL | MASK_SHIFT },
            { "quick_open", 'P', MASK_CONTROL },
            after_k("explorer", '0'),
            after_k("problems", '1'),
            after_k("references", '2'),
            after_k("output", '3'),
            after_k("inspector", '4'),
            after_k("search", '5'),
            after_k("weights", '6'),
            after_k("expanded", 'P'),
            { "preprocess", KEY_NONE, MASK_NONE },
            { "reference", KEY_F1, MASK_NONE },
        };
        return commands;
    }

    bool isMenuCommand(std::string_view item)
    {
        const std::vector<MenuCommand>& all = menuCommands();
        return std::any_of(all.begin(), all.end(), [item](const MenuCommand& one) { return item == one.id; });
    }

    ALKeyChord menuKey(std::string_view item)
    {
        const LLSD bound = rebound();
        const std::string name = MENU_PREFIX + std::string(item);
        if (bound.isMap() && bound.has(name))
        {
            return chordFrom(bound[name]);
        }
        for (const MenuCommand& one : menuCommands())
        {
            if (item == one.id)
            {
                return one.chord();
            }
        }
        return {};
    }

    bool isMenuRebound(std::string_view item)
    {
        const LLSD bound = rebound();
        return bound.isMap() && bound.has(MENU_PREFIX + std::string(item));
    }

    void rebindMenu(std::string_view item, const ALKeyChord& chord)
    {
        LLSD bound = rebound();
        if (!bound.isMap())
        {
            bound = LLSD::emptyMap();
        }
        bound[MENU_PREFIX + std::string(item)] = chordTo(chord);
        store(bound);
    }

    void restoreMenu(std::string_view item)
    {
        LLSD bound = rebound();
        const std::string name = MENU_PREFIX + std::string(item);
        if (bound.isMap() && bound.has(name))
        {
            bound.erase(name);
            store(bound);
        }
    }

    std::string describe(const keys_t& keys)
    {
        std::string text;
        for (const auto& [key, mask] : keys)
        {
            if (!text.empty())
            {
                text += ", ";
            }
            text += LLKeyboard::stringFromAccelerator(mask, key);
        }
        return text;
    }
}
