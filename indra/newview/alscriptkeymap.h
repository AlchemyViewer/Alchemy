/**
 * @file alscriptkeymap.h
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

#pragma once

#include "alkeymap.h"
#include "llpanel.h"
#include "llsetkeybinddialog.h"

#include <string>
#include <utility>
#include <vector>

class LLLineEditor;
class LLScrollListCtrl;

// The keymap every editor of the studio is given: the standard one, with
// the commands a person rebound as the setting says. A rebound command
// has only the keys the setting gives it; a command the setting does not
// name keeps the standard's.
namespace ALScriptKeymap
{
    typedef std::vector<std::pair<KEY, MASK>> keys_t;

    ALKeymap current();
    keys_t   keysOf(const ALKeymap& map, ALEditorCommand command);
    bool     isRebound(ALEditorCommand command);
    // The command's keys from now on, in the setting; none unbinds it.
    void     rebind(ALEditorCommand command, const keys_t& keys);
    // Back to the standard, for one command or for all.
    void     restore(ALEditorCommand command);
    void     restoreAll();
    // The keys as a person reads them: "Ctrl+Shift+K", or the several
    // joined.
    std::string describe(const keys_t& keys);
}

// The keyboard map: every command of the editor with the keys it answers
// to, to read, to search, and to change -- a key pressed into the
// viewer's own key dialog -- or put back. A tab of the studio's
// preferences.
class ALPanelScriptKeymap final : public LLPanel, public LLKeyBindResponderInterface
{
public:
    AL_VIEW_TYPE(ALPanelScriptKeymap, LLPanel);

    ALPanelScriptKeymap();
    ~ALPanelScriptKeymap() override;

    bool postBuild() override;
    // The list read from the keymap again: the setting changed under it.
    void refresh() override { fill(); }

    void onCancelKeyBind() override {}
    void onDefaultKeyBind(bool all_modes) override;
    bool onSetKeyBind(EMouseClickType click, KEY key, MASK mask, bool all_modes) override;

private:
    void fill();
    void onChange();
    void onAdd();
    void onClear();
    void onRestore();
    void onRestoreAll();
    void refreshButtons();
    // The command of the row chosen, or None.
    ALEditorCommand chosen() const;

    LLLineEditor*     mFilter  = nullptr;
    LLScrollListCtrl* mList    = nullptr;
    ALEditorCommand   mEditing = ALEditorCommand::None;
    // Whether the key dialog adds to the command's keys or replaces them.
    bool              mAdding  = false;
};
