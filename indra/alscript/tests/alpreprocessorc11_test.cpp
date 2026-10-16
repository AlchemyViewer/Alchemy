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

#include "../preprocessor/alpreprocessor.h"

#include <catch2/catch_test_macros.hpp>

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
namespace
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

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 5.1.1.2: backslash-newline joins lines first even in names and comments", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a directive's name");
        CHECK(pp(std::string("#def\\\n"
                         "ine SPLICED 1\n"
                         "SPLICED\n")) ==
                      std::string("1"));
        UNSCOPED_INFO("a name");
        CHECK(pp(std::string("#define SPLICED 1\n"
                         "SPLI\\\n"
                         "CED\n")) ==
                      std::string("1"));
        UNSCOPED_INFO("but in a string it is Firestorm's newline, which scripts rely on, the line after it");
        CHECK(pp(std::string("string s = \"ab\\\n"
                         "cd\";\n")) ==
                      std::string("string s = \"ab\\ncd\" ;"));
        {
            const ALPreprocessor::Result r = run("// a note \\\nint hidden;\nint shown;\n");
            INFO("a // comment goes on over the join: " + r.text);
            CHECK(r.text.find("\nint hidden") == std::string::npos);
            CHECK(r.text.find("int shown;") != std::string::npos);
        }
        UNSCOPED_INFO("the lines after keep their numbers");
        CHECK(pp(std::string("#define A 1 \\\n"
                         " + 2\n"
                         "A __LINE__\n")) ==
                      std::string("1 + 2 3"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 5.1.1.2: a comment is one space; in a directive it may run over lines", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("between tokens");
        CHECK(pp(std::string("a/**/b\n")) ==
                      std::string("a b"));
        UNSCOPED_INFO("in a body");
        CHECK(pp(std::string("#define f(x) x/**/x\n"
                         "f(1)\n")) ==
                      std::string("1 1"));
        UNSCOPED_INFO("a directive goes on past a comment over lines");
        CHECK(pp(std::string("#define LONG 1 /* a\n"
                         "b */ + 2\n"
                         "LONG\n")) ==
                      std::string("1 + 2"));
        UNSCOPED_INFO("comments at a body's ends are none of it");
        CHECK(pp(std::string("#define E /* a */ 1 /* b */\n"
                         "[E]\n")) ==
                      std::string("[1]"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10: blanks around #; the null directive; # not first on its line is none", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("before and after");
        CHECK(pp(std::string("  /* c */ #  define X 1\n"
                         "X\n")) ==
                      std::string("1"));
        UNSCOPED_INFO("the null directive");
        CHECK(pp(std::string("#\n"
                         "# /* nothing */\n"
                         "ok\n")) ==
                      std::string("ok"));
        UNSCOPED_INFO("# after a token");
        CHECK(pp(std::string("x # define Y 2\n"
                         "Y\n")) ==
                      std::string("x # define Y 2 Y"));
        UNSCOPED_INFO("a # a macro made is no directive (6.10.3.4)");
        CHECK(pp(std::string("#define EMPTY\n"
                         "EMPTY # define W 3\n"
                         "W\n")) ==
                      std::string("# define W 3 W"));
        UNSCOPED_INFO("a directive's name is as written, case and all");
        CHECK(saidOn(run(std::string("#DEFINE Z 1\n"))) == std::string("0"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: in a skipped group only the directives' names are read", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("nothing said");
        CHECK(pp(std::string("#if 0\n"
                         "#foo bar\n"
                         "#include <nowhere\n"
                         "#define\n"
                         "#if\n"
                         "it's\n"
                         "#endif\n"
                         "#endif\n"
                         "end\n")) ==
                      std::string("end"));
        UNSCOPED_INFO("an #elif after the group taken is not worked out");
        CHECK(pp(std::string("#if 1\n"
                         "a\n"
                         "#elif 1 / 0\n"
                         "b\n"
                         "#else\n"
                         "c\n"
                         "#endif\n")) ==
                      std::string("a"));
        UNSCOPED_INFO("nor one inside a skipped group");
        CHECK(pp(std::string("#if 0\n"
                         "#if 1 / 0\n"
                         "#elif garbage (\n"
                         "#else\n"
                         "#endif\n"
                         "#endif\n"
                         "d\n")) ==
                      std::string("d"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: defined in both forms; names left over are 0; every #if operator", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("defined");
        CHECK(pp(std::string("#define A\n"
                         "#if defined A && defined(A) && !defined B && defined ( A )\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("a name left over is 0, TRUE too");
        CHECK(pp(std::string("#if false || TRUE || sizeof || other\n"
                         "no\n"
                         "#else\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("but true is 1, as in C++ and Firestorm's Wave");
        CHECK(pp(std::string("#if true && !false\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("relations");
        CHECK(pp(std::string("#if 1 < 2 < 3 && (1 > 2) == 0 && 2 >= 2 && (3 <= 2) == 0 && 1 != 2\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("bits and unary");
        CHECK(pp(std::string("#if (7 & 3) == 3 && (4 | 1) == 5 && (6 ^ 3) == 5 && ~0 == -1 && -(-3) == 3 && +4 == 4 && !0 == 1 && !7 == 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("arithmetic");
        CHECK(pp(std::string("#if 17 % 5 == 2 && -7 / 2 == -3 && -7 % 2 == -1 && 1 << 62 > 0 && (1 << 3) >> 1 == 4 && 2 * 3 + 4 == 10 && 2 + 3 * 4 == 14\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("integers every way C writes them");
        CHECK(pp(std::string("#if 017 == 15 && 0XfF == 255 && 10U == 10 && 10L == 10 && 10ull == 10 && 10uLL == 10 && 0x7FFFFFFFFFFFFFFF > 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("?: and what it does not work out");
        CHECK(pp(std::string("#if (1 ? 2 : 3) == 2 && (0 ? 2 : 3) == 3 && (1 ? 0 : 1 / 0) == 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("&& and || do not work out their right side where the left decides");
        CHECK(pp(std::string("#if 0 && 1 / 0\n"
                         "#endif\n"
                         "#if 1 || 1 / 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("macros in an #if, function-like too");
        CHECK(pp(std::string("#define F(x) ((x) + 1)\n"
                         "#define TWO 2\n"
                         "#if F(1) == TWO && F(F(1)) == 3\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: #if works in intmax_t and uintmax_t; one unsigned operand makes both", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("-1 < 0u is false");
        CHECK(pp(std::string("#if -1 < 0u\n"
                         "no\n"
                         "#else\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("unsigned wraps");
        CHECK(pp(std::string("#if 0u - 1 == 18446744073709551615u\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("a decimal too big for intmax_t is unsigned");
        CHECK(pp(std::string("#if 18446744073709551615 == -1\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("a hex one too");
        CHECK(pp(std::string("#if 0xFFFFFFFFFFFFFFFF > 0 && 0xFFFFFFFFFFFFFFFF == -1\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("the least intmax_t");
        CHECK(pp(std::string("#if -9223372036854775807 - 1 < 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("?:'s result is unsigned where either side is");
        CHECK(pp(std::string("#if (1 ? -1 : 0u) > 0\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: an #if that is no integer constant expression is said on its line", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("too big, a float, a string, a comma, an assignment, % by zero, a call of no macro, nothing");
        CHECK(saidOn(run(std::string("#if 18446744073709551616\n"
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
                              "#endif\n"))) == std::string("0 2 4 6 8 10 12 14"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: #if takes character constants with the values C gives them", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("letters, escapes, octal and hex");
        CHECK(pp(std::string("#if 'A' == 65 && '\\n' == 10 && '\\x41' == 65 && '\\0' == 0 && '\\'' == 39 && '\\\\' == 92 && '0' == 48 && '\\101' == 65\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: extra tokens after #ifdef and #ifndef and #else and #endif are said", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("extra tokens");
        CHECK(saidOn(run(std::string("#ifdef A B\n"
                              "#endif\n"
                              "#ifndef A B\n"
                              "#endif\n"
                              "#if 1\n"
                              "#else junk\n"
                              "#endif junk\n"
                              "#undef A B\n"))) == std::string("W0 W2 W5 W6 W7"));
        UNSCOPED_INFO("#ifdef needs a name");
        CHECK(saidOn(run(std::string("#ifdef\n"
                              "#endif\n"
                              "#ifdef 1\n"
                              "#endif\n"))) == std::string("0 2"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.2: an #include's name may come of macros in quotes or angle brackets", "[alpreprocessorc11]")
    {
        files["lib.lsl"]  = "LIB\n";
        files["vers2.h"]  = "VERS\n";
        UNSCOPED_INFO("angle brackets");
        CHECK(pp(std::string("#define HDR <lib.lsl>\n"
                         "#include HDR\n")) ==
                      std::string("LIB"));
        {
            INFO("asked in brackets");
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back().name == "lib.lsl");
            CHECK(asked.back().angled);
        }
        UNSCOPED_INFO("quotes");
        CHECK(pp(std::string("#define Q \"lib.lsl\"\n"
                         "#include Q\n")) ==
                      std::string("LIB"));
        {
            INFO("asked in quotes");
            CHECK(asked.back().name == "lib.lsl");
            CHECK_FALSE(asked.back().angled);
        }
        UNSCOPED_INFO("made by stringizing");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "#define XSTR(x) STR(x)\n"
                         "#define INCFILE(n) vers ## n\n"
                         "#include XSTR(INCFILE(2).h)\n")) ==
                      std::string("VERS"));
        UNSCOPED_INFO("something after the name");
        CHECK(saidOn(run(std::string("#include \"lib.lsl\" extra\n"))) == std::string("W0"));
        UNSCOPED_INFO("no name at all");
        CHECK(saidOn(run(std::string("#include\n"
                              "#include 42\n"))) == std::string("0 1"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3 EXAMPLE 6: a redefinition the same but for blanks is none; any other is said", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("valid ones say nothing");
        CHECK(saidOn(run(std::string("#define OBJ_LIKE (1-1)\n"
                              "#define OBJ_LIKE /* white space */ (1-1) /* other */\n"
                              "#define FUNC_LIKE(a) ( a )\n"
                              "#define FUNC_LIKE( a )( /* note the white space */ \\\n"
                              " a /* other stuff on this line\n"
                              " */ )\n"))) == std::string(""));
        UNSCOPED_INFO("invalid ones are each said");
        CHECK(saidOn(run(std::string("#define OBJ_LIKE (1-1)\n"
                              "#define FUNC_LIKE(a) ( a )\n"
                              "#define OBJ_LIKE (0)\n"
                              "#define OBJ_LIKE (1 - 1)\n"
                              "#define FUNC_LIKE(b) ( a )\n"
                              "#define FUNC_LIKE(b) ( b )\n"))) == std::string("W2 W3 W4 W5"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3: a ( right after the name makes it function-like; uncalled it is a name", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a space before ( makes it object-like");
        CHECK(pp(std::string("#define f (x)\n"
                         "f\n")) ==
                      std::string("(x)"));
        UNSCOPED_INFO("a body straight after the name");
        CHECK(pp(std::string("#define X+1\n"
                         "[X]\n")) ==
                      std::string("[+1]"));
        UNSCOPED_INFO("a name alone, at the end or before something else");
        CHECK(pp(std::string("#define f(x) <x>\n"
                         "f; f\n")) ==
                      std::string("f; f"));
        UNSCOPED_INFO("the ( may be on the next line");
        CHECK(pp(std::string("#define f(x) <x>\n"
                         "f\n"
                         "(1)\n")) ==
                      std::string("<1>"));
        UNSCOPED_INFO("a ( that a macro makes is not the invocation's");
        CHECK(pp(std::string("#define f(x) <x>\n"
                         "#define LP (\n"
                         "f LP 1)\n")) ==
                      std::string("f ( 1)"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3: arguments part at commas not in parentheses; empty counts; arity said", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("parentheses");
        CHECK(pp(std::string("#define N(a) <a>\n"
                         "N((1, 2))\n")) ==
                      std::string("<(1, 2)>"));
        UNSCOPED_INFO("brackets and braces");
        CHECK(pp(std::string("#define M(a, b) <a|b>\n"
                         "M([1, 2]) M({1, 2})\n")) ==
                      std::string("<[1|2]> <{1|2}>"));
        UNSCOPED_INFO("empty");
        CHECK(pp(std::string("#define N(a) <a>\n"
                         "#define Z() 0\n"
                         "N() Z() Z( )\n")) ==
                      std::string("<> 0 0"));
        UNSCOPED_INFO("over lines");
        CHECK(pp(std::string("#define M(a, b) <a|b>\n"
                         "M(1,\n"
                         "2\n"
                         ")\n")) ==
                      std::string("<1|2>"));
        UNSCOPED_INFO("arity");
        CHECK(saidOn(run(std::string("#define Z() 0\n"
                              "#define M(a, b) <a|b>\n"
                              "Z(1)\n"
                              "M(1)\n"
                              "M(1, 2, 3)\n"))) == std::string("2 3 4"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3: __VA_ARGS__ is a variadic macro's alone; it and defined are no macros", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("variadic");
        CHECK(pp(std::string("#define V(...) [__VA_ARGS__]\n"
                         "V() V(a) V(a, b) V((a, b), c)\n")) ==
                      std::string("[] [a] [a, b] [(a, b), c]"));
        UNSCOPED_INFO("after named parameters");
        CHECK(pp(std::string("#define W(a, ...) [a|__VA_ARGS__]\n"
                         "W(1,) W(1, 2, 3)\n")) ==
                      std::string("[1|] [1|2, 3]"));
        UNSCOPED_INFO("refused");
        CHECK(saidOn(run(std::string("#define A(x) __VA_ARGS__\n"
                              "#define __VA_ARGS__ 1\n"
                              "#define defined 1\n"
                              "#undef defined\n"
                              "#define B(__VA_ARGS__) 1\n"))) == std::string("0 1 2 3 4"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.1: an argument is expanded first on its own unless # or ## is beside it", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("beside # and ##, not");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "#define XSTR(x) STR(x)\n"
                         "#define CAT(a, b) a ## b\n"
                         "#define XCAT(a, b) CAT(a, b)\n"
                         "#define FOUR 4\n"
                         "STR(FOUR) XSTR(FOUR) CAT(FOUR, x) XCAT(FOUR, x)\n")) ==
                      std::string("\"FOUR\" \"4\" FOURx 4x"));
        UNSCOPED_INFO("on its own: a name at an argument's end takes nothing after the invocation");
        CHECK(pp(std::string("#define id(x) x\n"
                         "#define h() H\n"
                         "id(h)()\n")) ==
                      std::string("H"));
        UNSCOPED_INFO("a comma an argument makes parts the arguments of the macro it goes on to");
        CHECK(pp(std::string("#define COMMA ,\n"
                         "#define f(a, b) [a|b]\n"
                         "#define g(x) f(x)\n"
                         "g(1 COMMA 2)\n")) ==
                      std::string("[1|2]"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.2: # makes one string with blanks made one and literals escaped", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("blanks");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR(  a   +   b  )\n")) ==
                      std::string("\"a + b\""));
        UNSCOPED_INFO("a comment is a blank");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR(a/**/b)\n")) ==
                      std::string("\"a b\""));
        UNSCOPED_INFO("a newline is a blank");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR( a\n"
                         "   b )\n")) ==
                      std::string("\"a b\""));
        UNSCOPED_INFO("no blank where none was");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR(a+b)\n")) ==
                      std::string("\"a+b\""));
        UNSCOPED_INFO("a string's quotes and backslashes");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR(\"a\\n\")\n")) ==
                      std::string("\"\\\"a\\\\n\\\"\""));
        UNSCOPED_INFO("a character's");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR('\"') STR('\\'')\n")) ==
                      std::string("\"'\\\"'\" \"'\\\\''\""));
        UNSCOPED_INFO("outside a literal, a backslash is as it is");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR(: @\\n)\n")) ==
                      std::string("\": @\\n\""));
        UNSCOPED_INFO("nothing");
        CHECK(pp(std::string("#define STR(x) #x\n"
                         "STR()\n")) ==
                      std::string("\"\""));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.3: ## pastes; an empty argument is a placemarker; the result rescans", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("tokens");
        CHECK(pp(std::string("#define CAT(a, b) a ## b\n"
                         "CAT(ll, Say) CAT(-, -) CAT(<, <=) CAT(1, 2) CAT(+, =)\n")) ==
                      std::string("llSay -- <<= 12 +="));
        UNSCOPED_INFO("a name made is expanded");
        CHECK(pp(std::string("#define CAT(a, b) a ## b\n"
                         "#define AB 42\n"
                         "CAT(A, B)\n")) ==
                      std::string("42"));
        UNSCOPED_INFO("empty on either side");
        CHECK(pp(std::string("#define CAT(a, b) a ## b\n"
                         "[CAT(, x)] [CAT(x, )] [CAT(, )]\n")) ==
                      std::string("[x] [x] []"));
        UNSCOPED_INFO("EXAMPLE 5");
        CHECK(pp(std::string("#define t(x,y,z) x ## y ## z\n"
                         "int j[] = { t(1,2,3), t(,4,5), t(6,,7), t(8,9,),\n"
                         " t(10,,), t(,11,), t(,,12), t(,,) };\n")) ==
                      std::string("int j[] = { 123, 45, 67, 89, 10, 11, 12, };"));
        UNSCOPED_INFO("the EXAMPLE's hash_hash: ## made of # ## # is no operator");
        CHECK(pp(std::string("#define hash_hash # ## #\n"
                         "#define mkstr(a) # a\n"
                         "#define in_between(a) mkstr(a)\n"
                         "#define join(c, d) in_between(c hash_hash d)\n"
                         "char p[] = join(x, y);\n")) ==
                      std::string("char p[] = \"x ## y\";"));
        UNSCOPED_INFO("a paste that makes no one token is said");
        CHECK(saidOn(run(std::string("#define CAT(a, b) a ## b\n"
                              "CAT(x, +)\n"
                              "CAT(\"a\", \"b\")\n"))) == std::string("1 2"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.4: a macro's own name stays a name; one handed on takes later arguments", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("the EXAMPLE");
        CHECK(pp(std::string("#define f(a) a*g\n"
                         "#define g(a) f(a)\n"
                         "f(2)(9)\n")) ==
                      std::string("2*9*g"));
        UNSCOPED_INFO("mutual");
        CHECK(pp(std::string("#define x y\n"
                         "#define y x\n"
                         "x y\n")) ==
                      std::string("x y"));
        UNSCOPED_INFO("painted: a name met while its macro was being replaced stays a name");
        CHECK(pp(std::string("#define obj(x) x obj\n"
                         "obj(obj)(1)\n")) ==
                      std::string("obj obj(1)"));
        UNSCOPED_INFO("a name handed on finds its ( in the source");
        CHECK(pp(std::string("#define f(x) <x>\n"
                         "#define g f\n"
                         "g(1) g\n"
                         "(2)\n")) ==
                      std::string("<1> <2>"));
        UNSCOPED_INFO("and through two macros");
        CHECK(pp(std::string("#define NIL(xxx) xxx\n"
                         "#define G_0(arg) NIL(G_1)(arg)\n"
                         "#define G_1(arg) NIL(arg)\n"
                         "G_0(42)\n")) ==
                      std::string("42"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.5 EXAMPLE 3: macros naming themselves or in arguments; placemarkers; #", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("the whole");
        CHECK(pp(std::string("#define x 3\n"
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
                         "char c[2][6] = { str(hello), str() };\n")) ==
                      std::string("f(2 * (y+1)) + f(2 * (f(2 * (z[0])))) % f(2 * (0)) + t(1); f(2 * (2+(3,4)-0,1)) | f(2 * (~ 5)) & f(2 * (0,1))^m(0,1); int i[] = { 1, 23, 4, 5, }; char c[2][6] = { \"hello\", \"\" };"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.5 EXAMPLE 4: # and ## and macros made of both", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("debug, fputs, glue");
        CHECK(pp(std::string("#define str(s) # s\n"
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
                         "xglue(HIGH, LOW)\n")) ==
                      std::string("printf(\"x\" \"1\" \"= %d, x\" \"2\" \"= %s\", x1, x2); fputs(\"strncmp(\\\"abc\\\\0d\\\", \\\"abc\\\", '\\\\4') == 0\" \": @\\n\", s); \"hello\"; \"hello\" \", world\""));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.5 EXAMPLE 7: variadic macros", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("debug, showlist, report");
        CHECK(pp(std::string("#define debug(...) fprintf(stderr, __VA_ARGS__)\n"
                         "#define showlist(...) puts(#__VA_ARGS__)\n"
                         "#define report(test, ...) ((test)?puts(#test):\\\n"
                         " printf(__VA_ARGS__))\n"
                         "debug(\"Flag\");\n"
                         "debug(\"X = %d\\n\", x);\n"
                         "showlist(The first, second, and third items.);\n"
                         "report(x>y, \"x is %d but y is %d\", x, y);\n")) ==
                      std::string("fprintf(stderr, \"Flag\"); fprintf(stderr, \"X = %d\\n\", x); puts(\"The first, second, and third items.\"); ((x>y)?puts(\"x>y\"): printf(\"x is %d but y is %d\", x, y));"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.4: #line sets __LINE__ and __FILE__ even from macros; a bad one is said", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a number");
        CHECK(pp(std::string("#line 100\n"
                         "__LINE__\n"
                         "__LINE__\n")) ==
                      std::string("100 101"));
        UNSCOPED_INFO("and a name");
        CHECK(pp(std::string("#line 7 \"other.lsl\"\n"
                         "__LINE__ __FILE__\n")) ==
                      std::string("7 \"other.lsl\""));
        UNSCOPED_INFO("of macros");
        CHECK(pp(std::string("#define L 40\n"
                         "#line L\n"
                         "__LINE__\n")) ==
                      std::string("40"));
        UNSCOPED_INFO("refused");
        CHECK(saidOn(run(std::string("#line 0\n"
                              "#line abc\n"
                              "#line\n"
                              "#line 10 name\n"))) == std::string("0 1 2 3"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.5 to 6.10.7: #error goes on; unknown #pragmas pass; # alone is nothing", "[alpreprocessorc11]")
    {
        {
            const ALPreprocessor::Result r = run("#error one  two\nafter\n");
            UNSCOPED_INFO("said as written");
            CHECK(messages(r) == std::string("E 0: one  two\n"));
            UNSCOPED_INFO("and on");
            CHECK(flat(r.text) == std::string("after"));
        }
        UNSCOPED_INFO("pragmas passed over");
        CHECK(pp(std::string("#pragma STDC FP_CONTRACT ON\n"
                         "#pragma whatever it says\n"
                         "ok\n")) ==
                      std::string("ok"));
        UNSCOPED_INFO("the null directive");
        CHECK(pp(std::string("#\n"
                         "ok\n")) ==
                      std::string("ok"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.8: __LINE__ __FILE__ __DATE__ __TIME__ as C has them; each is defined", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("lines from 1");
        CHECK(pp(std::string("__LINE__\n"
                         "\n"
                         "__LINE__\n")) ==
                      std::string("1 3"));
        UNSCOPED_INFO("defined");
        CHECK(pp(std::string("#if defined(__LINE__) && defined __FILE__ && defined(__DATE__) && defined __TIME__\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        {
            const std::string text = flat(run("__DATE__ __TIME__\n").text);
            UNSCOPED_INFO("\"Mmm dd yyyy\" \"hh:mm:ss\", a day under 10 with a space: " + text);
            CHECK(std::regex_match(text, std::regex("\"(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 123][0-9] [0-9]{4}\" \"[0-2][0-9]:[0-5][0-9]:[0-6][0-9]\"")));
        }
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.9: _Pragma is #pragma as an operator a macro can make", "[alpreprocessorc11]")
    {
        files["p.lsl"] = "_Pragma(\"once\")\nP\n";
        UNSCOPED_INFO("once");
        CHECK(pp(std::string("#include \"p.lsl\"\n"
                         "#include \"p.lsl\"\n")) ==
                      std::string("P"));
        UNSCOPED_INFO("made by a macro, and gone from the text");
        CHECK(pp(std::string("#define DO(x) _Pragma(#x)\n"
                         "DO(whatever) ok\n")) ==
                      std::string("ok"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: tokens a macro sets side by side stay apart: -f(1) is two minuses", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a minus before a macro's");
        CHECK(pp(std::string("#define f(x) -x\n"
                         "-f(1)\n")) ==
                      std::string("- -1"));
        UNSCOPED_INFO("a plus");
        CHECK(pp(std::string("#define PLUS +\n"
                         "+PLUS\n")) ==
                      std::string("+ +"));
        UNSCOPED_INFO("an empty macro between two");
        CHECK(pp(std::string("#define E\n"
                         "x E-E-y\n")) ==
                      std::string("x - -y"));
        UNSCOPED_INFO("two invocations");
        CHECK(pp(std::string("#define I(x) x\n"
                         "I(-)I(-)\n")) ==
                      std::string("- -"));
        UNSCOPED_INFO("names");
        CHECK(pp(std::string("#define A a\n"
                         "#define B b\n"
                         "A B\n")) ==
                      std::string("a b"));
        UNSCOPED_INFO("a paste means it");
        CHECK(pp(std::string("#define CAT(a, b) a ## b\n"
                         "CAT(-, -)\n")) ==
                      std::string("--"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.4: an empty macro defers an expansion that one more scan makes", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("deferred");
        CHECK(pp(std::string("#define EMPTY()\n"
                         "#define DEFER(id) id EMPTY()\n"
                         "#define A() 123\n"
                         "DEFER(A)()\n")) ==
                      std::string("A ()"));
        UNSCOPED_INFO("and expanded by one more scan");
        CHECK(pp(std::string("#define EMPTY()\n"
                         "#define DEFER(id) id EMPTY()\n"
                         "#define EXPAND(...) __VA_ARGS__\n"
                         "#define A() 123\n"
                         "EXPAND(DEFER(A)())\n")) ==
                      std::string("123"));
        UNSCOPED_INFO("a macro passed as an argument and called in the body");
        CHECK(pp(std::string("#define apply(m, x) m(x)\n"
                         "#define sq(x) ((x)*(x))\n"
                         "apply(sq, 3)\n")) ==
                      std::string("((3)*(3))"));
        UNSCOPED_INFO("each other through arguments");
        CHECK(pp(std::string("#define f(x) g(x)\n"
                         "#define g(x) f(x)\n"
                         "f(1) g(2)\n")) ==
                      std::string("f(1) g(2)"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.2: #__VA_ARGS__ is the arguments with their commas and blanks made one", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("none");
        CHECK(pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S()\n")) ==
                      std::string("\"\""));
        UNSCOPED_INFO("commas kept as written");
        CHECK(pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S(a,b) S( a , b ) S(a,  b)\n")) ==
                      std::string("\"a,b\" \"a , b\" \"a, b\""));
        UNSCOPED_INFO("a string among them");
        CHECK(pp(std::string("#define S(...) #__VA_ARGS__\n"
                         "S(\"x\", y)\n")) ==
                      std::string("\"\\\"x\\\", y\""));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3.3: a paste making // or two tokens is said and leaves them as they were", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("// and a name with a mark");
        CHECK(saidOn(run(std::string("#define CAT(a, b) a ## b\n"
                              "CAT(/, /)\n"
                              "CAT(x, .)\n"))) == std::string("1 2"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: #ifdef knows function-like and predefined macros; #undef says nothing", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("defined");
        CHECK(pp(std::string("#define F(x) x\n"
                         "#ifdef F\n"
                         "yes\n"
                         "#endif\n"
                         "#ifndef __LINE__\n"
                         "no\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("undef, then any body");
        CHECK(saidOn(run(std::string("#define A 1\n"
                              "#undef A\n"
                              "#define A 2\n"
                              "#undef NEVER_DEFINED\n"))) == std::string(""));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: #elif chains take the first true group and skip nested groups whole", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a chain");
        CHECK(pp(std::string("#define V 3\n"
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
                         "#endif\n")) ==
                      std::string("three"));
        UNSCOPED_INFO("nested");
        CHECK(pp(std::string("#if 1\n"
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
                         "#endif\n")) ==
                      std::string("b d"));
        UNSCOPED_INFO("an #elif with nothing, where it is worked out");
        CHECK(saidOn(run(std::string("#if 0\n"
                              "#elif\n"
                              "#endif\n"))) == std::string("1"));
        UNSCOPED_INFO("but not where it is skipped");
        CHECK(saidOn(run(std::string("#if 1\n"
                              "#elif\n"
                              "#endif\n"))) == std::string(""));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.1: unsigned shift and divide; GCC's negative right shift; bare 0x refused", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("unsigned");
        CHECK(pp(std::string("#if 1u << 63 > 0 && 18446744073709551615u / 2 == 9223372036854775807 && (0u - 1) >> 63 == 1\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("signed");
        CHECK(pp(std::string("#if -1 >> 1 == -1 && (-1) / 2 == 0 && -8 >> 1 == -4\n"
                         "yes\n"
                         "#endif\n")) ==
                      std::string("yes"));
        UNSCOPED_INFO("0x alone, and a suffix C has not got");
        CHECK(saidOn(run(std::string("#if 0x\n"
                              "#endif\n"
                              "#if 10lul\n"
                              "#endif\n"))) == std::string("0 2"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.2: an include has its own lines and name until the file it returns to", "[alpreprocessorc11]")
    {
        files["inc.lsl"] = "inc __LINE__ __FILE__\n#define FROM_INC 1\n";
        UNSCOPED_INFO("inside and after");
        CHECK(pp(std::string("one\n"
                         "#include \"inc.lsl\"\n"
                         "__LINE__ __FILE__ FROM_INC\n")) ==
                      std::string("one inc 1 \"inc.lsl\" 3 \"main.lsl\" 1"));
        files["half.lsl"] = "#define f(x) <x>\nf(\n";
        UNSCOPED_INFO("an invocation cannot run out of an include");
        CHECK(saidOn(run(std::string("#include \"half.lsl\"\n"
                              "1)\n"))) == std::string("1"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: files that end without a newline: a directive or invocation or conditional", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("a directive last");
        CHECK(pp(std::string("#define X 1\n"
                         "X\n"
                         "#undef X")) ==
                      std::string("1"));
        UNSCOPED_INFO("an invocation last");
        CHECK(pp(std::string("#define f(x) <x>\n"
                         "f(1)")) ==
                      std::string("<1>"));
        UNSCOPED_INFO("an #if left open");
        CHECK(saidOn(run(std::string("#if 1\n"
                              "x"))) == std::string("0"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 6.10.3: LSL's own words may name macros since C knows no keywords", "[alpreprocessorc11]")
    {
        UNSCOPED_INFO("words and events");
        CHECK(pp(std::string("#define default state_x\n"
                         "#define state_entry on_rez\n"
                         "#define jump return\n"
                         "default { state_entry() { jump; } }\n")) ==
                      std::string("state_x { on_rez() { return; } }"));
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: SLua: #if takes Luau's and/or/not/~= as well as C's operators", "[alpreprocessorc11]")
    {
        {
            ALPreprocessor::Options lua = options();
            lua.lua                     = true;
            const ALPreprocessor::Result r = ALPreprocessor::run("--#define A 1\n--#define B 0\n"
                                                               "--#if A and not B\none\n--#endif\n"
                                                               "--#if B or A ~= 2\ntwo\n--#endif\n"
                                                               "--#if (A && !B) == 1\nthree\n--#endif\n"
                                                               "--#if not A or B\nno\n--#else\nfour\n--#endif\n", lua);
            CHECK(messages(r) == std::string());
            CHECK(flat(r.text) == std::string("one two three four"));
        }
    }

    TEST_CASE_METHOD(alpreprocessorc11_data, "alpreprocessorc11: 5.1.1.2: lines end at CRLF and CR too; blanks after a backslash are warned of", "[alpreprocessorc11]")
    {
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
        {
            INFO("the macro's lines are its body: " + joined);
            CHECK(joined.find("PROBLEMS") == std::string::npos);
            CHECK(joined.find("llSay") != std::string::npos);
            CHECK(joined.find('\\') == std::string::npos);
            CHECK(joined.find("default") == 0);
        }
        UNSCOPED_INFO("CRLF");
        CHECK(pp(ended(lf, "\r\n")) == joined);
        UNSCOPED_INFO("a lone CR");
        CHECK(pp(ended(lf, "\r")) == joined);
        UNSCOPED_INFO("the lines after keep their numbers");
        CHECK(pp(ended("#define A 1 \\\n + 2\nA __LINE__\n", "\r\n")) == std::string("1 + 2 3"));

        {
            const ALPreprocessor::Result r = run("#define SAY(x) \\ \t\n"
                                                 "    llOwnerSay(x); \\  \n"
                                                 "    llSay(0, x)\n"
                                                 "default { state_entry() { SAY(\"hi\"); } }\n");
            UNSCOPED_INFO("blanks after a directive's backslash");
            CHECK(flat(r.text) == joined);
            UNSCOPED_INFO("said of each");
            CHECK(messages(r) ==
                          std::string("W 0: backslash and newline separated by space\nW 1: backslash and newline separated by space\n"));
        }
        {
            const ALPreprocessor::Result r = run("// a note \\ \nint shown;\n");
            UNSCOPED_INFO("a note's backslash and a blank take no line: " + r.text);
            CHECK(r.text.find("int shown;") != std::string::npos);
            CHECK(messages(r) == std::string());
        }
    }
}
