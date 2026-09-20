/**
 * @file tests/allslservice_test.cpp
 * @brief The LSL analyzer against the grid's builtins.
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

#include "../test/lltut.h"

namespace tut
{
    struct allslservice_data
    {
        ALLSLService service;
        std::string  error;
        bool         loaded = false;

        allslservice_data()
        {
            loaded = service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
        }

        static std::string said(const ALScriptProblems& problems)
        {
            std::string out;
            for (const ALScriptProblem& problem : problems)
            {
                out += llformat("[%d:%d] %s %s\n", problem.line, problem.column, problem.code.c_str(), problem.message.c_str());
            }
            return out;
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

        static const ALScriptProblem* coded(const ALScriptProblems& problems, const char* code)
        {
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.code == code)
                {
                    return &problem;
                }
            }
            return nullptr;
        }
    };

    typedef test_group<allslservice_data> allslservice_group;
    typedef allslservice_group::object    allslservice_object;
    allslservice_group                    allslservice_instance("allslservice");

    template<> template<>
    void allslservice_object::test<1>()
    {
        set_test_name("the grid's builtins load");
        ensure("builtins load: " + error, loaded);
        ensure("service says so", service.hasBuiltins());
    }

    template<> template<>
    void allslservice_object::test<2>()
    {
        set_test_name("a script that is right checks clean");
        ensure("builtins load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        llSay(0, \"Hello, \" + (string)llGetOwner());\n"
            "    }\n"
            "}\n");
        ensure_equals("nothing said: " + said(problems), problems.size(), 0);
    }

    template<> template<>
    void allslservice_object::test<3>()
    {
        set_test_name("a wrong argument type is an error on its line");
        ensure("builtins load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        llSay(\"zero\", 0);\n"
            "    }\n"
            "}\n");
        // E_ARGUMENT_WRONG_TYPE
        const ALScriptProblem* problem = coded(problems, "10011");
        ensure("the wrong-type error: " + said(problems), problem != nullptr);
        ensure("an error", problem->severity == ALScriptProblem::Severity::Error);
        ensure("from the checks", problem->source == ALScriptProblem::Source::Types);
        ensure_equals("on the call's line, zero-based", problem->line, 4);
    }

    template<> template<>
    void allslservice_object::test<4>()
    {
        set_test_name("an unused global is a warning");
        ensure("builtins load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "integer unused = 1;\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "    }\n"
            "}\n");
        // W_DECLARED_BUT_NOT_USED
        const ALScriptProblem* problem = coded(problems, "20009");
        ensure("the unused warning: " + said(problems), problem != nullptr);
        ensure("a warning", problem->severity == ALScriptProblem::Severity::Warning);
        ensure("from the lint", problem->source == ALScriptProblem::Source::Lint);
        ensure_equals("on the declaration's line", problem->line, 0);
        ensure_equals("no errors", errors(problems), 0);
    }

    template<> template<>
    void allslservice_object::test<5>()
    {
        set_test_name("a syntax error is the parser's");
        ensure("builtins load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "default\n"
            "{\n"
            "    state_entry(\n"
            "    {\n"
            "    }\n"
            "}\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("from the parser", problems.front().source == ALScriptProblem::Source::Parser);
        ensure("on a line past the first", problems.front().line > 0);
    }

    template<> template<>
    void allslservice_object::test<6>()
    {
        set_test_name("a builtins file that cannot be opened is refused, not fatal");
        ALLSLService bare;
        std::string  why;
        ensure("refused", !bare.loadBuiltins("/nonexistent/builtins.txt", why));
        ensure("with a reason", !why.empty());
        ensure("nothing loaded", !bare.hasBuiltins());
    }

    template<> template<>
    void allslservice_object::test<7>()
    {
        set_test_name("the script's symbols in scope: globals, functions, states, and locals declared before the position");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "integer count = 0;\n"
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    touch_start(integer total)\n"
            "    {\n"
            "        string before = \"a\";\n"
            "        llSay(0, before);\n"
            "        string after = \"b\";\n"
            "    }\n"
            "}\n"
            "state other { }\n";
        // Inside the llSay call.
        std::vector<ALScriptCompletion> found = service.symbols(script, 7, 12);
        auto has = [&](const char* name, ALScriptSymbolKind kind, const char* detail) {
            for (const ALScriptCompletion& c : found)
            {
                if (c.text == name)
                {
                    return c.kind == kind && c.detail == detail;
                }
            }
            return false;
        };
        ensure("the global", has("count", ALScriptSymbolKind::Variable, "integer count"));
        ensure("the function", has("half", ALScriptSymbolKind::Function, "float half(integer n)"));
        ensure("the state", has("other", ALScriptSymbolKind::State, "state other"));
        ensure("the event's parameter", has("total", ALScriptSymbolKind::Parameter, "integer total"));
        ensure("the local before", has("before", ALScriptSymbolKind::Variable, "string before"));
        bool after = false, builtin = false;
        for (const ALScriptCompletion& c : found)
        {
            after   = after || c.text == "after";
            builtin = builtin || c.text == "llSay";
        }
        ensure("not the local after", !after);
        ensure("not the builtins", !builtin);
    }

    template<> template<>
    void allslservice_object::test<8>()
    {
        set_test_name("hover reads a symbol's declaration and points at it; a signature says which argument");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        llSay(0, (string)half(4));\n"
            "    }\n"
            "}\n";
        ALScriptHover hover = service.hover(script, 5, 26);  // on `half`
        ensure("found", hover.found);
        ensure_equals("as declared", hover.label, std::string("float half(integer n)"));
        ensure("declared on the first line", hover.hasDefinition && hover.definitionLine == 0);
        hover = service.hover(script, 5, 10);  // on `llSay`
        ensure("a builtin is found", hover.found);
        ensure("as declared: " + hover.label, hover.label.find("llSay(") != std::string::npos);
        ensure("but not in the script", !hover.hasDefinition);

        ALScriptSignature sig = service.signature(script, 5, 15);  // in llSay's first argument
        ensure("in the call", sig.found);
        ensure("the builtin's signature: " + sig.label, sig.label.find("llSay(") == 0 || sig.label.find(" llSay(") != std::string::npos);
        ensure_equals("two parameters", sig.parameters.size(), size_t(2));
        ensure_equals("the first", sig.active, 0);
        sig = service.signature(script, 5, 30);  // just inside half(
        ensure("the inner call", sig.found && sig.label == "float half(integer n)");
        ensure_equals("its only parameter", sig.active, 0);
        ensure("none at the top", !service.signature(script, 0, 0).found);
    }
}
