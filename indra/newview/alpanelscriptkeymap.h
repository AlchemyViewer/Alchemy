/**
 * @file alpanelscriptkeymap.h
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

#pragma once

#include "alscriptkeymap.h"
#include "llpanel.h"

#include <string>
#include <vector>

class LLLineEditor;
class LLScrollListCtrl;
class LLTextBox;

// The keyboard map: every command of the editor, and of the studio's
// menus, with the keys it answers to, to read, to search, and to change --
// keys pressed into a popover of the studio's own (ALKeyCapture), which
// takes any key, the viewer's menus' among them -- or put back. Keys given
// to one command are taken from whatever had them, which the popover says
// before and the panel after, and a key the viewer's menus have is said to
// be the studio's while it has the keyboard. A tab of the studio's
// preferences.
class ALPanelScriptKeymap final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALPanelScriptKeymap, LLPanel);

    ALPanelScriptKeymap();
    ~ALPanelScriptKeymap() override;

    bool postBuild() override;
    // The list read from the keymap again: the setting changed under it.
    void refresh() override { fill(); }


private:
    void fill();
    void onChange();
    void onAdd();
    void onClear();
    void onRestore();
    void onRestoreAll();
    void refreshButtons();
    // The command of the row chosen: an editor's, or a menu item's by
    // name; neither where nothing is chosen.
    struct Chosen
    {
        ALEditorCommand command = ALEditorCommand::None;
        std::string     menu;
        bool            any() const { return command != ALEditorCommand::None || !menu.empty(); }
    };
    Chosen      chosen() const;
    // What a command is called in the list.
    std::string nameOf(const Chosen& which) const;
    // The keys taken from every command but this one that answered to
    // them, or to the first of them, or whose first they are, and what
    // those were called; only named where `apply` is false.
    std::vector<std::string> takeKey(const Chosen& keeping, const ALKeyChord& chord, bool apply) const;
    // What giving the keys would mean, as the popover says it: whose they
    // are now, and the viewer's menu item they would be the studio's over.
    std::string aboutKeys(const ALKeyChord& chord) const;
    // The keys pressed, given to the command being edited.
    void setKeys(const ALKeyChord& chord);

    LLLineEditor*     mFilter  = nullptr;
    LLScrollListCtrl* mList    = nullptr;
    LLTextBox*        mSaid    = nullptr;
    Chosen            mEditing;
    // Whether the key dialog adds to the command's keys or replaces them.
    bool              mAdding  = false;
};
