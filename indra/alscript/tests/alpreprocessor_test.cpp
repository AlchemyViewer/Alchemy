/**
 * @file alpreprocessor_test.cpp
 * @brief The preprocessor, directive by directive, and its golden files.
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

#include "../lsl/allslservice.h"
#include "../preprocessor/alpreprocessor.h"

#include <catch2/catch_test_macros.hpp>

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace
{
    // What a run puts ahead of a script that requires modules: each
    // module's function by its number, in a table the `require` it sets
    // runs them from, as Luau's require does.
    const std::string REQUIRE_TAIL = "    }\n"
                                     "    local loaded = {}\n"
                                     "    function require(module)\n"
                                     "        local held = loaded[module]\n"
                                     "        if held == nil then\n"
                                     "            held = dangerouslyexecuterequiredmodule(modules[module])\n"
                                     "            loaded[module] = held\n"
                                     "        end\n"
                                     "        return held\n"
                                     "    end\n"
                                     "end\n";
    std::string bundleOf(const std::vector<std::string>& modules, const std::string& script)
    {
        std::string out = "do\n    local modules = {\n";
        for (size_t i = 0; i < modules.size(); ++i)
        {
            out += "        [" + std::to_string(i + 1) + "] = function()\n" + modules[i] + "        end,\n";
        }
        return out + REQUIRE_TAIL + script;
    }
}

namespace
{
    struct alpreprocessor_data
    {
        // A resolver over a table of named texts.
        std::map<std::string, ALPreprocessor::Include> files;
        std::set<std::string>                          pending;

        ALPreprocessor::Options options(bool lua = false)
        {
            ALPreprocessor::Options o;
            o.lua      = lua;
            o.fileName = lua ? "main.luau" : "main.lsl";
            o.unixTime = 1234567890;
            o.resolve  = [this](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
                if (pending.count(ask.name))
                {
                    return ALPreprocessor::Found::Pending;
                }
                auto it = files.find(ask.name);
                if (it == files.end())
                {
                    return ALPreprocessor::Found::No;
                }
                out = it->second;
                if (out.name.empty())
                {
                    out.name = ask.name;
                }
                return ALPreprocessor::Found::Yes;
            };
            return o;
        }

        void add(const std::string& name, const std::string& text, const std::string& assetId = std::string())
        {
            ALPreprocessor::Include inc;
            inc.text    = text;
            inc.name    = name;
            inc.assetId = assetId;
            files[name] = inc;
        }

        static std::string squeeze(const std::string& text)
        {
            std::string        out;
            std::istringstream in(text);
            std::string        line;
            while (std::getline(in, line))
            {
                if (!line.empty())
                {
                    out += line + "\n";
                }
            }
            return out;
        }

        static std::string readFile(const std::filesystem::path& path)
        {
            llifstream        in(path, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        // Every newline in a text made `ending`.
        static std::string ended(const std::string& text, std::string_view ending)
        {
            if (ending == "\n")
            {
                return text;
            }
            std::string out;
            out.reserve(text.size() + text.size() / 16);
            for (char c : text)
            {
                if (c == '\n')
                {
                    out += ending;
                }
                else
                {
                    out += c;
                }
            }
            return out;
        }

        // A golden file under tests/preprocessor/ run as the golden test runs
        // it -- its includes from include/, the transforms its first line asks
        // for -- with its text and its includes' ending their lines in
        // `ending`.
        ALPreprocessor::Result runGolden(const std::filesystem::path& file, std::string_view ending)
        {
            namespace fs                = std::filesystem;
            const fs::path    dir       = file.parent_path();
            const std::string source    = readFile(file);
            const bool              lua = file.extension() == ".luau";
            ALPreprocessor::Options o   = options(lua);
            o.fileName                  = file.filename().string();
            o.resolve                   = [dir, ending](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
                // A require names its module without the extension.
                for (const char* ext : { "", ".luau", ".lsl" })
                {
                    const fs::path path = dir / "include" / (ask.name + ext);
                    if (fs::exists(path))
                    {
                        out.text = ended(readFile(path), ending);
                        out.name = path.filename().string();
                        out.path = out.name;
                        return ALPreprocessor::Found::Yes;
                    }
                }
                return ALPreprocessor::Found::No;
            };
            // The first line may ask for the transforms.
            const std::string first = source.substr(0, source.find('\n'));
            o.switches              = first.find("switches") != std::string::npos;
            o.lazyLists             = first.find("lazylists") != std::string::npos;
            o.compress              = first.find("compress") != std::string::npos;
            return ALPreprocessor::run(ended(source, ending), o);
        }

        // The grid's builtins, which the optimizer and const folding read. A
        // test that needs them asks for them itself: Tailslide keeps them for
        // the process once loaded, so whichever asks first loads them.
        static void requireGridBuiltins()
        {
            ALLSLService service;
            std::string  error;
            const bool   loaded = service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
            INFO("builtins: " << error);
            REQUIRE(loaded);
        }

        // The same but the notes of a require nothing reads, which a script
        // made to try something else may leave about (the last case here tries them).
        static std::string faults(const ALPreprocessor::Result& r)
        {
            ALPreprocessor::Result kept;
            for (const ALScriptProblem& p : r.problems)
            {
                if (p.key != "PreprocRequireUnread")
                {
                    kept.problems.push_back(p);
                }
            }
            return messages(kept);
        }
        static std::string messages(const ALPreprocessor::Result& r)
        {
            std::string out;
            for (const ALScriptProblem& p : r.problems)
            {
                out += (p.severity == ALScriptProblem::Severity::Error ? "E " : p.severity == ALScriptProblem::Severity::Warning ? "W " : "N ");
                if (!p.file.empty())
                {
                    out += p.file + ":";
                }
                out += std::to_string(p.line) + ": " + p.message + "\n";
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: object-like and function-like macros expand in code and nowhere else", "[alpreprocessor]")
    {
        const std::string source = "#define GREETING \"hello\"\n"
                                   "#define TWICE(x) x + x\n"
                                   "default { state_entry() { llSay(0, GREETING); integer y = TWICE(2); string s = \"GREETING\"; /* GREETING */ } }";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(r.text ==
                      std::string("\n\ndefault { state_entry() { llSay(0, \"hello\"); integer y = 2 + 2; string s = \"GREETING\"; /* GREETING */ } }"));
        CHECK_FALSE(r.disabled);

        // A function-like macro's name on its own is only a name, and its
        // arguments may run over lines.
        r = ALPreprocessor::run("#define M(a, b) a+b\nM + 1\nM(1,\n 2)\n", options());
        UNSCOPED_INFO("name alone");
        CHECK(r.text == std::string("\nM + 1\n1+2\n"));
        CHECK(messages(r) == std::string());

        // `#define` needs a name; an unknown directive is an error; a
        // redefinition is a warning; `#undef` takes it away.
        r = ALPreprocessor::run("#define\n#foo\n#define A 1\n#define A 2\nA\n#undef A\nA\n", options());
        CHECK(messages(r) == std::string("E 0: macro name missing in #define\nE 1: ill formed preprocessor directive '#foo'\nW 3: macro 'A' redefined\n"));
        UNSCOPED_INFO("undef");
        CHECK(squeeze(r.text) == std::string("2\nA\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: variadic macros with stringizing and pasting", "[alpreprocessor]")
    {
        const std::string source = "#define STR(x) #x\n"
                                   "#define CAT(a, b) a ## b\n"
                                   "#define LOG(...) llOwnerSay(#__VA_ARGS__)\n"
                                   "#define TAIL(first, ...) first __VA_ARGS__\n"
                                   "STR(hello   world)\n"
                                   "STR(\"q\\n\")\n"
                                   "CAT(ll, Say)(0, \"x\")\n"
                                   "LOG(a, b)\n"
                                   "CAT(, x) CAT(x,) CAT(1, 2)\n"
                                   "STR(__LINE__)\n"
                                   "TAIL(1) TAIL(1, 2, 3)\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(squeeze(r.text) ==
                      std::string("\"hello world\"\n"
                                  "\"\\\"q\\\\n\\\"\"\n"
                                  "llSay(0, \"x\")\n"
                                  "llOwnerSay(\"a, b\")\n"
                                  "x x 12\n"
                                  "\"__LINE__\"\n"
                                  "1  1 2, 3\n"));

        r = ALPreprocessor::run("#define P(a) ## a\n#define Q(a) # b\n#define R(a, a) a\nP(1)\n", options());
        UNSCOPED_INFO("refused");
        CHECK(messages(r) ==
                      std::string("E 0: '##' cannot be at either end of a macro body\n"
                                  "E 1: '#' is not followed by a macro parameter\n"
                                  "E 2: duplicate macro parameter 'a'\n"));
        UNSCOPED_INFO("nothing defined");
        CHECK(squeeze(r.text) == std::string("P(1)\n"));

        r = ALPreprocessor::run("#define M(a, b) a b\nM(1)\nM(1, 2, 3)\nM(1\n", options());
        UNSCOPED_INFO("arity");
        CHECK(messages(r) ==
                      std::string("E 1: too few arguments for macro 'M'\nE 2: too many arguments for macro 'M'\nE 3: unterminated argument list invoking macro 'M'\n"));
        UNSCOPED_INFO("left as written");
        CHECK(squeeze(r.text) == std::string("M(1)\nM(1, 2, 3)\nM(1\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: rescanning stops where the standard says", "[alpreprocessor]")
    {
        const std::string source = "#define f(a) a*g\n"
                                   "#define g(a) f(a)\n"
                                   "#define foo foo\n"
                                   "#define AB a B\n"
                                   "#define B b AB\n"
                                   "#define I(x) x\n"
                                   "f(2)(9)\n"
                                   "foo\n"
                                   "AB\n"
                                   "I(I)(3)\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(squeeze(r.text) == std::string("2*9*g\nfoo\na b AB\nI(3)\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: conditionals over C integer expressions keep their lines", "[alpreprocessor]")
    {
        const std::string source = "#define A 1\n"
                                   "#define B 0\n"
                                   "#if A && !B\n"
                                   "yes\n"
                                   "#elif B\n"
                                   "no\n"
                                   "#else\n"
                                   "no\n"
                                   "#endif\n"
                                   "#ifdef C\n"
                                   "no\n"
                                   "#endif\n"
                                   "#ifndef C\n"
                                   "yes2\n"
                                   "#endif\n"
                                   "#if defined(A) && defined C == 0\n"
                                   "yes3\n"
                                   "#endif\n"
                                   "#if (1 << 3) == 8 && 7 / 2 == 3 && -1 < 0 && 0x10 == 16 && (A ? 4 : 5) == 4 && 010 == 8 && ~0 == -1\n"
                                   "yes4\n"
                                   "#endif\n"
                                   "#if UNDEFINED_NAME + 1 == 1\n"
                                   "yes5\n"
                                   "#if 0\n"
                                   "no\n"
                                   "#elif 1\n"
                                   "yes6\n"
                                   "#else\n"
                                   "no\n"
                                   "#endif\n"
                                   "#endif\n"
                                   "#if 0\n"
                                   "#if 1\n"
                                   "no\n"
                                   "#endif\n"
                                   "#endif\n"
                                   "end\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(squeeze(r.text) == std::string("yes\nyes2\nyes3\nyes4\nyes5\nyes6\nend\n"));
        std::istringstream in(r.text);
        std::string        line;
        S32                n    = 0;
        S32                yes4 = -1;
        while (std::getline(in, line))
        {
            if (line == "yes4")
            {
                yes4 = n;
            }
            ++n;
        }
        UNSCOPED_INFO("one line each");
        CHECK(n == 37);
        UNSCOPED_INFO("yes4 on its line");
        CHECK(yes4 == 19);
        ALSourceMap::Loc loc = r.map.toSource(19, 0);
        UNSCOPED_INFO("yes4 from line 19");
        CHECK(loc.line == 19);

        r = ALPreprocessor::run("#if 1\n#else\n#else\n#endif\n#endif\n#elif 1\n#if 1/0\n#endif\n#if 1 +\n#endif\n#if (1\n#endif\n#if 1\n", options());
        CHECK(messages(r) ==
                      std::string("E 2: #else after #else\n"
                                  "E 4: #endif without #if\n"
                                  "E 5: #elif without #if\n"
                                  "E 6: division by zero in preprocessor expression\n"
                                  "E 8: expected a value in preprocessor expression\n"
                                  "E 10: expected ')' in preprocessor expression\n"
                                  "E 12: #if without #endif at the end of the file\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: includes come from the resolver once if asked and name their own file", "[alpreprocessor]")
    {
        add("a.lsl", "integer a = __LINE__; string f = __SHORTFILE__; string id = __ASSETID__;\n", "aaaa");
        add("once.lsl", "#pragma once\nonce\n");
        add("deep.lsl", "#include \"deep.lsl\"\n");
        add("err.lsl", "#error bad\n");
        add("mac.lsl", "#define FROM_INC 7\n");
        pending.insert("pending.lsl");
        const std::string source = "#include \"a.lsl\"\n"
                                   "string top = __SHORTFILE__; string tid = __ASSETID__;\n"
                                   "#include \"once.lsl\"\n"
                                   "#include \"once.lsl\"\n"
                                   "#include <pending.lsl>\n"
                                   "#include \"missing.lsl\"\n"
                                   "#define NAME \"mac.lsl\"\n"
                                   "#include NAME\n"
                                   "FROM_INC\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string("E 5: could not find include file 'missing.lsl'\n"));
        CHECK(r.text ==
                      std::string("\ninteger a = 1; string f = \"a.lsl\"; string id = \"aaaa\";\n"
                                  "string top = \"main.lsl\"; string tid = \"NOT_IN_WORLD\";\n"
                                  "\n\nonce\n"
                                  "\n\n\n\n\n\n7\n"));
        REQUIRE(r.includes.size() == 3u);
        CHECK(r.includes[0] == std::string("a.lsl"));
        REQUIRE(r.pending.size() == 1u);
        CHECK(r.pending[0] == std::string("pending.lsl"));
        REQUIRE(r.map.files().size() == 4u);
        CHECK(r.map.files()[0].name == std::string("main.lsl"));
        UNSCOPED_INFO("an include by its identity");
        CHECK(r.map.fileOf("once.lsl") == 2);

        r = ALPreprocessor::run("#include \"deep.lsl\"\n", options());
        REQUIRE(messages(r).find("nested too deeply") != std::string::npos);
        UNSCOPED_INFO("in the include");
        CHECK(r.problems.front().file == "deep.lsl");

        r = ALPreprocessor::run("#include \"err.lsl\"\nafter\n", options());
        UNSCOPED_INFO("error in an include");
        CHECK(messages(r) == std::string("E err.lsl:0: bad\n"));
        UNSCOPED_INFO("goes on");
        CHECK(squeeze(r.text) == std::string("after\n"));

        ALPreprocessor::Options none = options();
        none.resolve                 = nullptr;
        r                            = ALPreprocessor::run("#include \"a.lsl\"\n", none);
        UNSCOPED_INFO("no resolver");
        CHECK(messages(r) == std::string("E 0: could not find include file 'a.lsl'\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: Firestorm quirks: multiline strings; off switch; #line; predefines", "[alpreprocessor]")
    {
        ALPreprocessor::Result r = ALPreprocessor::run("string s = \"a\nb\";\nx", options());
        UNSCOPED_INFO("a newline in a string");
        CHECK(r.text == std::string("string s = \"a\\nb\"\n;\nx"));

        const std::string off = "//fspreprocessor off\n#define X 1\nX\n";
        r                     = ALPreprocessor::run(off, options());
        CHECK(r.disabled);
        UNSCOPED_INFO("as it was");
        CHECK(r.text == off);
        UNSCOPED_INFO("maps to itself");
        CHECK(r.map.toSource(2, 0).line == 2);

        r = ALPreprocessor::run("#line 5 \"x\"\ny\n", options());
        UNSCOPED_INFO("line passed through");
        CHECK(r.text == std::string("//#line 5 \"x\"\ny\n"));

        ALPreprocessor::Options o = options();
        o.agentId                 = "1234";
        o.agentName               = "Some Body";
        o.assetId                 = "asset-1";
        r = ALPreprocessor::run("__LINE__ __FILE__ __SHORTFILE__ __ASSETID__\n__AGENTKEY__ __AGENTID__ __AGENTIDRAW__ __AGENTNAME__ __UNIXTIME__\n"
                                "integer(1.5) list(1, 2) key(x)\n__DATE__ __TIME__\n",
                                o);
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("line one");
        CHECK(r.text.substr(0, r.text.find('\n')) == std::string("1 \"main.lsl\" \"main.lsl\" \"asset-1\""));
        std::istringstream in(r.text);
        std::string        line;
        std::getline(in, line);
        std::getline(in, line);
        UNSCOPED_INFO("line two");
        CHECK(line == std::string("\"1234\" \"1234\" 1234 \"Some Body\" 1234567890"));
        std::getline(in, line);
        UNSCOPED_INFO("line three");
        CHECK(line == std::string("((integer)(1.5)) ((list)(1, 2)) ((key)(x))"));
        std::getline(in, line);
        UNSCOPED_INFO("date and time");
        REQUIRE(line.size() == 13u + 1u + 10u);
        {
            INFO("date quoted");
            CHECK(line[0] == '"');
            CHECK(line[12] == '"');
        }
        {
            INFO("time quoted");
            CHECK(line[14] == '"');
            CHECK(line[23] == '"');
        }
        UNSCOPED_INFO("time colons");
        CHECK(line.substr(17, 1) + line.substr(20, 1) == std::string("::"));

        r = ALPreprocessor::run("#define BS 1 \\\n + 2\nBS\n", options());
        UNSCOPED_INFO("continuation");
        CHECK(squeeze(r.text) == std::string("1 + 2\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: switch becomes the jump table Firestorm emits", "[alpreprocessor]")
    {
        const std::string source = "#define USE_SWITCHES\n"
                                   "switch (x) {\n"
                                   "case 1: llSay(0, \"one\"); break;\n"
                                   "case 2 { llSay(0, \"two\"); }\n"
                                   "default: llSay(0, \"other\");\n"
                                   "}\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(r.usedSwitches);
        CHECK(r.text ==
                      std::string("\n{if((x) == (1))jump _sw1_1;\n"
                                  "if((x) == (2))jump _sw1_2;\n"
                                  "jump _sw1_default;\n"
                                  "\n@_sw1_1; llSay(0, \"one\"); jump _sw1_end;\n"
                                  "@_sw1_2;{ llSay(0, \"two\"); }\n"
                                  "@_sw1_default; llSay(0, \"other\");\n"
                                  "\n@_sw1_end;\n}\n"));

        // Nested, and without a default: the fall-through jumps past the end.
        ALPreprocessor::Options o = options();
        o.switches                = true;
        r = ALPreprocessor::run("switch (a) { case 1: switch (b) { case 2: break; } break; }\n", o);
        UNSCOPED_INFO("nested");
        CHECK(r.text ==
                      std::string("{if((a) == (1))jump _sw2_1;\njump _sw2_end;\n"
                                  " @_sw2_1; {if((b) == (2))jump _sw1_1;\njump _sw1_end;\n"
                                  " @_sw1_1; jump _sw1_end; \n@_sw1_end;\n} jump _sw2_end; \n@_sw2_end;\n}\n"));
        UNSCOPED_INFO("not used when not asked");
        CHECK(ALPreprocessor::run("switch (a) { }\n", options()).text == std::string("switch (a) { }\n"));
        r = ALPreprocessor::run("switch (a) { case 1 }\n", o);
        UNSCOPED_INFO("a case with no end");
        CHECK(messages(r) == std::string("E 0: cannot find ':' or '{' after case\n"));
        // A value worked out again for each case, said where that is more
        // than a cost: a call, or one that changes something.
        const std::string warned = "W 0: the switch's value is worked out again for each case: put it in a local first\n";
        UNSCOPED_INFO("a call");
        CHECK(messages(ALPreprocessor::run("switch ((integer)llFrand(3)) { case 1: break; }\n", o)) == warned);
        UNSCOPED_INFO("an increment");
        CHECK(messages(ALPreprocessor::run("switch (i++) { case 1: break; }\n", o)) == warned);
        UNSCOPED_INFO("an assignment");
        CHECK(messages(ALPreprocessor::run("switch (i = 2) { case 1: break; }\n", o)) == warned);
        UNSCOPED_INFO("not a name");
        CHECK(messages(ALPreprocessor::run("switch (i) { case 1: break; }\n", o)) == std::string());
        UNSCOPED_INFO("nor arithmetic or a field");
        CHECK(messages(ALPreprocessor::run("switch ((i + 1) * v.x) { case 1: break; }\n", o)) == std::string());
        UNSCOPED_INFO("nor a comparison");
        CHECK(messages(ALPreprocessor::run("switch (i == 2) { case 1: break; }\n", o)) == std::string());
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: lazy lists read and write through the helper", "[alpreprocessor]")
    {
        const std::string source = "#define USE_LAZY_LISTS\n"
                                   "list l; integer i = (integer)l[2]; l[0] = 5; string s = (string)(l[(integer)l[1]]); rotation r = (rotation)l[0];\n"
                                   "x = (integer)(l[0]; f((float)l[0], l[1] = 2);\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(r.usedLazyLists);
        const std::string helper = "list lazy_list_set(list L, integer i, list v)\n{\n    while (llGetListLength(L) < i)\n        L = L + 0;\n    return llListReplaceList(L, v, i, i);\n}\n\n";
        CHECK(r.text ==
                      helper + "\nlist l; integer i = llList2Integer(l, 2); l = lazy_list_set(l,0,[5]); string s = llList2String(l, llList2Integer(l, 1)); rotation r = llList2Rot(l, 0);\n"
                               "x = (integer)(l[0]; f(llList2Float(l, 0), l = lazy_list_set(l,1,[2]));\n");
        ALPreprocessor::Options o = options();
        o.lazyLists               = true;
        r                         = ALPreprocessor::run("integer a = b == c;\n", o);
        UNSCOPED_INFO("nothing to do");
        CHECK_FALSE(r.usedLazyLists);
        UNSCOPED_INFO("untouched");
        CHECK(r.text == std::string("integer a = b == c;\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: compression drops comments and every blank that is not needed", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options();
        o.compress                = true;
        ALPreprocessor::Result r =
            ALPreprocessor::run("// comment\ndefault   {  state_entry ( ) { integer a = - -1 ; /* x */ llSay( 0 , \"a  b\" ) ; a ++ ; float f = 1 . 5; } }\n", o);
        CHECK(messages(r) == std::string());
        CHECK(r.text == std::string("default{state_entry(){integer a=- -1;llSay(0,\"a  b\");a++;float f=1 . 5;}}\n"));
        r = ALPreprocessor::run("a\n\n\nb / /* c */ d\n", o);
        UNSCOPED_INFO("lines kept, not multiplied");
        CHECK(r.text == std::string("a\nb/d\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: strings and comments and # kept; require gathered into modules", "[alpreprocessor]")
    {
        const std::string source = "--#define N 3\n"
                                   "local s = [[N]] -- N\n"
                                   "local t = \"N\" .. #N_list .. 'N' .. `N{N}`\n"
                                   "--[==[ N\nN ]==]\n"
                                   "print(N)\n"
                                   "--#line 3\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options(true));
        CHECK(messages(r) == std::string());
        CHECK(r.text ==
                      std::string("\nlocal s = [[N]] -- N\nlocal t = \"N\" .. #N_list .. 'N' .. `N{N}`\n--[==[ N\nN ]==]\nprint(3)\n--#line 3\n"));

        add("a", "local b = require(\"b\")\nreturn { b = b }\n");
        add("b", "return 42\n");
        add("c", "return require(\"c\")\n");
        r = ALPreprocessor::run("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n", options(true));
        CHECK(faults(r) == std::string());
        // Numbered as met: a, then what a requires.
        const std::string bundled = bundleOf({ "local b = require(2)\nreturn { b = b }\n", "return 42\n" },
                                             "local a = require(1)\nlocal b = require(2)\nlocal x = require(name)\n");
        CHECK(r.text == bundled);
        REQUIRE(r.includes.size() == 2u);
        const ALSourceMap::Loc in_b = r.map.toSource(7, 0);
        {
            INFO("b mapped to its file");
            REQUIRE(in_b.found());
            CHECK(r.map.files()[in_b.file].name == std::string("b"));
        }
        // The modules' own lines, which the studio shows nothing of: not
        // the table's, which are no one's, nor the script's.
        std::string others;
        for (const auto& [first, last] : r.map.othersLines())
        {
            others += std::to_string(first) + "-" + std::to_string(last) + " ";
        }
        UNSCOPED_INFO("the modules' lines");
        CHECK(others == std::string("3-4 7-7 "));
        UNSCOPED_INFO("not apart unless asked");
        CHECK_FALSE(r.apart.valid);
        // Apart, for the analyzers: the script with its requires as calls,
        // and each module as the run made it, each mapped to its file; the
        // text the bundle still.
        ALPreprocessor::Options apart = options(true);
        apart.apart                   = true;
        const ALPreprocessor::Result split =
            ALPreprocessor::run("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n", apart);
        UNSCOPED_INFO("the bundle as ever");
        CHECK(split.text == bundled);
        CHECK(split.apart.valid);
        REQUIRE(split.apart.modules.size() == 2);
        UNSCOPED_INFO("the script, its requires calls");
        CHECK(split.apart.script.text ==
                      std::string("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n"));
        UNSCOPED_INFO("the first reached");
        CHECK(split.apart.modules[0].key == std::string("a"));
        UNSCOPED_INFO("as made, its own require a call");
        CHECK(split.apart.modules[0].text == std::string("local b = require(\"b\")\nreturn { b = b }\n"));
        UNSCOPED_INFO("then what it reached");
        CHECK(split.apart.modules[1].key == std::string("b"));
        CHECK(split.apart.modules[1].text == std::string("return 42\n"));
        const ALSourceMap::Loc in_a = split.apart.modules[0].map.toSource(1, 0);
        {
            INFO("mapped to its file");
            REQUIRE(in_a.found());
            CHECK(split.apart.modules[0].map.files()[in_a.file].name == "a");
            CHECK(in_a.line == 1);
        }
        UNSCOPED_INFO("the script to its own");
        CHECK(split.apart.script.map.toSource(2, 6).file == 0);
        r = ALPreprocessor::run("local c = require(\"c\")\n", options(true));
        UNSCOPED_INFO("a cycle");
        CHECK(messages(r) == std::string("E c:0: 'c' requires itself\n"));
        r = ALPreprocessor::run("local d = require(\"d\")\n", options(true));
        UNSCOPED_INFO("missing");
        CHECK(messages(r) == std::string("E 0: could not find module 'd'\n"));
        UNSCOPED_INFO("left alone");
        CHECK(r.text == std::string("local d = require(\"d\")\n"));

        r = ALPreprocessor::run("--fspreprocessor off\n--#define X\n", options(true));
        UNSCOPED_INFO("off in Lua");
        CHECK(r.disabled);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: the source map goes both ways", "[alpreprocessor]")
    {
        add("inc", "x;\n");
        ALPreprocessor::Result r = ALPreprocessor::run("#define X 10\ninteger a = X + 1;\n#include \"inc\"\ny;\n", options());
        CHECK(messages(r) == std::string());
        CHECK(r.text == std::string("\ninteger a = 10 + 1;\n\nx;\ny;\n"));
        ALSourceMap::Loc loc = r.map.toSource(1, 0);
        REQUIRE(loc.found());
        UNSCOPED_INFO("integer");
        CHECK(loc.line * 1000 + loc.column == 1000);
        loc = r.map.toSource(1, 5);
        UNSCOPED_INFO("within integer");
        CHECK(loc.column == 5);
        loc = r.map.toSource(1, 12);
        UNSCOPED_INFO("the 10 is the X");
        CHECK(loc.column == 12);
        loc = r.map.toSource(1, 13);
        UNSCOPED_INFO("still the X");
        CHECK(loc.column == 12);
        loc = r.map.toSource(1, 15);
        UNSCOPED_INFO("the + past the X");
        CHECK(loc.column == 14);
        loc = r.map.toSource(1, 40);
        UNSCOPED_INFO("past the end");
        CHECK(loc.column == 18);
        loc = r.map.toSource(0, 0);
        UNSCOPED_INFO("a directive line has no origin");
        REQUIRE_FALSE(loc.found());
        loc = r.map.toSource(3, 0);
        UNSCOPED_INFO("from the include");
        CHECK(loc.file == 1);
        UNSCOPED_INFO("its line");
        CHECK(loc.line == 0);
        loc = r.map.toSource(4, 0);
        UNSCOPED_INFO("after the include");
        CHECK(loc.file * 1000 + loc.line == 3);

        loc = r.map.toExpanded(0, 1, 12);
        UNSCOPED_INFO("forward found");
        REQUIRE(loc.found());
        UNSCOPED_INFO("X went to line 1");
        CHECK(loc.line == 1);
        UNSCOPED_INFO("X went to column 12");
        CHECK(loc.column == 12);
        loc = r.map.toExpanded(0, 1, 3);
        UNSCOPED_INFO("within integer forward");
        CHECK(loc.column == 3);
        loc = r.map.toExpanded(0, 0, 3);
        UNSCOPED_INFO("a directive made nothing");
        REQUIRE_FALSE(loc.found());
        loc = r.map.toExpanded(0, 3, 0);
        UNSCOPED_INFO("y went past the include");
        CHECK(loc.line == 4);
        loc = r.map.toExpanded(1, 0, 1);
        UNSCOPED_INFO("the include's x");
        CHECK(loc.line == 3);
        loc = r.map.toExpanded(0, 1, 100);
        UNSCOPED_INFO("past the end forward");
        CHECK(loc.column == 19);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: every golden file comes out as its expected text", "[alpreprocessor]")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fsyspath(AL_ALSCRIPT_TEST_DIR) / "preprocessor";
        S32            checked = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir))
        {
            const std::string name = entry.path().filename().string();
            if (name.compare(0, 5, "test_") != 0 || name.find("_expected") != std::string::npos)
            {
                continue;
            }
            const std::string stem     = entry.path().stem().string();
            const fs::path    expected = dir / (stem + "_expected" + entry.path().extension().string());
            ALPreprocessor::Result r   = runGolden(entry.path(), "\n");
            CAPTURE(name);
            REQUIRE(messages(r) == std::string());
            // With AL_PREPROCESSOR_WRITE_EXPECTED set, the run writes the
            // expected files instead of checking them: for a new golden
            // file, or a change that was meant, each to be read over
            // before it is kept.
            if (const char* write = std::getenv("AL_PREPROCESSOR_WRITE_EXPECTED"); write && *write)
            {
                llofstream out(expected, std::ios::binary);
                out << r.text;
                continue;
            }
            REQUIRE(fs::exists(expected));
            REQUIRE(r.text == readFile(expected));
            ++checked;
        }
        UNSCOPED_INFO("some golden files");
        REQUIRE((checked > 0 || std::getenv("AL_PREPROCESSOR_WRITE_EXPECTED")));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: the optimizer runs on expanded text and its notes map to the source", "[alpreprocessor]")
    {
        requireGridBuiltins();
        add("consts.lsl", "#define CHANNEL 7\ninteger unused = 1;\n");
        ALPreprocessor::Options o = options();
        o.optimize                = true;
        o.compress                = true;
        const std::string source  = "#include \"consts.lsl\"\n#define TWICE(x) ((x) * 2)\ndefault\n{\n    state_entry()\n    {\n        llSay(CHANNEL, (string)TWICE(21));\n    }\n}\n";
        ALPreprocessor::Result r  = ALPreprocessor::run(source, o);
        CHECK(r.optimized);
        CHECK(r.text == std::string("default\n{\nstate_entry()\n{\nllSay(7,\"42\");\n}\n}\n"));
        // The unused global was in the include, and the note says so.
        bool noted = false;
        for (const ALScriptProblem& p : r.problems)
        {
            if (p.message.find("removed the unused global unused") != std::string::npos)
            {
                noted = true;
                UNSCOPED_INFO("in the include");
                REQUIRE(p.file == std::string("consts.lsl"));
                UNSCOPED_INFO("on its line there");
                REQUIRE(p.line == 1);
            }
        }
        CHECK(noted);
        // The llSay is on output line 4, which came from source line 6.
        const ALSourceMap::Loc loc = r.map.toSource(4, 0);
        REQUIRE(loc.found());
        UNSCOPED_INFO("main file");
        CHECK(loc.file == 0);
        CHECK(loc.line == 6);
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: break and continue become jumps; &= |= ^= <<= >>= become assignments", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("while (a) { if (b) break; if (c) continue; d++; }\n", o);
        CHECK(messages(r) == std::string());
        CHECK(r.usedExtensions);
        UNSCOPED_INFO("a while");
        CHECK(r.text == std::string("while (a) { if (b) jump _brk1; if (c) jump _cnt1; d++; @_cnt1;}@_brk1;\n"));
        r = ALPreprocessor::run("for (i = 0; i < 3; i++) if (i == 1) continue; else x++;\n", o);
        UNSCOPED_INFO("a for with one statement for a body gets braces");
        CHECK(r.text == std::string("for (i = 0; i < 3; i++) { if (i == 1) jump _cnt1; else x++;@_cnt1;}\n"));
        r = ALPreprocessor::run("do { if (x) break 2; } while (y);\n", o);
        UNSCOPED_INFO("break 2 with one loop is an error");
        CHECK(messages(r) == std::string("E 0: break outside a loop\n"));
        r = ALPreprocessor::run("while (a) { do { if (x) break 2; if (y) break; } while (b); c++; }\n", o);
        UNSCOPED_INFO("break 2 goes past the outer loop, break past the inner");
        CHECK(r.text ==
                      std::string("while (a) { do { if (x) jump _brk1; if (y) jump _brk2; } while (b);@_brk2; c++; }@_brk1;\n"));
        r = ALPreprocessor::run("continue;\n", o);
        UNSCOPED_INFO("outside a loop");
        CHECK(messages(r) == std::string("E 0: continue outside a loop\n"));
        o.switches = true;
        r          = ALPreprocessor::run("while (a) { switch (b) { case 1: break; } while (c) { break; } }\n", o);
        UNSCOPED_INFO("a break in a switch is the switch's, one in a loop inside it the loop's");
        CHECK(r.text ==
                      std::string("while (a) { {if((b) == (1))jump _sw1_1;\njump _sw1_end;\n @_sw1_1; jump _sw1_end; \n@_sw1_end;\n} while (c) { jump _brk2; }@_brk2; }\n"));
        r = ALPreprocessor::run("x &= 6; v.y |= 1 << n; z ^= (a | b); f(k <<= 2, 3); m >>= 1 + p;\n", o);
        UNSCOPED_INFO("the assignments");
        CHECK(r.text == std::string("x = x & (6); v.y = v.y | (1 << n); z = z ^ ((a | b)); f(k = k << (2), 3); m = m >> (1 + p);\n"));
        UNSCOPED_INFO("not used when not asked");
        CHECK(ALPreprocessor::run("while (a) break;\n", options()).text == std::string("while (a) break;\n"));
        r = ALPreprocessor::run("#define USE_EXTENSIONS\nwhile (a) break;\n", options());
        UNSCOPED_INFO("USE_EXTENSIONS turns it on");
        CHECK(r.text == std::string("\nwhile (a) jump _brk1;@_brk1;\n"));
        r = ALPreprocessor::run("inline f() { }\ninline integer g(integer x) { return x; }\ninline = 3;\n", o);
        UNSCOPED_INFO("inline before a function is taken off and the name kept");
        CHECK(r.text == std::string("f() { }\ninteger g(integer x) { return x; }\ninline = 3;\n"));
        REQUIRE(r.inlined.size() == 2);
        CHECK(r.inlined[0] == "f");
        CHECK(r.inlined[1] == "g");
        r = ALPreprocessor::run("integer break; while (a) { break = 1; x = continue; }\n", o);
        UNSCOPED_INFO("the words as names are errors, not rewrites");
        CHECK(messages(r) ==
                      std::string("E 0: 'break' is a name here, which break and continue reserve\n"
                                  "E 0: 'break' is a name here, which break and continue reserve\n"
                                  "E 0: 'continue' is a name here, which break and continue reserve\n"));
        UNSCOPED_INFO("and left as they were");
        CHECK(r.text == std::string("integer break; while (a) { break = 1; x = continue; }\n"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a label the script has where a loop's would go is the loop's", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("while (a) { if (b) break; c++; }\n@out;\n", o);
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("a label after the loop is the break's");
        CHECK(r.text == std::string("while (a) { if (b) jump out; c++; }\n@out;\n"));
        r = ALPreprocessor::run("while (a) { if (b) continue; c++; @next; }\n", o);
        UNSCOPED_INFO("a label ending the body is the continue's");
        CHECK(r.text == std::string("while (a) { if (b) jump next; c++; @next; }\n"));
        r = ALPreprocessor::run("do { if (b) break 2; } while (a); @x;\n", o);
        UNSCOPED_INFO("break 2 with one loop is still an error");
        CHECK(messages(r) == std::string("E 0: break outside a loop\n"));
        r = ALPreprocessor::run("for (;;) { while (a) { if (b) break 2; } @in; }\n@after;\n", o);
        UNSCOPED_INFO("each loop's own: break 2 to the outer's following label, the inner's ending label unused");
        CHECK(r.text ==
                      std::string("for (;;) { while (a) { if (b) jump after; } @in; }\n@after;\n"));
        r = ALPreprocessor::run("while (a) { break; } x = 1; @late;\n", o);
        UNSCOPED_INFO("a label that is not right after the loop is not the loop's");
        CHECK(r.text == std::string("while (a) { jump _brk1; }@_brk1; x = 1; @late;\n"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a preprocessor problem carries a key and its words for translation", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("continue;\n#define f(a, a) a\n", o);
        // The directives go first, the extensions after: the macro's
        // problem is said before the loop's.
        REQUIRE(r.problems.size() == size_t(2));
        CHECK(r.problems[1].message == std::string("continue outside a loop"));
        CHECK(r.problems[1].key == std::string("PreprocOutsideLoop"));
        {
            INFO("the word it was built with");
            REQUIRE(r.problems[1].args.size() == 1);
            CHECK(r.problems[1].args[0] == "continue");
        }
        UNSCOPED_INFO("the macro's key");
        CHECK(r.problems[0].key == std::string("PreprocDuplicateParameter"));
        {
            INFO("and its word");
            REQUIRE(r.problems[0].args.size() == 1);
            CHECK(r.problems[0].args[0] == "a");
        }
        UNSCOPED_INFO("a translation is the text with the words put in");
        CHECK(ALScriptProblem::fill("[1] hors d'une boucle ([1], [2])", { "continue", "x" }) == std::string("continue hors d'une boucle (continue, x)"));
        UNSCOPED_INFO("a word with a mark in it is not read again");
        CHECK(ALScriptProblem::fill("[1]", { "[2]" }) == std::string("[2]"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a run is bounded: doubling macros stopped; deep expressions refused", "[alpreprocessor]")
    {
        // Hide sets stop a macro expanding as itself, not one that
        // doubles: this is 2^24 tokens and would otherwise eat the
        // machine, on the thread that opened the script.
        std::string bomb = "#define A0 x\n";
        for (int i = 1; i <= 24; ++i)
        {
            bomb += "#define A" + std::to_string(i) + " A" + std::to_string(i - 1) + " A" + std::to_string(i - 1) + "\n";
        }
        bomb += "A24\n";
        ALPreprocessor::Options o = options();
        o.tokenBudget             = 100000;
        ALPreprocessor::Result r  = ALPreprocessor::run(bomb, o);
        UNSCOPED_INFO("the run said it ran away");
        CHECK(r.overran);
        UNSCOPED_INFO("and gave the source back as it was");
        CHECK(r.text == bomb);
        {
            INFO("with the reason");
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocTooMuch"));
        }
        CHECK(r.problems.back().severity == ALScriptProblem::Severity::Error);
        // A run within the budget is untouched by it.
        ALPreprocessor::Result fine = ALPreprocessor::run("#define A 1\ninteger n = A;\n", o);
        UNSCOPED_INFO("nothing said");
        CHECK_FALSE(fine.overran);
        UNSCOPED_INFO("and the macro put in");
        REQUIRE(fine.text.find("integer n = 1;") != std::string::npos);
        // An expression nested past the depth is refused rather than
        // taking the C++ stack down with it.
        std::string deep = "#if ";
        for (int i = 0; i < 200; ++i)
        {
            deep += "(";
        }
        deep += "1";
        for (int i = 0; i < 200; ++i)
        {
            deep += ")";
        }
        deep += "\ninteger n;\n#endif\n";
        ALPreprocessor::Options d = options();
        d.expressionDepth         = 16;
        ALPreprocessor::Result nested = ALPreprocessor::run(deep, d);
        REQUIRE_FALSE(nested.problems.empty());
        CHECK(nested.problems.front().key == std::string("PreprocExpressionTooDeep"));
        // And one within the depth is answered.
        ALPreprocessor::Result shallow = ALPreprocessor::run("#if ((((1))))\ninteger n;\n#endif\n", d);
        REQUIRE(shallow.problems.empty());
        UNSCOPED_INFO("the group taken");
        REQUIRE(shallow.text.find("integer n;") != std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: an include that includes itself stops at the depth and is listed once", "[alpreprocessor]")
    {
        add("a.lsl", "#include \"b.lsl\"\n");
        add("b.lsl", "#include \"a.lsl\"\n");
        ALPreprocessor::Options o = options();
        o.includeDepth            = 8;
        ALPreprocessor::Result r  = ALPreprocessor::run("#include \"a.lsl\"\n", o);
        bool                   deep = false;
        for (const ALScriptProblem& p : r.problems)
        {
            deep = deep || p.key == "PreprocIncludeTooDeep";
        }
        CHECK(deep);
        UNSCOPED_INFO("each listed once");
        REQUIRE(r.includes.size() == size_t(2));
        // With `#pragma once` it settles rather than going deep at all.
        files.clear();
        add("c.lsl", "#pragma once\ninteger c;\n");
        ALPreprocessor::Result twice = ALPreprocessor::run("#include \"c.lsl\"\n#include \"c.lsl\"\n", o);
        REQUIRE(twice.problems.empty());
        REQUIRE(twice.includes.size() == size_t(1));
        UNSCOPED_INFO("and put in once");
        CHECK(twice.text.find("integer c;") == twice.text.rfind("integer c;"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: finish after an expansion alone gives what one whole run gives", "[alpreprocessor]")
    {
        // A caller that expands in rounds, waiting on includes from the
        // world, leaves the compression off until its last round and
        // calls `finish` itself. What it gets must be what one run
        // would have made of the same source, or the text a save
        // uploads is not the text the analyzers were shown.
        const std::string       source = "// comment\ndefault   {  state_entry ( ) { llSay( 0 , \"a  b\" ) ; } }\n";
        ALPreprocessor::Options whole  = options();
        whole.compress                 = true;
        const ALPreprocessor::Result at_once = ALPreprocessor::run(source, whole);

        ALPreprocessor::Options expanding = options();
        expanding.compress                = false;
        ALPreprocessor::Result in_two     = ALPreprocessor::run(source, expanding);
        UNSCOPED_INFO("the expansion alone leaves it as it was");
        REQUIRE(in_two.text.find("// comment") != std::string::npos);
        ALPreprocessor::finish(in_two, whole);
        CHECK(in_two.text == at_once.text);
        UNSCOPED_INFO("and the same way back");
        CHECK(in_two.map.toSource(0, 0).line == at_once.map.toSource(0, 0).line);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: the scripter's defines reach every script and #undef has the last word", "[alpreprocessor]")
    {
        ALPreprocessor::Options opts = options();
        opts.defines = { "DEBUG", "LEVEL=3", " SPACED = 7 ", "2BAD", "" };
        ALPreprocessor::Result r = ALPreprocessor::run("#if DEBUG\nx = LEVEL;\n#endif\ny = SPACED;\n#undef LEVEL\nz = LEVEL;\n", opts);
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("DEBUG is 1, so the block is in: " << r.text);
        REQUIRE(r.text.find("x = 3;") != std::string::npos);
        UNSCOPED_INFO("a value around blanks: " << r.text);
        REQUIRE(r.text.find("y = 7;") != std::string::npos);
        UNSCOPED_INFO("undefined by the script: " << r.text);
        REQUIRE(r.text.find("z = LEVEL;") != std::string::npos);
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a run is bounded through chained ?: and nested calls and self includes", "[alpreprocessor]")
    {
        // A chain of ?: is a level each, as a bracket is: said, and not
        // followed down the machine's stack.
        std::string chain = "#if ";
        for (int i = 0; i < 300000; ++i)
        {
            chain += "1?";
        }
        chain += "1";
        for (int i = 0; i < 300000; ++i)
        {
            chain += ":0";
        }
        chain += "\nyes\n#endif\n";
        ALPreprocessor::Result r = ALPreprocessor::run(chain, options());
        UNSCOPED_INFO("the chain is too deep: " << messages(r).substr(0, 200));
        REQUIRE(messages(r).find("nests too deeply") != std::string::npos);

        // A macro invoked in its own argument, deeper than the bound.
        std::string nested = "#define F(x) x\n";
        for (int i = 0; i < 5000; ++i)
        {
            nested += "F(";
        }
        nested += "1";
        for (int i = 0; i < 5000; ++i)
        {
            nested += ")";
        }
        nested += ";\n";
        r = ALPreprocessor::run(nested, options());
        CHECK(r.overran);
        UNSCOPED_INFO("and said why: " << messages(r).substr(0, 200));
        REQUIRE(messages(r).find("more deeply than this preprocessor follows") != std::string::npos);
        UNSCOPED_INFO("the text is the source as it was");
        CHECK(r.text == nested);

        // Within the bound, as deep as a person writes, it expands.
        r = ALPreprocessor::run("#define F(x) x\nF(F(F(F(F(1)))));\n", options());
        {
            INFO("five deep is nothing: " << messages(r));
            CHECK_FALSE(r.overran);
            REQUIRE(r.text.find("1;") != std::string::npos);
        }

        // A file that includes itself twice: each level doubles, and the
        // files it opens are counted against the budget like any tokens.
        add("twice.lsl", "#include \"twice.lsl\"\n#include \"twice.lsl\"\ninteger x;\n");
        ALPreprocessor::Options opts = options();
        opts.tokenBudget             = 200000;
        r                            = ALPreprocessor::run("#include \"twice.lsl\"\n", opts);
        UNSCOPED_INFO("overran, rather than opening two to the thirty-second files");
        CHECK(r.overran);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a name in a string literal is escaped; no module path reaches a save", "[alpreprocessor]")
    {
        ALPreprocessor::Options opts = options();
        opts.fileName                = "Say \"hi\" \\ there";
        ALPreprocessor::Result r     = ALPreprocessor::run("llSay(0, __FILE__);\nllSay(0, __SHORTFILE__);\n", opts);
        UNSCOPED_INFO("the quote and the backslash escaped: " << r.text);
        REQUIRE(r.text.find("\"Say \\\"hi\\\" \\\\ there\"") != std::string::npos);

        // A module found on a Windows disk: required by its number, and
        // the folders it is in -- the scripter's own -- nowhere in the text.
        ALPreprocessor::Include module;
        module.text = "return { x = 1 }\n";
        module.name = "util.luau";
        module.path = "disk:C:\\Users\\me\\lib\\util.luau";
        files["./util"] = module;
        r = ALPreprocessor::run("local util = require(\"./util\")\n", options(true));
        CHECK(faults(r) == std::string());
        UNSCOPED_INFO("required by its number: " << r.text);
        REQUIRE(r.text.find("local util = require(1)") != std::string::npos);
        {
            INFO("no path: " << r.text);
            REQUIRE(r.text.find("disk:") == std::string::npos);
            REQUIRE(r.text.find("Users") == std::string::npos);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: an interpolated SLua string with a brace or backtick ends where it ends", "[alpreprocessor]")
    {
        const std::vector<ALPreprocessor::Token> tokens = ALPreprocessor::tokenize("local s = `a{ \"{\" .. `{'}'}` }b` .. X\n", true);
        std::vector<std::string> kinds;
        for (const ALPreprocessor::Token& t : tokens)
        {
            if (t.kind != ALPreprocessor::Token::Kind::Space)
            {
                kinds.push_back(t.text);
            }
        }
        {
            INFO("the string whole, then what follows it");
            REQUIRE(kinds.size() >= 6);
            CHECK(kinds[3] == "`a{ \"{\" .. `{'}'}` }b`");
            CHECK(kinds[4] == "..");
            CHECK(kinds[5] == "X");
        }

        // And the macro after it is expanded.
        ALPreprocessor::Result r = ALPreprocessor::run("--#define X 42\nlocal s = `{\"{\"}` .. X\n", options(true));
        UNSCOPED_INFO("the macro after the string expanded: " << r.text);
        REQUIRE(r.text.find(".. 42") != std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: switch and loop transforms bounded; a long else-if chain no deeper", "[alpreprocessor]")
    {
        ALPreprocessor::Options opts = options();
        opts.switches                = true;
        opts.extensions              = true;

        // Blocks in blocks past the bound: said, and nothing made of it.
        std::string blocks = "default { state_entry() {\n";
        for (int i = 0; i < 2000; ++i)
        {
            blocks += "{";
        }
        blocks += "llOwnerSay(\"deep\");";
        for (int i = 0; i < 2000; ++i)
        {
            blocks += "}";
        }
        blocks += "\n} }\n";
        ALPreprocessor::Result r = ALPreprocessor::run(blocks, opts);
        CHECK(r.overran);
        UNSCOPED_INFO("and said why: " << messages(r).substr(0, 200));
        REQUIRE(messages(r).find("blocks nest more deeply") != std::string::npos);
        UNSCOPED_INFO("the text is the source as it was");
        CHECK(r.text == blocks);

        // Switches in switches, likewise.
        std::string switches = "default { state_entry() { integer x;\n";
        for (int i = 0; i < 2000; ++i)
        {
            switches += "switch (x) { case 1: ";
        }
        for (int i = 0; i < 2000; ++i)
        {
            switches += "}";
        }
        switches += "\n} }\n";
        r = ALPreprocessor::run(switches, opts);
        UNSCOPED_INFO("switches overran");
        CHECK(r.overran);
        UNSCOPED_INFO("and said why: " << messages(r).substr(0, 200));
        REQUIRE(messages(r).find("blocks nest more deeply") != std::string::npos);

        // As deep as anybody writes, done.
        std::string loops = "default { state_entry() { integer i;\n";
        for (int i = 0; i < 30; ++i)
        {
            loops += "while (i) { switch (i) { case 1: break; } ";
        }
        loops += "break;";
        for (int i = 0; i < 30; ++i)
        {
            loops += "}";
        }
        loops += "\n} }\n";
        r = ALPreprocessor::run(loops, opts);
        {
            INFO("thirty deep is nothing: " << messages(r).substr(0, 200));
            CHECK_FALSE(r.overran);
            CHECK_FALSE(r.hasErrors());
        }
        UNSCOPED_INFO("and the loops were lowered: " << r.text.substr(0, 200));
        REQUIRE(r.text.find("jump _brk") != std::string::npos);

        // A loop governing a long chain of else-ifs, on a thread with what
        // the platform gives one -- half a megabyte on a Mac: the chain is
        // walked along, not descended, however long it is.
        std::string chain = "default { state_entry() { integer i;\nwhile (i < 3)\n";
        for (int k = 0; k < 20000; ++k)
        {
            chain += std::string(k ? "else " : "") + "if (i == " + std::to_string(k) + ") i += 1;\n";
        }
        chain += "} }\n";
        std::thread worker([&]() { r = ALPreprocessor::run(chain, opts); });
        worker.join();
        {
            INFO("the chain is no deeper for its length: " << messages(r).substr(0, 200));
            CHECK_FALSE(r.overran);
            CHECK_FALSE(r.hasErrors());
        }
        UNSCOPED_INFO("and all of it is there");
        REQUIRE(r.text.find("else if (i == 19999) i += 1;") != std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: each line of a multiline comment or long string maps back to its own", "[alpreprocessor]")
    {
        const std::string source = "#define X 1\n/* one\n   two\n   three */ integer x = X;\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("the comment on the lines it was on: " << r.text);
        REQUIRE(r.text.find("\n/* one\n   two\n   three */ integer x = 1;") != std::string::npos);
        ALSourceMap::Loc loc = r.map.toSource(2, 4);
        UNSCOPED_INFO("the comment's middle line maps");
        REQUIRE(loc.found());
        UNSCOPED_INFO("to its own line");
        CHECK(loc.line == 2);
        UNSCOPED_INFO("and column");
        CHECK(loc.column == 4);
        loc = r.map.toSource(3, 4);
        UNSCOPED_INFO("its last line, before what follows it on the line");
        CHECK(loc.line == 3);
        UNSCOPED_INFO("at its own column");
        CHECK(loc.column == 4);
        loc = r.map.toSource(3, 12);
        UNSCOPED_INFO("and what follows it where it is");
        CHECK(loc.column == 12);
        loc = r.map.toExpanded(0, 2, 4);
        {
            INFO("and back");
            REQUIRE(loc.found());
            CHECK(loc.line == 2);
            CHECK(loc.column == 4);
        }

        const std::string lua = "--#define X 2\nlocal s = [[one\ntwo\nthree]] .. X\n";
        r = ALPreprocessor::run(lua, options(true));
        CHECK(messages(r) == std::string());
        loc = r.map.toSource(2, 1);
        {
            INFO("a long string's middle line maps to its own");
            REQUIRE(loc.found());
            CHECK(loc.line == 2);
            CHECK(loc.column == 1);
        }
        loc = r.map.toSource(3, 11);
        {
            INFO("and the macro after it to where it was invoked");
            REQUIRE(loc.found());
            CHECK(loc.line == 3);
            CHECK(loc.column == 11);
        }
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: transformAt reads a line's transform; a keyword as a name is none", "[alpreprocessor]")
    {
        using T = ALPreprocessor::Transform;
        const auto of = [](std::vector<std::string> lines, S32 at, std::string* said = nullptr) {
            std::string word;
            const T     t = ALPreprocessor::transformAt([&lines](S32 i) { return std::string_view(lines[i]); }, static_cast<S32>(lines.size()), at, word);
            if (said)
            {
                *said = word;
            }
            return t;
        };
        std::string word;
        CHECK(of({ "    switch (x)" }, 0, &word) == T::Switch);
        CHECK(word == std::string("switch"));
        UNSCOPED_INFO("switch not called");
        CHECK(of({ "switch = 2;" }, 0) == T::None);
        CHECK(of({ "  case 1:" }, 0, &word) == T::Switch);
        CHECK(word == std::string("case"));
        UNSCOPED_INFO("case assigned");
        CHECK(of({ "case = 1;" }, 0) == T::None);
        UNSCOPED_INFO("case called");
        CHECK(of({ "case(1);" }, 0) == T::None);
        UNSCOPED_INFO("case with no colon");
        CHECK(of({ "case 1" }, 0) == T::None);
        CHECK(of({ "break;" }, 0) == T::Extensions);
        UNSCOPED_INFO("break out of two");
        CHECK(of({ "break 2;" }, 0) == T::Extensions);
        CHECK(of({ "\tcontinue;" }, 0) == T::Extensions);
        UNSCOPED_INFO("break assigned");
        CHECK(of({ "break = 3;" }, 0) == T::None);
        UNSCOPED_INFO("an inline function");
        CHECK(of({ "inline f(integer a)" }, 0, &word) == T::Extensions);
        CHECK(word == std::string("inline"));
        UNSCOPED_INFO("an inline function with its type");
        CHECK(of({ "inline integer f(integer a)" }, 0) == T::Extensions);
        UNSCOPED_INFO("inline after the parameters");
        CHECK(of({ "integer f(integer a) inline {" }, 0, &word) == T::Extensions);
        CHECK(word == std::string("inline"));
        UNSCOPED_INFO("inline after them, the brace on the next line");
        CHECK(of({ "f() inline" }, 0) == T::Extensions);
        UNSCOPED_INFO("a name that ends in inline is not");
        CHECK(of({ "integer f(integer a) noinline {" }, 0) == T::None);
        UNSCOPED_INFO("inline assigned");
        CHECK(of({ "inline = 2;" }, 0) == T::None);
        UNSCOPED_INFO("const before a type");
        CHECK(of({ "const integer X = 1;" }, 0, &word) == T::Extensions);
        CHECK(word == std::string("const"));
        UNSCOPED_INFO("a const parameter");
        CHECK(of({ "f(integer a, const vector v)" }, 0) == T::Extensions);
        UNSCOPED_INFO("a variable named const");
        CHECK(of({ "integer const = 1;" }, 0) == T::None);
        UNSCOPED_INFO("a word that starts with const");
        CHECK(of({ "constant = 1;" }, 0) == T::None);
        UNSCOPED_INFO("after a brace and a statement's end");
        CHECK(of({ "} ; switch (x)" }, 0) == T::Switch);
        UNSCOPED_INFO("a plain statement");
        CHECK(of({ "integer x = 1;" }, 0) == T::None);
        UNSCOPED_INFO("a blank line");
        CHECK(of({ "   " }, 0) == T::None);

        // A brace of its own is the switch's where the line before opens one.
        UNSCOPED_INFO("the switch's brace");
        CHECK(of({ "switch (x)", "", "  {" }, 2) == T::Switch);
        UNSCOPED_INFO("a brace after anything else");
        CHECK(of({ "if (x)", "{" }, 1) == T::None);
        UNSCOPED_INFO("a brace after a break is not the switch's");
        CHECK(of({ "break;", "{" }, 1) == T::None);
        UNSCOPED_INFO("a brace with nothing before it");
        CHECK(of({ "", "{" }, 1) == T::None);
        UNSCOPED_INFO("a line past the end");
        CHECK(of({ "switch (x)" }, 1) == T::None);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: verbatimSpan maps copied source back whole but not a macro's output", "[alpreprocessor]")
    {
        const ALPreprocessor::Result r = ALPreprocessor::run("#define GREET llOwnerSay(\"hi\")\nGREET;\n    llSay(0, \"x\");\n", options());
        UNSCOPED_INFO("no problems: " << messages(r));
        REQUIRE(r.problems.empty());
        // Where each call landed in the output.
        S32 greet_line = -1, greet_column = -1, say_line = -1, say_column = -1;
        std::istringstream in(r.text);
        std::string        line;
        for (S32 n = 0; std::getline(in, line); ++n)
        {
            if (const size_t at = line.find("llOwnerSay"); at != std::string::npos)
            {
                greet_line   = n;
                greet_column = static_cast<S32>(at);
            }
            if (const size_t at = line.find("llSay"); at != std::string::npos)
            {
                say_line   = n;
                say_column = static_cast<S32>(at);
            }
        }
        {
            INFO("both in the output: " << r.text);
            CHECK(greet_line >= 0);
            CHECK(say_line >= 0);
        }
        ALSourceMap::Loc begin, end;
        UNSCOPED_INFO("llSay is the source's own");
        REQUIRE(r.map.verbatimSpan(say_line, say_column, say_column + 5, begin, end));
        UNSCOPED_INFO("from its line");
        CHECK(begin.line == 2);
        UNSCOPED_INFO("its column");
        CHECK(begin.column == 4);
        UNSCOPED_INFO("to its end");
        CHECK(end.column == 9);
        UNSCOPED_INFO("in the script");
        CHECK(begin.file == 0);
        {
            INFO("an insertion after it too");
            CHECK(r.map.verbatimSpan(say_line, say_column + 5, say_column + 5, begin, end));
            CHECK(begin.column == 9);
        }
        UNSCOPED_INFO("a stretch of several tokens, the blanks between them the source's");
        CHECK(r.map.verbatimSpan(say_line, say_column, say_column + 13, begin, end));
        UNSCOPED_INFO("to the call's end");
        CHECK(end.column == 17);
        UNSCOPED_INFO("what the macro made is not");
        CHECK_FALSE(r.map.verbatimSpan(greet_line, greet_column, greet_column + 10, begin, end));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a weighed optimizer run says what it saved in all and per note", "[alpreprocessor]")
    {
        requireGridBuiltins();
        const std::string source = "integer unused(integer n)\n"
                                   "{\n"
                                   "    return n * 3;\n"
                                   "}\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        llSay(0, (string)(21 * 2));\n"
                                   "    }\n"
                                   "}\n";
        ALPreprocessor::Options o = options();
        o.optimize                = true;
        o.optimizer.target        = ALLSLOptimizer::Target::LSO;
        o.weigh                   = true;
        const ALPreprocessor::Result r = ALPreprocessor::run(source, o);
        CHECK(r.optimized);
        {
            INFO("weighed, and lighter");
            CHECK(r.codeAfter > 0);
            CHECK(r.codeAfter < r.codeBefore);
        }
        const ALScriptProblem* removed = nullptr;
        const ALScriptProblem* folded  = nullptr;
        const ALScriptProblem* sizes   = nullptr;
        for (const ALScriptProblem& p : r.problems)
        {
            removed = p.key == "OptimizerRemovedFunction" ? &p : removed;
            folded  = p.line == 8 && p.source == ALScriptProblem::Source::Optimizer && p.key != "OptimizerRemovedFunction" ? &p : folded;
            sizes   = p.key == "OptimizerSizesWeighed" ? &p : sizes;
        }
        {
            INFO("the function's removal noted");
            REQUIRE(removed);
            REQUIRE(removed->savedBytes.has_value());
        }
        UNSCOPED_INFO("its lines' code all saved");
        CHECK(*removed->savedBytes > 0);
        {
            INFO("the fold noted, with what its line saved");
            REQUIRE(folded);
            REQUIRE(folded->savedBytes);
            CHECK(*folded->savedBytes > 0);
        }
        UNSCOPED_INFO("the whole said");
        REQUIRE(sizes != nullptr);
        REQUIRE(sizes->args.size() == size_t(5));
        UNSCOPED_INFO("in code");
        CHECK(sizes->args[2] == std::to_string(r.codeBefore));
        UNSCOPED_INFO("on its target");
        CHECK(sizes->args[4] == std::string("LSL (LSO)"));
        const S64 lost = S64(r.codeBefore) - S64(r.codeAfter);
        {
            INFO("no note says more than the whole lost");
            REQUIRE(*removed->savedBytes <= lost);
            CHECK(*folded->savedBytes <= lost);
        }

        // Not weighed unless asked: no bytes, and the words as they were.
        o.weigh                           = false;
        const ALPreprocessor::Result bare = ALPreprocessor::run(source, o);
        bool                         said = false;
        for (const ALScriptProblem& p : bare.problems)
        {
            UNSCOPED_INFO("nothing weighed");
            REQUIRE_FALSE(p.savedBytes);
            said = said || p.key == "OptimizerSizes";
        }
        {
            INFO("the size in source only");
            CHECK(said);
            CHECK(bare.codeBefore == 0);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a script that does not compile: the optimizer stands aside with a note", "[alpreprocessor]")
    {
        requireGridBuiltins();
        // Nexii's library, whose ObjectLinksetSittingAvatars has a parameter
        // without its type, included as a save would include it.
        llifstream        in(std::string(AL_ALSCRIPT_TEST_DIR) + "/preprocessor/include/linkset.lsl", std::ios::binary);
        std::stringstream library;
        library << in.rdbuf();
        add("linkset.lsl", library.str());
        const std::string source = "#include \"linkset.lsl\"\ndefault\n{\n    state_entry()\n    {\n        llSay(0, (string)LinkByName(\"Foot\"));\n    }\n}\n";
        ALPreprocessor::Options o = options();
        o.optimize                = true;
        o.weigh                   = true;
        ALPreprocessor::Result r  = ALPreprocessor::run(source, o);
        UNSCOPED_INFO("no error of the preprocessor's: " << messages(r));
        CHECK_FALSE(r.hasErrors());
        UNSCOPED_INFO("one note, on the library's line");
        CHECK(messages(r) ==
                      std::string("N linkset.lsl:110: not optimized: the script does not compile as it stands, and compiling it says why\n"));
        REQUIRE(r.problems.size() == 1);
        CHECK(r.problems[0].key == "OptimizerUncompiled");
        CHECK(r.problems[0].column == 33);
        CHECK_FALSE(r.optimized);
        ALPreprocessor::Options plain = options();
        UNSCOPED_INFO("the text as expanded, for the compiler to read");
        CHECK(r.text == ALPreprocessor::run(source, plain).text);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: only the global require finds a module and not a field or method", "[alpreprocessor]")
    {
        add("x", "return 1\n");
        const ALPreprocessor::Result r = ALPreprocessor::run("local a = t.require(\"x\")\n"
                                                             "local b = t : require(\"x\")\n"
                                                             "local c = t .. require(\"x\")\n",
                                                             options(true));
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("only the concatenated call is a module's");
        CHECK(r.text ==
                      bundleOf({ "return 1\n" }, "local a = t.require(\"x\")\n"
                                                  "local b = t : require(\"x\")\n"
                                                  "local c = t .. require(1)\n"));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: requiresIn finds just the calls a run puts a module in place of", "[alpreprocessor]")
    {
        const std::string source = "local a = require(\"a\")\n"   // 0
                                   "local b = require 'b'\n"       // 1: no parentheses
                                   "local c = require( -- the next\n"
                                   "    'c'\n"
                                   ")\n"                           // 4
                                   "-- require(\"d\")\n"
                                   "local e = \"require('e')\"\n"
                                   "local f = t.require(\"f\")\n"
                                   "local g = require(name)\n"
                                   "local h = require([[h]])\n"
                                   "local i = x .. require(\"i\")\n"; // 10
        const std::vector<ALPreprocessor::Required> found = ALPreprocessor::requiresIn(source);
        REQUIRE(found.size() == size_t(3));
        CHECK(found[0].name == std::string("a"));
        {
            INFO("a's stretch");
            CHECK(found[0].line == 0);
            CHECK(found[0].column == 10);
            CHECK(found[0].endLine == 0);
            CHECK(found[0].endColumn == 22);
        }
        CHECK(found[1].name == std::string("c"));
        {
            INFO("c's stretch, across lines");
            CHECK(found[1].line == 2);
            CHECK(found[1].column == 10);
            CHECK(found[1].endLine == 4);
            CHECK(found[1].endColumn == 1);
        }
        CHECK(found[2].name == std::string("i"));
        {
            INFO("i's line");
            CHECK(found[2].line == 10);
            CHECK(found[2].column == 15);
        }

        // Every one of them is one a run puts a module in place of, and
        // no other call is.
        add("a", "return 1\n");
        add("c", "return 3\n");
        add("i", "return 9\n");
        const ALPreprocessor::Result r = ALPreprocessor::run(source, options(true));
        CHECK(faults(r) == std::string());
        // Each module's function is put at the top once, and asked for by
        // its number where it was called for.
        size_t numbered = 0;
        for (size_t at = r.text.find("require("); at != std::string::npos; at = r.text.find("require(", at + 1))
        {
            numbered += at + 8 < r.text.size() && std::isdigit(static_cast<unsigned char>(r.text[at + 8]));
        }
        UNSCOPED_INFO("as many requires as calls found");
        REQUIRE(numbered == found.size());
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a run says what each include or module resolved to as written", "[alpreprocessor]")
    {
        add("lib/util.lsl", "#include \"inner.lsl\"\nint util() { return 1; }\n");
        files["lib/util.lsl"].path = "disk:/scripts/lib/util.lsl";
        add("inner.lsl", "int inner() { return 2; }\n");
        add("missing.lsl", "");
        files.erase("missing.lsl");
        const ALPreprocessor::Result r = ALPreprocessor::run("#include \"lib/util.lsl\"\n#include \"lib/util.lsl\"\ndefault {}\n", options());
        // Included twice, with no #pragma once: its own include asked for
        // twice as well.
        REQUIRE(r.resolved.size() == size_t(4));
        {
            INFO("the script's, by its name, as the identity found");
            REQUIRE(r.resolved[0].from.empty());
            CHECK(r.resolved[0].name == "lib/util.lsl");
            CHECK(r.resolved[0].path == "disk:/scripts/lib/util.lsl");
            CHECK_FALSE(r.resolved[0].require);
        }
        {
            INFO("the include's own, from it");
            CHECK(r.resolved[1].from == "disk:/scripts/lib/util.lsl");
            CHECK(r.resolved[1].name == "inner.lsl");
            CHECK(r.resolved[1].path == "inner.lsl");
        }
        {
            INFO("the same asked twice, twice");
            CHECK(r.resolved[2].name == "lib/util.lsl");
            REQUIRE(r.resolved[2].from.empty());
            CHECK(r.resolved[3].name == "inner.lsl");
        }

        add("mod", "return 1\n");
        files["mod"].path = "disk:/scripts/mod.luau";
        const ALPreprocessor::Result lua = ALPreprocessor::run("local m = require(\"mod\")\nlocal n = require(\"none\")\n", options(true));
        UNSCOPED_INFO("a module found, one not");
        REQUIRE(lua.resolved.size() == size_t(1));
        {
            INFO("as a require");
            CHECK(lua.resolved[0].require);
            CHECK(lua.resolved[0].name == "mod");
            CHECK(lua.resolved[0].path == "disk:/scripts/mod.luau");
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: repeated parameters and self pasting or stringizing stop within budget", "[alpreprocessor]")
    {
        // A parameter used sixteen times, through arguments that each
        // expand the last: sixteen to the seventh tokens by the end, which
        // were made whole before they were counted.
        const std::string many = "#define D(x) x x x x x x x x x x x x x x x x\nD(D(D(D(D(D(D(1)))))))\n";
        ALPreprocessor::Result r = ALPreprocessor::run(many, options());
        {
            INFO("ran away: " << messages(r));
            CHECK(r.overran);
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocTooMuch"));
        }
        UNSCOPED_INFO("and gave the source back");
        CHECK(r.text == many);

        // A token pasted to itself through an indirection doubles its text
        // and not the tokens: two to the fortieth bytes by the end.
        std::string call = "x";
        for (int i = 0; i < 40; ++i)
        {
            call = "D(" + call + ")";
        }
        r = ALPreprocessor::run("#define CAT(a) a##a\n#define D(a) CAT(a)\n" + call + ";\n", options());
        {
            INFO("pasting stopped: " << messages(r));
            CHECK(r.overran);
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocTokenTooLong"));
        }

        // A string stringized over and over, each level escaping the last.
        call = "\"a\"";
        for (int i = 0; i < 40; ++i)
        {
            call = "X(" + call + ")";
        }
        r = ALPreprocessor::run("#define S(x) #x\n#define X(x) S(x)\n" + call + ";\n", options());
        {
            INFO("stringizing stopped: " << messages(r));
            CHECK(r.overran);
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocTokenTooLong"));
        }

        // The bytes stop what the tokens would not, and what fits is
        // untouched.
        ALPreprocessor::Options small = options();
        small.byteBudget              = 1000;
        r = ALPreprocessor::run("#define L \"" + std::string(600, 'a') + "\"\nL L\n", small);
        {
            INFO("the bytes: " << messages(r));
            CHECK(r.overran);
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocTooMuch"));
        }
        r = ALPreprocessor::run("#define L \"" + std::string(100, 'a') + "\"\nL L\n", small);
        {
            INFO("within them: " << messages(r));
            CHECK_FALSE(r.overran);
            REQUIRE(r.text.find(std::string(100, 'a')) != std::string::npos);
        }

        // What a job answers when a run throws: as a run that ran away.
        const ALPreprocessor::Result failed = ALPreprocessor::failed("integer x;\n", options(), "std::bad_alloc");
        {
            INFO("overran, with the source");
            CHECK(failed.overran);
            CHECK(failed.text == "integer x;\n");
        }
        {
            INFO("and why");
            REQUIRE(failed.problems.size() == 1);
            CHECK(failed.problems[0].key == std::string("PreprocFailed"));
            REQUIRE(failed.problems[0].message.find("std::bad_alloc") != std::string::npos);
            CHECK(failed.problems[0].severity == ALScriptProblem::Severity::Error);
        }
        {
            INFO("mapped to itself");
            REQUIRE(failed.map.toSource(0, 3).found());
            CHECK(failed.map.toSource(0, 3).line == 0);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: nested lazy list reads stop at the block depth or each become a call", "[alpreprocessor]")
    {
        // A read inside a read's brackets, a thousand deep, made by macros
        // each expanding the last -- rescanned, so the expansion itself is
        // flat, and only the rewrite descends.
        std::string deep = "#define N0 0\n";
        for (int i = 1; i <= 1000; ++i)
        {
            deep += "#define N" + std::to_string(i) + " (integer)l[N" + std::to_string(i - 1) + "]\n";
        }
        deep += "list l;\ninteger x = N1000;\n";
        ALPreprocessor::Options o = options();
        o.lazyLists               = true;
        ALPreprocessor::Result r  = ALPreprocessor::run(deep, o);
        {
            INFO("stopped at the depth: " << messages(r));
            CHECK(r.overran);
            REQUIRE_FALSE(r.problems.empty());
            CHECK(r.problems.back().key == std::string("PreprocNestsTooDeep"));
        }

        // Within it, each read is a call, the inner in the outer's place.
        r = ALPreprocessor::run("#define N0 0\n#define N1 (integer)l[N0]\n#define N2 (integer)l[N1]\n#define N3 (integer)l[N2]\nlist l;\ninteger x = N3;\n", o);
        {
            INFO("nested reads each a call: " << r.text);
            CHECK_FALSE(r.overran);
            REQUIRE(r.text.find("integer x = llList2Integer(l, llList2Integer(l, llList2Integer(l, 0)));") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: a comment header stays above the unmapped module table", "[alpreprocessor]")
    {
        add("m", "return 1\n");
        const ALPreprocessor::Result r =
            ALPreprocessor::run("-- @file vehicle/hovertext.luau\n--!strict\n--!nolint LocalUnused\nlocal m = require(\"m\")\n", options(true));
        CHECK(faults(r) == std::string());
        UNSCOPED_INFO("the header first, whole");
        CHECK(r.text.substr(0, r.text.find("do\n    local modules")) ==
                      std::string("-- @file vehicle/hovertext.luau\n--!strict\n--!nolint LocalUnused\n"));
        {
            INFO("each header line its own");
            REQUIRE(r.map.toSource(1, 0).found());
            CHECK(r.map.toSource(1, 0).file == 0);
            CHECK(r.map.toSource(1, 0).line == 1);
        }
        {
            INFO("the table's lines and the require's map nowhere");
            REQUIRE_FALSE(r.map.toSource(3, 0).found());
            REQUIRE_FALSE(r.map.toSource(9, 0).found());
        }
        UNSCOPED_INFO("nor its module's wrapping");
        REQUIRE_FALSE(r.map.toSource(5, 0).found());
        {
            INFO("the module's own line maps to it");
            REQUIRE(r.map.toSource(6, 0).found());
            CHECK(r.map.files()[r.map.toSource(6, 0).file].name == "m");
        }
        {
            INFO("the script's code after it maps back");
            CHECK(r.map.toSource(19, 0).file == 0);
            CHECK(r.map.toSource(19, 0).line == 3);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a superseded run stops and gives the source back; a wanted one runs on", "[alpreprocessor]")
    {
        std::atomic<bool>       later{ false };
        ALPreprocessor::Options wanted = options(false);
        wanted.superseded              = &later;
        wanted.switches                = true;
        const std::string       source = "#define TWICE(x) ((x) * 2)\ndefault { state_entry() { integer n = TWICE(3); switch (n) { case 6: break; } } }\n";
        const ALPreprocessor::Result made = ALPreprocessor::run(source, wanted);
        {
            INFO("still wanted: made");
            CHECK_FALSE(made.superseded);
            CHECK_FALSE(made.overran);
            REQUIRE(made.text.find("((3) * 2)") != std::string::npos);
        }
        later = true;
        const ALPreprocessor::Result stopped = ALPreprocessor::run(source, wanted);
        {
            INFO("not wanted: stopped");
            CHECK(stopped.superseded);
            CHECK(stopped.overran);
        }
        UNSCOPED_INFO("the source as it was");
        CHECK(stopped.text == source);
        UNSCOPED_INFO("nothing said of it");
        CHECK(std::none_of(stopped.problems.begin(), stopped.problems.end(),
                                                  [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: include guards: once until #undef; no guard with #else or code after", "[alpreprocessor]")
    {
        add("guarded.lsl", "// the helpers\n#ifndef GUARDED\n#define GUARDED\ninteger helper() { return 1; }\n#endif\n");
        const ALPreprocessor::Result r = ALPreprocessor::run(
            "#include \"guarded.lsl\"\n#include \"guarded.lsl\"\n#include \"guarded.lsl\"\ndefault { state_entry() { helper(); } }\n", options());
        CHECK(messages(r) == std::string());
        size_t count = 0;
        for (size_t at = r.text.find("integer helper()"); at != std::string::npos; at = r.text.find("integer helper()", at + 1))
        {
            ++count;
        }
        CHECK(count == size_t(1));
        UNSCOPED_INFO("listed once");
        REQUIRE(r.map.files().size() == size_t(2));

        const ALPreprocessor::Result undone = ALPreprocessor::run(
            "#include \"guarded.lsl\"\n#undef GUARDED\n#include \"guarded.lsl\"\ndefault { state_entry() { } }\n", options());
        UNSCOPED_INFO("undefined: again");
        CHECK(undone.text.find("integer helper()") != undone.text.rfind("integer helper()"));

        add("elsed.lsl", "#ifndef ELSED\n#define ELSED\ninteger a;\n#else\ninteger b;\n#endif\n");
        const ALPreprocessor::Result elsed = ALPreprocessor::run("#include \"elsed.lsl\"\n#include \"elsed.lsl\"\n", options());
        UNSCOPED_INFO("an #else: no guard, the other branch made");
        REQUIRE(elsed.text.find("integer b;") != std::string::npos);
        add("after.lsl", "#ifndef AFTER\n#define AFTER\n#endif\ninteger after;\n");
        const ALPreprocessor::Result after = ALPreprocessor::run("#include \"after.lsl\"\n#include \"after.lsl\"\n", options());
        UNSCOPED_INFO("code after the #endif: no guard, made twice");
        CHECK(after.text.find("integer after;") != after.text.rfind("integer after;"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: macro arguments: none for no parameters; lines join; blanks kept", "[alpreprocessor]")
    {
        const std::string source = "#define Z() zero\n"
                                   "#define I(x) [x]\n"
                                   "#define TWO(a, b) a+b\n"
                                   "Z() Z( ) Z\n"
                                   "I (1) I  x I\n"
                                   "TWO(1,\n2) TWO( (1, 2) ,\t3 )\n"
                                   "I\n(3)\n";
        const ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        CHECK(messages(r) == std::string());
        CHECK(squeeze(r.text) == std::string("zero zero Z\n[1] I  x I\n1+2 (1, 2)+3\n[3]\n"));
    }
    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a function marked inline in any form is listed and the mark taken off", "[alpreprocessor]")
    {
        requireGridBuiltins();
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("integer f(integer a) inline { return a; }\n"
                                                        "g(string s) /*pragma inline*/ { llOwnerSay(s); }\n"
                                                        "h() //pragma inline\n{\n}\n"
                                                        "k() /* pragma inline */ { }\n",
                                                        o);
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("the four marked, in order");
        CHECK(r.inlined == std::vector<std::string>{ "f", "g", "h", "k" });
        UNSCOPED_INFO("the word after the parameters taken off: " << r.text);
        REQUIRE(r.text.find("integer f(integer a) { return a; }") != std::string::npos);
        {
            INFO("a comment left as it is: " << r.text);
            REQUIRE(r.text.find("g(string s) /*pragma inline*/ { llOwnerSay(s); }") != std::string::npos);
            REQUIRE(r.text.find("h() //pragma inline\n{") != std::string::npos);
        }

        r = ALPreprocessor::run("integer inline = 1;\nf() { integer x = inline; }\ng() /* not pragma inline */ { }\ninteger v = (1) /*pragma inline*/;\n", o);
        UNSCOPED_INFO("a variable named inline, another comment, a parenthesis that closes no parameters: none marked");
        REQUIRE(r.inlined.empty());
        UNSCOPED_INFO("and nothing taken off");
        CHECK(r.text == std::string("integer inline = 1;\nf() { integer x = inline; }\ng() /* not pragma inline */ { }\ninteger v = (1) /*pragma inline*/;\n"));

        r = ALPreprocessor::run("default { state_entry() inline { } }\n", o);
        UNSCOPED_INFO("an event is not a function to put in place");
        REQUIRE(r.inlined.empty());

        // With the extensions off the marks come off all the same.
        const ALPreprocessor::Result plain = ALPreprocessor::run("inline f() { }\ninteger g(integer a) inline { return a; }\n", options());
        UNSCOPED_INFO("taken off");
        CHECK(plain.text == std::string("f() { }\ninteger g(integer a) { return a; }\n"));
        UNSCOPED_INFO("and the names kept");
        CHECK(plain.inlined == std::vector<std::string>{ "f", "g" });
        UNSCOPED_INFO("the extensions not said to be used");
        CHECK_FALSE(plain.usedExtensions);

        // And the optimizer puts the marked function in place.
        ALPreprocessor::Options optimizing = options();
        optimizing.optimize                = true;
        const ALPreprocessor::Result put   = ALPreprocessor::run("say(string s) inline\n{\n    llOwnerSay(s);\n    llOwnerSay(s + \"!\");\n}\n"
                                                                 "default\n{\n    state_entry()\n    {\n        say(\"a\");\n        say(\"b\");\n    }\n}\n",
                                                                 optimizing);
        {
            INFO("both calls put in place: " << put.text);
            REQUIRE(put.text.find("say(") == std::string::npos);
            REQUIRE(put.text.find("llOwnerSay(\"a\")") != std::string::npos);
            REQUIRE(put.text.find("llOwnerSay(\"b\" + \"!\")") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: const before a type is taken off and each name said where it is", "[alpreprocessor]")
    {
        requireGridBuiltins();
        const auto at = [](const ALPreprocessor::Result& r, size_t i, const std::string& name, S32 line, S32 column, bool function) {
            return i < r.consts.size() && r.consts[i].name == name && r.consts[i].line == line && r.consts[i].column == column &&
                   r.consts[i].function == function;
        };
        ALPreprocessor::Result r = ALPreprocessor::run("const integer X = 5;\n"
                                                       "const   float Y = X * 2;\n"
                                                       "const integer sq(const integer x) { return x * x; }\n"
                                                       "default { state_entry() { const vector v = <1, 2, 3>; llOwnerSay((string)v); } }\n",
                                                       options());
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("taken off");
        CHECK(r.text ==
                      std::string("integer X = 5;\n"
                                  "float Y = 10.0;\n"
                                  "integer sq(integer x) { return x * x; }\n"
                                  "default { state_entry() { vector v = <1, 2, 3>; llOwnerSay((string)v); } }\n"));
        REQUIRE(r.consts.size() == size_t(5));
        UNSCOPED_INFO("a global");
        CHECK(at(r, 0, "X", 0, 8, false));
        UNSCOPED_INFO("the spaces up to the type gone too");
        CHECK(at(r, 1, "Y", 1, 6, false));
        UNSCOPED_INFO("a function");
        CHECK(at(r, 2, "sq", 2, 8, true));
        UNSCOPED_INFO("its parameter");
        CHECK(at(r, 3, "x", 2, 19, false));
        UNSCOPED_INFO("a local");
        CHECK(at(r, 4, "v", 3, 33, false));
        UNSCOPED_INFO("the extensions not said to be used");
        CHECK_FALSE(r.usedExtensions);

        // A name, an assignment, a word with no type after it: no const.
        const std::string plain = "integer const = 1;\nf() { const = 2; integer constant = const; }\n";
        r                       = ALPreprocessor::run(plain, options());
        UNSCOPED_INFO("nothing taken off");
        CHECK(r.text == plain);
        UNSCOPED_INFO("nothing said");
        REQUIRE(r.consts.empty());

        // Made by a macro, and with the extensions on.
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        r                         = ALPreprocessor::run("#define FIXED const integer\nFIXED Z = 3;\n", o);
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("a macro's");
        CHECK(squeeze(r.text) == std::string("integer Z = 3;\n"));
        const size_t z       = r.text.find('Z');
        const size_t newline = r.text.rfind('\n', z);
        const S32    line    = static_cast<S32>(std::count(r.text.begin(), r.text.begin() + z, '\n'));
        const S32    column  = static_cast<S32>(newline == std::string::npos ? z : z - newline - 1);
        {
            INFO("where the name is in the text");
            CHECK(at(r, 0, "Z", line, column, false));
            REQUIRE(r.consts.size() == 1);
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: directives are --# first on a line and # is always Luau's length", "[alpreprocessor]")
    {
        // A line that starts with # goes on an expression: never a
        // directive, and nothing said of it.
        ALPreprocessor::Result r = ALPreprocessor::run("local n =\n    #items\n#define N 3\nprint(N)\n", options(true));
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("# lines are Luau's");
        CHECK(r.text == std::string("local n =\n    #items\n#define N 3\nprint(N)\n"));

        // --# with a directive's name at once, indented or not; the
        // conditionals, the macros, #line.
        r = ALPreprocessor::run("--#define DEBUG\n"
                                "--#define LEN(t) #t\n"
                                "--#define LAST(t) t[#t]\n"
                                "--#define TWICE(t) ##t\n"
                                "--#ifdef DEBUG\n"
                                "    --#define LEVEL 2\n"
                                "--#else\n"
                                "    --#define LEVEL 0\n"
                                "--#endif\n"
                                "--#if LEVEL > 1\n"
                                "print(LEN(xs), LAST(xs), TWICE(xs), LEVEL)\n"
                                "--#endif\n"
                                "--#line 40\n",
                                options(true));
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("directives read, # as length");
        CHECK(r.text ==
                      std::string("\n\n\n\n\n\n\n\n\n\nprint(#xs, xs[#xs], ##xs, 2)\n\n--#line 40\n"));

        // Comments that only look like directives stay comments: selene's
        // file-wide word, a rule of #s, a note, a word that is no
        // directive's, a blank after the --, and one after code.
        const std::string comments = "--# selene: allow(unused_variable)\n"
                                     "--##########\n"
                                     "--# define X 1\n"
                                     "--#definitely not\n"
                                     "-- #define Y 2\n"
                                     "local z = 1 --#define Z 3\n"
                                     "print(X, Y, Z)\n";
        r = ALPreprocessor::run(comments, options(true));
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("all as written");
        CHECK(r.text == comments);

        // An include, as LSL has one.
        add("lib.luau", "--#define GREETING \"hi\"\nlocal lib = {}\n");
        r = ALPreprocessor::run("--#include \"lib.luau\"\nprint(GREETING)\n", options(true));
        CHECK(messages(r) == std::string());
        {
            INFO("the include's text in, and its macro");
            REQUIRE(r.text.find("local lib = {}") != std::string::npos);
            REQUIRE(r.text.find("print(\"hi\")") != std::string::npos);
        }

        // Its errors are the directive's.
        r = ALPreprocessor::run("--#endif\n--#if\n--#endif\n", options(true));
        {
            INFO("an #endif without #if, and an #if without an expression: " << messages(r));
            REQUIRE(messages(r).find("E 0:") != std::string::npos);
            REQUIRE(messages(r).find("E 1:") != std::string::npos);
        }

        // Read as a script's lines are, by what finds includes and names.
        UNSCOPED_INFO("--#include's name");
        CHECK(ALPreprocessor::directiveName("  --#include \"x\"", true) == size_t(5));
        UNSCOPED_INFO("not with a blank");
        REQUIRE(ALPreprocessor::directiveName("--# include \"x\"", true) == std::string_view::npos);
        UNSCOPED_INFO("not # in SLua");
        REQUIRE(ALPreprocessor::directiveName("#include \"x\"", true) == std::string_view::npos);
        UNSCOPED_INFO("# in LSL, blanks about it");
        CHECK(ALPreprocessor::directiveName(" # include \"x\"", false) == size_t(3));
        UNSCOPED_INFO("not --# in LSL");
        REQUIRE(ALPreprocessor::directiveName("--#include \"x\"", false) == std::string_view::npos);

        // And by the lines a verbatim reader sees: --# is a mark of its own.
        const std::vector<ALPreprocessor::Token> tokens = ALPreprocessor::tokenize("--#define A 1\n#a\n", true);
        {
            INFO("the mark, then the directive's words");
            REQUIRE(tokens.size() > 2);
            CHECK(tokens[0].text == "--#");
            CHECK(tokens[1].text == "define");
        }
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: a --#define continues over a backslash before any line ending", "[alpreprocessor]")
    {
        const std::string lf = "--#define SAY(x) \\\n"
                               "    ll.OwnerSay(x) \\\n"
                               "    print(x)\n"
                               "SAY(\"hi\")\n";
        ALPreprocessor::Result r = ALPreprocessor::run(lf, options(true));
        CHECK(messages(r) == std::string());
        const std::string joined = r.text;
        const size_t      used   = joined.find("ll.OwnerSay(\"hi\")");
        {
            INFO("the body on the line that uses it: " << joined);
            REQUIRE(used != std::string::npos);
            CHECK(joined.find("print(\"hi\")") > used);
            CHECK(joined.find('\n', used) > joined.find("print(\"hi\")"));
            REQUIRE(joined.find('\\') == std::string::npos);
        }
        const auto ended = [](const std::string& text, const char* end) {
            std::string out;
            for (char c : text)
            {
                out += c == '\n' ? std::string(end) : std::string(1, c);
            }
            return out;
        };
        r = ALPreprocessor::run(ended(lf, "\r\n"), options(true));
        UNSCOPED_INFO("CRLF said nothing");
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("CRLF");
        CHECK(r.text == joined);
        r = ALPreprocessor::run(ended(lf, "\r"), options(true));
        UNSCOPED_INFO("a lone CR");
        CHECK(r.text == joined);

        r = ALPreprocessor::run("--#define SAY(x) \\  \n"
                                "    ll.OwnerSay(x) \\\t\n"
                                "    print(x)\n"
                                "SAY(\"hi\")\n",
                                options(true));
        UNSCOPED_INFO("blanks after the backslash");
        CHECK(r.text == joined);
        UNSCOPED_INFO("said of each");
        CHECK(messages(r) ==
                      std::string("W 0: backslash and newline separated by space\nW 1: backslash and newline separated by space\n"));

        add("say.luau", "--#define SAY(x) \\\r\n    print(x)\r\n");
        r = ALPreprocessor::run("--#include \"say.luau\"\nSAY(1)\n", options(true));
        UNSCOPED_INFO("an include with CRLF said nothing");
        CHECK(messages(r) == std::string());
        UNSCOPED_INFO("and its macro goes on over its lines: " << r.text);
        REQUIRE(r.text.find("print(1)") != std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: every golden file with CRLF or lone CR line ends comes out as expected", "[alpreprocessor]")
    {
        namespace fs = std::filesystem;
        const fs::path dir     = fsyspath(AL_ALSCRIPT_TEST_DIR) / "preprocessor";
        S32            checked = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir))
        {
            const std::string name = entry.path().filename().string();
            if (name.compare(0, 5, "test_") != 0 || name.find("_expected") != std::string::npos)
            {
                continue;
            }
            const fs::path    expected = dir / (entry.path().stem().string() + "_expected" + entry.path().extension().string());
            const std::string want     = readFile(expected);
            for (const char* ending : { "\r\n", "\r" })
            {
                const std::string      said = ending[1] ? " with CRLF" : " with a lone CR";
                ALPreprocessor::Result r    = runGolden(entry.path(), ending);
                INFO(name << said);
                REQUIRE(messages(r) == std::string());
                REQUIRE(r.text == want);
            }
            ++checked;
        }
        UNSCOPED_INFO("some golden files");
        CHECK(checked > 0);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: usesDirectives and wanted: why a script is preprocessed", "[alpreprocessor]")
    {
        typedef ALPreprocessor P;
        CHECK(P::usesDirectives("#define X 1\ndefault { state_entry() { } }\n", false));
        UNSCOPED_INFO("one further down, indented");
        CHECK(P::usesDirectives("integer x;\n  #include \"lib.lsl\"\n", false));
        UNSCOPED_INFO("none: # only in a string");
        CHECK_FALSE(P::usesDirectives("default { state_entry() { llSay(0, \"#define\"); } }\n", false));
        UNSCOPED_INFO("none: a line comment");
        CHECK_FALSE(P::usesDirectives("// #define X 1\ndefault { }\n", false));
        UNSCOPED_INFO("none: in a block comment");
        CHECK_FALSE(P::usesDirectives("/* notes\n#define X 1\n*/\ndefault { }\n", false));
        UNSCOPED_INFO("after a block comment closes");
        CHECK(P::usesDirectives("/* notes */\n#define X 1\n", false));
        UNSCOPED_INFO("not opened by one in a string");
        CHECK(P::usesDirectives("string s = \"/*\";\n#define X 1\n", false));
        UNSCOPED_INFO("SLua's --#");
        CHECK(P::usesDirectives("--#define X 1\nprint(X)\n", true));
        UNSCOPED_INFO("none: Luau's length operator first on a line");
        CHECK_FALSE(P::usesDirectives("local n =\n    #items\n", true));
        UNSCOPED_INFO("none: an SLua comment");
        CHECK_FALSE(P::usesDirectives("-- # a heading\nprint(1)\n", true));
        UNSCOPED_INFO("none: LSL's # in SLua's place");
        CHECK_FALSE(P::usesDirectives("#define X 1\n", true));

        bool asked = false;
        const auto directives = [&asked](bool answer) {
            return [&asked, answer]() {
                asked = true;
                return answer;
            };
        };
        {
            INFO("its directives first, the setting on or off");
            CHECK(P::wanted(directives(true), true) == P::Wanted::Directives);
            CHECK(P::wanted(directives(true), false) == P::Wanted::Directives);
            CHECK(asked);
        }
        UNSCOPED_INFO("then the setting");
        CHECK(P::wanted(directives(false), true) == P::Wanted::Setting);
        UNSCOPED_INFO("else not, wrapped before or not: Tapple's");
        CHECK(P::wanted(directives(false), false) == P::Wanted::No);
        UNSCOPED_INFO("by text");
        CHECK(P::wanted("#define X 1\n", false, false) == P::Wanted::Directives);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a require only the old search found is said with a fix each way", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options(true);
        o.resolve                 = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            if (ask.name == "util")
            {
                out.searched = "/inc/util.luau";
                out.moves    = { { "@shared/util", "", "" }, { "@inc/util", "inc", "/inc" } };
            }
            return ALPreprocessor::Found::No;
        };
        const ALPreprocessor::Result r = ALPreprocessor::run("local util = require('util')\nlocal other = require(\"other\")\n", o);
        REQUIRE(r.problems.size() == size_t(2));
        const ALScriptProblem& moved = r.problems[0];
        UNSCOPED_INFO("where the search found it");
        CHECK(moved.key == std::string("PreprocModuleSearched"));
        {
            INFO("its words");
            CHECK(moved.args == std::vector<std::string>({ "util", "/inc/util.luau" }));
            REQUIRE(moved.message.find("/inc/util.luau") != std::string::npos);
        }
        UNSCOPED_INFO("a fix for each way");
        REQUIRE(moved.fixes.size() == size_t(2));
        const ALScriptFix& plain = moved.fixes[0];
        {
            INFO("the require's string put in, its quote kept");
            REQUIRE(plain.edits.size() == 1);
            CHECK(plain.edits[0].text == "'@shared/util'");
            CHECK(plain.edits[0].line == 0);
            CHECK(plain.edits[0].column == 21);
            CHECK(plain.edits[0].endColumn == 27);
        }
        CHECK(plain.preferred);
        CHECK(plain.key == "ScriptFixRequireAs");
        const ALScriptFix& alias = moved.fixes[1];
        {
            INFO("the alias's named in it, and not preferred");
            CHECK(alias.key == "ScriptFixRequireAlias");
            REQUIRE(alias.args.size() == 3);
            CHECK(alias.args[1] == "inc");
            CHECK_FALSE(alias.preferred);
            CHECK_FALSE(alias.safe);
        }
        UNSCOPED_INFO("one the search never found: as before");
        CHECK(r.problems[1].key == std::string("PreprocModuleNotFound"));
        UNSCOPED_INFO("with no fix");
        REQUIRE(r.problems[1].fixes.empty());
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a require of a file beside a same-named folder init warns naming both", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options(true);
        o.resolve                 = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            out.text       = "return 1\n";
            out.name       = "both.luau";
            out.path       = "disk:/proj/both.luau";
            out.passedOver = "both/init.luau";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result r = ALPreprocessor::run("local both = require('./both')\nprint(both)\n", o);
        REQUIRE(r.problems.size() == size_t(1));
        const ALScriptProblem& said = r.problems[0];
        CHECK(said.severity == ALScriptProblem::Severity::Warning);
        CHECK(said.key == "PreprocModuleBesideInit");
        UNSCOPED_INFO("naming both, each from the folder both are in");
        CHECK(said.args == std::vector<std::string>({ "./both", "both.luau", "both/init.luau" }));
        UNSCOPED_INFO("the module taken");
        REQUIRE(r.text.find("return 1") != std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: a missing require says why: keyed where known else in Luau's words", "[alpreprocessor]")
    {
        ALPreprocessor::Options o = options(true);
        o.resolve                 = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            out.why = ask.name == "./a" ? "could not resolve child component \"a\"" : ask.name == "./b" ? "something odd" : "";
            return ALPreprocessor::Found::No;
        };
        const ALPreprocessor::Result r = ALPreprocessor::run("require('./a')\nrequire('./b')\nrequire('./c')\n", o);
        REQUIRE(r.problems.size() == size_t(3));
        {
            INFO("keyed");
            CHECK(r.problems[0].key == "PreprocRequireNoChild");
            CHECK(r.problems[0].args == std::vector<std::string>({ "./a", "a" }));
            CHECK(r.problems[0].message == "could not find module './a': could not resolve child component \"a\"");
        }
        {
            INFO("in Luau's words");
            CHECK(r.problems[1].key == "PreprocModuleNotFoundWhy");
            CHECK(r.problems[1].args == std::vector<std::string>({ "./b", "something odd" }));
        }
        {
            INFO("no reason: as before");
            CHECK(r.problems[2].key == "PreprocModuleNotFound");
            CHECK(r.problems[2].args == std::vector<std::string>{ "./c" });
        }
    }

    // How often each module ran, as a module says it through `counted`,
    // a builtin of the VM, as SLua's are: a module's own globals are its
    // own, and what it sets is not the script's.
    static std::map<std::string, int> gRuns;

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua's bundled require is Luau's: each module once; nil runs again", "[alpreprocessor]")
    {
        add("count", "counted(\"count\")\nreturn {}\n");
        add("lazy", "counted(\"lazy\")\nreturn {}\n");
        add("never", "counted(\"never\")\nreturn {}\n");
        add("nothing", "counted(\"nothing\")\nreturn nil\n");
        add("own", "leaked = true\nlocal inner = require(\"count\")\nreturn { inner = inner, sees = leaked }\n");
        const ALPreprocessor::Result r = ALPreprocessor::run("local a = require(\"count\")\n"
                                                             "local b = require(\"count\")\n"
                                                             "same = a == b\n"
                                                             "local function later() return require(\"lazy\") end\n"
                                                             "lazyBefore = runs(\"lazy\")\n"
                                                             "later()\n"
                                                             "later()\n"
                                                             "local function unused() return require(\"never\") end\n"
                                                             "require(\"nothing\")\n"
                                                             "require(\"nothing\")\n"
                                                             "local own = require(\"own\")\n"
                                                             "shared = own.inner == a\n"
                                                             "ownSees = own.sees\n",
                                                             options(true));
        CHECK(messages(r) == std::string());

        // Run in SLua's VM, its globals read back.
        gRuns.clear();
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcfunction(L, [](lua_State* S) {
            ++gRuns[luaL_checkstring(S, 1)];
            return 0;
        }, "counted");
        lua_setglobal(L, "counted");
        lua_pushcfunction(L, [](lua_State* S) {
            lua_pushinteger(S, gRuns[luaL_checkstring(S, 1)]);
            return 1;
        }, "runs");
        lua_setglobal(L, "runs");
        // The script's own, as the grid's are: SLua's builtin runs only a
        // module of the scripter's making.
        lua_setmemcat(L, LUA_FIRST_USER_MEMCAT);
        const std::string bytecode = Luau::compile(r.text);
        UNSCOPED_INFO("loads: " << r.text);
        REQUIRE(luau_load(L, "=bundle", bytecode.data(), bytecode.size(), 0) == 0);
        const int status = lua_pcall(L, 0, 0, 0);
        UNSCOPED_INFO("runs: " << (status != 0 && lua_isstring(L, -1) ? lua_tostring(L, -1) : "") << "\n" << r.text);
        CHECK(status == 0);
        const auto truth = [L](const char* name) {
            lua_getglobal(L, name);
            const bool value = lua_toboolean(L, -1);
            lua_pop(L, 1);
            return value;
        };
        const auto number = [L](const char* name) {
            lua_getglobal(L, name);
            const double value = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return value;
        };
        UNSCOPED_INFO("required three times, run once");
        CHECK(gRuns["count"] == 1);
        {
            INFO("and the same value each time");
            CHECK(truth("same"));
            CHECK(truth("shared"));
        }
        UNSCOPED_INFO("not run before its first require");
        CHECK(number("lazyBefore") == 0.0);
        UNSCOPED_INFO("then once, however often required");
        CHECK(gRuns["lazy"] == 1);
        UNSCOPED_INFO("never required, never run");
        CHECK(gRuns["never"] == 0);
        UNSCOPED_INFO("nil kept by nothing: run again, as Luau runs it");
        CHECK(gRuns["nothing"] == 2);
        {
            INFO("a module's global its own");
            CHECK(truth("ownSees"));
            CHECK_FALSE(truth("leaked"));
        }
        lua_close(L);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: @line comments where asked agree with the map; none if nothing moved", "[alpreprocessor]")
    {
        // Read as the plugin reads them: the line after a comment is the
        // line it says of the file it names, and each line after the next.
        const auto readsTrue = [](const ALPreprocessor::Result& r, const std::string& script, std::string& why) {
            std::string        name = script;
            S32                line = 1;
            std::istringstream in(r.text);
            std::string        text;
            for (S32 out = 0; std::getline(in, text); ++out)
            {
                const size_t first = text.find_first_not_of(" \t");
                if (first == std::string::npos)
                {
                    ++line;
                    continue;
                }
                const std::string said = text.substr(first);
                if (said.rfind("-- @line ", 0) == 0 || said.rfind("// @line ", 0) == 0)
                {
                    const size_t quote = said.find('"');
                    line = std::atoi(said.c_str() + 9);
                    name = said.substr(quote + 1, said.size() - quote - 2);
                    continue;
                }
                const ALSourceMap::Loc at = r.map.toSource(out, S32(first));
                if (at.found() && (r.map.files()[at.file].name != name || at.line + 1 != line))
                {
                    why = "output line " + std::to_string(out) + " is " + r.map.files()[at.file].name + ":" + std::to_string(at.line + 1) +
                          ", read as " + name + ":" + std::to_string(line) + "\n" + r.text;
                    return false;
                }
                ++line;
            }
            return true;
        };
        std::string why;

        // SLua: each module's first line said, and the script's after them.
        add("util", "local M = {}\n\nfunction M.f() return 1 end\nreturn M\n");
        add("deep", "--#define K 2\nreturn { k = K }\n");
        ALPreprocessor::Options lua = options(true);
        lua.lineComments            = true;
        const std::string script    = "--!strict\nlocal util = require(\"util\")\n\nlocal deep = require(\"deep\")\nprint(util.f(), deep.k)\n";
        ALPreprocessor::Result r    = ALPreprocessor::run(script, lua);
        CHECK(messages(r) == std::string());
        bool reads = readsTrue(r, "main.luau", why);
        UNSCOPED_INFO("the reader finds every line: " << why);
        CHECK(reads);
        UNSCOPED_INFO("the header still first: " << r.text);
        CHECK(r.text.rfind("--!strict\ndo\n", 0) == 0);
        UNSCOPED_INFO("a module's first: " << r.text);
        REQUIRE(r.text.find("        [1] = function()\n-- @line 1 \"util\"\nlocal M = {}\n") != std::string::npos);
        UNSCOPED_INFO("one past a directive: " << r.text);
        REQUIRE(r.text.find("-- @line 2 \"deep\"\nreturn { k = 2 }\n") != std::string::npos);
        UNSCOPED_INFO("the script's after the modules: " << r.text);
        REQUIRE(r.text.find("end\n-- @line 2 \"main.luau\"\nlocal util = require(1)\n") != std::string::npos);
        UNSCOPED_INFO("three, no more");
        CHECK(std::count(r.text.begin(), r.text.end(), '@') == 3);
        UNSCOPED_INFO("none mapped anywhere");
        REQUIRE_FALSE(r.map.toSource(S32(std::count(r.text.begin(), r.text.begin() + r.text.find("-- @line 2 \"main"), '\n')), 0).found());
        const ALPreprocessor::Result plain = ALPreprocessor::run(script, options(true));
        UNSCOPED_INFO("off: none");
        REQUIRE(plain.text.find("@line") == std::string::npos);

        // LSL: an include's first line, the script's after it, a stretch a
        // condition left out, a macro over lines.
        add("inc.lsl", "integer x;\n\ninteger y;\n");
        ALPreprocessor::Options lsl = options();
        lsl.lineComments            = true;
        r = ALPreprocessor::run("integer a;\n#include \"inc.lsl\"\n#if 0\ninteger gone;\n\n#endif\n#define TWICE(v) \\\n    ((v) * 2)\ninteger b = TWICE(3);\ndefault { state_entry() { } }\n", lsl);
        CHECK(messages(r) == std::string());
        reads = readsTrue(r, "main.lsl", why);
        UNSCOPED_INFO("the reader finds every line: " << why);
        CHECK(reads);
        UNSCOPED_INFO("in LSL's comment: " << r.text);
        REQUIRE(r.text.find("// @line 1 \"inc.lsl\"\ninteger x;\n") != std::string::npos);

        // Nothing moved: none at all.
        r = ALPreprocessor::run("integer a;\n\ndefault { state_entry() { } }\n", lsl);
        UNSCOPED_INFO("nothing to say: " << r.text);
        REQUIRE(r.text.find("@line") == std::string::npos);
    }

    TEST_CASE_METHOD(alpreprocessor_data, "alpreprocessor: SLua: an unread require is noted with its weight and kept bundled", "[alpreprocessor]")
    {
        add("util", "local M = {}\nfunction M.twice(n) return n * 2 end\nreturn M\n");
        add("used", "return 1\n");
        const auto notes = [](const ALPreprocessor::Result& r) {
            std::vector<const ALScriptProblem*> out;
            for (const ALScriptProblem& p : r.problems)
            {
                if (p.key == "PreprocRequireUnread")
                {
                    out.push_back(&p);
                }
            }
            return out;
        };

        ALPreprocessor::Result r = ALPreprocessor::run("local util = require(\"util\")\nlocal used = require(\"used\")\nprint(used)\n", options(true));
        std::vector<const ALScriptProblem*> said = notes(r);
        UNSCOPED_INFO("one note: " << messages(r));
        REQUIRE(said.size() == size_t(1));
        {
            INFO("a note, at the require");
            REQUIRE(said[0]->severity == ALScriptProblem::Severity::Note);
            REQUIRE(said[0]->line == 0);
            CHECK(said[0]->column == 13);
        }
        {
            INFO("naming the local and the module");
            REQUIRE(said[0]->args.size() == 3);
            REQUIRE(said[0]->args[0] == "util");
            CHECK(said[0]->args[1] == "util");
        }
        UNSCOPED_INFO("with its weight: " << said[0]->args[2]);
        CHECK(std::atoi(said[0]->args[2].c_str()) > 0);
        UNSCOPED_INFO("still in the bundle");
        REQUIRE(r.text.find("M.twice") != std::string::npos);

        UNSCOPED_INFO("a name begun with _ is meant");
        REQUIRE(notes(ALPreprocessor::run("local _util = require(\"util\")\n", options(true))).empty());
        UNSCOPED_INFO("read through another require of it: removing this one saves nothing");
        REQUIRE(notes(ALPreprocessor::run("local a = require(\"util\")\nlocal b = require(\"util\")\nprint(b)\n", options(true))).empty());
        UNSCOPED_INFO("read in an interpolated string");
        REQUIRE(notes(ALPreprocessor::run("local util = require(\"util\")\nprint(`{util}`)\n", options(true))).empty());
        UNSCOPED_INFO("the require not the whole of the value");
        REQUIRE(notes(ALPreprocessor::run("local util = require(\"util\") or {}\nprint(1)\n", options(true))).empty());
        UNSCOPED_INFO("a require as a statement, for what it does");
        REQUIRE(notes(ALPreprocessor::run("require(\"util\")\n", options(true))).empty());

        // In a module: said in its file.
        ALPreprocessor::Include wrap;
        wrap.text      = "local inner = require(\"util\")\nreturn 1\n";
        wrap.name      = "wrap.luau";
        wrap.path      = "disk:/lib/wrap.luau";
        files["wrap"]  = wrap;
        r              = ALPreprocessor::run("local w = require(\"wrap\")\nprint(w)\n", options(true));
        said           = notes(r);
        UNSCOPED_INFO("one, in the module: " << messages(r));
        REQUIRE(said.size() == size_t(1));
        {
            INFO("in its file, at its require");
            REQUIRE(said[0]->file == "disk:/lib/wrap.luau");
            REQUIRE(said[0]->line == 0);
            REQUIRE(said[0]->args.size() == 3);
            CHECK(said[0]->args[0] == "inner");
        }
    }
}
