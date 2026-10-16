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

#include "../core/alscriptengine.h"
#include "../lsl/optimizer/allslinliner.h"
#include "../lsl/optimizer/allsloptimizer.h"
#include "../lsl/allslservice.h"
#include "../lsl/allsltraits.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace
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
                    FAIL("the builtins did not load: " + error);
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
                // Declared with no value, where its default is the smaller
                // (ALLSLCosts::drop*Default): the default.
                if (text.find(declaration + ";") != std::string::npos)
                {
                    const std::string type = declaration.substr(0, declaration.find(' '));
                    return type == "vector" ? std::vector<F32>{ 0.f, 0.f, 0.f } : type == "rotation" ? std::vector<F32>{ 0.f, 0.f, 0.f, 1.f } : std::vector<F32>{ 0.f };
                }
                return out;
            }
            const char* p = text.c_str() + at + declaration.size() + 3;
            if (*p == '<')
            {
                ++p;
            }
            while (true)
            {
                // A negative part may be written as its cast, on Mono.
                while (*p == ' ')
                {
                    ++p;
                }
                bool cast = false;
                for (const char* spelt : { "((float)", "((integer)" })
                {
                    if (std::strncmp(p, spelt, std::strlen(spelt)) == 0)
                    {
                        p += std::strlen(spelt);
                        cast = true;
                    }
                }
                char*     end = nullptr;
                const F32 v   = std::strtof(p, &end);
                if (end == p)
                {
                    break;
                }
                out.push_back(v);
                p = end;
                if (cast && *p == ')')
                {
                    ++p;
                }
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

        // A script optimized for a target with the defaults, what it became
        // checked to compile there.
        static std::string require_optimized(const std::string& source, ALLSLOptimizer::Target target)
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, target != ALLSLOptimizer::Target::LSO))
            {
                UNSCOPED_INFO("no error in what was written: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
            return r.text;
        }

        static bool in(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

        // A script whose default state also says each of `globals` when
        // touched: read before anything there sets them, they hold what was
        // set before, and stay globals (localizeGlobals).
        static std::string kept(const std::string& source, std::initializer_list<const char*> globals)
        {
            std::string said;
            for (const char* g : globals)
            {
                said += std::string("        llOwnerSay((string)") + g + ");\n";
            }
            const size_t end = source.rfind("}\n");
            return source.substr(0, end) + "    touch_start(integer kept)\n    {\n" + said + "    }\n}\n";
        }
    };

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: constants fold and never-assigned variables inline and unused ones go", "[allsloptimizer]")
    {
        const std::string source = "integer unused = 5;\ninteger used = 2;\n" +
                                   wrap("", "        integer x = used * 3 + 1;\n        llSay(0, (string)x);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        CHECK(r.text == wrap("", "        llSay(0, \"7\");\n"));
        CHECK(has(r, "removed the unused global unused"));
        UNSCOPED_INFO("noted the fold: " + notes(r));
        CHECK(has(r, "folded used * 3 + 1 to 7"));
        CHECK(has(r, "removed the unused local x"));
        CHECK(r.sizeAfter < r.sizeBefore);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: floats fold in single precision and print so that they read back exactly", "[allsloptimizer]")
    {
        // Each variable may be assigned again before it is read, so that
        // it stays a variable and the fold shows in its declaration.
        const std::string source = wrap("", "        float a = 0.1 + 0.2;\n        float b = PI * 2;\n        float c = 1.0 / 3;\n        if (llFrand(1) < 0.5)\n        {\n            a = b = c = 0;\n        }\n        llSay(0, (string)a + (string)b + (string)c);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        // 0.1f + 0.2f is 0.3 exactly as a single, PI * 2 is 6.2831855f,
        // and each reads back as the same single.
        UNSCOPED_INFO("as written: " + r.text);
        REQUIRE(r.text.find("float a = 0.3;") != std::string::npos);
        REQUIRE(r.text.find("float b = 6.2831855;") != std::string::npos);
        REQUIRE(r.text.find("float c = 0.33333334;") != std::string::npos);

        // A float that is a whole number prints as an integer where LSL
        // would convert one, and stays a float where it would not.
        r = ALLSLOptimizer::run(wrap("", "        float f = 2.0;\n        list l = [2.0];\n        string s = (string)2.0;\n        vector v = <1.0, 2.0, 3.5>;\n        if (llFrand(1) < 0.5)\n        {\n            f = 0; l = []; s = \"\"; v = ZERO_VECTOR;\n        }\n        llSay(0, (string)f + llList2CSV(l) + s + (string)v);\n"), options());
        UNSCOPED_INFO("whole float as integer: " + r.text);
        REQUIRE(r.text.find("float f = 2;") != std::string::npos);
        // As the cast of an integer on Mono, where that is smaller (ALLSLCosts::castForWholeFloat).
        UNSCOPED_INFO("list element stays a float: " + r.text);
        REQUIRE(r.text.find("list l = (list)((float)2);") != std::string::npos);
        UNSCOPED_INFO("cast folded with six places");
        REQUIRE(r.text.find("string s = \"2.000000\";") != std::string::npos);
        UNSCOPED_INFO("vector components");
        REQUIRE(r.text.find("vector v = <1, 2, 3.5>;") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: integer arithmetic wraps as the VM's does and division by zero is left", "[allsloptimizer]")
    {
        const std::string source = wrap("", "        llSay(0, (string)(2147483647 + 1));\n        llSay(0, (string)(1 / 0));\n        llSay(0, (string)(-2147483648 / -1));\n        llSay(0, (string)(7 / 2 * 2));\n        llSay(0, (string)(1 << 40));\n        llSay(0, (string)(-7 % 3));\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        UNSCOPED_INFO("wrapped: " + r.text);
        REQUIRE(r.text.find("llSay(0, \"-2147483648\");") != std::string::npos);
        UNSCOPED_INFO("zero stays");
        REQUIRE(r.text.find("(string)(1 / 0)") != std::string::npos);
        // On Mono, each negative number as its cast (ALLSLCosts::castForNegative).
        UNSCOPED_INFO("the overflow stays: " + r.text);
        REQUIRE(r.text.find("(string)(((integer)-2147483648) / ((integer)-1))") != std::string::npos);
        UNSCOPED_INFO("integer division");
        REQUIRE(r.text.find("llSay(0, \"6\");") != std::string::npos);
        UNSCOPED_INFO("a wide shift stays");
        REQUIRE(r.text.find("(string)(1 << 40)") != std::string::npos);
        UNSCOPED_INFO("modulo keeps the sign");
        REQUIRE(r.text.find("llSay(0, \"-1\");") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: pure library calls with constant arguments are evaluated and others left", "[allsloptimizer]")
    {
        const std::string source = wrap("", "        integer a = llAbs(-3);\n        float b = llSqrt(16);\n        string c = llToUpper(\"abc\");\n        string d = llGetSubString(\"abcdef\", 4, 1);\n        string e = llDeleteSubString(\"abcdef\", 1, 3);\n        integer f = llStringLength(\"hello\");\n        string g = llEscapeURL(\"a b\");\n        string h = llBase64ToString(llStringToBase64(\"hi\"));\n        integer i = llList2Integer([1, 2, 3], -1);\n        string j = llList2String([1, 2.5, <1,2,3>], 1);\n        list k = llList2List([1, 2, 3, 4], 1, 2);\n        integer l = llFloor(2.7) + llCeil(2.2);\n        string m = llGetSubString(\"h\xc3\xa9llo\", 0, 1);\n        integer n = llGetUnixTime();\n        string o = llList2CSV([1, \"a\", 2.0]);\n        if (llFrand(1) < 0.5)\n        {\n            a = f = i = l = n = 0; b = 0; c = d = e = g = h = j = m = o = \"\"; k = [];\n        }\n        llSay(0, (string)a + (string)b + c + d + e + (string)f + g + h + (string)i + j + llList2CSV(k) + (string)l + m + (string)n + o);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        UNSCOPED_INFO("abs: " + r.text);
        REQUIRE(r.text.find("integer a = 3;") != std::string::npos);
        UNSCOPED_INFO("sqrt");
        REQUIRE(r.text.find("float b = 4;") != std::string::npos);
        UNSCOPED_INFO("upper");
        REQUIRE(r.text.find("string c = \"ABC\";") != std::string::npos);
        UNSCOPED_INFO("wrapped range");
        REQUIRE(r.text.find("string d = \"abef\";") != std::string::npos);
        UNSCOPED_INFO("delete");
        REQUIRE(r.text.find("string e = \"aef\";") != std::string::npos);
        UNSCOPED_INFO("length");
        REQUIRE(r.text.find("integer f = 5;") != std::string::npos);
        UNSCOPED_INFO("escape");
        REQUIRE(r.text.find("string g = \"a%20b\";") != std::string::npos);
        UNSCOPED_INFO("base64 there and back");
        REQUIRE(r.text.find("string h = \"hi\";") != std::string::npos);
        UNSCOPED_INFO("list get from the end");
        REQUIRE(r.text.find("integer i = 3;") != std::string::npos);
        UNSCOPED_INFO("list to string");
        REQUIRE(r.text.find("string j = \"2.500000\";") != std::string::npos);
        UNSCOPED_INFO("list slice: " + r.text);
        REQUIRE(r.text.find("list k = [2, 3];") != std::string::npos);
        UNSCOPED_INFO("floor and ceil");
        REQUIRE(r.text.find("integer l = 5;") != std::string::npos);
        UNSCOPED_INFO("not ascii, not folded");
        REQUIRE(r.text.find("llGetSubString(\"h\xc3\xa9llo\", 0, 1)") != std::string::npos);
        UNSCOPED_INFO("impure, not folded");
        REQUIRE(r.text.find("llGetUnixTime()") != std::string::npos);
        UNSCOPED_INFO("csv");
        REQUIRE(r.text.find("string o = \"1, a, 2.000000\";") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: algebraic identities and redundant casts and conditions simplify", "[allsloptimizer]")
    {
        const std::string source = wrap("integer g;\ninteger h;\n",
                                        "        integer a = g + 0;\n        integer b = g * 1;\n        integer c = g - -1;\n        integer d = g + -2;\n        integer e = (integer)g;\n        integer f = g * 0;\n        integer i = (g == 0);\n        if (g != 0) h = 1;\n        if (!!g) h = 2;\n        if (!(g == h)) h = 3;\n        if (!g) h = 4; else h = 5;\n        h = a + b + c + d + e + f + i;\n        a = b = c = d = e = f = i = 0;\n        g = h;\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        UNSCOPED_INFO("plus zero: " + r.text);
        REQUIRE(r.text.find("integer a = g;") != std::string::npos);
        UNSCOPED_INFO("times one");
        REQUIRE(r.text.find("integer b = g;") != std::string::npos);
        // One more as -~g on Mono, whose ldc.i4 1 is five bytes
        // (ALLSLCosts::negateComplementForIncrement).
        UNSCOPED_INFO("minus minus: " + r.text);
        REQUIRE(r.text.find("integer c = -~g;") != std::string::npos);
        // And on Mono, two less as ~-~-g (ALLSLCosts::complementNegateForDecrement).
        UNSCOPED_INFO("plus minus: " + r.text);
        REQUIRE(r.text.find("integer d = ~-~-g;") != std::string::npos);
        UNSCOPED_INFO("cast");
        REQUIRE(r.text.find("integer e = g;") != std::string::npos);
        // Left at its default on Mono (ALLSLCosts::dropIntegerDefault).
        UNSCOPED_INFO("times zero: " + r.text);
        REQUIRE(r.text.find("integer f;") != std::string::npos);
        UNSCOPED_INFO("equals zero");
        REQUIRE(r.text.find("integer i = (!g);") != std::string::npos);
        UNSCOPED_INFO("not equal zero as condition");
        REQUIRE(r.text.find("if (g)\n            h = 1;") != std::string::npos);
        UNSCOPED_INFO("double not");
        REQUIRE(r.text.find("if (g)\n            h = 2;") != std::string::npos);
        // As a ^ on Mono, where only truth counts (ALLSLCosts::xorForNotEqual).
        UNSCOPED_INFO("not equals: " + r.text);
        REQUIRE(r.text.find("if (g ^ h)\n            h = 3;") != std::string::npos);
        // Turned round off its !, then a choice of two numbers as arithmetic.
        UNSCOPED_INFO("swapped, then chosen by arithmetic: " + r.text);
        REQUIRE(r.text.find("h = !!g + 4;") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: unreachable code and unused labels and functions and states go", "[allsloptimizer]")
    {
        const std::string source = "f()\n{\n    return;\n    llSay(0, \"never\");\n}\ng()\n{\n}\n"
                                   "default\n{\n    state_entry()\n    {\n        if (0) llSay(0, \"no\");\n        if (1) llSay(0, \"yes\"); else llSay(0, \"no\");\n        while (0) llSay(0, \"no\");\n        @unused;\n        f();\n        ;\n        {}\n        3;\n    }\n}\nstate other\n{\n    state_entry()\n    {\n    }\n}\n";
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        CHECK(r.text == "f()\n{\n    return;\n}\n\ndefault\n{\n    state_entry()\n    {\n        llSay(0, \"yes\");\n        f();\n    }\n}\n");
        UNSCOPED_INFO("noted the return");
        CHECK(has(r, "can never run"));
        CHECK(has(r, "removed the unused function g"));
        CHECK(has(r, "removed the state other"));
        UNSCOPED_INFO("noted the label");
        CHECK(has(r, "nothing jumps to"));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: llGetListLength as a comparison and a list literal as a sum where smaller", "[allsloptimizer]")
    {
        const std::string source = wrap("list l;\n", "        integer n = llGetListLength(l) + 1;\n        if (llGetListLength(l)) l = [1, \"a\"];\n        l = [];\n        llOwnerSay((string)n);\n");
        // On LSO both are smaller.
        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        ALLSLOptimizer::Result r    = ALLSLOptimizer::run(source, lso);
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        // One more as -~x on LSO (ALLSLCosts::negateComplementForIncrement).
        UNSCOPED_INFO("in a sum: " + r.text);
        REQUIRE(r.text.find("integer n = -~(l != []);") != std::string::npos);
        UNSCOPED_INFO("as a condition");
        REQUIRE(r.text.find("if (l != [])") != std::string::npos);
        UNSCOPED_INFO("list add: " + r.text);
        REQUIRE(r.text.find("l = (list)1 + \"a\";") != std::string::npos);
        UNSCOPED_INFO("an empty list stays");
        REQUIRE(r.text.find("l = [];") != std::string::npos);

        // On Mono the comparison is not, though one more is -~x there too;
        // nor is the sum here, one sum not paying for the helper it would
        // be the script's only use of.
        r = ALLSLOptimizer::run(source, options());
        {
            INFO("no comparison on Mono: " + r.text);
            REQUIRE(r.text.find("integer n = -~llGetListLength(l);") != std::string::npos);
            REQUIRE(r.text.find("l != []") == std::string::npos);
        }
        UNSCOPED_INFO("one sum not worth its helper on Mono: " + r.text);
        REQUIRE(r.text.find("l = [1, \"a\"];") != std::string::npos);

        // On Luau neither.
        ALLSLOptimizer::Options luau = options();
        luau.target                  = ALLSLOptimizer::Target::Luau;
        r                            = ALLSLOptimizer::run(source, luau);
        {
            INFO("neither on Luau: " + r.text);
            REQUIRE(r.text.find("l = [1, \"a\"];") != std::string::npos);
            REQUIRE(r.text.find("l != []") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: names shrink to the shortest free ones and the map says which", "[allsloptimizer]")
    {
        const std::string source = "integer counter;\nbump(integer by)\n{\n    counter += by;\n}\ndefault\n{\n    touch_start(integer total)\n    {\n        bump(total);\n        bump(1);\n        llSay(0, (string)counter);\n    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.shrinknames             = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        CHECK(r.optimized);
        REQUIRE(r.renamed.count("counter") == 1);
        REQUIRE(r.renamed.count("bump") == 1);
        REQUIRE(r.renamed.count("total") == 1);
        UNSCOPED_INFO("the event stays");
        REQUIRE(r.text.find("touch_start(integer ") != std::string::npos);
        UNSCOPED_INFO("default stays");
        REQUIRE(r.text.find("default\n{") != std::string::npos);
        {
            INFO("single letters");
            REQUIRE(r.renamed["bump"].size() == 1);
            REQUIRE(r.renamed["counter"].size() == 1);
        }
        {
            INFO("nothing of the old names");
            REQUIRE(r.text.find("counter") == std::string::npos);
            REQUIRE(r.text.find("bump") == std::string::npos);
        }
        CHECK(has(r, "renamed the function bump to"));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a script with errors is left alone and the map leads back", "[allsloptimizer]")
    {
        ALLSLOptimizer::Result r = ALLSLOptimizer::run("default { state_entry() { integer x = ; } }", options());
        CHECK_FALSE(r.optimized);
        REQUIRE(r.problems.size() == 1);
        CHECK(r.problems[0].severity == ALScriptProblem::Severity::Error);
        UNSCOPED_INFO("the source as it was");
        CHECK(r.text == "default { state_entry() { integer x = ; } }");

        const std::string source = "integer g = 3;\n\n\ndefault\n{\n    state_entry()\n    {\n        llSay(0, (string)g);\n    }\n}\n";
        r                        = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        CHECK(r.text == "default\n{\n    state_entry()\n    {\n        llSay(0, \"3\");\n    }\n}\n");
        // The llSay is on output line 4 and came from source line 7.
        const ALSourceMap::Loc loc = r.map.toSource(4, 8);
        REQUIRE(loc.found());
        CHECK(loc.line == 7);
        CHECK(loc.column == 8);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: every golden file comes out as its expected text", "[allsloptimizer]")
    {
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
            CAPTURE(name);
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
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            if (const char* write = std::getenv("AL_OPTIMIZER_WRITE_EXPECTED"); write && *write)
            {
                llofstream out(expected, std::ios::binary);
                out << r.text;
                continue;
            }
            REQUIRE(fs::exists(expected));
            REQUIRE(r.text == read(expected));
            ++checked;
        }
        UNSCOPED_INFO("some golden files");
        REQUIRE((checked > 0 || std::getenv("AL_OPTIMIZER_WRITE_EXPECTED")));
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: rotation functions fold over unit rotations and leave the rest", "[allsloptimizer]")
    {
        const std::string source = wrap("", "        rotation a = llEuler2Rot(<0, 0, PI_BY_TWO>);\n        vector b = llRot2Euler(<0, 0, 0.7071068, 0.7071068>);\n        rotation c = llAxisAngle2Rot(<0, 0, 2>, PI);\n        vector d = llRot2Axis(<0, 0, 0.7071068, 0.7071068>);\n        float e = llRot2Angle(<0, 0, 0.7071068, 0.7071068>);\n        vector f = llRot2Fwd(<0, 0, 0.7071068, 0.7071068>);\n        vector h = llRot2Up(<0.7071068, 0, 0, 0.7071068>);\n        vector i = llRot2Axis(<0, 0, 0, 1>);\n        vector j = llRot2Fwd(<0, 0, 2, 2>);\n        if (llFrand(1) < 0.5)\n        {\n            a = c = ZERO_ROTATION; b = d = f = h = i = j = ZERO_VECTOR; e = 0;\n        }\n        llSay(0, (string)a + (string)b + (string)c + (string)d + (string)e + (string)f + (string)h + (string)i + (string)j);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        // A quarter turn about z: x and y zero, z and s a half root two.
        UNSCOPED_INFO("euler to rot: " + r.text);
        CHECK(about(numbers(r.text, "rotation a"), { 0.f, 0.f, 0.70710677f, 0.70710677f }));
        UNSCOPED_INFO("rot to euler: " + r.text);
        CHECK(about(numbers(r.text, "vector b"), { 0.f, 0.f, 1.5707964f }));
        {
            INFO("axis and angle, the axis normalised: " + r.text);
            CHECK(about(numbers(r.text, "rotation c"), { 0.f, 0.f, 1.f, 0.f }));
            CHECK_FALSE(has("llAxisAngle2Rot"));
        }
        UNSCOPED_INFO("the axis back: " + r.text);
        CHECK(about(numbers(r.text, "vector d"), { 0.f, 0.f, 1.f }));
        UNSCOPED_INFO("the angle back: " + r.text);
        CHECK(about(numbers(r.text, "float e"), { 1.5707964f }));
        {
            INFO("fwd of a quarter turn is left: " + r.text);
            CHECK_FALSE(has("llRot2Fwd(<0, 0, 0.7071068, 0.7071068>)"));
            CHECK(has("vector f = <"));
        }
        {
            INFO("up of a roll: " + r.text);
            CHECK_FALSE(has("llRot2Up("));
            CHECK(has("vector h = <0, ((float)-"));
        }
        UNSCOPED_INFO("no rotation has no axis: left");
        CHECK(has("llRot2Axis(<0, 0, 0, 1>)"));
        UNSCOPED_INFO("not a unit rotation: left");
        CHECK(has("llRot2Fwd(<0, 0, 2, 2>)"));
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: the JSON functions fold strict plain JSON and leave the rest to the simulator", "[allsloptimizer]")
    {
        const std::string source = wrap("", "        string a = llJsonGetValue(\"{\\\"name\\\": \\\"Ann\\\", \\\"tags\\\": [\\\"x\\\", 7, {\\\"k\\\": \\\"v\\\"}]}\", [\"name\"]);\n        string b = llJsonGetValue(\"{\\\"tags\\\": [\\\"x\\\", -7]}\", [\"tags\", 1]);\n        string c = llJsonGetValue(\"{\\\"tags\\\": [\\\"x\\\", 7, {\\\"k\\\": \\\"v\\\"}]}\", [\"tags\", 2, \"k\"]);\n        string d = llJsonGetValue(\"[1.5]\", [0]);\n        string e = llJsonGetValue(\"[true]\", [0]);\n        string f = llJsonGetValue(\"[1]\", [3]);\n        string g = llJsonGetValue(\"{\\\"a\\\": 1, \\\"a\\\": 2}\", [\"a\"]);\n        string h = llJsonGetValue(\"[1,]\", [0]);\n        string i = llJsonGetValue(\"[\\\"a\\\\\\\"b\\\"]\", [0]);\n        if (llFrand(1) < 0.5)\n        {\n            a = b = c = d = e = f = g = h = i = \"\";\n        }\n        llSay(0, a + b + c + d + e + f + g + h + i);\n");
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        CHECK(r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        UNSCOPED_INFO("a key: " + r.text);
        CHECK(has("string a = \"Ann\";"));
        UNSCOPED_INFO("an index, a negative integer: " + r.text);
        CHECK(has("string b = \"-7\";"));
        UNSCOPED_INFO("down a path: " + r.text);
        CHECK(has("string c = \"v\";"));
        UNSCOPED_INFO("a fraction is left: " + r.text);
        CHECK(has("string d = llJsonGetValue"));
        UNSCOPED_INFO("true is its constant: " + r.text);
        CHECK(has("string e = JSON_TRUE;"));
        UNSCOPED_INFO("out of range is JSON_INVALID: " + r.text);
        CHECK(has("string f = JSON_INVALID;"));
        UNSCOPED_INFO("duplicate keys are left");
        CHECK(has("string g = llJsonGetValue"));
        UNSCOPED_INFO("a trailing comma is left");
        CHECK(has("string h = llJsonGetValue"));
        UNSCOPED_INFO("an escape unescaped: " + r.text);
        CHECK(has("string i = \"a\\\"b\";"));
        // The answers that are constants are the constants' names.
        const std::string more = wrap("", "        string a = llJsonGetValue(\"[true, null]\", [0]);\n        string b = llJsonGetValue(\"[1]\", [3]);\n        string c = llJsonValueType(\"{\\\"a\\\": [1, {}]}\", [\"a\", 1]);\n        string d = llJsonValueType(\"{\\\"a\\\": 1}\", [\"b\"]);\n        string e = llJsonValueType(\"[1,]\", []);\n        string f = llList2Json(JSON_OBJECT, [\"name\", \"Ann Lee\", \"count\", 3]);\n        string g = llList2Json(JSON_ARRAY, [1, \"true\"]);\n        string h = llList2Json(JSON_ARRAY, [1.5]);\n        list i = llJson2List(\"[1, \\\"two\\\", -3]\");\n        list j = llJson2List(\"{\\\"k\\\": \\\"v\\\"}\");\n        list k = llJson2List(\"[true]\");\n        if (llFrand(1) < 0.5)\n        {\n            a = b = c = d = e = f = g = h = \"\"; i = j = k = [];\n        }\n        llSay(0, a + b + c + d + e + f + g + h + llList2CSV(i) + llList2CSV(j) + llList2CSV(k));\n");
        ALLSLOptimizer::Result m = ALLSLOptimizer::run(more, options());
        CHECK(m.optimized);
        auto hasm = [&m](const char* text) { return m.text.find(text) != std::string::npos; };
        UNSCOPED_INFO("true is JSON_TRUE: " + m.text);
        CHECK(hasm("string a = JSON_TRUE;"));
        UNSCOPED_INFO("a miss is JSON_INVALID: " + m.text);
        CHECK(hasm("string b = JSON_INVALID;"));
        UNSCOPED_INFO("the type of a nested object: " + m.text);
        CHECK(hasm("string c = JSON_OBJECT;"));
        UNSCOPED_INFO("the type of nothing: " + m.text);
        CHECK(hasm("string d = JSON_INVALID;"));
        UNSCOPED_INFO("malformed is left to the simulator: " + m.text);
        CHECK(hasm("string e = llJsonValueType"));
        UNSCOPED_INFO("an object from plain strings and integers: " + m.text);
        CHECK(hasm("string f = \"{\\\"name\\\":\\\"Ann Lee\\\",\\\"count\\\":3}\";"));
        UNSCOPED_INFO("a string that reads as a JSON word is left: " + m.text);
        CHECK(hasm("string g = llList2Json"));
        UNSCOPED_INFO("a float is left: " + m.text);
        CHECK(hasm("string h = llList2Json"));
        UNSCOPED_INFO("an array to a list: " + m.text);
        CHECK(hasm("list i = [1, \"two\", ((integer)-3)];"));
        UNSCOPED_INFO("an object to a list: " + m.text);
        CHECK(hasm("list j = [\"k\", \"v\"];"));
        UNSCOPED_INFO("a true in it is left: " + m.text);
        CHECK(hasm("list k = llJson2List"));
        // A nested value comes out written the compact way; a set puts a
        // value in, at a key or an index, where the document is plain.
        const std::string sets = wrap("", "        string a = llJsonGetValue(\"{\\\"a\\\": [1, {\\\"b\\\": true}, \\\"c\\\"]}\", [\"a\"]);\n        string b = llJsonSetValue(\"{\\\"a\\\": 1}\", [\"b\"], \"two\");\n        string c = llJsonSetValue(\"[1, 2]\", [1], \"9\");\n        string d = llJsonSetValue(\"[1, 2]\", [JSON_APPEND], JSON_TRUE);\n        string e = llJsonSetValue(\"[1, 2]\", [5], \"9\");\n        string f = llJsonSetValue(\"{}\", [\"a\", \"b\"], \"x\");\n        string g = llJsonSetValue(\"[1]\", [0], \"1.5\");\n        string h = llJsonGetValue(\"[1.5]\", []);\n        if (llFrand(1) < 0.5)\n        {\n            a = b = c = d = e = f = g = h = \"\";\n        }\n        llSay(0, a + b + c + d + e + f + g + h);\n");
        ALLSLOptimizer::Result n = ALLSLOptimizer::run(sets, options());
        CHECK(n.optimized);
        auto hasn = [&n](const char* text) { return n.text.find(text) != std::string::npos; };
        UNSCOPED_INFO("a nested value, compact: " + n.text);
        CHECK(hasn("string a = \"[1,{\\\"b\\\":true},\\\"c\\\"]\";"));
        UNSCOPED_INFO("a new key at the end: " + n.text);
        CHECK(hasn("string b = \"{\\\"a\\\":1,\\\"b\\\":\\\"two\\\"}\";"));
        UNSCOPED_INFO("an index replaced with a number: " + n.text);
        CHECK(hasn("string c = \"[1,9]\";"));
        UNSCOPED_INFO("appended by JSON_APPEND with a constant: " + n.text);
        CHECK(hasn("string d = \"[1,2,true]\";"));
        UNSCOPED_INFO("past the end is JSON_INVALID: " + n.text);
        CHECK(hasn("string e = JSON_INVALID;"));
        UNSCOPED_INFO("a path to create is left: " + n.text);
        CHECK(hasn("string f = llJsonSetValue"));
        UNSCOPED_INFO("a value that is a fraction is left: " + n.text);
        CHECK(hasn("string g = llJsonSetValue"));
        UNSCOPED_INFO("a document with a fraction is left: " + n.text);
        CHECK(hasn("string h = llJsonGetValue"));
        const std::string dels = wrap("", "        string a = llJsonSetValue(\"{\\\"a\\\": 1, \\\"b\\\": 2}\", [\"a\"], JSON_DELETE);\n        string b = llJsonSetValue(\"[1, 2, 3]\", [1], JSON_DELETE);\n        string c = llJsonSetValue(\"[1, 2, 3]\", [7], JSON_DELETE);\n        if (llFrand(1) < 0.5)\n        {\n            a = b = c = \"\";\n        }\n        llSay(0, a + b + c);\n");
        ALLSLOptimizer::Result q = ALLSLOptimizer::run(dels, options());
        auto hasq = [&q](const char* text) { return q.text.find(text) != std::string::npos; };
        UNSCOPED_INFO("a key deleted: " + q.text);
        CHECK(hasq("string a = \"{\\\"b\\\":2}\";"));
        UNSCOPED_INFO("an index deleted: " + q.text);
        CHECK(hasq("string b = \"[1,3]\";"));
        UNSCOPED_INFO("deleting what is not there is the simulator's: " + q.text);
        CHECK(hasq("string c = llJsonSetValue"));
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a function called once goes in as a block or as its returned expression", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        // The inliner's own text is what the optimizer read.
        const ALLSLInliner::Result put = ALLSLInliner::run(source);
        CHECK(put.inlined == 5);
        UNSCOPED_INFO("the block where the call was: " + put.text);
        REQUIRE(put.text.find("        {\nstring what = \"hi\";\ninteger n = i;\n\n    integer i_1 = n * 2;\n    llSay(0, what + (string)i_1);\n\n}\n") != std::string::npos);
        UNSCOPED_INFO("the expression where the other call was, the temporaries before the statement: " + put.text);
        REQUIRE(put.text.find("        integer _t_1 = g;\n        integer _t_2 = i;\n        g = (i * 2 + g) + (_t_1 + _t_1 + _t_1) + (_t_2 + _t_2 + _t_2);") != std::string::npos);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        {
            INFO("say went into a block, its i renamed past the caller's: " + r.text);
            CHECK(has("integer i_1 = n * 2;"));
            CHECK(has("llSay(0, what + (string)i_1);"));
        }
        {
            INFO("the parameters became locals set to the arguments: " + r.text);
            CHECK(has("string what = \"hi\";"));
            CHECK(has("integer n = i;"));
        }
        UNSCOPED_INFO("say is gone: " + r.text);
        CHECK_FALSE(has("say(string what"));
        UNSCOPED_INFO("twice became its expression with the argument in: " + r.text);
        CHECK(has("(i * 2 + g)"));
        {
            INFO("thrice reads its parameter thrice, so each name is read once into a temporary before the statement: " + r.text);
            CHECK(has("integer _t_1 = g;"));
            CHECK(has("integer _t_2 = i;"));
            CHECK(has("(_t_1 + _t_1 + _t_1)"));
            CHECK(has("(_t_2 + _t_2 + _t_2)"));
            CHECK_FALSE(has("integer thrice(integer x)"));
        }
        {
            INFO("loop went in with its label and jump given a fresh name: " + r.text);
            CHECK_FALSE(has("loop();"));
            CHECK_FALSE(has("loop()\n"));
            CHECK(has("@again_1;"));
            CHECK(has("jump again_1;"));
        }
        S32 said = 0;
        for (const ALScriptProblem& p : r.problems)
        {
            said += p.severity == ALScriptProblem::Severity::Note && p.message.find("in place of") != std::string::npos;
        }
        UNSCOPED_INFO("five notes: say, twice, loop, and thrice twice");
        CHECK(said == 5);
        // The map leads from the block back to the function's own lines.
        const size_t at = r.text.find("llSay(0, what + (string)i_1);");
        REQUIRE(at != std::string::npos);
        S32 line = 0;
        for (size_t i = 0; i < at; ++i)
        {
            line += r.text[i] == '\n';
        }
        const ALSourceMap::Loc from = r.map.toSource(line, 8);
        UNSCOPED_INFO("the llSay came from the function's fifth line");
        CHECK(from.line == 4);
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
        UNSCOPED_INFO("a note on the body's code is at the function's line: " + notes(r));
        CHECK(folded_in_body);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a small expression function goes everywhere and a marked one at any size", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("sq went into both places, once each round: " + put.text);
        REQUIRE(put.text.find("integer a = (2 * 2) + (3 * 3);") != std::string::npos);
        UNSCOPED_INFO("and is gone");
        REQUIRE(put.text.find("integer sq(") == std::string::npos);
        {
            INFO("shout, called twice and unmarked, stays: " + put.text);
            REQUIRE(put.text.find("shout(\"x\");") != std::string::npos);
            REQUIRE(put.text.find("shout(string what)") != std::string::npos);
        }
        UNSCOPED_INFO("two rounds");
        CHECK(put.inlined == 2);
        put = ALLSLInliner::run(source, { "shout" });
        {
            INFO("marked, shout goes into both places: " + put.text);
            REQUIRE(put.text.find("string what = \"x\";") != std::string::npos);
            REQUIRE(put.text.find("string what = \"y\";") != std::string::npos);
            REQUIRE(put.text.find("shout(string what)") == std::string::npos);
        }
        UNSCOPED_INFO("four rounds");
        CHECK(put.inlined == 4);
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        {
            INFO("the optimizer reads the result: " + notes(r));
            CHECK(r.optimized);
            REQUIRE(r.text.find("(2 * 2) + (3 * 3)") != std::string::npos);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: returns become jumps to an end label and a round skips overlapping calls", "[allsloptimizer]")
    {
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
        {
            INFO("the returns are jumps: " + put.text);
            CHECK(has("if (n < 0) jump _ret_1;"));
            CHECK(has("        jump _ret_1;"));
        }
        UNSCOPED_INFO("to a label that ends the block: " + put.text);
        CHECK((has("@_ret_1;\n}") || has("@_ret_1;\n        }")));
        const ALLSLInliner::Result trailing = ALLSLInliner::run("f()\n{\n    llSay(0, \"x\");\n    return;\n}\ndefault\n{\n    state_entry()\n    {\n        f();\n    }\n}\n");
        {
            INFO("a return that ends the body is just dropped: " + trailing.text);
            REQUIRE(trailing.text.find("jump") == std::string::npos);
            REQUIRE(trailing.text.find("@_ret") == std::string::npos);
            REQUIRE(trailing.text.find("llSay(0, \"x\");") != std::string::npos);
        }
        UNSCOPED_INFO("check is gone");
        CHECK_FALSE(has("check(integer n)"));
        // outer's one call goes in the first round; inner's call is inside
        // outer, whose lines go that round, so inner waits for the next.
        {
            INFO("inner went in the end: " + put.text);
            CHECK(has("integer k = 7;"));
            CHECK_FALSE(has("inner(integer k)"));
            CHECK_FALSE(has("outer()\n"));
        }
        CHECK(put.inlined == 3);
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("the optimizer reads it: " + notes(r));
        CHECK(r.optimized);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: pure arguments go in directly or via a temporary and others force a block", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("a pure call read twice is made once into a temporary: " + put.text);
        CHECK(has("        integer _t_1 = llAbs(g);\n        integer a = (_t_1 + _t_1);"));
        UNSCOPED_INFO("an expression read once goes in, in parentheses: " + put.text);
        CHECK(has("integer b = ((g * 2) + 1);"));
        UNSCOPED_INFO("so does a pure call: " + put.text);
        CHECK(has("integer c = ((llAbs(g)) + 1);"));
        // An argument that may change something, or whose answer may
        // change, cannot go into an expression; a function called once
        // goes from a block before the statement instead, its value set by
        // the body.
        {
            INFO("bump, called once, is set by its body before the statement, and once takes its value as a name: " + put.text);
            CHECK(has("integer _r_1;"));
            CHECK(has("_r_1 = ++g;"));
            CHECK(has("integer d = (_r_1 + 1);"));
            CHECK_FALSE(has("integer once(integer x)"));
        }
        {
            INFO("both, its other call gone, sets e itself, the time read once into its parameter: " + put.text);
            CHECK(has("integer e;"));
            CHECK(has("integer x = llRound(llGetTime());"));
            CHECK(has("e = x + x;"));
            CHECK_FALSE(has("integer both(integer x)"));
        }
        UNSCOPED_INFO("no stray ; where the declaration stood: " + put.text);
        CHECK_FALSE(has("};"));
        CHECK(put.inlined == 6);
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
        UNSCOPED_INFO("above the if: " + above.text);
        CHECK(got("        integer _t_1 = llAbs(g);\n        if ((_t_1 + _t_1) > 3)"));
        UNSCOPED_INFO("above the for, an assignment after the call's part not minded: " + above.text);
        CHECK(got("        integer _t_2 = llAbs(g);\n        for (i = (_t_2 + _t_2), g = 0; i < 9; ++i) g++;"));
        UNSCOPED_INFO("the while keeps its call: " + above.text);
        CHECK(got("while (both(llAbs(g)) < 9) g++;"));
        UNSCOPED_INFO("so does the else-if: " + above.text);
        CHECK(got("else if (both(llAbs(g))) g = 2;"));
        CHECK(above.inlined == 2);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: recursive functions stay and a label after the call is the return's target", "[allsloptimizer]")
    {
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
        {
            INFO("ping and pong, each reaching the other, stay: " + put.text);
            CHECK(has("ping(integer n)"));
            CHECK(has("pong(integer n)"));
            CHECK(has("        ping(3);"));
        }
        {
            INFO("self stays: " + put.text);
            CHECK(has("self(integer n)"));
            CHECK(has("        self(3);"));
        }
        {
            INFO("early went in, its return a jump to the label already there: " + put.text);
            CHECK(has("if (n < 0) jump next;"));
            CHECK_FALSE(has("_ret"));
            CHECK(has("            @next;"));
        }
        CHECK(put.inlined == 1);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a run without notes says nothing and one out of budget stops where it is", "[allsloptimizer]")
    {
        const std::string       source = wrap("", "        integer a = 1 + 2;\n        integer b = 3 * 4;\n        llSay(0, (string)(a + b));\n");
        ALLSLOptimizer::Options o      = options();
        ALLSLOptimizer::Result  said   = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("the folds are said");
        REQUIRE_FALSE(said.problems.empty());
        o.notes = false;
        ALLSLOptimizer::Result quiet = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("nothing said");
        REQUIRE(quiet.problems.empty());
        UNSCOPED_INFO("and the same text either way");
        CHECK(quiet.text == said.text);
        // A budget too small for a round stops before the first, and
        // says so where the notes are read.
        o.notes       = true;
        o.visitBudget = 1;
        ALLSLOptimizer::Result stopped = ALLSLOptimizer::run(source, o);
        CHECK(stopped.stoppedEarly);
        {
            INFO("and said so: " + notes(stopped));
            REQUIRE_FALSE(stopped.problems.empty());
            CHECK(stopped.problems.back().key == std::string("OptimizerStoppedEarly"));
        }
        UNSCOPED_INFO("what it has stands");
        REQUIRE_FALSE(stopped.text.empty());
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: adding to a list never drops a zero or empty string or float element", "[allsloptimizer]")
    {
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
        CHECK(r.optimized);
        UNSCOPED_INFO("a zero appended stays: " + r.text);
        REQUIRE(r.text.find("l = l + 0;") != std::string::npos);
        UNSCOPED_INFO("an empty string appended stays: " + r.text);
        REQUIRE(r.text.find("l = l + \"\";") != std::string::npos);
        UNSCOPED_INFO("a zero put in front stays: " + r.text);
        REQUIRE(r.text.find("l = ((float)0) + l;") != std::string::npos);
        UNSCOPED_INFO("a negative appended is not turned into a subtraction: " + r.text);
        REQUIRE(r.text.find("l - 1") == std::string::npos);
        UNSCOPED_INFO("an empty list adds nothing: " + r.text);
        REQUIRE(r.text.find("l + [];") == std::string::npos);
        UNSCOPED_INFO("nor an empty string to a string: " + r.text);
        REQUIRE(r.text.find("llList2String(l, 0) + \"\"") == std::string::npos);
        // The count is read once, and goes where it is read.
        {
            INFO("nor a zero to an integer: " + r.text);
            REQUIRE(r.text.find("llGetListLength(l) + 0") == std::string::npos);
            REQUIRE(r.text.find("l != [] + 0") == std::string::npos);
            REQUIRE(r.text.find("integer n") == std::string::npos);
        }
        // The literal's elements, however it is written now, all there:
        // the texture's face and the glow.
        const size_t call = r.text.find("llSetPrimitiveParams(");
        REQUIRE(call != std::string::npos);
        const std::string args = r.text.substr(call, r.text.find(';', call) - call);
        UNSCOPED_INFO("the face, a zero: " + args);
        REQUIRE((args.find("PRIM_TEXTURE + 0") != std::string::npos || args.find("PRIM_TEXTURE, 0") != std::string::npos));
        UNSCOPED_INFO("and the last element, a float: " + args);
        REQUIRE((args.find("ZERO_VECTOR + ((float)0)") != std::string::npos ||
                                                          args.find("ZERO_VECTOR, ((float)0)") != std::string::npos));
        // And what it wrote compiles.
        ALLSLService service;
        const ALScriptProblems said = service.check(r.text);
        for (const ALScriptProblem& p : said)
        {
            UNSCOPED_INFO("no error in what was written: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }

        // The preprocessor's lazy-list helper grows its list one zero at a
        // time, and ends.
        const std::string lazy = "list lazy_list_set(list L, integer i, list v)\n{\n    while (llGetListLength(L) < i)\n        L = L + 0;\n    return llListReplaceList(L, v, i, i);\n}\n" +
                                 wrap("", "        list x;\n        x = lazy_list_set(x, 3, [1]);\n        llSay(0, llList2CSV(x));\n");
        r = ALLSLOptimizer::run(lazy, options());
        UNSCOPED_INFO("the list still grows: " + r.text);
        REQUIRE(r.text.find("L = L + 0;") != std::string::npos);
        UNSCOPED_INFO("rather than standing still");
        REQUIRE(r.text.find("L = L;") == std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: not-equal replaces a negated equals only where it means the same", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("a list's not-equal is the difference of the lengths, so !(a == b) stays: " + r.text);
        REQUIRE(r.text.find("!(a == b)") != std::string::npos);
        // Each read once, and gone where it is read.
        UNSCOPED_INFO("!(a != b) is a == b for lists too: " + r.text);
        REQUIRE(r.text.find("+ (a == b) +") != std::string::npos);
        UNSCOPED_INFO("and for integers !(i == j) is i != j: " + r.text);
        REQUIRE(r.text.find("(i != j)") != std::string::npos);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a function stays called where a caller's local shadows a global it reads", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("both events there");
        REQUIRE(touch != std::string::npos);
        const std::string entry = put.text.substr(0, touch);
        const std::string touched = put.text.substr(touch);
        {
            INFO("where count is the caller's own, the calls stay: " + entry);
            REQUIRE(entry.find("        bump();") != std::string::npos);
            REQUIRE(entry.find("plus(1)") != std::string::npos);
        }
        {
            INFO("where it is the global, bump goes in: " + touched);
            REQUIRE(touched.find("bump();") == std::string::npos);
            REQUIRE(touched.find("count = count + 1;") != std::string::npos);
        }
        UNSCOPED_INFO("and plus: " + touched);
        REQUIRE(touched.find("(n + count)") != std::string::npos);
        {
            INFO("so both functions stay, for the calls that kept them: " + put.text);
            REQUIRE(put.text.find("bump()\n{") != std::string::npos);
            REQUIRE(put.text.find("integer plus(integer x)") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: code after a jump or in a dead branch stays when it holds the jump's label", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
        UNSCOPED_INFO("the block the jump lands in stays: " + r.text);
        CHECK(has("llOwnerSay(\"second\");"));
        UNSCOPED_INFO("and what follows it: " + r.text);
        CHECK(has("llOwnerSay(\"third\");"));
        UNSCOPED_INFO("the if whose branch a jump lands in stays: " + r.text);
        CHECK(has("llOwnerSay(\"fourth\");"));
        UNSCOPED_INFO("and the loop whose body one does: " + r.text);
        CHECK(has("llOwnerSay(\"fifth\");"));
        UNSCOPED_INFO("nothing said to be unreachable: " + notes(r));
        REQUIRE(notes(r).find("can never run") == std::string::npos);

        // Where nothing jumps into it, it is still gone; and a jump from
        // inside it to its own label is no way in.
        r = ALLSLOptimizer::run(wrap("", "        return;\n        {\n            @x;\n            llOwnerSay(\"never\");\n        }\n"), options());
        UNSCOPED_INFO("a block after a return that nothing jumps into goes: " + r.text);
        REQUIRE(r.text.find("never") == std::string::npos);
        r = ALLSLOptimizer::run(wrap("", "        integer i;\n        return;\n        {\n            @again;\n            llOwnerSay(\"never\");\n            if (++i < 3) jump again;\n        }\n"),
                                options());
        UNSCOPED_INFO("nor one that only jumps to itself: " + r.text);
        REQUIRE(r.text.find("never") == std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: an inlined block evaluates its arguments left to right as a call does", "[allsloptimizer]")
    {
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
        {
            INFO("both arguments set to locals: " + put.text);
            REQUIRE(a != std::string::npos);
            REQUIRE(b != std::string::npos);
        }
        UNSCOPED_INFO("the first first: " + put.text);
        CHECK(a < b);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a true branch ending in an if gets braces so the else stays the outer if's", "[allsloptimizer]")
    {
        // if (!c) A; else if (d) B; swapped round: the inner if has no else.
        ALLSLOptimizer::Result r =
            ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else if (d) llOwnerSay(\"B\");\n        c = 1; d = 0;\n"), options());
        UNSCOPED_INFO("swapped: " + notes(r));
        CHECK(has(r, "swapped the branches"));
        UNSCOPED_INFO("the inner if in braces: " + r.text);
        REQUIRE(r.text.find("        if (c)\n        {\n            if (d)\n                llOwnerSay(\"B\");\n        }\n") != std::string::npos);
        UNSCOPED_INFO("the else the outer if's: " + r.text);
        REQUIRE(r.text.find("        }\n        else\n            llOwnerSay(\"A\");\n") != std::string::npos);
        // And one whose inner if stands in a loop.
        r = ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else while (d--) if (d == 3) llOwnerSay(\"B\");\n        c = 1;\n"), options());
        {
            INFO("the loop in braces: " + r.text);
            REQUIRE(r.text.find("        if (c)\n        {\n            while (d--)\n") != std::string::npos);
            REQUIRE(r.text.find("        }\n        else\n            llOwnerSay(\"A\");\n") != std::string::npos);
        }
        // Where the inner if has an else of its own, nothing is added.
        r = ALLSLOptimizer::run(wrap("integer c;\ninteger d = 1;\n", "        if (!c) llOwnerSay(\"A\"); else if (d) llOwnerSay(\"B\"); else llOwnerSay(\"C\");\n        c = 1; d = 0;\n"), options());
        UNSCOPED_INFO("no braces an else chain does not need: " + r.text);
        CHECK(r.text.find('{', r.text.find("if (c)")) > r.text.find("llOwnerSay(\"A\")"));
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: an inlined call as a brace-less loop body or branch keeps its return inside", "[allsloptimizer]")
    {
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
        {
            INFO("went in: " + put.text);
            CHECK(put.inlined == 1);
            REQUIRE(label != std::string::npos);
            REQUIRE(put.text.find("jump _ret_1;") != std::string::npos);
        }
        {
            INFO("the label ends the loop's body, before anything after the loop: " + put.text);
            REQUIRE(close != std::string::npos);
            CHECK(close < put.text.find("llOwnerSay(\"done\");"));
        }
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(fn + wrap("", "        integer i;\n        for (i = 0; i < 5; ++i) skip(i);\n        llOwnerSay(\"done\");\n"), o);
        UNSCOPED_INFO("the optimizer reads it: " + notes(r) + r.text);
        CHECK(r.optimized);
        // Between an if and its else, a label after the block was a
        // syntax error the user never wrote.
        put = ALLSLInliner::run(fn + wrap("integer g;\n", "        if (g) skip(g); else llOwnerSay(\"none\");\n"));
        UNSCOPED_INFO("went in: " + put.text);
        CHECK(put.inlined == 1);
        r = ALLSLOptimizer::run(fn + wrap("integer g;\n", "        if (g) skip(g); else llOwnerSay(\"none\");\n        g = llGetUnixTime();\n"), o);
        {
            INFO("the else still follows its if: " + notes(r) + r.text);
            CHECK(r.optimized);
            REQUIRE(r.text.find("else") != std::string::npos);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: the engine counts a clash from another thread but not a nested hold", "[allsloptimizer]")
    {
        const unsigned before = al_script_engine::clashes().load();
        {
            AL_SCRIPT_ENGINE_HELD;
            {
                // A hold within a hold is the same thread's.
                AL_SCRIPT_ENGINE_HELD;
            }
            UNSCOPED_INFO("none from within");
            CHECK(al_script_engine::clashes().load() == before);
            std::thread other([]() { AL_SCRIPT_ENGINE_HELD; });
            other.join();
            UNSCOPED_INFO("another thread, while this one is in: said");
            CHECK(al_script_engine::clashes().load() == before + 1);
        }
        std::thread after([]() { AL_SCRIPT_ENGINE_HELD; });
        after.join();
        UNSCOPED_INFO("another, once it is out: nothing");
        CHECK(al_script_engine::clashes().load() == before + 1);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: list literal sums bracket elements and stay literals where order shows", "[allsloptimizer]")
    {
        // On LSO, where a list's shapes are made wherever they are smaller:
        // Mono weighs them against the helpers they need (see the case on Mono's list shapes).
        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        const auto require_compiles = [](const ALLSLOptimizer::Result& r) {
            ALLSLService           service;
            const ALScriptProblems said = service.check(r.text);
            for (const ALScriptProblem& p : said)
            {
                UNSCOPED_INFO("no error in what was written: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
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
                                                       lso);
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        // Each element added to the list bare (ALLSLCosts::elementForList).
        UNSCOPED_INFO("the comparison bracketed: " + r.text);
        REQUIRE(r.text.find("loc_params + 41 + (-1 < --loc_sitTargetsRemaining) + <0.0, 0.0, 0.0> + <0.0, 0.0, 0.0, 1.0>") != std::string::npos);
        require_compiles(r);

        // The first element under the cast, bracketed where the cast would
        // take less of it; the rest where the sum would.
        r = ALLSLOptimizer::run(wrap("", "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n        list l;\n"
                                         "        l = [a - b, 1];\n        l = [--a, 2];\n        l = [-1, 3];\n        l = [a * b, a - b];\n        llSay(0, llList2CSV(l));\n"),
                                lso);
        UNSCOPED_INFO("a difference: " + r.text);
        REQUIRE(r.text.find("l = (list)(a - b) + 1;") != std::string::npos);
        UNSCOPED_INFO("a step down: " + r.text);
        REQUIRE(r.text.find("l = (list)(--a) + 2;") != std::string::npos);
        UNSCOPED_INFO("a negative constant bare: " + r.text);
        REQUIRE(r.text.find("l = (list)-1 + 3;") != std::string::npos);
        UNSCOPED_INFO("a product under the cast, a difference after it: " + r.text);
        REQUIRE(r.text.find("l = (list)(a * b) + (a - b);") != std::string::npos);
        require_compiles(r);

        // A sum takes its right side first, so the elements would be taken
        // last to first: where more than one of them could see another's
        // change, the literal stays.
        r = ALLSLOptimizer::run(wrap("integer g;\ninteger bump()\n{\n    return ++g;\n}\n",
                                     "        integer i = (integer)llFrand(9);\n        list l;\n"
                                     "        l = [i, --i];\n        l = [bump(), bump()];\n        l = [llGetUnixTime(), bump()];\n        l = [i, g];\n"
                                     "        llSay(0, llList2CSV(l));\n"),
                                lso);
        UNSCOPED_INFO("a name and its change: " + r.text);
        REQUIRE(r.text.find("l = [i, --i];") != std::string::npos);
        UNSCOPED_INFO("two calls that change: " + r.text);
        REQUIRE(r.text.find("l = [bump(), bump()];") != std::string::npos);
        UNSCOPED_INFO("a read and a call that changes: " + r.text);
        REQUIRE(r.text.find("l = [llGetUnixTime(), bump()];") != std::string::npos);
        UNSCOPED_INFO("reads alone, a sum: " + r.text);
        REQUIRE(r.text.find("l = (list)i + g;") != std::string::npos);
        require_compiles(r);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: removed unreachable code is quoted by its first line cut to a few words", "[allsloptimizer]")
    {
        std::string body = "        return;\n        llOwnerSay(\"";
        body += std::string(300, 'x');
        body += "\");\n";
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(wrap("", body), options());
        const auto note = std::find_if(r.problems.begin(), r.problems.end(), [](const ALScriptProblem& p) { return p.key == "OptimizerRemovedUnreachable"; });
        REQUIRE(note != r.problems.end());
        REQUIRE(note->args.size() == 1);
        {
            INFO("not all of it");
            REQUIRE(note->args[0].size() < 80);
            REQUIRE(note->args[0].find("\xE2\x80\xA6") != std::string::npos);
        }
        UNSCOPED_INFO("from its start");
        CHECK(note->args[0].rfind("llOwnerSay", 0) == 0);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: the inliner and optimizer share one visit budget and rounds stop within it", "[allsloptimizer]")
    {
        const std::string source = wrap("integer twice(integer n) { return n * 2; }\ninteger thrice(integer n) { return twice(n) + n; }\n",
                                        "        llSay(0, (string)thrice(2));\n        llSay(0, (string)(1 + 2));\n");
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        const ALLSLOptimizer::Result whole = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("with room: not stopped");
        CHECK_FALSE(whole.stoppedEarly);
        // Room for the inliner's first round and not much more: it stops,
        // and leaves the optimizer nothing.
        const ALLSLInliner::Result once = ALLSLInliner::run(source, ALLSLInliner::Asked(), size_t(-1));
        CHECK(once.visited > 0);
        o.visitBudget = once.visited / 4;
        const ALLSLOptimizer::Result short_of = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("the optimizer stopped for what the inliner spent");
        CHECK(short_of.stoppedEarly);
        CHECK(std::any_of(short_of.problems.begin(), short_of.problems.end(), [](const ALScriptProblem& p) { return p.key == "OptimizerStoppedEarly"; }));
        const ALLSLInliner::Result cut = ALLSLInliner::run(source, ALLSLInliner::Asked(), 1);
        {
            INFO("the inliner's one round done, the next not begun");
            CHECK(cut.inlined > 0);
            CHECK((cut.stoppedEarly || cut.inlined == once.inlined));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: an always-true if stays when it guards a state change in a function", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("a bare state change in a function does not compile");
        CHECK(refused);
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        for (const ALScriptProblem& p : service.check(r.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
        const auto body = [&r](const char* name) {
            const size_t at = r.text.find(std::string(name) + "()\n{");
            return at == std::string::npos ? std::string() : r.text.substr(at, r.text.find("\n}", at) - at);
        };
        {
            INFO("f keeps its if: " + r.text);
            REQUIRE(body("f").find("if (") != std::string::npos);
            REQUIRE(body("f").find("state other;") != std::string::npos);
        }
        UNSCOPED_INFO("g loses the outer if, the inner one standing: " + r.text);
        REQUIRE(body("g").find("llGetUnixTime()") != std::string::npos);
        UNSCOPED_INFO("h keeps its if too: " + r.text);
        REQUIRE(body("h").find("if (") != std::string::npos);
        const size_t entry = r.text.find("state_entry()");
        {
            INFO("an event changes state as it likes: its if goes: " + r.text);
            REQUIRE(entry != std::string::npos);
            CHECK(r.text.find("if (", entry) > r.text.find("state other;", entry));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: llList2Key out of range folds to NULL_KEY on LSO and stays a call on Mono", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    state_entry()\n    {\n        llOwnerSay((string)llList2Key([1, 2], 5));\n    }\n}\n";
        ALLSLOptimizer::Options o = options();
        o.target                  = ALLSLOptimizer::Target::LSO;
        ALLSLOptimizer::Result r  = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("LSO: NULL_KEY: " + r.text);
        REQUIRE(r.text.find("llOwnerSay((string)NULL_KEY);") != std::string::npos);
        o.target = ALLSLOptimizer::Target::Mono;
        r        = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("Mono: still the call: " + r.text);
        REQUIRE(r.text.find("llList2Key(") != std::string::npos);
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: unused world reads go but never move and ALLSLTraits describes each call", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        UNSCOPED_INFO("the unused read goes: " + r.text);
        REQUIRE(r.text.find("    llGetPos();") == std::string::npos);
        UNSCOPED_INFO("the change stays: " + r.text);
        REQUIRE(r.text.find("llSetPos(") != std::string::npos);
        UNSCOPED_INFO("so does the unused local read from the world: " + r.text);
        REQUIRE(r.text.find("vector p") == std::string::npos);
        UNSCOPED_INFO("two reads of the clock are not put in another order: " + r.text);
        REQUIRE(r.text.find("[llGetTime(), llGetTime()]") != std::string::npos);
        for (const char* pure : { "llRound", "llListFindList", "llListSort", "llDumpList2String", "llMD5String", "llAcos", "llAsin" })
        {
            CAPTURE(pure);
            REQUIRE(ALLSLTraits::pure(pure));
        }
        {
            INFO("llGetPos is not");
            CHECK_FALSE(ALLSLTraits::pure("llGetPos"));
            const ALLSLTraits::Trait* get_pos = ALLSLTraits::of("llGetPos");
            REQUIRE(get_pos);
            CHECK(get_pos->mustUse);
        }
        // What SLua makes of each, from the same definitions.
        {
            INFO("llGetSubString takes its second and third from one in SLua's ll");
            const ALLSLTraits::Trait* sub_string = ALLSLTraits::of("llGetSubString");
            REQUIRE(sub_string);
            CHECK(sub_string->slua == ALLSLTraits::SluaIndexArgs);
            CHECK(sub_string->sluaIndexArgs == 0x6);
        }
        const ALLSLTraits::Trait* find_list = ALLSLTraits::of("llListFindList");
        REQUIRE(find_list);
        UNSCOPED_INFO("llListFindList answers an index, and SLua deprecates it");
        CHECK(find_list->slua == (ALLSLTraits::SluaIndexResult | ALLSLTraits::SluaDeprecated));
        const ALLSLTraits::Trait* same_group = ALLSLTraits::of("llSameGroup");
        REQUIRE(same_group);
        UNSCOPED_INFO("llSameGroup answers a boolean");
        CHECK(same_group->slua == ALLSLTraits::SluaBool);
        const ALLSLTraits::Trait* timer_event = ALLSLTraits::of("llSetTimerEvent");
        REQUIRE(timer_event);
        UNSCOPED_INFO("llSetTimerEvent is llcompat's alone");
        CHECK(timer_event->slua == ALLSLTraits::SluaRemoved);
        {
            INFO("llSay is as it was");
            const ALLSLTraits::Trait* say = ALLSLTraits::of("llSay");
            REQUIRE(say);
            CHECK(say->slua == 0);
            CHECK_FALSE(say->sluaUse);
        }
        {
            INFO("llAbs has math.abs in its stead");
            const ALLSLTraits::Trait* ll_abs = ALLSLTraits::of("llAbs");
            REQUIRE(ll_abs);
            REQUIRE(ll_abs->sluaUse);
            CHECK(std::string(ll_abs->sluaUse) == "math.abs");
        }
        // How long each sleeps, under LSO and under Mono.
        {
            INFO("llSetPos sleeps 0.2 s");
            const ALLSLTraits::Trait* set_pos = ALLSLTraits::of("llSetPos");
            REQUIRE(set_pos);
            CHECK(set_pos->sleep == 0.2f);
            CHECK(set_pos->monoSleep == 0.2f);
        }
        const ALLSLTraits::Trait* params_fast = ALLSLTraits::of("llSetLinkPrimitiveParamsFast");
        REQUIRE(params_fast);
        UNSCOPED_INFO("llSetLinkPrimitiveParamsFast does not");
        CHECK(params_fast->sleep == 0.0f);
        {
            INFO("llTextBox sleeps under LSO alone");
            const ALLSLTraits::Trait* text_box = ALLSLTraits::of("llTextBox");
            REQUIRE(text_box);
            CHECK(text_box->sleep == 1.0f);
            CHECK(text_box->monoSleep == 0.0f);
        }
        {
            INFO("llOpenFloater under Mono alone");
            const ALLSLTraits::Trait* open_floater = ALLSLTraits::of("llOpenFloater");
            REQUIRE(open_floater);
            CHECK(open_floater->sleep == 0.0f);
            CHECK(open_floater->monoSleep == 0.2f);
        }
        // What item of the object's an argument names, by its LSL name or SLua's.
        using Item = ALLSLTraits::Item;
        UNSCOPED_INFO("llPlaySound's first, a sound");
        CHECK(ALLSLTraits::itemArg("llPlaySound", 0) == Item::Sound);
        UNSCOPED_INFO("its second names nothing");
        CHECK(ALLSLTraits::itemArg("llPlaySound", 1) == Item::None);
        UNSCOPED_INFO("llGiveInventory's second, whatever it is");
        CHECK(ALLSLTraits::itemArg("llGiveInventory", 1) == Item::Any);
        UNSCOPED_INFO("llMakeFountain's seventh, a texture");
        CHECK(ALLSLTraits::itemArg("llMakeFountain", 6) == Item::Texture);
        UNSCOPED_INFO("SLua's ll.GetNotecardLine, a notecard");
        CHECK(ALLSLTraits::itemArg("ll.GetNotecardLine", 0) == Item::Notecard);
        UNSCOPED_INFO("llcompat's the same");
        CHECK(ALLSLTraits::itemArg("llcompat.RezObject", 0) == Item::Object);
        {
            INFO("llSay names none");
            CHECK(ALLSLTraits::itemArg("llSay", 1) == Item::None);
            CHECK(ALLSLTraits::itemArg("ll.Say", 1) == Item::None);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a value-returning function goes in whatever its body where order allows", "[allsloptimizer]")
    {
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
        {
            INFO("the declaration's variable set by the body, its returns but the last jumps: " + put.text);
            CHECK(has("integer x;\n{\ninteger k = 1;"));
            CHECK(has("{ x = k + g; jump _ret_"));
            CHECK(has("    x = 0;"));
        }
        UNSCOPED_INFO("the assignment's too: " + put.text);
        CHECK(has("integer k = 2;"));
        UNSCOPED_INFO("a value nobody takes: the returns that change nothing go: " + put.text);
        CHECK(has("integer k = 3;\n\n    if (k) jump _ret_3;\n    g = 7;"));
        // A label is one to an event: every block's end its own, however
        // many rounds made them.
        for (const char* label : { "@_ret_1;", "@_ret_2;", "@_ret_3;", "@_ret_4;", "@_ret_5;", "@_ret_6;" })
        {
            const size_t first = put.text.find(label);
            CAPTURE(label);
            UNSCOPED_INFO("at most once: " + put.text);
            REQUIRE((first == std::string::npos || put.text.find(label, first + 1) == std::string::npos));
        }
        UNSCOPED_INFO("g runs before f(4) and f writes g: the call stays: " + put.text);
        CHECK(has("x = f(4) + g;"));
        {
            INFO("f(5) is the right operand and runs first: set before the statement: " + put.text);
            CHECK(has("integer _r_"));
            CHECK(has("x = g + _r_"));
        }
        UNSCOPED_INFO("a branch standing alone gets braces for the block: " + put.text);
        CHECK((has("if (x) {") || has("if (x) {\n")));
        {
            INFO("outer returns f's value as its own, then goes where it is called: " + put.text);
            CHECK_FALSE(has("integer outer(integer k)"));
            CHECK(has("integer k_1 = k;"));
        }
        UNSCOPED_INFO("f stays, one call of it standing: " + put.text);
        CHECK(has("integer f(integer k)"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(put.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + put.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.inlineNames             = { "f" };
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("the optimizer reads it: " + notes(r));
        CHECK(r.optimized);
        for (const ALScriptProblem& p : service.check(r.text))
        {
            UNSCOPED_INFO("and what it wrote compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: calls in loop conditions and steps go in with loops rewritten as jumps", "[allsloptimizer]")
    {
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
        {
            INFO("every call went, and both functions: " + put.text);
            CHECK_FALSE(has("next()"));
            CHECK_FALSE(has("tick("));
            CHECK_FALSE(has("integer next()"));
        }
        {
            INFO("the while and the for are labels and ifs: " + put.text);
            CHECK_FALSE(has("while (next"));
            CHECK_FALSE(has("for ("));
            CHECK(has("@_loop_1;"));
            CHECK(has("jump _loop_1;"));
            CHECK(has("@_loop_2;"));
            CHECK(has("jump _loop_2;"));
        }
        UNSCOPED_INFO("the for's first part runs once, before its label: " + put.text);
        CHECK(put.text.find("i = 0;") < put.text.find("@_loop_2;"));
        UNSCOPED_INFO("its step's call goes as a statement's, the argument set in the call's order: " + put.text);
        CHECK(has("integer i_1 = i++;"));
        {
            INFO("the do stays a do, braces round the if's branch for its value's local: " + put.text);
            CHECK(has("if (g) {"));
            CHECK(has("do {"));
            CHECK(has("} while (_r_"));
        }
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(put.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + put.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
        ALLSLOptimizer::Options o = options();
        o.inlining                = true;
        o.inlineNames             = { "next", "tick" };
        for (ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            o.target                       = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            for (const ALScriptProblem& p : service.check(r.text))
            {
                UNSCOPED_INFO("and what the optimizer wrote compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a pure local read once moves to its read and a jump to the next line goes", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        auto has = [&r](const std::string& text) { return r.text.find(text) != std::string::npos; };
        {
            INFO("b goes where it is read: " + r.text);
            CHECK_FALSE(has("integer b"));
            CHECK(has("llOwnerSay((string)(g * 2));"));
        }
        UNSCOPED_INFO("c's g is written between: it stays: " + r.text);
        CHECK(has("integer c = -~g;"));
        UNSCOPED_INFO("d is read twice: " + r.text);
        CHECK(has("integer d = g * 3;"));
        UNSCOPED_INFO("e is read round a loop: " + r.text);
        CHECK(has("integer e = g * 4;"));
        UNSCOPED_INFO("f is read in a branch, a statement of its own: " + r.text);
        CHECK(has("integer f = g * 5;"));
        UNSCOPED_INFO("h is read after a label: " + r.text);
        CHECK(has("integer h = g * 6;"));
        UNSCOPED_INFO("the read of the clock stays: " + r.text);
        CHECK(has("integer a = llGetUnixTime();"));
        {
            INFO("a jump to the label next goes, and the label with it: " + r.text);
            CHECK_FALSE(has("jump out;"));
            CHECK_FALSE(has("@out;"));
        }
        UNSCOPED_INFO("a jump to a label whose name is twice in the event stays: " + r.text);
        CHECK(has("jump twice;"));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: nested blocks flatten and declarations move down to their first assignment", "[allsloptimizer]")
    {
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
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        auto has = [&r](const std::string& text) { return r.text.find(text) != std::string::npos; };
        {
            INFO("a block of one statement is that statement: " + r.text);
            CHECK(has("    llOwnerSay(\"a\");"));
            CHECK_FALSE(has("{\n            llOwnerSay(\"a\");"));
        }
        UNSCOPED_INFO("one whose local is named nowhere else too: " + r.text);
        CHECK(has("        integer u = llGetUnixTime();"));
        UNSCOPED_INFO("two whose locals share a name stay blocks: " + r.text);
        CHECK(has("            integer v = llGetUnixTime();"));
        {
            INFO("x declared where it is first set: " + r.text);
            CHECK_FALSE(has("integer x;"));
            CHECK(has("integer x = llGetUnixTime();"));
        }
        {
            INFO("y not past the jump: " + r.text);
            CHECK(has("integer y;"));
            CHECK(has("y = llGetUnixTime();"));
        }
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: infinity reads back on every target and uncompilable output is refused", "[allsloptimizer]")
    {
        const std::string source = "float foo = -2.0e+9999;\nfloat bar = 2.0e+9999;\n"
                                   "default\n{\n    state_entry()\n    {\n        llSetPos(<-2.0e+9999, 2.0e+9999, 0>);\n"
                                   "        llSetRot(<-2.0e+9999, 2.0e+9999, 0, 0>);\n        llOwnerSay((string)(foo + bar));\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            UNSCOPED_INFO("written as past the range: " + r.text);
            REQUIRE(r.text.find("<-1.0e+999, 1.0e+999, 0") != std::string::npos);
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, target != ALLSLOptimizer::Target::LSO))
            {
                UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }

        // The check a run makes of what it wrote, which gives the source
        // back where what was written does not compile.
        UNSCOPED_INFO("a script that compiles passes");
        REQUIRE_FALSE(ALLSLOptimizer::checkWritten(source).has_value());
        const std::optional<ALScriptProblem> refused = ALLSLOptimizer::checkWritten("default { state_entry() { llOwnerSay(inf.0); } }\n");
        UNSCOPED_INFO("one that does not is refused");
        REQUIRE(refused.has_value());
        {
            INFO("as a warning with the parser's words");
            CHECK(refused->key == "OptimizerWroteUncompilable");
            CHECK(refused->severity == ALScriptProblem::Severity::Warning);
            REQUIRE_FALSE(refused->args[0].empty());
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a global constant is inlined only where that is cheaper on the target", "[allsloptimizer]")
    {
        const auto reads = [](const std::string& global, const std::string& read, int times) {
            std::string body;
            for (int i = 0; i < times; ++i)
            {
                body += "        " + read + "\n";
            }
            return allsloptimizer_data::wrap(global + "\n", "        vector v; float f; string s;\n" + body + "        llOwnerSay((string)[v, f, s]);\n");
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
        UNSCOPED_INFO("a vector read once goes on LSO");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(V, "v += V;", 1), lso), "vector V"));
        UNSCOPED_INFO("read three times it stays on LSO");
        CHECK(kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), lso), "vector V"));
        UNSCOPED_INFO("and on Mono");
        CHECK(kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), mono), "vector V"));
        UNSCOPED_INFO("but not whole on Mono");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(W, "v += W;", 3), mono), "vector W"));
        UNSCOPED_INFO("and not on Luau, which keeps a constant once");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(V, "v += V;", 3), luau), "vector V"));

        // A float goes on LSO and Luau however often; on Mono, up to where
        // nine bytes a read come to the field.
        const std::string F = "float F = 2.5;";
        UNSCOPED_INFO("a float read twenty times goes on LSO");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), lso), "float F"));
        UNSCOPED_INFO("and on Luau");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), luau), "float F"));
        UNSCOPED_INFO("but stays on Mono");
        CHECK(kept(ALLSLOptimizer::run(reads(F, "f += F;", 20), mono), "float F"));
        UNSCOPED_INFO("which takes it read three times");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(F, "f += F;", 3), mono), "float F"));

        // A string: Mono holds it once whatever; LSO writes it at each
        // place, which a long one read often does not pay for.
        const std::string S = "string S = \"a sentence long enough to matter\";";
        UNSCOPED_INFO("a long string read five times stays on LSO");
        CHECK(kept(ALLSLOptimizer::run(reads(S, "s += S;", 5), lso), "string S"));
        UNSCOPED_INFO("and goes on Mono");
        CHECK_FALSE(kept(ALLSLOptimizer::run(reads(S, "s += S;", 5), mono), "string S"));

        // Folded into a larger value, a global is no read of it.
        const ALLSLOptimizer::Result folded = ALLSLOptimizer::run(reads(V, "v += V * 2;", 3), lso);
        {
            INFO("a product folds: " + folded.text);
            CHECK_FALSE(kept(folded, "vector V"));
            REQUIRE(folded.text.find("<3.0, 5.0, 7.0>") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: inlining by cost inlines a many-called function only where it is smaller", "[allsloptimizer]")
    {
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
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            UNSCOPED_INFO("the small one goes: " + r.text);
            REQUIRE(r.text.find("say(") == std::string::npos);
            UNSCOPED_INFO("the large one stays: " + r.text);
            REQUIRE(r.text.find("tell(") != std::string::npos);
            REQUIRE(r.weight.compiled);
            REQUIRE(r.weight.total > 0);
            const ALScriptProblem* chose = nullptr;
            for (const ALScriptProblem& p : r.problems)
            {
                chose = p.key == "InlinerChoseFunction" ? &p : chose;
            }
            {
                INFO("said: " + notes(r));
                REQUIRE(chose);
                REQUIRE(chose->args.size() == 4);
                REQUIRE(chose->args[0] == "say");
                REQUIRE(chose->args[1] == "3");
            }
            UNSCOPED_INFO("at the function");
            REQUIRE(chose->line == 0);
            UNSCOPED_INFO("to its end");
            REQUIRE(chose->endLine == 3);

            o.inlineByCost                    = false;
            const ALLSLOptimizer::Result left = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("left alone without it: " + left.text);
            REQUIRE(left.text.find("say(") != std::string::npos);
            UNSCOPED_INFO("and nothing weighed");
            REQUIRE_FALSE(left.weight.compiled);
        }
        // On Luau, where a call costs little, putting it in place comes to
        // a byte more: tried, and not kept.
        o.target                              = ALLSLOptimizer::Target::Luau;
        o.inlineByCost                        = true;
        const ALLSLOptimizer::Result weighed  = ALLSLOptimizer::run(source, o);
        o.inlineByCost                        = false;
        const ALLSLOptimizer::Result unweighed = ALLSLOptimizer::run(source, o);
        {
            INFO("never larger for trying");
            CHECK(weighed.weight.compiled);
            CHECK(weighed.weight.total <= ALScriptWeigh::lslLuau(unweighed.text).total);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: -1 tests and small steps and list elements take each target's smaller form", "[allsloptimizer]")
    {
        const std::string source = kept(wrap("list l;\ninteger g;\n",
                                        "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n        string s = llGetObjectName();\n"
                                        "        integer found = llListFindList(l, [a]);\n"
                                        "        if (a != -1) g = 1;\n"
                                        "        if (found >= 0 && llSubStringIndex(s, \"x\") > -1) g = 2;\n"
                                        "        if (!(found < 0)) g = 3;\n"
                                        "        g = a == -1;\n"
                                        "        g = found < 0;\n"
                                        "        g = a + 1;\n"
                                        "        g = 2 + a * b;\n"
                                        "        g = b - 1;\n"
                                        "        g = -a - 2;\n"
                                        "        g = a != -1;\n"
                                        "        a++;\n"
                                        "        for (b = 0; b < 3; b--) g += b;\n"
                                        "        l = l + [a];\n"
                                        "        l = l + [a, s];\n"
                                        "        l = [s] + l;\n"
                                        "        l = l + (list)s;\n"
                                        "        l += [b];\n"
                                        "        l = l + [llFrand(1), llFrand(2)];\n"
                                        "        llOwnerSay((string)g + (string)l + (string)a + (string)b);\n"), { "g" });
        const auto run = [&](ALLSLOptimizer::Target target) {
            ALLSLOptimizer::Options o = allsloptimizer_data::options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + allsloptimizer_data::notes(r));
            REQUIRE(r.optimized);
            ALLSLService           service;
            const ALScriptProblems said = service.check(r.text, target != ALLSLOptimizer::Target::LSO);
            for (const ALScriptProblem& p : said)
            {
                UNSCOPED_INFO("no error in what was written: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
            return r;
        };
        const auto has = [](const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; };

        const std::string lso = run(ALLSLOptimizer::Target::LSO).text;
        UNSCOPED_INFO("x != -1 as ~x, in a condition: " + lso);
        CHECK(has(lso, "if (~a)"));
        UNSCOPED_INFO("finds, both ways round, inside &&: " + lso);
        CHECK(has(lso, "if (~found && ~llSubStringIndex(s, \"x\"))"));
        UNSCOPED_INFO("a find < 0 as !~find, under a !");
        CHECK((has(lso, "if (!!~found)") || has(lso, "if (~found)")));
        UNSCOPED_INFO("x == -1 as !~x, as a value");
        CHECK(has(lso, "g = !~a;"));
        UNSCOPED_INFO("a find < 0 as !~find, as a value, a local set to a find");
        CHECK(has(lso, "g = !~found;"));
        UNSCOPED_INFO("x + 1 as -~x");
        CHECK(has(lso, "g = -~a;"));
        UNSCOPED_INFO("2 + x as -~-~x, the product bracketed");
        CHECK(has(lso, "g = -~-~(a * b);"));
        UNSCOPED_INFO("x - 1 as ~-x");
        CHECK(has(lso, "g = ~-b;"));
        UNSCOPED_INFO("x - 2 as ~-~-x, a minus bracketed after a minus");
        CHECK(has(lso, "g = ~-~-(-a);"));
        UNSCOPED_INFO("x != -1 as a value stays");
        CHECK(has(lso, "g = a != -1;"));
        UNSCOPED_INFO("an increment before");
        CHECK(has(lso, "++a;"));
        UNSCOPED_INFO("and a for's step, the loop a do on LSO, its first check known");
        CHECK(has(lso, "--b"));
        UNSCOPED_INFO("l + [a] as l + a");
        CHECK(has(lso, "l = l + a;"));
        UNSCOPED_INFO("l + [a, s] as l + a + s");
        CHECK(has(lso, "l = l + a + s;"));
        UNSCOPED_INFO("[s] + l as s + l");
        CHECK(has(lso, "l = s + l;"));
        UNSCOPED_INFO("l + (list)s as l + s");
        CHECK(has(lso, "l = l + s;"));
        UNSCOPED_INFO("l += [b] as l += b");
        CHECK(has(lso, "l += b;"));
        UNSCOPED_INFO("elements whose order would show stay together");
        CHECK_FALSE(has(lso, "l + llFrand(1) + llFrand(2)"));

        const ALLSLOptimizer::Result monoRun = run(ALLSLOptimizer::Target::Mono);
        const std::string&           mono    = monoRun.text;
        UNSCOPED_INFO("Mono: x != -1 as ~x: " + mono);
        CHECK(has(mono, "if (~a)"));
        // Mono's ldc.i4 is five bytes, -1 and 1 too.
        UNSCOPED_INFO("Mono: x == -1 as !~x: " + mono);
        CHECK(has(mono, "g = !~a;"));
        UNSCOPED_INFO("Mono: x + 1 as -~x");
        CHECK(has(mono, "g = -~a;"));
        UNSCOPED_INFO("Mono: x - 1 as ~-x");
        CHECK(has(mono, "g = ~-b;"));
        UNSCOPED_INFO("Mono: ++a");
        CHECK(has(mono, "++a;"));
        // Its list's shapes weighed against the helpers they bring in, and
        // whichever was smaller kept, with its weight.
        UNSCOPED_INFO("Mono: l still added to, in whichever shape was kept");
        CHECK(has(mono, "l = l"));
        {
            INFO("Mono: what it wrote weighs what it says");
            CHECK(monoRun.weight.compiled);
            CHECK(monoRun.weight.total == ALScriptWeigh::mono(mono).total);
        }

        const std::string luau = run(ALLSLOptimizer::Target::Luau).text;
        UNSCOPED_INFO("Luau: x != -1 stays, ~ a library call there: " + luau);
        CHECK(has(luau, "if (a != -1)"));
        UNSCOPED_INFO("Luau: x - 1 stays");
        CHECK(has(luau, "g = b - 1;"));
        UNSCOPED_INFO("Luau: a++ stays");
        CHECK(has(luau, "a++;"));
        UNSCOPED_INFO("Luau: l + a");
        CHECK(has(luau, "l = l + a;"));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: on Mono list shapes are kept only where they pay for the helpers they need", "[allsloptimizer]")
    {
        // A literal whose order would show, which stays, and a list added
        // to a list: the helpers a literal and a list's sum need are the
        // script's whatever happens to the rest.
        const auto adding = [](int times) {
            std::string body = "        integer i = (integer)llFrand(9);\n        list m = [i, --i];\n        list l = m + m;\n";
            for (int k = 0; k < times; ++k)
            {
                body += "        l += [llGetSubString(llGetObjectName(), " + std::to_string(k) + ", -1)];\n";
            }
            return allsloptimizer_data::wrap("", body + "        llOwnerSay((string)l);\n");
        };
        // Once: adding a string bare needs a helper nothing else in the
        // script uses, which costs more than the one place saves.
        const ALLSLOptimizer::Result once = ALLSLOptimizer::run(adding(1), options());
        UNSCOPED_INFO("optimized: " + notes(once));
        CHECK(once.optimized);
        UNSCOPED_INFO("once, the literal stays: " + once.text);
        REQUIRE(once.text.find("l += [llGetSubString") != std::string::npos);
        {
            INFO("weighed, as written");
            CHECK(once.weight.compiled);
            CHECK(once.weight.total == ALScriptWeigh::mono(once.text).total);
        }
        // Often: the places pay for it.
        const ALLSLOptimizer::Result often = ALLSLOptimizer::run(adding(4), options());
        {
            INFO("often, bare: " + often.text);
            REQUIRE(often.text.find("l += llGetSubString") != std::string::npos);
            REQUIRE(often.text.find("l += [") == std::string::npos);
        }
        {
            INFO("weighed, as written");
            CHECK(often.weight.compiled);
            CHECK(often.weight.total == ALScriptWeigh::mono(often.text).total);
        }
        // LSO holds no helpers for a list: bare, once or often.
        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        UNSCOPED_INFO("LSO, bare once");
        REQUIRE(ALLSLOptimizer::run(adding(1), lso).text.find("l += llGetSubString") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a looked-up key given as NULL_KEY or a non-key becomes the empty string", "[allsloptimizer]")
    {
        const std::string source = wrap("say(key k)\n{\n    llOwnerSay((string)k);\n}\n",
                                        "        key o = llGetOwnerKey(NULL_KEY);\n"
                                        "        llListen(0, \"\", NULL_KEY, \"\");\n"
                                        "        llGiveInventory((key)\"not a key\", \"x\");\n"
                                        "        key v = llGetOwnerKey(\"01234567-89ab-cdef-0123-456789abcdef\");\n"
                                        "        llMessageLinked(LINK_SET, 0, \"\", NULL_KEY);\n"
                                        "        llOwnerSay(NULL_KEY);\n"
                                        "        say(NULL_KEY);\n"
                                        "        say(o);\n        say(v);\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
            UNSCOPED_INFO("a lookup's null key: " + r.text);
            REQUIRE(has("llGetOwnerKey(\"\")"));
            UNSCOPED_INFO("a listen's");
            REQUIRE(has("llListen(0, \"\", \"\", \"\")"));
            UNSCOPED_INFO("no key at all");
            REQUIRE(has("llGiveInventory(\"\", \"x\")"));
            UNSCOPED_INFO("a key stays");
            REQUIRE(has("\"01234567-89ab-cdef-0123-456789abcdef\""));
            UNSCOPED_INFO("a linked message's stays, passed on as it came");
            REQUIRE(has("llMessageLinked(LINK_SET, 0, \"\", NULL_KEY)"));
            UNSCOPED_INFO("a string's stays");
            REQUIRE(has("llOwnerSay(NULL_KEY)"));
            UNSCOPED_INFO("a function of the script's has its own");
            REQUIRE_FALSE(has("say(\"\")"));
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, target != ALLSLOptimizer::Target::LSO))
            {
                UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: unreachable functions and states and write-only variables are removed", "[allsloptimizer]")
    {
        const std::string source = "integer count;\ninteger shown;\nlist l;\n"
                                   "ping()\n{\n    pong();\n}\npong()\n{\n    ping();\n}\n"
                                   "leave()\n{\n    if (llFrand(1.0) > 0.5)\n        state away;\n}\n"
                                   "show()\n{\n    llOwnerSay((string)shown);\n}\n"
                                   "default\n{\n    state_entry()\n    {\n"
                                   "        integer h = llListen(0, \"\", NULL_KEY, \"\");\n"
                                   "        count++;\n        count += 2;\n        count = llGetUnixTime();\n"
                                   "        integer i;\n        for (i = 0; llGetListLength(l) > 5; i++)\n            l = llDeleteSubList(l, 0, 0);\n"
                                   "        shown = llGetUnixTime();\n        show();\n"
                                   "        if (llGetObjectName() == \"on\")\n            count = 1;\n        else\n            llOwnerSay(\"off\");\n"
                                   "        if (llGetUnixTime() & 1)\n            count = 2;\n        else\n            llOwnerSay(\"even\");\n"
                                   "        state on;\n    }\n}\n"
                                   "state on\n{\n    touch_start(integer n)\n    {\n        llOwnerSay(\"on\");\n    }\n}\n"
                                   "state away\n{\n    state_entry()\n    {\n        state back;\n    }\n}\n"
                                   "state back\n{\n    state_entry()\n    {\n        state away;\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
            {
                INFO("functions calling only each other: " + r.text);
                REQUIRE_FALSE(has("ping"));
                REQUIRE_FALSE(has("pong"));
            }
            {
                INFO("one nothing calls, and the states only it and they enter");
                REQUIRE_FALSE(has("leave"));
                REQUIRE_FALSE(has("state away"));
                REQUIRE_FALSE(has("state back"));
            }
            UNSCOPED_INFO("a state entered stays");
            REQUIRE(has("state on"));
            {
                INFO("a listen's handle never read: the call stays: " + r.text);
                REQUIRE(has("llListen(0, \"\", \"\", \"\");"));
                REQUIRE_FALSE(has("integer h"));
            }
            {
                INFO("a global only set, and a read of the clock that set it");
                REQUIRE_FALSE(has("count"));
                REQUIRE_FALSE(has("llGetUnixTime();\n        integer"));
            }
            {
                INFO("a loop's counter never read: " + r.text);
                REQUIRE_FALSE(has("i = 0"));
                REQUIRE_FALSE(has("i++"));
                REQUIRE_FALSE(has("integer i"));
                REQUIRE(has("for (; "));
                REQUIRE(has(" > 5; )"));
            }
            UNSCOPED_INFO("a global read stays");
            REQUIRE(has("shown = llGetUnixTime();"));
            UNSCOPED_INFO("a comparison left empty turned around the other way: " + r.text);
            REQUIRE(has("if (llGetObjectName() != \"on\")"));
            UNSCOPED_INFO("anything else, what it negates bracketed; Luau's odd test a remainder");
            REQUIRE(has(target == ALLSLOptimizer::Target::Luau ? "if (!(llGetUnixTime() % 2))" : "if (!(llGetUnixTime() & 1))"));
            UNSCOPED_INFO("said, as set and never read");
            REQUIRE(std::any_of(r.problems.begin(), r.problems.end(), [](const ALScriptProblem& p) {
                       return p.key == "OptimizerRemovedWriteOnly" && p.args.size() == 1 && p.args[0] == "count";
                   }));
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, target != ALLSLOptimizer::Target::LSO))
            {
                UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: comparisons and their negations take each target's smaller form", "[allsloptimizer]")
    {
        const std::string source = wrap("",
                                        "        list l = llParseString2List(llGetObjectName(), [\" \"], []);\n        list m = llParseString2List(llGetObjectDesc(), [\" \"], []);\n"
                                        "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n"
                                        "        float f = llFrand(1.0);\n        float g = llFrand(1.0);\n        string s = llGetObjectName();\n"
                                        "        integer r = !(a < b);\n"
                                        "        integer t = !(s == \"x\");\n"
                                        "        integer u = !(l == m);\n"
                                        "        integer v = !(f < g);\n"
                                        "        if (a != b)\n            llOwnerSay(\"ne\");\n"
                                        "        if (a == b)\n            llOwnerSay(\"eq\");\n        else\n            llOwnerSay(\"ne\");\n"
                                        "        if (a >= 5)\n            llOwnerSay(\"ge\");\n"
                                        "        if (b <= 5)\n            llOwnerSay(\"le\");\n"
                                        "        if (b <= 2147483647)\n            llOwnerSay(\"max\");\n"
                                        "        while (a || b)\n            a = b = 0;\n"
                                        "        if (llStringLength(s))\n            llOwnerSay(s);\n"
                                        "        llOwnerSay((string)[r, t, u, v]);\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has  = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
            const bool lso  = target == ALLSLOptimizer::Target::LSO;
            const bool mono = target == ALLSLOptimizer::Target::Mono;
            {
                INFO("the ! of a comparison of integers: " + r.text);
                REQUIRE(has("a >= b"));
                REQUIRE_FALSE(has("!(a < b)"));
            }
            {
                INFO("of strings, ==: " + r.text);
                REQUIRE(has("s != \"x\""));
                REQUIRE_FALSE(has("!(s == \"x\")"));
            }
            UNSCOPED_INFO("not of lists, whose != is a length");
            REQUIRE(has("!(l == m)"));
            UNSCOPED_INFO("not of floats, which may be no number");
            REQUIRE(has("!(f < g)"));
            UNSCOPED_INFO("!= where only truth counts, a difference on Luau");
            REQUIRE(has(lso || mono ? "if (a ^ b)" : "if (a - b)"));
            UNSCOPED_INFO("an if on ==, its branches swapped");
            REQUIRE((lso || mono ? has("if (a ^ b)\n            llOwnerSay(\"ne\");\n        else\n            llOwnerSay(\"eq\");") : has("if (!(a - b))")));
            {
                INFO("inclusive against a constant");
                REQUIRE(has(mono ? "if (a > 4)" : "if (a >= 5)"));
                REQUIRE(has(mono ? "if (b < 6)" : "if (b <= 5)"));
            }
            UNSCOPED_INFO("not past the largest integer");
            REQUIRE(has("if (b <= 2147483647)"));
            UNSCOPED_INFO("|| where only truth counts");
            REQUIRE(has(mono ? "while (a | b)" : "while (a || b)"));
            UNSCOPED_INFO("a string's length where only truth counts: " + r.text);
            REQUIRE(has(lso ? "if (s != \"\")" : "if (llStringLength(s))"));
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, !lso))
            {
                UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: list dumps and lone object details become casts as do Mono's whole floats", "[allsloptimizer]")
    {
        const std::string source = wrap("list L = [2.0];\n",
                                        "        list l = llParseString2List(llGetObjectName(), [\" \"], []);\n"
                                        "        string d = llDumpList2String(l, \"\");\n"
                                        "        string e = llDumpList2String(l, \", \");\n"
                                        "        string n = llList2String(llGetObjectDetails(llGetOwner(), [OBJECT_NAME]), 0);\n"
                                        "        string both = llList2String(llGetObjectDetails(llGetOwner(), [OBJECT_NAME, OBJECT_DESC]), 0);\n"
                                        "        string second = llList2String(llGetObjectDetails(llGetOwner(), [OBJECT_NAME]), 1);\n"
                                        "        l += [-3.0];\n"
                                        "        llOwnerSay(d + e + n + both + second + (string)l + (string)L);\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
            const bool lso = target == ALLSLOptimizer::Target::LSO;
            // On LSO and Mono; on Luau a place saves nothing.
            UNSCOPED_INFO("dumped with nothing between: " + r.text);
            REQUIRE((target == ALLSLOptimizer::Target::Luau ? has("llDumpList2String(l, \"\")") : has("(string)l") && !has("llDumpList2String(l, \"\")")));
            UNSCOPED_INFO("with something between, a dump");
            REQUIRE(has("llDumpList2String(l, \", \")"));
            // On LSO; on Mono a byte a place, against a helper the script may lack.
            UNSCOPED_INFO("one detail: " + r.text);
            REQUIRE(lso == (has("(string)llGetObjectDetails(llGetOwner(), [OBJECT_NAME])") ||
                                                     has("(string)llGetObjectDetails(llGetOwner(), (list)OBJECT_NAME)")));
            UNSCOPED_INFO("two, a read");
            REQUIRE(has("llList2String(llGetObjectDetails(llGetOwner(), "));
            UNSCOPED_INFO("the second, a read");
            REQUIRE((has("OBJECT_NAME]), 1)") || has("(list)OBJECT_NAME), 1)")));
            const bool mono = target == ALLSLOptimizer::Target::Mono;
            UNSCOPED_INFO("a whole float in a list: " + r.text);
            REQUIRE(has(mono ? "((float)-3)" : "-3.0"));
            UNSCOPED_INFO("not in a global's value");
            REQUIRE(has("list L = [2.0];"));
            ALLSLService service;
            for (const ALScriptProblem& p : service.check(r.text, !lso))
            {
                UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: code after a reset or any dead end goes and so do harmless empty handlers", "[allsloptimizer]")
    {
        const std::string source = "integer a;\n"
                                   "both()\n{\n    if (a)\n    {\n        llOwnerSay(\"a\");\n        return;\n    }\n    else\n    {\n        llOwnerSay(\"b\");\n        return;\n    }\n"
                                   "    llOwnerSay(\"after both\");\n}\n"
                                   "forever()\n{\n    while (TRUE)\n        llSleep(1.0);\n    llOwnerSay(\"after forever\");\n}\n"
                                   "out()\n{\n    while (TRUE)\n    {\n        if (a)\n            jump done;\n        llSleep(1.0);\n    }\n    @done;\n"
                                   "    llOwnerSay(\"after out\");\n}\n"
                                   "default\n{\n    state_entry()\n    {\n    }\n"
                                   "    touch_start(integer n)\n    {\n        a = n;\n        both();\n        forever();\n        out();\n"
                                   "        llResetScript();\n        llOwnerSay(\"after reset\");\n    }\n"
                                   "    collision_start(integer n)\n    {\n    }\n}\n"
                                   "state idle\n{\n    timer()\n    {\n    }\n}\n";
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, options());
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        const auto has = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
        UNSCOPED_INFO("after an if none of whose ways goes on: " + r.text);
        CHECK_FALSE(has("after both"));
        UNSCOPED_INFO("after a loop that never ends");
        CHECK_FALSE(has("after forever"));
        UNSCOPED_INFO("after one jumped out of, it runs");
        CHECK(has("after out"));
        {
            INFO("after a reset");
            CHECK_FALSE(has("after reset"));
            CHECK(has("llResetScript();"));
        }
        UNSCOPED_INFO("an empty state_entry goes");
        CHECK_FALSE(has("state_entry"));
        UNSCOPED_INFO("an empty collision stays, which having makes the object collide so");
        CHECK(has("collision_start(integer n)"));
        UNSCOPED_INFO("a state keeps its one handler");
        CHECK((!has("state idle") || has("timer()")));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: llRound and llAcos and list dumps and finds fold where the answer is known", "[allsloptimizer]")
    {
        const std::string source = wrap("",
                                        "        llOwnerSay((string)[llRound(2.5), llRound(-2.5), llRound(0.49999997), llAcos(1.0), llAsin(2.0)]);\n"
                                        "        llOwnerSay(llDumpList2String([1, 2.5, \"a\"], \"-\"));\n"
                                        "        llOwnerSay((string)[llListFindList([1, \"a\", 2], [\"a\", 2]), llListFindList([1, 2], [3]),\n"
                                        "                            llListFindList([1, 2], []), llListFindList([\"1\"], [1])]);\n");
        ALLSLOptimizer::Options o = options();
        o.target                  = ALLSLOptimizer::Target::LSO;
        const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        const auto has = [&r](const std::string& what) { return r.text.find(what) != std::string::npos; };
        {
            INFO("llRound half up: " + r.text);
            CHECK_FALSE(has("llRound(2.5)"));
            CHECK_FALSE(has("llRound(-2.5)"));
        }
        UNSCOPED_INFO("llRound where single and double would differ, left");
        CHECK(has("llRound("));
        {
            INFO("llAcos folded, llAsin out of its range left");
            CHECK_FALSE(has("llAcos"));
            CHECK(has("llAsin(2.0)"));
        }
        UNSCOPED_INFO("a dump folded");
        CHECK(has("\"1-2.500000-a\""));
        {
            INFO("a find folded, and one not found");
            CHECK_FALSE(has("llListFindList([1, \"a\", 2]"));
            CHECK_FALSE(has("[3])"));
        }
        UNSCOPED_INFO("an empty list to find left, and a string no integer");
        CHECK((has("llListFindList((list)1 + 2, [])") || has("llListFindList([1, 2], [])")));
        ALLSLService service;
        for (const ALScriptProblem& p : service.check(r.text, false))
        {
            UNSCOPED_INFO("what was written compiles: " + p.message + "\n" + r.text);
            REQUIRE(p.severity != ALScriptProblem::Severity::Error);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a call answering a longer string folds only where the answer is smaller", "[allsloptimizer]")
    {
        using Target      = ALLSLOptimizer::Target;
        const auto weigh  = [](Target target, const std::string& text) {
            return target == Target::LSO ? ALScriptWeigh::lso(text) : target == Target::Mono ? ALScriptWeigh::mono(text) : ALScriptWeigh::lslLuau(text);
        };
        // Folded or not, never larger than the source.
        const auto folded = [&weigh](Target target, const std::string& body) {
            ALLSLOptimizer::Options o = allsloptimizer_data::options();
            o.target                  = target;
            const std::string            source = allsloptimizer_data::wrap("", body);
            const ALLSLOptimizer::Result r      = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + allsloptimizer_data::notes(r));
            REQUIRE(r.optimized);
            const ALScriptWeight before = weigh(target, source);
            const ALScriptWeight after  = weigh(target, r.text);
            {
                INFO("no larger: " + r.text);
                REQUIRE(before.compiled);
                REQUIRE(after.compiled);
                REQUIRE(after.total <= before.total);
            }
            return r.text.find("llMD5String") == std::string::npos;
        };
        const std::string hashOfX = "\"70c6ab518c36e18d9925c3c27c048b26\"";
        for (const Target target : { Target::LSO, Target::Mono, Target::Luau })
        {
            INFO("a hash of one character is smaller as the call");
            REQUIRE_FALSE(folded(target, "        llOwnerSay(llMD5String(\"x\", 0));\n"));
        }
        // LSO writes the argument where it is used, as it would the answer.
        {
            INFO("a hash of a longer string is smaller as its answer on LSO");
            CHECK(folded(Target::LSO, "        llOwnerSay(llMD5String(\"Hello, Avatar!\", 0));\n"));
        }
        // Mono and Luau hold a string once: where a function holds the
        // answer already, it costs a load.
        const std::string held = "        llOwnerSay(llMD5String(\"x\", 0));\n        llOwnerSay(" + hashOfX + ");\n";
        {
            INFO("held already on Mono");
            CHECK(folded(Target::Mono, held));
        }
        {
            INFO("held already on Luau");
            CHECK(folded(Target::Luau, held));
        }
        {
            INFO("LSO writes it again");
            CHECK_FALSE(folded(Target::LSO, held));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: same-named labels in a handler keep one short name when names shrink", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    state_entry()\n    {\n"
                                   "        if (1)\n        {\n            jump there;\n            @there;\n        }\n"
                                   "        if (0)\n        {\n            jump there;\n            @there;\n            llOwnerSay(\"reached\");\n        }\n"
                                   "    }\n"
                                   "    touch_start(integer n)\n    {\n        jump there;\n        llOwnerSay(\"skipped\");\n        @there;\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            o.shrinknames             = true;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized, not refused: " + notes(r));
            REQUIRE(r.optimized);
            UNSCOPED_INFO("the label jumped to from the first block is still there: " + r.text);
            REQUIRE(r.text.find("reached") != std::string::npos);
            UNSCOPED_INFO("renamed: " + r.text);
            REQUIRE(r.text.find("there") == std::string::npos);
            // In state_entry, every jump and every label one name.
            const size_t      entry = r.text.find("state_entry");
            const size_t      touch = r.text.find("touch_start");
            const std::string body  = r.text.substr(entry, touch - entry);
            std::set<std::string> names;
            for (size_t at = body.find_first_of("@j"); at != std::string::npos; at = body.find_first_of("@j", at + 1))
            {
                const bool   label = body[at] == '@';
                const size_t from  = label ? at + 1 : body.compare(at, 5, "jump ") == 0 ? at + 5 : std::string::npos;
                if (from != std::string::npos)
                {
                    names.insert(body.substr(from, body.find(';', from) - from));
                }
            }
            UNSCOPED_INFO("one name in state_entry: " + r.text);
            REQUIRE(names.size() == size_t(1));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: names the target keeps shrink first and Mono names a state for a held string", "[allsloptimizer]")
    {
        const std::string source = "integer counted;\n"
                                   "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        integer busy = n;\n        busy = busy * busy + busy;\n        busy = busy - busy / 3;\n"
                                   "        counted = busy;\n        llOwnerSay(\"Q\" + (string)counted);\n        state waiting;\n    }\n}\n"
                                   "state waiting\n{\n    touch_start(integer n)\n    {\n        llOwnerSay((string)counted);\n        state default;\n    }\n}\n";
        const auto run = [&source](ALLSLOptimizer::Target target) {
            ALLSLOptimizer::Options o = allsloptimizer_data::options();
            o.target                  = target;
            o.shrinknames             = true;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + allsloptimizer_data::notes(r));
            REQUIRE(r.optimized);
            return r;
        };
        const auto nameOf = [](const ALLSLOptimizer::Result& r, const std::string& name) {
            const auto found = r.renamed.find(name);
            return found == r.renamed.end() ? std::string() : found->second;
        };
        // Mono keeps a global's name in its field and where it is used; a
        // local's nowhere.
        const ALLSLOptimizer::Result mono = run(ALLSLOptimizer::Target::Mono);
        UNSCOPED_INFO("the state takes the string it can share: " + mono.text);
        CHECK(nameOf(mono, "waiting") == "Q");
        {
            INFO("the global before the busier local: " + mono.text);
            REQUIRE(nameOf(mono, "counted").size() == 1);
            CHECK((nameOf(mono, "counted") < nameOf(mono, "busy")));
        }
        // LSO keeps no names: the most used is named first, the text's
        // shortest.
        const ALLSLOptimizer::Result lso = run(ALLSLOptimizer::Target::LSO);
        UNSCOPED_INFO("on LSO the busiest first: " + lso.text);
        CHECK(nameOf(lso, "busy") == "a");
        UNSCOPED_INFO("and no string taken for a state: " + lso.text);
        CHECK(nameOf(lso, "waiting") != "Q");
        const ALScriptWeight named = ALScriptWeigh::mono(mono.text);
        ALLSLOptimizer::Options  plain = options();
        plain.shrinknames              = true;
        plain.target                   = ALLSLOptimizer::Target::LSO;
        const ALScriptWeight asLso = ALScriptWeigh::mono(ALLSLOptimizer::run(source, plain).text);
        {
            INFO("smaller on Mono than named for LSO");
            CHECK(named.compiled);
            CHECK(asLso.compiled);
            CHECK(named.total < asLso.total);
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a repeated pure call is held in a local where no larger and safe (L12)", "[allsloptimizer]")
    {
        ALLSLOptimizer::Options lso = options();
        lso.target                  = ALLSLOptimizer::Target::LSO;
        const std::string head      = "default\n{\n    touch_start(integer n)\n    {\n        string s = llDetectedName(0);\n";
        const std::string tail      = "    }\n}\n";
        const auto        body      = [&](const ALLSLOptimizer::Result& r) {
            const size_t at = r.text.find("        string s = llDetectedName(0);\n");
            return at == std::string::npos ? r.text : r.text.substr(at + 38);
        };

        const std::string three  = head + "        llOwnerSay(llToUpper(s));\n        llSay(0, llToUpper(s));\n        llWhisper(0, llToUpper(s));\n" + tail;
        ALLSLOptimizer::Result r = ALLSLOptimizer::run(three, lso);
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        UNSCOPED_INFO("kept");
        CHECK(body(r) == "        string toUpper = llToUpper(s);\n        llOwnerSay(toUpper);\n        llSay(0, toUpper);\n        llWhisper(0, toUpper);\n" + tail);
        UNSCOPED_INFO("noted: " + notes(r));
        CHECK(has(r, "kept llToUpper(s) in the local toUpper, which its 3 places read"));
        // On Mono a call given one variable is smaller than a local.
        r = ALLSLOptimizer::run(three, options());
        UNSCOPED_INFO("not on Mono: " + r.text);
        REQUIRE(r.text.find("toUpper") == std::string::npos);

        // A place in a loop after one its statement always comes to.
        r = ALLSLOptimizer::run(head + "        llSay(0, llToUpper(s));\n        integer i;\n        for (i = 0; i < n; ++i)\n            llSay(i, llToUpper(s));\n" + tail, lso);
        {
            INFO("into the loop: " + r.text);
            REQUIRE(r.text.find("string toUpper = llToUpper(s);\n        llSay(0, toUpper);") != std::string::npos);
            REQUIRE(r.text.find("llSay(i, toUpper);") != std::string::npos);
        }

        // The outermost call, and the call it is given with it.
        r = ALLSLOptimizer::run(head + "        llSay(0, llGetSubString(llToUpper(s), 0, 2));\n        llSay(1, llGetSubString(llToUpper(s), 0, 2));\n" + tail, lso);
        UNSCOPED_INFO("outermost");
        CHECK(body(r) ==
                      "        string getSubString = llGetSubString(llToUpper(s), 0, 2);\n        llSay(0, getSubString);\n        llSay(1, getSubString);\n" + tail);

        // A name the script has is not taken again.
        r = ALLSLOptimizer::run(head + "        string toUpper = llDetectedName(1);\n        llSay(0, toUpper);\n        llSay(0, llToUpper(s));\n        llSay(1, llToUpper(s));\n" + tail, lso);
        UNSCOPED_INFO("numbered: " + r.text);
        REQUIRE(r.text.find("string toUpper2 = llToUpper(s);") != std::string::npos);

        // What it is given written between: two places, each alone.
        const std::string written = head + "        llSay(0, llToUpper(s));\n        s = llDetectedName(1);\n        llSay(0, llToUpper(s));\n        llSay(0, s);\n" + tail;
        r                         = ALLSLOptimizer::run(written, lso);
        UNSCOPED_INFO("not across a write: " + r.text);
        REQUIRE(r.text.find("toUpper") == std::string::npos);
        // A label between, which a jump could bring the code in by.
        r = ALLSLOptimizer::run(head + "        llSay(0, llToUpper(s));\n        @again;\n        llSay(0, llToUpper(s));\n        if (llFrand(1) < 0.5)\n            jump again;\n" + tail, lso);
        UNSCOPED_INFO("not across a label: " + r.text);
        REQUIRE(r.text.find("toUpper") == std::string::npos);
        // The first place in a branch: kept, it would run where it did not.
        r = ALLSLOptimizer::run(head + "        if (n)\n            llSay(0, llToUpper(s));\n        llSay(0, llToUpper(s));\n" + tail, lso);
        UNSCOPED_INFO("not begun in a branch: " + r.text);
        REQUIRE(r.text.find("toUpper") == std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: dot and cross products of constant vectors fold as the VM computes them", "[allsloptimizer]")
    {
        // Each may be set again before it is read, so that its fold shows
        // in its declaration.
        const std::string source = wrap("", "        float d = <1, 3, -5> * <4, -2, -1>;\n        vector c = <1.0, 2.0, 3.0> % <4.0, 5.0, 6.0>;\n"
                                            "        vector z = <44608876.0, 125129528.0, 357885664.0> % <44608876.0, 125129528.0, 357885664.0>;\n"
                                            "        if (llFrand(1) < 0.5)\n        {\n            d = 0;\n            c = z = ZERO_VECTOR;\n        }\n"
                                            "        llSay(0, (string)d + (string)c + (string)z);\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            UNSCOPED_INFO("dot: " + r.text);
            REQUIRE(about(numbers(r.text, "float d"), { 3.f }));
            UNSCOPED_INFO("cross: " + r.text);
            REQUIRE(about(numbers(r.text, "vector c"), { -3.f, 6.f, -3.f }));
            UNSCOPED_INFO("crossed by itself: " + r.text);
            REQUIRE(about(numbers(r.text, "vector z"), { 0.f, 0.f, 0.f }));
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a local's constant propagates to reads every path agrees on (L15)", "[allsloptimizer]")
    {
        const std::string head = "default\n{\n    touch_start(integer n)\n    {\n";
        const std::string tail = "    }\n}\n";
        ALLSLOptimizer::Result r =
            ALLSLOptimizer::run(head + "        integer x = 5;\n        llSay(0, (string)x);\n        x = 7;\n        llSay(0, (string)x);\n        n = 3;\n        llSay(n, \"n\");\n" + tail, options());
        UNSCOPED_INFO("optimized: " + notes(r));
        CHECK(r.optimized);
        UNSCOPED_INFO("each read its value, and the variable gone");
        CHECK(r.text ==
                      head + "        llSay(0, \"5\");\n        llSay(0, \"7\");\n        llSay(3, \"n\");\n" + tail);
        {
            INFO("noted: " + notes(r));
            CHECK(has(r, "folded (string)x to \"7\""));
            CHECK(has(r, "inlined n to 3"));
            CHECK(has(r, "removed x, which is set and never read"));
        }

        // The ways of an if: what both say goes on, what they do not stops.
        r = ALLSLOptimizer::run(head + "        integer x = 1;\n        integer y = 1;\n        if (n)\n        {\n            x = 2;\n            llSay(0, \"a\");\n        }\n"
                                       "        else\n        {\n            llSay(0, \"b\");\n        }\n        llSay(x, (string)y);\n        x = y = 0;\n" + tail,
                                options());
        UNSCOPED_INFO("agreed: " + r.text);
        REQUIRE(r.text.find("llSay(x, \"1\");") != std::string::npos);
        // A way that returns says nothing of what comes after.
        r = ALLSLOptimizer::run(head + "        integer x = 1;\n        if (n)\n        {\n            x = 2;\n            return;\n        }\n        llSay(x, \"c\");\n        x = 0;\n" + tail,
                                options());
        UNSCOPED_INFO("a way that ends: " + r.text);
        REQUIRE(r.text.find("llSay(1, \"c\");") != std::string::npos);

        // A loop that sets it: not inside, nor after.
        r = ALLSLOptimizer::run(head + "        integer i = 0;\n        while (i < n)\n        {\n            llSay(i, \"d\");\n            ++i;\n        }\n        llSay(i, \"e\");\n" + tail,
                                options());
        {
            INFO("in a loop: " + r.text);
            REQUIRE(r.text.find("llSay(i, \"d\");") != std::string::npos);
            REQUIRE(r.text.find("llSay(i, \"e\");") != std::string::npos);
        }
        // Past a label, which a jump may come to with anything.
        r = ALLSLOptimizer::run(head + "        integer x = 1;\n        @top;\n        llSay(x, \"f\");\n        x = 2;\n        if (llFrand(1) < 0.5)\n            jump top;\n" + tail,
                                options());
        UNSCOPED_INFO("past a label: " + r.text);
        REQUIRE(r.text.find("llSay(x, \"f\");") != std::string::npos);
        // A loop holding a label forgets everything as it begins.
        r = ALLSLOptimizer::run(head + "        integer x = 1;\n        while (llFrand(1) < 0.5)\n        {\n            @in;\n            llSay(x, \"g\");\n        }\n"
                                       "        x = 2;\n        if (llFrand(1) < 0.5)\n            jump in;\n" + tail,
                                options());
        UNSCOPED_INFO("a loop with a label: " + r.text);
        REQUIRE(r.text.find("llSay(x, \"g\");") != std::string::npos);
        // LSL runs a binary operator's right side first: the read on the
        // left comes after the write.
        r = ALLSLOptimizer::run(head + "        integer x = 1;\n        llSay(0, (string)(x + (x = 3)));\n        llSay(x, \"h\");\n" + tail, options());
        UNSCOPED_INFO("after a write in the statement: " + r.text);
        REQUIRE(r.text.find("(string)(x + (x = 3))") != std::string::npos);
        // A read left where another read of the variable is not known: a
        // literal in place of a read is no smaller, and the variable stays.
        r = ALLSLOptimizer::run(head + "        string s = \"a long string of text\";\n        llSay(0, s);\n        if (n)\n            s = llDetectedName(0);\n        llSay(1, s);\n" + tail,
                                options());
        UNSCOPED_INFO("not one read alone: " + r.text);
        REQUIRE(r.text.find("llSay(0, s);") != std::string::npos);
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: comparisons settle by the ranges of their sides (L16)", "[allsloptimizer]")
    {
        const std::string source = "integer said() { llOwnerSay(\"said\"); return 1; }\n"
                                   "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        list l = llParseString2List(llDetectedName(0), [\" \"], []);\n"
                                   "        if (llGetListLength(l) < 0) llSay(0, \"a\");\n"
                                   "        integer count = llGetListLength(l);\n"
                                   "        if (count >= 0) llSay(0, \"b\");\n"
                                   "        if (llFrand(10.0) < 0) llSay(0, \"c\");\n"
                                   "        if (llListFindList(l, [\"x\"]) > -2) llSay(0, \"d\");\n"
                                   "        if (llGetListEntryType(l, 0) == 9) llSay(0, \"e\");\n"
                                   "        if (llSameGroup(llDetectedKey(0)) == 2) llSay(0, \"f\");\n"
                                   "        if (llFrand(10.0) < 5) llSay(0, \"g\");\n"
                                   "        if (llGetListLength(l) > 0) llSay(0, \"h\");\n"
                                   "        if (llStringLength(llList2String(l, said())) < 0) llSay(0, \"i\");\n"
                                   "    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto said = [&r](const char* what) { return r.text.find(std::string("llSay(0, \"") + what + "\")") != std::string::npos; };
            UNSCOPED_INFO("a count below nought never: " + r.text);
            REQUIRE_FALSE(said("a"));
            {
                INFO("a count's local nought or more always: " + r.text);
                REQUIRE(said("b"));
                REQUIRE(r.text.find("count") == std::string::npos);
            }
            UNSCOPED_INFO("llFrand of ten never below nought: " + r.text);
            REQUIRE_FALSE(said("c"));
            {
                INFO("a find above -2 always: " + r.text);
                REQUIRE(said("d"));
                REQUIRE(r.text.find("llListFindList") == std::string::npos);
            }
            UNSCOPED_INFO("a type past TYPE_ROTATION never: " + r.text);
            REQUIRE_FALSE(said("e"));
            UNSCOPED_INFO("a truth never 2: " + r.text);
            REQUIRE_FALSE(said("f"));
            {
                INFO("where the ranges meet, kept: " + r.text);
                REQUIRE(said("g"));
                REQUIRE(said("h"));
            }
            UNSCOPED_INFO("where a side changes something, kept: " + r.text);
            REQUIRE(said("i"));
            UNSCOPED_INFO("noted: " + notes(r));
            REQUIRE((has(r, "settled llGetListLength(l) < 0 to 0") || has(r, "settled (l != []) < 0 to 0") || has(r, "settled l != [] < 0 to 0")));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: folding is IEEE's: llVecNorm only where both ways agree and -0 keeps its sign", "[allsloptimizer]")
    {
        // Each may be set again before it is read, so that its fold shows
        // in its declaration.
        const std::string source = wrap("", "        vector a = llVecNorm(<0.75, 3, 1>);\n        vector b = llVecNorm(<0, 3, 4>);\n"
                                            "        float m = llVecMag(<0, 3, 4>);\n        list l = [-0.0];\n"
                                            "        if (llFrand(1) < 0.5)\n        {\n            a = b = ZERO_VECTOR;\n            m = 0;\n            l = [];\n        }\n"
                                            "        llSay(0, (string)a + (string)b + (string)m + llList2CSV(l));\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            // 0.75 / 3.25 is 0.230769232 as a single, 0.75 * (1 / 3.25)
            // 0.230769247: which the VM gives is not known, so it is left.
            // Built with the viewer's float flags, the compiler made the
            // division a product with the reciprocal, and this folded.
            UNSCOPED_INFO("left where the two differ: " + r.text);
            REQUIRE(r.text.find("vector a = llVecNorm(<0.75, ") != std::string::npos);
            const std::vector<F32> b = numbers(r.text, "vector b");
            {
                INFO("folded where they agree: " + r.text);
                REQUIRE(b.size() == 3);
                REQUIRE(b[0] == 0.0f);
                REQUIRE(b[1] == 3.0f / 5.0f);
                REQUIRE(b[2] == 4.0f / 5.0f);
            }
            UNSCOPED_INFO("a length both ways the same: " + r.text);
            REQUIRE(numbers(r.text, "float m") == std::vector<F32>{ 5.0f });
            const size_t at = r.text.find("list l = ");
            {
                INFO("a negative zero keeps its sign: " + r.text);
                REQUIRE(at != std::string::npos);
                REQUIRE(r.text.substr(at, r.text.find('\n', at) - at).find("-0.0") != std::string::npos);
            }
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: one-bit tests of one value merge into one mask test (L18)", "[allsloptimizer]")
    {
        const std::string source = "integer said() { llOwnerSay(\"said\"); return 4; }\n"
                                   "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        integer m = llGetLinkNumber();\n"
                                   "        if ((n & 4) && (n & 8)) llSay(0, \"a\");\n"
                                   "        integer v = (n & 4) && (n & 8) && (n & 1);\n"
                                   "        if (!(n & 4) && !(n & 8)) llSay(0, \"b\");\n"
                                   "        if ((n & 4) || (n & 16)) llSay(0, \"c\");\n"
                                   "        integer w = (n & 3) | (n & 12);\n"
                                   "        if ((n & 4) && (m & 8)) llSay(0, \"d\");\n"
                                   "        if ((n & 6) && (n & 8)) llSay(0, \"e\");\n"
                                   "        if ((said() & 4) && (said() & 8)) llSay(0, \"f\");\n"
                                   "        llSay(v, (string)w);\n"
                                   "    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Luau })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
            // -13 as its cast on Mono (ALLSLCosts::castForNegative).
            UNSCOPED_INFO("all set: " + r.text);
            REQUIRE((has("if (!~(n | -13))") || has("if (!~(n | ((integer)-13)))")));
            // v and w are read once, and their values go where they are read.
            UNSCOPED_INFO("three, a value: " + r.text);
            REQUIRE((has("llSay(!~(n | -14), ") || has("llSay(!~(n | ((integer)-14)), ")));
            UNSCOPED_INFO("all clear: " + r.text);
            REQUIRE(has("if (!(n & 12))"));
            UNSCOPED_INFO("any, where only truth counts: " + r.text);
            REQUIRE(has("if (n & 20)"));
            UNSCOPED_INFO("an | of them: " + r.text);
            REQUIRE(has("(string)(n & 15)"));
            {
                INFO("two values left: " + r.text);
                REQUIRE(has("(n & 4)"));
                REQUIRE(has("(m & 8)"));
            }
            {
                INFO("more than one bit left: " + r.text);
                REQUIRE(has("(n & 6)"));
                REQUIRE(has("(n & 8)"));
            }
            {
                INFO("what changes left: " + r.text);
                REQUIRE(has("(said() & 4)"));
                REQUIRE(has("(said() & 8)"));
            }
            UNSCOPED_INFO("noted: " + notes(r));
            REQUIRE(allsloptimizer_data::has(r, "wrote (n & 4) && (n & 8) as !~(n | -13)"));
        }
    }
    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: parsing and CSV and sorting and rotations fold where both VMs agree (L17)", "[allsloptimizer]")
    {
        // Each may be set again before it is read, so that its fold shows
        // in its declaration.
        const std::string source = wrap("", "        list p = llParseString2List(\"a,b;;c\", [\",\"], [\";\"]);\n"
                                            "        list k = llParseStringKeepNulls(\"a,,b,\", [\",\"], []);\n"
                                            "        list x = llParseString2List(\"ab\", [\"a\", \"ab\"], []);\n"
                                            "        list v = llCSV2List(\"a,b,c\");\n"
                                            "        list w = llCSV2List(\"a, b\");\n"
                                            "        list s = llListSort([3, \"c\", 1, \"a\", 2, \"b\"], 2, TRUE);\n"
                                            "        list t = llListSort([\"pear\", \"apple\", \"fig\"], 1, FALSE);\n"
                                            "        list u = llListSort([2, 1, 2], 1, TRUE);\n"
                                            "        list m = llListSort([\"b\", \"A\"], 1, TRUE);\n"
                                            "        rotation r = llRotBetween(<1, 0, 0>, <0, 1, 0>);\n"
                                            "        rotation q = llRotBetween(<1, 0, 0>, <1, 0, 0>);\n"
                                            "        rotation n = llRotBetween(<1, 0, 0>, <0, 0, 2>);\n"
                                            "        float h = llAngleBetween(<0, 0, 0, 1>, <0, 0, 0, 1>);\n"
                                            "        rotation z = llAxes2Rot(<1, 0, 0>, <0, 1, 0>, <0, 0, 1>);\n"
                                            "        rotation e = llAxes2Rot(<0, 1, 0>, <-1, 0, 0>, <0, 0, 1>);\n"
                                            "        if (llFrand(1) < 0.5)\n        {\n            p = k = x = v = w = s = t = u = m = [];\n"
                                            "            r = q = n = z = e = ZERO_ROTATION;\n            h = 1;\n        }\n"
                                            "        llOwnerSay(llList2CSV(p + k + x + v + w + s + t + u + m) + (string)[r, q, n, h, z, e]);\n");
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::LSO })
        {
            ALLSLOptimizer::Options o = options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + notes(r));
            REQUIRE(r.optimized);
            const auto has = [&r](const char* text) { return r.text.find(text) != std::string::npos; };
            UNSCOPED_INFO("split, spacers kept: " + r.text);
            REQUIRE(has("list p = [\"a\", \"b\", \";\", \";\", \"c\"];"));
            UNSCOPED_INFO("split, empties kept: " + r.text);
            REQUIRE(has("list k = [\"a\", \"\", \"b\", \"\"];"));
            UNSCOPED_INFO("a separator the start of another: left: " + r.text);
            REQUIRE(has("list x = llParseString2List("));
            UNSCOPED_INFO("CSV: " + r.text);
            REQUIRE(has("list v = [\"a\", \"b\", \"c\"];"));
            UNSCOPED_INFO("a value starting with a space: left: " + r.text);
            REQUIRE(has("list w = llCSV2List("));
            UNSCOPED_INFO("sorted by stride: " + r.text);
            REQUIRE(has("list s = [1, \"a\", 2, \"b\", 3, \"c\"];"));
            UNSCOPED_INFO("sorted down: " + r.text);
            REQUIRE(has("list t = [\"pear\", \"fig\", \"apple\"];"));
            UNSCOPED_INFO("two keys the same: left: " + r.text);
            REQUIRE(has("list u = llListSort("));
            UNSCOPED_INFO("keys of two cases: left: " + r.text);
            REQUIRE(has("list m = llListSort("));
            const float half = static_cast<float>(std::sqrt(0.5));
            UNSCOPED_INFO("a quarter turn between: " + r.text);
            REQUIRE(numbers(r.text, "rotation r") == std::vector<F32>{ 0.f, 0.f, half, half });
            UNSCOPED_INFO("parallel: left: " + r.text);
            REQUIRE(has("rotation q = llRotBetween("));
            UNSCOPED_INFO("a quarter turn the other way: " + r.text);
            REQUIRE(numbers(r.text, "rotation n") == std::vector<F32>{ 0.f, -half, 0.f, half });
            UNSCOPED_INFO("no angle between the same: " + r.text);
            REQUIRE(numbers(r.text, "float h") == std::vector<F32>{ 0.f });
            UNSCOPED_INFO("the axes themselves: " + r.text);
            REQUIRE(numbers(r.text, "rotation z") == std::vector<F32>{ 0.f, 0.f, 0.f, 1.f });
            // Made unit, as LLQuaternion does today, a quarter turn is a bit
            // larger in its last place than left as it was, as in 2007.
            UNSCOPED_INFO("ways that part by a bit: left: " + r.text);
            REQUIRE(has("rotation e = llAxes2Rot("));
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: increments and left-out defaults and negative casts where each is smaller", "[allsloptimizer]")
    {
        const std::string source = "integer gz = 0;\n"
                                   "integer gn = -5;\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        integer i = (integer)llFrand(9);\n"
                                   "        integer j = (integer)llFrand(9);\n"
                                   "        float f = llFrand(9);\n"
                                   "        vector v = llGetPos();\n"
                                   "        i = i + 1;\n"
                                   "        j -= 1;\n"
                                   "        j = i = 1 + i;\n"
                                   "        f = f + 1;\n"
                                   "        v.x = v.x + 1;\n"
                                   "        integer z = 0;\n"
                                   "        float y = 0.0;\n"
                                   "        float n = -0.0;\n"
                                   "        key k = \"\";\n"
                                   "        key nk = NULL_KEY;\n"
                                   "        string s = \"\";\n"
                                   "        vector w = ZERO_VECTOR;\n"
                                   "        rotation r = ZERO_ROTATION;\n"
                                   "        if (llFrand(1) < 0.5)\n"
                                   "        {\n"
                                   "            z = 3; y = 1.5; n = 2.5; k = llGetOwner(); nk = llGetOwner(); s = \"a\"; w = llGetPos(); r = llGetRot();\n"
                                   "        }\n"
                                   "        llSay(-5, \"x\");\n"
                                   "        llSetText(\"\", <-1.5, 0, 0>, 1);\n"
                                   "        llOwnerSay((string)[i, j, f, v, z, y, n, k, nk, s, w, r, gz, gn]);\n"
                                   "    }\n"
                                   "    touch_start(integer t)\n"
                                   "    {\n"
                                   "        gz = t;\n"
                                   "        gn = t;\n"
                                   "    }\n"
                                   "}\n";
        const auto run = [&](ALLSLOptimizer::Target target) {
            ALLSLOptimizer::Options o = allsloptimizer_data::options();
            o.target                  = target;
            const ALLSLOptimizer::Result r = ALLSLOptimizer::run(source, o);
            UNSCOPED_INFO("optimized: " + allsloptimizer_data::notes(r));
            REQUIRE(r.optimized);
            ALLSLService           service;
            const ALScriptProblems said = service.check(r.text, target != ALLSLOptimizer::Target::LSO);
            for (const ALScriptProblem& p : said)
            {
                UNSCOPED_INFO("no error in what was written: " + p.message + "\n" + r.text);
                REQUIRE(p.severity != ALScriptProblem::Severity::Error);
            }
            return r.text;
        };
        const auto has = [](const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; };

        const std::string mono = run(ALLSLOptimizer::Target::Mono);
        UNSCOPED_INFO("Mono: x = x + 1 as ++x: " + mono);
        CHECK(has(mono, "++i;"));
        UNSCOPED_INFO("Mono: x -= 1 as --x");
        CHECK(has(mono, "--j;"));
        UNSCOPED_INFO("Mono: as a value, 1 + x");
        CHECK(has(mono, "j = ++i;"));
        {
            INFO("Mono: a float's stays");
            CHECK_FALSE(has(mono, "++f"));
            CHECK_FALSE(has(mono, "f;"));
        }
        UNSCOPED_INFO("Mono: a vector's part stays");
        CHECK_FALSE(has(mono, "++v.x"));
        UNSCOPED_INFO("Mono: an integer's default left out");
        CHECK(has(mono, "integer z;"));
        UNSCOPED_INFO("Mono: an integer global's too");
        CHECK(has(mono, "integer gz;"));
        UNSCOPED_INFO("Mono: a float's is not, there");
        CHECK_FALSE(has(mono, "float y;"));
        UNSCOPED_INFO("Mono: a negative number as its cast");
        CHECK(has(mono, "llSay(((integer)-5), "));
        UNSCOPED_INFO("Mono: a vector's negative part");
        CHECK(has(mono, "<((float)-1.5), 0, 0>"));
        UNSCOPED_INFO("Mono: never in a global's value");
        CHECK(has(mono, "integer gn = -5;"));

        const std::string lso = run(ALLSLOptimizer::Target::LSO);
        {
            INFO("LSO: x = x + 1 stays as LSO has it smaller: " + lso);
            CHECK_FALSE(has(lso, "++i;"));
            CHECK_FALSE(has(lso, "--j;"));
        }
        UNSCOPED_INFO("LSO: a float's default left out");
        CHECK(has(lso, "float y;"));
        UNSCOPED_INFO("LSO: -0.0 is not the default");
        CHECK_FALSE(has(lso, "float n;"));
        UNSCOPED_INFO("LSO: a key's default, \"\"");
        CHECK(has(lso, "key k;"));
        UNSCOPED_INFO("LSO: NULL_KEY is not a key's default");
        CHECK_FALSE(has(lso, "key nk;"));
        UNSCOPED_INFO("LSO: a string's never");
        CHECK_FALSE(has(lso, "string s;"));
        {
            INFO("LSO: a vector's and a rotation's");
            CHECK(has(lso, "vector w;"));
            CHECK(has(lso, "rotation r;"));
        }
        UNSCOPED_INFO("LSO: an integer's is not, there");
        CHECK_FALSE(has(lso, "integer z;"));
        {
            INFO("LSO: no casts of negative numbers");
            CHECK_FALSE(has(lso, "((integer)-"));
            CHECK_FALSE(has(lso, "((float)-"));
        }

        const std::string luau = run(ALLSLOptimizer::Target::Luau);
        UNSCOPED_INFO("Luau: a key's default left out: " + luau);
        CHECK(has(luau, "key k;"));
        {
            INFO("Luau: nothing else");
            CHECK_FALSE(has(luau, "integer z;"));
            CHECK_FALSE(has(luau, "++i;"));
            CHECK_FALSE(has(luau, "((integer)-"));
        }
    }


    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: operators take each target's smaller form with Luau's bit tests as arithmetic", "[allsloptimizer]")
    {
        const std::string source = kept(wrap("integer g;\nfloat h;\n",
                                        "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n"
                                        "        integer c = llStringLength(llGetObjectName());\n        float f = llFrand(9);\n        float e = llFrand(9);\n"
                                        "        g = a - b;\n"
                                        "        g -= b;\n"
                                        "        h = f - e;\n"
                                        "        g = a << 3;\n"
                                        "        g = -1 - b;\n"
                                        "        g = a * 2;\n"
                                        "        h = f / 4;\n"
                                        "        g = b * -1;\n"
                                        "        if (a < b && b > 2)\n            llOwnerSay(\"both\");\n"
                                        "        if (a & 1)\n            llOwnerSay(\"odd\");\n"
                                        "        g = a == b;\n"
                                        "        g = c >> 2;\n"
                                        "        g = c & 7;\n"
                                        "        g = a >> 2;\n"
                                        "        llOwnerSay((string)[a, b, c, f, e, g, h]);\n"), { "g", "h" });
        const std::string mono = require_optimized(source, ALLSLOptimizer::Target::Mono);
        UNSCOPED_INFO("Mono: a - b as a + -b: " + mono);
        CHECK(in(mono, "g = a + -b;"));
        UNSCOPED_INFO("Mono: -= as += -");
        CHECK(in(mono, "g += -b;"));
        UNSCOPED_INFO("Mono: of floats");
        CHECK(in(mono, "h = f + -e;"));
        UNSCOPED_INFO("Mono: a shift as a product");
        CHECK(in(mono, "g = a * 8;"));
        UNSCOPED_INFO("Mono: -1 - x as ~x");
        CHECK(in(mono, "g = ~b;"));
        UNSCOPED_INFO("Mono: a doubling as a sum");
        CHECK(in(mono, "g = a + a;"));
        UNSCOPED_INFO("Mono: a float by a power of two as a product");
        CHECK(in(mono, "h = f * 0.25;"));
        UNSCOPED_INFO("Mono: x * -1 as -x");
        CHECK(in(mono, "g = -b;"));
        UNSCOPED_INFO("Mono: && of truths as &");
        CHECK(in(mono, "if (a < b & b > 2)"));
        {
            INFO("Mono: the bitwise stay");
            CHECK(in(mono, "if (a & 1)"));
            CHECK(in(mono, "g = c >> 2;"));
            CHECK(in(mono, "g = c & 7;"));
        }

        const std::string lso = require_optimized(source, ALLSLOptimizer::Target::LSO);
        {
            INFO("LSO: a - b stays: " + lso);
            CHECK(in(lso, "g = a - b;"));
            CHECK(in(lso, "g -= b;"));
            CHECK(in(lso, "h = f - e;"));
        }
        UNSCOPED_INFO("LSO: -1 - x as ~x");
        CHECK(in(lso, "g = ~b;"));
        UNSCOPED_INFO("LSO: x * -1 as -x");
        CHECK(in(lso, "g = -b;"));
        {
            INFO("LSO: the rest stays");
            CHECK(in(lso, "g = a << 3;"));
            CHECK(in(lso, "g = a * 2;"));
            CHECK(in(lso, "h = f / 4;"));
            CHECK(in(lso, "if (a < b && b > 2)"));
        }

        const std::string luau = require_optimized(source, ALLSLOptimizer::Target::Luau);
        UNSCOPED_INFO("Luau: a shift as a product: " + luau);
        CHECK(in(luau, "g = a * 8;"));
        UNSCOPED_INFO("Luau: a doubling as a sum");
        CHECK(in(luau, "g = a + a;"));
        UNSCOPED_INFO("Luau: -1 - x stays, ~ a call there");
        CHECK(in(luau, "g = -1 - b;"));
        UNSCOPED_INFO("Luau: an odd test as a remainder");
        CHECK(in(luau, "if (a % 2)"));
        UNSCOPED_INFO("Luau: == as the ! of a difference");
        CHECK(in(luau, "g = !(a - b);"));
        {
            INFO("Luau: a shift right and a mask of a count as a quotient and a remainder");
            CHECK(in(luau, "g = c / 4;"));
            CHECK(in(luau, "g = c % 8;"));
        }
        UNSCOPED_INFO("Luau: not of what may be below nought");
        CHECK(in(luau, "g = a >> 2;"));
        UNSCOPED_INFO("Luau: a - b stays");
        CHECK(in(luau, "g = a - b;"));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: branches are written with fewer jumps on every target", "[allsloptimizer]")
    {
        const std::string source = "integer g;\nstring t;\n"
                                   "integer big(integer a)\n{\n    if (a > 3)\n        return TRUE;\n    return FALSE;\n}\n"
                                   "integer none(integer a)\n{\n    if (a)\n        return FALSE;\n    else\n        return TRUE;\n}\n"
                                   "integer pick(integer a)\n{\n    if (a)\n    {\n        llOwnerSay(\"one\");\n        return 1;\n    }\n    else\n    {\n"
                                   "        llOwnerSay(\"other\");\n    }\n    return 2;\n}\n"
                                   "default\n{\n    state_entry()\n    {\n"
                                   "        integer a = (integer)llFrand(9);\n        integer b = (integer)llFrand(9);\n"
                                   "        if (a > 2)\n            g = 1;\n        else\n            g = 0;\n"
                                   "        if (b > 2)\n            g = 5;\n        else\n            g = 7;\n"
                                   "        if (b)\n            g = 5;\n        else\n            g = 7;\n"
                                   "        string u;\n        if (a > 4)\n            u = llGetObjectName();\n        else\n            u = \"none\";\n"
                                   "        if (a > 5)\n            t = llGetObjectName();\n        else\n            t = \"global\";\n"
                                   "        if (llFrand(1) > 0.5)\n            llOwnerSay(\"same\");\n        else\n            llOwnerSay(\"same\");\n"
                                   "        if (a == b)\n        {\n            llOwnerSay(\"eq\");\n            llOwnerSay(\"end\");\n        }\n"
                                   "        else\n        {\n            llOwnerSay(\"ne\");\n            llOwnerSay(\"end\");\n        }\n"
                                   "        if (b)\n        {\n            llOwnerSay(\"x\");\n            jump past;\n        }\n"
                                   "        llOwnerSay(\"y\");\n        @past;\n"
                                   "        if (a < 3)\n        {\n        }\n"
                                   "        if (a > 1)\n            if (b < 4)\n                llOwnerSay(\"inner\");\n"
                                   "        if (a)\n            if (b)\n                llOwnerSay(\"both\");\n"
                                   "        if (a)\n            if (100 / b)\n                llOwnerSay(\"faults\");\n"
                                   "        llOwnerSay((string)[big(a), none(b), pick(a), g, t, u]);\n    }\n"
                                   "    touch_start(integer n)\n    {\n        llOwnerSay((string)g + t);\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            const bool        lso  = target == ALLSLOptimizer::Target::LSO;
            const bool        mono = target == ALLSLOptimizer::Target::Mono;
            UNSCOPED_INFO("a truth returned: " + text);
            REQUIRE(in(text, "return a > 3;"));
            UNSCOPED_INFO("its ! the other way");
            REQUIRE(in(text, "return !a;"));
            UNSCOPED_INFO("a truth set");
            REQUIRE(in(text, "g = a > 2;"));
            {
                INFO("a select of a truth as arithmetic, on every target: " + text);
                REQUIRE(in(text, "g = (b > 2) * "));
                REQUIRE(in(text, " + 7;"));
            }
            UNSCOPED_INFO("of anything else, on LSO and Mono");
            REQUIRE((lso || mono ? in(text, "g = !!b * ") : !in(text, "g = !!b")));
            UNSCOPED_INFO("a local's value set before the if: " + text);
            REQUIRE(in(text, "u = \"none\";\n        if (a > 4)"));
            {
                INFO("not a global's, which a call in the check could read");
                REQUIRE(in(text, "t = \"global\";\n"));
                REQUIRE_FALSE(in(text, "t = \"global\";\n        if"));
            }
            {
                INFO("ways the same written once");
                REQUIRE_FALSE(in(text, "else\n            llOwnerSay(\"same\")"));
                REQUIRE(in(text, "llOwnerSay(\"same\");"));
            }
            {
                INFO("what both ways end with once, after");
                REQUIRE(in(text, "llOwnerSay(\"end\");"));
                REQUIRE(text.find("llOwnerSay(\"end\")") == text.rfind("llOwnerSay(\"end\")"));
            }
            UNSCOPED_INFO("an else after a return dropped");
            REQUIRE_FALSE(in(text, "else\n    {\n        llOwnerSay(\"other\")"));
            {
                INFO("a jump over statements as an else");
                REQUIRE_FALSE(in(text, "jump past"));
                REQUIRE_FALSE(in(text, "@past"));
            }
            UNSCOPED_INFO("an empty if gone");
            REQUIRE_FALSE(in(text, "a < 3"));
            UNSCOPED_INFO("nested ifs of truths merged on LSO and Mono: " + text);
            REQUIRE((lso || mono ? in(text, "if ((a > 1) & (b < 4))") : in(text, "if (a > 1)\n            if (b < 4)")));
            UNSCOPED_INFO("nested ifs merged with && on LSO");
            REQUIRE((lso ? in(text, "if (a && b)") : !in(text, "if (a && b)")));
            UNSCOPED_INFO("not where the inner may stop the script");
            REQUIRE(in(text, "100 / b"));
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: loops known to run become dos and unread counters count down", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    state_entry()\n    {\n"
                                   "        list l = llParseString2List(llGetObjectName(), [\" \"], []);\n"
                                   "        integer i;\n        integer j;\n        integer k;\n        integer m;\n        integer n = (integer)llFrand(9);\n"
                                   "        for (i = 0; i < 10; ++i)\n            llOwnerSay((string)i);\n"
                                   "        for (j = 0; j < 3; ++j)\n            llOwnerSay(\"three\");\n"
                                   "        for (k = 0; k < llGetListLength(l); ++k)\n            llOwnerSay(\"each\");\n"
                                   "        for (m = n; m < 10; ++m)\n            llOwnerSay((string)m);\n"
                                   "        integer w = 0;\n        while (w < 5)\n        {\n            llOwnerSay((string)w);\n            w += 2;\n        }\n"
                                   "        integer z;\n        for (z = 5; z < 3; ++z)\n            llOwnerSay(\"never\");\n"
                                   "        llOwnerSay((string)z);\n"
                                   "    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            const bool        luau = target == ALLSLOptimizer::Target::Luau;
            UNSCOPED_INFO("a for known to run as a do, its step in the check: " + text);
            REQUIRE((luau ? in(text, "for (i = 0; i < 10; ++i)") : in(text, "while(++i < 10);")));
            {
                INFO("a counter read nowhere else counted down");
                REQUIRE(in(text, "j = 3;"));
                REQUIRE(in(text, "while(--j);"));
            }
            {
                INFO("to a count never below nought: " + text);
                REQUIRE((in(text, "integer k = llGetListLength(l);") || in(text, "integer k = (l != []);")));
                REQUIRE(in(text, "while (k--)"));
            }
            UNSCOPED_INFO("not where the first check is not known");
            REQUIRE(in(text, "for (m = n; m < 10; ++m)"));
            UNSCOPED_INFO("a while known to run as a do");
            REQUIRE((luau ? in(text, "while (w < 5)") : in(text, "while(w < 5);")));
            {
                INFO("a loop known never to run gone, its first part kept: " + text);
                REQUIRE_FALSE(in(text, "never"));
                REQUIRE(in(text, "llOwnerSay(\"5\");"));
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: a key compared with NULL_KEY is tested as itself unless it may be any string", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n"
                                   "    touch_start(integer d)\n    {\n"
                                   "        key owner = llGetOwner();\n"
                                   "        if (owner != NULL_KEY)\n            llOwnerSay(\"owned\");\n"
                                   "        if (llDetectedKey(0) == NULL_KEY)\n            llOwnerSay(\"nobody\");\n        else\n            llOwnerSay(\"somebody\");\n"
                                   "        key sitter = llAvatarOnSitTarget();\n"
                                   "        if (sitter == NULL_KEY)\n            llOwnerSay(\"empty\");\n"
                                   "        key given = (key)llGetObjectDesc();\n"
                                   "        if (given != NULL_KEY)\n            llOwnerSay(\"given\");\n"
                                   "    }\n"
                                   "    listen(integer c, string n, key id, string m)\n    {\n"
                                   "        if (id != NULL_KEY)\n            llOwnerSay(m);\n    }\n"
                                   "    link_message(integer s, integer num, string str, key id)\n    {\n"
                                   "        if (id != NULL_KEY)\n            llOwnerSay(str);\n    }\n"
                                   "}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            UNSCOPED_INFO("a local set to the owner: " + text);
            REQUIRE(in(text, "if (owner)"));
            UNSCOPED_INFO("a detected key, its ways turned round");
            REQUIRE(in(text, "if (llDetectedKey(0))\n            llOwnerSay(\"somebody\");\n        else\n            llOwnerSay(\"nobody\");"));
            UNSCOPED_INFO("a lone == turned round");
            REQUIRE(in(text, "if (sitter)\n            ;\n        else\n            llOwnerSay(\"empty\");"));
            UNSCOPED_INFO("a listen's speaker: " + text);
            REQUIRE(in(text, "if (id)\n            llOwnerSay(m);"));
            UNSCOPED_INFO("not a key cast from a string");
            REQUIRE(in(text, "if (given != "));
            {
                INFO("not a link message's key, which is any string");
                REQUIRE(in(text, "if (id != "));
                REQUIRE(in(text, "llOwnerSay(str);"));
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: unread parameters and shared constant arguments and unread returns go", "[allsloptimizer]")
    {
        const std::string source = "integer got;\n"
                                   "say(integer channel, string message, integer unused)\n{\n    llSay(channel, message);\n}\n"
                                   "tell(integer quiet, string message)\n{\n    llOwnerSay(message);\n}\n"
                                   "integer noisy(integer n)\n{\n    llOwnerSay((string)n);\n    return n * 2;\n}\n"
                                   "integer read(integer n)\n{\n    return n + 1;\n}\n"
                                   "set(integer n)\n{\n    n = n + 1;\n    llOwnerSay((string)n);\n}\n"
                                   "default\n{\n    state_entry()\n    {\n"
                                   "        say(0, \"a\", 1);\n        say(0, \"b\", 2);\n"
                                   "        tell(llListen(5, \"\", \"\", \"\"), \"c\");\n        tell(3, \"d\");\n"
                                   "        noisy(llGetUnixTime());\n        noisy(4);\n"
                                   "        got = read(llGetUnixTime());\n"
                                   "        set(7);\n        set(7);\n"
                                   "    }\n"
                                   "    touch_start(integer t)\n    {\n        llOwnerSay((string)got);\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            {
                INFO("a parameter never read gone, and its arguments: " + text);
                REQUIRE(in(text, "say(string message)"));
                REQUIRE(in(text, "say(\"a\");"));
            }
            UNSCOPED_INFO("one every call gives the same constant made its value");
            REQUIRE(in(text, "llSay(0, message);"));
            {
                INFO("not where a call's argument does something");
                REQUIRE(in(text, "tell(integer quiet, string message)"));
                REQUIRE(in(text, "llListen(5"));
            }
            {
                INFO("a value no call reads no longer returned");
                REQUIRE(in(text, "\nnoisy(integer n)"));
                REQUIRE_FALSE(in(text, "integer noisy"));
            }
            UNSCOPED_INFO("what it returned still worked out where it does something, or gone");
            REQUIRE((!in(text, "return n * 2") || in(text, "n * 2;")));
            UNSCOPED_INFO("not where a call reads it");
            REQUIRE(in(text, "integer read(integer n)"));
            {
                INFO("not a parameter that is set");
                REQUIRE(in(text, "set(integer n)"));
                REQUIRE(in(text, "set(7);"));
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: dead stores go and globals each handler sets first become its locals", "[allsloptimizer]")
    {
        const std::string source = "integer scratch;\n"
                                   "integer twice;\n"
                                   "integer kept = 3;\n"
                                   "integer shared;\n"
                                   "poke(integer n)\n{\n    shared = n;\n}\n"
                                   "default\n{\n"
                                   "    state_entry()\n    {\n"
                                   "        integer i = llGetUnixTime();\n        i = (integer)llFrand(9);\n        llOwnerSay((string)i);\n"
                                   "        integer j = 4;\n        for (j = 0; j < (integer)llFrand(9); ++j)\n            llOwnerSay((string)j);\n"
                                   "        scratch = llGetUnixTime();\n        llOwnerSay((string)scratch);\n"
                                   "        twice = llGetUnixTime();\n        llOwnerSay((string)twice);\n"
                                   "        shared = 1;\n        poke(2);\n        llOwnerSay((string)shared);\n"
                                   "        if ((integer)llFrand(2) || 1)\n            llOwnerSay(\"always\");\n"
                                   "        if ((integer)llFrand(2) && 0)\n            llOwnerSay(\"never\");\n"
                                   "    }\n"
                                   "    touch_start(integer t)\n    {\n        twice = t;\n        llOwnerSay((string)twice + (string)kept);\n"
                                   "        kept = t;\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            // A read of the clock changes nothing, and goes with it.
            UNSCOPED_INFO("a value set again before it is read not set: " + text);
            REQUIRE(in(text, "integer i = (integer)llFrand(9);"));
            UNSCOPED_INFO("a for's first part sets again");
            REQUIRE_FALSE(in(text, "integer j = 4;"));
            {
                INFO("a global one handler sets before it reads made a local");
                REQUIRE(in(text, "integer scratch = llGetUnixTime();"));
                REQUIRE_FALSE(in(text, "integer scratch;\n"));
            }
            UNSCOPED_INFO("one two handlers set before they read, a local of each but on Mono");
            REQUIRE((target == ALLSLOptimizer::Target::Mono ? in(text, "integer twice;\n") : in(text, "integer twice = llGetUnixTime();") && in(text, "llOwnerSay((string)t + ")));
            UNSCOPED_INFO("not one read before it is set");
            REQUIRE(in(text, "integer kept"));
            UNSCOPED_INFO("not one a function sets too");
            REQUIRE(in(text, "integer shared"));
            {
                INFO("a || 1 settled true");
                REQUIRE(in(text, "llOwnerSay(\"always\")"));
                REQUIRE_FALSE(in(text, "|| 1"));
            }
            UNSCOPED_INFO("a && 0 settled false");
            REQUIRE_FALSE(in(text, "never"));
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: repeated global reads and repeated constants are held once where cheaper", "[allsloptimizer]")
    {
        const std::string source = "integer channel;\n"
                                   "integer never = 7;\n"
                                   "default\n{\n"
                                   "    state_entry()\n    {\n        channel = (integer)llFrand(1000);\n    }\n"
                                   "    touch_start(integer n)\n    {\n"
                                   "        llSay(channel, \"one\");\n        llSay(channel, \"two\");\n        llSay(channel, \"three\");\n"
                                   "        channel = n;\n        llSay(channel, (string)never);\n        llSay(never, (string)never);\n"
                                   "        llOwnerSay(\"a sentence long enough to be worth holding\");\n"
                                   "        llOwnerSay(\"a sentence long enough to be worth holding\");\n"
                                   "        llOwnerSay(\"a sentence long enough to be worth holding\");\n"
                                   "        llSetPos(llGetPos() + <1.5, 2.5, 3.5>);\n        llSetPos(llGetPos() + <1.5, 2.5, 3.5>);\n"
                                   "        llSetPos(llGetPos() + <1.5, 2.5, 3.5>);\n        llSetPos(llGetPos() + <1.5, 2.5, 3.5>);\n"
                                   "    }\n}\n";
        const std::string lso = require_optimized(source, ALLSLOptimizer::Target::LSO);
        {
            INFO("LSO: a long string at three places kept in a global: " + lso);
            CHECK(in(lso, "string s = \"a sentence long enough to be worth holding\";"));
            CHECK(in(lso, "llOwnerSay(s);"));
        }
        UNSCOPED_INFO("LSO: a vector at four places, in a global");
        CHECK(in(lso, "vector v = <1.5, 2.5, 3.5>;"));
        UNSCOPED_INFO("LSO: a global's reads stay, as large as a local's there");
        CHECK(in(lso, "llSay(channel, \"one\");"));

        const std::string luau = require_optimized(source, ALLSLOptimizer::Target::Luau);
        {
            INFO("Luau: a global read three times read once: " + luau);
            CHECK(in(luau, "integer localChannel = channel;"));
            CHECK(in(luau, "llSay(localChannel, \"one\");"));
        }
        UNSCOPED_INFO("Luau: not across where it is set");
        CHECK(in(luau, "channel = n;"));
        UNSCOPED_INFO("Luau: never one the script never sets, which is the folder's");
        CHECK_FALSE(in(luau, "localNever"));
        UNSCOPED_INFO("Luau: constants held once in a function already");
        CHECK_FALSE(in(luau, "string s = "));

        const std::string mono = require_optimized(source, ALLSLOptimizer::Target::Mono);
        UNSCOPED_INFO("Mono: a vector at four places kept in a local, its frame paid for: " + mono);
        CHECK(in(mono, "vector v = <1.5, 2.5, 3.5>;"));
        UNSCOPED_INFO("Mono: three reads of a global not worth a local's frame");
        CHECK_FALSE(in(mono, "localChannel"));
        UNSCOPED_INFO("Mono: a string held once already");
        CHECK_FALSE(in(mono, "string s = "));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: string literals join on LSO or when asked and endless loops become jumps", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        llOwnerSay(\"Hello, \" + \"world\");\n"
                                   "        while (TRUE)\n        {\n            llOwnerSay((string)llFrand(1));\n"
                                   "            if (llFrand(1) > 0.5)\n                jump out;\n        }\n"
                                   "        @out;\n        llOwnerSay(\"done\");\n    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            const bool        lso  = target == ALLSLOptimizer::Target::LSO;
            const bool        luau = target == ALLSLOptimizer::Target::Luau;
            UNSCOPED_INFO("two literals joined on LSO alone, by default: " + text);
            REQUIRE((lso ? in(text, "\"Hello, world\"") : in(text, "\"Hello, \" + \"world\"")));
            UNSCOPED_INFO("a loop that never ends by a jump back, not on Luau");
            REQUIRE((luau ? in(text, "while (TRUE)") : in(text, "@loop;") && in(text, "jump loop;")));
            // Its jump out and back made one jump back where the check fails.
            {
                INFO("what follows it still reached: " + text);
                REQUIRE(in(text, "llOwnerSay(\"done\");"));
                REQUIRE((luau || in(text, "jump loop;\n        llOwnerSay(\"done\");")));
            }
        }
        ALLSLOptimizer::Options o = options();
        o.addstrings              = true;
        UNSCOPED_INFO("joined on Mono where the option asks");
        CHECK(in(ALLSLOptimizer::run(source, o).text, "\"Hello, world\""));
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: string to integer casts fold as 32-bit hosts do and big JSON numbers are left", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        llOwnerSay((string)((integer)\"4294967296\"));\n"
                                   "        llOwnerSay((string)((integer)\"-4294967296\"));\n"
                                   "        llOwnerSay((string)((integer)\"0x1FFFFFFFF\"));\n"
                                   "        llOwnerSay((string)((integer)\"99999999999999999999\"));\n"
                                   "        llOwnerSay((string)((integer)\"3000000000\"));\n"
                                   "        llOwnerSay((string)((integer)\" 12abc\"));\n"
                                   "        llOwnerSay((string)((integer)\"0x10\"));\n"
                                   "        llOwnerSay((string)((integer)\"-0x10\"));\n"
                                   "        llOwnerSay(llList2CSV(llJson2List(\"[9999999999]\")) + llList2CSV(llJson2List(\"[2147483647, -2147483648]\")));\n"
                                   "    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono, ALLSLOptimizer::Target::Luau })
        {
            const std::string text = require_optimized(source, target);
            size_t minus_ones = 0;
            for (size_t at = text.find("llOwnerSay(\"-1\");"); at != std::string::npos; at = text.find("llOwnerSay(\"-1\");", at + 1))
            {
                ++minus_ones;
            }
            UNSCOPED_INFO("past 0xFFFFFFFF, -1, its minus or not: " + text);
            REQUIRE(minus_ones == 4);
            UNSCOPED_INFO("within it, wrapped below nought: " + text);
            REQUIRE(in(text, "llOwnerSay(\"-1294967296\");"));
            {
                INFO("spaces and what follows the number, hexadecimal only from a leading 0x: " + text);
                REQUIRE(in(text, "llOwnerSay(\"12\");"));
                REQUIRE(in(text, "llOwnerSay(\"16\");"));
                REQUIRE(in(text, "llOwnerSay(\"0\");"));
            }
            UNSCOPED_INFO("a JSON number past 32 bits left: " + text);
            REQUIRE(in(text, "llJson2List(\"[9999999999]\")"));
            {
                INFO("one within them folded: " + text);
                REQUIRE(in(text, "2147483647"));
                REQUIRE_FALSE(in(text, "llJson2List(\"[2147483647"));
            }
        }
    }

    TEST_CASE_METHOD(allsloptimizer_data, "allsloptimizer: float library functions fold where double and single agree and -0 becomes 0", "[allsloptimizer]")
    {
        const std::string source = "default\n{\n    touch_start(integer n)\n    {\n"
                                   "        llOwnerSay((string)llPow(2.0, 10.0));\n"
                                   "        llOwnerSay((string)llSqrt(2.0));\n"
                                   "        llOwnerSay((string)llAtan2(-0.0, 1.0));\n"
                                   "        llOwnerSay((string)llPow(-0.0, 3.0));\n"
                                   "        llOwnerSay((string)llSin(1.0));\n"
                                   "        llOwnerSay((string)llSin(0.1 + 0.2));\n"
                                   "        llOwnerSay((string)llVecMag(<1, 1, 0>));\n"
                                   "        llOwnerSay((string)llVecMag(<0, 3, 4>));\n"
                                   "    }\n}\n";
        for (const ALLSLOptimizer::Target target : { ALLSLOptimizer::Target::LSO, ALLSLOptimizer::Target::Mono })
        {
            const std::string text = require_optimized(source, target);
            {
                INFO("each folded as the VM's single: " + text);
                REQUIRE(in(text, "llOwnerSay(\"1024.000000\");"));
                REQUIRE(in(text, "llOwnerSay(\"1.414214\");"));
                REQUIRE(in(text, "llOwnerSay(\"0.841471\");"));
                REQUIRE(in(text, "llOwnerSay(\"0.295520\");"));
                REQUIRE(in(text, "llOwnerSay(\"5.000000\");"));
                REQUIRE_FALSE(in(text, "llSin"));
                REQUIRE_FALSE(in(text, "llVecMag"));
            }
            UNSCOPED_INFO("-0 made 0, not -0: " + text);
            REQUIRE_FALSE(in(text, "-0.000000"));
        }
        const std::string luau = require_optimized(source, ALLSLOptimizer::Target::Luau);
        {
            INFO("a single, not the double: " + luau);
            CHECK(in(luau, "(string)0.8414709568023682"));
            CHECK(in(luau, "(string)1.4142135381698608"));
        }
        {
            INFO("exact, folded: " + luau);
            CHECK(in(luau, "(string)1024.0"));
            CHECK(in(luau, "(string)5.0"));
        }
        UNSCOPED_INFO("-0 made 0: " + luau);
        CHECK_FALSE(in(luau, "-0.0"));
        UNSCOPED_INFO("sin of what is no single, which SLua takes as it is, left: " + luau);
        CHECK(in(luau, "llSin(0.30000000447034836)"));
        UNSCOPED_INFO("a length no single is, which the simulator gives by a way not known, left: " + luau);
        CHECK(in(luau, "llVecMag("));
    }
} // anonymous namespace
