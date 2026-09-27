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
        // A value worked out again for each case, said where that is more
        // than a cost: a call, or one that changes something.
        const std::string warned = "W 0: the switch's value is worked out again for each case: put it in a local first\n";
        ensure_equals("a call", messages(ALPreprocessor::run("switch ((integer)llFrand(3)) { case 1: break; }\n", o)), warned);
        ensure_equals("an increment", messages(ALPreprocessor::run("switch (i++) { case 1: break; }\n", o)), warned);
        ensure_equals("an assignment", messages(ALPreprocessor::run("switch (i = 2) { case 1: break; }\n", o)), warned);
        ensure_equals("not a name", messages(ALPreprocessor::run("switch (i) { case 1: break; }\n", o)), std::string());
        ensure_equals("nor arithmetic or a field", messages(ALPreprocessor::run("switch ((i + 1) * v.x) { case 1: break; }\n", o)), std::string());
        ensure_equals("nor a comparison", messages(ALPreprocessor::run("switch (i == 2) { case 1: break; }\n", o)), std::string());
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
        const std::string bundled = "local __modules = {}\n"
                                    "__modules[\"b\"] = (function()\nreturn 42\nend)()\n"
                                    "__modules[\"a\"] = (function()\nlocal b = __modules[\"b\"]\nreturn { b = b }\nend)()\n"
                                    "local a = __modules[\"a\"]\nlocal b = __modules[\"b\"]\nlocal x = require(name)\n";
        ensure_equals("modules", r.text, bundled);
        ensure_equals("included", r.includes.size(), 2u);
        ensure_equals("b mapped to its file", r.map.files()[r.map.toSource(2, 0).file].name, std::string("b"));
        // The modules' own lines, which the studio shows nothing of: not
        // the table's, which are no one's, nor the script's.
        std::string others;
        for (const auto& [first, last] : r.map.othersLines())
        {
            others += std::to_string(first) + "-" + std::to_string(last) + " ";
        }
        ensure_equals("the modules' lines", others, std::string("2-2 5-6 "));
        ensure("not apart unless asked", !r.apart.valid);
        // Apart, for the analyzers: the script with its requires as calls,
        // and each module as the run made it, each mapped to its file; the
        // text the bundle still.
        ALPreprocessor::Options apart = options(true);
        apart.apart                   = true;
        const ALPreprocessor::Result split =
            ALPreprocessor::run("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n", apart);
        ensure_equals("the bundle as ever", split.text, bundled);
        ensure("apart", split.apart.valid && split.apart.modules.size() == 2);
        ensure_equals("the script, its requires calls", split.apart.script.text,
                      std::string("local a = require(\"a\")\nlocal b = require('b')\nlocal x = require(name)\n"));
        ensure_equals("the first reached", split.apart.modules[0].key, std::string("a"));
        ensure_equals("as made, its own require a call", split.apart.modules[0].text, std::string("local b = require(\"b\")\nreturn { b = b }\n"));
        ensure_equals("then what it reached", split.apart.modules[1].key, std::string("b"));
        ensure_equals("b", split.apart.modules[1].text, std::string("return 42\n"));
        const ALSourceMap::Loc in_a = split.apart.modules[0].map.toSource(1, 0);
        ensure("mapped to its file", in_a.found() && split.apart.modules[0].map.files()[in_a.file].name == "a" && in_a.line == 1);
        ensure("the script to its own", split.apart.script.map.toSource(2, 6).file == 0);
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
            const auto        read     = [](const fs::path& path) {
                llifstream        in(path, std::ios::binary);
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
                llofstream out(expected, std::ios::binary);
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
        ensure("inline after the parameters", of({ "integer f(integer a) inline {" }, 0, &word) == T::Extensions);
        ensure_equals("its word", word, std::string("inline"));
        ensure("inline after them, the brace on the next line", of({ "f() inline" }, 0) == T::Extensions);
        ensure("a name that ends in inline is not", of({ "integer f(integer a) noinline {" }, 0) == T::None);
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
        ensure("a stretch of several tokens, the blanks between them the source's", r.map.verbatimSpan(say_line, say_column, say_column + 13, begin, end));
        ensure_equals("to the call's end", end.column, 17);
        ensure("what the macro made is not", !r.map.verbatimSpan(greet_line, greet_column, greet_column + 10, begin, end));
    }

    template<> template<>
    void alpreprocessor_object::test<28>()
    {
        set_test_name("the optimizer's run, weighed, says what it saved in code: the whole, and each note what its lines came to less");
        {
            ALLSLService service;
            std::string  error;
            ensure("builtins: " + error, service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));
        }
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
        ensure("optimized", r.optimized);
        ensure("weighed, and lighter: " + std::to_string(r.codeBefore) + " to " + std::to_string(r.codeAfter), r.codeAfter > 0 && r.codeAfter < r.codeBefore);
        const ALScriptProblem* removed = nullptr;
        const ALScriptProblem* folded  = nullptr;
        const ALScriptProblem* sizes   = nullptr;
        for (const ALScriptProblem& p : r.problems)
        {
            removed = p.key == "OptimizerRemovedFunction" ? &p : removed;
            folded  = p.line == 8 && p.source == ALScriptProblem::Source::Optimizer && p.key != "OptimizerRemovedFunction" ? &p : folded;
            sizes   = p.key == "OptimizerSizesWeighed" ? &p : sizes;
        }
        ensure("the function's removal noted", removed && removed->savedBytes.has_value());
        ensure("its lines' code all saved: " + std::to_string(removed->savedBytes.value_or(0)), *removed->savedBytes > 0);
        ensure("the fold noted, with what its line saved", folded && folded->savedBytes && *folded->savedBytes > 0);
        ensure("the whole said", sizes != nullptr);
        ensure_equals("in code", sizes->args[2], std::to_string(r.codeBefore));
        ensure_equals("on its target", sizes->args[4], std::string("LSO"));
        const S64 lost = S64(r.codeBefore) - S64(r.codeAfter);
        ensure("no note says more than the whole lost", *removed->savedBytes <= lost && *folded->savedBytes <= lost);

        // Not weighed unless asked: no bytes, and the words as they were.
        o.weigh                           = false;
        const ALPreprocessor::Result bare = ALPreprocessor::run(source, o);
        bool                         said = false;
        for (const ALScriptProblem& p : bare.problems)
        {
            ensure("nothing weighed", !p.savedBytes);
            said = said || p.key == "OptimizerSizes";
        }
        ensure("the size in source only", said && bare.codeBefore == 0);
    }

    template<> template<>
    void alpreprocessor_object::test<29>()
    {
        set_test_name("a script that does not compile is not the preprocessor's error: the optimizer stands aside with a note where the first error is");
        {
            ALLSLService service;
            std::string  error;
            ensure("builtins: " + error, service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));
        }
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
        ensure("no error of the preprocessor's: " + messages(r), !r.hasErrors());
        ensure_equals("one note, on the library's line", messages(r),
                      std::string("N linkset.lsl:110: not optimized: the script does not compile as it stands, and compiling it says why\n"));
        ensure("keyed", r.problems.size() == 1 && r.problems[0].key == "OptimizerUncompiled" && r.problems[0].column == 33);
        ensure("not optimized", !r.optimized);
        ALPreprocessor::Options plain = options();
        ensure_equals("the text as expanded, for the compiler to read", r.text, ALPreprocessor::run(source, plain).text);
    }

    template<> template<>
    void alpreprocessor_object::test<30>()
    {
        set_test_name("SLua: a field or a method named require is the table's, and only the global finds a module");
        add("x", "return 1\n");
        const ALPreprocessor::Result r = ALPreprocessor::run("local a = t.require(\"x\")\n"
                                                             "local b = t : require(\"x\")\n"
                                                             "local c = t .. require(\"x\")\n",
                                                             options(true));
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("only the concatenated call is a module's", r.text,
                      std::string("local __modules = {}\n"
                                  "__modules[\"x\"] = (function()\nreturn 1\nend)()\n"
                                  "local a = t.require(\"x\")\n"
                                  "local b = t : require(\"x\")\n"
                                  "local c = t .. __modules[\"x\"]\n"));
    }

    template<> template<>
    void alpreprocessor_object::test<31>()
    {
        set_test_name("SLua: the calls a run puts a module in place of are found where they stand, and only those");
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
        ensure_equals("three", found.size(), size_t(3));
        ensure_equals("a", found[0].name, std::string("a"));
        ensure("a's stretch", found[0].line == 0 && found[0].column == 10 && found[0].endLine == 0 && found[0].endColumn == 22);
        ensure_equals("c", found[1].name, std::string("c"));
        ensure("c's stretch, across lines", found[1].line == 2 && found[1].column == 10 && found[1].endLine == 4 && found[1].endColumn == 1);
        ensure_equals("i", found[2].name, std::string("i"));
        ensure("i's line", found[2].line == 10 && found[2].column == 15);

        // Every one of them is one a run puts a module in place of, and
        // no other call is.
        add("a", "return 1\n");
        add("c", "return 3\n");
        add("i", "return 9\n");
        const ALPreprocessor::Result r = ALPreprocessor::run(source, options(true));
        ensure_equals("problems", messages(r), std::string());
        const auto count = [&r](const std::string& what) {
            size_t n = 0;
            for (size_t at = r.text.find(what); at != std::string::npos; at = r.text.find(what, at + 1))
            {
                ++n;
            }
            return n;
        };
        // Each module is filled once at the top, and looked up where it
        // was called for.
        ensure_equals("as many lookups as calls found", count("__modules[") - count("] = (function()"), found.size());
    }

    template<> template<>
    void alpreprocessor_object::test<32>()
    {
        set_test_name("a run says what each include and module was found as, by the name the file asking wrote");
        add("lib/util.lsl", "#include \"inner.lsl\"\nint util() { return 1; }\n");
        files["lib/util.lsl"].path = "disk:/scripts/lib/util.lsl";
        add("inner.lsl", "int inner() { return 2; }\n");
        add("missing.lsl", "");
        files.erase("missing.lsl");
        const ALPreprocessor::Result r = ALPreprocessor::run("#include \"lib/util.lsl\"\n#include \"lib/util.lsl\"\ndefault {}\n", options());
        // Included twice, with no #pragma once: its own include asked for
        // twice as well.
        ensure_equals("four found", r.resolved.size(), size_t(4));
        ensure("the script's, by its name, as the identity found",
               r.resolved[0].from.empty() && r.resolved[0].name == "lib/util.lsl" && r.resolved[0].path == "disk:/scripts/lib/util.lsl" &&
                   !r.resolved[0].require);
        ensure("the include's own, from it", r.resolved[1].from == "disk:/scripts/lib/util.lsl" && r.resolved[1].name == "inner.lsl" &&
                                                 r.resolved[1].path == "inner.lsl");
        ensure("the same asked twice, twice", r.resolved[2].name == "lib/util.lsl" && r.resolved[2].from.empty() &&
                                                  r.resolved[3].name == "inner.lsl");

        add("mod", "return 1\n");
        files["mod"].path = "disk:/scripts/mod.luau";
        const ALPreprocessor::Result lua = ALPreprocessor::run("local m = require(\"mod\")\nlocal n = require(\"none\")\n", options(true));
        ensure_equals("a module found, one not", lua.resolved.size(), size_t(1));
        ensure("as a require", lua.resolved[0].require && lua.resolved[0].name == "mod" && lua.resolved[0].path == "disk:/scripts/mod.luau");
    }

    template<> template<>
    void alpreprocessor_object::test<33>()
    {
        set_test_name("a parameter used over and over is spent as it goes in, and a token pasted or stringized to itself stops before it outgrows a script");
        // A parameter used sixteen times, through arguments that each
        // expand the last: sixteen to the seventh tokens by the end, which
        // were made whole before they were counted.
        const std::string many = "#define D(x) x x x x x x x x x x x x x x x x\nD(D(D(D(D(D(D(1)))))))\n";
        ALPreprocessor::Result r = ALPreprocessor::run(many, options());
        ensure("ran away: " + messages(r), r.overran && r.problems.back().key == std::string("PreprocTooMuch"));
        ensure("and gave the source back", r.text == many);

        // A token pasted to itself through an indirection doubles its text
        // and not the tokens: two to the fortieth bytes by the end.
        std::string call = "x";
        for (int i = 0; i < 40; ++i)
        {
            call = "D(" + call + ")";
        }
        r = ALPreprocessor::run("#define CAT(a) a##a\n#define D(a) CAT(a)\n" + call + ";\n", options());
        ensure("pasting stopped: " + messages(r), r.overran && r.problems.back().key == std::string("PreprocTokenTooLong"));

        // A string stringized over and over, each level escaping the last.
        call = "\"a\"";
        for (int i = 0; i < 40; ++i)
        {
            call = "X(" + call + ")";
        }
        r = ALPreprocessor::run("#define S(x) #x\n#define X(x) S(x)\n" + call + ";\n", options());
        ensure("stringizing stopped: " + messages(r), r.overran && r.problems.back().key == std::string("PreprocTokenTooLong"));

        // The bytes stop what the tokens would not, and what fits is
        // untouched.
        ALPreprocessor::Options small = options();
        small.byteBudget              = 1000;
        r = ALPreprocessor::run("#define L \"" + std::string(600, 'a') + "\"\nL L\n", small);
        ensure("the bytes: " + messages(r), r.overran && r.problems.back().key == std::string("PreprocTooMuch"));
        r = ALPreprocessor::run("#define L \"" + std::string(100, 'a') + "\"\nL L\n", small);
        ensure("within them: " + messages(r), !r.overran && r.text.find(std::string(100, 'a')) != std::string::npos);

        // What a job answers when a run throws: as a run that ran away.
        const ALPreprocessor::Result failed = ALPreprocessor::failed("integer x;\n", options(), "std::bad_alloc");
        ensure("overran, with the source", failed.overran && failed.text == "integer x;\n");
        ensure("and why", failed.problems.size() == 1 && failed.problems[0].key == std::string("PreprocFailed") &&
                              failed.problems[0].message.find("std::bad_alloc") != std::string::npos &&
                              failed.problems[0].severity == ALScriptProblem::Severity::Error);
        ensure("mapped to itself", failed.map.toSource(0, 3).found() && failed.map.toSource(0, 3).line == 0);
    }

    template<> template<>
    void alpreprocessor_object::test<34>()
    {
        set_test_name("lazy lists' reads nested by macros stop at the depth blocks may nest, and within it each is made a call");
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
        ensure("stopped at the depth: " + messages(r), r.overran && r.problems.back().key == std::string("PreprocNestsTooDeep"));

        // Within it, each read is a call, the inner in the outer's place.
        r = ALPreprocessor::run("#define N0 0\n#define N1 (integer)l[N0]\n#define N2 (integer)l[N1]\n#define N3 (integer)l[N2]\nlist l;\ninteger x = N3;\n", o);
        ensure("nested reads each a call: " + r.text,
               !r.overran && r.text.find("integer x = llList2Integer(l, llList2Integer(l, llList2Integer(l, 0)));") != std::string::npos);
    }

    template<> template<>
    void alpreprocessor_object::test<35>()
    {
        set_test_name("SLua: a header of plain comments and hot ones stays ahead of the module table, which maps to no line of the script");
        add("m", "return 1\n");
        const ALPreprocessor::Result r =
            ALPreprocessor::run("-- @file vehicle/hovertext.luau\n--!strict\n--!nolint LocalUnused\nlocal m = require(\"m\")\n", options(true));
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("the header first, whole", r.text.substr(0, r.text.find("local __modules")),
                      std::string("-- @file vehicle/hovertext.luau\n--!strict\n--!nolint LocalUnused\n"));
        ensure("each header line its own", r.map.toSource(1, 0).found() && r.map.toSource(1, 0).file == 0 && r.map.toSource(1, 0).line == 1);
        ensure("the table's line maps nowhere", !r.map.toSource(3, 0).found());
        ensure("nor its module's wrapping", !r.map.toSource(4, 0).found());
        ensure("the module's own line maps to it", r.map.toSource(5, 0).found() && r.map.files()[r.map.toSource(5, 0).file].name == "m");
        ensure("the script's code after it maps back", r.map.toSource(7, 0).file == 0 && r.map.toSource(7, 0).line == 3);
    }

    template<> template<>
    void alpreprocessor_object::test<36>()
    {
        set_test_name("a run no longer wanted stops where it stands and says so, the source as it was; one still wanted is not touched");
        std::atomic<bool>       later{ false };
        ALPreprocessor::Options wanted = options(false);
        wanted.superseded              = &later;
        wanted.switches                = true;
        const std::string       source = "#define TWICE(x) ((x) * 2)\ndefault { state_entry() { integer n = TWICE(3); switch (n) { case 6: break; } } }\n";
        const ALPreprocessor::Result made = ALPreprocessor::run(source, wanted);
        ensure("still wanted: made", !made.superseded && !made.overran && made.text.find("((3) * 2)") != std::string::npos);
        later = true;
        const ALPreprocessor::Result stopped = ALPreprocessor::run(source, wanted);
        ensure("not wanted: stopped", stopped.superseded && stopped.overran);
        ensure_equals("the source as it was", stopped.text, source);
        ensure("nothing said of it", std::none_of(stopped.problems.begin(), stopped.problems.end(),
                                                  [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; }));
    }

    template<> template<>
    void alpreprocessor_object::test<37>()
    {
        set_test_name("a header behind a classic include guard: expanded once however often included, listed once in the map; again once undefined; an #else or code after it no guard");
        add("guarded.lsl", "// the helpers\n#ifndef GUARDED\n#define GUARDED\ninteger helper() { return 1; }\n#endif\n");
        const ALPreprocessor::Result r = ALPreprocessor::run(
            "#include \"guarded.lsl\"\n#include \"guarded.lsl\"\n#include \"guarded.lsl\"\ndefault { state_entry() { helper(); } }\n", options());
        ensure_equals("problems", messages(r), std::string());
        size_t count = 0;
        for (size_t at = r.text.find("integer helper()"); at != std::string::npos; at = r.text.find("integer helper()", at + 1))
        {
            ++count;
        }
        ensure_equals("once", count, size_t(1));
        ensure_equals("listed once", r.map.files().size(), size_t(2));

        const ALPreprocessor::Result undone = ALPreprocessor::run(
            "#include \"guarded.lsl\"\n#undef GUARDED\n#include \"guarded.lsl\"\ndefault { state_entry() { } }\n", options());
        ensure("undefined: again", undone.text.find("integer helper()") != undone.text.rfind("integer helper()"));

        add("elsed.lsl", "#ifndef ELSED\n#define ELSED\ninteger a;\n#else\ninteger b;\n#endif\n");
        const ALPreprocessor::Result elsed = ALPreprocessor::run("#include \"elsed.lsl\"\n#include \"elsed.lsl\"\n", options());
        ensure("an #else: no guard, the other branch made", elsed.text.find("integer b;") != std::string::npos);
        add("after.lsl", "#ifndef AFTER\n#define AFTER\n#endif\ninteger after;\n");
        const ALPreprocessor::Result after = ALPreprocessor::run("#include \"after.lsl\"\n#include \"after.lsl\"\n", options());
        ensure("code after the #endif: no guard, made twice", after.text.find("integer after;") != after.text.rfind("integer after;"));
    }
    template<> template<>
    void alpreprocessor_object::test<38>()
    {
        set_test_name("a macro's arguments: none for one with no parameters, a line an argument runs over a space, blanks with no ( after them given back as they were");
        const std::string source = "#define Z() zero\n"
                                   "#define I(x) [x]\n"
                                   "#define TWO(a, b) a+b\n"
                                   "Z() Z( ) Z\n"
                                   "I (1) I  x I\n"
                                   "TWO(1,\n2) TWO( (1, 2) ,\t3 )\n"
                                   "I\n(3)\n";
        const ALPreprocessor::Result r = ALPreprocessor::run(source, options());
        ensure_equals("problems", messages(r), std::string());
        ensure_equals("text", squeeze(r.text), std::string("zero zero Z\n[1] I  x I\n1+2 (1, 2)+3\n[3]\n"));
    }
    template<> template<>
    void alpreprocessor_object::test<39>()
    {
        set_test_name("a function marked inline in any of the ways a mark is written, at the top of the script, and the marks taken off with the extensions off");
        ALPreprocessor::Options o = options();
        o.extensions              = true;
        ALPreprocessor::Result r  = ALPreprocessor::run("integer f(integer a) inline { return a; }\n"
                                                        "g(string s) /*pragma inline*/ { llOwnerSay(s); }\n"
                                                        "h() //pragma inline\n{\n}\n"
                                                        "k() /* pragma inline */ { }\n",
                                                        o);
        ensure_equals("problems", messages(r), std::string());
        ensure("the four marked, in order", r.inlined == std::vector<std::string>{ "f", "g", "h", "k" });
        ensure("the word after the parameters taken off: " + r.text, r.text.find("integer f(integer a) { return a; }") != std::string::npos);
        ensure("a comment left as it is: " + r.text, r.text.find("g(string s) /*pragma inline*/ { llOwnerSay(s); }") != std::string::npos &&
                                                         r.text.find("h() //pragma inline\n{") != std::string::npos);

        r = ALPreprocessor::run("integer inline = 1;\nf() { integer x = inline; }\ng() /* not pragma inline */ { }\ninteger v = (1) /*pragma inline*/;\n", o);
        ensure("a variable named inline, another comment, a parenthesis that closes no parameters: none marked", r.inlined.empty());
        ensure_equals("and nothing taken off", r.text, std::string("integer inline = 1;\nf() { integer x = inline; }\ng() /* not pragma inline */ { }\ninteger v = (1) /*pragma inline*/;\n"));

        r = ALPreprocessor::run("default { state_entry() inline { } }\n", o);
        ensure("an event is not a function to put in place", r.inlined.empty());

        // With the extensions off the marks come off all the same.
        const ALPreprocessor::Result plain = ALPreprocessor::run("inline f() { }\ninteger g(integer a) inline { return a; }\n", options());
        ensure_equals("taken off", plain.text, std::string("f() { }\ninteger g(integer a) { return a; }\n"));
        ensure("and the names kept", plain.inlined == std::vector<std::string>{ "f", "g" });
        ensure("the extensions not said to be used", !plain.usedExtensions);

        // And the optimizer puts the marked function in place.
        ALPreprocessor::Options optimizing = options();
        optimizing.optimize                = true;
        const ALPreprocessor::Result put   = ALPreprocessor::run("say(string s) inline\n{\n    llOwnerSay(s);\n    llOwnerSay(s + \"!\");\n}\n"
                                                                 "default\n{\n    state_entry()\n    {\n        say(\"a\");\n        say(\"b\");\n    }\n}\n",
                                                                 optimizing);
        ensure("both calls put in place: " + put.text, put.text.find("say(") == std::string::npos && put.text.find("llOwnerSay(\"a\")") != std::string::npos &&
                                                           put.text.find("llOwnerSay(\"b\" + \"!\")") != std::string::npos);
    }
}
