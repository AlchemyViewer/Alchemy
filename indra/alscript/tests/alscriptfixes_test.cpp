/**
 * @file tests/alscriptfixes_test.cpp
 * @brief The fixes made over real checks, applied, and the text checked again.
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

#include "../lint/alscriptfixes.h"
#include "../core/alscriptweight.h"

#include "../lsl/allslexports.h"
#include "../luau/alluauexports.h"

#include "../lsl/allslservice.h"
#include "../luau/alluauservice.h"
#include "../preprocessor/alpreprocessor.h"
#include "../lsl/optimizer/allsloptimizer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    struct alscriptfixes_data
    {
        ALLSLService lsl;
        ALLuauService luau;
        // Each load's own: one shared would say the last load's error under
        // another's name.
        std::string  builtinsError;
        std::string  definitionsError;
        bool         lslLoaded  = false;
        bool         luauLoaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool   newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alscriptfixes_data()
        {
            lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", builtinsError);
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            luau.setNewSolver(newSolver, definitionsError);
            luauLoaded = luau.loadDefinitions(text.str(), definitionsError);
        }

        ALScriptProblems check(const std::string& script, bool lua)
        {
            return lua ? luau.check(script) : lsl.check(script, true);
        }

        static std::string said(const ALScriptProblems& problems)
        {
            std::string out;
            for (const ALScriptProblem& problem : problems)
            {
                out += llformat("[%d:%d-%d:%d] %s %s %s (%d fixes)\n", problem.line, problem.column, problem.endLine, problem.endColumn,
                                problem.code.c_str(), problem.key.c_str(), problem.message.c_str(), static_cast<int>(problem.fixes.size()));
            }
            return out;
        }

        static const ALScriptProblem* keyed(const ALScriptProblems& problems, const std::string& key)
        {
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.key == key)
                {
                    return &problem;
                }
            }
            return nullptr;
        }

        static size_t errors(const ALScriptProblems& problems)
        {
            size_t count = 0;
            for (const ALScriptProblem& problem : problems)
            {
                count += problem.severity == ALScriptProblem::Severity::Error;
            }
            return count;
        }

        // The problem keyed so, its preferred fix, the text with it made,
        // and that text checked again: the problem gone, and no error in
        // its place.
        std::string require_fixed(const std::string& script, bool lua, const std::string& key, const std::string& title)
        {
            const ALScriptProblems problems = check(script, lua);
            const ALScriptProblem* problem  = keyed(problems, key);
            CAPTURE(key);
            INFO("said:\n" + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE_FALSE(problem->fixes.empty());
            const ALScriptFix& fix = problem->fixes.front();
            REQUIRE(fix.preferred);
            REQUIRE(fix.title == title);
            const std::optional<std::string> made = ALScriptFixes::apply(script, fix);
            REQUIRE(made.has_value());
            const ALScriptProblems after = check(*made, lua);
            INFO("made:\n" + *made + "\nsaid then:\n" + said(after));
            REQUIRE(keyed(after, key) == nullptr);
            UNSCOPED_INFO("no error in its place");
            REQUIRE(errors(after) <= errors(problems) - (problem->severity == ALScriptProblem::Severity::Error ? 1 : 0));
            return *made;
        }

        // Where a piece of the script is, zero-based: its first line and
        // column, and where it ends on that line.
        static void require_place(const std::string& script, const std::string& piece, S32& line, S32& column, S32& end)
        {
            const size_t at = script.find(piece);
            CAPTURE(piece);
            REQUIRE(at != std::string::npos);
            const size_t start = script.rfind('\n', at);
            line               = static_cast<S32>(std::count(script.begin(), script.begin() + at, '\n'));
            column             = static_cast<S32>(at - (start == std::string::npos ? 0 : start + 1));
            end                = column + static_cast<S32>(piece.size());
        }

        std::vector<ALScriptFix> actions(const std::string& script, bool lua, S32 line, S32 column, S32 endLine, S32 endColumn)
        {
            return lua ? luau.actions(script, line, column, endLine, endColumn) : lsl.actions(script, line, column, endLine, endColumn);
        }

        static std::string titles(const std::vector<ALScriptFix>& offered)
        {
            std::string out;
            for (const ALScriptFix& action : offered)
            {
                out += "'" + action.title + "' ";
            }
            return out;
        }

        // The text with the refactor titled so made, among those offered at
        // a place, and that text checked again: no error in it. Where none
        // is titled so, what was offered.
        std::string require_acted(const std::string& script, bool lua, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& title)
        {
            const std::vector<ALScriptFix> offered = actions(script, lua, line, column, endLine, endColumn);
            for (const ALScriptFix& action : offered)
            {
                if (action.title != title)
                {
                    continue;
                }
                CAPTURE(title);
                REQUIRE(action.kind == ALScriptFix::Kind::Refactor);
                const std::optional<std::string> made = ALScriptFixes::apply(script, action);
                REQUIRE(made.has_value());
                const ALScriptProblems after = check(*made, lua);
                INFO("made:\n" + *made + "\nsaid then:\n" + said(after));
                REQUIRE(errors(after) == 0);
                return *made;
            }
            return "offered: " + titles(offered);
        }

        // The same with the caret, or the stretch chosen, on a piece of the
        // script.
        std::string require_acted_on(const std::string& script, bool lua, const std::string& piece, bool chosen, const std::string& title)
        {
            S32 line = 0, column = 0, end = 0;
            require_place(script, piece, line, column, end);
            return require_acted(script, lua, line, column, line, chosen ? end : column, title);
        }
    };

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a missing semicolon is put where it is missing", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string made = require_fixed("default\n{\n    state_entry()\n    {\n        integer a = 1\n        llOwnerSay((string)a);\n    }\n}\n", false,
                                       "LSLSyntaxMissing", "Insert ';'");
        UNSCOPED_INFO("after the 1: " + made);
        REQUIRE(made.find("integer a = 1;\n") != std::string::npos);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL name spelt wrong becomes the nearest in scope or builtin", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        std::string made = require_fixed("default\n{\n    state_entry()\n    {\n        llownersay(\"hi\");\n    }\n}\n", false, "LSLUndeclared",
                                 "Change 'llownersay' to 'llOwnerSay'");
        UNSCOPED_INFO("the call: " + made);
        REQUIRE(made.find("llOwnerSay(\"hi\")") != std::string::npos);
        made = require_fixed("default\n{\n    state_entry()\n    {\n        integer count = 1;\n        llOwnerSay((string)coutn);\n    }\n}\n", false,
                     "LSLUndeclared", "Change 'coutn' to 'count'");
        UNSCOPED_INFO("the local: " + made);
        REQUIRE(made.find("(string)count)") != std::string::npos);
        // A local declared after the place is not in scope there, and not
        // offered.
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        llOwnerSay((string)coutn);\n        integer count = 1;\n    }\n}\n", false);
        const ALScriptProblem* later = keyed(problems, "LSLUndeclared");
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(later != nullptr);
        UNSCOPED_INFO("not a name declared after: " + titles(later->fixes));
        CHECK(std::none_of(later->fixes.begin(), later->fixes.end(), [](const ALScriptFix& fix) { return fix.args.size() > 1 && fix.args[1] == "count"; }));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a deprecated LSL function is replaced only where it has one replacement", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string made = require_fixed("default\n{\n    state_entry()\n    {\n        llSoundPreload(\"boom\");\n    }\n}\n", false,
                                       "LSLDeprecatedWithReplacement", "Use 'llPreloadSound' instead of 'llSoundPreload'");
        UNSCOPED_INFO("the call: " + made);
        REQUIRE(made.find("llPreloadSound(\"boom\");") != std::string::npos);

        const ALScriptProblems problems = check("default\n{\n    state_entry()\n    {\n        llSound(\"boom\", 1.0, TRUE, FALSE);\n    }\n}\n", false);
        const ALScriptProblem* several  = keyed(problems, "LSLDeprecatedWithReplacement");
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(several != nullptr);
        UNSCOPED_INFO("no fix among several: " + said(problems));
        REQUIRE(several->fixes.empty());
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an unused SLua local is marked unused safely", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const std::string      made     = require_fixed("local unused = 1\n", true, "LuauLintLocalUnused", "Rename to '_unused'");
        CHECK(made == std::string("local _unused = 1\n"));
        const ALScriptProblems problems = check("local function helper() end\n", true);
        const ALScriptProblem* problem  = keyed(problems, "LuauLintFunctionUnused");
        {
            INFO("said: " + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE_FALSE(problem->fixes.empty());
        }
        CHECK(problem->fixes.front().safe);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an SLua key spelt wrong is changed to the nearest there is", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        if (newSolver)
        {
            // Whose nonstrict mode says nothing of a key not there.
            ALLuauConfig config;
            config.mode = "strict";
            luau.setConfig(config);
        }
        const std::string made = require_fixed("ll.OwnerSya(\"hi\")\n", true, "LuauKeyNotFoundDidYouMean", "Change 'OwnerSya' to 'OwnerSay'");
        CHECK(made == std::string("ll.OwnerSay(\"hi\")\n"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a deprecated SLua call is replaced as the definitions say; never safe", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const ALScriptProblems problems = check("local n = ll.Abs(-1)\nprint(n)\n", true);
        const ALScriptProblem* problem  = nullptr;
        for (const ALScriptProblem& each : problems)
        {
            if (each.code == "DeprecatedApi")
            {
                problem = &each;
            }
        }
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("a fix: " + said(problems));
        REQUIRE_FALSE(problem->fixes.empty());
        CHECK_FALSE(problem->fixes.front().safe);
        const std::optional<std::string> made = ALScriptFixes::apply("local n = ll.Abs(-1)\nprint(n)\n", problem->fixes.front());
        REQUIRE(made.has_value());
        const ALScriptProblems after = check(*made, true);
        INFO("made:\n" + *made + "\nsaid then:\n" + said(after));
        for (const ALScriptProblem& each : after)
        {
            REQUIRE(each.code != "DeprecatedApi");
        }
        CHECK(errors(after) == static_cast<size_t>(0));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: edits are made in the text's order and overlapping ones are refused", "[ALScriptFixes]")
    {
        ALScriptFix fix;
        fix.edits.push_back({ 1, 0, 1, 3, "BAR" });
        fix.edits.push_back({ 0, 0, 0, 0, ">" });
        CHECK(ALScriptFixes::apply("foo\nbar\n", fix).value_or("refused") == std::string(">foo\nBAR\n"));

        fix.edits.push_back({ 1, 2, 1, 3, "x" });
        UNSCOPED_INFO("overlap refused");
        REQUIRE_FALSE(ALScriptFixes::apply("foo\nbar\n", fix).has_value());

        ALScriptFix past;
        past.edits.push_back({ 0, 4, 0, 4, "!" });
        UNSCOPED_INFO("past the line's end refused");
        REQUIRE_FALSE(ALScriptFixes::apply("foo\nbar\n", past).has_value());
        ALScriptFix at_end;
        at_end.edits.push_back({ 0, 3, 0, 3, ";" });
        UNSCOPED_INFO("at the line's end");
        CHECK(ALScriptFixes::apply("foo\nbar\n", at_end).value_or("refused") == std::string("foo;\nbar\n"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: NOLINT and NOLINTNEXTLINE suppress named or all lints; never a type error", "[ALScriptFixes]")
    {
        ALScriptProblem lint;
        lint.severity = ALScriptProblem::Severity::Warning;
        lint.source   = ALScriptProblem::Source::Lint;
        lint.code     = "LocalUnused";
        UNSCOPED_INFO("named");
        CHECK(ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(LocalUnused)", "", true));
        UNSCOPED_INFO("among others");
        CHECK(ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(ShadowLocal, LocalUnused)", "", true));
        UNSCOPED_INFO("bare");
        CHECK(ALScriptFixes::suppressed(lint, "local x = 1 --[[ NOLINT ]]", "", true));
        UNSCOPED_INFO("another name");
        CHECK_FALSE(ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(ShadowLocal)", "", true));
        UNSCOPED_INFO("the line before");
        CHECK(ALScriptFixes::suppressed(lint, "local x = 1", "-- NOLINTNEXTLINE(LocalUnused)", true));
        UNSCOPED_INFO("NOLINT on the line before is not about this one");
        CHECK_FALSE(ALScriptFixes::suppressed(lint, "local x = 1", "-- NOLINT(LocalUnused)", true));
        UNSCOPED_INFO("not in a string");
        CHECK_FALSE(ALScriptFixes::suppressed(lint, "local x = \"-- NOLINT\"", "", true));
        UNSCOPED_INFO("not in a long string");
        CHECK_FALSE(ALScriptFixes::suppressed(lint, "local x = [[ -- NOLINT ]]", "", true));
        UNSCOPED_INFO("not a longer word");
        CHECK_FALSE(ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINTING", "", true));
        lint.severity = ALScriptProblem::Severity::Error;
        UNSCOPED_INFO("a lint made an error, still");
        CHECK(ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT", "", true));

        ALScriptProblem type;
        type.severity = ALScriptProblem::Severity::Error;
        type.source   = ALScriptProblem::Source::Types;
        UNSCOPED_INFO("a type error, never");
        CHECK_FALSE(ALScriptFixes::suppressed(type, "local x: number = \"a\" -- NOLINT", "", true));

        ALScriptProblem lsl;
        lsl.severity = ALScriptProblem::Severity::Warning;
        lsl.source   = ALScriptProblem::Source::Lint;
        lsl.code     = "20009";
        lsl.key      = "LSLDeclaredButNotUsed";
        UNSCOPED_INFO("its name");
        CHECK(ALScriptFixes::lintName(lsl, false) == std::string("DeclaredButNotUsed"));
        UNSCOPED_INFO("by name");
        CHECK(ALScriptFixes::suppressed(lsl, "integer x; // NOLINT(DeclaredButNotUsed)", "", false));
        UNSCOPED_INFO("by number");
        CHECK(ALScriptFixes::suppressed(lsl, "integer x; /* NOLINT(20009) */", "", false));
        UNSCOPED_INFO("not in a string");
        CHECK_FALSE(ALScriptFixes::suppressed(lsl, "string s = \"// NOLINT\";", "", false));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a suppression joins a NOLINT or the line's comment or makes its own", "[ALScriptFixes]")
    {
        ALScriptProblem lint;
        lint.severity = ALScriptProblem::Severity::Warning;
        lint.source   = ALScriptProblem::Source::Lint;
        lint.code     = "LocalUnused";
        auto require_suppression = [&lint](const std::string& line, bool lua) {
            const std::optional<ALScriptFix> fix = ALScriptFixes::suppression(lint, line, lua);
            CAPTURE(line);
            REQUIRE(fix.has_value());
            REQUIRE(fix->kind == ALScriptFix::Kind::Suppress);
            REQUIRE_FALSE(fix->preferred);
            REQUIRE(fix->safe);
            const std::string after = ALScriptFixes::apply(line, *fix).value_or("refused");
            UNSCOPED_INFO("suppresses: " + after);
            REQUIRE(ALScriptFixes::suppressed(lint, after, "", lua));
            return after;
        };
        {
            INFO("its own");
            CHECK(require_suppression("local x = 1", true) == std::string("local x = 1  -- NOLINT(LocalUnused)"));
        }
        {
            INFO("the line's comment");
            CHECK(require_suppression("local x = 1 -- why", true) == std::string("local x = 1 -- why NOLINT(LocalUnused)"));
        }
        {
            INFO("a NOLINT's list");
            CHECK(require_suppression("local x = 1 -- NOLINT(ShadowLocal)", true) == std::string("local x = 1 -- NOLINT(ShadowLocal, LocalUnused)"));
        }
        {
            INFO("after a block comment");
            CHECK(require_suppression("local x = 1 --[[ why ]]", true) == std::string("local x = 1 --[[ why ]]  -- NOLINT(LocalUnused)"));
        }
        UNSCOPED_INFO("nothing where a bare NOLINT says it");
        REQUIRE_FALSE(ALScriptFixes::suppression(lint, "local x = 1 -- NOLINT", true).has_value());

        lint.code = "20009";
        lint.key  = "LSLDeclaredButNotUsed";
        {
            INFO("LSL");
            CHECK(require_suppression("    integer x;", false) == std::string("    integer x;  // NOLINT(DeclaredButNotUsed)"));
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an SLua global spelt wrong is changed to the nearest name in scope", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const ALScriptProblems problems = check("pirnt(\"hi\")\n", true);
        const ALScriptProblem* problem  = nullptr;
        for (const ALScriptProblem& each : problems)
        {
            if (each.key.find("UnknownGlobal") != std::string::npos)
            {
                problem = &each;
            }
        }
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        const std::string made = require_fixed("pirnt(\"hi\")\n", true, problem->key, "Change 'pirnt' to 'print'");
        CHECK(made == std::string("print(\"hi\")\n"));
        const std::string local = require_fixed("local count = 1\nprint(coutn)\n", true, problem->key, "Change 'coutn' to 'count'");
        CHECK(local == std::string("local count = 1\nprint(count)\n"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL value of the wrong type is cast where LSL has the cast", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        std::string made = require_fixed("default\n{\n    state_entry()\n    {\n        llOwnerSay(5 + 1);\n    }\n}\n", false, "LSLArgumentWrongType",
                                 "Cast to string");
        UNSCOPED_INFO("the argument: " + made);
        REQUIRE(made.find("llOwnerSay((string)(5 + 1));") != std::string::npos);
        made = require_fixed("default\n{\n    state_entry()\n    {\n        llSetText(\"a\", <1, 1, 1>, \"1\");\n    }\n}\n", false, "LSLArgumentWrongType",
                     "Cast to float");
        UNSCOPED_INFO("the third, past a vector's commas: " + made);
        REQUIRE(made.find("<1, 1, 1>, (float)\"1\")") != std::string::npos);
        made = require_fixed("default\n{\n    state_entry()\n    {\n        string s = 5;\n        llOwnerSay(s);\n    }\n}\n", false,
                     "LSLWrongTypeInAssignment", "Cast to string");
        UNSCOPED_INFO("the declaration: " + made);
        REQUIRE(made.find("string s = (string)5;") != std::string::npos);
        made = require_fixed("default\n{\n    state_entry()\n    {\n        string s;\n        s = 6;\n        llOwnerSay(s);\n    }\n}\n", false,
                     "LSLInvalidOperator", "Cast to string");
        UNSCOPED_INFO("the assignment: " + made);
        REQUIRE(made.find("s = (string)6;") != std::string::npos);
        made = require_fixed("string f()\n{\n    return 5;\n}\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay(f());\n    }\n}\n", false,
                     "LSLBadReturnType", "Cast to string");
        UNSCOPED_INFO("the return: " + made);
        REQUIRE(made.find("return (string)5;") != std::string::npos);
        // No cast LSL has: a list is no vector.
        const ALScriptProblems problems = check("default\n{\n    state_entry()\n    {\n        llSetPos([1]);\n    }\n}\n", false);
        const ALScriptProblem* none     = keyed(problems, "LSLArgumentWrongType");
        {
            INFO("said: " + said(problems));
            REQUIRE(none != nullptr);
            REQUIRE(none->fixes.empty());
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: the LSL warnings that say what to write are written so", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n        integer a = 2;\n";
        const std::string tail = "        llOwnerSay((string)a);\n    }\n}\n";
        std::string made = require_fixed(head + "        a == 3;\n" + tail, false, "LSLEqAsStatement", "Assign with '='");
        UNSCOPED_INFO("assigned: " + made);
        REQUIRE(made.find("        a = 3;\n") != std::string::npos);
        made = require_fixed(head + "        a *= 1.5;\n" + tail, false, "LSLIntFloatMulAssign", "Multiply as a float, then cast to integer");
        UNSCOPED_INFO("rewritten: " + made);
        REQUIRE(made.find("a = (integer)(a * 1.5);") != std::string::npos);
        made = require_fixed(head + "        state default;\n" + tail, false, "LSLChangeToCurrentState", "Write 'return' instead");
        UNSCOPED_INFO("returned: " + made);
        REQUIRE(made.find("        return;\n") != std::string::npos);

        // An assignment used as a condition: either was meant, so neither
        // is preferred, and the brackets change nothing.
        const std::string      script   = head + "        if (a = 1) llOwnerSay(\"one\");\n" + tail;
        const ALScriptProblems problems = check(script, false);
        const ALScriptProblem* problem  = keyed(problems, "LSLAssignmentInComparison");
        {
            INFO("said: " + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE(problem->fixes.size() >= 2);
        }
        REQUIRE_FALSE(problem->fixes[0].preferred);
        CHECK_FALSE(problem->fixes[1].preferred);
        REQUIRE(ALScriptFixes::apply(script, problem->fixes[0]).value_or("").find("if (a == 1)") != std::string::npos);
        REQUIRE(problem->fixes[1].safe);
        REQUIRE(ALScriptFixes::apply(script, problem->fixes[1]).value_or("").find("if ((a = 1))") != std::string::npos);
        const std::optional<std::string> quiet = ALScriptFixes::apply(script, problem->fixes[1]);
        {
            INFO("and quiet after");
            REQUIRE(quiet);
            REQUIRE(keyed(check(*quiet, false), "LSLAssignmentInComparison") == nullptr);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: the SLua fixes read off what the checker marks", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        std::string made = require_fixed("--!strict\nlocal n = 5\nlocal s: string = n\nprint(s)\n", true, "LuauTypeMismatch", "Wrap in tostring()");
        UNSCOPED_INFO("the value a declaration is given: " + made);
        REQUIRE(made.find("local s: string = tostring(n)") != std::string::npos);
        made = require_fixed("--!strict\nlocal t = {}\nfunction t:m() return self end\nprint(t.m())\n", true, "LuauRequiresSelf", "Call with ':'");
        UNSCOPED_INFO("with a colon: " + made);
        REQUIRE(made.find("print(t:m())") != std::string::npos);
        made = require_fixed("local function f()\n    counter = 1\n    return counter\nend\nprint(f())\n", true, "LuauLintGlobalUsedAsLocalFunction",
                     "Make 'counter' local");
        UNSCOPED_INFO("local: " + made);
        REQUIRE(made.find("    local counter = 1\n") != std::string::npos);
        made = require_fixed("--!strict\nlocal x\nprint(x)\n", true, "LuauLintUninitializedLocal", "Initialize 'x' with nil");
        UNSCOPED_INFO("given nil: " + made);
        REQUIRE(made.find("local x = nil\n") != std::string::npos);
        made = require_fixed("--!nonstrickt\nprint(1)\n", true, "LuauLintDirectiveUnknownDidYouMean", "Change 'nonstrickt' to 'nonstrict'");
        UNSCOPED_INFO("spelt: " + made);
        CHECK(made.compare(0, 12, "--!nonstrict") == 0);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an unused LSL declaration is removed unless its value does something", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string state = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"x\");\n    }\n}\n";
        std::string made = require_fixed("integer unused = 5;\n" + state, false, "LSLDeclaredButNotUsed", "Remove 'unused'");
        UNSCOPED_INFO("the global's line gone");
        CHECK(made == state);
        made = require_fixed("integer helper(integer x)\n{\n    return x;\n}\n" + state, false, "LSLDeclaredButNotUsed", "Remove 'helper'");
        UNSCOPED_INFO("the function's lines gone");
        CHECK(made == state);
        made = require_fixed("default\n{\n    state_entry()\n    {\n        integer a = 1 + 2;\n        llOwnerSay(\"x\");\n    }\n}\n", false,
                     "LSLDeclaredButNotUsed", "Remove 'a'");
        UNSCOPED_INFO("the local's line gone");
        CHECK(made == state);
        const ALScriptProblem* problem = nullptr;
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        integer h = llListen(0, \"\", NULL_KEY, \"\");\n    }\n}\n", false);
        for (const ALScriptProblem& each : problems)
        {
            problem = each.key == "LSLDeclaredButNotUsed" ? &each : problem;
        }
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("kept where what it is given does something");
        REQUIRE(problem->fixes.empty());
        // One that shares its line keeps the line.
        ALScriptProblem shared;
        shared.line = 0;
        ALScriptFixes::offerRemoval(shared, "integer a = 1; integer b = 2;\n", 0, 8, 0, 13, "a");
        REQUIRE_FALSE(shared.fixes.empty());
        UNSCOPED_INFO("only itself, and the space after it");
        CHECK(ALScriptFixes::apply("integer a = 1; integer b = 2;\n", shared.fixes.front()).value_or("") ==
                      std::string("integer b = 2;\n"));
        // The last on its line takes the space before it instead.
        ALScriptProblem last;
        ALScriptFixes::offerRemoval(last, "integer a = 1; integer b = 2;\n", 0, 23, 0, 28, "b");
        REQUIRE_FALSE(last.fixes.empty());
        UNSCOPED_INFO("and the space before it");
        CHECK(ALScriptFixes::apply("integer a = 1; integer b = 2;\n", last.fixes.front()).value_or("") ==
                      std::string("integer a = 1;\n"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: what the optimizer did is offered as a change to the source", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llOwnerSay((string)(1 + 2));\n        return;\n"
                                   "        llOwnerSay(\"never\");\n    }\n}\n";
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.optimize = true;
        options.resolve  = [](const ALPreprocessor::Ask&, ALPreprocessor::Include&) { return ALPreprocessor::Found::No; };
        const ALPreprocessor::Result result = ALPreprocessor::run(script, options);
        std::string                  notes;
        const ALScriptProblem*       folded      = nullptr;
        const ALScriptProblem*       unreachable = nullptr;
        for (const ALScriptProblem& p : result.problems)
        {
            notes += p.key + " " + p.message + llformat(" (%d fixes)\n", static_cast<int>(p.fixes.size()));
            folded      = p.key == "OptimizerFolded" && !p.fixes.empty() ? &p : folded;
            unreachable = p.key == "OptimizerRemovedUnreachable" && !p.fixes.empty() ? &p : unreachable;
        }
        UNSCOPED_INFO("a fold offered: " + notes);
        REQUIRE(folded != nullptr);
        UNSCOPED_INFO("never preferred: the source is the scripter's");
        CHECK_FALSE(folded->fixes.front().preferred);
        std::string made = ALScriptFixes::apply(script, folded->fixes.front()).value_or("refused");
        UNSCOPED_INFO("written in: " + made);
        REQUIRE(made.find("llOwnerSay(\"3\");") != std::string::npos);
        {
            INFO("what can never run offered: " + notes);
            REQUIRE(unreachable != nullptr);
            REQUIRE(unreachable->fixes.front().preferred);
            CHECK(unreachable->fixes.front().safe);
        }
        made = ALScriptFixes::apply(script, unreachable->fixes.front()).value_or("refused");
        {
            INFO("gone: " + made);
            REQUIRE(made.find("never") == std::string::npos);
            REQUIRE(made.find("        return;\n    }\n") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL expression chosen goes in a local before its statement where safe", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        std::string       made = require_acted_on(head + "        llOwnerSay((string)(llGetUnixTime() + 5));\n" + tail, false, "llGetUnixTime() + 5", true,
                                         "Put it in a local, 'value'");
        UNSCOPED_INFO("on its own line");
        CHECK(made ==
                      head + "        integer value = llGetUnixTime() + 5;\n        llOwnerSay((string)(value));\n" + tail);
        // After another statement on the line, just before its own; and
        // named as nothing in the script is.
        made = require_acted_on(head + "        integer value = 1; llOwnerSay((string)(value * 2.5));\n" + tail, false, "value * 2.5", true,
                       "Put it in a local, 'value2'");
        UNSCOPED_INFO("beside");
        CHECK(made == head + "        integer value = 1; float value2 = value * 2.5; llOwnerSay((string)(value2));\n" + tail);
        made = require_acted_on(head + "        llSetRot(llEuler2Rot(<0, 0, 1>));\n" + tail, false, "llEuler2Rot(<0, 0, 1>)", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("a rotation as LSL says it: " + made);
        REQUIRE(made.find("rotation value = llEuler2Rot(<0, 0, 1>);") != std::string::npos);
        // Not out of a loop's condition, nor from under an if with no
        // braces, nor what is assigned to.
        std::string none = head + "        integer i;\n        while (i < 10) ++i;\n" + tail;
        made = require_acted_on(none, false, "i < 10", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("a condition: " + made);
        CHECK(made.rfind("offered", 0) == 0);
        none = head + "        integer i;\n        if (i) llOwnerSay((string)(i + 1));\n" + tail;
        {
            INFO("under an if");
            CHECK(require_acted_on(none, false, "i + 1", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        none = head + "        integer i;\n        i = 4;\n        llOwnerSay((string)i);\n" + tail;
        S32 line = 0, column = 0, end = 0;
        require_place(none, "i = 4", line, column, end);
        {
            INFO("assigned to");
            CHECK(require_acted(none, false, line, column, line, column + 1, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        // Nor the whole of a statement, which would leave `value;`.
        none = head + "        llOwnerSay((string)llGetUnixTime());\n" + tail;
        {
            INFO("the whole statement");
            CHECK(require_acted_on(none, false, "llOwnerSay((string)llGetUnixTime())", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL if-else is inverted: the condition turned and the branches swapped", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string head = "default\n{\n    touch_start(integer n)\n    {\n";
        const std::string tail = "    }\n}\n";
        std::string       made = require_acted_on(head + "        if (n == 1)\n        {\n            llOwnerSay(\"one\");\n        }\n        else\n        {\n"
                                                "            llOwnerSay(\"many\");\n        }\n" + tail,
                                         false, "if", false, "Invert the if");
        UNSCOPED_INFO("braces");
        CHECK(made ==
                      head + "        if (n != 1)\n        {\n            llOwnerSay(\"many\");\n        }\n        else\n        {\n"
                             "            llOwnerSay(\"one\");\n        }\n" + tail);
        made = require_acted_on(head + "        if (!n) llOwnerSay(\"none\"); else llOwnerSay(\"some\");\n" + tail, false, "if", false, "Invert the if");
        UNSCOPED_INFO("a not taken off");
        CHECK(made == head + "        if (n) llOwnerSay(\"some\"); else llOwnerSay(\"none\");\n" + tail);
        made = require_acted_on(head + "        if (n > 2) llOwnerSay(\"many\"); else llOwnerSay(\"few\");\n" + tail, false, "if", false, "Invert the if");
        UNSCOPED_INFO("an order under a not");
        CHECK(made == head + "        if (!(n > 2)) llOwnerSay(\"few\"); else llOwnerSay(\"many\");\n" + tail);
        const std::string chain = head + "        if (n == 1) llOwnerSay(\"one\"); else if (n == 2) llOwnerSay(\"two\");\n" + tail;
        {
            INFO("not with an else if");
            CHECK(require_acted_on(chain, false, "if", false, "Invert the if").rfind("offered", 0) == 0);
        }
        const std::string lone = head + "        if (n == 1) llOwnerSay(\"one\");\n" + tail;
        {
            INFO("not without an else");
            CHECK(require_acted_on(lone, false, "if", false, "Invert the if").rfind("offered", 0) == 0);
        }
        // An else branch ending in an if of its own goes first in braces,
        // or the else would come to be that if's.
        made = require_acted_on(head + "        if (n) llOwnerSay(\"a\"); else while (n--) if (n == 3) llOwnerSay(\"c\");\n" + tail, false, "if", false,
                       "Invert the if");
        UNSCOPED_INFO("braced");
        CHECK(made == head + "        if (!n) { while (n--) if (n == 3) llOwnerSay(\"c\"); } else llOwnerSay(\"a\");\n" + tail);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL state asking for an event it does not hear is offered a handler", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llListen(0, \"\", NULL_KEY, \"\");\n        llSetTimerEvent(1.0);\n"
                                   "    }\n}\n";
        std::string made = require_acted_on(script, false, "llListen", false, "Add a handler for 'listen'");
        UNSCOPED_INFO("before the brace, after a blank line");
        CHECK(made ==
                      "default\n{\n    state_entry()\n    {\n        llListen(0, \"\", NULL_KEY, \"\");\n        llSetTimerEvent(1.0);\n    }\n\n"
                      "    listen(integer channel, string name, key id, string msg)\n    {\n    }\n}\n");
        UNSCOPED_INFO("the timer too");
        REQUIRE(actions(script, false, 4, 8, 4, 8).size() == 2);
        // Heard already: nothing.
        const std::string heard = "default\n{\n    state_entry() {\n        llSetTimerEvent(1.0);\n    }\n\n    timer() {\n    }\n}\n";
        UNSCOPED_INFO("heard: " + titles(actions(heard, false, 3, 8, 3, 8)));
        REQUIRE(actions(heard, false, 3, 8, 3, 8).empty());
        // The brace where the state's first handler has it.
        const std::string same = "default\n{\n    state_entry() {\n        llSensorRepeat(\"\", NULL_KEY, AGENT, 10.0, PI, 5.0);\n    }\n}\n";
        made = require_acted_on(same, false, "llSensorRepeat", false, "Add a handler for 'sensor'");
        UNSCOPED_INFO("on its line: " + made);
        REQUIRE(made.find("\n\n    sensor(integer num_detected) {\n    }\n}\n") != std::string::npos);
        // Asked for from a function a handler calls, and from one that one
        // calls: offered all the same.
        const std::string helper = "arm()\n{\n    llSetTimerEvent(1.0);\n}\nstart()\n{\n    arm();\n}\n"
                                   "default\n{\n    state_entry()\n    {\n        start();\n    }\n}\n";
        made = require_acted_on(helper, false, "start();\n    }", false, "Add a handler for 'timer'");
        UNSCOPED_INFO("through two calls: " + made);
        REQUIRE(made.find("    timer()\n    {\n    }\n") != std::string::npos);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a SLua expression chosen goes in a local where it surely runs as before", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        std::string made = require_acted_on("local function f(n: number)\n    print(n * 2 + 1)\nend\nf(1)\n", true, "n * 2", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("before the statement");
        CHECK(made == "local function f(n: number)\n    local value = n * 2\n    print(value + 1)\nend\nf(1)\n");
        made = require_acted_on("local x = 1; print(x + 1)\n", true, "x + 1", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("beside");
        CHECK(made == "local x = 1; local value = x + 1; print(value)\n");
        const std::string skipped = "local x = 1\nprint(x > 0 and tostring(x))\n";
        {
            INFO("not what and may skip");
            CHECK(require_acted_on(skipped, true, "tostring(x)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        const std::string looped = "local x = 1\nwhile x < 10 do\n    x += 1\nend\n";
        {
            INFO("not a loop's condition");
            CHECK(require_acted_on(looped, true, "x < 10", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        const std::string elseif = "local x = 1\nif x == 0 then\n    print(0)\nelseif x + 1 == 2 then\n    print(1)\nend\n";
        {
            INFO("not an elseif's condition");
            CHECK(require_acted_on(elseif, true, "x + 1", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        // Not a call that is the whole statement: `value` alone is no
        // statement.
        const std::string whole = "print(math.abs(-1))\n";
        {
            INFO("not the whole statement");
            CHECK(require_acted_on(whole, true, "print(math.abs(-1))", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        made = require_acted_on(whole, true, "math.abs(-1)", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("its argument yes");
        CHECK(made == "local value = math.abs(-1)\nprint(value)\n");
        // Not a call where all it gives is kept, since a local keeps one.
        const std::string two = "local function g(n: number) return n, 2 end\n";
        {
            INFO("not a last argument");
            CHECK(require_acted_on(two + "print(g(1))\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        {
            INFO("not a last return");
            CHECK(require_acted_on(two + "local function h() return 0, g(1) end\nh()\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        {
            INFO("not a table's last item");
            CHECK(require_acted_on(two + "local t = { g(1) }\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        {
            INFO("not to two names");
            CHECK(require_acted_on(two + "local a, b = g(1)\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        {
            INFO("not a for-in's values");
            CHECK(require_acted_on("for k, v in pairs({}) do end\n", true, "pairs({})", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        }
        // Where only the first is kept anyway, yes.
        made = require_acted_on(two + "print(g(1), 3)\n", true, "g(1)", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("an argument before the last");
        CHECK(made == two + "local value = g(1)\nprint(value, 3)\n");
        made = require_acted_on(two + "local a = g(1)\n", true, "g(1)", true, "Put it in a local, 'value'");
        UNSCOPED_INFO("to one name: " + made);
        CHECK(made.rfind("offered", 0) != 0);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a SLua if is inverted; a join interpolated; a local given its type", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        std::string made = require_acted_on("local x = 1\nif x == 1 then\n    print(\"one\")\nelse\n    print(\"other\")\nend\n", true, "if", false, "Invert the if");
        CHECK(made == "local x = 1\nif x ~= 1 then\n    print(\"other\")\nelse\n    print(\"one\")\nend\n");
        made = require_acted_on("local x = 1\nif not (x > 1) then\n    print(\"few\")\nelse\n    print(\"many\")\nend\n", true, "if", false, "Invert the if");
        UNSCOPED_INFO("a not taken off");
        CHECK(made == "local x = 1\nif x > 1 then\n    print(\"many\")\nelse\n    print(\"few\")\nend\n");
        made = require_acted_on("local x = 1\nprint(\"n = \" .. x .. \"!\")\n", true, "..", false, "Write it as an interpolated string");
        UNSCOPED_INFO("interpolated");
        CHECK(made == "local x = 1\nprint(`n = {x}!`)\n");
        made = require_acted_on("local count = 5\nprint(count)\n", true, "count", false, "Declare it as 'number'");
        UNSCOPED_INFO("annotated");
        CHECK(made == "local count: number = 5\nprint(count)\n");
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a SLua script asking for an event it does not hear is offered a handler", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        // Nonstrict, as the grid compiles: the new solver checking strict
        // cannot yet push an overloaded function's parameter types into a
        // function given to it, so every LLEvents:on there is an error it
        // is upstream's to put right.
        ALLuauConfig grid;
        grid.mode = "nonstrict";
        luau.setConfig(grid);
        const std::string script = "ll.Listen(0, \"\", ll.GetOwner(), \"\")\n";
        std::string       made   = require_acted_on(script, true, "Listen", false, "Add a handler for 'listen'");
        UNSCOPED_INFO("at the end");
        CHECK(made ==
                      script + "\nLLEvents:on(\"listen\", function(channel: number, name: string, id: uuid, msg: string)\nend)\n");
        const std::string heard = script + "LLEvents:on(\"listen\", function(channel, name, id, text) end)\n";
        {
            INFO("heard: " + titles(actions(heard, true, 0, 3, 0, 3)));
            CHECK(require_acted_on(heard, true, "Listen", false, "Add a handler for 'listen'").rfind("offered", 0) == 0);
        }
        // A handler taken off is no handler put on.
        const std::string off = script + "local function said(channel, name, id, text) end\nLLEvents:off(\"listen\", said)\n";
        {
            INFO("offered where only taken off: " + titles(actions(off, true, 0, 3, 0, 3)));
            CHECK(require_acted_on(off, true, "Listen", false, "Add a handler for 'listen'").rfind("offered", 0) != 0);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a refactor's name is fresh and a line put in maps to its source line", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("free");
        CHECK(ALScriptFixes::freshName("local x = 1", "value") == std::string("value"));
        UNSCOPED_INFO("taken");
        CHECK(ALScriptFixes::freshName("local value = 1 -- value2", "value") == std::string("value3"));
        UNSCOPED_INFO("a part is not a word");
        CHECK(ALScriptFixes::freshName("local values = 1", "value") == std::string("value"));
        // A line the preprocessor carries as it stands, a line down for the
        // directive it drops.
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask&, ALPreprocessor::Include&) { return ALPreprocessor::Found::No; };
        const std::string                  script = "#define N 2\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay((string)N);\n    }\n}\n";
        const ALPreprocessor::Result       result = ALPreprocessor::run(script, options);
        ALScriptProblem                    held;
        const ALSourceMap::Loc             carried = result.map.toExpanded(0, 5, 8);
        REQUIRE(carried.found());
        held.fixes.push_back(ALScriptFixes::titled("ScriptActionExtract", "Put it in a local, '[1]'", { "value" }));
        held.fixes.back().edits.push_back({ carried.line, 0, carried.line, 0, "        integer value = 1;\n" });
        ALScriptFixes::mapThrough(result.map, held);
        UNSCOPED_INFO("kept");
        REQUIRE(held.fixes.size() == static_cast<size_t>(1));
        UNSCOPED_INFO("the source's line");
        CHECK(held.fixes.front().edits.front().line == 5);
        CHECK(held.fixes.front().edits.front().column == 0);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a module's exports are the names of the table it returns however built", "[ALScriptFixes]")
    {
        const auto listed = [](const std::string& source) {
            std::string out;
            for (const std::string& name : ALLuauExports::of(source))
            {
                out += (out.empty() ? "" : " ") + name;
            }
            return out;
        };
        UNSCOPED_INFO("a table returned");
        CHECK(listed("local function greet() end\nreturn { greet = greet, count = 3, [\"spaced out\"] = 1, 4 }\n") ==
                      std::string("greet count"));
        UNSCOPED_INFO("a local table filled in");
        CHECK(listed("local M = { count = 3 }\nfunction M.greet() end\nfunction M:reset() end\nM.name = \"util\"\nM[\"size\"] = 2\n"
                             "local other = {}\nother.hidden = 1\nreturn M\n") ==
                      std::string("count greet reset name size"));
        UNSCOPED_INFO("through setmetatable");
        CHECK(listed("local M = {}\nM.x = 1\nreturn setmetatable(M, { __index = M })\n") == std::string("x"));
        UNSCOPED_INFO("each once");
        CHECK(listed("local M = {}\nM.x = 1\nM.x = 2\nreturn M\n") == std::string("x"));
        UNSCOPED_INFO("a function: no names");
        CHECK(listed("return function() end\n") == std::string());
        UNSCOPED_INFO("nothing returned");
        CHECK(listed("local M = {}\nM.x = 1\n") == std::string());
        UNSCOPED_INFO("a module that does not parse");
        CHECK(listed("local M = {\nreturn M\n") == std::string());
        UNSCOPED_INFO("not a keyword");
        CHECK(listed("return { [\"end\"] = 1, ok = 2 }\n") == std::string("ok"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a require goes after the opening requires or comments apart from code", "[ALScriptFixes]")
    {
        ALScriptProblem problem;
        problem.key  = "LuauUnknownGlobal";
        problem.args = { "util" };
        const auto made = [&problem](const std::string& text, const std::string& module, bool field) {
            problem.fixes.clear();
            ALScriptFixes::offerRequire(problem, text, module, field);
            return problem.fixes.empty() ? std::string("none") : ALScriptFixes::apply(text, problem.fixes.front()).value_or("refused");
        };
        UNSCOPED_INFO("after the last require");
        CHECK(made("--!strict\nlocal a = require(\"a\")\nlocal b = require(\"b\").b\n\nprint(util.x)\n", "util", false) ==
                      std::string("--!strict\nlocal a = require(\"a\")\nlocal b = require(\"b\").b\nlocal util = require(\"util\")\n\nprint(util.x)\n"));
        REQUIRE_FALSE(problem.fixes.empty());
        CHECK(problem.fixes.front().title == std::string("Require 'util'"));
        UNSCOPED_INFO("after the comments, the blank line kept");
        CHECK(made("--!strict\n--[[ A header\n  over lines ]]\n\nprint(util.x)\n", "@lib/util", false) ==
                      std::string("--!strict\n--[[ A header\n  over lines ]]\nlocal util = require(\"@lib/util\")\n\nprint(util.x)\n"));
        UNSCOPED_INFO("apart from code that follows at once");
        CHECK(made("print(util)\n", "helpers", true) ==
                      std::string("local util = require(\"helpers\").util\n\nprint(util)\n"));
        REQUIRE_FALSE(problem.fixes.empty());
        CHECK(problem.fixes.front().title == std::string("Take 'util' from 'helpers'"));
        UNSCOPED_INFO("past a last line with no break");
        CHECK(made("-- only a comment", "util", false) ==
                      std::string("-- only a comment\nlocal util = require(\"util\")"));
        UNSCOPED_INFO("a name quoted");
        CHECK(made("print(util)\n", "a\"b", false) == std::string("local util = require(\"a\\\"b\")\n\nprint(util)\n"));
        problem.fixes.clear();
        ALScriptFixes::offerRequire(problem, "print(util)\n", "util", false);
        ALScriptFixes::offerRequire(problem, "print(util)\n", "util", false);
        UNSCOPED_INFO("offered once");
        REQUIRE(problem.fixes.size() == static_cast<size_t>(1));
        CHECK_FALSE(problem.fixes.front().preferred);
        CHECK_FALSE(problem.fixes.front().safe);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL include exports its macros and top-level functions and globals", "[ALScriptFixes]")
    {
        const auto listed = [](const std::string& source) {
            std::string out;
            for (const std::string& name : ALLSLExports::of(source))
            {
                out += (out.empty() ? "" : " ") + name;
            }
            return out;
        };
        UNSCOPED_INFO("everything at the top");
        CHECK(listed("// helpers\n#define CHANNEL -42\n#define say(x) \\\n    llOwnerSay(x)\n#ifdef DEBUG\n#include \"more.lsl\"\n#endif\n"
                             "integer gCount = 0;\nlist gSeen;\nvector gAt = <1, 2, 3>;\n"
                             "string greet(string name)\n{\n    integer inner = 1;\n    return \"hi \" + name;\n}\n"
                             "reset() { gCount = 0; }\ninline integer twice(integer n) { return n * 2; }\n") ==
                      std::string("CHANNEL say gCount gSeen gAt greet reset twice"));
        UNSCOPED_INFO("a state holds no declarations");
        CHECK(listed("integer shared;\ndefault\n{\n    state_entry()\n    {\n        integer local = 1;\n    }\n}\nstate other { touch_start(integer n) {} }\n") ==
                      std::string("shared"));
        UNSCOPED_INFO("each once");
        CHECK(listed("#define A 1\n#undef A\n#define A 2\n") == std::string("A"));
        UNSCOPED_INFO("SLua is no LSL");
        CHECK(listed("local function greet() end\nprint(greet())\nreturn { greet = greet }\n") == std::string());
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an LSL include goes after opening directives or comments apart from code", "[ALScriptFixes]")
    {
        ALScriptProblem problem;
        problem.key  = "LSLUndeclared";
        problem.args = { "greet" };
        const auto made = [&problem](const std::string& text, const std::string& include) {
            problem.fixes.clear();
            ALScriptFixes::offerInclude(problem, text, include);
            return problem.fixes.empty() ? std::string("none") : ALScriptFixes::apply(text, problem.fixes.front()).value_or("refused");
        };
        UNSCOPED_INFO("after the last directive");
        CHECK(made("// Hello\n#include \"a.lsl\"\n#define LONG \\\n    1\n\ndefault {}\n", "helpers.lsl") ==
                      std::string("// Hello\n#include \"a.lsl\"\n#define LONG \\\n    1\n#include \"helpers.lsl\"\n\ndefault {}\n"));
        REQUIRE_FALSE(problem.fixes.empty());
        CHECK(problem.fixes.front().title == std::string("Include 'helpers.lsl'"));
        UNSCOPED_INFO("after the comments, the blank line kept");
        CHECK(made("/* A header\n   over lines */\n\ndefault {}\n", "helpers") ==
                      std::string("/* A header\n   over lines */\n#include \"helpers\"\n\ndefault {}\n"));
        UNSCOPED_INFO("apart from code that follows at once");
        CHECK(made("default {}\n", "helpers") == std::string("#include \"helpers\"\n\ndefault {}\n"));
        UNSCOPED_INFO("no name with a quote in it");
        CHECK(made("default {}\n", "a\"b") == std::string("none"));
        // What the studio offers them on: a function, a global and a macro
        // alike, each undeclared by its name.
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        greet(\"x\");\n        llOwnerSay((string)gCount);\n        llSay(CHANNEL, \"\");\n    }\n}\n", false);
        for (const char* name : { "greet", "gCount", "CHANNEL" })
        {
            bool said_so = false;
            for (const ALScriptProblem& each : problems)
            {
                said_so = said_so || (each.key == "LSLUndeclared" && each.args.size() == 1 && each.args[0] == name);
            }
            CAPTURE(name);
            INFO("said:\n" + said(problems));
            REQUIRE(said_so);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a source fix maps into the expansion where copied but not over a macro", "[ALScriptFixes]")
    {
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask&, ALPreprocessor::Include&) { return ALPreprocessor::Found::No; };
        const std::string script = "#define N 2\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay(\"hello\");\n        llOwnerSay((string)N);\n        "
                                   "llOwnerSay(\"bye\");\n    }\n}\n";
        const ALPreprocessor::Result result = ALPreprocessor::run(script, options);
        const auto                   edited = [](S32 line, S32 column, S32 end_line, S32 end_column, const std::string& text) {
            ALScriptFix fix;
            fix.edits.push_back({ line, column, end_line, end_column, text });
            return fix;
        };
        const auto into = [&result](ALScriptFix fix, std::string& out) {
            if (!ALScriptFixes::intoExpansion(result.map, fix))
            {
                return false;
            }
            const std::optional<std::string> made = ALScriptFixes::apply(result.text, fix);
            out                                   = made.value_or(std::string());
            return made.has_value();
        };
        std::string out;
        UNSCOPED_INFO("a word the expansion copied");
        CHECK(into(edited(5, 20, 5, 25, "hi"), out));
        std::string expected = result.text;
        expected.replace(expected.find("\"hello\""), 7, "\"hi\"");
        UNSCOPED_INFO("replaced there alone");
        CHECK(out == expected);

        ALScriptFix over_macro = edited(6, 27, 6, 28, "3");
        UNSCOPED_INFO("not where a macro stood");
        CHECK_FALSE(ALScriptFixes::intoExpansion(result.map, over_macro));
        UNSCOPED_INFO("and the fix as it was");
        CHECK(over_macro.edits.front().line == 6);
        UNSCOPED_INFO("nor across lines unless whole");
        CHECK_FALSE(into(edited(5, 8, 6, 8, ""), out));

        UNSCOPED_INFO("a line put in at a line's start");
        CHECK(into(edited(7, 0, 7, 0, "        integer x;\n"), out));
        const size_t put = out.find("integer x;");
        {
            INFO("before the line it was put before");
            REQUIRE(put != std::string::npos);
            CHECK(put < out.find("\"bye\""));
            CHECK(put > out.find("(string)"));
        }
        UNSCOPED_INFO("whole lines taken out");
        CHECK(into(edited(5, 0, 6, 0, ""), out));
        {
            INFO("that line gone, the rest kept");
            REQUIRE(out.find("hello") == std::string::npos);
            REQUIRE(out.find("\"bye\"") != std::string::npos);
            REQUIRE(out.find("(string)") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a repeated LSL string goes into a global after the others; uses named", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "string gGreeting = \"hello there\";\n"
                                   "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"hello there\");\n"
                                   "        llSetText(\"hello there\", <1.0, 1.0, 1.0>, 1.0);\n    }\n"
                                   "    touch_start(integer n)\n    {\n        llOwnerSay(\"hello there\");\n        llOwnerSay(\"other\");\n    }\n}\n";
        const std::string made = require_acted_on(script, false, "\"hello there\");", false, "Put the string in a global, 'gHelloThere', for its 3 uses");
        UNSCOPED_INFO("declared after the globals, each use named, the global's own value kept");
        CHECK(made ==
                      "string gGreeting = \"hello there\";\nstring gHelloThere = \"hello there\";\n"
                      "default\n{\n    state_entry()\n    {\n        llOwnerSay(gHelloThere);\n"
                      "        llSetText(gHelloThere, <1.0, 1.0, 1.0>, 1.0);\n    }\n"
                      "    touch_start(integer n)\n    {\n        llOwnerSay(gHelloThere);\n        llOwnerSay(\"other\");\n    }\n}\n");
        // A short one costs more as a global than it saves: the list says
        // so, weighing it. A long one, used as often, is lighter.
        UNSCOPED_INFO("heavier on LSO");
        CHECK(ALScriptWeigh::lso(made).total >= ALScriptWeigh::lso(script).total);
        std::string long_script = script;
        std::string long_made   = made;
        for (std::string* text : { &long_script, &long_made })
        {
            for (size_t at = text->find("hello there"); at != std::string::npos; at = text->find("hello there", at + 1))
            {
                text->replace(at, 11, "hello there, and a sentence of some length said to the owner");
            }
        }
        UNSCOPED_INFO("a long one lighter on LSO");
        CHECK(ALScriptWeigh::lso(long_made).total < ALScriptWeigh::lso(long_script).total);
        {
            INFO("not for a string used once");
            CHECK(require_acted_on(script, false, "\"other\"", false, "Put the string in a global, 'gOther', for its 1 uses").rfind("offered", 0) == 0);
        }

        // No globals to follow: ahead of what the script opens with.
        const std::string bare = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"hi\\n\");\n        llOwnerSay(\"hi\\n\");\n    }\n}\n";
        const std::string placed = require_acted_on(bare, false, "\"hi", false, "Put the string in a global, 'gHi', for its 2 uses");
        UNSCOPED_INFO("ahead of the state, a blank line after: " + placed);
        CHECK(placed.rfind("string gHi = \"hi\\n\";\n\ndefault\n", 0) == 0);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a list literal is written as a sum bracketed where part of more", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "list gFixed = [1, 2];\n"
                                   "default\n{\n    state_entry()\n    {\n        integer n = 5;\n"
                                   "        list l = [n, \"two\", <3.0, 3.0, 3.0>, -4, n + 3, llGetListLength(gFixed)];\n"
                                   "        llOwnerSay((string)[1, 2]);\n    }\n}\n";
        std::string made = require_acted_on(script, false, "[n,", false, "Write the list as a sum");
        UNSCOPED_INFO("each where it would mean what it meant");
        CHECK(made ==
                      "list gFixed = [1, 2];\n"
                      "default\n{\n    state_entry()\n    {\n        integer n = 5;\n"
                      "        list l = (list)n + \"two\" + <3.0, 3.0, 3.0> + (-4) + (n + 3) + llGetListLength(gFixed);\n"
                      "        llOwnerSay((string)[1, 2]);\n    }\n}\n");
        made = require_acted_on(script, false, "[1, 2]);", false, "Write the list as a sum");
        UNSCOPED_INFO("bracketed under a cast: " + made);
        REQUIRE(made.find("llOwnerSay((string)((list)1 + 2));") != std::string::npos);
        {
            INFO("not a global's value, which must be written out");
            CHECK(require_acted_on(script, false, "[1, 2];", false, "Write the list as a sum").rfind("offered", 0) == 0);
        }
    }
    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a fix that takes code out says so and one that only renames does not", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const std::string state = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"x\");\n    }\n}\n";
        const ALScriptProblems lsl = check("integer helper(integer x)\n{\n    return x;\n}\n" + state, false);
        const ALScriptProblem* unused = keyed(lsl, "LSLDeclaredButNotUsed");
        REQUIRE(unused != nullptr);
        REQUIRE_FALSE(unused->fixes.empty());
        REQUIRE(unused->fixes.front().safe);
        CHECK(unused->fixes.front().removes);
        const ALScriptProblems lua = check("local function f()\n    local unused = 1\nend\nf()\n", true);
        const ALScriptProblem* local = keyed(lua, "LuauLintLocalUnused");
        {
            INFO("said: " + said(lua));
            REQUIRE(local != nullptr);
            REQUIRE_FALSE(local->fixes.empty());
        }
        REQUIRE(local->fixes.front().safe);
        CHECK_FALSE(local->fixes.front().removes);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: short names guess one edit away; a swap is one edit; ties not preferred", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        // One character: nothing but itself in another case; two edits
        // made PI of it once.
        ALScriptProblems       problems = check(head + "        llOwnerSay((string)n);\n" + tail, false);
        const ALScriptProblem* problem  = keyed(problems, "LSLUndeclared");
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("no guess at n: " + titles(problem->fixes));
        REQUIRE(problem->fixes.empty());
        const auto unknown = [](const ALScriptProblems& all) -> const ALScriptProblem* {
            for (const ALScriptProblem& each : all)
            {
                if (each.key.find("UnknownGlobal") != std::string::npos)
                {
                    return &each;
                }
            }
            return nullptr;
        };
        problems = check("print(xy)\n", true);
        problem  = unknown(problems);
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("no guess at xy two edits from ll: " + titles(problem->fixes));
        REQUIRE(problem->fixes.empty());
        // Two as near: both, neither preferred.
        problems = check(head + "        integer cat = 1;\n        integer car = 2;\n        llOwnerSay((string)(cat + car + cax));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("both: " + titles(problem->fixes));
        REQUIRE(problem->fixes.size() == size_t(2));
        REQUIRE_FALSE(problem->fixes[0].preferred);
        CHECK_FALSE(problem->fixes[1].preferred);
        problems = check("local cat, car = 1, 2\nprint(cat, car, cax)\n", true);
        problem  = unknown(problems);
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        {
            INFO("both in SLua: " + titles(problem->fixes));
            REQUIRE(problem->fixes.size() == 2);
            REQUIRE_FALSE(problem->fixes[0].preferred);
            CHECK_FALSE(problem->fixes[1].preferred);
        }
        // One near a short name: offered, not preferred; one near a longer
        // name, preferred, a swap being one edit.
        problems = check(head + "        integer cat = 1;\n        llOwnerSay((string)(cat + cta));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        {
            INFO("said: " + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE(problem->fixes.size() == 1);
        }
        UNSCOPED_INFO("a guess at three characters is not preferred");
        CHECK_FALSE(problem->fixes.front().preferred);
        problems = check(head + "        integer total = 1;\n        llOwnerSay((string)(total + totla));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        {
            INFO("said: " + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE(problem->fixes.size() == 1);
            CHECK(problem->fixes.front().preferred);
        }
        UNSCOPED_INFO("swapped back");
        CHECK(ALScriptFixes::editDistance("totla", "total") == size_t(1));
        // A name one edit past how far a guess may be is no guess, even
        // where it is the only one that near.
        problems = check(head + "        integer count = 1;\n        llOwnerSay((string)(count + cqunx));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        UNSCOPED_INFO("said: " + said(problems));
        REQUIRE(problem != nullptr);
        UNSCOPED_INFO("two edits from a five-letter name: no guess: " + titles(problem->fixes));
        REQUIRE(problem->fixes.empty());
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a fix's edits are applied in text order: insertions before replacements", "[ALScriptFixes]")
    {
        for (const bool insertion_first : { true, false })
        {
            ALScriptFix fix;
            const ALScriptEdit put{ 0, 0, 0, 0, "local v = print(x)\n" };
            const ALScriptEdit swap{ 0, 0, 0, 8, "v" };
            fix.edits = insertion_first ? std::vector<ALScriptEdit>{ put, swap } : std::vector<ALScriptEdit>{ swap, put };
            CAPTURE(insertion_first);
            REQUIRE(ALScriptFixes::apply("print(x)\n", fix).value_or("refused") ==
                          std::string("local v = print(x)\nv\n"));
        }
        ALScriptFix two;
        two.edits = { { 0, 0, 0, 0, "a" }, { 0, 0, 0, 0, "b" } };
        UNSCOPED_INFO("two at one place in the order given");
        CHECK(ALScriptFixes::apply("x", two).value_or("refused") == std::string("abx"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: the optimizer's removal is added unpreferred; other fixes keep their mark", "[ALScriptFixes]")
    {
        ALScriptProblem problem;
        problem.key  = "OptimizerRemovedLocal";
        problem.args = { "x" };
        ALScriptFix other;
        other.title     = "Something else";
        other.preferred = true;
        problem.fixes.push_back(other);
        // Where nothing can be taken out -- past the text's end -- nothing
        // is added, and the fix already there keeps its mark.
        problem.line = problem.endLine = 9;
        problem.column                 = 0;
        problem.endColumn              = 1;
        ALScriptFixes::attachOptimizer(problem, "integer x;\n");
        REQUIRE(problem.fixes.size() == size_t(1));
        CHECK(problem.fixes.front().preferred);
        // Where it can, its own is added unpreferred.
        problem.line = problem.endLine = 0;
        problem.column                 = 8;
        problem.endColumn              = 9;
        ALScriptFixes::attachOptimizer(problem, "integer x;\n");
        REQUIRE(problem.fixes.size() == 2);
        CHECK_FALSE(problem.fixes.back().preferred);
        CHECK(problem.fixes.front().preferred);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: LSL's operators and zero indexes in SLua fixed; only ~= is safe", "[ALScriptFixes]")
    {
        {
            INFO("~=");
            CHECK(require_fixed("local a, b = 1, 2\nif a != b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '!=' to '~='") ==
                  std::string("local a, b = 1, 2\nif a ~= b then print(1) end\n"));
        }
        {
            INFO("and, apart from what it joins");
            CHECK(require_fixed("local a, b = true, false\nif a&&b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '&&' to 'and'") ==
                  std::string("local a, b = true, false\nif a and b then print(1) end\n"));
        }
        {
            INFO("or");
            CHECK(require_fixed("local a, b = true, false\nif a || b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '||' to 'or'") ==
                  std::string("local a, b = true, false\nif a or b then print(1) end\n"));
        }
        {
            INFO("not, apart from its operand");
            CHECK(require_fixed("local a = true\nif(!a) then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '!' to 'not'") ==
                  std::string("local a = true\nif(not a) then print(1) end\n"));
        }
        const ALScriptProblems compared = check("local a, b = 1, 2\nif a != b then print(1) end\n", true);
        const ALScriptProblems denied   = check("local a = true\nif !a then print(1) end\n", true);
        const ALScriptProblem* unequal  = keyed(compared, "LuauUnexpectedDidYouMean");
        const ALScriptProblem* negated  = keyed(denied, "LuauUnexpectedDidYouMean");
        {
            INFO("~= safe, the same whatever it compares");
            REQUIRE(unequal);
            CHECK(unequal->fixes.front().safe);
        }
        {
            INFO("not unsafe: 0 is true in SLua");
            REQUIRE(negated);
            REQUIRE_FALSE(negated->fixes.empty());
            CHECK_FALSE(negated->fixes.front().safe);
        }

        {
            INFO("an index from 0");
            CHECK(require_fixed("local t = {}\ntable.insert(t, 0, 5)\n", true, "LuauLintTableInsertZero", "Change '0' to '1'") ==
                  std::string("local t = {}\ntable.insert(t, 1, 5)\n"));
        }
        {
            INFO("a loop from 0");
            CHECK(require_fixed("local t = {1, 2}\nfor i = 0, #t do print(t[i]) end\n", true, "LuauLintForRangeZero", "Change '0' to '1'") ==
                  std::string("local t = {1, 2}\nfor i = 1, #t do print(t[i]) end\n"));
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: LSL fixes: an if's ';' and event parameters and untyped ones and braces", "[ALScriptFixes]")
    {
        {
            INFO("the if's ';' out");
            CHECK(require_fixed("default { state_entry() { integer x = llGetUnixTime(); if (x); llOwnerSay(\"a\"); } }", false, "LSLEmptyIf",
                                "Take out the ';' that ends the if") ==
                  std::string("default { state_entry() { integer x = llGetUnixTime(); if (x) llOwnerSay(\"a\"); } }"));
        }
        {
            INFO("a wrong type, the name kept");
            CHECK(require_fixed("default { touch_start(string n) { llOwnerSay((string)n); } }", false, "LSLArgumentWrongTypeEvent",
                                "Write the parameters 'touch_start' takes") ==
                  std::string("default { touch_start(integer n) { llOwnerSay((string)n); } }"));
        }
        {
            INFO("too few, the builtins' names");
            CHECK(require_fixed("default { touch_start() { } }", false, "LSLTooFewArgumentsEvent", "Write the parameters 'touch_start' takes") ==
                  std::string("default { touch_start(integer num_detected) { } }"));
        }
        {
            INFO("too many");
            CHECK(require_fixed("default { touch_start(integer n, integer m) { } }", false, "LSLTooManyArgumentsEvent", "Write the parameters 'touch_start' takes") ==
                  std::string("default { touch_start(integer n) { } }"));
        }
        {
            INFO("an event's with no type");
            CHECK(require_fixed("default { touch_start(n) { } }", false, "LSLParameterUntyped", "Write the parameters 'touch_start' takes") ==
                  std::string("default { touch_start(integer n) { } }"));
        }
        {
            INFO("a declaration as an if's body, braced");
            CHECK(require_fixed("default { state_entry() { if (llGetUnixTime()) integer i = 1; } }", false, "LSLDeclarationInvalidHere",
                                "Put it in braces of its own") ==
                  std::string("default { state_entry() { if (llGetUnixTime()) { integer i = 1; } } }"));
        }

        const std::string      function = "say(x) { llOwnerSay((string)x); }\ndefault { state_entry() { say(1); } }";
        const ALScriptProblems untyped  = check(function, false);
        const ALScriptProblem* bare     = keyed(untyped, "LSLParameterUntyped");
        {
            INFO("a function's: every type, none preferred");
            REQUIRE(bare);
            REQUIRE(bare->fixes.size() == 7);
            CHECK(std::none_of(bare->fixes.begin(), bare->fixes.end(), [](const ALScriptFix& fix) { return fix.preferred; }));
        }
        const std::optional<std::string> typed = ALScriptFixes::apply(function, bare->fixes.front());
        {
            INFO("declared as the first");
            REQUIRE(typed);
            REQUIRE(typed->rfind("say(integer x)", 0) == 0);
            CHECK(errors(check(*typed, false)) == 0);
        }

        const ALScriptProblems missing = check("default { state_entry() { integer x = 1 llOwnerSay(\"a\"); } }", false);
        const ALScriptProblem* ended   = keyed(missing, "LSLSyntaxMissing");
        {
            INFO("a missing ';' put in safely");
            REQUIRE(ended);
            REQUIRE_FALSE(ended->fixes.empty());
            CHECK(ended->fixes.front().safe);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a handler's missing brace goes on its own line level with the next one", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llSay(0, \"Hello, Avatar!\");\n\n"
                                   "    touch_start(integer total_number)\n    {\n        llSay(0, \"Touched.\");\n    }\n}\n";
        const std::string made = require_fixed(script, false, "LSLSyntaxMissing", "Insert '}'");
        UNSCOPED_INFO("closed after the call, its own line: " + made);
        REQUIRE(made.find("\"Hello, Avatar!\");\n    }\n\n    touch_start(") != std::string::npos);
        CHECK(errors(check(made, false)) == 0);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: SlCompoundAssign's fix writes x op= y safely; a field and a join too", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        const std::string made = require_fixed("local x = 1\nx = x + 2 * x\nprint(x)\n", true, "LuauLintSlCompoundAssign", "Write it x += ...");
        CHECK(made == std::string("local x = 1\nx += 2 * x\nprint(x)\n"));
        const ALScriptProblems problems = check("local x = 1\nx = x + 2 * x\nprint(x)\n", true);
        const ALScriptProblem* problem  = keyed(problems, "LuauLintSlCompoundAssign");
        REQUIRE(problem);
        CHECK(problem->fixes.front().safe);
        {
            INFO("a field, joined");
            CHECK(require_fixed("local t = { n = \"a\" }\nt.n = t.n ..\"b\" .. \"c\"\nprint(t.n)\n", true, "LuauLintSlCompoundAssign",
                                "Write it t.n ..= ...") ==
                  std::string("local t = { n = \"a\" }\nt.n ..= \"b\" .. \"c\"\nprint(t.n)\n"));
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: SlLoopInvariantCall's fix: the call in a fresh local before the loop", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string script = "default { state_entry() {\n"
                                   "    list l = [1, 2]; integer length = 3; integer i;\n"
                                   "    for (i = 0; i < llGetListLength(l); ++i) llOwnerSay((string)(i + length));\n"
                                   "} }\n";
        const std::string made = require_fixed(script, false, "LSLSlLoopInvariantCall", "Work out llGetListLength once, before the loop");
        CHECK(made ==
                      std::string("default { state_entry() {\n"
                                  "    list l = [1, 2]; integer length = 3; integer i;\n"
                                  "    integer length2 = llGetListLength(l);\n"
                                  "    for (i = 0; i < length2; ++i) llOwnerSay((string)(i + length));\n"
                                  "} }\n"));
        const ALScriptProblems problems = check(script, false);
        const ALScriptProblem* problem  = keyed(problems, "LSLSlLoopInvariantCall");
        REQUIRE(problem);
        CHECK_FALSE(problem->fixes.front().safe);
        // Not where the loop is all its if has: a declaration may not be.
        const ALScriptProblems in_if = check("default { state_entry() {\n"
                                             "    list l; integer i;\n"
                                             "    if (TRUE) while (i < llGetListLength(l)) ++i;\n"
                                             "} }\n",
                                             false);
        const ALScriptProblem* alone = keyed(in_if, "LSLSlLoopInvariantCall");
        REQUIRE(alone);
        REQUIRE(alone->fixes.empty());
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: SlNumberTruth's fixes: n ~= 0 bracketed where needed and not n as n == 0", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        {
            INFO("compared");
            CHECK(require_fixed("local n = 0\nif n then print(n) end\n", true, "LuauLintSlNumberTruth", "Compare it: n ~= 0") ==
                  std::string("local n = 0\nif n ~= 0 then print(n) end\n"));
        }
        {
            INFO("bracketed");
            CHECK(require_fixed("local c = true\nif (if c then 1 else 2) then print(c) end\n", true, "LuauLintSlNumberTruth",
                                "Compare it: if c then 1 else 2 ~= 0") ==
                  std::string("local c = true\nif ((if c then 1 else 2) ~= 0) then print(c) end\n"));
        }
        {
            INFO("not");
            CHECK(require_fixed("local n = 0\nprint(not n)\n", true, "LuauLintSlNumberTruthNot", "Ask whether it is 0: n == 0") ==
                  std::string("local n = 0\nprint(n == 0)\n"));
        }
        const ALScriptProblems problems = check("local n = 0\nif n then print(n) end\n", true);
        const ALScriptProblem* problem  = keyed(problems, "LuauLintSlNumberTruth");
        REQUIRE(problem);
        CHECK_FALSE(problem->fixes.front().safe);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: SlNilSentinel's fixes: nil for SLua's finds and -1 for llcompat's; safe", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        {
            INFO("never");
            CHECK(require_fixed("local l = {1}\nif ll.ListFindList(l, {2}) == -1 then print(l) end\n", true, "LuauLintSlNilSentinel",
                                "Write it ll.ListFindList(...) == nil") ==
                  std::string("local l = {1}\nif ll.ListFindList(l, {2}) == nil then print(l) end\n"));
        }
        {
            INFO("on the right");
            CHECK(require_fixed("local i = table.find({1}, 1)\nprint(-1 ~= i)\n", true, "LuauLintSlNilSentinelAlways", "Write it i ~= nil") ==
                  std::string("local i = table.find({1}, 1)\nprint(i ~= nil)\n"));
        }
        {
            INFO("ordered, bracketed");
            CHECK(require_fixed("local i = ll.SubStringIndex(\"ab\", \"b\")\nprint((i) < 0)\n", true, "LuauLintSlNilSentinelOrder",
                                "Write it i == nil") ==
                  std::string("local i = ll.SubStringIndex(\"ab\", \"b\")\nprint((i) == nil)\n"));
        }
        {
            INFO("ordered, on the right");
            CHECK(require_fixed("local i = ll.SubStringIndex(\"ab\", \"b\")\nprint(0 <= i)\n", true,
                                "LuauLintSlNilSentinelOrder", "Write it i ~= nil") ==
                  std::string("local i = ll.SubStringIndex(\"ab\", \"b\")\nprint(i ~= nil)\n"));
        }
        {
            INFO("llcompat's");
            CHECK(require_fixed("print(llcompat.ListFindList({1}, {1}) ~= nil)\n", true, "LuauLintSlNilSentinelAlways",
                                "Write it llcompat.ListFindList(...) ~= -1") ==
                  std::string("print(llcompat.ListFindList({1}, {1}) ~= -1)\n"));
        }
        const ALScriptProblems problems = check("print(table.find({1}, 1) == -1)\n", true);
        const ALScriptProblem* problem  = keyed(problems, "LuauLintSlNilSentinel");
        REQUIRE(problem);
        REQUIRE_FALSE(problem->fixes.empty());
        CHECK(problem->fixes.front().safe);
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a deprecated ll function is renamed only where the arguments match", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        {
            INFO("the same arguments");
            CHECK(require_fixed("ll.SetLinkPrimitiveParams(LINK_THIS, {})\n", true, "LuauLintDeprecatedMemberUse",
                                "Use 'll.SetLinkPrimitiveParamsFast' instead of 'll.SetLinkPrimitiveParams'") ==
                  std::string("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {})\n"));
        }
        for (const char* call : { "ll.SetPrimitiveParams({})\n", "ll.MakeFire(1, 1, 1, 1, 1, \"\", vector(0, 0, 0))\n" })
        {
            const ALScriptProblems problems = check(call, true);
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.key.rfind("LuauLintDeprecatedMemberUse", 0) == 0)
                {
                    for (const ALScriptFix& fix : problem.fixes)
                    {
                        CAPTURE(call, fix.title);
                        REQUIRE(fix.key != "ScriptFixUseInstead");
                    }
                }
            }
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: making a script --!strict changes its head's mode or puts it first", "[ALScriptFixes]")
    {
        const auto made = [](const std::string& text) {
            const std::optional<ALScriptFix> fix = ALScriptFixes::strictFix(text);
            return fix ? ALScriptFixes::apply(text, *fix).value_or("(not made)") : std::string("(none)");
        };
        UNSCOPED_INFO("put first");
        CHECK(made("-- a door\nlocal x = 1\n") == std::string("--!strict\n-- a door\nlocal x = 1\n"));
        UNSCOPED_INFO("changed");
        CHECK(made("-- a door\n--!nonstrict\nlocal x = 1\n") == std::string("-- a door\n--!strict\nlocal x = 1\n"));
        UNSCOPED_INFO("from nocheck");
        CHECK(made("--!nocheck\nprint(1)\n") == std::string("--!strict\nprint(1)\n"));
        UNSCOPED_INFO("already");
        CHECK(made("--!strict\nprint(1)\n") == std::string("(none)"));
        UNSCOPED_INFO("past the head, not read");
        CHECK(made("print(1)\n--!nonstrict\n") == std::string("--!strict\nprint(1)\n--!nonstrict\n"));
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: an unread require is offered for removal neither safe nor preferred", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        // The fixture's statics named by its class: MSVC finds none of them
        // from a lambda, even one holding `this`.
        const auto require_removal = [this](const std::string& script) -> const ALScriptFix* {
            static ALScriptProblems problems;
            problems                       = check(script, true);
            const ALScriptProblem* problem = alscriptfixes_data::keyed(problems, "LuauLintImportUnused");
            {
                INFO("said: " + alscriptfixes_data::said(problems));
                REQUIRE(problem != nullptr);
                REQUIRE_FALSE(problem->fixes.empty());
            }
            UNSCOPED_INFO("marking it unused first");
            REQUIRE(problem->fixes.front().title.substr(0, 9) == std::string("Rename to"));
            for (const ALScriptFix& fix : problem->fixes)
            {
                if (fix.title.rfind("Remove the require", 0) == 0)
                {
                    return &fix;
                }
            }
            return nullptr;
        };
        const std::string script = "local util = require(\"util\")\nprint(1)\n";
        const ALScriptFix* fix   = require_removal(script);
        REQUIRE(fix != nullptr);
        CHECK(fix->title == std::string("Remove the require of \"util\""));
        REQUIRE_FALSE(fix->preferred);
        CHECK_FALSE(fix->safe);
        UNSCOPED_INFO("its line gone");
        CHECK(ALScriptFixes::apply(script, *fix).value_or("(not made)") == std::string("print(1)\n"));

        const std::string bare = "local util = require 'util'; print(1)\n";
        fix                    = require_removal(bare);
        UNSCOPED_INFO("without brackets, and its ;");
        REQUIRE(fix != nullptr);
        UNSCOPED_INFO("the statement alone gone");
        CHECK(ALScriptFixes::apply(bare, *fix).value_or("(not made)") == std::string("print(1)\n"));

        {
            INFO("a module named by an expression: nothing to name");
            REQUIRE(require_removal("local name = 'util'\nlocal util = require(name)\n") == nullptr);
        }
    }

    TEST_CASE_METHOD(alscriptfixes_data, "ALScriptFixes: a deprecated call with other arguments is rewritten as a call that works", "[ALScriptFixes]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(luauLoaded);
        // The fixture's statics named by its class: MSVC finds none of them
        // from a lambda, even one holding `this`.
        const auto require_deprecated = [this](const std::string& script) -> const ALScriptProblem* {
            static ALScriptProblems problems;
            problems = check(script, true);
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.key.rfind("LuauLintDeprecated", 0) == 0)
                {
                    return &problem;
                }
            }
            FAIL("no deprecation said:\n" + alscriptfixes_data::said(problems));
            return nullptr;
        };
        const auto require_written = [&](const std::string& script) {
            CAPTURE(script);
            const ALScriptProblem* problem = require_deprecated(script);
            REQUIRE_FALSE(problem->fixes.empty());
            const ALScriptFix& fix = problem->fixes.front();
            CAPTURE(fix.title);
            REQUIRE(fix.preferred);
            REQUIRE_FALSE(fix.safe);
            const std::string made = ALScriptFixes::apply(script, fix).value_or("(not made)");
            const ALScriptProblems after = check(made, true);
            INFO("made:\n" + made + "\nsaid then:\n" + alscriptfixes_data::said(after));
            for (const ALScriptProblem& each : after)
            {
                REQUIRE(each.key.rfind("LuauLintDeprecated", 0) != 0);
            }
            REQUIRE(alscriptfixes_data::errors(after) == static_cast<size_t>(0));
            return fix.title + "\n" + made;
        };
        const std::string vectors = "local a, b = vector(1, 0, 0), vector(0, 1, 0)\n";
        {
            INFO("a distance");
            CHECK(require_written(vectors + "print(ll.VecDist(a, ll.GetPos()))\n") ==
                  "Write it vector.magnitude(a - ll.GetPos())\n" + vectors + "print(vector.magnitude(a - ll.GetPos()))\n");
        }
        {
            INFO("bracketed");
            CHECK(require_written(vectors + "print(ll.VecDist(a + b, b))\n") ==
                  "Write it vector.magnitude((a + b) - b)\n" + vectors + "print(vector.magnitude((a + b) - b))\n");
        }
        {
            INFO("half up");
            CHECK(require_written("local n = 2\nprint(ll.Round(n * 0.25))\n") ==
                  std::string("Write it math.floor((n * 0.25) + 0.5)\nlocal n = 2\nprint(math.floor((n * 0.25) + 0.5))\n"));
        }
        {
            INFO("a link first");
            CHECK(require_written("ll.SetPrimitiveParams({PRIM_SIZE, vector(1, 1, 1)})\n") ==
                  std::string("Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_SIZE, vector(1, 1, 1)})\n"
                              "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_SIZE, vector(1, 1, 1)})\n"));
        }
        {
            INFO("a length");
            CHECK(require_written("local t = {1, 2}\nprint(table.getn(t))\n") ==
                  std::string("Write it #t\nlocal t = {1, 2}\nprint(#t)\n"));
        }
        {
            INFO("a name");
            CHECK(require_written("print(ll.Abs(-1))\n") == std::string("Use 'math.abs' instead of 'll.Abs'\nprint(math.abs(-1))\n"));
        }

        const ALScriptProblem* norm = require_deprecated("print(ll.VecNorm(vector(0, 0, 0)))\n");
        {
            INFO("normalize offered: " + alscriptfixes_data::said({ *norm }));
            REQUIRE_FALSE(norm->fixes.empty());
            CHECK(norm->fixes.front().title == "Use 'vector.normalize' instead of 'll.VecNorm'");
        }
        UNSCOPED_INFO("not preferred, NaN at zero");
        CHECK_FALSE(norm->fixes.front().preferred);
        for (const char* script : { "LLEvents:on(\"touch_start\", function(detected) print(ll.DetectedKey(0)) end)\n",
                                    "local a = vector(1, 0, 0)\nprint(ll.VecDist(a,\n    a))\n", "local f = ll.VecDist\nprint(f)\n",
                                    "print(ll.GetListEntryType({1}, 0))\n" })
        {
            CAPTURE(script);
            const ALScriptProblem* problem = require_deprecated(script);
            INFO(alscriptfixes_data::said({ *problem }));
            REQUIRE(problem->fixes.empty());
        }

        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const ALScriptProblems lsl = check("default\n{\n    state_entry()\n    {\n        llMakeFire(1, 1.0, 1.0, 1.0, 1.0, \"\", ZERO_VECTOR);\n    }\n}\n", false);
        const ALScriptProblem* fire = alscriptfixes_data::keyed(lsl, "LSLDeprecatedWithReplacement");
        UNSCOPED_INFO("said: " + alscriptfixes_data::said(lsl));
        REQUIRE(fire != nullptr);
        UNSCOPED_INFO("no llParticleSystem by name: " + alscriptfixes_data::said(lsl));
        REQUIRE(fire->fixes.empty());
    }
}
