/**
 * @file alluauconfigscript.h
 * @brief A .config.luau run in a VM of its own, and what it returns read as a .luaurc.
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

#include "stdtypes.h"

#include <string>

// A `.config.luau`: Luau's configuration as a script, which returns a
// table whose `luau` table says what a `.luaurc` says -- `languagemode`,
// `lint`, `linterrors`, `typeerrors`, `globals`, `aliases` -- in keys of
// lower case. Run as Luau runs one (Luau::extractConfig): compiled,
// sandboxed, returning exactly one table, never yielding; held to a time
// and to a size of memory. Then its `luau` table written as the `.luaurc`
// that says the same, for everything that reads one (ALLuauConfig::parse):
// the navigator, the aliases in reach, the analyzer's mode and lints.
//
// Not through Luau::extractConfig itself: Second Life's VM opens `ares`
// among its libraries -- Eris, which persists a state and unpersists
// whatever bytes it is given -- and a script's references to it are
// resolved as it loads, before any hook could take it away. Here the
// state is our own, with Luau's libraries but that one.
//
// Only a file on disk is run, in a folder whose configuration a scripter
// keeps there; never a notecard in the world.
class ALLuauConfigScript
{
public:
    static constexpr const char* NAME = ".config.luau";
    // How long a run may take, and how much memory it may hold.
    static constexpr F64    MOST_SECONDS = 0.5;
    static constexpr size_t MOST_BYTES   = 8 * 1024 * 1024;

    // The `.luaurc` a `.config.luau`'s source says the same as: `{}` where
    // it returns no `luau` table. False, with why in Luau's words where
    // they are Luau's, where it does not compile, fails, yields, takes too
    // long or too much, or returns what is no configuration. What a source
    // came to is kept, for the same text asked of again: a check asks at
    // every keystroke. Any thread's.
    static bool asLuaurc(const std::string& source, std::string& json, std::string& error);
};
