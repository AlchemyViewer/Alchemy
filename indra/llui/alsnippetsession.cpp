/**
 * @file alsnippetsession.cpp
 * @brief A snippet or a call being filled in, in a code editor: its stops, their mirrors, and where the caret lands.
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

#include "alcodeeditor.h"

// static
size_t ALCodeEditor::parameterListAt(std::string_view detail, std::string_view name)
{
    if (!name.empty())
    {
        const std::string called = std::string(name) + "(";
        if (const size_t at = detail.find(called); at != std::string_view::npos)
        {
            return at + name.size();
        }
    }
    return detail.find('(');
}

// static
std::vector<std::string> ALCodeEditor::parameterNames(std::string_view detail, std::string_view name)
{
    std::vector<std::string> names;
    const size_t             open = parameterListAt(detail, name);
    if (open == std::string::npos)
    {
        return names;
    }
    // To the bracket that closes it, minding the ones inside.
    size_t close = open + 1;
    for (S32 depth = 1; close < detail.size() && depth > 0; ++close)
    {
        if (detail[close] == '(')
        {
            ++depth;
        }
        else if (detail[close] == ')')
        {
            if (--depth == 0)
            {
                break;
            }
        }
    }
    if (close >= detail.size())
    {
        return names;
    }
    const std::string_view inside = detail.substr(open + 1, close - open - 1);
    size_t                 at     = 0;
    S32                    depth  = 0;
    std::string            piece;
    auto take = [&]() {
        // "integer channel", "channel: number", "...any" or "channel".
        size_t a = piece.find_first_not_of(' ');
        size_t z = piece.find_last_not_of(' ');
        if (a == std::string::npos)
        {
            return;
        }
        std::string one = piece.substr(a, z - a + 1);
        if (const size_t colon = one.find(':'); colon != std::string::npos)
        {
            one = one.substr(0, colon);
        }
        else if (const size_t space = one.rfind(' '); space != std::string::npos)
        {
            one = one.substr(space + 1);
        }
        while (!one.empty() && one.back() == '?')
        {
            one.pop_back();
        }
        if (one.rfind("...", 0) == 0)
        {
            one = "...";
        }
        if (!one.empty())
        {
            names.push_back(one);
        }
    };
    for (; at < inside.size(); ++at)
    {
        const char c = inside[at];
        if (c == '(' || c == '<' || c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == ')' || c == '>' || c == '}' || c == ']')
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            take();
            piece.clear();
        }
        else
        {
            piece.push_back(c);
        }
    }
    take();
    return names;
}
