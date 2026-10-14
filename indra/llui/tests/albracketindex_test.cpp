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

#include "albracketindex.h"
#include "alsyntaxhighlighter.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace
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

        // Line and block comments, strings, and brackets as punctuation;
        // and with `paths`, a string one word long with a dot or a slash in
        // it a path, as LSL's grammar has it.
        static std::shared_ptr<const ALSyntaxGrammar> require_grammar(bool paths = false)
        {
            LLSD description;
            description["name"] = "brackets";
            LLSD main;
            main.append(rule({ { "match", "//" }, { "kind", "comment" }, { "push", "line_comment" } }));
            main.append(rule({ { "span", "/*" }, { "end", "*/" }, { "kind", "comment" } }));
            if (paths)
            {
                main.append(rule({ { "regex", R"re("[^"\s\\]*[./][^"\s\\]*")re" }, { "kind", "path" } }));
            }
            main.append(rule({ { "span", "\"" }, { "end", "\"" }, { "escape", "\\" }, { "kind", "string" }, { "multiline", false } }));
            main.append(rule({ { "chars", "()[]{}" }, { "max", 1 }, { "kind", "punctuation" } }));
            description["states"]["main"] = main;
            LLSD line_comment;
            line_comment["default"] = "comment";
            line_comment["rules"].append(rule({ { "eol", true }, { "pop", true } }));
            description["states"]["line_comment"] = line_comment;
            auto        loaded = std::make_shared<ALSyntaxGrammar>();
            std::string error;
            const bool  ok = loaded->load(description, error);
            INFO("grammar loads: " << error);
            REQUIRE(ok);
            return loaded;
        }

        ALTextDocument      doc;
        ALSyntaxHighlighter highlighter;
        ALBracketIndex      index{ &highlighter };

        void make(const char* text, bool paths = false)
        {
            doc.setText(text);
            highlighter.setGrammar(require_grammar(paths));
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

    TEST_CASE_METHOD(albracketindex_data, "albracketindex: match pairs brackets across lines but none in strings or comments", "[albracketindex]")
    {
        make("f(a[1], \"(\" /* ) */\n  { g(x) }\n) // (\n");
        UNSCOPED_INFO("the round pair across lines");
        CHECK(matched(0, 1) == std::string("2:0"));
        UNSCOPED_INFO("and back");
        CHECK(matched(2, 0) == std::string("0:1"));
        UNSCOPED_INFO("a square pair inside it");
        CHECK(matched(0, 3) == std::string("0:5"));
        UNSCOPED_INFO("a curly pair on its own line");
        CHECK(matched(1, 2) == std::string("1:9"));
        UNSCOPED_INFO("in a string: none");
        CHECK(matched(0, 9) == std::string("none"));
        UNSCOPED_INFO("in a comment: none");
        CHECK(matched(2, 5) == std::string("none"));
        UNSCOPED_INFO("not a bracket: none");
        CHECK(matched(0, 0) == std::string("none"));
    }

    TEST_CASE_METHOD(albracketindex_data, "albracketindex: enclosing finds the bracket of a kind left open around a place", "[albracketindex]")
    {
        make("a(b(c)d[e(f)g]h)\n");
        UNSCOPED_INFO("the one around, the other kind passed through, its own kind's pairs skipped");
        CHECK(around(0, 12, '(') == std::string("0:1"));
        UNSCOPED_INFO("inside the inner pair, its opener");
        CHECK(around(0, 11, '(') == std::string("0:9"));
        UNSCOPED_INFO("inside the square");
        CHECK(around(0, 10, '[') == std::string("0:7"));
        UNSCOPED_INFO("two out");
        CHECK(around(0, 5, '(', 2) == std::string("0:1"));
        UNSCOPED_INFO("not so many");
        CHECK(around(0, 5, '(', 3) == std::string("none"));
        UNSCOPED_INFO("a closer under the place not counted");
        CHECK(around(0, 5, ')') == std::string("0:15"));
        UNSCOPED_INFO("the closer after, the pair inside skipped");
        CHECK(around(0, 2, ')') == std::string("0:15"));
    }

    TEST_CASE_METHOD(albracketindex_data, "albracketindex: a search stops at its reach and depths count the brackets left open", "[albracketindex]")
    {
        std::string text = "(\n";
        for (int i = 0; i < 50; ++i)
        {
            text += "  x\n";
        }
        text += ")\n)\n";
        make(text.c_str());
        UNSCOPED_INFO("found within reach");
        CHECK(matched(0, 0, 60) == std::string("51:0"));
        UNSCOPED_INFO("not past the reach");
        CHECK(matched(0, 0, 10) == std::string("none"));
        UNSCOPED_INFO("nor back past it");
        CHECK(matched(51, 0, 10) == std::string("none"));
        UNSCOPED_INFO("open inside");
        CHECK(index.depthBefore(1) == 1);
        UNSCOPED_INFO("closed after");
        CHECK(index.depthBefore(52) == 0);
        UNSCOPED_INFO("a closer past none closes nothing");
        CHECK(index.depthBefore(53) == 0);
        UNSCOPED_INFO("at a place of a line");
        CHECK(index.depthAt(ALTextPos(0, 1)) == 1);
        UNSCOPED_INFO("before the bracket");
        CHECK(index.depthAt(ALTextPos(0, 0)) == 0);
    }

    TEST_CASE_METHOD(albracketindex_data, "albracketindex: the index follows edits to a line's text and to its tokens", "[albracketindex]")
    {
        make("(a\nb)\nc\n");
        CHECK(matched(0, 0) == std::string("1:1"));
        CHECK(index.depthBefore(1) == 1);
        doc.insert(ALTextPos(0, 0), "/*");
        UNSCOPED_INFO("the opener now in a comment");
        CHECK(matched(0, 2) == std::string("none"));
        UNSCOPED_INFO("the closer too, the comment running on");
        CHECK(matched(1, 1) == std::string("none"));
        UNSCOPED_INFO("and nothing open");
        CHECK(index.depthBefore(1) == 0);
        doc.remove(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        UNSCOPED_INFO("paired again");
        CHECK(matched(0, 0) == std::string("1:1"));
        UNSCOPED_INFO("open again");
        CHECK(index.depthBefore(1) == 1);
        doc.replace(ALTextRange(ALTextPos(1, 1), ALTextPos(1, 2)), "]");
        UNSCOPED_INFO("a bracket of another kind in its place pairs with nothing");
        CHECK(matched(0, 0) == std::string("none"));
    }

    TEST_CASE_METHOD(albracketindex_data, "albracketindex: a bracket inside a path is none and those around it pair past it", "[albracketindex]")
    {
        make("f(\"v1.0)\", g(x));\n{\n}\n", /*paths*/ true);
        UNSCOPED_INFO("the call's opener pairs with its own closer, not the path's");
        CHECK(matched(0, 1) == std::string("0:15"));
        UNSCOPED_INFO("in a path: none");
        CHECK(matched(0, 7) == std::string("none"));
        UNSCOPED_INFO("nothing open after the line");
        CHECK(index.depthBefore(1) == 0);
        UNSCOPED_INFO("nor inside the block below");
        CHECK(index.depthBefore(2) == 1);
    }
}
