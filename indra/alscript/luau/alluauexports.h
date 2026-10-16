/**
 * @file alluauexports.h
 * @brief What a SLua module gives whoever requires it, read off its text.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luau
{
    class AstStatBlock;
}

// What a module exports, as its text says it: the names of the table it
// returns. Read by Luau's parser alone, with nothing checked -- a module
// is read for this whether or not it type checks, and many are read at
// once -- so only what the text says outright counts:
//
//   return { greet = greet, count = 3 }          -- greet, count
//   local M = { count = 3 }
//   function M.greet() end
//   function M:reset() end
//   M.name = "util"
//   return M                                     -- count, greet, reset, name
//   return setmetatable(M, meta)                 -- as `return M`
//
// A module that returns anything else -- a function, a value, a table
// built some other way -- exports no names, though it may still be
// required whole. Only names a script could write after a dot, each once,
// in the order the text gives them.
namespace ALLuauExports
{
    std::vector<std::string> of(std::string_view source);
    // The same of a module already parsed, whole: what the analysis has
    // of a module it checked, which it does not parse again for this.
    std::vector<std::string> of(const Luau::AstStatBlock& root);

    // Whether a field's name is one a script could write after a dot:
    // a name, and not one of Luau's keywords -- `util.end` does not parse.
    bool isName(std::string_view name);

    // What a module the analysis checked was found to export -- the names
    // of the table its type says it returns, whatever built it, as a parse
    // alone cannot see: one returned from a function, fields set in a loop
    // over names given outright -- kept by the module's key and the text it
    // was checked from, for a look at the same text to take in place of
    // `of`. The latest few hundred kept; any thread's.
    void                                    checked(const std::string& key, std::string_view text, std::vector<std::string> names);
    std::optional<std::vector<std::string>> checkedOf(const std::string& key, std::string_view text);
}
