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

#include "../test/lltut.h"

namespace tut
{
    struct alscriptmessages_data
    {
    };
    typedef test_group<alscriptmessages_data> alscriptmessages_group;
    typedef alscriptmessages_group::object    alscriptmessages_object;
    tut::alscriptmessages_group               alscriptmessages_instance("alscriptmessages");

    template<> template<>
    void alscriptmessages_object::test<1>()
    {
        set_test_name("an LSL compiler's line gives its place, its level and its words, zero-based as the viewer counts");
        const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic("(12, 4) : ERROR : Syntax error", false);
        ensure_equals("the line as it came", place.line, 12);
        ensure_equals("the column too", place.column, 4);
        ensure("a column was given", place.hasColumn);
        ensure_equals("the level", place.level, std::string("ERROR"));
        ensure_equals("the words", place.message, std::string("Syntax error"));
        const ALScriptMessages::Place warned = ALScriptMessages::readDiagnostic("(3, 0) : WARNING : Unused variable x", false);
        ensure_equals("a warning says so", warned.level, std::string("WARNING"));
        // Anything else is an error with no place: "Math Error" comes so.
        const ALScriptMessages::Place bare = ALScriptMessages::readDiagnostic("Math Error", false);
        ensure_equals("no place", bare.line, 0);
        ensure("and none given", !bare.hasColumn && !bare.hasLine && place.hasLine);
        ensure_equals("an error all the same", bare.level, std::string("ERROR"));
        ensure_equals("the words as they came", bare.message, std::string("Math Error"));
    }

    template<> template<>
    void alscriptmessages_object::test<2>()
    {
        set_test_name("Luau's line names the chunk and counts from one, which the viewer counts from zero");
        const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic("script:14: unexpected symbol near 'end'", true);
        ensure_equals("one off the line", place.line, 13);
        ensure("no column", !place.hasColumn);
        ensure_equals("an error", place.level, std::string("ERROR"));
        ensure_equals("the words past the colon", place.message, std::string("unexpected symbol near 'end'"));
        // Line one stays line zero rather than going negative.
        ensure_equals("the first line", ALScriptMessages::readDiagnostic("x:1: bad", true).line, 0);
        // A line of neither form is no place: the whole is the words.
        const ALScriptMessages::Place other = ALScriptMessages::readDiagnostic("Math Error", true);
        ensure_equals("nothing found", other.message, std::string("Math Error"));
        ensure("no line named", !other.hasLine && place.hasLine);
    }

    template<> template<>
    void alscriptmessages_object::test<3>()
    {
        set_test_name("every line of a compile's answer, in order");
        LLSD errors;
        errors.append("(1, 2) : ERROR : first");
        errors.append("(3, 4) : WARNING : second");
        const std::vector<ALScriptMessages::Place> places = ALScriptMessages::readDiagnostics(errors, false);
        ensure_equals("both", places.size(), size_t(2));
        ensure_equals("in order", places[1].message, std::string("second"));
        ensure_equals("with their places", places[1].line, 3);
    }

    template<> template<>
    void alscriptmessages_object::test<4>()
    {
        set_test_name("a script with no default state is taken for Lua");
        ensure("no state anywhere", ALScriptMessages::looksLikeLua("local x = 1\nprint(x)\n"));
        ensure("a default state is LSL", !ALScriptMessages::looksLikeLua("default\n{\n    state_entry()\n    {\n    }\n}\n"));
        ensure("however it is spaced", !ALScriptMessages::looksLikeLua("default   {"));
        ensure("and after other code", !ALScriptMessages::looksLikeLua("integer n;\nstate two { }\ndefault {\n}\n"));
    }

    template<> template<>
    void alscriptmessages_object::test<5>()
    {
        set_test_name("a run-time error's header names the object and the script, and the lines that follow it belong with it");
        ALScriptMessages::Header named;
        ensure("a header", ALScriptMessages::readRuntimeHeader("Object [script:My Script] Script run-time error", named));
        ensure_equals("the object", named.object, std::string("Object"));
        ensure_equals("the script, spaces and all", named.script, std::string("My Script"));
        ensure("ending with the words marks it", ALScriptMessages::endsRuntimeError("Object [script:x] Script run-time error"));
        ensure("an ordinary line is no header", !ALScriptMessages::readRuntimeHeader("Object: hello there", named));
        ensure("nor marked", !ALScriptMessages::endsRuntimeError("Object: hello there"));
        // The words alone, without the brackets, are not a header.
        ensure("no script named", !ALScriptMessages::readRuntimeHeader("Script run-time error", named));
    }

