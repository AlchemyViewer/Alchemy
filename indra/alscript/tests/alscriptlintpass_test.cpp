/**
 * @file tests/alscriptlintpass_test.cpp
 * @brief The studio's own lints over real checks: what each finds and what it leaves, and its fix made and the text checked again.
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

#include "../alluauservice.h"
#include "../alscriptfixes.h"

#include "../test/lltut.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tut
{
    struct alscriptlintpass_data
    {
        ALLuauService luau;
        std::string   error;
        bool          loaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alscriptlintpass_data()
        {
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            luau.setNewSolver(newSolver, error);
            loaded = luau.loadDefinitions(text.str(), error);
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

        static size_t errors(const ALScriptProblems& problems)
        {
            return std::count_if(problems.begin(), problems.end(),
                                 [](const ALScriptProblem& problem) { return problem.severity == ALScriptProblem::Severity::Error; });
        }

        static const ALScriptProblem* keyed(const ALScriptProblems& problems, const std::string& key)
        {
            const auto found = std::find_if(problems.begin(), problems.end(), [&](const ALScriptProblem& problem) { return problem.key == key; });
            return found == problems.end() ? nullptr : &*found;
        }

        // What a rule said of a script, in the order of its places: each as
        // its line, its key, and its args, split by |; and its severity where
        // that is not the one expected.
        std::string found(const std::string& script, const std::string& rule, ALScriptProblem::Severity expected)
        {
            ALScriptProblems problems = luau.check(script);
            std::stable_sort(problems.begin(), problems.end(), [](const ALScriptProblem& a, const ALScriptProblem& b) {
                return a.line != b.line ? a.line < b.line : a.column < b.column;
            });
            std::string out;
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.code != rule)
                {
                    continue;
                }
                out += std::to_string(problem.line) + " " + problem.key;
                for (const std::string& arg : problem.args)
                {
                    out += "|" + arg;
                }
                out += problem.severity == expected ? "\n" : " (another severity)\n";
            }
            return out;
        }

        // The problem keyed so, its preferred fix -- titled so, and safe or
        // not as said -- the text with it made, and that text checked again:
        // the problem gone, and no error in its place.
        std::string fixed(const std::string& script, const std::string& key, const std::string& title, bool safe)
        {
            const ALScriptProblems problems = luau.check(script);
            const ALScriptProblem* problem  = keyed(problems, key);
            ensure("said: " + key + "\n" + said(problems), problem != nullptr);
            ensure("a fix: " + said(problems), !problem->fixes.empty());
            const ALScriptFix& fix = problem->fixes.front();
            ensure("preferred", fix.preferred);
            ensure_equals("its words", fix.title, title);
            ensure_equals("safe or not", fix.safe, safe);
            const std::optional<std::string> made = ALScriptFixes::apply(script, fix);
            ensure("applies", made.has_value());
            const ALScriptProblems after = luau.check(*made);
            ensure("gone from:\n" + *made + "\n" + said(after), keyed(after, key) == nullptr);
            ensure("no error in its place:\n" + *made + "\n" + said(after),
                   errors(after) <= errors(problems) - (problem->severity == ALScriptProblem::Severity::Error ? 1 : 0));
            return *made;
        }

        // The problem keyed so is said, and offers no fix.
        void unfixed(const std::string& script, const std::string& key)
        {
            const ALScriptProblems problems = luau.check(script);
            const ALScriptProblem* problem  = keyed(problems, key);
            ensure("said: " + key + "\n" + said(problems), problem != nullptr);
            ensure("no fix: " + said(problems), problem->fixes.empty());
        }
    };
    // Raised from TUT's fifty: stages C and D of the SLua help plan add a
    // test for each rule.
    typedef test_group<alscriptlintpass_data, 100> alscriptlintpass_group;
    typedef alscriptlintpass_group::object         object;
    tut::alscriptlintpass_group                    alscriptlintpass_test("ALScriptLintPass");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("SlTableCompare: a table compared with {} on either side, by length where it is a list; with items, or a string, no fix");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local t = {1}\n"
                                       "if t == {} then print(1) end\n"
                                       "if {} ~= t then print(2) end\n"
                                       "local d = {a = 1}\n"
                                       "if (d) == ({}) then print(3) end\n"
                                       "if t == {1, 2} then print(4) end\n"
                                       "local s = \"x\"\n"
                                       "if s == {} then print(5) end\n"
                                       "local function f(l) return l ~= {} end\n"
                                       "if t == nil or t == d then print(f(t)) end\n",
                                       "SlTableCompare", ALScriptProblem::Severity::Error);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlTableCompare|t|#t == 0\n"
                                  "2 LuauLintSlTableCompareAlways|t|#t > 0\n"
                                  "4 LuauLintSlTableCompare|d|next(d) == nil\n"
                                  "5 LuauLintSlTableCompareItems|t\n"
                                  "7 LuauLintSlTableCompareItems|s\n"
                                  "8 LuauLintSlTableCompareAlways|l|next(l) ~= nil\n"));
        ensure_equals("a list", fixed("local t = {1}\nif t == {} then print(t) end\n", "LuauLintSlTableCompare", "Write it #t == 0", false),
                      std::string("local t = {1}\nif #t == 0 then print(t) end\n"));
        ensure_equals("a table, on the right, bracketed", fixed("local d = {a = 1}\nprint({} ~= (d))\n", "LuauLintSlTableCompareAlways", "Write it next(d) ~= nil", false),
                      std::string("local d = {a = 1}\nprint(next(d) ~= nil)\n"));
        unfixed("local t = {1}\nprint(t == {1})\n", "LuauLintSlTableCompareItems");
    }
}
