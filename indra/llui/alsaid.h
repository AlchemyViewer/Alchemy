/**
 * @file alsaid.h
 * @brief Words in the skin's language where the skin has them, else these.
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

#include "llstl.h"
#include "llstring.h"
#include "lltrans.h"
#include "llui.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <string>
#include <string_view>

// What a widget or a mode says, in the skin's words where the skin has
// them -- strings.xml, by the key -- else in the English here, which is
// also what the tests read and what a language without the key falls
// back to; [NAME]s filled in from the map either way. For words made in
// code rather than in a floater's file.
//
// The skin's string for a key is looked up once and kept: LLTrans's own
// lookup copies its defaults into a map every time it is asked, and a
// mode's banner is asked for every frame, a log of notes several
// hundred times a pass. The language does not change within a session,
// so what was found stays found. Main thread only, as the strings are.
inline const std::string& alSaidTemplate(std::string_view key, const std::string& english)
{
    static boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> found;
    if (const auto it = found.find(key); it != found.end())
    {
        return it->second;
    }
    std::string text;
    if (!LLTrans::findString(text, key))
    {
        text = english;
    }
    return found.emplace(std::string(key), std::move(text)).first->second;
}

inline std::string alSaid(const char* key, const std::string& english)
{
    return alSaidTemplate(key, english);
}

inline std::string alSaid(const char* key, const std::string& english, const LLStringUtil::format_map_t& args)
{
    std::string out = alSaidTemplate(key, english);
    if (!args.empty())
    {
        LLStringUtil::format(out, args);
    }
    return out;
}

// A counted thing, in the form its count takes in the viewer's language:
// the key with LLTrans's form suffix -- A for one, B for many in English,
// and a C where a language counts a third way -- with [COUNT] filled in,
// and any more from the map; the English for one and for many where the
// skin has no such form.
inline std::string alSaidCount(const char* key, S32 count, const std::string& english_one, const std::string& english_many,
                               LLStringUtil::format_map_t args = LLStringUtil::format_map_t())
{
    const std::string formed = std::string(key) + LLTrans::countForm(LLUI::getLanguage(), count);
    args["[COUNT]"]          = std::to_string(count);
    std::string out          = alSaidTemplate(formed, count == 1 ? english_one : english_many);
    LLStringUtil::format(out, args);
    return out;
}
