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

    template<> template<>
    void allslservice_object::test<9>()
    {
        set_test_name("references find a symbol's declaration and every use; builtins, events and default are not the script's to rename");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "integer count = 0;\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        count = count + 1;\n"
            "        llSay(0, (string)count);\n"
            "        state other;\n"
            "    }\n"
            "}\n"
            "state other\n"
            "{\n"
            "    state_entry() { state default; }\n"
            "}\n";
        ALScriptReferences refs = service.references(script, 5, 16);  // on the second `count`
        ensure("found", refs.found && refs.name == "count");
        ensure("a variable", refs.kind == ALScriptSymbolKind::Variable);
        ensure(llformat("declared on the first line, not %d:%d-%d (%d)", refs.definition.line, refs.definition.column, refs.definition.endColumn, (int)refs.hasDefinition),
               refs.hasDefinition && refs.definition.line == 0 && refs.definition.column == 8 && refs.definition.endColumn == 13);
        ensure_equals("four places", refs.references.size(), size_t(4));
        ensure("the declaration first", refs.references[0] == refs.definition);
        ensure("the cast's operand last", refs.references[3].line == 6 && refs.references[3].column == 25);
        ensure("renamable", refs.renamable);

        refs = service.references(script, 6, 10);  // on llSay
        ensure("a builtin is found", refs.found && refs.name == "llSay");
        ensure("used once, declared nowhere here", refs.references.size() == 1 && !refs.hasDefinition && !refs.renamable);

        refs = service.references(script, 7, 15);  // on `other` in `state other;`
        ensure("a state is found", refs.found && refs.kind == ALScriptSymbolKind::State);
        ensure("declared where it is", refs.hasDefinition && refs.definition.line == 10 && refs.definition.column == 6);
        ensure_equals("the declaration and the change", refs.references.size(), size_t(2));
        ensure("renamable", refs.renamable);

        refs = service.references(script, 12, 26);  // on `default` in `state default;`
        ensure("default is found", refs.found && refs.name == "default");
        ensure("but is not the script's to rename", !refs.renamable);

        refs = service.references(script, 3, 6);  // on state_entry
        ensure("an event is found", refs.found && refs.kind == ALScriptSymbolKind::Event);
        ensure(llformat("in both states, not %d places", (int)refs.references.size()), refs.references.size() == 2);
        ensure("not renamable", !refs.renamable);
    }

    template<> template<>
    void allslservice_object::test<10>()
    {
        set_test_name("the outline lists the globals, the functions and the states with their events one deeper");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "integer count = 0;\n"
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    state_entry() { }\n"
            "    touch_start(integer total)\n"
            "    {\n"
            "    }\n"
            "}\n"
            "state other { }\n";
        std::vector<ALScriptOutlineEntry> outline = service.outline(script);
        std::string names;
        for (const ALScriptOutlineEntry& e : outline) names += " " + e.name + llformat("@%d", e.depth);
        ensure_equals("six entries:" + names, outline.size(), size_t(6));
        ensure("the global", outline[0].name == "count" && outline[0].kind == ALScriptSymbolKind::Variable && outline[0].detail == "integer count" && outline[0].depth == 0);
        ensure("the function, as declared", outline[1].name == "half" && outline[1].kind == ALScriptSymbolKind::Function && outline[1].detail == "float half(integer n)");
        ensure("its name where it is", outline[1].nameSpan.line == 1 && outline[1].nameSpan.column == 6 && outline[1].nameSpan.endColumn == 10);
        ensure("default, a state", outline[2].name == "default" && outline[2].kind == ALScriptSymbolKind::State && outline[2].depth == 0);
        ensure("spanning its lines", outline[2].span.line == 2 && outline[2].span.endLine == 8);
        ensure("state_entry, one deeper", outline[3].name == "state_entry" && outline[3].kind == ALScriptSymbolKind::Event && outline[3].depth == 1);
        ensure("touch_start with its parameter", outline[4].name == "touch_start" && outline[4].detail == "touch_start(integer total)" && outline[4].span.line == 5 && outline[4].span.endLine == 7);
        ensure("the other state", outline[5].name == "other" && outline[5].kind == ALScriptSymbolKind::State);
    }
}
