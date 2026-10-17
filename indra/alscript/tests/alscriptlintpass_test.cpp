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

#include "../lsl/allslservice.h"
#include "../lsl/allsltoslua.h"
#include "../luau/alluauservice.h"
#include "../lint/alscriptfixes.h"
#include "../lint/alscriptlintpass.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    struct alscriptlintpass_data
    {
        ALLuauService luau;
        ALLSLService  lsl;
        // Each load's own: one shared would say the last load's error under
        // another's name.
        std::string   definitionsError;
        std::string   builtinsError;
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
            luau.setNewSolver(newSolver, definitionsError);
            loaded    = luau.loadDefinitions(text.str(), definitionsError);
            lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", builtinsError);
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
        std::string require_fixed(const std::string& script, const std::string& key, const std::string& title, bool safe, bool lua = true)
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
            REQUIRE(fix.safe == safe);
            const std::optional<std::string> made = ALScriptFixes::apply(script, fix);
            REQUIRE(made.has_value());
            const ALScriptProblems after = check(*made, lua);
            INFO("made:\n" + *made + "\nsaid then:\n" + said(after));
            REQUIRE(keyed(after, key) == nullptr);
            UNSCOPED_INFO("no error in its place");
            REQUIRE(errors(after) <= errors(problems) - (problem->severity == ALScriptProblem::Severity::Error ? 1 : 0));
            return *made;
        }

        // The problem keyed so is said, and offers no fix.
        void require_unfixed(const std::string& script, const std::string& key, bool lua = true)
        {
            const ALScriptProblems problems = check(script, lua);
            const ALScriptProblem* problem  = keyed(problems, key);
            CAPTURE(key);
            INFO("said:\n" + said(problems));
            REQUIRE(problem != nullptr);
            REQUIRE(problem->fixes.empty());
        }
    };

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlTableCompare: a table compared with {}; with items or a string no fix", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
        CHECK(said ==
                      std::string("1 LuauLintSlTableCompare|t|#t == 0\n"
                                  "2 LuauLintSlTableCompareAlways|t|#t > 0\n"
                                  "4 LuauLintSlTableCompare|d|next(d) == nil\n"
                                  "5 LuauLintSlTableCompareItems|t\n"
                                  "7 LuauLintSlTableCompareItems|s\n"
                                  "8 LuauLintSlTableCompareAlways|l|next(l) ~= nil\n"));
        {
            INFO("a list");
            CHECK(require_fixed("local t = {1}\nif t == {} then print(t) end\n", "LuauLintSlTableCompare", "Write it #t == 0", false) ==
                  std::string("local t = {1}\nif #t == 0 then print(t) end\n"));
        }
        {
            INFO("a table, on the right, bracketed");
            CHECK(require_fixed("local d = {a = 1}\nprint({} ~= (d))\n", "LuauLintSlTableCompareAlways", "Write it next(d) ~= nil", false) ==
                  std::string("local d = {a = 1}\nprint(next(d) ~= nil)\n"));
        }
        require_unfixed("local t = {1}\nprint(t == {1})\n", "LuauLintSlTableCompareItems");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlZeroIndex: counting from 0 in lists and loops and subs and finds", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
        CHECK(said ==
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
        {
            INFO("a list");
            CHECK(require_fixed("local t = {1}\nprint(t[0])\n", "LuauLintSlZeroIndex", "Write it t[1]", false) ==
                  std::string("local t = {1}\nprint(t[1])\n"));
        }
        {
            INFO("a loop");
            CHECK(require_fixed("local t = {1}\nfor i = 0, #t - 1 do print(t[i]) end\n", "LuauLintSlZeroIndexLoop", "Write it for i = 1, #t", false) ==
                  std::string("local t = {1}\nfor i = 1, #t do print(t[i]) end\n"));
        }
        {
            INFO("a method to a length less 1");
            CHECK(require_fixed("local s = \"abc\"\nprint(s:sub(0, #s - 1))\n", "LuauLintSlZeroIndexSub", "Write it s:sub(1, #s)", false) ==
                  std::string("local s = \"abc\"\nprint(s:sub(1, #s))\n"));
        }
        {
            INFO("string's, to a number");
            CHECK(require_fixed("print(string.sub(\"abc\", 0, 2))\n", "LuauLintSlZeroIndexSub", "Write it string.sub(\"abc\", 1, 3)", false) ==
                  std::string("print(string.sub(\"abc\", 1, 3))\n"));
        }
        {
            INFO("ll's, one from the end kept");
            CHECK(require_fixed("print(ll.GetSubString(\"abc\", 0, -1))\n", "LuauLintSlZeroIndexArg",
                                "Write it ll.GetSubString(\"abc\", 1, -1)", false) ==
                  std::string("print(ll.GetSubString(\"abc\", 1, -1))\n"));
        }
        {
            INFO("a find");
            CHECK(require_fixed("print(ll.SubStringIndex(\"ab\", \"a\") == 0)\n", "LuauLintSlZeroIndexFound", "Write it ll.SubStringIndex(...) == 1", false) ==
                  std::string("print(ll.SubStringIndex(\"ab\", \"a\") == 1)\n"));
        }
        require_unfixed("local t = {1}\nfor i = 0, #t - 1 do print(i, t[i]) end\n", "LuauLintSlZeroIndexLoop");
        require_unfixed("local s = \"abc\"\nlocal n = 2\nprint(s:sub(0, n))\n", "LuauLintSlZeroIndexSub");
        require_unfixed("local s = \"abc\"\nprint(ll.GetSubString(s, 0, #s))\n", "LuauLintSlZeroIndexArg");
        UNSCOPED_INFO("only a table built as a list: not a map keyed by number, whose [0] may well be something, nor one given {}");
        CHECK(found("local handlers: {[number]: () -> ()} = {}\n"
                    "local function first(t: {string}) return t[0] end\n"
                    "local m = {}\n"
                    "local l = {\"a\"}\n"
                    "l = {\"b\", \"c\"}\n"
                    "print(handlers[0], m[0], l[0], string.split(\"a,b\", \",\")[0])\n",
                    "SlZeroIndex", ALScriptProblem::Severity::Warning) ==
              std::string("1 LuauLintSlZeroIndex|t\n"
                          "5 LuauLintSlZeroIndex|l\n"
                          "5 LuauLintSlZeroIndex|string.split(\"a,b\", \",\")\n"));
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlCompatCall: llcompat's where ll's means the same; not where it differs", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
                                       "print(n, g, llcompat.ListFindList({1}, {1}) == -1)\n"
                                       "print(llcompat.List2Json(JSON_ARRAY, {s}))\n",
                                       "SlCompatCall", ALScriptProblem::Severity::Note);
        CHECK(said ==
                      std::string("1 LuauLintSlCompatCall|llcompat.Say|ll.Say(...)\n"
                                  "2 LuauLintSlCompatCall|llcompat.GetSubString|ll.GetSubString(s, 1, -1)\n"
                                  "4 LuauLintSlCompatCall|llcompat.SameGroup|ll.SameGroup(...)\n"
                                  "5 LuauLintSlCompatCall|llcompat.SameGroup|not ll.SameGroup(...)\n"
                                  "6 LuauLintSlCompatCall|llcompat.SubStringIndex|ll.SubStringIndex(...) ~= nil\n"
                                  "7 LuauLintSlCompatCall|llcompat.SubStringIndex|ll.SubStringIndex(...) == nil\n"
                                  "8 LuauLintSlCompatCall|llcompat.SameGroup|ll.SameGroup(...)\n"
                                  "12 LuauLintSlCompatCall|llcompat.List2Json|ll.List2Json(...)\n"));
        {
            INFO("the same");
            CHECK(require_fixed("llcompat.Say(0, \"hi\")\n", "LuauLintSlCompatCall", "Write it ll.Say(...)", true) ==
                  std::string("ll.Say(0, \"hi\")\n"));
        }
        {
            INFO("indexes");
            CHECK(require_fixed("print(llcompat.GetSubString(\"abc\", 0, 1))\n", "LuauLintSlCompatCall", "Write it ll.GetSubString(\"abc\", 1, 2)", true) ==
                  std::string("print(ll.GetSubString(\"abc\", 1, 2))\n"));
        }
        {
            INFO("a boolean on the right");
            CHECK(require_fixed("print(0 == llcompat.SameGroup(ll.GetOwner()))\n", "LuauLintSlCompatCall", "Write it not ll.SameGroup(...)", true) ==
                  std::string("print(not ll.SameGroup(ll.GetOwner()))\n"));
        }
        {
            INFO("a boolean");
            CHECK(require_fixed("print(llcompat.SameGroup(ll.GetOwner()) ~= 0)\n", "LuauLintSlCompatCall", "Write it ll.SameGroup(...)", true) ==
                  std::string("print(ll.SameGroup(ll.GetOwner()))\n"));
        }
        {
            INFO("a find, first");
            CHECK(require_fixed("print(llcompat.SubStringIndex(\"ab\", \"a\") == 0)\n", "LuauLintSlCompatCall", "Write it ll.SubStringIndex(...) == 1", true) ==
                  std::string("print(ll.SubStringIndex(\"ab\", \"a\") == 1)\n"));
        }
        {
            INFO("a find, on the right");
            CHECK(require_fixed("print(-1 < llcompat.SubStringIndex(\"ab\", \"a\"))\n", "LuauLintSlCompatCall", "Write it ll.SubStringIndex(...) ~= nil", true) ==
                  std::string("print(ll.SubStringIndex(\"ab\", \"a\") ~= nil)\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlBooleanNumber: a boolean against a number; an if of 1 and 0 a note", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local on = false\n"
                                       "local n = 1\n"
                                       "if on == 1 or 0 ~= on then print(1) end\n"
                                       "if ll.SameGroup(ll.GetOwner()) == 0 or (n > 2) == 5 then print(2) end\n"
                                       "if (if on then 1 else 0) == 1 or (if n > 2 then 0 else 1) ~= 0 then print(3) end\n"
                                       "if n == 1 or (if on then 2 else 0) == 1 then print(4) end\n",
                                       "SlBooleanNumber", ALScriptProblem::Severity::Error);
        CHECK(said ==
                      std::string("2 LuauLintSlBooleanNumber|on|1\n"
                                  "2 LuauLintSlBooleanNumberAlways|on|0\n"
                                  "3 LuauLintSlBooleanNumber|ll.SameGroup(ll.GetOwner())|0\n"
                                  "3 LuauLintSlBooleanNumber|(n > 2)|5\n"
                                  "4 LuauLintSlBooleanNumberChoice|(if on then 1 else 0) == 1|on (another severity)\n"
                                  "4 LuauLintSlBooleanNumberChoice|(if n > 2 then 0 else 1) ~= 0|not (n > 2) (another severity)\n"));
        {
            INFO("true");
            CHECK(require_fixed("local on = false\nprint(on == 1)\n", "LuauLintSlBooleanNumber", "Write it on", false) ==
                  std::string("local on = false\nprint(on)\n"));
        }
        {
            INFO("false, on the right");
            CHECK(require_fixed("print(0 == ll.SameGroup(ll.GetOwner()))\n", "LuauLintSlBooleanNumber", "Write it not ll.SameGroup(ll.GetOwner())", false) ==
                  std::string("print(not ll.SameGroup(ll.GetOwner()))\n"));
        }
        {
            INFO("a choice");
            CHECK(require_fixed("local n = 1\nprint((if n > 2 then 0 else 1) ~= 0)\n", "LuauLintSlBooleanNumberChoice", "Write it not (n > 2)", false) ==
                  std::string("local n = 1\nprint(not (n > 2))\n"));
        }
        {
            INFO("an operand of what is around it, bracketed");
            CHECK(require_fixed("local x = true\nlocal a = false\nlocal b = true\nprint(x and (if a or b then 1 else 0) == 1)\n",
                                "LuauLintSlBooleanNumberChoice", "Write it (a or b)", false) ==
                  std::string("local x = true\nlocal a = false\nlocal b = true\nprint(x and (a or b))\n"));
        }
        require_unfixed("local n = 1\nprint((n > 2) == 5)\n", "LuauLintSlBooleanNumber");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlGlobalAssign: globals and global functions made local; not with _G", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string script = "count = 0\n"
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
                                   "count = 1\n";
        const std::string said = found(script, "SlGlobalAssign", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("0 LuauLintSlGlobalAssign|count (another severity)\n"
                                  "1 LuauLintSlGlobalInFunction|total (another severity)\n"
                                  "3 LuauLintSlGlobalAssign|later (another severity)\n"
                                  "6 LuauLintSlGlobalFunctionInScope|inner\n"
                                  "7 LuauLintSlGlobalFunctionInScope|shared\n"
                                  "10 LuauLintSlGlobalAssign|x, y (another severity)\n"));
        UNSCOPED_INFO("the function at the top");
        CHECK(found(script, "SlGlobalFunction", ALScriptProblem::Severity::Note) ==
                      std::string("1 LuauLintSlGlobalFunction|bump\n"));
        UNSCOPED_INFO("off on its own: the rest still said");
        CHECK(found("--!nolint SlGlobalFunction\n" + script, "SlGlobalAssign", ALScriptProblem::Severity::Warning) ==
                      std::string("1 LuauLintSlGlobalAssign|count (another severity)\n"
                                  "2 LuauLintSlGlobalInFunction|total (another severity)\n"
                                  "4 LuauLintSlGlobalAssign|later (another severity)\n"
                                  "7 LuauLintSlGlobalFunctionInScope|inner\n"
                                  "8 LuauLintSlGlobalFunctionInScope|shared\n"
                                  "11 LuauLintSlGlobalAssign|x, y (another severity)\n"));
        UNSCOPED_INFO("and it not");
        REQUIRE(found("--!nolint SlGlobalFunction\n" + script, "SlGlobalFunction", ALScriptProblem::Severity::Note).empty());
        UNSCOPED_INFO("nor the function where SlGlobalAssign alone is off");
        CHECK(found("--!nolint SlGlobalAssign\n" + script, "SlGlobalFunction", ALScriptProblem::Severity::Note) == "2 LuauLintSlGlobalFunction|bump\n");
        {
            INFO("in place");
            CHECK(require_fixed("count = 0\nprint(count)\n", "LuauLintSlGlobalAssign", "Write it local count", false) ==
                  std::string("local count = 0\nprint(count)\n"));
        }
        {
            INFO("declared first");
            CHECK(require_fixed("local function early() return later end\nlater = 5\nprint(early())\n", "LuauLintSlGlobalAssign",
                                "Declare local later before it is first named", false) ==
                  std::string("local later\nlocal function early() return later end\nlater = 5\nprint(early())\n"));
        }
        {
            INFO("a function");
            CHECK(require_fixed("function f() return 1 end\nprint(f())\n", "LuauLintSlGlobalFunction", "Write it local function f", false) ==
                  std::string("local function f() return 1 end\nprint(f())\n"));
        }
        {
            INFO("in a block");
            CHECK(require_fixed("local function outer()\n    function inner() return 1 end\n    return inner()\nend\nprint(outer())\n",
                                "LuauLintSlGlobalFunctionInScope", "Write it local function inner", false) ==
                  std::string("local function outer()\n    local function inner() return 1 end\n    return inner()\nend\nprint(outer())\n"));
        }
        require_unfixed("local function outer()\n    function shared() return 2 end\nend\nprint(outer, shared)\n", "LuauLintSlGlobalFunctionInScope");
        require_unfixed("print(_G)\ny = 2\n", "LuauLintSlGlobalAssign");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlParenCondition: a whole condition in brackets; taken out safely", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local x = true\n"
                                       "if (x) then print(1) elseif (not x) then print(2) end\n"
                                       "while(x)do break end\n"
                                       "repeat local y = 1 until (y > 0)\n"
                                       "print(if (x) then 1 else 2)\n"
                                       "if (x) and (x) then print(3) end\n"
                                       "if x then print(4) end\n",
                                       "SlParenCondition", ALScriptProblem::Severity::Note);
        CHECK(said ==
                      std::string("1 LuauLintSlParenCondition|if\n"
                                  "1 LuauLintSlParenCondition|elseif\n"
                                  "2 LuauLintSlParenCondition|while\n"
                                  "3 LuauLintSlParenCondition|until\n"
                                  "4 LuauLintSlParenCondition|if\n"));
        {
            INFO("spaced");
            CHECK(require_fixed("local x = true\nif (x) then print(1) end\n", "LuauLintSlParenCondition", "Take out the brackets", true) ==
                  std::string("local x = true\nif x then print(1) end\n"));
        }
        {
            INFO("run on");
            CHECK(require_fixed("local x = true\nwhile(x)do break end\n", "LuauLintSlParenCondition", "Take out the brackets", true) ==
                  std::string("local x = true\nwhile x do break end\n"));
        }
        UNSCOPED_INFO("allowed by selene's name");
        CHECK(found("local x = true\n-- selene: allow(parenthese_conditions)\nif (x) then print(1) end\n",
                                                        "SlParenCondition", ALScriptProblem::Severity::Note) ==
                      std::string());
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlAlmostSwapped: a = b then b = a is a warning fixed as a swap", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
        CHECK(said ==
                      std::string("1 LuauLintSlAlmostSwapped|a|b|a, b = b, a\n"
                                  "4 LuauLintSlAlmostSwapped|t.x|t.y|t.x, t.y = t.y, t.x\n"));
        {
            INFO("swapped");
            CHECK(require_fixed("local a, b = 1, 2\na = b\nb = a\nprint(a, b)\n", "LuauLintSlAlmostSwapped", "Write it a, b = b, a", false) ==
                  std::string("local a, b = 1, 2\na, b = b, a\nprint(a, b)\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlMustUse: an answer left unread; given back to the first argument", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
        CHECK(said ==
                      std::string("2 LuauLintSlMustUse|ll.DeleteSubList\n"
                                  "3 LuauLintSlMustUse|string.upper\n"
                                  "4 LuauLintSlMustUse|s:lower\n"
                                  "5 LuauLintSlMustUse|string.len\n"
                                  "6 LuauLintSlMustUse|tostring\n"
                                  "7 LuauLintSlMustUse|llcompat.GetPos\n"));
        {
            INFO("a list");
            CHECK(require_fixed("local l = {1, 2}\nll.DeleteSubList(l, 1, 1)\nprint(l)\n", "LuauLintSlMustUse", "Write it l = ll.DeleteSubList(...)", false) ==
                  std::string("local l = {1, 2}\nl = ll.DeleteSubList(l, 1, 1)\nprint(l)\n"));
        }
        {
            INFO("a method");
            CHECK(require_fixed("local s = \"abc\"\ns:upper()\nprint(s)\n", "LuauLintSlMustUse", "Write it s = s:upper(...)", false) ==
                  std::string("local s = \"abc\"\ns = s:upper()\nprint(s)\n"));
        }
        require_unfixed("local s = \"abc\"\nstring.len(s)\n", "LuauLintSlMustUse");
        require_unfixed("string.upper(\"abc\")\n", "LuauLintSlMustUse");
        UNSCOPED_INFO("gsub given a function or a table is called for each match: its answer is not its point");
        CHECK(found("local text = \"a b\"\n"
                    "text:gsub(\"%S+\", function(w) print(w) end)\n"
                    "string.gsub(text, \"%S+\", print)\n"
                    "text:gsub(\"%S+\", { a = \"b\" })\n"
                    "text:gsub(\"a\", \"b\")\n"
                    "string.gsub(text, \"a\", (\"b\"))\n",
                    "SlMustUse", ALScriptProblem::Severity::Warning) ==
              std::string("4 LuauLintSlMustUse|text:gsub\n"
                          "5 LuauLintSlMustUse|string.gsub\n"));
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlGeneralizedFor: pairs and ipairs and next in a for become the table", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local t = {1, 2}\n"
                                       "for k, v in pairs(t) do print(k, v) end\n"
                                       "for i, v in ipairs(t) do print(i, v) end\n"
                                       "for k, v in next, t do print(k, v) end\n"
                                       "local function walk(x) return next, x end\n"
                                       "for k, v in walk(t) do print(k, v) end\n"
                                       "for k, v in t do print(k, v) end\n",
                                       "SlGeneralizedFor", ALScriptProblem::Severity::Note);
        CHECK(said ==
                      std::string("1 LuauLintSlGeneralizedFor|t|pairs(t)\n"
                                  "2 LuauLintSlGeneralizedForList|t|ipairs(t)\n"
                                  "3 LuauLintSlGeneralizedFor|t|next, t\n"));
        {
            INFO("pairs");
            CHECK(require_fixed("local t = {1}\nfor k, v in pairs(t) do print(k, v) end\n", "LuauLintSlGeneralizedFor", "Write it in t", false) ==
                  std::string("local t = {1}\nfor k, v in t do print(k, v) end\n"));
        }
        {
            INFO("next");
            CHECK(require_fixed("local t = {1}\nfor k, v in next, t do print(k, v) end\n", "LuauLintSlGeneralizedFor", "Write it in t", false) ==
                  std::string("local t = {1}\nfor k, v in t do print(k, v) end\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlEmptyBlock: an empty block is a warning; an empty then or else fixed", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
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
        CHECK(said ==
                      std::string("1 LuauLintSlEmptyBlock|if\n"
                                  "2 LuauLintSlEmptyBlock|if\n"
                                  "3 LuauLintSlEmptyBlock|elseif\n"
                                  "4 LuauLintSlEmptyBlock|else\n"
                                  "5 LuauLintSlEmptyBlock|while\n"
                                  "6 LuauLintSlEmptyBlock|for\n"
                                  "7 LuauLintSlEmptyBlock|for\n"
                                  "8 LuauLintSlEmptyBlock|repeat\n"));
        {
            INFO("turned round");
            CHECK(require_fixed("local c = true\nif c then else print(1) end\n", "LuauLintSlEmptyBlock", "Write it if not c then", false) ==
                  std::string("local c = true\nif not c then print(1) end\n"));
        }
        {
            INFO("an else");
            CHECK(require_fixed("local c = true\nif c then print(3) else end\n", "LuauLintSlEmptyBlock", "Take out the empty else", true) ==
                  std::string("local c = true\nif c then print(3) end\n"));
        }
        {
            INFO("an else on its own line");
            CHECK(require_fixed("local c = true\nif c then\n    print(3)\nelse\nend\n", "LuauLintSlEmptyBlock", "Take out the empty else", true) ==
                  std::string("local c = true\nif c then\n    print(3)\nend\n"));
        }
        UNSCOPED_INFO("selene's names");
        CHECK(found("local c = true\n-- selene: allow(empty_loop)\nwhile c do end\n-- selene: allow(empty_if)\nif c then end\n",
                                              "SlEmptyBlock", ALScriptProblem::Severity::Warning) ==
                      std::string());
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlIndexDivision: a / in a list's index is a warning fixed as //", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local t = {1, 2, 3}\n"
                                       "local n = #t\n"
                                       "print(t[n / 2], t[(n + 1) / 2], t[n / 2 + 1], t[n // 2])\n"
                                       "t[n / 2] = 5\n"
                                       "local d = {a = 1}\n"
                                       "print(d[n / 2], string.rep(\"x\", n / 2))\n",
                                       "SlIndexDivision", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("2 LuauLintSlIndexDivision|n / 2|n // 2\n"
                                  "2 LuauLintSlIndexDivision|(n + 1) / 2|(n + 1) // 2\n"
                                  "2 LuauLintSlIndexDivision|n / 2|n // 2\n"
                                  "3 LuauLintSlIndexDivision|n / 2|n // 2\n"));
        {
            INFO("rounded down");
            CHECK(require_fixed("local t = {1}\nlocal n = #t\nprint(t[n / 2])\n", "LuauLintSlIndexDivision", "Write it n // 2", false) ==
                  std::string("local t = {1}\nlocal n = #t\nprint(t[n // 2])\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlVectorProduct: vectors' * as a number is vector.dot; % is vector.cross", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local a = vector(1, 0, 0)\n"
                                       "local b = vector(0, 1, 0)\n"
                                       "if a * b > 0.5 then print(1) end\n"
                                       "print(math.acos(a * b), (a * b) + 1)\n"
                                       "local c = a % b\n"
                                       "local s = a * b\n"
                                       "print(a * 2, s, c)\n",
                                       "SlVectorProduct", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("2 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "3 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "3 LuauLintSlVectorProduct|a * b|a|b\n"
                                  "4 LuauLintSlVectorCross|a % b|a|b (another severity)\n"));
        UNSCOPED_INFO("a note");
        CHECK(found("local a = vector(1, 0, 0)\nlocal c = a % a\nprint(c)\n", "SlVectorProduct", ALScriptProblem::Severity::Note) ==
                      std::string("1 LuauLintSlVectorCross|a % a|a|a\n"));
        {
            INFO("dot");
            CHECK(require_fixed("local a = vector(1, 0, 0)\nprint(a * a > 0.5)\n", "LuauLintSlVectorProduct", "Write it vector.dot(a, a)", false) ==
                  std::string("local a = vector(1, 0, 0)\nprint(vector.dot(a, a) > 0.5)\n"));
        }
        {
            INFO("cross");
            CHECK(require_fixed("local a = vector(1, 0, 0)\nlocal c = a % a\nprint(c)\n", "LuauLintSlVectorCross", "Write it vector.cross(a, a)", false) ==
                  std::string("local a = vector(1, 0, 0)\nlocal c = vector.cross(a, a)\nprint(c)\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlSleepingCall in SLua: a sleeping call warned in loops; fixed as Fast", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local v = vector(1, 2, 3)\n"
                                       "ll.SetPos(v)\n"
                                       "for i = 1, 3 do ll.SetRot(quaternion(0, 0, 0, 1)) end\n"
                                       "LLTimers:every(1, function() ll.SetPrimitiveParams({PRIM_POSITION, v}) end)\n"
                                       "ll.SetTexture(\"x\", 0)\n"
                                       "ll.SetLinkRenderMaterial(LINK_THIS, \"m\", 0)\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {})\n"
                                       "llcompat.SetPos(v)\n",
                                       "SlSleepingCall", ALScriptProblem::Severity::Note);
        CHECK(said ==
                      std::string("1 LuauLintSlSleepingCall|ll.SetPos|0.2|ll.SetLinkPrimitiveParamsFast\n"
                                  "2 LuauLintSlSleepingCallOften|ll.SetRot|0.2|ll.SetLinkPrimitiveParamsFast (another severity)\n"
                                  "3 LuauLintSlSleepingCallOften|ll.SetPrimitiveParams|0.2|ll.SetLinkPrimitiveParamsFast (another severity)\n"
                                  "4 LuauLintSlSleepingTexture|ll.SetTexture|0.2|PRIM_TEXTURE\n"
                                  "5 LuauLintSlSleepingCall|ll.SetLinkRenderMaterial|0.2|ll.SetLinkPrimitiveParamsFast\n"
                                  "7 LuauLintSlSleepingCall|llcompat.SetPos|0.2|llcompat.SetLinkPrimitiveParamsFast\n"));
        {
            INFO("put in");
            CHECK(require_fixed("local v = vector(1, 2, 3)\nll.SetPos(v)\n", "LuauLintSlSleepingCall",
                                "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, v})", false) ==
                  std::string("local v = vector(1, 2, 3)\nll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, v})\n"));
        }
        {
            INFO("a link first, in a timer");
            CHECK(require_fixed("LLTimers:every(1, function() ll.SetPrimitiveParams({}) end)\n", "LuauLintSlSleepingCallOften",
                                "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {})", false) ==
                  std::string("LLTimers:every(1, function() ll.SetLinkPrimitiveParamsFast(LINK_THIS, {}) end)\n"));
        }
        {
            INFO("written again");
            CHECK(require_fixed("ll.SetLinkRenderMaterial(LINK_THIS, \"m\", 0)\n", "LuauLintSlSleepingCall",
                                "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_RENDER_MATERIAL, 0, \"m\"})", false) ==
                  std::string("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_RENDER_MATERIAL, 0, \"m\"})\n"));
        }
        require_unfixed("ll.SetTexture(\"x\", 0)\n", "LuauLintSlSleepingTexture");
        require_unfixed("ll.SetRenderMaterial(tostring(1), 0)\n", "LuauLintSlSleepingCall");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlSleepingCall in LSL: the same; fixed over commas and comments", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
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
        CHECK(said ==
                      std::string("2 LSLSlSleepingCall|llSetPos|0.2|llSetLinkPrimitiveParamsFast\n"
                                  "4 LSLSlSleepingCallOften|llSetRot|0.2|llSetLinkPrimitiveParamsFast (another severity)\n"
                                  "5 LSLSlSleepingCall|llSetLinkRenderMaterial|0.2|llSetLinkPrimitiveParamsFast\n"
                                  "6 LSLSlSleepingTexture|llSetTexture|0.2|PRIM_TEXTURE\n"
                                  "9 LSLSlSleepingCallOften|llSetPrimitiveParams|0.2|llSetLinkPrimitiveParamsFast (another severity)\n"));
        {
            INFO("put in");
            CHECK(require_fixed("default { state_entry() { llSetPos( <1, 2, 3> /* here */ ); } }\n", "LSLSlSleepingCall",
                                "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_POSITION, <1, 2, 3> /* here */])", false, false) ==
                  std::string("default { state_entry() { llSetLinkPrimitiveParamsFast( LINK_THIS, [PRIM_POSITION, <1, 2, 3> /* here */] ); } }\n"));
        }
        {
            INFO("written again");
            CHECK(require_fixed("default { state_entry() { llSetLinkRenderMaterial(LINK_THIS, \"m\", ALL_SIDES); } }\n", "LSLSlSleepingCall",
                                "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"])", false, false) ==
                  std::string("default { state_entry() { llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"]); } }\n"));
        }
        {
            INFO("a comment with a comma after an argument is no comma");
            CHECK(require_fixed("default { state_entry() { llSetLinkRenderMaterial(LINK_THIS, // m, sides\n    \"m\", ALL_SIDES); } }\n", "LSLSlSleepingCall",
                                "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"])", false, false) ==
                  std::string("default { state_entry() { llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_RENDER_MATERIAL, ALL_SIDES, \"m\"]); } }\n"));
        }
        require_unfixed("default { state_entry() { llSetRenderMaterial(llGetInventoryName(INVENTORY_MATERIAL, 0), 0); } }\n", "LSLSlSleepingCall", false);
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlMergeablePrimParams in SLua: a run of prim-params calls merged", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_COLOR, ALL_SIDES, vector(1, 0, 0), 1})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5})\n"
                                       "ll.SetLinkPrimitiveParamsFast(2, {PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "print(\"between\")\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_POSITION, ll.GetPos()})\n"
                                       "ll.SetPrimitiveParams({PRIM_GLOW, ALL_SIDES, 0})\n"
                                       "ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                       "SlMergeablePrimParams", ALScriptProblem::Severity::Note);
        CHECK(said == std::string("0 LuauLintSlMergeablePrimParams|3|ll.SetLinkPrimitiveParamsFast\n"));
        {
            INFO("a link changed");
            CHECK(require_fixed("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5})\nll.SetLinkPrimitiveParamsFast(2, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                "LuauLintSlMergeablePrimParams",
                                "Write it ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0})",
                                false) ==
                  std::string("ll.SetLinkPrimitiveParamsFast(LINK_THIS, {PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0})\n"));
        }
        {
            INFO("sent elsewhere, then back");
            CHECK(require_fixed("ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1})\n"
                                "ll.SetLinkPrimitiveParamsFast(1, {PRIM_GLOW, ALL_SIDES, 0})\n",
                                "LuauLintSlMergeablePrimParams",
                                "Write it ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1, PRIM_LINK_TARGET, 1, "
                                "PRIM_GLOW, ALL_SIDES, 0})",
                                false) ==
                  std::string("ll.SetLinkPrimitiveParamsFast(1, {PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 1, PRIM_LINK_TARGET, 1, PRIM_GLOW, ALL_SIDES, 0})\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlMergeablePrimParams in LSL: the same over the text keeping the last ;", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const std::string said = found("default {\n"
                                       "    state_entry() {\n"
                                       "        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5]);\n"
                                       "        llSetLinkPrimitiveParamsFast(2, [PRIM_GLOW, ALL_SIDES, 0.0]);\n"
                                       "        llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.0]);\n"
                                       "        llSetPrimitiveParams([PRIM_POSITION, llGetPos()]);\n"
                                       "    }\n"
                                       "}\n",
                                       "SlMergeablePrimParams", ALScriptProblem::Severity::Note, false);
        CHECK(said == std::string("2 LSLSlMergeablePrimParams|2|llSetLinkPrimitiveParamsFast\n"));
        {
            INFO("a link changed");
            CHECK(require_fixed("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5]);\n"
                                "    llSetLinkPrimitiveParamsFast(2, [ PRIM_GLOW, ALL_SIDES, 0.0 ]);\n} }\n",
                                "LSLSlMergeablePrimParams",
                                "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, PRIM_GLOW, ALL_SIDES, 0.0])",
                                false, false) ==
                  std::string("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5, PRIM_LINK_TARGET, 2, "
                              "PRIM_GLOW, ALL_SIDES, 0.0]);\n} }\n"));
        }
        {
            INFO("no link");
            CHECK(require_fixed("default { state_entry() { llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5]); llSetPrimitiveParams([PRIM_COLOR, "
                                "ALL_SIDES, <1, 0, 0>, 1.0]); } }\n",
                                "LSLSlMergeablePrimParams", "Write it llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5, PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0])",
                                false, false) ==
                  std::string("default { state_entry() { llSetPrimitiveParams([PRIM_GLOW, ALL_SIDES, 0.5, PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0]); } }\n"));
        }
        {
            INFO("a comment with a comma after an argument is no comma");
            CHECK(require_fixed("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, // colour, alpha\n"
                                "        [PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0]);\n"
                                "    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_GLOW, ALL_SIDES, 0.5]);\n} }\n",
                                "LSLSlMergeablePrimParams",
                                "Write it llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0, PRIM_GLOW, ALL_SIDES, 0.5])",
                                false, false) ==
                  std::string("default { state_entry() {\n    llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_COLOR, ALL_SIDES, <1, 0, 0>, 1.0, "
                              "PRIM_GLOW, ALL_SIDES, 0.5]);\n} }\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlCostlyListen and SlFastTimer and SlFastSensor: notes in both languages", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        const auto three = [&](const std::string& script, bool lua) {
            return found(script, "SlCostlyListen", ALScriptProblem::Severity::Note, lua) +
                   found(script, "SlFastTimer", ALScriptProblem::Severity::Note, lua) +
                   found(script, "SlFastSensor", ALScriptProblem::Severity::Note, lua);
        };
        UNSCOPED_INFO("SLua");
        CHECK(three("ll.Listen(0, \"\", NULL_KEY, \"\")\n"
                                    "ll.Listen(PUBLIC_CHANNEL, \"\", \"\", \"hello\")\n"
                                    "ll.Listen(0, \"Bob\", NULL_KEY, \"\")\n"
                                    "ll.Listen(7, \"\", NULL_KEY, \"\")\n"
                                    "LLTimers:every(0.05, function() end)\n"
                                    "llcompat.SetTimerEvent(0.02)\n"
                                    "LLTimers:every(0.1, function() end)\n"
                                    "llcompat.SetTimerEvent(0)\n"
                                    "ll.SensorRepeat(\"\", NULL_KEY, AGENT, 10, PI, 0.5)\n"
                                    "ll.SensorRepeat(\"\", NULL_KEY, AGENT, 10, PI, 1)\n",
                                    true) ==
                      std::string("0 LuauLintSlCostlyListen\n"
                                  "1 LuauLintSlCostlyListen\n"
                                  "4 LuauLintSlFastTimer|0.05\n"
                                  "5 LuauLintSlFastTimer|0.02\n"
                                  "8 LuauLintSlFastSensor|0.5\n"));
        UNSCOPED_INFO("LSL");
        CHECK(three("default {\n"
                                   "    state_entry() {\n"
                                   "        llListen(PUBLIC_CHANNEL, \"\", NULL_KEY, \"\");\n"
                                   "        llListen(0, \"\", llGetOwner(), \"\");\n"
                                   "        llSetTimerEvent(0.05);\n"
                                   "        llSetTimerEvent(0.0);\n"
                                   "        llSensorRepeat(\"\", NULL_KEY, AGENT, 10.0, PI, 0.25);\n"
                                   "    }\n"
                                   "}\n",
                                   false) ==
                      std::string("2 LSLSlCostlyListen\n"
                                  "4 LSLSlFastTimer|0.05\n"
                                  "6 LSLSlFastSensor|0.25\n"));
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlStringBuild: a string grown in a loop; in SLua its pieces in a table", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        UNSCOPED_INFO("SLua");
        CHECK(found("local s = \"\"\n"
                                    "for i = 1, 3 do s ..= tostring(i) end\n"
                                    "local t = \"\"\n"
                                    "for i = 1, 3 do t = t .. i; print(t) end\n"
                                    "g = \"\"\n"
                                    "while #g < 3 do g ..= \"x\" end\n"
                                    "local u = \"\"\n"
                                    "for i = 1, 2 do for j = 1, 2 do u ..= \"y\" end end\n"
                                    "for i = 1, 2 do local v = \"\"; v ..= \"z\"; print(v) end\n"
                                    "print(s, u)\n",
                                    "SlStringBuild", ALScriptProblem::Severity::Note) ==
                      std::string("1 LuauLintSlStringBuild|s\n"
                                  "3 LuauLintSlStringBuild|t\n"
                                  "5 LuauLintSlStringBuild|g\n"
                                  "7 LuauLintSlStringBuild|u\n"));
        {
            INFO("in a table");
            CHECK(require_fixed("local s = \"\"\nfor i = 1, 3 do s ..= tostring(i) end\nprint(s)\n", "LuauLintSlStringBuild",
                                "Put s's pieces in a table, joined once after the loop", false) ==
                  std::string("local s = \"\"\nlocal sParts = {}\nfor i = 1, 3 do table.insert(sParts, tostring(i)) end\ns ..= table.concat(sParts)\nprint(s)\n"));
        }
        {
            INFO("named anew, indented");
            CHECK(require_fixed("local function f()\n    local s = \"\"\n    local sParts = 1\n    for i = 1, 3 do\n        s = s .. i\n    end\n"
                                "    return s, sParts\nend\nprint(f())\n",
                                "LuauLintSlStringBuild", "Put s's pieces in a table, joined once after the loop", false) ==
                  std::string("local function f()\n    local s = \"\"\n    local sParts = 1\n    local sParts2 = {}\n    for i = 1, 3 do\n"
                              "        table.insert(sParts2, tostring(i))\n    end\n    s ..= table.concat(sParts2)\n    return s, sParts\nend\nprint(f())\n"));
        }
        require_unfixed("local t = \"\"\nfor i = 1, 3 do t = t .. i; print(t) end\n", "LuauLintSlStringBuild");
        require_unfixed("local function f()\n    local s = \"\"\n    for i = 1, 3 do\n        s ..= i\n        if i > 1 then return s end\n    end\n    return s\nend\nprint(f())\n",
                "LuauLintSlStringBuild");
        UNSCOPED_INFO("read by the loop's condition, which would never see it grow");
        require_unfixed("local s = \"\"\nwhile #s < 3 do s ..= \"x\" end\nprint(s)\n", "LuauLintSlStringBuild");
        require_unfixed("local r = \"\"\nrepeat r ..= \"x\" until #r >= 3\nprint(r)\n", "LuauLintSlStringBuild");
        UNSCOPED_INFO("LSL");
        CHECK(found("default {\n"
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
                                   "SlStringBuild", ALScriptProblem::Severity::Note, false) ==
                      std::string("4 LSLSlStringBuild|s\n"
                                  "6 LSLSlStringBuild|t\n"));
        UNSCOPED_INFO("the converter's note names it");
        CHECK(std::string(ALLSLToSLua::lintOf("SluaStringBuild")) == "SlStringBuild");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlRepeatedCall in SLua: a steady call made again kept in a local", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        CHECK(found("local function report()\n"
                                    "    print(ll.GetOwner())\n"
                                    "    if ll.GetOwner() == ll.GetKey() then print(ll.GetOwner()) end\n"
                                    "    print(ll.GetKey())\n"
                                    "end\n"
                                    "print(ll.GetOwner(), llcompat.GetOwner())\n"
                                    "LLEvents:on(\"touch_start\", function() print(ll.GetScriptName()) print(ll.GetScriptName()) end)\n"
                                    "print(report)\n",
                                    "SlRepeatedCall", ALScriptProblem::Severity::Note) ==
                      std::string("1 LuauLintSlRepeatedCall|ll.GetOwner()|3\n"
                                  "2 LuauLintSlRepeatedCall|ll.GetKey()|2\n"
                                  "6 LuauLintSlRepeatedCall|ll.GetScriptName()|2\n"));
        {
            INFO("kept");
            CHECK(require_fixed("local function report()\n    print(ll.GetOwner())\n    print(ll.GetOwner())\nend\nprint(report)\n",
                                "LuauLintSlRepeatedCall", "Keep ll.GetOwner() in a local, owner", false) ==
                  std::string("local function report()\n    local owner = ll.GetOwner()\n    print(owner)\n    print(owner)\nend\nprint(report)\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlRepeatedCall in LSL: steady and pure calls repeated kept in a local", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        CHECK(found("default {\n"
                                    "    touch_start(integer n) {\n"
                                    "        string s = llDetectedName(0);\n"
                                    "        llOwnerSay((string)llGetOwner());\n"
                                    "        if (llGetOwner() == llDetectedKey(0)) llOwnerSay(llToUpper(s) + llToUpper(s));\n"
                                    "        integer i;\n"
                                    "        for (i = 0; i < 2; ++i) llOwnerSay(llGetSubString(s, i, i) + llGetSubString(s, i, i));\n"
                                    "    }\n"
                                    "}\n",
                                    "SlRepeatedCall", ALScriptProblem::Severity::Note, false) ==
                      std::string("3 LSLSlRepeatedCall|llGetOwner()|2\n"
                                  "4 LSLSlRepeatedCall|llToUpper(s)|2\n"));
        {
            INFO("a steady one");
            CHECK(require_fixed("default { touch_start(integer n) {\n    llOwnerSay((string)llGetOwner());\n    llOwnerSay((string)llGetOwner());\n} }\n",
                                "LSLSlRepeatedCall", "Keep llGetOwner() in a local, owner", false, false) ==
                  std::string("default { touch_start(integer n) {\n    key owner = llGetOwner();\n    llOwnerSay((string)owner);\n    llOwnerSay((string)owner);\n} }\n"));
        }
        {
            INFO("a pure one");
            CHECK(require_fixed("default { state_entry() {\n    string s = \"a\";\n    llOwnerSay(llToUpper(s) + llToUpper(s));\n} }\n",
                                "LSLSlRepeatedCall", "Keep llToUpper(s) in a local, toUpper", false, false) ==
                  std::string("default { state_entry() {\n    string s = \"a\";\n    string toUpper = llToUpper(s);\n    llOwnerSay(toUpper + toUpper);\n} }\n"));
        }
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: migration: migration lints and notes and llcompat calls left from LSL", "[ALScriptLintPass]")
    {
        ALScriptProblem p;
        p.source = ALScriptProblem::Source::Lint;
        p.code   = "SlZeroIndex";
        UNSCOPED_INFO("a migration rule's");
        CHECK(ALScriptLintPass::migration(p));
        p.code = "SlSleepingCall";
        UNSCOPED_INFO("not a performance rule's");
        CHECK_FALSE(ALScriptLintPass::migration(p));
        p.code = "";
        p.key  = "LuauLintDeprecatedMemberUse";
        p.args = { "llcompat.Say" };
        UNSCOPED_INFO("llcompat's");
        CHECK(ALScriptLintPass::migration(p));
        p.args = { "ll.SetPrimitiveParams" };
        UNSCOPED_INFO("not ll's");
        CHECK_FALSE(ALScriptLintPass::migration(p));
        ALScriptProblem note;
        note.source = ALScriptProblem::Source::Assistant;
        note.key    = "SluaNote";
        UNSCOPED_INFO("a note");
        CHECK(ALScriptLintPass::migration(note));
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlForIndexAssign: a numeric for's variable set in its body is a warning", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local list = {\"a\", \"a\", \"b\"}\n"
                                       "local start = 1\n"
                                       "for i = 1, #list - 1 do\n"
                                       "    if list[i] ~= list[start] then\n"
                                       "        start += 1\n"
                                       "        i = start\n"
                                       "    end\n"
                                       "    i += 1\n"
                                       "    local function later() i = 0 end\n"
                                       "    later()\n"
                                       "end\n"
                                       "for i = 1, 2 do\n"
                                       "    local i = 5\n"
                                       "    i = 6\n"
                                       "    print(i)\n"
                                       "end\n"
                                       "for k, v in list do k = 2; print(k, v) end\n"
                                       "local n = 1\n"
                                       "while n < 3 do n = n + 1 end\n",
                                       "SlForIndexAssign", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("5 LuauLintSlForIndexAssign|i\n"
                                  "7 LuauLintSlForIndexAssign|i\n"
                                  "8 LuauLintSlForIndexAssign|i\n"));
        require_unfixed("for i = 1, 3 do i = 2 end\n", "LuauLintSlForIndexAssign");
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlGlobalAssign: a global set in a function and named elsewhere goes local", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string shared = "function init()\n"
                                   "    count = 0\n"
                                   "end\n"
                                   "LLEvents:on(\"touch_start\", function(detected)\n"
                                   "    count += 1\n"
                                   "    print(count)\n"
                                   "end)\n"
                                   "init()\n";
        UNSCOPED_INFO("set in one, named in another");
        CHECK(found(shared, "SlGlobalAssign", ALScriptProblem::Severity::Note) ==
                      std::string("1 LuauLintSlGlobalInFunction|count\n"));
        {
            INFO("declared before the first that names it");
            CHECK(require_fixed(shared, "LuauLintSlGlobalInFunction", "Declare local count before it is first named", false) ==
                  std::string("local count\n") + shared);
        }
        UNSCOPED_INFO("one function alone: Luau's");
        REQUIRE(found("LLEvents:on(\"touch_start\", function(detected)\n    total = 1\n    print(total)\nend)\n", "SlGlobalAssign", ALScriptProblem::Severity::Note)
                   .find("LuauLintSlGlobalInFunction") == std::string::npos);
        UNSCOPED_INFO("set at the top too: said there, not here");
        REQUIRE(found("count = 0\nfunction bump() count += 1 end\nbump()\n", "SlGlobalAssign", ALScriptProblem::Severity::Note).find("LuauLintSlGlobalInFunction") ==
                   std::string::npos);
        UNSCOPED_INFO("not where _G is");
        REQUIRE(found(std::string("print(_G)\n") + shared, "SlGlobalAssign", ALScriptProblem::Severity::Note).find("LuauLintSlGlobalInFunction") == std::string::npos);
        UNSCOPED_INFO("set at the top in a block of it: not in a function");
        REQUIRE(found("if true then\n    x = 1\nend\nfunction f() x = 2 end\nf()\nprint(x)\n", "SlGlobalAssign", ALScriptProblem::Severity::Note)
                   .find("LuauLintSlGlobalInFunction") == std::string::npos);
        UNSCOPED_INFO("made by function f() at the top, its own lint off: made there");
        REQUIRE(found("--!nolint SlGlobalFunction\nfunction onTouch() end\nfunction reset() onTouch = nil end\nreset()\nonTouch()\n", "SlGlobalAssign",
                     ALScriptProblem::Severity::Note)
                   .find("LuauLintSlGlobalInFunction") == std::string::npos);
    }

    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlIntegerPast32Bits: an integer literal past 0xFFFFFFFF reads as -1", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("builtins: " + builtinsError);
        CHECK(lslLoaded);
        UNSCOPED_INFO("LSL");
        CHECK(found("integer a = 4294967296;\n"
                                   "integer b = 0x100000000;\n"
                                   "integer c = -4294967296;\n"
                                   "integer d = 99999999999999999999999;\n"
                                   "integer e = 4294967295; integer f = 0xFFFFFFFF; integer g = 2147483648;\n"
                                   "float h = 4294967296.0; float i = 1e10; string j = \"4294967296\"; // 4294967296\n"
                                   "default { state_entry() {\n"
                                   "    integer k = a - 4294967296;\n"
                                   "    vector v = <4294967296, 0, 0>;\n"
                                   "    llOwnerSay((string)(k + b + c + d + e + f + g) + (string)h + (string)i + j + (string)v);\n"
                                   "} }\n",
                                   "SlIntegerPast32Bits", ALScriptProblem::Severity::Warning, false) ==
                      std::string("0 LSLSlIntegerPast32Bits|4294967296|-1\n"
                                  "1 LSLSlIntegerPast32Bits|0x100000000|-1\n"
                                  "2 LSLSlIntegerPast32Bits|-4294967296|1\n"
                                  "3 LSLSlIntegerPast32Bits|99999999999999999999999|-1\n"
                                  "7 LSLSlIntegerPast32Bits|4294967296|-1\n"
                                  "8 LSLSlIntegerPast32Bits|4294967296|-1\n"));
        UNSCOPED_INFO("not SLua's, whose numbers are doubles");
        CHECK(found("local a = 4294967296\n", "SlIntegerPast32Bits", ALScriptProblem::Severity::Warning, true) ==
                      std::string());
    }
    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlNumberTruth beyond numbers: other types read as conditions are fixed", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local id = ll.GetOwner()\n"
                                       "local v = ll.GetPos()\n"
                                       "local r = ll.GetRot()\n"
                                       "local s = ll.GetObjectName()\n"
                                       "local l = {1, 2}\n"
                                       "local i = 5i\n"
                                       "if id then print(1) end\n"
                                       "if v and s then print(2) end\n"
                                       "while r do break end\n"
                                       "if l then print(3) end\n"
                                       "if i then print(4) end\n"
                                       "print(not s, not id, not l, not (v), not r, not i)\n"
                                       "local names: {[string]: string} = {}\n"
                                       "local named = names[\"a\"]\n"
                                       "if names[\"a\"] or named then print(5) end\n"
                                       "local later: string\n"
                                       "if later then print(later) end\n"
                                       "local t = {a = 1}\n"
                                       "local b = true\n"
                                       "if t or b or ll.GetOwner() then print(t) end\n",
                                       "SlNumberTruth", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("6 LuauLintSlUuidTruth|id|id.istruthy\n"
                                  "7 LuauLintSlVectorTruth|v|v ~= ZERO_VECTOR\n"
                                  "7 LuauLintSlStringTruth|s|s ~= \"\"\n"
                                  "8 LuauLintSlQuaternionTruth|r|r ~= ZERO_ROTATION\n"
                                  "9 LuauLintSlListTruth|l|#l > 0\n"
                                  "10 LuauLintSlIntegerTruth|i|i ~= 0i\n"
                                  "11 LuauLintSlStringTruthNot|s|s == \"\"\n"
                                  "11 LuauLintSlUuidTruthNot|id|not id.istruthy\n"
                                  "11 LuauLintSlListTruthNot|l|#l == 0\n"
                                  "11 LuauLintSlVectorTruthNot|(v)|(v) == ZERO_VECTOR\n"
                                  "11 LuauLintSlQuaternionTruthNot|r|r == ZERO_ROTATION\n"
                                  "11 LuauLintSlIntegerTruthNot|i|i == 0i\n"
                                  "19 LuauLintSlUuidTruth|ll.GetOwner()|ll.GetOwner().istruthy\n"));
        {
            INFO("a uuid");
            CHECK(require_fixed("local id = ll.GetOwner()\nif id then print(id) end\n", "LuauLintSlUuidTruth", "Write it id.istruthy", false) ==
                  std::string("local id = ll.GetOwner()\nif id.istruthy then print(id) end\n"));
        }
        {
            INFO("a uuid under not");
            CHECK(require_fixed("if not ll.GetOwner() then print(1) end\n", "LuauLintSlUuidTruthNot", "Write it not ll.GetOwner().istruthy", false) ==
                  std::string("if not ll.GetOwner().istruthy then print(1) end\n"));
        }
        {
            INFO("a vector");
            CHECK(require_fixed("local v = ll.GetPos()\nwhile v do break end\n", "LuauLintSlVectorTruth", "Write it v ~= ZERO_VECTOR", false) ==
                  std::string("local v = ll.GetPos()\nwhile v ~= ZERO_VECTOR do break end\n"));
        }
        {
            INFO("a quaternion");
            CHECK(require_fixed("local r = ll.GetRot()\nprint(not r)\n", "LuauLintSlQuaternionTruthNot", "Write it r == ZERO_ROTATION", false) ==
                  std::string("local r = ll.GetRot()\nprint(r == ZERO_ROTATION)\n"));
        }
        {
            INFO("a string, an if-then-else bracketed first");
            CHECK(require_fixed("local c = true\nif (if c then \"a\" else \"\") then print(c) end\n",
                                "LuauLintSlStringTruth", "Write it (if c then \"a\" else \"\") ~= \"\"", false) ==
                  std::string("local c = true\nif ((if c then \"a\" else \"\") ~= \"\") then print(c) end\n"));
        }
        {
            INFO("a list");
            CHECK(require_fixed("local l = {1}\nif l then print(l) end\n", "LuauLintSlListTruth", "Write it #l > 0", false) ==
                  std::string("local l = {1}\nif #l > 0 then print(l) end\n"));
        }
        {
            INFO("a list under not");
            CHECK(require_fixed("local l = {1}\nprint(not l)\n", "LuauLintSlListTruthNot", "Write it #l == 0", false) ==
                  std::string("local l = {1}\nprint(#l == 0)\n"));
        }
        {
            INFO("an integer");
            CHECK(require_fixed("local i = 5i\nif i then print(i) end\n", "LuauLintSlIntegerTruth", "Write it i ~= 0i", false) ==
                  std::string("local i = 5i\nif i ~= 0i then print(i) end\n"));
        }
        UNSCOPED_INFO("a number read by a key: not said either");
        CHECK(found("local counts: {[string]: number} = {}\nif counts[\"a\"] then print(1) end\n",
                                                                          "SlNumberTruth", ALScriptProblem::Severity::Warning) ==
                      std::string());
    }
    TEST_CASE_METHOD(alscriptlintpass_data, "ALScriptLintPass: SlMustUse over SLua's own types: their makers and readers left unread", "[ALScriptLintPass]")
    {
        UNSCOPED_INFO("definitions: " + definitionsError);
        CHECK(loaded);
        const std::string said = found("local i = 5i\n"
                                       "local b = buffer.create(4)\n"
                                       "integer.add(i, 1)\n"
                                       "uuid(\"00000000-0000-0000-0000-000000000001\")\n"
                                       "uuid.create(\"00000000-0000-0000-0000-000000000001\")\n"
                                       "vector(1, 2, 3)\n"
                                       "quaternion(0, 0, 0, 1)\n"
                                       "buffer.readu8(b, 0)\n"
                                       "buffer.readinteger(b, 0)\n"
                                       "buffer.len(b)\n"
                                       "buffer.create(8)\n"
                                       "table.create(3, 0)\n"
                                       "buffer.writeu8(b, 0, 1)\n"
                                       "buffer.fill(b, 0, 0)\n"
                                       "print(integer.add(i, 1), buffer.readu8(b, 0), vector(1, 2, 3))\n",
                                       "SlMustUse", ALScriptProblem::Severity::Warning);
        CHECK(said ==
                      std::string("2 LuauLintSlMustUse|integer.add\n"
                                  "3 LuauLintSlMustUse|uuid\n"
                                  "4 LuauLintSlMustUse|uuid.create\n"
                                  "5 LuauLintSlMustUse|vector\n"
                                  "6 LuauLintSlMustUse|quaternion\n"
                                  "7 LuauLintSlMustUse|buffer.readu8\n"
                                  "8 LuauLintSlMustUse|buffer.readinteger\n"
                                  "9 LuauLintSlMustUse|buffer.len\n"
                                  "10 LuauLintSlMustUse|buffer.create\n"
                                  "11 LuauLintSlMustUse|table.create\n"));
        {
            INFO("an integer's");
            CHECK(require_fixed("local i = 5i\ninteger.band(i, 3i)\nprint(i)\n", "LuauLintSlMustUse", "Write it i = integer.band(...)", false) ==
                  std::string("local i = 5i\ni = integer.band(i, 3i)\nprint(i)\n"));
        }
        require_unfixed("vector(1, 2, 3)\n", "LuauLintSlMustUse");
    }
}
