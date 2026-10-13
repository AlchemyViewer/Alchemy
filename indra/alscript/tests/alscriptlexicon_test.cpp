/**
 * @file alscriptlexicon_test.cpp
 * @brief Tests for ALScriptLexicon: the words and name characters of LSL and SLua.
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

#include "../core/alscriptlexicon.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("alscriptlexicon: each LSL word has one kind; keywords are control words or types or reserved", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        for (const char* word : { "default", "state", "jump", "return", "if", "else", "for", "do", "while" })
        {
            CAPTURE(word);
            REQUIRE(lslWord(word) == LSL_CONTROL);
            REQUIRE(isLslKeyword(word));
        }
        for (const char* word : { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" })
        {
            CAPTURE(word);
            REQUIRE(lslWord(word) == LSL_TYPE);
            REQUIRE(isLslType(word));
            REQUIRE(isLslKeyword(word));
        }
        CHECK(lslWord("event") == LSL_RESERVED);
        CHECK(lslWord("print") == LSL_RESERVED);
        CHECK(isLslKeyword("print"));
        CHECK(lslWord("TRUE") == LSL_CONSTANT);
        CHECK(lslWord("FALSE") == LSL_CONSTANT);
        CHECK_FALSE(isLslKeyword("TRUE"));
        for (const char* word : { "inline", "const", "break", "continue", "switch", "case" })
        {
            CAPTURE(word);
            REQUIRE(lslWord(word) == LSL_EXTENSION);
            REQUIRE_FALSE(isLslKeyword(word));
        }
        CHECK(lslWord("count") == LSL_NAME);
        CHECK(lslWord("llSay") == LSL_NAME);
        CHECK(lslWord("") == LSL_NAME);
        {
            INFO("case matters");
            CHECK(lslWord("Integer") == LSL_NAME);
            CHECK(lslWord("true") == LSL_NAME);
        }
    }

    TEST_CASE("alscriptlexicon: Luau's reserved and statement words are keywords; type and typeof are not", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        for (const char* word : { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not",
                                  "or", "repeat", "return", "then", "true", "until", "while", "continue", "export", "const" })
        {
            CAPTURE(word);
            REQUIRE(isLuauKeyword(word));
            REQUIRE(isKeyword(true, word));
        }
        {
            INFO("type and typeof are called");
            CHECK_FALSE(isLuauKeyword("type"));
            CHECK_FALSE(isLuauKeyword("typeof"));
        }
        CHECK_FALSE(isLuauKeyword("print"));
        CHECK_FALSE(isLuauKeyword("ll"));
        {
            INFO("the language decides");
            CHECK(isKeyword(false, "state"));
            CHECK_FALSE(isKeyword(true, "state"));
            CHECK(isKeyword(true, "local"));
            CHECK_FALSE(isKeyword(false, "local"));
        }
    }

    TEST_CASE("alscriptlexicon: a name is ASCII letters and digits and underscores; no digit first", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        CHECK(isName("_count2"));
        CHECK(isName("x"));
        CHECK(isName("llSay"));
        CHECK_FALSE(isName("2x"));
        CHECK_FALSE(isName(""));
        CHECK_FALSE(isName("a-b"));
        CHECK_FALSE(isName("a.b"));
        CHECK_FALSE(isName("caf\xc3\xa9"));
        CHECK(isNameStart('_'));
        CHECK_FALSE(isNameStart('7'));
        CHECK(isNameByte('7'));
        CHECK_FALSE(isNameByte('$'));
    }

    TEST_CASE("alscriptlexicon: a long bracket's level and where its closer of that level ends", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        CHECK(longBracketLevel("x = [[a]]", 4) == 0);
        CHECK(longBracketLevel("[==[a]==]", 0) == 2);
        UNSCOPED_INFO("not one");
        CHECK(longBracketLevel("[=a", 0) == -1);
        UNSCOPED_INFO("not a bracket");
        CHECK(longBracketLevel("a[[", 0) == -1);
        UNSCOPED_INFO("past the end");
        CHECK(longBracketLevel("[[", 5) == -1);
        UNSCOPED_INFO("the closer of its level, not another's");
        CHECK(longBracketClose("a]=]b]]c", 0, 0) == size_t(7));
        CHECK(longBracketClose("a]]]=]]==]x", 0, 2) == size_t(10));
        CHECK(longBracketClose("a]=]", 0, 0) == std::string_view::npos);
    }

    TEST_CASE("alscriptlexicon: LSL: strings run past escapes and breaks; comments to the break or closer", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        const std::string_view text = "a \"b\\\"c\nd\" // e\n/* f */ g /* h";
        Stretch s = stretchAt(text, 0, false);
        CHECK(s.kind == Kind::Code);
        CHECK(s.end == 1);
        s = stretchAt(text, 2, false);
        {
            INFO("the string, across the break");
            CHECK(s.kind == Kind::String);
            CHECK(s.closed);
            CHECK(s.open == 1);
            CHECK(s.close == 1);
            CHECK(text.substr(2, s.end - 2) == "\"b\\\"c\nd\"");
        }
        const size_t slash = text.find("//");
        s                  = stretchAt(text, slash, false);
        {
            INFO("the line comment, to the break");
            REQUIRE(s.kind == Kind::Comment);
            REQUIRE(s.closed);
            REQUIRE(s.close == 0);
            REQUIRE(s.end < text.size());
            CHECK(text[s.end] == '\n');
            CHECK(s.open == 2);
        }
        const size_t block = text.find("/*");
        s                  = stretchAt(text, block, false);
        CHECK(s.kind == Kind::Comment);
        CHECK(s.closed);
        CHECK(s.close == 2);
        CHECK(text.substr(block, s.end - block) == "/* f */");
        s = stretchAt(text, text.rfind("/*"), false);
        {
            INFO("a block comment the text runs out in");
            CHECK(s.kind == Kind::Comment);
            CHECK_FALSE(s.closed);
            CHECK(s.end == text.size());
            CHECK(s.close == 0);
        }
        UNSCOPED_INFO("no char constants");
        CHECK(stretchAt("'x'", 0, false).kind == Kind::Code);
        UNSCOPED_INFO("no -- comment");
        CHECK(stretchAt("--x", 0, false).kind == Kind::Code);
        CHECK_FALSE(stretchAt("\"abc", 0, false).closed);
    }

    TEST_CASE("alscriptlexicon: SLua: quoted strings and line comments end at the line; long ones at closers", "[alscriptlexicon]")
    {
        using namespace ALScriptLexicon;
        for (const char* quoted : { "\"a\\\"b\"", "'a\\'b'", "`a{b}c`" })
        {
            const Stretch s = stretchAt(quoted, 0, true);
            CAPTURE(quoted);
            REQUIRE(s.kind == Kind::String);
            REQUIRE(s.closed);
            REQUIRE(s.end == std::string_view(quoted).size());
        }
        Stretch s = stretchAt("'abc\nd'", 0, true);
        {
            INFO("a quoted string ends at its break");
            CHECK(s.kind == Kind::String);
            CHECK_FALSE(s.closed);
            CHECK(s.end == 4);
        }
        s = stretchAt("'a\\\nb'", 0, true);
        {
            INFO("but for an escaped one");
            CHECK(s.closed);
            CHECK(s.end == 6);
        }
        s = stretchAt("[==[a]]b]==]c", 0, true);
        CHECK(s.kind == Kind::String);
        CHECK(s.closed);
        CHECK(s.open == 4);
        CHECK(s.close == 4);
        CHECK(s.end == 12);
        s = stretchAt("--[[a\nb]] c", 0, true);
        CHECK(s.kind == Kind::Comment);
        CHECK(s.closed);
        CHECK(s.open == 4);
        CHECK(s.close == 2);
        CHECK(s.end == 9);
        s = stretchAt("-- a\nb", 0, true);
        CHECK(s.kind == Kind::Comment);
        CHECK(s.closed);
        CHECK(s.close == 0);
        CHECK(s.end == 4);
        s = stretchAt("--[[ a", 0, true);
        {
            INFO("a long comment the text runs out in");
            CHECK(s.kind == Kind::Comment);
            CHECK_FALSE(s.closed);
            CHECK(s.end == 6);
        }
        UNSCOPED_INFO("[ alone is code");
        CHECK(stretchAt("[1]", 0, true).kind == Kind::Code);
        UNSCOPED_INFO("no // comment");
        CHECK(stretchAt("//x", 0, true).kind == Kind::Code);
    }
}
