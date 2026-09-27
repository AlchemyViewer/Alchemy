/**
 * @file albracketindex_test.cpp
 * @brief Where a text's brackets pair up, found by the line rather than by the byte.
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

#include "../albracketindex.h"
#include "../alsyntaxhighlighter.h"

#include "../test/lltut.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace tut
{
    struct albracketindex_data
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

        // Line and block comments, strings, and brackets as punctuation.
        static std::shared_ptr<const ALSyntaxGrammar> grammar()
        {
            LLSD description;
            description["name"] = "brackets";
            LLSD main;
            main.append(rule({ { "match", "//" }, { "kind", "comment" }, { "push", "line_comment" } }));
            main.append(rule({ { "span", "/*" }, { "end", "*/" }, { "kind", "comment" } }));
            main.append(rule({ { "span", "\"" }, { "end", "\"" }, { "escape", "\\" }, { "kind", "string" }, { "multiline", false } }));
            main.append(rule({ { "chars", "()[]{}" }, { "max", 1 }, { "kind", "punctuation" } }));
            description["states"]["main"] = main;
            LLSD line_comment;
            line_comment["default"] = "comment";
            line_comment["rules"].append(rule({ { "eol", true }, { "pop", true } }));
            description["states"]["line_comment"] = line_comment;
            auto        loaded = std::make_shared<ALSyntaxGrammar>();
            std::string error;
            ensure("grammar loads: " + error, loaded->load(description, error));
            return loaded;
        }

        ALTextDocument      doc;
        ALSyntaxHighlighter highlighter;
        ALBracketIndex      index{ &highlighter };

        void make(const char* text)
        {
            doc.setText(text);
            highlighter.setGrammar(grammar());
            highlighter.attach(&doc);
            index.attach(&doc);
        }

        std::string matched(S32 line, S32 column, S32 lines = ALBracketIndex::NEARBY)
        {
            ALTextPos out;
            return index.match(ALTextPos(line, column), out, lines) ? llformat("%d:%d", out.line, out.column) : std::string("none");
        }
        std::string around(S32 line, S32 column, char bracket, S32 count = 1, S32 lines = ALBracketIndex::NEARBY)
        {
            ALTextPos out;
            return index.enclosing(ALTextPos(line, column), bracket, count, out, lines) ? llformat("%d:%d", out.line, out.column) : std::string("none");
        }
    };
    typedef test_group<albracketindex_data> albracketindex_group;
    typedef albracketindex_group::object    albracketindex_object;
    albracketindex_group                    albracketindex_instance("albracketindex");

    template<> template<>
    void albracketindex_object::test<1>()
    {
        set_test_name("a bracket's partner, its own kind's nesting counted, across lines, both ways; one in a string or a comment is none");
        make("f(a[1], \"(\" /* ) */\n  { g(x) }\n) // (\n");
        ensure_equals("the round pair across lines", matched(0, 1), std::string("2:0"));
        ensure_equals("and back", matched(2, 0), std::string("0:1"));
        ensure_equals("a square pair inside it", matched(0, 3), std::string("0:5"));
        ensure_equals("a curly pair on its own line", matched(1, 2), std::string("1:9"));
        ensure_equals("in a string: none", matched(0, 9), std::string("none"));
        ensure_equals("in a comment: none", matched(2, 5), std::string("none"));
        ensure_equals("not a bracket: none", matched(0, 0), std::string("none"));
    }

    template<> template<>
    void albracketindex_object::test<2>()
    {
        set_test_name("the brackets of a kind left open around a place, the count out; for a closer, after it");
        make("a(b(c)d[e(f)g]h)\n");
        ensure_equals("the one around, the other kind passed through, its own kind's pairs skipped", around(0, 12, '('), std::string("0:1"));
        ensure_equals("inside the inner pair, its opener", around(0, 11, '('), std::string("0:9"));
        ensure_equals("inside the square", around(0, 10, '['), std::string("0:7"));
        ensure_equals("two out", around(0, 5, '(', 2), std::string("0:1"));
        ensure_equals("not so many", around(0, 5, '(', 3), std::string("none"));
        ensure_equals("a closer under the place not counted", around(0, 5, ')'), std::string("0:15"));
        ensure_equals("the closer after, the pair inside skipped", around(0, 2, ')'), std::string("0:15"));
    }

    template<> template<>
    void albracketindex_object::test<3>()
    {
        set_test_name("a search goes only as far as told; the depth at each line's start and at a place, a closer past none closing nothing");
        std::string text = "(\n";
        for (int i = 0; i < 50; ++i)
        {
            text += "  x\n";
        }
        text += ")\n)\n";
        make(text.c_str());
        ensure_equals("found within reach", matched(0, 0, 60), std::string("51:0"));
        ensure_equals("not past the reach", matched(0, 0, 10), std::string("none"));
        ensure_equals("nor back past it", matched(51, 0, 10), std::string("none"));
        ensure_equals("open inside", index.depthBefore(1), 1);
        ensure_equals("closed after", index.depthBefore(52), 0);
        ensure_equals("a closer past none closes nothing", index.depthBefore(53), 0);
        ensure_equals("at a place of a line", index.depthAt(ALTextPos(0, 1)), 1);
        ensure_equals("before the bracket", index.depthAt(ALTextPos(0, 0)), 0);
    }

    template<> template<>
    void albracketindex_object::test<4>()
    {
        set_test_name("kept true through edits: a line's text changed, and its tokens changed under it by a comment opened above");
        make("(a\nb)\nc\n");
        ensure_equals("paired", matched(0, 0), std::string("1:1"));
        ensure_equals("depth", index.depthBefore(1), 1);
        doc.insert(ALTextPos(0, 0), "/*");
        ensure_equals("the opener now in a comment", matched(0, 2), std::string("none"));
        ensure_equals("the closer too, the comment running on", matched(1, 1), std::string("none"));
        ensure_equals("and nothing open", index.depthBefore(1), 0);
        doc.remove(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        ensure_equals("paired again", matched(0, 0), std::string("1:1"));
        ensure_equals("open again", index.depthBefore(1), 1);
        doc.replace(ALTextRange(ALTextPos(1, 1), ALTextPos(1, 2)), "]");
        ensure_equals("a bracket of another kind in its place pairs with nothing", matched(0, 0), std::string("none"));
    }
}
