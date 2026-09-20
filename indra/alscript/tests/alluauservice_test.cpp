/**
 * @file tests/alluauservice_test.cpp
 * @brief The SLua analyzer against the grid's definitions.
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

#include "../test/lltut.h"

#include <fstream>
#include <sstream>

namespace tut
{
    struct alluauservice_data
    {
        ALLuauService service;
        std::string   definitions;
        std::string   error;
        bool          loaded = false;

        alluauservice_data()
        {
            std::ifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            definitions = text.str();
            loaded      = service.loadDefinitions(definitions, error);
        }

        // Every problem on a line, for a failure message that says what
        // the analyzer actually said.
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

        static bool mentions(const ALScriptProblems& problems, const char* word)
        {
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.message.find(word) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    };

    typedef test_group<alluauservice_data> alluauservice_group;
    typedef alluauservice_group::object    alluauservice_object;
    alluauservice_group                    alluauservice_instance("alluauservice");

    template<> template<>
    void alluauservice_object::test<1>()
    {
        set_test_name("the grid's definitions load");
        ensure("definitions file read", !definitions.empty());
        ensure("definitions load: " + error, loaded);
        ensure("service says so", service.hasDefinitions());
    }

    template<> template<>
    void alluauservice_object::test<2>()
    {
        set_test_name("a script using ll and the SL types checks clean");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check(
            "local owner: uuid = ll.GetOwner()\n"
            "ll.Say(0, tostring(owner))\n"
            "local here: vector = vector.create(1, 2, 3)\n"
            "ll.SetPos(here + vector.one)\n");
        ensure_equals("no errors: " + said(problems), errors(problems), 0);
    }

    template<> template<>
    void alluauservice_object::test<3>()
    {
        set_test_name("a wrong argument type is a type error");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("ll.Say(\"zero\", 0)\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("from the type checker", problems.front().source == ALScriptProblem::Source::Types);
        ensure_equals("on the first line", problems.front().line, 0);
    }

    template<> template<>
    void alluauservice_object::test<4>()
    {
        set_test_name("an unknown global is reported by name");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("frobnicate(1)\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("naming it: " + said(problems), mentions(problems, "frobnicate"));
    }

    template<> template<>
    void alluauservice_object::test<5>()
    {
        set_test_name("a deprecated function is a lint");
        ensure("definitions load: " + error, loaded);
        // ll.Cloud is one of the few functions the ll table itself marks
        // deprecated; the legacy names live in llcompat.
        ALScriptProblems problems = service.check("local density = ll.Cloud(vector.zero)\n");
        ensure("something said: " + said(problems), !problems.empty());
        ensure("deprecated: " + said(problems), mentions(problems, "deprecated"));
    }

    template<> template<>
    void alluauservice_object::test<6>()
    {
        set_test_name("a syntax error is the parser's");
        ensure("definitions load: " + error, loaded);
        ALScriptProblems problems = service.check("local x = \n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("from the parser", problems.front().source == ALScriptProblem::Source::Parser);
    }

    template<> template<>
    void alluauservice_object::test<7>()
    {
        set_test_name("without the definitions, ll is unknown");
        ALLuauService bare;
        ensure("nothing loaded", !bare.hasDefinitions());
        ALScriptProblems problems = bare.check("ll.Say(0, \"x\")\n");
        ensure("an error: " + said(problems), errors(problems) > 0);
        ensure("naming ll: " + said(problems), mentions(problems, "ll"));
    }

    template<> template<>
    void alluauservice_object::test<8>()
    {
        set_test_name("bad definitions are refused with a reason");
        ALLuauService bare;
        std::string   why;
        ensure("refused", !bare.loadDefinitions("declare ll: {\n", why));
        ensure("with a reason", !why.empty());
        ensure("and nothing loaded", !bare.hasDefinitions());
    }

    template<> template<>
    void alluauservice_object::test<9>()
    {
        set_test_name("completion offers the fields of ll and the locals in scope");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "local count = 1\nll.Sa\n";
        // At the end of `ll.Sa`.
        std::vector<ALScriptCompletion> found = service.complete(script, 1, 5);
        bool say = false;
        for (const ALScriptCompletion& c : found)
        {
            if (c.text == "Say")
            {
                say = true;
                ensure("a function", c.kind == ALScriptSymbolKind::Function);
                ensure("with a signature: " + c.detail, c.detail.find("(") != std::string::npos);
            }
        }
        std::string names = llformat("%d entries:", (int)found.size());
        for (size_t i = 0; i < found.size() && i < 12; ++i) names += " " + found[i].text;
        ensure("ll.Say is offered; " + names, say);
        // At the start of an empty statement: bindings and keywords.
        found = service.complete(script, 2, 0);
        bool local = false, keyword = false;
        for (const ALScriptCompletion& c : found)
        {
            local   = local || (c.text == "count" && c.kind == ALScriptSymbolKind::Variable && c.detail == "number");
            keyword = keyword || (c.text == "local" && c.kind == ALScriptSymbolKind::Keyword);
        }
        ensure("the local, typed", local);
        ensure("a keyword", keyword);
    }

    template<> template<>
    void alluauservice_object::test<10>()
    {
        set_test_name("hover says what a name is, with its documentation, and where a local was bound");
        ensure("definitions loaded: " + error, loaded);
        std::ifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.docs.json", std::ios::binary);
        std::stringstream json;
        json << in.rdbuf();
        std::string docs_error;
        ensure("docs loaded: " + docs_error, service.loadDocs(json.str(), docs_error));
        const std::string script = "local count = 1\nll.Say(0, tostring(count))\n";
        ALScriptHover hover = service.hover(script, 1, 4);  // on `Say`
        ensure("found", hover.found);
        ensure("the name and its type: " + hover.label, hover.label.find("Say") != std::string::npos && hover.label.find("(") != std::string::npos);
        ensure("documented: " + hover.documentation, !hover.documentation.empty());
        ensure("not the script's own", !hover.hasDefinition);
        hover = service.hover(script, 1, 20);  // on `count` in the call
        ensure("found the local", hover.found);
        ensure("a number: " + hover.label, hover.label.find("number") != std::string::npos);
        ensure("bound in the script", hover.hasDefinition && hover.definitionLine == 0);
        ensure("nothing at nothing", !service.hover(script, 2, 0).found);
    }

    template<> template<>
    void alluauservice_object::test<11>()
    {
        set_test_name("a signature names the call the position is in and which parameter it is at");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "ll.Say(0, \"hi\")\n";
        ALScriptSignature sig = service.signature(script, 0, 7);  // in the first argument
        ensure("found", sig.found);
        ensure("the call: " + sig.label, sig.label.find("Say") != std::string::npos);
        ensure_equals("two parameters", sig.parameters.size(), size_t(2));
        ensure_equals("at the first", sig.active, 0);
        sig = service.signature(script, 0, 11);  // in the second
        ensure_equals("at the second", sig.active, 1);
        ensure("none outside a call", !service.signature(script, 0, 0).found);
    }

    template<> template<>
    void alluauservice_object::test<12>()
    {
        set_test_name("references find where a name is bound and every place it stands, and know what is the script's to rename");
        ensure("definitions loaded: " + error, loaded);
        const std::string script =
            "local count = 1\n"
            "ll.Say(0, tostring(count))\n"
            "count = count + 1\n"
            "local M = {}\n"
            "function M.f(n: number) return n end\n"
            "M.f(count)\n";
        ALScriptReferences refs = service.references(script, 1, 20);  // on `count` in the call
        ensure("the local is found", refs.found);
        ensure_equals("by name", refs.name, std::string("count"));
        ensure(llformat("a variable, not kind %d", (int)refs.kind), refs.kind == ALScriptSymbolKind::Variable);
        ensure(llformat("bound on the first line, not %d:%d-%d", refs.definition.line, refs.definition.column, refs.definition.endColumn),
               refs.hasDefinition && refs.definition.line == 0 && refs.definition.column == 6 && refs.definition.endColumn == 11);
        ensure_equals("five places", refs.references.size(), size_t(5));
        ensure("the first is the binding", refs.references[0] == refs.definition);
        ensure("the last is the argument", refs.references[4].line == 5 && refs.references[4].column == 4);
        ensure("the script's to rename", refs.renamable);

        refs = service.references(script, 1, 4);  // on `Say`
        ensure("a field of ll is found", refs.found && refs.name == "Say");
        ensure("a function", refs.kind == ALScriptSymbolKind::Function);
        ensure("not bound in the script", !refs.hasDefinition && !refs.renamable);
        ensure_equals("used once", refs.references.size(), size_t(1));

        refs = service.references(script, 5, 2);  // on `f` in `M.f(count)`
        ensure("the script's own field is found", refs.found && refs.name == "f");
        ensure("bound by the function statement", refs.hasDefinition && refs.definition.line == 4 && refs.definition.column == 11);
        ensure_equals("declared and called", refs.references.size(), size_t(2));
        ensure("and renamable", refs.renamable);

        refs = service.references(script, 0, 6);  // on the binding itself
        ensure("asked at the binding, the same answer", refs.found && refs.references.size() == 5);
        ensure("nothing at nothing", !service.references(script, 3, 0).found);
    }

    template<> template<>
    void alluauservice_object::test<13>()
    {
        set_test_name("the outline lists what the top binds and every function, each function's own one deeper");
        ensure("definitions loaded: " + error, loaded);
        const std::string script =
            "local count = 1\n"
            "local function half(n: number)\n"
            "    local inner = 2\n"
            "    local function quarter() return n / 4 end\n"
            "    return n / 2\n"
            "end\n"
            "function LLEvents.touch_start(n: number) end\n"
            "handlers = {}\n";
        std::vector<ALScriptOutlineEntry> outline = service.outline(script);
        std::string names;
        for (const ALScriptOutlineEntry& e : outline) names += " " + e.name + llformat("@%d", e.depth);
        ensure_equals("five entries:" + names, outline.size(), size_t(5));
        ensure("count first, a number", outline[0].name == "count" && outline[0].kind == ALScriptSymbolKind::Variable && outline[0].detail == "number" && outline[0].depth == 0);
        ensure("half, a function spanning its lines", outline[1].name == "half" && outline[1].kind == ALScriptSymbolKind::Function && outline[1].span.line == 1 && outline[1].span.endLine == 5);
        ensure("its name where it is", outline[1].nameSpan.line == 1 && outline[1].nameSpan.column == 15);
        ensure("quarter inside it, one deeper", outline[2].name == "quarter" && outline[2].depth == 1);
        ensure("the event handler, as an event", outline[3].name == "LLEvents.touch_start" && outline[3].kind == ALScriptSymbolKind::Event);
        ensure("the global", outline[4].name == "handlers" && outline[4].kind == ALScriptSymbolKind::Variable);
    }
}
