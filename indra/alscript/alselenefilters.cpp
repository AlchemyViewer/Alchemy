/**
 * @file alselenefilters.cpp
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


#include "linden_common.h"

#include "alselenefilters.h"

#include "alluauconfig.h"

#include <utility>

namespace
{
    std::string_view trimmed(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    // selene's checks that Luau makes too, by the names Luau gives them.
    const std::pair<const char*, const char*> SELENE_TO_LUAU[] = {
        { "unused_variable", "LocalUnused" },
        { "unused_variable", "FunctionUnused" },
        { "unused_variable", "ImportUnused" },
        { "shadowing", "LocalShadow" },
        { "undefined_variable", "UnknownGlobal" },
        { "unscoped_variables", "GlobalUsedAsLocal" },
        { "multiple_statements", "SameLineStatement" },
        { "unbalanced_assignments", "UnbalancedAssignment" },
        { "duplicate_keys", "TableLiteral" },
        { "deprecated", "DeprecatedApi" },
        { "deprecated", "DeprecatedGlobal" },
        { "incorrect_standard_library_use", "FormatString" },
        { "incorrect_standard_library_use", "TableOperations" },
        { "ifs_same_cond", "DuplicateCondition" },
        { "suspicious_reverse_loop", "ForRange" },
    };
}

// static
std::optional<ALSeleneFilters::Directive> ALSeleneFilters::read(std::string_view comment)
{
    if (comment.substr(0, 2) != "--")
    {
        return std::nullopt;
    }
    std::string_view rest = trimmed(comment.substr(2));
    Directive        out;
    if (!rest.empty() && rest.front() == '#')
    {
        out.file = true;
        rest     = trimmed(rest.substr(1));
    }
    if (rest.substr(0, 6) != "selene")
    {
        return std::nullopt;
    }
    rest = trimmed(rest.substr(6));
    if (rest.empty() || rest.front() != ':')
    {
        return std::nullopt;
    }
    rest = trimmed(rest.substr(1));
    for (const auto& [word, action] : { std::pair{ std::string_view("allow"), Action::Allow }, std::pair{ std::string_view("warn"), Action::Warn },
                                        std::pair{ std::string_view("deny"), Action::Deny } })
    {
        if (rest.substr(0, word.size()) == word)
        {
            out.action = action;
            rest       = trimmed(rest.substr(word.size()));
            break;
        }
        if (action == Action::Deny)
        {
            return std::nullopt;
        }
    }
    if (rest.empty() || rest.front() != '(')
    {
        return std::nullopt;
    }
    const size_t close = rest.find(')');
    if (close == std::string_view::npos)
    {
        return std::nullopt;
    }
    for (std::string_view names = rest.substr(1, close - 1); !names.empty();)
    {
        const size_t           comma = names.find(',');
        const std::string_view name  = trimmed(names.substr(0, comma));
        if (!name.empty())
        {
            out.lints.emplace_back(name);
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        names.remove_prefix(comma + 1);
    }
    if (out.lints.empty())
    {
        return std::nullopt;
    }
    return out;
}

// static
uint64_t ALSeleneFilters::luauLints(std::string_view name)
{
    uint64_t out = ALLuauConfig::lintBit(name);
    for (const auto& [selene, luau] : SELENE_TO_LUAU)
    {
        if (name == selene)
        {
            out |= ALLuauConfig::lintBit(luau);
        }
    }
    return out;
}

// static
uint64_t ALSeleneFilters::luauLints(const Directive& directive)
{
    uint64_t out = 0;
    for (const std::string& name : directive.lints)
    {
        out |= luauLints(name);
    }
    return out;
}
