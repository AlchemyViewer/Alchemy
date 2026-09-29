/**
 * @file alscriptlexicon.h
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


#pragma once

#include "stdtypes.h"

#include <string_view>

// The lexical rules of the two languages the studio edits, written once
// for everything that reads a script by hand rather than through its
// parser: which words are the language's own, and which characters make a
// name.
namespace ALScriptLexicon
{
    // What a word is in LSL: one of these, or none -- a name a script may
    // give.
    enum LslWord : U8
    {
        LSL_NAME      = 0,
        // default state jump return if else for do while: the words that
        // shape the code.
        LSL_CONTROL   = 1 << 0,
        // integer float string key vector rotation quaternion list.
        LSL_TYPE      = 1 << 1,
        // event print: reserved besides.
        LSL_RESERVED  = 1 << 2,
        // TRUE FALSE: the constants the compiler knows by name.
        LSL_CONSTANT  = 1 << 3,
        // inline const break continue switch case: the preprocessor's
        // extensions, words only while their transforms are on.
        LSL_EXTENSION = 1 << 4,
    };
    // The language's own words, which no name of a script's may be.
    constexpr U8 LSL_KEYWORD = LSL_CONTROL | LSL_TYPE | LSL_RESERVED;

    // The types, in the order LSL's documentation gives them.
    inline constexpr std::string_view LSL_TYPES[] = { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };

    U8          lslWord(std::string_view word);
    inline bool isLslKeyword(std::string_view word) { return (lslWord(word) & LSL_KEYWORD) != 0; }
    inline bool isLslType(std::string_view word) { return (lslWord(word) & LSL_TYPE) != 0; }

    // The words Luau reserves, and those it reads as a statement's first
    // word where one stands there -- continue, export and const -- which
    // the studio keeps from names too. Not type or typeof, which a script
    // calls far more often than it begins a statement with them.
    bool isLuauKeyword(std::string_view word);

    inline bool isKeyword(bool lua, std::string_view word) { return lua ? isLuauKeyword(word) : isLslKeyword(word); }

    // The characters of a name, as both languages spell one: ASCII
    // letters, digits and '_', not beginning with a digit.
    constexpr bool isNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    constexpr bool isNameByte(char c) { return isNameStart(c) || (c >= '0' && c <= '9'); }
    bool           isName(std::string_view word);
}
