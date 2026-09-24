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

#include "../allslservice.h"
#include "../alpreprocessor.h"

#include "../test/lltut.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace tut
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

    typedef test_group<alpreprocessor_data> alpreprocessor_group;
    typedef alpreprocessor_group::object    alpreprocessor_object;
    alpreprocessor_group                    alpreprocessor_instance("alpreprocessor");

    template<> template<>
    void alpreprocessor_object::test<1>()
    {
        set_test_name("object-like and function-like macros expand in code and nowhere else");
        const std::string source = "#define GREETING \"hello\"\n"
                                   "#define TWICE(x) x + x\n"
                                   "default { state_entry() { llSay(0, GREETING); integer y = TWICE(2); string s = \"GREETING\"; /* GREETING */ } }";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", r.text,
                      std::string("\n\ndefault { state_entry() { llSay(0, \"hello\"); integer y = 2 + 2; string s = \"GREETING\"; /* GREETING */ } }"));
        ensure("not disabled", !r.disabled);

        // A function-like macro's name on its own is only a name, and its
        // arguments may run over lines.
        r = ALPreprocessor::run("#define M(a, b) a+b\nM + 1\nM(1,\n 2)\n", options());
        ensure_equals("name alone", r.text, std::string("\nM + 1\n1+2\n"));
        ensure_equals("no problems", messages(r), std::string());

        // `#define` needs a name; an unknown directive is an error; a
        // redefinition is a warning; `#undef` takes it away.
        r = ALPreprocessor::run("#define\n#foo\n#define A 1\n#define A 2\nA\n#undef A\nA\n", options());
        ensure_equals("errors", messages(r), std::string("E 0: macro name missing in #define\nE 1: ill formed preprocessor directive '#foo'\nW 3: macro 'A' redefined\n"));
        ensure_equals("undef", squeeze(r.text), std::string("2\nA\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<2>()
    {
        set_test_name("variadics, stringizing and pasting");
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
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", squeeze(r.text),
                      std::string("\"hello world\"\n"
                                  "\"\\\"q\\\\n\\\"\"\n"
                                  "llSay(0, \"x\")\n"
                                  "llOwnerSay(\"a, b\")\n"
                                  "x x 12\n"
                                  "\"__LINE__\"\n"
                                  "1  1 2, 3\n"));

        r = ALPreprocessor::run("#define P(a) ## a\n#define Q(a) # b\n#define R(a, a) a\nP(1)\n", options());
        ensure_equals("refused", messages(r),
                      std::string("E 0: '##' cannot be at either end of a macro body\n"
                                  "E 1: '#' is not followed by a macro parameter\n"
                                  "E 2: duplicate macro parameter 'a'\n"));
        ensure_equals("nothing defined", squeeze(r.text), std::string("P(1)\n"));

        r = ALPreprocessor::run("#define M(a, b) a b\nM(1)\nM(1, 2, 3)\nM(1\n", options());
        ensure_equals("arity", messages(r),
                      std::string("E 1: too few arguments for macro 'M'\nE 2: too many arguments for macro 'M'\nE 3: unterminated argument list invoking macro 'M'\n"));
        ensure_equals("left as written", squeeze(r.text), std::string("M(1)\nM(1, 2, 3)\nM(1\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<3>()
    {
        set_test_name("rescanning stops where the standard says");
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
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", squeeze(r.text), std::string("2*9*g\nfoo\na b AB\nI(3)\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<4>()
    {
        set_test_name("conditionals over C integer expressions keep their lines");
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
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", squeeze(r.text), std::string("yes\nyes2\nyes3\nyes4\nyes5\nyes6\nend\n"));
        std::istringstream in(r.text);
        std::string        line;
        S32                n = 0;
        while (std::getline(in, line))
        {
            ++n;
        }
        ensure_equals("one line each", n, 37);
        ensure_equals("yes4 on its line", r.text.find("yes4"), r.text.find("yes4"));
        ALSourceMap::Loc loc = r.map.toSource(19, 0);
        ensure_equals("yes4 from line 19", loc.line, 19);

        r = ALPreprocessor::run("#if 1\n#else\n#else\n#endif\n#endif\n#elif 1\n#if 1/0\n#endif\n#if 1 +\n#endif\n#if (1\n#endif\n#if 1\n", options());
        ensure_equals("errors", messages(r),
                      std::string("E 2: #else after #else\n"
                                  "E 4: #endif without #if\n"
                                  "E 5: #elif without #if\n"
                                  "E 6: division by zero in preprocessor expression\n"
                                  "E 8: expected a value in preprocessor expression\n"
                                  "E 10: expected ')' in preprocessor expression\n"
                                  "E 12: #if without #endif at the end of the file\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<5>()
    {
        set_test_name("includes come from the resolver, once if they ask, and say which file they are");
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
        ensure_equals("problems", messages(r), std::string("E 5: could not find include file 'missing.lsl'\n"));
        ensure_equals("text", r.text,
                      std::string("\ninteger a = 1; string f = \"a.lsl\"; string id = \"aaaa\";\n"
                                  "string top = \"main.lsl\"; string tid = \"NOT_IN_WORLD\";\n"
                                  "\n\nonce\n"
                                  "\n\n\n\n\n\n7\n"));
        ensure_equals("includes", r.includes.size(), 3u);
        ensure_equals("first include", r.includes[0], std::string("a.lsl"));
        ensure_equals("pending", r.pending.size(), 1u);
        ensure_equals("pending name", r.pending[0], std::string("pending.lsl"));
        ensure_equals("files", r.map.files().size(), 4u);
        ensure_equals("main file", r.map.files()[0].name, std::string("main.lsl"));
        ensure_equals("an include by its identity", r.map.fileOf("once.lsl"), 2);

        r = ALPreprocessor::run("#include \"deep.lsl\"\n", options());
        ensure("too deep", messages(r).find("nested too deeply") != std::string::npos);
        ensure("in the include", r.problems.front().file == "deep.lsl");

        r = ALPreprocessor::run("#include \"err.lsl\"\nafter\n", options());
        ensure_equals("error in an include", messages(r), std::string("E err.lsl:0: bad\n"));
        ensure_equals("goes on", squeeze(r.text), std::string("after\n"));

        ALPreprocessor::Options none = options();
        none.resolve                 = nullptr;
        r                            = ALPreprocessor::run("#include \"a.lsl\"\n", none);
        ensure_equals("no resolver", messages(r), std::string("E 0: could not find include file 'a.lsl'\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<6>()
    {
        set_test_name("Firestorm's LSL quirks: strings over lines, the off switch, #line, and the predefined macros");
        ALPreprocessor::Result r = ALPreprocessor::run("string s = \"a\nb\";\nx", options());
        ensure_equals("a newline in a string", r.text, std::string("string s = \"a\\nb\"\n;\nx"));

        const std::string off = "//fspreprocessor off\n#define X 1\nX\n";
        r                     = ALPreprocessor::run(off, options());
        ensure("disabled", r.disabled);
        ensure_equals("as it was", r.text, off);
        ensure_equals("maps to itself", r.map.toSource(2, 0).line, 2);

        r = ALPreprocessor::run("#line 5 \"x\"\ny\n", options());
        ensure_equals("line passed through", r.text, std::string("//#line 5 \"x\"\ny\n"));

        ALPreprocessor::Options o = options();
        o.agentId                 = "1234";
        o.agentName               = "Some Body";
        o.assetId                 = "asset-1";
        r = ALPreprocessor::run("__LINE__ __FILE__ __SHORTFILE__ __ASSETID__\n__AGENTKEY__ __AGENTID__ __AGENTIDRAW__ __AGENTNAME__ __UNIXTIME__\n"
                                "integer(1.5) list(1, 2) key(x)\n__DATE__ __TIME__\n",
                                o);
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("line one", r.text.substr(0, r.text.find('\n')), std::string("1 \"main.lsl\" \"main.lsl\" \"asset-1\""));
        std::istringstream in(r.text);
        std::string        line;
        std::getline(in, line);
        std::getline(in, line);
        ensure_equals("line two", line, std::string("\"1234\" \"1234\" 1234 \"Some Body\" 1234567890"));
        std::getline(in, line);
        ensure_equals("line three", line, std::string("((integer)(1.5)) ((list)(1, 2)) ((key)(x))"));
        std::getline(in, line);
        ensure_equals("date and time", line.size(), 13u + 1u + 10u);
        ensure("date quoted", line[0] == '"' && line[12] == '"');
        ensure("time quoted", line[14] == '"' && line[23] == '"');
        ensure_equals("time colons", line.substr(17, 1) + line.substr(20, 1), std::string("::"));

        r = ALPreprocessor::run("#define BS 1 \\\n + 2\nBS\n", options());
        ensure_equals("continuation", squeeze(r.text), std::string("1 + 2\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<7>()
    {
        set_test_name("switch becomes the jump table Firestorm emits");
        const std::string source = "#define USE_SWITCHES\n"
                                   "switch (x) {\n"
                                   "case 1: llSay(0, \"one\"); break;\n"
                                   "case 2 { llSay(0, \"two\"); }\n"
                                   "default: llSay(0, \"other\");\n"
                                   "}\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        ensure_equals("problems", messages(r), std::string());
        ensure("used", r.usedSwitches);
        ensure_equals("text", r.text,
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
        ensure_equals("nested", r.text,
                      std::string("{if((a) == (1))jump _sw2_1;\njump _sw2_end;\n"
                                  " @_sw2_1; {if((b) == (2))jump _sw1_1;\njump _sw1_end;\n"
                                  " @_sw1_1; jump _sw1_end; \n@_sw1_end;\n} jump _sw2_end; \n@_sw2_end;\n}\n"));
        ensure_equals("not used when not asked", ALPreprocessor::run("switch (a) { }\n", options()).text, std::string("switch (a) { }\n"));
        r = ALPreprocessor::run("switch (a) { case 1 }\n", o);
        ensure_equals("a case with no end", messages(r), std::string("E 0: cannot find ':' or '{' after case\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<8>()
    {
        set_test_name("lazy lists read and write through the helper");
        const std::string source = "#define USE_LAZY_LISTS\n"
                                   "list l; integer i = (integer)l[2]; l[0] = 5; string s = (string)(l[(integer)l[1]]); rotation r = (rotation)l[0];\n"
                                   "x = (integer)(l[0]; f((float)l[0], l[1] = 2);\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        ensure_equals("problems", messages(r), std::string());
        ensure("used", r.usedLazyLists);
        const std::string helper = "list lazy_list_set(list L, integer i, list v)\n{\n    while (llGetListLength(L) < i)\n        L = L + 0;\n    return llListReplaceList(L, v, i, i);\n}\n\n";
        ensure_equals("text", r.text,
                      helper + "\nlist l; integer i = llList2Integer(l, 2); l = lazy_list_set(l,0,[5]); string s = llList2String(l, llList2Integer(l, 1)); rotation r = llList2Rot(l, 0);\n"
                               "x = (integer)(l[0]; f(llList2Float(l, 0), l = lazy_list_set(l,1,[2]));\n");
        ALPreprocessor::Options o = options();
        o.lazyLists               = true;
        r                         = ALPreprocessor::run("integer a = b == c;\n", o);
        ensure("nothing to do", !r.usedLazyLists);
        ensure_equals("untouched", r.text, std::string("integer a = b == c;\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<9>()
    {
        set_test_name("compression drops comments and every blank that is not needed");
        ALPreprocessor::Options o = options();
        o.compress                = true;
        ALPreprocessor::Result r =
            ALPreprocessor::run("// comment\ndefault   {  state_entry ( ) { integer a = - -1 ; /* x */ llSay( 0 , \"a  b\" ) ; a ++ ; float f = 1 . 5; } }\n", o);
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", r.text, std::string("default{state_entry(){integer a=- -1;llSay(0,\"a  b\");a++;float f=1 . 5;}}\n"));
        r = ALPreprocessor::run("a\n\n\nb / /* c */ d\n", o);
        ensure_equals("lines kept, not multiplied", r.text, std::string("a\nb/d\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<10>()
    {
        set_test_name("SLua: its own strings and comments, # in a line, and require gathered into modules");
        const std::string source = "#define N 3\n"
                                   "local s = [[N]] -- N\n"
                                   "local t = \"N\" .. #N_list .. 'N' .. `N{N}`\n"
                                   "--[==[ N\nN ]==]\n"
                                   "print(N)\n"
                                   "#line 3\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options(true));
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", r.text,
                      std::string("\nlocal s = [[N]] -- N\nlocal t = \"N\" .. #N_list .. 'N' .. `N{N}`\n--[==[ N\nN ]==]\nprint(3)\n--#line 3\n"));

        add("a", "local b = require(\"b\")\nreturn { b = b }\n");
        add("b", "return 42\n");
        add("c", "return require(\"c\")\n");
        r = ALPreprocessor::run("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n", options(true));
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("modules", r.text,
                      std::string("local __modules = {}\n"
                                  "__modules[\"b\"] = (function()\nreturn 42\nend)()\n"
                                  "__modules[\"a\"] = (function()\nlocal b = __modules[\"b\"]\nreturn { b = b }\nend)()\n"
                                  "local a = __modules[\"a\"]\nlocal b = __modules[\"b\"]\nlocal x = require(name)\n"));
        ensure_equals("included", r.includes.size(), 2u);
        ensure_equals("b mapped to its file", r.map.files()[r.map.toSource(2, 0).file].name, std::string("b"));
        r = ALPreprocessor::run("local c = require(\"c\")\n", options(true));
        ensure_equals("a cycle", messages(r), std::string("E c:0: 'c' requires itself\n"));
        r = ALPreprocessor::run("local d = require(\"d\")\n", options(true));
        ensure_equals("missing", messages(r), std::string("E 0: could not find module 'd'\n"));
        ensure_equals("left alone", r.text, std::string("local d = require(\"d\")\n"));

        r = ALPreprocessor::run("--fspreprocessor off\n#define X\n", options(true));
        ensure("off in Lua", r.disabled);
    }

    template<> template<>
    void alpreprocessor_object::test<11>()
    {
        set_test_name("the source map goes both ways");
        add("inc", "x;\n");
        ALPreprocessor::Result r = ALPreprocessor::run("#define X 10\ninteger a = X + 1;\n#include \"inc\"\ny;\n", options());
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", r.text, std::string("\ninteger a = 10 + 1;\n\nx;\ny;\n"));
        ALSourceMap::Loc loc = r.map.toSource(1, 0);
        ensure("found", loc.found());
        ensure_equals("integer", loc.line * 1000 + loc.column, 1000);
        loc = r.map.toSource(1, 5);
        ensure_equals("within integer", loc.column, 5);
        loc = r.map.toSource(1, 12);
        ensure_equals("the 10 is the X", loc.column, 12);
        loc = r.map.toSource(1, 13);
        ensure_equals("still the X", loc.column, 12);
        loc = r.map.toSource(1, 15);
        ensure_equals("the + past the X", loc.column, 14);
        loc = r.map.toSource(1, 40);
        ensure_equals("past the end", loc.column, 18);
        loc = r.map.toSource(0, 0);
        ensure("a directive line has no origin", !loc.found());
        loc = r.map.toSource(3, 0);
        ensure_equals("from the include", loc.file, 1);
        ensure_equals("its line", loc.line, 0);
        loc = r.map.toSource(4, 0);
        ensure_equals("after the include", loc.file * 1000 + loc.line, 3);

        loc = r.map.toExpanded(0, 1, 12);
        ensure("forward found", loc.found());
        ensure_equals("X went to line 1", loc.line, 1);
        ensure_equals("X went to column 12", loc.column, 12);
        loc = r.map.toExpanded(0, 1, 3);
        ensure_equals("within integer forward", loc.column, 3);
        loc = r.map.toExpanded(0, 0, 3);
        ensure("a directive made nothing", !loc.found());
        loc = r.map.toExpanded(0, 3, 0);
        ensure_equals("y went past the include", loc.line, 4);
        loc = r.map.toExpanded(1, 0, 1);
        ensure_equals("the include's x", loc.line, 3);
        loc = r.map.toExpanded(0, 1, 100);
        ensure_equals("past the end forward", loc.column, 19);
    }

    template<> template<>
    void alpreprocessor_object::test<13>()
    {
        set_test_name("the optimizer runs over the expanded text, and its notes come back to the source");
        {
            ALLSLService service;
            std::string  error;
            ensure("builtins: " + error, service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));
        }
        add("consts.lsl", "#define CHANNEL 7\ninteger unused = 1;\n");
        ALPreprocessor::Options o = options();
        o.optimize                = true;
        o.compress                = true;
        const std::string source  = "#include \"consts.lsl\"\n#define TWICE(x) ((x) * 2)\ndefault\n{\n    state_entry()\n    {\n        llSay(CHANNEL, (string)TWICE(21));\n    }\n}\n";
        ALPreprocessor::Result r  = ALPreprocessor::run(source, o);
        ensure("optimized", r.optimized);
        ensure_equals("text", r.text, std::string("default\n{\nstate_entry()\n{\nllSay(7,\"42\");\n}\n}\n"));
        // The unused global was in the include, and the note says so.
        bool noted = false;
        for (const ALScriptProblem& p : r.problems)
        {
            if (p.message.find("removed the unused global unused") != std::string::npos)
            {
                noted = true;
                ensure_equals("in the include", p.file, std::string("consts.lsl"));
                ensure_equals("on its line there", p.line, 1);
            }
        }
        ensure("noted", noted);
        // The llSay is on output line 4, which came from source line 6.
        const ALSourceMap::Loc loc = r.map.toSource(4, 0);
        ensure("found", loc.found());
        ensure_equals("main file", loc.file, 0);
        ensure_equals("line", loc.line, 6);
    }

    template<> template<>
    void alpreprocessor_object::test<12>()
    {
        set_test_name("every golden file comes out as its expected text");
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(AL_ALSCRIPT_TEST_DIR) / "preprocessor";
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
            const auto        read     = [](const fs::path& path) {
                std::ifstream     in(path, std::ios::binary);
                std::stringstream buffer;
                buffer << in.rdbuf();
                return buffer.str();
            };
            const std::string source = read(entry.path());
            const bool              lua = entry.path().extension() == ".luau";
            ALPreprocessor::Options o   = options(lua);
            o.fileName                  = name;
            o.resolve                   = [&](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
                // A require names its module without the extension.
                for (const char* ext : { "", ".luau", ".lsl" })
                {
                    const fs::path path = dir / "include" / (ask.name + ext);
                    if (fs::exists(path))
                    {
                        out.text = read(path);
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
            ALPreprocessor::Result r = ALPreprocessor::run(source, o);
            ensure_equals("problems in " + name, messages(r), std::string());
            // With AL_PREPROCESSOR_WRITE_EXPECTED set, the run writes the
            // expected files instead of checking them: for a new golden
            // file, or a change that was meant, each to be read over
            // before it is kept.
            if (const char* write = std::getenv("AL_PREPROCESSOR_WRITE_EXPECTED"); write && *write)
            {
                std::ofstream out(expected, std::ios::binary);
                out << r.text;
                continue;
            }
            ensure("expected file for " + name, fs::exists(expected));
            ensure_equals("output of " + name, r.text, read(expected));
            ++checked;
        }
        ensure("some golden files", checked > 0 || std::getenv("AL_PREPROCESSOR_WRITE_EXPECTED"));
    }
    template<> template<>
    void alpreprocessor_object::test<14>()
    {
        set_test_name("break and continue become jumps to labels after the loop and at the body's end, and &= |= ^= <<= >>= the assignments they are");
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("while (a) { if (b) break; if (c) continue; d++; }\n", o);
        ensure_equals("problems", messages(r), std::string());
        ensure("used", r.usedExtensions);
        ensure_equals("a while", r.text, std::string("while (a) { if (b) jump _brk1; if (c) jump _cnt1; d++; @_cnt1;}@_brk1;\n"));
        r = ALPreprocessor::run("for (i = 0; i < 3; i++) if (i == 1) continue; else x++;\n", o);
        ensure_equals("a for with one statement for a body gets braces", r.text, std::string("for (i = 0; i < 3; i++) { if (i == 1) jump _cnt1; else x++;@_cnt1;}\n"));
        r = ALPreprocessor::run("do { if (x) break 2; } while (y);\n", o);
        ensure_equals("break 2 with one loop is an error", messages(r), std::string("E 0: break outside a loop\n"));
        r = ALPreprocessor::run("while (a) { do { if (x) break 2; if (y) break; } while (b); c++; }\n", o);
        ensure_equals("break 2 goes past the outer loop, break past the inner", r.text,
                      std::string("while (a) { do { if (x) jump _brk1; if (y) jump _brk2; } while (b);@_brk2; c++; }@_brk1;\n"));
        r = ALPreprocessor::run("continue;\n", o);
        ensure_equals("outside a loop", messages(r), std::string("E 0: continue outside a loop\n"));
        o.switches = true;
        r          = ALPreprocessor::run("while (a) { switch (b) { case 1: break; } while (c) { break; } }\n", o);
        ensure_equals("a break in a switch is the switch's, one in a loop inside it the loop's", r.text,
                      std::string("while (a) { {if((b) == (1))jump _sw1_1;\njump _sw1_end;\n @_sw1_1; jump _sw1_end; \n@_sw1_end;\n} while (c) { jump _brk2; }@_brk2; }\n"));
        r = ALPreprocessor::run("x &= 6; v.y |= 1 << n; z ^= (a | b); f(k <<= 2, 3); m >>= 1 + p;\n", o);
        ensure_equals("the assignments", r.text, std::string("x = x & (6); v.y = v.y | (1 << n); z = z ^ ((a | b)); f(k = k << (2), 3); m = m >> (1 + p);\n"));
        ensure_equals("not used when not asked", ALPreprocessor::run("while (a) break;\n", options()).text, std::string("while (a) break;\n"));
        r = ALPreprocessor::run("#define USE_EXTENSIONS\nwhile (a) break;\n", options());
        ensure_equals("USE_EXTENSIONS turns it on", r.text, std::string("\nwhile (a) jump _brk1;@_brk1;\n"));
        r = ALPreprocessor::run("inline f() { }\ninline integer g(integer x) { return x; }\ninline = 3;\n", o);
        ensure_equals("inline before a function is taken off and the name kept", r.text, std::string("f() { }\ninteger g(integer x) { return x; }\ninline = 3;\n"));
        ensure("the names", r.inlined.size() == 2 && r.inlined[0] == "f" && r.inlined[1] == "g");
        r = ALPreprocessor::run("integer break; while (a) { break = 1; x = continue; }\n", o);
        ensure_equals("the words as names are errors, not rewrites", messages(r),
                      std::string("E 0: 'break' is a name here, which break and continue reserve\n"
                                  "E 0: 'break' is a name here, which break and continue reserve\n"
                                  "E 0: 'continue' is a name here, which break and continue reserve\n"));
        ensure_equals("and left as they were", r.text, std::string("integer break; while (a) { break = 1; x = continue; }\n"));
    }
    template<> template<>
    void alpreprocessor_object::test<15>()
    {
        set_test_name("a label the script has where a loop's would go is the loop's");
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("while (a) { if (b) break; c++; }\n@out;\n", o);
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("a label after the loop is the break's", r.text, std::string("while (a) { if (b) jump out; c++; }\n@out;\n"));
        r = ALPreprocessor::run("while (a) { if (b) continue; c++; @next; }\n", o);
        ensure_equals("a label ending the body is the continue's", r.text, std::string("while (a) { if (b) jump next; c++; @next; }\n"));
        r = ALPreprocessor::run("do { if (b) break 2; } while (a); @x;\n", o);
        ensure_equals("break 2 with one loop is still an error", messages(r), std::string("E 0: break outside a loop\n"));
        r = ALPreprocessor::run("for (;;) { while (a) { if (b) break 2; } @in; }\n@after;\n", o);
        ensure_equals("each loop's own: break 2 to the outer's following label, the inner's ending label unused", r.text,
                      std::string("for (;;) { while (a) { if (b) jump after; } @in; }\n@after;\n"));
        r = ALPreprocessor::run("while (a) { break; } x = 1; @late;\n", o);
        ensure_equals("a label that is not right after the loop is not the loop's", r.text, std::string("while (a) { jump _brk1; }@_brk1; x = 1; @late;\n"));
    }
    template<> template<>
    void alpreprocessor_object::test<16>()
    {
        set_test_name("a problem of the preprocessor's own carries a key and its words, for a translation to be built the same way");
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("continue;\n#define f(a, a) a\n", o);
        // The directives go first, the extensions after: the macro's
        // problem is said before the loop's.
        ensure_equals("two problems", r.problems.size(), size_t(2));
        ensure_equals("the words", r.problems[1].message, std::string("continue outside a loop"));
        ensure_equals("the key", r.problems[1].key, std::string("PreprocOutsideLoop"));
        ensure("the word it was built with", r.problems[1].args.size() == 1 && r.problems[1].args[0] == "continue");
        ensure_equals("the macro's key", r.problems[0].key, std::string("PreprocDuplicateParameter"));
        ensure("and its word", r.problems[0].args.size() == 1 && r.problems[0].args[0] == "a");
        ensure_equals("a translation is the text with the words put in", ALScriptProblem::fill("[1] hors d'une boucle ([1], [2])", { "continue", "x" }), std::string("continue hors d'une boucle (continue, x)"));
        ensure_equals("a word with a mark in it is not read again", ALScriptProblem::fill("[1]", { "[2]" }), std::string("[2]"));
    }
    template<> template<>
    void alpreprocessor_object::test<17>()
    {
        set_test_name("a run is bounded: macros that double are stopped, a deep expression is refused, and a deep include is");
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
        ensure("the run said it ran away", r.overran);
        ensure("and gave the source back as it was", r.text == bomb);
        ensure("with the reason", !r.problems.empty() && r.problems.back().key == std::string("PreprocTooMuch"));
        ensure("an error", r.problems.back().severity == ALScriptProblem::Severity::Error);
        // A run within the budget is untouched by it.
        ALPreprocessor::Result fine = ALPreprocessor::run("#define A 1\ninteger n = A;\n", o);
        ensure("nothing said", !fine.overran);
        ensure("and the macro put in", fine.text.find("integer n = 1;") != std::string::npos);
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
        ensure("said", !nested.problems.empty());
        ensure_equals("with its key", nested.problems.front().key, std::string("PreprocExpressionTooDeep"));
        // And one within the depth is answered.
        ALPreprocessor::Result shallow = ALPreprocessor::run("#if ((((1))))\ninteger n;\n#endif\n", d);
        ensure("nothing said", shallow.problems.empty());
        ensure("the group taken", shallow.text.find("integer n;") != std::string::npos);
    }

    template<> template<>
    void alpreprocessor_object::test<18>()
    {
        set_test_name("an include that includes itself is stopped by the depth, and each include is listed once");
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
        ensure("said so", deep);
        ensure_equals("each listed once", r.includes.size(), size_t(2));
        // With `#pragma once` it settles rather than going deep at all.
        files.clear();
        add("c.lsl", "#pragma once\ninteger c;\n");
        ALPreprocessor::Result twice = ALPreprocessor::run("#include \"c.lsl\"\n#include \"c.lsl\"\n", o);
        ensure("nothing said", twice.problems.empty());
        ensure_equals("listed once", twice.includes.size(), size_t(1));
        ensure_equals("and put in once", twice.text.find("integer c;"), twice.text.rfind("integer c;"));
    }
    template<> template<>
    void alpreprocessor_object::test<19>()
    {
        set_test_name("what a run does after the expansion it will do on its own, and to the same end");
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
        ensure("the expansion alone leaves it as it was", in_two.text.find("// comment") != std::string::npos);
        ALPreprocessor::finish(in_two, whole);
        ensure_equals("the same text", in_two.text, at_once.text);
        ensure_equals("and the same way back", in_two.map.toSource(0, 0).line, at_once.map.toSource(0, 0).line);
    }

    template<> template<>
    void alpreprocessor_object::test<20>()
    {
        set_test_name("a scripter's own macros are defined for every script, and a script's #undef has the last word");
        ALPreprocessor::Options opts = options();
        opts.defines = { "DEBUG", "LEVEL=3", " SPACED = 7 ", "2BAD", "" };
        ALPreprocessor::Result r = ALPreprocessor::run("#if DEBUG\nx = LEVEL;\n#endif\ny = SPACED;\n#undef LEVEL\nz = LEVEL;\n", opts);
        ensure_equals("no problems", messages(r), std::string());
        ensure("DEBUG is 1, so the block is in: " + r.text, r.text.find("x = 3;") != std::string::npos);
        ensure("a value around blanks: " + r.text, r.text.find("y = 7;") != std::string::npos);
        ensure("undefined by the script: " + r.text, r.text.find("z = LEVEL;") != std::string::npos);
    }
    template<> template<>
    void alpreprocessor_object::test<21>()
    {
        set_test_name("a run is bounded however the script nests: chained ?:, macros in macros' arguments, files that include themselves");
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
        ensure("the chain is too deep: " + messages(r).substr(0, 200), messages(r).find("nests too deeply") != std::string::npos);

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
        ensure("overran", r.overran);
        ensure("and said why: " + messages(r).substr(0, 200), messages(r).find("more deeply than this preprocessor follows") != std::string::npos);
        ensure_equals("the text is the source as it was", r.text, nested);

        // Within the bound, as deep as a person writes, it expands.
        r = ALPreprocessor::run("#define F(x) x\nF(F(F(F(F(1)))));\n", options());
        ensure("five deep is nothing: " + messages(r), !r.overran && r.text.find("1;") != std::string::npos);

        // A file that includes itself twice: each level doubles, and the
        // files it opens are counted against the budget like any tokens.
        add("twice.lsl", "#include \"twice.lsl\"\n#include \"twice.lsl\"\ninteger x;\n");
        ALPreprocessor::Options opts = options();
        opts.tokenBudget             = 200000;
        r                            = ALPreprocessor::run("#include \"twice.lsl\"\n", opts);
        ensure("overran, rather than opening two to the thirty-second files", r.overran);
    }

    template<> template<>
    void alpreprocessor_object::test<22>()
    {
        set_test_name("a name put in a string literal is escaped: a script's, and a module's path");
        ALPreprocessor::Options opts = options();
        opts.fileName                = "Say \"hi\" \\ there";
        ALPreprocessor::Result r     = ALPreprocessor::run("llSay(0, __FILE__);\nllSay(0, __SHORTFILE__);\n", opts);
        ensure("the quote and the backslash escaped: " + r.text, r.text.find("\"Say \\\"hi\\\" \\\\ there\"") != std::string::npos);

        // A module found on a Windows disk: its path is the key it is
        // looked up under, which must read back as itself in Luau.
        ALPreprocessor::Include module;
        module.text = "return { x = 1 }\n";
        module.name = "util.luau";
        module.path = "disk:C:\\Users\\me\\lib\\util.luau";
        files["./util"] = module;
        r = ALPreprocessor::run("local util = require(\"./util\")\n", options(true));
        ensure_equals("nothing wrong", messages(r), std::string());
        const std::string key = "\"disk:C:\\\\Users\\\\me\\\\lib\\\\util.luau\"";
        ensure("the table filled under the escaped key: " + r.text, r.text.find("__modules[" + key + "] = (function()") != std::string::npos);
        ensure("and the call looks it up under the same: " + r.text, r.text.find("local util = __modules[" + key + "]") != std::string::npos);
    }

    template<> template<>
    void alpreprocessor_object::test<23>()
    {
        set_test_name("an interpolated SLua string whose expression holds a string with a brace or a backtick ends where it ends");
        const std::vector<ALPreprocessor::Token> tokens = ALPreprocessor::tokenize("local s = `a{ \"{\" .. `{'}'}` }b` .. X\n", true);
        std::vector<std::string> kinds;
        for (const ALPreprocessor::Token& t : tokens)
        {
            if (t.kind != ALPreprocessor::Token::Kind::Space)
            {
                kinds.push_back(t.text);
            }
        }
        ensure("the string whole, then what follows it: " + std::to_string(kinds.size()),
               kinds.size() >= 6 && kinds[3] == "`a{ \"{\" .. `{'}'}` }b`" && kinds[4] == ".." && kinds[5] == "X");

        // And the macro after it is expanded.
        ALPreprocessor::Result r = ALPreprocessor::run("#define X 42\nlocal s = `{\"{\"}` .. X\n", options(true));
        ensure("the macro after the string expanded: " + r.text, r.text.find(".. 42") != std::string::npos);
    }

    template<> template<>
    void alpreprocessor_object::test<24>()
    {
        set_test_name("the switch and loop transforms are bounded however the script nests, and a long run of else-ifs is no deeper for its length");
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
        ensure("overran", r.overran);
        ensure("and said why: " + messages(r).substr(0, 200), messages(r).find("blocks nest more deeply") != std::string::npos);
        ensure_equals("the text is the source as it was", r.text, blocks);

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
        ensure("switches overran", r.overran);
        ensure("and said why: " + messages(r).substr(0, 200), messages(r).find("blocks nest more deeply") != std::string::npos);

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
        ensure("thirty deep is nothing: " + messages(r).substr(0, 200), !r.overran && !r.hasErrors());
        ensure("and the loops were lowered: " + r.text.substr(0, 200), r.text.find("jump _brk") != std::string::npos);

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
        ensure("the chain is no deeper for its length: " + messages(r).substr(0, 200), !r.overran && !r.hasErrors());
        ensure("and all of it is there", r.text.find("else if (i == 19999) i += 1;") != std::string::npos);
    }

    template<> template<>
    void alpreprocessor_object::test<25>()
    {
        set_test_name("every line of a token over several lines maps back to its own: a block comment, a long string");
        const std::string source = "#define X 1\n/* one\n   two\n   three */ integer x = X;\n";
        ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        ensure_equals("nothing wrong", messages(r), std::string());
        ensure("the comment on the lines it was on: " + r.text, r.text.find("\n/* one\n   two\n   three */ integer x = 1;") != std::string::npos);
        ALSourceMap::Loc loc = r.map.toSource(2, 4);
        ensure("the comment's middle line maps", loc.found());
        ensure_equals("to its own line", loc.line, 2);
        ensure_equals("and column", loc.column, 4);
        loc = r.map.toSource(3, 4);
        ensure_equals("its last line, before what follows it on the line", loc.line, 3);
        ensure_equals("at its own column", loc.column, 4);
        loc = r.map.toSource(3, 12);
        ensure_equals("and what follows it where it is", loc.column, 12);
        loc = r.map.toExpanded(0, 2, 4);
        ensure("and back", loc.found() && loc.line == 2 && loc.column == 4);

        const std::string lua = "#define X 2\nlocal s = [[one\ntwo\nthree]] .. X\n";
        r = ALPreprocessor::run(lua, options(true));
        ensure_equals("nothing wrong in SLua", messages(r), std::string());
        loc = r.map.toSource(2, 1);
        ensure("a long string's middle line maps to its own", loc.found() && loc.line == 2 && loc.column == 1);
        loc = r.map.toSource(3, 11);
        ensure("and the macro after it to where it was invoked", loc.found() && loc.line == 3 && loc.column == 11);
    }
    template<> template<>
    void alpreprocessor_object::test<26>()
    {
        set_test_name("a line is read as the transform it is written for, and a name that is one of the words is no transform");
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
        ensure("switch", of({ "    switch (x)" }, 0, &word) == T::Switch);
        ensure_equals("its word", word, std::string("switch"));
        ensure("switch not called", of({ "switch = 2;" }, 0) == T::None);
        ensure("a case", of({ "  case 1:" }, 0, &word) == T::Switch);
        ensure_equals("the case's word", word, std::string("case"));
        ensure("case assigned", of({ "case = 1;" }, 0) == T::None);
        ensure("case called", of({ "case(1);" }, 0) == T::None);
        ensure("case with no colon", of({ "case 1" }, 0) == T::None);
        ensure("break", of({ "break;" }, 0) == T::Extensions);
        ensure("break out of two", of({ "break 2;" }, 0) == T::Extensions);
        ensure("continue", of({ "\tcontinue;" }, 0) == T::Extensions);
        ensure("break assigned", of({ "break = 3;" }, 0) == T::None);
        ensure("an inline function", of({ "inline f(integer a)" }, 0, &word) == T::Extensions);
        ensure_equals("inline's word", word, std::string("inline"));
        ensure("an inline function with its type", of({ "inline integer f(integer a)" }, 0) == T::Extensions);
        ensure("inline assigned", of({ "inline = 2;" }, 0) == T::None);
        ensure("after a brace and a statement's end", of({ "} ; switch (x)" }, 0) == T::Switch);
        ensure("a plain statement", of({ "integer x = 1;" }, 0) == T::None);
        ensure("a blank line", of({ "   " }, 0) == T::None);

        // A brace of its own is the switch's where the line before opens one.
        ensure("the switch's brace", of({ "switch (x)", "", "  {" }, 2) == T::Switch);
        ensure("a brace after anything else", of({ "if (x)", "{" }, 1) == T::None);
        ensure("a brace after a break is not the switch's", of({ "break;", "{" }, 1) == T::None);
        ensure("a brace with nothing before it", of({ "", "{" }, 1) == T::None);
        ensure("a line past the end", of({ "switch (x)" }, 1) == T::None);
    }

    template<> template<>
    void alpreprocessor_object::test<27>()
    {
        set_test_name("a stretch of the output copied from the source maps back whole, and one a macro made does not");
        const ALPreprocessor::Result r = ALPreprocessor::run("#define GREET llOwnerSay(\"hi\")\nGREET;\n    llSay(0, \"x\");\n", options());
        ensure("no problems: " + messages(r), r.problems.empty());
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
        ensure("both in the output: " + r.text, greet_line >= 0 && say_line >= 0);
        ALSourceMap::Loc begin, end;
        ensure("llSay is the source's own", r.map.verbatimSpan(say_line, say_column, say_column + 5, begin, end));
        ensure_equals("from its line", begin.line, 2);
        ensure_equals("its column", begin.column, 4);
        ensure_equals("to its end", end.column, 9);
        ensure_equals("in the script", begin.file, 0);
        ensure("an insertion after it too", r.map.verbatimSpan(say_line, say_column + 5, say_column + 5, begin, end) && begin.column == 9);
        ensure("what the macro made is not", !r.map.verbatimSpan(greet_line, greet_column, greet_column + 10, begin, end));
    }
} // namespace tut
