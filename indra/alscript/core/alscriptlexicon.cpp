/**
 * @file alscriptlexicon.cpp
 * @brief The words, name characters, strings and comments of LSL and SLua, written once.
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

    S32 longBracketLevel(std::string_view text, size_t at)
    {
        if (at >= text.size() || text[at] != '[')
        {
            return -1;
        }
        size_t i = at + 1;
        while (i < text.size() && text[i] == '=')
        {
            ++i;
        }
        return i < text.size() && text[i] == '[' ? static_cast<S32>(i - at - 1) : -1;
    }

    size_t longBracketClose(std::string_view text, size_t from, S32 level)
    {
        for (size_t at = text.find(']', from); at != std::string_view::npos; at = text.find(']', at + 1))
        {
            size_t i = at + 1;
            while (i < text.size() && text[i] == '=')
            {
                ++i;
            }
            if (static_cast<S32>(i - at - 1) == level && i < text.size() && text[i] == ']')
            {
                return i + 1;
            }
        }
        return std::string_view::npos;
    }

    Stretch stretchAt(std::string_view text, size_t at, bool lua)
    {
        Stretch    out;
        const char c    = text[at];
        const char next = at + 1 < text.size() ? text[at + 1] : '\0';
        // A long bracket, as a string or after `--` as a comment: to its
        // closer of the same level.
        const auto long_one = [&](Kind kind, size_t opener, S32 level) {
            out.kind = kind;
            out.open = opener + static_cast<size_t>(level) + 2;
            const size_t past = longBracketClose(text, at + out.open, level);
            out.closed        = past != std::string_view::npos;
            out.end           = out.closed ? past : text.size();
            out.close         = out.closed ? static_cast<size_t>(level) + 2 : 0;
            return out;
        };
        if (c == '"' || (lua && (c == '\'' || c == '`')))
        {
            out.kind = Kind::String;
            out.open = 1;
            for (size_t i = at + 1; i < text.size(); ++i)
            {
                if (text[i] == '\\')
                {
                    ++i;
                }
                else if (text[i] == c)
                {
                    out.end   = i + 1;
                    out.close = 1;
                    return out;
                }
                else if (lua && text[i] == '\n')
                {
                    out.end    = i;
                    out.closed = false;
                    return out;
                }
            }
            out.end    = text.size();
            out.closed = false;
            return out;
        }
        if (lua && c == '[')
        {
            if (const S32 level = longBracketLevel(text, at); level >= 0)
            {
                return long_one(Kind::String, 0, level);
            }
        }
        const bool line_comment = lua ? c == '-' && next == '-' : c == '/' && next == '/';
        if (line_comment)
        {
            if (lua)
            {
                if (const S32 level = longBracketLevel(text, at + 2); level >= 0)
                {
                    return long_one(Kind::Comment, 2, level);
                }
            }
            const size_t line_end = text.find('\n', at + 2);
            out.kind              = Kind::Comment;
            out.open              = 2;
            out.end               = line_end == std::string_view::npos ? text.size() : line_end;
            return out;
        }
        if (!lua && c == '/' && next == '*')
        {
            const size_t closer = text.find("*/", at + 2);
            out.kind            = Kind::Comment;
            out.open            = 2;
            out.closed          = closer != std::string_view::npos;
            out.end             = out.closed ? closer + 2 : text.size();
            out.close           = out.closed ? 2 : 0;
            return out;
        }
        out.end = at + 1;
        return out;
    }
}
