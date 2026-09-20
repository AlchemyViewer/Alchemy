/**
 * @file tests/alsyntaxhighlighter_test.cpp
 * @brief The grammar engine and the highlighter over a document.
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

#include "../alsyntaxhighlighter.h"

#include "../test/lltut.h"

#include <initializer_list>
#include <string>
#include <utility>

namespace tut
{
    struct alsyntaxhighlighter_data
    {
        static LLSD rule(std::initializer_list<std::pair<const char*, LLSD>> fields)
        {
            LLSD out;
            for (const auto& field : fields)
            {
                out[field.first] = field.second;
            }
            return out;
        }

        static LLSD rules(std::initializer_list<LLSD> list)
        {
            LLSD out;
            for (const LLSD& r : list)
            {
                out.append(r);
            }
            return out;
        }

        static LLSD table(const char* name, const char* kind)
        {
            return rule({ { "table", name }, { "kind", kind } });
        }

        // A little language: line and block comments, strings with an
        // escape that end with the line, numbers, words looked up in two
        // tables, two operators and some punctuation.
        static LLSD mini()
        {
            LLSD grammar;
            grammar["name"] = "mini";
            grammar["extensions"].append("mini");
            grammar["states"]["main"] = rules({
                rule({ { "match", "//" }, { "kind", "comment" }, { "push", "line_comment" } }),
                rule({ { "span", "/*" }, { "end", "*/" }, { "kind", "comment" } }),
                rule({ { "span", "\"" }, { "end", "\"" }, { "escape", "\\" }, { "kind", "string" }, { "multiline", false } }),
                rule({ { "chars", "0-9" }, { "kind", "number" } }),
                rule({ { "match", "if" }, { "whole_word", true }, { "kind", "keyword" } }),
                rule({ { "word", true }, { "tables", rules({ table("keyword", "control"), table("function", "function") }) } }),
                rule({ { "match", "==" }, { "kind", "operator" } }),
                rule({ { "match", "=" }, { "kind", "operator" } }),
                rule({ { "chars", "(){};" }, { "max", 1 }, { "kind", "punctuation" } }),
            });
            LLSD line_comment;
            line_comment["default"]           = "comment";
            line_comment["rules"]             = rules({ rule({ { "eol", true }, { "pop", true } }) });
            grammar["states"]["line_comment"] = line_comment;
            return grammar;
        }

        // Lua's long brackets: the level the opening captures is what the
        // end has to carry.
        static LLSD brackets()
        {
            LLSD grammar;
            grammar["name"]           = "brackets";
            grammar["states"]["main"] = rules({
                rule({ { "span_regex", "\\[(=*)\\[" }, { "end_regex", "\\]\\1\\]" }, { "kind", "string" } }),
            });
            return grammar;
        }

        static std::shared_ptr<const ALSyntaxGrammar> loaded(const LLSD& description)
        {
            auto        grammar = std::make_shared<ALSyntaxGrammar>();
            std::string error;
            ensure("grammar loads: " + error, grammar->load(description, error));
            return grammar;
        }

        // Every token as kind:text, joined with bars.
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

        static std::string lexed(const ALSyntaxGrammar& grammar, std::string_view line, ALSyntaxState& state, const ALSyntaxWords& words)
        {
            std::vector<ALSyntaxToken> tokens;
            grammar.lexLine(line, state, tokens, words);
            return said(line, tokens);
        }
    };

    typedef test_group<alsyntaxhighlighter_data> alsyntaxhighlighter_group;
    typedef alsyntaxhighlighter_group::object    alsyntaxhighlighter_object;
    alsyntaxhighlighter_group                    alsyntaxhighlighter_instance("alsyntaxhighlighter");

    template<> template<>
    void alsyntaxhighlighter_object::test<1>()
    {
        set_test_name("a line lexes into kinds, with words looked up in tables");
        auto          grammar = loaded(mini());
        ALSyntaxWords words;
        words.set("keyword", { "while" });
        words.set("function", { "foo" });
        ALSyntaxState state = grammar->initialState();
        ensure_equals("tokens", lexed(*grammar, "x = foo(12) // hi", state, words),
                      std::string("text:x |operator:=|text: |function:foo|punctuation:(|number:12|punctuation:)|text: |comment:// hi"));
        ensure("back in main after the line", state == grammar->initialState());
        ensure_equals("a table word", lexed(*grammar, "while", state, words), std::string("control:while"));
        ensure_equals("a whole-word literal, and not inside a word", lexed(*grammar, "if iffy", state, words), std::string("keyword:if|text: iffy"));
        ensure_equals("the longer operator first", lexed(*grammar, "a==b", state, words), std::string("text:a|operator:==|text:b"));
        ensure_equals("tables named", grammar->wordTables().size(), size_t(2));
        ensure_equals("extension", grammar->extensions().front(), std::string("mini"));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<2>()
    {
        set_test_name("a string with an escape ends with its line");
        auto          grammar = loaded(mini());
        ALSyntaxWords words;
        ALSyntaxState state = grammar->initialState();
        ensure_equals("escape inside", lexed(*grammar, "\"a\\\"b\" c", state, words), std::string("string:\"a|escape:\\\"|string:b\"|text: c"));
        ensure_equals("unterminated", lexed(*grammar, "\"open", state, words), std::string("string:\"open"));
        ensure("and closed by the line end", state == grammar->initialState());
        ensure_equals("so the next line is code", lexed(*grammar, "1", state, words), std::string("number:1"));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<3>()
    {
        set_test_name("a block comment runs on across lines and the state says so");
        auto          grammar = loaded(mini());
        ALSyntaxWords words;
        ALSyntaxState state = grammar->initialState();
        ensure_equals("opened", lexed(*grammar, "a /* b", state, words), std::string("text:a |comment:/* b"));
        ensure("still inside", state != grammar->initialState());
        ensure_equals("all comment", lexed(*grammar, "c 12", state, words), std::string("comment:c 12"));
        ensure_equals("closed", lexed(*grammar, "d */ 3", state, words), std::string("comment:d */|text: |number:3"));
        ensure("out again", state == grammar->initialState());
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<4>()
    {
        set_test_name("a span's end can carry what its opening captured");
        auto          grammar = loaded(brackets());
        ALSyntaxWords words;
        ALSyntaxState state = grammar->initialState();
        ensure_equals("level two ends at level two", lexed(*grammar, "[==[ a ]=] b ]==] c", state, words),
                      std::string("string:[==[ a ]=] b ]==]|text: c"));
        ensure_equals("open across lines", lexed(*grammar, "[=[ x", state, words), std::string("string:[=[ x"));
        ensure_equals("with the level in the state", state.frames.back().payload, std::string("="));
        ensure_equals("and closed on the next", lexed(*grammar, "y ]] ]=] z", state, words), std::string("string:y ]] ]=]|text: z"));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<5>()
    {
        set_test_name("a description that does not hold together is refused with its reason");
        std::string     error;
        ALSyntaxGrammar grammar;
        LLSD            bad = mini();
        bad["states"]["main"][0]["kind"] = "purple";
        ensure("unknown kind", !grammar.load(bad, error));
        ensure("named: " + error, error.find("purple") != std::string::npos);

        bad = mini();
        bad["states"]["main"][0]["push"] = "nowhere";
        ensure("unknown state", !grammar.load(bad, error));
        ensure("named: " + error, error.find("nowhere") != std::string::npos);

        LLSD nameless;
        nameless["states"]["main"] = rules({});
        ensure("no name", !grammar.load(nameless, error));

        bad = mini();
        bad["states"]["main"][3]["chars"] = "z-a";
        ensure("a class that runs downward", !grammar.load(bad, error));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<6>()
    {
        set_test_name("the highlighter follows a document and re-lexes only what an edit reaches");
        ALTextDocument      doc("a\nb /* c\nd\ne */ f\ng");
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(loaded(mini()));
        highlighter.attach(&doc);
        ensure_equals("line 2 is comment", said(doc.line(2), highlighter.tokens(2)), std::string("comment:d"));
        ensure_equals("three lines lexed to get there", highlighter.lastLexed(), 3);
        ensure_equals("line 3 closes it", said(doc.line(3), highlighter.tokens(3)), std::string("comment:e */|text: f"));
        ensure_equals("line 4 is code", said(doc.line(4), highlighter.tokens(4)), std::string("text:g"));
        const U32 revision_of_4 = highlighter.revision(4);

        // A change inside the comment reaches the line it is on and no
        // further, since the next line still starts inside the comment.
        doc.replace(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1)), "dd");
        ensure_equals("line 2 again", said(doc.line(2), highlighter.tokens(2)), std::string("comment:dd"));
        ensure_equals("one line lexed", highlighter.lastLexed(), 1);
        highlighter.tokens(4);
        ensure_equals("nothing more", highlighter.lastLexed(), 0);
        ensure_equals("line 4 unchanged", highlighter.revision(4), revision_of_4);

        // Taking the opening out reaches every line until one starts in
        // the same state as before: line 4 does.
        doc.replace(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 4)), "");
        ensure_equals("line 3 is code now", said(doc.line(3), highlighter.tokens(3)), std::string("text:e */ f"));
        ensure_equals("lines 1 to 3 lexed", highlighter.lastLexed(), 3);
        highlighter.tokens(4);
        ensure_equals("line 4 still not", highlighter.revision(4), revision_of_4);

        // A line added and a line removed keep the lines lined up.
        doc.insert(ALTextPos(0, 1), "\n1");
        ensure_equals("six lines", doc.lineCount(), 6);
        ensure_equals("the new line", said(doc.line(1), highlighter.tokens(1)), std::string("number:1"));
        ensure_equals("the last still there", said(doc.line(5), highlighter.tokens(5)), std::string("text:g"));
        doc.removeFirstLines(2);
        ensure_equals("the first now", said(doc.line(0), highlighter.tokens(0)), std::string("text:b  c"));
        ensure_equals("the last still there", said(doc.line(3), highlighter.tokens(3)), std::string("text:g"));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<7>()
    {
        set_test_name("words changing lexes everything again, and no grammar lexes nothing");
        ALTextDocument      doc("foo bar");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        ensure("nothing without a grammar", highlighter.tokens(0).empty());
        highlighter.setGrammar(loaded(mini()));
        ensure_equals("plain words", said(doc.line(0), highlighter.tokens(0)), std::string("text:foo bar"));
        highlighter.words().set("function", { "bar" });
        highlighter.wordsChanged();
        ensure_equals("a function now", said(doc.line(0), highlighter.tokens(0)), std::string("text:foo |function:bar"));
    }
}
