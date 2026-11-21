/**
 * @file allslcomments_test.cpp
 * @brief ALLSLComments: an LSL script's comments read from its source, and those that may be code.
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

#include "../lsl/allslcomments.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using Comment = ALLSLComments::Comment;
    using Unit    = ALLSLComments::Unit;

    std::vector<Unit> unitsOf(const std::string& lsl) { return ALLSLComments::units(lsl, ALLSLComments::scan(lsl)); }

    TEST_CASE("ALLSLComments: comments where they begin and end, past strings", "[ALLSLComments]")
    {
        const std::string lsl = "integer a; // after\n"
                                "  /* a block\n"
                                "     over lines */\n"
                                "string s = \"// not one /* nor this */\";\n"
                                "string t = \"a \\\" quote\"; // one\n";
        const std::vector<Comment> found = ALLSLComments::scan(lsl);
        REQUIRE(found.size() == 3);
        CHECK(found[0].kind == Comment::Kind::Line);
        CHECK(found[0].line == 1);
        CHECK(found[0].column == 12);
        CHECK(found[0].begin == lsl.find("// after"));
        CHECK(found[0].end == lsl.find('\n'));
        CHECK_FALSE(found[0].alone);
        CHECK(ALLSLComments::body(lsl, found[0]) == " after");
        CHECK(found[1].kind == Comment::Kind::Block);
        CHECK(found[1].line == 2);
        CHECK(found[1].column == 3);
        CHECK(found[1].endLine == 3);
        CHECK(found[1].alone);
        CHECK(found[1].closed);
        CHECK(ALLSLComments::body(lsl, found[1]) == " a block\n     over lines ");
        CHECK(found[2].line == 5);
        CHECK(ALLSLComments::body(lsl, found[2]) == " one");

        const std::vector<Comment> open = ALLSLComments::scan("/* never closed\ninteger a;");
        REQUIRE(open.size() == 1);
        CHECK_FALSE(open[0].closed);
        UNSCOPED_INFO("one never closed that ends in // is no toggle, and pairs no opener");
        const std::string          ends  = "//* a\n/* never closed\ninteger a; //";
        const std::vector<Comment> loose = ALLSLComments::scan(ends);
        REQUIRE(loose.size() == 2);
        CHECK_FALSE(loose[0].opener);
        CHECK_FALSE(loose[1].closed);
        CHECK_FALSE(loose[1].toggled);
        CHECK(ALLSLComments::body(ends, loose[1]) == " never closed\ninteger a; //");
        UNSCOPED_INFO("alone, among many on one line");
        std::string line(1, ' ');
        for (int n = 0; n < 3; ++n)
        {
            line += "/* x */ ";
        }
        const std::vector<Comment> many = ALLSLComments::scan(line + "\n  /* y */\n");
        REQUIRE(many.size() == 4);
        CHECK_FALSE(many[0].alone);
        CHECK_FALSE(many[1].alone);
        CHECK_FALSE(many[2].alone);
        CHECK(many[3].alone);
    }

    TEST_CASE("ALLSLComments: the toggle's markers paired; the toggle turned off", "[ALLSLComments]")
    {
        const std::string lsl = "//*\n"         // 0, an opener
                                "integer a;\n"  // 1
                                "//*/\n"        // 2, its closer
                                "//* alone\n"   // 3, closed by the block below
                                "/*\n"          // 4
                                "integer b;\n"  // 5
                                "//*/\n"        // 6, the toggle off
                                "//**\n";       // 7, a rule: no opener
        const std::vector<Comment> found = ALLSLComments::scan(lsl);
        REQUIRE(found.size() == 5);
        CHECK(found[0].opener);
        CHECK(found[0].closedBy == 1);
        CHECK(found[1].closer);
        CHECK(found[2].opener);
        CHECK(found[2].closedBy == 3);
        CHECK(found[3].kind == Comment::Kind::Block);
        CHECK(found[3].toggled);
        CHECK(ALLSLComments::body(lsl, found[3]) == "\ninteger b;\n");
        CHECK_FALSE(found[4].opener);
        CHECK_FALSE(found[4].closer);
    }

    TEST_CASE("ALLSLComments: units: runs alone on consecutive lines, blocks, toggles, after code", "[ALLSLComments]")
    {
        const std::string lsl = "default {\n"
                                "    state_entry() {\n"
                                "        //llSay(0, \"a\");\n"      // 3: a run of two
                                "        //llSay(0, \"b\");\n"      // 4
                                "\n"
                                "        // llSay(0, \"c\");\n"     // 6: another, after a blank line
                                "        /*\n"                      // 7: a block, not in the run
                                "        llSay(0, \"d\");\n"
                                "        */\n"
                                "        /*\n"                      // 10: a toggle off
                                "        llSay(0, \"e\");\n"
                                "        //*/\n"
                                "        llSay(0, \"f\"); // llSay(0, \"g\");\n"  // 13: after code
                                "    }\n"
                                "}\n";
        const std::vector<Unit> found = unitsOf(lsl);
        REQUIRE(found.size() == 5);
        CHECK(found[0].shape == Unit::Shape::Lines);
        CHECK(found[0].line == 3);
        CHECK(found[0].endLine == 4);
        CHECK(found[0].first == 0);
        CHECK(found[0].last == 1);
        CHECK(found[1].shape == Unit::Shape::Lines);
        CHECK(found[1].line == 6);
        CHECK(found[1].endLine == 6);
        CHECK(found[2].shape == Unit::Shape::Block);
        CHECK(found[2].line == 7);
        CHECK(found[2].endLine == 9);
        CHECK(found[3].shape == Unit::Shape::Toggle);
        CHECK(found[3].line == 10);
        CHECK(found[4].shape == Unit::Shape::Trailing);
        CHECK(found[4].line == 13);
        CHECK(found[4].column == 24);
    }

    TEST_CASE("ALLSLComments: not units: prose, rules, markers, what would run past itself", "[ALLSLComments]")
    {
        UNSCOPED_INFO("words alone");
        CHECK(unitsOf("// just some words\n/* and more, here */\ninteger a; // the count\n").empty());
        UNSCOPED_INFO("rules and headings");
        CHECK(unitsOf("////////////////\n/******* f(); *******/\n/** doc(); */\n").empty());
        UNSCOPED_INFO("the toggle's markers");
        CHECK(unitsOf("//*\ninteger a;\n//*/\n").empty());
        UNSCOPED_INFO("a string not closed on its line");
        CHECK(unitsOf("//llSay(0, \"a);\n").empty());
        UNSCOPED_INFO("a block opened or closed inside");
        CHECK(unitsOf("//llSay(0, 1); /* more\n/* f(); /* g(); */\n").empty());
        UNSCOPED_INFO("a block never closed, or with code beside it on a line");
        CHECK(unitsOf("/* f();\n").empty());
        CHECK(unitsOf("integer a; /* f(); */\n/* g(); */ integer b;\n").empty());
        UNSCOPED_INFO("after another comment on its line");
        CHECK(unitsOf("/* a */ // f();\n").empty());
        UNSCOPED_INFO("nothing that ends code: a header in prose, a block's first line");
        CHECK(unitsOf("// flash(prim, on) - finds a prim named so\n").empty());
        CHECK(unitsOf("//if (x) {\n").empty());
        CHECK(unitsOf("integer a; // see b()\n").empty());
        CHECK(unitsOf("/* f(x) does it */\n").empty());
        UNSCOPED_INFO("a run of words is not tried, one with any code is, whole");
        CHECK(unitsOf("// one\n// two\n").empty());
        const std::vector<Unit> mixed = unitsOf("// old way:\n// f();\n// else\n");
        REQUIRE(mixed.size() == 1);
        CHECK(mixed[0].first == 0);
        CHECK(mixed[0].last == 2);
    }

    TEST_CASE("ALLSLComments: a run split at its lines of words; or each line of code alone", "[ALLSLComments]")
    {
        const std::string          lsl     = "// old way:\n// f();\n// g();\n// then\n// h();\n// if (x) {\n// so\n// if (y) {\n";
        const std::vector<Comment> found   = ALLSLComments::scan(lsl);
        const std::vector<Unit>    units   = ALLSLComments::units(lsl, found);
        REQUIRE(units.size() == 1);
        const std::vector<Unit> groups = ALLSLComments::runOf(units[0], lsl, found, false);
        REQUIRE(groups.size() == 2);
        CHECK(groups[0].first == 1);
        CHECK(groups[0].last == 2);
        CHECK(groups[0].line == 2);
        CHECK(groups[0].endLine == 3);
        UNSCOPED_INFO("a line that ends no code with one that does; and not alone");
        CHECK(groups[1].first == 4);
        CHECK(groups[1].last == 5);
        const std::vector<Unit> lines = ALLSLComments::runOf(units[0], lsl, found, true);
        REQUIRE(lines.size() == 3);
        CHECK(lines[0].first == 1);
        CHECK(lines[1].first == 2);
        CHECK(lines[2].first == 4);
    }

    TEST_CASE("ALLSLComments: uncommented where it stands, every line and column kept", "[ALLSLComments]")
    {
        const std::string          lsl   = "  //f();\n  //g(); // why\n/*\nh();\n*/\n/*\ni();\n//*/\nj(); // k();\n";
        const std::vector<Comment> found = ALLSLComments::scan(lsl);
        const std::vector<Unit>    units = ALLSLComments::units(lsl, found);
        REQUIRE(units.size() == 4);
        CHECK(ALLSLComments::uncommented(lsl, found, units[0]) == "    f();\n    g(); // why\n/*\nh();\n*/\n/*\ni();\n//*/\nj(); // k();\n");
        CHECK(ALLSLComments::uncommented(lsl, found, units[1]) == "  //f();\n  //g(); // why\n  \nh();\n  \n/*\ni();\n//*/\nj(); // k();\n");
        CHECK(ALLSLComments::uncommented(lsl, found, units[2]) == "  //f();\n  //g(); // why\n/*\nh();\n*/\n  \ni();\n    \nj(); // k();\n");
        CHECK(ALLSLComments::uncommented(lsl, found, units[3]) == "  //f();\n  //g(); // why\n/*\nh();\n*/\n/*\ni();\n//*/\nj();    k();\n");
    }

    TEST_CASE("ALLSLComments: free bytes: the lowest for the toggle, the highest for others", "[ALLSLComments]")
    {
        CHECK(ALLSLComments::freeByte("abc") == '\x01');
        CHECK(ALLSLComments::freeByte("a\x01\x02") == '\x03');
        CHECK(ALLSLComments::freeByte("abc", '\x01') == '\x02');
        const std::vector<char> high = ALLSLComments::freeBytes("a\x1f", 2, '\x1e');
        REQUIRE(high.size() == 2);
        CHECK(high[0] == '\x1d');
        CHECK(high[1] == '\x1c');
        std::string every;
        for (char c = 1; c < ' '; ++c)
        {
            every += c;
        }
        CHECK(ALLSLComments::freeByte(every) == 0);
        CHECK(ALLSLComments::freeBytes(every, 2, 0).empty());
    }
}
