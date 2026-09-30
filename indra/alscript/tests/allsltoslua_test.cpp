/**
 * @file allsltoslua_test.cpp
 * @brief Tests for ALLSLToSLua: LSL written again as SLua, checked as SLua.
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

#include "../allsltoslua.h"
#include "../allslservice.h"
#include "../alluauservice.h"

#include "../test/lltut.h"

#include <fstream>
#include <sstream>

namespace tut
{
    struct allsltoslua_data
    {
        ALLuauService service;
        bool          definitions = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        allsltoslua_data()
        {
            static bool loaded = false;
            std::string error;
            if (!loaded)
            {
                ALLSLService lsl;
                loaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
                if (!loaded)
                {
                    fail("the builtins did not load: " + error);
                }
            }
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            service.setNewSolver(newSolver, error);
            definitions = service.loadDefinitions(text.str(), error);
            // Nonstrict, as the grid compiles: the new solver checking strict
            // cannot yet push an overloaded function's parameter types into a
            // function given to it, so every LLEvents:on there is an error it
            // is upstream's to put right.
            ALLuauConfig grid;
            grid.mode = "nonstrict";
            service.setConfig(grid);
        }

        // Written again, and it must have been.
        ALLSLToSLua::Result convert(const std::string& lsl)
        {
            ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl);
            std::string         why;
            for (const ALScriptProblem& p : r.problems)
            {
                why += p.message + "\n";
            }
            ensure("converted: " + why, r.converted);
            return r;
        }

        // What SLua's check says of it: nothing at the error level, and
        // nothing the studio's own lints would have written SLua's way.
        void checksClean(const ALLSLToSLua::Result& r)
        {
            ensure("the definitions", definitions);
            std::string said;
            for (const ALScriptProblem& p : service.check(r.text))
            {
                if (p.severity == ALScriptProblem::Severity::Error || p.code == "SlCompoundAssign" || p.code == "SlNumberTruth")
                {
                    said += llformat("[%d:%d] %s\n", p.line + 1, p.column + 1, p.message.c_str());
                }
            }
            ensure("checks as SLua:\n" + said + "---\n" + r.text, said.empty());
        }

        static bool has(const ALLSLToSLua::Result& r, const std::string& text) { return r.text.find(text) != std::string::npos; }
        static size_t count(const ALLSLToSLua::Result& r, const std::string& text)
        {
            size_t n = 0;
            for (size_t at = r.text.find(text); at != std::string::npos; at = r.text.find(text, at + 1))
            {
                ++n;
            }
            return n;
        }
        static bool noted(const ALLSLToSLua::Result& r, const std::string& key)
        {
            for (const ALScriptProblem& p : r.notes)
            {
                if (p.key == key)
                {
                    return true;
                }
            }
            return false;
        }
    };

    typedef test_group<allsltoslua_data> allsltoslua_group;
    typedef allsltoslua_group::object    allsltoslua_object;
    tut::allsltoslua_group               allsltoslua_test("ALLSLToSLua");

    template<> template<>
    void allsltoslua_object::test<1>()
    {
        set_test_name("a script of one state: globals and functions as locals, handlers on LLEvents, the detected count from the table, state_entry run last");
        const ALLSLToSLua::Result r = convert("integer gCount = 2;\n"
                                              "string greet(string who) { return \"Hello, \" + who; }\n"
                                              "default {\n"
                                              "    state_entry() { llSay(0, greet(\"world\")); }\n"
                                              "    touch_start(integer total_number) {\n"
                                              "        gCount += total_number;\n"
                                              "        llSay(0, greet(llDetectedName(0)) + \" \" + (string)gCount);\n"
                                              "    }\n"
                                              "}\n");
        ensure("the global: " + r.text, has(r, "local gCount = 2"));
        ensure("the function: " + r.text, has(r, "local function greet(who)") && has(r, "return \"Hello, \" .. who"));
        ensure("the handler: " + r.text, has(r, "LLEvents:on(\"touch_start\", function(detected)") && has(r, "local total_number = #detected"));
        ensure("the step: " + r.text, has(r, "gCount += total_number"));
        ensure("what was detected from the table: " + r.text, has(r, "detected[1]:getName()") && noted(r, "SluaDetectedTable"));
        ensure("an integer as a string: " + r.text, has(r, "tostring(gCount)"));
        ensure("state_entry last: " + r.text, r.text.find("-- state_entry") > r.text.find("LLEvents:on") && has(r, "ll.Say(0, greet(\"world\"))"));
        ensure("the note over its line: " + r.text, has(r, "-- LSL: detected[n] is what LSL read"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<2>()
    {
        set_test_name("expressions: conditions as LSL reads them, comparisons as 1 or 0 where they are numbers, integer division and remainder noted, lists grown and measured");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    integer a = 7; integer b = 2; float f = 1.5; string s = \"x\"; list l = [1, 2];\n"
                                              "    integer same = a == b;\n"
                                              "    if (a) llOwnerSay(\"a\");\n"
                                              "    if (s) llOwnerSay(\"said\");\n"
                                              "    if (a > 1 && llGetListLength(l) > 1) llOwnerSay(\"both\");\n"
                                              "    integer q = a / b; integer m = a % b;\n"
                                              "    l += 3; l = l + [4] + \"five\";\n"
                                              "    integer t = TRUE; if (FALSE) t = 0;\n"
                                              "    llOwnerSay((string)f + (string)((integer)\"12abc\"));\n"
                                              "    if (l == [1]) llOwnerSay(\"one long\");\n"
                                              "    integer flags = a & b | 4;\n"
                                              "    vector v = <1, 2, 3>; v.x = 4; v.z += 1;\n"
                                              "    llOwnerSay((string)v + (string)same + (string)q + (string)m + (string)t + (string)flags);\n"
                                              "} }\n");
        ensure("a comparison as a number: " + r.text, has(r, "local same = if a == b then 1 else 0"));
        ensure("an integer's truth: " + r.text, has(r, "if a ~= 0 then"));
        ensure("a string's: " + r.text, has(r, "if s ~= \"\" then"));
        ensure("and: " + r.text, has(r, "if a > 1 and #l > 1 then"));
        ensure("integer division: " + r.text, has(r, "local q = a // b") && noted(r, "SluaIntegerDivision"));
        ensure("remainder: " + r.text, has(r, "local m = a % b") && noted(r, "SluaModulo"));
        ensure("a list no other holds grown in place: " + r.text, has(r, "table.insert(l, 3)") && has(r, "table.insert(l, 4)") &&
                                                                    has(r, "table.insert(l, \"five\")") && !has(r, "joinLists"));
        ensure("TRUE and FALSE: " + r.text, has(r, "local t = 1") && has(r, "if false then"));
        ensure("a float as LSL writes it: " + r.text, has(r, "string.format(\"%.6f\", f)"));
        ensure("a string as an integer: " + r.text, has(r, "lslInteger(\"12abc\")") && has(r, "local function lslInteger"));
        ensure("lists compared by length: " + r.text, has(r, "#l == #{1}") && noted(r, "SluaListCompare"));
        ensure("bits: " + r.text, has(r, "bit32.bor(bit32.band(a, b), 4)") && noted(r, "SluaBit32"));
        ensure("a vector's part set: " + r.text, has(r, "v = vector(4, v.y, v.z)") && has(r, "v = vector(v.x, v.y, v.z + 1)"));
        ensure("a vector as LSL writes it: " + r.text, has(r, "ll.DumpList2String({v}, \"\")"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<3>()
    {
        set_test_name("loops: a counting for as Luau's numeric for, a jump out as break and to the next turn as continue, a do as repeat; another jump noted");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    integer i; integer n;\n"
                                              "    for (i = 0; i < 10; ++i) {\n"
                                              "        if (i == 3) jump next;\n"
                                              "        if (i == 8) jump done;\n"
                                              "        n += i;\n"
                                              "        @next;\n"
                                              "    }\n"
                                              "    @done;\n"
                                              "    do { n -= 1; } while (n > 5);\n"
                                              "    while (n) { n--; }\n"
                                              "    jump away;\n"
                                              "    n = 1;\n"
                                              "    @away;\n"
                                              "    llOwnerSay((string)n);\n"
                                              "} }\n");
        ensure("a numeric for: " + r.text, has(r, "for i = 0, 9 do"));
        ensure("its counter declared by it alone: " + r.text, !has(r, "local i = 0"));
        ensure("continue, the step Luau's own: " + r.text, has(r, "then\n        continue"));
        ensure("break: " + r.text, has(r, "break"));
        ensure("repeat: " + r.text, has(r, "repeat") && has(r, "until not (n > 5)"));
        ensure("a step: " + r.text, has(r, "n -= 1"));
        ensure("another jump noted and kept as a mark: " + r.text, noted(r, "SluaJump") && has(r, "-- jump away") && has(r, "-- @away"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<4>()
    {
        set_test_name("states: each a table of handlers, entered by setState, a state change ending the event; the timer on LLTimers, calling the state's");
        const ALLSLToSLua::Result r = convert("default {\n"
                                              "    state_entry() { llSetTimerEvent(1.0); }\n"
                                              "    timer() { state running; }\n"
                                              "    state_exit() { llOwnerSay(\"leaving\"); }\n"
                                              "}\n"
                                              "state running {\n"
                                              "    state_entry() { llOwnerSay(\"running\"); }\n"
                                              "    touch_start(integer n) { if (n > 1) { state default; } llOwnerSay(\"touched\"); }\n"
                                              "}\n");
        ensure("the tables: " + r.text, has(r, "states.default = {") && has(r, "states.running = {"));
        ensure("setState: " + r.text, has(r, "local function setState(name: string)") && has(r, "setState(\"default\")"));
        ensure("a change ends the event: " + r.text, has(r, "setState(\"running\")\n        return") && has(r, "setState(\"default\")\n            return\n        end"));
        ensure("the timer on LLTimers: " + r.text, has(r, "setTimer(1") && has(r, "LLTimers:every(seconds") && noted(r, "SluaTimers"));
        ensure("calling the state's handler: " + r.text, has(r, "states[currentState].timer") && has(r, "event ~= \"timer\""));
        ensure("state_exit a handler of its state: " + r.text, has(r, "state_exit = function()"));
        ensure("what is let go of noted: " + r.text, noted(r, "SluaStates"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<5>()
    {
        set_test_name("names: one Luau or SLua holds renamed, functions declared first where one calls another after it; assignments inside expressions made in place");
        const ALLSLToSLua::Result r = convert("integer table = 1;\n"
                                              "integer first(integer end) { return second(end) + table; }\n"
                                              "integer second(integer x) { return x * 2; }\n"
                                              "default { state_entry() {\n"
                                              "    integer i; integer j = (i = 4) + i++;\n"
                                              "    llOwnerSay((string)first(j));\n"
                                              "} }\n");
        ensure("renamed: " + r.text, has(r, "local table_ = 1") && has(r, "(end_)"));
        ensure("declared first: " + r.text, has(r, "local first, second\n") && has(r, "function first(end_)"));
        ensure("an assignment in place: " + r.text, has(r, "(function() i = 4 return i end)()") && noted(r, "SluaAssignInExpression"));
        ensure("a step after, what it was: " + r.text, has(r, "(function() local was = i; i += 1 return was end)()"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<6>()
    {
        set_test_name("LSL that does not parse writes nothing, and says why");
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("default { state_entry() { integer = ; } }");
        ensure("not converted", !r.converted && r.text.empty());
        ensure("why", !r.problems.empty());
    }

    template<> template<>
    void allsltoslua_object::test<7>()
    {
        set_test_name("close to LSL: llcompat's Detected functions and timer event, as LSL had them, noted with SLua's ways");
        const ALLSLToSLua::Options close = ALLSLToSLua::Options::closeToLSL();
        const ALLSLToSLua::Result  r     = ALLSLToSLua::convert("default {\n"
                                                                "    state_entry() { llSetTimerEvent(2.0); }\n"
                                                                "    timer() { llOwnerSay(\"tick\"); }\n"
                                                                "    touch_start(integer n) { llOwnerSay(llDetectedName(0)); }\n"
                                                                "}\n",
                                                                close);
        ensure("converted", r.converted);
        ensure("llcompat's Detected: " + r.text, has(r, "llcompat.DetectedName(0)") && noted(r, "SluaDetected"));
        ensure("the timer event: " + r.text, has(r, "llcompat.SetTimerEvent(2") && has(r, "LLEvents:on(\"timer\"") && noted(r, "SluaTimer"));
        ensure("ll.OwnerSay, with print noted: " + r.text, has(r, "ll.OwnerSay(\"tick\")") && noted(r, "SluaUsellOwnerSay"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<8>()
    {
        set_test_name("SLua's ll where it means the same -- a boolean, a constant index moved on, a find against nil -- and what SLua has in a call's stead");
        const ALLSLToSLua::Result r = convert("default { touch_start(integer n) {\n"
                                              "    key k = llDetectedKey(0); string s = \"abcdef\"; list l = [\"a\", \"b\"]; integer i = 2;\n"
                                              "    if (llSameGroup(k)) llOwnerSay(\"group\");\n"
                                              "    integer same = llSameGroup(k);\n"
                                              "    string head = llGetSubString(s, 0, 2);\n"
                                              "    string tail = llGetSubString(s, i, -1);\n"
                                              "    if (llListFindList(l, [\"a\"]) != -1) llOwnerSay(\"found\");\n"
                                              "    if (~llSubStringIndex(s, \"c\")) llOwnerSay(\"has c\");\n"
                                              "    if (llSubStringIndex(s, \"z\") < 0) llOwnerSay(\"no z\");\n"
                                              "    integer at = llListFindList(l, [\"a\", \"b\"]);\n"
                                              "    float p = llPow(2.0, 3.0) + llFabs(-1.0) + llVecDist(<1,2,3>, ZERO_VECTOR);\n"
                                              "    integer r = llRound(2.5) + llGetUnixTime();\n"
                                              "    llOwnerSay(head + tail + (string)same + (string)at + (string)p + (string)r);\n"
                                              "} }\n");
        ensure("a boolean in a condition: " + r.text, has(r, "if ll.SameGroup(k) then"));
        ensure("and as a number: " + r.text, has(r, "local same = if ll.SameGroup(k) then 1 else 0"));
        ensure("a constant index moved on: " + r.text, has(r, "ll.GetSubString(s, 1, 3)"));
        ensure("one that is not, through llcompat: " + r.text, has(r, "llcompat.GetSubString(s, i, -1)") && noted(r, "SluaIndexllGetSubString"));
        ensure("one thing found in a list: " + r.text, has(r, "if table.find(l, \"a\") ~= nil then"));
        ensure("~ of a find: " + r.text, has(r, "if ll.SubStringIndex(s, \"c\") ~= nil then"));
        ensure("< 0 of one: " + r.text, has(r, "if ll.SubStringIndex(s, \"z\") == nil then"));
        ensure("a find's index as a number, through llcompat: " + r.text, has(r, "llcompat.ListFindList(l, {\"a\", \"b\"})"));
        ensure("^, math and vector: " + r.text, has(r, "2.0 ^ 3.0") && has(r, "math.abs(-1.0)") && has(r, "vector.magnitude(vector(1, 2, 3) - ZERO_VECTOR)"));
        ensure("not math.round for llRound: " + r.text, has(r, "ll.Round(2.5)"));
        ensure("os.time and print: " + r.text, has(r, "os.time()") && has(r, "print("));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<9>()
    {
        set_test_name("handlers assigned to LLEvents' fields, and Luau types on what LSL typed");
        ALLSLToSLua::Options options;
        options.handlers = ALLSLToSLua::Options::Handlers::Field;
        options.types    = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("integer gCount = 2;\n"
                                                           "string greet(string who) { return \"Hi \" + who; }\n"
                                                           "default { touch_start(integer n) { string s = greet(\"x\"); gCount += n; llOwnerSay(s); } }\n",
                                                           options);
        ensure("converted", r.converted);
        ensure("a field: " + r.text, has(r, "LLEvents.touch_start = function(detected)") && !has(r, "LLEvents:on"));
        ensure("typed: " + r.text, has(r, "local gCount: number = 2") && has(r, "local function greet(who: string): string") &&
                                       has(r, "local s: string = greet(\"x\")"));
        checksClean(r);

        const ALLSLToSLua::Result many = ALLSLToSLua::convert("default { touch_start(integer n) { state other; } }\n"
                                                              "state other { touch_start(integer n) { state default; } }\n",
                                                              options);
        ensure("setState sets the fields: " + many.text, has(many, "(LLEvents :: any)[event] = handler") && has(many, "(LLEvents :: any)[event] = nil"));
        checksClean(many);
    }

    template<> template<>
    void allsltoslua_object::test<10>()
    {
        set_test_name("no comments where they are not wanted: the notes are still said");
        ALLSLToSLua::Options options;
        options.comments = false;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("default { state_entry() { integer a = 7 / 2; llOwnerSay((string)a); } }\n", options);
        ensure("converted", r.converted);
        ensure("no comment: " + r.text, !has(r, "-- LSL:"));
        ensure("but noted", noted(r, "SluaIntegerDivision"));
    }

    template<> template<>
    void allsltoslua_object::test<11>()
    {
        set_test_name("numeric for where it counts as LSL's did: up, down, by a step, to a length; a while where the counter or the limit could change, or the counter is read after");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    list l = [1, 2, 3]; integer n = 4; integer total;\n"
                                              "    integer a; for (a = 0; a < llGetListLength(l); a++) total += llList2Integer(l, a);\n"
                                              "    integer b; for (b = 10; b >= 0; b -= 2) total += b;\n"
                                              "    integer c; for (c = 1; c <= n; ++c) total += c;\n"
                                              "    integer d; for (d = 0; d < n; ++d) { if (total > 100) d = n; }\n"
                                              "    integer e; for (e = 0; e < n; ++e) { n--; }\n"
                                              "    integer f; for (f = 0; f < 3; ++f) total += f;\n"
                                              "    llOwnerSay((string)(total + f));\n"
                                              "} }\n");
        ensure("up to a length: " + r.text, has(r, "for a = 0, #l - 1 do"));
        ensure("down by a step: " + r.text, has(r, "for b = 10, 0, -2 do"));
        ensure("up to and with: " + r.text, has(r, "for c = 1, n do"));
        ensure("a counter set in its body keeps the while: " + r.text, has(r, "while d < n do"));
        ensure("a limit set in its body keeps the while: " + r.text, has(r, "while e < n do"));
        ensure("a counter read after keeps the while: " + r.text, has(r, "while f < 3 do") && has(r, "local f = 0"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<12>()
    {
        set_test_name("integers only ever truth values are booleans, and functions that answer one; each use that reads a number keeps it one");
        const ALLSLToSLua::Result r = convert("integer gOn = FALSE;\n"
                                              "integer gSeen;\n"
                                              "integer isOwner(key k) { return k == llGetOwner(); }\n"
                                              "integer anyOf(list l) { return llGetListLength(l); }\n"
                                              "default { touch_start(integer n) {\n"
                                              "    gOn = !gOn;\n"
                                              "    if (gOn) llOwnerSay(\"on\"); else llOwnerSay(\"off\");\n"
                                              "    if (gSeen == FALSE && isOwner(llDetectedKey(0))) gSeen = TRUE;\n"
                                              "    integer busy = anyOf([1]);\n"
                                              "    while (!busy) busy = TRUE;\n"
                                              "    integer count; count++;\n"
                                              "    integer shown = TRUE; llOwnerSay((string)shown);\n"
                                              "    integer loose = 5; if (loose == TRUE) llOwnerSay(\"one\");\n"
                                              "    integer y; integer z = (y = 5); if (y) llOwnerSay((string)z);\n"
                                              "    if (count) llOwnerSay((string)count);\n"
                                              "} }\n");
        ensure("a global toggled: " + r.text, has(r, "local gOn = false") && has(r, "gOn = not gOn") && has(r, "if gOn then"));
        ensure("compared with FALSE: " + r.text, has(r, "local gSeen = false") && has(r, "if not gSeen and isOwner(") && has(r, "gSeen = true"));
        ensure("a function that answers one: " + r.text, has(r, "return k == ll.GetOwner()"));
        ensure("a length, as a truth: " + r.text, has(r, "return #l ~= 0") && has(r, "local busy = anyOf({1})") && has(r, "while not busy do"));
        ensure("stepped: " + r.text, has(r, "local count = 0") && has(r, "count += 1"));
        ensure("printed: " + r.text, has(r, "local shown = 1"));
        ensure("more than TRUE, compared with it: " + r.text, has(r, "local loose = 5") && has(r, "if loose == 1 then"));
        ensure("set where the assignment is read: " + r.text, has(r, "local y = 0"));
        checksClean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("integer ready(integer n) { return n > 2; }\n"
                                                           "default { state_entry() { integer ok = ready(3); if (ok) llOwnerSay(\"yes\"); } }\n",
                                                           typed);
        ensure("typed boolean: " + t.text, has(t, "local function ready(n: number): boolean") && has(t, "local ok: boolean = ready(3)"));
        checksClean(t);
    }

    template<> template<>
    void allsltoslua_object::test<13>()
    {
        set_test_name("LSL ran a binary's right side first: (l = []) + l is the append it meant, and where the order could show it is noted");
        const ALLSLToSLua::Result r = convert("list gItems;\n"
                                              "string gLog = \"a\";\n"
                                              "default { touch_start(integer n) {\n"
                                              "    gItems = (gItems = []) + gItems + [\"x\"];\n"
                                              "    gItems = (gItems = []) + [\"y\"] + gItems;\n"
                                              "    gLog = (gLog = \"\") + gLog + \"b\";\n"
                                              "    llOwnerSay(gLog + (string)llGetListLength(gItems));\n"
                                              "} }\n");
        ensure("appended: " + r.text, has(r, "table.insert(gItems, \"x\")"));
        ensure("prepended: " + r.text, has(r, "table.insert(gItems, 1, \"y\")"));
        ensure("a string: " + r.text, has(r, "gLog ..= \"b\""));
        ensure("nothing cleared first: " + r.text, !has(r, "gItems = {} return") && !has(r, "gLog = \"\" return"));
        ensure("noted once for each variable: " + r.text, noted(r, "SluaMemoryHack") && count(r, "-- LSL: (gItems = []) + gItems") == 1 &&
                                                         count(r, "-- LSL: (gLog = \"\") + gLog") == 1);
        ensure("not taken as an order that shows", !noted(r, "SluaRightFirst"));
        checksClean(r);

        const ALLSLToSLua::Result order = convert("integer gCount;\n"
                                                  "integer bump() { return ++gCount; }\n"
                                                  "default { state_entry() {\n"
                                                  "    integer i = 1;\n"
                                                  "    integer j = i + (i = 5);\n"
                                                  "    integer k = gCount - bump();\n"
                                                  "    llOwnerSay((string)(j + k));\n"
                                                  "} }\n");
        ensure("an assignment the other side reads", noted(order, "SluaRightFirst"));
        ensure("said over its line: " + order.text, has(order, "-- LSL: LSL ran the right side of this before the left"));
        checksClean(order);

        const ALLSLToSLua::Result quiet = convert("integer gCount;\n"
                                                  "integer bump() { return ++gCount; }\n"
                                                  "default { state_entry() {\n"
                                                  "    integer i = 1;\n"
                                                  "    integer j = (i + 1) * bump();\n"
                                                  "    string s = \"n: \" + (string)bump();\n"
                                                  "    llOwnerSay(s + (string)(j + (integer)llGetTime()));\n"
                                                  "} }\n");
        ensure("nothing either side changes the other reads: " + quiet.text, !noted(quiet, "SluaRightFirst"));
        checksClean(quiet);
    }

    template<> template<>
    void allsltoslua_object::test<14>()
    {
        set_test_name("lists no other holds grown in place: appended, prepended, by a list's name, by a call, several at once, passed to a function that only reads it; a list handed on keeps LSL's copy, and says why");
        const ALLSLToSLua::Result r = convert("list gAll;\n"
                                              "list gSeen;\n"
                                              "list gPassed;\n"
                                              "list gStored;\n"
                                              "list gGrown;\n"
                                              "integer addOne() { gGrown += [1]; return 1; }\n"
                                              "list gKept = [1];\n"
                                              "list parts(string s) { list out = llParseString2List(s, [\",\"], []); out += [\"end\"]; return out; }\n"
                                              "list kept() { return gKept; }\n"
                                              "show(list l) { llOwnerSay(llDumpList2String(l, \",\")); }\n"
                                              "showAll(list l) { show(l); }\n"
                                              "keep(list l) { gStored = l; }\n"
                                              "keepLater(list l) { keep(l); }\n"
                                              "default { touch_start(integer n) {\n"
                                              "    gAll = parts(\"a,b\");\n"
                                              "    gAll += parts(\"c\");\n"
                                              "    gAll = [\"first\"] + gAll;\n"
                                              "    list more = [4, 5];\n"
                                              "    gAll += more;\n"
                                              "    gAll += [llGetTime(), llFrand(1.0)];\n"
                                              "    gSeen += [n];\n"
                                              "    showAll(gSeen);\n"
                                              "    gPassed += [n];\n"
                                              "    keepLater(gPassed);\n"
                                              "    gKept += [2];\n"
                                              "    list a = [1]; list b = a; a += [2];\n"
                                              "    show(llListInsertList(gGrown, [addOne()], 0));\n"
                                              "    show(kept() + b + a + gAll + gStored);\n"
                                              "} }\n");
        ensure("a local returned, grown in place: " + r.text, has(r, "local out = ll.ParseString2List(s, {\",\"}, {})") && has(r, "table.insert(out, \"end\")"));
        ensure("a call's list, each put in: " + r.text, has(r, "gAll = parts(\"a,b\")") && has(r, "for _, item in parts(\"c\") do") &&
                                                         has(r, "    table.insert(gAll, item)"));
        ensure("prepended: " + r.text, has(r, "table.insert(gAll, 1, \"first\")"));
        ensure("a list by its name: " + r.text, has(r, "table.move(more, 1, #more, #gAll + 1, gAll)"));
        ensure("several that do not run apart: " + r.text, has(r, "table.move({ll.GetTime(), ll.Frand(1.0)}, 1, 2, #gAll + 1, gAll)"));
        ensure("passed to a function that only reads it: " + r.text, has(r, "table.insert(gSeen, n)"));
        ensure("passed to one that keeps it: " + r.text,
               has(r, "gPassed = joinLists(gPassed, {n})") &&
                   has(r, "-- LSL: LSL's lists were values, and gPassed is passed to a function of the script's that keeps it"));
        ensure("returned: " + r.text, has(r, "gKept = joinLists(gKept, {2})") && has(r, "gKept is returned by a function"));
        ensure("given to another: " + r.text, has(r, "a = joinLists(a, {2})") && has(r, "a is given to another variable"));
        ensure("read where a call in the statement grows it: " + r.text,
               has(r, "gGrown = joinLists(gGrown, {1})") && has(r, "gGrown is read where a call of the script's in the same statement changes it"));
        ensure("said once for each: " + r.text, count(r, "and gPassed is") == 1);
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<15>()
    {
        set_test_name("strings built in loops: their pieces in a table, joined once after the loop that builds them; noted where they cannot be");
        const ALLSLToSLua::Result r = convert("string gReport;\n"
                                              "default { touch_start(integer n) {\n"
                                              "    list names = [\"a\", \"b\", \"c\"];\n"
                                              "    integer outParts = 1;\n"
                                              "    string out = \"Names: \";\n"
                                              "    integer i;\n"
                                              "    for (i = 0; i < llGetListLength(names); ++i) {\n"
                                              "        string row = \"\";\n"
                                              "        integer j;\n"
                                              "        for (j = 0; j < 3; ++j) row += (string)j;\n"
                                              "        out = out + llList2String(names, i) + row;\n"
                                              "    }\n"
                                              "    string seen;\n"
                                              "    integer k;\n"
                                              "    for (k = 0; k < 3; ++k) { seen += \"x\"; if (llStringLength(seen) > 2) llOwnerSay(seen); }\n"
                                              "    for (k = 0; k < 2; ++k) gReport += \"y\";\n"
                                              "    llOwnerSay(out + seen + gReport + (string)outParts);\n"
                                              "} }\n");
        ensure("the outer loop's table, a name of its own: " + r.text, has(r, "local outParts2 = {}\n    for i = 0, #names - 1 do"));
        ensure("the pieces put in: " + r.text, has(r, "table.insert(outParts2, ") && has(r, " .. row)"));
        ensure("joined after: " + r.text, has(r, "    end\n    out ..= table.concat(outParts2)"));
        ensure("the inner loop's, declared in the outer: " + r.text,
               has(r, "        local rowParts = {}\n        for j = 0, 2 do\n            table.insert(rowParts, tostring(j))") &&
                   has(r, "        row ..= table.concat(rowParts)"));
        ensure("read in its loop: noted: " + r.text, has(r, "seen ..= \"x\"") && has(r, "-- LSL: seen is built with .. in a loop"));
        ensure("a global: noted: " + r.text, has(r, "gReport ..= \"y\"") && has(r, "-- LSL: gReport is built with .. in a loop"));
        checksClean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("default { state_entry() { string s; integer i; for (i = 0; i < 3; ++i) s += \"z\"; llOwnerSay(s); } }\n",
                                                           typed);
        ensure("typed: " + t.text, has(t, "local sParts: { string } = {}"));
        checksClean(t);
    }

    template<> template<>
    void allsltoslua_object::test<16>()
    {
        set_test_name("LSL's /= and %=, and x = x op y, as Luau's own compound assignments, noted as / and % are; a vector's %= its cross product");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    integer a = 7; float f = 3.0; vector v = <1, 0, 0>;\n"
                                              "    a /= 2; a %= 3; f /= 2.0; v %= <0, 1, 0>;\n"
                                              "    integer b = 1; integer c = 2; string s = \"x\";\n"
                                              "    b = b + c; b = b - (c - 1); b = b - c - 1; c = c * 2 + b; s = s + \"y\" + (string)b;\n"
                                              "    llOwnerSay((string)a + (string)f + (string)v + s + (string)(b + c));\n"
                                              "} }\n");
        ensure("integer division: " + r.text, has(r, "a //= 2") && noted(r, "SluaIntegerDivision"));
        ensure("remainder: " + r.text, has(r, "a %= 3") && noted(r, "SluaModulo"));
        ensure("a float's: " + r.text, has(r, "f /= 2.0"));
        ensure("a vector's cross product: " + r.text, has(r, "v = vector.cross(v, vector(0, 1, 0))"));
        ensure("x = x op y: " + r.text, has(r, "b += c") && has(r, "b -= c - 1"));
        ensure("not where the left of the operator is more than x: " + r.text, has(r, "b = b - c - 1") && has(r, "c = c * 2 + b"));
        ensure("a string's pieces, joined: " + r.text, has(r, "s ..= \"y\" .. tostring(b)"));
        checksClean(r);
    }
}
