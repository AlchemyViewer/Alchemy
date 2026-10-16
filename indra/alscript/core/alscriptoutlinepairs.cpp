/**
 * @file alscriptoutlinepairs.cpp
 * @brief Two outlines' functions, events and states paired by what they are.
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

#include "alscriptoutlinepairs.h"

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <string>

namespace
{
    bool paired(ALScriptSymbolKind kind)
    {
        return kind == ALScriptSymbolKind::Function || kind == ALScriptSymbolKind::Event || kind == ALScriptSymbolKind::State;
    }

    // The last line a span covers: the one its end is on, but where it
    // ends at the start of a line, the one before.
    S32 lastLine(const ALScriptSpan& span) { return span.endColumn > 0 || span.endLine <= span.line ? span.endLine : span.endLine - 1; }

    // Each symbol paired, by what it is: its kind and name after those of
    // what holds it, and which of those so called it is.
    std::vector<std::pair<std::string, const ALScriptOutlineEntry*>> keyed(const std::vector<ALScriptOutlineEntry>& outline)
    {
        std::vector<std::pair<std::string, const ALScriptOutlineEntry*>>               out;
        std::vector<std::string>                                                       holders;
        boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> seen;
        for (const ALScriptOutlineEntry& entry : outline)
        {
            // What holds it is the last symbol one shallower.
            holders.resize(static_cast<size_t>(std::clamp(entry.depth, 0, static_cast<S32>(holders.size()))));
            const std::string name =
                (holders.empty() ? std::string() : holders.back() + "/") + std::to_string(static_cast<S32>(entry.kind)) + ":" + entry.name;
            std::string key = name + "#" + std::to_string(seen[name]++);
            holders.push_back(key);
            if (paired(entry.kind))
            {
                out.emplace_back(std::move(key), &entry);
            }
        }
        return out;
    }
}

std::vector<ALScriptOutlinePairs::Pair> ALScriptOutlinePairs::pair(const std::vector<ALScriptOutlineEntry>& left, const std::vector<ALScriptOutlineEntry>& right)
{
    boost::unordered_flat_map<std::string, const ALScriptOutlineEntry*, ll::string_hash, std::equal_to<>> by_key;
    for (auto& [key, entry] : keyed(right))
    {
        by_key.emplace(std::move(key), entry);
    }
    std::vector<Pair> out;
    for (const auto& [key, entry] : keyed(left))
    {
        const auto other = by_key.find(key);
        if (other != by_key.end())
        {
            out.push_back(Pair{ entry->span.line, lastLine(entry->span), other->second->span.line, lastLine(other->second->span) });
        }
    }
    return out;
}
