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

#include "../alscriptfixes.h"
#include "../alscriptweight.h"

#include "../allslexports.h"
#include "../alluauexports.h"

#include "../allslservice.h"
#include "../alluauservice.h"
#include "../alpreprocessor.h"
#include "../allsloptimizer.h"

#include "../test/lltut.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tut
{
    struct alscriptfixes_data
    {
        ALLSLService lsl;
        ALLuauService luau;
        std::string  error;
        bool         lslLoaded  = false;
        bool         luauLoaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool   newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alscriptfixes_data()
        {
            lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            luau.setNewSolver(newSolver, error);
            luauLoaded = luau.loadDefinitions(text.str(), error);
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
        std::string fixed(const std::string& script, bool lua, const std::string& key, const std::string& title)
        {
            const ALScriptProblems problems = check(script, lua);
            const ALScriptProblem* problem  = keyed(problems, key);
            ensure("said: " + key + "\n" + said(problems), problem != nullptr);
            ensure("a fix: " + said(problems), !problem->fixes.empty());
            const ALScriptFix& fix = problem->fixes.front();
            ensure("preferred", fix.preferred);
            ensure_equals("its words", fix.title, title);
            const std::optional<std::string> made = ALScriptFixes::apply(script, fix);
            ensure("applies", made.has_value());
            const ALScriptProblems after = check(*made, lua);
            ensure("gone from:\n" + *made + "\n" + said(after), keyed(after, key) == nullptr);
            ensure("no error in its place:\n" + *made + "\n" + said(after), errors(after) <= errors(problems) - (problem->severity == ALScriptProblem::Severity::Error ? 1 : 0));
            return *made;
        }

        // Where a piece of the script is, zero-based: its first line and
        // column, and where it ends on that line.
        static void place(const std::string& script, const std::string& piece, S32& line, S32& column, S32& end)
        {
            const size_t at = script.find(piece);
            ensure("in the script: " + piece, at != std::string::npos);
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
        std::string acted(const std::string& script, bool lua, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& title)
        {
            const std::vector<ALScriptFix> offered = actions(script, lua, line, column, endLine, endColumn);
            for (const ALScriptFix& action : offered)
            {
                if (action.title != title)
                {
                    continue;
                }
                ensure("a refactor", action.kind == ALScriptFix::Kind::Refactor);
                const std::optional<std::string> made = ALScriptFixes::apply(script, action);
                ensure("applies", made.has_value());
                const ALScriptProblems after = check(*made, lua);
                ensure("no error in:\n" + *made + "\n" + said(after), errors(after) == 0);
                return *made;
            }
            return "offered: " + titles(offered);
        }

        // The same with the caret, or the stretch chosen, on a piece of the
        // script.
        std::string actedOn(const std::string& script, bool lua, const std::string& piece, bool chosen, const std::string& title)
        {
            S32 line = 0, column = 0, end = 0;
            place(script, piece, line, column, end);
            return acted(script, lua, line, column, line, chosen ? end : column, title);
        }
    };
    typedef test_group<alscriptfixes_data> alscriptfixes_group;
    typedef alscriptfixes_group::object    object;
    tut::alscriptfixes_group               alscriptfixes_test("ALScriptFixes");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("a missing semicolon is put where it is missing");
        ensure("builtins: " + error, lslLoaded);
        const std::string made = fixed("default\n{\n    state_entry()\n    {\n        integer a = 1\n        llOwnerSay((string)a);\n    }\n}\n", false,
                                       "LSLSyntaxMissing", "Insert ';'");
        ensure("after the 1: " + made, made.find("integer a = 1;\n") != std::string::npos);
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("an LSL name spelt wrong is changed to the nearest in scope or among the builtins");
        ensure("builtins: " + error, lslLoaded);
        std::string made = fixed("default\n{\n    state_entry()\n    {\n        llownersay(\"hi\");\n    }\n}\n", false, "LSLUndeclared",
                                 "Change 'llownersay' to 'llOwnerSay'");
        ensure("the call: " + made, made.find("llOwnerSay(\"hi\")") != std::string::npos);
        made = fixed("default\n{\n    state_entry()\n    {\n        integer count = 1;\n        llOwnerSay((string)coutn);\n    }\n}\n", false,
                     "LSLUndeclared", "Change 'coutn' to 'count'");
        ensure("the local: " + made, made.find("(string)count)") != std::string::npos);
        // A local declared after the place is not in scope there, and not
        // offered.
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        llOwnerSay((string)coutn);\n        integer count = 1;\n    }\n}\n", false);
        const ALScriptProblem* later = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), later != nullptr);
        ensure("not a name declared after: " + said(problems), later->fixes.empty() || later->fixes.front().args[1] != "count");
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("a deprecated LSL function with one replacement is replaced, and one with several is not");
        ensure("builtins: " + error, lslLoaded);
        const std::string made = fixed("default\n{\n    state_entry()\n    {\n        llSoundPreload(\"boom\");\n    }\n}\n", false,
                                       "LSLDeprecatedWithReplacement", "Use 'llPreloadSound' instead of 'llSoundPreload'");
        ensure("the call: " + made, made.find("llPreloadSound(\"boom\");") != std::string::npos);

        const ALScriptProblems problems = check("default\n{\n    state_entry()\n    {\n        llSound(\"boom\", 1.0, TRUE, FALSE);\n    }\n}\n", false);
        const ALScriptProblem* several  = keyed(problems, "LSLDeprecatedWithReplacement");
        ensure("said: " + said(problems), several != nullptr);
        ensure("no fix among several: " + said(problems), several->fixes.empty());
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("an unused SLua local is marked unused, safely");
        ensure("definitions: " + error, luauLoaded);
        const std::string      made     = fixed("local unused = 1\n", true, "LuauLintLocalUnused", "Rename to '_unused'");
        ensure_equals("marked", made, std::string("local _unused = 1\n"));
        const ALScriptProblems problems = check("local function helper() end\n", true);
        const ALScriptProblem* problem  = keyed(problems, "LuauLintFunctionUnused");
        ensure("said: " + said(problems), problem != nullptr && !problem->fixes.empty());
        ensure("safe", problem->fixes.front().safe);
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("an SLua key spelt wrong is changed to the nearest there is");
        ensure("definitions: " + error, luauLoaded);
        if (newSolver)
        {
            // Whose nonstrict mode says nothing of a key not there.
            ALLuauConfig config;
            config.mode = "strict";
            luau.setConfig(config);
        }
        const std::string made = fixed("ll.OwnerSya(\"hi\")\n", true, "LuauKeyNotFoundDidYouMean", "Change 'OwnerSya' to 'OwnerSay'");
        ensure_equals("the call", made, std::string("ll.OwnerSay(\"hi\")\n"));
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("an SLua global spelt wrong is changed to the nearest name in scope");
        ensure("definitions: " + error, luauLoaded);
        const ALScriptProblems problems = check("pirnt(\"hi\")\n", true);
        const ALScriptProblem* problem  = nullptr;
        for (const ALScriptProblem& each : problems)
        {
            if (each.key.find("UnknownGlobal") != std::string::npos)
            {
                problem = &each;
            }
        }
        ensure("said: " + said(problems), problem != nullptr);
        const std::string made = fixed("pirnt(\"hi\")\n", true, problem->key, "Change 'pirnt' to 'print'");
        ensure_equals("the call", made, std::string("print(\"hi\")\n"));
        const std::string local = fixed("local count = 1\nprint(coutn)\n", true, problem->key, "Change 'coutn' to 'count'");
        ensure_equals("a local", local, std::string("local count = 1\nprint(count)\n"));
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("a deprecated SLua call is replaced by what the definitions say to use, never as safe");
        ensure("definitions: " + error, luauLoaded);
        const ALScriptProblems problems = check("local n = ll.Abs(-1)\nprint(n)\n", true);
        const ALScriptProblem* problem  = nullptr;
        for (const ALScriptProblem& each : problems)
        {
            if (each.code == "DeprecatedApi")
            {
                problem = &each;
            }
        }
        ensure("said: " + said(problems), problem != nullptr);
        ensure("a fix: " + said(problems), !problem->fixes.empty());
        ensure("not safe", !problem->fixes.front().safe);
        const std::optional<std::string> made = ALScriptFixes::apply("local n = ll.Abs(-1)\nprint(n)\n", problem->fixes.front());
        ensure("applies", made.has_value());
        const ALScriptProblems after = check(*made, true);
        for (const ALScriptProblem& each : after)
        {
            ensure("no longer deprecated:\n" + *made + "\n" + said(after), each.code != "DeprecatedApi");
        }
        ensure_equals("no errors:\n" + *made + "\n" + said(after), errors(after), static_cast<size_t>(0));
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("edits are made in the text's order, and overlapping ones are refused");
        ALScriptFix fix;
        fix.edits.push_back({ 1, 0, 1, 3, "BAR" });
        fix.edits.push_back({ 0, 0, 0, 0, ">" });
        ensure_equals("both", ALScriptFixes::apply("foo\nbar\n", fix).value_or("refused"), std::string(">foo\nBAR\n"));

        fix.edits.push_back({ 1, 2, 1, 3, "x" });
        ensure("overlap refused", !ALScriptFixes::apply("foo\nbar\n", fix).has_value());

        ALScriptFix past;
        past.edits.push_back({ 0, 4, 0, 4, "!" });
        ensure("past the line's end refused", !ALScriptFixes::apply("foo\nbar\n", past).has_value());
        ALScriptFix at_end;
        at_end.edits.push_back({ 0, 3, 0, 3, ";" });
        ensure_equals("at the line's end", ALScriptFixes::apply("foo\nbar\n", at_end).value_or("refused"), std::string("foo;\nbar\n"));
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("NOLINT and NOLINTNEXTLINE suppress lints by name, bare or listed, and never an error");
        ALScriptProblem lint;
        lint.severity = ALScriptProblem::Severity::Warning;
        lint.source   = ALScriptProblem::Source::Lint;
        lint.code     = "LocalUnused";
        ensure("named", ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(LocalUnused)", "", true));
        ensure("among others", ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(ShadowLocal, LocalUnused)", "", true));
        ensure("bare", ALScriptFixes::suppressed(lint, "local x = 1 --[[ NOLINT ]]", "", true));
        ensure("another name", !ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT(ShadowLocal)", "", true));
        ensure("the line before", ALScriptFixes::suppressed(lint, "local x = 1", "-- NOLINTNEXTLINE(LocalUnused)", true));
        ensure("NOLINT on the line before is not about this one", !ALScriptFixes::suppressed(lint, "local x = 1", "-- NOLINT(LocalUnused)", true));
        ensure("not in a string", !ALScriptFixes::suppressed(lint, "local x = \"-- NOLINT\"", "", true));
        ensure("not in a long string", !ALScriptFixes::suppressed(lint, "local x = [[ -- NOLINT ]]", "", true));
        ensure("not a longer word", !ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINTING", "", true));
        lint.severity = ALScriptProblem::Severity::Error;
        ensure("a lint made an error, still", ALScriptFixes::suppressed(lint, "local x = 1 -- NOLINT", "", true));

        ALScriptProblem type;
        type.severity = ALScriptProblem::Severity::Error;
        type.source   = ALScriptProblem::Source::Types;
        ensure("a type error, never", !ALScriptFixes::suppressed(type, "local x: number = \"a\" -- NOLINT", "", true));

        ALScriptProblem lsl;
        lsl.severity = ALScriptProblem::Severity::Warning;
        lsl.source   = ALScriptProblem::Source::Lint;
        lsl.code     = "20009";
        lsl.key      = "LSLDeclaredButNotUsed";
        ensure_equals("its name", ALScriptFixes::lintName(lsl, false), std::string("DeclaredButNotUsed"));
        ensure("by name", ALScriptFixes::suppressed(lsl, "integer x; // NOLINT(DeclaredButNotUsed)", "", false));
        ensure("by number", ALScriptFixes::suppressed(lsl, "integer x; /* NOLINT(20009) */", "", false));
        ensure("not in a string", !ALScriptFixes::suppressed(lsl, "string s = \"// NOLINT\";", "", false));
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("the suppression goes in a NOLINT already there, the comment the line ends in, or one of its own");
        ALScriptProblem lint;
        lint.severity = ALScriptProblem::Severity::Warning;
        lint.source   = ALScriptProblem::Source::Lint;
        lint.code     = "LocalUnused";
        auto made = [&lint](const std::string& line, bool lua) {
            const std::optional<ALScriptFix> fix = ALScriptFixes::suppression(lint, line, lua);
            ensure("a fix for: " + line, fix.has_value());
            ensure("a suppression", fix->kind == ALScriptFix::Kind::Suppress && !fix->preferred && fix->safe);
            const std::string after = ALScriptFixes::apply(line, *fix).value_or("refused");
            ensure("suppresses: " + after, ALScriptFixes::suppressed(lint, after, "", lua));
            return after;
        };
        ensure_equals("its own", made("local x = 1", true), std::string("local x = 1  -- NOLINT(LocalUnused)"));
        ensure_equals("the line's comment", made("local x = 1 -- why", true), std::string("local x = 1 -- why NOLINT(LocalUnused)"));
        ensure_equals("a NOLINT's list", made("local x = 1 -- NOLINT(ShadowLocal)", true), std::string("local x = 1 -- NOLINT(ShadowLocal, LocalUnused)"));
        ensure_equals("after a block comment", made("local x = 1 --[[ why ]]", true), std::string("local x = 1 --[[ why ]]  -- NOLINT(LocalUnused)"));
        ensure("nothing where a bare NOLINT says it", !ALScriptFixes::suppression(lint, "local x = 1 -- NOLINT", true).has_value());

        lint.code = "20009";
        lint.key  = "LSLDeclaredButNotUsed";
        ensure_equals("LSL", made("    integer x;", false), std::string("    integer x;  // NOLINT(DeclaredButNotUsed)"));
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("an LSL value of the wrong type is cast to the right one, where LSL casts it");
        ensure("builtins: " + error, lslLoaded);
        std::string made = fixed("default\n{\n    state_entry()\n    {\n        llOwnerSay(5 + 1);\n    }\n}\n", false, "LSLArgumentWrongType",
                                 "Cast to string");
        ensure("the argument: " + made, made.find("llOwnerSay((string)(5 + 1));") != std::string::npos);
        made = fixed("default\n{\n    state_entry()\n    {\n        llSetText(\"a\", <1, 1, 1>, \"1\");\n    }\n}\n", false, "LSLArgumentWrongType",
                     "Cast to float");
        ensure("the third, past a vector's commas: " + made, made.find("<1, 1, 1>, (float)\"1\")") != std::string::npos);
        made = fixed("default\n{\n    state_entry()\n    {\n        string s = 5;\n        llOwnerSay(s);\n    }\n}\n", false,
                     "LSLWrongTypeInAssignment", "Cast to string");
        ensure("the declaration: " + made, made.find("string s = (string)5;") != std::string::npos);
        made = fixed("default\n{\n    state_entry()\n    {\n        string s;\n        s = 6;\n        llOwnerSay(s);\n    }\n}\n", false,
                     "LSLInvalidOperator", "Cast to string");
        ensure("the assignment: " + made, made.find("s = (string)6;") != std::string::npos);
        made = fixed("string f()\n{\n    return 5;\n}\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay(f());\n    }\n}\n", false,
                     "LSLBadReturnType", "Cast to string");
        ensure("the return: " + made, made.find("return (string)5;") != std::string::npos);
        // No cast LSL has: a list is no vector.
        const ALScriptProblems problems = check("default\n{\n    state_entry()\n    {\n        llSetPos([1]);\n    }\n}\n", false);
        const ALScriptProblem* none     = keyed(problems, "LSLArgumentWrongType");
        ensure("said: " + said(problems), none != nullptr && none->fixes.empty());
    }

    template<> template<>
    void object::test<12>()
    {
        set_test_name("the LSL warnings that say what to write are written so");
        ensure("builtins: " + error, lslLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n        integer a = 2;\n";
        const std::string tail = "        llOwnerSay((string)a);\n    }\n}\n";
        std::string made = fixed(head + "        a == 3;\n" + tail, false, "LSLEqAsStatement", "Assign with '='");
        ensure("assigned: " + made, made.find("        a = 3;\n") != std::string::npos);
        made = fixed(head + "        a *= 1.5;\n" + tail, false, "LSLIntFloatMulAssign", "Multiply as a float, then cast to integer");
        ensure("rewritten: " + made, made.find("a = (integer)(a * 1.5);") != std::string::npos);
        made = fixed(head + "        state default;\n" + tail, false, "LSLChangeToCurrentState", "Write 'return' instead");
        ensure("returned: " + made, made.find("        return;\n") != std::string::npos);

        // An assignment used as a condition: either was meant, so neither
        // is preferred, and the brackets change nothing.
        const std::string      script   = head + "        if (a = 1) llOwnerSay(\"one\");\n" + tail;
        const ALScriptProblems problems = check(script, false);
        const ALScriptProblem* problem  = keyed(problems, "LSLAssignmentInComparison");
        ensure("said: " + said(problems), problem != nullptr && problem->fixes.size() >= 2);
        ensure("neither preferred", !problem->fixes[0].preferred && !problem->fixes[1].preferred);
        ensure_equals("compared", ALScriptFixes::apply(script, problem->fixes[0]).value_or("").find("if (a == 1)") != std::string::npos, true);
        ensure("bracketed, safely", problem->fixes[1].safe && ALScriptFixes::apply(script, problem->fixes[1]).value_or("").find("if ((a = 1))") != std::string::npos);
        const std::optional<std::string> quiet = ALScriptFixes::apply(script, problem->fixes[1]);
        ensure("and quiet after", quiet && keyed(check(*quiet, false), "LSLAssignmentInComparison") == nullptr);
    }

    template<> template<>
    void object::test<13>()
    {
        set_test_name("the SLua fixes read off what the checker marks");
        ensure("definitions: " + error, luauLoaded);
        std::string made = fixed("--!strict\nlocal n = 5\nlocal s: string = n\nprint(s)\n", true, "LuauTypeMismatch", "Wrap in tostring()");
        ensure("the value a declaration is given: " + made, made.find("local s: string = tostring(n)") != std::string::npos);
        made = fixed("--!strict\nlocal t = {}\nfunction t:m() return self end\nprint(t.m())\n", true, "LuauRequiresSelf", "Call with ':'");
        ensure("with a colon: " + made, made.find("print(t:m())") != std::string::npos);
        made = fixed("local function f()\n    counter = 1\n    return counter\nend\nprint(f())\n", true, "LuauLintGlobalUsedAsLocalFunction",
                     "Make 'counter' local");
        ensure("local: " + made, made.find("    local counter = 1\n") != std::string::npos);
        made = fixed("--!strict\nlocal x\nprint(x)\n", true, "LuauLintUninitializedLocal", "Initialize 'x' with nil");
        ensure("given nil: " + made, made.find("local x = nil\n") != std::string::npos);
        made = fixed("--!nonstrickt\nprint(1)\n", true, "LuauLintDirectiveUnknownDidYouMean", "Change 'nonstrickt' to 'nonstrict'");
        ensure("spelt: " + made, made.compare(0, 12, "--!nonstrict") == 0);
    }

    template<> template<>
    void object::test<14>()
    {
        set_test_name("an LSL declaration nothing uses is taken out whole, and a local only where what it is given does nothing");
        ensure("builtins: " + error, lslLoaded);
        const std::string state = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"x\");\n    }\n}\n";
        std::string made = fixed("integer unused = 5;\n" + state, false, "LSLDeclaredButNotUsed", "Remove 'unused'");
        ensure_equals("the global's line gone", made, state);
        made = fixed("integer helper(integer x)\n{\n    return x;\n}\n" + state, false, "LSLDeclaredButNotUsed", "Remove 'helper'");
        ensure_equals("the function's lines gone", made, state);
        made = fixed("default\n{\n    state_entry()\n    {\n        integer a = 1 + 2;\n        llOwnerSay(\"x\");\n    }\n}\n", false,
                     "LSLDeclaredButNotUsed", "Remove 'a'");
        ensure_equals("the local's line gone", made, state);
        const ALScriptProblem* problem = nullptr;
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        integer h = llListen(0, \"\", NULL_KEY, \"\");\n    }\n}\n", false);
        for (const ALScriptProblem& each : problems)
        {
            problem = each.key == "LSLDeclaredButNotUsed" ? &each : problem;
        }
        ensure("said: " + said(problems), problem != nullptr);
        ensure("kept where what it is given does something", problem->fixes.empty());
        // One that shares its line keeps the line.
        ALScriptProblem shared;
        shared.line = 0;
        ALScriptFixes::offerRemoval(shared, "integer a = 1; integer b = 2;\n", 0, 8, 0, 13, "a");
        ensure_equals("only itself, and the space after it", ALScriptFixes::apply("integer a = 1; integer b = 2;\n", shared.fixes.front()).value_or(""),
                      std::string("integer b = 2;\n"));
        // The last on its line takes the space before it instead.
        ALScriptProblem last;
        ALScriptFixes::offerRemoval(last, "integer a = 1; integer b = 2;\n", 0, 23, 0, 28, "b");
        ensure_equals("and the space before it", ALScriptFixes::apply("integer a = 1; integer b = 2;\n", last.fixes.front()).value_or(""),
                      std::string("integer a = 1;\n"));
    }

    template<> template<>
    void object::test<15>()
    {
        set_test_name("what the optimizer did is offered as a change to the source, where the source says what it read");
        ensure("builtins: " + error, lslLoaded);
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
        ensure("a fold offered: " + notes, folded != nullptr);
        ensure("never preferred: the source is the scripter's", !folded->fixes.front().preferred);
        std::string made = ALScriptFixes::apply(script, folded->fixes.front()).value_or("refused");
        ensure("written in: " + made, made.find("llOwnerSay(\"3\");") != std::string::npos);
        ensure("what can never run offered: " + notes, unreachable != nullptr && unreachable->fixes.front().preferred && unreachable->fixes.front().safe);
        made = ALScriptFixes::apply(script, unreachable->fixes.front()).value_or("refused");
        ensure("gone: " + made, made.find("never") == std::string::npos && made.find("        return;\n    }\n") != std::string::npos);
    }

    template<> template<>
    void object::test<16>()
    {
        set_test_name("an LSL expression chosen goes into a local just before its statement, where that runs it as it ran");
        ensure("builtins: " + error, lslLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        std::string       made = actedOn(head + "        llOwnerSay((string)(llGetUnixTime() + 5));\n" + tail, false, "llGetUnixTime() + 5", true,
                                         "Put it in a local, 'value'");
        ensure_equals("on its own line", made,
                      head + "        integer value = llGetUnixTime() + 5;\n        llOwnerSay((string)(value));\n" + tail);
        // After another statement on the line, just before its own; and
        // named as nothing in the script is.
        made = actedOn(head + "        integer value = 1; llOwnerSay((string)(value * 2.5));\n" + tail, false, "value * 2.5", true,
                       "Put it in a local, 'value2'");
        ensure_equals("beside", made, head + "        integer value = 1; float value2 = value * 2.5; llOwnerSay((string)(value2));\n" + tail);
        made = actedOn(head + "        llSetRot(llEuler2Rot(<0, 0, 1>));\n" + tail, false, "llEuler2Rot(<0, 0, 1>)", true, "Put it in a local, 'value'");
        ensure("a rotation as LSL says it: " + made, made.find("rotation value = llEuler2Rot(<0, 0, 1>);") != std::string::npos);
        // Not out of a loop's condition, nor from under an if with no
        // braces, nor what is assigned to.
        std::string none = head + "        integer i;\n        while (i < 10) ++i;\n" + tail;
        ensure("a condition: " + actedOn(none, false, "i < 10", true, "Put it in a local, 'value'"),
               actedOn(none, false, "i < 10", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        none = head + "        integer i;\n        if (i) llOwnerSay((string)(i + 1));\n" + tail;
        ensure("under an if", actedOn(none, false, "i + 1", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        none = head + "        integer i;\n        i = 4;\n        llOwnerSay((string)i);\n" + tail;
        S32 line = 0, column = 0, end = 0;
        place(none, "i = 4", line, column, end);
        ensure("assigned to", acted(none, false, line, column, line, column + 1, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        // Nor the whole of a statement, which would leave `value;`.
        none = head + "        llOwnerSay((string)llGetUnixTime());\n" + tail;
        ensure("the whole statement", actedOn(none, false, "llOwnerSay((string)llGetUnixTime())", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
    }

    template<> template<>
    void object::test<17>()
    {
        set_test_name("an LSL if with an else is inverted: the condition turned round, the branches swapped");
        ensure("builtins: " + error, lslLoaded);
        const std::string head = "default\n{\n    touch_start(integer n)\n    {\n";
        const std::string tail = "    }\n}\n";
        std::string       made = actedOn(head + "        if (n == 1)\n        {\n            llOwnerSay(\"one\");\n        }\n        else\n        {\n"
                                                "            llOwnerSay(\"many\");\n        }\n" + tail,
                                         false, "if", false, "Invert the if");
        ensure_equals("braces", made,
                      head + "        if (n != 1)\n        {\n            llOwnerSay(\"many\");\n        }\n        else\n        {\n"
                             "            llOwnerSay(\"one\");\n        }\n" + tail);
        made = actedOn(head + "        if (!n) llOwnerSay(\"none\"); else llOwnerSay(\"some\");\n" + tail, false, "if", false, "Invert the if");
        ensure_equals("a not taken off", made, head + "        if (n) llOwnerSay(\"some\"); else llOwnerSay(\"none\");\n" + tail);
        made = actedOn(head + "        if (n > 2) llOwnerSay(\"many\"); else llOwnerSay(\"few\");\n" + tail, false, "if", false, "Invert the if");
        ensure_equals("an order under a not", made, head + "        if (!(n > 2)) llOwnerSay(\"few\"); else llOwnerSay(\"many\");\n" + tail);
        const std::string chain = head + "        if (n == 1) llOwnerSay(\"one\"); else if (n == 2) llOwnerSay(\"two\");\n" + tail;
        ensure("not with an else if", actedOn(chain, false, "if", false, "Invert the if").rfind("offered", 0) == 0);
        const std::string lone = head + "        if (n == 1) llOwnerSay(\"one\");\n" + tail;
        ensure("not without an else", actedOn(lone, false, "if", false, "Invert the if").rfind("offered", 0) == 0);
        // An else branch ending in an if of its own goes first in braces,
        // or the else would come to be that if's.
        made = actedOn(head + "        if (n) llOwnerSay(\"a\"); else while (n--) if (n == 3) llOwnerSay(\"c\");\n" + tail, false, "if", false,
                       "Invert the if");
        ensure_equals("braced", made, head + "        if (!n) { while (n--) if (n == 3) llOwnerSay(\"c\"); } else llOwnerSay(\"a\");\n" + tail);
    }

    template<> template<>
    void object::test<18>()
    {
        set_test_name("a handler is offered for an event the LSL state asks for and does not hear, laid out as its others are");
        ensure("builtins: " + error, lslLoaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llListen(0, \"\", NULL_KEY, \"\");\n        llSetTimerEvent(1.0);\n"
                                   "    }\n}\n";
        std::string made = actedOn(script, false, "llListen", false, "Add a handler for 'listen'");
        ensure_equals("before the brace, after a blank line", made,
                      "default\n{\n    state_entry()\n    {\n        llListen(0, \"\", NULL_KEY, \"\");\n        llSetTimerEvent(1.0);\n    }\n\n"
                      "    listen(integer Channel, string Name, key ID, string Text)\n    {\n    }\n}\n");
        ensure("the timer too", actions(script, false, 4, 8, 4, 8).size() == 2);
        // Heard already: nothing.
        const std::string heard = "default\n{\n    state_entry() {\n        llSetTimerEvent(1.0);\n    }\n\n    timer() {\n    }\n}\n";
        ensure("heard: " + titles(actions(heard, false, 3, 8, 3, 8)), actions(heard, false, 3, 8, 3, 8).empty());
        // The brace where the state's first handler has it.
        const std::string same = "default\n{\n    state_entry() {\n        llSensorRepeat(\"\", NULL_KEY, AGENT, 10.0, PI, 5.0);\n    }\n}\n";
        made = actedOn(same, false, "llSensorRepeat", false, "Add a handler for 'sensor'");
        ensure("on its line: " + made, made.find("\n\n    sensor(integer NumberDetected) {\n    }\n}\n") != std::string::npos);
        // Asked for from a function a handler calls, and from one that one
        // calls: offered all the same.
        const std::string helper = "arm()\n{\n    llSetTimerEvent(1.0);\n}\nstart()\n{\n    arm();\n}\n"
                                   "default\n{\n    state_entry()\n    {\n        start();\n    }\n}\n";
        made = actedOn(helper, false, "start();\n    }", false, "Add a handler for 'timer'");
        ensure("through two calls: " + made, made.find("    timer()\n    {\n    }\n") != std::string::npos);
    }

    template<> template<>
    void object::test<19>()
    {
        set_test_name("a SLua expression chosen goes into a local, where that runs it as surely as it ran");
        ensure("definitions: " + error, luauLoaded);
        std::string made = actedOn("local function f(n: number)\n    print(n * 2 + 1)\nend\nf(1)\n", true, "n * 2", true, "Put it in a local, 'value'");
        ensure_equals("before the statement", made, "local function f(n: number)\n    local value = n * 2\n    print(value + 1)\nend\nf(1)\n");
        made = actedOn("local x = 1; print(x + 1)\n", true, "x + 1", true, "Put it in a local, 'value'");
        ensure_equals("beside", made, "local x = 1; local value = x + 1; print(value)\n");
        const std::string skipped = "local x = 1\nprint(x > 0 and tostring(x))\n";
        ensure("not what and may skip", actedOn(skipped, true, "tostring(x)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        const std::string looped = "local x = 1\nwhile x < 10 do\n    x += 1\nend\n";
        ensure("not a loop's condition", actedOn(looped, true, "x < 10", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        const std::string elseif = "local x = 1\nif x == 0 then\n    print(0)\nelseif x + 1 == 2 then\n    print(1)\nend\n";
        ensure("not an elseif's condition", actedOn(elseif, true, "x + 1", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        // Not a call that is the whole statement: `value` alone is no
        // statement.
        const std::string whole = "print(math.abs(-1))\n";
        ensure("not the whole statement", actedOn(whole, true, "print(math.abs(-1))", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        ensure("its argument yes: " + actedOn(whole, true, "math.abs(-1)", true, "Put it in a local, 'value'"),
               actedOn(whole, true, "math.abs(-1)", true, "Put it in a local, 'value'") == "local value = math.abs(-1)\nprint(value)\n");
        // Not a call where all it gives is kept, since a local keeps one.
        const std::string two = "local function g(n: number) return n, 2 end\n";
        ensure("not a last argument", actedOn(two + "print(g(1))\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        ensure("not a last return", actedOn(two + "local function h() return 0, g(1) end\nh()\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        ensure("not a table's last item", actedOn(two + "local t = { g(1) }\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        ensure("not to two names", actedOn(two + "local a, b = g(1)\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        ensure("not a for-in's values", actedOn("for k, v in pairs({}) do end\n", true, "pairs({})", true, "Put it in a local, 'value'").rfind("offered", 0) == 0);
        // Where only the first is kept anyway, yes.
        ensure("an argument before the last: " + actedOn(two + "print(g(1), 3)\n", true, "g(1)", true, "Put it in a local, 'value'"),
               actedOn(two + "print(g(1), 3)\n", true, "g(1)", true, "Put it in a local, 'value'") == two + "local value = g(1)\nprint(value, 3)\n");
        ensure("to one name: " + actedOn(two + "local a = g(1)\n", true, "g(1)", true, "Put it in a local, 'value'"),
               actedOn(two + "local a = g(1)\n", true, "g(1)", true, "Put it in a local, 'value'").rfind("offered", 0) != 0);
    }

    template<> template<>
    void object::test<20>()
    {
        set_test_name("a SLua if is inverted, a concatenation interpolated, a local given the type it has");
        ensure("definitions: " + error, luauLoaded);
        std::string made = actedOn("local x = 1\nif x == 1 then\n    print(\"one\")\nelse\n    print(\"other\")\nend\n", true, "if", false, "Invert the if");
        ensure_equals("inverted", made, "local x = 1\nif x ~= 1 then\n    print(\"other\")\nelse\n    print(\"one\")\nend\n");
        made = actedOn("local x = 1\nif not (x > 1) then\n    print(\"few\")\nelse\n    print(\"many\")\nend\n", true, "if", false, "Invert the if");
        ensure_equals("a not taken off", made, "local x = 1\nif x > 1 then\n    print(\"many\")\nelse\n    print(\"few\")\nend\n");
        made = actedOn("local x = 1\nprint(\"n = \" .. x .. \"!\")\n", true, "..", false, "Write it as an interpolated string");
        ensure_equals("interpolated", made, "local x = 1\nprint(`n = {x}!`)\n");
        made = actedOn("local count = 5\nprint(count)\n", true, "count", false, "Declare it as 'number'");
        ensure_equals("annotated", made, "local count: number = 5\nprint(count)\n");
    }

    template<> template<>
    void object::test<21>()
    {
        set_test_name("a handler is offered for an event a SLua script asks for and does not hear, with the parameters the definitions give");
        ensure("definitions: " + error, luauLoaded);
        const std::string script = "ll.Listen(0, \"\", ll.GetOwner(), \"\")\n";
        std::string       made   = actedOn(script, true, "Listen", false, "Add a handler for 'listen'");
        ensure_equals("at the end", made,
                      script + "\nLLEvents:on(\"listen\", function(Channel: number, Name: string, ID: uuid, Text: string)\nend)\n");
        const std::string heard = script + "LLEvents:on(\"listen\", function(channel, name, id, text) end)\n";
        ensure("heard: " + titles(actions(heard, true, 0, 3, 0, 3)), actedOn(heard, true, "Listen", false, "Add a handler for 'listen'").rfind("offered", 0) == 0);
        // A handler taken off is no handler put on.
        const std::string off = script + "local function said(channel, name, id, text) end\nLLEvents:off(\"listen\", said)\n";
        ensure("offered where only taken off: " + titles(actions(off, true, 0, 3, 0, 3)),
               actedOn(off, true, "Listen", false, "Add a handler for 'listen'").rfind("offered", 0) != 0);
    }

    template<> template<>
    void object::test<22>()
    {
        set_test_name("a refactor's name is one the text has nowhere, and one put in at a line's start is mapped to the source line it began as");
        ensure_equals("free", ALScriptFixes::freshName("local x = 1", "value"), std::string("value"));
        ensure_equals("taken", ALScriptFixes::freshName("local value = 1 -- value2", "value"), std::string("value3"));
        ensure_equals("a part is not a word", ALScriptFixes::freshName("local values = 1", "value"), std::string("value"));
        // A line the preprocessor carries as it stands, a line down for the
        // directive it drops.
        ALPreprocessor::Options options;
        options.fileName = "main.lsl";
        options.resolve  = [](const ALPreprocessor::Ask&, ALPreprocessor::Include&) { return ALPreprocessor::Found::No; };
        const std::string                  script = "#define N 2\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay((string)N);\n    }\n}\n";
        const ALPreprocessor::Result       result = ALPreprocessor::run(script, options);
        ALScriptProblem                    held;
        const ALSourceMap::Loc             carried = result.map.toExpanded(0, 5, 8);
        ensure("carried", carried.found());
        held.fixes.push_back(ALScriptFixes::titled("ScriptActionExtract", "Put it in a local, '[1]'", { "value" }));
        held.fixes.back().edits.push_back({ carried.line, 0, carried.line, 0, "        integer value = 1;\n" });
        ALScriptFixes::mapThrough(result.map, held);
        ensure_equals("kept", held.fixes.size(), static_cast<size_t>(1));
        ensure_equals("the source's line", held.fixes.front().edits.front().line, 5);
        ensure_equals("its start", held.fixes.front().edits.front().column, 0);
    }

    template<> template<>
    void object::test<23>()
    {
        set_test_name("a module's exports are the names of the table it returns, however it builds it");
        const auto listed = [](const std::string& source) {
            std::string out;
            for (const std::string& name : ALLuauExports::of(source))
            {
                out += (out.empty() ? "" : " ") + name;
            }
            return out;
        };
        ensure_equals("a table returned", listed("local function greet() end\nreturn { greet = greet, count = 3, [\"spaced out\"] = 1, 4 }\n"),
                      std::string("greet count"));
        ensure_equals("a local table filled in",
                      listed("local M = { count = 3 }\nfunction M.greet() end\nfunction M:reset() end\nM.name = \"util\"\nM[\"size\"] = 2\n"
                             "local other = {}\nother.hidden = 1\nreturn M\n"),
                      std::string("count greet reset name size"));
        ensure_equals("through setmetatable", listed("local M = {}\nM.x = 1\nreturn setmetatable(M, { __index = M })\n"), std::string("x"));
        ensure_equals("each once", listed("local M = {}\nM.x = 1\nM.x = 2\nreturn M\n"), std::string("x"));
        ensure_equals("a function: no names", listed("return function() end\n"), std::string());
        ensure_equals("nothing returned", listed("local M = {}\nM.x = 1\n"), std::string());
        ensure_equals("a module that does not parse", listed("local M = {\nreturn M\n"), std::string());
        ensure_equals("not a keyword", listed("return { [\"end\"] = 1, ok = 2 }\n"), std::string("ok"));
    }

    template<> template<>
    void object::test<24>()
    {
        set_test_name("a require goes after the requires a script opens with, else after its comments, apart from the code");
        ALScriptProblem problem;
        problem.key  = "LuauUnknownGlobal";
        problem.args = { "util" };
        const auto made = [&problem](const std::string& text, const std::string& module, bool field) {
            problem.fixes.clear();
            ALScriptFixes::offerRequire(problem, text, module, field);
            return problem.fixes.empty() ? std::string("none") : ALScriptFixes::apply(text, problem.fixes.front()).value_or("refused");
        };
        ensure_equals("after the last require", made("--!strict\nlocal a = require(\"a\")\nlocal b = require(\"b\").b\n\nprint(util.x)\n", "util", false),
                      std::string("--!strict\nlocal a = require(\"a\")\nlocal b = require(\"b\").b\nlocal util = require(\"util\")\n\nprint(util.x)\n"));
        ensure_equals("its words", problem.fixes.front().title, std::string("Require 'util'"));
        ensure_equals("after the comments, the blank line kept",
                      made("--!strict\n--[[ A header\n  over lines ]]\n\nprint(util.x)\n", "@lib/util", false),
                      std::string("--!strict\n--[[ A header\n  over lines ]]\nlocal util = require(\"@lib/util\")\n\nprint(util.x)\n"));
        ensure_equals("apart from code that follows at once", made("print(util)\n", "helpers", true),
                      std::string("local util = require(\"helpers\").util\n\nprint(util)\n"));
        ensure_equals("a field's words", problem.fixes.front().title, std::string("Take 'util' from 'helpers'"));
        ensure_equals("past a last line with no break", made("-- only a comment", "util", false),
                      std::string("-- only a comment\nlocal util = require(\"util\")"));
        ensure_equals("a name quoted", made("print(util)\n", "a\"b", false), std::string("local util = require(\"a\\\"b\")\n\nprint(util)\n"));
        problem.fixes.clear();
        ALScriptFixes::offerRequire(problem, "print(util)\n", "util", false);
        ALScriptFixes::offerRequire(problem, "print(util)\n", "util", false);
        ensure_equals("offered once", problem.fixes.size(), static_cast<size_t>(1));
        ensure("neither preferred nor safe", !problem.fixes.front().preferred && !problem.fixes.front().safe);
    }

    template<> template<>
    void object::test<25>()
    {
        set_test_name("an LSL include declares its macros and the functions and globals at its top, a fragment and all");
        const auto listed = [](const std::string& source) {
            std::string out;
            for (const std::string& name : ALLSLExports::of(source))
            {
                out += (out.empty() ? "" : " ") + name;
            }
            return out;
        };
        ensure_equals("everything at the top",
                      listed("// helpers\n#define CHANNEL -42\n#define say(x) \\\n    llOwnerSay(x)\n#ifdef DEBUG\n#include \"more.lsl\"\n#endif\n"
                             "integer gCount = 0;\nlist gSeen;\nvector gAt = <1, 2, 3>;\n"
                             "string greet(string name)\n{\n    integer inner = 1;\n    return \"hi \" + name;\n}\n"
                             "reset() { gCount = 0; }\ninline integer twice(integer n) { return n * 2; }\n"),
                      std::string("CHANNEL say gCount gSeen gAt greet reset twice"));
        ensure_equals("a state holds no declarations",
                      listed("integer shared;\ndefault\n{\n    state_entry()\n    {\n        integer local = 1;\n    }\n}\nstate other { touch_start(integer n) {} }\n"),
                      std::string("shared"));
        ensure_equals("each once", listed("#define A 1\n#undef A\n#define A 2\n"), std::string("A"));
        ensure_equals("SLua is no LSL", listed("local function greet() end\nprint(greet())\nreturn { greet = greet }\n"), std::string());
    }

    template<> template<>
    void object::test<26>()
    {
        set_test_name("an include goes after the directives an LSL script opens with, else after its comments, apart from the code");
        ALScriptProblem problem;
        problem.key  = "LSLUndeclared";
        problem.args = { "greet" };
        const auto made = [&problem](const std::string& text, const std::string& include) {
            problem.fixes.clear();
            ALScriptFixes::offerInclude(problem, text, include);
            return problem.fixes.empty() ? std::string("none") : ALScriptFixes::apply(text, problem.fixes.front()).value_or("refused");
        };
        ensure_equals("after the last directive",
                      made("// Hello\n#include \"a.lsl\"\n#define LONG \\\n    1\n\ndefault {}\n", "helpers.lsl"),
                      std::string("// Hello\n#include \"a.lsl\"\n#define LONG \\\n    1\n#include \"helpers.lsl\"\n\ndefault {}\n"));
        ensure_equals("its words", problem.fixes.front().title, std::string("Include 'helpers.lsl'"));
        ensure_equals("after the comments, the blank line kept", made("/* A header\n   over lines */\n\ndefault {}\n", "helpers"),
                      std::string("/* A header\n   over lines */\n#include \"helpers\"\n\ndefault {}\n"));
        ensure_equals("apart from code that follows at once", made("default {}\n", "helpers"), std::string("#include \"helpers\"\n\ndefault {}\n"));
        ensure_equals("no name with a quote in it", made("default {}\n", "a\"b"), std::string("none"));
        // What the studio offers them on: a function, a global and a macro
        // alike, each undeclared by its name.
        ensure("builtins: " + error, lslLoaded);
        const ALScriptProblems problems =
            check("default\n{\n    state_entry()\n    {\n        greet(\"x\");\n        llOwnerSay((string)gCount);\n        llSay(CHANNEL, \"\");\n    }\n}\n", false);
        for (const char* name : { "greet", "gCount", "CHANNEL" })
        {
            bool said_so = false;
            for (const ALScriptProblem& each : problems)
            {
                said_so = said_so || (each.key == "LSLUndeclared" && each.args.size() == 1 && each.args[0] == name);
            }
            ensure(std::string("undeclared: ") + name + "\n" + said(problems), said_so);
        }
    }

    template<> template<>
    void object::test<27>()
    {
        set_test_name("a fix made over the source is taken into the expansion where it lands on what the expansion copied, and not where a macro stood");
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
        ensure("a word the expansion copied", into(edited(5, 20, 5, 25, "hi"), out));
        std::string expected = result.text;
        expected.replace(expected.find("\"hello\""), 7, "\"hi\"");
        ensure_equals("replaced there alone", out, expected);

        ALScriptFix over_macro = edited(6, 27, 6, 28, "3");
        ensure("not where a macro stood", !ALScriptFixes::intoExpansion(result.map, over_macro));
        ensure_equals("and the fix as it was", over_macro.edits.front().line, 6);
        ensure("nor across lines unless whole", !into(edited(5, 8, 6, 8, ""), out));

        ensure("a line put in at a line's start", into(edited(7, 0, 7, 0, "        integer x;\n"), out));
        const size_t put = out.find("integer x;");
        ensure("before the line it was put before", put != std::string::npos && put < out.find("\"bye\"") && put > out.find("(string)"));
        ensure("whole lines taken out", into(edited(5, 0, 6, 0, ""), out));
        ensure("that line gone, the rest kept", out.find("hello") == std::string::npos && out.find("\"bye\"") != std::string::npos &&
                                                   out.find("(string)") != std::string::npos);
    }

    template<> template<>
    void object::test<28>()
    {
        set_test_name("a string written the same several times over goes into a global after the ones the script opens with, each use named");
        ensure("builtins: " + error, lslLoaded);
        const std::string script = "string gGreeting = \"hello there\";\n"
                                   "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"hello there\");\n"
                                   "        llSetText(\"hello there\", <1.0, 1.0, 1.0>, 1.0);\n    }\n"
                                   "    touch_start(integer n)\n    {\n        llOwnerSay(\"hello there\");\n        llOwnerSay(\"other\");\n    }\n}\n";
        const std::string made = actedOn(script, false, "\"hello there\");", false, "Put the string in a global, 'gHelloThere', for its 3 uses");
        ensure_equals("declared after the globals, each use named, the global's own value kept", made,
                      "string gGreeting = \"hello there\";\nstring gHelloThere = \"hello there\";\n"
                      "default\n{\n    state_entry()\n    {\n        llOwnerSay(gHelloThere);\n"
                      "        llSetText(gHelloThere, <1.0, 1.0, 1.0>, 1.0);\n    }\n"
                      "    touch_start(integer n)\n    {\n        llOwnerSay(gHelloThere);\n        llOwnerSay(\"other\");\n    }\n}\n");
        // A short one costs more as a global than it saves: the list says
        // so, weighing it. A long one, used as often, is lighter.
        ensure("heavier on LSO: " + std::to_string(ALScriptWeigh::lso(made).total) + " from " + std::to_string(ALScriptWeigh::lso(script).total),
               ALScriptWeigh::lso(made).total >= ALScriptWeigh::lso(script).total);
        std::string long_script = script;
        std::string long_made   = made;
        for (std::string* text : { &long_script, &long_made })
        {
            for (size_t at = text->find("hello there"); at != std::string::npos; at = text->find("hello there", at + 1))
            {
                text->replace(at, 11, "hello there, and a sentence of some length said to the owner");
            }
        }
        ensure("a long one lighter on LSO: " + std::to_string(ALScriptWeigh::lso(long_made).total) + " from " +
                   std::to_string(ALScriptWeigh::lso(long_script).total),
               ALScriptWeigh::lso(long_made).total < ALScriptWeigh::lso(long_script).total);
        ensure("not for a string used once", actedOn(script, false, "\"other\"", false, "Put the string in a global, 'gOther', for its 1 uses").rfind("offered", 0) == 0);

        // No globals to follow: ahead of what the script opens with.
        const std::string bare = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"hi\\n\");\n        llOwnerSay(\"hi\\n\");\n    }\n}\n";
        const std::string placed = actedOn(bare, false, "\"hi", false, "Put the string in a global, 'gHi', for its 2 uses");
        ensure("ahead of the state, a blank line after: " + placed, placed.rfind("string gHi = \"hi\\n\";\n\ndefault\n", 0) == 0);
    }

    template<> template<>
    void object::test<29>()
    {
        set_test_name("a list written out is written as a sum, each element meaning what it meant, and the sum bracketed where it is part of more");
        ensure("builtins: " + error, lslLoaded);
        const std::string script = "list gFixed = [1, 2];\n"
                                   "default\n{\n    state_entry()\n    {\n        integer n = 5;\n"
                                   "        list l = [n, \"two\", <3.0, 3.0, 3.0>, -4, n + 3, llGetListLength(gFixed)];\n"
                                   "        llOwnerSay((string)[1, 2]);\n    }\n}\n";
        std::string made = actedOn(script, false, "[n,", false, "Write the list as a sum");
        ensure_equals("each where it would mean what it meant", made,
                      "list gFixed = [1, 2];\n"
                      "default\n{\n    state_entry()\n    {\n        integer n = 5;\n"
                      "        list l = (list)n + \"two\" + <3.0, 3.0, 3.0> + (-4) + (n + 3) + llGetListLength(gFixed);\n"
                      "        llOwnerSay((string)[1, 2]);\n    }\n}\n");
        made = actedOn(script, false, "[1, 2]);", false, "Write the list as a sum");
        ensure("bracketed under a cast: " + made, made.find("llOwnerSay((string)((list)1 + 2));") != std::string::npos);
        ensure("not a global's value, which must be written out", actedOn(script, false, "[1, 2];", false, "Write the list as a sum").rfind("offered", 0) == 0);
    }
    template<> template<>
    void object::test<30>()
    {
        set_test_name("a fix that takes code out says so, and one that only renames does not, so that a save makes the one and not the other");
        ensure("builtins: " + error, lslLoaded);
        ensure("definitions: " + error, luauLoaded);
        const std::string state = "default\n{\n    state_entry()\n    {\n        llOwnerSay(\"x\");\n    }\n}\n";
        const ALScriptProblems lsl = check("integer helper(integer x)\n{\n    return x;\n}\n" + state, false);
        const ALScriptProblem* unused = keyed(lsl, "LSLDeclaredButNotUsed");
        ensure("said", unused != nullptr && !unused->fixes.empty());
        ensure("a removal is safe, and removes", unused->fixes.front().safe && unused->fixes.front().removes);
        const ALScriptProblems lua = check("local function f()\n    local unused = 1\nend\nf()\n", true);
        const ALScriptProblem* local = keyed(lua, "LuauLintLocalUnused");
        ensure("said: " + said(lua), local != nullptr && !local->fixes.empty());
        ensure("a rename is safe, and removes nothing", local->fixes.front().safe && !local->fixes.front().removes);
    }

    template<> template<>
    void object::test<31>()
    {
        set_test_name("a short name gets a guess only one edit away, a swap is one edit, and names as near as each other are all offered and none preferred");
        ensure("builtins: " + error, lslLoaded);
        ensure("definitions: " + error, luauLoaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        // One character: nothing but itself in another case; two edits
        // made PI of it once.
        ALScriptProblems       problems = check(head + "        llOwnerSay((string)n);\n" + tail, false);
        const ALScriptProblem* problem  = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), problem != nullptr);
        ensure("no guess at n: " + titles(problem->fixes), problem->fixes.empty());
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
        ensure("said: " + said(problems), problem != nullptr);
        ensure("no guess at xy two edits from ll: " + titles(problem->fixes), problem->fixes.empty());
        // Two as near: both, neither preferred.
        problems = check(head + "        integer cat = 1;\n        integer car = 2;\n        llOwnerSay((string)(cat + car + cax));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), problem != nullptr);
        ensure_equals("both: " + titles(problem->fixes), problem->fixes.size(), size_t(2));
        ensure("neither preferred", !problem->fixes[0].preferred && !problem->fixes[1].preferred);
        problems = check("local cat, car = 1, 2\nprint(cat, car, cax)\n", true);
        problem  = unknown(problems);
        ensure("said: " + said(problems), problem != nullptr);
        ensure("both in SLua: " + titles(problem->fixes), problem->fixes.size() == 2 && !problem->fixes[0].preferred && !problem->fixes[1].preferred);
        // One near a short name: offered, not preferred; one near a longer
        // name, preferred, a swap being one edit.
        problems = check(head + "        integer cat = 1;\n        llOwnerSay((string)(cat + cta));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), problem != nullptr && problem->fixes.size() == 1);
        ensure("a guess at three characters is not preferred", !problem->fixes.front().preferred);
        problems = check(head + "        integer total = 1;\n        llOwnerSay((string)(total + totla));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), problem != nullptr && problem->fixes.size() == 1 && problem->fixes.front().preferred);
        ensure_equals("swapped back", ALScriptFixes::editDistance("totla", "total"), size_t(1));
        // A name one edit past how far a guess may be is no guess, even
        // where it is the only one that near.
        problems = check(head + "        integer count = 1;\n        llOwnerSay((string)(count + cqunx));\n" + tail, false);
        problem  = keyed(problems, "LSLUndeclared");
        ensure("said: " + said(problems), problem != nullptr);
        ensure("two edits from a five-letter name: no guess: " + titles(problem->fixes), problem->fixes.empty());
    }

    template<> template<>
    void object::test<32>()
    {
        set_test_name("a fix's edits are made in the text's order, something put in at a place before a stretch replaced from there, whatever order it gives them");
        for (const bool insertion_first : { true, false })
        {
            ALScriptFix fix;
            const ALScriptEdit put{ 0, 0, 0, 0, "local v = print(x)\n" };
            const ALScriptEdit swap{ 0, 0, 0, 8, "v" };
            fix.edits = insertion_first ? std::vector<ALScriptEdit>{ put, swap } : std::vector<ALScriptEdit>{ swap, put };
            ensure_equals(insertion_first ? "put in first" : "replaced first", ALScriptFixes::apply("print(x)\n", fix).value_or("refused"),
                          std::string("local v = print(x)\nv\n"));
        }
        ALScriptFix two;
        two.edits = { { 0, 0, 0, 0, "a" }, { 0, 0, 0, 0, "b" } };
        ensure_equals("two at one place in the order given", ALScriptFixes::apply("x", two).value_or("refused"), std::string("abx"));
    }

    template<> template<>
    void object::test<33>()
    {
        set_test_name("the optimizer's removal, offered beside the analyzer's, takes the preferred mark off its own fix only");
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
        ensure_equals("nothing added", problem.fixes.size(), size_t(1));
        ensure("still preferred", problem.fixes.front().preferred);
        // Where it can, its own is added unpreferred.
        problem.line = problem.endLine = 0;
        problem.column                 = 8;
        problem.endColumn              = 9;
        ALScriptFixes::attachOptimizer(problem, "integer x;\n");
        ensure("added: " + std::to_string(problem.fixes.size()), problem.fixes.size() == 2 && !problem.fixes.back().preferred && problem.fixes.front().preferred);
    }

    template<> template<>
    void object::test<34>()
    {
        set_test_name("SLua written with LSL's habits -- != && || ! -- changed to SLua's words, only ~= safe; an index or a loop from 0 counted from 1");
        ensure_equals("~=", fixed("local a, b = 1, 2\nif a != b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '!=' to '~='"),
                      std::string("local a, b = 1, 2\nif a ~= b then print(1) end\n"));
        ensure_equals("and, apart from what it joins", fixed("local a, b = true, false\nif a&&b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '&&' to 'and'"),
                      std::string("local a, b = true, false\nif a and b then print(1) end\n"));
        ensure_equals("or", fixed("local a, b = true, false\nif a || b then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '||' to 'or'"),
                      std::string("local a, b = true, false\nif a or b then print(1) end\n"));
        ensure_equals("not, apart from its operand", fixed("local a = true\nif(!a) then print(1) end\n", true, "LuauUnexpectedDidYouMean", "Change '!' to 'not'"),
                      std::string("local a = true\nif(not a) then print(1) end\n"));
        const ALScriptProblems compared = check("local a, b = 1, 2\nif a != b then print(1) end\n", true);
        const ALScriptProblems denied   = check("local a = true\nif !a then print(1) end\n", true);
        const ALScriptProblem* unequal  = keyed(compared, "LuauUnexpectedDidYouMean");
        const ALScriptProblem* negated  = keyed(denied, "LuauUnexpectedDidYouMean");
        ensure("~= safe, the same whatever it compares", unequal && unequal->fixes.front().safe);
        ensure("not unsafe: 0 is true in SLua", negated && !negated->fixes.front().safe);

        ensure_equals("an index from 0", fixed("local t = {}\ntable.insert(t, 0, 5)\n", true, "LuauLintTableInsertZero", "Change '0' to '1'"),
                      std::string("local t = {}\ntable.insert(t, 1, 5)\n"));
        ensure_equals("a loop from 0", fixed("local t = {1, 2}\nfor i = 0, #t do print(t[i]) end\n", true, "LuauLintForRangeZero", "Change '0' to '1'"),
                      std::string("local t = {1, 2}\nfor i = 1, #t do print(t[i]) end\n"));
    }

    template<> template<>
    void object::test<35>()
    {
        set_test_name("LSL: an if ended by its ';', an event's parameters as it takes them, a parameter with no type, a declaration braced, a missing ';' safe");
        ensure_equals("the if's ';' out", fixed("default { state_entry() { integer x = llGetUnixTime(); if (x); llOwnerSay(\"a\"); } }", false, "LSLEmptyIf",
                                                "Take out the ';' that ends the if"),
                      std::string("default { state_entry() { integer x = llGetUnixTime(); if (x) llOwnerSay(\"a\"); } }"));
        ensure_equals("a wrong type, the name kept", fixed("default { touch_start(string n) { llOwnerSay((string)n); } }", false, "LSLArgumentWrongTypeEvent",
                                                          "Write the parameters 'touch_start' takes"),
                      std::string("default { touch_start(integer n) { llOwnerSay((string)n); } }"));
        ensure_equals("too few, the builtins' names", fixed("default { touch_start() { } }", false, "LSLTooFewArgumentsEvent", "Write the parameters 'touch_start' takes"),
                      std::string("default { touch_start(integer NumberOfTouches) { } }"));
        ensure_equals("too many", fixed("default { touch_start(integer n, integer m) { } }", false, "LSLTooManyArgumentsEvent", "Write the parameters 'touch_start' takes"),
                      std::string("default { touch_start(integer n) { } }"));
        ensure_equals("an event's with no type", fixed("default { touch_start(n) { } }", false, "LSLParameterUntyped", "Write the parameters 'touch_start' takes"),
                      std::string("default { touch_start(integer n) { } }"));
        ensure_equals("a declaration as an if's body, braced", fixed("default { state_entry() { if (llGetUnixTime()) integer i = 1; } }", false, "LSLDeclarationInvalidHere",
                                                                    "Put it in braces of its own"),
                      std::string("default { state_entry() { if (llGetUnixTime()) { integer i = 1; } } }"));

        const std::string      function = "say(x) { llOwnerSay((string)x); }\ndefault { state_entry() { say(1); } }";
        const ALScriptProblems untyped  = check(function, false);
        const ALScriptProblem* bare     = keyed(untyped, "LSLParameterUntyped");
        ensure("a function's: every type, none preferred", bare && bare->fixes.size() == 7 &&
               std::none_of(bare->fixes.begin(), bare->fixes.end(), [](const ALScriptFix& fix) { return fix.preferred; }));
        const std::optional<std::string> typed = ALScriptFixes::apply(function, bare->fixes.front());
        ensure("declared as the first", typed && typed->rfind("say(integer x)", 0) == 0 && errors(check(*typed, false)) == 0);

        const ALScriptProblems missing = check("default { state_entry() { integer x = 1 llOwnerSay(\"a\"); } }", false);
        const ALScriptProblem* ended   = keyed(missing, "LSLSyntaxMissing");
        ensure("a missing ';' put in safely", ended && !ended->fixes.empty() && ended->fixes.front().safe);
    }

    template<> template<>
    void object::test<36>()
    {
        set_test_name("a handler's missing brace is put on a line of its own, level with the handler after it, and the script checks clean");
        ensure("builtins: " + error, lslLoaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llSay(0, \"Hello, Avatar!\");\n\n"
                                   "    touch_start(integer total_number)\n    {\n        llSay(0, \"Touched.\");\n    }\n}\n";
        const std::string made = fixed(script, false, "LSLSyntaxMissing", "Insert '}'");
        ensure("closed after the call, its own line: " + made, made.find("\"Hello, Avatar!\");\n    }\n\n    touch_start(") != std::string::npos);
        ensure("no error left", errors(check(made, false)) == 0);
    }
}
