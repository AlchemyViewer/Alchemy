/**
 * @file alpreprocessorc11_test.cpp
 * @brief The preprocessor against C11 6.10: each clause, and the examples the standard gives.
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

#include "../alpreprocessor.h"

#include "../test/lltut.h"

#include <map>
#include <regex>
#include <string>
#include <vector>

// Each test is a clause of C11's 6.10, preprocessing directives, or of
// 5.1.1.2, the translation phases the preprocessor does, run over LSL: the
// language the preprocessor is C's for, as Firestorm's, over Boost.Wave, is.
// What comes out is compared as tokens, blanks between them made one space,
// since how much space stands between two tokens is the implementation's
// (6.10.3.2 aside, which the stringizing tests compare exactly).
namespace tut
{
    struct alpreprocessorc11_data
    {
        // A resolver over a table of named texts, which keeps what it was
        // asked.
        std::map<std::string, std::string> files;
        std::vector<ALPreprocessor::Ask>   asked;

        ALPreprocessor::Options options()
        {
            ALPreprocessor::Options o;
            o.fileName = "main.lsl";
            o.unixTime = 1234567890;
            o.resolve  = [this](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
                asked.push_back(ask);
                auto it = files.find(ask.name);
                if (it == files.end())
                {
                    return ALPreprocessor::Found::No;
                }
                out.name = ask.name;
                out.text = it->second;
                return ALPreprocessor::Found::Yes;
            };
            return o;
        }

        ALPreprocessor::Result run(const std::string& source) { return ALPreprocessor::run(source, options()); }

        // The text as tokens: blanks and comments outside string and
        // character literals one space each, none at the ends. A comment is
        // one space to C (5.1.1.2); the run keeps them, for the lines.
        static std::string flat(const std::string& text)
        {
            std::string out;
            char        quote = 0;
            bool        space = false;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quote)
                {
                    out += c;
                    if (c == '\\' && i + 1 < text.size())
                    {
                        out += text[++i];
                    }
                    else if (c == quote)
                    {
                        quote = 0;
                    }
                    continue;
                }
                if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*'))
                {
                    const size_t end = text[i + 1] == '/' ? text.find('\n', i) : text.find("*/", i + 2);
                    i                = end == std::string::npos ? text.size() : (text[i + 1] == '/' ? end - 1 : end + 1);
                    space            = !out.empty();
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    space = !out.empty();
                    continue;
                }
                if (space)
                {
                    out += ' ';
                    space = false;
                }
                if (c == '"' || c == '\'')
                {
                    quote = c;
                }
                out += c;
            }
            return out;
        }

        // What a run made, as tokens, where it said nothing; what it said
        // otherwise.
        std::string pp(const std::string& source)
        {
            const ALPreprocessor::Result r = run(source);
            const std::string            said = messages(r);
            return said.empty() ? flat(r.text) : "PROBLEMS: " + said + "TEXT: " + flat(r.text);
        }

        static std::string messages(const ALPreprocessor::Result& r)
        {
            std::string out;
            for (const ALScriptProblem& p : r.problems)
            {
                out += (p.severity == ALScriptProblem::Severity::Error ? "E " : p.severity == ALScriptProblem::Severity::Warning ? "W " : "N ");
                out += std::to_string(p.line) + ": " + p.message + "\n";
            }
            return out;
        }

        // The lines, from 0, a run said an error of; a warning's with W.
        static std::string saidOn(const ALPreprocessor::Result& r)
        {
            std::string out;
            for (const ALScriptProblem& p : r.problems)
            {
                out += (out.empty() ? "" : " ") + std::string(p.severity == ALScriptProblem::Severity::Error ? "" : "W") + std::to_string(p.line);
            }
            return out;
        }
    };

    typedef test_group<alpreprocessorc11_data, 100> alpreprocessorc11_group;
    typedef alpreprocessorc11_group::object         alpreprocessorc11_object;
    alpreprocessorc11_group                         alpreprocessorc11_instance("alpreprocessorc11");

    template<> template<>
    void alpreprocessorc11_object::test<1>()
    {
        set_test_name("5.1.1.2: a backslash before a newline joins the lines first, inside a directive's name, a name, a string, a // comment");
        ensure_equals("a directive's name", pp(std::string("#def\\\n"
                         "ine SPLICED 1\n"
                         "SPLICED\n")),
                      std::string("1"));
        ensure_equals("a name", pp(std::string("#define SPLICED 1\n"
                         "SPLI\\\n"
                         "CED\n")),
                      std::string("1"));
        ensure_equals("but in a string it is Firestorm's newline, which scripts rely on, the line after it", pp(std::string("string s = \"ab\\\n"
                         "cd\";\n")),
                      std::string("string s = \"ab\\ncd\" ;"));
        {
            const ALPreprocessor::Result r = run("// a note \\\nint hidden;\nint shown;\n");
            ensure("a // comment goes on over the join: " + r.text, r.text.find("\nint hidden") == std::string::npos &&
                                                                    r.text.find("int shown;") != std::string::npos);
        }
        ensure_equals("the lines after keep their numbers", pp(std::string("#define A 1 \\\n"
                         " + 2\n"
                         "A __LINE__\n")),
                      std::string("1 + 2 3"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<2>()
    {
        set_test_name("5.1.1.2: a comment is one space: between tokens, in a macro's body, and over the lines of a directive");
        ensure_equals("between tokens", pp(std::string("a/**/b\n")),
                      std::string("a b"));
        ensure_equals("in a body", pp(std::string("#define f(x) x/**/x\n"
                         "f(1)\n")),
                      std::string("1 1"));
        ensure_equals("a directive goes on past a comment over lines", pp(std::string("#define LONG 1 /* a\n"
                         "b */ + 2\n"
                         "LONG\n")),
                      std::string("1 + 2"));
        ensure_equals("comments at a body's ends are none of it", pp(std::string("#define E /* a */ 1 /* b */\n"
                         "[E]\n")),
                      std::string("[1]"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<3>()
    {
        set_test_name("6.10: blanks and comments may stand before #, blanks after it; the null directive; # not first on a line is no directive");
        ensure_equals("before and after", pp(std::string("  /* c */ #  define X 1\n"
                         "X\n")),
                      std::string("1"));
        ensure_equals("the null directive", pp(std::string("#\n"
                         "# /* nothing */\n"
                         "ok\n")),
                      std::string("ok"));
        ensure_equals("# after a token", pp(std::string("x # define Y 2\n"
                         "Y\n")),
                      std::string("x # define Y 2 Y"));
        ensure_equals("a # a macro made is no directive (6.10.3.4)", pp(std::string("#define EMPTY\n"
                         "EMPTY # define W 3\n"
                         "W\n")),
                      std::string("# define W 3 W"));
        ensure_equals("a directive's name is as written, case and all", saidOn(run(std::string("#DEFINE Z 1\n"))), std::string("0"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<4>()
    {
        set_test_name("6.10.1: in a skipped group only the directives' names are read: any other line, an unknown directive, an unfinished one");
        ensure_equals("nothing said", pp(std::string("#if 0\n"
                         "#foo bar\n"
                         "#include <nowhere\n"
                         "#define\n"
                         "#if\n"
                         "it's\n"
                         "#endif\n"
                         "#endif\n"
                         "end\n")),
                      std::string("end"));
        ensure_equals("an #elif after the group taken is not worked out", pp(std::string("#if 1\n"
                         "a\n"
                         "#elif 1 / 0\n"
                         "b\n"
                         "#else\n"
                         "c\n"
                         "#endif\n")),
                      std::string("a"));
        ensure_equals("nor one inside a skipped group", pp(std::string("#if 0\n"
                         "#if 1 / 0\n"
                         "#elif garbage (\n"
                         "#else\n"
                         "#endif\n"
                         "#endif\n"
                         "d\n")),
                      std::string("d"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<5>()
    {
        set_test_name("6.10.1: defined in both forms, names left over are 0, and each operator an #if may have");
        ensure_equals("defined", pp(std::string("#define A\n"
                         "#if defined A && defined(A) && !defined B && defined ( A )\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("a name left over is 0, TRUE too", pp(std::string("#if false || TRUE || sizeof || other\n"
                         "no\n"
                         "#else\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("but true is 1, as in C++ and Firestorm's Wave", pp(std::string("#if true && !false\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("relations", pp(std::string("#if 1 < 2 < 3 && (1 > 2) == 0 && 2 >= 2 && (3 <= 2) == 0 && 1 != 2\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("bits and unary", pp(std::string("#if (7 & 3) == 3 && (4 | 1) == 5 && (6 ^ 3) == 5 && ~0 == -1 && -(-3) == 3 && +4 == 4 && !0 == 1 && !7 == 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("arithmetic", pp(std::string("#if 17 % 5 == 2 && -7 / 2 == -3 && -7 % 2 == -1 && 1 << 62 > 0 && (1 << 3) >> 1 == 4 && 2 * 3 + 4 == 10 && 2 + 3 * 4 == 14\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("integers every way C writes them", pp(std::string("#if 017 == 15 && 0XfF == 255 && 10U == 10 && 10L == 10 && 10ull == 10 && 10uLL == 10 && 0x7FFFFFFFFFFFFFFF > 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("?: and what it does not work out", pp(std::string("#if (1 ? 2 : 3) == 2 && (0 ? 2 : 3) == 3 && (1 ? 0 : 1 / 0) == 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("&& and || do not work out their right side where the left decides", pp(std::string("#if 0 && 1 / 0\n"
                         "#endif\n"
                         "#if 1 || 1 / 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("macros in an #if, function-like too", pp(std::string("#define F(x) ((x) + 1)\n"
                         "#define TWO 2\n"
                         "#if F(1) == TWO && F(F(1)) == 3\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<6>()
    {
        set_test_name("6.10.1: an #if works in intmax_t and uintmax_t, and an unsigned operand makes the other unsigned");
        ensure_equals("-1 < 0u is false", pp(std::string("#if -1 < 0u\n"
                         "no\n"
                         "#else\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("unsigned wraps", pp(std::string("#if 0u - 1 == 18446744073709551615u\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("a decimal too big for intmax_t is unsigned", pp(std::string("#if 18446744073709551615 == -1\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("a hex one too", pp(std::string("#if 0xFFFFFFFFFFFFFFFF > 0 && 0xFFFFFFFFFFFFFFFF == -1\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("the least intmax_t", pp(std::string("#if -9223372036854775807 - 1 < 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("?:'s result is unsigned where either side is", pp(std::string("#if (1 ? -1 : 0u) > 0\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<7>()
    {
        set_test_name("6.10.1: an #if refuses what is no integer constant expression, each said on its line");
        ensure_equals("too big, a float, a string, a comma, an assignment, % by zero, a call of no macro, nothing", saidOn(run(std::string("#if 18446744073709551616\n"
                              "#endif\n"
                              "#if 1.0\n"
                              "#endif\n"
                              "#if \"s\"\n"
                              "#endif\n"
                              "#if 1, 2\n"
                              "#endif\n"
                              "#if X = 1\n"
                              "#endif\n"
                              "#if 5 % 0\n"
                              "#endif\n"
                              "#if G(1)\n"
                              "#endif\n"
                              "#if\n"
                              "#endif\n"))), std::string("0 2 4 6 8 10 12 14"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<8>()
    {
        set_test_name("6.10.1: #if takes character constants, as C gives their values");
        ensure_equals("letters, escapes, octal and hex", pp(std::string("#if 'A' == 65 && '\\n' == 10 && '\\x41' == 65 && '\\0' == 0 && '\\'' == 39 && '\\\\' == 92 && '0' == 48 && '\\101' == 65\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<9>()
    {
        set_test_name("6.10.1: #ifdef, #ifndef, #else and #endif take nothing more, and something more is said");
        ensure_equals("extra tokens", saidOn(run(std::string("#ifdef A B\n"
                              "#endif\n"
                              "#ifndef A B\n"
                              "#endif\n"
                              "#if 1\n"
                              "#else junk\n"
                              "#endif junk\n"
                              "#undef A B\n"))), std::string("W0 W2 W5 W6 W7"));
        ensure_equals("#ifdef needs a name", saidOn(run(std::string("#ifdef\n"
                              "#endif\n"
                              "#ifdef 1\n"
                              "#endif\n"))), std::string("0 2"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<10>()
    {
        set_test_name("6.10.2: an #include's name may come of macros, in quotes or in angle brackets");
        files["lib.lsl"]  = "LIB\n";
        files["vers2.h"]  = "VERS\n";
        ensure_equals("angle brackets", pp(std::string("#define HDR <lib.lsl>\n"
                         "#include HDR\n")),
                      std::string("LIB"));
        ensure("asked in brackets", !asked.empty() && asked.back().name == "lib.lsl" && asked.back().angled);
        ensure_equals("quotes", pp(std::string("#define Q \"lib.lsl\"\n"
                         "#include Q\n")),
                      std::string("LIB"));
        ensure("asked in quotes", asked.back().name == "lib.lsl" && !asked.back().angled);
        ensure_equals("made by stringizing", pp(std::string("#define STR(x) #x\n"
                         "#define XSTR(x) STR(x)\n"
                         "#define INCFILE(n) vers ## n\n"
                         "#include XSTR(INCFILE(2).h)\n")),
                      std::string("VERS"));
        ensure_equals("something after the name", saidOn(run(std::string("#include \"lib.lsl\" extra\n"))), std::string("W0"));
        ensure_equals("no name at all", saidOn(run(std::string("#include\n"
                              "#include 42\n"))), std::string("0 1"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<11>()
    {
        set_test_name("6.10.3 EXAMPLE 6: a redefinition the same but for blanks is none; any other is said");
        ensure_equals("valid ones say nothing", saidOn(run(std::string("#define OBJ_LIKE (1-1)\n"
                              "#define OBJ_LIKE /* white space */ (1-1) /* other */\n"
                              "#define FUNC_LIKE(a) ( a )\n"
                              "#define FUNC_LIKE( a )( /* note the white space */ \\\n"
                              " a /* other stuff on this line\n"
                              " */ )\n"))), std::string(""));
        ensure_equals("invalid ones are each said", saidOn(run(std::string("#define OBJ_LIKE (1-1)\n"
                              "#define FUNC_LIKE(a) ( a )\n"
                              "#define OBJ_LIKE (0)\n"
                              "#define OBJ_LIKE (1 - 1)\n"
                              "#define FUNC_LIKE(b) ( a )\n"
                              "#define FUNC_LIKE(b) ( b )\n"))), std::string("W2 W3 W4 W5"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<12>()
    {
        set_test_name("6.10.3: object-like or function-like by a ( straight after the name; a function-like name without ( is a name");
        ensure_equals("a space before ( makes it object-like", pp(std::string("#define f (x)\n"
                         "f\n")),
                      std::string("(x)"));
        ensure_equals("a body straight after the name", pp(std::string("#define X+1\n"
                         "[X]\n")),
                      std::string("[+1]"));
        ensure_equals("a name alone, at the end or before something else", pp(std::string("#define f(x) <x>\n"
                         "f; f\n")),
                      std::string("f; f"));
        ensure_equals("the ( may be on the next line", pp(std::string("#define f(x) <x>\n"
                         "f\n"
                         "(1)\n")),
                      std::string("<1>"));
        ensure_equals("a ( that a macro makes is not the invocation's", pp(std::string("#define f(x) <x>\n"
                         "#define LP (\n"
                         "f LP 1)\n")),
                      std::string("f ( 1)"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<13>()
    {
        set_test_name("6.10.3: arguments: commas inside parentheses stay, inside brackets and braces they part; an empty argument is one; too many or few are said");
        ensure_equals("parentheses", pp(std::string("#define N(a) <a>\n"
                         "N((1, 2))\n")),
                      std::string("<(1, 2)>"));
        ensure_equals("brackets and braces", pp(std::string("#define M(a, b) <a|b>\n"
                         "M([1, 2]) M({1, 2})\n")),
                      std::string("<[1|2]> <{1|2}>"));
        ensure_equals("empty", pp(std::string("#define N(a) <a>\n"
                         "#define Z() 0\n"
                         "N() Z() Z( )\n")),
                      std::string("<> 0 0"));
        ensure_equals("over lines", pp(std::string("#define M(a, b) <a|b>\n"
                         "M(1,\n"
                         "2\n"
                         ")\n")),
                      std::string("<1|2>"));
        ensure_equals("arity", saidOn(run(std::string("#define Z() 0\n"
                              "#define M(a, b) <a|b>\n"
                              "Z(1)\n"
                              "M(1)\n"
                              "M(1, 2, 3)\n"))), std::string("2 3 4"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<14>()
    {
        set_test_name("6.10.3: __VA_ARGS__ is a variadic macro's alone, and neither it nor defined may be a macro");
        ensure_equals("variadic", pp(std::string("#define V(...) [__VA_ARGS__]\n"
                         "V() V(a) V(a, b) V((a, b), c)\n")),
                      std::string("[] [a] [a, b] [(a, b), c]"));
        ensure_equals("after named parameters", pp(std::string("#define W(a, ...) [a|__VA_ARGS__]\n"
                         "W(1,) W(1, 2, 3)\n")),
                      std::string("[1|] [1|2, 3]"));
        ensure_equals("refused", saidOn(run(std::string("#define A(x) __VA_ARGS__\n"
                              "#define __VA_ARGS__ 1\n"
                              "#define defined 1\n"
                              "#undef defined\n"
                              "#define B(__VA_ARGS__) 1\n"))), std::string("0 1 2 3 4"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<15>()
    {
        set_test_name("6.10.3.1: an argument is expanded first, on its own, unless # or ## is beside it");
        ensure_equals("beside # and ##, not", pp(std::string("#define STR(x) #x\n"
                         "#define XSTR(x) STR(x)\n"
                         "#define CAT(a, b) a ## b\n"
                         "#define XCAT(a, b) CAT(a, b)\n"
                         "#define FOUR 4\n"
                         "STR(FOUR) XSTR(FOUR) CAT(FOUR, x) XCAT(FOUR, x)\n")),
                      std::string("\"FOUR\" \"4\" FOURx 4x"));
        ensure_equals("on its own: a name at an argument's end takes nothing after the invocation", pp(std::string("#define id(x) x\n"
                         "#define h() H\n"
                         "id(h)()\n")),
                      std::string("H"));
        ensure_equals("a comma an argument makes parts the arguments of the macro it goes on to", pp(std::string("#define COMMA ,\n"
                         "#define f(a, b) [a|b]\n"
                         "#define g(x) f(x)\n"
                         "g(1 COMMA 2)\n")),
                      std::string("[1|2]"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<16>()
    {
        set_test_name("6.10.3.2: # makes one string: blanks at the ends dropped, those inside one space, a literal's quotes and backslashes escaped");
        ensure_equals("blanks", pp(std::string("#define STR(x) #x\n"
                         "STR(  a   +   b  )\n")),
                      std::string("\"a + b\""));
        ensure_equals("a comment is a blank", pp(std::string("#define STR(x) #x\n"
                         "STR(a/**/b)\n")),
                      std::string("\"a b\""));
        ensure_equals("a newline is a blank", pp(std::string("#define STR(x) #x\n"
                         "STR( a\n"
                         "   b )\n")),
                      std::string("\"a b\""));
        ensure_equals("no blank where none was", pp(std::string("#define STR(x) #x\n"
                         "STR(a+b)\n")),
                      std::string("\"a+b\""));
        ensure_equals("a string's quotes and backslashes", pp(std::string("#define STR(x) #x\n"
                         "STR(\"a\\n\")\n")),
                      std::string("\"\\\"a\\\\n\\\"\""));
        ensure_equals("a character's", pp(std::string("#define STR(x) #x\n"
                         "STR('\"') STR('\\'')\n")),
                      std::string("\"'\\\"'\" \"'\\\\''\""));
        ensure_equals("outside a literal, a backslash is as it is", pp(std::string("#define STR(x) #x\n"
                         "STR(: @\\n)\n")),
                      std::string("\": @\\n\""));
        ensure_equals("nothing", pp(std::string("#define STR(x) #x\n"
                         "STR()\n")),
                      std::string("\"\""));
    }

    template<> template<>
    void alpreprocessorc11_object::test<17>()
    {
        set_test_name("6.10.3.3: ## pastes, an empty argument is a placemarker, and what it makes is expanded again");
        ensure_equals("tokens", pp(std::string("#define CAT(a, b) a ## b\n"
                         "CAT(ll, Say) CAT(-, -) CAT(<, <=) CAT(1, 2) CAT(+, =)\n")),
                      std::string("llSay -- <<= 12 +="));
        ensure_equals("a name made is expanded", pp(std::string("#define CAT(a, b) a ## b\n"
                         "#define AB 42\n"
                         "CAT(A, B)\n")),
                      std::string("42"));
        ensure_equals("empty on either side", pp(std::string("#define CAT(a, b) a ## b\n"
                         "[CAT(, x)] [CAT(x, )] [CAT(, )]\n")),
                      std::string("[x] [x] []"));
        ensure_equals("EXAMPLE 5", pp(std::string("#define t(x,y,z) x ## y ## z\n"
                         "int j[] = { t(1,2,3), t(,4,5), t(6,,7), t(8,9,),\n"
                         " t(10,,), t(,11,), t(,,12), t(,,) };\n")),
                      std::string("int j[] = { 123, 45, 67, 89, 10, 11, 12, };"));
        ensure_equals("the EXAMPLE's hash_hash: ## made of # ## # is no operator", pp(std::string("#define hash_hash # ## #\n"
                         "#define mkstr(a) # a\n"
                         "#define in_between(a) mkstr(a)\n"
                         "#define join(c, d) in_between(c hash_hash d)\n"
                         "char p[] = join(x, y);\n")),
                      std::string("char p[] = \"x ## y\";"));
        ensure_equals("a paste that makes no one token is said", saidOn(run(std::string("#define CAT(a, b) a ## b\n"
                              "CAT(x, +)\n"
                              "CAT(\"a\", \"b\")\n"))), std::string("1 2"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<18>()
    {
        set_test_name("6.10.3.4: rescanning: a macro's own name is not expanded again, even later, and one it hands on takes its arguments from what follows");
        ensure_equals("the EXAMPLE", pp(std::string("#define f(a) a*g\n"
                         "#define g(a) f(a)\n"
                         "f(2)(9)\n")),
                      std::string("2*9*g"));
        ensure_equals("mutual", pp(std::string("#define x y\n"
                         "#define y x\n"
                         "x y\n")),
                      std::string("x y"));
        ensure_equals("painted: a name met while its macro was being replaced stays a name", pp(std::string("#define obj(x) x obj\n"
                         "obj(obj)(1)\n")),
                      std::string("obj obj(1)"));
        ensure_equals("a name handed on finds its ( in the source", pp(std::string("#define f(x) <x>\n"
                         "#define g f\n"
                         "g(1) g\n"
                         "(2)\n")),
                      std::string("<1> <2>"));
        ensure_equals("and through two macros", pp(std::string("#define NIL(xxx) xxx\n"
                         "#define G_0(arg) NIL(G_1)(arg)\n"
                         "#define G_1(arg) NIL(arg)\n"
                         "G_0(42)\n")),
                      std::string("42"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<19>()
    {
        set_test_name("6.10.3.5 EXAMPLE 3: macros that name themselves, macros in arguments, placemarkers and #");
        ensure_equals("the whole", pp(std::string("#define x 3\n"
                         "#define f(a) f(x * (a))\n"
                         "#undef x\n"
                         "#define x 2\n"
                         "#define g f\n"
                         "#define z z[0]\n"
                         "#define h g(~\n"
                         "#define m(a) a(w)\n"
                         "#define w 0,1\n"
                         "#define t(a) a\n"
                         "#define p() int\n"
                         "#define q(x) x\n"
                         "#define r(x,y) x ## y\n"
                         "#define str(x) # x\n"
                         "f(y+1) + f(f(z)) % t(t(g)(0) + t)(1);\n"
                         "g(x+(3,4)-w) | h 5) & m\n"
                         "(f)^m(m);\n"
                         "p() i[q()] = { q(1), r(2,3), r(4,), r(,5), r(,) };\n"
                         "char c[2][6] = { str(hello), str() };\n")),
                      std::string("f(2 * (y+1)) + f(2 * (f(2 * (z[0])))) % f(2 * (0)) + t(1); f(2 * (2+(3,4)-0,1)) | f(2 * (~ 5)) & f(2 * (0,1))^m(0,1); int i[] = { 1, 23, 4, 5, }; char c[2][6] = { \"hello\", \"\" };"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<20>()
    {
        set_test_name("6.10.3.5 EXAMPLE 4: #, ##, and macros made of both");
        ensure_equals("debug, fputs, glue", pp(std::string("#define str(s) # s\n"
                         "#define xstr(s) str(s)\n"
                         "#define debug(s, t) printf(\"x\" # s \"= %d, x\" # t \"= %s\", \\\n"
                         " x ## s, x ## t)\n"
                         "#define INCFILE(n) vers ## n\n"
                         "#define glue(a, b) a ## b\n"
                         "#define xglue(a, b) glue(a, b)\n"
                         "#define HIGHLOW \"hello\"\n"
                         "#define LOW LOW \", world\"\n"
                         "debug(1, 2);\n"
                         "fputs(str(strncmp(\"abc\\0d\", \"abc\", '\\4') // this goes away\n"
                         " == 0) str(: @\\n), s);\n"
                         "glue(HIGH, LOW);\n"
                         "xglue(HIGH, LOW)\n")),
                      std::string("printf(\"x\" \"1\" \"= %d, x\" \"2\" \"= %s\", x1, x2); fputs(\"strncmp(\\\"abc\\\\0d\\\", \\\"abc\\\", '\\\\4') == 0\" \": @\\n\", s); \"hello\"; \"hello\" \", world\""));
    }

    template<> template<>
    void alpreprocessorc11_object::test<21>()
    {
        set_test_name("6.10.3.5 EXAMPLE 7: variadic macros");
        ensure_equals("debug, showlist, report", pp(std::string("#define debug(...) fprintf(stderr, __VA_ARGS__)\n"
                         "#define showlist(...) puts(#__VA_ARGS__)\n"
                         "#define report(test, ...) ((test)?puts(#test):\\\n"
                         " printf(__VA_ARGS__))\n"
                         "debug(\"Flag\");\n"
                         "debug(\"X = %d\\n\", x);\n"
                         "showlist(The first, second, and third items.);\n"
                         "report(x>y, \"x is %d but y is %d\", x, y);\n")),
                      std::string("fprintf(stderr, \"Flag\"); fprintf(stderr, \"X = %d\\n\", x); puts(\"The first, second, and third items.\"); ((x>y)?puts(\"x>y\"): printf(\"x is %d but y is %d\", x, y));"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<22>()
    {
        set_test_name("6.10.4: #line sets the next line's __LINE__, and __FILE__ with a name; its numbers may come of macros; out of range is said");
        ensure_equals("a number", pp(std::string("#line 100\n"
                         "__LINE__\n"
                         "__LINE__\n")),
                      std::string("100 101"));
        ensure_equals("and a name", pp(std::string("#line 7 \"other.lsl\"\n"
                         "__LINE__ __FILE__\n")),
                      std::string("7 \"other.lsl\""));
        ensure_equals("of macros", pp(std::string("#define L 40\n"
                         "#line L\n"
                         "__LINE__\n")),
                      std::string("40"));
        ensure_equals("refused", saidOn(run(std::string("#line 0\n"
                              "#line abc\n"
                              "#line\n"
                              "#line 10 name\n"))), std::string("0 1 2 3"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<23>()
    {
        set_test_name("6.10.5, 6.10.6, 6.10.7: #error says its words and the run goes on; a #pragma not known is passed over; # alone is nothing");
        {
            const ALPreprocessor::Result r = run("#error one  two\nafter\n");
            ensure_equals("said as written", messages(r), std::string("E 0: one  two\n"));
            ensure_equals("and on", flat(r.text), std::string("after"));
        }
        ensure_equals("pragmas passed over", pp(std::string("#pragma STDC FP_CONTRACT ON\n"
                         "#pragma whatever it says\n"
                         "ok\n")),
                      std::string("ok"));
        ensure_equals("the null directive", pp(std::string("#\n"
                         "ok\n")),
                      std::string("ok"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<24>()
    {
        set_test_name("6.10.8: __LINE__ and __FILE__ where they stand, __DATE__ and __TIME__ as C writes them, and #ifdef knows each");
        ensure_equals("lines from 1", pp(std::string("__LINE__\n"
                         "\n"
                         "__LINE__\n")),
                      std::string("1 3"));
        ensure_equals("defined", pp(std::string("#if defined(__LINE__) && defined __FILE__ && defined(__DATE__) && defined __TIME__\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        {
            const std::string text = flat(run("__DATE__ __TIME__\n").text);
            ensure("\"Mmm dd yyyy\" \"hh:mm:ss\", a day under 10 with a space: " + text,
                   std::regex_match(text, std::regex("\"(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 123][0-9] [0-9]{4}\" \"[0-2][0-9]:[0-5][0-9]:[0-6][0-9]\"")));
        }
    }

    template<> template<>
    void alpreprocessorc11_object::test<25>()
    {
        set_test_name("6.10.9: _Pragma is #pragma as an operator, where a macro can make it");
        files["p.lsl"] = "_Pragma(\"once\")\nP\n";
        ensure_equals("once", pp(std::string("#include \"p.lsl\"\n"
                         "#include \"p.lsl\"\n")),
                      std::string("P"));
        ensure_equals("made by a macro, and gone from the text", pp(std::string("#define DO(x) _Pragma(#x)\n"
                         "DO(whatever) ok\n")),
                      std::string("ok"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<26>()
    {
        set_test_name("the text keeps apart tokens that a macro set beside each other: -f(1) is still two minuses");
        ensure_equals("a minus before a macro's", pp(std::string("#define f(x) -x\n"
                         "-f(1)\n")),
                      std::string("- -1"));
        ensure_equals("a plus", pp(std::string("#define PLUS +\n"
                         "+PLUS\n")),
                      std::string("+ +"));
        ensure_equals("an empty macro between two", pp(std::string("#define E\n"
                         "x E-E-y\n")),
                      std::string("x - -y"));
        ensure_equals("two invocations", pp(std::string("#define I(x) x\n"
                         "I(-)I(-)\n")),
                      std::string("- -"));
        ensure_equals("names", pp(std::string("#define A a\n"
                         "#define B b\n"
                         "A B\n")),
                      std::string("a b"));
        ensure_equals("a paste means it", pp(std::string("#define CAT(a, b) a ## b\n"
                         "CAT(-, -)\n")),
                      std::string("--"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<27>()
    {
        set_test_name("6.10.3.4: expansion deferred by an empty macro, and made to happen by a macro that rescans");
        ensure_equals("deferred", pp(std::string("#define EMPTY()\n"
                         "#define DEFER(id) id EMPTY()\n"
                         "#define A() 123\n"
                         "DEFER(A)()\n")),
                      std::string("A ()"));
        ensure_equals("and expanded by one more scan", pp(std::string("#define EMPTY()\n"
                         "#define DEFER(id) id EMPTY()\n"
                         "#define EXPAND(...) __VA_ARGS__\n"
                         "#define A() 123\n"
                         "EXPAND(DEFER(A)())\n")),
                      std::string("123"));
        ensure_equals("a macro passed as an argument and called in the body", pp(std::string("#define apply(m, x) m(x)\n"
                         "#define sq(x) ((x)*(x))\n"
                         "apply(sq, 3)\n")),
                      std::string("((3)*(3))"));
        ensure_equals("each other through arguments", pp(std::string("#define f(x) g(x)\n"
                         "#define g(x) f(x)\n"
                         "f(1) g(2)\n")),
                      std::string("f(1) g(2)"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<28>()
    {
        set_test_name("6.10.3.2: # of __VA_ARGS__: the arguments and their commas, blanks as written between them made one");
        ensure_equals("none", pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S()\n")),
                      std::string("\"\""));
        ensure_equals("commas kept as written", pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S(a,b) S( a , b ) S(a,  b)\n")),
                      std::string("\"a,b\" \"a , b\" \"a, b\""));
        ensure_equals("a string among them", pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S(\"x\", y)\n")),
                      std::string("\"\\\"x\\\", y\""));
    }

    template<> template<>
    void alpreprocessorc11_object::test<29>()
    {
        set_test_name("6.10.3.3: a paste that makes a comment's start, or any two tokens, is said, and the tokens are left as they were");
        ensure_equals("// and a name with a mark", saidOn(run(std::string("#define CAT(a, b) a ## b\n"
                              "CAT(/, /)\n"
                              "CAT(x, .)\n"))), std::string("1 2"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<30>()
    {
        set_test_name("6.10.1: #ifdef knows a function-like macro and a predefined one; #undef and a new body say nothing");
        ensure_equals("defined", pp(std::string("#define F(x) x\n"
                         "#ifdef F\n"
                         "yes\n"
                         "#endif\n"
                         "#ifndef __LINE__\n"
                         "no\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("undef, then any body", saidOn(run(std::string("#define A 1\n"
                              "#undef A\n"
                              "#define A 2\n"
                              "#undef NEVER_DEFINED\n"))), std::string(""));
    }

    template<> template<>
    void alpreprocessorc11_object::test<31>()
    {
        set_test_name("6.10.1: #elif and #else chains: the first true group only, nested groups inside skipped ones skipped whole");
        ensure_equals("a chain", pp(std::string("#define V 3\n"
                         "#if V == 1\n"
                         "one\n"
                         "#elif V == 2\n"
                         "two\n"
                         "#elif V == 3\n"
                         "three\n"
                         "#elif V == 3\n"
                         "again\n"
                         "#else\n"
                         "else\n"
                         "#endif\n")),
                      std::string("three"));
        ensure_equals("nested", pp(std::string("#if 1\n"
                         "#if 0\n"
                         "a\n"
                         "#elif 1\n"
                         "b\n"
                         "#if 0\n"
                         "c\n"
                         "#else\n"
                         "d\n"
                         "#endif\n"
                         "#endif\n"
                         "#else\n"
                         "#if 1\n"
                         "e\n"
                         "#endif\n"
                         "#endif\n")),
                      std::string("b d"));
        ensure_equals("an #elif with nothing, where it is worked out", saidOn(run(std::string("#if 0\n"
                              "#elif\n"
                              "#endif\n"))), std::string("1"));
        ensure_equals("but not where it is skipped", saidOn(run(std::string("#if 1\n"
                              "#elif\n"
                              "#endif\n"))), std::string(""));
    }

    template<> template<>
    void alpreprocessorc11_object::test<32>()
    {
        set_test_name("6.10.1: more integer arithmetic: unsigned shifts and division, a negative right shift as GCC and Wave do it, a bare 0x refused");
        ensure_equals("unsigned", pp(std::string("#if 1u << 63 > 0 && 18446744073709551615u / 2 == 9223372036854775807 && (0u - 1) >> 63 == 1\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("signed", pp(std::string("#if -1 >> 1 == -1 && (-1) / 2 == 0 && -8 >> 1 == -4\n"
                         "yes\n"
                         "#endif\n")),
                      std::string("yes"));
        ensure_equals("0x alone, and a suffix C has not got", saidOn(run(std::string("#if 0x\n"
                              "#endif\n"
                              "#if 10lul\n"
                              "#endif\n"))), std::string("0 2"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<33>()
    {
        set_test_name("6.10.2: an include's own lines and name, and the file it came back to after it");
        files["inc.lsl"] = "inc __LINE__ __FILE__\n#define FROM_INC 1\n";
        ensure_equals("inside and after", pp(std::string("one\n"
                         "#include \"inc.lsl\"\n"
                         "__LINE__ __FILE__ FROM_INC\n")),
                      std::string("one inc 1 \"inc.lsl\" 3 \"main.lsl\" 1"));
        files["half.lsl"] = "#define f(x) <x>\nf(\n";
        ensure_equals("an invocation cannot run out of an include", saidOn(run(std::string("#include \"half.lsl\"\n"
                              "1)\n"))), std::string("1"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<34>()
    {
        set_test_name("files that end without a newline: a directive, an invocation, a conditional");
        ensure_equals("a directive last", pp(std::string("#define X 1\n"
                         "X\n"
                         "#undef X")),
                      std::string("1"));
        ensure_equals("an invocation last", pp(std::string("#define f(x) <x>\n"
                         "f(1)")),
                      std::string("<1>"));
        ensure_equals("an #if left open", saidOn(run(std::string("#if 1\n"
                              "x"))), std::string("0"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<35>()
    {
        set_test_name("6.10.3: a macro named as LSL names its own words: C knows no keywords, so any name is a macro's");
        ensure_equals("words and events", pp(std::string("#define default state_x\n"
                         "#define state_entry on_rez\n"
                         "#define jump return\n"
                         "default { state_entry() { jump; } }\n")),
                      std::string("state_x { on_rez() { return; } }"));
    }

    template<> template<>
    void alpreprocessorc11_object::test<36>()
    {
        set_test_name("SLua: #if written as Luau writes it: and, or, not, ~=, and C's too");
        {
            ALPreprocessor::Options lua = options();
            lua.lua                     = true;
            const ALPreprocessor::Result r = ALPreprocessor::run("--#define A 1\n--#define B 0\n"
                                                               "--#if A and not B\none\n--#endif\n"
                                                               "--#if B or A ~= 2\ntwo\n--#endif\n"
                                                               "--#if (A && !B) == 1\nthree\n--#endif\n"
                                                               "--#if not A or B\nno\n--#else\nfour\n--#endif\n", lua);
            ensure_equals("said", messages(r), std::string());
            ensure_equals("taken", flat(r.text), std::string("one two three four"));
        }
    }

    template<> template<>
    void alpreprocessorc11_object::test<37>()
    {
        set_test_name("5.1.1.2: a line ends at CRLF and at a lone CR as at LF, so a backslash before either joins; on a directive's line blanks between the backslash and the newline are let by with a warning, elsewhere they join nothing");
        const auto ended = [](std::string text, const char* end) {
            std::string out;
            for (char c : text)
            {
                out += c == '\n' ? std::string(end) : std::string(1, c);
            }
            return out;
        };
        const std::string lf = "#define SAY(x) \\\n"
                               "    llOwnerSay(x); \\\n"
                               "    llSay(0, x)\n"
                               "default { state_entry() { SAY(\"hi\"); } }\n";
        const std::string joined = pp(lf);
        ensure("the macro's lines are its body: " + joined, joined.find("PROBLEMS") == std::string::npos && joined.find("llSay") != std::string::npos &&
                                                                 joined.find('\\') == std::string::npos && joined.find("default") == 0);
        ensure_equals("CRLF", pp(ended(lf, "\r\n")), joined);
        ensure_equals("a lone CR", pp(ended(lf, "\r")), joined);
        ensure_equals("the lines after keep their numbers", pp(ended("#define A 1 \\\n + 2\nA __LINE__\n", "\r\n")), std::string("1 + 2 3"));

        {
            const ALPreprocessor::Result r = run("#define SAY(x) \\ \t\n"
                                                 "    llOwnerSay(x); \\  \n"
                                                 "    llSay(0, x)\n"
                                                 "default { state_entry() { SAY(\"hi\"); } }\n");
            ensure_equals("blanks after a directive's backslash", flat(r.text), joined);
            ensure_equals("said of each", messages(r),
                          std::string("W 0: backslash and newline separated by space\nW 1: backslash and newline separated by space\n"));
        }
        {
            const ALPreprocessor::Result r = run("// a note \\ \nint shown;\n");
            ensure("a note's backslash and a blank take no line: " + r.text, r.text.find("int shown;") != std::string::npos);
            ensure_equals("nothing said", messages(r), std::string());
        }
    }
}
