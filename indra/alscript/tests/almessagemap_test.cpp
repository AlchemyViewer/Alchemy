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

#include "../test/lltut.h"

namespace tut
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
    typedef test_group<almessagemap_data> almessagemap_group;
    typedef almessagemap_group::object    almessagemap_object;
    tut::almessagemap_group               almessagemap_instance("almessagemap");

    template<> template<>
    void almessagemap_object::test<1>()
    {
        set_test_name("the words between the literals, in the marks' order");
        std::vector<std::string> args;
        ensure("matches", ALMessageMap::match("Passing [1] as argument [2] of `[3]' which is declared as `[4] [5]'.",
                                              "Passing string as argument 2 of `llSay' which is declared as `integer channel'.", args));
        ensure_equals("the words", joined(args), std::string("<string><2><llSay><integer><channel>"));
        ensure("the first literal must begin it", !ALMessageMap::match("Too many arguments to function `[1]'.", "Warning: Too many arguments to function `f'.", args));
        ensure("the last must end it", !ALMessageMap::match("Too many arguments to function `[1]'.", "Too many arguments to function `f'. Really.", args));
        ensure("a template without marks is the message itself", ALMessageMap::match("Functions cannot change state.", "Functions cannot change state.", args));
        ensure("and no other", !ALMessageMap::match("Functions cannot change state.", "Functions cannot change state", args));
        ensure_equals("no words then", args.size(), size_t(0));
    }

    template<> template<>
    void almessagemap_object::test<2>()
    {
        set_test_name("a mark used twice stands for one word");
        std::vector<std::string> args;
        ensure("the same word twice", ALMessageMap::match("Trying to access `[1].[2]', but `[1]' is a [3]", "Trying to access `v.x', but `v' is a integer", args));
        ensure_equals("named once", joined(args), std::string("<v><x><integer>"));
        ensure("a different word is no match", !ALMessageMap::match("Trying to access `[1].[2]', but `[1]' is a [3]", "Trying to access `v.x', but `w' is a integer", args));
    }

    template<> template<>
    void almessagemap_object::test<3>()
    {
        set_test_name("a literal is found at its first occurrence after the last, which a word containing it can mislead");
        std::vector<std::string> args;
        // The engines' templates keep their words in quotes, so the
        // literal after a mark begins with the closing quote and a name
        // cannot contain it; this pins what a bare literal does.
        ensure("a name with the literal in it", ALMessageMap::match("[1] is [2] now", "x is y is z now", args));
        ensure_equals("cut at the first: the rest is the second word", joined(args), std::string("<x><y is z>"));
        ensure("quoted, a name is whole", ALMessageMap::match("Variable '[1]' is never used; prefix with '_' to silence", "Variable 'is' is never used; prefix with '_' to silence", args));
        ensure_equals("the name", joined(args), std::string("<is>"));
        // The last literal is anchored at the end, so a word that holds
        // it is not cut there.
        ensure("the end holds", ALMessageMap::match("Unknown require: [1]", "Unknown require: a: b", args));
        ensure_equals("the whole rest", joined(args), std::string("<a: b>"));
    }

    template<> template<>
    void almessagemap_object::test<4>()
    {
        set_test_name("Tailslide's messages by code, Luau's lints by name");
        ALMessageMap::Match m;
        ensure("10007", ALMessageMap::lsl(10007, "`llSay' is undeclared; did you mean llSay?", m));
        ensure_equals("its key", m.key, std::string("LSLUndeclaredWithSuggestion"));
        ensure_equals("its words", joined(m.args), std::string("<llSay><llSay>"));
        ensure("a code with another message is not taken", !ALMessageMap::lsl(10007, "`llSay' is undeclared.", m));
        ensure("a code the table lacks", !ALMessageMap::lsl(10020, "syntax error, unexpected ';'", m));
        ensure("a lint by name", ALMessageMap::luauLint("LocalUnused", "Variable 'x' is never used; prefix with '_' to silence", m));
        ensure_equals("its key", m.key, std::string("LuauLintLocalUnused"));
        ensure("the name is part of the match", !ALMessageMap::luauLint("FunctionUnused", "Variable 'x' is never used; prefix with '_' to silence", m));
        ensure("the comparison lints have a mark for each operator", ALMessageMap::luauLint("ComparisonPrecedence", "X == Y == Z is equivalent to (X == Y) == Z; add parentheses to silence", m));
        ensure_equals("four of them", m.args.size(), size_t(4));
    }

    template<> template<>
    void almessagemap_object::test<5>()
    {
        set_test_name("a lint whose template stands for clauses is matched by the shapes it takes, the fuller first");
        ALMessageMap::Match m;
        ensure("plain", ALMessageMap::luauLint("DeprecatedApi", "Member 'table.getn' is deprecated", m));
        ensure_equals("plain key", m.key, std::string("LuauLintDeprecatedMember"));
        ensure_equals("the qualified name whole", joined(m.args), std::string("<table.getn>"));
        ensure("with a replacement", ALMessageMap::luauLint("DeprecatedApi", "Member 'table.getn' is deprecated, use '#' instead", m));
        ensure_equals("its key", m.key, std::string("LuauLintDeprecatedMemberUse"));
        ensure_equals("its words", joined(m.args), std::string("<table.getn><#>"));
        ensure("with a replacement and a reason", ALMessageMap::luauLint("DeprecatedApi", "Member 'a.b' is deprecated, use 'c' instead. It was slow", m));
        ensure_equals("the fuller key", m.key, std::string("LuauLintDeprecatedMemberUseReason"));
        ensure_equals("three words", joined(m.args), std::string("<a.b><c><It was slow>"));
        ensure("a function", ALMessageMap::luauLint("DeprecatedApi", "Function 'f' is deprecated. Gone", m));
        ensure_equals("its key", m.key, std::string("LuauLintDeprecatedFunctionReason"));
    }

    template<> template<>
    void almessagemap_object::test<6>()
    {
        set_test_name("Luau's type errors by shape: the fuller shape before the one it begins with");
        ALMessageMap::Match m;
        ensure("a mismatch", ALMessageMap::luauError("Expected this to be 'number', but got 'string'", m));
        ensure_equals("its key", m.key, std::string("LuauTypeMismatch"));
        ensure_equals("wanted then given, quotes and all", joined(m.args), std::string("<'number'><'string'>"));
        ensure("with a reason", ALMessageMap::luauError("Expected this to be 'number', but got 'string'; a table is not a number", m));
        ensure_equals("the reason's key", m.key, std::string("LuauTypeMismatchReason"));
        ensure_equals("three words", joined(m.args), std::string("<'number'><'string'><a table is not a number>"));
        ensure("exactly", ALMessageMap::luauError("Expected this to be exactly 'number', but got 'string'", m));
        ensure_equals("not the plain one with 'exactly' in the word", m.key, std::string("LuauTypeMismatchExactly"));
        ensure("unreachable", ALMessageMap::luauError("Expected this to be unreachable, but got 'string'", m));
        ensure_equals("its key", m.key, std::string("LuauTypeMismatchUnreachable"));
        ensure("a count", ALMessageMap::luauError("Argument count mismatch. Function 'll.Say' expects 2 arguments, but only 1 is specified", m));
        ensure_equals("only one", m.key, std::string("LuauArgumentCountOnlyOne"));
        ensure_equals("the name and the count", joined(m.args), std::string("<ll.Say><2>"));
        ensure("at least", ALMessageMap::luauError("Argument count mismatch. Function 'f' expects at least 2 arguments, but only 1 is specified", m));
        ensure_equals("not the plain one with 'at least' in the count", m.key, std::string("LuauArgumentCountAtLeastOnlyOne"));
        ensure("a range", ALMessageMap::luauError("Argument count mismatch. Function 'f' expects 1 to 2 arguments, but none are specified", m));
        ensure_equals("range, none", m.key, std::string("LuauArgumentCountRangeNone"));
        ensure("one wanted", ALMessageMap::luauError("Argument count mismatch. Function 'f' expects 1 argument, but 3 are specified", m));
        ensure_equals("its key", m.key, std::string("LuauArgumentCountOne"));
        ensure("a key not found", ALMessageMap::luauError("Key 'b' not found in table '{ a: number }'.  Did you mean 'a'?", m));
        ensure_equals("did you mean", m.key, std::string("LuauMissingPropertyDidYouMean"));
        ensure_equals("its words", joined(m.args), std::string("<b><{ a: number }><a>"));
        ensure("nothing the table knows", !ALMessageMap::luauError("Some words nobody wrote", m));
        // The new solver's nonstrict mode.
        ensure("a checked call", ALMessageMap::luauError("the function 'string.len' expects to get a string as its 1st argument, but is being given a number", m));
        ensure_equals("its key", m.key, std::string("LuauCheckedCall"));
        ensure_equals("and words", joined(m.args), std::string("<string.len><string><1st><number>"));
        ensure("an argument that fails, in a function",
               ALMessageMap::luauError("in the function 'f', 'the argument 'x' is used in a way that will error at runtime", m));
        ensure_equals("in a function", m.key + joined(m.args), std::string("LuauFailsAtRuntimeIn<f><x>"));
        ensure("and in none", ALMessageMap::luauError("the argument 'x' is used in a way that will error at runtime", m));
        ensure_equals("in none", m.key + joined(m.args), std::string("LuauFailsAtRuntime<x>"));
    }
    template<> template<>
    void almessagemap_object::test<7>()
    {
        set_test_name("a template without marks is its message whole; a bracket that is no mark is part of a literal; and a word is put in as it is");
        std::vector<std::string> args;
        ensure("not the start of a longer one", !ALMessageMap::match("Functions cannot change state.", "Functions cannot change state. And more.", args));
        ensure("a bracket before a mark stays in the literal", ALMessageMap::match("Index [a] of [1] is out", "Index [a] of list is out", args));
        ensure_equals("and the word is found after it", joined(args), std::string("<list>"));
        ensure("the literal still has to begin the message", !ALMessageMap::match("Index [a] of [1] is out", "a] of list is out", args));
        ensure("a bracket after the last mark too", ALMessageMap::match("[1] is [10] long", "list is [10] long", args) && joined(args) == "<list>");
        ensure_equals("a word with a mark in it is not filled again", ALScriptProblem::fill("[1] and [2]", { "a[2]", "b" }), std::string("a[2] and b"));
        ensure_equals("a mark with no word stays", ALScriptProblem::fill("[1] and [3]", { "a" }), std::string("a and [3]"));
        ensure_equals("each mark as often as it is used", ALScriptProblem::fill("[1][1]", { "x" }), std::string("xx"));
    }
}
