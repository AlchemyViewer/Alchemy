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

#include "../alsyntaxgrammar.h"

#include "albigscript.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct alsyntaxgrammars_data
    {
        ALSyntaxLibrary library;

        alsyntaxgrammars_data()
        {
            for (const char* name : { "lsl", "slua", "xml", "json", "text", "config" })
            {
                std::string error;
                ensure(std::string(name) + " loads: " + error,
                       library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/" + name + ".xml", error));
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

        std::string lexed(const char* grammar_name, std::string_view line, ALSyntaxState& state, const ALSyntaxWords& words)
        {
            std::shared_ptr<const ALSyntaxGrammar> grammar = library.find(grammar_name);
            ensure(std::string("grammar ") + grammar_name, grammar != nullptr);
            if (state.frames.empty())
            {
                state = grammar->initialState();
            }
            std::vector<ALSyntaxToken> tokens;
            grammar->lexLine(line, state, tokens, words);
            return said(line, tokens);
        }
    };

    typedef test_group<alsyntaxgrammars_data> alsyntaxgrammars_group;
    typedef alsyntaxgrammars_group::object    alsyntaxgrammars_object;
    alsyntaxgrammars_group                    alsyntaxgrammars_instance("alsyntaxgrammars");

    template<> template<>
    void alsyntaxgrammars_object::test<1>()
    {
        set_test_name("the six grammars load and answer to their extensions");
        ensure_equals("six", library.names().size(), size_t(6));
        ensure_equals("lsl by extension", library.forExtension(".lsl")->name(), std::string("lsl"));
        ensure_equals("luau by extension", library.forExtension("luau")->name(), std::string("slua"));
        ensure_equals("xui is xml", library.forExtension("xui")->name(), std::string("xml"));
        ensure_equals("ini is config", library.forExtension("ini")->name(), std::string("config"));
        ensure("nothing for nothing", !library.forExtension("exe"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<2>()
    {
        set_test_name("LSL");
        ALSyntaxWords words;
        words.set("function", { "llSay" });
        words.set("event", { "state_entry" });
        ALSyntaxState state;
        ensure_equals("comment", lexed("lsl", "// hello", state, words), std::string("comment:// hello"));
        ensure_equals("declaration", lexed("lsl", "integer count = 0x1F;", state, words),
                      std::string("type:integer|text: count |operator:=|text: |number:0x1F|punctuation:;"));
        ensure_equals("a state", lexed("lsl", "default { state_entry() { llSay(0, \"hi \\\"there\\\"\"); @loop; jump loop; } }", state, words),
                      std::string("control:default|text: |punctuation:{|text: |event:state_entry|punctuation:()|text: |punctuation:{|text: "
                                  "|function:llSay|punctuation:(|number:0|punctuation:,|text: |string:\"hi |escape:\\\"|string:there|escape:\\\"|string:\""
                                  "|punctuation:);|text: |label:@loop|punctuation:;|text: |control:jump|text: loop|punctuation:;|text: |punctuation:}|text: |punctuation:}"));
        ensure_equals("a directive and a float", lexed("lsl", "#define X 1.5e3", state, words), std::string("preprocessor:#define X 1.5e3"));
        ensure_equals("a comment after a directive is a comment", lexed("lsl", "#define X 1 // the x", state, words),
                      std::string("preprocessor:#define X 1 |comment:// the x"));
        ensure_equals("an include's name is a path", lexed("lsl", "#include \"lib.lsl\"", state, words),
                      std::string("preprocessor:#include |path:\"lib.lsl\""));
        ensure_equals("in brackets too, and a comment after it", lexed("lsl", "#  include <lib.lsl> // the lib", state, words),
                      std::string("preprocessor:#  include |path:<lib.lsl>|preprocessor: |comment:// the lib"));
        ensure_equals("a macro's string is a string", lexed("lsl", "#define GREETING \"hello\"", state, words),
                      std::string("preprocessor:#define GREETING |string:\"hello\""));
        ensure_equals("a directive continued", lexed("lsl", "#define TWICE(a) \\", state, words), std::string("preprocessor:#define TWICE(a) \\"));
        ensure_equals("goes on onto the next line", lexed("lsl", "    ((a) * 2)", state, words), std::string("preprocessor:    ((a) * 2)"));
        ensure_equals("and ends with it", lexed("lsl", "integer y;", state, words), std::string("type:integer|text: y|punctuation:;"));
        ensure_equals("a block comment opens", lexed("lsl", "x /* y", state, words), std::string("text:x |comment:/* y"));
        ensure_equals("and closes", lexed("lsl", "z */ 2.", state, words), std::string("comment:z */|text: |number:2."));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<3>()
    {
        set_test_name("SLua");
        ALSyntaxWords words;
        words.set("function", { "Say" });
        words.set("type", { "number" });
        ALSyntaxState state;
        ensure_equals("comment", lexed("slua", "-- comment", state, words), std::string("comment:-- comment"));
        ensure_equals("a local and a block comment", lexed("slua", "local n: number = 0x10 --[[ block", state, words),
                      std::string("control:local|text: n|punctuation::|text: |type:number|text: |operator:=|text: |number:0x10|text: |comment:--[[ block"));
        ensure_equals("closed", lexed("slua", "still ]] + 1", state, words), std::string("comment:still ]]|text: |operator:+|text: |number:1"));
        ensure_equals("a call with an interpolated string", lexed("slua", "ll.Say(0, `hi {n}`)", state, words),
                      std::string("text:ll|punctuation:.|function:Say|punctuation:(|number:0|punctuation:,|text: |string:`hi |punctuation:{|text:n|punctuation:}|string:`|punctuation:)"));
        ensure_equals("a long string with a level", lexed("slua", "s = [==[ a ]] b ]==] .. 'c'", state, words),
                      std::string("text:s |operator:=|text: |string:[==[ a ]] b ]==]|text: |punctuation:..|text: |string:'c'"));
        ensure_equals("an attribute and constants", lexed("slua", "@native true nil", state, words),
                      std::string("attribute:@native|text: |constant:true|text: |constant:nil"));
        ensure_equals("a directive, written as a comment to Luau", lexed("slua", "--#define X 1.5 -- the x", state, words),
                      std::string("preprocessor:--#define X 1.5 |comment:-- the x"));
        ensure_equals("an include's name is a path", lexed("slua", "--#include \"lib.luau\"", state, words),
                      std::string("preprocessor:--#include |path:\"lib.luau\""));
        ensure_equals("selene's file-wide word is a comment", lexed("slua", "--# selene: allow(unused_variable)", state, words),
                      std::string("comment:--# selene: allow(unused_variable)"));
        ensure_equals("and so is a rule of #s", lexed("slua", "--##########", state, words), std::string("comment:--##########"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<4>()
    {
        set_test_name("XML");
        ALSyntaxWords words;
        ALSyntaxState state;
        ensure_equals("declaration", lexed("xml", "<?xml version=\"1.0\"?>", state, words), std::string("preprocessor:<?xml version=\"1.0\"?>"));
        ensure_equals("comment", lexed("xml", "<!-- note -->", state, words), std::string("comment:<!-- note -->"));
        ensure_equals("an element", lexed("xml", "<panel name=\"x\" width='2'>&amp;text</panel>", state, words),
                      std::string("tag:<panel|text: |attribute:name|operator:=|attribute_value:\"x\"|text: |attribute:width|operator:=|attribute_value:'2'"
                                  "|punctuation:>|entity:&amp;|text:text|tag:</panel|punctuation:>"));
        ensure_equals("a tag across lines opens", lexed("xml", "<button", state, words), std::string("tag:<button"));
        ensure_equals("and closes", lexed("xml", "  label=\"Go\" />", state, words), std::string("text:  |attribute:label|operator:=|attribute_value:\"Go\"|text: |punctuation:/>"));
        ensure_equals("cdata", lexed("xml", "<![CDATA[ <x> ]]>", state, words), std::string("string:<![CDATA[ <x> ]]>"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<5>()
    {
        set_test_name("JSON and plain text");
        ALSyntaxWords words;
        ALSyntaxState state;
        ensure_equals("json", lexed("json", "{\"a\": [1, true, null, \"s\\n\"]}", state, words),
                      std::string("punctuation:{|property:\"a\"|punctuation::|text: |punctuation:[|number:1|punctuation:,|text: |constant:true|punctuation:,|text: "
                                  "|constant:null|punctuation:,|text: |string:\"s|escape:\\n|string:\"|punctuation:]}"));
        ALSyntaxState plain;
        ensure_equals("text", lexed("text", "hello // world", plain, words), std::string("text:hello // world"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<6>()
    {
        set_test_name("SLua's tables name ll.Say, and Say after ll. is found by that name");
        ALSyntaxWords words;
        words.set("function", { "ll.Say", "print" });
        ALSyntaxState state;
        ensure_equals("a member call", lexed("slua", "ll.Say(0, x)", state, words),
                      std::string("text:ll|punctuation:.|function:Say|punctuation:(|number:0|punctuation:,|text: x|punctuation:)"));
        ensure_equals("the same name alone is not the function", lexed("slua", "Say(1)", state, words),
                      std::string("text:Say|punctuation:(|number:1|punctuation:)"));
        ensure_equals("another head is another name", lexed("slua", "t.Say", state, words), std::string("text:t|punctuation:.|text:Say"));
        ensure_equals("a plain function still is", lexed("slua", "print(t)", state, words), std::string("function:print|punctuation:(|text:t|punctuation:)"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<7>()
    {
        set_test_name("Luau's own words and strings: const, export and type where they are statements, and what an interpolated string holds");
        ALSyntaxWords words;
        words.set("function", { "type", "print" });
        words.set("type", { "number" });
        ALSyntaxState state;
        ensure_equals("const", lexed("slua", "const n = 1", state, words), std::string("control:const|text: n |operator:=|text: |number:1"));
        ensure_equals("export const", lexed("slua", "export const f", state, words), std::string("control:export|text: |control:const|text: f"));
        ensure_equals("export function", lexed("slua", "export function f()", state, words),
                      std::string("control:export|text: |control:function|text: f|punctuation:()"));
        ensure_equals("a type alias", lexed("slua", "type Pair = {}", state, words), std::string("control:type|text: Pair |operator:=|text: |punctuation:{}"));
        ensure_equals("an exported generic alias", lexed("slua", "export type Box<T> = T", state, words),
                      std::string("control:export|text: |control:type|text: Box|operator:<|text:T|operator:>|text: |operator:=|text: T"));
        ensure_equals("type the function is still the function", lexed("slua", "local t = type(x)", state, words),
                      std::string("control:local|text: t |operator:=|text: |function:type|punctuation:(|text:x|punctuation:)"));
        ensure("a field called type is no statement", lexed("slua", "t.type = 1", state, words).find("control:type") == std::string::npos);
        ensure_equals("a local called const is a local", lexed("slua", "const = 2", state, words), std::string("text:const |operator:=|text: |number:2"));
        ensure_equals("an interpolated string", lexed("slua", "print(`n is {n + 1} and {x}!`)", state, words),
                      std::string("function:print|punctuation:(|string:`n is |punctuation:{|text:n |operator:+|text: |number:1|punctuation:}|string: and "
                                  "|punctuation:{|text:x|punctuation:}|string:!`|punctuation:)"));
        ensure("back in the main state", state.frames.size() == 1);
        ensure_equals("an escape inside it", lexed("slua", "`a\\{b`", state, words), std::string("string:`a|escape:\\{|string:b`"));
        ensure_equals("one left open ends with its line", lexed("slua", "`open {x", state, words), std::string("string:`open |punctuation:{|text:x"));
        ensure_equals("and the next line is code again", lexed("slua", "y = 1", state, words), std::string("text:y |operator:=|text: |number:1"));
        ensure_equals("a number with separators, and floor division", lexed("slua", "1_000 // 3", state, words),
                      std::string("number:1_000|text: |operator://|text: |number:3"));
        ensure_equals("compound assignment and concatenation", lexed("slua", "s ..= \"x\"", state, words),
                      std::string("text:s |punctuation:..|operator:=|text: |string:\"x\""));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<8>()
    {
        set_test_name("a pattern a backtracking engine would take for ever over is only as long as its line; states nest so deep and no deeper; span ends are not kept without end");
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
        ensure("loads: " + error, grammar.load(description, error));
        ALSyntaxWords words;

        // Nested repeats over a long line, which Boost.Regex gave up on:
        // RE2 goes over the line once, and it lexes in its default kind.
        const std::string          long_line(4000, 'a');
        ALSyntaxState              state = grammar.initialState();
        std::vector<ALSyntaxToken> tokens;
        grammar.lexLine(long_line, state, tokens, words);
        ensure("the line covered", !tokens.empty() && tokens.front().begin == 0 && tokens.back().end == static_cast<S32>(long_line.size()));
        ensure("as text", tokens.size() == 1 && tokens.front().kind == ALSyntaxKind::Text);

        // Brackets opened past reason: the states go so deep and no deeper.
        state = grammar.initialState();
        grammar.lexLine(std::string(500, '('), state, tokens, words);
        ensure("no deeper than the depth", state.frames.size() <= ALSyntaxGrammar::MAX_DEPTH);
        grammar.lexLine(std::string(10, ')'), state, tokens, words);
        ensure("and out again", state.frames.size() < ALSyntaxGrammar::MAX_DEPTH);

        // A span whose end is its opening's capture, over many captures:
        // the end is text once the capture is known, and nothing is kept.
        for (S32 i = 0; i < 400; ++i)
        {
            state = grammar.initialState();
            grammar.lexLine("<<w" + std::to_string(i) + " x w" + std::to_string(i), state, tokens, words);
            ensure("each closes", state.frames.size() == 1);
        }

        // An end with more to it than text around the capture is a regex,
        // made from the capture as the line meets it.
        LLSD regex_states;
        regex_states["main"] = LLSD::emptyArray().with(0, LLSD().with("span_regex", "<<(\\w+)").with("end_regex", "\\1\\d+").with("kind", "string"));
        LLSD regex_description;
        regex_description["name"]   = "tested";
        regex_description["states"] = regex_states;
        ALSyntaxGrammar regex_grammar;
        ensure("loads: " + error, regex_grammar.load(regex_description, error));
        state = regex_grammar.initialState();
        regex_grammar.lexLine("<<ab x ab y", state, tokens, words);
        ensure("not closed by the capture alone", state.frames.size() == 2);
        regex_grammar.lexLine("still ab42 out", state, tokens, words);
        ensure("closed by the capture and digits", state.frames.size() == 1);
        ensure_equals("the span to there, and text after", said("still ab42 out", tokens), std::string("string:still ab42|text: out"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<9>()
    {
        set_test_name("what opens a block: in SLua a function the line leaves open, in XML a tag that does not close itself");
        std::shared_ptr<const ALSyntaxGrammar> slua = library.find("slua");
        ensure("slua indents", slua && slua->indents());
        ensure("then", slua->opensBlock("if x then"));
        ensure("a brace", slua->opensBlock("local t = {"));
        ensure("a function left open", slua->opensBlock("foo(function(x)"));
        ensure("one with its return type", slua->opensBlock("local function f(a: number): string"));
        ensure("not one that ends on the line", !slua->opensBlock("foo(function() return 1 end)"));
        ensure("the last of two, open", slua->opensBlock("f(function() end, function(x)"));
        ensure("do after a function closed on the line", slua->opensBlock("x = function() end; for i = 1, 3 do"));
        ensure("not one with end in a comment after it", !slua->opensBlock("function f() -- the end"));
        ensure("nor a name that holds end", slua->opensBlock("function f(endpoint)"));

        std::shared_ptr<const ALSyntaxGrammar> xml = library.find("xml");
        ensure("xml indents", xml && xml->indents());
        ensure("a tag", xml->opensBlock("<a>"));
        ensure("one with attributes", xml->opensBlock("<panel name=\"x\" follows=\"all\">"));
        ensure("a blank inside", xml->opensBlock("<a >"));
        ensure("not one that closes itself", !xml->opensBlock("<a/>"));
        ensure("nor with attributes", !xml->opensBlock("<a b=\"c\"/>"));
        ensure("the last tag, open", xml->opensBlock("<a/> <b>"));
        ensure("not a closing tag's line", !xml->opensBlock("</a>"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<10>()
    {
        set_test_name("SLua: what a require takes in by name is a path; any other string is a string");
        ALSyntaxWords words;
        words.set("function", { "require", "print" });
        ALSyntaxState state;
        ensure_equals("a module's path", lexed("slua", "local util = require(\"./util\")", state, words),
                      std::string("control:local|text: util |operator:=|text: |function:require|punctuation:(|path:\"./util\"|punctuation:)"));
        ensure_equals("in single quotes, spaced", lexed("slua", "require( 'lib' ).greet()", state, words),
                      std::string("function:require|punctuation:(|text: |path:'lib'|text: |punctuation:).|text:greet|punctuation:()"));
        ensure_equals("another call's string is a string", lexed("slua", "print(\"see ./util\")", state, words),
                      std::string("function:print|punctuation:(|string:\"see ./util\"|punctuation:)"));
        ensure_equals("and after a require, the line goes on as before", lexed("slua", "print(\"hi\")", state, words),
                      std::string("function:print|punctuation:(|string:\"hi\"|punctuation:)"));
    }

    template<> template<>
    void alsyntaxgrammars_object::test<11>()
    {
        set_test_name("a string one word long with a dot or a slash in it is a path, in LSL, in its directives and in SLua; a sentence or an escape is a string");
        ALSyntaxWords words;
        words.set("function", { "llHTTPRequest", "llOwnerSay", "print" });
        ALSyntaxState state;
        ensure_equals("an address", lexed("lsl", "llHTTPRequest(\"https://example.com/api\", [], \"\");", state, words),
                      std::string("function:llHTTPRequest|punctuation:(|path:\"https://example.com/api\"|punctuation:,|text: |punctuation:[],|text: "
                                  "|string:\"\"|punctuation:);"));
        ensure_equals("a file's name", lexed("lsl", "llOwnerSay(\"texture.png\");", state, words),
                      std::string("function:llOwnerSay|punctuation:(|path:\"texture.png\"|punctuation:);"));
        ensure_equals("a sentence", lexed("lsl", "llOwnerSay(\"Loaded the texture.png file.\");", state, words),
                      std::string("function:llOwnerSay|punctuation:(|string:\"Loaded the texture.png file.\"|punctuation:);"));
        ensure_equals("an escape is no slash", lexed("lsl", "llOwnerSay(\"a\\nb\");", state, words),
                      std::string("function:llOwnerSay|punctuation:(|string:\"a|escape:\\n|string:b\"|punctuation:);"));
        ensure_equals("in a directive", lexed("lsl", "#define URL \"http://example.com\"", state, words),
                      std::string("preprocessor:#define URL |path:\"http://example.com\""));
        ALSyntaxState json;
        ensure_equals("JSON, a value but not a key", lexed("json", "{\"lib\": \"./lib\", \"mode\": \"strict\"}", json, words),
                      std::string("punctuation:{|property:\"lib\"|punctuation::|text: |path:\"./lib\"|punctuation:,|text: |property:\"mode\"|punctuation::|text: "
                                  "|string:\"strict\"|punctuation:}"));
        ALSyntaxState slua;
        ensure_equals("SLua, either quote", lexed("slua", "print('lib.lua', \"a/b\")", slua, words),
                      std::string("function:print|punctuation:(|path:'lib.lua'|punctuation:,|text: |path:\"a/b\"|punctuation:)"));
    }
    template<> template<>
    void alsyntaxgrammars_object::test<12>()
    {
        set_test_name("lexed the quick way -- rules by first byte, spans' ends looked for, a word's tables in one look -- is lexed as the plain way does it, token for token and state for state");
        ALSyntaxWords words;
        words.set("function", { "llSay", "llOwnerSay", "print", "ll.Say", "tostring" });
        words.set("constant", { "TRUE", "FALSE", "PI", "ZERO_VECTOR" });
        words.set("event", { "state_entry", "touch_start" });
        words.set("type", { "number", "string" });
        words.set("deprecated", { "llSound" });
        const std::string mixed = "x = \"a\\\"b\\n\" -- c\n[[long\nstring]] --[=[ lvl\n ]] ]=] y\n"
                                  "/* block\n * more */ z = \"open\n`s {n} \\{ t` q\n<a href=\"x\">&amp;</a> {\"k\": [1, 2]}\n";
        const auto compare = [&](const char* grammar_name, const std::string& text) {
            std::shared_ptr<const ALSyntaxGrammar> grammar = library.find(grammar_name);
            ensure(std::string("grammar ") + grammar_name, grammar != nullptr);
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
                const std::string where = std::string(grammar_name) + " line " + std::to_string(line);
                ensure(where + ": the same tokens: " + alsyntaxgrammars_data::said(one, plain_tokens) + " against " +
                           alsyntaxgrammars_data::said(one, quick_tokens),
                       quick_tokens == plain_tokens);
                ensure(where + ": the same state after", quick == plain);
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
            compare(grammar_name, mixed);
        }
        const std::string lsl  = ll_test::bigLSL(3000);
        const std::string slua = ll_test::bigSLua(3000);
        compare("lsl", lsl);
        compare("lsl", "/*\n" + lsl + "\n*/");
        compare("slua", slua);
        compare("slua", "--[==[\n" + slua + "\n]==]");
        compare("slua", "x = [[\n" + slua + "\n]]");
    }

    template<> template<>
    void alsyntaxgrammars_object::test<13>()
    {
        set_test_name("a notecard of settings: comments, sections, and a key's value -- strings, numbers, keys, true and false -- to the end of its line");
        ALSyntaxWords words;
        ALSyntaxState state;
        ensure_equals("a comment", lexed("config", "# the door", state, words), std::string("comment:# the door"));
        ensure_equals("a section", lexed("config", "[Door]", state, words), std::string("type:[Door]"));
        ensure_equals("a number", lexed("config", "speed = 2.5", state, words), std::string("property:speed|punctuation: =|text: |number:2.5"));
        ensure_equals("a key and true", lexed("config", "owner: 01234567-89ab-cdef-0123-456789abcdef, true", state, words),
                      std::string("property:owner|punctuation::|text: |constant:01234567-89ab-cdef-0123-456789abcdef|punctuation:,|text: |constant:true"));
        ensure_equals("a string, and a colon in it left as text", lexed("config", "url = \"http://x\" at 10:30", state, words),
                      std::string("property:url|punctuation: =|text: |string:\"http://x\"|text: at |number:10|text::|number:30"));
        ensure_equals("the next line a key again", lexed("config", "name = Door", state, words), std::string("property:name|punctuation: =|text: Door"));
        ensure_equals("a line with no key, text", lexed("config", "just words here", state, words), std::string("text:just words here"));
    }
}
