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
#include "alstudiofloater.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The keymap every editor of the studio is given: the standard one, with
// the commands a person rebound as the setting says. A rebound command
// has only the keys the setting gives it; a command the setting does not
// name keeps the standard's. The studio's own menu commands -- saving,
// going to a line, the panes -- are rebound in the same setting, each to
// one key or two in turn, since a menu item answers to one.
namespace ALScriptKeymap
{
    typedef std::vector<std::pair<KEY, MASK>> keys_t;

    // A command of the studio's menus a person may give a key to: the
    // item's name in the menus, and the key it answers to as standard --
    // the studio's own table of keyed commands (ALStudioFloater), which
    // Script Studio gives its window as well.
    using MenuCommand = ALStudioFloater::KeyedCommand;
    const std::vector<MenuCommand>& menuCommands();
    // Whether an item is one of them.
    bool                 isMenuCommand(std::string_view item);
    // Its key, or two in turn: the person's, else the standard; none for
    // none.
    ALKeyChord           menuKey(std::string_view item);
    bool                 isMenuRebound(std::string_view item);
    // The item's key from now on, none for none; and the standard's
    // again.
    void                 rebindMenu(std::string_view item, const ALKeyChord& chord);
    void                 restoreMenu(std::string_view item);

    ALKeymap current();
    keys_t   keysOf(const ALKeymap& map, ALEditorCommand command);
    bool     isRebound(ALEditorCommand command);
    // The command's keys from now on, in the setting; none unbinds it.
    void     rebind(ALEditorCommand command, const keys_t& keys);
    // Back to the standard, for one command or for all, the menus' too.
    void     restore(ALEditorCommand command);
    void     restoreAll();
    // The keys as a person reads them: "Ctrl+Shift+K", or the several
    // joined.
    std::string describe(const keys_t& keys);
}
