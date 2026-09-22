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

#include "../alscriptmessages.h"

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
        ensure("and none given", !bare.hasColumn);
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
        // An LSL line read as Luau's is not one: the whole is the words.
        const ALScriptMessages::Place other = ALScriptMessages::readDiagnostic("Math Error", true);
        ensure_equals("nothing found", other.message, std::string("Math Error"));
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
}
