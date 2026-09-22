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
    }
} // namespace tut
