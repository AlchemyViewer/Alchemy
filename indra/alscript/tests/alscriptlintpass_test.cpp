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

    template<> template<>
    void object::test<2>()
    {
        set_test_name("SlZeroIndex: a list at 0, a loop from 0 to a length less 1 that indexes, string.sub and ll from 0, a find against 0; "
                      "not a table keyed at 0, nor a loop given to llcompat");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local t = {1, 2}\n"
                                       "print(t[0])\n"
                                       "for i = 0, #t - 1 do print(t[i]) end\n"
                                       "for i = 0, #t - 1 do print(i, t[i]) end\n"
                                       "for i = 0, #t - 1 do print(llcompat.List2String(t, i)) end\n"
                                       "local s = \"abcdef\"\n"
                                       "print(string.sub(s, 0, 2), s:sub(0, #s - 1), s:sub(0, -1), s:sub(0, #t), s:sub(1, 2))\n"
                                       "print(ll.GetSubString(s, 0, 2), ll.GetSubString(s, 0, #s), ll.GetSubString(s, 1, 2))\n"
                                       "if ll.SubStringIndex(s, \"a\") == 0 or 0 ~= table.find(t, 2) then print(1) end\n"
                                       "local d = {[0] = 1}\n"
                                       "local h = {}\n"
                                       "h[0] = 2\n"
                                       "print(d[0], h[0], llcompat.SubStringIndex(s, \"a\") == 0)\n",
                                       "SlZeroIndex", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlZeroIndex|t\n"
                                  "2 LuauLintSlZeroIndexLoop|#t|t[i]|i\n"
                                  "3 LuauLintSlZeroIndexLoop|#t|t[i]|i\n"
                                  "6 LuauLintSlZeroIndexSub|string.sub\n"
                                  "6 LuauLintSlZeroIndexSub|s:sub\n"
                                  "6 LuauLintSlZeroIndexSub|s:sub\n"
                                  "6 LuauLintSlZeroIndexSub|s:sub\n"
                                  "7 LuauLintSlZeroIndexArg|ll.GetSubString\n"
                                  "7 LuauLintSlZeroIndexArg|ll.GetSubString\n"
                                  "8 LuauLintSlZeroIndexFound|ll.SubStringIndex(...)|ll.SubStringIndex\n"
                                  "8 LuauLintSlZeroIndexFound|table.find(...)|table.find\n"));
        ensure_equals("a list", fixed("local t = {1}\nprint(t[0])\n", "LuauLintSlZeroIndex", "Write it t[1]", false),
                      std::string("local t = {1}\nprint(t[1])\n"));
        ensure_equals("a loop", fixed("local t = {1}\nfor i = 0, #t - 1 do print(t[i]) end\n", "LuauLintSlZeroIndexLoop", "Write it for i = 1, #t", false),
                      std::string("local t = {1}\nfor i = 1, #t do print(t[i]) end\n"));
        ensure_equals("a method to a length less 1", fixed("local s = \"abc\"\nprint(s:sub(0, #s - 1))\n", "LuauLintSlZeroIndexSub", "Write it s:sub(1, #s)", false),
                      std::string("local s = \"abc\"\nprint(s:sub(1, #s))\n"));
        ensure_equals("string's, to a number", fixed("print(string.sub(\"abc\", 0, 2))\n", "LuauLintSlZeroIndexSub", "Write it string.sub(\"abc\", 1, 3)", false),
                      std::string("print(string.sub(\"abc\", 1, 3))\n"));
        ensure_equals("ll's, one from the end kept", fixed("print(ll.GetSubString(\"abc\", 0, -1))\n", "LuauLintSlZeroIndexArg",
                                                           "Write it ll.GetSubString(\"abc\", 1, -1)", false),
                      std::string("print(ll.GetSubString(\"abc\", 1, -1))\n"));
        ensure_equals("a find", fixed("print(ll.SubStringIndex(\"ab\", \"a\") == 0)\n", "LuauLintSlZeroIndexFound", "Write it ll.SubStringIndex(...) == 1", false),
                      std::string("print(ll.SubStringIndex(\"ab\", \"a\") == 1)\n"));
        unfixed("local t = {1}\nfor i = 0, #t - 1 do print(i, t[i]) end\n", "LuauLintSlZeroIndexLoop");
        unfixed("local s = \"abc\"\nlocal n = 2\nprint(s:sub(0, n))\n", "LuauLintSlZeroIndexSub");
        unfixed("local s = \"abc\"\nprint(ll.GetSubString(s, 0, #s))\n", "LuauLintSlZeroIndexArg");
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("SlCompatCall: llcompat's where ll's means the same -- the same function, indexes that are numbers, a boolean against 1 "
                      "or 0, a find against -1 or 0, an answer unread; not where ll deprecates it, nor an answer read otherwise");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local s = \"abc\"\n"
                                       "llcompat.Say(0, s)\n"
                                       "print(llcompat.GetSubString(s, 0, -1))\n"
                                       "print(llcompat.GetSubString(s, 0, #s))\n"
                                       "if llcompat.SameGroup(ll.GetOwner()) == 1 then print(1) end\n"
                                       "if 0 == llcompat.SameGroup(ll.GetOwner()) then print(2) end\n"
                                       "if llcompat.SubStringIndex(s, \"b\") ~= -1 then print(3) end\n"
                                       "if 0 > llcompat.SubStringIndex(s, \"b\") then print(4) end\n"
                                       "llcompat.SameGroup(ll.GetOwner())\n"
                                       "local n = llcompat.SubStringIndex(s, \"b\")\n"
                                       "local g = llcompat.SameGroup(ll.GetOwner()) + 1\n"
                                       "print(n, g, llcompat.ListFindList({1}, {1}) == -1)\n",
                                       "SlCompatCall", ALScriptProblem::Severity::Note);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlCompatCall|llcompat.Say|ll.Say(...)\n"
                                  "2 LuauLintSlCompatCall|llcompat.GetSubString|ll.GetSubString(s, 1, -1)\n"
                                  "4 LuauLintSlCompatCall|llcompat.SameGroup|ll.SameGroup(...)\n"
                                  "5 LuauLintSlCompatCall|llcompat.SameGroup|not ll.SameGroup(...)\n"
                                  "6 LuauLintSlCompatCall|llcompat.SubStringIndex|ll.SubStringIndex(...) ~= nil\n"
                                  "7 LuauLintSlCompatCall|llcompat.SubStringIndex|ll.SubStringIndex(...) == nil\n"
                                  "8 LuauLintSlCompatCall|llcompat.SameGroup|ll.SameGroup(...)\n"));
        ensure_equals("the same", fixed("llcompat.Say(0, \"hi\")\n", "LuauLintSlCompatCall", "Write it ll.Say(...)", true),
                      std::string("ll.Say(0, \"hi\")\n"));
        ensure_equals("indexes", fixed("print(llcompat.GetSubString(\"abc\", 0, 1))\n", "LuauLintSlCompatCall", "Write it ll.GetSubString(\"abc\", 1, 2)", true),
                      std::string("print(ll.GetSubString(\"abc\", 1, 2))\n"));
        ensure_equals("a boolean on the right", fixed("print(0 == llcompat.SameGroup(ll.GetOwner()))\n", "LuauLintSlCompatCall", "Write it not ll.SameGroup(...)", true),
                      std::string("print(not ll.SameGroup(ll.GetOwner()))\n"));
        ensure_equals("a boolean", fixed("print(llcompat.SameGroup(ll.GetOwner()) ~= 0)\n", "LuauLintSlCompatCall", "Write it ll.SameGroup(...)", true),
                      std::string("print(ll.SameGroup(ll.GetOwner()))\n"));
        ensure_equals("a find, first", fixed("print(llcompat.SubStringIndex(\"ab\", \"a\") == 0)\n", "LuauLintSlCompatCall", "Write it ll.SubStringIndex(...) == 1", true),
                      std::string("print(ll.SubStringIndex(\"ab\", \"a\") == 1)\n"));
        ensure_equals("a find, on the right", fixed("print(-1 < llcompat.SubStringIndex(\"ab\", \"a\"))\n", "LuauLintSlCompatCall", "Write it ll.SubStringIndex(...) ~= nil", true),
                      std::string("print(ll.SubStringIndex(\"ab\", \"a\") ~= nil)\n"));
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("SlBooleanNumber: a boolean compared with a number, on either side, an error; an if-then-else of 1 and 0 compared with "
                      "one, a note; not a number compared with one");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local on = false\n"
                                       "local n = 1\n"
                                       "if on == 1 or 0 ~= on then print(1) end\n"
                                       "if ll.SameGroup(ll.GetOwner()) == 0 or (n > 2) == 5 then print(2) end\n"
                                       "if (if on then 1 else 0) == 1 or (if n > 2 then 0 else 1) ~= 0 then print(3) end\n"
                                       "if n == 1 or (if on then 2 else 0) == 1 then print(4) end\n",
                                       "SlBooleanNumber", ALScriptProblem::Severity::Error);
        ensure_equals("each", said,
                      std::string("2 LuauLintSlBooleanNumber|on|1\n"
                                  "2 LuauLintSlBooleanNumberAlways|on|0\n"
                                  "3 LuauLintSlBooleanNumber|ll.SameGroup(ll.GetOwner())|0\n"
                                  "3 LuauLintSlBooleanNumber|(n > 2)|5\n"
                                  "4 LuauLintSlBooleanNumberChoice|(if on then 1 else 0) == 1|on (another severity)\n"
                                  "4 LuauLintSlBooleanNumberChoice|(if n > 2 then 0 else 1) ~= 0|not (n > 2) (another severity)\n"));
        ensure_equals("true", fixed("local on = false\nprint(on == 1)\n", "LuauLintSlBooleanNumber", "Write it on", false),
                      std::string("local on = false\nprint(on)\n"));
        ensure_equals("false, on the right", fixed("print(0 == ll.SameGroup(ll.GetOwner()))\n", "LuauLintSlBooleanNumber", "Write it not ll.SameGroup(ll.GetOwner())", false),
                      std::string("print(not ll.SameGroup(ll.GetOwner()))\n"));
        ensure_equals("a choice", fixed("local n = 1\nprint((if n > 2 then 0 else 1) ~= 0)\n", "LuauLintSlBooleanNumberChoice", "Write it not (n > 2)", false),
                      std::string("local n = 1\nprint(not (n > 2))\n"));
        unfixed("local n = 1\nprint((n > 2) == 5)\n", "LuauLintSlBooleanNumber");
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("SlGlobalAssign: a global made at the top, once, a note; a function made global in a block, a warning; not a name the "
                      "script is given; each made local, in place or declared before it is first named, and not where _G is");
        ensure("definitions: " + error, loaded);
        const std::string said = found("count = 0\n"
                                       "function bump() count += 1; total = count end\n"
                                       "local function early() return later end\n"
                                       "later = 5\n"
                                       "print = print\n"
                                       "local function outer()\n"
                                       "    function inner() return 1 end\n"
                                       "    function shared() return 2 end\n"
                                       "    return inner()\n"
                                       "end\n"
                                       "x, y = 1, 2\n"
                                       "print(bump, early, outer, shared, total, x, y)\n"
                                       "count = 1\n",
                                       "SlGlobalAssign", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("0 LuauLintSlGlobalAssign|count (another severity)\n"
                                  "1 LuauLintSlGlobalFunction|bump (another severity)\n"
                                  "3 LuauLintSlGlobalAssign|later (another severity)\n"
                                  "6 LuauLintSlGlobalFunctionInScope|inner\n"
                                  "7 LuauLintSlGlobalFunctionInScope|shared\n"
                                  "10 LuauLintSlGlobalAssign|x, y (another severity)\n"));
        ensure_equals("in place", fixed("count = 0\nprint(count)\n", "LuauLintSlGlobalAssign", "Write it local count", false),
                      std::string("local count = 0\nprint(count)\n"));
        ensure_equals("declared first", fixed("local function early() return later end\nlater = 5\nprint(early())\n", "LuauLintSlGlobalAssign",
                                              "Declare local later before it is first named", false),
                      std::string("local later\nlocal function early() return later end\nlater = 5\nprint(early())\n"));
        ensure_equals("a function", fixed("function f() return 1 end\nprint(f())\n", "LuauLintSlGlobalFunction", "Write it local function f", false),
                      std::string("local function f() return 1 end\nprint(f())\n"));
        ensure_equals("in a block", fixed("local function outer()\n    function inner() return 1 end\n    return inner()\nend\nprint(outer())\n",
                                          "LuauLintSlGlobalFunctionInScope", "Write it local function inner", false),
                      std::string("local function outer()\n    local function inner() return 1 end\n    return inner()\nend\nprint(outer())\n"));
        unfixed("local function outer()\n    function shared() return 2 end\nend\nprint(outer, shared)\n", "LuauLintSlGlobalFunctionInScope");
        unfixed("print(_G)\ny = 2\n", "LuauLintSlGlobalAssign");
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("SlParenCondition: if, elseif, while, until and an if-then-else bracketed whole, a note; not brackets round a part; "
                      "the brackets taken out, a blank kept where a word would run on, safe; selene's name allows it");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local x = true\n"
                                       "if (x) then print(1) elseif (not x) then print(2) end\n"
                                       "while(x)do break end\n"
                                       "repeat local y = 1 until (y > 0)\n"
                                       "print(if (x) then 1 else 2)\n"
                                       "if (x) and (x) then print(3) end\n"
                                       "if x then print(4) end\n",
                                       "SlParenCondition", ALScriptProblem::Severity::Note);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlParenCondition|if\n"
                                  "1 LuauLintSlParenCondition|elseif\n"
                                  "2 LuauLintSlParenCondition|while\n"
                                  "3 LuauLintSlParenCondition|until\n"
                                  "4 LuauLintSlParenCondition|if\n"));
        ensure_equals("spaced", fixed("local x = true\nif (x) then print(1) end\n", "LuauLintSlParenCondition", "Take out the brackets", true),
                      std::string("local x = true\nif x then print(1) end\n"));
        ensure_equals("run on", fixed("local x = true\nwhile(x)do break end\n", "LuauLintSlParenCondition", "Take out the brackets", true),
                      std::string("local x = true\nwhile x do break end\n"));
        ensure_equals("allowed by selene's name", found("local x = true\n-- selene: allow(parenthese_conditions)\nif (x) then print(1) end\n",
                                                        "SlParenCondition", ALScriptProblem::Severity::Note),
                      std::string());
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("SlAlmostSwapped: a = b then b = a, locals, globals or fields, a warning, fixed as a swap; not a swap, nor with "
                      "something between");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local a, b = 1, 2\n"
                                       "a = b\n"
                                       "b = a\n"
                                       "local t = {x = 1, y = 2}\n"
                                       "t.x = t.y; t.y = t.x\n"
                                       "a, b = b, a\n"
                                       "a = b\n"
                                       "print(a)\n"
                                       "b = a\n",
                                       "SlAlmostSwapped", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlAlmostSwapped|a|b|a, b = b, a\n"
                                  "4 LuauLintSlAlmostSwapped|t.x|t.y|t.x, t.y = t.y, t.x\n"));
        ensure_equals("swapped", fixed("local a, b = 1, 2\na = b\nb = a\nprint(a, b)\n", "LuauLintSlAlmostSwapped", "Write it a, b = b, a", false),
                      std::string("local a, b = 1, 2\na, b = b, a\nprint(a, b)\n"));
    }
}
