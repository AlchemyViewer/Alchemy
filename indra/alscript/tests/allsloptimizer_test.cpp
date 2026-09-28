/**
 * @file allsloptimizer_test.cpp
 * @brief The optimizer, pass by pass, and its golden files.
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

#include "../alscriptengine.h"
#include "../allslinliner.h"
#include "../allsloptimizer.h"
#include "../allslservice.h"
#include "../allsltraits.h"

#include "../test/lltut.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <thread>
#include <vector>

namespace tut
{
    struct allsloptimizer_data
    {
        allsloptimizer_data()
        {
            static bool loaded = false;
            if (!loaded)
            {
                ALLSLService service;
                std::string  error;
                loaded = service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
                if (!loaded)
                {
                    fail("the builtins did not load: " + error);
                }
            }
        }

        static ALLSLOptimizer::Options options()
        {
            ALLSLOptimizer::Options o;
            return o;
        }

        static std::string notes(const ALLSLOptimizer::Result& r)
        {
            std::string out;
            for (const ALScriptProblem& p : r.problems)
            {
                out += (p.severity == ALScriptProblem::Severity::Error ? "E " : p.severity == ALScriptProblem::Severity::Warning ? "W " : "N ") +
                       std::to_string(p.line) + ": " + p.message + "\n";
            }
            return out;
        }

        static bool has(const ALLSLOptimizer::Result& r, const std::string& text) { return notes(r).find(text) != std::string::npos; }

        // The numbers a declaration was folded to, `<x, y, z>` or one
        // alone; none where it was left a call.
        static std::vector<F32> numbers(const std::string& text, const std::string& declaration)
        {
            std::vector<F32> out;
            const size_t     at = text.find(declaration + " = ");
            if (at == std::string::npos)
            {
                return out;
            }
            const char* p = text.c_str() + at + declaration.size() + 3;
            if (*p == '<')
            {
                ++p;
            }
            while (true)
            {
                char*     end = nullptr;
                const F32 v   = std::strtof(p, &end);
                if (end == p)
                {
                    break;
                }
                out.push_back(v);
                p = end;
                if (*p != ',')
                {
                    break;
                }
                ++p;
                while (*p == ' ')
                {
                    ++p;
                }
            }
            return out;
        }

        // Within a few units in the last place of each: the viewer's float
        // arithmetic is as near as the simulator's own comes, and how near
        // is the compiler's to say.
        static bool about(const std::vector<F32>& got, std::initializer_list<F32> want)
        {
            if (got.size() != want.size())
            {
                return false;
            }
            size_t i = 0;
            for (const F32 w : want)
            {
                if (std::fabs(got[i++] - w) > 1e-6f)
                {
                    return false;
                }
            }
            return true;
        }

        // A script around a state_entry body, and the body printed back.
        static std::string wrap(const std::string& globals, const std::string& body)
        {
            return globals + "default\n{\n    state_entry()\n    {\n" + body + "    }\n}\n";
        }
    };

    typedef test_group<allsloptimizer_data> allsloptimizer_group;
    typedef allsloptimizer_group::object    allsloptimizer_object;
    allsloptimizer_group                    allsloptimizer_instance("allsloptimizer");

    template<> template<>
    void allsloptimizer_object::test<1>()
    {
        set_test_name("constants fold, never-assigned variables inline, and what is left unused goes");
        const std::string source = "integer unused = 5;\ninteger used = 2;\n" +
                                   wrap("", "        integer x = used * 3 + 1;\n        llSay(0, (string)x);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure_equals("text", r.text, wrap("", "        llSay(0, \"7\");\n"));
        ensure("noted the global", has(r, "removed the unused global unused"));
        ensure("noted the fold: " + notes(r), has(r, "folded used * 3 + 1 to 7"));
        ensure("noted the local", has(r, "removed the unused local x"));
        ensure("smaller", r.sizeAfter < r.sizeBefore);
    }

    template<> template<>
    void allsloptimizer_object::test<2>()
    {
        set_test_name("floats fold in single precision and print so that they read back exactly");
        // Each variable is assigned again later, so that it stays a
        // variable and the fold shows in its declaration.
        const std::string source = wrap("", "        float a = 0.1 + 0.2;\n        float b = PI * 2;\n        float c = 1.0 / 3;\n        llSay(0, (string)a + (string)b + (string)c);\n        a = b = c = 0;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        // 0.1f + 0.2f is 0.3 exactly as a single, PI * 2 is 6.2831855f,
        // and each reads back as the same single.
        ensure("a: " + r.text, r.text.find("float a = 0.3;") != std::string::npos);
        ensure("b", r.text.find("float b = 6.2831855;") != std::string::npos);
        ensure("c", r.text.find("float c = 0.33333334;") != std::string::npos);

        // A float that is a whole number prints as an integer where LSL
        // would convert one, and stays a float where it would not.
        r = ALLSLOptimizer::run(wrap("", "        float f = 2.0;\n        list l = [2.0];\n        string s = (string)2.0;\n        vector v = <1.0, 2.0, 3.5>;\n        llSay(0, (string)f + llList2CSV(l) + s + (string)v);\n        f = 0; l = []; s = \"\"; v = ZERO_VECTOR;\n"), options());
        ensure("whole float as integer: " + r.text, r.text.find("float f = 2;") != std::string::npos);
        ensure("list element stays a float: " + r.text, r.text.find("list l = (list)2.0;") != std::string::npos);
        ensure("cast folded with six places", r.text.find("string s = \"2.000000\";") != std::string::npos);
        ensure("vector components", r.text.find("vector v = <1, 2, 3.5>;") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<3>()
    {
        set_test_name("the arithmetic is the VM's: wrapping integers, division by zero left alone");
        const std::string source = wrap("", "        llSay(0, (string)(2147483647 + 1));\n        llSay(0, (string)(1 / 0));\n        llSay(0, (string)(-2147483648 / -1));\n        llSay(0, (string)(7 / 2 * 2));\n        llSay(0, (string)(1 << 40));\n        llSay(0, (string)(-7 % 3));\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure("wrapped: " + r.text, r.text.find("llSay(0, \"-2147483648\");") != std::string::npos);
        ensure("zero stays", r.text.find("(string)(1 / 0)") != std::string::npos);
        ensure("the overflow stays", r.text.find("(string)(-2147483648 / -1)") != std::string::npos);
        ensure("integer division", r.text.find("llSay(0, \"6\");") != std::string::npos);
        ensure("a wide shift stays", r.text.find("(string)(1 << 40)") != std::string::npos);
        ensure("modulo keeps the sign", r.text.find("llSay(0, \"-1\");") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<4>()
    {
        set_test_name("pure library calls with constant arguments are evaluated, others left");
        const std::string source = wrap("", "        integer a = llAbs(-3);\n        float b = llSqrt(16);\n        string c = llToUpper(\"abc\");\n        string d = llGetSubString(\"abcdef\", 4, 1);\n        string e = llDeleteSubString(\"abcdef\", 1, 3);\n        integer f = llStringLength(\"hello\");\n        string g = llEscapeURL(\"a b\");\n        string h = llBase64ToString(llStringToBase64(\"hi\"));\n        integer i = llList2Integer([1, 2, 3], -1);\n        string j = llList2String([1, 2.5, <1,2,3>], 1);\n        list k = llList2List([1, 2, 3, 4], 1, 2);\n        integer l = llFloor(2.7) + llCeil(2.2);\n        string m = llGetSubString(\"h\xc3\xa9llo\", 0, 1);\n        integer n = llGetUnixTime();\n        string o = llList2CSV([1, \"a\", 2.0]);\n        llSay(0, (string)a + (string)b + c + d + e + (string)f + g + h + (string)i + j + llList2CSV(k) + (string)l + m + (string)n + o);\n        a = f = i = l = n = 0; b = 0; c = d = e = g = h = j = m = o = \"\"; k = [];\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure("abs: " + r.text, r.text.find("integer a = 3;") != std::string::npos);
        ensure("sqrt", r.text.find("float b = 4;") != std::string::npos);
        ensure("upper", r.text.find("string c = \"ABC\";") != std::string::npos);
        ensure("wrapped range", r.text.find("string d = \"abef\";") != std::string::npos);
        ensure("delete", r.text.find("string e = \"aef\";") != std::string::npos);
        ensure("length", r.text.find("integer f = 5;") != std::string::npos);
        ensure("escape", r.text.find("string g = \"a%20b\";") != std::string::npos);
        ensure("base64 there and back", r.text.find("string h = \"hi\";") != std::string::npos);
        ensure("list get from the end", r.text.find("integer i = 3;") != std::string::npos);
        ensure("list to string", r.text.find("string j = \"2.500000\";") != std::string::npos);
        ensure("list slice: " + r.text, r.text.find("list k = [2, 3];") != std::string::npos);
        ensure("floor and ceil", r.text.find("integer l = 5;") != std::string::npos);
        ensure("not ascii, not folded", r.text.find("llGetSubString(\"h\xc3\xa9llo\", 0, 1)") != std::string::npos);
        ensure("impure, not folded", r.text.find("llGetUnixTime()") != std::string::npos);
        ensure("csv", r.text.find("string o = \"1, a, 2.000000\";") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<5>()
    {
        set_test_name("identities, signs, casts and conditions are simplified");
        const std::string source = wrap("integer g;\ninteger h;\n",
                                        "        integer a = g + 0;\n        integer b = g * 1;\n        integer c = g - -1;\n        integer d = g + -2;\n        integer e = (integer)g;\n        integer f = g * 0;\n        integer i = (g == 0);\n        if (g != 0) h = 1;\n        if (!!g) h = 2;\n        if (!(g == h)) h = 3;\n        if (!g) h = 4; else h = 5;\n        h = a + b + c + d + e + f + i;\n        a = b = c = d = e = f = i = 0;\n        g = h;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure("plus zero: " + r.text, r.text.find("integer a = g;") != std::string::npos);
        ensure("times one", r.text.find("integer b = g;") != std::string::npos);
        ensure("minus minus", r.text.find("integer c = g + 1;") != std::string::npos);
        ensure("plus minus", r.text.find("integer d = g - 2;") != std::string::npos);
        ensure("cast", r.text.find("integer e = g;") != std::string::npos);
        ensure("times zero", r.text.find("integer f = 0;") != std::string::npos);
        ensure("equals zero", r.text.find("integer i = (!g);") != std::string::npos);
        ensure("not equal zero as condition", r.text.find("if (g)\n            h = 1;") != std::string::npos);
        ensure("double not", r.text.find("if (g)\n            h = 2;") != std::string::npos);
        ensure("not equals", r.text.find("if (g != h)\n            h = 3;") != std::string::npos);
        ensure("swapped", r.text.find("if (g)\n            h = 5;\n        else\n            h = 4;") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<6>()
    {
        set_test_name("dead code goes: after a return, under a false condition, unused labels, functions and states");
        const std::string source = "f()\n{\n    return;\n    llSay(0, \"never\");\n}\ng()\n{\n}\n"
                                   "default\n{\n    state_entry()\n    {\n        if (0) llSay(0, \"no\");\n        if (1) llSay(0, \"yes\"); else llSay(0, \"no\");\n        while (0) llSay(0, \"no\");\n        @unused;\n        f();\n        ;\n        {}\n        3;\n    }\n}\nstate other\n{\n    state_entry()\n    {\n    }\n}\n";
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure_equals("text", r.text, "f()\n{\n    return;\n}\n\ndefault\n{\n    state_entry()\n    {\n        llSay(0, \"yes\");\n        f();\n    }\n}\n");
        ensure("noted the return", has(r, "can never run"));
        ensure("noted g", has(r, "removed the unused function g"));
        ensure("noted the state", has(r, "removed the state other"));
        ensure("noted the label", has(r, "nothing jumps to"));
    }

    template<> template<>
    void allsloptimizer_object::test<7>()
    {
        set_test_name("llGetListLength becomes a comparison, and a list literal a sum, with parentheses where they matter, each where smaller");
        const std::string source = wrap("list l;\n", "        integer n = llGetListLength(l) + 1;\n        if (llGetListLength(l)) l = [1, \"a\"];\n        l = [];\n        n = 0;\n");
        // On LSO both are smaller.
        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        ALLSLOptimizer::Result r    = ALLSLOptimizer::run(source, lso);
        ensure("optimized: " + notes(r), r.optimized);
        ensure("in a sum: " + r.text, r.text.find("integer n = (l != []) + 1;") != std::string::npos);
        ensure("as a condition", r.text.find("if (l != [])") != std::string::npos);
        ensure("list add: " + r.text, r.text.find("l = (list)1 + \"a\";") != std::string::npos);
        ensure("an empty list stays", r.text.find("l = [];") != std::string::npos);

        // On Mono the sum is, and the comparison is not.
        r = ALLSLOptimizer::run(source, options());
        ensure("no comparison on Mono: " + r.text, r.text.find("llGetListLength(l) + 1") != std::string::npos && r.text.find("l != []") == std::string::npos);
        ensure("list add on Mono: " + r.text, r.text.find("l = (list)1 + \"a\";") != std::string::npos);

        // On Luau neither.
        ALLSLOptimizer::Options luau = options();
        luau.target                  = ALLSLOptimizer::Target::Luau;
        r                            = ALLSLOptimizer::run(source, luau);
        ensure("neither on Luau: " + r.text, r.text.find("l = [1, \"a\"];") != std::string::npos && r.text.find("l != []") == std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<8>()
    {
        set_test_name("names shrink to the shortest that are free, most used first, and the map says which");
        const std::string source = "integer counter;\nbump(integer by)\n{\n    counter += by;\n}\ndefault\n{\n    touch_start(integer total)\n    {\n        bump(total);\n        bump(1);\n        llSay(0, (string)counter);\n    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.shrinknames             = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        ensure("optimized", r.optimized);
        ensure("renamed counter", r.renamed.count("counter") == 1);
        ensure("renamed bump", r.renamed.count("bump") == 1);
        ensure("renamed total", r.renamed.count("total") == 1);
        ensure("the event stays", r.text.find("touch_start(integer ") != std::string::npos);
        ensure("default stays", r.text.find("default\n{") != std::string::npos);
        ensure("single letters", r.renamed["bump"].size() == 1 && r.renamed["counter"].size() == 1);
        ensure("nothing of the old names", r.text.find("counter") == std::string::npos && r.text.find("bump") == std::string::npos);
        ensure("noted", has(r, "renamed the function bump to"));
    }

    template<> template<>
    void allsloptimizer_object::test<9>()
    {
        set_test_name("a script with errors is left alone, and the map leads back");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run("default { state_entry() { integer x = ; } }", options());
        ensure("not optimized", !r.optimized);
        ensure("an error", r.problems.size() == 1 && r.problems[0].severity == ALScriptProblem::Severity::Error);
        ensure("the source as it was", r.text == "default { state_entry() { integer x = ; } }");

        const std::string source = "integer g = 3;\n\n\ndefault\n{\n    state_entry()\n    {\n        llSay(0, (string)g);\n    }\n}\n";
        r                        = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure_equals("text", r.text, "default\n{\n    state_entry()\n    {\n        llSay(0, \"3\");\n    }\n}\n");
        // The llSay is on output line 4 and came from source line 7.
        const ALSourceMap::Loc loc = r.map.toSource(4, 8);
        ensure("found", loc.found());
        ensure_equals("line", loc.line, 7);
        ensure_equals("column", loc.column, 8);
    }

    template<> template<>
    void allsloptimizer_object::test<10>()
    {
        set_test_name("every golden file comes out as its expected text");
        namespace fs = std::filesystem;
        const fs::path dir     = fsyspath(AL_ALSCRIPT_TEST_DIR) / "optimizer";
        S32            checked = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir))
        {
            const std::string name = entry.path().filename().string();
            if (name.compare(0, 5, "test_") != 0 || name.find("_expected") != std::string::npos)
            {
                continue;
            }
            const std::string stem     = entry.path().stem().string();
            const fs::path    expected = dir / (stem + "_expected.lsl");
            const auto        read     = [](const fs::path& path) {
                llifstream        in(path, std::ios::binary);
                std::stringstream buffer;
                buffer << in.rdbuf();
                return buffer.str();
            };
            const std::string      source = read(entry.path());
            ALLSLOptimizer::Options o     = options();
            const std::string       first = source.substr(0, source.find('\n'));
            o.shrinknames                 = first.find("shrinknames") != std::string::npos;
            o.addstrings                  = first.find("addstrings") != std::string::npos;
            o.target = first.find("lso") != std::string::npos ? ALLSLOptimizer::Target::LSO : first.find("luau") != std::string::npos ? ALLSLOptimizer::Target::Luau : ALLSLOptimizer::Target::Mono;
            ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            ensure("optimized " + name + ": " + notes(r), r.optimized);
            if (const char* write = std::getenv("AL_OPTIMIZER_WRITE_EXPECTED"); write && *write)
            {
                llofstream out(expected, std::ios::binary);
                out << r.text;
                continue;
            }
            ensure("expected file for " + name, fs::exists(expected));
            ensure_equals("output of " + name, r.text, read(expected));
            ++checked;
        }
        ensure("some golden files", checked > 0 || std::getenv("AL_OPTIMIZER_WRITE_EXPECTED"));
    }
    template<> template<>
    void allsloptimizer_object::test<11>()
    {
        set_test_name("the rotation functions fold over unit rotations by the viewer's own quaternion, and leave the rest");
        const std::string source = wrap("", "        rotation a = llEuler2Rot(<0, 0, PI_BY_TWO>);\n        vector b = llRot2Euler(<0, 0, 0.7071068, 0.7071068>);\n        rotation c = llAxisAngle2Rot(<0, 0, 2>, PI);\n        vector d = llRot2Axis(<0, 0, 0.7071068, 0.7071068>);\n        float e = llRot2Angle(<0, 0, 0.7071068, 0.7071068>);\n        vector f = llRot2Fwd(<0, 0, 0.7071068, 0.7071068>);\n        vector h = llRot2Up(<0.7071068, 0, 0, 0.7071068>);\n        vector i = llRot2Axis(<0, 0, 0, 1>);\n        vector j = llRot2Fwd(<0, 0, 2, 2>);\n        llSay(0, (string)a + (string)b + (string)c + (string)d + (string)e + (string)f + (string)h + (string)i + (string)j);\n        a = c = ZERO_ROTATION; b = d = f = h = i = j = ZERO_VECTOR; e = 0;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        // A quarter turn about z: x and y zero, z and s a half root two.
        ensure("euler to rot: " + r.text, about(numbers(r.text, "rotation a"), { 0.f, 0.f, 0.70710677f, 0.70710677f }));
        ensure("rot to euler: " + r.text, about(numbers(r.text, "vector b"), { 0.f, 0.f, 1.5707964f }));
        ensure("axis and angle, the axis normalised: " + r.text, about(numbers(r.text, "rotation c"), { 0.f, 0.f, 1.f, 0.f }) && !has("llAxisAngle2Rot"));
        ensure("the axis back: " + r.text, about(numbers(r.text, "vector d"), { 0.f, 0.f, 1.f }));
        ensure("the angle back: " + r.text, about(numbers(r.text, "float e"), { 1.5707964f }));
        ensure("fwd of a quarter turn is left: " + r.text, !has("llRot2Fwd(<0, 0, 0.7071068, 0.7071068>)") && has("vector f = <"));
        ensure("up of a roll: " + r.text, !has("llRot2Up(") && has("vector h = <0, -"));
        ensure("no rotation has no axis: left", has("llRot2Axis(<0, 0, 0, 1>)"));
        ensure("not a unit rotation: left", has("llRot2Fwd(<0, 0, 2, 2>)"));
    }
    template<> template<>
    void allsloptimizer_object::test<12>()
    {
        set_test_name("llJsonGetValue folds a string or a plain number out of strict JSON, and leaves the rest to the simulator");
        const std::string source = wrap("", "        string a = llJsonGetValue(\"{\\\"name\\\": \\\"Ann\\\", \\\"tags\\\": [\\\"x\\\", 7, {\\\"k\\\": \\\"v\\\"}]}\", [\"name\"]);\n        string b = llJsonGetValue(\"{\\\"tags\\\": [\\\"x\\\", -7]}\", [\"tags\", 1]);\n        string c = llJsonGetValue(\"{\\\"tags\\\": [\\\"x\\\", 7, {\\\"k\\\": \\\"v\\\"}]}\", [\"tags\", 2, \"k\"]);\n        string d = llJsonGetValue(\"[1.5]\", [0]);\n        string e = llJsonGetValue(\"[true]\", [0]);\n        string f = llJsonGetValue(\"[1]\", [3]);\n        string g = llJsonGetValue(\"{\\\"a\\\": 1, \\\"a\\\": 2}\", [\"a\"]);\n        string h = llJsonGetValue(\"[1,]\", [0]);\n        string i = llJsonGetValue(\"[\\\"a\\\\\\\"b\\\"]\", [0]);\n        llSay(0, a + b + c + d + e + f + g + h + i);\n        a = b = c = d = e = f = g = h = i = \"\";\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        ensure("a key: " + r.text, has("string a = \"Ann\";"));
        ensure("an index, a negative integer: " + r.text, has("string b = \"-7\";"));
        ensure("down a path: " + r.text, has("string c = \"v\";"));
        ensure("a fraction is left: " + r.text, has("string d = llJsonGetValue"));
        ensure("true is its constant: " + r.text, has("string e = JSON_TRUE;"));
        ensure("out of range is JSON_INVALID: " + r.text, has("string f = JSON_INVALID;"));
        ensure("duplicate keys are left", has("string g = llJsonGetValue"));
        ensure("a trailing comma is left", has("string h = llJsonGetValue"));
        ensure("an escape unescaped: " + r.text, has("string i = \"a\\\"b\";"));
        // The answers that are constants are the constants' names.
        const std::string more = wrap("", "        string a = llJsonGetValue(\"[true, null]\", [0]);\n        string b = llJsonGetValue(\"[1]\", [3]);\n        string c = llJsonValueType(\"{\\\"a\\\": [1, {}]}\", [\"a\", 1]);\n        string d = llJsonValueType(\"{\\\"a\\\": 1}\", [\"b\"]);\n        string e = llJsonValueType(\"[1,]\", []);\n        string f = llList2Json(JSON_OBJECT, [\"name\", \"Ann Lee\", \"count\", 3]);\n        string g = llList2Json(JSON_ARRAY, [1, \"true\"]);\n        string h = llList2Json(JSON_ARRAY, [1.5]);\n        list i = llJson2List(\"[1, \\\"two\\\", -3]\");\n        list j = llJson2List(\"{\\\"k\\\": \\\"v\\\"}\");\n        list k = llJson2List(\"[true]\");\n        llSay(0, a + b + c + d + e + f + g + h + llList2CSV(i) + llList2CSV(j) + llList2CSV(k));\n        a = b = c = d = e = f = g = h = \"\"; i = j = k = [];\n");
        ALLSLOptimizer::Result m = ALLSLOptimizer::run(more, options());
        ensure("optimized", m.optimized);
        auto hasm = [&m](const char* text) { return m.text.find(text) != std::string::npos; };
        ensure("true is JSON_TRUE: " + m.text, hasm("string a = JSON_TRUE;"));
        ensure("a miss is JSON_INVALID: " + m.text, hasm("string b = JSON_INVALID;"));
        ensure("the type of a nested object: " + m.text, hasm("string c = JSON_OBJECT;"));
        ensure("the type of nothing: " + m.text, hasm("string d = JSON_INVALID;"));
        ensure("malformed is left to the simulator: " + m.text, hasm("string e = llJsonValueType"));
        ensure("an object from plain strings and integers: " + m.text, hasm("string f = \"{\\\"name\\\":\\\"Ann Lee\\\",\\\"count\\\":3}\";"));
        ensure("a string that reads as a JSON word is left: " + m.text, hasm("string g = llList2Json"));
        ensure("a float is left: " + m.text, hasm("string h = llList2Json"));
        ensure("an array to a list: " + m.text, hasm("list i = [1, \"two\", -3];"));
        ensure("an object to a list: " + m.text, hasm("list j = [\"k\", \"v\"];"));
        ensure("a true in it is left: " + m.text, hasm("list k = llJson2List"));
        // A nested value comes out written the compact way; a set puts a
        // value in, at a key or an index, where the document is plain.
        const std::string sets = wrap("", "        string a = llJsonGetValue(\"{\\\"a\\\": [1, {\\\"b\\\": true}, \\\"c\\\"]}\", [\"a\"]);\n        string b = llJsonSetValue(\"{\\\"a\\\": 1}\", [\"b\"], \"two\");\n        string c = llJsonSetValue(\"[1, 2]\", [1], \"9\");\n        string d = llJsonSetValue(\"[1, 2]\", [JSON_APPEND], JSON_TRUE);\n        string e = llJsonSetValue(\"[1, 2]\", [5], \"9\");\n        string f = llJsonSetValue(\"{}\", [\"a\", \"b\"], \"x\");\n        string g = llJsonSetValue(\"[1]\", [0], \"1.5\");\n        string h = llJsonGetValue(\"[1.5]\", []);\n        llSay(0, a + b + c + d + e + f + g + h);\n        a = b = c = d = e = f = g = h = \"\";\n");
        ALLSLOptimizer::Result n = ALLSLOptimizer::run(sets, options());
        ensure("optimized", n.optimized);
        auto hasn = [&n](const char* text) { return n.text.find(text) != std::string::npos; };
        ensure("a nested value, compact: " + n.text, hasn("string a = \"[1,{\\\"b\\\":true},\\\"c\\\"]\";"));
        ensure("a new key at the end: " + n.text, hasn("string b = \"{\\\"a\\\":1,\\\"b\\\":\\\"two\\\"}\";"));
        ensure("an index replaced with a number: " + n.text, hasn("string c = \"[1,9]\";"));
        ensure("appended by JSON_APPEND with a constant: " + n.text, hasn("string d = \"[1,2,true]\";"));
        ensure("past the end is JSON_INVALID: " + n.text, hasn("string e = JSON_INVALID;"));
        ensure("a path to create is left: " + n.text, hasn("string f = llJsonSetValue"));
        ensure("a value that is a fraction is left: " + n.text, hasn("string g = llJsonSetValue"));
        ensure("a document with a fraction is left: " + n.text, hasn("string h = llJsonGetValue"));
        const std::string dels = wrap("", "        string a = llJsonSetValue(\"{\\\"a\\\": 1, \\\"b\\\": 2}\", [\"a\"], JSON_DELETE);\n        string b = llJsonSetValue(\"[1, 2, 3]\", [1], JSON_DELETE);\n        string c = llJsonSetValue(\"[1, 2, 3]\", [7], JSON_DELETE);\n        llSay(0, a + b + c);\n        a = b = c = \"\";\n");
        ALLSLOptimizer::Result q = ALLSLOptimizer::run(dels, options());
        auto hasq = [&q](const char* text) { return q.text.find(text) != std::string::npos; };
        ensure("a key deleted: " + q.text, hasq("string a = \"{\\\"b\\\":2}\";"));
        ensure("an index deleted: " + q.text, hasq("string b = \"[1,3]\";"));
        ensure("deleting what is not there is the simulator's: " + q.text, hasq("string c = llJsonSetValue"));
    }
    template<> template<>
    void allsloptimizer_object::test<13>()
    {
        set_test_name("a function called once is put in its place: a void one as a block with its parameters as locals, a one-return one as its expression");
        const std::string source =
            "integer g;\n"
            "say(string what, integer n)\n"
            "{\n"
            "    integer i = n * 2;\n"
            "    llSay(0, what + (string)i);\n"
            "}\n"
            "integer twice(integer x)\n"
            "{\n"
            "    return x * 2 + g;\n"
            "}\n"
            "integer thrice(integer x)\n"
            "{\n"
            "    return x + x + x;\n"
            "}\n"
            "loop()\n"
            "{\n"
            "    @again;\n"
            "    jump again;\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        integer i = 1;\n"
            "        say(\"hi\", i);\n"
            "        g = twice(i) + thrice(g) + thrice(i);\n"
            "        loop();\n"
            "    }\n"
            "}\n";
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.dcr                     = false;
        o.constfold               = false;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        ensure("optimized: " + notes(r), r.optimized);
        // The inliner's own text is what the optimizer read.
        const ALLSLInliner::Result put = ALLSLInliner::run(source);
        ensure_equals("five put in place", put.inlined, 5);
        ensure("the block where the call was: " + put.text,
               put.text.find("        {\nstring what = \"hi\";\ninteger n = i;\n\n    integer i_1 = n * 2;\n    llSay(0, what + (string)i_1);\n\n}\n") != std::string::npos);
        ensure("the expression where the other call was, the temporaries before the statement: " + put.text,
               put.text.find("        integer _t_1 = g;\n        integer _t_2 = i;\n        g = (i * 2 + g) + (_t_1 + _t_1 + _t_1) + (_t_2 + _t_2 + _t_2);") != std::string::npos);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        ensure("say went into a block, its i renamed past the caller's: " + r.text, has("integer i_1 = n * 2;") && has("llSay(0, what + (string)i_1);"));
        ensure("the parameters became locals set to the arguments: " + r.text, has("string what = \"hi\";") && has("integer n = i;"));
        ensure("say is gone: " + r.text, !has("say(string what"));
        ensure("twice became its expression with the argument in: " + r.text, has("(i * 2 + g)"));
        ensure("thrice reads its parameter thrice, so each name is read once into a temporary before the statement: " + r.text,
               has("integer _t_1 = g;") && has("integer _t_2 = i;") && has("(_t_1 + _t_1 + _t_1)") && has("(_t_2 + _t_2 + _t_2)") && !has("integer thrice(integer x)"));
        ensure("loop went in with its label and jump given a fresh name: " + r.text, !has("loop();") && !has("loop()\n") && has("@again_1;") && has("jump again_1;"));
        S32 said = 0;
        for (const ALScriptProblem& p : r.problems)
        {
            said += p.severity == ALScriptProblem::Severity::Note && p.message.find("in place of") != std::string::npos;
        }
        ensure_equals("five notes: say, twice, loop, and thrice twice", said, 5);
        // The map leads from the block back to the function's own lines.
        const size_t at = r.text.find("llSay(0, what + (string)i_1);");
        ensure("found", at != std::string::npos);
        S32 line = 0;
        for (size_t i = 0; i < at; ++i)
        {
            line += r.text[i] == '\n';
        }
        const ALSourceMap::Loc from = r.map.toSource(line, 8);
        ensure_equals("the llSay came from the function's fifth line", from.line, 4);
        // With the folds on, a note about the block's code points at the
        // function's own lines.
        o.constfold = true;
        r           = ALLSLOptimizer::run(source, o);
        bool folded_in_body = false;
        for (const ALScriptProblem& p : r.problems)
        {
            if (p.severity == ALScriptProblem::Severity::Note && p.message.find("n * 2") != std::string::npos)
            {
                folded_in_body = p.line == 3;
            }
        }
        ensure("a note on the body's code is at the function's line: " + notes(r), folded_in_body);
    }
    template<> template<>
    void allsloptimizer_object::test<14>()
    {
        set_test_name("a small function returning an expression goes in every place, and a marked one whatever its size");
        const std::string source =
            "integer sq(integer x)\n"
            "{\n"
            "    return x * x;\n"
            "}\n"
            "shout(string what)\n"
            "{\n"
            "    llShout(0, what);\n"
            "    llShout(1, what);\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        integer a = sq(2) + sq(3);\n"
            "        shout(\"x\");\n"
            "        shout(\"y\");\n"
            "        llSay(0, (string)a);\n"
            "    }\n"
            "}\n";
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.dcr                     = false;
        o.constfold               = false;
        ALLSLInliner::Result put  = ALLSLInliner::run(source);
        ensure("sq went into both places, once each round: " + put.text, put.text.find("integer a = (2 * 2) + (3 * 3);") != std::string::npos);
        ensure("and is gone", put.text.find("integer sq(") == std::string::npos);
        ensure("shout, called twice and unmarked, stays: " + put.text, put.text.find("shout(\"x\");") != std::string::npos && put.text.find("shout(string what)") != std::string::npos);
        ensure_equals("two rounds", put.inlined, 2);
        put = ALLSLInliner::run(source, { "shout" });
        ensure("marked, shout goes into both places: " + put.text,
               put.text.find("string what = \"x\";") != std::string::npos && put.text.find("string what = \"y\";") != std::string::npos &&
               put.text.find("shout(string what)") == std::string::npos);
        ensure_equals("four rounds", put.inlined, 4);
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        ensure("the optimizer reads the result: " + notes(r), r.optimized && r.text.find("(2 * 2) + (3 * 3)") != std::string::npos);
    }
    template<> template<>
    void allsloptimizer_object::test<15>()
    {
        set_test_name("a return in the body becomes a jump to a label that ends the block, and a round takes every call that does not cross another's edit");
        const std::string source =
            "check(integer n)\n"
            "{\n"
            "    if (n < 0) return;\n"
            "    llSay(0, \"ok\");\n"
            "    if (n > 9)\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "    llSay(0, \"small\");\n"
            "}\n"
            "inner(integer k)\n"
            "{\n"
            "    llSay(1, (string)k);\n"
            "}\n"
            "outer()\n"
            "{\n"
            "    inner(7);\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        check(3);\n"
            "        outer();\n"
            "    }\n"
            "}\n";
        ALLSLInliner::Result put = ALLSLInliner::run(source);
        auto has = [&put](const char* text) { return put.text.find(text) != std::string::npos; };
        ensure("the returns are jumps: " + put.text, has("if (n < 0) jump _ret_1;") && has("        jump _ret_1;"));
        ensure("to a label that ends the block: " + put.text, has("@_ret_1;\n}") || has("@_ret_1;\n        }"));
        const ALLSLInliner::Result trailing = ALLSLInliner::run("f()\n{\n    llSay(0, \"x\");\n    return;\n}\ndefault\n{\n    state_entry()\n    {\n        f();\n    }\n}\n");
        ensure("a return that ends the body is just dropped: " + trailing.text, trailing.text.find("jump") == std::string::npos && trailing.text.find("@_ret") == std::string::npos &&
               trailing.text.find("llSay(0, \"x\");") != std::string::npos);
        ensure("check is gone", !has("check(integer n)"));
        // outer's one call goes in the first round; inner's call is inside
        // outer, whose lines go that round, so inner waits for the next.
        ensure("inner went in the end: " + put.text, has("integer k = 7;") && !has("inner(integer k)") && !has("outer()\n"));
        ensure_equals("three calls went", put.inlined, 3);
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        ensure("the optimizer reads it: " + notes(r), r.optimized);
    }
    template<> template<>
    void allsloptimizer_object::test<16>()
    {
        set_test_name("an argument that changes nothing goes in as a name does, a temporary where it is read twice; one that may change something keeps the expression out, and a function called once goes as a block");
        const std::string source =
            "integer g;\n"
            "integer both(integer x)\n"
            "{\n"
            "    return x + x;\n"
            "}\n"
            "integer once(integer x)\n"
            "{\n"
            "    return x + 1;\n"
            "}\n"
            "integer bump()\n"
            "{\n"
            "    return ++g;\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        integer a = both(llAbs(g));\n"
            "        integer b = once(g * 2);\n"
            "        integer c = once(llAbs(g));\n"
            "        integer d = once(bump());\n"
            "        integer e = both(llRound(llGetTime()));\n"
            "    }\n"
            "}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source);
        auto has = [&put](const char* text) { return put.text.find(text) != std::string::npos; };
        ensure("a pure call read twice is made once into a temporary: " + put.text, has("        integer _t_1 = llAbs(g);\n        integer a = (_t_1 + _t_1);"));
        ensure("an expression read once goes in, in parentheses: " + put.text, has("integer b = ((g * 2) + 1);"));
        ensure("so does a pure call: " + put.text, has("integer c = ((llAbs(g)) + 1);"));
        // An argument that may change something, or whose answer may
        // change, cannot go into an expression; a function called once
        // goes from a block before the statement instead, its value set by
        // the body.
        ensure("bump, called once, is set by its body before the statement, and once takes its value as a name: " + put.text,
               has("integer _r_1;") && has("_r_1 = ++g;") && has("integer d = (_r_1 + 1);") && !has("integer once(integer x)"));
        ensure("both, its other call gone, sets e itself, the time read once into its parameter: " + put.text,
               has("integer e;") && has("integer x = llRound(llGetTime());") && has("e = x + x;") && !has("integer both(integer x)"));
        ensure("no stray ; where the declaration stood: " + put.text, !has("};"));
        ensure_equals("six went", put.inlined, 6);
        // A temporary above an if whose condition has the call, and above
        // a for whose first part has it; not above a while, whose
        // condition is read every time round, nor an else-if.
        const std::string heads =
            "integer g;\n"
            "integer both(integer x)\n"
            "{\n"
            "    return x + x;\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        if (both(llAbs(g)) > 3)\n"
            "        {\n"
            "            g = 1;\n"
            "        }\n"
            "        integer i;\n"
            "        for (i = both(llAbs(g)), g = 0; i < 9; ++i) g++;\n"
            "        while (both(llAbs(g)) < 9) g++;\n"
            "        if (g) g = 0; else if (both(llAbs(g))) g = 2;\n"
            "    }\n"
            "}\n";
        const ALLSLInliner::Result above = ALLSLInliner::run(heads);
        auto got = [&above](const char* text) { return above.text.find(text) != std::string::npos; };
        ensure("above the if: " + above.text, got("        integer _t_1 = llAbs(g);\n        if ((_t_1 + _t_1) > 3)"));
        ensure("above the for, an assignment after the call's part not minded: " + above.text,
               got("        integer _t_2 = llAbs(g);\n        for (i = (_t_2 + _t_2), g = 0; i < 9; ++i) g++;"));
        ensure("the while keeps its call: " + above.text, got("while (both(llAbs(g)) < 9) g++;"));
        ensure("so does the else-if: " + above.text, got("else if (both(llAbs(g))) g = 2;"));
        ensure_equals("two went", above.inlined, 2);
    }
    template<> template<>
    void allsloptimizer_object::test<17>()
    {
        set_test_name("a function that reaches itself is never put in place, marked or not; a label after the call's statement is the return's target");
        const std::string source =
            "ping(integer n)\n"
            "{\n"
            "    if (n > 0) pong(n - 1);\n"
            "}\n"
            "pong(integer n)\n"
            "{\n"
            "    if (n > 0) ping(n - 1);\n"
            "}\n"
            "self(integer n)\n"
            "{\n"
            "    if (n > 0) self(n - 1);\n"
            "}\n"
            "early(integer n)\n"
            "{\n"
            "    if (n < 0) return;\n"
            "    llSay(0, \"ok\");\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        ping(3);\n"
            "        self(3);\n"
            "        integer i;\n"
            "        for (i = 0; i < 3; ++i)\n"
            "        {\n"
            "            early(i);\n"
            "            @next;\n"
            "        }\n"
            "    }\n"
            "}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source, { "ping", "pong", "self" });
        auto has = [&put](const char* text) { return put.text.find(text) != std::string::npos; };
        ensure("ping and pong, each reaching the other, stay: " + put.text, has("ping(integer n)") && has("pong(integer n)") && has("        ping(3);"));
        ensure("self stays: " + put.text, has("self(integer n)") && has("        self(3);"));
        ensure("early went in, its return a jump to the label already there: " + put.text, has("if (n < 0) jump next;") && !has("_ret") && has("            @next;"));
        ensure_equals("one went", put.inlined, 1);
    }
    template<> template<>
    void allsloptimizer_object::test<18>()
    {
        set_test_name("a run nobody reads the notes of says nothing, and one that reaches its budget stops where it is");
        const std::string       source = wrap("", "        integer a = 1 + 2;\n        integer b = 3 * 4;\n        llSay(0, (string)(a + b));\n");
        ALLSLOptimizer::Options o      = options();
        ALLSLOptimizer::Result  said   = ALLSLOptimizer::run(source, o);
        ensure("the folds are said", !said.problems.empty());
        o.notes = false;
        ALLSLOptimizer::Result quiet = ALLSLOptimizer::run(source, o);
        ensure("nothing said", quiet.problems.empty());
        ensure_equals("and the same text either way", quiet.text, said.text);
        // A budget too small for a round stops before the first, and
        // says so where the notes are read.
        o.notes       = true;
        o.visitBudget = 1;
        ALLSLOptimizer::Result stopped = ALLSLOptimizer::run(source, o);
        ensure("it stopped early", stopped.stoppedEarly);
        ensure("and said so: " + notes(stopped), !stopped.problems.empty() && stopped.problems.back().key == std::string("OptimizerStoppedEarly"));
        ensure("what it has stands", !stopped.text.empty());
    }
    template<> template<>
    void allsloptimizer_object::test<19>()
    {
        set_test_name("a list plus anything is one element longer: no zero, empty string or float is dropped from one, nor a literal's element");
        const std::string source =
            wrap("", "        list l = llGetPrimitiveParams([PRIM_SIZE]);\n"
                     "        l = l + 0;\n"
                     "        l = l + \"\";\n"
                     "        l = 0.0 + l;\n"
                     "        l = l + -1;\n"
                     "        l = l + [];\n"
                     "        string s = llList2String(l, 0) + \"\";\n"
                     "        integer n = llGetListLength(l) + 0;\n"
                     "        llSetPrimitiveParams([PRIM_TEXTURE, 0, s, <1, 1, 0>, ZERO_VECTOR, 0.0]);\n"
                     "        llSay(n, llList2CSV(l) + s);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized", r.optimized);
        ensure("a zero appended stays: " + r.text, r.text.find("l = l + 0;") != std::string::npos);
        ensure("an empty string appended stays: " + r.text, r.text.find("l = l + \"\";") != std::string::npos);
        ensure("a zero put in front stays: " + r.text, r.text.find("l = 0") != std::string::npos && r.text.find("+ l;") != std::string::npos);
        ensure("a negative appended is not turned into a subtraction: " + r.text, r.text.find("l - 1") == std::string::npos);
        ensure("an empty list adds nothing: " + r.text, r.text.find("l + [];") == std::string::npos);
        ensure("nor an empty string to a string: " + r.text, r.text.find("llList2String(l, 0) + \"\"") == std::string::npos);
        // The count is read once, and goes where it is read.
        ensure("nor a zero to an integer: " + r.text, r.text.find("llGetListLength(l) + 0") == std::string::npos && r.text.find("l != [] + 0") == std::string::npos &&
                                                          r.text.find("integer n") == std::string::npos);
        // The literal's elements, however it is written now, all there:
        // the texture's face and the glow.
        const size_t call = r.text.find("llSetPrimitiveParams(");
        ensure("the call", call != std::string::npos);
        const std::string args = r.text.substr(call, r.text.find(';', call) - call);
        ensure("the face, a zero: " + args, args.find("PRIM_TEXTURE + 0") != std::string::npos || args.find("PRIM_TEXTURE, 0") != std::string::npos);
        ensure("and the last element: " + args, args.find("ZERO_VECTOR + 0") != std::string::npos || args.find("ZERO_VECTOR, 0") != std::string::npos);
        // And what it wrote compiles.
        ALLSLService service;
        const ALScriptProblems said = service.check(r.text);
        for (const ALScriptProblem& p : said)
        {
            ensure("no error in what was written: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
        }

        // The preprocessor's lazy-list helper grows its list one zero at a
        // time, and ends.
        const std::string lazy = "list lazy_list_set(list L, integer i, list v)\n{\n    while (llGetListLength(L) < i)\n        L = L + 0;\n    return llListReplaceList(L, v, i, i);\n}\n" +
                                 wrap("", "        list x;\n        x = lazy_list_set(x, 3, [1]);\n        llSay(0, llList2CSV(x));\n");
        r = ALLSLOptimizer::run(lazy, options());
        ensure("the list still grows: " + r.text, r.text.find("L = L + 0;") != std::string::npos);
        ensure("rather than standing still", r.text.find("L = L;") == std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<20>()
    {
        set_test_name("not-equal only takes the place of not-equals where the two say the same: numbers, or any two that are compared not-equal");
        const std::string source =
            wrap("", "        list a = llGetPrimitiveParams([PRIM_SIZE]);\n"
                     "        list b = llGetPrimitiveParams([PRIM_TYPE]);\n"
                     "        integer i = llGetListLength(a);\n"
                     "        integer j = llGetListLength(b);\n"
                     "        integer lists = !(a == b);\n"
                     "        integer same = !(a != b);\n"
                     "        integer ints = !(i == j);\n"
                     "        llSay(0, (string)(lists + same + ints));\n"
                     "        a = b = [];\n        i = j = 0;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("a list's not-equal is the difference of the lengths, so !(a == b) stays: " + r.text, r.text.find("!(a == b)") != std::string::npos);
        // Each read once, and gone where it is read.
        ensure("!(a != b) is a == b for lists too: " + r.text, r.text.find("+ (a == b) +") != std::string::npos);
        ensure("and for integers !(i == j) is i != j: " + r.text, r.text.find("(i != j)") != std::string::npos);
    }
    template<> template<>
    void allsloptimizer_object::test<21>()
    {
        set_test_name("a function goes in place only where the globals it reads are the same there: a caller's local of the name keeps the call");
        const std::string source =
            "integer count;\n"
            "bump()\n"
            "{\n"
            "    count = count + 1;\n"
            "}\n"
            "integer plus(integer x)\n"
            "{\n"
            "    return x + count;\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        integer count = 5;\n"
            "        bump();\n"
            "        llSay(0, (string)plus(1) + (string)count);\n"
            "    }\n"
            "    touch_start(integer n)\n"
            "    {\n"
            "        bump();\n"
            "        llSay(0, (string)plus(n));\n"
            "    }\n"
            "}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source, { "bump", "plus" });
        const size_t               touch = put.text.find("touch_start");
        ensure("both events there", touch != std::string::npos);
        const std::string entry = put.text.substr(0, touch);
        const std::string touched = put.text.substr(touch);
        ensure("where count is the caller's own, the calls stay: " + entry,
               entry.find("        bump();") != std::string::npos && entry.find("plus(1)") != std::string::npos);
        ensure("where it is the global, bump goes in: " + touched, touched.find("bump();") == std::string::npos && touched.find("count = count + 1;") != std::string::npos);
        ensure("and plus: " + touched, touched.find("(n + count)") != std::string::npos);
        ensure("so both functions stay, for the calls that kept them: " + put.text,
               put.text.find("bump()\n{") != std::string::npos && put.text.find("integer plus(integer x)") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<22>()
    {
        set_test_name("what follows a jump stays where it holds the label the jump goes to, and so does a branch never taken that holds one");
        // A jump in SL goes to the last label of its name in the function,
        // whatever block it is in: here, the one in the block after it.
        const std::string source = wrap("",
                                        "        @a;\n"
                                        "        llOwnerSay(\"first\");\n"
                                        "        jump a;\n"
                                        "        {\n"
                                        "            @a;\n"
                                        "            llOwnerSay(\"second\");\n"
                                        "        }\n"
                                        "        llOwnerSay(\"third\");\n"
                                        "        @b;\n"
                                        "        if (0)\n"
                                        "        {\n"
                                        "            @b;\n"
                                        "            llOwnerSay(\"fourth\");\n"
                                        "        }\n"
                                        "        @c;\n"
                                        "        while (0)\n"
                                        "        {\n"
                                        "            @c;\n"
                                        "            llOwnerSay(\"fifth\");\n"
                                        "        }\n"
                                        "        if (llGetUnixTime() > 0) jump b;\n"
                                        "        jump c;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized: " + notes(r), r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        ensure("the block the jump lands in stays: " + r.text, has("llOwnerSay(\"second\");"));
        ensure("and what follows it: " + r.text, has("llOwnerSay(\"third\");"));
        ensure("the if whose branch a jump lands in stays: " + r.text, has("llOwnerSay(\"fourth\");"));
        ensure("and the loop whose body one does: " + r.text, has("llOwnerSay(\"fifth\");"));
        ensure("nothing said to be unreachable: " + notes(r), notes(r).find("can never run") == std::string::npos);

        // Where nothing jumps into it, it is still gone; and a jump from
        // inside it to its own label is no way in.
        r = ALLSLOptimizer::run(wrap("", "        return;\n        {\n            @x;\n            llOwnerSay(\"never\");\n        }\n"), options());
        ensure("a block after a return that nothing jumps into goes: " + r.text, r.text.find("never") == std::string::npos);
        r = ALLSLOptimizer::run(wrap("", "        integer i;\n        return;\n        {\n            @again;\n            llOwnerSay(\"never\");\n            if (++i < 3) jump again;\n        }\n"),
                                options());
        ensure("nor one that only jumps to itself: " + r.text, r.text.find("never") == std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<23>()
    {
        set_test_name("a function put in place as a block takes its arguments in the order a call does: left to right, as LSO and Mono both push them");
        const std::string source =
            "integer n;\n"
            "integer next()\n"
            "{\n"
            "    n = n + 1;\n"
            "    return n;\n"
            "}\n"
            "show(integer a, integer b)\n"
            "{\n"
            "    llOwnerSay((string)a + \",\" + (string)b);\n"
            "}\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        show(next(), next() * 10);\n"
            "    }\n"
            "}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source);
        const size_t               a   = put.text.find("integer a = next();");
        const size_t               b   = put.text.find("integer b = next() * 10;");
        ensure("both arguments set to locals: " + put.text, a != std::string::npos && b != std::string::npos);
        ensure("the first first: " + put.text, a < b);
    }
    template<> template<>
    void allsloptimizer_object::test<24>()
    {
        set_test_name("a true branch that ends in an if of its own is printed in braces where an else follows, so that the else stays the outer if's");
        // if (!c) A; else if (d) B; swapped round: the inner if has no else.
        ALLSLOptimizer::Result r =
            ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else if (d) llOwnerSay(\"B\");\n        c = 1; d = 0;\n"), options());
        ensure("swapped: " + notes(r), has(r, "swapped the branches"));
        ensure("the inner if in braces: " + r.text, r.text.find("        if (c)\n        {\n            if (d)\n                llOwnerSay(\"B\");\n        }\n") != std::string::npos);
        ensure("the else the outer if's: " + r.text, r.text.find("        }\n        else\n            llOwnerSay(\"A\");\n") != std::string::npos);
        // And one whose inner if stands in a loop.
        r = ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else while (d--) if (d == 3) llOwnerSay(\"B\");\n        c = 1;\n"), options());
        ensure("the loop in braces: " + r.text, r.text.find("        if (c)\n        {\n            while (d--)\n") != std::string::npos &&
                                                    r.text.find("        }\n        else\n            llOwnerSay(\"A\");\n") != std::string::npos);
        // Where the inner if has an else of its own, nothing is added.
        r = ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else if (d) llOwnerSay(\"B\"); else llOwnerSay(\"C\");\n        c = 1; d = 0;\n"), options());
        ensure("no braces an else chain does not need: " + r.text, r.text.find('{', r.text.find("if (c)")) > r.text.find("llOwnerSay(\"A\")"));
    }
    template<> template<>
    void allsloptimizer_object::test<25>()
    {
        set_test_name("a call that is the whole of a loop's body or an if's branch, written without braces, keeps its return inside the loop or the branch");
        const std::string fn =
            "skip(integer n)\n"
            "{\n"
            "    if (n == 2) return;\n"
            "    llOwnerSay((string)n);\n"
            "}\n";
        // A return is the end of this time round, not of the loop.
        ALLSLInliner::Result put = ALLSLInliner::run(fn + wrap("", "        integer i;\n        for (i = 0; i < 5; ++i) skip(i);\n        llOwnerSay(\"done\");\n"));
        const size_t label = put.text.find("@_ret_1;");
        const size_t close = put.text.find('}', label);
        ensure("went in: " + put.text, put.inlined == 1 && label != std::string::npos && put.text.find("jump _ret_1;") != std::string::npos);
        ensure("the label ends the loop's body, before anything after the loop: " + put.text,
               close != std::string::npos && close < put.text.find("llOwnerSay(\"done\");"));
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(fn + wrap("", "        integer i;\n        for (i = 0; i < 5; ++i) skip(i);\n        llOwnerSay(\"done\");\n"), o);
        ensure("the optimizer reads it: " + notes(r) + r.text, r.optimized);
        // Between an if and its else, a label after the block was a
        // syntax error the user never wrote.
        put = ALLSLInliner::run(fn + wrap("integer g;\n", "        if (g) skip(g); else llOwnerSay(\"none\");\n"));
        ensure("went in: " + put.text, put.inlined == 1);
        r = ALLSLOptimizer::run(fn + wrap("integer g;\n", "        if (g) skip(g); else llOwnerSay(\"none\");\n        g = llGetUnixTime();\n"), o);
        ensure("the else still follows its if: " + notes(r) + r.text, r.optimized && r.text.find("else") != std::string::npos);
    }
    template<> template<>
    void allsloptimizer_object::test<26>()
    {
        set_test_name("the engine is one thread's at a time: another coming in while one is in is said, and a hold within a hold is none");
        const unsigned before = al_script_engine::clashes().load();
        {
            AL_SCRIPT_ENGINE_HELD;
            {
                // A hold within a hold is the same thread's.
                AL_SCRIPT_ENGINE_HELD;
            }
            ensure_equals("none from within", al_script_engine::clashes().load(), before);
            std::thread other([]() { AL_SCRIPT_ENGINE_HELD; });
            other.join();
            ensure_equals("another thread, while this one is in: said", al_script_engine::clashes().load(), before + 1);
        }
        std::thread after([]() { AL_SCRIPT_ENGINE_HELD; });
        after.join();
        ensure_equals("another, once it is out: nothing", al_script_engine::clashes().load(), before + 1);
    }

    template<> template<>
    void allsloptimizer_object::test<27>()
    {
        set_test_name("a list literal as a sum brackets an element where the sum would bind it otherwise, and stays a literal where the order would show");
        const auto compiles = [](const ALLSLOptimizer::Result& r) {
            ALLSLService           service;
            const ALScriptProblems said = service.check(r.text);
            for (const ALScriptProblem& p : said)
            {
                ensure("no error in what was written: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
            }
        };
        // A comparison among the elements, as a preprocessor writes one:
        // under a + it would take the sum before it as its left side.
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(wrap("", "        list loc_params;\n        integer loc_sitTargetsRemaining = 1;\n"
                                                                "        loc_params = loc_params +\n"
                                                                "            [ 41\n"
                                                                "            , ((integer)-1) < --loc_sitTargetsRemaining\n"
                                                                "            , <((float)0), ((float)0), ((float)0)>\n"
                                                                "            , <((float)0), ((float)0), ((float)0), ((float)1)>\n"
                                                                "            ];\n"),
                                                       options());
        ensure("optimized: " + notes(r), r.optimized);
        ensure("the comparison bracketed: " + r.text,
               r.text.find("(list)41 + (-1 < --loc_sitTargetsRemaining) + <0, 0, 0> + <0, 0, 0, 1>") != std::string::npos);
        compiles(r);

        // The first element under the cast, bracketed where the cast would
        // take less of it; the rest where the sum would.
        r = ALLSLOptimizer::run(wrap("", "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n        list l;\n"
                                         "        l = [a - b, 1];\n        l = [--a, 2];\n        l = [-1, 3];\n        l = [a * b, a - b];\n        llSay(0, llList2CSV(l));\n"),
                                options());
        ensure("a difference: " + r.text, r.text.find("l = (list)(a - b) + 1;") != std::string::npos);
        ensure("a step down: " + r.text, r.text.find("l = (list)(--a) + 2;") != std::string::npos);
        ensure("a negative constant bare: " + r.text, r.text.find("l = (list)-1 + 3;") != std::string::npos);
        ensure("a product under the cast, a difference after it: " + r.text, r.text.find("l = (list)(a * b) + (a - b);") != std::string::npos);
        compiles(r);

        // A sum takes its right side first, so the elements would be taken
        // last to first: where more than one of them could see another's
        // change, the literal stays.
        r = ALLSLOptimizer::run(wrap("integer g;\ninteger bump()\n{\n    return ++g;\n}\n",
                                     "        integer i = (integer)llFrand(9);\n        list l;\n"
                                     "        l = [i, --i];\n        l = [bump(), bump()];\n        l = [llGetUnixTime(), bump()];\n        l = [i, g];\n"
                                     "        llSay(0, llList2CSV(l));\n"),
                                options());
        ensure("a name and its change: " + r.text, r.text.find("l = [i, --i];") != std::string::npos);
        ensure("two calls that change: " + r.text, r.text.find("l = [bump(), bump()];") != std::string::npos);
        ensure("a read and a call that changes: " + r.text, r.text.find("l = [llGetUnixTime(), bump()];") != std::string::npos);
        ensure("reads alone, a sum: " + r.text, r.text.find("l = (list)i + g;") != std::string::npos);
        compiles(r);
    }

    template<> template<>
    void allsloptimizer_object::test<28>()
    {
        set_test_name("what can never run, removed, is said as far as its first line and a few words of it");
        std::string body = "        return;\n        llOwnerSay(\"";
        body += std::string(300, 'x');
        body += "\");\n";
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(wrap("", body), options());
        const auto note = std::find_if(r.problems.begin(), r.problems.end(), [](const ALScriptProblem& p) { return p.key == "OptimizerRemovedUnreachable"; });
        ensure("said", note != r.problems.end() && note->args.size() == 1);
        ensure("not all of it", note->args[0].size() < 80 && note->args[0].find("\xE2\x80\xA6") != std::string::npos);
        ensure("from its start", note->args[0].rfind("llOwnerSay", 0) == 0);
    }

    template<> template<>
    void allsloptimizer_object::test<29>()
    {
        set_test_name("one budget for the inliner and the optimizer: what the inliner visits is spent, and a round is not begun the budget cannot see through");
        const std::string source = wrap("integer twice(integer n) { return n * 2; }\ninteger thrice(integer n) { return twice(n) + n; }\n",
                                        "        llSay(0, (string)thrice(2));\n        llSay(0, (string)(1 + 2));\n");
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        const ALLSLOptimizer::Result whole = ALLSLOptimizer::run(source, o);
        ensure("with room: not stopped", !whole.stoppedEarly);
        // Room for the inliner's first round and not much more: it stops,
        // and leaves the optimizer nothing.
        const ALLSLInliner::Result once = ALLSLInliner::run(source, {}, size_t(-1));
        ensure("the inliner visits", once.visited > 0);
        o.visitBudget = once.visited / 4;
        const ALLSLOptimizer::Result short_of = ALLSLOptimizer::run(source, o);
        ensure("the optimizer stopped for what the inliner spent", short_of.stoppedEarly);
        ensure("said", std::any_of(short_of.problems.begin(), short_of.problems.end(), [](const ALScriptProblem& p) { return p.key == "OptimizerStoppedEarly"; }));
        const ALLSLInliner::Result cut = ALLSLInliner::run(source, {}, 1);
        ensure("the inliner's one round done, the next not begun", cut.inlined > 0 && (cut.stoppedEarly || cut.inlined == once.inlined));
    }
    template<> template<>
    void allsloptimizer_object::test<30>()
    {
        set_test_name("an if whose condition is always true stays where it is the last around a state change in a function, which may change state only under one");
        const std::string source = "f()\n{\n    if (TRUE) state other;\n}\n"
                                   "g()\n{\n    if (TRUE)\n    {\n        if (llGetUnixTime()) state other;\n    }\n}\n"
                                   "h()\n{\n    if (FALSE) llOwnerSay(\"x\");\n    else state other;\n}\n"
                                   "default\n{\n    state_entry()\n    {\n        f();\n        g();\n        h();\n        if (TRUE) state other;\n    }\n}\n"
                                   "state other\n{\n    state_entry()\n    {\n    }\n}\n";
        ALLSLService service;
        bool         refused = false;
        for (const ALScriptProblem& p : service.check("f()\n{\n    state other;\n}\ndefault\n{\n    state_entry()\n    {\n        f();\n    }\n}\nstate other\n{\n    state_entry()\n    {\n    }\n}\n"))
        {
            refused = refused || p.severity == ALScriptProblem::Severity::Error;
        }
        ensure("a bare state change in a function does not compile", refused);
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized: " + notes(r), r.optimized);
        for (const ALScriptProblem& p : service.check(r.text))
        {
            ensure("what was written compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
        }
        const auto body = [&r](const char* name) {
            const size_t at = r.text.find(std::string(name) + "()\n{");
            return at == std::string::npos ? std::string() : r.text.substr(at, r.text.find("\n}", at) - at);
        };
        ensure("f keeps its if: " + r.text, body("f").find("if (") != std::string::npos && body("f").find("state other;") != std::string::npos);
        ensure("g loses the outer if, the inner one standing: " + r.text, body("g").find("llGetUnixTime()") != std::string::npos);
        ensure("h keeps its if too: " + r.text, body("h").find("if (") != std::string::npos);
        const size_t entry = r.text.find("state_entry()");
        ensure("an event changes state as it likes: its if goes: " + r.text,
               entry != std::string::npos && r.text.find("if (", entry) > r.text.find("state other;", entry));
    }
    template<> template<>
    void allsloptimizer_object::test<31>()
    {
        set_test_name("a key out of a list's range is NULL_KEY under LSO, written by its name; under Mono an empty key, which a key cannot be written as");
        const std::string source = "default\n{\n    state_entry()\n    {\n        llOwnerSay((string)llList2Key([1, 2], 5));\n    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.target                  = ALLSLOptimizer::Target::LSO;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        ensure("LSO: NULL_KEY: " + r.text, r.text.find("llOwnerSay((string)NULL_KEY);") != std::string::npos);
        o.target = ALLSLOptimizer::Target::Mono;
        r        = ALLSLOptimizer::run(source, o);
        ensure("Mono: still the call: " + r.text, r.text.find("llList2Key(") != std::string::npos);
    }
    template<> template<>
    void allsloptimizer_object::test<32>()
    {
        set_test_name("a read of the world or the clock whose answer nobody takes goes, but is never read at another time; a function whose answer is its arguments' alone is pure");
        const std::string source = "default\n{\n    state_entry()\n    {\n"
                                   "        llGetPos();\n"
                                   "        llSetPos(<1, 2, 3>);\n"
                                   "        vector p = llGetPos();\n"
                                   "        list l = [llGetTime(), llGetTime()];\n"
                                   "        llOwnerSay(llList2CSV(l));\n"
                                   "    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.target                  = ALLSLOptimizer::Target::Mono;
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        ensure("optimized: " + notes(r), r.optimized);
        ensure("the unused read goes: " + r.text, r.text.find("    llGetPos();") == std::string::npos);
        ensure("the change stays: " + r.text, r.text.find("llSetPos(") != std::string::npos);
        ensure("so does the unused local read from the world: " + r.text, r.text.find("vector p") == std::string::npos);
        ensure("two reads of the clock are not put in another order: " + r.text, r.text.find("[llGetTime(), llGetTime()]") != std::string::npos);
        for (const char* pure : { "llRound", "llListFindList", "llListSort", "llDumpList2String", "llMD5String", "llAcos", "llAsin" })
        {
            ensure(std::string(pure) + " is pure", ALLSLTraits::pure(pure));
        }
        ensure("llGetPos is not", !ALLSLTraits::pure("llGetPos") && ALLSLTraits::of("llGetPos")->mustUse);
    }
    template<> template<>
    void allsloptimizer_object::test<33>()
    {
        set_test_name("a function returning a value goes in whatever its body: into what a declaration or an assignment sets, back out of a function returning the same, into a fresh local before the statement -- where nothing run before it would see a difference");
        const std::string source =
            "integer g;\n"
            "integer f(integer k)\n{\n    if (k) return k + g;\n    g = 7;\n    return 0;\n}\n"
            "integer outer(integer k)\n{\n    return f(k);\n}\n"
            "default\n{\n    state_entry()\n    {\n"
            "        integer x = f(1);\n"
            "        x = f(2);\n"
            "        f(3);\n"
            "        x = f(4) + g;\n"
            "        x = g + f(5);\n"
            "        if (x) x = f(6);\n"
            "        llOwnerSay((string)outer(x));\n"
            "    }\n}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source, { "f" });
        auto has = [&put](const std::string& text) { return put.text.find(text) != std::string::npos; };
        ensure("the declaration's variable set by the body, its returns but the last jumps: " + put.text,
               has("integer x;\n{\ninteger k = 1;") && has("{ x = k + g; jump _ret_") && has("    x = 0;"));
        ensure("the assignment's too: " + put.text, has("integer k = 2;"));
        ensure("a value nobody takes: the returns that change nothing go: " + put.text, has("integer k = 3;\n\n    if (k) jump _ret_3;\n    g = 7;"));
        // A label is one to an event: every block's end its own, however
        // many rounds made them.
        for (const char* label : { "@_ret_1;", "@_ret_2;", "@_ret_3;", "@_ret_4;", "@_ret_5;", "@_ret_6;" })
        {
            const size_t first = put.text.find(label);
            ensure(std::string("one ") + label + ": " + put.text, first == std::string::npos || put.text.find(label, first + 1) == std::string::npos);
        }
        ensure("g runs before f(4) and f writes g: the call stays: " + put.text, has("x = f(4) + g;"));
        ensure("f(5) is the right operand and runs first: set before the statement: " + put.text, has("integer _r_") && has("x = g + _r_"));
        ensure("a branch standing alone gets braces for the block: " + put.text, has("if (x) {") || has("if (x) {\n"));
        ensure("outer returns f's value as its own, then goes where it is called: " + put.text, !has("integer outer(integer k)") && has("integer k_1 = k;"));
        ensure("f stays, one call of it standing: " + put.text, has("integer f(integer k)"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(put.text))
        {
            ensure("what was written compiles: " + p.message + "\n" + put.text, p.severity != ALScriptProblem::Severity::Error);
        }
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.inlineNames             = { "f" };
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        ensure("the optimizer reads it: " + notes(r), r.optimized);
        for (const ALScriptProblem& p : service.check(r.text))
        {
            ensure("and what it wrote compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
        }
    }
    template<> template<>
    void allsloptimizer_object::test<34>()
    {
        set_test_name("a call in a loop's condition or a for's step goes too: a while or a for written as its label, if and jump back, a do's value set at the end of its body");
        const std::string source =
            "integer g;\n"
            "integer next()\n{\n    g = g + 1;\n    return g;\n}\n"
            "tick(integer i)\n{\n    llOwnerSay((string)i);\n}\n"
            "default\n{\n    state_entry()\n    {\n"
            "        while (next() < 10) { if (g == 3) jump c1; llOwnerSay(\"w\"); @c1; }\n"
            "        integer i;\n"
            "        for (i = 0; next() < 20; tick(i++)) { if (i == 5) jump b2; }\n"
            "        @b2;\n"
            "        if (g) do llOwnerSay(\"d\"); while (next() < 30);\n"
            "    }\n}\n";
        const ALLSLInliner::Result put = ALLSLInliner::run(source, { "next", "tick" });
        auto has = [&put](const std::string& text) { return put.text.find(text) != std::string::npos; };
        ensure("every call went, and both functions: " + put.text, !has("next()") && !has("tick(") && !has("integer next()"));
        ensure("the while and the for are labels and ifs: " + put.text, !has("while (next") && !has("for (") && has("@_loop_1;") && has("jump _loop_1;") &&
                                                                              has("@_loop_2;") && has("jump _loop_2;"));
        ensure("the for's first part runs once, before its label: " + put.text, put.text.find("i = 0;") < put.text.find("@_loop_2;"));
        ensure("its step's call goes as a statement's, the argument set in the call's order: " + put.text, has("integer i_1 = i++;"));
        ensure("the do stays a do, braces round the if's branch for its value's local: " + put.text, has("if (g) {") && has("do {") && has("} while (_r_"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(put.text))
        {
            ensure("what was written compiles: " + p.message + "\n" + put.text, p.severity != ALScriptProblem::Severity::Error);
        }
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.inlineNames             = { "next", "tick" };
        for (ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            o.target                       = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            ensure("optimized: " + notes(r), r.optimized);
            for (const ALScriptProblem& p : service.check(r.text))
            {
                ensure("and what the optimizer wrote compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }
    template<> template<>
    void allsloptimizer_object::test<35>()
    {
        set_test_name("a local set from what changes nothing and read once goes where it is read, where nothing between writes what it reads; a jump that goes where running on goes goes");
        const std::string source =
            wrap("integer g;\n", "        integer a = llGetUnixTime();\n"
                                   "        integer b = g * 2;\n"
                                   "        llOwnerSay((string)b);\n"
                                   "        integer c = g + 1;\n"
                                   "        g = 5;\n"
                                   "        llOwnerSay((string)c);\n"
                                   "        integer d = g * 3;\n"
                                   "        llOwnerSay((string)(d + d));\n"
                                   "        integer e = g * 4;\n"
                                   "        while (a--) llOwnerSay((string)e);\n"
                                   "        integer f = g * 5;\n"
                                   "        if (a) { llOwnerSay((string)f); }\n"
                                   "        integer h = g * 6;\n"
                                   "        @here;\n"
                                   "        llOwnerSay((string)h);\n"
                                   "        if (a) { g = 1; jump out; }\n"
                                   "        @out;\n"
                                   "        if (a) { g = 2; jump twice; }\n"
                                   "        @twice;\n"
                                   "        if (g) jump here;\n"
                                   "        if (a) { @twice; g = 3; }\n"
                                   "        llOwnerSay((string)(a + g));\n");
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized: " + notes(r), r.optimized);
        auto has = [&r](const std::string& text) { return r.text.find(text) != std::string::npos; };
        ensure("b goes where it is read: " + r.text, !has("integer b") && has("llOwnerSay((string)(g * 2));"));
        ensure("c's g is written between: it stays: " + r.text, has("integer c = g + 1;"));
        ensure("d is read twice: " + r.text, has("integer d = g * 3;"));
        ensure("e is read round a loop: " + r.text, has("integer e = g * 4;"));
        ensure("f is read in a branch, a statement of its own: " + r.text, has("integer f = g * 5;"));
        ensure("h is read after a label: " + r.text, has("integer h = g * 6;"));
        ensure("the read of the clock stays: " + r.text, has("integer a = llGetUnixTime();"));
        ensure("a jump to the label next goes, and the label with it: " + r.text, !has("jump out;") && !has("@out;"));
        ensure("a jump to a label whose name is twice in the event stays: " + r.text, has("jump twice;"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            ensure("what was written compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
        }
    }
    template<> template<>
    void allsloptimizer_object::test<36>()
    {
        set_test_name("a block in a block is flattened where its names are its own, and a declaration moves down to its first assignment where nothing between jumps or names it");
        const std::string source =
            wrap("integer g;\n", "        { llOwnerSay(\"a\"); }\n"
                              "        { integer u = llGetUnixTime(); llOwnerSay((string)(u + u)); }\n"
                              "        { integer v = llGetUnixTime(); llOwnerSay((string)(v + v)); }\n"
                              "        { integer v = llGetUnixTime(); llOwnerSay((string)(v * v)); }\n"
                              "        integer x;\n"
                              "        llOwnerSay(\"b\");\n"
                              "        x = llGetUnixTime();\n"
                              "        llOwnerSay((string)(x + x));\n"
                              "        integer y;\n"
                              "        if (llGetUnixTime() & 1) jump over;\n"
                              "        y = llGetUnixTime();\n"
                              "        @over;\n"
                              "        llOwnerSay((string)(y + y));\n");
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized: " + notes(r), r.optimized);
        auto has = [&r](const std::string& text) { return r.text.find(text) != std::string::npos; };
        ensure("a block of one statement is that statement: " + r.text, has("    llOwnerSay(\"a\");") && !has("{\n            llOwnerSay(\"a\");"));
        ensure("one whose local is named nowhere else too: " + r.text, has("        integer u = llGetUnixTime();"));
        ensure("two whose locals share a name stay blocks: " + r.text, has("            integer v = llGetUnixTime();"));
        ensure("x declared where it is first set: " + r.text, !has("integer x;") && has("integer x = llGetUnixTime();"));
        ensure("y not past the jump: " + r.text, has("integer y;") && has("y = llGetUnixTime();"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            ensure("what was written compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
        }
    }
    template<> template<>
    void allsloptimizer_object::test<37>()
    {
        set_test_name("what the optimizer wrote is checked: where it does not compile, the script goes as it was, and that is said");
        // A literal past a float's range: what it folds to cannot be
        // written back (O0g), which is how this test finds a failure.
        const std::string source = "default\n{\n    state_entry()\n    {\n        llSetPos(<-2.0e+9999, 2.0e+9999, 0>);\n    }\n}\n";
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        if (r.optimized)
        {
            // Should the optimizer learn to write it, the check has nothing
            // to refuse; what it wrote must then compile.
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text))
            {
                ensure("what was written compiles: " + p.message + "\n" + r.text, p.severity != ALScriptProblem::Severity::Error);
            }
            return;
        }
        ensure_equals("the source as it was", r.text, source);
        ensure("said, once, as a warning", r.problems.size() == 1 && r.problems[0].key == "OptimizerWroteUncompilable" &&
                                                r.problems[0].severity == ALScriptProblem::Severity::Warning);
        ensure_equals("its map the source's own", r.map.toSource(4, 8).line, 4);
    }

    template<> template<>
    void allsloptimizer_object::test<38>()
    {
        set_test_name("a global's value is written where it is read only where that costs less than the global, on each target");
        const auto reads = [](const std::string& global, const std::string& read, int times) {
            std::string body;
            for (int i = 0; i < times; ++i)
            {
                body += "        " + read + "\n";
            }
            return wrap(global + "\n", "        vector v; float f; string s;\n" + body + "        llOwnerSay((string)[v, f, s]);\n");
        };
        const auto kept = [](const ALLSLOptimizer::Result& r, const std::string& global) { return r.text.find(global) != std::string::npos; };
        ALLSLOptimizer::Options lso  = options();
        lso.target                   = ALLSLOptimizer::Target::LSO;
        ALLSLOptimizer::Options mono = options();
        ALLSLOptimizer::Options luau = options();
        luau.target                  = ALLSLOptimizer::Target::Luau;

        // A vector: once is less than the global everywhere; three times
        // is not on LSO, nor on Mono unless its parts are whole.
        const std::string V = "vector V = <1.5, 2.5, 3.5>;";
        const std::string W = "vector W = <1.0, 2.0, 3.0>;";
        ensure("a vector read once goes on LSO", !kept(ALLSLOptimizer::run(reads(V, "v += V;", 1), lso), "vector V"));
        ensure("read three times it stays on LSO", kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), lso), "vector V"));
        ensure("and on Mono", kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), mono), "vector V"));
        ensure("but not whole on Mono", !kept(ALLSLOptimizer::run(reads(W, "v += W;", 3), mono), "vector W"));
        ensure("and not on Luau, which keeps a constant once", !kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), luau), "vector V"));

        // A float goes on LSO and Luau however often; on Mono, up to where
        // nine bytes a read come to the field.
        const std::string F = "float F = 2.5;";
        ensure("a float read twenty times goes on LSO", !kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), lso), "float F"));
        ensure("and on Luau", !kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), luau), "float F"));
        ensure("but stays on Mono", kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), mono), "float F"));
        ensure("which takes it read three times", !kept(ALLSLOptimizer::run(reads(F, "f += F;", 3), mono), "float F"));

        // A string: Mono holds it once whatever; LSO writes it at each
        // place, which a long one read often does not pay for.
        const std::string S = "string S = \"a sentence long enough to matter\";";
        ensure("a long string read five times stays on LSO", kept(ALLSLOptimizer::run(reads(S, "s += S;", 5), lso), "string S"));
        ensure("and goes on Mono", !kept(ALLSLOptimizer::run(reads(S, "s += S;", 5), mono), "string S"));

        // Folded into a larger value, a global is no read of it.
        const ALLSLOptimizer::Result folded = ALLSLOptimizer::run(reads(V, "v += V * 2;", 3), lso);
        ensure("a product folds: " + folded.text, !kept(folded, "vector V") && folded.text.find("<3.0, 5.0, 7.0>") != std::string::npos);
    }

    template<> template<>
    void allsloptimizer_object::test<39>()
    {
        set_test_name("with inlining, a function called from several places goes where the target's compiler says that is smaller, and says so");
        const std::string source = "say(integer n)\n{\n    llOwnerSay(\"n=\" + (string)n);\n}\n"
                                   "tell(integer n)\n{\n    if (n > 2)\n        llOwnerSay(\"many: \" + (string)n + \" of them, which is a lot\");\n"
                                   "    else\n        llOwnerSay(\"a few: \" + (string)n);\n    llSetText((string)n, <1.0, 0.5, 0.25>, 1.0);\n}\n"
                                   "default\n{\n    touch_start(integer t)\n    {\n        integer a = t * 3;\n        integer b = a + t;\n"
                                   "        say(a); say(b); say(t);\n        tell(a); tell(b); tell(a * b); tell(a + b); tell(t);\n    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono })
        {
            o.target                       = target;
            o.inlineByCost                 = true;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            ensure("optimized: " + notes(r), r.optimized);
            ensure("the small one goes: " + r.text, r.text.find("say(") == std::string::npos);
            ensure("the large one stays: " + r.text, r.text.find("tell(") != std::string::npos);
            ensure("weighed", r.weight.compiled && r.weight.total > 0);
            const ALScriptProblem* chose = nullptr;
            for (const ALScriptProblem& p : r.problems)
            {
                chose = p.key == "InlinerChoseFunction" ? &p : chose;
            }
            ensure("said: " + notes(r), chose && chose->args.size() == 4 && chose->args[0] == "say" && chose->args[1] == "3");
            ensure_equals("at the function", chose->line, 0);
            ensure_equals("to its end", chose->endLine, 3);

            o.inlineByCost                    = false;
            const ALLSLOptimizer::Result left = ALLSLOptimizer::run(source, o);
            ensure("left alone without it: " + left.text, left.text.find("say(") != std::string::npos);
            ensure("and nothing weighed", !left.weight.compiled);
        }
        // On Luau, where a call costs little, putting it in place comes to
        // a byte more: tried, and not kept.
        o.target                              = ALLSLOptimizer::Target::Luau;
        o.inlineByCost                        = true;
        const ALLSLOptimizer::Result weighed  = ALLSLOptimizer::run(source, o);
        o.inlineByCost                        = false;
        const ALLSLOptimizer::Result unweighed = ALLSLOptimizer::run(source, o);
        ensure("never larger for trying", weighed.weight.compiled && weighed.weight.total <= ALScriptWeigh::lslLuau(unweighed.text).total);
    }
} // namespace tut
