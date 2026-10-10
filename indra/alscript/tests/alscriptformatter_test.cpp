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

#include "../core/alscriptformatter.h"

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
        // SLua's are written --#, and are as written too: a function-like
        // macro's ( stays against its name, and a # is no operator's.
        ensure_equals("SLua's directives as written, the script as it should be",
                      lua("--#define SQUARE(x) ((x)*(x))\n--#define LEN(t) #t\nlocal function f(a)\nreturn SQUARE(a)+LEN(a)\nend\n"),
                      std::string("--#define SQUARE(x) ((x)*(x))\n--#define LEN(t) #t\nlocal function f(a)\n    return SQUARE(a) + LEN(a)\nend\n"));
    }
    template<> template<>
    void alscriptformatter_object::test<8>()
    {
        set_test_name("the line breaks that stand inside a string are known, so that tidying lines leaves them and the blanks before them alone");
        const auto marked = [](const std::vector<bool>& breaks) {
            std::string out;
            for (const bool in : breaks)
            {
                out += in ? '1' : '0';
            }
            return out;
        };
        // A Luau long string over five lines, with blank lines in it; a long
        // comment is not a string.
        ensure_equals("a long string", marked(ALScriptFormatter::breaksInStrings("local s = [[\n  a  \n\n\n\nb]]\nprint(s)\n", true)), std::string("11111000"));
        ensure_equals("a long comment", marked(ALScriptFormatter::breaksInStrings("--[[\n\n]]\nprint(1)\n", true)), std::string("00000"));
        ensure_equals("a string continued by a backslash", marked(ALScriptFormatter::breaksInStrings("print(\"a \\\nb\")\n", true)), std::string("100"));
        // LSL: a break written into a string, an escaped quote that does not
        // end it, and quotes in comments that begin nothing.
        ensure_equals("an LSL string", marked(ALScriptFormatter::breaksInStrings("string s = \"a  \n\n\nb\";\n// \"not\n/* \" */ integer i;\n", false)),
                      std::string("1110000"));
        ensure_equals("an escaped quote", marked(ALScriptFormatter::breaksInStrings("string s = \"a\\\"\nb\";\n", false)), std::string("100"));
    }
    template<> template<>
    void alscriptformatter_object::test<9>()
    {
        set_test_name("a line holding nothing but a comment keeps it, indented as a statement there would be");
        ensure_equals("LSL",
                      lsl("// first\ndefault\n{\n// a note\nstate_entry()\n{\n/* over\n   lines */\nif (x)\n// why\nllSay(0, \"a\");\n}\n}\n"),
                      std::string("// first\ndefault\n{\n    // a note\n    state_entry()\n    {\n        /* over\n   lines */\n        if (x)\n"
                                  "            // why\n            llSay(0, \"a\");\n    }\n}\n"));
        ensure_equals("Luau",
                      lua("-- first\nlocal function f()\n-- a note\nreturn 1\nend\n--[[ over\n   lines ]]\n"),
                      std::string("-- first\nlocal function f()\n    -- a note\n    return 1\nend\n--[[ over\n   lines ]]\n"));
        // Some lines asked for, as the studio asks for all of a script's:
        // every line kept, the comments too.
        ALScriptFormatter::Options options;
        ensure_equals("LSL, by lines", ALScriptFormatter::formatLines("{\n// note\n/* a\nb */\n}\n", options, 0, 5),
                      std::string("{\n    // note\n    /* a\nb */\n}\n"));
        options.lua = true;
        ensure_equals("Luau, by lines", ALScriptFormatter::formatLines("do\n-- note\nend\n", options, 0, 3), std::string("do\n    -- note\nend\n"));
    }

    template<> template<>
    void alscriptformatter_object::test<10>()
    {
        set_test_name("a line past the width broken at the commas of its widest bracket, or of the table a call ends with on the call's "
                      "line, each part a level further in and broken again where still too long, its comment after the last; formatted "
                      "again, the same; not where no width is set, nor by formatLines; formatEach gives it as one line's");
        ALScriptFormatter::Options options;
        options.lua   = true;
        options.width = 100;
        const std::string in = "local function f()\n"
                               "    local r: string = llcompat.List2Json(JSON_OBJECT, {\"fn\", \"SELECT\", \"radius\", 10, \"ima\", "
                               "llcompat.List2Json(JSON_ARRAY, {\"Ungulate\"}), \"requestor\", llcompat.List2Json(JSON_OBJECT, {\"id\", "
                               "ll.GetKey(), \"ima\", \"[]\", \"playsWith\", \"[]\"}), \"responders\", \"[]\"}) -- sent\n"
                               "    print(string.format(\"%s and %s and %s\", tostring(aVeryLongName), tostring(anotherVeryLongName), "
                               "tostring(yetAnotherName)))\n"
                               "    print(\"a string with no comma in it that is longer than the width of one hundred columns, and so on\")\n"
                               "end\n";
        const std::string want = "local function f()\n"
                                 "    local r: string = llcompat.List2Json(JSON_OBJECT, {\n"
                                 "        \"fn\",\n"
                                 "        \"SELECT\",\n"
                                 "        \"radius\",\n"
                                 "        10,\n"
                                 "        \"ima\",\n"
                                 "        llcompat.List2Json(JSON_ARRAY, { \"Ungulate\" }),\n"
                                 "        \"requestor\",\n"
                                 "        llcompat.List2Json(JSON_OBJECT, { \"id\", ll.GetKey(), \"ima\", \"[]\", \"playsWith\", \"[]\" }),\n"
                                 "        \"responders\",\n"
                                 "        \"[]\"\n"
                                 "    }) -- sent\n"
                                 "    print(string.format(\n"
                                 "        \"%s and %s and %s\",\n"
                                 "        tostring(aVeryLongName),\n"
                                 "        tostring(anotherVeryLongName),\n"
                                 "        tostring(yetAnotherName)\n"
                                 "    ))\n"
                                 "    print(\"a string with no comma in it that is longer than the width of one hundred columns, and so on\")\n"
                                 "end\n";
        const std::string out = ALScriptFormatter::format(in, options);
        ensure_equals("broken", out, want);
        ensure_equals("the same again", ALScriptFormatter::format(out, options), want);

        ALScriptFormatter::Options none = options;
        none.width                      = 0;
        ensure("no width, no break", ALScriptFormatter::format(in, none).find("{\n") == std::string::npos);
        const std::string lines = ALScriptFormatter::formatLines(in, options, 0, 5);
        ensure_equals("formatLines keeps every line's number", std::count(lines.begin(), lines.end(), '\n'), std::count(in.begin(), in.end(), '\n'));

        const std::vector<std::string> each = ALScriptFormatter::formatEach(in, options, 0, 5);
        ensure_equals("one a line, and the empty one after the last break", each.size(), size_t(6));
        ensure_equals("the broken line's, breaks and all", each[1] + "\n", want.substr(want.find("    local r"), want.find("    print(string") - want.find("    local r")));
        ensure_equals("a line asked for", ALScriptFormatter::formatEach(in, options, 2, 2)[1], in.substr(in.find("    local r"), in.find(" -- sent") + 8 - in.find("    local r")));
    }

    template<> template<>
    void alscriptformatter_object::test<11>()
    {
        set_test_name("LSL past the width: a vector or rotation never broken, nor spaced inside where a line begins with one; a call's list "
                      "on the call's line; tabs a level wide; not a line with a comment inside it");
        ALScriptFormatter::Options options;
        options.width        = 100;
        const std::string in = "default\n{\n    state_entry()\n    {\n"
                               "        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_POSITION, <1.0, 2.0, 3.0>, PRIM_ROTATION, "
                               "<0.0, 0.0, 0.0, 1.0>, PRIM_SIZE, <0.5, 0.5, 0.5>, PRIM_COLOR, ALL_SIDES, <1, 1, 1>, 1.0]);\n"
                               "        llSetText(\"a\", <1, 1, 1>, 1.0); /* inside */ llSetText(\"and a longer piece of text than fits\", "
                               "<1, 1, 1>, 1.0);\n"
                               "    }\n}\n";
        const std::string want = "default\n{\n    state_entry()\n    {\n"
                                 "        llSetLinkPrimitiveParamsFast(LINK_THIS, [\n"
                                 "            PRIM_POSITION,\n"
                                 "            <1.0, 2.0, 3.0>,\n"
                                 "            PRIM_ROTATION,\n"
                                 "            <0.0, 0.0, 0.0, 1.0>,\n"
                                 "            PRIM_SIZE,\n"
                                 "            <0.5, 0.5, 0.5>,\n"
                                 "            PRIM_COLOR,\n"
                                 "            ALL_SIDES,\n"
                                 "            <1, 1, 1>,\n"
                                 "            1.0\n"
                                 "        ]);\n"
                                 "        llSetText(\"a\", <1, 1, 1>, 1.0); /* inside */ llSetText(\"and a longer piece of text than fits\", "
                                 "<1, 1, 1>, 1.0);\n"
                                 "    }\n}\n";
        const std::string out = ALScriptFormatter::format(in, options);
        ensure_equals("broken", out, want);
        ensure_equals("the same again", ALScriptFormatter::format(out, options), want);

        // With tabs, each a level's width: 2 levels of 4 and the call's 92
        // are 100, which fits; at 99 it does not.
        ALScriptFormatter::Options tabs = options;
        tabs.tabs                       = true;
        const std::string call = "llSay(0, \"" + std::string(92 - 13, 'x') + "\");";
        ensure_equals("a call of 92", call.size(), size_t(92));
        const std::string tabbed = "default\n{\n    state_entry()\n    {\n        " + call + "\n    }\n}\n";
        ensure("fits", ALScriptFormatter::format(tabbed, tabs).find("\t\t" + call + "\n") != std::string::npos);
        tabs.width = 99;
        ensure("does not", ALScriptFormatter::format(tabbed, tabs).find("\t\tllSay(\n\t\t\t0,\n") != std::string::npos);
    }
}
