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
}
