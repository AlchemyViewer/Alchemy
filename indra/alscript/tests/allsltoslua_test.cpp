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

#include "../lsl/allsldepth.h"
#include "../lsl/allsltoslua.h"
#include "../core/alscriptformatter.h"
#include "../lint/alscriptfixes.h"
#include "../lsl/allslservice.h"
#include "../lsl/allsltraits.h"
#include "../luau/alluauservice.h"

#include <catch2/catch_test_macros.hpp>

#include "Luau/Allocator.h"
#include "Luau/Lexer.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace
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
                    FAIL("the builtins did not load: " + error);
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
        ALLSLToSLua::Result require_converted(const std::string& lsl)
        {
            ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl);
            std::string         why;
            for (const ALScriptProblem& p : r.problems)
            {
                why += p.message + "\n";
            }
            CAPTURE(why);
            REQUIRE(r.converted);
            return r;
        }

        // What the converter must not write: an error, what the studio's own
        // lints would have written SLua's way, or a deprecated ll function --
        // SLua's way where it means the same, llcompat's where not. But one
        // SLua only deprecates, the one function llcompat's is: ll's.
        static bool unwanted(const ALScriptProblem& p)
        {
            static const std::vector<std::string_view> OWN = { "SlCompoundAssign", "SlNumberTruth",    "SlZeroIndex",    "SlParenCondition",
                                                               "SlGeneralizedFor", "SlIndexDivision", "SlVectorProduct" };
            const auto only_deprecated = [](const std::string& member) {
                const ALLSLTraits::Trait* trait = ALLSLTraits::of(("ll" + member.substr(3)).c_str());
                return trait && trait->slua == ALLSLTraits::SluaDeprecated;
            };
            return p.severity == ALScriptProblem::Severity::Error || std::find(OWN.begin(), OWN.end(), p.code) != OWN.end() ||
                   (p.key.rfind("LuauLintDeprecatedMember", 0) == 0 && !p.args.empty() && p.args[0].rfind("ll.", 0) == 0 &&
                    !only_deprecated(p.args[0]));
        }

        // What SLua's check says of it: nothing unwanted.
        void require_checks_clean(const ALLSLToSLua::Result& r)
        {
            REQUIRE(definitions);
            std::string said;
            for (const ALScriptProblem& p : service.check(r.text))
            {
                if (unwanted(p))
                {
                    said += llformat("[%d:%d] %s\n", p.line + 1, p.column + 1, p.message.c_str());
                }
            }
            CAPTURE(said, r.text);
            REQUIRE(said.empty());
        }

        // The comment Luau's own lexer reads beginning at `at` in the text,
        // all of it: a line's to its line's end, a block's to its close.
        // Nothing where none begins there, or a block is never closed.
        static std::string commentAt(const std::string& slua, size_t at)
        {
            std::vector<size_t> starts{ 0 };
            for (size_t i = 0; i < slua.size(); ++i)
            {
                if (slua[i] == '\n')
                {
                    starts.push_back(i + 1);
                }
            }
            Luau::Allocator    allocator;
            Luau::AstNameTable names(allocator);
            Luau::Lexer        lexer(slua.data(), slua.size(), names);
            lexer.setSkipComments(false);
            for (const Luau::Lexeme* token = &lexer.next(); token->type != Luau::Lexeme::Eof; token = &lexer.next())
            {
                const size_t from = starts[token->location.begin.line] + token->location.begin.column;
                if (from < at)
                {
                    continue;
                }
                if (from > at || (token->type != Luau::Lexeme::Comment && token->type != Luau::Lexeme::BlockComment))
                {
                    return {};
                }
                return slua.substr(from, starts[token->location.end.line] + token->location.end.column - from);
            }
            return {};
        }
        static std::string require_comment_at(const std::string& slua, const std::string& begins)
        {
            const size_t at = slua.find(begins);
            CAPTURE(begins, slua);
            REQUIRE(at != std::string::npos);
            return commentAt(slua, at);
        }
        // A toggle turned the other way in SLua: a dash taken out of its
        // opener's ---, or put in a block comment's --; the comment that
        // begins there then.
        static std::string require_toggled(const std::string& slua, const std::string& opener, bool off = true)
        {
            const size_t at = slua.find(opener);
            CAPTURE(opener, slua);
            REQUIRE(at != std::string::npos);
            std::string other = slua;
            if (off)
            {
                other.erase(at, 1);
            }
            else
            {
                other.insert(at, "-");
            }
            return commentAt(other, at);
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
        // Any of the notes that LSL ran a binary's right side first.
        static bool orderNoted(const ALLSLToSLua::Result& r)
        {
            for (const ALScriptProblem& p : r.notes)
            {
                if (p.key.rfind("SluaRightFirst", 0) == 0)
                {
                    return true;
                }
            }
            return false;
        }
    };

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a one-state script as locals and LLEvents handlers; state_entry last", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gCount = 2;\n"
                                                        "string greet(string who) { return \"Hello, \" + who; }\n"
                                                        "default {\n"
                                                        "    state_entry() { llSay(0, greet(\"world\")); }\n"
                                                        "    touch_start(integer total_number) {\n"
                                                        "        gCount += total_number;\n"
                                                        "        llSay(0, greet(llDetectedName(0)) + \" \" + (string)gCount);\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("the global");
        CHECK(has(r, "local gCount = 2"));
        {
            INFO("the function");
            CHECK(has(r, "local function greet(who)"));
            CHECK(has(r, "return \"Hello, \" .. who"));
        }
        {
            INFO("the handler");
            CHECK(has(r, "LLEvents:on(\"touch_start\", function(detected)"));
            CHECK(has(r, "local total_number = #detected"));
        }
        UNSCOPED_INFO("the step");
        CHECK(has(r, "gCount += total_number"));
        {
            INFO("what was detected from the table");
            CHECK(has(r, "detected[1]:getName()"));
            CHECK(noted(r, "SluaDetectedTable"));
        }
        UNSCOPED_INFO("an integer joined to text, interpolated");
        CHECK(has(r, "ll.Say(0, `{greet(detected[1]:getName())} {gCount}`)"));
        {
            INFO("state_entry last");
            const size_t entered = r.text.find("-- state_entry");
            CHECK(entered != std::string::npos);
            CHECK(entered > r.text.find("LLEvents:on"));
            CHECK(has(r, "ll.Say(0, greet(\"world\"))"));
        }
        UNSCOPED_INFO("the note over its line");
        CHECK(has(r, "-- LSL: detected[n] is what LSL read"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: expressions: truth tests; 1 or 0 comparisons; / and % toward nought; lists", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("a comparison as a number");
        CHECK(has(r, "local same = if a == b then 1 else 0"));
        UNSCOPED_INFO("an integer's truth");
        CHECK(has(r, "if a ~= 0 then"));
        UNSCOPED_INFO("a string's");
        CHECK(has(r, "if s ~= \"\" then"));
        CHECK(has(r, "if a > 1 and #l > 1 then"));
        UNSCOPED_INFO("integer division");
        CHECK(has(r, "local q = bit32.s32(a / b)"));
        UNSCOPED_INFO("remainder");
        CHECK(has(r, "local m = math.fmod(a, b)"));
        {
            INFO("a list no other holds grown in place");
            CHECK(has(r, "table.insert(l, 3)"));
            CHECK(has(r, "table.append(l, 4, \"five\")"));
            CHECK_FALSE(has(r, "table.extend"));
        }
        {
            INFO("TRUE and FALSE");
            CHECK(has(r, "local t = 1"));
            CHECK(has(r, "if false then"));
        }
        UNSCOPED_INFO("a float as LSL writes it");
        CHECK(has(r, "string.format(\"%.6f\", f)"));
        {
            INFO("a string as an integer, as a list's item converts it");
            CHECK(has(r, "lslInteger(\"12abc\")"));
            CHECK(has(r, "return llcompat.List2Integer({ s }, 0)"));
        }
        {
            INFO("lists compared by length");
            CHECK(has(r, "#l == #{ 1 }"));
            CHECK(noted(r, "SluaListCompare"));
        }
        // bit32 answers 0 to 4294967295: made LSL's signed integer, but where
        // bit32 takes it again.
        CHECK(has(r, "bit32.s32(bit32.bor(bit32.band(a, b), 4))"));
        CHECK_FALSE(has(r, "local function int32"));
        {
            INFO("a vector's part set");
            CHECK(has(r, "v = vector(4, v.y, v.z)"));
            CHECK(has(r, "v = vector(v.x, v.y, v.z + 1)"));
        }
        UNSCOPED_INFO("a vector as LSL writes it");
        CHECK(has(r, "ll.DumpList2String({ v }, \"\")"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: loops: numeric for; break and continue jumps; repeat; other jumps noted", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("a numeric for");
        CHECK(has(r, "for i = 0, 9 do"));
        UNSCOPED_INFO("its counter declared by it alone");
        CHECK_FALSE(has(r, "local i = 0"));
        UNSCOPED_INFO("continue, the step Luau's own");
        CHECK(has(r, "then continue end"));
        CHECK(has(r, "break"));
        CHECK(has(r, "repeat"));
        CHECK(has(r, "until not (n > 5)"));
        UNSCOPED_INFO("a step");
        CHECK(has(r, "n -= 1"));
        {
            INFO("another jump noted and kept as a mark");
            CHECK(noted(r, "SluaJump"));
            CHECK(has(r, "-- jump away"));
            CHECK(has(r, "-- @away"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: states as handler tables entered by setState; the timer on LLTimers", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default {\n"
                                                        "    state_entry() { llSetTimerEvent(1.0); }\n"
                                                        "    timer() { state running; }\n"
                                                        "    state_exit() { llOwnerSay(\"leaving\"); }\n"
                                                        "}\n"
                                                        "state running {\n"
                                                        "    state_entry() { llOwnerSay(\"running\"); }\n"
                                                        "    touch_start(integer n) { if (n > 1) { state default; } llOwnerSay(\"touched\"); }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("the tables");
            CHECK(has(r, "states.default = {"));
            CHECK(has(r, "states.running = {"));
        }
        CHECK(has(r, "local function setState(name: string)"));
        CHECK(has(r, "setState(\"default\")"));
        {
            INFO("a change ends the event, where anything follows");
            CHECK(has(r, "setState(\"running\")\n    end,"));
            CHECK(has(r, "setState(\"default\")\n            return\n        end"));
        }
        {
            INFO("the timer on LLTimers");
            CHECK(has(r, "setTimer(1"));
            CHECK(has(r, "LLTimers:every(seconds"));
            CHECK(noted(r, "SluaTimers"));
        }
        {
            INFO("calling the state's handler");
            CHECK(has(r, "states[currentState].timer"));
            CHECK(has(r, "event ~= \"timer\""));
        }
        UNSCOPED_INFO("state_exit a handler of its state");
        CHECK(has(r, "state_exit = function()"));
        UNSCOPED_INFO("what is let go of noted");
        CHECK(noted(r, "SluaStates"));
        // Tapple's: a name with no state, which only an edit could write,
        // said so at the call, before the state it is in is left.
        const size_t looked = r.text.find("    local entering = states[name]\n    if not entering then\n        error(\"unknown state: \" .. name, 2)\n    end\n");
        {
            INFO("a state not there said");
            REQUIRE(looked != std::string::npos);
            const size_t leaving = r.text.find("local leaving");
            CHECK(leaving != std::string::npos);
            CHECK(looked < leaving);
        }
        require_checks_clean(r);
        ALLSLToSLua::Options typed;
        typed.types = true;
        require_checks_clean(ALLSLToSLua::convert("default { touch_start(integer n) { state two; } }\nstate two { state_entry() { state default; } }\n", typed));
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: names: reserved ones renamed; forward declarations; inline assignments", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer table = 1;\n"
                                                        "integer first(integer end) { return second(end) + table; }\n"
                                                        "integer second(integer x) { return x * 2; }\n"
                                                        "default { state_entry() {\n"
                                                        "    integer i; integer j = (i = 4) + i++;\n"
                                                        "    llOwnerSay((string)first(j));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        CHECK(has(r, "local table_ = 1"));
        CHECK(has(r, "(end_)"));
        {
            INFO("only the one called before it is written declared first");
            CHECK(has(r, "local second\n"));
            CHECK(has(r, "local function first(end_)"));
            CHECK(has(r, "\nfunction second(x)"));
        }
        {
            INFO("an assignment in place");
            CHECK(has(r, "(function() i = 4 return i end)()"));
            CHECK(noted(r, "SluaAssignInExpression"));
        }
        UNSCOPED_INFO("a step after, what it was");
        CHECK(has(r, "(function() local was = i; i += 1 return was end)()"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: LSL that does not parse writes nothing and says why", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("default { state_entry() { integer = ; } }");
        CHECK_FALSE(r.converted);
        CHECK(r.text.empty());
        CHECK_FALSE(r.problems.empty());
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: close to LSL: llcompat's Detected and timer event noted; ll.OwnerSay kept", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Options close = ALLSLToSLua::Options::closeToLSL();
        const ALLSLToSLua::Result  r     = ALLSLToSLua::convert("default {\n"
                                                                "    state_entry() { llSetTimerEvent(2.0); }\n"
                                                                "    timer() { llOwnerSay(\"tick\"); }\n"
                                                                "    touch_start(integer n) { llOwnerSay(llDetectedName(0)); }\n"
                                                                "}\n",
                                                                close);
        CAPTURE(r.text);
        CHECK(r.converted);
        {
            INFO("llcompat's Detected");
            CHECK(has(r, "llcompat.DetectedName(0)"));
            CHECK(noted(r, "SluaDetected"));
        }
        {
            INFO("the timer event");
            CHECK(has(r, "llcompat.SetTimerEvent(2"));
            CHECK(has(r, "LLEvents:on(\"timer\""));
            CHECK(noted(r, "SluaTimer"));
        }
        {
            INFO("ll.OwnerSay, SLua deprecating it for print");
            CHECK(has(r, "ll.OwnerSay(\"tick\")"));
            CHECK_FALSE(has(r, "llcompat.OwnerSay"));
            CHECK(noted(r, "SluaDeprecatedFor", "OwnerSay"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: SLua's ll where it means the same and SLua's own forms for some calls", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("a boolean in a condition");
        CHECK(has(r, "if ll.SameGroup(k) then"));
        UNSCOPED_INFO("and as a number");
        CHECK(has(r, "local same = if ll.SameGroup(k) then 1 else 0"));
        UNSCOPED_INFO("a constant index moved on");
        CHECK(has(r, "ll.GetSubString(s, 1, 3)"));
        {
            INFO("one that is not, through llcompat");
            CHECK(has(r, "llcompat.GetSubString(s, i, -1)"));
            CHECK(noted(r, "SluaIndex", "GetSubString"));
        }
        UNSCOPED_INFO("one thing found in a list");
        CHECK(has(r, "if table.find(l, \"a\") ~= nil then"));
        UNSCOPED_INFO("~ of a find");
        CHECK(has(r, "if string.find(s, \"c\", 1, true) ~= nil then"));
        UNSCOPED_INFO("< 0 of one");
        CHECK(has(r, "if string.find(s, \"z\", 1, true) == nil then"));
        UNSCOPED_INFO("a find's index as a number, through llcompat");
        CHECK(has(r, "llcompat.ListFindList(l, { \"a\", \"b\" })"));
        {
            INFO("^, math and vector");
            CHECK(has(r, "2.0 ^ 3.0"));
            CHECK(has(r, "math.abs(-1.0)"));
            CHECK(has(r, "vector.magnitude(vector(1, 2, 3) - ZERO_VECTOR)"));
        }
        UNSCOPED_INFO("half up, as LSL rounds, not math.round");
        CHECK(has(r, "math.floor(2.5 + 0.5)"));
        {
            INFO("os.time and print");
            CHECK(has(r, "os.time()"));
            CHECK(has(r, "print("));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: handlers assigned to LLEvents' fields and Luau types on what LSL typed", "[ALLSLToSLua]")
    {
        ALLSLToSLua::Options options;
        options.handlers = ALLSLToSLua::Options::Handlers::Field;
        options.types    = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("integer gCount = 2;\n"
                                                           "string greet(string who) { return \"Hi \" + who; }\n"
                                                           "default { touch_start(integer n) { string s = greet(\"x\"); gCount += n; llOwnerSay(s); } }\n",
                                                           options);
        CAPTURE(r.text);
        CHECK(r.converted);
        {
            INFO("a field, as SLua's own scripts write one");
            CHECK(has(r, "function LLEvents.touch_start(detected: { DetectedEvent })"));
            CHECK_FALSE(has(r, "LLEvents:on"));
            CHECK_FALSE(has(r, "= function("));
        }
        UNSCOPED_INFO("the count typed");
        CHECK(has(r, "local n: number = #detected"));
        CHECK(has(r, "local gCount: number = 2"));
        CHECK(has(r, "local function greet(who: string): string"));
        CHECK(has(r, "local s: string = greet(\"x\")"));
        require_checks_clean(r);

        // Each parameter as the definitions type SLua's handler: a key a
        // uuid, link_message's id the string SLua passes; none on a key the
        // body takes as text.
        const char* const handlers = "default {\n"
                                     "    on_rez(integer param) { llOwnerSay((string)param); }\n"
                                     "    listen(integer c, string n, key id, string m) { llOwnerSay(n + m + (string)c + (string)id); }\n"
                                     "    link_message(integer s, integer num, string str, key id) { llOwnerSay(str + (string)id); }\n"
                                     "    money(key giver, integer amount) { giver = \"someone\"; llOwnerSay((string)giver); }\n"
                                     "}\n";
        const ALLSLToSLua::Result typed = ALLSLToSLua::convert(handlers, options);
        CAPTURE(typed.text);
        UNSCOPED_INFO("an integer");
        CHECK(has(typed, "function LLEvents.on_rez(param: number)"));
        UNSCOPED_INFO("a key a uuid");
        CHECK(has(typed, "function LLEvents.listen(c: number, n: string, id: uuid, m: string)"));
        UNSCOPED_INFO("link_message's id a string");
        CHECK(has(typed, "function LLEvents.link_message(s: number, num: number, str: string, id: string)"));
        UNSCOPED_INFO("a key the body takes as text, under a name of its own");
        CHECK(has(typed, "function LLEvents.money(giverKey: uuid, amount: number)\n    local giver: string = tostring(giverKey)\n    giver = \"someone\"\n"));
        require_checks_clean(typed);
        // Strict as well, under the grid's solver: each handler as the
        // definitions type it, the text key made text.
        if (!newSolver)
        {
            std::string said;
            for (const ALScriptProblem& p : service.check("--!strict\n" + typed.text))
            {
                said += p.severity == ALScriptProblem::Severity::Error ? p.message + "\n" : std::string();
            }
            CAPTURE(said);
            CHECK(said.empty());
        }
        ALLSLToSLua::Options on = options;
        on.handlers             = ALLSLToSLua::Options::Handlers::On;
        UNSCOPED_INFO("typed with LLEvents:on too");
        CHECK(has(ALLSLToSLua::convert(handlers, on), "LLEvents:on(\"on_rez\", function(param: number)"));
        on.types = false;
        UNSCOPED_INFO("and none where types are not written");
        CHECK(has(ALLSLToSLua::convert(handlers, on), "LLEvents:on(\"on_rez\", function(param)"));

        const ALLSLToSLua::Result many = ALLSLToSLua::convert("default { touch_start(integer n) { state other; } }\n"
                                                              "state other { touch_start(integer n) { state default; } }\n",
                                                              options);
        CAPTURE(many.text);
        // SLua stops the script at a field assigned nil: taken off by
        // LLEvents:off, as LLEvents:on's are.
        {
            INFO("setState sets the fields");
            CHECK(has(many, "(LLEvents :: any)[event] = handler"));
            CHECK(has(many, "LLEvents:off(event :: any, handler)"));
            CHECK_FALSE(has(many, "[event] = nil"));
        }
        UNSCOPED_INFO("a state's handlers typed in its table");
        CHECK(has(many, "touch_start = function(detected: { DetectedEvent })"));
        require_checks_clean(many);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: no comments where they are not wanted: the notes are still said", "[ALLSLToSLua]")
    {
        ALLSLToSLua::Options options;
        options.comments = false;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("default { state_entry() { list l = [7]; integer a = l == [2]; llOwnerSay((string)a); } }\n", options);
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("no comment");
        CHECK_FALSE(has(r, "-- LSL:"));
        UNSCOPED_INFO("but noted");
        CHECK(noted(r, "SluaListCompare"));
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: numeric for where it counts as LSL's did; while where it might not", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    list l = [1, 2, 3]; integer n = 4; integer total;\n"
                                                        "    integer a; for (a = 0; a < llGetListLength(l); a++) total += llList2Integer(l, a);\n"
                                                        "    integer b; for (b = 10; b >= 0; b -= 2) total += b;\n"
                                                        "    integer c; for (c = 1; c <= n; ++c) total += c;\n"
                                                        "    integer d; for (d = 0; d < n; ++d) { if (total > 100) d = n; }\n"
                                                        "    integer e; for (e = 0; e < n; ++e) { n--; }\n"
                                                        "    integer f; for (f = 0; f < 3; ++f) total += f;\n"
                                                        "    llOwnerSay((string)(total + f));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("up to a length, walking its items");
        CHECK(has(r, "for _, item in l do\n    total += item"));
        UNSCOPED_INFO("down by a step");
        CHECK(has(r, "for b = 10, 0, -2 do"));
        UNSCOPED_INFO("up to and with");
        CHECK(has(r, "for c = 1, n do"));
        UNSCOPED_INFO("a counter set in its body keeps the while");
        CHECK(has(r, "while d < n do"));
        UNSCOPED_INFO("a limit set in its body keeps the while");
        CHECK(has(r, "while e < n do"));
        {
            INFO("a counter read after keeps the while");
            CHECK(has(r, "while f < 3 do"));
            CHECK(has(r, "local f = 0"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: integers only used as truths become booleans; number reads keep numbers", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gOn = FALSE;\n"
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
        CAPTURE(r.text);
        {
            INFO("a global toggled");
            CHECK(has(r, "local gOn = false"));
            CHECK(has(r, "gOn = not gOn"));
            CHECK(has(r, "if gOn then"));
        }
        {
            INFO("compared with FALSE");
            CHECK(has(r, "local gSeen = false"));
            CHECK(has(r, "if not gSeen and isOwner("));
            CHECK(has(r, "gSeen = true"));
        }
        UNSCOPED_INFO("a function that answers one");
        CHECK(has(r, "return k == ll.GetOwner()"));
        {
            INFO("a length, as a truth");
            CHECK(has(r, "return #l ~= 0"));
            CHECK(has(r, "local busy = anyOf({ 1 })"));
            CHECK(has(r, "while not busy do"));
        }
        {
            INFO("stepped");
            CHECK(has(r, "local count = 0"));
            CHECK(has(r, "count += 1"));
        }
        UNSCOPED_INFO("printed");
        CHECK(has(r, "local shown = 1"));
        {
            INFO("more than TRUE, compared with it");
            CHECK(has(r, "local loose = 5"));
            CHECK(has(r, "if loose == 1 then"));
        }
        UNSCOPED_INFO("set where the assignment is read");
        CHECK(has(r, "local y = 0"));
        require_checks_clean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("integer ready(integer n) { return n > 2; }\n"
                                                           "default { state_entry() { integer ok = ready(3); if (ok) llOwnerSay(\"yes\"); } }\n",
                                                           typed);
        CAPTURE(t.text);
        {
            INFO("typed boolean");
            CHECK(has(t, "local function ready(n: number): boolean"));
            CHECK(has(t, "local ok: boolean = ready(3)"));
        }
        require_checks_clean(t);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: the right-first memory hack is an append; a visible order is noted", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gItems;\n"
                                                        "string gLog = \"a\";\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    gItems = (gItems = []) + gItems + [\"x\"];\n"
                                                        "    gItems = (gItems = []) + [\"y\"] + gItems;\n"
                                                        "    gLog = (gLog = \"\") + gLog + \"b\";\n"
                                                        "    llOwnerSay(gLog + (string)llGetListLength(gItems));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("appended");
        CHECK(has(r, "table.insert(gItems, \"x\")"));
        UNSCOPED_INFO("prepended");
        CHECK(has(r, "table.insert(gItems, 1, \"y\")"));
        UNSCOPED_INFO("a string");
        CHECK(has(r, "gLog ..= \"b\""));
        {
            INFO("nothing cleared first");
            CHECK_FALSE(has(r, "gItems = {} return"));
            CHECK_FALSE(has(r, "gLog = \"\" return"));
        }
        {
            INFO("noted once for each variable");
            CHECK(noted(r, "SluaMemoryHack"));
            CHECK(count(r, "-- LSL: (gItems = []) + gItems") == 1);
            CHECK(count(r, "-- LSL: (gLog = \"\") + gLog") == 1);
        }
        UNSCOPED_INFO("not taken as an order that shows");
        CHECK_FALSE(orderNoted(r));
        require_checks_clean(r);

        const ALLSLToSLua::Result order = require_converted("integer gCount;\n"
                                                            "integer bump() { return ++gCount; }\n"
                                                            "default { state_entry() {\n"
                                                            "    integer i = 1;\n"
                                                            "    integer j = i + (i = 5);\n"
                                                            "    integer k = gCount - bump();\n"
                                                            "    llOwnerSay((string)(j + k));\n"
                                                            "} }\n");
        CAPTURE(order.text);
        UNSCOPED_INFO("an assignment the other side reads, named");
        CHECK(noted(order, "SluaRightFirstSet", "i"));
        UNSCOPED_INFO("a call that sets what the other side reads, named");
        CHECK(noted(order, "SluaRightFirstCall", "bump()"));
        UNSCOPED_INFO("said over its line, with what");
        CHECK(has(order, "-- LSL: LSL ran the right side of this before the left, and Luau "
                         "runs the left first: bump() sets gCount, which the other side "
                         "reads or sets too."));
        UNSCOPED_INFO("said over its line");
        CHECK(has(order, "-- LSL: LSL ran the right side of this before the left"));
        require_checks_clean(order);

        const ALLSLToSLua::Result quiet = require_converted("integer gCount;\n"
                                                            "integer bump() { return ++gCount; }\n"
                                                            "default { state_entry() {\n"
                                                            "    integer i = 1;\n"
                                                            "    integer j = (i + 1) * bump();\n"
                                                            "    string s = \"n: \" + (string)bump();\n"
                                                            "    llOwnerSay(s + (string)(j + (integer)llGetTime()));\n"
                                                            "} }\n");
        CAPTURE(quiet.text);
        UNSCOPED_INFO("nothing either side changes the other reads");
        CHECK_FALSE(orderNoted(quiet));
        require_checks_clean(quiet);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: unshared lists grown in place; a list handed on copied and noted", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gAll;\n"
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
        CAPTURE(r.text);
        {
            INFO("a local returned, grown in place");
            CHECK(has(r, "local out = ll.ParseString2List(s, { \",\" }, {})"));
            CHECK(has(r, "table.insert(out, \"end\")"));
        }
        {
            INFO("a call's list, put on the end");
            CHECK(has(r, "gAll = parts(\"a,b\")"));
            CHECK(has(r, "table.extend(gAll, parts(\"c\"))"));
        }
        UNSCOPED_INFO("prepended");
        CHECK(has(r, "table.insert(gAll, 1, \"first\")"));
        UNSCOPED_INFO("a list by its name");
        CHECK(has(r, "table.extend(gAll, more)"));
        UNSCOPED_INFO("several that do not run apart, each had before any is added");
        CHECK(has(r, "table.append(gAll, ll.GetTime(), ll.Frand(1.0))"));
        UNSCOPED_INFO("passed to a function that only reads it");
        CHECK(has(r, "table.insert(gSeen, n)"));
        {
            INFO("passed to one that keeps it");
            CHECK(has(r, "gPassed = table.extend(table.clone(gPassed), { n })"));
            CHECK(has(r, "-- LSL: LSL's lists were values, and gPassed is passed to a function of the script's that keeps it"));
        }
        {
            INFO("returned");
            CHECK(has(r, "gKept = table.extend(table.clone(gKept), { 2 })"));
            CHECK(has(r, "gKept is returned by a function"));
        }
        {
            INFO("given to another");
            CHECK(has(r, "a = table.extend(table.clone(a), { 2 })"));
            CHECK(has(r, "a is given to another variable"));
        }
        {
            INFO("read where a call in the statement grows it");
            CHECK(has(r, "gGrown = table.extend(table.clone(gGrown), { 1 })"));
            CHECK(has(r, "gGrown is read where a call of the script's in the same statement changes it"));
        }
        UNSCOPED_INFO("said once for each");
        CHECK(count(r, "and gPassed is") == 1);
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: strings built in loops joined once from a table; noted where not", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("string gReport;\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("the outer loop's table, a name of its own");
        CHECK(has(r, "local outParts2 = {}\n    for _, item in names do"));
        UNSCOPED_INFO("the pieces put in");
        CHECK(has(r, "table.insert(outParts2, item .. row)"));
        UNSCOPED_INFO("joined after");
        CHECK(has(r, "    end\n    out ..= table.concat(outParts2)"));
        UNSCOPED_INFO("the inner loop's, declared in the outer");
        CHECK(has(r, "        local rowParts = {}\n        for j = 0, 2 do\n            table.insert(rowParts, tostring(j))"));
        {
            INFO("declared empty just before it: declared where it is joined");
            CHECK(has(r, "        end\n        local row = table.concat(rowParts)"));
            CHECK_FALSE(has(r, "local row = \"\""));
        }
        {
            INFO("declared with words of its own: added to");
            CHECK(has(r, "local out = \"Names: \""));
            CHECK(has(r, "out ..= table.concat(outParts2)"));
        }
        {
            INFO("read in its loop: noted");
            CHECK(has(r, "seen ..= \"x\""));
            CHECK(has(r, "-- LSL: seen is built with .. in a loop"));
        }
        {
            INFO("a global: noted");
            CHECK(has(r, "gReport ..= \"y\""));
            CHECK(has(r, "-- LSL: gReport is built with .. in a loop"));
        }
        require_checks_clean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("default { state_entry() { string s; integer i; for (i = 0; i < 3; ++i) s += \"z\"; llOwnerSay(s); } }\n",
                                                           typed);
        CAPTURE(t.text);
        CHECK(has(t, "local sParts: { string } = {}"));
        CHECK(has(t, "local s: string = table.concat(sParts)"));
        require_checks_clean(t);

        const ALLSLToSLua::Result read = require_converted("default { state_entry() { string t = \"\"; llOwnerSay(t); integer i; for (i = 0; i < 3; ++i) t += \"q\"; llOwnerSay(t); } }\n");
        CAPTURE(read.text);
        {
            INFO("read between: declared where it was");
            CHECK(has(read, "local t = \"\""));
            CHECK(has(read, "t ..= table.concat(tParts)"));
        }
        require_checks_clean(read);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: compound assignments: x op= y; integer /= and %= whole; vector %= cross", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    integer a = 7; float f = 3.0; vector v = <1, 0, 0>;\n"
                                                        "    a /= 2; a %= 3; f /= 2.0; v %= <0, 1, 0>;\n"
                                                        "    integer b = 1; integer c = 2; string s = \"x\";\n"
                                                        "    b = b + c; b = b - (c - 1); b = b - c - 1; c = c * 2 + b; s = s + \"y\" + (string)b;\n"
                                                        "    llOwnerSay((string)a + (string)f + (string)v + s + (string)(b + c));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("integer division");
            CHECK(has(r, "a = bit32.s32(a / 2)"));
            CHECK_FALSE(has(r, "//="));
        }
        {
            INFO("remainder");
            CHECK(has(r, "a = math.fmod(a, 3)"));
            CHECK_FALSE(has(r, "%="));
        }
        UNSCOPED_INFO("a float's");
        CHECK(has(r, "f /= 2.0"));
        UNSCOPED_INFO("a vector's cross product");
        CHECK(has(r, "v = vector.cross(v, vector(0, 1, 0))"));
        {
            INFO("x = x op y");
            CHECK(has(r, "b += c"));
            CHECK(has(r, "b -= c - 1"));
        }
        {
            INFO("not where the left of the operator is more than x");
            CHECK(has(r, "b = b - c - 1"));
            CHECK(has(r, "c = c * 2 + b"));
        }
        UNSCOPED_INFO("a string's pieces, joined");
        CHECK(has(r, "s ..= `y{b}`"));
        UNSCOPED_INFO("a float's and a vector's as LSL writes them, in one interpolated string");
        CHECK(has(r, "print(`{a}{string.format(\"%.6f\", f)}{ll.DumpList2String({ v }, \"\")}{s}{b + c}`)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a minus before a negative number is bracketed and not a comment", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() { integer a = -5; llOwnerSay((string)(-(-2147483648)) + (string)(-a) + (string)(- -a)); } }\n");
        CAPTURE(r.text);
        {
            INFO("bracketed");
            CHECK(has(r, "-(-2147483648)"));
            CHECK_FALSE(has(r, "--2147483648"));
        }
        UNSCOPED_INFO("a name as it was");
        CHECK(has(r, "}{-a}{"));
        UNSCOPED_INFO("twice");
        CHECK(has(r, "-(-a)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a one-state script's state_exit never ran: left out and said", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() { llOwnerSay(\"in\"); } state_exit() { llOwnerSay(\"out\"); } touch_start(integer n) { } }\n");
        CAPTURE(r.text);
        {
            INFO("not an event");
            CHECK_FALSE(has(r, "\"state_exit\""));
            CHECK_FALSE(has(r, "print(\"out\")"));
        }
        {
            INFO("said where it was");
            CHECK(noted(r, "SluaStateExit"));
            CHECK(has(r, "-- LSL: state_exit runs as a script leaves a state"));
            CHECK(has(r, "-- state_exit, left out"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a function SLua lacks left out and said; boolean ll lists kept on llcompat", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    llPointAt(ZERO_VECTOR);\n"
                                                        "    list e = llGetExperienceList(NULL_KEY);\n"
                                                        "    if (llList2Integer(llGetPrimitiveParams([PRIM_FULLBRIGHT, 0]), 0)) llOwnerSay((string)llGetListLength(e));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("left out");
            CHECK_FALSE(has(r, "PointAt("));
            CHECK(has(r, "-- llPointAt, left out"));
            CHECK(noted(r, "SluaAbsent", "llPointAt"));
        }
        {
            INFO("its empty value where read");
            CHECK(has(r, "local e = {}"));
            CHECK(noted(r, "SluaAbsent", "llGetExperienceList"));
        }
        {
            INFO("llcompat, said");
            CHECK(has(r, "llcompat.GetPrimitiveParams({ PRIM_FULLBRIGHT, 0 })"));
            CHECK(noted(r, "SluaBoolList", "GetPrimitiveParams"));
        }
        UNSCOPED_INFO("not a truth");
        CHECK_FALSE(has(r, "if llcompat.GetPrimitiveParams"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: NULL_KEY and such constants: a uuid as a key and a string as text", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("string gName = NULL_KEY;\n"
                                                        "key gKey = NULL_KEY;\n"
                                                        "string nothing() { return NULL_KEY; }\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    key k = llDetectedKey(0);\n"
                                                        "    if (k != NULL_KEY) llOwnerSay(\"someone\");\n"
                                                        "    if (NULL_KEY != \"\") llOwnerSay(llGetSubString(NULL_KEY, 0, 7));\n"
                                                        "    list l = [NULL_KEY, TEXTURE_BLANK];\n"
                                                        "    if (NULL_KEY) llOwnerSay(gName + nothing() + (string)gKey + (string)llGetListLength(l));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a key against a key");
        CHECK(has(r, "if k ~= NULL_KEY then"));
        {
            INFO("a string where LSL's string was");
            CHECK(has(r, "tostring(NULL_KEY) ~= \"\""));
            CHECK(has(r, "ll.GetSubString(tostring(NULL_KEY), 1, 8)"));
        }
        UNSCOPED_INFO("in a list, as LSL had it");
        CHECK(has(r, "{ tostring(NULL_KEY), tostring(TEXTURE_BLANK) }"));
        UNSCOPED_INFO("a string's truth");
        CHECK(has(r, "if tostring(NULL_KEY) ~= \"\" then print("));
        {
            INFO("given to a string and a key");
            CHECK(has(r, "local gName = tostring(NULL_KEY)"));
            CHECK(has(r, "local gKey = NULL_KEY"));
        }
        UNSCOPED_INFO("returned as a string function's");
        CHECK(has(r, "return tostring(NULL_KEY)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: Tailslide's test scripts convert and check clean or are refused", "[ALLSLToSLua]")
    {
        // Those Tailslide says are wrong LSL, by design.
        static const std::set<std::string> REFUSED = { "bad_globals.lsl", "bugs/0002.lsl", "bugs/0003.lsl", "bugs/0006.lsl", "bugs/0007.lsl",
                                                          "bugs/0010.lsl", "bugs/0015.lsl", "bugs/0017.lsl", "bugs/0019.lsl", "bugs/bad-for-exprs.lsl",
                                                          "bugs/typecast_builtin.lsl", "bugs/unary_minus_glob_bad_sym.lsl", "compound_assignment.lsl", "constants.lsl", "declaration_expressions.lsl",
                                                          "error1.lsl", "events.lsl", "illegal_cast.lsl", "invalid_decl.lsl", "list_append_void.lsl",
                                                          "logmessage_test.lsl", "nested_lists.lsl", "parserstackdepth3.lsl", "pathological_expression.lsl", "precluded_globals.lsl",
                                                          "print_no_shadowing.lsl", "print_type_bug.lsl", "rvalue_assignments.lsl", "scope1.lsl", "scope2.lsl",
                                                          "scope4.lsl", "type_error_no_assert.lsl", "void_return.lsl" };
        // Those Tailslide parses but nested deeper than the studio walks a
        // tree (allsldepth.h): refused with that said.
        static const std::set<std::string> TOO_DEEP = { "parserstackdepth2.lsl" };
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
        CAPTURE(dir);
        REQUIRE(files.size() > 100);
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
            if (TOO_DEEP.contains(name))
            {
                const bool said = !r.problems.empty() && ALLSLDepth::said(r.problems.front().message);
                wrong += r.converted || !said ? name + ": not refused as too deep\n" : "";
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
        CAPTURE(wrong);
        CHECK(wrong.empty());
        UNSCOPED_INFO("those taken, converted");
        CHECK(converted + REFUSED.size() + TOO_DEEP.size() == files.size());
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: deprecated ll: SLua's own way where it means the same or llcompat noted", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    string s = \"a:b\"; list l = [\"x\", 2];\n"
                                                        "    integer at = llSubStringIndex(s, \":\");\n"
                                                        "    integer n = llStringLength(s) + llRound(1.5);\n"
                                                        "    llOwnerSay(llList2String(l, at) + (string)(at + n));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("an index read as LSL's");
        CHECK(has(r, "local at = (ll.SubStringIndex(s, \":\") or 0) - 1"));
        UNSCOPED_INFO("utf8.len and half up");
        CHECK(has(r, "local n = (utf8.len(s) :: number) + math.floor(1.5 + 0.5)"));
        {
            INFO("llcompat, with SLua's word");
            CHECK(has(r, "llcompat.List2String(l, at)"));
            CHECK(has(r, "-- LSL: SLua deprecates ll.List2String: Use '[]' and 'tostring' instead."));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: keys that hold text stay strings: link_message's id and non-UUID text", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("key DOMAIN = \"MY CHANNEL\";\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("a key given text, a string");
        CHECK(has(r, "local DOMAIN = \"MY CHANNEL\""));
        {
            INFO("text where SLua takes it");
            CHECK(has(r, "ll.MessageLinked(LINK_SET, 1, \"hi\", DOMAIN)"));
            CHECK(has(r, "ll.MessageLinked(LINK_SET, 2, \"x\", gOwner)"));
            CHECK(has(r, "send(\"other text\")"));
            CHECK(has(r, "ll.MessageLinked(LINK_SET, 0, \"\", to)"));
        }
        {
            INFO("compared as text");
            CHECK(has(r, "if id == DOMAIN then"));
            CHECK(has(r, "if id == tostring(gOwner) then"));
        }
        {
            INFO("a key given the id, text");
            CHECK(has(r, "local k = id"));
            CHECK(has(r, "{k}`)"));
        }
        {
            INFO("ll's key given text as it is, said");
            CHECK(has(r, "ll.GetOwnerKey(\"abc\" :: any)"));
            CHECK(noted(r, "SluaKeyAsText"));
            CHECK_FALSE(noted(r, "SluaKeyText"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: notes only where they say something: &&; deprecations; bit32's range", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer sayIt() { llOwnerSay(\"x\"); return 1; }\n"
                                                        "default { changed(integer what) {\n"
                                                        "    if (what & CHANGED_LINK && llGetLinkNumber() == 0) llDie();\n"
                                                        "    integer m = what | 0x80000000;\n"
                                                        "    list l = [\"a\"];\n"
                                                        "    llOwnerSay(llList2String(l, what) + (string)m);\n"
                                                        "    if ((what & 3) && sayIt()) llDie();\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("bit32's range made LSL's where it shows");
            CHECK(has(r, "local m = bit32.s32(bit32.bor(what, "));
            CHECK(count(r, "-- LSL: bit32") == 1);
        }
        UNSCOPED_INFO("not for a flag's truth");
        CHECK(has(r, "if bit32.btest(what, CHANGED_LINK)"));
        {
            INFO("&& said once, over a call that does something");
            CHECK(count(r, "leaves its right side unrun") == 1);
            CHECK(has(r, "LSL ran both sides, the right one first.\n    if bit32.btest(what, 3) and sayIt()"));
        }
        {
            INFO("the deprecated call's reason only");
            CHECK(has(r, "-- LSL: SLua deprecates ll.List2String"));
            CHECK_FALSE(has(r, "takes indexes from 0"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: flatter expressions: joined text unbracketed; one bit32 call per chain", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    string b = \"b\"; integer n = 3; integer f = 5;\n"
                                                        "    llOwnerSay(\"a\" + b + \"c\" + (string)n);\n"
                                                        "    llSetTextureAnim(ANIM_ON | ROTATE | LOOP, ALL_SIDES, 0, 0, 0, TWO_PI, 1.0);\n"
                                                        "    llOwnerSay((string)((f | 2) & n));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a chain, one string");
        CHECK(has(r, "print(`a{b}c{n}`)"));
        UNSCOPED_INFO("one call");
        CHECK(has(r, "ll.SetTextureAnim(bit32.bor(ANIM_ON, ROTATE, LOOP), ALL_SIDES"));
        UNSCOPED_INFO("not across operators");
        CHECK(has(r, "bit32.band(bit32.bor(f, 2), n)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: steps as statements before or after theirs unless something reads x", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gLine;\n"
                                                        "integer bump() { return ++gLine; }\n"
                                                        "default { state_entry() {\n"
                                                        "    integer n = 0;\n"
                                                        "    llSay(n++, \"a\");\n"
                                                        "    integer m = ++n;\n"
                                                        "    integer k = n++ + n;\n"
                                                        "    llOwnerSay((string)(gLine++));\n"
                                                        "    llOwnerSay((string)bump() + (string)(gLine++) + (string)(m + k));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("after");
        CHECK(has(r, "ll.Say(n, \"a\")\nn += 1\n"));
        UNSCOPED_INFO("before");
        CHECK(has(r, "\nn += 1\nlocal m = n\n"));
        UNSCOPED_INFO("read again, kept");
        CHECK(has(r, "local k = (function() local was = n; n += 1 return was end)() + n"));
        UNSCOPED_INFO("a global with nothing of the script's called");
        CHECK(has(r, "print(tostring(gLine))\ngLine += 1\n"));
        UNSCOPED_INFO("a global beside a call of the script's, kept");
        CHECK(has(r, "print(`{bump()}{(function() local was = gLine;"));
        UNSCOPED_INFO("a return's step before it");
        CHECK(has(r, "    gLine += 1\n    return gLine\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: simpler conditions: negated comparisons flipped; if-set booleans direct", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gOn;\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    if (!(n == 2)) llOwnerSay(\"not two\");\n"
                                                        "    if (!(n != 3)) llOwnerSay(\"three\");\n"
                                                        "    if (n > 1) gOn = TRUE; else gOn = FALSE;\n"
                                                        "    if (n > 5) { gOn = FALSE; } else { gOn = TRUE; }\n"
                                                        "    if (gOn) llOwnerSay(\"on\");\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("flipped");
            CHECK(has(r, "if n ~= 2 then"));
            CHECK(has(r, "if n == 3 then"));
        }
        const ALLSLToSLua::Result nots = require_converted("integer gOn;\n"
                                                           "default { touch_start(integer n) {\n"
                                                           "    if (!n) llOwnerSay(\"none\"); if (!(n & 4)) llOwnerSay(\"no four\"); if (!(n > 1)) llDie();\n"
                                                           "    gOn = !gOn; if (gOn) llOwnerSay(\"on\");\n"
                                                           "} }\n");
        CAPTURE(nots.text);
        {
            INFO("not a number: the other way round");
            CHECK(has(nots, "if n == 0 then"));
            CHECK(has(nots, "if not bit32.btest(n, 4) then"));
            CHECK(has(nots, "if not (n > 1) then"));
            CHECK(has(nots, "gOn = not gOn"));
        }
        require_checks_clean(nots);
        {
            INFO("the check itself");
            CHECK(has(r, "    gOn = n > 1\n"));
            CHECK(has(r, "    gOn = not (n > 5)\n"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a non-negative counter moved on by one as an index; loops count from 1", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    integer n = llGetInventoryNumber(INVENTORY_SOUND);\n"
                                                        "    integer i; for (i = 0; i < n; ++i) llOwnerSay(llGetInventoryName(INVENTORY_SOUND, i));\n"
                                                        "    integer j; for (j = 0; j < n; ++j) llOwnerSay((string)j + llGetInventoryName(INVENTORY_SOUND, j));\n"
                                                        "    integer k; for (k = n - 1; k >= 0; --k) llOwnerSay(llGetInventoryName(INVENTORY_SOUND, k));\n"
                                                        "    string s = \"abcdef\";\n"
                                                        "    integer c; for (c = 0; c <= 2; ++c) llOwnerSay(llGetSubString(s, c, c + 1));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("counting from 1");
        CHECK(has(r, "for i = 1, n do\n    print(ll.GetInventoryName(INVENTORY_SOUND, i))"));
        UNSCOPED_INFO("read as a number too, moved on by one");
        CHECK(has(r, "for j = 0, n - 1 do\n    print(`{j}{ll.GetInventoryName(INVENTORY_SOUND, j + 1)}`)"));
        UNSCOPED_INFO("down to nought");
        CHECK(has(r, "for k = n - 1, 0, -1 do\n    print(ll.GetInventoryName(INVENTORY_SOUND, k + 1))"));
        UNSCOPED_INFO("to and with, a number added");
        CHECK(has(r, "for c = 1, 3 do\n    print(ll.GetSubString(s, c, c + 1))"));
        UNSCOPED_INFO("no llcompat");
        CHECK_FALSE(has(r, "llcompat."));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: list items read by index directly or through tostring; else llcompat", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gNames = [\"a\", \"b\"];\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("from the start, empty past the end");
        CHECK(has(r, "(gNames[1] or \"\")"));
        UNSCOPED_INFO("back from the end, through a function's list");
        CHECK(has(r, "(p[#p] or \"\")"));
        UNSCOPED_INFO("keys and text made text");
        CHECK(has(r, "tostring(keys[2] or \"\")"));
        {
            INFO("a float's text is not tostring's, and an index not known: llcompat");
            CHECK(has(r, "llcompat.List2String(mixed, 2)"));
            CHECK(has(r, "llcompat.List2String(gNames, n)"));
        }
        UNSCOPED_INFO("mixed items for a number: llcompat");
        CHECK(has(r, "llcompat.List2Integer(mixed, 0)"));
        UNSCOPED_INFO("within its length, as it is, counting from 1");
        CHECK(has(r, "for i = 1, #p do\n    print(p[i] .. ll.GetSubString(\"abc\", i, i))"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: unshared lists changed at either end in place; elsewhere a new list", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gQueue;\n"
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
        CAPTURE(r.text);
        {
            INFO("in place");
            CHECK(has(r, "table.insert(gQueue, 1, n + 1)"));
            CHECK(has(r, "table.remove(gQueue, 1)"));
            CHECK(has(r, "table.remove(gQueue)\n"));
        }
        UNSCOPED_INFO("elsewhere in the list, as LSL did");
        CHECK(has(r, "gQueue = llcompat.DeleteSubList(gQueue, 1, 1)"));
        UNSCOPED_INFO("one held elsewhere, as LSL did");
        CHECK(has(r, "gShared = llcompat.DeleteSubList(gShared, 0, 0)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: notes are keyed with their words and lint; the studio's words used", "[ALLSLToSLua]")
    {
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
        CHECK(r.converted);
        std::string listed;
        for (const ALScriptProblem& p : r.notes)
        {
            listed += p.key + "(" + (p.args.empty() ? std::string() : p.args[0]) + ") " + p.code + "\n";
        }
        CAPTURE(listed);
        // Once for each function, with the lint where there is one.
        UNSCOPED_INFO("an index");
        CHECK(noted(r, "SluaIndex", "GetSubString"));
        size_t indexes = 0;
        for (const ALScriptProblem& p : r.notes)
        {
            indexes += p.key == "SluaIndex";
            const bool compat = p.key == "SluaIndex" || p.key == "SluaIndexFound" || p.key == "SluaBool";
            CAPTURE(p.key);
            REQUIRE((compat ? p.code == "SlCompatCall" : p.code.empty()));
            UNSCOPED_INFO("no mark left unfilled");
            REQUIRE(p.message == ALScriptProblem::fill(p.message, p.args));
        }
        UNSCOPED_INFO("one index note for one function");
        CHECK(indexes == size_t(1));
        {
            INFO("a found index and a boolean");
            CHECK(noted(r, "SluaIndexFound", "SubStringIndex"));
            CHECK(noted(r, "SluaBool", "SameGroup"));
        }
        UNSCOPED_INFO("words filled");
        CHECK(has(r, "-- LSL: llcompat.GetSubString takes indexes from 0, as LSL did; ll.GetSubString takes them from 1."));
        {
            INFO("the lint by key");
            CHECK(std::string(ALLSLToSLua::lintOf("SluaIndex")) == "SlCompatCall");
            CHECK_FALSE(ALLSLToSLua::lintOf("SluaListCompare"));
        }

        // The studio's words: each comment's, each note's, and the header's,
        // a line at a time.
        close.words = [](const std::string& key, const std::vector<std::string>& args, const std::string&) {
            return key == "SluaHeader" ? std::string("first\nsecond") : "<" + key + (args.empty() ? "" : " " + args[0]) + ">";
        };
        const ALLSLToSLua::Result said = ALLSLToSLua::convert(lsl, close);
        CAPTURE(said.text);
        UNSCOPED_INFO("the header's");
        CHECK(said.text.rfind("-- first\n-- second\n\n", 0) == 0);
        UNSCOPED_INFO("a comment's");
        CHECK(has(said, "-- LSL: <SluaIndex GetSubString>\n"));
        {
            INFO("a note's");
            CHECK(noted(said, "SluaIndex", "GetSubString"));
            CHECK([&] {
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
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: LSL comments read back as notes with Done and the lint's fix", "[ALLSLToSLua]")
    {
        CHECK(definitions);
        const std::string lsl = "default { state_entry() { string s = \"abc\"; llOwnerSay(llGetSubString(s, 0, 2)); } }";
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CAPTURE(r.text);
        CHECK(r.converted);
        ALScriptProblems notes = ALLSLToSLua::notesIn(r.text, nullptr);
        {
            INFO("as many as the converter noted");
            REQUIRE(notes.size() == r.notes.size());
            REQUIRE_FALSE(notes.empty());
        }
        ALScriptProblem* index = nullptr;
        for (ALScriptProblem& note : notes)
        {
            REQUIRE(note.key == "SluaNote");
            REQUIRE(note.severity == ALScriptProblem::Severity::Note);
            REQUIRE(note.fixes.size() == 1);
            REQUIRE(note.fixes[0].safe);
            REQUIRE(note.fixes[0].removes);
            REQUIRE_FALSE(note.fixes[0].preferred);
            index = note.message.rfind("llcompat.GetSubString takes indexes", 0) == 0 ? &note : index;
        }
        {
            INFO("the index's, the lint's");
            REQUIRE(index);
            CHECK(index->code == "SlCompatCall");
        }
        const size_t at = index - notes.data();
        ALLSLToSLua::linkNotes(notes, service.check(r.text), r.text);
        const ALScriptProblem& linked = notes[at];
        {
            INFO("the lint's fix first, then Done");
            REQUIRE(linked.fixes.size() == 2);
            CHECK(linked.fixes[0].preferred);
            CHECK(linked.fixes[0].title == "Write it ll.GetSubString(s, 1, 3), and take the note out");
            CHECK(linked.fixes[1].title == "Done: take the note out");
        }
        const std::optional<std::string> made = ALScriptFixes::apply(r.text, linked.fixes[0]);
        {
            INFO("made: ll's, the note gone");
            REQUIRE(made);
            CHECK(made->find("ll.GetSubString(s, 1, 3)") != std::string::npos);
            CHECK(made->find("llcompat.GetSubString takes indexes") == std::string::npos);
        }
        const std::optional<std::string> done = ALScriptFixes::apply(r.text, linked.fixes[1]);
        {
            INFO("done: the line gone, the call kept");
            REQUIRE(done);
            CHECK(done->find("llcompat.GetSubString takes indexes") == std::string::npos);
            CHECK(done->find("llcompat.GetSubString(s, 0, 2)") != std::string::npos);
            CHECK(std::count(done->begin(), done->end(), '\n') + 1 == std::count(r.text.begin(), r.text.end(), '\n'));
        }

        // Written in the studio's words, read back in them.
        ALLSLToSLua::Options close = ALLSLToSLua::Options::closeToLSL();
        close.words = [](const std::string& key, const std::vector<std::string>& args, const std::string& english) {
            return ALScriptProblem::fill(key == "SluaIndex" ? "Index de [1]" : english, args);
        };
        const ALLSLToSLua::Result said = ALLSLToSLua::convert(lsl, close);
        UNSCOPED_INFO("written so");
        CHECK(has(said, "-- LSL: Index de GetSubString\n"));
        const auto linted = [](const ALScriptProblems& read) {
            return std::count_if(read.begin(), read.end(), [](const ALScriptProblem& note) { return note.code == "SlCompatCall"; });
        };
        UNSCOPED_INFO("read in the studio's words");
        CHECK(linted(ALLSLToSLua::notesIn(said.text, close.words)) == 1);
        UNSCOPED_INFO("not in English's");
        CHECK(linted(ALLSLToSLua::notesIn(said.text, nullptr)) == 0);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: parameters only read as truths are booleans; others and events numbers", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("say(integer loud, string text) { if (loud) llShout(0, text); else llSay(0, text); }\n"
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
        CAPTURE(r.text);
        {
            INFO("read only as a truth: a boolean");
            CHECK(has(r, "local function say(loud, text)"));
            CHECK(has(r, "if loud then"));
        }
        {
            INFO("each call giving one, a number as its truth");
            CHECK(has(r, "say(true, \"hi\")"));
            CHECK(has(r, "say(n > 1, \"many\")"));
            CHECK(has(r, "say(n ~= 0, \"some\")"));
        }
        {
            INFO("not, as a truth");
            CHECK(has(r, "if not on then"));
            CHECK(has(r, "glow(false)"));
        }
        {
            INFO("read as a number: kept one");
            CHECK(has(r, "return n + 1"));
            CHECK(has(r, "next_(n)"));
        }
        {
            INFO("given 5 where asked about TRUE: kept a number");
            CHECK(has(r, "f == 1"));
            CHECK(has(r, "exactly(5)"));
        }
        UNSCOPED_INFO("an event's: the grid's number");
        CHECK(has(r, "if p ~= 0 then"));
        require_checks_clean(r);

        ALLSLToSLua::Options typed;
        typed.types = true;
        const ALLSLToSLua::Result t = ALLSLToSLua::convert("say(integer loud, string text) { if (loud) llSay(0, text); }\n"
                                                           "default { state_entry() { say(TRUE, \"hi\"); } }\n",
                                                           typed);
        CAPTURE(t.text);
        CHECK(has(t, "local function say(loud: boolean, text: string)"));
        require_checks_clean(t);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a counting loop from 0 whose counter only indexes walks the items", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gNames = [\"a\", \"b\"];\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("walked");
        CHECK(has(r, "for _, item in gNames do\n    print(item)\n"));
        UNSCOPED_INFO("one inside another, each its own name, a key's made text");
        CHECK(has(r, "for _, item2 in keys do\n        print(item .. tostring(item2))"));
        UNSCOPED_INFO("from the second: counted");
        CHECK(has(r, "for m = 2, #gNames do\n    print(gNames[m])"));
        UNSCOPED_INFO("its counter read as a number too: counted");
        CHECK(has(r, "for n = 0, #gNames - 1 do"));
        require_checks_clean(r);

        const ALLSLToSLua::Result named = require_converted("list gNames = [\"a\"];\n"
                                                            "integer _ = 2;\n"
                                                            "default { state_entry() {\n"
                                                            "    integer i;\n"
                                                            "    for (i = 0; i < llGetListLength(gNames); ++i) llOwnerSay(llList2String(gNames, i) + (string)_);\n"
                                                            "} }\n");
        CAPTURE(named.text);
        UNSCOPED_INFO("an index that would hide the script's _ named afresh");
        CHECK(has(named, "for _2, item in gNames do"));
        require_checks_clean(named);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: strings built in loops: two loops share one; one read in its loop kept", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("declared at the first loop's join");
        CHECK(has(r, "local s = table.concat(sParts)"));
        {
            INFO("the second adds to it");
            CHECK(has(r, "s ..= table.concat(sParts2)"));
            CHECK_FALSE(has(r, "local s = table.concat(sParts2)"));
        }
        {
            INFO("one read in its loop kept as it is");
            CHECK(has(r, "local line = \"\""));
            CHECK_FALSE(has(r, "table.concat(lineParts)"));
        }
        {
            INFO("the other built from it");
            CHECK(has(r, "table.insert(allParts, line)"));
            CHECK(has(r, "local all = table.concat(allParts)"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: bit32 answers made signed where they can pass 2147483647 and it shows", "[ALLSLToSLua]")
    {
        // A protocol of link messages whose numbers have their top bit set,
        // as a set of scripts may share: before, the mask's answer was never
        // the message's number, and no message was heard.
        const ALLSLToSLua::Result r = require_converted("integer MASK = 0xFFFF0000;\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("the mask's answer signed");
        CHECK(has(r, "if bit32.s32(bit32.band(num, MASK)) == MSG then"));
        UNSCOPED_INFO("the number sent signed");
        CHECK(has(r, "ll.MessageLinked(LINK_SET, bit32.s32(bit32.bor(MSG, FN_RESET)), \"\", \"\")"));
        UNSCOPED_INFO("an & with a constant not below nought as it is");
        CHECK(has(r, "local fn = bit32.band(num, FN_MASK)\n"));
        UNSCOPED_INFO("~ signed");
        CHECK(has(r, "local inverse = bit32.s32(bit32.bnot(num))"));
        UNSCOPED_INFO("<< signed");
        CHECK(has(r, "local high = bit32.s32(bit32.lshift(num, 4))"));
        UNSCOPED_INFO(">> of what may be below nought signed");
        CHECK(has(r, "local low = bit32.s32(bit32.arshift(num, 2))"));
        UNSCOPED_INFO(">> of a constant not below nought as it is");
        CHECK(has(r, "local part = bit32.arshift(FN_MASK, 8)\n"));
        UNSCOPED_INFO("a truth, and what bit32 takes again, as they are");
        CHECK(has(r, "if bit32.btest(bit32.bnot(ll.GetPermissions()), PERMISSION_TRIGGER_ANIMATION) then"));
        {
            INFO("SLua's own bit32.s32, no helper, said once");
            CHECK_FALSE(has(r, "local function int32"));
            CHECK(count(r, "-- LSL: bit32 answers") == 1);
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: comments carried over where they stood with blocks at a safe level", "[ALLSLToSLua]")
    {
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
        const ALLSLToSLua::Result r = require_converted(lsl);
        CAPTURE(r.text);
        const auto                at = [&r](const std::string& text) { return r.text.find(text); };
        UNSCOPED_INFO("the script's own at the top");
        CHECK(has(r, "-- Door script by Someone.\n-- Do as you like with it.\n\n"));
        {
            INFO("over everything, once");
            CHECK(at("-- Door script by Someone.") < at("local SWING"));
            CHECK(count(r, "Door script") == 1);
        }
        UNSCOPED_INFO("a global's, what trailed it still after it");
        CHECK(has(r, "-- How far it swings, in degrees\nlocal SWING = 90.0 -- a right angle\n"));
        UNSCOPED_INFO("those inside a list, on their lines");
        CHECK(has(r, "local NAMES = {\n    \"open\", -- when it is open\n    \"closed\" -- when it is shut\n}\n"));
        UNSCOPED_INFO("a block comment as Luau's");
        CHECK(has(r, "--[[ old code:\ninteger unused() { return 1; }\n]]\n"));
        {
            INFO("over the function it stood over");
            CHECK(at("--[[ old code:") > at("local NAMES"));
            CHECK(at("--[[ old code:") < at("-- Turns the door."));
        }
        UNSCOPED_INFO("a brace's, after the line that opens it");
        CHECK(has(r, "-- Turns the door.\nlocal function swing(by) -- by degrees\n"));
        UNSCOPED_INFO("over a statement");
        CHECK(has(r, "    -- the rotation it turns by\n    local r = "));
        UNSCOPED_INFO("a trailing one, after its statement");
        CHECK(has(r, "    ll.SetLinkPrimitiveParamsFast(LINK_THIS, { PRIM_ROT_LOCAL, r * ll.GetLocalRot() }) -- turn\n"));
        UNSCOPED_INFO("between branches, over the elseif");
        CHECK(has(r, "    -- when it shuts\n    elseif by < 0 then\n"));
        UNSCOPED_INFO("an empty branch's, in it");
        CHECK(has(r, "    if false then\n        --[[ left off ]]\n    elseif by == 0 then\n"));
        UNSCOPED_INFO("at a block's end");
        CHECK(has(r, "    -- done\nend\n"));
        UNSCOPED_INFO("a state's, over its handler");
        CHECK(has(r, "-- on a touch\nLLEvents:on(\"touch_start\""));
        UNSCOPED_INFO("not a block of Luau's");
        CHECK(has(r, "    swing(SWING) -- [[ not a block of Luau's ]]\n"));
        UNSCOPED_INFO("a level nothing in it closes");
        CHECK(has(r, "    --[=[ it ends ]] here ]=]\nend)"));
        require_checks_clean(r);

        ALLSLToSLua::Options none;
        none.keepComments              = false;
        const ALLSLToSLua::Result bare = ALLSLToSLua::convert(lsl, none);
        CAPTURE(bare.text);
        {
            INFO("none where asked");
            CHECK(bare.converted);
            CHECK(bare.text.find("Door script") == std::string::npos);
            CHECK(bare.text.find("degrees") == std::string::npos);
            CHECK(bare.text.find("--[[") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: spans: each LSL line beside the SLua line made of it", "[ALLSLToSLua]")
    {
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
        const ALLSLToSLua::Result r = require_converted(lsl);
        std::vector<std::string>  lines;
        for (size_t from = 0; from <= r.text.size();)
        {
            const size_t cut = std::min(r.text.find('\n', from), r.text.size());
            lines.push_back(r.text.substr(from, cut - from));
            from = cut + 1;
        }
        for (const ALLSLToSLua::Span& span : r.spans)
        {
            CAPTURE(span.lslFirst, span.sluaFirst);
            REQUIRE(span.lslFirst >= 0);
            REQUIRE(span.lslLast < 14);
            REQUIRE(span.lslFirst <= span.lslLast);
            REQUIRE(span.sluaFirst >= 0);
            REQUIRE(span.sluaFirst <= span.sluaLast);
            REQUIRE(span.sluaLast < static_cast<S32>(lines.size()));
        }
        const auto at = [&](S32 lsl_line) -> std::string {
            for (const ALLSLToSLua::Span& span : r.spans)
            {
                if (span.lslFirst == lsl_line)
                {
                    return lines[static_cast<size_t>(span.sluaFirst)];
                }
            }
            return "(none)";
        };
        UNSCOPED_INFO("the global: " + at(0) + "\n" + r.text);
        CHECK(at(0).find("local count") != std::string::npos);
        UNSCOPED_INFO("the function: " + at(1));
        CHECK(at(1).find("function add(") != std::string::npos);
        {
            INFO("its statement: " + at(3));
            CHECK(at(3).find("count") != std::string::npos);
            CHECK(at(3).find("n") != std::string::npos);
        }
        UNSCOPED_INFO("the handler: " + at(7));
        CHECK(at(7).find("touch_start") != std::string::npos);
        UNSCOPED_INFO("a call under its comment, not the comment: " + at(10));
        CHECK(at(10).find("add(1)") != std::string::npos);
        UNSCOPED_INFO("the next: " + at(11));
        CHECK(at(11).find("ll.Say(0") != std::string::npos);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: fewer calls: bit32.btest and table.append; hexadecimal kept where safe", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer MASK = 0x0000FF00;\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("an & as a condition");
        CHECK(has(r, "if bit32.btest(flags, PARCEL_FLAG_ALLOW_CREATE_OBJECTS) then"));
        UNSCOPED_INFO("not of one, an & of &s one call");
        CHECK(has(r, "if not bit32.btest(flags, MASK, 0x300) then"));
        UNSCOPED_INFO("against nought");
        CHECK(has(r, "if not bit32.btest(flags, 4) then"));
        UNSCOPED_INFO("an & as a number is still bit32.band");
        CHECK(has(r, "local masked = bit32.band(flags, MASK)"));
        UNSCOPED_INFO("one bit32 takes, as written, which it reads the same");
        CHECK(has(r, "local top = bit32.s32(bit32.band(flags, 0x80000000))"));
        {
            INFO("values added together");
            CHECK(has(r, "table.append(params, PRIM_LINK_TARGET, LINK_ROOT, PRIM_POSITION, ll.GetPos())"));
            CHECK(has(r, "table.insert(params, n)"));
        }
        {
            INFO("the null key");
            CHECK(has(r, "local gSitter = NULL_KEY"));
            CHECK_FALSE(has(r, "uuid(\"\")"));
        }
        {
            INFO("a float from a string");
            CHECK(has(r, "lslFloat(\"1.5\")"));
            CHECK(has(r, "return llcompat.List2Float({ s }, 0)"));
        }
        UNSCOPED_INFO("hexadecimal kept");
        CHECK(has(r, "local MASK = 0x0000FF00"));
        UNSCOPED_INFO("past 0x7FFFFFFF, which SLua reads as another number, as LSL had it");
        CHECK(has(r, "local HIGH = -268435456"));
        // As the grid's 32-bit hosts read them: strtoul stops at 0xFFFFFFFF.
        {
            INFO("past 32 bits, -1");
            CHECK(has(r, "local BIG = -1\n"));
            CHECK(has(r, "local BIG_HEX = -1\n"));
        }
        {
            INFO("and said");
            CHECK(noted(r, "SluaIntegerPast32Bits", "4294967296"));
            CHECK(noted(r, "SluaIntegerPast32Bits", "0x100000000"));
        }
        {
            INFO("a global only bit32 reads keeps its hexadecimal");
            CHECK(has(r, "local S_MASK = 0xF0000000\n"));
            CHECK(has(r, "bit32.band(flags, S_MASK)"));
        }
        UNSCOPED_INFO("one read as a number does not");
        CHECK(has(r, "local HIGH = -268435456"));
        UNSCOPED_INFO("nor a local, never set, only bit32 reads");
        CHECK(has(r, "local low = 0xFFFF0000\n"));
        // Each said once, over the first place: what SLua reads such a
        // number as, and what bit32.s32 is for.
        const auto times = [&r](const char* key) {
            return std::count_if(r.notes.begin(), r.notes.end(), [key](const ALScriptProblem& p) { return p.key == key; });
        };
        {
            INFO("the hexadecimal's number said once, over the first");
            CHECK(times("SluaHexSign") == 1);
            CHECK(noted(r, "SluaHexSign", "0xF0000000"));
            CHECK(has(r, "-- LSL: SLua reads 0xF0000000 as 4026531840, where LSL wrapped it to -268435456: bit32 takes either the same, so it "
                          "stays as written where only bit32 reads it, and is -268435456 where it is read as a number.\nlocal HIGH = -268435456"));
        }
        {
            INFO("bit32.s32 said once");
            CHECK(times("SluaBit32Signed") == 1);
            CHECK(count(r, "-- LSL: bit32 answers 0 to 4294967295") == 1);
        }
        UNSCOPED_INFO("within them, wrapped");
        CHECK(has(r, "local WRAPPED = -2147483648\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: numeric for by a constant and a shared counter; one forward declaration", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer ACTIVE_PROPS_STRIDE = 3;\n"
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
        CAPTURE(r.text);
        UNSCOPED_INFO("down by the constant's name");
        CHECK(has(r, "for i = #activeProps - ACTIVE_PROPS_STRIDE, 0, -ACTIVE_PROPS_STRIDE do"));
        UNSCOPED_INFO("the same counter's other for");
        CHECK(has(r, "for i = 0, #unattachedProps - 1 do"));
        {
            INFO("the counter's declaration gone");
            CHECK_FALSE(has(r, "local i = 0"));
            CHECK_FALSE(has(r, "while i"));
        }
        {
            INFO("only the one called first declared first");
            CHECK(has(r, "local report\n"));
            CHECK(has(r, "local function setProps("));
            CHECK(has(r, "\nfunction report()"));
            CHECK(has(r, "local function twice("));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result set = require_converted("integer gStep = 2;\n"
                                                          "default { state_entry() { integer i; for (i = 0; i < 10; i += gStep) llOwnerSay((string)i); gStep = 3; } }\n");
        CAPTURE(set.text);
        UNSCOPED_INFO("a step by a global something sets keeps the while");
        CHECK(has(set, "while i < 10 do"));
        require_checks_clean(set);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: comments as laid out: trailing; rules as dashes; the toggle; one-line ifs", "[ALLSLToSLua]")
    {
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
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("a rule of stars");
        CHECK(has(r, "\n-----------------------------------------\n--------------- CONSTANTS ---------------\n"));
        UNSCOPED_INFO("after its statement");
        CHECK(has(r, "local S_MASK: number = -268435456 -- -268435456\n"));
        UNSCOPED_INFO("a rule of slashes");
        CHECK((has(r, "\n---------------------------------------------------------------\n") ||
               has(r, "------------------------- DEBUGGING ---------------------------\n")));
        {
            INFO("the toggle");
            CHECK(has(r, "---[[\nlocal function get("));
            CHECK(has(r, "end\n--]]\n"));
        }
        UNSCOPED_INFO("the toggle the other way");
        CHECK(has(r, "--[[\nstring old() { return \"\"; }\n--]]"));
        UNSCOPED_INFO("an if on one line on one");
        CHECK(has(r, "    if bit32.btest(parcelFlags, PARCEL_FLAG_ALLOW_CREATE_OBJECTS) then return 1 end -- may build\n"));
        UNSCOPED_INFO("one on two lines on three");
        CHECK(has(r, "    if bit32.btest(parcelFlags, 4) then\n        return 2\n    end\n"));
        {
            INFO("after a declaration and a block comment after a step");
            CHECK(has(r, "local n: number = 3 -- three\n"));
            CHECK(has(r, "n += 1 --[[ and one ]]\n"));
        }
        // The lines beside the LSL's past an if put on one line.
        std::vector<std::string> lines;
        for (size_t from = 0; from <= r.text.size();)
        {
            const size_t cut = std::min(r.text.find('\n', from), r.text.size());
            lines.push_back(r.text.substr(from, cut - from));
            from = cut + 1;
        }
        const auto at = [&](S32 lsl_line) -> std::string {
            for (const ALLSLToSLua::Span& span : r.spans)
            {
                if (span.lslFirst == lsl_line && span.sluaFirst >= 0 && span.sluaFirst < static_cast<S32>(lines.size()))
                {
                    return lines[static_cast<size_t>(span.sluaFirst)];
                }
            }
            return "(none)";
        };
        UNSCOPED_INFO("the if on one line beside its line: " + at(15));
        CHECK(at(15).find("then return 1 end") != std::string::npos);
        UNSCOPED_INFO("the next beside its own: " + at(16));
        CHECK(at(16).find("if bit32.btest(parcelFlags, 4) then") != std::string::npos);
        {
            INFO("and those after: " + at(18) + " / " + at(19));
            CHECK(at(18).find("local n: number = 3") != std::string::npos);
            CHECK(at(19).find("n += 1") != std::string::npos);
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: declarations the LSL lined up lined up again and others as written", "[ALLSLToSLua]")
    {
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
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("globals lined up");
        CHECK(has(r, "local S_MASK: number        = -268435456 -- the sign\n"
                     "local ACTIVE_STRIDE: number = 3          -- per prop\n"
                     "local PREFIX: string        = \"ARS#\"     -- linkset data\n"));
        UNSCOPED_INFO("not lined up, not lined up");
        CHECK(has(r, "local a: number = 1\nlocal bee: string = \"b\"\n"));
        UNSCOPED_INFO("locals lined up");
        CHECK(has(r, "\nlocal x: number  = 1\nlocal yy: number = 2.5\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a vector or rotation cast from text bracketed only where needed", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("vector offset;\n"
                                                           "default { state_entry() {\n"
                                                           "    list command = [\"a\", \"<1,2,3>\"];\n"
                                                           "    offset = (vector)llList2String(command, 1);\n"
                                                           "    rotation r = (rotation)llList2String(command, 1);\n"
                                                           "    llOwnerSay((string)((vector)llList2String(command, 1) * 2.0));\n"
                                                           "    llSetPos((vector)llList2String(command, 1));\n"
                                                           "} }\n");
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("assigned as it is");
        CHECK(has(r, "offset = tovector(command[2] or \"\") or ZERO_VECTOR\n"));
        UNSCOPED_INFO("a local's value too");
        CHECK(has(r, "local r = toquaternion(command[2] or \"\") or ZERO_ROTATION\n"));
        UNSCOPED_INFO("an argument too");
        CHECK(has(r, "ll.SetPos(tovector(command[2] or \"\") or ZERO_VECTOR)"));
        UNSCOPED_INFO("an operand bracketed");
        CHECK(has(r, "(tovector(command[2] or \"\") or ZERO_VECTOR) * 2"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: an integer as text interpolated where joined and by tostring alone", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("set(integer keey, string value) { llLinksetDataWrite(\"ARS#\" + (string)keey, value); }\n"
                                                        "default { state_entry() {\n"
                                                        "    integer n = 4; float f = 1.5; key k = llGetOwner();\n"
                                                        "    set(n, (string)n);\n"
                                                        "    llOwnerSay((string)f + \";\" + (string)k + \";\" + (string)(n * 2) + (string)(-n));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("joined, interpolated");
        CHECK(has(r, "ll.LinksetDataWrite(`ARS#{keey}`, value)"));
        UNSCOPED_INFO("alone, still text");
        CHECK(has(r, "set(n, tostring(n))"));
        UNSCOPED_INFO("a float's six places, a key's text, a product bracketed, a negation bare");
        CHECK(has(r, "print(`{string.format(\"%.6f\", f)};{k};{n * 2}{-n}`)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a vector as text with five places as LSL wrote it noted once", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer SEAT_NUM = 0;\n"
                                                        "vector offset = ZERO_VECTOR;\n"
                                                        "set(integer which, string value) { llLinksetDataWrite((string)which, value); }\n"
                                                        "default { state_entry() {\n"
                                                        "    set(7, (string)SEAT_NUM + \";\" + (string)offset);\n"
                                                        "    llOwnerSay((string)ZERO_ROTATION);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("LSL's text kept");
        CHECK(has(r, "set(7, `{SEAT_NUM};{ll.DumpList2String({ offset }, \"\")}`)"));
        UNSCOPED_INFO("a rotation's too");
        CHECK(has(r, "ll.DumpList2String({ ZERO_ROTATION }, \"\")"));
        {
            INFO("said why, once");
            CHECK(noted(r, "SluaVectorText"));
            CHECK(r.text.find("-- LSL: ll.DumpList2String writes") != std::string::npos);
            CHECK(r.text.find("-- LSL: ll.DumpList2String writes") == r.text.rfind("-- LSL: ll.DumpList2String writes"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: text joined with non-strings interpolated and escaped; strings with ..", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    key k = llGetOwner(); integer n = 3; string s = \"x\"; float f = 0.5;\n"
                                                        "    llOwnerSay(\"100% `{a}` \\\\ \\\"q\\\"\\n\" + (string)k);\n"
                                                        "    llOwnerSay(\"n=\" + (string)n + \";\" + s);\n"
                                                        "    llOwnerSay(\"a\" + llToUpper(\"b\" + (string)k) + (string)f);\n"
                                                        "    s = s + \"/\" + (string)k;\n"
                                                        "    string all = \"\";\n"
                                                        "    integer i; for (i = 0; i < n; ++i) all += (string)k + \",\";\n"
                                                        "    llOwnerSay(all + s);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("its text escaped -- a backtick, a brace, a backslash, a line -- and % as it is");
        CHECK(has(r, "print(`100% \\`\\{a}\\` \\\\ \"q\"\\n{k}`)"));
        UNSCOPED_INFO("a number joined too");
        CHECK(has(r, "print(`n={n};{s}`)"));
        UNSCOPED_INFO("strings alone, with ..");
        CHECK(has(r, "print(all .. s)"));
        UNSCOPED_INFO("one inside another's braces");
        CHECK(has(r, "print(`a{ll.ToUpper(`b{k}`)}{string.format(\"%.6f\", f)}`)"));
        UNSCOPED_INFO("joined onto a string");
        CHECK(has(r, "s ..= `/{k}`"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: spans end where their SLua does: blocks at end and statements on a line", "[ALLSLToSLua]")
    {
        const std::string lsl = "default\n"                        // 0
                                "{\n"                              // 1
                                "    touch_start(integer d)\n"     // 2
                                "    {\n"                          // 3
                                "        if (d > 1)\n"             // 4
                                "        {\n"                      // 5
                                "            llSay(0, \"many\");\n" // 6
                                "        }\n"                      // 7
                                "        // after\n"               // 8
                                "        llSay(0, \"one\");\n"     // 9
                                "    }\n"                          // 10
                                "}\n";                             // 11
        const ALLSLToSLua::Result r = require_converted(lsl);
        std::vector<std::string>  lines;
        for (size_t from = 0; from <= r.text.size();)
        {
            const size_t cut = std::min(r.text.find('\n', from), r.text.size());
            lines.push_back(r.text.substr(from, cut - from));
            from = cut + 1;
        }
        // The innermost span starting on an LSL line, and its SLua's first
        // and last lines.
        const auto span = [&](S32 lsl_first) -> const ALLSLToSLua::Span* {
            const ALLSLToSLua::Span* found = nullptr;
            for (const ALLSLToSLua::Span& each : r.spans)
            {
                if (each.lslFirst == lsl_first && (!found || each.lslLast <= found->lslLast))
                {
                    found = &each;
                }
            }
            return found;
        };
        const auto trimmed = [&](S32 line) {
            const std::string& text  = lines[static_cast<size_t>(line)];
            const size_t       first = text.find_first_not_of(' ');
            return first == std::string::npos ? std::string() : text.substr(first);
        };
        const ALLSLToSLua::Span* block = span(4);
        {
            INFO("the if");
            REQUIRE(block);
            CHECK(block->lslLast == 7);
        }
        UNSCOPED_INFO("from its head: " + trimmed(block->sluaFirst));
        CHECK(trimmed(block->sluaFirst).find("if d > 1 then") == 0);
        UNSCOPED_INFO("to its end: " + trimmed(block->sluaLast) + "\n" + r.text);
        CHECK(trimmed(block->sluaLast) == "end");
        const ALLSLToSLua::Span* inside = span(6);
        std::string all;
        for (const ALLSLToSLua::Span& each : r.spans)
        {
            all += std::to_string(each.lslFirst) + "-" + std::to_string(each.lslLast) + " : " + std::to_string(each.sluaFirst) + "-" + std::to_string(each.sluaLast) + "\n";
        }
        {
            INFO("the statement in it, a line of its own\n" + all + r.text);
            REQUIRE(inside);
            CHECK(inside->lslLast == 6);
            CHECK(inside->sluaFirst == inside->sluaLast);
            CHECK(trimmed(inside->sluaFirst).find("ll.Say(0, \"many\")") == 0);
        }
        const ALLSLToSLua::Span* next = span(9);
        {
            INFO("the next, under the comment: " + (next ? trimmed(next->sluaFirst) : std::string()));
            REQUIRE(next);
            CHECK(next->sluaFirst == next->sluaLast);
            REQUIRE(trimmed(next->sluaFirst).find("ll.Say(0, \"one\")") == 0);
            CHECK(trimmed(next->sluaFirst - 1) == "-- after");
        }
        const ALLSLToSLua::Span* handler = span(2);
        {
            INFO("the handler, over all of it");
            REQUIRE(handler);
            CHECK(handler->lslLast == 10);
            CHECK(handler->sluaFirst < block->sluaFirst);
            CHECK(handler->sluaLast >= next->sluaLast);
        }
        UNSCOPED_INFO("to its own end: " + trimmed(handler->sluaLast));
        CHECK(trimmed(handler->sluaLast).find("end") == 0);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: what the SLua says otherwise: calls and names and syntax; no semicolons", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer end = 1;\n"
                                                        "default\n"
                                                        "{\n"
                                                        "    state_entry()\n"
                                                        "    {\n"
                                                        "        if (end != 2 && TRUE) llSay(0, \"hi\");\n"
                                                        "        llOwnerSay((string)llAbs(end));\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        const auto has_pair = [&r](const std::string& lsl, const std::string& slua) {
            return std::find(r.same.begin(), r.same.end(), std::make_pair(lsl, slua)) != r.same.end();
        };
        UNSCOPED_INFO("llSay as ll.Say");
        CHECK(has_pair("llSay", "ll.Say"));
        UNSCOPED_INFO("llOwnerSay as SLua's print");
        CHECK(has_pair("llOwnerSay", "print"));
        UNSCOPED_INFO("llAbs as SLua's own");
        CHECK(has_pair("llAbs", "math.abs"));
        {
            INFO("a name Luau holds, marked");
            CHECK(has_pair("end", "end_"));
            CHECK(has(r, "local end_"));
        }
        {
            INFO("the syntax");
            CHECK(has_pair("!=", "~="));
            CHECK(has_pair("&&", "and"));
            CHECK(has_pair("integer", "local"));
            CHECK(has_pair("TRUE", "true"));
            CHECK(has_pair("(string)", "tostring"));
        }
        UNSCOPED_INFO("each once");
        CHECK(std::count(r.same.begin(), r.same.end(), std::make_pair(std::string("llSay"), std::string("ll.Say"))) == 1);
        UNSCOPED_INFO("semicolons let go of");
        CHECK(r.dropped == std::vector<std::string>{ ";" });
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: an integer added to a string by += or s = s + joined by ..= alone", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("string gOut;\n"
                                                        "string gTwo;\n"
                                                        "default { state_entry() {\n"
                                                        "    integer n = llGetUnixTime();\n"
                                                        "    gOut += (string)n;\n"
                                                        "    gTwo = gTwo + (string)n;\n"
                                                        "    llOwnerSay(gOut + gTwo);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("+=");
        CHECK(has(r, "gOut ..= n\n"));
        UNSCOPED_INFO("s = s +");
        CHECK(has(r, "gTwo ..= n\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: spans: statements sharing an LSL line side by side and not nested", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer x;\n"                  // 0
                                                        "integer y;\n"                  // 1
                                                        "default\n"                     // 2
                                                        "{\n"                           // 3
                                                        "    state_entry()\n"           // 4
                                                        "    {\n"                       // 5
                                                        "        x = 1; y = 2;\n"       // 6
                                                        "        llSay(0, (string)(x + y));\n" // 7
                                                        "    }\n"                       // 8
                                                        "}\n");                         // 9
        std::vector<const ALLSLToSLua::Span*> shared;
        std::string                           all;
        for (const ALLSLToSLua::Span& each : r.spans)
        {
            all += std::to_string(each.lslFirst) + "-" + std::to_string(each.lslLast) + " : " + std::to_string(each.sluaFirst) + "-" +
                   std::to_string(each.sluaLast) + "\n";
            if (each.lslFirst == 6 && each.lslLast == 6)
            {
                shared.push_back(&each);
            }
        }
        CAPTURE(all, r.text);
        UNSCOPED_INFO("two statements on the line");
        REQUIRE(shared.size() == size_t(2));
        UNSCOPED_INFO("their SLua apart");
        CHECK((shared[0]->sluaLast < shared[1]->sluaFirst || shared[1]->sluaLast < shared[0]->sluaFirst));
        // The handler, over more than one LSL line, still holds both.
        const auto handler = std::find_if(r.spans.begin(), r.spans.end(), [](const ALLSLToSLua::Span& each) { return each.lslFirst == 4; });
        {
            INFO("the handler over both");
            REQUIRE(handler != r.spans.end());
            CHECK(handler->sluaFirst < shared[0]->sluaFirst);
            CHECK(handler->sluaLast >= shared[1]->sluaLast);
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: what the SLua says otherwise: a call as written; marks only where written", "[ALLSLToSLua]")
    {
        const auto has_pair = [](const ALLSLToSLua::Result& r, const std::string& lsl, const std::string& slua) {
            return std::find(r.same.begin(), r.same.end(), std::make_pair(lsl, slua)) != r.same.end();
        };
        const ALLSLToSLua::Result plain = require_converted("default { state_entry() {\n"
                                                            "    integer n = 2;\n"
                                                            "    if (n > 1) llSay(0, (string)llPow(llFrand(1.0), 2.0));\n"
                                                            "} }\n");
        CAPTURE(plain.text);
        UNSCOPED_INFO("llFrand as ll.Frand");
        CHECK(has_pair(plain, "llFrand", "ll.Frand"));
        UNSCOPED_INFO("llPow, written as ^, not as the call inside it");
        CHECK(std::none_of(plain.same.begin(), plain.same.end(), [](const auto& p) { return p.first == "llPow"; }));
        {
            INFO("a block's braces");
            CHECK(has_pair(plain, "{", "then"));
            CHECK(has_pair(plain, "}", "end"));
        }
        {
            INFO("no vector, no string joined: no marks for them");
            CHECK_FALSE(has_pair(plain, ">", ")"));
            CHECK_FALSE(has_pair(plain, "<", "vector("));
            CHECK_FALSE(has_pair(plain, "+", ".."));
            CHECK_FALSE(has_pair(plain, "+=", "..="));
        }
        const ALLSLToSLua::Result marks = require_converted("string gOut;\n"
                                                            "default { state_entry() {\n"
                                                            "    vector v = <1, 2, llFrand(1.0)>;\n"
                                                            "    gOut += llGetObjectName();\n"
                                                            "    llSay(0, gOut + llGetObjectDesc() + (string)v.z);\n"
                                                            "} }\n");
        CAPTURE(marks.text);
        {
            INFO("a vector's brackets");
            CHECK(has_pair(marks, "<", "vector("));
            CHECK(has_pair(marks, ">", ")"));
        }
        UNSCOPED_INFO("a string added to");
        CHECK(has_pair(marks, "+=", "..="));
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a comment after a block's brace stays after the line that opens it", "[ALLSLToSLua]")
    {
        const std::string lsl = "vector offset;\n"
                                "list adjustments;\n"
                                "integer count() { // how many\n"
                                "    return llGetListLength(adjustments);\n"
                                "}\n"
                                "default {\n"
                                "    touch_start(integer n) { // whoever touches\n"
                                "        integer i = llListFindList(adjustments, [n]);\n"
                                "        if (i < 0) { // no saved setting. use 0\n"
                                "            offset = ZERO_VECTOR;\n"
                                "        } else if (i > 100) { // too far\n"
                                "            offset = <1, 1, 1>;\n"
                                "        } else { // restore saved setting\n"
                                "            offset = llList2Vector(adjustments, i+1);\n"
                                "        }\n"
                                "        while (i > 0) { // count down\n"
                                "            --i;\n"
                                "        }\n"
                                "        do { /* at least once */\n"
                                "            ++i;\n"
                                "        } while (i < 3);\n"
                                "        for (i = 0; i < count(); ++i) { // each\n"
                                "            llOwnerSay((string)i);\n"
                                "        }\n"
                                "        if (i) { /* over\n"
                                "                    two lines */\n"
                                "            llOwnerSay(\"x\");\n"
                                "        }\n"
                                "        if (n) { /* one */ llOwnerSay(\"y\"); }\n"
                                "    }\n"
                                "}\n";
        const ALLSLToSLua::Result r = require_converted(lsl);
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("then");
        CHECK(has(r, "    if i < 0 then -- no saved setting. use 0\n        offset = ZERO_VECTOR\n"));
        UNSCOPED_INFO("elseif");
        CHECK(has(r, "    elseif i > 100 then -- too far\n"));
        UNSCOPED_INFO("else");
        CHECK(has(r, "    else -- restore saved setting\n"));
        {
            INFO("not over the if");
            CHECK_FALSE(has(r, "-- no saved setting. use 0\n    -- restore"));
            CHECK(count(r, "restore saved setting") == 1);
        }
        UNSCOPED_INFO("a while's do");
        CHECK(has(r, " do -- count down\n"));
        UNSCOPED_INFO("repeat");
        CHECK(has(r, "repeat --[[ at least once ]]\n"));
        UNSCOPED_INFO("a for's do");
        CHECK(has(r, " do -- each\n"));
        UNSCOPED_INFO("a function");
        CHECK(has(r, "local function count() -- how many\n"));
        UNSCOPED_INFO("a handler");
        CHECK(has(r, "LLEvents:on(\"touch_start\", function(detected) -- whoever touches\n"));
        UNSCOPED_INFO("one over two lines, running on");
        CHECK(has(r, "    if i ~= 0 then --[[ over\n                    two lines ]]\n        print(\"x\")\n"));
        UNSCOPED_INFO("a one-line if on one line still");
        CHECK(has(r, "    if n ~= 0 then --[[ one ]] print(\"y\") end\n"));
        require_checks_clean(r);

        // The same with the field style, and in a state's table.
        ALLSLToSLua::Options field;
        field.handlers = ALLSLToSLua::Options::Handlers::Field;
        UNSCOPED_INFO("a field's handler");
        CHECK(has(ALLSLToSLua::convert(lsl, field), "function LLEvents.touch_start(detected) -- whoever touches\n"));
        const ALLSLToSLua::Result states = require_converted("default {\n"
                                                             "    touch_start(integer n) { // first state\n"
                                                             "        state other;\n"
                                                             "    }\n"
                                                             "}\n"
                                                             "state other {\n"
                                                             "    state_entry() { // entered\n"
                                                             "        llOwnerSay(\"in\");\n"
                                                             "    }\n"
                                                             "}\n");
        CAPTURE(states.text);
        {
            INFO("in a state's table");
            CHECK(has(states, "touch_start = function(detected) -- first state\n"));
            CHECK(has(states, "state_entry = function() -- entered\n"));
        }
        require_checks_clean(states);
        // A single state's state_entry runs at the top: its comment at the
        // top of what it ran.
        const ALLSLToSLua::Result entry = require_converted("default {\n    state_entry() { // set up\n        llOwnerSay(\"hi\");\n    }\n}\n");
        CAPTURE(entry.text);
        {
            INFO("state_entry's, kept");
            const size_t set_up = entry.text.find("-- set up\n");
            const size_t said   = entry.text.find("print(\"hi\")");
            CHECK(set_up != std::string::npos);
            CHECK(said != std::string::npos);
            CHECK(set_up < said);
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a comment after the brace of an unwritten block goes over its stand-in", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gMany;\n"
                                                        "default {\n"
                                                        "    touch_start(integer n) {\n"
                                                        "        if (n > 1) { // many\n"
                                                        "            gMany = TRUE;\n"
                                                        "        } else { // one\n"
                                                        "            gMany = FALSE;\n"
                                                        "        }\n"
                                                        "        if (gMany) llOwnerSay(\"many\");\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        const size_t many = r.text.find("    -- many\n");
        const size_t one  = r.text.find("    -- one\n");
        const size_t set  = r.text.find("    gMany = n > 1\n");
        {
            INFO("both over the line that stands for the if");
            REQUIRE(set != std::string::npos);
            CHECK(many < one);
            CHECK(one < set);
        }
        {
            INFO("once each");
            CHECK(count(r, "-- many") == 1);
            CHECK(count(r, "-- one") == 1);
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result exit = require_converted("default {\n"
                                                           "    state_entry() {\n"
                                                           "        llOwnerSay(\"in\");\n"
                                                           "    }\n"
                                                           "    state_exit() { // say goodbye\n"
                                                           "        llOwnerSay(\"out\");\n"
                                                           "    }\n"
                                                           "}\n");
        CAPTURE(exit.text);
        const size_t left = exit.text.find("-- state_exit, left out");
        {
            INFO("state_exit's, over the line said in its place");
            REQUIRE(left != std::string::npos);
            CHECK(exit.text.find("-- say goodbye\n") < left);
            CHECK(count(exit, "-- say goodbye") == 1);
        }

        const ALLSLToSLua::Result dashes = require_converted("default {\n"
                                                             "    listen(integer c, string name, key id, string s) {\n"
                                                             "        if (s == \" -- x\") llOwnerSay(\"y\");\n"
                                                             "    }\n"
                                                             "}\n");
        CAPTURE(dashes.text);
        UNSCOPED_INFO("a string's dashes no comment");
        CHECK(has(dashes, " == \" -- x\" then print(\"y\") end\n"));
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: llVecNorm through one vecNorm that keeps zero zero unless known nonzero", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    vector d = llDetectedPos(0) - llGetPos();\n"
                                                        "    vector a = llVecNorm(d);\n"
                                                        "    vector b = llVecNorm(llGetVel());\n"
                                                        "    vector c = llVecNorm(<0, 3, 4>);\n"
                                                        "    vector z = llVecNorm(ZERO_VECTOR);\n"
                                                        "    llOwnerSay((string)(a + b + c + z));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("guarded");
            CHECK(has(r, "local a = vecNorm(d)"));
            CHECK(has(r, "vecNorm(ll.GetVel())"));
        }
        UNSCOPED_INFO("written once");
        CHECK(count(r, "local function vecNorm(v: vector): vector") == 1);
        UNSCOPED_INFO("a vector known");
        CHECK(has(r, "vector.normalize(vector(0, 3, 4))"));
        UNSCOPED_INFO("nought guarded");
        CHECK(has(r, "vecNorm(ZERO_VECTOR)"));
        require_checks_clean(r);
        const ALLSLToSLua::Result none = require_converted("default { state_entry() { llOwnerSay((string)llVecMag(<1, 2, 3>)); } }\n");
        CAPTURE(none.text);
        UNSCOPED_INFO("no helper unasked");
        CHECK_FALSE(has(none, "vecNorm"));
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: lists appended with table.extend and + on a copy of the left; no joinLists", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gKept = [1];\n"
                                                        "list kept() { return gKept; }\n"
                                                        "show(list l) { llOwnerSay(llDumpList2String(l, \",\")); }\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    list all = llParseString2List(\"a,b\", [\",\"], []);\n"
                                                        "    list more = [4, 5];\n"
                                                        "    all += more;\n"
                                                        "    all += all;\n"
                                                        "    all += llList2List(more, 0, n);\n"
                                                        "    all += [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, "
                                                        "26, 27, 28, 29, 30, 31, 32, \"x\"];\n"
                                                        "    show(all);\n"
                                                        "    show(kept() + more + all);\n"
                                                        "    show(more + n);\n"
                                                        "    show(n + more);\n"
                                                        "    show([PRIM_NAME, \"x\"] + more);\n"
                                                        "    show(llParseString2List(\"c\", [], []) + [1] + [<1, 2, 3>]);\n"
                                                        "    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_COLOR, ALL_SIDES, <1, 1, 1>, 1.0] + more);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a list by its name");
        CHECK(has(r, "table.extend(all, more)"));
        UNSCOPED_INFO("itself");
        CHECK(has(r, "table.extend(all, all)"));
        {
            INFO("a call's, run once");
            CHECK(has(r, "table.extend(all, llcompat.List2List(more, 0, n))"));
            CHECK_FALSE(has(r, "for _, item in"));
        }
        {
            INFO("many values");
            CHECK(has(r, "table.extend(all, { 0, 1, 2,"));
            CHECK(has(r, "32, \"x\" })"));
        }
        UNSCOPED_INFO("one copy, of the variable a call answers");
        CHECK(has(r, "show(table.extend(table.extend(table.clone(kept()), more), all))"));
        UNSCOPED_INFO("a variable copied");
        CHECK(has(r, "show(table.extend(table.clone(more), { n }))"));
        UNSCOPED_INFO("a value");
        CHECK(has(r, "show(table.extend({ n } :: { any }, more))"));
        {
            INFO("written out");
            CHECK(has(r, "show(table.extend({ PRIM_NAME, \"x\" } :: { any }, more))"));
            CHECK(has(r, "table.extend({ PRIM_COLOR, ALL_SIDES, vector(1, 1, 1), 1.0 } :: { any }, more)"));
        }
        UNSCOPED_INFO("a library call's");
        CHECK(has(r, "show(table.extend(table.extend(ll.ParseString2List(\"c\", {}, {}) :: { any }, { 1 }), { vector(1, 2, 3) }))"));
        {
            INFO("no helper, nor a move");
            CHECK_FALSE(has(r, "joinLists"));
            CHECK_FALSE(has(r, "table.move("));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: list functions through SLua's own where they do as LSL's; else llcompat", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gNames = [\"a\", \"b\"];\n"
                                                        "list gKept;\n"
                                                        "list gL = [1, 2, 3];\n"
                                                        "integer bump() { gL += [0]; return 1; }\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    list l = [1, 2, 3];\n"
                                                        "    integer i;\n"
                                                        "    for (i = 0; i < llGetListLength(l); ++i)\n"
                                                        "        l = llListReplaceList(l, [llList2Integer(l, i) * 2], i, i);\n"
                                                        "    list held = [1, 2];\n"
                                                        "    gKept = held;\n"
                                                        "    integer j;\n"
                                                        "    for (j = 0; j < llGetListLength(held); ++j)\n"
                                                        "        held = llListReplaceList(held, [j], j, j);\n"
                                                        "    integer k;\n"
                                                        "    for (k = 0; k < llGetListLength(gL); ++k)\n"
                                                        "        gL = llListReplaceList(gL, [bump()], k, k);\n"
                                                        "    l = llListReplaceList(l, [7], -1, -1);\n"
                                                        "    l = llListInsertList(l, [9], llGetListLength(l));\n"
                                                        "    list part = llList2List(l, 1, 2);\n"
                                                        "    list all = llList2List(l, 0, -1);\n"
                                                        "    list rest = llList2List(l, 2, -1);\n"
                                                        "    list wrapped = llList2List(l, 3, 1);\n"
                                                        "    llOwnerSay(llDumpList2String(l, \",\"));\n"
                                                        "    llOwnerSay(llDumpList2String(gNames + [n], \", \"));\n"
                                                        "    llOwnerSay(llDumpList2String(l + [1.5], \",\"));\n"
                                                        "    llOwnerSay((string)l);\n"
                                                        "    integer at = llListFindList(gNames, [\"b\"]);\n"
                                                        "    integer none = llListFindList(gNames, [NULL_KEY]);\n"
                                                        "    if (llListFindList(gNames, [NULL_KEY]) != -1) llOwnerSay(\"null\");\n"
                                                        "    integer parsed = llListFindList(llParseString2List(\"a,b\", [\",\"], []), [\"b\"]);\n"
                                                        "    llOwnerSay((string)(at + none + parsed) + llDumpList2String(part + all + rest + wrapped + held, \"\"));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("replaced in place, in Luau's numeric for");
        CHECK(has(r, "for i = 0, #l - 1 do\n        l[i + 1] = l[i + 1] * 2\n"));
        {
            INFO("a list held elsewhere, replaced as LSL's");
            CHECK(has(r, "for j = 0, #held - 1 do\n"));
            CHECK(has(r, "        held = llcompat.ListReplaceList(held, { j }, j, j)\n"));
        }
        {
            INFO("a global a call of the script's could change");
            CHECK(has(r, "while k < #gL do"));
            CHECK(has(r, "gL = llcompat.ListReplaceList(gL, { bump() }, k, k)"));
        }
        UNSCOPED_INFO("the last of a list that could be empty");
        CHECK(has(r, "l = llcompat.ListReplaceList(l, { 7 }, -1, -1)"));
        UNSCOPED_INFO("on the end");
        CHECK(has(r, "table.insert(l, 9)"));
        {
            INFO("a part, and all of it");
            CHECK(has(r, "local part = table.move(l, 2, 3, 1, {})"));
            CHECK(has(r, "local all = table.clone(l)"));
        }
        {
            INFO("to the end, and the part left out, as LSL's");
            CHECK(has(r, "local rest = llcompat.List2List(l, 2, -1)"));
            CHECK(has(r, "local wrapped = llcompat.List2List(l, 3, 1)"));
        }
        {
            INFO("whole numbers joined");
            CHECK(has(r, "print(table.concat(l, \",\"))"));
            CHECK(has(r, "print(table.concat(l))"));
        }
        UNSCOPED_INFO("and text");
        CHECK(has(r, "print(table.concat(table.extend(table.clone(gNames), { n }), \", \"))"));
        UNSCOPED_INFO("a float as LSL writes one");
        CHECK(has(r, "print(ll.DumpList2String(table.extend(table.clone(l), { 1.5 }), \",\"))"));
        UNSCOPED_INFO("found, its place");
        CHECK(has(r, "local at = (table.find(gNames, \"b\") or 0) - 1"));
        {
            INFO("NULL_KEY as LSL's string, as its place and found");
            CHECK(has(r, "local none = (table.find(gNames, tostring(NULL_KEY)) or 0) - 1"));
            CHECK(has(r, "if table.find(gNames, tostring(NULL_KEY)) ~= nil then"));
        }
        UNSCOPED_INFO("in a library call's list");
        CHECK(has(r, "local parsed = (table.find(ll.ParseString2List(\"a,b\", { \",\" }, {}) :: { any }, \"b\") or 0) - 1"));
        UNSCOPED_INFO("no llcompat but those");
        CHECK(count(r, "llcompat.") == 5);
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: llSubStringIndex asked only whether it found is a plain string.find", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    string s = llGetObjectName(); key k = llGetOwner();\n"
                                                        "    if (llSubStringIndex(s, \"a\") != -1) llOwnerSay(\"a\");\n"
                                                        "    if (llSubStringIndex(s, \"b\") >= 0) llOwnerSay(\"b\");\n"
                                                        "    if (llSubStringIndex(s, \"c\") > -1) llOwnerSay(\"c\");\n"
                                                        "    if (llSubStringIndex(s, \"d\") == -1) llOwnerSay(\"no d\");\n"
                                                        "    if (!~llSubStringIndex(s, \"e\")) llOwnerSay(\"no e\");\n"
                                                        "    if (~llSubStringIndex(s, \".\")) llOwnerSay(\"dot\");\n"
                                                        "    if (~llSubStringIndex(s, k)) llOwnerSay(\"owner\");\n"
                                                        "    if (~llSubStringIndex(s, \"\")) llOwnerSay(\"empty\");\n"
                                                        "    integer at = llSubStringIndex(s, \"x\");\n"
                                                        "    llOwnerSay((string)at);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("found");
            CHECK(has(r, "if string.find(s, \"a\", 1, true) ~= nil then"));
            CHECK(has(r, "if string.find(s, \"b\", 1, true) ~= nil then"));
            CHECK(has(r, "if string.find(s, \"c\", 1, true) ~= nil then"));
        }
        {
            INFO("not");
            CHECK(has(r, "if string.find(s, \"d\", 1, true) == nil then"));
            CHECK(has(r, "if not (string.find(s, \"e\", 1, true) ~= nil) then"));
        }
        UNSCOPED_INFO("plain, not a pattern");
        CHECK(has(r, "if string.find(s, \".\", 1, true) ~= nil then"));
        UNSCOPED_INFO("a key as its text");
        CHECK(has(r, "if string.find(s, tostring(k), 1, true) ~= nil then"));
        UNSCOPED_INFO("the empty text");
        CHECK(has(r, "if string.find(s, \"\", 1, true) ~= nil then"));
        UNSCOPED_INFO("its index in characters, ll's");
        CHECK(has(r, "local at = (ll.SubStringIndex(s, \"x\") or 0) - 1"));
        UNSCOPED_INFO("found by string.find alone");
        CHECK(count(r, "SubStringIndex") == 1);
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a script that resets its time keeps its own timeBase clock; else ll's", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("float timeBase;\n"
                                                        "default {\n"
                                                        "    state_entry() { llResetTime(); state running; }\n"
                                                        "}\n"
                                                        "state running {\n"
                                                        "    state_entry() { timeBase = llGetTime(); }\n"
                                                        "    touch_start(integer n) {\n"
                                                        "        float lap = llGetAndResetTime();\n"
                                                        "        llGetAndResetTime();\n"
                                                        "        llOwnerSay((string)(lap + timeBase) + \" \" + (string)(llGetTime() * 2));\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("the clock, once, over everything");
            CHECK(count(r, "local timeBase = ll.GetTime()") == 1);
            CHECK(r.text.find("local timeBase = ll.GetTime()") < r.text.find("local states"));
        }
        {
            INFO("the script's own name made another");
            CHECK(has(r, "local timeBase_ = 0"));
            CHECK(has(r, "timeBase_ = ll.GetTime() - timeBase"));
        }
        UNSCOPED_INFO("a reset");
        CHECK(has(r, "state_entry = function()\n        timeBase = ll.GetTime()\n        setState(\"running\")"));
        {
            INFO("both, as a value, through the helper");
            CHECK(has(r, "local lap = getAndResetTime()"));
            CHECK(count(r, "local function getAndResetTime(): number") == 1);
        }
        UNSCOPED_INFO("both where nothing reads the time, the reset alone");
        CHECK(has(r, "local lap = getAndResetTime()\n        timeBase = ll.GetTime()\n"));
        UNSCOPED_INFO("a read bracketed");
        CHECK(has(r, "(ll.GetTime() - timeBase) * 2"));
        {
            INFO("nothing of llcompat's");
            CHECK_FALSE(has(r, "llcompat.ResetTime"));
            CHECK_FALSE(has(r, "llcompat.GetAndResetTime"));
            CHECK_FALSE(noted(r, "SluaCompatOnly", "ResetTime"));
            CHECK_FALSE(noted(r, "SluaCompatOnly", "GetAndResetTime"));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result reads = require_converted("default { touch_start(integer n) { llOwnerSay((string)llGetTime()); } }\n");
        CAPTURE(reads.text);
        {
            INFO("only read, ll's");
            CHECK(has(reads, "ll.GetTime()"));
            CHECK_FALSE(has(reads, "timeBase"));
        }

        const ALLSLToSLua::Result close = ALLSLToSLua::convert("default { touch_start(integer n) {\n"
                                                               "    llOwnerSay((string)llGetTime());\n"
                                                               "    llResetTime();\n"
                                                               "} }\n",
                                                               ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("close to LSL, llcompat's together");
            CHECK(has(close, "llcompat.GetTime()"));
            CHECK(has(close, "llcompat.ResetTime()"));
            CHECK_FALSE(has(close, "ll.GetTime"));
            CHECK_FALSE(has(close, "timeBase"));
        }
        require_checks_clean(close);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: Base64 through llbase64 with decode noted once; ll's close to LSL", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    key k = llDetectedKey(0);\n"
                                                        "    string s = llStringToBase64(\"hello\");\n"
                                                        "    string t = llStringToBase64(k);\n"
                                                        "    string d = llBase64ToString(s);\n"
                                                        "    string e = llBase64ToString(t + \"=\");\n"
                                                        "    llOwnerSay(s + t + d + e);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("encode");
            CHECK(has(r, "local s = llbase64.encode(\"hello\")"));
            CHECK(has(r, "local t = llbase64.encode(tostring(k))"));
        }
        {
            INFO("decode");
            CHECK(has(r, "local d = llbase64.decode(s)"));
            CHECK(has(r, "local e = llbase64.decode(t .. \"=\")"));
        }
        {
            INFO("decode noted once");
            CHECK(noted(r, "SluaBase64Decode"));
            CHECK(count(r, "-- LSL: llbase64.decode keeps every byte") == 1);
        }
        {
            INFO("nothing of llcompat's or ll's");
            CHECK_FALSE(has(r, "llcompat."));
            CHECK_FALSE(has(r, "ll.StringToBase64"));
            CHECK_FALSE(has(r, "ll.Base64ToString"));
            CHECK_FALSE(noted(r, "SluaDeprecatedFor", "StringToBase64"));
            CHECK_FALSE(noted(r, "SluaDeprecatedFor", "Base64ToString"));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result close =
            ALLSLToSLua::convert("default { state_entry() { llOwnerSay(llBase64ToString(llStringToBase64(\"x\"))); } }\n", ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("close to LSL");
            CHECK(has(close, "ll.Base64ToString(ll.StringToBase64(\"x\"))"));
            CHECK_FALSE(has(close, "llcompat."));
            CHECK_FALSE(has(close, "llbase64.decode("));
            CHECK_FALSE(has(close, "llbase64.encode("));
            CHECK_FALSE(noted(close, "SluaBase64Decode"));
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: llAdjustDamage in on_damage as detected adjustDamage; llcompat elsewhere", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("halve() { llAdjustDamage(0, 2.0); }\n"
                                                        "default {\n"
                                                        "    on_damage(integer n) {\n"
                                                        "        llAdjustDamage(0, 0.5);\n"
                                                        "        integer i;\n"
                                                        "        for (i = 1; i < n; ++i) llAdjustDamage(i, 5);\n"
                                                        "        if (n > 3) halve();\n"
                                                        "    }\n"
                                                        "    touch_start(integer n) { llAdjustDamage(0, 1.0); }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("on_damage's own");
            CHECK(has(r, "detected[1]:adjustDamage(0.5)"));
            CHECK(has(r, "detected[i + 1]:adjustDamage(5)"));
            CHECK(noted(r, "SluaDetectedTable"));
        }
        UNSCOPED_INFO("a function's, llcompat's");
        CHECK(has(r, "llcompat.AdjustDamage(0, 2.0)"));
        UNSCOPED_INFO("another event's, llcompat's");
        CHECK(has(r, "llcompat.AdjustDamage(0, 1.0)"));
        require_checks_clean(r);

        const ALLSLToSLua::Result close =
            ALLSLToSLua::convert("default { on_damage(integer n) { llAdjustDamage(0, 0.5); } }\n", ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("close to LSL, llcompat's");
            CHECK(has(close, "llcompat.AdjustDamage(0, 0.5)"));
            CHECK_FALSE(has(close, "adjustDamage("));
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a timer every handler turns off first is LLTimers:once; else :every", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default {\n"
                                                        "    state_entry() { llSetTimerEvent(5.0); }\n"
                                                        "    timer() {\n"
                                                        "        llSetTimerEvent(0.0);\n"
                                                        "        llOwnerSay(\"once\");\n"
                                                        "        if (llFrand(1.0) < 0.5) llSetTimerEvent(2);\n"
                                                        "    }\n"
                                                        "    touch_start(integer n) { llSetTimerEvent(1); }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("once");
            CHECK(has(r, "timerHandle = LLTimers:once(seconds, function()"));
            CHECK_FALSE(has(r, "LLTimers:every(seconds, function"));
        }
        UNSCOPED_INFO("said so");
        CHECK(has(r, "-- time it is set, as its handler turns it off before anything else."));
        {
            INFO("the handler's own off kept, and set again");
            CHECK(has(r, "function timerHandler()\n    setTimer(0.0)\n"));
            CHECK(has(r, "setTimer(2)"));
            CHECK(has(r, "setTimer(1)"));
        }
        require_checks_clean(r);
        // Tapple's: the local the preamble declared, assigned by a function
        // statement of its name, which types as it is declared.
        ALLSLToSLua::Options typed;
        typed.types                     = true;
        const ALLSLToSLua::Result types = ALLSLToSLua::convert("default { state_entry() { llSetTimerEvent(5); } timer() { llOwnerSay(\"t\"); } }\n", typed);
        CAPTURE(types.text);
        {
            INFO("a function statement, typed");
            CHECK(has(types, "local timerHandler: (() -> ())? = nil\n"));
            CHECK(has(types, "\nfunction timerHandler()\n    print(\"t\")\nend\n"));
            CHECK_FALSE(has(types, "timerHandler = function"));
        }
        require_checks_clean(types);

        const ALLSLToSLua::Result states = require_converted("default {\n"
                                                             "    state_entry() { llSetTimerEvent(1); }\n"
                                                             "    timer() { llSetTimerEvent(0); state two; }\n"
                                                             "}\n"
                                                             "state two {\n"
                                                             "    timer() { llSetTimerEvent(FALSE); llOwnerSay(\"two\"); }\n"
                                                             "    touch_start(integer n) { llSetTimerEvent(3); }\n"
                                                             "}\n");
        CAPTURE(states.text);
        {
            INFO("every state's, once");
            CHECK(has(states, "timerHandle = LLTimers:once(seconds, function"));
            CHECK(has(states, "states[currentState].timer"));
        }
        require_checks_clean(states);

        const ALLSLToSLua::Result without = require_converted("default {\n"
                                                              "    state_entry() { llSetTimerEvent(1); state two; }\n"
                                                              "    timer() { llSetTimerEvent(0); }\n"
                                                              "}\n"
                                                              "state two {\n"
                                                              "    touch_start(integer n) { state default; }\n"
                                                              "}\n");
        CAPTURE(without.text);
        {
            INFO("a state with none, every");
            CHECK(has(without, "timerHandle = LLTimers:every(seconds, function"));
            CHECK_FALSE(has(without, "LLTimers:once(seconds, function"));
        }

        const ALLSLToSLua::Result later = require_converted("default {\n"
                                                            "    state_entry() { llSetTimerEvent(1); }\n"
                                                            "    timer() { llOwnerSay(\"tick\"); llSetTimerEvent(0); }\n"
                                                            "}\n");
        CAPTURE(later.text);
        {
            INFO("off after something else, every");
            CHECK(has(later, "timerHandle = LLTimers:every(seconds, function"));
            CHECK_FALSE(has(later, "LLTimers:once(seconds, function"));
        }

        const ALLSLToSLua::Result again = require_converted("float gNext = 0.0;\n"
                                                            "default {\n"
                                                            "    state_entry() { llSetTimerEvent(1); }\n"
                                                            "    timer() { llSetTimerEvent(gNext); }\n"
                                                            "    touch_start(integer n) { gNext = 2.0; }\n"
                                                            "}\n");
        CAPTURE(again.text);
        {
            INFO("set to what is not written nought, every");
            CHECK(has(again, "timerHandle = LLTimers:every(seconds, function"));
            CHECK_FALSE(has(again, "LLTimers:once(seconds, function"));
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: integer / and % as LSL's via bit32.s32 and math.fmod; // and % if safe", "[ALLSLToSLua]")
    {
        const char* const script = "integer gSize = 16;\n"
                                   "default { touch_start(integer n) {\n"
                                   "    integer a = n - 3; integer b = 1 - n; float f = 2.5;\n"
                                   "    integer q = a / b; integer m = a % b;\n"
                                   "    integer sum = (a + 1) / (b - 2) + (a + 1) % (b - 2);\n"
                                   "    integer back = -7 / 2 + 7 % -2;\n"
                                   "    integer cut = gSize / 4 + gSize % 3 + (n & 255) / 16 + (n & 255) % gSize;\n"
                                   "    integer half = a / gSize;\n"
                                   "    a /= b; a %= b; b = b / 2; b = b % 3;\n"
                                   "    f = f / 2;\n"
                                   "    llOwnerSay((string)(q + m + sum + back + cut + half + a + b) + (string)f);\n"
                                   "} }\n";
        const ALLSLToSLua::Result r = require_converted(script);
        CAPTURE(r.text);
        {
            INFO("toward nought");
            CHECK(has(r, "local q = bit32.s32(a / b)"));
            CHECK(has(r, "local m = math.fmod(a, b)"));
        }
        UNSCOPED_INFO("bracketed as / binds");
        CHECK(has(r, "bit32.s32((a + 1) / (b - 2)) + math.fmod(a + 1, b - 2)"));
        UNSCOPED_INFO("a side below nought");
        CHECK(has(r, "bit32.s32(-7 / 2) + math.fmod(7, -2)"));
        UNSCOPED_INFO("neither below nought");
        CHECK(has(r, "gSize // 4 + gSize % 3 + bit32.band(n, 255) // 16 + bit32.band(n, 255) % gSize"));
        UNSCOPED_INFO("one known, not the other");
        CHECK(has(r, "local half = bit32.s32(a / gSize)"));
        {
            INFO("compound ones whole");
            CHECK(has(r, "a = bit32.s32(a / b)"));
            CHECK(has(r, "a = math.fmod(a, b)"));
            CHECK(has(r, "b = bit32.s32(b / 2)"));
            CHECK(has(r, "b = math.fmod(b, 3)"));
            CHECK_FALSE(has(r, "//="));
            CHECK_FALSE(has(r, "%="));
        }
        UNSCOPED_INFO("a float's");
        CHECK(has(r, "f /= 2"));
        {
            INFO("unnoted");
            CHECK_FALSE(noted(r, "SluaIntegerDivision"));
            CHECK_FALSE(noted(r, "SluaModulo"));
        }
        require_checks_clean(r);
        // Typed, each one a number to the checker, strict under the grid's
        // solver: math.fmod's one answer and bit32.s32's.
        ALLSLToSLua::Options options;
        options.types                 = true;
        const ALLSLToSLua::Result typed = ALLSLToSLua::convert(script, options);
        CAPTURE(typed.text);
        CHECK(has(typed, "local q: number = bit32.s32(a / b)"));
        CHECK(has(typed, "local m: number = math.fmod(a, b)"));
        require_checks_clean(typed);
        if (!newSolver)
        {
            std::string said;
            for (const ALScriptProblem& p : service.check("--!strict\n" + typed.text))
            {
                said += p.severity == ALScriptProblem::Severity::Error ? p.message + "\n" : std::string();
            }
            CAPTURE(said);
            CHECK(said.empty());
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: keys from empty or capital text noted; the XorBase64Strings pair noted", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    key a = (key)\"\"; key c = (key)\"A2E76FCD-9360-4F6D-A924-938F923DF11D\";\n"
                                                        "    if ((key)\"\" == NULL_KEY) llOwnerSay(\"same\");\n"
                                                        "    string s = llGetObjectDesc(); key d = (key)s;\n"
                                                        "    llOwnerSay((string)a + (string)c + (string)d + llXorBase64(\"a\", \"b\") + llXorBase64Strings(\"a\", \"b\") +\n"
                                                        "               llXorBase64StringsCorrect(\"a\", \"b\"));\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("no text, NULL_KEY");
            CHECK(has(r, "local a = uuid(\"\")"));
            CHECK(noted(r, "SluaKeyEmpty"));
            CHECK(has(r, "-- LSL: SLua's uuid of no text is NULL_KEY"));
        }
        {
            INFO("capitals, small");
            CHECK(has(r, "uuid(\"A2E76FCD-9360-4F6D-A924-938F923DF11D\")"));
            CHECK(noted(r, "SluaKeyCase"));
        }
        {
            INFO("other text as it was");
            CHECK(has(r, "local d = uuid(s)"));
            CHECK(noted(r, "SluaUuid"));
        }
        UNSCOPED_INFO("ll.XorBase64 as it is");
        CHECK(has(r, "ll.XorBase64(\"a\", \"b\")"));
        {
            INFO("the two, LSL's");
            CHECK(has(r, "ll.XorBase64Strings(\"a\", \"b\")"));
            CHECK(has(r, "ll.XorBase64StringsCorrect(\"a\", \"b\")"));
            CHECK_FALSE(has(r, "llcompat."));
        }
        {
            INFO("said what ll.XorBase64 is not");
            CHECK(noted(r, "SluaXorBase64Wrong"));
            CHECK(noted(r, "SluaXorBase64Nul"));
            CHECK_FALSE(noted(r, "SluaDeprecated", "XorBase64Strings"));
            CHECK_FALSE(noted(r, "SluaDeprecated", "XorBase64StringsCorrect"));
        }
        for (const ALScriptProblem& p : r.notes)
        {
            if (p.key == "SluaXorBase64Wrong" || p.key == "SluaXorBase64Nul")
            {
                CAPTURE(p.message);
                REQUIRE(p.message.find("for ll.XorBase64,") != std::string::npos);
            }
        }
        require_checks_clean(r);

        // Given rather than cast: no text kept as text, as other text that is
        // no UUID is; capitals the same said, and a UUID written small
        // nothing at all.
        const ALLSLToSLua::Result given = require_converted("default { touch_start(integer n) {\n"
                                                            "    key b = \"\"; key c = \"A2E76FCD-9360-4F6D-A924-938F923DF11D\";\n"
                                                            "    key l = \"a2e76fcd-9360-4f6d-a924-938f923df11d\";\n"
                                                            "    llOwnerSay((string)llGetOwnerKey(b) + (string)llGetOwnerKey(c) + (string)llGetOwnerKey(l));\n"
                                                            "} }\n");
        CAPTURE(given.text);
        {
            INFO("given no text, text");
            CHECK(has(given, "local b = \"\""));
            CHECK(has(given, "ll.GetOwnerKey(b :: any)"));
            CHECK_FALSE(noted(given, "SluaKeyEmpty"));
            CHECK(noted(given, "SluaKeyCase"));
        }
        {
            INFO("nothing else said of them");
            CHECK_FALSE(noted(given, "SluaKeyText"));
            CHECK_FALSE(noted(given, "SluaUuidText"));
        }
        require_checks_clean(given);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a particle rule list written out as a ParticleParams table", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer gFlags = 3;\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    llParticleSystem([\n"
                                                        "        PSYS_PART_FLAGS, PSYS_PART_EMISSIVE_MASK | PSYS_PART_INTERP_COLOR_MASK,\n"
                                                        "        PSYS_PART_START_COLOR, <1, 0, 0>, PSYS_PART_START_ALPHA, 1,\n"
                                                        "        PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_EXPLODE,\n"
                                                        "        PSYS_SRC_TEXTURE, NULL_KEY, PSYS_SRC_TARGET_KEY, llGetOwner()\n"
                                                        "    ]);\n"
                                                        "    llLinkParticleSystem(LINK_SET, [PSYS_PART_FLAGS, gFlags, PSYS_SRC_BURST_RATE, 0.1]);\n"
                                                        "    llLinkParticleSystem(2, [PSYS_PART_FLAGS, PSYS_PART_BOUNCE_MASK]);\n"
                                                        "    llParticleSystem([]);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a key to a line");
        CHECK(has(r, "    ll.ParticleSystem({\n"
                     "        emissive = true,\n"
                     "        color_interp = true,\n"
                     "        color_begin = vector(1, 0, 0),\n"
                     "        alpha_begin = 1,\n"
                     "        pattern = PSYS_SRC_PATTERN_EXPLODE,\n"
                     "        texture = tostring(NULL_KEY),\n"
                     "        target_key = ll.GetOwner(),\n"
                     "    })\n"));
        {
            INFO("on one line as written; flags not of masks as they are");
            CHECK(has(r, "ll.LinkParticleSystem(LINK_SET, { flags = gFlags, burst_rate = 0.1 })"));
            CHECK(has(r, "ll.LinkParticleSystem(2, { bounce = true })"));
        }
        UNSCOPED_INFO("no rules, as they were");
        CHECK(has(r, "ll.ParticleSystem({})"));
        const auto same = [&r](const std::string& lsl, const std::string& slua) {
            return std::find(r.same.begin(), r.same.end(), std::make_pair(lsl, slua)) != r.same.end();
        };
        {
            INFO("each constant the same word as its key");
            CHECK(same("PSYS_PART_START_COLOR", "color_begin"));
            CHECK(same("PSYS_PART_EMISSIVE_MASK", "emissive"));
            CHECK(same("PSYS_SRC_BURST_RATE", "burst_rate"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a media rule list as MediaParams with booleans and whitelist pieces", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    integer on = llGetLinkNumber();\n"
                                                        "    llSetPrimMediaParams(0, [PRIM_MEDIA_AUTO_PLAY, TRUE, PRIM_MEDIA_AUTO_LOOP, 0, PRIM_MEDIA_CURRENT_URL, \"http://a.com\",\n"
                                                        "        PRIM_MEDIA_WIDTH_PIXELS, 512, PRIM_MEDIA_WHITELIST_ENABLE, 1, PRIM_MEDIA_WHITELIST, \"a.com,*.b.com,\"]);\n"
                                                        "    if (llSetLinkMedia(LINK_THIS, 1, [PRIM_MEDIA_HOME_URL, \"http://h\"]) == STATUS_OK) llOwnerSay(\"ok\");\n"
                                                        "    llSetPrimMediaParams(1, [PRIM_MEDIA_WHITELIST, \",a.com\"]);\n"
                                                        "    llSetPrimMediaParams(2, [PRIM_MEDIA_WHITELIST, \"a\\\\,b\"]);\n"
                                                        "    llSetPrimMediaParams(3, [PRIM_MEDIA_WHITELIST, \"\"]);\n"
                                                        "    llSetPrimMediaParams(4, [PRIM_MEDIA_AUTO_PLAY, on]);\n"
                                                        "    llSetPrimMediaParams(5, [PRIM_MEDIA_AUTO_ZOOM, 2]);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a table");
        CHECK(has(r, "    ll.SetPrimMediaParams(0, {\n"
                     "        auto_play = true,\n"
                     "        auto_loop = false,\n"
                     "        current_url = \"http://a.com\",\n"
                     "        width = 512,\n"
                     "        whitelist_enable = true,\n"
                     "        whitelist = { \"a.com\", \"*.b.com\", \"\" },\n"
                     "    })\n"));
        UNSCOPED_INFO("what it answers read");
        CHECK(has(r, "if ll.SetLinkMedia(LINK_THIS, 1, { home_url = \"http://h\" }) == STATUS_OK then"));
        // The join leaves a first piece of none out, escapes a \, and sends
        // no rule at all for no text.
        {
            INFO("a whitelist the join would change, a list");
            CHECK(has(r, "ll.SetPrimMediaParams(1, { PRIM_MEDIA_WHITELIST, \",a.com\" })"));
            CHECK(has(r, "ll.SetPrimMediaParams(2, { PRIM_MEDIA_WHITELIST, \"a"));
            CHECK(has(r, "ll.SetPrimMediaParams(3, { PRIM_MEDIA_WHITELIST, \"\" })"));
        }
        {
            INFO("a truth not written out as one, a list");
            CHECK(has(r, "ll.SetPrimMediaParams(4, { PRIM_MEDIA_AUTO_PLAY, on })"));
            CHECK(has(r, "ll.SetPrimMediaParams(5, { PRIM_MEDIA_AUTO_ZOOM, 2 })"));
        }
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: an HTTP rule list as HttpRequestParams with headers by name", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    string token = llGetObjectDesc();\n"
                                                        "    llHTTPRequest(\"https://x\", [HTTP_METHOD, \"POST\", HTTP_MIMETYPE, \"application/json\", HTTP_VERIFY_CERT, FALSE,\n"
                                                        "        HTTP_CUSTOM_HEADER, \"X-B\", token, HTTP_BODY_MAXLENGTH, 16384, HTTP_CUSTOM_HEADER, \"X-A\", \"two\",\n"
                                                        "        HTTP_ACCEPT, \"application/json\", HTTP_ACCEPT, \"text/plain\"], \"{}\");\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a table");
        CHECK(has(r, "    ll.HTTPRequest(\"https://x\", {\n"
                     "        method = \"POST\",\n"
                     "        mimetype = \"application/json\",\n"
                     "        verify_cert = false,\n"
                     "        custom_header = { [\"X-B\"] = token, [\"X-A\"] = \"two\" },\n"
                     "        max_body_length = 16384,\n"
                     "        accept = { \"application/json\", \"text/plain\" },\n"
                     "    }, \"{}\")\n"));
        {
            INFO("the headers' order said");
            CHECK(noted(r, "SluaHeaderOrder"));
            CHECK(has(r, "-- LSL: SLua passes ll.HTTPRequest a custom_header table's headers sorted by name"));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result sorted = require_converted("default { touch_start(integer n) {\n"
                                                             "    key id = llHTTPRequest(\"https://y\", [HTTP_CUSTOM_HEADER, \"A\", \"1\", HTTP_CUSTOM_HEADER, \"B\", \"2\"], \"\");\n"
                                                             "    llOwnerSay((string)id);\n"
                                                             "} }\n");
        CAPTURE(sorted.text);
        {
            INFO("in order by name, nothing said");
            CHECK(has(sorted, "local id = ll.HTTPRequest(\"https://y\", { custom_header = { [\"A\"] = \"1\", [\"B\"] = \"2\" } }, \"\")"));
            CHECK_FALSE(noted(sorted, "SluaHeaderOrder"));
        }
        require_checks_clean(sorted);

        const ALLSLToSLua::Result lists = require_converted("integer gCount;\n"
                                                            "string counted() { ++gCount; return (string)gCount; }\n"
                                                            "default { touch_start(integer n) {\n"
                                                            "    string token = llGetObjectDesc();\n"
                                                            "    llHTTPRequest(\"https://z\", [HTTP_CUSTOM_HEADER, \"A\", \"1\", HTTP_CUSTOM_HEADER, \"A\", \"2\"], \"\");\n"
                                                            "    llHTTPRequest(\"https://z\", [HTTP_CUSTOM_HEADER, token, \"1\"], \"\");\n"
                                                            "    llHTTPRequest(\"https://z\", [HTTP_CUSTOM_HEADER, \"A\", counted(), HTTP_METHOD, \"GET\", HTTP_CUSTOM_HEADER, \"B\", counted()], \"\");\n"
                                                            "    llHTTPRequest(\"https://z\", [HTTP_CUSTOM_HEADER, \"A\", counted(), HTTP_CUSTOM_HEADER, \"B\", counted()], \"\");\n"
                                                            "} }\n");
        CAPTURE(lists.text);
        UNSCOPED_INFO("a name twice, a list");
        CHECK(has(lists, "ll.HTTPRequest(\"https://z\", { HTTP_CUSTOM_HEADER, \"A\", \"1\", HTTP_CUSTOM_HEADER, \"A\", \"2\" }, \"\")"));
        UNSCOPED_INFO("a name not written out, a list");
        CHECK(has(lists, "ll.HTTPRequest(\"https://z\", { HTTP_CUSTOM_HEADER, token, \"1\" }, \"\")"));
        UNSCOPED_INFO("moved past another, a list");
        CHECK(has(lists, "{ HTTP_CUSTOM_HEADER, \"A\", counted(), HTTP_METHOD, \"GET\", HTTP_CUSTOM_HEADER, \"B\", counted() }"));
        UNSCOPED_INFO("one after another, run in their order");
        CHECK(has(lists, "{ custom_header = { [\"A\"] = counted(), [\"B\"] = counted() } }"));
        require_checks_clean(lists);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a rule list the table cannot say just so stays a list", "[ALLSLToSLua]")
    {
        const char* const script = "list gRules = [PSYS_PART_MAX_AGE, 2.0];\n"
                                   "default { touch_start(integer n) {\n"
                                   "    llParticleSystem(gRules);\n"
                                   "    llParticleSystem([PSYS_PART_MAX_AGE, 2.0, PSYS_PART_MAX_AGE, 3.0]);\n"
                                   "    llParticleSystem([PRIM_MEDIA_AUTO_PLAY, TRUE]);\n"
                                   "    llParticleSystem([7, 2.0]);\n"
                                   "    llParticleSystem([PSYS_PART_START_ALPHA, <1, 0, 0>]);\n"
                                   "    llParticleSystem([PSYS_PART_MAX_AGE]);\n"
                                   "    llLinkParticleSystem(1, [PSYS_PART_FLAGS, PSYS_PART_WIND_MASK + PSYS_PART_BOUNCE_MASK]);\n"
                                   "    llLinkParticleSystem(2, [PSYS_PART_FLAGS, PSYS_PART_WIND_MASK | PSYS_PART_WIND_MASK]);\n"
                                   "    llLinkParticleSystem(3, [PSYS_PART_FLAGS, PSYS_PART_WIND_MASK | (PSYS_PART_BOUNCE_MASK | PSYS_PART_RIBBON_MASK)]);\n"
                                   "} }\n";
        const ALLSLToSLua::Result r = require_converted(script);
        CAPTURE(r.text);
        UNSCOPED_INFO("not written out");
        CHECK(has(r, "ll.ParticleSystem(gRules)"));
        UNSCOPED_INFO("a key twice");
        CHECK(has(r, "ll.ParticleSystem({ PSYS_PART_MAX_AGE, 2.0, PSYS_PART_MAX_AGE, 3.0 })"));
        UNSCOPED_INFO("another table's rule");
        CHECK(has(r, "ll.ParticleSystem({ PRIM_MEDIA_AUTO_PLAY, 1 })"));
        UNSCOPED_INFO("a number for a rule");
        CHECK(has(r, "ll.ParticleSystem({ 7, 2.0 })"));
        UNSCOPED_INFO("a value of another shape");
        CHECK(has(r, "ll.ParticleSystem({ PSYS_PART_START_ALPHA, vector(1, 0, 0) })"));
        UNSCOPED_INFO("no value");
        CHECK(has(r, "ll.ParticleSystem({ PSYS_PART_MAX_AGE })"));
        {
            INFO("flags as they are");
            CHECK(has(r, "ll.LinkParticleSystem(1, { flags = PSYS_PART_WIND_MASK + PSYS_PART_BOUNCE_MASK })"));
            CHECK(has(r, "ll.LinkParticleSystem(2, { flags = bit32.bor(PSYS_PART_WIND_MASK, PSYS_PART_WIND_MASK) })"));
        }
        UNSCOPED_INFO("masks bracketed, each true");
        CHECK(has(r, "ll.LinkParticleSystem(3, { wind = true, bounce = true, ribbon = true })"));
        ALLSLToSLua::Options plain;
        plain.idioms                    = false;
        const ALLSLToSLua::Result lists = ALLSLToSLua::convert(script, plain);
        CAPTURE(lists.text);
        {
            INFO("no idioms, no tables");
            CHECK(has(lists, "ll.LinkParticleSystem(3, { PSYS_PART_FLAGS, "));
            CHECK_FALSE(has(lists, "wind = true"));
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: every rule of the three tables matches the definitions' types", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted(
            "default { touch_start(integer n) {\n"
            "    key k = llGetOwner();\n"
            "    llParticleSystem([PSYS_PART_FLAGS, PSYS_PART_INTERP_COLOR_MASK | PSYS_PART_INTERP_SCALE_MASK | PSYS_PART_BOUNCE_MASK |\n"
            "        PSYS_PART_WIND_MASK | PSYS_PART_FOLLOW_SRC_MASK | PSYS_PART_FOLLOW_VELOCITY_MASK | PSYS_PART_TARGET_POS_MASK |\n"
            "        PSYS_PART_TARGET_LINEAR_MASK | PSYS_PART_EMISSIVE_MASK | PSYS_PART_RIBBON_MASK,\n"
            "        PSYS_PART_START_COLOR, <1, 1, 1>, PSYS_PART_START_ALPHA, 1.0, PSYS_PART_END_COLOR, <0, 0, 0>, PSYS_PART_END_ALPHA, 0.0,\n"
            "        PSYS_PART_START_SCALE, <0.1, 0.1, 0>, PSYS_PART_END_SCALE, <1, 1, 0>, PSYS_PART_MAX_AGE, 2.0, PSYS_SRC_ACCEL, <0, 0, -1>,\n"
            "        PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_ANGLE_CONE, PSYS_SRC_INNERANGLE, 0.1, PSYS_SRC_OUTERANGLE, 0.2, PSYS_SRC_TEXTURE, \"\",\n"
            "        PSYS_SRC_BURST_RATE, 0.1, PSYS_SRC_BURST_PART_COUNT, 5, PSYS_SRC_BURST_RADIUS, 1.0, PSYS_SRC_BURST_SPEED_MIN, 0.5,\n"
            "        PSYS_SRC_BURST_SPEED_MAX, 1.5, PSYS_SRC_MAX_AGE, 0.0, PSYS_SRC_TARGET_KEY, k, PSYS_SRC_OMEGA, <0, 0, 1>,\n"
            "        PSYS_SRC_ANGLE_BEGIN, 0.0, PSYS_SRC_ANGLE_END, 3.14, PSYS_PART_BLEND_FUNC_SOURCE, PSYS_PART_BF_SOURCE_ALPHA,\n"
            "        PSYS_PART_BLEND_FUNC_DEST, PSYS_PART_BF_ONE, PSYS_PART_START_GLOW, 0.1, PSYS_PART_END_GLOW, 0.0]);\n"
            "    llLinkParticleSystem(2, [PSYS_PART_FLAGS,\n"
            "        n]);\n"
            "    llSetLinkMedia(LINK_THIS, 0, [PRIM_MEDIA_ALT_IMAGE_ENABLE, TRUE, PRIM_MEDIA_CONTROLS, PRIM_MEDIA_CONTROLS_MINI,\n"
            "        PRIM_MEDIA_CURRENT_URL, \"http://a\", PRIM_MEDIA_HOME_URL, \"http://b\", PRIM_MEDIA_AUTO_LOOP, TRUE, PRIM_MEDIA_AUTO_PLAY, TRUE,\n"
            "        PRIM_MEDIA_AUTO_SCALE, FALSE, PRIM_MEDIA_AUTO_ZOOM, FALSE, PRIM_MEDIA_FIRST_CLICK_INTERACT, TRUE,\n"
            "        PRIM_MEDIA_WIDTH_PIXELS, 640, PRIM_MEDIA_HEIGHT_PIXELS, 480, PRIM_MEDIA_WHITELIST_ENABLE, TRUE,\n"
            "        PRIM_MEDIA_WHITELIST, \"a.com,b.com\", PRIM_MEDIA_PERMS_INTERACT, PRIM_MEDIA_PERM_ANYONE, PRIM_MEDIA_PERMS_CONTROL, PRIM_MEDIA_PERM_OWNER]);\n"
            "    llHTTPRequest(\"https://x\", [HTTP_METHOD, \"PUT\", HTTP_MIMETYPE, \"text/plain\", HTTP_BODY_MAXLENGTH, 4096, HTTP_VERIFY_CERT, TRUE,\n"
            "        HTTP_VERBOSE_THROTTLE, FALSE, HTTP_CUSTOM_HEADER, \"X-A\", \"1\", HTTP_PRAGMA_NO_CACHE, TRUE, HTTP_USER_AGENT, \"me\",\n"
            "        HTTP_ACCEPT, \"text/plain\", HTTP_EXTENDED_ERROR, TRUE], \"\");\n"
            "} }\n");
        CAPTURE(r.text);
        // The keys each table was written with, a line each.
        std::map<std::string, std::set<std::string>> written;
        {
            std::istringstream in(r.text);
            std::string        text;
            std::string        type;
            while (std::getline(in, text))
            {
                type = text.find("ParticleSystem(") != std::string::npos      ? "ParticleParams"
                       : text.find("ll.SetLinkMedia(") != std::string::npos   ? "MediaParams"
                       : text.find("ll.HTTPRequest(") != std::string::npos    ? "HttpRequestParams"
                                                                              : type;
                const size_t equals = text.find(" = ");
                if (text.rfind("        ", 0) == 0 && equals != std::string::npos)
                {
                    written[type].insert(text.substr(8, equals - 8));
                }
            }
        }
        // And the keys each of the definitions' types has.
        llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
        std::stringstream defs;
        defs << in.rdbuf();
        for (const char* type : { "ParticleParams", "MediaParams", "HttpRequestParams" })
        {
            const std::string text  = defs.str();
            const size_t      begin = text.find(std::string("export type ") + type + " = {");
            UNSCOPED_INFO("the definitions have " << type);
            REQUIRE(begin != std::string::npos);
            const std::string block = text.substr(begin, text.find("\n}", begin) - begin);
            std::set<std::string> fields;
            std::istringstream    lines(block);
            std::string           line;
            std::getline(lines, line);
            while (std::getline(lines, line))
            {
                const size_t colon = line.find(':');
                if (line.rfind("  ", 0) == 0 && colon != std::string::npos)
                {
                    fields.insert(line.substr(2, colon - 2));
                }
            }
            std::string missing;
            for (const std::string& key : fields)
            {
                missing += written[type].contains(key) ? "" : " " + key;
            }
            std::string unknown;
            for (const std::string& key : written[type])
            {
                unknown += fields.contains(key) ? "" : " " + key;
            }
            UNSCOPED_INFO(type << " has every key written:" << unknown);
            REQUIRE(unknown.empty());
            UNSCOPED_INFO(type << " written with every key it has:" << missing);
            REQUIRE(missing.empty());
        }
        require_checks_clean(r);
        if (!newSolver)
        {
            std::string said;
            for (const ALScriptProblem& p : service.check("--!strict\n" + r.text))
            {
                said += p.severity == ALScriptProblem::Severity::Error ? p.message + "\n" : std::string();
            }
            CAPTURE(said);
            CHECK(said.empty());
        }
    }
}

namespace
{
    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a list written over several lines keeps its lines and comments", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("list gColours = [\n"
                                                        "    // warm\n"
                                                        "    \"red\", \"orange\",\n"
                                                        "    /* cool */ \"blue\" // last\n"
                                                        "];\n"
                                                        "list gTwo = [1, 2,\n"
                                                        "             3]; // three\n"
                                              "default { touch_start(integer n) {\n"
                                              "       string activeRequest = llList2Json(JSON_OBJECT, [\n"
                                              "            \"fn\", \"SELECT\",\n"
                                              "            \"radius\", 10,\n"
                                              "            \"ima\", llList2Json(JSON_ARRAY, [\"Ungulate\"]),\n"
                                              "            \"ownerOnly\", TRUE, // only the owner\n"
                                              "            \"toucher\", llGetOwner(),\n"
                                              "            \"requestor\", llList2Json(JSON_OBJECT, [\n"
                                              "                \"id\", llGetKey(),\n"
                                              "                \"ima\", \"[]\",\n"
                                              "                \"playsWith\", \"[]\"\n"
                                              "            ]),\n"
                                              "            \"responders\", \"[]\"\n"
                                              "        ]); // sent\n"
                                              "       llSay(0, activeRequest + llList2CSV(gColours + gTwo + [n, 1]));\n"
                                              "       list m;\n"
                                              "       llSay(0, (string)llGetListLength(m = [1,\n"
                                              "                                             2]));\n"
                                              "       while (llGetListLength(m = [3,\n"
                                              "                                   4]) < 0) {}\n"
                                              "       if (~llListFindList([\"a\", // first\n"
                                              "                            \"b\"], [activeRequest])) llSay(0, \"found\");\n"
                                              "       llParticleSystem([\n"
                                              "           PSYS_PART_FLAGS, 0, // none\n"
                                              "           PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_DROP\n"
                                              "       ]);\n"
                                              "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a global's, its comments where they were");
        CHECK(has(r, "local gColours = {\n    -- warm\n    \"red\", \"orange\",\n    --[[ cool ]] \"blue\" -- last\n}\n"));
        UNSCOPED_INFO("closed on the line it closed on, a comment after it trailing that");
        CHECK(has(r, "local gTwo = { 1, 2,\n    3 } -- three\n"));
        UNSCOPED_INFO("Tapple's, as the LSL had it");
        CHECK(has(r, "    local activeRequest = ll.List2Json(JSON_OBJECT, {\n"
                      "        \"fn\", \"SELECT\",\n"
                      "        \"radius\", 10,\n"
                      "        \"ima\", ll.List2Json(JSON_ARRAY, { \"Ungulate\" }),\n"
                      "        \"ownerOnly\", 1, -- only the owner\n"
                      "        \"toucher\", ll.GetOwner(),\n"
                      "        \"requestor\", ll.List2Json(JSON_OBJECT, {\n"
                      "            \"id\", ll.GetKey(),\n"
                      "            \"ima\", \"[]\",\n"
                      "            \"playsWith\", \"[]\"\n"
                      "        }),\n"
                      "        \"responders\", \"[]\"\n"
                      "    }) -- sent\n"));
        {
            INFO("each comment once");
            CHECK(count(r, "only the owner") == 1);
            CHECK(count(r, "-- sent") == 1);
            CHECK(count(r, "-- first") == 1);
        }
        UNSCOPED_INFO("in a line made inside an expression, one line");
        CHECK(has(r, "(function() m = { 3, 4 } return m end)()"));
        UNSCOPED_INFO("in a condition");
        CHECK(has(r, "    if table.find({ \"a\", -- first\n        \"b\" }"));
        UNSCOPED_INFO("one written another way, its comment over it");
        CHECK(has(r, "    -- none\n    ll.ParticleSystem({\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a detected event's count only declared where the body names it", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("selectHorse() {}\n"
                                                        "default {\n"
                                                        "    touch_start (integer num) {\n"
                                                        "        if (llDetectedKey(0) == llGetOwner()) {\n"
                                                        "            selectHorse();\n"
                                                        "        }\n"
                                                        "    }\n"
                                                        "    collision_start(integer n) { llOwnerSay((string)n); }\n"
                                                        "    sensor(integer found) { found = 0; }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("none that nothing reads");
            CHECK(has(r, "LLEvents:on(\"touch_start\", function(detected)\n    -- LSL: detected[n]"));
            CHECK_FALSE(has(r, "local num"));
        }
        UNSCOPED_INFO("one read");
        CHECK(has(r, "function(detected)\n    local n = #detected\n    print(tostring(n))\n"));
        UNSCOPED_INFO("one set, which needs it there");
        CHECK(has(r, "    local found = #detected\n    found = 0\n"));
        // Unused, of what Luau says: only what the LSL itself never read.
        std::string unused;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            unused += p.code == "LocalUnused" ? p.message + "\n" : std::string();
        }
        CHECK(unused == std::string("Variable 'found' is never used; prefix with '_' to silence\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: text where ll takes a key passed as it is with :: any and noted once", "[ALLSLToSLua]")
    {
        const std::string lsl = "string gJson;\n"
                                "integer mine(key who) { return who == llGetOwner(); }\n"
                                "default { touch_start(integer n) {\n"
                                "    llOwnerSay(\"selected: \" + llKey2Name(llJsonGetValue(gJson, [\"selected\"])));\n"
                                "    llOwnerSay(llKey2Name(\"a2e76fcd-9360-4f6d-a924-\" + \"938f923df11d\"));\n"
                                "    llOwnerSay(llKey2Name(\"a2e76fcd-9360-4f6d-a924-938f923df11d\"));\n"
                                "    if (mine(llJsonGetValue(gJson, [\"owner\"]))) llOwnerSay(\"mine\");\n"
                                "} }\n";
        const ALLSLToSLua::Result r = require_converted(lsl);
        CAPTURE(r.text);
        UNSCOPED_INFO("Tapple's, as it is");
        CHECK(has(r, "ll.Key2Name(ll.JsonGetValue(gJson, { \"selected\" }) :: any)"));
        UNSCOPED_INFO("bracketed where it must be");
        CHECK(has(r, "ll.Key2Name((\"a2e76fcd-9360-4f6d-a924-\" .. \"938f923df11d\") :: any)"));
        UNSCOPED_INFO("a UUID written out, a uuid");
        CHECK(has(r, "ll.Key2Name(uuid(\"a2e76fcd-9360-4f6d-a924-938f923df11d\"))"));
        {
            INFO("the script's own function's key, uuid()");
            CHECK(has(r, "mine(uuid(ll.JsonGetValue(gJson, { \"owner\" })))"));
            CHECK(noted(r, "SluaUuidText"));
        }
        UNSCOPED_INFO("said once");
        CHECK(count(r, "-- LSL: SLua's ll takes a key as text") == 1);
        require_checks_clean(r);
        if (!newSolver)
        {
            // ll's calls alone, strict.
            const ALLSLToSLua::Result calls = require_converted("default { touch_start(integer n) {\n"
                                                                "    llOwnerSay(llKey2Name(llJsonGetValue(\"{}\", [\"selected\"])));\n"
                                                                "    llOwnerSay((string)llGetOwnerKey(\"a\" + (string)n));\n"
                                                                "} }\n");
            std::string said;
            for (const ALScriptProblem& p : service.check("--!strict\n" + calls.text))
            {
                said += p.severity == ALScriptProblem::Severity::Error ? p.message + "\n" : std::string();
            }
            CAPTURE(said, calls.text);
            CHECK(said.empty());
            CHECK(calls.text.find(":: any") != std::string::npos);
        }

        const ALLSLToSLua::Result close = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("llcompat's the same");
            CHECK(close.converted);
            CHECK(close.text.find(":: any)") != std::string::npos);
        }
        require_checks_clean(close);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: deprecated calls noted with what to use; XorBase64Strings its own note", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    llMakeFire(10, 1.0, 1.0, 1.0, PI, \"\", ZERO_VECTOR);\n"
                                                        "    llSoundPreload(\"boom\");\n"
                                                        "    llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 1.0]);\n"
                                                        "    llOwnerSay(llXorBase64Strings(\"YQ==\", \"Yg==\"));\n"
                                                        "    llTakeCamera(llGetOwner());\n"
                                                        "} }\n");
        CAPTURE(r.text);
        const auto named = [&r](const std::string& bare, const std::string& use) {
            return std::any_of(r.notes.begin(), r.notes.end(), [&](const ALScriptProblem& p) {
                return p.key == "SluaDeprecatedFor" && p.args.size() == 2 && p.args[0] == bare && p.args[1] == use;
            });
        };
        {
            INFO("each, for what it names");
            CHECK(named("MakeFire", "ll.ParticleSystem"));
            CHECK(named("SoundPreload", "ll.PreloadSound"));
            CHECK(named("SetPrimitiveParams", "ll.SetLinkPrimitiveParamsFast"));
            CHECK(named("TakeCamera", "ll.SetCameraParams"));
        }
        {
            INFO("XorBase64Strings' own");
            CHECK(noted(r, "SluaXorBase64Wrong"));
            CHECK_FALSE(named("XorBase64Strings", "ll.XorBase64"));
        }
        UNSCOPED_INFO("said so");
        CHECK(has(r, "-- LSL: SLua deprecates ll.MakeFire, for ll.ParticleSystem.\n"));
        UNSCOPED_INFO("none said bare");
        CHECK_FALSE(noted(r, "SluaDeprecated"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a find's place against a literal compared in SLua's count from 1", "[ALLSLToSLua]")
    {
        const std::string lsl = "string P = \"cfg\";\n"
                                "list L;\n"
                                "check(string item) {\n"
                                "    if (P != \"\" && llSubStringIndex(item, P) == 0) llOwnerSay(\"a\");\n"
                                "    if (0 == llSubStringIndex(item, P)) llOwnerSay(\"b\");\n"
                                "    if (llSubStringIndex(item, P) != 2) llOwnerSay(\"c\");\n"
                                "    if (llSubStringIndex(item, P) > 0) llOwnerSay(\"d\");\n"
                                "    if (3 < llSubStringIndex(item, P)) llOwnerSay(\"e\");\n"
                                "    if (llSubStringIndex(item, P) == -2) llOwnerSay(\"f\");\n"
                                "    if (!(llSubStringIndex(item, P) == 0)) llOwnerSay(\"g\");\n"
                                "    if (llListFindList(L, [item]) == 0) llOwnerSay(\"h\");\n"
                                "    integer at = llSubStringIndex(item, P);\n"
                                "    llOwnerSay((string)at);\n"
                                "}\n"
                                "default { touch_start(integer n) { check(\"x\"); } }\n";
        const ALLSLToSLua::Result r = require_converted(lsl);
        CAPTURE(r.text);
        UNSCOPED_INFO("Tapple's");
        CHECK(has(r, "if P ~= \"\" and ll.SubStringIndex(item, P) == 1 then"));
        UNSCOPED_INFO("the number on the left");
        CHECK(has(r, "if ll.SubStringIndex(item, P) == 1 then print(\"b\")"));
        UNSCOPED_INFO("~=");
        CHECK(has(r, "if ll.SubStringIndex(item, P) ~= 3 then"));
        UNSCOPED_INFO("an order, nil read as LSL's -1");
        CHECK(has(r, "if (ll.SubStringIndex(item, P) or 0) > 1 then"));
        UNSCOPED_INFO("an order the other way round");
        CHECK(has(r, "if (ll.SubStringIndex(item, P) or 0) > 4 then"));
        UNSCOPED_INFO("below -1, as written");
        CHECK(has(r, "if (ll.SubStringIndex(item, P) or 0) - 1 == -2 then"));
        UNSCOPED_INFO("under a not");
        CHECK(has(r, "if ll.SubStringIndex(item, P) ~= 1 then print(\"g\")"));
        UNSCOPED_INFO("table.find's");
        CHECK(has(r, "if table.find(L, item) == 1 then"));
        UNSCOPED_INFO("a number, LSL's");
        CHECK(has(r, "local at = (ll.SubStringIndex(item, P) or 0) - 1"));
        require_checks_clean(r);

        // table.find's alone, without SLua's ll: whether it found, against nil.
        ALLSLToSLua::Options idioms = ALLSLToSLua::Options::closeToLSL();
        idioms.idioms               = true;
        const ALLSLToSLua::Result found =
            ALLSLToSLua::convert("list L;\ndefault { touch_start(integer n) {\n"
                                 "    if (llListFindList(L, [n]) == -1) llOwnerSay(\"none\");\n"
                                 "    if (llListFindList(L, [n]) >= 0) llOwnerSay(\"some\");\n"
                                 "    if (llListFindList(L, [n]) == 2) llOwnerSay(\"third\");\n"
                                 "} }\n",
                                 idioms);
        CAPTURE(found.text);
        {
            INFO("found, against nil");
            CHECK(found.converted);
            CHECK(has(found, "if table.find(L, n) == nil then"));
            CHECK(has(found, "if table.find(L, n) ~= nil then"));
            CHECK(has(found, "if table.find(L, n) == 3 then"));
        }
        require_checks_clean(found);

        const ALLSLToSLua::Result close = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("llcompat's, LSL's count");
            CHECK(close.converted);
            CHECK(has(close, "llcompat.SubStringIndex(item, P) == 0"));
        }
        require_checks_clean(close);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: blank lines between statements kept singly and not at a block's ends", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer a = 1;\n"
                                                        "integer b = 2;\n"
                                                        "\n"
                                                        "\n"
                                                        "integer c = 3;\n"
                                                        "integer preCheck(key avatar) {\n"
                                                        "\n"
                                                        "    // only allow my owner to request contents\n"
                                                        "    if (avatar != llGetOwner()) return FALSE;\n"
                                                        "\n"
                                                        "    return TRUE;\n"
                                                        "}\n"
                                                        "default {\n"
                                                        "    touch_start(integer n) {\n"
                                                        "        if (preCheck(llDetectedKey(0))) {\n"
                                                        "            llOwnerSay((string)a);\n"
                                                        "\n"
                                                        "            llOwnerSay((string)b);\n"
                                                        "\n"
                                                        "        }\n"
                                                        "\n"
                                                        "        // said apart\n"
                                                        "        llOwnerSay((string)c);\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("the globals");
        CHECK(has(r, "local a = 1\nlocal b = 2\n\nlocal c = 3\n"));
        UNSCOPED_INFO("Tapple's");
        CHECK(has(r, "    -- only allow my owner to request contents\n    if avatar ~= ll.GetOwner() then return false end\n\n    return true\nend"));
        UNSCOPED_INFO("none at a block's start");
        CHECK(has(r, "(avatar)\n    -- only allow"));
        UNSCOPED_INFO("in a block, none at its end");
        CHECK(has(r, "        print(tostring(a))\n\n        print(tostring(b))\n    end\n"));
        UNSCOPED_INFO("one over a comment kept apart");
        CHECK(has(r, "    end\n\n    -- said apart\n    print(tostring(c))\n"));
        UNSCOPED_INFO("never two");
        CHECK_FALSE(has(r, "\n\n\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: table braces spaced as asked as the formatter spaces them", "[ALLSLToSLua]")
    {
        const std::string lsl = "list gTwo = [1, 2,\n    3];\n"
                                "default {\n"
                                "    state_entry() { state two; }\n"
                                "}\n"
                                "state two {\n"
                                "    listen(integer c, string name, key id, string msg) {\n"
                                "        list params = llParseStringKeepNulls(msg, [\"\\n\"], []);\n"
                                "        llSetPrimMediaParams(0, [PRIM_MEDIA_AUTO_PLAY, TRUE, PRIM_MEDIA_WHITELIST, \"a.com,b.com\"]);\n"
                                "        llOwnerSay(\"n=\" + (string)llGetListLength(params) + \" \" + (string)<1, 2, 3>);\n"
                                "    }\n"
                                "}\n";
        ALLSLToSLua::Options typed;
        typed.types                 = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, typed);
        CAPTURE(r.text);
        {
            INFO("Tapple's");
            CHECK(r.converted);
            CHECK(has(r, "local params: { any } = ll.ParseStringKeepNulls(msg, { \"\\n\" }, {})"));
        }
        UNSCOPED_INFO("a rule table");
        CHECK(has(r, "ll.SetPrimMediaParams(0, { auto_play = true, whitelist = { \"a.com\", \"b.com\" } })"));
        UNSCOPED_INFO("the states' type");
        CHECK(has(r, "local states: { [string]: { [string]: (...any) -> () } } = {}"));
        UNSCOPED_INFO("over lines");
        CHECK(has(r, "local gTwo: { any } = { 1, 2,\n    3 }\n"));
        UNSCOPED_INFO("an interpolated string's");
        CHECK(has(r, "`n={#params} {ll.DumpList2String({ vector(1, 2, 3) }, \"\")}`"));
        require_checks_clean(r);

        typed.braceSpaces              = false;
        const ALLSLToSLua::Result none = ALLSLToSLua::convert(lsl, typed);
        CAPTURE(none.text);
        {
            INFO("none");
            CHECK(none.converted);
            CHECK(has(none, "local params: {any} = ll.ParseStringKeepNulls(msg, {\"\\n\"}, {})"));
            CHECK(has(none, "ll.SetPrimMediaParams(0, {auto_play = true, whitelist = {\"a.com\", \"b.com\"}})"));
            CHECK(has(none, "local states: {[string]: {[string]: (...any) -> ()}} = {}"));
            CHECK(has(none, "local gTwo: {any} = {1, 2,\n    3}\n"));
            CHECK_FALSE(has(none, "{ "));
        }
        require_checks_clean(none);

        // As the formatter would leave them, either way.
        for (bool spaces : { true, false })
        {
            ALScriptFormatter::Options formatter;
            formatter.lua                  = true;
            formatter.braceSpaces          = spaces;
            const ALLSLToSLua::Result same = spaces ? r : none;
            CAPTURE(spaces);
            REQUIRE(ALScriptFormatter::format(same.text, formatter) == same.text);
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: converted SLua is as format on save leaves it with either brace spacing", "[ALLSLToSLua]")
    {
        const std::string lsl = "integer CHANNEL    = 1;    // channel\n"
                                "integer OWNER_ONLY = 0;    // owner\n"
                                "float   TIMEOUT    = 2.5;  // seconds\n"
                                "integer asInt(string s) { return (integer)s; }\n"
                                "default {\n"
                                "    touch_start(integer n) {\n"
                                "        integer b;\n"
                                "        if (n == 1)\n"
                                "            llOwnerSay(\"a\"); // after a\n"
                                "        else if (n == 2)\n"
                                "            llOwnerSay(\"b\"); // after b\n"
                                "        else\n"
                                "            llOwnerSay(\"c\"); // after c\n"
                                "        if ((b = 1) == 2);    // an empty body\n"
                                "        else llOwnerSay(\"f\"); // after f\n"
                                "        while (n > 5)\n"
                                "            n = n - 1; // after the step\n"
                                "        if (n) {\n"
                                "            llOwnerSay(\"d\");\n"
                                "        }\n"
                                "        // over the else\n"
                                "        else {\n"
                                "            llOwnerSay(\"e\");\n"
                                "        }\n"
                                "        do ; while ((b = b - 1) > 0);\n"
                                "        llOwnerSay((string)asInt(\"3\") + (string)CHANNEL + (string)OWNER_ONLY + (string)TIMEOUT);\n"
                                "    }\n"
                                "}\n";
        for (bool spaces : { true, false })
        {
            CAPTURE(spaces);
            ALLSLToSLua::Options options;
            options.braceSpaces         = spaces;
            const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, options);
            CAPTURE(r.text);
            REQUIRE(r.converted);
            {
                INFO("each after its own");
                REQUIRE(has(r, "        print(\"a\") -- after a\n    elseif"));
                REQUIRE(has(r, "        print(\"b\") -- after b\n    else"));
                REQUIRE(has(r, "        print(\"c\") -- after c\n    end"));
            }
            UNSCOPED_INFO("an empty body's, after the line that opens it");
            REQUIRE(has(r, " == 2 then -- an empty body\n    else\n        print(\"f\") -- after f\n"));
            UNSCOPED_INFO("a loop's body's");
            REQUIRE(has(r, "        n -= 1 -- after the step\n    end"));
            UNSCOPED_INFO("between a block and its else, over what the else runs");
            REQUIRE(has(r, "    else\n        -- over the else\n        print(\"e\")\n"));
            UNSCOPED_INFO("lined up");
            REQUIRE(has(r, "local CHANNEL    = 1   -- channel\nlocal OWNER_ONLY = 0   -- owner\nlocal TIMEOUT    = 2.5 -- seconds\n"));
            {
                INFO("one line break at the end");
                REQUIRE(r.text.size() > 2);
                REQUIRE(r.text.compare(r.text.size() - 2, 2, "\n\n") != 0);
                REQUIRE(r.text.back() == '\n');
            }
            ALScriptFormatter::Options formatter;
            formatter.lua         = true;
            formatter.braceSpaces = spaces;
            REQUIRE(ALScriptFormatter::format(r.text, formatter) == r.text);
            require_checks_clean(r);
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: the //* toggle with a word after its star as two SLua line comments", "[ALLSLToSLua]")
    {
        ALLSLToSLua::Options options;
        options.types               = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert("//* OVERRIDE\n"
                                                           "integer allowAttachedAO;\n"
                                                           "//*/\n"
                                                           "default { state_entry() { if (allowAttachedAO) llOwnerSay(\"on\"); } }\n",
                                                           options);
        CAPTURE(r.text);
        CHECK(r.converted);
        UNSCOPED_INFO("the toggle");
        CHECK(has(r, "\n---[[ OVERRIDE\nlocal allowAttachedAO: boolean = false\n--]]\n"));
        UNSCOPED_INFO("the opener a line comment");
        CHECK(require_comment_at(r.text, "---[[") == std::string("---[[ OVERRIDE"));
        UNSCOPED_INFO("the closer one");
        CHECK(require_comment_at(r.text, "--]]") == std::string("--]]"));
        UNSCOPED_INFO("turned off");
        CHECK(require_toggled(r.text, "---[[") == std::string("--[[ OVERRIDE\nlocal allowAttachedAO: boolean = false\n--]]"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: half a toggle: an opener only before a closer and openers share it", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result lone = require_converted("integer a;\n"
                                                           "//* nothing closes this\n"
                                                           "integer b;\n"
                                                           "default { state_entry() { llOwnerSay((string)(a + b)); } }\n");
        CAPTURE(lone.text);
        {
            INFO("a lone opener a comment with a star");
            CHECK(has(lone, "\n--* nothing closes this\nlocal b = 0\n"));
            CHECK_FALSE(has(lone, "---["));
        }
        require_checks_clean(lone);

        const ALLSLToSLua::Result r = require_converted("//* not one */\n"
                                                        "//**************\n"
                                                        "integer c = 1;\n"
                                                        "//* one\n"
                                                        "integer d = 2;\n"
                                                        "//* two\n"
                                                        "integer e = 3;\n"
                                                        "//*/ // both\n"
                                                        "//*/\n"
                                                        "default { state_entry() { llOwnerSay((string)(c + d + e)); } }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("closed on its line");
        CHECK(has(r, "\n--* not one */\n"));
        UNSCOPED_INFO("a rule of stars");
        CHECK(has(r, "\n--**************\n"));
        UNSCOPED_INFO("two openers and a closer, then one alone");
        CHECK(has(r, "\n---[[ one\nlocal d = 2\n---[[ two\nlocal e = 3\n--]] -- both\n--]]\n"));
        UNSCOPED_INFO("the first off");
        CHECK(require_toggled(r.text, "---[[ one") == std::string("--[[ one\nlocal d = 2\n---[[ two\nlocal e = 3\n--]]"));
        UNSCOPED_INFO("the second off");
        CHECK(require_toggled(r.text, "---[[ two") == std::string("--[[ two\nlocal e = 3\n--]]"));
        std::string off = r.text;
        off.erase(off.find("---[[ one"), 1);
        UNSCOPED_INFO("the closer's own comment after it, off");
        CHECK(require_comment_at(off, "-- both") == std::string("-- both"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: the toggle the other way round as a block comment SLua can turn on", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("/* OVERRIDE\n"
                                                        "integer allowAttachedAO;\n"
                                                        "//*/\n"
                                                        "//* A\n"
                                                        "integer x = 1;\n"
                                                        "/* B\n"
                                                        "integer y = 2;\n"
                                                        "//*/\n"
                                                        "default { state_entry() { llOwnerSay((string)x); } }\n");
        UNSCOPED_INFO("one block comment");
        CHECK(require_comment_at(r.text, "--[[ OVERRIDE") == std::string("--[[ OVERRIDE\ninteger allowAttachedAO;\n--]]"));
        // What it holds stays LSL: nothing says it is code.
        std::string on = r.text;
        on.insert(on.find("--[[ OVERRIDE"), "-");
        UNSCOPED_INFO("turned on, the opener a line comment");
        CHECK(require_comment_at(on, "---[[ OVERRIDE") == std::string("---[[ OVERRIDE"));
        UNSCOPED_INFO("and the closer");
        CHECK(require_comment_at(on, "--]]") == std::string("--]]"));
        UNSCOPED_INFO("the opener over code off, to the closer of the one after it");
        CHECK(require_toggled(r.text, "---[[ A") ==
              std::string("--[[ A\nlocal x = 1\n\n--[[ B\ninteger y = 2;\n--]]"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a toggle's level is the fewest =s that nothing inside closes", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("//* OVERRIDE\n"
                                                        "string s = \"]]\";\n"
                                                        "//*/\n"
                                                        "default { state_entry() { llOwnerSay(s); } }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("past a string's ]]");
        CHECK(has(r, "\n---[=[ OVERRIDE\nlocal s = \"]]\"\n--]=]\n"));
        CHECK(require_toggled(r.text, "---[=[") == std::string("--[=[ OVERRIDE\nlocal s = \"]]\"\n--]=]"));
        require_checks_clean(r);

        const ALLSLToSLua::Result shared = require_converted("//* A\n"
                                                             "integer x = 1; /* one */\n"
                                                             "/* B\n"
                                                             "string s = \"]]\";\n"
                                                             "//*/\n"
                                                             "default { state_entry() { llOwnerSay((string)x); } }\n");
        CAPTURE(shared.text);
        {
            INFO("one level for the pair");
            CHECK(has(shared, "\n---[=[ A\nlocal x = 1 --[[ one ]]\n"));
            CHECK(has(shared, "\n--[=[ B\nstring s = \"]]\";\n--]=]\n"));
        }
        UNSCOPED_INFO("the opener off");
        CHECK(require_toggled(shared.text, "---[=[ A") ==
              std::string("--[=[ A\nlocal x = 1 --[[ one ]]\n\n--[=[ B\nstring s = \"]]\";\n--]=]"));
        require_checks_clean(shared);

        std::string every = "// ";
        for (char c = 1; c < ' '; ++c)
        {
            if (c != '\t' && c != '\n' && c != '\r')
            {
                every += c;
            }
        }
        const ALLSLToSLua::Result unmarked = require_converted(every + "\ninteger y;\n//* A\ninteger x = 1;\n//*/\n"
                                                                       "default { state_entry() { llOwnerSay((string)(x + y)); } }\n");
        CAPTURE(unmarked.text);
        UNSCOPED_INFO("paired still");
        CHECK(has(unmarked, "\n---[[ A\nlocal x = 1\n--]]\n"));
        UNSCOPED_INFO("no byte of its own left in");
        CHECK(count(unmarked, std::string(1, '\x01')) == size_t(1));
        require_checks_clean(unmarked);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a toggle after code or in a handler or at the top above the helpers", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() {\n"
                                                        "    integer a = 1; //* A\n"
                                              "    llOwnerSay((string)a); //*/\n"
                                              "    //* DEBUG\n"
                                              "    llOwnerSay(\"debug\");\n"
                                              "    //*/\n"
                                              "    llOwnerSay(\"done\");\n"
                                              "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("after code");
        CHECK(has(r, "\nlocal a = 1 ---[[ A\nprint(tostring(a)) --]]\n"));
        UNSCOPED_INFO("off from it");
        CHECK(require_toggled(r.text, "---[[ A") == std::string("--[[ A\nprint(tostring(a)) --]]"));
        UNSCOPED_INFO("in a handler");
        CHECK(has(r, "\n---[[ DEBUG\nprint(\"debug\")\n--]]\nprint(\"done\")\n"));
        CHECK(require_toggled(r.text, "---[[ DEBUG") == std::string("--[[ DEBUG\nprint(\"debug\")\n--]]"));
        require_checks_clean(r);

        const ALLSLToSLua::Result top = require_converted("//* DEBUG\n"
                                                          "\n"
                                                          "vector v = <1, 2, 3>;\n"
                                                          "//*/\n"
                                                          "default { state_entry() { llOwnerSay((string)llVecNorm(v)); } }\n");
        CAPTURE(top.text);
        {
            INFO("the helper over it");
            CHECK(has(top, "local function vecNorm("));
            CHECK(top.text.find("local function vecNorm(") < top.text.find("---[[ DEBUG"));
        }
        CHECK(require_toggled(top.text, "---[[ DEBUG") == std::string("--[[ DEBUG\nlocal v = vector(1, 2, 3)\n--]]"));
        require_checks_clean(top);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a comment after an unbraced body trails the statement ending its line", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { touch_start(integer n) {\n"
                                                        "    integer a; integer b;\n"
                                                        "    if (n == 1)\n"
                                                        "        llOwnerSay(\"a\"); // after a\n"
                                              "    else if (n == 2)\n"
                                              "        llOwnerSay(\"b\"); // after b\n"
                                              "    else\n"
                                              "        llOwnerSay(\"c\"); // after c\n"
                                              "    if ( (b = 1) == 2 );    // empty\n"
                                              "    else if ( b == 2 );     // empty too\n"
                                              "    else a = 2;             // after a = 2\n"
                                              "    if (a)\n"
                                              "        if (b) llOwnerSay(\"x\"); // inner, one line\n"
                                              "    while (n > 5)\n"
                                              "        n = n - 1; // after the step\n"
                                              "    while (a) ; // an empty loop\n"
                                              "    if (n == 5) llOwnerSay(\"h\"); // after h\n"
                                              "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("a then's, an elseif's, an else's");
        CHECK(has(r, "    if n == 1 then\n        print(\"a\") -- after a\n    elseif n == 2 then\n        print(\"b\") -- after b\n"
                      "    else\n        print(\"c\") -- after c\n    end\n"));
        UNSCOPED_INFO("an empty body's, the line that opens it");
        CHECK(has(r, " == 2 then -- empty\n    elseif b == 2 then -- empty too\n    else\n        a = true -- after a = 2\n    end\n"));
        UNSCOPED_INFO("an if on one line inside another");
        CHECK(has(r, "        if b ~= 0 then print(\"x\") end -- inner, one line\n"));
        {
            INFO("a loop's body's");
            CHECK(has(r, "        n -= 1 -- after the step\n    end\n"));
            CHECK(has(r, "    while a do -- an empty loop\n    end\n"));
        }
        UNSCOPED_INFO("a one-line if, as before");
        CHECK(has(r, "    if n == 5 then print(\"h\") end -- after h\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a float cut to an integer is math.modf bracketed where its values spread", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("integer menuChannel;\n"
                                                        "vector gV;\n"
                                                        "integer cut(float f) { return (integer)f; }\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    float f = llFrand(10);\n"
                                                        "    menuChannel = (integer)llFrand(1000) - 10000;\n"
                                                        "    integer x = (integer)f;\n"
                                                        "    x = (integer)f;\n"
                                                        "    x += (integer)f;\n"
                                                        "    x = -(integer)f;\n"
                                                        "    if ((integer)f == 3) llOwnerSay((string)((integer)f));\n"
                                                        "    llOwnerSay(llList2CSV([(integer)f]));\n"
                                                        "    gV.z = (integer)f;\n"
                                                        "    llOwnerSay((string)(cut(f) + x + menuChannel) + (string)gV);\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("Tapple's");
        CHECK(has(r, "menuChannel = math.modf(ll.Frand(1000)) - 10000\n"));
        UNSCOPED_INFO("a local");
        CHECK(has(r, "local x = math.modf(f)\n"));
        UNSCOPED_INFO("assigned");
        CHECK(has(r, "    x = math.modf(f)\n"));
        UNSCOPED_INFO("added");
        CHECK(has(r, "    x += math.modf(f)\n"));
        UNSCOPED_INFO("its negative");
        CHECK(has(r, "    x = -math.modf(f)\n"));
        UNSCOPED_INFO("compared");
        CHECK(has(r, "if math.modf(f) == 3 then"));
        UNSCOPED_INFO("an argument");
        CHECK(has(r, "print(tostring((math.modf(f))))"));
        UNSCOPED_INFO("a list's item");
        CHECK(has(r, "{ (math.modf(f)) }"));
        UNSCOPED_INFO("returned");
        CHECK(has(r, "return (math.modf(f))"));
        UNSCOPED_INFO("a vector's part, an argument");
        CHECK(has(r, "gV = vector(gV.x, gV.y, (math.modf(f)))"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a local whose Luau name would hide another is named apart", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("f1(string f2) {\n"
                                                        "    llOwnerSay(f2);\n"
                                                        "    f2();\n"
                                                        "}\n"
                                                        "\n"
                                                        "f2() {\n"
                                                        "    llOwnerSay(\"world\");\n"
                                                        "}\n"
                                                        "\n"
                                                        "default { state_entry() {\n"
                                                        "    f1(\"hello\");\n"
                                                        "} }\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("Tapple's, the parameter named apart");
        CHECK(has(r, "local function f1(f2_)\n    print(f2_)\n    f2()\nend\n"));
        {
            INFO("the function as it was");
            CHECK(has(r, "local f2\n"));
            CHECK(has(r, "function f2()\n"));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result detected = require_converted("integer detected() {\n"
                                                               "    return 5;\n"
                                                               "}\n"
                                                               "\n"
                                                               "default\n"
                                                               "{\n"
                                                               "    touch_start(integer total_number)\n"
                                                               "    {\n"
                                                               "        string detected = \"fritz\";\n"
                                                               "\n"
                                                               "        llOwnerSay(llDetectedName(0));\n"
                                                               "        llOwnerSay(detected);\n"
                                                               "        llOwnerSay((string)(detected() * detected()));\n"
                                                               "    }\n"
                                                               "}\n");
        CAPTURE(detected.text);
        UNSCOPED_INFO("Tapple's detected, the function marked");
        CHECK(has(detected, "local function detected_()"));
        {
            INFO("the local apart from it");
            CHECK(has(detected, "local detected_2 = \"fritz\""));
            CHECK(has(detected, "print(detected_2)"));
        }
        UNSCOPED_INFO("the function called");
        CHECK(has(detected, "print(tostring(detected_() * detected_()))"));
        require_checks_clean(detected);

        const ALLSLToSLua::Result own = require_converted("integer fact(integer fact) {\n"
                                                          "    if (fact <= 1) return 1;\n"
                                                          "    return fact * fact(fact - 1);\n"
                                                          "}\n"
                                                          "string table = \"t\";\n"
                                                          "string table_ = \"u\";\n"
                                                          "f3(string f2) { llOwnerSay(f2); }\n"
                                                          "f2() { llOwnerSay(\"f2\"); }\n"
                                                          "f4() { f2(); string f2 = \"x\"; llOwnerSay(f2); }\n"
                                                          "default { touch_start(integer n) {\n"
                                                          "    integer was = 1;\n"
                                                          "    integer k = was++ + was;\n"
                                                          "    llOwnerSay(table + table_ + (string)(fact(n) + k));\n"
                                                          "    f3(\"a\"); f4();\n"
                                                          "} }\n");
        CAPTURE(own.text);
        {
            INFO("a parameter named as its function");
            CHECK(has(own, "local function fact(fact_)"));
            CHECK(has(own, "return fact_ * fact(fact_ - 1)"));
        }
        {
            INFO("a global the mark gave another's name");
            CHECK(has(own, "local table_2 = \"t\"\n"));
            CHECK(has(own, "local table_ = \"u\"\n"));
            CHECK(has(own, "print(`{table_2}{table_}{fact(n) + k}`)"));
        }
        UNSCOPED_INFO("nothing hidden, nothing renamed");
        CHECK(has(own, "local function f3(f2)\n    print(f2)\n"));
        UNSCOPED_INFO("a call before the local, not in its reach");
        CHECK(has(own, "    f2()\n    local f2 = \"x\"\n    print(f2)\n"));
        UNSCOPED_INFO("what a step was, under a name the script does not have");
        CHECK(has(own, "(function() local was2 = was; was += 1 return was2 end)() + was"));
        require_checks_clean(own);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a state's handlers kept apart by blank lines only where the LSL had them", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("default { state_entry() { state collect; } }\n"
                                                        "state collect {\n"
                                                        "    changed(integer change) {\n"
                                                        "        if (change & CHANGED_INVENTORY) {\n"
                                                        "            llOwnerSay(\"c\");\n"
                                                        "        }\n"
                                                        "    }\n"
                                                        "\n"
                                                        "    on_rez(integer param) {\n"
                                                        "        llOwnerSay(\"r\");\n"
                                                        "    }\n"
                                                        "    touch_start(integer n) { llOwnerSay(\"t\"); }\n"
                                                        "\n"
                                                        "    // listening\n"
                                                        "    listen(integer c, string name, key id, string msg) { llOwnerSay(msg); }\n"
                                                        "}\n");
        CAPTURE(r.text);
        UNSCOPED_INFO("Tapple's, apart");
        CHECK(has(r, "        end\n    end,\n\n    on_rez = function(param)\n"));
        UNSCOPED_INFO("together");
        CHECK(has(r, "        print(\"r\")\n    end,\n    touch_start = function(detected)\n"));
        UNSCOPED_INFO("one blank line over a comment over it");
        CHECK(has(r, "    end,\n\n    -- listening\n    listen = function("));
        UNSCOPED_INFO("none at the table's top");
        CHECK(has(r, "states.collect = {\n    changed = function(change)\n"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: LSL's right-side-first order noted only where it could show", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("key gSender;\n"
                                                        "string gCache;\n"
                                                        "string myInfo() { return (string)llGetKey(); }\n"
                                                        "string say() { llOwnerSay(\"x\"); return \"y\"; }\n"
                                                        "string cached() { return gCache; }\n"
                                                        "string fill() { gCache = \"z\"; return gCache; }\n"
                                                        "string other() { return \"o\"; }\n"
                                                        "default { touch_start(integer n) {\n"
                                                        "    llOwnerSay(\"activeRequestor: \" + llKey2Name(gSender) + \"; llGetKey: \" + llKey2Name(myInfo()));\n"
                                                        "    llOwnerSay(llKey2Name(gSender) + say());\n"
                                                        "    llOwnerSay(cached() + fill());\n"
                                                        "    llOwnerSay(other() + fill());\n"
                                                        "} }\n");
        CAPTURE(r.text);
        {
            INFO("Tapple's, two sides that only read");
            CHECK_FALSE(noted(r, "SluaRightFirst"));
            CHECK_FALSE(noted(r, "SluaRightFirstWorld", "myInfo()"));
        }
        {
            INFO("a call that changes the world, named with the other");
            CHECK(noted(r, "SluaRightFirstWorld", "say()"));
            CHECK(has(r, "-- LSL: LSL ran the right side of this before the left, and Luau runs the left first: say() may change what "
                      "llKey2Name() does or finds outside the script.\n    print(ll.Key2Name(gSender) .. say())"));
        }
        {
            INFO("a call that sets what the other's reads");
            CHECK(noted(r, "SluaRightFirstCall", "fill()"));
            CHECK(has(r, "fill() sets gCache, which the other side reads or sets too.\n    print(cached() .. fill())"));
        }
        UNSCOPED_INFO("noted twice, no more");
        CHECK(count(r, "LSL ran the right side") == 2);
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: with keyTextAny off text for a key passes bare and the checker marks it", "[ALLSLToSLua]")
    {
        const std::string lsl = "key inventorySender;\n"
                                "string myInfo() { return (string)llGetKey(); }\n"
                                "default { touch_start(integer n) {\n"
                                "    llOwnerSay(\"activeRequestor: \" + llKey2Name(inventorySender) + \"; llGetKey: \" + llKey2Name(myInfo()));\n"
                                "    llOwnerSay(llKey2Name(\"a2e76fcd-9360-4f6d-a924-938f923df11d\"));\n"
                                "} }\n";
        ALLSLToSLua::Options o;
        o.keyTextAny                = false;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, o);
        CAPTURE(r.text);
        CHECK(r.converted);
        {
            INFO("as it is");
            CHECK(has(r, "ll.Key2Name(myInfo()))"));
            CHECK_FALSE(has(r, "myInfo() :: any"));
        }
        UNSCOPED_INFO("a UUID written out, still a uuid");
        CHECK(has(r, "ll.Key2Name(uuid(\"a2e76fcd-9360-4f6d-a924-938f923df11d\"))"));
        {
            INFO("the note with the ways");
            CHECK(noted(r, "SluaKeyAsTextBare"));
            CHECK_FALSE(noted(r, "SluaKeyAsText"));
            CHECK(has(r, "`:: any` quiets it, uuid() stops the script on text that is no UUID"));
        }
        // What the checker marks as an error, each by its line.
        const auto marked = [&](const std::string& slua) {
            std::string said;
            for (const ALScriptProblem& p : service.check(slua))
            {
                said += p.severity == ALScriptProblem::Severity::Error ? llformat("[%d] %s\n", p.line + 1, p.message.c_str()) : std::string();
            }
            return said;
        };
        // The line the text for a key is on, as marked says it.
        const auto lineOf = [](const std::string& slua) {
            const size_t at = slua.find("ll.Key2Name(myInfo())");
            CAPTURE(slua);
            REQUIRE(at != std::string::npos);
            return llformat("[%d] ", static_cast<S32>(std::count(slua.begin(), slua.begin() + at, '\n')) + 1);
        };
        // Nonstrict, as the grid has it, takes an untyped function's answer
        // for anything; typed, as Tapple converts, or strict, it is text.
        // The old solver's: the new one's nonstrict marks only what must
        // fail, and its strict cannot yet take LLEvents:on's handlers.
        if (!newSolver)
        {
            o.types                         = true;
            const ALLSLToSLua::Result typed = ALLSLToSLua::convert(lsl, o);
            const std::string         said  = marked(typed.text);
            {
                INFO("typed, the text for a key marked, alone");
                CAPTURE(said, typed.text);
                CHECK(said.rfind(lineOf(typed.text), 0) == 0);
                CHECK(std::count(said.begin(), said.end(), '\n') == 1);
            }
            const std::string strict = "--!strict\n" + r.text;
            const std::string loud   = marked(strict);
            {
                INFO("strict, the same");
                CAPTURE(loud, strict);
                CHECK(loud.rfind(lineOf(strict), 0) == 0);
                CHECK(std::count(loud.begin(), loud.end(), '\n') == 1);
            }
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a key given no text holds text as LSL's did; a uuid key compared as one", "[ALLSLToSLua]")
    {
        const ALLSLToSLua::Result r = require_converted("key gUser = \"\";\n"
                                                        "default {\n"
                                                        "    state_entry() {\n"
                                                        "        key k = \"\";\n"
                                                        "        llOwnerSay((string)(k == \"\"));\n"
                                                        "    }\n"
                                                        "    touch_start(integer n) {\n"
                                                        "        if (gUser) llOwnerSay(\"busy\");\n"
                                                        "        gUser = llDetectedKey(0);\n"
                                                        "        llMessageLinked(LINK_SET, 0, \"\", gUser);\n"
                                                        "        gUser = \"\";\n"
                                                        "        llMessageLinked(LINK_SET, 0, \"\", gUser);\n"
                                                        "        llOwnerSay(\"user: \" + (string)gUser);\n"
                                                        "    }\n"
                                                        "    link_message(integer s, integer num, string m, key id) {\n"
                                                        "        if (id == \"\") llOwnerSay(\"nobody\");\n"
                                                        "        if (id) llOwnerSay(\"someone\");\n"
                                                        "        llOwnerSay(llList2CSV([id, gUser]));\n"
                                                        "        key d;\n"
                                                        "        if (d == \"\") llOwnerSay(\"no key yet\");\n"
                                                        "        if (\"\" != d) llOwnerSay(\"some\");\n"
                                                        "        if (d == \"a2e76fcd-9360-4f6d-a924-938f923df11d\") llOwnerSay(\"that one\");\n"
                                                        "        if (d == \"none\") llOwnerSay(\"text\");\n"
                                                        "        if (NULL_KEY != \"\") llOwnerSay(\"NULL_KEY has text\");\n"
                                                        "    }\n"
                                                        "}\n");
        CAPTURE(r.text);
        {
            INFO("Tapple's, text");
            CHECK(has(r, "local k = \"\""));
            CHECK(has(r, "print(tostring(if k == \"\" then 1 else 0))"));
        }
        {
            INFO("a global given no text, text");
            CHECK(has(r, "local gUser = \"\""));
            CHECK(has(r, "gUser = tostring(detected[1]:getKey())"));
        }
        UNSCOPED_INFO("sent as text, no text and all");
        CHECK(count(r, "ll.MessageLinked(LINK_SET, 0, \"\", gUser)") == 2);
        UNSCOPED_INFO("made text as it is");
        CHECK(has(r, "print(\"user: \" .. gUser)"));
        UNSCOPED_INFO("its truth as LSL's key's");
        CHECK(has(r, "if (touuid(gUser) or NULL_KEY).istruthy then"));
        {
            INFO("the id, text");
            CHECK(has(r, "if id == \"\" then"));
            CHECK(has(r, "if (touuid(id) or NULL_KEY).istruthy then"));
        }
        {
            INFO("keys held as text, in a list as text");
            CHECK(has(r, "{ id, gUser }"));
            CHECK_FALSE(has(r, "{ uuid("));
        }
        {
            INFO("a uuid against no text, a uuid");
            CHECK(has(r, "local d = NULL_KEY"));
            CHECK(has(r, "if d == uuid(\"\") then"));
            CHECK(has(r, "if uuid(\"\") ~= d then"));
        }
        UNSCOPED_INFO("a UUID written out");
        CHECK(has(r, "if d == uuid(\"a2e76fcd-9360-4f6d-a924-938f923df11d\") then"));
        UNSCOPED_INFO("other text, as text");
        CHECK(has(r, "if tostring(d) == \"none\" then"));
        UNSCOPED_INFO("NULL_KEY, as LSL's text");
        CHECK(has(r, "if tostring(NULL_KEY) ~= \"\" then"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: with uuidKeys off the script's own keys are held as text as in LSL", "[ALLSLToSLua]")
    {
        const std::string lsl = "key gOwner;\n"
                                "key pick() { return llGetOwner(); }\n"
                                "integer mine(key who) { return who == gOwner; }\n"
                                "default {\n"
                                "    state_entry() {\n"
                                "        key k = \"\";\n"
                                "        llOwnerSay((string)(k == \"\"));\n"
                                "        gOwner = llGetOwner();\n"
                                "        string s = llGetObjectDesc();\n"
                                "        key cast = (key)s;\n"
                                "        if (gOwner) llDialog(gOwner, \"hi\", [\"OK\"], -5);\n"
                                "        if (gOwner != NULL_KEY && mine(pick())) llOwnerSay((string)cast + (string)((key)\"\"));\n"
                                "        key odd = (key)\"not-a-uuid\";\n"
                                "        list values = [odd, k, NULL_KEY];\n"
                                "        llOwnerSay(llList2CSV(values) + llList2CSV([odd]));\n"
                                "    }\n"
                                "    listen(integer c, string n, key id, string m) {\n"
                                "        if (id == gOwner) llOwnerSay(m);\n"
                                "    }\n"
                                "}\n";
        ALLSLToSLua::Options o;
        o.uuidKeys                  = false;
        o.types                     = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, o);
        CAPTURE(r.text);
        UNSCOPED_INFO("converted: " + (r.problems.empty() ? std::string() : r.problems.front().message));
        CHECK(r.converted);
        {
            INFO("Tapple's");
            CHECK(has(r, "local k: string = \"\"\n"));
            CHECK(has(r, "print(tostring(if k == \"\" then 1 else 0))"));
        }
        {
            INFO("a global, LSL's default");
            CHECK(has(r, "local gOwner: string = \"\"\n"));
            CHECK(has(r, "gOwner = tostring(ll.GetOwner())"));
        }
        UNSCOPED_INFO("what a function gives");
        CHECK(has(r, "local function pick(): string\n    return tostring(ll.GetOwner())"));
        {
            INFO("a parameter");
            CHECK(has(r, "local function mine(who: string): boolean"));
            CHECK(has(r, "who == gOwner"));
        }
        {
            INFO("a cast, the text as it is");
            CHECK(has(r, "local cast: string = s\n"));
            CHECK(has(r, "print(cast .. \"\")"));
            CHECK_FALSE(has(r, "uuid(s)"));
            CHECK_FALSE(has(r, "uuid(\"\")"));
        }
        UNSCOPED_INFO("its truth as LSL's");
        CHECK(has(r, "if (touuid(gOwner) or NULL_KEY).istruthy then"));
        UNSCOPED_INFO("given ll as it is");
        CHECK(has(r, "ll.Dialog(gOwner :: any, \"hi\""));
        UNSCOPED_INFO("against NULL_KEY, as text");
        CHECK(has(r, "gOwner ~= tostring(NULL_KEY)"));
        {
            INFO("a handler's key, made text as it begins");
            CHECK(has(r, "function(c: number, n: string, idKey: uuid, m: string)"));
            CHECK(has(r, "local id: string = tostring(idKey)"));
            CHECK(has(r, "if id == gOwner then"));
        }
        {
            INFO("in a list, text as it is");
            CHECK(has(r, "local values: { any } = { odd, k, tostring(NULL_KEY) }"));
            CHECK_FALSE(has(r, "{ uuid("));
        }
        UNSCOPED_INFO("nothing said of NULL_KEY");
        CHECK_FALSE(noted(r, "SluaKeyEmpty"));
        require_checks_clean(r);

        const ALLSLToSLua::Result close = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        {
            INFO("closeToLSL the same");
            CHECK(close.converted);
            CHECK(has(close, "local gOwner = \"\"\n"));
            CHECK(has(close, "local cast = s\n"));
        }
        require_checks_clean(close);

        const ALLSLToSLua::Result uuids = require_converted(lsl);
        CAPTURE(uuids.text);
        {
            INFO("uuids by default");
            CHECK(has(uuids, "local gOwner = NULL_KEY\n"));
            CHECK(has(uuids, "local cast = uuid(s)"));
        }
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: a key that may hold any text given on to a key held as text holds text", "[ALLSLToSLua]")
    {
        const std::string lsl = "key driver = \"\";\n"
                                "key gOwner = \"\";\n"
                                "onDriverChange(key newDriver) {\n"
                                "    driver = newDriver;\n"
                                "    if (driver == \"\") {\n"
                                "        llOwnerSay(\"no driver\");\n"
                                "    } else {\n"
                                "        llOwnerSay(\"driver\");\n"
                                "    }\n"
                                "}\n"
                                "onStateChange(string value) {\n"
                                "    onDriverChange(value);\n"
                                "}\n"
                                "default {\n"
                                "    state_entry() {\n"
                                "        onStateChange(\"\");\n"
                                "        string m = llGetObjectDesc();\n"
                                "        key a = m;\n"
                                "        key b = a;\n"
                                "        driver = b;\n"
                                "        key id = llGetSubString(m, 0, 35);\n"
                                "        llGiveInventory(id, \"x\");\n"
                                "        key owner = llGetOwner();\n"
                                "        gOwner = owner;\n"
                                "        llOwnerSay((string)owner);\n"
                                "    }\n"
                                "}\n";
        ALLSLToSLua::Options o;
        o.types                     = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, o);
        CAPTURE(r.text);
        REQUIRE(r.converted);
        {
            INFO("Tapple's: the parameter text, given the text as it is");
            CHECK(has(r, "local driver: string = \"\"\n"));
            CHECK(has(r, "local function onDriverChange(newDriver: string)"));
            CHECK(has(r, "    driver = newDriver\n"));
            CHECK(has(r, "if driver == \"\" then"));
            CHECK(has(r, "    onDriverChange(value)\n"));
            CHECK_FALSE(has(r, "uuid(value)"));
        }
        {
            INFO("through another key");
            CHECK(has(r, "local a: string = m\n"));
            CHECK(has(r, "local b: string = a\n"));
            CHECK(has(r, "driver = b\n"));
        }
        UNSCOPED_INFO("text not given on to text, a uuid");
        CHECK(has(r, "local id: uuid = uuid(ll.GetSubString(m, 1, 36))"));
        UNSCOPED_INFO("a key that holds a UUID given on to text, made text there");
        CHECK(has(r, "local owner: uuid = ll.GetOwner()"));
        CHECK(has(r, "gOwner = tostring(owner)"));
        require_checks_clean(r);
    }

    TEST_CASE_METHOD(allsltoslua_data, "ALLSLToSLua: with uuidKeys off llList2Key's item is read as text as List2String reads it", "[ALLSLToSLua]")
    {
        const std::string lsl = "list gMixed;\n"
                                "default { state_entry() {\n"
                                "    list l = [\"hello world\"];\n"
                                "    string msg = llList2Key(l, 0);\n"
                                "    llOwnerSay(msg);\n"
                                "    key k = llList2Key(l, 0);\n"
                                "    if (k) llOwnerSay((string)k);\n"
                                "    gMixed = llParseString2List(llGetObjectDesc(), [\",\"], []) + [1];\n"
                                "    llOwnerSay(llList2Key(gMixed, 0));\n"
                                "} }\n";
        ALLSLToSLua::Options o;
        o.uuidKeys                  = false;
        o.types                     = true;
        const ALLSLToSLua::Result r = ALLSLToSLua::convert(lsl, o);
        CAPTURE(r.text);
        REQUIRE(r.converted);
        {
            INFO("Tapple's: the item's text, as LSL gave it");
            CHECK(has(r, "local msg: string = l[1] or \"\"\n"));
            CHECK(has(r, "local k: string = l[1] or \"\"\n"));
            CHECK(has(r, "if (touuid(k) or NULL_KEY).istruthy then print(k) end"));
            CHECK_FALSE(has(r, "List2Key("));
        }
        {
            INFO("items that may be neither, read as List2String reads them, and said");
            CHECK(has(r, "print(llcompat.List2String(gMixed, 0))"));
            CHECK(noted(r, "SluaList2KeyText"));
        }
        require_checks_clean(r);

        const ALLSLToSLua::Result close = ALLSLToSLua::convert(lsl, ALLSLToSLua::Options::closeToLSL());
        CAPTURE(close.text);
        REQUIRE(close.converted);
        {
            INFO("closeToLSL: List2String's call, as Tapple wrote it");
            CHECK(has(close, "local msg = llcompat.List2String(l, 0)\n"));
            CHECK(has(close, "local k = llcompat.List2String(l, 0)\n"));
            CHECK_FALSE(has(close, "List2Key("));
        }
        require_checks_clean(close);

        const ALLSLToSLua::Result uuids = require_converted(lsl);
        CAPTURE(uuids.text);
        {
            INFO("uuids by default: List2Key's uuid");
            CHECK(has(uuids, "local msg = tostring(llcompat.List2Key(l, 0))"));
            CHECK(has(uuids, "local k = llcompat.List2Key(l, 0)"));
            CHECK_FALSE(noted(uuids, "SluaList2KeyText"));
        }
    }
}