    template<> template<>
    void alscriptmessages_object::test<6>()
    {
        set_test_name("where a run-time error happened, from the lines after the header, by the VM that said it");
        ALScriptMessages::Location where;
        const std::vector<std::string> luau = { "Object [script:s] Script run-time error", "s:7: attempt to index nil", "stack traceback:" };
        ensure("Luau's", ALScriptMessages::readRuntimeLocation(luau, true, where));
        ensure_equals("one off its line", where.line, 6);
        ensure_equals("no column", where.column, -1);
        ensure_equals("the words", where.message, std::string("attempt to index nil"));
        const std::vector<std::string> lsl = { "Object [script:s] Script run-time error", "(5, 2) : ERROR : Stack-Heap Collision" };
        ensure("LSL's", ALScriptMessages::readRuntimeLocation(lsl, false, where));
        ensure_equals("the line as it came", where.line, 5);
        ensure_equals("and the column", where.column, 2);
        ensure_equals("the words past the level", where.message, std::string("Stack-Heap Collision"));
        // Nothing says where: the caller falls back to the first line
        // that is not the header.
        const std::vector<std::string> nowhere = { "Object [script:s] Script run-time error", "Math Error" };
        ensure("nothing found", !ALScriptMessages::readRuntimeLocation(nowhere, false, where));
        // An LSL error read as Luau's is not read by it.
        ensure("the wrong VM finds nothing", !ALScriptMessages::readRuntimeLocation(lsl, true, where) || where.line != 5);
    }

    template<> template<>
    void alscriptmessages_object::test<7>()
    {
        set_test_name("a Luau traceback's frames, each its chunk and its line, and nothing else read as one");
        ALScriptMessages::Frame frame;
        ensure("a bare frame", ALScriptMessages::readStackFrame("s:7", frame));
        ensure_equals("its chunk", frame.chunk, std::string("s"));
        ensure_equals("its line, one off", frame.line, 6);
        ensure("indented, with the function", ALScriptMessages::readStackFrame("  My Script:12 function counter", frame));
        ensure_equals("a script's name, spaces and all", frame.chunk, std::string("My Script"));
        ensure_equals("that line", frame.line, 11);
        ensure("as Lua quotes a chunk", ALScriptMessages::readStackFrame("[string \"My Script\"]:30: in function 'tick'", frame));
        ensure_equals("the quoted name, spaces and all", frame.chunk, std::string("My Script"));
        ensure_equals("and its line", frame.line, 29);
        ensure("the heading is no frame", !ALScriptMessages::readStackFrame("stack traceback:", frame));
        ensure("nor a C function", !ALScriptMessages::readStackFrame("[C] function error", frame));
        ensure("nor the error's own line", !ALScriptMessages::readStackFrame("s:7: attempt to index nil", frame));
        ensure("nor words", !ALScriptMessages::readStackFrame("Math Error", frame));
        ensure("nor a line zero", !ALScriptMessages::readStackFrame("s:0", frame));
    }

    template<> template<>
    void alscriptmessages_object::test<8>()
    {
        // As the simulator's executor says a fault: the error as the VM
        // raised it, then lua_debugtrace's frames -- the script loaded as
        // "=lua_script", so its chunk reads `lua_script`; a C function as
        // `[C]`; a deep stack's middle folded -- each on a line of its
        // own (slua Executor/src/Script.cpp, VM/src/ldebug.cpp).
        set_test_name("a SLua fault as the simulator says it: where it happened, and each frame of the script");
        const std::vector<std::string> said = { "Object [script:Counter] Script run-time error",
                                                "lua_script:12: attempt to index nil with 'field'",
                                                "[C] function error",
                                                "lua_script:12 function tick",
                                                "... (+4 frames)",
                                                "lua_script:30" };
        ALScriptMessages::Header named;
        ensure("the header", ALScriptMessages::readRuntimeHeader(said[0], named));
        ensure_equals("names the script", named.script, std::string("Counter"));
        ALScriptMessages::Location where;
        ensure("where", ALScriptMessages::readRuntimeLocation(said, true, where));
        ensure_equals("its line", where.line, 11);
        ensure_equals("its words", where.message, std::string("attempt to index nil with 'field'"));
        std::vector<S32> frames;
        for (size_t i = 1; i < said.size(); ++i)
        {
            ALScriptMessages::Frame frame;
            if (ALScriptMessages::readStackFrame(said[i], frame))
            {
                ensure_equals("the script's chunk", frame.chunk, std::string("lua_script"));
                frames.push_back(frame.line);
            }
        }
        ensure_equals("the script's two frames, and nothing else", frames.size(), size_t(2));
        ensure_equals("the function's", frames[0], 11);
        ensure_equals("the one that called it", frames[1], 29);
    }

