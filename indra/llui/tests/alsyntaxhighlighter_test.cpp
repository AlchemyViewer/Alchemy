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

#include "alsyntaxhighlighter.h"

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

        // RE2 has no lookaround; not_after_chars and consume stand in.
        bad = mini();
        bad["states"]["main"] = rules({ LLSD().with("regex", "x(?=y)").with("kind", "keyword") });
        ensure("lookahead", !grammar.load(bad, error));
        ensure("said as a regex: " + error, error.find("x(?=y)") != std::string::npos);

        bad = mini();
        bad["states"]["main"] = rules({ LLSD().with("regex", "(x)y").with("consume", 2).with("kind", "keyword") });
        ensure("consuming a group the regex has not", !grammar.load(bad, error));

        bad = mini();
        bad["states"]["main"] = rules({ LLSD().with("match", "x").with("not_after_chars", "a-z").with("kind", "keyword") });
        ensure("not_after_chars on no regex", !grammar.load(bad, error));

        LLSD good = mini();
        good["states"]["main"] = rules({ LLSD().with("regex", "(x)y").with("consume", 1).with("not_after_chars", "a-z").with("kind", "keyword") });
        ensure("both on a regex: " + error, grammar.load(good, error));
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
        doc.remove(ALTextRange(doc.start(), ALTextPos(2, 0)));
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
        highlighter.ownWords().set("function", { "bar" });
        highlighter.wordsChanged();
        ensure_equals("a function now", said(doc.line(0), highlighter.tokens(0)), std::string("text:foo |function:bar"));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<8>()
    {
        set_test_name("attached to another document, the one it followed before is heard no more");
        ALTextDocument      first("a\nb\nc");
        ALTextDocument      second("x\ny\nz");
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(loaded(mini()));
        highlighter.attach(&first);
        highlighter.tokens(2);
        highlighter.attach(&second);
        highlighter.tokens(2);
        ensure_equals("the second lexed", highlighter.lastLexed(), 3);
        first.insert(ALTextPos(0, 0), "/* one\ntwo\n");
        highlighter.tokens(2);
        ensure_equals("the first's edit reached nothing", highlighter.lastLexed(), 0);
        ensure_equals("and the second reads as it did", said(second.line(1), highlighter.tokens(1)), std::string("text:y"));
    }
    template<> template<>
    void alsyntaxhighlighter_object::test<9>()
    {
        set_test_name("the states lines are in are kept once, and those no line is in any longer let go of; the tokens are what lexing afresh gives");
        ALTextDocument doc;
        std::string    text;
        for (S32 i = 0; i < 100; ++i)
        {
            text += "a\n";
        }
        doc.setText(text);
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(loaded(brackets()));
        highlighter.attach(&doc);
        highlighter.tokens(99);
        ensure("one state for every line in the same state", highlighter.statesKept() <= 2);

        // Each time, the first line opens a bracket of another level, which
        // every line after it is inside: a hundred new states each time,
        // and the last time's no longer anyone's.
        for (S32 level = 1; level <= 60; ++level)
        {
            const S32 first_length = doc.lineLength(0);
            doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, first_length)), "[" + std::string(static_cast<size_t>(level), '=') + "[");
            highlighter.tokens(99);
        }
        highlighter.tokens(99);
        ensure("let go of past a number: " + std::to_string(highlighter.statesKept()), highlighter.statesKept() <= 4096 + 100);

        // One more edit, and every line's tokens as a highlighter made
        // afresh has them.
        doc.replace(ALTextRange(ALTextPos(50, 0), ALTextPos(50, 1)), "]" + std::string(60, '=') + "] b");
        ALSyntaxHighlighter fresh;
        fresh.setGrammar(loaded(brackets()));
        fresh.attach(&doc);
        for (S32 line = 0; line < doc.lineCount(); ++line)
        {
            ensure("line " + std::to_string(line) + " as lexed afresh", highlighter.tokens(line) == fresh.tokens(line));
        }
    }
    template<> template<>
    void alsyntaxhighlighter_object::test<10>()
    {
        set_test_name("words set once are shared between views; a view's own copy changes it and no other; none set, none");
        auto shared = std::make_shared<ALSyntaxWords>();
        shared->set("function", { "foo" });
        ALTextDocument doc;
        doc.setText("foo bar\n");
        ALSyntaxHighlighter a, b;
        a.setGrammar(loaded(mini()));
        b.setGrammar(loaded(mini()));
        a.attach(&doc);
        b.attach(&doc);
        a.setWords(shared);
        b.setWords(shared);
        ensure("the same words", &a.words() == &b.words());
        ensure_equals("a word of them", said(doc.line(0), a.tokens(0)), std::string("function:foo|text: bar"));

        b.ownWords().set("function", { "bar" });
        b.wordsChanged();
        ensure("its own now", &a.words() != &b.words());
        ensure("the other's as they were", a.words().has("function", "foo") && !a.words().has("function", "bar"));
        ensure_equals("its own word", said(doc.line(0), b.tokens(0)), std::string("text:foo |function:bar"));
        ensure_equals("and the other's", said(doc.line(0), a.tokens(0)), std::string("function:foo|text: bar"));

        a.setWords(nullptr);
        ensure_equals("none set, none", said(doc.line(0), a.tokens(0)), std::string("text:foo bar"));
    }
    template<> template<>
    void alsyntaxhighlighter_object::test<11>()
    {
        set_test_name("lexed a slice at a time: no more lines lexed than asked, the lines that lex as they did passed for nothing, and the tokens what lexing afresh gives");
        ALTextDocument doc;
        std::string    text;
        for (S32 i = 0; i < 1000; ++i)
        {
            text += "a 1\n";
        }
        doc.setText(text);
        const S32           last = doc.lineCount() - 1;
        ALSyntaxHighlighter highlighter;
        ensure("nothing to lex without a grammar", highlighter.lexSome(1));
        highlighter.setGrammar(loaded(mini()));
        highlighter.attach(&doc);
        highlighter.tokens(last);
        ensure("lexed to its end, nothing more to lex", highlighter.lexSome(1) && highlighter.lastLexed() == 0);

        // A comment opened at the top: every line after it starts in it.
        doc.insert(ALTextPos(0, 0), "/* ");
        ensure("a slice: not the end", !highlighter.lexSome(100));
        ensure_equals("as many as asked", highlighter.lastLexed(), 100);
        ensure("and again", !highlighter.lexSome(100) && highlighter.lastLexed() == 100);
        ensure_equals("a line asked for lexed on from where the slices stopped", said(doc.line(500), highlighter.tokens(500)), std::string("comment:a 1"));
        ensure_equals("from there", highlighter.lastLexed(), 301);
        ensure("the rest", highlighter.lexSome(1000) && highlighter.lastLexed() == last - 500);

        // A line typed in: lexed, and the lines after it, which start as
        // they did, passed for nothing.
        doc.insert(ALTextPos(10, 0), "b");
        ensure("one line lexed, and none after it wants lexing", highlighter.lexSome(1) && highlighter.lastLexed() == 1);

        ALSyntaxHighlighter fresh;
        fresh.setGrammar(loaded(mini()));
        fresh.attach(&doc);
        for (S32 line = 0; line < doc.lineCount(); ++line)
        {
            ensure("line " + std::to_string(line) + " as lexed afresh", highlighter.tokens(line) == fresh.tokens(line));
        }
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<12>()
    {
        set_test_name("a rule matches one thing, a span among them, and says only what a rule of its kind reads: a span goes nowhere but into itself, its escape is something, and a key no rule knows is refused by name");
        ALSyntaxGrammar grammar;
        std::string     error;
        const auto      refused = [&](const LLSD& bad, const std::string& what) {
            LLSD description              = alsyntaxhighlighter_data::mini();
            description["states"]["main"] = alsyntaxhighlighter_data::rules({ bad });
            ensure(what + " refused", !grammar.load(description, error));
            ensure(what + ", and said why", !error.empty());
        };
        refused(LLSD().with("regex", "x+").with("span", "\"").with("end", "\"").with("kind", "string"), "a span opened by a regex too");
        refused(LLSD().with("span", "\"").with("span_regex", "'").with("end", "\""), "a span with two openings");
        refused(LLSD().with("span", "(").with("end", ")").with("pop", true), "a span that pops");
        refused(LLSD().with("span", "(").with("end", ")").with("push", "line_comment"), "a span that pushes");
        refused(LLSD().with("span", "(").with("end", ")").with("next", "line_comment"), "a span that goes on to a state");
        refused(LLSD().with("span", "if").with("end", "fi").with("whole_word", true), "a span of whole words");
        refused(LLSD().with("span", "\"").with("end", "\"").with("escape", ""), "a span whose escape is empty");
        refused(LLSD().with("span", "\"").with("end", "\"").with("unless_after", "\\"), "a span that runs on and ends with its line");
        refused(LLSD().with("match", "x").with("end", "y"), "a span's end on a literal");
        refused(LLSD().with("chars", "a-z").with("whole_word", true), "whole words of a class");
        refused(LLSD().with("word", true).with("min", 2), "a class's least on a word");
        refused(LLSD().with("span", "\"").with("end", "\"").with("mutliline", false), "a key misspelt");
        ensure("named: " + error, error.find("mutliline") != std::string::npos);

        LLSD good = mini();
        good["states"]["main"] = rules({ LLSD().with("span", "\"").with("end", "\"").with("escape", "\\").with("multiline", false).with("unless_after", "\\"),
                                         LLSD().with("eol", true).with("unless_after", "\\").with("pop", true) });
        ensure("a span its line ends, but for a backslash, and an eol that says the same: " + error, grammar.load(good, error));
        ensure("and the little language: " + error, grammar.load(mini(), error));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<13>()
    {
        set_test_name("lexed again for a grammar or words that change nothing on a line, the line keeps its revision; one they change moves on, as every line of another document does; and with no grammar there are no tokens");
        ALTextDocument      doc("foo\nbar");
        ALTextDocument      other("foo\nbar");
        ALSyntaxHighlighter highlighter;
        const auto          grammar = loaded(mini());
        highlighter.setGrammar(grammar);
        highlighter.attach(&doc);
        const U32 foo = highlighter.revision(0);
        const U32 bar = highlighter.revision(1);

        highlighter.setGrammar(grammar);
        ensure_equals("the same grammar again: the first line as it was", highlighter.revision(0), foo);
        ensure_equals("and the second", highlighter.revision(1), bar);

        highlighter.ownWords().set("function", { "bar" });
        highlighter.wordsChanged();
        ensure_equals("a word taught that the first line does not hold: as it was", highlighter.revision(0), foo);
        ensure_equals("the second's word a function now", said(doc.line(1), highlighter.tokens(1)), std::string("function:bar"));
        ensure("and its revision moved on", highlighter.revision(1) != bar);

        highlighter.attach(&other);
        ensure("another document's lines move on, though they lex the same", highlighter.revision(0) != foo);
        ensure_equals("and lex as they do", said(other.line(0), highlighter.tokens(0)), std::string("text:foo"));

        highlighter.setGrammar(nullptr);
        ensure("no grammar, no tokens", highlighter.tokens(0).empty() && highlighter.tokens(1).empty());
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<14>()
    {
        set_test_name("after edits, asking for the last line lexes each from where it was made to where lexing settles, and looks at no line between them or past");
        std::string text;
        for (S32 i = 0; i < 20000; ++i)
        {
            text += "if x == " + std::to_string(i) + "\n";
        }
        ALTextDocument      doc(text);
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(loaded(mini()));
        highlighter.attach(&doc);
        const S32 last = doc.lineCount() - 1;
        highlighter.tokens(last);
        doc.insert(ALTextPos(5, 0), "y ");
        highlighter.tokens(last);
        ensure_equals("the line edited lexed", highlighter.lastLexed(), 1);
        ensure("and the one after it looked at, no more", highlighter.lastLooked() <= 2);
        doc.insert(ALTextPos(10, 0), "a\nb ");
        doc.insert(ALTextPos(15000, 0), "c ");
        highlighter.tokens(last + 1);
        ensure_equals("two edits: the lines each made", highlighter.lastLexed(), 3);
        ensure("and the line after each looked at", highlighter.lastLooked() <= 5);
        ALSyntaxHighlighter fresh;
        fresh.setGrammar(loaded(mini()));
        fresh.attach(&doc);
        ensure("lexed as they read", highlighter.tokens(15000) == fresh.tokens(15000) && highlighter.tokens(11) == fresh.tokens(11));
    }

    template<> template<>
    void alsyntaxhighlighter_object::test<15>()
    {
        set_test_name("edits of every kind, with lexing asked for a line at a time, a slice at a time or to the end between them, leave every line's tokens what a highlighter lexing afresh finds");
        const auto grammar = loaded(mini());
        const char* pieces[] = { "if a == 1", "/* open", "close */", "x = \"s", "// note", "y = \"a\" + 2", "", "end */ if", "\"", "/**/ z" };
        U32         seed     = 777;
        const auto  next     = [&seed](U32 below) {
            seed = seed * 1664525u + 1013904223u;
            return below == 0 ? 0 : (seed >> 8) % below;
        };
        std::string text;
        for (S32 i = 0; i < 300; ++i)
        {
            text += std::string(pieces[next(10)]) + "\n";
        }
        ALTextDocument      doc(text);
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(grammar);
        highlighter.attach(&doc);
        for (S32 step = 0; step < 600; ++step)
        {
            const S32 lines = doc.lineCount();
            const S32 line  = static_cast<S32>(next(static_cast<U32>(lines)));
            const U32 kind  = next(6);
            if (kind == 0)
            {
                doc.insert(ALTextPos(line, 0), std::string(pieces[next(10)]) + "\n");
            }
            else if (kind == 1 && lines > 2)
            {
                const S32 to = llmin(line + static_cast<S32>(next(4)) + 1, lines - 1);
                doc.replace(ALTextRange(ALTextPos(line, 0), ALTextPos(to, 0)), std::string());
            }
            else if (kind == 2)
            {
                doc.insert(ALTextPos(line, static_cast<S32>(next(static_cast<U32>(doc.line(line).size() + 1)))), pieces[next(10)]);
            }
            else if (kind == 3 && lines > 40)
            {
                // A batch's runs, apart.
                std::vector<std::pair<ALTextRange, std::string>> batch;
                S32                                              at = static_cast<S32>(next(10));
                while (at + 3 < lines && batch.size() < 4)
                {
                    batch.emplace_back(ALTextRange(ALTextPos(at, 0), ALTextPos(at + static_cast<S32>(next(2)), 0)), std::string(pieces[next(10)]) + "\n");
                    at += 5 + static_cast<S32>(next(30));
                }
                doc.replaceMany(std::move(batch));
            }
            else
            {
                doc.insert(ALTextPos(line, 0), pieces[next(10)]);
            }
            // Between edits, as a view asks: a line drawn, a slice a frame,
            // the last line, or nothing.
            const U32 ask = next(4);
            if (ask == 0)
            {
                highlighter.tokens(static_cast<S32>(next(static_cast<U32>(doc.lineCount()))));
            }
            else if (ask == 1)
            {
                highlighter.lexSome(static_cast<S32>(next(20)) + 1);
            }
            else if (ask == 2)
            {
                highlighter.tokens(doc.lineCount() - 1);
            }
            if (step % 20 == 19)
            {
                ALSyntaxHighlighter fresh;
                fresh.setGrammar(grammar);
                fresh.attach(&doc);
                for (S32 l = 0; l < doc.lineCount(); ++l)
                {
                    const std::vector<ALSyntaxToken> mine = highlighter.tokens(l);
                    if (mine != fresh.tokens(l))
                    {
                        ensure_equals("step " + std::to_string(step) + ", line " + std::to_string(l), said(doc.line(l), mine), said(doc.line(l), fresh.tokens(l)));
                    }
                }
            }
        }
    }
}
