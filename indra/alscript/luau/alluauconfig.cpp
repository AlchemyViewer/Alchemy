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

#include "alscriptlintpass.h"

#include "Luau/Config.h"
#include "Luau/Lexer.h"

#include <algorithm>
#include <cctype>

namespace
{
    // What a file's "lint" object says of the studio's own lints, in the
    // order it says it: each Sl... name and "*", with its value.
    using Said = std::vector<std::pair<std::string, std::string>>;

    // The file's text with its Sl... lint entries blanked -- each key, its
    // value and the comma that parts it from the rest -- so that Luau's
    // parser, which knows none of them, reads the rest, and its words on
    // it keep their lines. Read as Luau reads the file: Luau's own tokens,
    // with // comments passed over. Where the text stops being a
    // configuration, what was read to there is blanked and the rest left
    // for Luau to say what is wrong with.
    std::string withoutSl(std::string_view text, Said& said)
    {
        std::string         out(text);
        std::vector<size_t> starts{ 0 };
        for (size_t at = text.find('\n'); at != std::string_view::npos; at = text.find('\n', at + 1))
        {
            starts.push_back(at + 1);
        }
        const auto offset = [&](const Luau::Position& p) {
            return p.line < starts.size() ? std::min(text.size(), starts[p.line] + p.column) : text.size();
        };
        const auto blank = [&](const Luau::Position& from, const Luau::Position& to) {
            for (size_t at = offset(from); at < offset(to); ++at)
            {
                if (out[at] != '\n')
                {
                    out[at] = ' ';
                }
            }
        };

        Luau::Allocator    allocator;
        Luau::AstNameTable names(allocator);
        Luau::Lexer        lexer(text.data(), text.size(), names);
        const auto         next = [&] {
            lexer.next();
            while (lexer.current().type == Luau::Lexeme::FloorDiv)
            {
                lexer.nextline();
            }
        };
        next();
        if (lexer.current().type != '{')
        {
            return out;
        }
        next();
        // The keys down to here; and the comma before the entry being read,
        // where one was.
        std::vector<std::string>      keys;
        bool                          array = false;
        std::optional<Luau::Location> comma;
        for (int guard = 0; guard < 1000000; ++guard)
        {
            const Luau::Lexeme& at = lexer.current();
            if (at.type == Luau::Lexeme::Eof)
            {
                return out;
            }
            if (array)
            {
                if (at.type == ']')
                {
                    array = false;
                    keys.pop_back();
                }
                next();
                continue;
            }
            if (at.type == '}')
            {
                if (keys.empty())
                {
                    return out;
                }
                keys.pop_back();
                comma.reset();
                next();
                continue;
            }
            if (at.type == ',')
            {
                comma = at.location;
                next();
                continue;
            }
            if (at.type != Luau::Lexeme::QuotedString)
            {
                return out;
            }
            const Luau::Location key_at = at.location;
            std::string          key(at.data, at.getLength());
            next();
            if (lexer.current().type != ':')
            {
                return out;
            }
            next();
            const Luau::Lexeme& value = lexer.current();
            if (value.type == '{' || value.type == '[')
            {
                array = value.type == '[';
                keys.push_back(std::move(key));
                comma.reset();
                next();
                continue;
            }
            if (value.type != Luau::Lexeme::QuotedString && value.type != Luau::Lexeme::ReservedTrue && value.type != Luau::Lexeme::ReservedFalse)
            {
                return out;
            }
            const std::string said_value = value.type == Luau::Lexeme::QuotedString ? std::string(value.data, value.getLength())
                                                                                    : value.type == Luau::Lexeme::ReservedTrue ? "true" : "false";
            const Luau::Position value_end = value.location.end;
            next();
            const bool ours = keys.size() == 1 && keys[0] == "lint" && key.rfind("Sl", 0) == 0;
            if (keys.size() == 1 && keys[0] == "lint" && (ours || key == "*"))
            {
                said.emplace_back(key, said_value);
            }
            if (!ours)
            {
                comma.reset();
                continue;
            }
            // The comma after it goes with it; the last one's, the comma
            // before.
            if (lexer.current().type == ',')
            {
                blank(key_at.begin, lexer.current().location.end);
                next();
            }
            else
            {
                blank(comma ? comma->begin : key_at.begin, value_end);
            }
            comma.reset();
        }
        return out;
    }

