/**
 * @file tests/alsyntaxgrammars_test.cpp
 * @brief The grammars that ship, each over a line or two of its language.
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

#include "alsyntaxgrammar.h"

#include "albigscript.h"
#include "altextview.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

namespace
{
    struct alsyntaxgrammars_data
    {
        ALSyntaxLibrary library;

        alsyntaxgrammars_data()
        {
            for (const char* name : { "lsl", "slua", "xml", "json", "text", "config" })
            {
                std::string error;
                const bool  loaded = library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/" + name + ".xml", error);
                INFO(name << " loads: " << error);
                REQUIRE(loaded);
            }
        }

        static std::string said(std::string_view line, const std::vector<ALSyntaxToken>& tokens)
        {
            std::string out;
            for (const ALSyntaxToken& token : tokens)
            {
                if (!out.empty())
                {
                    out += "|";
                }
                out += alSyntaxKindName(token.kind);
                out += ":";
                out += line.substr(token.begin, token.end - token.begin);
            }
            return out;
        }

        std::string require_lexed(const char* grammar_name, std::string_view line, ALSyntaxState& state, const ALSyntaxWords& words)
        {
            std::shared_ptr<const ALSyntaxGrammar> grammar = library.find(grammar_name);
            CAPTURE(grammar_name);
            REQUIRE(grammar != nullptr);
            if (state.frames.empty())
            {
                state = grammar->initialState();
            }
            std::vector<ALSyntaxToken> tokens;
            grammar->lexLine(line, state, tokens, words);
            return said(line, tokens);
        }
    };

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: the six grammars load and answer to their extensions", "[alsyntaxgrammars]")
    {
        REQUIRE(library.names().size() == size_t(6));
        const auto named = [this](std::string_view extension)
        {
            std::shared_ptr<const ALSyntaxGrammar> grammar = library.forExtension(extension);
            CAPTURE(extension);
            REQUIRE(grammar);
            return grammar->name();
        };
        CHECK(named(".lsl") == std::string("lsl"));
        CHECK(named("luau") == std::string("slua"));
        CHECK(named("xui") == std::string("xml"));
        CHECK(named("ini") == std::string("config"));
        REQUIRE_FALSE(library.forExtension("exe"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: LSL's comments and code and directives lex into their kinds", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "llSay" });
        words.set("event", { "state_entry" });
        ALSyntaxState state;
        CHECK(require_lexed("lsl", "// hello", state, words) == std::string("comment:// hello"));
        CHECK(require_lexed("lsl", "integer count = 0x1F;", state, words) ==
                      std::string("type:integer|text: count |operator:=|text: |number:0x1F|punctuation:;"));
        CHECK(require_lexed("lsl", "default { state_entry() { llSay(0, \"hi \\\"there\\\"\"); @loop; jump loop; } }", state, words) ==
                      std::string("control:default|text: |punctuation:{|text: |event:state_entry|punctuation:()|text: |punctuation:{|text: "
                                  "|function:llSay|punctuation:(|number:0|punctuation:,|text: |string:\"hi |escape:\\\"|string:there|escape:\\\"|string:\""
                                  "|punctuation:);|text: |label:@loop|punctuation:;|text: |control:jump|text: loop|punctuation:;|text: |punctuation:}|text: |punctuation:}"));
        CHECK(require_lexed("lsl", "#define X 1.5e3", state, words) == std::string("preprocessor:#define X 1.5e3"));
        CHECK(require_lexed("lsl", "#define X 1 // the x", state, words) ==
                      std::string("preprocessor:#define X 1 |comment:// the x"));
        CHECK(require_lexed("lsl", "#include \"lib.lsl\"", state, words) ==
                      std::string("preprocessor:#include |path:\"lib.lsl\""));
        CHECK(require_lexed("lsl", "#  include <lib.lsl> // the lib", state, words) ==
                      std::string("preprocessor:#  include |path:<lib.lsl>|preprocessor: |comment:// the lib"));
        CHECK(require_lexed("lsl", "#define GREETING \"hello\"", state, words) ==
                      std::string("preprocessor:#define GREETING |string:\"hello\""));
        {
            INFO("a directive continued onto the next line, and ending with it");
            CHECK(require_lexed("lsl", "#define TWICE(a) \\", state, words) == std::string("preprocessor:#define TWICE(a) \\"));
            CHECK(require_lexed("lsl", "    ((a) * 2)", state, words) == std::string("preprocessor:    ((a) * 2)"));
            CHECK(require_lexed("lsl", "integer y;", state, words) == std::string("type:integer|text: y|punctuation:;"));
        }
        {
            INFO("a block comment opened on one line and closed on the next");
            CHECK(require_lexed("lsl", "x /* y", state, words) == std::string("text:x |comment:/* y"));
            CHECK(require_lexed("lsl", "z */ 2.", state, words) == std::string("comment:z */|text: |number:2."));
        }
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua's comments and strings and directives lex into their kinds", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "ll.Say" });
        words.set("type", { "number" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "-- comment", state, words) == std::string("comment:-- comment"));
        {
            INFO("a long comment opened on one line and closed on the next");
            CHECK(require_lexed("slua", "local n: number = 0x10 --[[ block", state, words) ==
                          std::string("control:local|text: n|punctuation::|text: |type:number|text: |operator:=|text: |number:0x10|text: |comment:--[[ block"));
            CHECK(require_lexed("slua", "still ]] + 1", state, words) == std::string("comment:still ]]|text: |operator:+|text: |number:1"));
        }
        CHECK(require_lexed("slua", "ll.Say(0, `hi {n}`)", state, words) ==
                      std::string("text:ll|punctuation:.|function:Say|punctuation:(|number:0|punctuation:,|text: |string:`hi |punctuation:{|text:n|punctuation:}|string:`|punctuation:)"));
        CHECK(require_lexed("slua", "s = [==[ a ]] b ]==] .. 'c'", state, words) ==
                      std::string("text:s |operator:=|text: |string:[==[ a ]] b ]==]|text: |punctuation:..|text: |string:'c'"));
        CHECK(require_lexed("slua", "@native true nil", state, words) ==
                      std::string("attribute:@native|text: |constant:true|text: |constant:nil"));
        {
            INFO("a directive, written as a comment to Luau");
            CHECK(require_lexed("slua", "--#define X 1.5 -- the x", state, words) ==
                          std::string("preprocessor:--#define X 1.5 |comment:-- the x"));
        }
        CHECK(require_lexed("slua", "--#include \"lib.luau\"", state, words) ==
                      std::string("preprocessor:--#include |path:\"lib.luau\""));
        {
            INFO("--# that is no directive is a comment: selene's word, a rule of #s");
            CHECK(require_lexed("slua", "--# selene: allow(unused_variable)", state, words) ==
                          std::string("comment:--# selene: allow(unused_variable)"));
            CHECK(require_lexed("slua", "--##########", state, words) == std::string("comment:--##########"));
        }
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: XML's declarations and elements and tags across lines lex into kinds", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("xml", "<?xml version=\"1.0\"?>", state, words) == std::string("preprocessor:<?xml version=\"1.0\"?>"));
        CHECK(require_lexed("xml", "<!-- note -->", state, words) == std::string("comment:<!-- note -->"));
        CHECK(require_lexed("xml", "<panel name=\"x\" width='2'>&amp;text</panel>", state, words) ==
                      std::string("tag:<panel|text: |attribute:name|operator:=|attribute_value:\"x\"|text: |attribute:width|operator:=|attribute_value:'2'"
                                  "|punctuation:>|entity:&amp;|text:text|tag:</panel|punctuation:>"));
        {
            INFO("a tag across lines, opened and closed");
            CHECK(require_lexed("xml", "<button", state, words) == std::string("tag:<button"));
            CHECK(require_lexed("xml", "  label=\"Go\" />", state, words) == std::string("text:  |attribute:label|operator:=|attribute_value:\"Go\"|text: |punctuation:/>"));
        }
        CHECK(require_lexed("xml", "<![CDATA[ <x> ]]>", state, words) == std::string("string:<![CDATA[ <x> ]]>"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: JSON lexes into its kinds and plain text into one token", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("json", "{\"a\": [1, true, null, \"s\\n\"]}", state, words) ==
                      std::string("punctuation:{|property:\"a\"|punctuation::|text: |punctuation:[|number:1|punctuation:,|text: |constant:true|punctuation:,|text: "
                                  "|constant:null|punctuation:,|text: |string:\"s|escape:\\n|string:\"|punctuation:]}"));
        ALSyntaxState plain;
        CHECK(require_lexed("text", "hello // world", plain, words) == std::string("text:hello // world"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua's ll.Say is found as Say after ll. and not as Say alone", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "ll.Say", "print" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "ll.Say(0, x)", state, words) ==
                      std::string("text:ll|punctuation:.|function:Say|punctuation:(|number:0|punctuation:,|text: x|punctuation:)"));
        CHECK(require_lexed("slua", "Say(1)", state, words) ==
                      std::string("text:Say|punctuation:(|number:1|punctuation:)"));
        CHECK(require_lexed("slua", "t.Say", state, words) == std::string("text:t|punctuation:.|text:Say"));
        CHECK(require_lexed("slua", "print(t)", state, words) == std::string("function:print|punctuation:(|text:t|punctuation:)"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: Luau's own keywords where they are statements and its templates", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "type", "print" });
        words.set("type", { "number" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "const n = 1", state, words) == std::string("control:const|text: n |operator:=|text: |number:1"));
        CHECK(require_lexed("slua", "export const f", state, words) == std::string("control:export|text: |control:const|text: f"));
        CHECK(require_lexed("slua", "export function f()", state, words) ==
                      std::string("control:export|text: |control:function|text: f|punctuation:()"));
        CHECK(require_lexed("slua", "type Pair = {}", state, words) == std::string("control:type|text: Pair |operator:=|text: |punctuation:{}"));
        CHECK(require_lexed("slua", "export type Box<T> = T", state, words) ==
                      std::string("control:export|text: |control:type|text: Box|operator:<|text:T|operator:>|text: |operator:=|text: T"));
        {
            INFO("type called as a function is still the function");
            CHECK(require_lexed("slua", "local t = type(x)", state, words) ==
                          std::string("control:local|text: t |operator:=|text: |function:type|punctuation:(|text:x|punctuation:)"));
        }
        {
            INFO("a field called type is no statement");
            REQUIRE(require_lexed("slua", "t.type = 1", state, words).find("control:type") == std::string::npos);
        }
        {
            INFO("a local called const is a local");
            CHECK(require_lexed("slua", "const = 2", state, words) == std::string("text:const |operator:=|text: |number:2"));
        }
        CHECK(require_lexed("slua", "print(`n is {n + 1} and {x}!`)", state, words) ==
                      std::string("function:print|punctuation:(|string:`n is |punctuation:{|text:n |operator:+|text: |number:1|punctuation:}|string: and "
                                  "|punctuation:{|text:x|punctuation:}|string:!`|punctuation:)"));
        UNSCOPED_INFO("back in the main state");
        REQUIRE(state.frames.size() == 1);
        CHECK(require_lexed("slua", "`a\\{b`", state, words) == std::string("string:`a|escape:\\{|string:b`"));
        {
            INFO("a template left open ends with its line, and the next line is code again");
            CHECK(require_lexed("slua", "`open {x", state, words) == std::string("string:`open |punctuation:{|text:x"));
            CHECK(require_lexed("slua", "y = 1", state, words) == std::string("text:y |operator:=|text: |number:1"));
        }
        CHECK(require_lexed("slua", "1_000 // 3", state, words) ==
                      std::string("number:1_000|text: |operator://|text: |number:3"));
        CHECK(require_lexed("slua", "s ..= \"x\"", state, words) ==
                      std::string("text:s |punctuation:..|operator:=|text: |string:\"x\""));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: nested repeats and deep brackets and capture-ended spans stay bounded", "[alsyntaxgrammars]")
    {
        LLSD states;
        states["main"] = LLSD::emptyArray()
                             .with(0, LLSD().with("regex", "(a*)*b").with("kind", "keyword"))
                             .with(1, LLSD().with("match", "(").with("kind", "punctuation").with("push", "main"))
                             .with(2, LLSD().with("match", ")").with("kind", "punctuation").with("pop", true))
                             .with(3, LLSD().with("span_regex", "<<(\\w+)").with("end_regex", "\\1").with("kind", "string"));
        LLSD description;
        description["name"]   = "tested";
        description["states"] = states;
        ALSyntaxGrammar grammar;
        std::string     error;
        const bool      loaded = grammar.load(description, error);
        UNSCOPED_INFO("loads: " << error);
        REQUIRE(loaded);
        ALSyntaxWords words;

        // Nested repeats over a long line, which Boost.Regex gave up on:
        // RE2 goes over the line once, and it lexes in its default kind.
        const std::string          long_line(4000, 'a');
        ALSyntaxState              state = grammar.initialState();
        std::vector<ALSyntaxToken> tokens;
        grammar.lexLine(long_line, state, tokens, words);
        {
            INFO("the line covered");
            REQUIRE_FALSE(tokens.empty());
            CHECK(tokens.front().begin == 0);
            REQUIRE(tokens.back().end == static_cast<S32>(long_line.size()));
        }
        {
            INFO("as text");
            REQUIRE(tokens.size() == 1);
            CHECK(tokens.front().kind == ALSyntaxKind::Text);
        }

        // Brackets opened past reason: the states go so deep and no deeper.
        state = grammar.initialState();
        grammar.lexLine(std::string(500, '('), state, tokens, words);
        REQUIRE(state.frames.size() <= ALSyntaxGrammar::MAX_DEPTH);
        grammar.lexLine(std::string(10, ')'), state, tokens, words);
        UNSCOPED_INFO("and out again");
        REQUIRE(state.frames.size() < ALSyntaxGrammar::MAX_DEPTH);

        // A span whose end is its opening's capture, over many captures:
        // the end is text once the capture is known, and nothing is kept.
        for (S32 i = 0; i < 400; ++i)
        {
            state = grammar.initialState();
            grammar.lexLine("<<w" + std::to_string(i) + " x w" + std::to_string(i), state, tokens, words);
            CAPTURE(i);
            UNSCOPED_INFO("each closes");
            REQUIRE(state.frames.size() == 1);
        }

        // An end with more to it than text around the capture is a regex,
        // made from the capture as the line meets it.
        LLSD regex_states;
        regex_states["main"] = LLSD::emptyArray().with(0, LLSD().with("span_regex", "<<(\\w+)").with("end_regex", "\\1\\d+").with("kind", "string"));
        LLSD regex_description;
        regex_description["name"]   = "tested";
        regex_description["states"] = regex_states;
        ALSyntaxGrammar regex_grammar;
        const bool      regex_loaded = regex_grammar.load(regex_description, error);
        UNSCOPED_INFO("loads: " << error);
        REQUIRE(regex_loaded);
        state = regex_grammar.initialState();
        regex_grammar.lexLine("<<ab x ab y", state, tokens, words);
        UNSCOPED_INFO("not closed by the capture alone");
        REQUIRE(state.frames.size() == 2);
        regex_grammar.lexLine("still ab42 out", state, tokens, words);
        UNSCOPED_INFO("closed by the capture and digits");
        REQUIRE(state.frames.size() == 1);
        CHECK(said("still ab42 out", tokens) == std::string("string:still ab42|text: out"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: opensBlock: an SLua block left open on its line or an open XML tag", "[alsyntaxgrammars]")
    {
        std::shared_ptr<const ALSyntaxGrammar> slua = library.find("slua");
        {
            INFO("slua indents");
            REQUIRE(slua);
            CHECK(slua->indents());
        }
        CHECK(slua->opensBlock("if x then"));
        CHECK(slua->opensBlock("local t = {"));
        CHECK(slua->opensBlock("foo(function(x)"));
        CHECK(slua->opensBlock("local function f(a: number): string"));
        CHECK_FALSE(slua->opensBlock("foo(function() return 1 end)"));
        UNSCOPED_INFO("the last of two, open");
        CHECK(slua->opensBlock("f(function() end, function(x)"));
        UNSCOPED_INFO("do after a function closed on the line");
        CHECK(slua->opensBlock("x = function() end; for i = 1, 3 do"));
        UNSCOPED_INFO("an end in a comment after it still counts");
        CHECK_FALSE(slua->opensBlock("function f() -- the end"));
        UNSCOPED_INFO("a name that holds end does not");
        CHECK(slua->opensBlock("function f(endpoint)"));

        std::shared_ptr<const ALSyntaxGrammar> xml = library.find("xml");
        {
            INFO("xml indents");
            REQUIRE(xml);
            CHECK(xml->indents());
        }
        CHECK(xml->opensBlock("<a>"));
        CHECK(xml->opensBlock("<panel name=\"x\" follows=\"all\">"));
        CHECK(xml->opensBlock("<a >"));
        CHECK_FALSE(xml->opensBlock("<a/>"));
        CHECK_FALSE(xml->opensBlock("<a b=\"c\"/>"));
        UNSCOPED_INFO("the last tag, open");
        CHECK(xml->opensBlock("<a/> <b>"));
        CHECK_FALSE(xml->opensBlock("</a>"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua: the string a require takes is a path and any other a string", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "require", "print" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "local util = require(\"./util\")", state, words) ==
                      std::string("control:local|text: util |operator:=|text: |function:require|punctuation:(|path:\"./util\"|punctuation:)"));
        CHECK(require_lexed("slua", "require( 'lib' ).greet()", state, words) ==
                      std::string("function:require|punctuation:(|text: |path:'lib'|text: |punctuation:).|text:greet|punctuation:()"));
        CHECK(require_lexed("slua", "print(\"see ./util\")", state, words) ==
                      std::string("function:print|punctuation:(|string:\"see ./util\"|punctuation:)"));
        {
            INFO("and after a require, the line goes on as before");
            CHECK(require_lexed("slua", "print(\"hi\")", state, words) ==
                          std::string("function:print|punctuation:(|string:\"hi\"|punctuation:)"));
        }
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: a one-word string with a dot or a slash is a path but a sentence is not", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "llHTTPRequest", "llOwnerSay", "print" });
        ALSyntaxState state;
        CHECK(require_lexed("lsl", "llHTTPRequest(\"https://example.com/api\", [], \"\");", state, words) ==
                      std::string("function:llHTTPRequest|punctuation:(|path:\"https://example.com/api\"|punctuation:,|text: |punctuation:[],|text: "
                                  "|string:\"\"|punctuation:);"));
        CHECK(require_lexed("lsl", "llOwnerSay(\"texture.png\");", state, words) ==
                      std::string("function:llOwnerSay|punctuation:(|path:\"texture.png\"|punctuation:);"));
        CHECK(require_lexed("lsl", "llOwnerSay(\"Loaded the texture.png file.\");", state, words) ==
                      std::string("function:llOwnerSay|punctuation:(|string:\"Loaded the texture.png file.\"|punctuation:);"));
        {
            INFO("an escape is no slash");
            CHECK(require_lexed("lsl", "llOwnerSay(\"a\\nb\");", state, words) ==
                          std::string("function:llOwnerSay|punctuation:(|string:\"a|escape:\\n|string:b\"|punctuation:);"));
        }
        CHECK(require_lexed("lsl", "#define URL \"http://example.com\"", state, words) ==
                      std::string("preprocessor:#define URL |path:\"http://example.com\""));
        ALSyntaxState json;
        {
            INFO("JSON, a value but not a key");
            CHECK(require_lexed("json", "{\"lib\": \"./lib\", \"mode\": \"strict\"}", json, words) ==
                          std::string("punctuation:{|property:\"lib\"|punctuation::|text: |path:\"./lib\"|punctuation:,|text: |property:\"mode\"|punctuation::|text: "
                                      "|string:\"strict\"|punctuation:}"));
        }
        ALSyntaxState slua;
        CHECK(require_lexed("slua", "print('lib.lua', \"a/b\")", slua, words) ==
                      std::string("function:print|punctuation:(|path:'lib.lua'|punctuation:,|text: |path:\"a/b\"|punctuation:)"));
    }
    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: the quick lexer gives the plain one's tokens and states line for line", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "llSay", "llOwnerSay", "print", "ll.Say", "tostring" });
        words.set("constant", { "TRUE", "FALSE", "PI", "ZERO_VECTOR" });
        words.set("event", { "state_entry", "touch_start" });
        words.set("type", { "number", "string" });
        words.set("deprecated", { "llSound" });
        const std::string mixed = "x = \"a\\\"b\\n\" -- c\n[[long\nstring]] --[=[ lvl\n ]] ]=] y\n"
                                  "/* block\n * more */ z = \"open\n`s {n} \\{ t` q\n<a href=\"x\">&amp;</a> {\"k\": [1, 2]}\n";
        const auto require_lexed_alike = [&](const char* grammar_name, const std::string& text) {
            std::shared_ptr<const ALSyntaxGrammar> grammar = library.find(grammar_name);
            CAPTURE(grammar_name);
            REQUIRE(grammar != nullptr);
            ALSyntaxState              quick = grammar->initialState();
            ALSyntaxState              plain = grammar->initialState();
            std::vector<ALSyntaxToken> quick_tokens, plain_tokens;
            size_t                     at   = 0;
            S32                        line = 0;
            while (at <= text.size())
            {
                const size_t           next = text.find('\n', at);
                const std::string_view one  = std::string_view(text).substr(at, next == std::string::npos ? std::string::npos : next - at);
                ALSyntaxGrammar::sPlainLexing = true;
                grammar->lexLine(one, plain, plain_tokens, words);
                ALSyntaxGrammar::sPlainLexing = false;
                grammar->lexLine(one, quick, quick_tokens, words);
                CAPTURE(line);
                UNSCOPED_INFO("plain " << alsyntaxgrammars_data::said(one, plain_tokens) << " against quick "
                                       << alsyntaxgrammars_data::said(one, quick_tokens));
                REQUIRE(quick_tokens == plain_tokens);
                UNSCOPED_INFO("the same state after");
                REQUIRE(quick == plain);
                if (next == std::string::npos)
                {
                    break;
                }
                at = next + 1;
                ++line;
            }
        };
        for (const char* grammar_name : { "lsl", "slua", "xml", "json", "text", "config" })
        {
            require_lexed_alike(grammar_name, mixed);
        }
        const std::string lsl  = ll_test::bigLSL(3000);
        const std::string slua = ll_test::bigSLua(3000);
        require_lexed_alike("lsl", lsl);
        require_lexed_alike("lsl", "/*\n" + lsl + "\n*/");
        require_lexed_alike("slua", slua);
        require_lexed_alike("slua", "--[==[\n" + slua + "\n]==]");
        require_lexed_alike("slua", "x = [[\n" + slua + "\n]]");

        // One long line of many strings, as a minified file is: some with an
        // escaped quote, which is where their end is first found, and one
        // of many escapes whose end is far along.
        std::string minified = "[";
        for (S32 i = 0; i < 300; ++i)
        {
            minified += "{\"k" + std::to_string(i) + "\": \"v" + std::to_string(i) + (i % 3 == 0 ? "\\\"\\n" : "") + "\", \"n\": " + std::to_string(i) + "}, ";
        }
        minified += "\"";
        for (S32 i = 0; i < 200; ++i)
        {
            minified += "\\t";
        }
        minified += std::string(300, 'x') + "\"]";
        for (const char* grammar_name : { "json", "lsl", "slua", "config" })
        {
            require_lexed_alike(grammar_name, minified);
        }
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: a settings notecard's comments and sections and key values", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("config", "# the door", state, words) == std::string("comment:# the door"));
        CHECK(require_lexed("config", "[Door]", state, words) == std::string("type:[Door]"));
        CHECK(require_lexed("config", "speed = 2.5", state, words) == std::string("property:speed|punctuation: =|text: |number:2.5"));
        CHECK(require_lexed("config", "owner: 01234567-89ab-cdef-0123-456789abcdef, true", state, words) ==
                      std::string("property:owner|punctuation::|text: |constant:01234567-89ab-cdef-0123-456789abcdef|punctuation:,|text: |constant:true"));
        CHECK(require_lexed("config", "url = \"http://x\" at 10:30", state, words) ==
                      std::string("property:url|punctuation: =|text: |string:\"http://x\"|text: at |number:10|text::|number:30"));
        {
            INFO("the next line a key again");
            CHECK(require_lexed("config", "name = Door", state, words) == std::string("property:name|punctuation: =|text: Door"));
        }
        CHECK(require_lexed("config", "just words here", state, words) == std::string("text:just words here"));
        {
            INFO("after a line with no key, a key again");
            CHECK(require_lexed("config", "name = Door", state, words) == std::string("property:name|punctuation: =|text: Door"));
        }
        CHECK(require_lexed("config", "just words # here", state, words) ==
                      std::string("text:just words|comment: # here"));
        CHECK(require_lexed("config", "[Door] ; the door", state, words) ==
                      std::string("type:[Door]|comment: ; the door"));
        CHECK(require_lexed("config", "[Door] name = x", state, words) ==
                      std::string("type:[Door]|text: |property:name|punctuation: =|text: x"));
    }
    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua's i-suffixed integers are one number but a fraction takes no i", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("slua", "local n = 5i + 0x1Fi + 0b101i + 1_000i", state, words) ==
                      std::string("control:local|text: n |operator:=|text: |number:5i|text: |operator:+|text: |number:0x1Fi|text: "
                                  "|operator:+|text: |number:0b101i|text: |operator:+|text: |number:1_000i"));
        CHECK(require_lexed("slua", "print(`{5i}`)", state, words) ==
                      std::string("text:print|punctuation:(|string:`|punctuation:{|number:5i|punctuation:}|string:`|punctuation:)"));
        {
            INFO("not a fraction's, which Luau calls malformed");
            CHECK(require_lexed("slua", "x = 1.5i", state, words) ==
                          std::string("text:x |operator:=|text: |number:1.5|text:i"));
        }
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua: two dots before a number concatenate and .5 is still a number", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "print" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "print(\"n=\"..5)", state, words) ==
                      std::string("function:print|punctuation:(|string:\"n=\"|punctuation:..|number:5|punctuation:)"));
        CHECK(require_lexed("slua", "s = s.. .5", state, words) ==
                      std::string("text:s |operator:=|text: s|punctuation:..|text: |number:.5"));
        CHECK(require_lexed("slua", "x = .5 + 1.", state, words) ==
                      std::string("text:x |operator:=|text: |number:.5|text: |operator:+|text: |number:1."));
        CHECK(require_lexed("slua", "f(...)", state, words) == std::string("text:f|punctuation:(...)"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua: a line-ending backslash carries a string on but an escaped one not", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("slua", "local s = \"one\\", state, words) ==
                      std::string("control:local|text: s |operator:=|text: |string:\"one|escape:\\"));
        UNSCOPED_INFO("still in the string");
        REQUIRE(state.frames.size() == 2);
        {
            INFO("the next line closes it");
            CHECK(require_lexed("slua", "two\" .. x", state, words) == std::string("string:two\"|text: |punctuation:..|text: x"));
        }
        UNSCOPED_INFO("and code after it");
        REQUIRE(state.frames.size() == 1);
        CHECK(require_lexed("slua", "s = 'one\\", state, words) == std::string("text:s |operator:=|text: |string:'one|escape:\\"));
        {
            INFO("a single-quoted one goes on too");
            CHECK(require_lexed("slua", "two'", state, words) == std::string("string:two'"));
        }
        CHECK(require_lexed("slua", "s = 'a\\\\", state, words) == std::string("text:s |operator:=|text: |string:'a|escape:\\\\"));
        UNSCOPED_INFO("an escaped backslash ends the string with its line");
        REQUIRE(state.frames.size() == 1);
        {
            INFO("so the next is code");
            CHECK(require_lexed("slua", "y = 1", state, words) == std::string("text:y |operator:=|text: |number:1"));
        }
        CHECK(require_lexed("slua", "s = 'a\\ ", state, words) == std::string("text:s |operator:=|text: |string:'a|escape:\\ "));
        UNSCOPED_INFO("an escaped blank ends the string with its line too");
        REQUIRE(state.frames.size() == 1);
        CHECK(require_lexed("slua", "s = `one\\", state, words) ==
                      std::string("text:s |operator:=|text: |string:`one\\"));
        {
            INFO("an interpolated one goes on too");
            CHECK(require_lexed("slua", "two` y", state, words) == std::string("string:two`|text: y"));
        }
        UNSCOPED_INFO("and ends on that line");
        REQUIRE(state.frames.size() == 1);
        CHECK(require_lexed("slua", "s = `a\\\\", state, words) == std::string("text:s |operator:=|text: |string:`a|escape:\\\\"));
        UNSCOPED_INFO("an escaped backslash ends the template with its line");
        REQUIRE(state.frames.size() == 1);
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: an include or macro string left open ends with its line unless continued", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("lsl", "#include <lib", state, words) == std::string("preprocessor:#include |path:<lib"));
        UNSCOPED_INFO("a bracket left open ends with its line");
        REQUIRE(state.frames.size() == 1);
        {
            INFO("so the next line is code");
            CHECK(require_lexed("lsl", "integer x;", state, words) == std::string("type:integer|text: x|punctuation:;"));
        }
        CHECK(require_lexed("lsl", "#include \"lib", state, words) == std::string("preprocessor:#include |path:\"lib"));
        UNSCOPED_INFO("a quote left open ends with its line too");
        REQUIRE(state.frames.size() == 1);
        {
            INFO("and the next line is code");
            CHECK(require_lexed("lsl", "x = \"a\";", state, words) == std::string("text:x |operator:=|text: |string:\"a\"|punctuation:;"));
        }
        CHECK(require_lexed("lsl", "#define X \"abc", state, words) == std::string("preprocessor:#define X |string:\"abc"));
        UNSCOPED_INFO("ends with its line, and the directive with it");
        REQUIRE(state.frames.size() == 1);
        {
            INFO("code below it");
            CHECK(require_lexed("lsl", "integer y;", state, words) == std::string("type:integer|text: y|punctuation:;"));
        }
        CHECK(require_lexed("lsl", "#define GREETING \"hello \\", state, words) ==
                      std::string("preprocessor:#define GREETING |string:\"hello |escape:\\"));
        {
            INFO("a macro's string continued goes on onto the next line");
            CHECK(require_lexed("lsl", "world\"", state, words) == std::string("string:world\""));
        }
        UNSCOPED_INFO("and the directive ends with that line");
        REQUIRE(state.frames.size() == 1);
        ALSyntaxState slua;
        CHECK(require_lexed("slua", "--#include \"lib", slua, words) == std::string("preprocessor:--#include |path:\"lib"));
        UNSCOPED_INFO("SLua: a quote left open ends with its line");
        REQUIRE(slua.frames.size() == 1);
        {
            INFO("so the next line is code");
            CHECK(require_lexed("slua", "local x = 1", slua, words) == std::string("control:local|text: x |operator:=|text: |number:1"));
        }
        CHECK(require_lexed("slua", "--#define X \"abc", slua, words) == std::string("preprocessor:--#define X |string:\"abc"));
        UNSCOPED_INFO("SLua: a macro's string left open ends with its line");
        REQUIRE(slua.frames.size() == 1);
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: SLua: a member named as a global is not that global", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        words.set("function", { "type", "next", "print", "ll.Say" });
        ALSyntaxState state;
        CHECK(require_lexed("slua", "if item.type == x then", state, words) ==
                      std::string("control:if|text: item|punctuation:.|text:type |operator:==|text: x |control:then"));
        CHECK(require_lexed("slua", "node = node.next", state, words) == std::string("text:node |operator:=|text: node|punctuation:.|text:next"));
        CHECK(require_lexed("slua", "next(t)", state, words) == std::string("function:next|punctuation:(|text:t|punctuation:)"));
        {
            INFO("after a concatenation, the global still");
            CHECK(require_lexed("slua", "s = s..type(x)", state, words) ==
                          std::string("text:s |operator:=|text: s|punctuation:..|function:type|punctuation:(|text:x|punctuation:)"));
        }
        CHECK(require_lexed("slua", "ll.Say(0)", state, words) ==
                      std::string("text:ll|punctuation:.|function:Say|punctuation:(|number:0|punctuation:)"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: every kind has a name of its own that names it back and names its colour", "[alsyntaxgrammars]")
    {
        std::set<std::string> names;
        for (size_t i = 0; i < static_cast<size_t>(ALSyntaxKind::COUNT); ++i)
        {
            const ALSyntaxKind kind = static_cast<ALSyntaxKind>(i);
            const std::string  name = alSyntaxKindName(kind);
            CAPTURE(name);
            UNSCOPED_INFO("named once");
            REQUIRE(names.insert(name).second);
            REQUIRE(alSyntaxKindFromName(name) == kind);
            UNSCOPED_INFO("a colour named, but for text's");
            REQUIRE(ALTextView::kindColorName("Script", kind).empty() == (kind == ALSyntaxKind::Text));
        }
        CHECK(ALTextView::kindColorName("Script", ALSyntaxKind::Comment) == std::string("ScriptComment"));
        CHECK(ALTextView::kindColorName("Syntax", ALSyntaxKind::DocComment) == std::string("SyntaxDocComment"));
        CHECK(ALTextView::kindColorName("Syntax", ALSyntaxKind::AttributeValue) == std::string("SyntaxAttributeValue"));
        CHECK(ALTextView::kindColorName("Script", ALSyntaxKind::GlobalVariable) == std::string("ScriptGlobalVariable"));
        CHECK(ALTextView::kindColorName("Script", ALSyntaxKind::Path) == std::string("ScriptPath"));
    }

    TEST_CASE_METHOD(alsyntaxgrammars_data, "alsyntaxgrammars: a comment right after an operator is not part of the operator", "[alsyntaxgrammars]")
    {
        ALSyntaxWords words;
        ALSyntaxState state;
        CHECK(require_lexed("lsl", "x = y +/* note */ 1;", state, words) ==
                      std::string("text:x |operator:=|text: y |operator:+|comment:/* note */|text: |number:1|punctuation:;"));
        UNSCOPED_INFO("closed on its line");
        REQUIRE(state.frames.size() == 1);
        CHECK(require_lexed("lsl", "x =// the x", state, words) == std::string("text:x |operator:=|comment:// the x"));
        {
            INFO("operators side by side are still one");
            CHECK(require_lexed("lsl", "x += y != z", state, words) ==
                          std::string("text:x |operator:+=|text: y |operator:!=|text: z"));
        }
        ALSyntaxState slua;
        CHECK(require_lexed("slua", "local n = 1 +--[[ off ]] 2", slua, words) ==
                      std::string("control:local|text: n |operator:=|text: |number:1|text: |operator:+|comment:--[[ off ]]|text: |number:2"));
        CHECK(require_lexed("slua", "n = n *-- twice", slua, words) == std::string("text:n |operator:=|text: n |operator:*|comment:-- twice"));
        {
            INFO("operators side by side are still one");
            CHECK(require_lexed("slua", "n //= 2 ~= 3", slua, words) ==
                          std::string("text:n |operator://=|text: |number:2|text: |operator:~=|text: |number:3"));
        }
    }
}
