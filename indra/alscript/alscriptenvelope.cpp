/**
 * @file alscriptenvelope.cpp
 * @brief The preprocessor's envelope: the source kept in a comment ahead of what it expanded to.
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

#include "alscriptenvelope.h"

#include "alpreprocessor.h"

#include <algorithm>
#include <vector>

namespace
{
    // Firestorm's, byte for byte.
    const std::string_view LSL_START     = "//start_unprocessed_text\n/*";
    const std::string_view LSL_END       = "*/\n//end_unprocessed_text";
    const std::string_view LUA_START     = "--start_unprocessed_text\n--[";
    const std::string_view LUA_END_LINE  = "--end_unprocessed_text";
    const std::string_view VERSION_LINE  = "nfo_preprocessor_version ";
    const std::string_view PROGRAM_LINE  = "program_version ";
    const std::string_view COMPILED_LINE = "last_compiled ";

    bool startsWith(std::string_view text, std::string_view head)
    {
        return text.size() >= head.size() && text.compare(0, head.size(), head) == 0;
    }

    // One line of the header after the source block: the comment lead,
    // then a known word. Answers the rest of the line and steps past it.
    bool takeLine(std::string_view& rest, std::string_view lead, std::string_view word, std::string& value)
    {
        if (!startsWith(rest, lead))
        {
            return false;
        }
        std::string_view line = rest.substr(lead.size());
        if (!startsWith(line, word))
        {
            return false;
        }
        const size_t end = line.find('\n');
        value.assign(line.substr(word.size(), end == std::string_view::npos ? std::string_view::npos : end - word.size()));
        rest.remove_prefix(lead.size() + (end == std::string_view::npos ? line.size() : end + 1));
        return true;
    }

    // A target line: the lead, the word, and nothing else on the line.
    bool takeTarget(std::string_view& rest, std::string_view lead, std::string& target)
    {
        for (std::string_view word : { std::string_view("mono"), std::string_view("lsl2"), std::string_view("luau") })
        {
            if (startsWith(rest, lead) && startsWith(rest.substr(lead.size()), word))
            {
                const size_t after = lead.size() + word.size();
                if (after == rest.size() || rest[after] == '\n' || rest[after] == '\r')
                {
                    target.assign(word);
                    const size_t end = rest.find('\n', after);
                    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
                    return true;
                }
            }
        }
        return false;
    }

    // The smallest long-bracket level whose closer first turns up where
    // wrap() puts it, after the source: not inside the source, and not
    // across the join either, as a source ending in ] would make ]] with
    // the level-zero closer's first ] and close the comment a byte early.
    S32 luaBracketLevel(std::string_view source)
    {
        std::string joined(source);
        for (S32 level = 0;; ++level)
        {
            const std::string closer = "]" + std::string(static_cast<size_t>(level), '=') + "]";
            joined.resize(source.size());
            joined += closer;
            if (joined.find(closer) == source.size())
            {
                return level;
            }
        }
    }
}

std::string ALScriptEnvelope::encodeSource(std::string_view source)
{
    // ([/*])(?=[/*|]) -> $1|
    std::string out;
    out.reserve(source.size() + source.size() / 16);
    for (size_t i = 0; i < source.size(); ++i)
    {
        const char c = source[i];
        out.push_back(c);
        if ((c == '/' || c == '*') && i + 1 < source.size())
        {
            const char next = source[i + 1];
            if (next == '/' || next == '*' || next == '|')
            {
                out.push_back('|');
            }
        }
    }
    return out;
}

std::string ALScriptEnvelope::decodeSource(std::string_view encoded)
{
    // ([/*])\| -> $1
    std::string out;
    out.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        const char c = encoded[i];
        out.push_back(c);
        if ((c == '/' || c == '*') && i + 1 < encoded.size() && encoded[i + 1] == '|')
        {
            ++i;
        }
    }
    return out;
}

std::string ALScriptEnvelope::directiveOf(std::string_view text, bool lua)
{
    const std::string_view lead = lua ? "--" : "//";
    for (std::string_view word : { std::string_view("mono"), std::string_view("lsl2"), std::string_view("luau") })
    {
        const std::string line = std::string(lead) + std::string(word) + "\n";
        const size_t      at   = text.find(line);
        if (at != std::string_view::npos && (at == 0 || text[at - 1] == '\n'))
        {
            return std::string(word);
        }
    }
    return std::string();
}

bool ALScriptEnvelope::looksWrapped(std::string_view asset)
{
    return startsWith(asset, LSL_START) || startsWith(asset, LUA_START);
}

