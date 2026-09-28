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
}
