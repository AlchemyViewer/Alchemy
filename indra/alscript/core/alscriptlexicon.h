/**
 * @file alscriptlexicon.h
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


#pragma once

#include "stdtypes.h"

#include <string_view>

// The lexical rules of the two languages the studio edits, written once
// for everything that reads a script by hand rather than through its
// parser: which words are the language's own, which characters make a
// name, and where a string or a comment ends.
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

    // What the studio offers after `--!`, the comments that say how an
    // SLua script is checked: its modes, and nolint. Not optimize, until
    // what the grid does with it is known, nor native, which nothing the
    // grid or the viewer runs makes.
    inline constexpr std::string_view LUAU_HOT_COMMENTS[] = { "strict", "nonstrict", "nocheck", "nolint" };
    bool                              isLuauHotComment(std::string_view word);

    // The characters of a name, as both languages spell one: ASCII
    // letters, digits and '_', not beginning with a digit.
    constexpr bool isNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    constexpr bool isNameByte(char c) { return isNameStart(c) || (c >= '0' && c <= '9'); }
    bool           isName(std::string_view word);

    // -- strings and comments ---------------------------------------------

    // A long bracket opening at a place -- `[[`, `[==[` -- as Luau opens a
    // long string with one, and after `--` a long comment: its level, the
    // count of its equals signs; or -1, where none opens there.
    S32    longBracketLevel(std::string_view text, size_t at);
    // Past the long bracket of a level that closes at or after `from` --
    // its `]]`, its `]==]` -- or npos, where the text runs out first.
    size_t longBracketClose(std::string_view text, size_t from, S32 level);

    // What a stretch of a script's text is, read as its language's lexer
    // reads it.
    enum class Kind : U8
    {
        Code,
        String,
        Comment,
    };
    struct Stretch
    {
        Kind   kind   = Kind::Code;
        // Past its end: past a string's closing quote or bracket, past a
        // block comment's closer; at the break that ends a line comment,
        // or a Luau quoted string not closed on its line; at the text's
        // end, where it ran out first.
        size_t end    = 0;
        // Its delimiters' lengths, what it holds being between them: the
        // opener's; and the closer's, none for a line comment or one the
        // text ran out in.
        size_t open   = 0;
        size_t close  = 0;
        // Whether it was closed: false for a string or a block comment the
        // text ran out in, or a Luau quoted string its line did.
        bool   closed = true;
    };
    // The string or the comment that opens at a place, and where it ends;
    // or one byte of code, where neither does.
    // - LSL: "" strings, a backslash escaping the next byte and a break
    //   held as written; // to the line's end; /* */.
    // - SLua: "", '' and `` strings, a backslash escaping the next byte,
    //   ending at a break not escaped; [[ ]] long strings; -- to the
    //   line's end, --[[ ]] long comments. An interpolated string's
    //   expressions are string here: only the preprocessor's tokenizer
    //   reads into them.
    Stretch stretchAt(std::string_view text, size_t at, bool lua);
}