    template<> template<>
    void alscriptmessages_object::test<9>()
    {
        set_test_name("what goes on with a run-time error after its words, and what a script says besides");
        for (const char* goes_on : { "lua_script:12: attempt to index nil with 'field'", "stack traceback:", "[C] function error",
                                     "lua_script:12 function tick", "  lua_script:30", "... (+4 frames)", "(12, 3) : ERROR : Stack-Heap Collision" })
        {
            ensure(std::string("goes on: ") + goes_on, ALScriptMessages::continuesRuntimeError(goes_on));
        }
        for (const char* besides : { "hello", "Counter: 12 visitors", "", "   ", "Object [script:Counter] Script run-time error" })
        {
            ensure(std::string("its own: ") + besides, !ALScriptMessages::continuesRuntimeError(besides));
        }
    }

    template<> template<>
    void alscriptmessages_object::test<10>()
    {
        set_test_name("a line in the other compiler's form is read in that form: an LSL script the grid compiles for Luau's VM may be "
                      "answered in either");
        const ALScriptMessages::Place luau = ALScriptMessages::readDiagnostic("script:7: bad thing", false);
        ensure("Luau's, for LSL", luau.hasLine && luau.line == 6 && !luau.hasColumn && luau.message == "bad thing");
        const ALScriptMessages::Place lsl = ALScriptMessages::readDiagnostic("(4, 2) : ERROR : Syntax error", true);
        ensure("LSL's, for SLua", lsl.hasLine && lsl.line == 4 && lsl.hasColumn && lsl.column == 2 && lsl.message == "Syntax error");
    }

    template<> template<>
    void alscriptmessages_object::test<11>()
    {
        set_test_name("Tailslide's line, as the grid's LSL compiler for Luau's VM says it: its line and the places its words name "
                      "count from one, which the viewer counts from zero; its level as it says it");
        const std::string said = "Line 320: WARN: Declaration of `activeRequest' in this scope shadows previous declaration at (345, 1)";
        for (const bool lua : { false, true })
        {
            const ALScriptMessages::Place place = ALScriptMessages::readDiagnostic(said, lua);
            ensure("a line named", place.hasLine && !place.hasColumn);
            ensure_equals("one off the line", place.line, 319);
            ensure_equals("its level", place.level, std::string("WARN"));
            ensure_equals("the words past the level",
                          place.message, std::string("Declaration of `activeRequest' in this scope shadows previous declaration at (345, 1)"));
            ensure_equals("the place the words name", place.mentions.size(), size_t(1));
            const ALScriptMessages::Mention& named = place.mentions.front();
            ensure_equals("where it stands", place.message.substr(named.at, named.length), std::string("(345, 1)"));
            ensure("one off each", named.line == 344 && named.column == 0);
        }
        const ALScriptMessages::Place error = ALScriptMessages::readDiagnostic("Line 1: ERROR: Duplicate declaration of `x'; previously "
                                                                               "declared at (1, 9). And (2, 3).", false);
        ensure("an error on the first line", error.hasLine && error.line == 0 && error.level == "ERROR");
        ensure_equals("each place named", error.mentions.size(), size_t(2));
        ensure("in order", error.mentions[0].line == 0 && error.mentions[0].column == 8 && error.mentions[1].line == 1 &&
                               error.mentions[1].column == 2);
        // Words with a place in them, in LSL's own form, are LSL's.
        ensure("no Tailslide in LSL's", ALScriptMessages::readDiagnostic("(4, 2) : ERROR : at (1, 1)", false).mentions.empty());
    }
}
