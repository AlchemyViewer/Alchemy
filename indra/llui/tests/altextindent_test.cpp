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

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
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
                const bool  loaded = library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/" + name + ".xml", error);
                INFO(name << " loads: " << error);
                REQUIRE(loaded);
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
        // A command at one selection, as its groups make it: the text left,
        // and where the selection goes, where its group places it.
        static std::string require_applied(const std::string& text, const std::vector<ALTextEditing::Group>& groups, std::optional<ALTextRange>& placed)
        {
            const ALTextEditing::Combined                    combined = ALTextEditing::combine(groups, 1);
            REQUIRE(combined.selections.size() == 1);
            ALTextDocument                                   doc(text);
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const Replacement& one : combined.replacements)
            {
                edits.emplace_back(one.range, one.text);
            }
            doc.replaceMany(std::move(edits));
            placed = combined.selections.front();
            return doc.text();
        }

        // Nothing matches brackets: the rules fall back on the line above.
        static opener_t none()
        {
            return [](const ALTextPos&, ALTextPos&) { return false; };
        }
    };

    TEST_CASE_METHOD(altextindent_data, "altextindent: a tab's text and a level in or out are written in the blanks in use", "[altextindent]")
    {
        const ALTextDocument doc("ab\n\tx");
        UNSCOPED_INFO("a hard tab");
        CHECK(tabText(doc, ALTextPos(0, 1), tabs) == std::string("\t"));
        UNSCOPED_INFO("spaces to the next stop");
        CHECK(tabText(doc, ALTextPos(0, 1), spaces) == std::string("   "));
        UNSCOPED_INFO("from past a tab");
        CHECK(tabText(doc, ALTextPos(1, 1), spaces) == std::string("    "));

        UNSCOPED_INFO("a level in, as tabs are typed");
        CHECK(indentUnit("", tabs) == std::string("\t"));
        UNSCOPED_INFO("in spaces");
        CHECK(indentUnit("", spaces) == std::string("    "));
        UNSCOPED_INFO("as the indentation around it is written");
        CHECK(indentUnit("\t", spaces) == std::string("\t"));
        UNSCOPED_INFO("a level out of spaces");
        CHECK(outdented("        ", spaces) == std::string("    "));
        UNSCOPED_INFO("of tabs, in tabs");
        CHECK(outdented("\t\t", spaces) == std::string("\t"));
        UNSCOPED_INFO("never past none");
        CHECK(outdented("  ", spaces) == std::string());
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: indentLines leaves empty lines and moves the selection with line starts", "[altextindent]")
    {
        const std::string text = "a\n\nb\n  c";
        const ALTextDocument doc(text);
        std::optional<ALTextRange> placed;
        {
            INFO("each line with anything on it a level in");
            CHECK(require_applied(text, indentLines(doc, { ALTextRange(ALTextPos(0, 1), ALTextPos(3, 3)) }, true, spaces), placed) ==
                  std::string("    a\n\n    b\n      c"));
        }
        UNSCOPED_INFO("the anchor moved with its line, and the caret too");
        CHECK(placed == ALTextRange(ALTextPos(0, 5), ALTextPos(3, 7)));
        {
            INFO("a tab where tabs are typed");
            CHECK(require_applied(text, indentLines(doc, { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)) }, true, tabs), placed) ==
                  std::string("\ta\n\nb\n  c"));
        }

        const std::string   deep = "\tx\n      y\n  z";
        const ALTextDocument deep_doc(deep);
        {
            INFO("a tab, or up to a level of spaces, out");
            CHECK(require_applied(deep, indentLines(deep_doc, { ALTextRange(ALTextPos(0, 0), ALTextPos(2, 1)) }, false, spaces), placed) ==
                  std::string("x\n  y\nz"));
        }
        {
            INFO("the caret never before its line's start");
            REQUIRE(placed);
            CHECK(placed->end == ALTextPos(2, 0));
        }
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: splitLine keeps indentation and goes in under an opener or splits braces", "[altextindent]")
    {
        const ALSyntaxGrammar* lsl  = grammar("lsl");
        const ALSyntaxGrammar* slua = grammar("slua");
        REQUIRE(lsl);
        REQUIRE(slua);

        const std::string    plain = "    x = 1;";
        const ALTextDocument plain_doc(plain);
        const Split          kept  = splitLine(plain_doc, ALTextRange(ALTextPos(0, 10), ALTextPos(0, 10)), lsl, spaces);
        UNSCOPED_INFO("the indentation kept");
        CHECK(kept.text == std::string("\n    "));
        UNSCOPED_INFO("the caret after it");
        CHECK_FALSE(kept.caret);

        const std::string    opens = "\tdefault {";
        const ALTextDocument opens_doc(opens);
        const Split          in    = splitLine(opens_doc, ALTextRange(ALTextPos(0, 10), ALTextPos(0, 10)), lsl, spaces);
        UNSCOPED_INFO("a level in, in the blank written");
        CHECK(in.text == std::string("\n\t\t"));

        const std::string    pair = "if x then end";
        const ALTextDocument pair_doc(pair);
        const Split          between = splitLine(pair_doc, ALTextRange(ALTextPos(0, 9), ALTextPos(0, 9)), slua, spaces);
        UNSCOPED_INFO("a word closing is no bracket: it goes down with the caret");
        CHECK(between.text == std::string("\n    "));
        UNSCOPED_INFO("its blank taken with it");
        CHECK(between.range == ALTextRange(ALTextPos(0, 9), ALTextPos(0, 10)));

        const std::string    braces = "  f() {}";
        const ALTextDocument braces_doc(braces);
        const Split          split  = splitLine(braces_doc, ALTextRange(ALTextPos(0, 7), ALTextPos(0, 7)), lsl, spaces);
        UNSCOPED_INFO("the closing bracket on a line of its own");
        CHECK(applied(braces, Replacement{ split.range, split.text }) ==
                      std::string("  f() {\n      \n  }"));
        UNSCOPED_INFO("the caret on the line between");
        CHECK(split.caret == std::optional<ALTextPos>(ALTextPos(1, 6)));

        const std::string    blank = "    ";
        const ALTextDocument blank_doc(blank);
        const Split          none  = splitLine(blank_doc, ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), lsl, spaces);
        UNSCOPED_INFO("a line of blanks keeps none");
        CHECK(applied(blank, Replacement{ none.range, none.text }) == std::string("\n    "));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: a closing word typed brings its line out and back if the word goes on", "[altextindent]")
    {
        const ALSyntaxGrammar* slua = grammar("slua");
        const ALSyntaxGrammar* lsl  = grammar("lsl");

        const std::string    text = "if x then\n    y()\n    end";
        const ALTextDocument doc(text);
        const ALTextPos      after_end(2, 7);
        const Outdent        out = outdentAsTyped(doc, after_end, after_end, 'd', slua, none(), AutoOutdent(), spaces);
        UNSCOPED_INFO("brought out");
        REQUIRE(out.replacement.has_value());
        UNSCOPED_INFO("level with what opened the block");
        CHECK(applied(text, *out.replacement) == std::string("if x then\n    y()\nend"));
        {
            INFO("the word kept, with the caret where the line's coming out leaves it");
            CHECK(out.next.line == 2);
            CHECK(out.next.column == 3);
            CHECK(out.next.indent == "    ");
        }

        // The word goes on -- `endp` -- and is a name: back where it was.
        const std::string    name = "if x then\n    y()\nendp";
        const ALTextDocument name_doc(name);
        const Outdent back = outdentAsTyped(name_doc, ALTextPos(2, 4), ALTextPos(2, 4), 'p', slua, none(), out.next, spaces);
        UNSCOPED_INFO("put back");
        REQUIRE(back.replacement.has_value());
        UNSCOPED_INFO("where it was typed");
        CHECK(applied(name, *back.replacement) == std::string("if x then\n    y()\n    endp"));
        UNSCOPED_INFO("and nothing kept");
        CHECK(back.next.line < 0);

        UNSCOPED_INFO("not with something after it on the line");
        CHECK_FALSE(outdentAsTyped(ALTextDocument("if x then\n    end y"), ALTextPos(1, 7), ALTextPos(1, 7), 'd', slua, none(), AutoOutdent(), spaces)
                    .replacement);
        UNSCOPED_INFO("nor with a selection");
        CHECK_FALSE(outdentAsTyped(doc, ALTextPos(2, 4), after_end, 'd', slua, none(), AutoOutdent(), spaces).replacement);

        // A bracket the caller matches goes level with its opener, however
        // the lines between are indented.
        const std::string    braces = "  f() {\n        x;\n        }";
        const ALTextDocument braces_doc(braces);
        const opener_t       matched = [](const ALTextPos&, ALTextPos& opener) {
            opener = ALTextPos(0, 6);
            return true;
        };
        const Outdent closed = outdentAsTyped(braces_doc, ALTextPos(2, 9), ALTextPos(2, 9), '}', lsl, matched, AutoOutdent(), spaces);
        REQUIRE(closed.replacement);
        UNSCOPED_INFO("level with the bracket's line");
        CHECK(applied(braces, *closed.replacement) == std::string("  f() {\n        x;\n  }"));
        UNSCOPED_INFO("a bracket is kept for nothing");
        CHECK(closed.next.line < 0);

        const std::string    outer = "if x then\n  y()\nend";
        const ALTextDocument outer_doc(outer);
        UNSCOPED_INFO("a line already out stays");
        CHECK_FALSE(reindent(outer_doc, 1, "    ", spaces));
        UNSCOPED_INFO("one in comes out");
        CHECK(reindent(outer_doc, 1, "", spaces).has_value());
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: closingBeforeReturn brings a closing word out but not with a selection", "[altextindent]")
    {
        const ALSyntaxGrammar* slua = grammar("slua");
        const std::string      text = "while x do\n    y()\n    end";
        const ALTextDocument   doc(text);
        const std::optional<Replacement> closing = closingBeforeReturn(doc, ALTextPos(2, 7), ALTextPos(2, 7), slua, none(), spaces);
        UNSCOPED_INFO("brought out");
        REQUIRE(closing.has_value());
        UNSCOPED_INFO("level with the loop");
        CHECK(applied(text, *closing) == std::string("while x do\n    y()\nend"));
        UNSCOPED_INFO("not with a selection");
        CHECK_FALSE(closingBeforeReturn(doc, ALTextPos(2, 4), ALTextPos(2, 7), slua, none(), spaces));
        UNSCOPED_INFO("not a word that is a name");
        CHECK_FALSE(closingBeforeReturn(ALTextDocument("x\n    ending"), ALTextPos(1, 10), ALTextPos(1, 10), slua,
                                                                 none(), spaces));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: indentation remade in spaces or tabs measured as the tabs were", "[altextindent]")
    {
        const std::string code = "\tif x\n  \ty\n      z\nw";
        const ALTextDocument indented(code);
        const std::optional<Change> to_spaces = convertIndentation(indented, 0, 3, true, 4);
        REQUIRE(to_spaces.has_value());
        UNSCOPED_INFO("to spaces, a tab to its stop");
        CHECK(applied(code, *to_spaces) == std::string("    if x\n    y\n      z\nw"));
        const std::optional<Change> to_tabs = convertIndentation(indented, 0, 3, false, 4);
        REQUIRE(to_tabs.has_value());
        UNSCOPED_INFO("to tabs, as far as they go");
        CHECK(applied(code, *to_tabs) == std::string("\tif x\n\ty\n\t  z\nw"));
        UNSCOPED_INFO("nothing to change is nothing");
        CHECK_FALSE(convertIndentation(ALTextDocument("    a\nb"), 0, 1, true, 4));
        const std::optional<Change> measured = convertIndentation(ALTextDocument("\ta"), 0, 0, false, 4, 2);
        REQUIRE(measured.has_value());
        UNSCOPED_INFO("measured as the tabs were: a tab of two, two spaces at four");
        CHECK(applied("\ta", *measured) == std::string("  a"));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: detect reads tabs or spaces and the level from the commonest step", "[altextindent]")
    {
        const auto read = [](const char* text, S32 lines = 10000) {
            const std::optional<Options> own = detect(ALTextDocument(text), 4, lines);
            return own ? llformat("%s %d", own->softTabs ? "spaces" : "tabs", own->tabWidth) : std::string("none");
        };
        UNSCOPED_INFO("four spaces a level");
        CHECK(read("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}") == std::string("spaces 4"));
        CHECK(read("if x then\n  y()\n  if z then\n    w()\n  end\nend") == std::string("spaces 2"));
        UNSCOPED_INFO("tabs, a tab as wide as the default");
        CHECK(read("a\n\tb\n\t\tc\n\td") == std::string("tabs 4"));
        UNSCOPED_INFO("nothing indented");
        CHECK(read("a\nb\n\nc") == std::string("none"));
        UNSCOPED_INFO("as many each way");
        CHECK(read("\ta\n    b") == std::string("none"));
        UNSCOPED_INFO("a block comment's stars are not a level");
        CHECK(read("/**\n * one\n * two\n */\nf()\n{\n  x;\n}") == std::string("spaces 2"));
        UNSCOPED_INFO("only the lines asked about");
        CHECK(read("a\nb\n\tc", 2) == std::string("none"));
        UNSCOPED_INFO("closing two levels at once is still steps of four");
        CHECK(read("a\n    b\n        c\nd\n    e\n        f\ng") == std::string("spaces 4"));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: backspaceFrom takes leading spaces back to the stop before", "[altextindent]")
    {
        const ALTextDocument doc("        x\n   y\n\t  z\n  w  v\n     u");
        const auto from = [&](S32 line, S32 column) {
            const std::optional<ALTextPos> to = backspaceFrom(doc, ALTextPos(line, column), spaces);
            return to ? llformat("%d:%d", to->line, to->column) : std::string("one");
        };
        UNSCOPED_INFO("a level from a stop");
        CHECK(from(0, 8) == std::string("0:4"));
        UNSCOPED_INFO("to the stop from between");
        CHECK(from(0, 6) == std::string("0:4"));
        UNSCOPED_INFO("from just past a stop, one");
        CHECK(from(0, 5) == std::string("one"));
        UNSCOPED_INFO("three to none");
        CHECK(from(1, 3) == std::string("1:0"));
        UNSCOPED_INFO("never past a tab");
        CHECK(from(2, 3) == std::string("2:1"));
        UNSCOPED_INFO("not in the text");
        CHECK(from(3, 5) == std::string("one"));
        UNSCOPED_INFO("from five, to four");
        CHECK(from(4, 5) == std::string("one"));
        UNSCOPED_INFO("before the text, not after it");
        CHECK(from(0, 9) == std::string("one"));
        Options wide = spaces;
        wide.tabWidth = 8;
        const std::optional<ALTextPos> wide_back = backspaceFrom(doc, ALTextPos(0, 8), wide);
        REQUIRE(wide_back.has_value());
        UNSCOPED_INFO("a level as wide as the tab");
        CHECK(wide_back->column == 0);
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: pasted lines go where they belong keeping their depth from the first", "[altextindent]")
    {
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
        UNSCOPED_INFO("whole lines into a blank line under an opener: a level in from it");
        CHECK(pasted("f()\n{\n\n}", ALTextPos(2, 0), "        a;\n        if (x)\n        {\n            b;\n        }\n", spaces) ==
                      std::string("f()\n{\n    a;\n    if (x)\n    {\n        b;\n    }\n\n}"));
        ALTextPos caret;
        UNSCOPED_INFO("copied from where the text begins: the rest say how far in it was; what followed kept at its level");
        CHECK(pasted("f()\n{\n    x;\n}", ALTextPos(2, 4), "if (y) {\n        z;\n    }\n", spaces, &caret) ==
                      std::string("f()\n{\n    if (y) {\n        z;\n    }\n    x;\n}"));
        UNSCOPED_INFO("the caret with its line");
        CHECK(caret == ALTextPos(5, 4));
        UNSCOPED_INFO("spaces made tabs where the text is indented by tabs");
        CHECK(pasted("f()\n{\n\tx;\n}", ALTextPos(2, 1), "    a;\n        b;\n", tabs) == std::string("f()\n{\n\ta;\n\t\tb;\n\tx;\n}"));
        UNSCOPED_INFO("a closer first: a level out from where the line above says");
        CHECK(pasted("f()\n{\n    x;\n\n", ALTextPos(3, 0), "}\ng()\n", spaces) == std::string("f()\n{\n    x;\n}\ng()\n\n"));
        UNSCOPED_INFO("a blank line pasted keeps no blanks");
        CHECK(pasted("{\n\n}", ALTextPos(1, 0), "  a;\n   \n  b;", spaces) ==
                      std::string("{\n    a;\n\n    b;\n}"));

        const ALTextDocument doc("{\n    x;\n}");
        UNSCOPED_INFO("one line: nothing");
        CHECK_FALSE(planPaste(doc, ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)), "a;", lsl, spaces));
        UNSCOPED_INFO("after text on its line: nothing");
        CHECK_FALSE(planPaste(doc, ALTextRange(ALTextPos(1, 6), ALTextPos(1, 6)), "a;\nb;", lsl, spaces));
        UNSCOPED_INFO("no grammar that indents: nothing");
        CHECK_FALSE(planPaste(doc, ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)), "a;\nb;", nullptr, spaces));
        UNSCOPED_INFO("already where it goes: nothing to do");
        CHECK(pasted("{\n    x;\n}", ALTextPos(1, 4), "a;\n    b;\n", spaces) ==
                      std::string("{\n    a;\n    b;\n    x;\n}"));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: in LSL a braceless head indents one line and a brace typed comes level", "[altextindent]")
    {
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
        UNSCOPED_INFO("if (x): the next line in");
        CHECK(returned("if (x)", 0, 6) == std::string("if (x)\n    |"));
        UNSCOPED_INFO("else too");
        CHECK(returned("    else // otherwise", 0, 21) == std::string("    else // otherwise\n        |"));
        UNSCOPED_INFO("a brace after the caret stays level");
        CHECK(returned("if (x){", 0, 6) == std::string("if (x)\n|{"));
        UNSCOPED_INFO("a statement on the line is none");
        CHECK(returned("if (x) llSay(0, \"a\");", 0, 21) == std::string("if (x) llSay(0, \"a\");\n|"));
        UNSCOPED_INFO("past its statement, back to the head");
        CHECK(returned("if (a)\n    x;", 1, 6) == std::string("if (a)\n    x;\n|"));
        UNSCOPED_INFO("the outermost of a run");
        CHECK(returned("  if (a)\n      if (b)\n          x;", 2, 12) == std::string("  if (a)\n      if (b)\n          x;\n  |"));
        UNSCOPED_INFO("a line of nothing under it stays in");
        CHECK(returned("if (a)\n    ", 1, 4) == std::string("if (a)\n\n    |"));
        UNSCOPED_INFO("under a brace, no head");
        CHECK(returned("if (a)\n{\n    x;", 2, 6) == std::string("if (a)\n{\n    x;\n    |"));

        const ALTextDocument brace("if (x)\n    {");
        const Outdent        level = outdentAsTyped(brace, ALTextPos(1, 5), ALTextPos(1, 5), '{', lsl, none(), AutoOutdent(), spaces);
        {
            INFO("the brace level with the head");
            REQUIRE(level.replacement);
            CHECK(applied("if (x)\n    {", *level.replacement) == "if (x)\n{");
        }
        const ALTextDocument opened("if (x) {\n    {");
        UNSCOPED_INFO("under a block's opener, where it is");
        CHECK_FALSE(outdentAsTyped(opened, ALTextPos(1, 5), ALTextPos(1, 5), '{', lsl, none(), AutoOutdent(), spaces).replacement);
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: a new line closes an open SLua block and continues an LSL block comment", "[altextindent]")
    {
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
        UNSCOPED_INFO("then, nothing below");
        CHECK(returned(slua, "if x then", 0, 9) == std::string("if x then\n    |\nend"));
        UNSCOPED_INFO("closed below already");
        CHECK(returned(slua, "if x then\n    y()\nend", 0, 9) == std::string("if x then\n    |\n    y()\nend"));
        UNSCOPED_INFO("an elseif, the if's end below");
        CHECK(returned(slua, "if a then\n    x()\nelseif b then\nend", 2, 13) ==
                      std::string("if a then\n    x()\nelseif b then\n    |\nend"));
        UNSCOPED_INFO("inside a block that ends further out");
        CHECK(returned(slua, "do\n    while x do\nend", 1, 14) == std::string("do\n    while x do\n        |\n    end\nend"));
        UNSCOPED_INFO("a function's, before the bracket after it");
        CHECK(returned(slua, "f(function())", 0, 12) == std::string("f(function()\n    |\nend)"));
        UNSCOPED_INFO("one closed on its line opens nothing");
        CHECK(returned(slua, "local g = function() return 1 end", 0, 33) ==
                      std::string("local g = function() return 1 end\n|"));
        UNSCOPED_INFO("else: the if's own end");
        CHECK(returned(slua, "if a then\nelse", 1, 4) == std::string("if a then\nelse\n    |"));

        UNSCOPED_INFO("a doc comment's first line");
        CHECK(returned(lsl, "    /**", 0, 7, true) == std::string("    /**\n     * |"));
        UNSCOPED_INFO("its lines");
        CHECK(returned(lsl, "     * one", 0, 10, true) == std::string("     * one\n     * |"));
        UNSCOPED_INFO("ended, none");
        CHECK(returned(lsl, "     */", 0, 7, true) == std::string("     */\n     |"));
        UNSCOPED_INFO("closed on its line, none");
        CHECK(returned(lsl, "/* a */", 0, 7, true) == std::string("/* a */\n|"));
        UNSCOPED_INFO("not in a comment, none");
        CHECK(returned(lsl, "/**", 0, 3, false) == std::string("/**\n|"));
        UNSCOPED_INFO("a line comment, none");
        CHECK(returned(lsl, "// a", 0, 4, true) == std::string("// a\n|"));

        const ALTextDocument bare("     * /");
        const Outdent        closed = outdentAsTyped(bare, ALTextPos(0, 8), ALTextPos(0, 8), '/', lsl, none(), AutoOutdent(), spaces);
        {
            INFO("its end typed on a bare line: the blank taken");
            REQUIRE(closed.replacement);
            CHECK(applied("     * /", *closed.replacement) == "     */");
        }
        const ALTextDocument written("     * a /");
        UNSCOPED_INFO("after something written, as typed");
        CHECK_FALSE(outdentAsTyped(written, ALTextPos(0, 10), ALTextPos(0, 10), '/', lsl, none(), AutoOutdent(), spaces).replacement);
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: shiftLines goes levels in or out as vim's > and < and leaves empty lines", "[altextindent]")
    {
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
        UNSCOPED_INFO("in by two, the empty line left");
        CHECK(applied(ALTextIndent::shiftLines(doc, 0, 4, 2, true, soft)) ==
                      std::string("    a\n\n      \n    \tb\n          c"));
        ALTextIndent::Options hard;
        hard.tabWidth = 4;
        UNSCOPED_INFO("in by a tab");
        CHECK(applied(ALTextIndent::shiftLines(doc, 0, 0, 1, true, hard)) == std::string("\ta\n\n  \n\tb\n      c"));
        UNSCOPED_INFO("out by one: a tab, or up to a tab's width of spaces");
        CHECK(applied(ALTextIndent::shiftLines(doc, 0, 4, 1, false, hard)) ==
                      std::string("a\n\n\nb\n  c"));
        UNSCOPED_INFO("out by two");
        CHECK(applied(ALTextIndent::shiftLines(doc, 0, 4, 2, false, hard)) == std::string("a\n\n\nb\nc"));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: indentLines at several selections moves a shared line once", "[altextindent]")
    {
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
        UNSCOPED_INFO("each line in once");
        CHECK(made(text, indentLines(doc, selections, true, spaces), placed) == std::string("    a\n    b\nc\n        d"));
        UNSCOPED_INFO("each selection moved with its lines");
        CHECK(placed == std::string("0:4-1:5 1:5-1:4 3:9-3:8 "));
        UNSCOPED_INFO("and out");
        CHECK(made(text, indentLines(doc, selections, false, spaces), placed) == std::string("a\nb\nc\nd"));
        UNSCOPED_INFO("those with nothing to take left to slide; the other moved");
        CHECK(placed == std::string("- - 3:1-3:0 "));
    }

    TEST_CASE_METHOD(altextindent_data, "altextindent: lines shifted out by drawn width take spaces before a tab with it", "[altextindent]")
    {
        const std::string    text = "  \tfoo\n \t\tbar\n\t  baz\n      qux";
        const ALTextDocument doc(text);
        ALTextIndent::Options hard;
        hard.tabWidth = 4;
        const Change one = ALTextIndent::shiftLines(doc, 0, 3, 1, false, hard);
        UNSCOPED_INFO("each a tab's width out");
        CHECK(applied(text, one) == std::string("foo\n\tbar\n  baz\n  qux"));
        bool taken_from_the_front = one.replacements.size() == 4;
        for (const Replacement& replacement : one.replacements)
        {
            taken_from_the_front = taken_from_the_front && replacement.range.begin.column == 0 && replacement.text.empty();
        }
        UNSCOPED_INFO("each taken from the line's front");
        CHECK(taken_from_the_front);
        UNSCOPED_INFO("two levels");
        CHECK(applied(text, ALTextIndent::shiftLines(doc, 0, 3, 2, false, hard)) == std::string("foo\nbar\nbaz\nqux"));

        // The caret on the f of foo, past the spaces and the tab.
        const std::vector<ALTextEditing::Group> groups = indentLines(doc, { ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)) }, false, hard);
        {
            INFO("one group");
            REQUIRE(groups.size() == 1);
            REQUIRE(groups[0].placed.size() == 1);
        }
        UNSCOPED_INFO("the caret still on the f");
        CHECK(groups[0].placed[0].second == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
    }
}
