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

#include "../allslconsts.h"
#include "../allslservice.h"
#include "../alpreprocessor.h"

#include "../test/lltut.h"

namespace tut
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
    typedef test_group<allslconsts_data> allslconsts_group;
    typedef allslconsts_group::object    allslconsts_object;
    allslconsts_group                    allslconsts_instance("ALLSLConsts");

    template<> template<>
    void allslconsts_object::test<1>()
    {
        set_test_name("a const variable is given its value where it is declared, and set nowhere else");
        ensure("builtins: " + error, lslLoaded);
        ensure_equals("nothing wrong", said("const integer A = 1;\n"
                                            "default { state_entry() { const float f = A * 2; llOwnerSay((string)(f + A)); } }\n"),
                      std::string());
        ensure_equals("each write, where it is",
                      said("const integer A = 1;\n"
                           "f(const integer n) { n = 2; }\n"
                           "default\n{\n    state_entry()\n    {\n        const vector v = <1, 2, 3>;\n        A += 1;\n        v.x = 2;\n        ++A;\n"
                           "        llOwnerSay((string)v);\n    }\n}\n"),
                      std::string("ConstAssigned 1:21 n\n"
                                  "ConstAssigned 7:8 A\n"
                                  "ConstAssigned 8:8 v\n"
                                  "ConstAssigned 9:8 A\n"));
        ensure_equals("no value", said("const integer A;\ndefault { state_entry() { const string s; llOwnerSay(s + (string)A); } }\n"),
                      std::string("ConstWithoutValue 0:14 A\n"
                                  "ConstWithoutValue 1:39 s\n"));
        ensure_equals("a script that does not compile is the compiler's", said("const integer A = ;\n"), std::string());
    }

    template<> template<>
    void allslconsts_object::test<2>()
    {
        set_test_name("a const function works out what it gives back from its arguments alone");
        ensure("builtins: " + error, lslLoaded);
        const std::string globals = "integer counter;\ninteger fixed = 3;\n";
        const std::string handler = "default { state_entry() { counter = 1; } }\n";
        ensure_equals("pure, calling what is pure, reading what never changes",
                      said(globals +
                           "integer twice(integer x) { return x * 2; }\n"
                           "const integer f(integer x) { integer y = twice(x) + fixed; return llAbs(y); }\n" + handler),
                      std::string());
        ensure_equals("each thing it may not do, where it does it",
                      said(globals +
                           "const integer f(integer x)\n{\n    counter = x;\n    llOwnerSay(\"hi\");\n    if (x) state other;\n    return x + counter;\n}\n"
                           "default { state_entry() { counter = 1; } }\nstate other { state_entry() { } }\n"),
                      std::string("ConstFunctionSets 4:4 f counter\n"
                                  "ConstFunctionCalls 5:4 f llOwnerSay\n"
                                  "ConstFunctionChangesState 6:11 f\n"
                                  "ConstFunctionReads 7:15 f counter\n"));
        ensure_equals("a function it calls that sets a global",
                      said(globals + "bump() { counter += 1; }\nconst integer f(integer x) { bump(); return x; }\n" + handler),
                      std::string("ConstFunctionCalls 3:29 f bump\n"));
    }

    template<> template<>
    void allslconsts_object::test<3>()
    {
        set_test_name("a const global's value is worked out, and written as the literal LSL takes there, with the optimizer off");
        ensure("builtins: " + error, lslLoaded);
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
        ensure_equals("nothing wrong", said(source), std::string());
        ensure_equals("each written",
                      r.text,
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
        ensure("the value maps to the expression", twenty.found() && twenty.line == 0 && twenty.column == 22);
        // The names are where they are now: the list's two lines are one.
        ensure("nine", r.consts.size() == 9);
        ensure("a name after the folded list, a line up", r.consts[6].name == "SAME" && r.consts[6].line == 6 && r.consts[6].column == 8);
        const ALSourceMap::Loc same = r.map.toSource(6, 8);
        ensure("and mapped to where it was", same.found() && same.line == 7);
        // The target's arithmetic: Luau's doubles.
        ALPreprocessor::Options luau = ALPreprocessor::Options();
        luau.optimizer.target        = ALLSLOptimizer::Target::Luau;
        const ALPreprocessor::Result wide = ALPreprocessor::run("const float STEP = TWO_PI / 12;\ndefault { state_entry() { llOwnerSay((string)STEP); } }\n", luau);
        ensure("in doubles on Luau: " + wide.text, wide.text.find("float STEP = 0.5235987") == 0 && wide.text.find(";") > 26);
    }

    template<> template<>
    void allslconsts_object::test<4>()
    {
        set_test_name("a const global whose value cannot be worked out before the script runs is an error, and so is one that cannot be written");
        ensure("builtins: " + error, lslLoaded);
        // A global the script changes is no bar: globals are given their
        // values as the script starts, when each has its first.
        ensure_equals("each, at its value",
                      said("const integer T = llGetUnixTime();\n"
                           "const float BIG = 1e30 * 1e30;\n"
                           "const list KEYS = [(key)NULL_KEY] + [];\n"
                           "default { state_entry() { llOwnerSay((string)[T, BIG] + (string)KEYS); } }\n"),
                      std::string("ConstNotKnown 0:18 T\n"
                                  "ConstNotKnown 1:18 BIG\n"
                                  "ConstNotKnown 2:18 KEYS\n"));
        // A plain global is left to the compiler: only a const is worked out.
        const std::string plain = "integer A = 2 * 3;\ndefault { state_entry() { llOwnerSay((string)A); } }\n";
        ensure_equals("not a const", ALPreprocessor::run(plain, ALPreprocessor::Options()).text, plain);
    }

    template<> template<>
    void allslconsts_object::test<5>()
    {
        set_test_name("with the optimizer on, a const's value goes where it is read, and the global with it");
        ensure("builtins: " + error, lslLoaded);
        ALPreprocessor::Options o = ALPreprocessor::Options();
        o.optimize                = true;
        const ALPreprocessor::Result r =
            ALPreprocessor::run("const integer FLAGS = PERMISSION_TAKE_CONTROLS | PERMISSION_TRIGGER_ANIMATION;\n"
                                "default { state_entry() { llRequestPermissions(llGetOwner(), FLAGS); } }\n",
                                o);
        ensure("optimized: " + r.text, r.optimized);
        ensure("the value in place: " + r.text, r.text.find("llRequestPermissions(llGetOwner(), 20);") != std::string::npos);
        ensure("and no global: " + r.text, r.text.find("FLAGS") == std::string::npos);
    }
}
