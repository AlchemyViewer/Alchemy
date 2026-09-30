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

#include "alscriptkeypresets.h"
#include "llkeyboard.h"
#include "llstl.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>

namespace
{
    const char* const SETTING = "ALScriptStudioKeymap";
    // The editor whose keys are put over the standard, and the person's
    // over them (ALScriptKeyPresets).
    const char* const PRESET_SETTING = "ALScriptStudioKeymapPreset";
    // A menu item's key is kept under its name after this, beside the
    // editor's commands, which have no such prefix.
    const std::string MENU_PREFIX = "menu:";

    // The setting: a map from a command's name to its keys, each a map
    // of key and mask, and of the key before it for two.
    const LLSD& rebound();

    void store(const LLSD& map);

    ALScriptKeymap::keys_t keysFrom(const LLSD& list)
    {
        ALScriptKeymap::keys_t keys;
        for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
        {
            keys.emplace_back(static_cast<KEY>((*it)["key"].asInteger()), static_cast<MASK>((*it)["mask"].asInteger()));
        }
        return keys;
    }

    // A menu item's keys as the setting keeps them, each with the key
    // pressed before it where it is two.
    ALScriptKeymap::chords_t chordsFrom(const LLSD& list)
    {
        ALScriptKeymap::chords_t chords;
        for (LLSD::array_const_iterator it = list.beginArray(); it != list.endArray(); ++it)
        {
            ALKeyChord chord = { static_cast<KEY>((*it)["key"].asInteger()), static_cast<MASK>((*it)["mask"].asInteger()) };
            if (it->has("lead_key"))
            {
                chord.leadKey  = static_cast<KEY>((*it)["lead_key"].asInteger());
                chord.leadMask = static_cast<MASK>((*it)["lead_mask"].asInteger());
            }
            if (!chord.none())
            {
                chords.push_back(chord);
            }
        }
        return chords;
    }

