/**
 * @file alpanelscriptkeymap.cpp
 * @brief Script Studio's keyboard map, a tab of its preferences.
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

#include "alpanelscriptkeymap.h"

#include "alstringmatch.h"

#include "alfloaterscriptstudio.h"
#include "llbutton.h"
#include "llfloaterreg.h"
#include "llviewerwindow.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"

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
        LLSD value;
        value["menu"] = one.id;
        add(value, getString(std::string("menu_") + one.id), ALScriptKeymap::menuKey(one.id).describe(), one.id, ALScriptKeymap::isMenuRebound(one.id),
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
        ALScriptKeymap::rebindMenu(which.menu, {});
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
    // both answered to was the editor's alone in the editor. One the key
    // is the first of two of is left without too: the key now runs a
    // command of its own, and the window would never wait for a second.
    for (const ALScriptKeymap::MenuCommand& one : ALScriptKeymap::menuCommands())
    {
        const ALKeyChord had = ALScriptKeymap::menuKey(one.id);
        if (keeping.menu == one.id || (had != ALKeyChord{ key, mask } && !had.ledBy(key, mask)))
        {
            continue;
        }
        ALScriptKeymap::rebindMenu(one.id, {});
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
        ALScriptKeymap::rebindMenu(mEditing.menu, ALKeyChord{ key, mask });
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
