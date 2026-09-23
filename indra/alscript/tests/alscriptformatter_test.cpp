/**
 * @file alscriptformatter_test.cpp
 * @brief The formatter: indentation from the structure, spacing from the rules, and everything else as written.
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

#include "../alscriptformatter.h"

#include "../test/lltut.h"

namespace tut
{
    struct alscriptformatter_data
    {
        static std::string lsl(std::string_view text)
        {
            ALScriptFormatter::Options options;
            return ALScriptFormatter::format(text, options);
        }
        static std::string lua(std::string_view text)
        {
            ALScriptFormatter::Options options;
            options.lua = true;
            return ALScriptFormatter::format(text, options);
        }
    };
    typedef test_group<alscriptformatter_data> alscriptformatter_group;
    typedef alscriptformatter_group::object    alscriptformatter_object;
    tut::alscriptformatter_group               alscriptformatter_instance("alscriptformatter");

    // LSL: braces, a hanging statement, a list across lines, a directive.
    template<> template<>
    void alscriptformatter_object::test<1>()
    {
        const std::string in =
            "#define X 1\n"
            "default\n"
            "{\n"
            "state_entry()\n"
            "{\n"
            "if(x)\n"
            "llSay(0,\"hi\");\n"
            "else\n"
            "{\n"
            "list l = [\n"
            "1,\n"
            "2\n"
            "];\n"
            "}\n"
            "}\n"
            "}\n";
        const std::string want =
            "#define X 1\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        if (x)\n"
            "            llSay(0, \"hi\");\n"
            "        else\n"
            "        {\n"
            "            list l = [\n"
            "                1,\n"
            "                2\n"
            "            ];\n"
            "        }\n"
            "    }\n"
            "}\n";
        ensure_equals("lsl structure", lsl(in), want);
    }

    // LSL spacing: operators, unary signs, vectors, casts, comments.
    template<> template<>
    void alscriptformatter_object::test<2>()
    {
        ensure_equals("binary", lsl("x=a+b*c;\n"), std::string("x = a + b * c;\n"));
        ensure_equals("unary", lsl("x = -a * -b;\n"), std::string("x = -a * -b;\n"));
        ensure_equals("not", lsl("if(!x)y=~z;\n"), std::string("if (!x) y = ~z;\n"));
        ensure_equals("vector", lsl("v=<1,-2,3>*<0,0,1>;\n"), std::string("v = <1, -2, 3> * <0, 0, 1>;\n"));
        ensure_equals("comparison", lsl("if(a<b&&c>d)x++;\n"), std::string("if (a < b && c > d) x++;\n"));
        ensure_equals("cast kept", lsl("x=(integer)y;\n"), std::string("x = (integer)y;\n"));
        ensure_equals("cast spaced kept", lsl("x=(integer) y;\n"), std::string("x = (integer) y;\n"));
        ensure_equals("for", lsl("for(i=0;i<n;i++){}\n"), std::string("for (i = 0; i < n; i++) {}\n"));
        ensure_equals("trailing comment kept", lsl("x = 1;   // one\n"), std::string("x = 1;   // one\n"));
        ensure_equals("string kept", lsl("s=\"a  +  b\";\n"), std::string("s = \"a  +  b\";\n"));
        ensure_equals("label", lsl("@loop;jump loop;\n"), std::string("@loop; jump loop;\n"));
        ensure_equals("braces on a line", lsl("if (x){llSay(0,\"a\");}\n"), std::string("if (x) { llSay(0, \"a\"); }\n"));
    }

    // Luau: blocks by keyword, a function in a call, a table, and the
    // spacing that is its own.
    template<> template<>
    void alscriptformatter_object::test<3>()
    {
        const std::string in =
            "local function f(a,b)\n"
            "if a then\n"
            "return a..b\n"
            "elseif b then\n"
            "return #b\n"
            "else\n"
            "for i=1,10 do\n"
            "print(i)\n"
            "end\n"
            "end\n"
            "end\n"
            "ll.Listen(0,\"\",function(x)\n"
            "print(x)\n"
            "end)\n"
            "local t={\n"
            "a=1,\n"
            "b={1,2},\n"
            "}\n";
        const std::string want =
            "local function f(a, b)\n"
            "    if a then\n"
            "        return a .. b\n"
            "    elseif b then\n"
            "        return #b\n"
            "    else\n"
            "        for i = 1, 10 do\n"
            "            print(i)\n"
            "        end\n"
            "    end\n"
            "end\n"
            "ll.Listen(0, \"\", function(x)\n"
            "    print(x)\n"
            "end)\n"
            "local t = {\n"
            "    a = 1,\n"
            "    b = { 1, 2 },\n"
            "}\n";
        ensure_equals("luau structure", lua(in), want);
        ensure_equals("generics kept", lua("local x: Array<number> = {}\n"), std::string("local x: Array<number> = {}\n"));
        ensure_equals("method kept", lua("obj:method(1)\n"), std::string("obj:method(1)\n"));
        ensure_equals("not", lua("if not(x)then end\n"), std::string("if not (x) then end\n"));
        ensure_equals("long string kept", lua("s = [[\n  a\n]]\nx=1\n"), std::string("s = [[\n  a\n]]\nx = 1\n"));
    }

    // Blank lines, trailing whitespace, and the end of the file.
    template<> template<>
    void alscriptformatter_object::test<4>()
    {
        ensure_equals("blanks shortened", lsl("\n\nx = 1;   \n\n\n\n\ny = 2;\n\n\n"), std::string("x = 1;\n\n\ny = 2;\n"));
        ensure_equals("a newline added", lsl("x = 1;"), std::string("x = 1;\n"));
        ensure_equals("tabs", [] {
            ALScriptFormatter::Options options;
            options.tabs = true;
            return ALScriptFormatter::format("{\nx;\n}\n", options);
        }(), std::string("{\n\tx;\n}\n"));
    }

    // Only some lines, the rest as written and every line kept.
    template<> template<>
    void alscriptformatter_object::test<5>()
    {
        const std::string in = "default\n{\nstate_entry()\n{\n\n\nx=1;\n}\n}\n";
        ALScriptFormatter::Options options;
        ensure_equals("one line", ALScriptFormatter::formatLines(in, options, 6, 6), std::string("default\n{\nstate_entry()\n{\n\n\n        x = 1;\n}\n}\n"));
        ensure_equals("blanks kept", ALScriptFormatter::formatLines(in, options, 4, 7), std::string("default\n{\nstate_entry()\n{\n\n\n        x = 1;\n    }\n}\n"));
        // A string across lines is one line to the formatter, left alone
        // unless the whole of it is asked for.
        const std::string across = "x = \"a\nb\";\ny=2;\n";
        ensure_equals("across, partly", ALScriptFormatter::formatLines(across, options, 1, 2), std::string("x = \"a\nb\";\ny = 2;\n"));
        ensure_equals("across, whole", ALScriptFormatter::formatLines(across, options, 0, 1), std::string("x = \"a\nb\";\ny=2;\n"));
    }

    // Luau's own: an if-expression opens no block, `const` and `export`
    // read as the keywords they are, and a function's body starts afresh.
    template<> template<>
    void alscriptformatter_object::test<6>()
    {
        ensure_equals("an if-expression", lua("local x = if a then 1 else 2\ny = 3\n"), std::string("local x = if a then 1 else 2\ny = 3\n"));
        ensure_equals("chained", lua("local x = if a then 1 elseif b then 2 else 3\ny = 3\n"), std::string("local x = if a then 1 elseif b then 2 else 3\ny = 3\n"));
        ensure_equals("else if, as one expression", lua("return if a then 1 else if b then 2 else 3\n"), std::string("return if a then 1 else if b then 2 else 3\n"));
        ensure_equals("across lines", lua("local x =\nif a then 1\nelse 2\ny = 3\n"), std::string("local x =\nif a then 1\nelse 2\ny = 3\n"));
        ensure_equals("a statement if still opens its block", lua("if a then\nx = 1\nelse\nx = 2\nend\ny = 3\n"),
                      std::string("if a then\n    x = 1\nelse\n    x = 2\nend\ny = 3\n"));
        ensure_equals("a statement if inside a function inside an expression",
                      lua("local f = if a then function()\nif b then\nreturn 1\nend\nend else nil\ny = 3\n"),
                      std::string("local f = if a then function()\n    if b then\n        return 1\n    end\nend else nil\ny = 3\n"));
        ensure_equals("const and export", lua("export const n=1\nconst function f(a,b)\nreturn a+b\nend\n"),
                      std::string("export const n = 1\nconst function f(a, b)\n    return a + b\nend\n"));
    }
    template<> template<>
    void alscriptformatter_object::test<7>()
    {
        set_test_name("a directive continued over several lines is the directive's: its braces open and close nothing of the script's");
        const std::string text =
            "#define BLOCK(x) { \\\n"
            "      llSay(0, x); \\\n"
            "}\n"
            "default\n"
            "{\n"
            "state_entry()\n"
            "{\n"
            "BLOCK(\"hi\")\n"
            "}\n"
            "}\n";
        const std::string out = lsl(text);
        ensure_equals("the macro as written, the script as it should be",
                      out,
                      std::string("#define BLOCK(x) { \\\n"
                                  "      llSay(0, x); \\\n"
                                  "}\n"
                                  "default\n"
                                  "{\n"
                                  "    state_entry()\n"
                                  "    {\n"
                                  "        BLOCK(\"hi\")\n"
                                  "    }\n"
                                  "}\n"));
    }
}
