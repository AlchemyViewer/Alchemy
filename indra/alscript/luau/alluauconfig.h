/**
 * @file alluauconfig.h
 * @brief What a .luaurc says, as far as a require needs it: its aliases and its mode.
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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A `.luaurc`, read by Luau's own parser so that what it takes is what
// Luau takes: the aliases, each a name and the path it stands for, which
// is relative to where the file is unless it is absolute, so that
// `require("@lib/util")` reads as `./lib/util` from beside the file; the
// language mode; which lints are on and which are errors; and the
// globals the script may use without declaring. In the world a
// `.luaurc` is a notecard so named in the script's folder or the nearest
// folder above it, or an item so named in its object; on disk it is the
// file so named in the script's directory or the nearest above.
struct ALLuauConfig
{
    ALLuauConfig();

    // By name, in lower case, as Luau matches them.
    std::map<std::string, std::string> aliases;
    // "strict", "nonstrict" or "nocheck", or empty where it does not say.
    std::string mode;
    // The lints on, and the ones that are errors, as Luau's masks of
    // its warning codes: Luau's defaults unless the file says.
    uint64_t lints      = 0;
    uint64_t fatalLints = 0;
    // The studio's own lints on, and the ones that are errors, as the
    // masks of ALScriptLintPass's bits: its defaults unless the file says.
    // Luau knows none of them; a file's "lint" entries named Sl... are
    // taken out before Luau reads the rest, and "*" and lintErrors say for
    // them too.
    uint64_t slLints      = 0;
    uint64_t slFatalLints = 0;
    // Every lint an error.
    bool lintErrors = false;
    // Names the script may use as globals without declaring them.
    std::vector<std::string> globals;

    // False, with Luau's own word on what is wrong, for text that is not
    // a configuration. What the file does not say is the base's, where
    // one is given -- a scripter's own choice of lints and mode, which a
    // `.luaurc` overrides key by key, or the file above this one -- else
    // Luau's defaults; its globals are added to the base's, and its
    // aliases put over them, as Luau reads a chain of files from the top.
    static bool parse(std::string_view text, ALLuauConfig& out, std::string& error, const ALLuauConfig* base = nullptr);
    // A chain of files, given nearest first, read as Luau reads one: from
    // the furthest to the nearest, each over the ones above it and over
    // the base, so that the nearest to say a thing wins and globals add
    // up. One that does not parse is passed over. False where none did,
    // and the base or the defaults.
    static bool parseChain(const std::vector<std::string_view>& nearest_first, ALLuauConfig& out, const ALLuauConfig* base = nullptr);

    // Every lint by the name a `.luaurc` gives it, in Luau's order, and
    // the bit of the masks above that one is.
    static const std::vector<std::string>& lintNames();
    static uint64_t                        lintBit(std::string_view name);

    // The alias a require name starts with -- `@lib/util` names `lib`,
    // in lower case -- and what follows it; false where the name has none.
    static bool aliasOf(std::string_view name, std::string& alias, std::string& rest);
    // Whether a path is absolute: from a root, on any platform.
    static bool absolute(std::string_view path);
    // Whether an alias's name is kept from anybody's naming, in any case:
    // `self`, which is Luau's, and `sl-*`, which is Second Life's. Neither a
    // configuration's nor the studio's, nor offered after an @.
    static bool reservedAlias(std::string_view name);
    // Whether a name may be one of Script Studio's own aliases: as Luau
    // takes an alias's name, and not reserved. And a folder's name made
    // one: lower case, what Luau does not take put as `-`, and a number
    // after it where `taken` has it in any case.
    static bool        studioAliasName(std::string_view name);
    static std::string studioAliasFor(std::string_view folder_name, const std::vector<std::string>& taken);
};
