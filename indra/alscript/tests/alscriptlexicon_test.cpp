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

#include "../alscriptlexicon.h"

#include "../test/lltut.h"

namespace tut
{
    struct alscriptlexicon_data
    {
    };
    typedef test_group<alscriptlexicon_data> alscriptlexicon_group;
    typedef alscriptlexicon_group::object    alscriptlexicon_object;
    tut::alscriptlexicon_group               alscriptlexicon_instance("alscriptlexicon");

    template<> template<>
    void alscriptlexicon_object::test<1>()
    {
        set_test_name("each LSL word is of one kind; a keyword is a control word, a type or a reserved word");
        using namespace ALScriptLexicon;
        for (const char* word : { "default", "state", "jump", "return", "if", "else", "for", "do", "while" })
        {
            ensure(std::string("control: ") + word, lslWord(word) == LSL_CONTROL && isLslKeyword(word));
        }
        for (const char* word : { "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" })
        {
            ensure(std::string("type: ") + word, lslWord(word) == LSL_TYPE && isLslType(word) && isLslKeyword(word));
        }
        ensure("event and print are reserved", lslWord("event") == LSL_RESERVED && lslWord("print") == LSL_RESERVED && isLslKeyword("print"));
        ensure("TRUE and FALSE are constants, not keywords", lslWord("TRUE") == LSL_CONSTANT && lslWord("FALSE") == LSL_CONSTANT && !isLslKeyword("TRUE"));
        for (const char* word : { "inline", "const", "break", "continue", "switch", "case" })
        {
            ensure(std::string("extension, not a keyword: ") + word, lslWord(word) == LSL_EXTENSION && !isLslKeyword(word));
        }
        ensure("a name is none of them", lslWord("count") == LSL_NAME && lslWord("llSay") == LSL_NAME && lslWord("") == LSL_NAME);
        ensure("case matters", lslWord("Integer") == LSL_NAME && lslWord("true") == LSL_NAME);
    }

    template<> template<>
    void alscriptlexicon_object::test<2>()
    {
        set_test_name("Luau's reserved words and its statement words are keywords; type and typeof are not");
        using namespace ALScriptLexicon;
        for (const char* word : { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not",
                                  "or", "repeat", "return", "then", "true", "until", "while", "continue", "export", "const" })
        {
            ensure(std::string("keyword: ") + word, isLuauKeyword(word) && isKeyword(true, word));
        }
        ensure("type and typeof are called", !isLuauKeyword("type") && !isLuauKeyword("typeof"));
        ensure("a name is not", !isLuauKeyword("print") && !isLuauKeyword("ll"));
        ensure("the language decides", isKeyword(false, "state") && !isKeyword(true, "state") && isKeyword(true, "local") && !isKeyword(false, "local"));
    }

    template<> template<>
    void alscriptlexicon_object::test<3>()
    {
        set_test_name("a name is ASCII letters, digits and underscores, not beginning with a digit");
        using namespace ALScriptLexicon;
        ensure("letters and an underscore", isName("_count2") && isName("x") && isName("llSay"));
        ensure("not a digit first", !isName("2x"));
        ensure("not empty, nor with other bytes", !isName("") && !isName("a-b") && !isName("a.b") && !isName("caf\xc3\xa9"));
        ensure("the bytes", isNameStart('_') && !isNameStart('7') && isNameByte('7') && !isNameByte('$'));
    }

    template<> template<>
    void alscriptlexicon_object::test<4>()
    {
        set_test_name("a long bracket's level, and where its closer of that level ends");
        using namespace ALScriptLexicon;
        ensure_equals("[[", longBracketLevel("x = [[a]]", 4), 0);
        ensure_equals("[==[", longBracketLevel("[==[a]==]", 0), 2);
        ensure_equals("not one", longBracketLevel("[=a", 0), -1);
        ensure_equals("not a bracket", longBracketLevel("a[[", 0), -1);
        ensure_equals("past the end", longBracketLevel("[[", 5), -1);
        ensure_equals("the closer of its level, not another's", longBracketClose("a]=]b]]c", 0, 0), size_t(7));
        ensure_equals("level two", longBracketClose("a]]]=]]==]x", 0, 2), size_t(10));
        ensure("none", longBracketClose("a]=]", 0, 0) == std::string_view::npos);
    }

    template<> template<>
    void alscriptlexicon_object::test<5>()
    {
        set_test_name("LSL: a string to its quote past escapes and breaks; a line comment to the break; a block comment to its closer");
        using namespace ALScriptLexicon;
        const std::string_view text = "a \"b\\\"c\nd\" // e\n/* f */ g /* h";
        Stretch s = stretchAt(text, 0, false);
        ensure("code, one byte", s.kind == Kind::Code && s.end == 1);
        s = stretchAt(text, 2, false);
        ensure("the string, across the break", s.kind == Kind::String && s.closed && s.open == 1 && s.close == 1 && text.substr(2, s.end - 2) == "\"b\\\"c\nd\"");
        const size_t slash = text.find("//");
        s                  = stretchAt(text, slash, false);
        ensure("the line comment, to the break", s.kind == Kind::Comment && s.closed && s.close == 0 && text[s.end] == '\n' && s.open == 2);
        const size_t block = text.find("/*");
        s                  = stretchAt(text, block, false);
        ensure("the block comment", s.kind == Kind::Comment && s.closed && s.close == 2 && text.substr(block, s.end - block) == "/* f */");
        s = stretchAt(text, text.rfind("/*"), false);
        ensure("a block comment the text runs out in", s.kind == Kind::Comment && !s.closed && s.end == text.size() && s.close == 0);
        ensure("no char constants", stretchAt("'x'", 0, false).kind == Kind::Code);
        ensure("no -- comment", stretchAt("--x", 0, false).kind == Kind::Code);
        ensure("a string not closed", !stretchAt("\"abc", 0, false).closed);
    }

    template<> template<>
    void alscriptlexicon_object::test<6>()
    {
        set_test_name("SLua: quoted strings end at their line, long strings and comments at their closer, a line comment at its break");
        using namespace ALScriptLexicon;
        for (const char* quoted : { "\"a\\\"b\"", "'a\\'b'", "`a{b}c`" })
        {
            const Stretch s = stretchAt(quoted, 0, true);
            ensure(std::string("closed: ") + quoted, s.kind == Kind::String && s.closed && s.end == std::string_view(quoted).size());
        }
        Stretch s = stretchAt("'abc\nd'", 0, true);
        ensure("a quoted string ends at its break", s.kind == Kind::String && !s.closed && s.end == 4);
        s = stretchAt("'a\\\nb'", 0, true);
        ensure("but for an escaped one", s.closed && s.end == 6);
        s = stretchAt("[==[a]]b]==]c", 0, true);
        ensure("a long string", s.kind == Kind::String && s.closed && s.open == 4 && s.close == 4 && s.end == 12);
        s = stretchAt("--[[a\nb]] c", 0, true);
        ensure("a long comment", s.kind == Kind::Comment && s.closed && s.open == 4 && s.close == 2 && s.end == 9);
        s = stretchAt("-- a\nb", 0, true);
        ensure("a line comment", s.kind == Kind::Comment && s.closed && s.close == 0 && s.end == 4);
        s = stretchAt("--[[ a", 0, true);
        ensure("a long comment the text runs out in", s.kind == Kind::Comment && !s.closed && s.end == 6);
        ensure("[ alone is code", stretchAt("[1]", 0, true).kind == Kind::Code);
        ensure("no // comment", stretchAt("//x", 0, true).kind == Kind::Code);
    }
}