    LLSD chordsTo(const ALScriptKeymap::chords_t& chords)
    {
        LLSD list = LLSD::emptyArray();
        for (const ALKeyChord& chord : chords)
        {
            if (chord.none())
            {
                continue;
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
        }
        return list;
    }

    ALKeymap build(const LLSD& bound);
    LLSD     effectiveOf(const std::string& preset, const LLSD& bound);

    // The settings, and the keymap they make, read again only once one has
    // changed -- by a key given here, or by anything else that sets it.
    // Every editor is given the keymap, and the menus ask each of their
    // commands for its keys at every key pressed. `bound` is the person's
    // own; `effective` the standard's changed by the preset's and then the
    // person's, each over the one before (effectiveOf).
    struct Built
    {
        LLSD                               bound;
        std::string                        preset = ALScriptKeyPresets::STANDARD;
        LLSD                               effective;
        ALKeymap                           map;
        bool                               stale = true;
        boost::signals2::scoped_connection listening;
        boost::signals2::scoped_connection listeningPreset;
    };
    Built& built()
    {
        static Built one;
        if (!one.listening.connected())
        {
            if (LLControlVariable* control = gSavedSettings.getControl(SETTING))
            {
                one.listening = control->getSignal()->connect([](LLControlVariable*, const LLSD&, const LLSD&) { built().stale = true; });
            }
        }
        if (!one.listeningPreset.connected())
        {
            if (LLControlVariable* control = gSavedSettings.getControl(PRESET_SETTING))
            {
                one.listeningPreset = control->getSignal()->connect([](LLControlVariable*, const LLSD&, const LLSD&) { built().stale = true; });
            }
        }
        if (one.stale)
        {
            one.bound = gSavedSettings.getLLSD(SETTING);
            // One this platform does not offer, or none: the standard.
            const std::string preset = gSavedSettings.controlExists(PRESET_SETTING) ? gSavedSettings.getString(PRESET_SETTING) : std::string();
            one.preset    = ALScriptKeyPresets::find(preset) ? preset : std::string(ALScriptKeyPresets::STANDARD);
            one.effective = effectiveOf(one.preset, one.bound);
            one.map       = build(one.effective);
            one.stale     = false;
        }
        return one;
    }

    const LLSD& rebound() { return built().bound; }

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

namespace
{
    void store(const LLSD& map)
    {
        gSavedSettings.setLLSD(SETTING, map);
        built().stale = true;
    }

    // A command's name in a layer: an editor's own, a menu item's under
    // MENU_PREFIX.
    std::string layerName(const ALScriptKeymap::Owner& owner)
    {
        return owner.menu.empty() ? std::string(alEditorCommandName(owner.command)) : MENU_PREFIX + owner.menu;
    }

    ALScriptKeymap::Owner ownerNamed(const std::string& name)
    {
        ALScriptKeymap::Owner owner;
        if (name.compare(0, MENU_PREFIX.size(), MENU_PREFIX) == 0)
        {
            owner.menu = name.substr(MENU_PREFIX.size());
        }
        else if (const std::optional<ALEditorCommand> command = alEditorCommandFromName(name))
        {
            owner.command = *command;
        }
        return owner;
    }

    // A command's keys in a layer: its own there, else the standard's.
    ALScriptKeymap::keys_t keysIn(const LLSD& layer, ALEditorCommand command)
    {
        const char* const name = alEditorCommandName(command);
        if (layer.isMap() && layer.has(name))
        {
            return keysFrom(layer[name]);
        }
        static const ALKeymap standard = ALKeymap::standard();
        return ALScriptKeymap::keysOf(standard, command);
    }

    ALScriptKeymap::chords_t chordsIn(const LLSD& layer, std::string_view item)
    {
        const std::string name = MENU_PREFIX + std::string(item);
        if (layer.isMap() && layer.has(name))
        {
            return chordsFrom(layer[name]);
        }
        ALScriptKeymap::chords_t keys;
        for (const ALScriptKeymap::MenuCommand& one : ALScriptKeymap::menuCommands())
        {
            if (item == one.id && !one.chord().none())
            {
                keys.push_back(one.chord());
            }
        }
        return keys;
    }

    // What giving `chord` to `keeping` takes, in a layer: each command
    // that loses keys to it, with the keys it keeps, as the layer keeps
    // them. The editors' command that hears its first key -- the key
    // pressed, or the first of two -- since the editor has a key before
    // the studio would wait; and the menus' commands with the same keys,
    // with keys whose first is the key given (it runs a command of its
    // own now, and the studio would never wait for a second), or with the
    // first of the two given as a key of its own.
    struct Taken
    {
        ALScriptKeymap::Owner owner;
        LLSD                  rest;
    };
    std::vector<Taken> takesIn(const LLSD& layer, const ALScriptKeymap::Owner& keeping, const ALKeyChord& chord)
    {
        std::vector<Taken> taken;
        const KEY          first_key  = alKeyAsBound(chord.twoKeys() ? chord.leadKey : chord.key);
        const MASK         first_mask = chord.twoKeys() ? chord.leadMask : chord.mask;
        for (U8 i = 1; i < static_cast<U8>(ALEditorCommand::COUNT); ++i)
        {
            const ALEditorCommand command = static_cast<ALEditorCommand>(i);
            if (command == keeping.command)
            {
                continue;
            }
            const ALScriptKeymap::keys_t had = keysIn(layer, command);
            ALScriptKeymap::keys_t       rest;
            for (const auto& [key, mask] : had)
            {
                if (alKeyAsBound(key) != first_key || mask != first_mask)
                {
                    rest.emplace_back(key, mask);
                }
            }
            if (rest.size() != had.size())
            {
                Taken one;
                one.owner.command = command;
                one.rest          = keysTo(rest);
                taken.push_back(one);
            }
        }
        for (const std::string& id : ALScriptKeymap::menuIds())
        {
            if (keeping.menu == id)
            {
                continue;
            }
            const ALScriptKeymap::chords_t had = chordsIn(layer, id);
            ALScriptKeymap::chords_t       rest;
            for (const ALKeyChord& one : had)
            {
                const bool same     = one == chord;
                const bool led      = !chord.twoKeys() && one.ledBy(chord.key, chord.mask);
                const bool leads_it = chord.twoKeys() && !one.twoKeys() && one.key == chord.leadKey && one.mask == chord.leadMask;
                if (!same && !led && !leads_it)
                {
                    rest.push_back(one);
                }
            }
            if (rest.size() != had.size())
            {
                Taken one;
                one.owner.menu = id;
                one.rest       = chordsTo(rest);
                taken.push_back(one);
            }
        }
        return taken;
    }

    // A command given keys in a layer, taken from whatever else in it had
    // them.
    void giveIn(LLSD& layer, const ALScriptKeymap::Owner& owner, const ALScriptKeymap::chords_t& chords, const LLSD& list)
    {
        for (const ALKeyChord& chord : chords)
        {
            for (const Taken& one : takesIn(layer, owner, chord))
            {
                layer[layerName(one.owner)] = one.rest;
            }
        }
        layer[layerName(owner)] = list;
    }

    ALScriptKeymap::chords_t chordsOf(const ALScriptKeymap::Owner& owner, const LLSD& list)
    {
        if (!owner.menu.empty())
        {
            return chordsFrom(list);
        }
        ALScriptKeymap::chords_t chords;
        for (const auto& [key, mask] : keysFrom(list))
        {
            chords.push_back(ALKeyChord{ key, mask });
        }
        return chords;
    }

    // The preset's keys over the standard, each binding in its turn taking
    // its keys from whatever had them; made once for each preset.
    const LLSD& presetLayer(const std::string& id)
    {
        static boost::unordered_flat_map<std::string, LLSD, ll::string_hash, std::equal_to<>> made;
        if (const auto found = made.find(id); found != made.end())
        {
            return found->second;
        }
        LLSD layer = LLSD::emptyMap();
        if (const ALScriptKeyPresets::Preset* preset = ALScriptKeyPresets::find(id))
        {
            for (const ALScriptKeyPresets::Binding& binding : preset->bindings)
            {
                ALScriptKeymap::Owner owner;
                if (const std::optional<ALEditorCommand> command = alEditorCommandFromName(binding.id))
                {
                    owner.command = *command;
                    ALScriptKeymap::keys_t keys;
                    for (const ALKeyChord& chord : binding.keys)
                    {
                        keys.emplace_back(chord.key, chord.mask);
                    }
                    giveIn(layer, owner, binding.keys, keysTo(keys));
                }
                else
                {
                    owner.menu = binding.id;
                    giveIn(layer, owner, binding.keys, chordsTo(binding.keys));
                }
            }
        }
        return made.emplace(id, layer).first->second;
    }

    LLSD effectiveOf(const std::string& preset, const LLSD& bound)
    {
        LLSD layer = presetLayer(preset);
        if (bound.isMap())
        {
            for (LLSD::map_const_iterator it = bound.beginMap(); it != bound.endMap(); ++it)
            {
                const ALScriptKeymap::Owner owner = ownerNamed(it->first);
                if (owner.command == ALEditorCommand::None && owner.menu.empty())
                {
                    continue;
                }
                giveIn(layer, owner, chordsOf(owner, it->second), it->second);
            }
        }
        return layer;
    }

    ALKeymap build(const LLSD& bound)
    {
        ALKeymap map = ALKeymap::standard();
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
            for (const auto& [key, mask] : ALScriptKeymap::keysOf(map, *command))
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
}

namespace ALScriptKeymap
{
    const ALKeymap& current() { return built().map; }

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
        // open. A command named again has another key as standard: the
        // menu shows the first.
#if LL_DARWIN
        constexpr MASK REAL_CONTROL = MASK_MAC_CONTROL;
#else
        constexpr MASK REAL_CONTROL = MASK_CONTROL;
#endif
        // Control-K, then a key: for what would otherwise hold Control
        // and Alt together, which is AltGr on many a keyboard -- a German
        // } or a Polish s typed would fold a pane or save every script.
        const auto after_k = [](const char* id, KEY key) { return MenuCommand{ id, key, MASK_NONE, true, 'K', MASK_CONTROL }; };
        // Every command of the menus that is not the editors' own (those are
        // ALKeymap's), in the menus' order, so that each can be given keys.
        static const std::vector<MenuCommand> commands{
            // File
            { "new_script", 'N', MASK_CONTROL },
            { "new_lua_script", KEY_NONE, MASK_NONE },
            { "save", 'S', MASK_CONTROL },
            after_k("save_all", 'S'),
            { "revert", KEY_NONE, MASK_NONE },
            { "open_file", KEY_NONE, MASK_NONE },
            { "local_history", KEY_NONE, MASK_NONE },
            { "recover", KEY_NONE, MASK_NONE },
            { "insert_file", KEY_NONE, MASK_NONE },
            { "load_file", KEY_NONE, MASK_NONE },
            { "save_file", KEY_NONE, MASK_NONE },
            { "save_as", KEY_NONE, MASK_NONE },
            { "external_editor", KEY_NONE, MASK_NONE },
            { "preferences", KEY_NONE, MASK_NONE },
            { "update_definitions", KEY_NONE, MASK_NONE },
            { "pop_out", KEY_NONE, MASK_NONE },
            { "close", 'W', MASK_CONTROL },
            { "close_others", KEY_NONE, MASK_NONE },
            { "close_saved", KEY_NONE, MASK_NONE },
            { "close_all", KEY_NONE, MASK_NONE },
            // Edit
            { "fix_all", KEY_NONE, MASK_NONE },
            { "check_object", KEY_NONE, MASK_NONE },
            { "format", 'F', MASK_SHIFT | MASK_ALT },
            { "format_selection", KEY_NONE, MASK_NONE },
            { "indent_spaces", KEY_NONE, MASK_NONE },
            { "indent_tabs", KEY_NONE, MASK_NONE },
            { "convert_slua", KEY_NONE, MASK_NONE },
            { "find_in_files", 'F', MASK_CONTROL | MASK_SHIFT },
            // Insert
            { "insert_snippet", 'I', MASK_CONTROL | MASK_SHIFT },
            { "insert_function", KEY_NONE, MASK_NONE },
            { "insert_event", KEY_NONE, MASK_NONE },
            { "insert_constant", KEY_NONE, MASK_NONE },
            // Go
#if LL_DARWIN
            // The Control key's on a Mac, as an editor of code has them
            // there: Command-minus is the text's size.
            { "back", '-', MASK_MAC_CONTROL },
            { "forward", '-', MASK_MAC_CONTROL | MASK_SHIFT },
#else
            // Alt with an arrow, as VS Code has them: Control-minus is the
            // text's size.
            { "back", KEY_LEFT, MASK_ALT },
            { "forward", KEY_RIGHT, MASK_ALT },
#endif
            { "quick_open", 'P', MASK_CONTROL },
#if LL_DARWIN
            // The Mac's own Control-G, as its editors go to a line:
            // Command-G is the next match there.
            { "go_to_line", 'G', MASK_MAC_CONTROL },
#else
            { "go_to_line", 'G', MASK_CONTROL },
#endif
            { "go_to_symbol", 'O', MASK_CONTROL | MASK_SHIFT },
#if LL_DARWIN
            // Xcode's next and previous issue, first, the function keys
            // needing Fn on most Macs.
            { "next_problem", '\'', MASK_CONTROL },
            { "previous_problem", '\'', MASK_CONTROL | MASK_SHIFT },
#endif
            { "next_problem", KEY_F8, MASK_NONE },
            { "previous_problem", KEY_F8, MASK_SHIFT },
            { "next_tab", KEY_PAGE_DOWN, MASK_CONTROL },
            // Control-Tab too, as everywhere: the Mac's own Control key,
            // Command-Tab being the system's.
            { "next_tab", KEY_TAB, REAL_CONTROL },
            { "previous_tab", KEY_PAGE_UP, MASK_CONTROL },
            { "previous_tab", KEY_TAB, REAL_CONTROL | MASK_SHIFT },
#if LL_DARWIN
            // And Command-Shift with a bracket, as the Mac's own go
            // between tabs.
            { "next_tab", ']', MASK_CONTROL | MASK_SHIFT },
            { "previous_tab", '[', MASK_CONTROL | MASK_SHIFT },
#endif
            { "last_tab", '6', REAL_CONTROL },
            { "all_tabs", KEY_NONE, MASK_NONE },
            { "move_tab_left", KEY_PAGE_UP, MASK_CONTROL | MASK_SHIFT },
            { "move_tab_right", KEY_PAGE_DOWN, MASK_CONTROL | MASK_SHIFT },
            { "focus_tabs", KEY_NONE, MASK_NONE },
#if LL_DARWIN
            { "next_pane", ']', MASK_CONTROL | MASK_MAC_CONTROL },
            { "previous_pane", '[', MASK_CONTROL | MASK_MAC_CONTROL },
#endif
            { "next_pane", KEY_F6, MASK_NONE },
            { "previous_pane", KEY_F6, MASK_SHIFT },
            // View
            { "command_palette", 'P', MASK_CONTROL | MASK_SHIFT },
            { "word_wrap", KEY_NONE, MASK_NONE },
            { "line_numbers", KEY_NONE, MASK_NONE },
            { "relative_numbers", KEY_NONE, MASK_NONE },
            { "notecard_from_zero", KEY_NONE, MASK_NONE },
            { "indent_guides", KEY_NONE, MASK_NONE },
            { "blanks_none", KEY_NONE, MASK_NONE },
            { "blanks_selection", KEY_NONE, MASK_NONE },
            { "blanks_trailing", KEY_NONE, MASK_NONE },
            { "blanks_all", KEY_NONE, MASK_NONE },
            { "rainbow_brackets", KEY_NONE, MASK_NONE },
            { "sticky_headers", KEY_NONE, MASK_NONE },
            { "vim_mode", KEY_NONE, MASK_NONE },
            { "semantic_colors", KEY_NONE, MASK_NONE },
            { "inlay_parameters", KEY_NONE, MASK_NONE },
            { "inlay_types", KEY_NONE, MASK_NONE },
            { "weight_notes", KEY_NONE, MASK_NONE },
            { "weight_heat", KEY_NONE, MASK_NONE },
            { "spell_check", KEY_NONE, MASK_NONE },
            { "scroll_bar", KEY_NONE, MASK_NONE },
            { "scroll_map", KEY_NONE, MASK_NONE },
            { "map_narrow", KEY_NONE, MASK_NONE },
            { "map_medium", KEY_NONE, MASK_NONE },
            { "map_wide", KEY_NONE, MASK_NONE },
            { "map_preview", KEY_NONE, MASK_NONE },
            { "map_left", KEY_NONE, MASK_NONE },
            after_k("explorer", '0'),
            after_k("problems", '1'),
            after_k("references", '2'),
            after_k("output", '3'),
            after_k("search", '5'),
            after_k("weights", '6'),
            after_k("inspector", '4'),
            after_k("expanded", 'P'),
            { "compare_saved", KEY_NONE, MASK_NONE },
            { "compare_inline", KEY_NONE, MASK_NONE },
            // The text's size: Control-= and Control-minus, Shift or not,
            // the keypad's too, as everywhere -- Command on a Mac -- and
            // Control-0 back to the size chosen. The '=' key is '=' from
            // Windows and KEY_EQUALS from SDL.
            { "zoom_in", '=', MASK_CONTROL },
            { "zoom_in", KEY_EQUALS, MASK_CONTROL },
            { "zoom_in", '=', MASK_CONTROL | MASK_SHIFT },
            { "zoom_in", KEY_EQUALS, MASK_CONTROL | MASK_SHIFT },
            { "zoom_in", KEY_ADD, MASK_CONTROL },
            { "zoom_out", '-', MASK_CONTROL },
            { "zoom_out", KEY_SUBTRACT, MASK_CONTROL },
            { "zoom_reset", '0', MASK_CONTROL },
            // Build
            { "preprocess", KEY_NONE, MASK_NONE },
            { "running", KEY_NONE, MASK_NONE },
            { "reset_script", KEY_NONE, MASK_NONE },
            { "choose_target", KEY_NONE, MASK_NONE },
            { "choose_experience", KEY_NONE, MASK_NONE },
            { "preflight", KEY_NONE, MASK_NONE },
            { "preproc_enabled", KEY_NONE, MASK_NONE },
            { "preproc_disk", KEY_NONE, MASK_NONE },
            { "preproc_folder", KEY_NONE, MASK_NONE },
            { "preproc_switch", KEY_NONE, MASK_NONE },
            { "preproc_lazy", KEY_NONE, MASK_NONE },
            { "preproc_compress", KEY_NONE, MASK_NONE },
            { "preproc_extensions", KEY_NONE, MASK_NONE },
            { "preproc_optimize", KEY_NONE, MASK_NONE },
            { "preproc_shrink", KEY_NONE, MASK_NONE },
            { "preproc_addstrings", KEY_NONE, MASK_NONE },
            { "preproc_inline", KEY_NONE, MASK_NONE },
            // Help
#if LL_DARWIN
            // Xcode's documentation, first, F1 needing Fn.
            { "reference", '0', MASK_CONTROL | MASK_SHIFT },
#endif
            { "reference", KEY_F1, MASK_NONE },
            { "browse_reference", KEY_NONE, MASK_NONE },
            { "wiki", KEY_NONE, MASK_NONE },
        };
        return commands;
    }

    const std::vector<std::string>& menuIds()
    {
        static const std::vector<std::string> ids = []() {
            std::vector<std::string> out;
            for (const MenuCommand& one : menuCommands())
            {
                if (std::find(out.begin(), out.end(), one.id) == out.end())
                {
                    out.emplace_back(one.id);
                }
            }
            return out;
        }();
        return ids;
    }

    bool isMenuCommand(std::string_view item)
    {
        const std::vector<std::string>& all = menuIds();
        return std::find(all.begin(), all.end(), item) != all.end();
    }

    chords_t menuKeys(std::string_view item) { return chordsIn(built().effective, item); }

    ALKeyChord menuKey(std::string_view item)
    {
        const chords_t keys = menuKeys(item);
        return keys.empty() ? ALKeyChord{} : keys.front();
    }

    bool isMenuRebound(std::string_view item)
    {
        const LLSD& bound = rebound();
        return bound.isMap() && bound.has(MENU_PREFIX + std::string(item));
    }

    void rebindMenu(std::string_view item, const chords_t& keys)
    {
        LLSD bound = rebound();
        if (!bound.isMap())
        {
            bound = LLSD::emptyMap();
        }
        bound[MENU_PREFIX + std::string(item)] = chordsTo(keys);
        store(bound);
    }

    void restoreMenu(std::string_view item)
    {
        LLSD              bound = rebound();
        const std::string name  = MENU_PREFIX + std::string(item);
        if (bound.isMap() && bound.has(name))
        {
            bound.erase(name);
            store(bound);
        }
    }

    std::vector<Owner> takeKeys(const Owner& keeping, const ALKeyChord& chord, bool apply)
    {
        std::vector<Owner>       from;
        const std::vector<Taken> taken = takesIn(built().effective, keeping, chord);
        LLSD                     bound = rebound();
        if (!bound.isMap())
        {
            bound = LLSD::emptyMap();
        }
        for (const Taken& one : taken)
        {
            bound[layerName(one.owner)] = one.rest;
            from.push_back(one.owner);
        }
        if (apply && !taken.empty())
        {
            store(bound);
        }
        return from;
    }

    const std::string& preset() { return built().preset; }

    void setPreset(const std::string& id)
    {
        if (gSavedSettings.controlExists(PRESET_SETTING))
        {
            gSavedSettings.setString(PRESET_SETTING, ALScriptKeyPresets::find(id) ? id : std::string(ALScriptKeyPresets::STANDARD));
        }
        built().stale = true;
    }

    bool anyRebound()
    {
        const LLSD& bound = rebound();
        return bound.isMap() && bound.size() > 0;
    }

    std::vector<MenuItem> menuItemsIn(const LLXMLNodePtr& root)
    {
        std::vector<MenuItem>                                         out;
        const std::function<void(const LLXMLNodePtr&, const std::string&)> walk = [&](const LLXMLNodePtr& node, const std::string& path) {
            for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
            {
                std::string name;
                std::string label;
                child->getAttributeString("name", name);
                child->getAttributeString("label", label);
                const std::string here = path.empty() ? label : path + " > " + label;
                if (child->hasName("menu"))
                {
                    walk(child, here);
                }
                else if ((child->hasName("menu_item_call") || child->hasName("menu_item_check")) && !name.empty())
                {
                    out.push_back({ name, here });
                }
                else if (child->hasName("menu_bar") || child->hasName("floater") || child->hasName("panel") || child->hasName("layout_stack") ||
                         child->hasName("layout_panel"))
                {
                    // The bar, wherever in the window it is.
                    if (child->hasName("menu_bar") && name != "studio_menu")
                    {
                        continue;
                    }
                    walk(child, child->hasName("menu_bar") ? std::string() : path);
                }
            }
        };
        walk(root, std::string());
        return out;
    }

    const std::vector<MenuItem>& studioMenuItems()
    {
        static const std::vector<MenuItem> items = []() {
            LLXMLNodePtr root;
            return LLUICtrlFactory::getLayeredXMLNode("floater_script_studio.xml", root) ? menuItemsIn(root) : std::vector<MenuItem>();
        }();
        return items;
    }

    std::string describe(const chords_t& keys)
    {
        std::string text;
        for (const ALKeyChord& chord : keys)
        {
            if (!text.empty())
            {
                text += ", ";
            }
            text += chord.describe();
        }
        return text;
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
