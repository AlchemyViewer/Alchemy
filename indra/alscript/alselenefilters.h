/**
 * @file alselenefilters.h
 * @brief selene's comments that allow or deny a lint, read, and its lints named as Luau's.
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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a comment for selene, the Lua linter, says of the lints -- its own
// names mapped to Luau's and the studio's own (ALScriptLintPass), which
// are what the studio's SLua checks raise, so that a script written for
// selene is checked as it asks:
//
//   -- selene: allow(unused_variable)       the code beside it
//   --# selene: allow(unused_variable, x)   the whole file, before any code
//
// `deny` and `warn` as well as `allow`. Read leniently: blanks about the
// `#` and the colon, and whatever follows the closing bracket, as a
// header written for several tools has them.
class ALSeleneFilters
{
public:
    enum class Action
    {
        Allow,
        Warn,
        Deny,
    };
    struct Directive
    {
        Action                   action = Action::Allow;
        // As written, selene's names or Luau's.
        std::vector<std::string> lints;
        // `--#`: for the whole file.
        bool                     file = false;
    };
    // The directive a comment's text is, from its `--`; nothing where it
    // is none.
    static std::optional<Directive> read(std::string_view comment);
    // The Luau lints a name stands for, as ALLuauConfig::lintBit's bits:
    // selene's where Luau has the same check -- `unused_variable` is
    // LocalUnused, FunctionUnused and ImportUnused -- and a Luau lint's own
    // name itself; none for a check Luau does not make.
    static uint64_t luauLints(std::string_view name);
    // Every name a directive gives, so.
    static uint64_t luauLints(const Directive& directive);
    // The studio's own lints a name stands for, as ALScriptLintPass's
    // bits: one's own name, or selene's for the same check.
    static uint64_t slLints(std::string_view name);
    static uint64_t slLints(const Directive& directive);
};
