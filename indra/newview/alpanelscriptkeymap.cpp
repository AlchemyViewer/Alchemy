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

#include "alscriptstudio.h"
#include "alkeycapture.h"
#include "alscriptkeypresets.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "llviewermenu.h"

static LLPanelInjector<ALPanelScriptKeymap> t_script_keymap("al_panel_script_keymap");

ALPanelScriptKeymap::ALPanelScriptKeymap() = default;

ALPanelScriptKeymap::~ALPanelScriptKeymap() = default;

bool ALPanelScriptKeymap::postBuild()
{
    mPreset = getChild<LLComboBox>("preset");
    mFilter = getChild<LLLineEditor>("filter");
    fillPresets();
    mPreset->setCommitCallback([this](LLUICtrl*, const LLSD&) { onPreset(); });
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

void ALPanelScriptKeymap::refresh()
{
    fillPresets();
    fill();
}

void ALPanelScriptKeymap::fillPresets()
{
    mPreset->removeall();
    for (const ALScriptKeyPresets::Preset& preset : ALScriptKeyPresets::all())
    {
        mPreset->add(getString("preset_" + preset.id), LLSD(preset.id));
    }
    mPreset->selectByValue(LLSD(ALScriptKeymap::preset()));
}

void ALPanelScriptKeymap::onPreset()
{
    const std::string id = mPreset->getValue().asString();
    if (id.empty() || id == ALScriptKeymap::preset())
    {
        return;
    }
    ALScriptKeymap::setPreset(id);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = getString("preset_" + id);
    mSaid->setText(getString(ALScriptKeymap::anyRebound() ? "PresetChosenOwn" : "PresetChosen", args));
    fill();
    ALScriptStudio::refreshAll();
}

std::string ALPanelScriptKeymap::nameOf(const Chosen& which) const
{
    if (which.menu.empty())
    {
        return getString(std::string("cmd_") + alEditorCommandName(which.command));
    }
    // A menu's command by where it is in the menus, in their own words.
    for (const ALScriptKeymap::MenuItem& item : ALScriptKeymap::studioMenuItems())
    {
        if (item.id == which.menu)
        {
            return item.path;
        }
    }
    return which.menu;
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
    for (const std::string& id : ALScriptKeymap::menuIds())
    {
        LLSD value;
        value["menu"] = id;
        Chosen which;
        which.menu = id;
        add(value, nameOf(which), ALScriptKeymap::describe(ALScriptKeymap::menuKeys(id)), id, ALScriptKeymap::isMenuRebound(id), was.menu == id);
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
    getChild<LLButton>("add")->setEnabled(some);
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
    // A menu's command may be two keys in turn, which the studio waits for;
    // an editor's hears one.
    const bool                 two_keys = !mEditing.menu.empty();
    ALKeyCapture::Words        words;
    LLStringUtil::format_map_t args;
    args["[WHAT]"] = nameOf(mEditing);
    words.title    = getString("CaptureTitle", args);
    words.prompt   = getString(two_keys ? "CapturePromptTwo" : "CapturePrompt");
    words.nothing  = getString("CaptureNothing");
    words.set      = getString("CaptureSet");
    words.cancel   = getString("CaptureCancel");
    const LLHandle<LLPanel> handle = getHandle();
    ALKeyCapture::show(
        mList, words, two_keys,
        [handle](const ALKeyChord& chord) {
            const ALPanelScriptKeymap* panel = ALViewType::as<ALPanelScriptKeymap>(handle.get());
            return panel ? panel->aboutKeys(chord) : std::string();
        },
        [handle](const ALKeyChord& chord) {
            if (ALPanelScriptKeymap* panel = ALViewType::as<ALPanelScriptKeymap>(handle.get()))
            {
                panel->setKeys(chord);
            }
        });
}

void ALPanelScriptKeymap::onAdd()
{
    onChange();
    mAdding = true;
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
    ALScriptStudio::refreshAll();
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
    ALScriptStudio::refreshAll();
}

void ALPanelScriptKeymap::onRestoreAll()
{
    ALScriptKeymap::restoreAll();
    mSaid->setText(LLStringUtil::null);
    fill();
    ALScriptStudio::refreshAll();
}

std::vector<std::string> ALPanelScriptKeymap::takeKey(const Chosen& keeping, const ALKeyChord& chord, bool apply) const
{
    ALScriptKeymap::Owner owner;
    owner.command = keeping.command;
    owner.menu    = keeping.menu;
    std::vector<std::string> from;
    for (const ALScriptKeymap::Owner& other : ALScriptKeymap::takeKeys(owner, chord, apply))
    {
        Chosen was;
        was.command = other.command;
        was.menu    = other.menu;
        from.push_back(nameOf(was));
    }
    return from;
}

namespace
{
    // The viewer's menu item a key is the accelerator of, by its label, or
    // nothing: the studio keeps its keys while it has the keyboard, so the
    // item is out of reach from it then.
    std::string viewerMenuItemOf(KEY key, MASK mask)
    {
        std::string                        found;
        const std::function<void(LLView*)> walk = [&](LLView* menu) {
            for (LLView* child : *menu->getChildList())
            {
                if (!found.empty())
                {
                    return;
                }
                if (LLMenuItemBranchGL* branch = child->as<LLMenuItemBranchGL>())
                {
                    if (LLMenuGL* under = branch->getBranch())
                    {
                        walk(under);
                    }
                }
                else if (LLMenuItemGL* item = child->as<LLMenuItemGL>();
                         item && item->getAcceleratorKey() == key && (item->getAcceleratorMask() & MASK_MODIFIERS) == mask)
                {
                    found = item->getLabel();
                }
            }
        };
        if (gMenuBarView && key != KEY_NONE)
        {
            walk(gMenuBarView);
        }
        return found;
    }
}

std::string ALPanelScriptKeymap::aboutKeys(const ALKeyChord& chord) const
{
    std::string said;
    const auto  add = [&said](const std::string& line) { said += (said.empty() ? "" : "\n") + line; };
    const std::vector<std::string> from = takeKey(mEditing, chord, false);
    if (!from.empty())
    {
        std::string whose;
        for (const std::string& name : from)
        {
            whose += (whose.empty() ? "" : ", ") + name;
        }
        add(getString("CaptureTakes", LLStringUtil::format_map_t{ { "[WHOSE]", whose } }));
    }
    const KEY  first_key  = chord.twoKeys() ? chord.leadKey : chord.key;
    const MASK first_mask = chord.twoKeys() ? chord.leadMask : chord.mask;
    if (const std::string item = viewerMenuItemOf(first_key, first_mask); !item.empty())
    {
        add(getString("CaptureShadows", LLStringUtil::format_map_t{ { "[KEYS]", ALKeyChord{ first_key, first_mask }.describe() }, { "[ITEM]", item } }));
    }
    return said;
}

void ALPanelScriptKeymap::setKeys(const ALKeyChord& chord)
{
    if (!mEditing.any() || chord.none() || (mEditing.menu.empty() && chord.twoKeys()))
    {
        return;
    }
    // Whatever else answered to the keys lets them go, and is named.
    const std::vector<std::string> from = takeKey(mEditing, chord, true);
    if (mEditing.menu.empty())
    {
        ALScriptKeymap::keys_t keys;
        if (mAdding)
        {
            keys = ALScriptKeymap::keysOf(ALScriptKeymap::current(), mEditing.command);
        }
        keys.emplace_back(chord.key, chord.mask);
        ALScriptKeymap::rebind(mEditing.command, keys);
    }
    else
    {
        ALScriptKeymap::chords_t keys;
        if (mAdding)
        {
            keys = ALScriptKeymap::menuKeys(mEditing.menu);
        }
        keys.push_back(chord);
        ALScriptKeymap::rebindMenu(mEditing.menu, keys);
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
        args["[KEYS]"]  = chord.describe();
        args["[WHOSE]"] = whose;
        args["[WHAT]"]  = nameOf(mEditing);
        mSaid->setText(getString("KeyTaken", args));
    }
    fill();
    ALScriptStudio::refreshAll();
}
