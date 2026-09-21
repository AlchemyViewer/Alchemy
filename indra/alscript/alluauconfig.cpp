/**
 * @file alluauconfig.cpp
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

#include "linden_common.h"

#include "alluauconfig.h"

#include "Luau/Config.h"

#include <algorithm>
#include <cctype>

// static
bool ALLuauConfig::parse(std::string_view text, ALLuauConfig& out, std::string& error)
{
    out = ALLuauConfig();
    Luau::Config       config;
    Luau::ConfigOptions options;
    // Aliases are taken however they are cased in the file.
    options.aliasOptions = Luau::ConfigOptions::AliasOptions{ std::nullopt, true };
    if (std::optional<std::string> failed = Luau::parseConfig(std::string(text), config, options))
    {
        error = *failed;
        return false;
    }
    for (const auto& [name, info] : config.aliases)
    {
        out.aliases[name] = info.value;
    }
    switch (config.mode)
    {
        case Luau::Mode::Strict:    out.mode = "strict";    break;
        case Luau::Mode::Nonstrict: out.mode = "nonstrict"; break;
        case Luau::Mode::NoCheck:   out.mode = "nocheck";   break;
        default:                    break;
    }
    // Luau's default mode is nonstrict whether or not the file said so;
    // only what the file says is reported.
    if (text.find("languageMode") == std::string_view::npos)
    {
        out.mode.clear();
    }
    error.clear();
    return true;
}

// static
bool ALLuauConfig::aliasOf(std::string_view name, std::string& alias, std::string& rest)
{
    if (name.empty() || name.front() != '@')
    {
        return false;
    }
    const size_t slash = name.find_first_of("/\\");
    alias.assign(name.substr(1, slash == std::string_view::npos ? std::string_view::npos : slash - 1));
    rest.assign(slash == std::string_view::npos ? std::string_view() : name.substr(slash + 1));
    std::transform(alias.begin(), alias.end(), alias.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !alias.empty() && Luau::isValidAlias(alias);
}

// static
bool ALLuauConfig::absolute(std::string_view path)
{
    if (path.empty())
    {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\')
    {
        return true;
    }
    return path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}
