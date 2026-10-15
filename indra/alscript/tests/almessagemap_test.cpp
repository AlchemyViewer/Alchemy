/**
 * @file tests/almessagemap_test.cpp
 * @brief An engine's message taken apart again.
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

#include "../core/almessagemap.h"
#include "../core/alscriptproblem.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct almessagemap_data
    {
        static std::string joined(const std::vector<std::string>& words)
        {
            std::string out;
            for (const std::string& word : words)
            {
                out += "<" + word + ">";
            }
            return out;
        }
    };

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: match gives the words between the literals in the marks' order", "[almessagemap]")
    {
        std::vector<std::string> args;
        REQUIRE(ALMessageMap::match("Passing [1] as argument [2] of `[3]' which is declared as `[4] [5]'.",
                                              "Passing string as argument 2 of `llSay' which is declared as `integer channel'.", args));
        CHECK(joined(args) == std::string("<string><2><llSay><integer><channel>"));
        UNSCOPED_INFO("the first literal must begin it");
        CHECK_FALSE(ALMessageMap::match("Too many arguments to function `[1]'.", "Warning: Too many arguments to function `f'.", args));
        UNSCOPED_INFO("the last must end it");
        CHECK_FALSE(ALMessageMap::match("Too many arguments to function `[1]'.", "Too many arguments to function `f'. Really.", args));
        UNSCOPED_INFO("a template without marks is the message itself");
        CHECK(ALMessageMap::match("Functions cannot change state.", "Functions cannot change state.", args));
        UNSCOPED_INFO("and no other");
        CHECK_FALSE(ALMessageMap::match("Functions cannot change state.", "Functions cannot change state", args));
        UNSCOPED_INFO("no words then");
        REQUIRE(args.size() == size_t(0));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: a mark used twice stands for one word", "[almessagemap]")
    {
        std::vector<std::string> args;
        UNSCOPED_INFO("the same word twice");
        REQUIRE(ALMessageMap::match("Trying to access `[1].[2]', but `[1]' is a [3]", "Trying to access `v.x', but `v' is a integer", args));
        UNSCOPED_INFO("named once");
        CHECK(joined(args) == std::string("<v><x><integer>"));
        UNSCOPED_INFO("a different word is no match");
        CHECK_FALSE(ALMessageMap::match("Trying to access `[1].[2]', but `[1]' is a [3]", "Trying to access `v.x', but `w' is a integer", args));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: a literal is found at its first occurrence so a word holding it misleads", "[almessagemap]")
    {
        std::vector<std::string> args;
        // The engines' templates keep their words in quotes, so the
        // literal after a mark begins with the closing quote and a name
        // cannot contain it; this pins what a bare literal does.
        UNSCOPED_INFO("a name with the literal in it");
        REQUIRE(ALMessageMap::match("[1] is [2] now", "x is y is z now", args));
        UNSCOPED_INFO("cut at the first: the rest is the second word");
        CHECK(joined(args) == std::string("<x><y is z>"));
        UNSCOPED_INFO("quoted, a name is whole");
        CHECK(ALMessageMap::match("Variable '[1]' is never used; prefix with '_' to silence", "Variable 'is' is never used; prefix with '_' to silence", args));
        CHECK(joined(args) == std::string("<is>"));
        // The last literal is anchored at the end, so a word that holds
        // it is not cut there.
        UNSCOPED_INFO("the end holds");
        CHECK(ALMessageMap::match("Unknown require: [1]", "Unknown require: a: b", args));
        UNSCOPED_INFO("the whole rest");
        CHECK(joined(args) == std::string("<a: b>"));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: Tailslide's messages are keyed by code and Luau's lints by name", "[almessagemap]")
    {
        ALMessageMap::Match m;
        REQUIRE(ALMessageMap::lsl(10007, "`llSay' is undeclared; did you mean llSay?", m));
        CHECK(m.key == std::string("LSLUndeclaredWithSuggestion"));
        CHECK(joined(m.args) == std::string("<llSay><llSay>"));
        UNSCOPED_INFO("a code with another message is not taken");
        CHECK_FALSE(ALMessageMap::lsl(10007, "`llSay' is undeclared.", m));
        UNSCOPED_INFO("a code the table lacks");
        CHECK_FALSE(ALMessageMap::lsl(10020, "syntax error, unexpected ';'", m));
        UNSCOPED_INFO("a lint by name");
        CHECK(ALMessageMap::luauLint("LocalUnused", "Variable 'x' is never used; prefix with '_' to silence", m));
        CHECK(m.key == std::string("LuauLintLocalUnused"));
        UNSCOPED_INFO("the name is part of the match");
        CHECK_FALSE(ALMessageMap::luauLint("FunctionUnused", "Variable 'x' is never used; prefix with '_' to silence", m));
        UNSCOPED_INFO("the comparison lints have a mark for each operator");
        CHECK(ALMessageMap::luauLint("ComparisonPrecedence", "X == Y == Z is equivalent to (X == Y) == Z; add parentheses to silence", m));
        REQUIRE(m.args.size() == size_t(4));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: a lint whose template has clauses matches its shapes fuller first", "[almessagemap]")
    {
        ALMessageMap::Match m;
        UNSCOPED_INFO("plain");
        REQUIRE(ALMessageMap::luauLint("DeprecatedApi", "Member 'table.getn' is deprecated", m));
        CHECK(m.key == std::string("LuauLintDeprecatedMember"));
        UNSCOPED_INFO("the qualified name whole");
        CHECK(joined(m.args) == std::string("<table.getn>"));
        UNSCOPED_INFO("with a replacement");
        CHECK(ALMessageMap::luauLint("DeprecatedApi", "Member 'table.getn' is deprecated, use '#' instead", m));
        CHECK(m.key == std::string("LuauLintDeprecatedMemberUse"));
        CHECK(joined(m.args) == std::string("<table.getn><#>"));
        UNSCOPED_INFO("with a replacement and a reason");
        CHECK(ALMessageMap::luauLint("DeprecatedApi", "Member 'a.b' is deprecated, use 'c' instead. It was slow", m));
        UNSCOPED_INFO("the fuller key");
        CHECK(m.key == std::string("LuauLintDeprecatedMemberUseReason"));
        CHECK(joined(m.args) == std::string("<a.b><c><It was slow>"));
        UNSCOPED_INFO("a function");
        CHECK(ALMessageMap::luauLint("DeprecatedApi", "Function 'f' is deprecated. Gone", m));
        CHECK(m.key == std::string("LuauLintDeprecatedFunctionReason"));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: Luau's type errors by shape: the fuller shape before the one it begins with", "[almessagemap]")
    {
        ALMessageMap::Match m;
        UNSCOPED_INFO("a mismatch");
        REQUIRE(ALMessageMap::luauError("Expected this to be 'number', but got 'string'", m));
        CHECK(m.key == std::string("LuauTypeMismatch"));
        UNSCOPED_INFO("wanted then given, quotes and all");
        CHECK(joined(m.args) == std::string("<'number'><'string'>"));
        UNSCOPED_INFO("with a reason");
        CHECK(ALMessageMap::luauError("Expected this to be 'number', but got 'string'; a table is not a number", m));
        CHECK(m.key == std::string("LuauTypeMismatchReason"));
        CHECK(joined(m.args) == std::string("<'number'><'string'><a table is not a number>"));
        UNSCOPED_INFO("exactly");
        CHECK(ALMessageMap::luauError("Expected this to be exactly 'number', but got 'string'", m));
        UNSCOPED_INFO("not the plain one with 'exactly' in the word");
        CHECK(m.key == std::string("LuauTypeMismatchExactly"));
        UNSCOPED_INFO("unreachable");
        CHECK(ALMessageMap::luauError("Expected this to be unreachable, but got 'string'", m));
        CHECK(m.key == std::string("LuauTypeMismatchUnreachable"));
        UNSCOPED_INFO("a count");
        CHECK(ALMessageMap::luauError("Argument count mismatch. Function 'll.Say' expects 2 arguments, but only 1 is specified", m));
        CHECK(m.key == std::string("LuauArgumentCountOnlyOne"));
        UNSCOPED_INFO("the name and the count");
        CHECK(joined(m.args) == std::string("<ll.Say><2>"));
        UNSCOPED_INFO("at least");
        CHECK(ALMessageMap::luauError("Argument count mismatch. Function 'f' expects at least 2 arguments, but only 1 is specified", m));
        UNSCOPED_INFO("not the plain one with 'at least' in the count");
        CHECK(m.key == std::string("LuauArgumentCountAtLeastOnlyOne"));
        UNSCOPED_INFO("a range");
        CHECK(ALMessageMap::luauError("Argument count mismatch. Function 'f' expects 1 to 2 arguments, but none are specified", m));
        CHECK(m.key == std::string("LuauArgumentCountRangeNone"));
        UNSCOPED_INFO("one wanted");
        CHECK(ALMessageMap::luauError("Argument count mismatch. Function 'f' expects 1 argument, but 3 are specified", m));
        CHECK(m.key == std::string("LuauArgumentCountOne"));
        UNSCOPED_INFO("a key not found");
        CHECK(ALMessageMap::luauError("Key 'b' not found in table '{ a: number }'.  Did you mean 'a'?", m));
        CHECK(m.key == std::string("LuauMissingPropertyDidYouMean"));
        CHECK(joined(m.args) == std::string("<b><{ a: number }><a>"));
        UNSCOPED_INFO("nothing the table knows");
        CHECK_FALSE(ALMessageMap::luauError("Some words nobody wrote", m));
        // The new solver's nonstrict mode.
        UNSCOPED_INFO("a checked call");
        CHECK(ALMessageMap::luauError("the function 'string.len' expects to get a string as its 1st argument, but is being given a number", m));
        CHECK(m.key == std::string("LuauCheckedCall"));
        CHECK(joined(m.args) == std::string("<string.len><string><1st><number>"));
        UNSCOPED_INFO("an argument that fails, in a function");
        CHECK(ALMessageMap::luauError("in the function 'f', 'the argument 'x' is used in a way that will error at runtime", m));
        CHECK(m.key + joined(m.args) == std::string("LuauFailsAtRuntimeIn<f><x>"));
        UNSCOPED_INFO("and in none");
        CHECK(ALMessageMap::luauError("the argument 'x' is used in a way that will error at runtime", m));
        CHECK(m.key + joined(m.args) == std::string("LuauFailsAtRuntime<x>"));
    }
    TEST_CASE_METHOD(almessagemap_data, "almessagemap: unmarked templates and brackets are literal and fill puts words in as is", "[almessagemap]")
    {
        std::vector<std::string> args;
        UNSCOPED_INFO("not the start of a longer one");
        REQUIRE_FALSE(ALMessageMap::match("Functions cannot change state.", "Functions cannot change state. And more.", args));
        UNSCOPED_INFO("a bracket before a mark stays in the literal");
        CHECK(ALMessageMap::match("Index [a] of [1] is out", "Index [a] of list is out", args));
        UNSCOPED_INFO("and the word is found after it");
        CHECK(joined(args) == std::string("<list>"));
        UNSCOPED_INFO("the literal still has to begin the message");
        CHECK_FALSE(ALMessageMap::match("Index [a] of [1] is out", "a] of list is out", args));
        {
            INFO("a bracket after the last mark too");
            CHECK(ALMessageMap::match("[1] is [10] long", "list is [10] long", args));
            CHECK(joined(args) == "<list>");
        }
        UNSCOPED_INFO("a word with a mark in it is not filled again");
        CHECK(ALScriptProblem::fill("[1] and [2]", { "a[2]", "b" }) == std::string("a[2] and b"));
        UNSCOPED_INFO("a mark with no word stays");
        CHECK(ALScriptProblem::fill("[1] and [3]", { "a" }) == std::string("a and [3]"));
        UNSCOPED_INFO("each mark as often as it is used");
        CHECK(ALScriptProblem::fill("[1][1]", { "x" }) == std::string("xx"));
    }

    TEST_CASE_METHOD(almessagemap_data, "almessagemap: why a SLua require found nothing is keyed with the module name left blank", "[almessagemap]")
    {
        {
            INFO("a child");
            ALMessageMap::Match m;
            REQUIRE(ALMessageMap::luauRequire("could not resolve child component \"util\"", m));
            CHECK(m.key == "PreprocRequireNoChild");
            CHECK(m.args == std::vector<std::string>({ "", "util" }));
        }
        {
            INFO("ambiguous, the fuller shape");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("could not resolve child component \"util\" (ambiguous)", m));
            CHECK(m.key == "PreprocRequireNoChildAmbiguous");
        }
        {
            INFO("an alias");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("@lib is not a valid alias", m));
            CHECK(m.key == "PreprocRequireNotAlias");
            CHECK(m.args == std::vector<std::string>({ "", "lib" }));
        }
        {
            INFO("a cycle");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("detected alias cycle (@a -> @b -> @a)", m));
            CHECK(m.key == "PreprocRequireAliasCycle");
            CHECK(m.args == std::vector<std::string>({ "", "@a -> @b -> @a" }));
        }
        {
            INFO("the studio's own");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("the alias '@sl-std' is reserved: aliases starting @sl- are Second Life's", m));
            CHECK(m.key == "PreprocRequireReserved");
            CHECK(m.args == std::vector<std::string>({ "", "sl-std" }));
        }
        {
            INFO("a folder with both configurations");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("could not resolve alias \"lib\" (ambiguous configuration file)", m));
            CHECK(m.key == "PreprocRequireConfigAmbiguous");
            CHECK(m.args == std::vector<std::string>({ "", "lib" }));
        }
        {
            INFO("a file nothing lets in");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("'C:/lib/x.luau' is there, but no include folder or alias lets a require read it", m));
            CHECK(m.key == "PreprocRequireNotAdmitted");
            CHECK(m.args == std::vector<std::string>({ "", "C:/lib/x.luau" }));
        }
        {
            INFO("an alias past its configuration's folder");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("the configuration in 'C:/lib' names 'D:/out', which is outside it and outside the include folders", m));
            CHECK(m.key == "PreprocRequireAliasRefused");
            CHECK(m.args == std::vector<std::string>({ "", "C:/lib", "D:/out" }));
        }
        {
            INFO("a configuration that could not be read");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("the configuration 'C:/lib/.luaurc' could not be read: expected value", m));
            CHECK(m.key == "PreprocRequireConfigBroken");
            CHECK(m.args == std::vector<std::string>({ "", "C:/lib/.luaurc", "expected value" }));
        }
        {
            INFO("a world alias naming the disk");
            ALMessageMap::Match m;
            CHECK(ALMessageMap::luauRequire("the alias stands for '/x', a path on disk, which a configuration in the world may not name", m));
            CHECK(m.key == "PreprocRequireAliasNamesDisk");
            CHECK(m.args == std::vector<std::string>({ "", "/x" }));
        }
        ALMessageMap::Match m;
        UNSCOPED_INFO("a reason of no known shape");
        CHECK_FALSE(ALMessageMap::luauRequire("something else went wrong", m));
        {
            INFO("its keys known as such");
            CHECK(ALMessageMap::requireReason("PreprocRequireNoChild"));
            CHECK_FALSE(ALMessageMap::requireReason("PreprocRequireTooDeep"));
        }
    }
}