std::optional<ALScriptEnvelope> ALScriptEnvelope::parse(std::string_view asset)
{
    ALScriptEnvelope envelope;
    std::string_view rest;
    if (startsWith(asset, LSL_START))
    {
        const size_t end = asset.find(LSL_END, LSL_START.size());
        if (end == std::string_view::npos)
        {
            return std::nullopt;
        }
        envelope.source = decodeSource(asset.substr(LSL_START.size(), end - LSL_START.size()));
        rest            = asset.substr(end + LSL_END.size());
    }
    else if (startsWith(asset, LUA_START))
    {
        // --[==[ ... ]==]\n--end_unprocessed_text
        size_t level = 0;
        size_t at    = LUA_START.size();
        while (at < asset.size() && asset[at] == '=')
        {
            ++level;
            ++at;
        }
        if (at >= asset.size() || asset[at] != '[')
        {
            return std::nullopt;
        }
        ++at;
        const std::string closer = "]" + std::string(level, '=') + "]\n" + std::string(LUA_END_LINE);
        const size_t      end    = asset.find(closer, at);
        if (end == std::string_view::npos)
        {
            return std::nullopt;
        }
        envelope.lua    = true;
        envelope.source = std::string(asset.substr(at, end - at));
        rest            = asset.substr(end + closer.size());
    }
    else
    {
        return std::nullopt;
    }
    if (startsWith(rest, "\n"))
    {
        rest.remove_prefix(1);
    }
    else if (startsWith(rest, "\r\n"))
    {
        rest.remove_prefix(2);
    }

    const std::string_view lead = envelope.lua ? "--" : "//";
    std::string            value;
    if (takeLine(rest, lead, VERSION_LINE, value))
    {
        // Version 0 is the only one there has been.
    }
    if (takeLine(rest, lead, PROGRAM_LINE, value))
    {
        envelope.programVersion = value;
    }
    if (takeLine(rest, lead, COMPILED_LINE, value))
    {
        envelope.lastCompiled = value;
    }
    takeTarget(rest, lead, envelope.compileTarget);
    envelope.expanded = std::string(rest);
    return envelope;
}

std::string ALScriptEnvelope::wrap() const
{
    const std::string lead = lua ? "--" : "//";
    std::string       out;
    if (lua)
    {
        const std::string level(static_cast<size_t>(luaBracketLevel(source)), '=');
        out += "--start_unprocessed_text\n--[" + level + "[" + source + "]" + level + "]\n--end_unprocessed_text";
    }
    else
    {
        out += std::string(LSL_START) + encodeSource(source) + std::string(LSL_END);
    }
    out += "\n" + lead + std::string(VERSION_LINE) + "0";
    out += "\n" + lead + std::string(PROGRAM_LINE) + programVersion;
    out += "\n" + lead + std::string(COMPILED_LINE) + lastCompiled;
    out += "\n";
    if (!compileTarget.empty())
    {
        out += lead + compileTarget + "\n";
    }
    out += expanded;
    return out;
}

namespace
{
    typedef std::vector<ALPreprocessor::Token> Tokens;

    // The tokens that say something: spacing and comments aside.
    Tokens meaningful(std::string_view text, bool lua)
    {
        Tokens out;
        for (ALPreprocessor::Token& t : ALPreprocessor::tokenize(text, lua))
        {
            if (t.kind != ALPreprocessor::Token::Kind::Space && t.kind != ALPreprocessor::Token::Kind::Newline &&
                t.kind != ALPreprocessor::Token::Kind::Comment)
            {
                out.push_back(std::move(t));
            }
        }
        return out;
    }

    bool same(const ALPreprocessor::Token& a, const ALPreprocessor::Token& b) { return a.kind == b.kind && a.text == b.text; }

    // An LSL script's top-level parts -- a global, a function, a state --
    // each as the tokens it runs over: to a `;` at the top, or to the `}`
    // that brings the braces back to none.
    std::vector<std::pair<size_t, size_t>> parts(const Tokens& tokens)
    {
        std::vector<std::pair<size_t, size_t>> out;
        size_t                                 begin = 0;
        S32                                    depth = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const ALPreprocessor::Token& t = tokens[i];
            if (t.kind != ALPreprocessor::Token::Kind::Punct)
            {
                continue;
            }
            if (t.text == "{")
            {
                ++depth;
            }
            else if (t.text == "}")
            {
                if (--depth == 0)
                {
                    out.emplace_back(begin, i + 1);
                    begin = i + 1;
                }
            }
            else if (t.text == ";" && depth == 0)
            {
                out.emplace_back(begin, i + 1);
                begin = i + 1;
            }
        }
        if (begin < tokens.size())
        {
            out.emplace_back(begin, tokens.size());
        }
        return out;
    }
}

// static
bool ALScriptEnvelope::compiledFrom(std::string_view expansion, std::string_view compiled, bool lua)
{
    const Tokens made = meaningful(expansion, lua);
    const Tokens kept = meaningful(compiled, lua);
    if (lua)
    {
        // Nothing optimizes SLua: the same tokens, or not.
        return std::equal(made.begin(), made.end(), kept.begin(), kept.end(), same);
    }
    // Each part kept is the next of the expansion's that is the same to the
    // token; the ones passed over are what an optimizer left out.
    const auto made_parts = parts(made);
    const auto kept_parts = parts(kept);
    size_t     next       = 0;
    for (const auto& [kept_begin, kept_end] : kept_parts)
    {
        bool found = false;
        for (; next < made_parts.size() && !found; ++next)
        {
            const auto& [made_begin, made_end] = made_parts[next];
            found = std::equal(made.begin() + made_begin, made.begin() + made_end, kept.begin() + kept_begin, kept.begin() + kept_end, same);
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

// static
bool ALScriptEnvelope::comparable(std::string_view source, bool lua, bool compiled_here, bool transformed)
{
    if (!compiled_here && transformed)
    {
        return false;
    }
    static const char* const DIFFERS[] = { "__DATE__",   "__TIME__",      "__UNIXTIME__", "__AGENTKEY__",  "__AGENTID__",
                                           "__AGENTIDRAW__", "__AGENTNAME__", "__ASSETID__", "__FILE__", "__SHORTFILE__" };
    for (const ALPreprocessor::Token& t : ALPreprocessor::tokenize(source, lua))
    {
        if (t.kind == ALPreprocessor::Token::Kind::Ident &&
            std::find(std::begin(DIFFERS), std::end(DIFFERS), t.text) != std::end(DIFFERS))
        {
            return false;
        }
    }
    return true;
}
