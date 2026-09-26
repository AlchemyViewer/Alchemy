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

#include "alstringmatch.h"

#include "alfloaterscriptstudio.h"
#include "llbutton.h"
#include "llfloaterreg.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
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
        static const std::vector<MenuCommand> commands{
            { "new_script", 'N', MASK_CONTROL },
            { "new_lua_script", KEY_NONE, MASK_NONE },
            { "save", 'S', MASK_CONTROL },
            { "save_all", 'S', MASK_CONTROL | MASK_ALT },
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
            { "explorer", '0', MASK_CONTROL | MASK_ALT },
            { "problems", '1', MASK_CONTROL | MASK_ALT },
            { "references", '2', MASK_CONTROL | MASK_ALT },
            { "output", '3', MASK_CONTROL | MASK_ALT },
            { "inspector", '4', MASK_CONTROL | MASK_ALT },
            { "search", '5', MASK_CONTROL | MASK_ALT },
            { "weights", '6', MASK_CONTROL | MASK_ALT },
            { "expanded", 'P', MASK_CONTROL | MASK_ALT },
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

    std::pair<KEY, MASK> menuKey(std::string_view item)
    {
        const LLSD bound = rebound();
        const std::string name = MENU_PREFIX + std::string(item);
        if (bound.isMap() && bound.has(name))
        {
            const keys_t keys = keysFrom(bound[name]);
            return keys.empty() ? std::make_pair(KEY_NONE, MASK_NONE) : keys.front();
        }
        for (const MenuCommand& one : menuCommands())
        {
            if (item == one.id)
            {
                return { one.key, one.mask };
            }
        }
        return { KEY_NONE, MASK_NONE };
    }

    bool isMenuRebound(std::string_view item)
    {
        const LLSD bound = rebound();
        return bound.isMap() && bound.has(MENU_PREFIX + std::string(item));
    }

    void rebindMenu(std::string_view item, KEY key, MASK mask)
    {
        LLSD bound = rebound();
        if (!bound.isMap())
        {
            bound = LLSD::emptyMap();
        }
        keys_t keys;
        if (key != KEY_NONE)
        {
            keys.emplace_back(key, mask);
        }
        bound[MENU_PREFIX + std::string(item)] = keysTo(keys);
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

// --- the panel -----------------------------------------------------------------------

static LLPanelInjector<ALPanelScriptKeymap> t_script_keymap("al_panel_script_keymap");

ALPanelScriptKeymap::ALPanelScriptKeymap() = default;

ALPanelScriptKeymap::~ALPanelScriptKeymap() = default;

bool ALPanelScriptKeymap::postBuild()
{
    mFilter = getChild<LLLineEditor>("filter");
    mList   = getChild<LLScrollListCtrl>("keys");
    mSaid   = getChild<LLTextBox>("said");
    mFilter->setKeystrokeCallback([this](LLLineEditor*, void*) { fill(); }, nullptr);
    mList->setCommitOnSelectionChange(true);
    mList->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshButtons(); });
    mList->setDoubleClickCallback([this]() { onChange(); });
    getChild<LLButton>("change")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onChange(); });
    getChild<LLButton>("add")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAdd(); });
    getChild<LLButton>("clear")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onClear(); });
    getChild<LLButton>("restore")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestore(); });
    getChild<LLButton>("restore_all")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestoreAll(); });
    fill();
    return true;
}

std::string ALPanelScriptKeymap::nameOf(const Chosen& which) const
{
    return which.menu.empty() ? getString(std::string("cmd_") + alEditorCommandName(which.command)) : getString("menu_" + which.menu);
}

