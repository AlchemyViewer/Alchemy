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

#include "alfloaterscriptstudio.h"
#include "llbutton.h"
#include "llfloaterreg.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "llviewercontrol.h"

namespace
{
    const char* const SETTING = "ALScriptStudioKeymap";

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

void ALPanelScriptKeymap::fill()
{
    const ALEditorCommand was    = chosen();
    std::string           filter = mFilter->getText();
    LLStringUtil::toLower(filter);
    const ALKeymap map = ALScriptKeymap::current();
    mList->deleteAllItems();
    for (U8 i = 1; i < static_cast<U8>(ALEditorCommand::COUNT); ++i)
    {
        const ALEditorCommand command = static_cast<ALEditorCommand>(i);
        const std::string     name    = alEditorCommandName(command);
        const std::string     about   = getString("cmd_" + name);
        const std::string     keys    = ALScriptKeymap::describe(ALScriptKeymap::keysOf(map, command));
        if (!filter.empty())
        {
            std::string haystack = about + " " + keys + " " + name;
            LLStringUtil::toLower(haystack);
            if (haystack.find(filter) == std::string::npos)
            {
                continue;
            }
        }
        LLSD row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "command";
        row["columns"][0]["value"]  = about;
        row["columns"][1]["column"] = "keys";
        row["columns"][1]["value"]  = keys.empty() ? getString("NoKeys") : keys;
        row["columns"][2]["column"] = "changed";
        row["columns"][2]["value"]  = ALScriptKeymap::isRebound(command) ? getString("Changed") : LLStringUtil::null;
        LLScrollListItem* item      = mList->addElement(row);
        item->setSelected(command == was);
    }
    refreshButtons();
}

ALEditorCommand ALPanelScriptKeymap::chosen() const
{
    const LLScrollListItem* item = mList->getFirstSelected();
    return item ? static_cast<ALEditorCommand>(item->getValue().asInteger()) : ALEditorCommand::None;
}

void ALPanelScriptKeymap::refreshButtons()
{
    const ALEditorCommand command = chosen();
    const bool            some    = command != ALEditorCommand::None;
    getChild<LLButton>("change")->setEnabled(some);
    getChild<LLButton>("add")->setEnabled(some);
    getChild<LLButton>("clear")->setEnabled(some);
    getChild<LLButton>("restore")->setEnabled(some && ALScriptKeymap::isRebound(command));
}

void ALPanelScriptKeymap::onChange()
{
    mEditing = chosen();
    mAdding  = false;
    if (mEditing == ALEditorCommand::None)
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
    mAdding = true;
}

void ALPanelScriptKeymap::onClear()
{
    const ALEditorCommand command = chosen();
    if (command != ALEditorCommand::None)
    {
        ALScriptKeymap::rebind(command, {});
        fill();
        ALFloaterScriptStudio::refreshAll();
    }
}

void ALPanelScriptKeymap::onRestore()
{
    const ALEditorCommand command = chosen();
    if (command != ALEditorCommand::None)
    {
        ALScriptKeymap::restore(command);
        fill();
        ALFloaterScriptStudio::refreshAll();
    }
}

void ALPanelScriptKeymap::onRestoreAll()
{
    ALScriptKeymap::restoreAll();
    fill();
    ALFloaterScriptStudio::refreshAll();
}

void ALPanelScriptKeymap::onDefaultKeyBind(bool)
{
    if (mEditing != ALEditorCommand::None)
    {
        ALScriptKeymap::restore(mEditing);
        fill();
        ALFloaterScriptStudio::refreshAll();
    }
}

bool ALPanelScriptKeymap::onSetKeyBind(EMouseClickType click, KEY key, MASK mask, bool)
{
    if (mEditing == ALEditorCommand::None || click != CLICK_NONE || key == KEY_NONE)
    {
        return false;
    }
    // Whatever else answered to the key lets it go.
    const ALKeymap        map   = ALScriptKeymap::current();
    const ALEditorCommand other = map.lookup(key, mask);
    if (other != ALEditorCommand::None && other != mEditing)
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
    }
    ALScriptKeymap::keys_t keys;
    if (mAdding)
    {
        keys = ALScriptKeymap::keysOf(map, mEditing);
    }
    keys.emplace_back(key, mask);
    ALScriptKeymap::rebind(mEditing, keys);
    fill();
    ALFloaterScriptStudio::refreshAll();
    return false;
}
