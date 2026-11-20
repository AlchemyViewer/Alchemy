/**
 * @file allslcommentcode_test.cpp
 * @brief ALLSLCommentCode: the code a comment switched off, converted where it stands and kept commented.
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

#include "../lsl/allslcommentcode.h"
#include "../lsl/allslservice.h"
#include "../lsl/allsltoslua.h"
#include "../luau/alluauservice.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace
{
    struct allslcommentcode_data
    {
        ALLuauService service;
        bool          definitions = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        allslcommentcode_data()
        {
            static bool loaded = false;
            std::string error;
            if (!loaded)
            {
                ALLSLService lsl;
                loaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
                if (!loaded)
                {
                    FAIL("the builtins did not load: " + error);
                }
            }
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            service.setNewSolver(newSolver, error);
            definitions = service.loadDefinitions(text.str(), error);
            ALLuauConfig grid;
            grid.mode = "nonstrict";
            service.setConfig(grid);
        }

        // Converted with the comments' code, as by default.
        static ALLSLToSLua::Result converted(const std::string& lsl, ALLSLCommentCode::Report* report = nullptr)
        {
            ALLSLToSLua::Result r = ALLSLCommentCode::convert(lsl, ALLSLToSLua::Options(), report);
            CAPTURE(lsl);
            REQUIRE(r.converted);
            return r;
        }
        // Converted with every comment as the LSL had it.
        static ALLSLToSLua::Result plain(const std::string& lsl)
        {
            ALLSLToSLua::Options options;
            options.commentCode = false;
            return ALLSLToSLua::convert(lsl, options);
        }
        static bool has(const ALLSLToSLua::Result& r, const std::string& text) { return r.text.find(text) != std::string::npos; }

        // What SLua's check says of the text: no error.
        void require_checks_clean(const std::string& slua)
        {
            REQUIRE(definitions);
            std::string said;
            for (const ALScriptProblem& p : service.check(slua))
            {
                if (p.severity == ALScriptProblem::Severity::Error)
                {
                    said += llformat("[%d:%d] %s\n", p.line + 1, p.column + 1, p.message.c_str());
                }
            }
            CAPTURE(said, slua);
            REQUIRE(said.empty());
        }

        // The text with `lines` lines from the one `from` is on taken out of
        // their comment: the code switched on, as a scripter would.
        static std::string switchedOn(const std::string& slua, const std::string& from, size_t lines)
        {
            const size_t at = slua.find(from);
            CAPTURE(from, slua);
            REQUIRE(at != std::string::npos);
            std::string out  = slua;
            size_t      line = out.rfind('\n', at) + 1;
            for (size_t n = 0; n < lines; ++n)
            {
                const size_t marks = out.find_first_not_of(' ', line);
                REQUIRE(marks != std::string::npos);
                REQUIRE(out.compare(marks, 2, "--") == 0);
                out.erase(marks, 2);
                line = out.find('\n', marks) + 1;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: a statement, a global, a function and a handler converted where each stood",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "integer gCount = 0;\n"
                                "//integer gOld = 5;\n"
                                "/*\n"
                                "integer twice(integer x) {\n"
                                "    return x * 2;\n"
                                "}\n"
                                "*/\n"
                                "default {\n"
                                "    touch_start(integer n) {\n"
                                "        //llOwnerSay(\"touched by \" + llDetectedName(0));\n"
                                "        llSay(0, \"hi\");\n"
                                "        state other;\n"
                                "    }\n"
                                "}\n"
                                "state other {\n"
                                "    state_entry() { llSay(0, (string)gCount); }\n"
                                "    //touch_start(integer n) { llSay(0, \"touched\"); }\n"
                                "}\n";
        const ALLSLToSLua::Result r = converted(lsl);
        CAPTURE(r.text);
        UNSCOPED_INFO("a global");
        CHECK(has(r, "\n--local gOld = 5\n"));
        UNSCOPED_INFO("a function, its block as a block");
        CHECK(has(r, "\n--[[\nlocal function twice(x)\n    return x * 2\nend\n]]\n"));
        UNSCOPED_INFO("a statement, in the handler's detected table");
        CHECK(has(r, "        --print(\"touched by \" .. detected[1]:getName())\n        ll.Say(0, \"hi\")\n"));
        UNSCOPED_INFO("a handler, in its state's table");
        CHECK(has(r, "    --touch_start = function(detected)\n    --    ll.Say(0, \"touched\")\n    --end,\n"));
        CHECK(r.commentsConverted == 4);
        CHECK(r.commentsLeft == 0);
        require_checks_clean(r.text);
        UNSCOPED_INFO("each switched on, SLua that checks clean");
        require_checks_clean(switchedOn(r.text, "--local gOld", 1));
        require_checks_clean(switchedOn(r.text, "--print(\"touched by", 1));
        require_checks_clean(switchedOn(r.text, "--touch_start = function", 3));
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: left as the LSL had it where it is not code that resolves",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "integer gCount = 0;\n"
                                "integer DEBUG = FALSE;\n"
                                "// integer DEBUG = TRUE;\n"         // declared again
                                "default {\n"
                                "    state_entry() {\n"
                                "        // Say hello (to all).\n"   // prose
                                "        //llSay(0, \"a\";\n"        // no parse
                                "        llSay(0, \"a\");\n"
                                "        //nope();\n"                // undeclared
                                "        llSay(0, \"b\");\n"
                                "        //if (gCount > 0) {\n"      // a brace across two
                                "        llSay(0, \"c\");\n"
                                "        //}\n"
                                "        llSay(0, \"d\");\n"
                                "        //gCount;\n"                // a name, which writes nothing
                                "        if (DEBUG) llSay(0, \"e\");\n"
                                "    }\n"
                                "}\n";
        ALLSLCommentCode::Report  report;
        const ALLSLToSLua::Result r = converted(lsl, &report);
        CAPTURE(r.text);
        UNSCOPED_INFO("the plain conversion, byte for byte");
        CHECK(r.text == plain(lsl).text);
        CHECK(r.commentsConverted == 0);
        CHECK(r.commentsLeft == 7);
        REQUIRE(report.tried.size() == 7);
        UNSCOPED_INFO("what was tried, a run line by line once whole it was not, and why each was left");
        CHECK(report.tried[0].unit.line == 3);
        CHECK(report.tried[0].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[1].unit.line == 6);
        CHECK(report.tried[1].unit.endLine == 6);
        CHECK(report.tried[1].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[2].unit.line == 7);
        CHECK(report.tried[2].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[3].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[4].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[5].outcome == ALLSLCommentCode::Outcome::Refused);
        CHECK(report.tried[6].outcome == ALLSLCommentCode::Outcome::NoStatement);
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: a unit's lines: several for one; left where it would change another line",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "key gOwner;\n"
                                "default {\n"
                                "    state_entry() {\n"
                                "        gOwner = llGetOwner();\n"
                                "        //integer j; for (j = 0; j < 3; j++) { llSay(0, (string)j); j += 1; }\n"
                                "        llOwnerSay((string)gOwner);\n"
                                "        //vector v = llVecNorm(llGetPos());\n"  // a helper
                                "        llSay(0, \"a\");\n"
                                "        //gOwner = \"abc\";\n"                  // the key made text
                                "        llSay(0, \"b\");\n"
                                "    }\n"
                                "}\n";
        ALLSLCommentCode::Report  report;
        const ALLSLToSLua::Result r = converted(lsl, &report);
        CAPTURE(r.text);
        UNSCOPED_INFO("a for counting as LSL's might not, a while over lines");
        CHECK(has(r, "\n--local j = 0\n"
                     "--j = 0\n"
                     "--while j < 3 do\n"
                     "--    ll.Say(0, tostring(j))\n"
                     "--    j += 1\n"
                     "--    j += 1\n"
                     "--end\n"
                     "print(tostring(gOwner))\n"));
        UNSCOPED_INFO("a helper, and a key made text, left");
        CHECK(has(r, "\n--vector v = llVecNorm(llGetPos());\n"));
        CHECK(has(r, "\n--gOwner = \"abc\";\n"));
        REQUIRE(report.tried.size() == 3);
        CHECK(report.tried[0].outcome == ALLSLCommentCode::Outcome::Converted);
        CHECK(report.tried[1].outcome == ALLSLCommentCode::Outcome::Changes);
        CHECK(report.tried[2].outcome == ALLSLCommentCode::Outcome::Changes);
        require_checks_clean(switchedOn(r.text, "--local j = 0", 7));
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: written where it stood: the marks' spacing, its depth, a block's level, the toggle",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "integer gCount = 1;\n"
                                "default {\n"
                                "    state_entry() {\n"
                                "        //llSay(0, \"tight\");\n"
                                "\n"
                                "        // llSay(0, \"spaced\");\n"
                                "        if (gCount) {\n"
                                "            if (gCount > 1) {\n"
                                "                //llSay(0, \"deep\");\n"
                                "                llSay(0, \"x\");\n"
                                "            }\n"
                                "        }\n"
                                "        /* llSay(0, \"a]]b\"); */\n"
                                "        /*  llSay(0, \"one\");\n"
                                "            llSay(0, \"two\");  */\n"
                                "        /*\n"
                                "        if (gCount > 2) {\n"
                                "            llSay(0, \"off\");\n"
                                "        }\n"
                                "        //*/\n"
                                "        llSay(0, \"end\");\n"
                                "    }\n"
                                "}\n";
        const ALLSLToSLua::Result r = converted(lsl);
        CAPTURE(r.text);
        UNSCOPED_INFO("the spacing after the marks, as the LSL had it");
        CHECK(has(r, "\n--ll.Say(0, \"tight\")\n\n-- ll.Say(0, \"spaced\")\n"));
        UNSCOPED_INFO("at the depth it was written at");
        CHECK(has(r, "\n        --ll.Say(0, \"deep\")\n        ll.Say(0, \"x\")\n"));
        UNSCOPED_INFO("a block at a level nothing in it closes");
        CHECK(has(r, "\n--[=[ ll.Say(0, \"a]]b\") ]=]\n"));
        UNSCOPED_INFO("a block's code on its marks' lines, as it was");
        CHECK(has(r, "\n--[[  ll.Say(0, \"one\")\nll.Say(0, \"two\")  ]]\n"));
        UNSCOPED_INFO("the toggle turned off, as such");
        CHECK(has(r, "\n--[[\nif gCount > 2 then\n    ll.Say(0, \"off\")\nend\n--]]\nll.Say(0, \"end\")\n"));
        CHECK(r.commentsConverted == 6);
        require_checks_clean(r.text);
        // Turned on with a dash, as the LSL was with a slash taken out.
        std::string on = r.text;
        on.insert(on.find("--[[\nif gCount > 2"), "-");
        CAPTURE(on);
        CHECK(on.find("---[[\nif gCount > 2 then\n    ll.Say(0, \"off\")\nend\n--]]\n") != std::string::npos);
        require_checks_clean(on);
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: a run of prose and code: its code converted, its prose kept",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "key gOwner;\n"
                                "default {\n"
                                "    state_entry() {\n"
                                "        gOwner = llGetOwner();\n"
                                "        // old way:\n"                                  // words, kept
                                "        // llSay(0, \"x\");\n"                         // converted
                                "        llOwnerSay((string)gOwner);\n"
                                "        //list l = llCSV2List(\"a,b\"); llSay(0, llList2String(l, 0));\n"
                                "        //gOwner = \"abc\";\n"                         // a line at a time: the key made text
                                "\n"
                                "        //if (gOwner) llSay(0, \"a\");\n"              // whole, its else a line of words
                                "        //else\n"
                                "        //    llSay(0, \"b\");\n"
                                "    }\n"
                                "}\n";
        ALLSLCommentCode::Report  report;
        const ALLSLToSLua::Result r = converted(lsl, &report);
        CAPTURE(r.text);
        UNSCOPED_INFO("the words kept over the code converted");
        CHECK(has(r, "\n-- old way:\n-- ll.Say(0, \"x\")\nprint(tostring(gOwner))\n"));
        UNSCOPED_INFO("a line converted where the run with the next was not");
        CHECK(has(r, "\n--local l = ll.CSV2List(\"a,b\")\n--ll.Say(0, l[1] or \"\")\n--gOwner = \"abc\";\n"));
        UNSCOPED_INFO("a run whole where it is code whole");
        CHECK(has(r, "\n\n--if gOwner.istruthy then\n--    ll.Say(0, \"a\")\n--else\n--    ll.Say(0, \"b\")\n--end\n"));
        CHECK(r.commentsConverted == 3);
        CHECK(r.commentsLeft == 1);
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: nothing noted of commented code; the live code's notes said still",
                     "[ALLSLCommentCode]")
    {
        const std::string lsl = "default {\n"
                                "    state_entry() {\n"
                                "        //llOwnerSay((string)llGetPos());\n"
                                "        llOwnerSay((string)llGetPos());\n"
                                "    }\n"
                                "}\n";
        const ALLSLToSLua::Result r = converted(lsl);
        CAPTURE(r.text);
        CHECK(has(r, "\n--print(ll.DumpList2String({ ll.GetPos() }, \"\"))\n-- LSL: "));
        size_t vector_notes = 0;
        for (const ALScriptProblem& p : r.notes)
        {
            CHECK(p.line == 3);
            vector_notes += p.key == "SluaVectorText" ? 1 : 0;
        }
        CHECK(vector_notes == 1);
    }

    TEST_CASE_METHOD(allslcommentcode_data, "ALLSLCommentCode: off, or with no comments kept, nothing tried", "[ALLSLCommentCode]")
    {
        const std::string lsl = "default {\n"
                                "    state_entry() {\n"
                                "        //llSay(0, \"a\");\n"
                                "        llSay(0, \"b\");\n"
                                "    }\n"
                                "}\n";
        ALLSLToSLua::Options off;
        off.commentCode             = false;
        const ALLSLToSLua::Result a = ALLSLToSLua::convert(lsl, off);
        CHECK(a.text.find("\n--llSay(0, \"a\");\n") != std::string::npos);
        CHECK(a.commentsConverted == 0);
        CHECK(a.commentsLeft == 0);
        ALLSLToSLua::Options none;
        none.keepComments           = false;
        const ALLSLToSLua::Result b = ALLSLToSLua::convert(lsl, none);
        CHECK(b.text.find("--ll.Say") == std::string::npos);
        CHECK(b.text.find("--llSay") == std::string::npos);
        const ALLSLToSLua::Result c = ALLSLToSLua::convert(lsl);
        CHECK(c.text.find("\n--ll.Say(0, \"a\")\n") != std::string::npos);
        CHECK(c.commentsConverted == 1);
        UNSCOPED_INFO("close to LSL changes nothing of it");
        const ALLSLToSLua::Result d = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CHECK(d.commentsConverted == 1);
    }
}