void ALPanelScriptKeymap::fill()
{
    const Chosen was    = chosen();
    const std::string filter = mFilter->getText();
    const ALKeymap map = ALScriptKeymap::current();
    mList->deleteAllItems();
    const auto add = [&](const LLSD& value, const std::string& about, const std::string& keys, const std::string& name, bool changed, bool selected) {
        if (!ALStringMatch::containsNoCase(about + " " + keys + " " + name, filter))
        {
            return;
        }
        LLSD row;
        row["value"]                = value;
        row["columns"][0]["column"] = "command";
        row["columns"][0]["value"]  = about;
        row["columns"][1]["column"] = "keys";
        row["columns"][1]["value"]  = keys.empty() ? getString("NoKeys") : keys;
        row["columns"][2]["column"] = "changed";
        row["columns"][2]["value"]  = changed ? getString("Changed") : LLStringUtil::null;
        LLScrollListItem* item      = mList->addElement(row);
        item->setSelected(selected);
    };
    // The editor's commands, then the menus'.
    for (U8 i = 1; i < static_cast<U8>(ALEditorCommand::COUNT); ++i)
    {
        const ALEditorCommand command = static_cast<ALEditorCommand>(i);
        LLSD                  value;
        value["command"] = static_cast<S32>(i);
        add(value, getString(std::string("cmd_") + alEditorCommandName(command)), ALScriptKeymap::describe(ALScriptKeymap::keysOf(map, command)),
            alEditorCommandName(command), ALScriptKeymap::isRebound(command), command == was.command);
    }
    for (const ALScriptKeymap::MenuCommand& one : ALScriptKeymap::menuCommands())
    {
        const auto [key, mask] = ALScriptKeymap::menuKey(one.id);
        ALScriptKeymap::keys_t keys;
        if (key != KEY_NONE)
        {
            keys.emplace_back(key, mask);
        }
        LLSD value;
        value["menu"] = one.id;
        add(value, getString(std::string("menu_") + one.id), ALScriptKeymap::describe(keys), one.id, ALScriptKeymap::isMenuRebound(one.id),
            was.menu == one.id);
    }
    refreshButtons();
}

ALPanelScriptKeymap::Chosen ALPanelScriptKeymap::chosen() const
{
    Chosen                  which;
    const LLScrollListItem* item = mList->getFirstSelected();
    if (!item)
    {
        return which;
    }
    const LLSD& value = item->getValue();
    if (value.has("menu"))
    {
        which.menu = value["menu"].asString();
    }
    else
    {
        which.command = static_cast<ALEditorCommand>(value["command"].asInteger());
    }
    return which;
}

void ALPanelScriptKeymap::refreshButtons()
{
    const Chosen which = chosen();
    const bool   some  = which.any();
    getChild<LLButton>("change")->setEnabled(some);
    // A menu item answers to one key.
    getChild<LLButton>("add")->setEnabled(some && which.menu.empty());
    getChild<LLButton>("clear")->setEnabled(some);
    getChild<LLButton>("restore")->setEnabled(some && (which.menu.empty() ? ALScriptKeymap::isRebound(which.command) : ALScriptKeymap::isMenuRebound(which.menu)));
}

void ALPanelScriptKeymap::onChange()
{
    mEditing = chosen();
    mAdding  = false;
    if (!mEditing.any())
    {
        return;
    }
    if (LLSetKeyBindDialog* dialog = LLFloaterReg::getTypedInstance<LLSetKeyBindDialog>("keybind_dialog", LLSD()))
    {
        dialog->setParent(this, mList, ALLOW_KEYS | ALLOW_MASK_KEYS | ALLOW_MASKS);
        if (LLFloater* root = gFloaterView->getParentFloater(this))
        {
            root->addDependentFloater(dialog);
        }
        dialog->openFloater();
        dialog->setFocus(true);
    }
}

void ALPanelScriptKeymap::onAdd()
{
    onChange();
    mAdding = mEditing.menu.empty();
}

void ALPanelScriptKeymap::onClear()
{
    const Chosen which = chosen();
    if (!which.any())
    {
        return;
    }
    if (which.menu.empty())
    {
        ALScriptKeymap::rebind(which.command, {});
    }
    else
    {
        ALScriptKeymap::rebindMenu(which.menu, KEY_NONE, MASK_NONE);
    }
    mSaid->setText(LLStringUtil::null);
    fill();
    ALFloaterScriptStudio::refreshAll();
}

