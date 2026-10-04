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

#include "../lsl/allsltoslua.h"
#include "../lint/alscriptfixes.h"
#include "../lsl/allslservice.h"
#include "../luau/alluauservice.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <set>
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

        // What the converter must not write: an error, what the studio's own
        // lints would have written SLua's way, or a deprecated ll function --
        // SLua's way where it means the same, llcompat's where not.
        static bool unwanted(const ALScriptProblem& p)
        {
            static const std::vector<std::string_view> OWN = { "SlCompoundAssign", "SlNumberTruth",    "SlZeroIndex",    "SlParenCondition",
                                                               "SlGeneralizedFor", "SlIndexDivision", "SlVectorProduct" };
            return p.severity == ALScriptProblem::Severity::Error || std::find(OWN.begin(), OWN.end(), p.code) != OWN.end() ||
                   (p.key.rfind("LuauLintDeprecatedMember", 0) == 0 && !p.args.empty() && p.args[0].rfind("ll.", 0) == 0);
        }

        // What SLua's check says of it: nothing unwanted.
        void checksClean(const ALLSLToSLua::Result& r)
        {
            ensure("the definitions", definitions);
            std::string said;
            for (const ALScriptProblem& p : service.check(r.text))
            {
                if (unwanted(p))
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
        // A note of the key, and where one is given, whose first arg is it:
        // the function a note of a call is about.
        static bool noted(const ALLSLToSLua::Result& r, const std::string& key, const std::string& about = {})
        {
            for (const ALScriptProblem& p : r.notes)
            {
                if (p.key == key && (about.empty() || (!p.args.empty() && p.args[0] == about)))
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
        ensure("an integer joined to text as it is, which .. writes as LSL did: " + r.text, has(r, "greet(detected[1]:getName()) .. \" \" .. gCount)"));
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
        ensure("a list no other holds grown in place: " + r.text, has(r, "table.insert(l, 3)") && has(r, "table.append(l, 4, \"five\")") &&
                                                                    !has(r, "joinLists"));
        ensure("TRUE and FALSE: " + r.text, has(r, "local t = 1") && has(r, "if false then"));
        ensure("a float as LSL writes it: " + r.text, has(r, "string.format(\"%.6f\", f)"));
        ensure("a string as an integer, as a list's item converts it: " + r.text,
               has(r, "lslInteger(\"12abc\")") && has(r, "return llcompat.List2Integer({ s }, 0)"));
        ensure("lists compared by length: " + r.text, has(r, "#l == #{1}") && noted(r, "SluaListCompare"));
        // bit32 answers 0 to 4294967295: made LSL's signed integer, but where
        // bit32 takes it again.
        ensure("bits: " + r.text, has(r, "bit32.s32(bit32.bor(bit32.band(a, b), 4))") && !has(r, "local function int32"));
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
        ensure("continue, the step Luau's own: " + r.text, has(r, "then continue end"));
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
        ensure("a change ends the event, where anything follows: " + r.text,
               has(r, "setState(\"running\")\n    end,") && has(r, "setState(\"default\")\n            return\n        end"));
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
        ensure("only the one called before it is written declared first: " + r.text,
               has(r, "local second\n") && has(r, "local function first(end_)") && has(r, "\nfunction second(x)"));
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
        ensure("llcompat.OwnerSay, SLua deprecating ll's for print: " + r.text,
               has(r, "llcompat.OwnerSay(\"tick\")") && noted(r, "SluaDeprecatedFor", "OwnerSay"));
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
        ensure("one that is not, through llcompat: " + r.text, has(r, "llcompat.GetSubString(s, i, -1)") && noted(r, "SluaIndex", "GetSubString"));
        ensure("one thing found in a list: " + r.text, has(r, "if table.find(l, \"a\") ~= nil then"));
        ensure("~ of a find: " + r.text, has(r, "if ll.SubStringIndex(s, \"c\") ~= nil then"));
        ensure("< 0 of one: " + r.text, has(r, "if ll.SubStringIndex(s, \"z\") == nil then"));
        ensure("a find's index as a number, through llcompat: " + r.text, has(r, "llcompat.ListFindList(l, {\"a\", \"b\"})"));
        ensure("^, math and vector: " + r.text, has(r, "2.0 ^ 3.0") && has(r, "math.abs(-1.0)") && has(r, "vector.magnitude(vector(1, 2, 3) - ZERO_VECTOR)"));
        ensure("half up, as LSL rounds, not math.round: " + r.text, has(r, "math.floor(2.5 + 0.5)"));
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
        // SLua stops the script at a field assigned nil: taken off by
        // LLEvents:off, as LLEvents:on's are.
        ensure("setState sets the fields: " + many.text, has(many, "(LLEvents :: any)[event] = handler") &&
                                                         has(many, "LLEvents:off(event :: any, handler)") && !has(many, "[event] = nil"));
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
        ensure("up to a length, walking its items: " + r.text, has(r, "for _, item in l do\n    total += item"));
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
        ensure("several that do not run apart, each had before any is added: " + r.text, has(r, "table.append(gAll, ll.GetTime(), ll.Frand(1.0))"));
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
        ensure("the outer loop's table, a name of its own: " + r.text, has(r, "local outParts2 = {}\n    for _, item in names do"));
        ensure("the pieces put in: " + r.text, has(r, "table.insert(outParts2, item .. row)"));
        ensure("joined after: " + r.text, has(r, "    end\n    out ..= table.concat(outParts2)"));
        ensure("the inner loop's, declared in the outer: " + r.text,
               has(r, "        local rowParts = {}\n        for j = 0, 2 do\n            table.insert(rowParts, tostring(j))"));
        ensure("declared empty just before it: declared where it is joined: " + r.text,
               has(r, "        end\n        local row = table.concat(rowParts)") && !has(r, "local row = \"\""));
        ensure("declared with words of its own: added to: " + r.text, has(r, "local out = \"Names: \"") && has(r, "out ..= table.concat(outParts2)"));
        ensure("read in its loop: noted: " + r.text, has(r, "seen ..= \"x\"") && has(r, "-- LSL: seen is built with .. in a loop"));
        ensure("a global: noted: " + r.text, has(r, "gReport ..= \"y\"") && has(r, "-- LSL: gReport is built with .. in a loop"));
        checksClean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("default { state_entry() { string s; integer i; for (i = 0; i < 3; ++i) s += \"z\"; llOwnerSay(s); } }\n",
                                                           typed);
        ensure("typed: " + t.text, has(t, "local sParts: { string } = {}") && has(t, "local s: string = table.concat(sParts)"));
        checksClean(t);

        const ALLSLToSLua::Result read = convert("default { state_entry() { string t = \"\"; llOwnerSay(t); integer i; for (i = 0; i < 3; ++i) t += \"q\"; llOwnerSay(t); } }\n");
        ensure("read between: declared where it was: " + read.text, has(read, "local t = \"\"") && has(read, "t ..= table.concat(tParts)"));
        checksClean(read);
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
        ensure("a string's pieces, joined: " + r.text, has(r, "s ..= \"y\" .. b"));
        ensure("an integer's sum joined bracketed, a float's and a vector's as LSL writes them: " + r.text,
               has(r, "print(a .. string.format(\"%.6f\", f) .. ll.DumpList2String({v}, \"\") .. s .. (b + c))"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<17>()
    {
        set_test_name("a minus before a negative number is bracketed, not a comment");
        const ALLSLToSLua::Result r = convert("default { state_entry() { integer a = -5; llOwnerSay((string)(-(-2147483648)) + (string)(-a) + (string)(- -a)); } }\n");
        ensure("bracketed: " + r.text, has(r, "-(-2147483648)") && !has(r, "--2147483648"));
        ensure("a name as it was: " + r.text, has(r, " .. -a .. "));
        ensure("twice: " + r.text, has(r, "-(-a)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<18>()
    {
        set_test_name("a script of one state: its state_exit, which never ran and SLua has no event for, left out and said");
        const ALLSLToSLua::Result r = convert("default { state_entry() { llOwnerSay(\"in\"); } state_exit() { llOwnerSay(\"out\"); } touch_start(integer n) { } }\n");
        ensure("not an event: " + r.text, !has(r, "\"state_exit\"") && !has(r, "print(\"out\")"));
        ensure("said where it was: " + r.text, noted(r, "SluaStateExit") && has(r, "-- LSL: state_exit runs as a script leaves a state") &&
                                                  has(r, "-- state_exit, left out"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<19>()
    {
        set_test_name("a function SLua has nowhere left out and said; one whose ll list has booleans for LSL's 1 and 0 kept on llcompat");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    llPointAt(ZERO_VECTOR);\n"
                                              "    list e = llGetExperienceList(NULL_KEY);\n"
                                              "    if (llList2Integer(llGetPrimitiveParams([PRIM_FULLBRIGHT, 0]), 0)) llOwnerSay((string)llGetListLength(e));\n"
                                              "} }\n");
        ensure("left out: " + r.text, !has(r, "PointAt(") && has(r, "-- llPointAt, left out") && noted(r, "SluaAbsent", "llPointAt"));
        ensure("its empty value where read: " + r.text, has(r, "local e = {}") && noted(r, "SluaAbsent", "llGetExperienceList"));
        ensure("llcompat, said: " + r.text, has(r, "llcompat.GetPrimitiveParams({PRIM_FULLBRIGHT, 0})") && noted(r, "SluaBoolList", "GetPrimitiveParams"));
        ensure("not a truth: " + r.text, !has(r, "if llcompat.GetPrimitiveParams"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<20>()
    {
        set_test_name("NULL_KEY and the constants LSL types a string and SLua a uuid: a key where compared with one or given as one, LSL's string where read as one");
        const ALLSLToSLua::Result r = convert("string gName = NULL_KEY;\n"
                                              "key gKey = NULL_KEY;\n"
                                              "string nothing() { return NULL_KEY; }\n"
                                              "default { touch_start(integer n) {\n"
                                              "    key k = llDetectedKey(0);\n"
                                              "    if (k != NULL_KEY) llOwnerSay(\"someone\");\n"
                                              "    if (NULL_KEY != \"\") llOwnerSay(llGetSubString(NULL_KEY, 0, 7));\n"
                                              "    list l = [NULL_KEY, TEXTURE_BLANK];\n"
                                              "    if (NULL_KEY) llOwnerSay(gName + nothing() + (string)gKey + (string)llGetListLength(l));\n"
                                              "} }\n");
        ensure("a key against a key: " + r.text, has(r, "if k ~= NULL_KEY then"));
        ensure("a string where LSL's string was: " + r.text, has(r, "tostring(NULL_KEY) ~= \"\"") && has(r, "ll.GetSubString(tostring(NULL_KEY), 1, 8)"));
        ensure("in a list, as LSL had it: " + r.text, has(r, "{tostring(NULL_KEY), tostring(TEXTURE_BLANK)}"));
        ensure("a string's truth: " + r.text, has(r, "if tostring(NULL_KEY) ~= \"\" then print("));
        ensure("given to a string and a key: " + r.text, has(r, "local gName = tostring(NULL_KEY)") && has(r, "local gKey = NULL_KEY"));
        ensure("returned as a string function's: " + r.text, has(r, "return tostring(NULL_KEY)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<21>()
    {
        set_test_name("Tailslide's own test scripts: every one it takes converts and checks as SLua, and each it refuses is refused");
        // Those Tailslide says are wrong LSL, by design.
        static const std::set<std::string> REFUSED = { "bad_globals.lsl", "bugs/0002.lsl", "bugs/0003.lsl", "bugs/0006.lsl", "bugs/0007.lsl",
                                                          "bugs/0010.lsl", "bugs/0015.lsl", "bugs/0017.lsl", "bugs/0019.lsl", "bugs/bad-for-exprs.lsl",
                                                          "bugs/typecast_builtin.lsl", "bugs/unary_minus_glob_bad_sym.lsl", "compound_assignment.lsl", "constants.lsl", "declaration_expressions.lsl",
                                                          "error1.lsl", "events.lsl", "illegal_cast.lsl", "invalid_decl.lsl", "list_append_void.lsl",
                                                          "logmessage_test.lsl", "nested_lists.lsl", "parserstackdepth3.lsl", "pathological_expression.lsl", "precluded_globals.lsl",
                                                          "print_no_shadowing.lsl", "print_type_bug.lsl", "rvalue_assignments.lsl", "scope1.lsl", "scope2.lsl",
                                                          "scope4.lsl", "type_error_no_assert.lsl", "void_return.lsl" };
        const std::string dir = std::string(AL_ALSCRIPT_TEST_DIR) + "/tailslide/scripts";
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.path().extension() == ".lsl")
            {
                files.push_back(std::filesystem::relative(entry.path(), dir).generic_string());
            }
        }
        std::sort(files.begin(), files.end());
        ensure("the scripts: " + dir, files.size() > 100);
        std::string wrong;
        size_t      converted = 0;
        for (const std::string& name : files)
        {
            llifstream        in(dir + "/" + name, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const ALLSLToSLua::Result r = ALLSLToSLua::convert(text.str());
            if (REFUSED.contains(name))
            {
                wrong += r.converted ? name + ": converted, and Tailslide refuses it\n" : "";
                continue;
            }
            if (!r.converted)
            {
                wrong += name + ": not converted: " + (r.problems.empty() ? std::string() : r.problems.front().message) + "\n";
                continue;
            }
            ++converted;
            for (const ALScriptProblem& p : service.check(r.text))
            {
                if (unwanted(p))
                {
                    wrong += llformat("%s: [%d:%d] %s\n", name.c_str(), p.line + 1, p.column + 1, p.message.c_str());
                    break;
                }
            }
        }
        ensure("every one as it should be:\n" + wrong, wrong.empty());
        ensure_equals("those taken, converted", converted + REFUSED.size(), files.size());
    }

    template<> template<>
    void allsltoslua_object::test<22>()
    {
        set_test_name("what SLua deprecates: its own way where it means the same -- math.floor(x + 0.5), utf8.len, an index read as LSL's -- and llcompat, with SLua's word, where not");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    string s = \"a:b\"; list l = [\"x\", 2];\n"
                                              "    integer at = llSubStringIndex(s, \":\");\n"
                                              "    integer n = llStringLength(s) + llRound(1.5);\n"
                                              "    llOwnerSay(llList2String(l, at) + (string)(at + n));\n"
                                              "} }\n");
        ensure("an index read as LSL's: " + r.text, has(r, "local at = (ll.SubStringIndex(s, \":\") or 0) - 1"));
        ensure("utf8.len and half up: " + r.text, has(r, "local n = (utf8.len(s) :: number) + math.floor(1.5 + 0.5)"));
        ensure("llcompat, with SLua's word: " + r.text,
               has(r, "llcompat.List2String(l, at)") && has(r, "-- LSL: SLua deprecates ll.List2String: Use '[]' and 'tostring' instead."));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<23>()
    {
        set_test_name("keys that hold text: link_message's id as SLua passes it, a string; a key given text that is no UUID kept a string; text where SLua takes it, uuid() where only a uuid will do");
        const ALLSLToSLua::Result r = convert("key DOMAIN = \"MY CHANNEL\";\n"
                                              "key gOwner;\n"
                                              "send(key to) { llMessageLinked(LINK_SET, 0, \"\", to); }\n"
                                              "default {\n"
                                              "    state_entry() {\n"
                                              "        gOwner = llGetOwner();\n"
                                              "        llMessageLinked(LINK_SET, 1, \"hi\", DOMAIN);\n"
                                              "        send(\"other text\");\n"
                                              "        llMessageLinked(LINK_SET, 2, \"x\", gOwner);\n"
                                              "    }\n"
                                              "    link_message(integer s, integer n, string m, key id) {\n"
                                              "        if (id == DOMAIN) llOwnerSay(m);\n"
                                              "        if (id == gOwner) llOwnerSay(\"owner\");\n"
                                              "        key k = id;\n"
                                              "        llOwnerSay((string)llGetOwnerKey(\"abc\") + (string)k);\n"
                                              "    }\n"
                                              "}\n");
        ensure("a key given text, a string: " + r.text, has(r, "local DOMAIN = \"MY CHANNEL\""));
        ensure("text where SLua takes it: " + r.text,
               has(r, "ll.MessageLinked(LINK_SET, 1, \"hi\", DOMAIN)") && has(r, "ll.MessageLinked(LINK_SET, 2, \"x\", gOwner)") &&
                   has(r, "send(\"other text\")") && has(r, "ll.MessageLinked(LINK_SET, 0, \"\", to)"));
        ensure("compared as text: " + r.text, has(r, "if id == DOMAIN then") && has(r, "if id == tostring(gOwner) then"));
        ensure("a key given the id, text: " + r.text, has(r, "local k = id") && has(r, " .. k)"));
        ensure("uuid() where only a uuid will do, said: " + r.text, has(r, "ll.GetOwnerKey(uuid(\"abc\"))") && noted(r, "SluaKeyText"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<24>()
    {
        set_test_name("a note only where it says something: && not over a read, a deprecated call's reason rather than its indexes; bit32's range made LSL's, not for an & with a flag nor a truth");
        const ALLSLToSLua::Result r = convert("integer sayIt() { llOwnerSay(\"x\"); return 1; }\n"
                                              "default { changed(integer what) {\n"
                                              "    if (what & CHANGED_LINK && llGetLinkNumber() == 0) llDie();\n"
                                              "    integer m = what | 0x80000000;\n"
                                              "    list l = [\"a\"];\n"
                                              "    llOwnerSay(llList2String(l, what) + (string)m);\n"
                                              "    if ((what & 3) && sayIt()) llDie();\n"
                                              "} }\n");
        ensure("bit32's range made LSL's where it shows: " + r.text, has(r, "local m = bit32.s32(bit32.bor(what, ") && count(r, "-- LSL: bit32") == 1);
        ensure("not for a flag's truth: " + r.text, has(r, "if bit32.btest(what, CHANGED_LINK)"));
        ensure("&& said once, over a call that does something: " + r.text,
               count(r, "leaves its right side unrun") == 1 && has(r, "LSL ran both sides, the right one first.\n    if bit32.btest(what, 3) and sayIt()"));
        ensure("the deprecated call's reason only: " + r.text, has(r, "-- LSL: SLua deprecates ll.List2String") && !has(r, "takes indexes from 0"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<25>()
    {
        set_test_name("flatter expressions: text joined in a chain without brackets, and an &, | or ^ of the same again one bit32 call");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    string b = \"b\"; integer n = 3; integer f = 5;\n"
                                              "    llOwnerSay(\"a\" + b + \"c\" + (string)n);\n"
                                              "    llSetTextureAnim(ANIM_ON | ROTATE | LOOP, ALL_SIDES, 0, 0, 0, TWO_PI, 1.0);\n"
                                              "    llOwnerSay((string)((f | 2) & n));\n"
                                              "} }\n");
        ensure("a chain: " + r.text, has(r, "print(\"a\" .. b .. \"c\" .. n)"));
        ensure("one call: " + r.text, has(r, "ll.SetTextureAnim(bit32.bor(ANIM_ON, ROTATE, LOOP), ALL_SIDES"));
        ensure("not across operators: " + r.text, has(r, "bit32.band(bit32.bor(f, 2), n)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<26>()
    {
        set_test_name("steps as statements: x++ after its statement, ++x and x = v before it, where nothing else in it reads x; kept in place where something could");
        const ALLSLToSLua::Result r = convert("integer gLine;\n"
                                              "integer bump() { return ++gLine; }\n"
                                              "default { state_entry() {\n"
                                              "    integer n = 0;\n"
                                              "    llSay(n++, \"a\");\n"
                                              "    integer m = ++n;\n"
                                              "    integer k = n++ + n;\n"
                                              "    llOwnerSay((string)(gLine++));\n"
                                              "    llOwnerSay((string)bump() + (string)(gLine++) + (string)(m + k));\n"
                                              "} }\n");
        ensure("after: " + r.text, has(r, "ll.Say(n, \"a\")\nn += 1\n"));
        ensure("before: " + r.text, has(r, "\nn += 1\nlocal m = n\n"));
        ensure("read again, kept: " + r.text, has(r, "local k = (function() local was = n; n += 1 return was end)() + n"));
        ensure("a global with nothing of the script's called: " + r.text, has(r, "print(tostring(gLine))\ngLine += 1\n"));
        ensure("a global beside a call of the script's, kept: " + r.text, has(r, "print(bump() .. (function() local was = gLine;"));
        ensure("a return's step before it: " + r.text, has(r, "    gLine += 1\n    return gLine\n"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<27>()
    {
        set_test_name("simpler conditions: !(a == b) as a ~= b, a boolean given TRUE or FALSE by an if as the check itself");
        const ALLSLToSLua::Result r = convert("integer gOn;\n"
                                              "default { touch_start(integer n) {\n"
                                              "    if (!(n == 2)) llOwnerSay(\"not two\");\n"
                                              "    if (!(n != 3)) llOwnerSay(\"three\");\n"
                                              "    if (n > 1) gOn = TRUE; else gOn = FALSE;\n"
                                              "    if (n > 5) { gOn = FALSE; } else { gOn = TRUE; }\n"
                                              "    if (gOn) llOwnerSay(\"on\");\n"
                                              "} }\n");
        ensure("flipped: " + r.text, has(r, "if n ~= 2 then") && has(r, "if n == 3 then"));
        const ALLSLToSLua::Result nots = convert("integer gOn;\n"
                                                 "default { touch_start(integer n) {\n"
                                                 "    if (!n) llOwnerSay(\"none\"); if (!(n & 4)) llOwnerSay(\"no four\"); if (!(n > 1)) llDie();\n"
                                                 "    gOn = !gOn; if (gOn) llOwnerSay(\"on\");\n"
                                                 "} }\n");
        ensure("not a number: the other way round: " + nots.text,
               has(nots, "if n == 0 then") && has(nots, "if not bit32.btest(n, 4) then") && has(nots, "if not (n > 1) then") &&
                   has(nots, "gOn = not gOn"));
        checksClean(nots);
        ensure("the check itself: " + r.text, has(r, "    gOn = n > 1\n") && has(r, "    gOn = not (n > 5)\n"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<28>()
    {
        set_test_name("a counter never below nought is an index ll takes moved on by one, and a loop whose counter is only such an index counts from 1");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    integer n = llGetInventoryNumber(INVENTORY_SOUND);\n"
                                              "    integer i; for (i = 0; i < n; ++i) llOwnerSay(llGetInventoryName(INVENTORY_SOUND, i));\n"
                                              "    integer j; for (j = 0; j < n; ++j) llOwnerSay((string)j + llGetInventoryName(INVENTORY_SOUND, j));\n"
                                              "    integer k; for (k = n - 1; k >= 0; --k) llOwnerSay(llGetInventoryName(INVENTORY_SOUND, k));\n"
                                              "    string s = \"abcdef\";\n"
                                              "    integer c; for (c = 0; c <= 2; ++c) llOwnerSay(llGetSubString(s, c, c + 1));\n"
                                              "} }\n");
        ensure("counting from 1: " + r.text, has(r, "for i = 1, n do\n    print(ll.GetInventoryName(INVENTORY_SOUND, i))"));
        ensure("read as a number too, moved on by one: " + r.text,
               has(r, "for j = 0, n - 1 do\n    print(j .. ll.GetInventoryName(INVENTORY_SOUND, j + 1))"));
        ensure("down to nought: " + r.text, has(r, "for k = n - 1, 0, -1 do\n    print(ll.GetInventoryName(INVENTORY_SOUND, k + 1))"));
        ensure("to and with, a number added: " + r.text, has(r, "for c = 1, 3 do\n    print(ll.GetSubString(s, c, c + 1))"));
        ensure("no llcompat: " + r.text, !has(r, "llcompat."));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<29>()
    {
        set_test_name("a list's items read by index where they are already what is asked for: within its length as they are, else LSL's empty value past the end; tostring where that makes them so; llcompat where nothing does");
        const ALLSLToSLua::Result r = convert("list gNames = [\"a\", \"b\"];\n"
                                              "list parts(string s) { return llParseString2List(s, [\",\"], []); }\n"
                                              "default { state_entry() {\n"
                                              "    list p = parts(\"x,y\");\n"
                                              "    list mixed = [1, \"two\", 3.0];\n"
                                              "    list keys = [llGetOwner(), \"text\"];\n"
                                              "    integer n = 1;\n"
                                              "    llOwnerSay(llList2String(gNames, 0) + llList2String(p, -1) + llList2String(keys, 1));\n"
                                              "    llOwnerSay(llList2String(mixed, 2) + (string)llList2Integer(mixed, 0) + llList2String(gNames, n));\n"
                                              "    integer i; for (i = 0; i < llGetListLength(p); ++i) llOwnerSay(llList2String(p, i) + llGetSubString(\"abc\", i, i));\n"
                                              "} }\n");
        ensure("from the start, empty past the end: " + r.text, has(r, "(gNames[1] or \"\")"));
        ensure("back from the end, through a function's list: " + r.text, has(r, "(p[#p] or \"\")"));
        ensure("keys and text made text: " + r.text, has(r, "tostring(keys[2] or \"\")"));
        ensure("a float's text is not tostring's, and an index not known: llcompat: " + r.text,
               has(r, "llcompat.List2String(mixed, 2)") && has(r, "llcompat.List2String(gNames, n)"));
        ensure("mixed items for a number: llcompat: " + r.text, has(r, "llcompat.List2Integer(mixed, 0)"));
        ensure("within its length, as it is, counting from 1: " + r.text, has(r, "for i = 1, #p do\n    print(p[i] .. ll.GetSubString(\"abc\", i, i))"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<30>()
    {
        set_test_name("a list no other holds taken from or put to at its front or back in place: table.remove and table.insert; anywhere else, or one held elsewhere, as LSL made a new one");
        const ALLSLToSLua::Result r = convert("list gQueue;\n"
                                              "list gShared;\n"
                                              "keep(list l) { gShared = l; }\n"
                                              "default { touch_start(integer n) {\n"
                                              "    gQueue += [n];\n"
                                              "    gQueue = llListInsertList(gQueue, [n + 1], 0);\n"
                                              "    gQueue = llDeleteSubList(gQueue, 0, 0);\n"
                                              "    gQueue = llDeleteSubList(gQueue, -1, -1);\n"
                                              "    gQueue = llDeleteSubList(gQueue, 1, 1);\n"
                                              "    keep(gShared);\n"
                                              "    gShared = llDeleteSubList(gShared, 0, 0);\n"
                                              "} }\n");
        ensure("in place: " + r.text, has(r, "table.insert(gQueue, 1, n + 1)") && has(r, "table.remove(gQueue, 1)") && has(r, "table.remove(gQueue)\n"));
        ensure("elsewhere in the list, as LSL did: " + r.text, has(r, "gQueue = llcompat.DeleteSubList(gQueue, 1, 1)"));
        ensure("one held elsewhere, as LSL did: " + r.text, has(r, "gShared = llcompat.DeleteSubList(gShared, 0, 0)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<31>()
    {
        set_test_name("each note is keyed, with the words it was said with, and carries the lint that finds the same in SLua where one "
                      "does; the studio's words for a note are its comment's, and the header's");
        const std::string lsl = "default {\n"
                                "    touch_start(integer n) {\n"
                                "        string s = \"abc\";\n"
                                "        integer i = llSubStringIndex(s, \"b\");\n"
                                "        integer j = n / 2;\n"
                                "        llOwnerSay(llGetSubString(s, i, j) + llGetSubString(s, j, i) + (string)llSameGroup(llGetOwner()));\n"
                                "    }\n"
                                "}\n";
        ALLSLToSLua::Options close = ALLSLToSLua::Options::closeToLSL();
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, close);
        ensure("converted", r.converted);
        std::string listed;
        for (const ALScriptProblem& p : r.notes)
        {
            listed += p.key + "(" + (p.args.empty() ? std::string() : p.args[0]) + ") " + p.code + "\n";
        }
        // Once for each function, with the lint where there is one.
        ensure("an index: " + listed, noted(r, "SluaIndex", "GetSubString"));
        size_t indexes = 0;
        for (const ALScriptProblem& p : r.notes)
        {
            indexes += p.key == "SluaIndex";
            const bool compat = p.key == "SluaIndex" || p.key == "SluaIndexFound" || p.key == "SluaBool";
            ensure("the lint of " + p.key + ": " + listed, compat ? p.code == "SlCompatCall" : p.code.empty());
            ensure_equals("no mark left unfilled", p.message, ALScriptProblem::fill(p.message, p.args));
        }
        ensure_equals("one index note for one function", indexes, size_t(1));
        ensure("a found index and a boolean: " + listed, noted(r, "SluaIndexFound", "SubStringIndex") && noted(r, "SluaBool", "SameGroup"));
        ensure("words filled: " + listed, has(r, "-- LSL: llcompat.GetSubString takes indexes from 0, as LSL did; ll.GetSubString takes them from 1."));
        ensure("the lint by key", std::string(ALLSLToSLua::lintOf("SluaIndex")) == "SlCompatCall" && !ALLSLToSLua::lintOf("SluaIntegerDivision"));

        // The studio's words: each comment's, each note's, and the header's,
        // a line at a time.
        close.words = [](const std::string& key, const std::vector<std::string>& args, const std::string&) {
            return key == "SluaHeader" ? std::string("first\nsecond") : "<" + key + (args.empty() ? "" : " " + args[0]) + ">";
        };
        const ALLSLToSLua::Result said = ALLSLToSLua::convert(lsl, close);
        ensure("the header's: " + said.text, said.text.rfind("-- first\n-- second\n\n", 0) == 0);
        ensure("a comment's: " + said.text, has(said, "-- LSL: <SluaIndex GetSubString>\n"));
        ensure("a note's", noted(said, "SluaIndex", "GetSubString") && [&] {
            for (const ALScriptProblem& p : said.notes)
            {
                if (p.key == "SluaIndex")
                {
                    return p.message == "<SluaIndex GetSubString>";
                }
            }
            return false;
        }());
    }

    template<> template<>
    void allsltoslua_object::test<32>()
    {
        set_test_name("each LSL comment read back as a note: Done takes it out; one a lint finds too offers the lint's fix with it, read in "
                      "the words it was written in");
        ensure("the definitions", definitions);
        const std::string lsl = "default { state_entry() { string s = \"abc\"; llOwnerSay(llGetSubString(s, 0, 2)); } }";
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        ensure("converted", r.converted);
        ALScriptProblems notes = ALLSLToSLua::notesIn(r.text, nullptr);
        ensure("as many as the converter noted: " + r.text, notes.size() == r.notes.size() && !notes.empty());
        ALScriptProblem* index = nullptr;
        for (ALScriptProblem& note : notes)
        {
            ensure("a note's", note.key == "SluaNote" && note.severity == ALScriptProblem::Severity::Note && note.fixes.size() == 1 &&
                                   note.fixes[0].safe && note.fixes[0].removes && !note.fixes[0].preferred);
            index = note.message.rfind("llcompat.GetSubString takes indexes", 0) == 0 ? &note : index;
        }
        ensure("the index's, the lint's: " + r.text, index && index->code == "SlCompatCall");
        const size_t at = index - notes.data();
        ALLSLToSLua::linkNotes(notes, service.check(r.text), r.text);
        const ALScriptProblem& linked = notes[at];
        ensure("the lint's fix first, then Done", linked.fixes.size() == 2 && linked.fixes[0].preferred &&
                                                      linked.fixes[0].title == "Write it ll.GetSubString(s, 1, 3), and take the note out" &&
                                                      linked.fixes[1].title == "Done: take the note out");
        const std::optional<std::string> made = ALScriptFixes::apply(r.text, linked.fixes[0]);
        ensure("made: ll's, the note gone", made && made->find("ll.GetSubString(s, 1, 3)") != std::string::npos &&
                                                made->find("llcompat.GetSubString takes indexes") == std::string::npos);
        const std::optional<std::string> done = ALScriptFixes::apply(r.text, linked.fixes[1]);
        ensure("done: the line gone, the call kept", done && done->find("llcompat.GetSubString takes indexes") == std::string::npos &&
                                                         done->find("llcompat.GetSubString(s, 0, 2)") != std::string::npos &&
                                                         std::count(done->begin(), done->end(), '\n') + 1 == std::count(r.text.begin(), r.text.end(), '\n'));

        // Written in the studio's words, read back in them.
        ALLSLToSLua::Options close = ALLSLToSLua::Options::closeToLSL();
        close.words = [](const std::string& key, const std::vector<std::string>& args, const std::string& english) {
            return ALScriptProblem::fill(key == "SluaIndex" ? "Index de [1]" : english, args);
        };
        const ALLSLToSLua::Result said = ALLSLToSLua::convert(lsl, close);
        ensure("written so", has(said, "-- LSL: Index de GetSubString\n"));
        const auto linted = [](const ALScriptProblems& read) {
            return std::count_if(read.begin(), read.end(), [](const ALScriptProblem& note) { return note.code == "SlCompatCall"; });
        };
        ensure("read in the studio's words", linted(ALLSLToSLua::notesIn(said.text, close.words)) == 1);
        ensure("not in English's", linted(ALLSLToSLua::notesIn(said.text, nullptr)) == 0);
    }

    template<> template<>
    void allsltoslua_object::test<33>()
    {
        set_test_name("a parameter the script only reads as a truth is a boolean, each call giving one; one read as a number, or given more than TRUE where it is asked about TRUE, stays a number; an event's stay the grid's numbers");
        const ALLSLToSLua::Result r = convert("say(integer loud, string text) { if (loud) llShout(0, text); else llSay(0, text); }\n"
                                              "glow(integer on) { if (!on) return; llOwnerSay(\"glow\"); }\n"
                                              "integer next(integer n) { return n + 1; }\n"
                                              "integer exactly(integer f) { return f == TRUE; }\n"
                                              "default {\n"
                                              "    touch_start(integer n) {\n"
                                              "        say(TRUE, \"hi\");\n"
                                              "        say(n > 1, \"many\");\n"
                                              "        say(n, \"some\");\n"
                                              "        glow(FALSE);\n"
                                              "        llOwnerSay((string)next(n));\n"
                                              "        exactly(1); exactly(5);\n"
                                              "    }\n"
                                              "    on_rez(integer p) { if (p) llOwnerSay(\"rezzed\"); }\n"
                                              "}\n");
        ensure("read only as a truth: a boolean: " + r.text, has(r, "local function say(loud, text)") && has(r, "if loud then"));
        ensure("each call giving one, a number as its truth: " + r.text,
               has(r, "say(true, \"hi\")") && has(r, "say(n > 1, \"many\")") && has(r, "say(n ~= 0, \"some\")"));
        ensure("not, as a truth: " + r.text, has(r, "if not on then") && has(r, "glow(false)"));
        ensure("read as a number: kept one: " + r.text, has(r, "return n + 1") && has(r, "next_(n)"));
        ensure("given 5 where asked about TRUE: kept a number: " + r.text, has(r, "f == 1") && has(r, "exactly(5)"));
        ensure("an event's: the grid's number: " + r.text, has(r, "if p ~= 0 then"));
        checksClean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("say(integer loud, string text) { if (loud) llSay(0, text); }\n"
                                                           "default { state_entry() { say(TRUE, \"hi\"); } }\n",
                                                           typed);
        ensure("typed: " + t.text, has(t, "local function say(loud: boolean, text: string)"));
        checksClean(t);
    }

    template<> template<>
    void allsltoslua_object::test<34>()
    {
        set_test_name("a counting loop over a list's items, its counter read as nothing else, walks the items; from another start, or read otherwise, it counts");
        const ALLSLToSLua::Result r = convert("list gNames = [\"a\", \"b\"];\n"
                                              "default { state_entry() {\n"
                                              "    list keys = [llGetOwner()];\n"
                                              "    integer i;\n"
                                              "    for (i = 0; i < llGetListLength(gNames); ++i) llOwnerSay(llList2String(gNames, i));\n"
                                              "    integer j;\n"
                                              "    for (j = 0; j < llGetListLength(gNames); ++j) {\n"
                                              "        integer k;\n"
                                              "        for (k = 0; k < llGetListLength(keys); ++k) llOwnerSay(llList2String(gNames, j) + llList2String(keys, k));\n"
                                              "    }\n"
                                              "    integer m;\n"
                                              "    for (m = 1; m < llGetListLength(gNames); ++m) llOwnerSay(llList2String(gNames, m));\n"
                                              "    integer n;\n"
                                              "    for (n = 0; n < llGetListLength(gNames); ++n) llOwnerSay(llList2String(gNames, n) + (string)n);\n"
                                              "} }\n");
        ensure("walked: " + r.text, has(r, "for _, item in gNames do\n    print(item)\n"));
        ensure("one inside another, each its own name, a key's made text: " + r.text,
               has(r, "for _, item2 in keys do\n        print(item .. tostring(item2))"));
        ensure("from the second: counted: " + r.text, has(r, "for m = 2, #gNames do\n    print(gNames[m])"));
        ensure("its counter read as a number too: counted: " + r.text, has(r, "for n = 0, #gNames - 1 do"));
        checksClean(r);

        const ALLSLToSLua::Result named = convert("list gNames = [\"a\"];\n"
                                                  "integer _ = 2;\n"
                                                  "default { state_entry() {\n"
                                                  "    integer i;\n"
                                                  "    for (i = 0; i < llGetListLength(gNames); ++i) llOwnerSay(llList2String(gNames, i) + (string)_);\n"
                                                  "} }\n");
        ensure("an index that would hide the script's _ named afresh: " + named.text, has(named, "for _2, item in gNames do"));
        checksClean(named);
    }

    template<> template<>
    void allsltoslua_object::test<35>()
    {
        set_test_name("strings built in loops: one built by two loops declared once, the second adding to it; one appended to another in the loop it builds read as it stands, not built");
        const ALLSLToSLua::Result r = convert("default { state_entry() {\n"
                                              "    string s;\n"
                                              "    integer i;\n"
                                              "    for (i = 0; i < 2; ++i) s += \"a\";\n"
                                              "    integer j;\n"
                                              "    for (j = 0; j < 2; ++j) s += \"b\";\n"
                                              "    llOwnerSay(s);\n"
                                              "    string line;\n"
                                              "    string all;\n"
                                              "    integer k;\n"
                                              "    for (k = 0; k < 3; ++k) { line += \"x\"; all += line; }\n"
                                              "    llOwnerSay(all);\n"
                                              "} }\n");
        ensure("declared at the first loop's join: " + r.text, has(r, "local s = table.concat(sParts)"));
        ensure("the second adds to it: " + r.text, has(r, "s ..= table.concat(sParts2)") && !has(r, "local s = table.concat(sParts2)"));
        ensure("one read in its loop kept as it is: " + r.text, has(r, "local line = \"\"") && !has(r, "table.concat(lineParts)"));
        ensure("the other built from it: " + r.text, has(r, "table.insert(allParts, line)") && has(r, "local all = table.concat(allParts)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<36>()
    {
        set_test_name("bit32's answers made LSL's signed integers where they can pass 2147483647 -- a message number with its top bit set, the mask "
                      "that finds it, ~, a shift -- and not where bit32 takes them again, only truth is asked, or an & is with a constant not below nought");
        // A protocol of link messages whose numbers have their top bit set,
        // as a set of scripts may share: before, the mask's answer was never
        // the message's number, and no message was heard.
        const ALLSLToSLua::Result r = convert("integer MASK = 0xFFFF0000;\n"
                                              "integer MSG = 0xB3470000;\n"
                                              "integer FN_MASK = 0x0000FF00;\n"
                                              "integer FN_RESET = 0x0500;\n"
                                              "default {\n"
                                              "    state_entry() {\n"
                                              "        llMessageLinked(LINK_SET, MSG | FN_RESET, \"\", \"\");\n"
                                              "    }\n"
                                              "    link_message(integer sender, integer num, string text, key id) {\n"
                                              "        if ((num & MASK) == MSG) {\n"
                                              "            integer fn = num & FN_MASK;\n"
                                              "            if (fn == FN_RESET) llOwnerSay(\"reset\");\n"
                                              "        }\n"
                                              "        integer inverse = ~num;\n"
                                              "        integer high = num << 4;\n"
                                              "        integer low = num >> 2;\n"
                                              "        integer part = FN_MASK >> 8;\n"
                                              "        if (~llGetPermissions() & PERMISSION_TRIGGER_ANIMATION) llOwnerSay(\"ask\");\n"
                                              "        llOwnerSay((string)(inverse + high + low + part));\n"
                                              "    }\n"
                                              "}\n");
        ensure("the mask's answer signed: " + r.text, has(r, "if bit32.s32(bit32.band(num, MASK)) == MSG then"));
        ensure("the number sent signed: " + r.text, has(r, "ll.MessageLinked(LINK_SET, bit32.s32(bit32.bor(MSG, FN_RESET)), \"\", \"\")"));
        ensure("an & with a constant not below nought as it is: " + r.text, has(r, "local fn = bit32.band(num, FN_MASK)\n"));
        ensure("~ signed: " + r.text, has(r, "local inverse = bit32.s32(bit32.bnot(num))"));
        ensure("<< signed: " + r.text, has(r, "local high = bit32.s32(bit32.lshift(num, 4))"));
        ensure(">> of what may be below nought signed: " + r.text, has(r, "local low = bit32.s32(bit32.arshift(num, 2))"));
        ensure(">> of a constant not below nought as it is: " + r.text, has(r, "local part = bit32.arshift(FN_MASK, 8)\n"));
        ensure("a truth, and what bit32 takes again, as they are: " + r.text,
               has(r, "if bit32.btest(bit32.bnot(ll.GetPermissions()), PERMISSION_TRIGGER_ANIMATION) then"));
        ensure("SLua's own bit32.s32, no helper, said once: " + r.text, !has(r, "local function int32") && count(r, "-- LSL: bit32 answers") == 1);
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<37>()
    {
        set_test_name("the script's comments carried over, each over what it stood over: the script's own at the top, a global's, those inside a list, "
                      "a trailing one, a brace's, between branches, at a block's end; // as --, /* */ as --[[ ]] at a level nothing in it closes");
        const std::string lsl = "// Door script by Someone.\n"
                                "// Do as you like with it.\n"
                                "\n"
                                "// How far it swings, in degrees\n"
                                "float SWING = 90.0; // a right angle\n"
                                "list NAMES = [\n"
                                "    \"open\",   // when it is open\n"
                                "    \"closed\"  // when it is shut\n"
                                "];\n"
                                "/* old code:\n"
                                "integer unused() { return 1; }\n"
                                "*/\n"
                                "\n"
                                "// Turns the door.\n"
                                "swing(float by) { // by degrees\n"
                                "    // the rotation it turns by\n"
                                "    rotation r = llEuler2Rot(<0, 0, by * DEG_TO_RAD>);\n"
                                "    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_ROT_LOCAL, r * llGetLocalRot()]); // turn\n"
                                "    if (by > 0) {\n"
                                "        llOwnerSay(\"opening\");\n"
                                "    }\n"
                                "    // when it shuts\n"
                                "    else if (by < 0) {\n"
                                "        llOwnerSay(\"closing\");\n"
                                "    }\n"
                                "    if (FALSE) {\n"
                                "        /* left off */\n"
                                "    } else if (by == 0) {\n"
                                "        llOwnerSay(\"still\");\n"
                                "    }\n"
                                "    // done\n"
                                "}\n"
                                "\n"
                                "default {\n"
                                "    // on a touch\n"
                                "    touch_start(integer n) {\n"
                                "        swing(SWING); //[[ not a block of Luau's ]]\n"
                                "        /* it ends ]] here */\n"
                                "    }\n"
                                "}\n";
        const ALLSLToSLua::Result r = convert(lsl);
        const auto                at = [&r](const std::string& text) { return r.text.find(text); };
        ensure("the script's own at the top: " + r.text, has(r, "-- Door script by Someone.\n-- Do as you like with it.\n\n"));
        ensure("over everything, once: " + r.text, at("-- Door script by Someone.") < at("local SWING") && count(r, "Door script") == 1);
        ensure("a global's, what trailed it still after it: " + r.text, has(r, "-- How far it swings, in degrees\nlocal SWING = 90.0 -- a right angle\n"));
        ensure("those inside a list, over it: " + r.text, has(r, "-- when it is open\n-- when it is shut\nlocal NAMES = {\"open\", \"closed\"}\n"));
        ensure("a block comment as Luau's: " + r.text, has(r, "--[[ old code:\ninteger unused() { return 1; }\n]]\n"));
        ensure("over the function it stood over: " + r.text, at("--[[ old code:") > at("local NAMES") && at("--[[ old code:") < at("-- Turns the door."));
        ensure("a brace's, over what it opens: " + r.text, has(r, "-- Turns the door.\n-- by degrees\nlocal function swing(by)\n"));
        ensure("over a statement: " + r.text, has(r, "    -- the rotation it turns by\n    local r = "));
        ensure("a trailing one, after its statement: " + r.text, has(r, "    ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_ROT_LOCAL, r * ll.GetLocalRot()}) -- turn\n"));
        ensure("between branches, over the elseif: " + r.text, has(r, "    -- when it shuts\n    elseif by < 0 then\n"));
        ensure("an empty branch's, in it: " + r.text, has(r, "    if false then\n        --[[ left off ]]\n    elseif by == 0 then\n"));
        ensure("at a block's end: " + r.text, has(r, "    -- done\nend\n"));
        ensure("a state's, over its handler: " + r.text, has(r, "-- on a touch\nLLEvents:on(\"touch_start\""));
        ensure("not a block of Luau's: " + r.text, has(r, "    swing(SWING) -- [[ not a block of Luau's ]]\n"));
        ensure("a level nothing in it closes: " + r.text, has(r, "    --[=[ it ends ]] here ]=]\nend)"));
        checksClean(r);

        ALLSLToSLua::Options none;
        none.keepComments              = false;
        const ALLSLToSLua::Result bare = ALLSLToSLua::convert(lsl, none);
        ensure("none where asked: " + bare.text, bare.converted && bare.text.find("Door script") == std::string::npos &&
                                                    bare.text.find("degrees") == std::string::npos && bare.text.find("--[[") == std::string::npos);
    }

    template<> template<>
    void allsltoslua_object::test<38>()
    {
        set_test_name("anchors: each global, function, handler and statement's LSL line beside the line of SLua made of it, under the head, its notes and comments");
        const std::string lsl = "integer count = 0;\n"               // 0
                                "add(integer n)\n"                   // 1
                                "{\n"                                // 2
                                "    count += n;\n"                  // 3
                                "}\n"                                // 4
                                "default\n"                          // 5
                                "{\n"                                // 6
                                "    touch_start(integer d)\n"       // 7
                                "    {\n"                            // 8
                                "        // one more\n"              // 9
                                "        add(1);\n"                  // 10
                                "        llSay(0, (string)count);\n" // 11
                                "    }\n"                            // 12
                                "}\n";                               // 13
        const ALLSLToSLua::Result r = convert(lsl);
        std::vector<std::string>  lines;
        for (size_t from = 0; from <= r.text.size();)
        {
            const size_t cut = std::min(r.text.find('\n', from), r.text.size());
            lines.push_back(r.text.substr(from, cut - from));
            from = cut + 1;
        }
        for (const auto& [l, s] : r.anchors)
        {
            ensure("within both: " + std::to_string(l) + " " + std::to_string(s), l >= 0 && l < 14 && s >= 0 && s < static_cast<S32>(lines.size()));
        }
        const auto at = [&](S32 lsl_line) -> std::string {
            for (const auto& [l, s] : r.anchors)
            {
                if (l == lsl_line)
                {
                    return lines[static_cast<size_t>(s)];
                }
            }
            return "(none)";
        };
        ensure("the global: " + at(0) + "\n" + r.text, at(0).find("local count") != std::string::npos);
        ensure("the function: " + at(1), at(1).find("function add(") != std::string::npos);
        ensure("its statement: " + at(3), at(3).find("count") != std::string::npos && at(3).find("n") != std::string::npos);
        ensure("the handler: " + at(7), at(7).find("touch_start") != std::string::npos);
        ensure("a call under its comment, not the comment: " + at(10), at(10).find("add(1)") != std::string::npos);
        ensure("the next: " + at(11), at(11).find("ll.Say(0") != std::string::npos);
    }

    template<> template<>
    void allsltoslua_object::test<39>()
    {
        set_test_name("in fewer calls: an & asked whether it is nought by bit32.btest, values added together by one table.append, a key's default "
                      "the null key; and hexadecimal as written where SLua reads the same number");
        const ALLSLToSLua::Result r = convert("integer MASK = 0x0000FF00;\n"
                                              "integer HIGH = 0xF0000000;\n"
                                              "integer S_MASK = 0xF0000000;\n"
                                              "integer BIG = 4294967296;\n"
                                              "integer BIG_HEX = 0x100000000;\n"
                                              "integer WRAPPED = 2147483648;\n"
                                              "key gSitter;\n"
                                              "default { touch_start(integer n) {\n"
                                              "    integer flags = llGetParcelFlags(llGetPos());\n"
                                              "    if (flags & PARCEL_FLAG_ALLOW_CREATE_OBJECTS) llOwnerSay(\"build\");\n"
                                              "    if (!(flags & MASK & 0x300)) llOwnerSay(\"none\");\n"
                                              "    if ((flags & 4) == 0) llOwnerSay(\"no 4\");\n"
                                              "    integer masked = flags & MASK;\n"
                                              "    integer top = flags & 0x80000000;\n"
                                              "    integer sign = flags & S_MASK; if (flags & ~S_MASK) sign = 0;\n"
                                              "    integer low = 0xFFFF0000; sign = sign | low;\n"
                                              "    list params = [PRIM_NAME, \"a\"];\n"
                                              "    params += [PRIM_LINK_TARGET, LINK_ROOT, PRIM_POSITION, llGetPos()];\n"
                                              "    params += n;\n"
                                              "    llSetLinkPrimitiveParamsFast(LINK_THIS, params);\n"
                                              "    float f = (float)\"1.5\";\n"
                                              "    llOwnerSay((string)(masked + HIGH + top + BIG + BIG_HEX + WRAPPED + sign) + (string)f + (string)gSitter);\n"
                                              "} }\n");
        ensure("an & as a condition: " + r.text, has(r, "if bit32.btest(flags, PARCEL_FLAG_ALLOW_CREATE_OBJECTS) then"));
        ensure("not of one, an & of &s one call: " + r.text, has(r, "if not bit32.btest(flags, MASK, 0x300) then"));
        ensure("against nought: " + r.text, has(r, "if not bit32.btest(flags, 4) then"));
        ensure("an & as a number is still bit32.band: " + r.text, has(r, "local masked = bit32.band(flags, MASK)"));
        ensure("one bit32 takes, as written, which it reads the same: " + r.text, has(r, "local top = bit32.s32(bit32.band(flags, 0x80000000))"));
        ensure("values added together: " + r.text,
               has(r, "table.append(params, PRIM_LINK_TARGET, LINK_ROOT, PRIM_POSITION, ll.GetPos())") && has(r, "table.insert(params, n)"));
        ensure("the null key: " + r.text, has(r, "local gSitter = NULL_KEY") && !has(r, "uuid(\"\")"));
        ensure("a float from a string: " + r.text, has(r, "lslFloat(\"1.5\")") && has(r, "return llcompat.List2Float({ s }, 0)"));
        ensure("hexadecimal kept: " + r.text, has(r, "local MASK = 0x0000FF00"));
        ensure("past 0x7FFFFFFF, which SLua reads as another number, as LSL had it: " + r.text, has(r, "local HIGH = -268435456"));
        // As the grid's 32-bit hosts read them: strtoul stops at 0xFFFFFFFF.
        ensure("past 32 bits, -1: " + r.text, has(r, "local BIG = -1\n") && has(r, "local BIG_HEX = -1\n"));
        ensure("and said: " + r.text, noted(r, "SluaIntegerPast32Bits", "4294967296") && noted(r, "SluaIntegerPast32Bits", "0x100000000"));
        ensure("a global only bit32 reads keeps its hexadecimal: " + r.text, has(r, "local S_MASK = 0xF0000000\n") &&
                                                                               has(r, "bit32.band(flags, S_MASK)"));
        ensure("one read as a number does not: " + r.text, has(r, "local HIGH = -268435456"));
        ensure("nor a local, never set, only bit32 reads: " + r.text, has(r, "local low = 0xFFFF0000\n"));
        // Each said once, over the first place: what SLua reads such a
        // number as, and what bit32.s32 is for.
        const auto times = [&r](const char* key) {
            return std::count_if(r.notes.begin(), r.notes.end(), [key](const ALScriptProblem& p) { return p.key == key; });
        };
        ensure("the hexadecimal's number said once, over the first: " + r.text,
               times("SluaHexSign") == 1 && noted(r, "SluaHexSign", "0xF0000000") &&
                   has(r, "-- LSL: SLua reads 0xF0000000 as 4026531840, where LSL wrapped it to -268435456: bit32 takes either the same, so it "
                          "stays as written where only bit32 reads it, and is -268435456 where it is read as a number.\nlocal HIGH = -268435456"));
        ensure("bit32.s32 said once: " + r.text, times("SluaBit32Signed") == 1 && count(r, "-- LSL: bit32 answers 0 to 4294967295") == 1);
        ensure("within them, wrapped: " + r.text, has(r, "local WRAPPED = -2147483648\n"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<40>()
    {
        set_test_name("numeric for by a constant's name, and a counter its fors share; each function local but one called before it is written");
        const ALLSLToSLua::Result r = convert("integer ACTIVE_PROPS_STRIDE = 3;\n"
                                              "list activeProps = [\"a\", 1, 2, \"b\", 3, 4];\n"
                                              "setProps(list unattachedProps) {\n"
                                              "    integer i;\n"
                                              "    for (i = llGetListLength(activeProps) - ACTIVE_PROPS_STRIDE; i >= 0; i -= ACTIVE_PROPS_STRIDE) {\n"
                                              "        llOwnerSay((string)i);\n"
                                              "    }\n"
                                              "    for (i = 0; i < llGetListLength(unattachedProps); i++) {\n"
                                              "        llOwnerSay((string)i + llList2String(unattachedProps, i));\n"
                                              "    }\n"
                                              "    report();\n"
                                              "}\n"
                                              "report() { llOwnerSay(\"done\"); }\n"
                                              "integer twice(integer n) { return n * 2; }\n"
                                              "default { state_entry() { setProps([\"x\"]); llOwnerSay((string)twice(2)); } }\n");
        ensure("down by the constant's name: " + r.text, has(r, "for i = #activeProps - ACTIVE_PROPS_STRIDE, 0, -ACTIVE_PROPS_STRIDE do"));
        ensure("the same counter's other for: " + r.text, has(r, "for i = 0, #unattachedProps - 1 do"));
        ensure("the counter's declaration gone: " + r.text, !has(r, "local i = 0") && !has(r, "while i"));
        ensure("only the one called first declared first: " + r.text, has(r, "local report\n") && has(r, "local function setProps(") &&
                                                                             has(r, "\nfunction report()") && has(r, "local function twice("));
        checksClean(r);

        const ALLSLToSLua::Result set = convert("integer gStep = 2;\n"
                                                "default { state_entry() { integer i; for (i = 0; i < 10; i += gStep) llOwnerSay((string)i); gStep = 3; } }\n");
        ensure("a step by a global something sets keeps the while: " + set.text, has(set, "while i < 10 do"));
        checksClean(set);
    }

    template<> template<>
    void allsltoslua_object::test<41>()
    {
        set_test_name("the script's comments as it laid them out: one after a statement still after it, rules of stars and slashes as dashes, "
                      "the //* toggle as ---[[, and an if on one line on one line");
        ALLSLToSLua::Options options;
        options.types               = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("/***************************************/\n"
                                                           "/************** CONSTANTS **************/\n"
                                                           "/***************************************/\n"
                                                           "integer S_MASK = 0xF0000000; // -268435456\n"
                                                           "///////////////////////// DEBUGGING ///////////////////////////\n"
                                                           "integer gDebug = TRUE;\n"
                                                           "//*\n"
                                                           "string get(integer keey) {\n"
                                                           "    return llLinksetDataRead(\"ARS#\" + (string)keey);\n"
                                                           "}\n"
                                                           "//*/\n"
                                                           "/*\n"
                                                           "string old() { return \"\"; }\n"
                                                           "//*/\n"
                                                           "integer allowed(integer parcelFlags) {\n"
                                                           "    if (parcelFlags & PARCEL_FLAG_ALLOW_CREATE_OBJECTS) return TRUE; // may build\n"
                                                           "    if (parcelFlags & 4)\n"
                                                           "        return 2;\n"
                                                           "    integer n = 3; // three\n"
                                                           "    n += 1;    /* and one */\n"
                                                           "    return FALSE;\n"
                                                           "}\n"
                                                           "default { state_entry() { llOwnerSay(get(allowed(S_MASK)) + (string)gDebug); } }\n",
                                                           options);
        ensure("converted", r.converted);
        ensure("a rule of stars: " + r.text, has(r, "\n-----------------------------------------\n--------------- CONSTANTS ---------------\n"));
        ensure("after its statement: " + r.text, has(r, "local S_MASK: number = -268435456 -- -268435456\n"));
        ensure("a rule of slashes: " + r.text, has(r, "\n---------------------------------------------------------------\n") ||
                                                    has(r, "------------------------- DEBUGGING ---------------------------\n"));
        ensure("the toggle: " + r.text, has(r, "---[[\nlocal function get(") && has(r, "end\n\n--]]\n"));
        ensure("the toggle the other way: " + r.text, has(r, "--[[\nstring old() { return \"\"; }\n--]]"));
        ensure("an if on one line on one: " + r.text,
               has(r, "    if bit32.btest(parcelFlags, PARCEL_FLAG_ALLOW_CREATE_OBJECTS) then return 1 end -- may build\n"));
        ensure("one on two lines on three: " + r.text, has(r, "    if bit32.btest(parcelFlags, 4) then\n        return 2\n    end\n"));
        ensure("after a declaration and a block comment after a step: " + r.text,
               has(r, "local n: number = 3 -- three\n") && has(r, "n += 1 --[[ and one ]]\n"));
        // The lines beside the LSL's past an if put on one line.
        std::vector<std::string> lines;
        for (size_t from = 0; from <= r.text.size();)
        {
            const size_t cut = std::min(r.text.find('\n', from), r.text.size());
            lines.push_back(r.text.substr(from, cut - from));
            from = cut + 1;
        }
        const auto at = [&](S32 lsl_line) -> std::string {
            for (const auto& [l, s] : r.anchors)
            {
                if (l == lsl_line && s >= 0 && s < static_cast<S32>(lines.size()))
                {
                    return lines[static_cast<size_t>(s)];
                }
            }
            return "(none)";
        };
        ensure("the if on one line beside its line: " + at(15), at(15).find("then return 1 end") != std::string::npos);
        ensure("the next beside its own: " + at(16), at(16).find("if bit32.btest(parcelFlags, 4) then") != std::string::npos);
        ensure("and those after: " + at(18) + " / " + at(19), at(18).find("local n: number = 3") != std::string::npos && at(19).find("n += 1") != std::string::npos);
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<42>()
    {
        set_test_name("declarations the LSL lined up lined up again, their = and the comments after them; those it did not, as written");
        ALLSLToSLua::Options options;
        options.types               = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("integer S_MASK        = 0xF0000000; // the sign\n"
                                                           "integer ACTIVE_STRIDE = 3;          // per prop\n"
                                                           "string  PREFIX        = \"ARS#\";     // linkset data\n"
                                                           "\n"
                                                           "integer a = 1;\n"
                                                           "string bee = \"b\";\n"
                                                           "default { state_entry() {\n"
                                                           "    integer x    = 1;\n"
                                                           "    float   yy   = 2.5;\n"
                                                           "    llOwnerSay(PREFIX + (string)(S_MASK + ACTIVE_STRIDE + a + x + yy) + bee);\n"
                                                           "} }\n",
                                                           options);
        ensure("converted", r.converted);
        ensure("globals lined up: " + r.text, has(r, "local S_MASK: number        = -268435456 -- the sign\n"
                                                     "local ACTIVE_STRIDE: number = 3          -- per prop\n"
                                                     "local PREFIX: string        = \"ARS#\"     -- linkset data\n"));
        ensure("not lined up, not lined up: " + r.text, has(r, "local a: number = 1\nlocal bee: string = \"b\"\n"));
        ensure("locals lined up: " + r.text, has(r, "\nlocal x: number  = 1\nlocal yy: number = 2.5\n"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<43>()
    {
        set_test_name("text made a vector or a rotation bracketed only where an operator around it needs it");
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("vector offset;\n"
                                                           "default { state_entry() {\n"
                                                           "    list command = [\"a\", \"<1,2,3>\"];\n"
                                                           "    offset = (vector)llList2String(command, 1);\n"
                                                           "    rotation r = (rotation)llList2String(command, 1);\n"
                                                           "    llOwnerSay((string)((vector)llList2String(command, 1) * 2.0));\n"
                                                           "    llSetPos((vector)llList2String(command, 1));\n"
                                                           "} }\n");
        ensure("converted", r.converted);
        ensure("assigned as it is: " + r.text, has(r, "offset = tovector(command[2] or \"\") or ZERO_VECTOR\n"));
        ensure("a local's value too: " + r.text, has(r, "local r = toquaternion(command[2] or \"\") or ZERO_ROTATION\n"));
        ensure("an argument too: " + r.text, has(r, "ll.SetPos(tovector(command[2] or \"\") or ZERO_VECTOR)"));
        ensure("an operand bracketed: " + r.text, has(r, "(tovector(command[2] or \"\") or ZERO_VECTOR) * 2"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<44>()
    {
        set_test_name("an integer made text where .. joins it written bare, which .. makes the same text of; alone, a float's, a key's, or a lone piece of a table's, as before");
        const ALLSLToSLua::Result r = convert("set(integer keey, string value) { llLinksetDataWrite(\"ARS#\" + (string)keey, value); }\n"
                                              "default { state_entry() {\n"
                                              "    integer n = 4; float f = 1.5; key k = llGetOwner();\n"
                                              "    set(n, (string)n);\n"
                                              "    llOwnerSay((string)f + \";\" + (string)k + \";\" + (string)(n * 2) + (string)(-n));\n"
                                              "} }\n");
        ensure("converted", r.converted);
        ensure("as Tapple would have it: " + r.text, has(r, "ll.LinksetDataWrite(\"ARS#\" .. keey, value)"));
        ensure("alone, still text: " + r.text, has(r, "set(n, tostring(n))"));
        ensure("a float's six places, a key's text, a product bracketed, a negation bare: " + r.text,
               has(r, "print(string.format(\"%.6f\", f) .. \";\" .. tostring(k) .. \";\" .. (n * 2) .. -n)"));
        checksClean(r);
    }

    template<> template<>
    void allsltoslua_object::test<45>()
    {
        set_test_name("a vector made text as LSL wrote it, five places a part, which tostring would not: noted, once");
        const ALLSLToSLua::Result r = convert("integer SEAT_NUM = 0;\n"
                                              "vector offset = ZERO_VECTOR;\n"
                                              "set(integer which, string value) { llLinksetDataWrite((string)which, value); }\n"
                                              "default { state_entry() {\n"
                                              "    set(7, (string)SEAT_NUM + \";\" + (string)offset);\n"
                                              "    llOwnerSay((string)ZERO_ROTATION);\n"
                                              "} }\n");
        ensure("converted", r.converted);
        ensure("LSL's text kept: " + r.text, has(r, "set(7, SEAT_NUM .. \";\" .. ll.DumpList2String({offset}, \"\"))"));
        ensure("a rotation's too: " + r.text, has(r, "ll.DumpList2String({ZERO_ROTATION}, \"\")"));
        ensure("said why, once: " + r.text, noted(r, "SluaVectorText") && r.text.find("-- LSL: ll.DumpList2String writes") != std::string::npos &&
                                                 r.text.find("-- LSL: ll.DumpList2String writes") == r.text.rfind("-- LSL: ll.DumpList2String writes"));
        checksClean(r);
    }
}
