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

#include "../allslinliner.h"
#include "../allsloptimizer.h"
#include "../allslservice.h"

#include "../test/lltut.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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
        set_test_name("llGetListLength becomes a comparison, and a list literal a sum, with parentheses where they matter");
        const std::string source = wrap("list l;\n", "        integer n = llGetListLength(l) + 1;\n        if (llGetListLength(l)) l = [1, \"a\"];\n        l = [];\n        n = 0;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        ensure("optimized: " + notes(r), r.optimized);
        ensure("in a sum: " + r.text, r.text.find("integer n = (l != []) + 1;") != std::string::npos);
        ensure("as a condition", r.text.find("if (l != [])") != std::string::npos);
        ensure("list add: " + r.text, r.text.find("l = (list)1 + \"a\";") != std::string::npos);
        ensure("an empty list stays", r.text.find("l = [];") != std::string::npos);

        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        r                           = ALLSLOptimizer::run(source, lso);
        ensure("no list add on LSO", r.text.find("l = [1, \"a\"];") != std::string::npos);
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
        const fs::path dir     = fs::path(AL_ALSCRIPT_TEST_DIR) / "optimizer";
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
                std::ifstream     in(path, std::ios::binary);
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
                std::ofstream out(expected, std::ios::binary);
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
        ensure("euler to rot: " + r.text, has("rotation a = <0, 0, 0.70710677, 0.70710677>;") || has("rotation a = <0, 0, 0.7071068, 0.7071068>;"));
        ensure("rot to euler: " + r.text, has("vector b = <0, 0, 1.5707964>;") || has("vector b = <0, 0, 1.5707963>;"));
        ensure("axis and angle, the axis normalised: " + r.text, has("rotation c = <0, 0, 1, ") && !has("llAxisAngle2Rot"));
        ensure("the axis back, a bit under one as the division leaves it: " + r.text, has("vector d = <0, 0, 0.99999994>;") || has("vector d = <0, 0, 1>;"));
        ensure("the angle back: " + r.text, has("float e = 1.5707964;") || has("float e = 1.5707963;"));
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
        ensure("true is left", has("string e = llJsonGetValue"));
        ensure("out of range is left", has("string f = llJsonGetValue"));
        ensure("duplicate keys are left", has("string g = llJsonGetValue"));
        ensure("a trailing comma is left", has("string h = llJsonGetValue"));
        ensure("an escape unescaped: " + r.text, has("string i = \"a\\\"b\";"));
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
        ensure_equals("two put in place", put.inlined, 2);
        ensure("the block where the call was: " + put.text,
               put.text.find("        {\nstring what = \"hi\";\ninteger n = i;\n\n    integer i_1 = n * 2;\n    llSay(0, what + (string)i_1);\n\n}\n") != std::string::npos);
        ensure("the expression where the other call was: " + put.text, put.text.find("g = (i * 2 + g) + thrice(g) + thrice(i);") != std::string::npos);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        ensure("say went into a block, its i renamed past the caller's: " + r.text, has("integer i_1 = n * 2;") && has("llSay(0, what + (string)i_1);"));
        ensure("the parameters became locals set to the arguments: " + r.text, has("string what = \"hi\";") && has("integer n = i;"));
        ensure("say is gone: " + r.text, !has("say(string what"));
        ensure("twice became its expression with the argument in: " + r.text, has("(i * 2 + g)"));
        ensure("thrice uses its parameter thrice, so only a constant would do: left: " + r.text, has("thrice(g)") && has("thrice(i)") && has("integer thrice(integer x)"));
        ensure("loop has a label and a jump: left: " + r.text, has("loop();") && has("@again;"));
        S32 notes = 0;
        for (const ALScriptProblem& p : r.problems)
        {
            notes += p.severity == ALScriptProblem::Severity::Note && p.message.find("in place of its one call") != std::string::npos;
        }
        ensure_equals("two notes", notes, 2);
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
    }
} // namespace tut
