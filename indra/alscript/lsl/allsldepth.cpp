/**
 * @file allsldepth.cpp
 * @brief How deep an LSL script's tree may go for alscript to walk it
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

#include "allsldepth.h"

#include <tailslide/tailslide.hh>

#include <utility>
#include <vector>

namespace
{
    // The refusal's words, which said() knows it by: the depth goes in.
    constexpr const char*      SAYING     = "Nested more than %d levels deep, too deep for Script Studio to analyze; the script still saves and compiles.";
    constexpr std::string_view SAID_START = "Nested more than ";
    constexpr std::string_view SAID_END   = " levels deep, too deep for Script Studio to analyze; the script still saves and compiles.";
}

namespace ALLSLDepth
{
    Tailslide::LSLASTNode* deeperThan(Tailslide::LSLASTNode* root, S32 limit)
    {
        if (!root)
        {
            return nullptr;
        }
        std::vector<std::pair<Tailslide::LSLASTNode*, S32>> pending{ { root, 1 } };
        while (!pending.empty())
        {
            const auto [node, depth] = pending.back();
            pending.pop_back();
            if (depth > limit)
            {
                return node;
            }
            for (Tailslide::LSLASTNode* child = node->getChild(0); child; child = child->getNext())
            {
                pending.emplace_back(child, depth + 1);
            }
        }
        return nullptr;
    }

    Tailslide::LSLScript* parse(Tailslide::ScopedScriptParser& parser, const char* text, int size, bool* deep)
    {
        if (deep)
        {
            *deep = false;
        }
        Tailslide::LSLScript* script = parser.parseLSLBytes(text, size);
        if (!script)
        {
            return nullptr;
        }
        if (Tailslide::LSLASTNode* at = deeperThan(script))
        {
            parser.logger.log(Tailslide::LOG_ERROR, at->getLoc(), SAYING, LIMIT);
            if (deep)
            {
                *deep = true;
            }
            return nullptr;
        }
        return script;
    }

    bool said(std::string_view message)
    {
        return message.starts_with(SAID_START) && message.ends_with(SAID_END);
    }
}
