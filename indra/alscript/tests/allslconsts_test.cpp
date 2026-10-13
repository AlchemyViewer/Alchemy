/**
 * @file tests/allslconsts_test.cpp
 * @brief The const keyword: declarations checked, and values folded.
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

#include "../lsl/allslconsts.h"
#include "../lsl/allslservice.h"
#include "../preprocessor/alpreprocessor.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct allslconsts_data
    {
        ALLSLService lsl;
        std::string  error;
        bool         lslLoaded = false;

        allslconsts_data() { lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error); }

        // The problems of a preprocessor run, one to a line: key, line and
        // column, the words.
        static std::string said(const std::string& source)
        {
            const ALPreprocessor::Result r = ALPreprocessor::run(source, ALPreprocessor::Options());
            std::string                  out;
            for (const ALScriptProblem& p : r.problems)
            {
                out += p.key + " " + std::to_string(p.line) + ":" + std::to_string(p.column);
                for (const std::string& a : p.args)
                {
                    out += " " + a;
                }
                out += "\n";
            }
            return out;
        }
    };

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: a const variable is given its value where declared and set nowhere else", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        CHECK(said("const integer A = 1;\n"
                                            "default { state_entry() { const float f = A * 2; llOwnerSay((string)(f + A)); } }\n") ==
                      std::string());
        CHECK(said("const integer A = 1;\n"
                           "f(const integer n) { n = 2; }\n"
                           "default\n{\n    state_entry()\n    {\n        const vector v = <1, 2, 3>;\n        A += 1;\n        v.x = 2;\n        ++A;\n"
                           "        llOwnerSay((string)v);\n    }\n}\n") ==
                      std::string("ConstAssigned 1:21 n\n"
                                  "ConstAssigned 7:8 A\n"
                                  "ConstAssigned 8:8 v\n"
                                  "ConstAssigned 9:8 A\n"));
        CHECK(said("const integer A;\ndefault { state_entry() { const string s; llOwnerSay(s + (string)A); } }\n") ==
                      std::string("ConstWithoutValue 0:14 A\n"
                                  "ConstWithoutValue 1:39 s\n"));
        UNSCOPED_INFO("a script that does not compile is the compiler's");
        CHECK(said("const integer A = ;\n") == std::string());
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: a const function works out what it gives back from its arguments alone", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string globals = "integer counter;\ninteger fixed = 3;\n";
        const std::string handler = "default { state_entry() { counter = 1; } }\n";
        UNSCOPED_INFO("pure, calling what is pure, reading what never changes");
        CHECK(said(globals +
                           "integer twice(integer x) { return x * 2; }\n"
                           "const integer f(integer x) { integer y = twice(x) + fixed; return llAbs(y); }\n" + handler) ==
                      std::string());
        CHECK(said(globals +
                           "const integer f(integer x)\n{\n    counter = x;\n    llOwnerSay(\"hi\");\n    if (x) state other;\n    return x + counter;\n}\n"
                           "default { state_entry() { counter = 1; } }\nstate other { state_entry() { } }\n") ==
                      std::string("ConstFunctionSets 4:4 f counter\n"
                                  "ConstFunctionCalls 5:4 f llOwnerSay\n"
                                  "ConstFunctionChangesState 6:11 f\n"
                                  "ConstFunctionReads 7:15 f counter\n"));
        UNSCOPED_INFO("a function it calls that sets a global");
        CHECK(said(globals + "bump() { counter += 1; }\nconst integer f(integer x) { bump(); return x; }\n" + handler) ==
                      std::string("ConstFunctionCalls 3:29 f bump\n"));
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: optimizer off: a const global is written as the literal of its value", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string source = "const integer FLAGS = PERMISSION_TAKE_CONTROLS | PERMISSION_TRIGGER_ANIMATION;\n"
                                   "const float STEP = TWO_PI / 12;\n"
                                   "const integer TWICE = FLAGS * 2;\n"
                                   "const vector UP = <0, 0, 1> * 2;\n"
                                   "const string NAME = \"a \\\"b\\\"\" + \"\\n\" + (string)llAbs(-3);\n"
                                   "const list L = [FLAGS * 1,\n    TWICE];\n"
                                   "const integer SAME = FLAGS;\n"
                                   "const integer PLAIN = -5;\n"
                                   "integer changing = 1;\n"
                                   "const integer C = changing + 1;\n"
                                   "default { state_entry() { changing = 2; llOwnerSay((string)[FLAGS, STEP, TWICE, UP, NAME, SAME, PLAIN, C] + (string)L); } }\n";
        const ALPreprocessor::Result r = ALPreprocessor::run(source, ALPreprocessor::Options());
        CHECK(said(source) == std::string());
        CHECK(r.text ==
                      std::string("integer FLAGS = 20;\n"
                                  "float STEP = 0.5235988;\n"
                                  "integer TWICE = 40;\n"
                                  "vector UP = <0.0, 0.0, 2.0>;\n"
                                  "string NAME = \"a \\\"b\\\"\\n3\";\n"
                                  "list L = [20, 40];\n"
                                  "integer SAME = FLAGS;\n"
                                  "integer PLAIN = -5;\n"
                                  "integer changing = 1;\n"
                                  "integer C = 2;\n"
                                  "default { state_entry() { changing = 2; llOwnerSay((string)[FLAGS, STEP, TWICE, UP, NAME, SAME, PLAIN, C] + (string)L); } }\n"));
        // What was written maps to what it was written for.
        const ALSourceMap::Loc twenty = r.map.toSource(0, 16);
        {
            INFO("the value maps to the expression");
            REQUIRE(twenty.found());
            CHECK(twenty.line == 0);
            CHECK(twenty.column == 22);
        }
        // The names are where they are now: the list's two lines are one.
        REQUIRE(r.consts.size() == 9);
        {
            INFO("a name after the folded list, a line up");
            CHECK(r.consts[6].name == "SAME");
            CHECK(r.consts[6].line == 6);
            CHECK(r.consts[6].column == 8);
        }
        const ALSourceMap::Loc same = r.map.toSource(6, 8);
        {
            INFO("and mapped to where it was");
            REQUIRE(same.found());
            CHECK(same.line == 7);
        }
        // The target's arithmetic: Luau's doubles.
        ALPreprocessor::Options luau = ALPreprocessor::Options();
        luau.optimizer.target        = ALLSLOptimizer::Target::Luau;
        const ALPreprocessor::Result wide = ALPreprocessor::run("const float STEP = TWO_PI / 12;\ndefault { state_entry() { llOwnerSay((string)STEP); } }\n", luau);
        {
            INFO("in doubles on Luau: " + wide.text);
            CHECK(wide.text.find("float STEP = 0.5235987") == 0);
            CHECK(wide.text.find(";") > 26);
        }
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: a const global known only at run time or not writable is an error", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        // A global the script changes is no bar: globals are given their
        // values as the script starts, when each has its first.
        UNSCOPED_INFO("each, at its value");
        CHECK(said("const integer T = llGetUnixTime();\n"
                           "const float BIG = 1e30 * 1e30;\n"
                           "const list KEYS = [(key)NULL_KEY] + [];\n"
                           "default { state_entry() { llOwnerSay((string)[T, BIG] + (string)KEYS); } }\n") ==
                      std::string("ConstNotKnown 0:18 T\n"
                                  "ConstNotKnown 1:18 BIG\n"
                                  "ConstNotKnown 2:18 KEYS\n"));
        // A plain global is left to the compiler: only a const is worked out.
        const std::string plain = "integer A = 2 * 3;\ndefault { state_entry() { llOwnerSay((string)A); } }\n";
        CHECK(ALPreprocessor::run(plain, ALPreprocessor::Options()).text == plain);
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: optimizer on: a const's value goes where it is read and its global goes", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        ALPreprocessor::Options o = ALPreprocessor::Options();
        o.optimize                = true;
        const ALPreprocessor::Result r =
            ALPreprocessor::run("const integer FLAGS = PERMISSION_TAKE_CONTROLS | PERMISSION_TRIGGER_ANIMATION;\n"
                                "default { state_entry() { llRequestPermissions(llGetOwner(), FLAGS); } }\n",
                                o);
        UNSCOPED_INFO("optimized: " + r.text);
        CHECK(r.optimized);
        UNSCOPED_INFO("the value in place: " + r.text);
        REQUIRE(r.text.find("llRequestPermissions(llGetOwner(), 20);") != std::string::npos);
        UNSCOPED_INFO("and no global: " + r.text);
        REQUIRE(r.text.find("FLAGS") == std::string::npos);
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: optimizer on: a const function folds on constants and other calls stay", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string source = "const integer sq(integer x) { return x * x; }\n"
                                   "const float deg(float d) { return d * DEG_TO_RAD; }\n"
                                   "const integer clampi(integer v, integer lo, integer hi)\n{\n    if (v < lo)\n        return lo;\n"
                                   "    if (v > hi)\n        return hi;\n    return v;\n}\n"
                                   "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        llOwnerSay((string)[sq(3), deg(90), clampi(12, 0, 10), sq(n)]);\n    }\n}\n";
        ALPreprocessor::Options o = ALPreprocessor::Options();
        o.optimize                = true;
        const ALPreprocessor::Result r = ALPreprocessor::run(source, o);
        UNSCOPED_INFO("optimized: " + r.text);
        CHECK(r.optimized);
        CHECK(said(source) == std::string());
        // The list stays a literal: as a sum it would need a helper the
        // script has no other use for, which costs Mono more than it saves.
        UNSCOPED_INFO("each worked out: " + r.text);
        REQUIRE(r.text.find("[9, 1.5707964, 10, sq(n)]") != std::string::npos);
        {
            INFO("the functions no call is left to go: " + r.text);
            REQUIRE(r.text.find("deg(") == std::string::npos);
            REQUIRE(r.text.find("clampi(") == std::string::npos);
        }
        UNSCOPED_INFO("and the one a call is left to stays: " + r.text);
        REQUIRE(r.text.find("integer sq(integer x)") != std::string::npos);

        // With the optimizer off, a call is a call.
        const ALPreprocessor::Result off = ALPreprocessor::run(source, ALPreprocessor::Options());
        UNSCOPED_INFO("calls stay: " + off.text);
        REQUIRE(off.text.find("sq(3), deg(90), clampi(12, 0, 10), sq(n)") != std::string::npos);
    }

    TEST_CASE_METHOD(allslconsts_data, "ALLSLConsts: the analyzer and the preprocessor agree where each const is declared", "[ALLSLConsts]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const ALPreprocessor::Result r = ALPreprocessor::run("const integer LIMIT = 3 * 4;\n"
                                                             "const integer twice(const integer n) { return n * 2; }\n"
                                                             "default { state_entry() { llOwnerSay((string)twice(LIMIT)); } }\n",
                                                             ALPreprocessor::Options());
        REQUIRE(r.consts.size() == size_t(3));
        const auto declaredAt = [&](const std::string& word, size_t from) {
            const size_t at     = r.text.find(word, from);
            REQUIRE(at != std::string::npos);
            const S32    line   = static_cast<S32>(std::count(r.text.begin(), r.text.begin() + at, '\n'));
            const size_t start  = r.text.rfind('\n', at);
            const S32    column = static_cast<S32>(start == std::string::npos ? at : at - start - 1);
            return lsl.hover(r.text, line, column);
        };
        const size_t handler = r.text.find("default");
        for (const auto& [word, from] : std::vector<std::pair<std::string, size_t>>{ { "LIMIT", handler }, { "twice", handler }, { "n * 2", 0 } })
        {
            CAPTURE(word);
            const ALScriptHover hover = declaredAt(word, from);
            REQUIRE(hover.found);
            REQUIRE(hover.hasDefinition);
            const bool matches = std::any_of(r.consts.begin(), r.consts.end(), [&](const ALPreprocessor::Result::Const& c) {
                return c.line == hover.definitionLine && c.column == hover.definitionColumn;
            });
            CAPTURE(hover.definitionLine, hover.definitionColumn);
            REQUIRE(matches);
        }
    }
}
