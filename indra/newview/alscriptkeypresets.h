/**
 * @file alscriptkeypresets.h
 * @brief Script Studio's keys as other editors have them, to put over its own.
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

#include "alkeychord.h"

#include <string>
#include <string_view>
#include <vector>

// Script Studio's keys as other editors have them -- Visual Studio Code,
// Visual Studio, the JetBrains IDEs, Xcode -- each a set of commands and
// the keys the editor gives them as standard, put over the studio's own
// (ALScriptKeymap): what a person used to one of them reaches for does
// the same here. Only where they differ from the studio's, and only
// commands the studio has. The Mac is given Xcode and not Visual Studio,
// and the JetBrains IDEs' own Mac keys; elsewhere the reverse.
namespace ALScriptKeyPresets
{
    // A command's keys as the editor has them: an editor command's name
    // (alEditorCommandName) or a menu item's (ALScriptKeymap::menuIds).
    // An editor command answers to one key at a time; a menu item's may
    // be two in turn.
    struct Binding
    {
        std::string             id;
        std::vector<ALKeyChord> keys;
    };
    struct Preset
    {
        // Its name in the setting, and the end of its name in the Keys
        // tab's words ("preset_vscode").
        std::string          id;
        std::vector<Binding> bindings;
    };
    // Those this platform offers, the studio's own first, which changes
    // nothing.
    const std::vector<Preset>& all();
    const Preset*              find(std::string_view id);
    // The studio's own.
    inline const char* const STANDARD = "studio";
}