void ALPanelScriptKeymap::onRestore()
{
    const Chosen which = chosen();
    if (!which.any())
    {
        return;
    }
    if (which.menu.empty())
    {
        ALScriptKeymap::restore(which.command);
    }
    else
    {
        ALScriptKeymap::restoreMenu(which.menu);
    }
    mSaid->setText(LLStringUtil::null);
    fill();
    ALFloaterScriptStudio::refreshAll();
}

void ALPanelScriptKeymap::onRestoreAll()
{
    ALScriptKeymap::restoreAll();
    mSaid->setText(LLStringUtil::null);
    fill();
    ALFloaterScriptStudio::refreshAll();
}

void ALPanelScriptKeymap::onDefaultKeyBind(bool)
{
    if (!mEditing.any())
    {
        return;
    }
    if (mEditing.menu.empty())
    {
        ALScriptKeymap::restore(mEditing.command);
    }
    else
    {
        ALScriptKeymap::restoreMenu(mEditing.menu);
    }
    mSaid->setText(LLStringUtil::null);
    fill();
    ALFloaterScriptStudio::refreshAll();
}

std::vector<std::string> ALPanelScriptKeymap::takeKey(const Chosen& keeping, KEY key, MASK mask)
{
    std::vector<std::string> from;
    // An editor's command: the key goes from its keys, the rest kept.
    const ALKeymap        map   = ALScriptKeymap::current();
    const ALEditorCommand other = map.lookup(key, mask);
    if (other != ALEditorCommand::None && other != keeping.command)
    {
        ALScriptKeymap::keys_t rest;
        for (const auto& bound : ALScriptKeymap::keysOf(map, other))
        {
            if (bound.first != key || bound.second != mask)
            {
                rest.push_back(bound);
            }
        }
        ALScriptKeymap::rebind(other, rest);
        Chosen was;
        was.command = other;
        from.push_back(nameOf(was));
    }
    // A menu's: it has no key left. The editor has a key first, so a key
    // both answered to was the editor's alone in the editor.
    for (const ALScriptKeymap::MenuCommand& one : ALScriptKeymap::menuCommands())
    {
        if (keeping.menu == one.id || ALScriptKeymap::menuKey(one.id) != std::make_pair(key, mask))
        {
            continue;
        }
        ALScriptKeymap::rebindMenu(one.id, KEY_NONE, MASK_NONE);
        Chosen was;
        was.menu = one.id;
        from.push_back(nameOf(was));
    }
    return from;
}

bool ALPanelScriptKeymap::onSetKeyBind(EMouseClickType click, KEY key, MASK mask, bool)
{
    if (!mEditing.any() || click != CLICK_NONE || key == KEY_NONE)
    {
        return false;
    }
    // Whatever else answered to the key lets it go, and is named.
    const std::vector<std::string> from = takeKey(mEditing, key, mask);
    if (mEditing.menu.empty())
    {
        ALScriptKeymap::keys_t keys;
        if (mAdding)
        {
            keys = ALScriptKeymap::keysOf(ALScriptKeymap::current(), mEditing.command);
        }
        keys.emplace_back(key, mask);
        ALScriptKeymap::rebind(mEditing.command, keys);
    }
    else
    {
        ALScriptKeymap::rebindMenu(mEditing.menu, key, mask);
    }
    if (from.empty())
    {
        mSaid->setText(LLStringUtil::null);
    }
    else
    {
        std::string whose;
        for (const std::string& name : from)
        {
            whose += (whose.empty() ? "" : ", ") + name;
        }
        LLStringUtil::format_map_t args;
        args["[KEYS]"]  = LLKeyboard::stringFromAccelerator(mask, key);
        args["[WHOSE]"] = whose;
        args["[WHAT]"]  = nameOf(mEditing);
        mSaid->setText(getString("KeyTaken", args));
    }
    fill();
    ALFloaterScriptStudio::refreshAll();
    return false;
}
