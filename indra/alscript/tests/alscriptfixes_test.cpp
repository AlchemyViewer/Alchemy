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

#include "../allslservice.h"
#include "../alluauservice.h"

#include "../test/lltut.h"

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

        alscriptfixes_data()
        {
            lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
            std::ifstream     in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
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
}
