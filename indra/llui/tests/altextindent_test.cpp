/**
 * @file altextindent_test.cpp
 * @brief Where a line's indentation belongs, over a document with nothing drawn.
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

#include "altextindent.h"

#include "alsyntaxgrammar.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    using namespace ALTextIndent;

    struct altextindent_data
    {
        ALSyntaxLibrary library;
        Options         spaces;
        Options         tabs;

        altextindent_data()
        {
            for (const char* name : { "lsl", "slua" })
            {
                std::string error;
                ensure(std::string(name) + " loads: " + error,
                       library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/" + name + ".xml", error));
            }
            spaces.softTabs = true;
            spaces.tabWidth = 4;
            tabs.softTabs   = false;
            tabs.tabWidth   = 4;
        }

        const ALSyntaxGrammar* grammar(const char* name) const { return library.find(name).get(); }

        // The text a change leaves, its replacements made in order.
        static std::string applied(const std::string& text, const Change& change)
        {
            ALTextDocument doc(text);
            for (const Replacement& one : change.replacements)
            {
                doc.replace(one.range, one.text);
            }
            return doc.text();
        }
        static std::string applied(const std::string& text, const Replacement& one)
        {
            ALTextDocument doc(text);
            doc.replace(one.range, one.text);
            return doc.text();
        }

        // Nothing matches brackets: the rules fall back on the line above.
        static opener_t none()
        {
            return [](const ALTextPos&, ALTextPos&) { return false; };
        }
    };

    typedef test_group<altextindent_data> altextindent_group;
    typedef altextindent_group::object    altextindent_object;
    altextindent_group                    altextindent_instance("altextindent");

    template<> template<>
    void altextindent_object::test<1>()
    {
        set_test_name("a tab's text, and a level in and out in the blank it is written in");
        const ALTextDocument doc("ab\n\tx");
        ensure_equals("a hard tab", tabText(doc, ALTextPos(0, 1), tabs), std::string("\t"));
        ensure_equals("spaces to the next stop", tabText(doc, ALTextPos(0, 1), spaces), std::string("   "));
        ensure_equals("from past a tab", tabText(doc, ALTextPos(1, 1), spaces), std::string("    "));

        ensure_equals("a level in, as tabs are typed", indentUnit("", tabs), std::string("\t"));
        ensure_equals("in spaces", indentUnit("", spaces), std::string("    "));
        ensure_equals("as the indentation around it is written", indentUnit("\t", spaces), std::string("\t"));
        ensure_equals("a level out of spaces", outdented("        ", spaces), std::string("    "));
        ensure_equals("of tabs, in tabs", outdented("\t\t", spaces), std::string("\t"));
        ensure_equals("never past none", outdented("  ", spaces), std::string());
    }

    template<> template<>
    void altextindent_object::test<2>()
    {
        set_test_name("lines indented and outdented, the empty left, the caret and the anchor moved with their lines' starts");
        const std::string text = "a\n\nb\n  c";
        const ALTextDocument doc(text);
        const Change in = indentLines(doc, ALTextPos(0, 1), ALTextPos(3, 3), true, spaces);
        ensure_equals("each line with anything on it a level in", applied(text, in), std::string("    a\n\n    b\n      c"));
        ensure("a selection still", in.selects);
        ensure("the anchor moved with its line", in.anchor == ALTextPos(0, 5));
        ensure("the caret too", in.caret == ALTextPos(3, 7));
        const Change tabbed = indentLines(doc, ALTextPos(0, 0), ALTextPos(0, 0), true, tabs);
        ensure_equals("a tab where tabs are typed", applied(text, tabbed), std::string("\ta\n\nb\n  c"));

        const std::string   deep = "\tx\n      y\n  z";
        const ALTextDocument deep_doc(deep);
        const Change out = indentLines(deep_doc, ALTextPos(0, 0), ALTextPos(2, 1), false, spaces);
        ensure_equals("a tab, or up to a level of spaces, out", applied(deep, out), std::string("x\n  y\nz"));
        ensure("the caret never before its line's start", out.caret == ALTextPos(2, 0));
    }

    template<> template<>
    void altextindent_object::test<3>()
    {
        set_test_name("Return: the new line indented as the one it leaves, a level in under what opens a block, and a bracket closed on a line of its own");
        const ALSyntaxGrammar* lsl  = grammar("lsl");
        const ALSyntaxGrammar* slua = grammar("slua");
        ensure("grammars", lsl && slua);

        const std::string    plain = "    x = 1;";
        const ALTextDocument plain_doc(plain);
        const Split          kept  = splitLine(plain_doc, ALTextRange(ALTextPos(0, 10), ALTextPos(0, 10)), lsl, spaces);
        ensure_equals("the indentation kept", kept.text, std::string("\n    "));
        ensure("the caret after it", !kept.caret);

        const std::string    opens = "\tdefault {";
        const ALTextDocument opens_doc(opens);
        const Split          in    = splitLine(opens_doc, ALTextRange(ALTextPos(0, 10), ALTextPos(0, 10)), lsl, spaces);
        ensure_equals("a level in, in the blank written", in.text, std::string("\n\t\t"));

        const std::string    pair = "if x then end";
        const ALTextDocument pair_doc(pair);
        const Split          between = splitLine(pair_doc, ALTextRange(ALTextPos(0, 9), ALTextPos(0, 9)), slua, spaces);
        ensure_equals("a word closing is no bracket: it goes down with the caret", between.text, std::string("\n    "));
        ensure("its blank taken with it", between.range == ALTextRange(ALTextPos(0, 9), ALTextPos(0, 10)));

        const std::string    braces = "  f() {}";
        const ALTextDocument braces_doc(braces);
        const Split          split  = splitLine(braces_doc, ALTextRange(ALTextPos(0, 7), ALTextPos(0, 7)), lsl, spaces);
        ensure_equals("the closing bracket on a line of its own", applied(braces, Replacement{ split.range, split.text }),
                      std::string("  f() {\n      \n  }"));
        ensure("the caret on the line between", split.caret == std::optional<ALTextPos>(ALTextPos(1, 6)));

        const std::string    blank = "    ";
        const ALTextDocument blank_doc(blank);
        const Split          none  = splitLine(blank_doc, ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), lsl, spaces);
        ensure_equals("a line of blanks keeps none", applied(blank, Replacement{ none.range, none.text }), std::string("\n    "));
    }

    template<> template<>
    void altextindent_object::test<4>()
    {
        set_test_name("a line closing a block brought out as it is finished, only ever further out, and put back where the word goes on");
        const ALSyntaxGrammar* slua = grammar("slua");
        const ALSyntaxGrammar* lsl  = grammar("lsl");

        const std::string    text = "if x then\n    y()\n    end";
        const ALTextDocument doc(text);
        const ALTextPos      after_end(2, 7);
        const Outdent        out = outdentAsTyped(doc, after_end, after_end, 'd', slua, none(), AutoOutdent(), spaces);
        ensure("brought out", out.replacement.has_value());
        ensure_equals("level with what opened the block", applied(text, *out.replacement), std::string("if x then\n    y()\nend"));
        ensure("the word kept, with the caret where the line's coming out leaves it",
               out.next.line == 2 && out.next.column == 3 && out.next.indent == "    ");

        // The word goes on -- `endp` -- and is a name: back where it was.
        const std::string    name = "if x then\n    y()\nendp";
        const ALTextDocument name_doc(name);
        const Outdent back = outdentAsTyped(name_doc, ALTextPos(2, 4), ALTextPos(2, 4), 'p', slua, none(), out.next, spaces);
        ensure("put back", back.replacement.has_value());
        ensure_equals("where it was typed", applied(name, *back.replacement), std::string("if x then\n    y()\n    endp"));
        ensure("and nothing kept", back.next.line < 0);

        ensure("not with something after it on the line",
               !outdentAsTyped(ALTextDocument("if x then\n    end y"), ALTextPos(1, 7), ALTextPos(1, 7), 'd', slua, none(), AutoOutdent(), spaces)
                    .replacement);
        ensure("nor with a selection",
               !outdentAsTyped(doc, ALTextPos(2, 4), after_end, 'd', slua, none(), AutoOutdent(), spaces).replacement);

        // A bracket the caller matches goes level with its opener, however
        // the lines between are indented.
        const std::string    braces = "  f() {\n        x;\n        }";
        const ALTextDocument braces_doc(braces);
        const opener_t       matched = [](const ALTextPos&, ALTextPos& opener) {
            opener = ALTextPos(0, 6);
            return true;
        };
        const Outdent closed = outdentAsTyped(braces_doc, ALTextPos(2, 9), ALTextPos(2, 9), '}', lsl, matched, AutoOutdent(), spaces);
        ensure_equals("level with the bracket's line", applied(braces, *closed.replacement), std::string("  f() {\n        x;\n  }"));
        ensure("a bracket is kept for nothing", closed.next.line < 0);

        const std::string    outer = "if x then\n  y()\nend";
        const ALTextDocument outer_doc(outer);
        ensure("a line already out stays", !reindent(outer_doc, 1, "    ", spaces));
        ensure("one in comes out", reindent(outer_doc, 1, "", spaces).has_value());
    }

    template<> template<>
    void altextindent_object::test<5>()
    {
        set_test_name("Return after a closing word brings it out first, and not with a selection");
        const ALSyntaxGrammar* slua = grammar("slua");
        const std::string      text = "while x do\n    y()\n    end";
        const ALTextDocument   doc(text);
        const std::optional<Replacement> closing = closingBeforeReturn(doc, ALTextPos(2, 7), ALTextPos(2, 7), slua, none(), spaces);
        ensure("brought out", closing.has_value());
        ensure_equals("level with the loop", applied(text, *closing), std::string("while x do\n    y()\nend"));
        ensure("not with a selection", !closingBeforeReturn(doc, ALTextPos(2, 4), ALTextPos(2, 7), slua, none(), spaces));
        ensure("not a word that is a name", !closingBeforeReturn(ALTextDocument("x\n    ending"), ALTextPos(1, 10), ALTextPos(1, 10), slua,
                                                                 none(), spaces));
    }

    template<> template<>
    void altextindent_object::test<6>()
    {
        set_test_name("indentation made again of spaces or of tabs, measured as the tabs were");
        const std::string code = "\tif x\n  \ty\n      z\nw";
        const ALTextDocument indented(code);
        ensure_equals("to spaces, a tab to its stop", applied(code, *convertIndentation(indented, 0, 3, true, 4)),
                      std::string("    if x\n    y\n      z\nw"));
        ensure_equals("to tabs, as far as they go", applied(code, *convertIndentation(indented, 0, 3, false, 4)),
                      std::string("\tif x\n\ty\n\t  z\nw"));
        ensure("nothing to change is nothing", !convertIndentation(ALTextDocument("    a\nb"), 0, 1, true, 4));
        ensure_equals("measured as the tabs were: a tab of two, two spaces at four",
                      applied("\ta", *convertIndentation(ALTextDocument("\ta"), 0, 0, false, 4, 2)), std::string("  a"));
    }

    template<> template<>
    void altextindent_object::test<7>()
    {
        set_test_name("a text's own indentation read: tabs or spaces by which more lines begin with, a level's spaces by the step seen most, nothing where it does not say");
        const auto read = [](const char* text, S32 lines = 10000) {
            const std::optional<Options> own = detect(ALTextDocument(text), 4, lines);
            return own ? llformat("%s %d", own->softTabs ? "spaces" : "tabs", own->tabWidth) : std::string("none");
        };
        ensure_equals("four spaces a level", read("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}"), std::string("spaces 4"));
        ensure_equals("two", read("if x then\n  y()\n  if z then\n    w()\n  end\nend"), std::string("spaces 2"));
        ensure_equals("tabs, a tab as wide as the default", read("a\n\tb\n\t\tc\n\td"), std::string("tabs 4"));
        ensure_equals("nothing indented", read("a\nb\n\nc"), std::string("none"));
        ensure_equals("as many each way", read("\ta\n    b"), std::string("none"));
        ensure_equals("a block comment's stars are not a level", read("/**\n * one\n * two\n */\nf()\n{\n  x;\n}"), std::string("spaces 2"));
        ensure_equals("only the lines asked about", read("a\nb\n\tc", 2), std::string("none"));
        ensure_equals("closing two levels at once is still steps of four", read("a\n    b\n        c\nd\n    e\n        f\ng"), std::string("spaces 4"));
    }

    template<> template<>
    void altextindent_object::test<8>()
    {
        set_test_name("Backspace in a line's leading spaces takes them back to the stop before; one character past a tab, in the text, or where one is all there is");
        const ALTextDocument doc("        x\n   y\n\t  z\n  w  v\n     u");
        const auto from = [&](S32 line, S32 column) {
            const std::optional<ALTextPos> to = backspaceFrom(doc, ALTextPos(line, column), spaces);
            return to ? llformat("%d:%d", to->line, to->column) : std::string("one");
        };
        ensure_equals("a level from a stop", from(0, 8), std::string("0:4"));
        ensure_equals("to the stop from between", from(0, 6), std::string("0:4"));
        ensure_equals("from just past a stop, one", from(0, 5), std::string("one"));
        ensure_equals("three to none", from(1, 3), std::string("1:0"));
        ensure_equals("never past a tab", from(2, 3), std::string("2:1"));
        ensure_equals("not in the text", from(3, 5), std::string("one"));
        ensure_equals("from five, to four", from(4, 5), std::string("one"));
        ensure_equals("before the text, not after it", from(0, 9), std::string("one"));
        Options wide = spaces;
        wide.tabWidth = 8;
        ensure_equals("a level as wide as the tab", backspaceFrom(doc, ALTextPos(0, 8), wide)->column, 0);
    }

    template<> template<>
    void altextindent_object::test<9>()
    {
        set_test_name("lines pasted into a line's indentation brought to where they go: the line's level, or the line above's say; each as far in from the first as it was; the caret with its line");
        const ALSyntaxGrammar* lsl = grammar("lsl");
        // The text with the paste in, then brought to where it goes; and
        // where the caret ends.
        const auto pasted = [&](const std::string& text, const ALTextPos& at, const std::string& clip, const Options& options, ALTextPos* caret = nullptr) {
            ALTextDocument                 doc(text);
            const std::optional<PastePlan> plan = planPaste(doc, ALTextRange(at, at), clip, lsl, options);
            const ALTextPos                end  = doc.insert(at, clip).endAfter();
            std::optional<Change>          change;
            if (plan)
            {
                change = reindentPasted(doc, at, *plan, end, options);
            }
            if (caret)
            {
                *caret = change ? change->caret : end;
            }
            return change ? altextindent_data::applied(doc.text(), *change) : doc.text();
        };
        ensure_equals("whole lines into a blank line under an opener: a level in from it",
                      pasted("f()\n{\n\n}", ALTextPos(2, 0), "        a;\n        if (x)\n        {\n            b;\n        }\n", spaces),
                      std::string("f()\n{\n    a;\n    if (x)\n    {\n        b;\n    }\n\n}"));
        ALTextPos caret;
        ensure_equals("copied from where the text begins: the rest say how far in it was; what followed kept at its level",
                      pasted("f()\n{\n    x;\n}", ALTextPos(2, 4), "if (y) {\n        z;\n    }\n", spaces, &caret),
                      std::string("f()\n{\n    if (y) {\n        z;\n    }\n    x;\n}"));
        ensure("the caret with its line", caret == ALTextPos(5, 4));
        ensure_equals("spaces made tabs where the text is indented by tabs",
                      pasted("f()\n{\n\tx;\n}", ALTextPos(2, 1), "    a;\n        b;\n", tabs), std::string("f()\n{\n\ta;\n\t\tb;\n\tx;\n}"));
        ensure_equals("a closer first: a level out from where the line above says",
                      pasted("f()\n{\n    x;\n\n", ALTextPos(3, 0), "}\ng()\n", spaces), std::string("f()\n{\n    x;\n}\ng()\n\n"));
        ensure_equals("a blank line pasted keeps no blanks", pasted("{\n\n}", ALTextPos(1, 0), "  a;\n   \n  b;", spaces),
                      std::string("{\n    a;\n\n    b;\n}"));

        const ALTextDocument doc("{\n    x;\n}");
        ensure("one line: nothing", !planPaste(doc, ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)), "a;", lsl, spaces));
        ensure("after text on its line: nothing", !planPaste(doc, ALTextRange(ALTextPos(1, 6), ALTextPos(1, 6)), "a;\nb;", lsl, spaces));
        ensure("no grammar that indents: nothing", !planPaste(doc, ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)), "a;\nb;", nullptr, spaces));
        ensure_equals("already where it goes: nothing to do", pasted("{\n    x;\n}", ALTextPos(1, 4), "a;\n    b;\n", spaces),
                      std::string("{\n    a;\n    b;\n    x;\n}"));
    }

    template<> template<>
    void altextindent_object::test<10>()
    {
        set_test_name("Return in LSL under a head with no brace: the one line in, then back out to the head, the outermost of a run; a brace under it brought level as it is typed");
        const ALSyntaxGrammar* lsl = grammar("lsl");
        // The text with Return pressed at a place, a | where the caret goes.
        const auto returned = [&](const std::string& text, S32 line, S32 column) {
            const ALTextDocument doc(text);
            const Split          split = splitLine(doc, ALTextRange(ALTextPos(line, column), ALTextPos(line, column)), lsl, spaces);
            ALTextDocument       after(text);
            const ALTextPos      end   = after.replace(split.range, split.text).endAfter();
            const ALTextPos      caret = split.caret ? *split.caret : end;
            std::string          out   = after.text();
            return out.insert(static_cast<size_t>(after.offsetOf(caret)), "|");
        };
        ensure_equals("if (x): the next line in", returned("if (x)", 0, 6), std::string("if (x)\n    |"));
        ensure_equals("else too", returned("    else // otherwise", 0, 21), std::string("    else // otherwise\n        |"));
        ensure_equals("a brace after the caret stays level", returned("if (x){", 0, 6), std::string("if (x)\n|{"));
        ensure_equals("a statement on the line is none", returned("if (x) llSay(0, \"a\");", 0, 21), std::string("if (x) llSay(0, \"a\");\n|"));
        ensure_equals("past its statement, back to the head", returned("if (a)\n    x;", 1, 6), std::string("if (a)\n    x;\n|"));
        ensure_equals("the outermost of a run", returned("  if (a)\n      if (b)\n          x;", 2, 12), std::string("  if (a)\n      if (b)\n          x;\n  |"));
        ensure_equals("a line of nothing under it stays in", returned("if (a)\n    ", 1, 4), std::string("if (a)\n\n    |"));
        ensure_equals("under a brace, no head", returned("if (a)\n{\n    x;", 2, 6), std::string("if (a)\n{\n    x;\n    |"));

        const ALTextDocument brace("if (x)\n    {");
        const Outdent        level = outdentAsTyped(brace, ALTextPos(1, 5), ALTextPos(1, 5), '{', lsl, none(), AutoOutdent(), spaces);
        ensure("the brace level with the head", level.replacement && applied("if (x)\n    {", *level.replacement) == "if (x)\n{");
        const ALTextDocument opened("if (x) {\n    {");
        ensure("under a block's opener, where it is",
               !outdentAsTyped(opened, ALTextPos(1, 5), ALTextPos(1, 5), '{', lsl, none(), AutoOutdent(), spaces).replacement);
    }

    template<> template<>
    void altextindent_object::test<11>()
    {
        set_test_name("Return in SLua puts a block's end in where it is not closed below, before a bracket after the caret; in an LSL block comment, its lines' star");
        const ALSyntaxGrammar* slua = grammar("slua");
        const ALSyntaxGrammar* lsl  = grammar("lsl");
        const auto returned = [&](const ALSyntaxGrammar* grammar, const std::string& text, S32 line, S32 column, bool in_comment = false) {
            const ALTextDocument doc(text);
            const Split          split = splitLine(doc, ALTextRange(ALTextPos(line, column), ALTextPos(line, column)), grammar, spaces, in_comment);
            ALTextDocument       after(text);
            const ALTextPos      end   = after.replace(split.range, split.text).endAfter();
            const ALTextPos      caret = split.caret ? *split.caret : end;
            std::string          out   = after.text();
            return out.insert(static_cast<size_t>(after.offsetOf(caret)), "|");
        };
        ensure_equals("then, nothing below", returned(slua, "if x then", 0, 9), std::string("if x then\n    |\nend"));
        ensure_equals("closed below already", returned(slua, "if x then\n    y()\nend", 0, 9), std::string("if x then\n    |\n    y()\nend"));
        ensure_equals("an elseif, the if's end below", returned(slua, "if a then\n    x()\nelseif b then\nend", 2, 13),
                      std::string("if a then\n    x()\nelseif b then\n    |\nend"));
        ensure_equals("inside a block that ends further out", returned(slua, "do\n    while x do\nend", 1, 14), std::string("do\n    while x do\n        |\n    end\nend"));
        ensure_equals("a function's, before the bracket after it", returned(slua, "f(function())", 0, 12), std::string("f(function()\n    |\nend)"));
        ensure_equals("one closed on its line opens nothing", returned(slua, "local g = function() return 1 end", 0, 33),
                      std::string("local g = function() return 1 end\n|"));
        ensure_equals("else: the if's own end", returned(slua, "if a then\nelse", 1, 4), std::string("if a then\nelse\n    |"));

        ensure_equals("a doc comment's first line", returned(lsl, "    /**", 0, 7, true), std::string("    /**\n     * |"));
        ensure_equals("its lines", returned(lsl, "     * one", 0, 10, true), std::string("     * one\n     * |"));
        ensure_equals("ended, none", returned(lsl, "     */", 0, 7, true), std::string("     */\n     |"));
        ensure_equals("closed on its line, none", returned(lsl, "/* a */", 0, 7, true), std::string("/* a */\n|"));
        ensure_equals("not in a comment, none", returned(lsl, "/**", 0, 3, false), std::string("/**\n|"));
        ensure_equals("a line comment, none", returned(lsl, "// a", 0, 4, true), std::string("// a\n|"));

        const ALTextDocument bare("     * /");
        const Outdent        closed = outdentAsTyped(bare, ALTextPos(0, 8), ALTextPos(0, 8), '/', lsl, none(), AutoOutdent(), spaces);
        ensure("its end typed on a bare line: the blank taken", closed.replacement && applied("     * /", *closed.replacement) == "     */");
        const ALTextDocument written("     * a /");
        ensure("after something written, as typed",
               !outdentAsTyped(written, ALTextPos(0, 10), ALTextPos(0, 10), '/', lsl, none(), AutoOutdent(), spaces).replacement);
    }

    template<> template<>
    void altextindent_object::test<12>()
    {
        set_test_name("lines shifted so many levels in, empty ones left alone, and out by a tab or a tab's width of spaces a level, as vim's > and <");
        const std::string text = "a\n\n  \n\tb\n      c";
        const ALTextDocument doc(text);
        const auto applied = [&](const ALTextIndent::Change& change) {
            ALTextDocument out(text);
            for (auto it = change.replacements.rbegin(); it != change.replacements.rend(); ++it)
            {
                out.replace(it->range, it->text);
            }
            return out.text();
        };
        ALTextIndent::Options soft;
        soft.tabWidth = 2;
        soft.softTabs = true;
        ensure_equals("in by two, the empty line left", applied(ALTextIndent::shiftLines(doc, 0, 4, 2, true, soft)),
                      std::string("    a\n\n      \n    \tb\n          c"));
        ALTextIndent::Options hard;
        hard.tabWidth = 4;
        ensure_equals("in by a tab", applied(ALTextIndent::shiftLines(doc, 0, 0, 1, true, hard)), std::string("\ta\n\n  \n\tb\n      c"));
        ensure_equals("out by one: a tab, or up to a tab's width of spaces", applied(ALTextIndent::shiftLines(doc, 0, 4, 1, false, hard)),
                      std::string("a\n\n\nb\n  c"));
        ensure_equals("out by two", applied(ALTextIndent::shiftLines(doc, 0, 4, 2, false, hard)), std::string("a\n\n\nb\nc"));
    }

    template<> template<>
    void altextindent_object::test<13>()
    {
        set_test_name("lines in and out at several selections: once for those over the same lines, each selection moved with its lines");
        const std::string    text = "a\nb\nc\n    d";
        const ALTextDocument doc(text);
        const auto           made = [](const std::string& before, const std::vector<ALTextEditing::Group>& groups, std::string& placed) {
            ALTextEditing::Combined combined = ALTextEditing::combine(groups, 3);
            ALTextDocument          after(before);
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const ALTextEditing::Replacement& one : combined.replacements)
            {
                edits.emplace_back(one.range, one.text);
            }
            after.replaceMany(std::move(edits));
            placed.clear();
            for (const std::optional<ALTextRange>& one : combined.selections)
            {
                placed += one ? llformat("%d:%d-%d:%d ", one->begin.line, one->begin.column, one->end.line, one->end.column) : std::string("- ");
            }
            return after.text();
        };
        std::string placed;
        // Two selections over lines 0 and 1, which share line 1, and one on
        // line 3.
        const std::vector<ALTextRange> selections = { ALTextRange(ALTextPos(0, 0), ALTextPos(1, 1)), ALTextRange(ALTextPos(1, 1), ALTextPos(1, 0)),
                                                      ALTextRange(ALTextPos(3, 5), ALTextPos(3, 4)) };
        ensure_equals("each line in once", made(text, indentLines(doc, selections, true, spaces), placed), std::string("    a\n    b\nc\n        d"));
        ensure_equals("each selection moved with its lines", placed, std::string("0:4-1:5 1:5-1:4 3:9-3:8 "));
        ensure_equals("and out", made(text, indentLines(doc, selections, false, spaces), placed), std::string("a\nb\nc\nd"));
        ensure_equals("those with nothing to take left to slide; the other moved", placed, std::string("- - 3:1-3:0 "));
    }

    template<> template<>
    void altextindent_object::test<14>()
    {
        set_test_name("lines shifted out by how wide their blanks are drawn: spaces before a tab go with it, and the caret stays on its character");
        const std::string    text = "  \tfoo\n \t\tbar\n\t  baz\n      qux";
        const ALTextDocument doc(text);
        ALTextIndent::Options hard;
        hard.tabWidth = 4;
        const Change one = ALTextIndent::shiftLines(doc, 0, 3, 1, false, hard);
        ensure_equals("each a tab's width out", applied(text, one), std::string("foo\n\tbar\n  baz\n  qux"));
        bool taken_from_the_front = one.replacements.size() == 4;
        for (const Replacement& replacement : one.replacements)
        {
            taken_from_the_front = taken_from_the_front && replacement.range.begin.column == 0 && replacement.text.empty();
        }
        ensure("each taken from the line's front", taken_from_the_front);
        ensure_equals("two levels", applied(text, ALTextIndent::shiftLines(doc, 0, 3, 2, false, hard)), std::string("foo\nbar\nbaz\nqux"));

        // The caret on the f of foo, past the spaces and the tab.
        const std::vector<ALTextEditing::Group> groups = indentLines(doc, { ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)) }, false, hard);
        ensure("one group", groups.size() == 1 && groups[0].placed.size() == 1);
        ensure("the caret still on the f", groups[0].placed[0].second == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
    }
}
