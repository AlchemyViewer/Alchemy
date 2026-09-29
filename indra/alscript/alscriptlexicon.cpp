/**
 * @file alscriptlexicon.cpp
 * @brief The words and name characters of LSL and SLua, written once.
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

#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace ALScriptLexicon
{
    U8 lslWord(std::string_view word)
    {
        static const boost::unordered_flat_map<std::string_view, U8> WORDS = {
            { "default", LSL_CONTROL },    { "state", LSL_CONTROL },    { "jump", LSL_CONTROL },       { "return", LSL_CONTROL },
            { "if", LSL_CONTROL },         { "else", LSL_CONTROL },     { "for", LSL_CONTROL },        { "do", LSL_CONTROL },
            { "while", LSL_CONTROL },      { "integer", LSL_TYPE },     { "float", LSL_TYPE },         { "string", LSL_TYPE },
            { "key", LSL_TYPE },           { "vector", LSL_TYPE },      { "rotation", LSL_TYPE },      { "quaternion", LSL_TYPE },
            { "list", LSL_TYPE },          { "event", LSL_RESERVED },   { "print", LSL_RESERVED },     { "TRUE", LSL_CONSTANT },
            { "FALSE", LSL_CONSTANT },     { "inline", LSL_EXTENSION }, { "const", LSL_EXTENSION },    { "break", LSL_EXTENSION },
            { "continue", LSL_EXTENSION }, { "switch", LSL_EXTENSION }, { "case", LSL_EXTENSION },
        };
        const auto found = WORDS.find(word);
        return found == WORDS.end() ? LSL_NAME : found->second;
    }

    bool isLuauKeyword(std::string_view word)
    {
        static const boost::unordered_flat_set<std::string_view> WORDS = {
            "and", "break", "do",   "else",   "elseif", "end",  "false", "for",   "function", "if",       "in",     "local",
            "nil", "not",   "or",   "repeat", "return", "then", "true",  "until", "while",    "continue", "export", "const",
        };
        return WORDS.contains(word);
    }

    bool isName(std::string_view word)
    {
        return !word.empty() && isNameStart(word.front()) && std::all_of(word.begin(), word.end(), isNameByte);
    }
}
