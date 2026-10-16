/**
 * @file tests/alscriptmessages_test.cpp
 * @brief What the compilers and the running scripts say, read.
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

#include "../core/alscriptmessages.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("alscriptmessages: an LSL compiler's line gives its zero-based place and its level and words", "[alscriptmessages]")
    {
        const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic("(12, 4) : ERROR : Syntax error", false);
        UNSCOPED_INFO("the line as it came");
        CHECK(place.line == 12);
        CHECK(place.column == 4);
        CHECK(place.hasColumn);
        CHECK(place.level == std::string("ERROR"));
        CHECK(place.message == std::string("Syntax error"));
        const ALScriptMessages::Place warned = ALScriptMessages::readDiagnostic("(3, 0) : WARNING : Unused variable x", false);
        CHECK(warned.level == std::string("WARNING"));
        // Anything else is an error with no place: "Math Error" comes so.
        const ALScriptMessages::Place bare = ALScriptMessages::readDiagnostic("Math Error", false);
        UNSCOPED_INFO("no place");
        CHECK(bare.line == 0);
        {
            INFO("and none given");
            CHECK_FALSE(bare.hasColumn);
            CHECK_FALSE(bare.hasLine);
            CHECK(place.hasLine);
        }
        UNSCOPED_INFO("an error all the same");
        CHECK(bare.level == std::string("ERROR"));
        UNSCOPED_INFO("the words as they came");
        CHECK(bare.message == std::string("Math Error"));
    }

    TEST_CASE("alscriptmessages: Luau's line names its chunk and is moved from one-based to zero-based", "[alscriptmessages]")
    {
        const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic("script:14: unexpected symbol near 'end'", true);
        UNSCOPED_INFO("one off the line");
        CHECK(place.line == 13);
        CHECK_FALSE(place.hasColumn);
        CHECK(place.level == std::string("ERROR"));
        UNSCOPED_INFO("the words past the colon");
        CHECK(place.message == std::string("unexpected symbol near 'end'"));
        // Line one stays line zero rather than going negative.
        CHECK(ALScriptMessages::readDiagnostic("x:1: bad", true).line == 0);
        // A line of neither form is no place: the whole is the words.
        const ALScriptMessages::Place other = ALScriptMessages::readDiagnostic("Math Error", true);
        UNSCOPED_INFO("nothing found");
        CHECK(other.message == std::string("Math Error"));
        {
            INFO("no line named");
            CHECK_FALSE(other.hasLine);
            CHECK(place.hasLine);
        }
    }

    TEST_CASE("alscriptmessages: every line of a compile's answer in order", "[alscriptmessages]")
    {
        LLSD errors;
        errors.append("(1, 2) : ERROR : first");
        errors.append("(3, 4) : WARNING : second");
        const std::vector<ALScriptMessages::Place> places = ALScriptMessages::readDiagnostics(errors, false);
        REQUIRE(places.size() == size_t(2));
        UNSCOPED_INFO("in order");
        CHECK(places[1].message == std::string("second"));
        UNSCOPED_INFO("with their places");
        CHECK(places[1].line == 3);
    }

    TEST_CASE("alscriptmessages: a script with no default state is taken for Lua", "[alscriptmessages]")
    {
        UNSCOPED_INFO("no state anywhere");
        CHECK(ALScriptMessages::looksLikeLua("local x = 1\nprint(x)\n"));
        UNSCOPED_INFO("a default state is LSL");
        CHECK_FALSE(ALScriptMessages::looksLikeLua("default\n{\n    state_entry()\n    {\n    }\n}\n"));
        UNSCOPED_INFO("however it is spaced");
        CHECK_FALSE(ALScriptMessages::looksLikeLua("default   {"));
        UNSCOPED_INFO("and after other code");
        CHECK_FALSE(ALScriptMessages::looksLikeLua("integer n;\nstate two { }\ndefault {\n}\n"));
    }

    TEST_CASE("alscriptmessages: a run-time error's header names the object and script and marks what follows", "[alscriptmessages]")
    {
        ALScriptMessages::Header named;
        REQUIRE(ALScriptMessages::readRuntimeHeader("Object [script:My Script] Script run-time error", named));
        CHECK(named.object == std::string("Object"));
        UNSCOPED_INFO("the script, spaces and all");
        CHECK(named.script == std::string("My Script"));
        UNSCOPED_INFO("ending with the words marks it");
        CHECK(ALScriptMessages::endsRuntimeError("Object [script:x] Script run-time error"));
        UNSCOPED_INFO("an ordinary line is no header");
        CHECK_FALSE(ALScriptMessages::readRuntimeHeader("Object: hello there", named));
        UNSCOPED_INFO("nor marked");
        CHECK_FALSE(ALScriptMessages::endsRuntimeError("Object: hello there"));
        // The words alone, without the brackets, are not a header.
        UNSCOPED_INFO("no script named");
        CHECK_FALSE(ALScriptMessages::readRuntimeHeader("Script run-time error", named));
    }

    TEST_CASE("alscriptmessages: where a run-time error happened is read by the VM that said it", "[alscriptmessages]")
    {
        ALScriptMessages::Location where;
        const std::vector<std::string> luau = { "Object [script:s] Script run-time error", "s:7: attempt to index nil", "stack traceback:" };
        UNSCOPED_INFO("Luau's");
        REQUIRE(ALScriptMessages::readRuntimeLocation(luau, true, where));
        UNSCOPED_INFO("one off its line");
        CHECK(where.line == 6);
        UNSCOPED_INFO("no column");
        CHECK(where.column == -1);
        CHECK(where.message == std::string("attempt to index nil"));
        const std::vector<std::string> lsl = { "Object [script:s] Script run-time error", "(5, 2) : ERROR : Stack-Heap Collision" };
        UNSCOPED_INFO("LSL's");
        CHECK(ALScriptMessages::readRuntimeLocation(lsl, false, where));
        UNSCOPED_INFO("the line as it came");
        CHECK(where.line == 5);
        CHECK(where.column == 2);
        UNSCOPED_INFO("the words past the level");
        CHECK(where.message == std::string("Stack-Heap Collision"));
        // Nothing says where: the caller falls back to the first line
        // that is not the header.
        const std::vector<std::string> nowhere = { "Object [script:s] Script run-time error", "Math Error" };
        UNSCOPED_INFO("nothing found");
        CHECK_FALSE(ALScriptMessages::readRuntimeLocation(nowhere, false, where));
        // An LSL error read as Luau's is not read by it.
        UNSCOPED_INFO("the wrong VM finds nothing");
        CHECK((!ALScriptMessages::readRuntimeLocation(lsl, true, where) || where.line != 5));
    }

    TEST_CASE("alscriptmessages: a Luau traceback frame gives its chunk and line and nothing else is one", "[alscriptmessages]")
    {
        ALScriptMessages::Frame frame;
        UNSCOPED_INFO("a bare frame");
        REQUIRE(ALScriptMessages::readStackFrame("s:7", frame));
        CHECK(frame.chunk == std::string("s"));
        UNSCOPED_INFO("its line, one off");
        CHECK(frame.line == 6);
        UNSCOPED_INFO("indented, with the function");
        CHECK(ALScriptMessages::readStackFrame("  My Script:12 function counter", frame));
        UNSCOPED_INFO("a script's name, spaces and all");
        CHECK(frame.chunk == std::string("My Script"));
        CHECK(frame.line == 11);
        UNSCOPED_INFO("as Lua quotes a chunk");
        CHECK(ALScriptMessages::readStackFrame("[string \"My Script\"]:30: in function 'tick'", frame));
        UNSCOPED_INFO("the quoted name, spaces and all");
        CHECK(frame.chunk == std::string("My Script"));
        CHECK(frame.line == 29);
        UNSCOPED_INFO("the heading is no frame");
        CHECK_FALSE(ALScriptMessages::readStackFrame("stack traceback:", frame));
        UNSCOPED_INFO("nor a C function");
        CHECK_FALSE(ALScriptMessages::readStackFrame("[C] function error", frame));
        UNSCOPED_INFO("nor the error's own line");
        CHECK_FALSE(ALScriptMessages::readStackFrame("s:7: attempt to index nil", frame));
        UNSCOPED_INFO("nor words");
        CHECK_FALSE(ALScriptMessages::readStackFrame("Math Error", frame));
        UNSCOPED_INFO("nor a line zero");
        CHECK_FALSE(ALScriptMessages::readStackFrame("s:0", frame));
    }

    TEST_CASE("alscriptmessages: a SLua fault as the simulator says it: its place and the script's frames", "[alscriptmessages]")
    {
        // As the simulator's executor says a fault: the error as the VM
        // raised it, then lua_debugtrace's frames -- the script loaded as
        // "=lua_script", so its chunk reads `lua_script`; a C function as
        // `[C]`; a deep stack's middle folded -- each on a line of its
        // own (slua Executor/src/Script.cpp, VM/src/ldebug.cpp).
        const std::vector<std::string> said = { "Object [script:Counter] Script run-time error",
                                                "lua_script:12: attempt to index nil with 'field'",
                                                "[C] function error",
                                                "lua_script:12 function tick",
                                                "... (+4 frames)",
                                                "lua_script:30" };
        ALScriptMessages::Header named;
        REQUIRE(ALScriptMessages::readRuntimeHeader(said[0], named));
        CHECK(named.script == std::string("Counter"));
        ALScriptMessages::Location where;
        REQUIRE(ALScriptMessages::readRuntimeLocation(said, true, where));
        CHECK(where.line == 11);
        CHECK(where.message == std::string("attempt to index nil with 'field'"));
        std::vector<S32> frames;
        for (size_t i = 1; i < said.size(); ++i)
        {
            ALScriptMessages::Frame frame;
            if (ALScriptMessages::readStackFrame(said[i], frame))
            {
                UNSCOPED_INFO("the script's chunk");
                REQUIRE(frame.chunk == std::string("lua_script"));
                frames.push_back(frame.line);
            }
        }
        UNSCOPED_INFO("the script's two frames, and nothing else");
        REQUIRE(frames.size() == size_t(2));
        UNSCOPED_INFO("the function's");
        CHECK(frames[0] == 11);
        UNSCOPED_INFO("the one that called it");
        CHECK(frames[1] == 29);
    }

    TEST_CASE("alscriptmessages: continuesRuntimeError tells a run-time error's lines from a script's own", "[alscriptmessages]")
    {
        for (const char* goes_on : { "lua_script:12: attempt to index nil with 'field'", "stack traceback:", "[C] function error",
                                     "lua_script:12 function tick", "  lua_script:30", "... (+4 frames)", "(12, 3) : ERROR : Stack-Heap Collision" })
        {
            CAPTURE(goes_on);
            REQUIRE(ALScriptMessages::continuesRuntimeError(goes_on));
        }
        for (const char* besides : { "hello", "Counter: 12 visitors", "", "   ", "Object [script:Counter] Script run-time error" })
        {
            CAPTURE(besides);
            REQUIRE_FALSE(ALScriptMessages::continuesRuntimeError(besides));
        }
    }

    TEST_CASE("alscriptmessages: a line in the other compiler's form is read in that form", "[alscriptmessages]")
    {
        const ALScriptMessages::Place luau = ALScriptMessages::readDiagnostic("script:7: bad thing", false);
        {
            INFO("Luau's, for LSL");
            CHECK(luau.hasLine);
            CHECK(luau.line == 6);
            CHECK_FALSE(luau.hasColumn);
            CHECK(luau.message == "bad thing");
        }
        const ALScriptMessages::Place lsl = ALScriptMessages::readDiagnostic("(4, 2) : ERROR : Syntax error", true);
        {
            INFO("LSL's, for SLua");
            CHECK(lsl.hasLine);
            CHECK(lsl.line == 4);
            CHECK(lsl.hasColumn);
            CHECK(lsl.column == 2);
            CHECK(lsl.message == "Syntax error");
        }
    }

    TEST_CASE("alscriptmessages: Tailslide's line and the places it names count from one; its level as said", "[alscriptmessages]")
    {
        const std::string said = "Line 320: WARN: Declaration of `activeRequest' in this scope shadows previous declaration at (345, 1)";
        for (const bool lua : { false, true })
        {
            CAPTURE(lua);
            const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic(said, lua);
            {
                INFO("a line named");
                REQUIRE(place.hasLine);
                REQUIRE_FALSE(place.hasColumn);
            }
            UNSCOPED_INFO("one off the line");
            REQUIRE(place.line == 319);
            REQUIRE(place.level == std::string("WARN"));
            UNSCOPED_INFO("the words past the level");
            REQUIRE(place.message == std::string("Declaration of `activeRequest' in this scope shadows previous declaration at (345, 1)"));
            UNSCOPED_INFO("the place the words name");
            REQUIRE(place.mentions.size() == size_t(1));
            const ALScriptMessages::Mention& named = place.mentions.front();
            UNSCOPED_INFO("where it stands");
            REQUIRE(place.message.substr(named.at, named.length) == std::string("(345, 1)"));
            {
                INFO("one off each");
                REQUIRE(named.line == 344);
                REQUIRE(named.column == 0);
            }
        }
        const ALScriptMessages::Place error = ALScriptMessages::readDiagnostic("Line 1: ERROR: Duplicate declaration of `x'; previously "
                                                                               "declared at (1, 9). And (2, 3).", false);
        {
            INFO("an error on the first line");
            CHECK(error.hasLine);
            CHECK(error.line == 0);
            CHECK(error.level == "ERROR");
        }
        UNSCOPED_INFO("each place named");
        REQUIRE(error.mentions.size() == size_t(2));
        {
            INFO("in order");
            CHECK(error.mentions[0].line == 0);
            CHECK(error.mentions[0].column == 8);
            CHECK(error.mentions[1].line == 1);
            CHECK(error.mentions[1].column == 2);
        }
        // Words with a place in them, in LSL's own form, are LSL's.
        UNSCOPED_INFO("no Tailslide in LSL's");
        REQUIRE(ALScriptMessages::readDiagnostic("(4, 2) : ERROR : at (1, 1)", false).mentions.empty());
    }
}