    // What a file says of the studio's own lints, over what was there.
    bool applySl(const Said& said, ALLuauConfig& out, std::string& error)
    {
        for (const auto& [name, value] : said)
        {
            if (value != "true" && value != "false")
            {
                error = "Bad setting '" + value + "'.  Valid options are true and false";
                return false;
            }
            const uint64_t bits = name == "*" ? ~0ull : ALScriptLintPass::bit(name);
            if (!bits)
            {
                error = "Unknown lint " + name;
                return false;
            }
            out.slLints = value == "true" ? out.slLints | bits : out.slLints & ~bits;
        }
        return true;
    }
}

ALLuauConfig::ALLuauConfig()
{
    Luau::LintOptions defaults;
    defaults.setDefaults();
    lints   = defaults.warningMask;
    slLints = ALScriptLintPass::defaults();
}

// static
bool ALLuauConfig::parse(std::string_view text, ALLuauConfig& out, std::string& error, const ALLuauConfig* base)
{
    out = base ? *base : ALLuauConfig();
    Luau::Config       config;
    if (base)
    {
        config.enabledLint.warningMask = base->lints;
        config.fatalLint.warningMask   = base->fatalLints;
        config.lintErrors              = base->lintErrors;
        // Added to, as Luau adds a file's globals to the ones above it.
        config.globals                 = base->globals;
    }
    Luau::ConfigOptions options;
    // Aliases are taken however they are cased in the file.
    options.aliasOptions = Luau::ConfigOptions::AliasOptions{ std::nullopt, true };
    Said said;
    if (std::optional<std::string> failed = Luau::parseConfig(withoutSl(text, said), config, options))
    {
        error = *failed;
        return false;
    }
    if (!applySl(said, out, error))
    {
        return false;
    }
    for (const auto& [name, info] : config.aliases)
    {
        out.aliases[name] = info.value;
    }
    out.lints      = config.enabledLint.warningMask;
    out.fatalLints = config.fatalLint.warningMask;
    out.lintErrors = config.lintErrors;
    out.globals    = config.globals;
    switch (config.mode)
    {
        case Luau::Mode::Strict:    out.mode = "strict";    break;
        case Luau::Mode::Nonstrict: out.mode = "nonstrict"; break;
        case Luau::Mode::NoCheck:   out.mode = "nocheck";   break;
        default:                    break;
    }
    // Luau's default mode is nonstrict whether or not the file said so;
    // only what the file says is reported, else the base's.
    if (text.find("languageMode") == std::string_view::npos)
    {
        out.mode = base ? base->mode : std::string();
    }
    error.clear();
    return true;
}

// static
bool ALLuauConfig::parseChain(const std::vector<std::string_view>& nearest_first, ALLuauConfig& out, const ALLuauConfig* base)
{
    out      = base ? *base : ALLuauConfig();
    bool any = false;
    for (auto it = nearest_first.rbegin(); it != nearest_first.rend(); ++it)
    {
        ALLuauConfig next;
        std::string  error;
        if (parse(*it, next, error, &out))
        {
            out = std::move(next);
            any = true;
        }
    }
    return any;
}

// static
bool ALLuauConfig::studioAliasName(std::string_view name)
{
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty() || lower == "self" || lower.compare(0, 3, "sl-") == 0 || !Luau::isValidAlias(lower) || lower.front() == '@')
    {
        return false;
    }
    return true;
}

// static
std::string ALLuauConfig::studioAliasFor(std::string_view folder_name, const std::vector<std::string>& taken)
{
    std::string name;
    for (const char c : folder_name)
    {
        const bool kept = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        name += kept ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
    }
    if (!studioAliasName(name))
    {
        name = "lib";
    }
    const auto is_taken = [&taken](const std::string& one) {
        return std::any_of(taken.begin(), taken.end(), [&one](const std::string& other) {
            return other.size() == one.size() && std::equal(other.begin(), other.end(), one.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   });
        });
    };
    std::string out = name;
    for (int count = 2; is_taken(out); ++count)
    {
        out = name + std::to_string(count);
    }
    return out;
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

// static
const std::vector<std::string>& ALLuauConfig::lintNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (int code = Luau::LintWarning::Code_Unknown + 1; code < Luau::LintWarning::Code__Count; ++code)
        {
            out.emplace_back(Luau::LintWarning::getName(static_cast<Luau::LintWarning::Code>(code)));
        }
        return out;
    }();
    return names;
}

// static
uint64_t ALLuauConfig::lintBit(std::string_view name)
{
    const Luau::LintWarning::Code code = Luau::LintWarning::parseName(std::string(name).c_str());
    return code == Luau::LintWarning::Code_Unknown ? 0 : (1ull << code);
}
