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

#include "../altextindent.h"

#include "../alsyntaxgrammar.h"

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
}
