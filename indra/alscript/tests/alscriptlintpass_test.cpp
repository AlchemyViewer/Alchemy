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

#include "../allslservice.h"
#include "../allsltoslua.h"
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
        ALLSLService  lsl;
        std::string   error;
        bool          loaded    = false;
        bool          lslLoaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alscriptlintpass_data()
        {
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            luau.setNewSolver(newSolver, error);
            loaded    = luau.loadDefinitions(text.str(), error);
            lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
        }

        // A rule both languages have is said by both: SLua's, or LSL's.
        ALScriptProblems check(const std::string& script, bool lua) { return lua ? luau.check(script) : lsl.check(script); }

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
        std::string found(const std::string& script, const std::string& rule, ALScriptProblem::Severity expected, bool lua = true)
        {
            ALScriptProblems problems = check(script, lua);
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
        std::string fixed(const std::string& script, const std::string& key, const std::string& title, bool safe, bool lua = true)
        {
            const ALScriptProblems problems = check(script, lua);
            const ALScriptProblem* problem  = keyed(problems, key);
            ensure("said: " + key + "\n" + said(problems), problem != nullptr);
            ensure("a fix: " + said(problems), !problem->fixes.empty());
            const ALScriptFix& fix = problem->fixes.front();
            ensure("preferred", fix.preferred);
            ensure_equals("its words", fix.title, title);
            ensure_equals("safe or not", fix.safe, safe);
            const std::optional<std::string> made = ALScriptFixes::apply(script, fix);
            ensure("applies", made.has_value());
            const ALScriptProblems after = check(*made, lua);
            ensure("gone from:\n" + *made + "\n" + said(after), keyed(after, key) == nullptr);
            ensure("no error in its place:\n" + *made + "\n" + said(after),
                   errors(after) <= errors(problems) - (problem->severity == ALScriptProblem::Severity::Error ? 1 : 0));
            return *made;
        }

        // The problem keyed so is said, and offers no fix.
        void unfixed(const std::string& script, const std::string& key, bool lua = true)
        {
            const ALScriptProblems problems = check(script, lua);
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

    template<> template<>
    void object::test<8>()
    {
        set_test_name("SlMustUse: ll's, llcompat's, a library's, a global's and a string's method that only answer, their answers unread; "
                      "given back where the first given is a variable of the kind answered; not a call that does something");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local l = {1, 2}\n"
                                       "local s = \"abc\"\n"
                                       "ll.DeleteSubList(l, 1, 1)\n"
                                       "string.upper(s)\n"
                                       "s:lower()\n"
                                       "string.len(s)\n"
                                       "tostring(l)\n"
                                       "llcompat.GetPos()\n"
                                       "ll.Say(0, s)\n"
                                       "table.insert(l, 3)\n"
                                       "math.random()\n"
                                       "print(ll.DeleteSubList(l, 1, 1))\n",
                                       "SlMustUse", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("2 LuauLintSlMustUse|ll.DeleteSubList\n"
                                  "3 LuauLintSlMustUse|string.upper\n"
                                  "4 LuauLintSlMustUse|s:lower\n"
                                  "5 LuauLintSlMustUse|string.len\n"
                                  "6 LuauLintSlMustUse|tostring\n"
                                  "7 LuauLintSlMustUse|llcompat.GetPos\n"));
        ensure_equals("a list", fixed("local l = {1, 2}\nll.DeleteSubList(l, 1, 1)\nprint(l)\n", "LuauLintSlMustUse", "Write it l = ll.DeleteSubList(...)", false),
                      std::string("local l = {1, 2}\nl = ll.DeleteSubList(l, 1, 1)\nprint(l)\n"));
        ensure_equals("a method", fixed("local s = \"abc\"\ns:upper()\nprint(s)\n", "LuauLintSlMustUse", "Write it s = s:upper(...)", false),
                      std::string("local s = \"abc\"\ns = s:upper()\nprint(s)\n"));
        unfixed("local s = \"abc\"\nstring.len(s)\n", "LuauLintSlMustUse");
        unfixed("string.upper(\"abc\")\n", "LuauLintSlMustUse");
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("SlGeneralizedFor: pairs, ipairs and next, t in a for, a note, fixed as the table itself, not safe; not a function "
                      "of the script's, nor for over a table already");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local t = {1, 2}\n"
                                       "for k, v in pairs(t) do print(k, v) end\n"
                                       "for i, v in ipairs(t) do print(i, v) end\n"
                                       "for k, v in next, t do print(k, v) end\n"
                                       "local function walk(x) return next, x end\n"
                                       "for k, v in walk(t) do print(k, v) end\n"
                                       "for k, v in t do print(k, v) end\n",
                                       "SlGeneralizedFor", ALScriptProblem::Severity::Note);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlGeneralizedFor|t|pairs(t)\n"
                                  "2 LuauLintSlGeneralizedForList|t|ipairs(t)\n"
                                  "3 LuauLintSlGeneralizedFor|t|next, t\n"));
        ensure_equals("pairs", fixed("local t = {1}\nfor k, v in pairs(t) do print(k, v) end\n", "LuauLintSlGeneralizedFor", "Write it in t", false),
                      std::string("local t = {1}\nfor k, v in t do print(k, v) end\n"));
        ensure_equals("next", fixed("local t = {1}\nfor k, v in next, t do print(k, v) end\n", "LuauLintSlGeneralizedFor", "Write it in t", false),
                      std::string("local t = {1}\nfor k, v in t do print(k, v) end\n"));
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("SlEmptyBlock: an empty if, elseif, else, while, for and repeat, a warning; not one holding a comment; an empty then "
                      "before an else turned round, and an empty else taken out, safe; selene's names allow it");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local c = true\n"
                                       "if c then end\n"
                                       "if c then else print(1) end\n"
                                       "if c then print(2) elseif not c then end\n"
                                       "if c then print(3) else end\n"
                                       "while c do end\n"
                                       "for i = 1, 2 do end\n"
                                       "for k, v in {} do end\n"
                                       "repeat until c\n"
                                       "if c then -- later\n"
                                       "end\n"
                                       "while c do break end\n",
                                       "SlEmptyBlock", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlEmptyBlock|if\n"
                                  "2 LuauLintSlEmptyBlock|if\n"
                                  "3 LuauLintSlEmptyBlock|elseif\n"
                                  "4 LuauLintSlEmptyBlock|else\n"
                                  "5 LuauLintSlEmptyBlock|while\n"
                                  "6 LuauLintSlEmptyBlock|for\n"
                                  "7 LuauLintSlEmptyBlock|for\n"
                                  "8 LuauLintSlEmptyBlock|repeat\n"));
        ensure_equals("turned round", fixed("local c = true\nif c then else print(1) end\n", "LuauLintSlEmptyBlock", "Write it if not c then", false),
                      std::string("local c = true\nif not c then print(1) end\n"));
        ensure_equals("an else", fixed("local c = true\nif c then print(3) else end\n", "LuauLintSlEmptyBlock", "Take out the empty else", true),
                      std::string("local c = true\nif c then print(3) end\n"));
        ensure_equals("an else on its own line", fixed("local c = true\nif c then\n    print(3)\nelse\nend\n", "LuauLintSlEmptyBlock", "Take out the empty else", true),
                      std::string("local c = true\nif c then\n    print(3)\nend\n"));
        ensure_equals("selene's names", found("local c = true\n-- selene: allow(empty_loop)\nwhile c do end\n-- selene: allow(empty_if)\nif c then end\n",
                                              "SlEmptyBlock", ALScriptProblem::Severity::Warning),
                      std::string());
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("SlIndexDivision: a / in what indexes a list, alone, bracketed or in a sum, read or written, a warning, fixed as //; "
                      "not //, nor a table that is no list, nor a count");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local t = {1, 2, 3}\n"
                                       "local n = #t\n"
                                       "print(t[n / 2], t[(n + 1) / 2], t[n / 2 + 1], t[n // 2])\n"
                                       "t[n / 2] = 5\n"
                                       "local d = {a = 1}\n"
                                       "print(d[n / 2], string.rep(\"x\", n / 2))\n",
                                       "SlIndexDivision", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("2 LuauLintSlIndexDivision|n / 2|n // 2\n"
                                  "2 LuauLintSlIndexDivision|(n + 1) / 2|(n + 1) // 2\n"
                                  "2 LuauLintSlIndexDivision|n / 2|n // 2\n"
                                  "3 LuauLintSlIndexDivision|n / 2|n // 2\n"));
        ensure_equals("rounded down", fixed("local t = {1}\nlocal n = #t\nprint(t[n / 2])\n", "LuauLintSlIndexDivision", "Write it n // 2", false),
                      std::string("local t = {1}\nlocal n = #t\nprint(t[n // 2])\n"));
    }

    template<> template<>
    void object::test<12>()
    {
        set_test_name("SlVectorProduct: two vectors multiplied where a number is wanted -- compared, added to one, given to math -- and "
                      "taken % each other, a warning, fixed as vector.dot and vector.cross; not a product kept a vector, nor by a number");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local a = vector(1, 0, 0)\n"
                                       "local b = vector(0, 1, 0)\n"
                                       "if a * b > 0.5 then print(1) end\n"
                                       "print(math.acos(a * b), (a * b) + 1)\n"
                                       "local c = a % b\n"
                                       "local s = a * b\n"
                                       "print(a * 2, s, c)\n",
                                       "SlVectorProduct", ALScriptProblem::Severity::Warning);
        ensure_equals("each", said,
                      std::string("2 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "3 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "3 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "4 LuauLintSlVectorCross|a % b|a|b\n"));
        ensure_equals("dot", fixed("local a = vector(1, 0, 0)\nprint(a * a > 0.5)\n", "LuauLintSlVectorProduct", "Write it vector.dot(a, a)", false),
                      std::string("local a = vector(1, 0, 0)\nprint(vector.dot(a, a) > 0.5)\n"));
        ensure_equals("cross", fixed("local a = vector(1, 0, 0)\nlocal c = a % a\nprint(c)\n", "LuauLintSlVectorCross", "Write it vector.cross(a, a)", false),
                      std::string("local a = vector(1, 0, 0)\nlocal c = vector.cross(a, a)\nprint(c)\n"));
    }

    template<> template<>
    void object::test<13>()
    {
        set_test_name("SlSleepingCall in SLua: a call that sleeps where a Fast one does not, a note, and a warning in a loop or a timer; "
                      "fixed as the Fast call, its arguments put in or, where only settled, written again; a texture's, no fix");
        ensure("definitions: " + error, loaded);
        const std::string said = found("local v = vector(1, 2, 3)\n"
                                       "ll.SetPos(v)\n"
                                       "for i = 1, 3 do ll.SetRot(quaternion(0, 0, 0, 1)) end\n"
                                       "LLTimers:every(1, function() ll.SetPrimitiveParams({PRIM_POSITION, v}) end)\n"
                                       "ll.SetTexture(\"x\", 0)\n"
                                       "ll.SetLinkRenderMaterial(LINK_THIS, \"m\", 0)\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {})\n"
                                       "llcompat.SetPos(v)\n",
                                       "SlSleepingCall", ALScriptProblem::Severity::Note);
        ensure_equals("each", said,
                      std::string("1 LuauLintSlSleepingCall|ll.SetPos|0.2|ll.SetLinkPrimitiveParamsFast\n"
                                  "2 LuauLintSlSleepingCallOften|ll.SetRot|0.2|ll.SetLinkPrimitiveParamsFast (another severity)\n"
                                  "3 LuauLintSlSleepingCallOften|ll.SetPrimitiveParams|0.2|ll.SetLinkPrimitiveParamsFast (another severity)\n"
                                  "4 LuauLintSlSleepingTexture|ll.SetTexture|0.2|PRIM_TEXTURE\n"
                                  "5 LuauLintSlSleepingCall|ll.SetLinkRenderMaterial|0.2|ll.SetLinkPrimitiveParamsFast\n"
                                  "7 LuauLintSlSleepingCall|llcompat.SetPos|0.2|llcompat.SetLinkPrimitiveParamsFast\n"));
        ensure_equals("put in", fixed("local v = vector(1, 2, 3)\nll.SetPos(v)\n", "LuauLintSlSleepingCall",
                                      "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, v})", false),
                      std::string("local v = vector(1, 2, 3)\nll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, v})\n"));
        ensure_equals("a link first, in a timer", fixed("LLTimers:every(1, function() ll.SetPrimitiveParams({}) end)\n", "LuauLintSlSleepingCallOften",
                                                        "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {})", false),
                      std::string("LLTimers:every(1, function() ll.SetLinkPrimitiveParamsFast(LINK_THIS, {}) end)\n"));
        ensure_equals("written again", fixed("ll.SetLinkRenderMaterial(LINK_THIS, \"m\", 0)\n", "LuauLintSlSleepingCall",
                                             "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_RENDER_MATERIAL, 0, \"m\"})", false),
                      std::string("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_RENDER_MATERIAL, 0, \"m\"})\n"));
        unfixed("ll.SetTexture(\"x\", 0)\n", "LuauLintSlSleepingTexture");
        unfixed("ll.SetRenderMaterial(tostring(1), 0)\n", "LuauLintSlSleepingCall");
    }

    template<> template<>
    void object::test<14>()
    {
        set_test_name("SlSleepingCall in LSL: the same table, a warning in a loop or the timer event; fixed over the text, a vector's commas "
                      "and a comment in the way");
        ensure("builtins: " + error, lslLoaded);
        const std::string said = found("default {\n"
                                       "    state_entry() {\n"
                                       "        llSetPos(<1, 2, 3>);\n"
                                       "        integer i;\n"
                                       "        for (i = 0; i < 3; ++i) llSetRot(ZERO_ROTATION);\n"
                                       "        llSetLinkRenderMaterial(LINK_THIS, \"m\", ALL_SIDES);\n"
                                       "        llSetTexture(\"x\", 0);\n"
                                       "    }\n"
                                       "    timer() {\n"
                                       "        llSetPrimitiveParams([PRIM_POSITION, <1, 2, 3>]);\n"
                                       "    }\n"
                                       "}\n",
                                       "SlSleepingCall", ALScriptProblem::Severity::Note, false);
        ensure_equals("each", said,
                      std::string("2 LSLSlSleepingCall|llSetPos|0.2|llSetLinkPrimitiveParamsFast\n"
                                  "4 LSLSlSleepingCallOften|llSetRot|0.2|llSetLinkPrimitiveParamsFast (another severity)\n"
                                  "5 LSLSlSleepingCall|llSetLinkRenderMaterial|0.2|llSetLinkPrimitiveParamsFast\n"
                                  "6 LSLSlSleepingTexture|llSetTexture|0.2|PRIM_TEXTURE\n"
                                  "9 LSLSlSleepingCallOften|llSetPrimitiveParams|0.2|llSetLinkPrimitiveParamsFast (another severity)\n"));
        ensure_equals("put in", fixed("default { state_entry() { llSetPos( <1, 2, 3> /* here */ ); } }\n", "LSLSlSleepingCall",
                                      "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_POSITION, <1, 2, 3> /* here */])", false, false),
                      std::string("default { state_entry() { llSetLinkPrimitiveParamsFast( LINK_THIS, [PRIM_POSITION, <1, 2, 3> /* here */] ); } }\n"));
        ensure_equals("written again", fixed("default { state_entry() { llSetLinkRenderMaterial(LINK_THIS, \"m\", ALL_SIDES); } }\n", "LSLSlSleepingCall",
                                             "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"])", false, false),
                      std::string("default { state_entry() { llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"]); } }\n"));
        unfixed("default { state_entry() { llSetRenderMaterial(llGetInventoryName(INVENTORY_MATERIAL, 0), 0); } }\n", "LSLSlSleepingCall", false);
    }

    template<> template<>
    void object::test<15>()
    {
        set_test_name("SlMergeablePrimParams in SLua: a run of the same prim-params call, its rules written out, a note; merged with "
                      "PRIM_LINK_TARGET where the link changes, or a list sends its rules elsewhere; broken by anything between, a call "
                      "whose arguments read the world, or another function");
        ensure("definitions: " + error, loaded);
        const std::string said = found("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_COLOR, ALL_SIDES, vector(1, 0, 0), 1})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5})\n"
                                       "ll.SetLinkPrimitiveParamsFast(2, {PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "print(\"between\")\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, ll.GetPos()})\n"
                                       "ll.SetPrimitiveParams({PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                       "SlMergeablePrimParams", ALScriptProblem::Severity::Note);
        ensure_equals("each", said, std::string("0 LuauLintSlMergeablePrimParams|3|ll.SetLinkPrimitiveParamsFast\n"));
        ensure_equals("a link changed", fixed("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5})\nll.SetLinkPrimitiveParamsFast(2, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                              "LuauLintSlMergeablePrimParams",
                                              "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0})",
                                              false),
                      std::string("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0})\n"));
        ensure_equals("sent elsewhere, then back", fixed("ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1})\n"
                                                         "ll.SetLinkPrimitiveParamsFast(1, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                                         "LuauLintSlMergeablePrimParams",
                                                         "Write it ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1, PRIM_LINK_TARGET, 1, "
                                                         "PRIM_GLOW, ALL_SIDES, 0})",
                                                         false),
                      std::string("ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1, PRIM_LINK_TARGET, 1, PRIM_GLOW, ALL_SIDES, 0})\n"));
    }

    template<> template<>
    void object::test<16>()
    {
        set_test_name("SlMergeablePrimParams in LSL: the same, over the text, each call's ; but the last's taken with it");
        ensure("builtins: " + error, lslLoaded);
        const std::string said = found("default {\n"
                                       "    state_entry() {\n"
                                       "        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5]);\n"
                                       "        llSetLinkPrimitiveParamsFast(2, [PRIM_GLOW, ALL_SIDES, 0.0]);\n"
                                       "        llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.0]);\n"
                                       "        llSetPrimitiveParams([PRIM_POSITION, llGetPos()]);\n"
                                       "    }\n"
                                       "}\n",
                                       "SlMergeablePrimParams", ALScriptProblem::Severity::Note, false);
        ensure_equals("each", said, std::string("2 LSLSlMergeablePrimParams|2|llSetLinkPrimitiveParamsFast\n"));
        ensure_equals("a link changed", fixed("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5]);\n"
                                              "    llSetLinkPrimitiveParamsFast(2, [ PRIM_GLOW, ALL_SIDES, 0.0 ]);\n} }\n",
                                              "LSLSlMergeablePrimParams",
                                              "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0.0])",
                                              false, false),
                      std::string("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, "
                                  "PRIM_GLOW, ALL_SIDES, 0.0]);\n} }\n"));
        ensure_equals("no link", fixed("default { state_entry() { llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5]); llSetPrimitiveParams([PRIM_COLOR, "
                                       "ALL_SIDES, <1, 0, 0>, 1.0]); } }\n",
                                       "LSLSlMergeablePrimParams", "Write it llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5, PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0])",
                                       false, false),
                      std::string("default { state_entry() { llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5, PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0]); } }\n"));
    }

    template<> template<>
    void object::test<17>()
    {
        set_test_name("SlCostlyListen, SlFastTimer, SlFastSensor in both languages: notes, with no fix; not a listen narrowed, a timer "
                      "stopped or of a tenth, nor a sensor once a second");
        ensure("definitions: " + error, loaded);
        ensure("builtins: " + error, lslLoaded);
        const auto three = [&](const std::string& script, bool lua) {
            return found(script, "SlCostlyListen", ALScriptProblem::Severity::Note, lua) +
                   found(script, "SlFastTimer", ALScriptProblem::Severity::Note, lua) +
                   found(script, "SlFastSensor", ALScriptProblem::Severity::Note, lua);
        };
        ensure_equals("SLua", three("ll.Listen(0, \"\", NULL_KEY, \"\")\n"
                                    "ll.Listen(PUBLIC_CHANNEL, \"\", \"\", \"hello\")\n"
                                    "ll.Listen(0, \"Bob\", NULL_KEY, \"\")\n"
                                    "ll.Listen(7, \"\", NULL_KEY, \"\")\n"
                                    "LLTimers:every(0.05, function() end)\n"
                                    "llcompat.SetTimerEvent(0.02)\n"
                                    "LLTimers:every(0.1, function() end)\n"
                                    "llcompat.SetTimerEvent(0)\n"
                                    "ll.SensorRepeat(\"\", NULL_KEY, AGENT, 10, PI, 0.5)\n"
                                    "ll.SensorRepeat(\"\", NULL_KEY, AGENT, 10, PI, 1)\n",
                                    true),
                      std::string("0 LuauLintSlCostlyListen\n"
                                  "1 LuauLintSlCostlyListen\n"
                                  "4 LuauLintSlFastTimer|0.05\n"
                                  "5 LuauLintSlFastTimer|0.02\n"
                                  "8 LuauLintSlFastSensor|0.5\n"));
        ensure_equals("LSL", three("default {\n"
                                   "    state_entry() {\n"
                                   "        llListen(PUBLIC_CHANNEL, \"\", NULL_KEY, \"\");\n"
                                   "        llListen(0, \"\", llGetOwner(), \"\");\n"
                                   "        llSetTimerEvent(0.05);\n"
                                   "        llSetTimerEvent(0.0);\n"
                                   "        llSensorRepeat(\"\", NULL_KEY, AGENT, 10.0, PI, 0.25);\n"
                                   "    }\n"
                                   "}\n",
                                   false),
                      std::string("2 LSLSlCostlyListen\n"
                                  "4 LSLSlFastTimer|0.05\n"
                                  "6 LSLSlFastSensor|0.25\n"));
    }

    template<> template<>
    void object::test<18>()
    {
        set_test_name("SlStringBuild: a string joined to in a loop, from outside it, once for each string at its outermost loop, in both "
                      "languages; in SLua put in a table where it is a local of the block read in the loop by its appends alone");
        ensure("definitions: " + error, loaded);
        ensure("builtins: " + error, lslLoaded);
        ensure_equals("SLua", found("local s = \"\"\n"
                                    "for i = 1, 3 do s ..= tostring(i) end\n"
                                    "local t = \"\"\n"
                                    "for i = 1, 3 do t = t .. i; print(t) end\n"
                                    "g = \"\"\n"
                                    "while #g < 3 do g ..= \"x\" end\n"
                                    "local u = \"\"\n"
                                    "for i = 1, 2 do for j = 1, 2 do u ..= \"y\" end end\n"
                                    "for i = 1, 2 do local v = \"\"; v ..= \"z\"; print(v) end\n"
                                    "print(s, u)\n",
                                    "SlStringBuild", ALScriptProblem::Severity::Note),
                      std::string("1 LuauLintSlStringBuild|s\n"
                                  "3 LuauLintSlStringBuild|t\n"
                                  "5 LuauLintSlStringBuild|g\n"
                                  "7 LuauLintSlStringBuild|u\n"));
        ensure_equals("in a table", fixed("local s = \"\"\nfor i = 1, 3 do s ..= tostring(i) end\nprint(s)\n", "LuauLintSlStringBuild",
                                          "Put s's pieces in a table, joined once after the loop", false),
                      std::string("local s = \"\"\nlocal sParts = {}\nfor i = 1, 3 do table.insert(sParts, tostring(i)) end\ns ..= table.concat(sParts)\nprint(s)\n"));
        ensure_equals("named anew, indented", fixed("local function f()\n    local s = \"\"\n    local sParts = 1\n    for i = 1, 3 do\n        s = s .. i\n    end\n"
                                                    "    return s, sParts\nend\nprint(f())\n",
                                                    "LuauLintSlStringBuild", "Put s's pieces in a table, joined once after the loop", false),
                      std::string("local function f()\n    local s = \"\"\n    local sParts = 1\n    local sParts2 = {}\n    for i = 1, 3 do\n"
                                  "        table.insert(sParts2, tostring(i))\n    end\n    s ..= table.concat(sParts2)\n    return s, sParts\nend\nprint(f())\n"));
        unfixed("local t = \"\"\nfor i = 1, 3 do t = t .. i; print(t) end\n", "LuauLintSlStringBuild");
        unfixed("local function f()\n    local s = \"\"\n    for i = 1, 3 do\n        s ..= i\n        if i > 1 then return s end\n    end\n    return s\nend\nprint(f())\n",
                "LuauLintSlStringBuild");
        ensure_equals("LSL", found("default {\n"
                                   "    state_entry() {\n"
                                   "        string s;\n"
                                   "        integer i;\n"
                                   "        for (i = 0; i < 3; ++i) s += (string)i;\n"
                                   "        string t;\n"
                                   "        while (llStringLength(t) < 3) t = t + \"x\";\n"
                                   "        for (i = 0; i < 3; ++i) { string u; u += \"y\"; llOwnerSay(u); }\n"
                                   "        llOwnerSay(s + t);\n"
                                   "    }\n"
                                   "}\n",
                                   "SlStringBuild", ALScriptProblem::Severity::Note, false),
                      std::string("4 LSLSlStringBuild|s\n"
                                  "6 LSLSlStringBuild|t\n"));
        ensure("the converter's note names it", std::string(ALLSLToSLua::lintOf("SluaStringBuild")) == "SlStringBuild");
    }

    template<> template<>
    void object::test<19>()
    {
        set_test_name("SlRepeatedCall in SLua: ll.GetOwner() and its kin called again in one function's body, not a nested one's; a "
                      "note, fixed as a local before the body's first statement that calls it");
        ensure("definitions: " + error, loaded);
        ensure_equals("each", found("local function report()\n"
                                    "    print(ll.GetOwner())\n"
                                    "    if ll.GetOwner() == ll.GetKey() then print(ll.GetOwner()) end\n"
                                    "    print(ll.GetKey())\n"
                                    "end\n"
                                    "print(ll.GetOwner(), llcompat.GetOwner())\n"
                                    "LLEvents:on(\"touch_start\", function() print(ll.GetScriptName()) print(ll.GetScriptName()) end)\n"
                                    "print(report)\n",
                                    "SlRepeatedCall", ALScriptProblem::Severity::Note),
                      std::string("1 LuauLintSlRepeatedCall|ll.GetOwner()|3\n"
                                  "2 LuauLintSlRepeatedCall|ll.GetKey()|2\n"
                                  "6 LuauLintSlRepeatedCall|ll.GetScriptName()|2\n"));
        ensure_equals("kept", fixed("local function report()\n    print(ll.GetOwner())\n    print(ll.GetOwner())\nend\nprint(report)\n",
                                    "LuauLintSlRepeatedCall", "Keep ll.GetOwner() in a local, owner", false),
                      std::string("local function report()\n    local owner = ll.GetOwner()\n    print(owner)\n    print(owner)\nend\nprint(report)\n"));
    }

    template<> template<>
    void object::test<20>()
    {
        set_test_name("SlRepeatedCall in LSL: a steady call, and a pure one given what the body never changes, declared by the body before; "
                      "not one given a loop's counter");
        ensure("builtins: " + error, lslLoaded);
        ensure_equals("each", found("default {\n"
                                    "    touch_start(integer n) {\n"
                                    "        string s = llDetectedName(0);\n"
                                    "        llOwnerSay((string)llGetOwner());\n"
                                    "        if (llGetOwner() == llDetectedKey(0)) llOwnerSay(llToUpper(s) + llToUpper(s));\n"
                                    "        integer i;\n"
                                    "        for (i = 0; i < 2; ++i) llOwnerSay(llGetSubString(s, i, i) + llGetSubString(s, i, i));\n"
                                    "    }\n"
                                    "}\n",
                                    "SlRepeatedCall", ALScriptProblem::Severity::Note, false),
                      std::string("3 LSLSlRepeatedCall|llGetOwner()|2\n"
                                  "4 LSLSlRepeatedCall|llToUpper(s)|2\n"));
        ensure_equals("a steady one", fixed("default { touch_start(integer n) {\n    llOwnerSay((string)llGetOwner());\n    llOwnerSay((string)llGetOwner());\n} }\n",
                                            "LSLSlRepeatedCall", "Keep llGetOwner() in a local, owner", false, false),
                      std::string("default { touch_start(integer n) {\n    key owner = llGetOwner();\n    llOwnerSay((string)owner);\n    llOwnerSay((string)owner);\n} }\n"));
        ensure_equals("a pure one", fixed("default { state_entry() {\n    string s = \"a\";\n    llOwnerSay(llToUpper(s) + llToUpper(s));\n} }\n",
                                          "LSLSlRepeatedCall", "Keep llToUpper(s) in a local, toUpper", false, false),
                      std::string("default { state_entry() {\n    string s = \"a\";\n    string toUpper = llToUpper(s);\n    llOwnerSay(toUpper + toUpper);\n} }\n"));
    }
}
