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

#include "../luau/alluauservice.h"
#include "../luau/alluauexports.h"
#include "../luau/alluauconfig.h"
#include "../lint/alscriptfixes.h"
#include "../lint/alscriptlintpass.h"
#include "../lint/alselenefilters.h"
#include "../core/alscriptlexicon.h"

#include <catch2/catch_test_macros.hpp>
#include "llsdserialize.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
    struct alluauservice_data
    {
        ALLuauService service;
        std::string   definitions;
        std::string   error;
        bool          loaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alluauservice_data()
        {
            llifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            definitions = text.str();
            service.setNewSolver(newSolver, error);
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

        static bool offers(const std::vector<ALScriptCompletion>& found, const std::string& text)
        {
            return std::any_of(found.begin(), found.end(), [&text](const ALScriptCompletion& c) { return c.text == text; });
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

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the grid's definitions load", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions file read");
        REQUIRE_FALSE(definitions.empty());
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        CHECK(service.hasDefinitions());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a script using ll and the SL types checks clean", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check(
            "local owner: uuid = ll.GetOwner()\n"
            "ll.Say(0, tostring(owner))\n"
            "local here: vector = vector.create(1, 2, 3)\n"
            "ll.SetPos(here + vector.one)\n");
        UNSCOPED_INFO("no errors: " + said(problems));
        CHECK(errors(problems) == 0);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a wrong argument type is a type error", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check("ll.Say(\"zero\", 0)\n");
        UNSCOPED_INFO("an error: " + said(problems));
        REQUIRE(errors(problems) > 0);
        CHECK(problems.front().source == ALScriptProblem::Source::Types);
        CHECK(problems.front().line == 0);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: an unknown global is reported by name", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check("frobnicate(1)\n");
        UNSCOPED_INFO("an error: " + said(problems));
        CHECK(errors(problems) > 0);
        UNSCOPED_INFO("naming it: " + said(problems));
        CHECK(mentions(problems, "frobnicate"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a deprecated function is a lint; lints and unknown globals come keyed", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        // ll.Cloud is one of the few functions the ll table itself marks
        // deprecated; the legacy names live in llcompat.
        ALScriptProblems problems = service.check("local density = ll.Cloud(vector.zero)\n");
        UNSCOPED_INFO("something said: " + said(problems));
        REQUIRE_FALSE(problems.empty());
        UNSCOPED_INFO("deprecated: " + said(problems));
        CHECK(mentions(problems, "deprecated"));
        // A lint the map knows is taken apart by its name, an unknown
        // global by its shape, for another language to say.
        problems = service.check("local unused = 1\nnope()\n");
        bool keyed_lint = false, keyed_error = false;
        for (const ALScriptProblem& p : problems)
        {
            if (p.key == "LuauLintLocalUnused" && p.args.size() == 1 && p.args[0] == "unused")
            {
                keyed_lint = true;
            }
            // The checker's, or the lint's, by the mode the script is in.
            if ((p.key == "LuauUnknownGlobal" || p.key == "LuauUnknownGlobalAssign" || p.key == "LuauLintUnknownGlobal" || p.key == "LuauLintUnknownGlobalAssign") &&
                p.args.size() == 1 && p.args[0] == "nope")
            {
                keyed_error = true;
            }
        }
        UNSCOPED_INFO("the unused local's lint, keyed with its name: " + said(problems));
        CHECK(keyed_lint);
        UNSCOPED_INFO("the unknown global, keyed with its name: " + said(problems));
        CHECK(keyed_error);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a syntax error is the parser's", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check("local x = \n");
        UNSCOPED_INFO("an error: " + said(problems));
        REQUIRE(errors(problems) > 0);
        CHECK(problems.front().source == ALScriptProblem::Source::Parser);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: without the definitions ll is unknown", "[alluauservice]")
    {
        ALLuauService bare;
        CHECK_FALSE(bare.hasDefinitions());
        ALScriptProblems problems = bare.check("ll.Say(0, \"x\")\n");
        UNSCOPED_INFO("an error: " + said(problems));
        CHECK(errors(problems) > 0);
        UNSCOPED_INFO("naming ll: " + said(problems));
        CHECK(mentions(problems, "ll"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: bad definitions are refused with a reason", "[alluauservice]")
    {
        ALLuauService bare;
        std::string   why;
        REQUIRE_FALSE(bare.loadDefinitions("declare ll: {\n", why));
        REQUIRE_FALSE(why.empty());
        CHECK_FALSE(bare.hasDefinitions());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: completion offers the fields of ll and the locals in scope", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local count = 1\nll.Sa\n";
        // At the end of `ll.Sa`.
        std::vector<ALScriptCompletion> found = service.complete(script, 1, 5);
        bool say = false;
        for (const ALScriptCompletion& c : found)
        {
            if (c.text == "Say")
            {
                say = true;
                REQUIRE(c.kind == ALScriptSymbolKind::Function);
                UNSCOPED_INFO("with a signature: " + c.detail);
                REQUIRE(c.detail.find("(") != std::string::npos);
            }
        }
        std::string names = llformat("%d entries:", (int)found.size());
        for (size_t i = 0; i < found.size() && i < 12; ++i) names += " " + found[i].text;
        UNSCOPED_INFO("ll.Say is offered; " + names);
        CHECK(say);
        // At the start of an empty statement: bindings and keywords.
        found = service.complete(script, 2, 0);
        bool local = false, keyword = false;
        for (const ALScriptCompletion& c : found)
        {
            local   = local || (c.text == "count" && c.kind == ALScriptSymbolKind::Variable && c.detail == "number");
            keyword = keyword || (c.text == "local" && c.kind == ALScriptSymbolKind::Keyword);
        }
        UNSCOPED_INFO("the local, typed");
        CHECK(local);
        CHECK(keyword);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: hover says what a name is with its docs and where a local was bound", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        llifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.docs.json", std::ios::binary);
        std::stringstream json;
        json << in.rdbuf();
        std::string docs_error;
        const bool  docs_loaded = service.loadDocs(json.str(), docs_error);
        UNSCOPED_INFO("docs loaded: " + docs_error);
        CHECK(docs_loaded);
        const std::string script = "local count = 1\nll.Say(0, tostring(count))\n";
        ALScriptHover hover = service.hover(script, 1, 4);  // on `Say`
        REQUIRE(hover.found);
        {
            INFO("the name and its type: " + hover.label);
            REQUIRE(hover.label.find("Say") != std::string::npos);
            REQUIRE(hover.label.find("(") != std::string::npos);
        }
        REQUIRE_FALSE(hover.documentation.empty());
        UNSCOPED_INFO("not the script's own");
        CHECK_FALSE(hover.hasDefinition);
        hover = service.hover(script, 1, 20);  // on `count` in the call
        UNSCOPED_INFO("found the local");
        REQUIRE(hover.found);
        UNSCOPED_INFO("a number: " + hover.label);
        REQUIRE(hover.label.find("number") != std::string::npos);
        {
            INFO("bound in the script");
            CHECK(hover.hasDefinition);
            CHECK(hover.definitionLine == 0);
        }
        UNSCOPED_INFO("nothing at nothing");
        REQUIRE_FALSE(service.hover(script, 2, 0).found);
        hover = service.hover(script, 1, 0);  // on `ll` itself
        UNSCOPED_INFO("the table is found");
        REQUIRE(hover.found);
        UNSCOPED_INFO("and said in a glance");
        REQUIRE(hover.label.size() < 1200);
        UNSCOPED_INFO("with how many more there are: " + hover.label);
        REQUIRE(hover.label.find("more") != std::string::npos);
        {
            INFO("and the whole of it apart, a field to a line");
            REQUIRE(hover.typeDetail.size() > hover.label.size());
            REQUIRE(hover.typeDetail.find('\n') != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a signature names the call the position is in and which parameter it is at", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "ll.Say(0, \"hi\")\n";
        ALScriptSignature sig = service.signature(script, 0, 7);  // in the first argument
        REQUIRE(sig.found);
        UNSCOPED_INFO("the call: " + sig.label);
        REQUIRE(sig.label.find("Say") != std::string::npos);
        REQUIRE(sig.parameters.size() == size_t(2));
        CHECK(sig.active == 0);
        sig = service.signature(script, 0, 11);  // in the second
        CHECK(sig.active == 1);
        UNSCOPED_INFO("none outside a call");
        REQUIRE_FALSE(service.signature(script, 0, 0).found);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: references find a name's binding and uses and whether it can be renamed", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "local count = 1\n"
            "ll.Say(0, tostring(count))\n"
            "count = count + 1\n"
            "local M = {}\n"
            "function M.f(n: number) return n end\n"
            "M.f(count)\n";
        ALScriptReferences refs = service.references(script, 1, 20);  // on `count` in the call
        UNSCOPED_INFO("the local is found");
        REQUIRE(refs.found);
        CHECK(refs.name == std::string("count"));
        CHECK(refs.kind == ALScriptSymbolKind::Variable);
        {
            INFO("bound on the first line");
            CHECK(refs.hasDefinition);
            CHECK(refs.definition.line == 0);
            CHECK(refs.definition.column == 6);
            CHECK(refs.definition.endColumn == 11);
        }
        REQUIRE(refs.references.size() == size_t(5));
        UNSCOPED_INFO("the first is the binding");
        CHECK(refs.references[0] == refs.definition);
        {
            INFO("the last is the argument");
            CHECK(refs.references[4].line == 5);
            CHECK(refs.references[4].column == 4);
        }
        UNSCOPED_INFO("the script's to rename");
        CHECK(refs.renamable);

        refs = service.references(script, 1, 4);  // on `Say`
        {
            INFO("a field of ll is found");
            REQUIRE(refs.found);
            CHECK(refs.name == "Say");
        }
        CHECK(refs.kind == ALScriptSymbolKind::Function);
        {
            INFO("not bound in the script");
            CHECK_FALSE(refs.hasDefinition);
            CHECK_FALSE(refs.renamable);
        }
        REQUIRE(refs.references.size() == size_t(1));

        refs = service.references(script, 5, 2);  // on `f` in `M.f(count)`
        {
            INFO("the script's own field is found");
            REQUIRE(refs.found);
            CHECK(refs.name == "f");
        }
        {
            INFO("bound by the function statement");
            CHECK(refs.hasDefinition);
            CHECK(refs.definition.line == 4);
            CHECK(refs.definition.column == 11);
        }
        UNSCOPED_INFO("declared and called");
        REQUIRE(refs.references.size() == size_t(2));
        CHECK(refs.renamable);

        refs = service.references(script, 0, 6);  // on the binding itself
        {
            INFO("asked at the binding, the same answer");
            REQUIRE(refs.found);
            REQUIRE(refs.references.size() == 5);
        }
        UNSCOPED_INFO("nothing at nothing");
        REQUIRE_FALSE(service.references(script, 3, 0).found);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the outline lists what the top binds and functions with inner ones deeper", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("five entries:" + names);
        REQUIRE(outline.size() == size_t(5));
        {
            INFO("count first, a number");
            CHECK(outline[0].name == "count");
            CHECK(outline[0].kind == ALScriptSymbolKind::Variable);
            CHECK(outline[0].detail == "number");
            CHECK(outline[0].depth == 0);
        }
        {
            INFO("half, a function spanning its lines");
            CHECK(outline[1].name == "half");
            CHECK(outline[1].kind == ALScriptSymbolKind::Function);
            CHECK(outline[1].span.line == 1);
            CHECK(outline[1].span.endLine == 5);
        }
        {
            INFO("its name where it is");
            CHECK(outline[1].nameSpan.line == 1);
            CHECK(outline[1].nameSpan.column == 15);
        }
        {
            INFO("quarter inside it, one deeper");
            CHECK(outline[2].name == "quarter");
            CHECK(outline[2].depth == 1);
        }
        {
            INFO("the event handler, as an event");
            CHECK(outline[3].name == "LLEvents.touch_start");
            CHECK(outline[3].kind == ALScriptSymbolKind::Event);
        }
        {
            INFO("the global");
            CHECK(outline[4].name == "handlers");
            CHECK(outline[4].kind == ALScriptSymbolKind::Variable);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a missing key is named as written with the nearest key there is", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check("ll.ay(0, \"hi\")\n");
        REQUIRE_FALSE(problems.empty());
        const std::string& message = problems.front().message;
        {
            INFO("names ll, not its fields: " + message);
            REQUIRE(message.find("not found in ll") != std::string::npos);
            REQUIRE(message.find("Abs") == std::string::npos);
        }
        UNSCOPED_INFO("and suggests Say: " + message);
        REQUIRE(message.find("'Say'") != std::string::npos);
        UNSCOPED_INFO("in a glance");
        REQUIRE(message.size() < 100);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: every name is told by what it is: its kind and modifiers in order", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "local count = 1\n"
            "local function half(n: number): number\n"
            "    return n / 2\n"
            "end\n"
            "type Pair = { a: number, b: number }\n"
            "local p: Pair = { a = 1, b = 2 }\n"
            "ll.Say(0, tostring(p.a + half(count)))\n"
            "handlers = {}\n";
        std::vector<ALScriptSemanticToken> tokens = service.semanticTokens(script);
        auto at = [&](S32 line, S32 column) -> const ALScriptSemanticToken* {
            for (const ALScriptSemanticToken& t : tokens)
            {
                if (t.span.line == line && t.span.column == column) return &t;
            }
            return nullptr;
        };
        std::string listed;
        for (const ALScriptSemanticToken& t : tokens) listed += llformat(" %d:%d/%d+%d", t.span.line, t.span.column, (int)t.kind, (int)t.modifiers);
        const ALScriptSemanticToken* count_decl = at(0, 6);
        {
            INFO("count declared, a variable:" + listed);
            REQUIRE(count_decl);
            REQUIRE(count_decl->kind == ALScriptSymbolKind::Variable);
            CHECK(count_decl->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* half_decl = at(1, 15);
        {
            INFO("half declared, a function:" + listed);
            REQUIRE(half_decl);
            REQUIRE(half_decl->kind == ALScriptSymbolKind::Function);
            CHECK(half_decl->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* n_param = at(1, 20);
        {
            INFO("n, a parameter:" + listed);
            REQUIRE(n_param);
            CHECK(n_param->kind == ALScriptSymbolKind::Parameter);
        }
        const ALScriptSemanticToken* n_type = at(1, 23);
        {
            INFO("number, a type:" + listed);
            REQUIRE(n_type);
            CHECK(n_type->kind == ALScriptSymbolKind::Type);
        }
        const ALScriptSemanticToken* n_use = at(2, 11);
        {
            INFO("n used, still a parameter:" + listed);
            REQUIRE(n_use);
            REQUIRE(n_use->kind == ALScriptSymbolKind::Parameter);
            CHECK_FALSE(n_use->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* pair = at(4, 5);
        {
            INFO("Pair declared, a type:" + listed);
            REQUIRE(pair);
            REQUIRE(pair->kind == ALScriptSymbolKind::Type);
            CHECK(pair->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* a_key = at(5, 18);
        {
            INFO("a in the table, a field declared:" + listed);
            REQUIRE(a_key);
            REQUIRE(a_key->kind == ALScriptSymbolKind::Field);
            CHECK(a_key->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* ll = at(6, 0);
        {
            INFO("ll, a builtin global:" + listed);
            REQUIRE(ll);
            REQUIRE(ll->modifiers & ALScriptSemanticToken::Builtin);
            CHECK(ll->modifiers & ALScriptSemanticToken::Global);
        }
        const ALScriptSemanticToken* say = at(6, 3);
        {
            INFO("Say, a builtin function:" + listed);
            REQUIRE(say);
            REQUIRE(say->kind == ALScriptSymbolKind::Function);
            CHECK(say->modifiers & ALScriptSemanticToken::Builtin);
        }
        const ALScriptSemanticToken* p_a = at(6, 21);
        {
            INFO("p.a, a field:" + listed);
            REQUIRE(p_a);
            CHECK(p_a->kind == ALScriptSymbolKind::Field);
        }
        const ALScriptSemanticToken* half_call = at(6, 25);
        {
            INFO("half called, a function:" + listed);
            REQUIRE(half_call);
            CHECK(half_call->kind == ALScriptSymbolKind::Function);
        }
        const ALScriptSemanticToken* handlers = at(7, 0);
        {
            INFO("handlers, a global the script binds:" + listed);
            REQUIRE(handlers);
            REQUIRE(handlers->modifiers & ALScriptSemanticToken::Global);
            CHECK_FALSE(handlers->modifiers & ALScriptSemanticToken::Builtin);
        }
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            UNSCOPED_INFO("in order, each once");
            REQUIRE(tokens[i - 1].span < tokens[i].span);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: inlay hints name each argument's parameter and each untyped local's type", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "local function greet(name: string, times: number) end\n"
            "local times = 3\n"
            "local shown: number = 1\n"
            "greet(\"hi\", times)\n"
            "ll.Say(0, \"hello\")\n";
        std::vector<ALScriptInlayHint> hints = service.inlayHints(script, true, true);
        std::string listed;
        for (const ALScriptInlayHint& h : hints) listed += llformat(" %d:%d[%s]", h.line, h.column, h.text.c_str());
        auto has = [&](S32 line, S32 column, const char* text) {
            for (const ALScriptInlayHint& h : hints)
            {
                if (h.line == line && h.column == column && h.text == text) return true;
            }
            return false;
        };
        UNSCOPED_INFO("the local's type after its name:" + listed);
        CHECK(has(1, 11, ": number"));
        for (const ALScriptInlayHint& h : hints)
        {
            UNSCOPED_INFO("a type may be written in, a parameter's name never: " + h.text);
            REQUIRE(h.writable == (h.kind == ALScriptInlayHint::Kind::Type));
        }
        // A type cut short says less than the text would have to.
        const std::vector<ALScriptInlayHint> wide = service.inlayHints("local t = { a = 1, b = 2, c = 3, d = 4, e = 5 }\n", false, true);
        UNSCOPED_INFO("not a table cut short");
        REQUIRE((wide.empty() || !wide.front().writable || wide.front().text.find("...") == std::string::npos));
        UNSCOPED_INFO("not one that says its type:" + listed);
        CHECK_FALSE(has(2, 11, ": number"));
        UNSCOPED_INFO("name: before the string:" + listed);
        CHECK(has(3, 6, "name:"));
        UNSCOPED_INFO("nothing before an argument that is the name:" + listed);
        CHECK_FALSE(has(3, 12, "times:"));
        UNSCOPED_INFO("the channel's name before the 0:" + listed);
        CHECK(has(4, 7, "channel:"));
        UNSCOPED_INFO("and the message's:" + listed);
        CHECK(has(4, 10, "msg:"));
        std::vector<ALScriptInlayHint> only_types = service.inlayHints(script, false, true);
        {
            INFO("types alone, when asked");
            REQUIRE(only_types.size() == 1);
            CHECK(only_types.front().kind == ALScriptInlayHint::Kind::Type);
        }
        UNSCOPED_INFO("nothing when neither is asked");
        REQUIRE(service.inlayHints(script, false, false).empty());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: hover tells each kind of name and the type wanted where a type is wrong", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "local function half(n: number): number\n"
            "    return n / 2\n"
            "end\n"
            "const limit = 3\n"
            "local p = { a = 1 }\n"
            "ll.Say(0, half(\"x\"))\n"
            "print(limit, p.a)\n";
        ALScriptHover hover = service.hover(script, 0, 16);  // half, declared
        UNSCOPED_INFO("a function by its signature: " + hover.label);
        CHECK(hover.label.rfind("function half(n: number): number", 0) == 0);
        hover = service.hover(script, 0, 20);  // n, the parameter
        UNSCOPED_INFO("a parameter: " + hover.label);
        CHECK(hover.label == "(parameter) n: number");
        hover = service.hover(script, 1, 11);  // n, used
        UNSCOPED_INFO("still a parameter where used: " + hover.label);
        CHECK(hover.label == "(parameter) n: number");
        hover = service.hover(script, 3, 7);  // limit
        UNSCOPED_INFO("a const: " + hover.label);
        CHECK(hover.label == "const limit: number");
        hover = service.hover(script, 4, 6);  // p
        UNSCOPED_INFO("a local: " + hover.label);
        CHECK(hover.label.rfind("local p: ", 0) == 0);
        hover = service.hover(script, 6, 15);  // p.a
        UNSCOPED_INFO("a field: " + hover.label);
        CHECK(hover.label == "(field) p.a: number");
        hover = service.hover(script, 5, 16);  // "x" where a number is wanted
        UNSCOPED_INFO("what is wanted there: " + hover.expected);
        CHECK(hover.expected == "number");
        hover = service.hover(script, 6, 0);  // print
        // The new solver gives print its generic pack: print<T...>.
        UNSCOPED_INFO("a builtin by its signature: " + hover.label);
        CHECK(hover.label.rfind(newSolver ? "function print<" : "function print(", 0) == 0);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the configuration adds globals and turns lints off until it is replaced", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local unused = 1\nAlchemy.log(\"hi\")\n";
        ALScriptProblems  plain  = service.check(script);
        UNSCOPED_INFO("Alchemy unknown without a configuration: " + said(plain));
        CHECK(mentions(plain, "Alchemy"));
        UNSCOPED_INFO("the unused local warned of: " + said(plain));
        CHECK(mentions(plain, "unused"));
        ALLuauConfig config;
        std::string  bad;
        const bool   parsed = ALLuauConfig::parse("{ \"globals\": [\"Alchemy\"], \"lint\": { \"LocalUnused\": false } }", config, bad);
        UNSCOPED_INFO("parses: " + bad);
        REQUIRE(parsed);
        service.setConfig(config);
        ALScriptProblems configured = service.check(script);
        UNSCOPED_INFO("Alchemy known: " + said(configured));
        CHECK_FALSE(mentions(configured, "Alchemy"));
        UNSCOPED_INFO("the unused local let be: " + said(configured));
        CHECK_FALSE(mentions(configured, "unused"));
        service.setConfig(ALLuauConfig());
        ALScriptProblems again = service.check(script);
        UNSCOPED_INFO("unknown again: " + said(again));
        CHECK(mentions(again, "Alchemy"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the engine's commonest messages come back keyed", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        // Each script says one thing the map has a row for; a Luau whose
        // wording moved on gives the message with no key, and this is
        // where that shows -- run check_script_strings.py then.
        struct Case
        {
            const char* script;
            const char* key;
        };
        const Case cases[] = {
            { "local n: number = \"s\"\n", "LuauTypeMismatch" },
            { "ll.Say(0)\n", "LuauArgumentCountOnlyOne" },
            { "ll.Say(0, \"a\", 1)\n", "LuauArgumentCount" },
            // A key not found the studio rewords itself, keyed as its own.
            { "local t = { a = 1 }\nprint(t.b)\n", "LuauKeyNotFound" },
            { "local x = table.getn({})\n", "LuauLintDeprecatedMember" },
            { "local a, b = 1, 2\nif not a == b then end\n", "LuauLintNotPrecedence" },
            { "local unused = 1\n", "LuauLintLocalUnused" },
            { "for i = 10, 1 do end\n", "LuauLintForRange" },
        };
        for (const Case& c : cases)
        {
            ALScriptProblems problems = service.check(c.script);
            bool             keyed    = false;
            for (const ALScriptProblem& p : problems)
            {
                keyed |= p.key.compare(0, strlen(c.key), c.key) == 0;
            }
            UNSCOPED_INFO(std::string("keyed ") + c.key + " for " + c.script + ": " + said(problems));
            REQUIRE(keyed);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: questions reuse a text's check and hints are the same whatever came first", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local function f(a) return a + 1 end\nlocal y = f(2)\nlocal z = y * 2\nprint(z)\n";
        ALLuauConfig      config;
        service.setConfig(config);
        // Hints after a check, in the script's own mode.
        service.check(script);
        const std::vector<ALScriptInlayHint> after_check = service.inlayHints(script, true, true);
        const size_t                         checked     = service.typeChecks();
        // Asked again, in every way, of the same text: nothing checked again.
        service.hover(script, 1, 6);
        service.hover(script, 2, 6);
        service.signature(script, 1, 13);
        service.references(script, 1, 6);
        service.outline(script);
        service.semanticTokens(script);
        UNSCOPED_INFO("the queries share the one check");
        CHECK(service.typeChecks() == checked);
        service.setConfig(config);
        service.hover(script, 1, 6);
        UNSCOPED_INFO("the same configuration again changes nothing");
        CHECK(service.typeChecks() == checked);
        // A text asked about before a check has what a hover does.
        const std::vector<ALScriptInlayHint> after_hover = service.inlayHints(script, true, true);
        REQUIRE(after_hover.size() == after_check.size());
        for (size_t i = 0; i < after_hover.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(after_hover[i].text == after_check[i].text);
            REQUIRE(after_hover[i].line == after_check[i].line);
        }
        // Another configuration, or another text, is checked again: one
        // unlike either solver's default.
        ALLuauConfig strict;
        strict.mode = "nocheck";
        service.setConfig(strict);
        service.hover(script, 1, 6);
        UNSCOPED_INFO("another configuration, checked again");
        CHECK(service.typeChecks() > checked);
        const size_t now = service.typeChecks();
        service.hover(script + "print(y)\n", 1, 6);
        UNSCOPED_INFO("another text, checked again");
        CHECK(service.typeChecks() > now);
        service.setConfig(config);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a dotted method call takes its object first and the signature counts it", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local T = {}\nfunction T:m(a: number, b: string) end\nT.m(T, 1, \"x\")\nT:m(1, \"x\")\n";
        ALScriptSignature dotted = service.signature(script, 2, 12);  // in "x"
        REQUIRE(dotted.found);
        UNSCOPED_INFO("self, a and b: " + dotted.label);
        REQUIRE(dotted.parameters.size() == size_t(3));
        CHECK(dotted.active == 2);
        UNSCOPED_INFO("which is b: " + dotted.parameters[2]);
        CHECK(dotted.parameters[2].find("b") == 0);
        ALScriptSignature colon = service.signature(script, 3, 9);  // in "x"
        UNSCOPED_INFO("with a colon, a and b");
        REQUIRE(colon.parameters.size() == size_t(2));
        {
            INFO("at b: " + colon.label);
            CHECK(colon.active == 1);
            CHECK(colon.parameters[1].find("b") == 0);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: both solvers use the definitions; the new nonstrict says only sure failures", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        for (const bool use : { true, false, true })
        {
            std::string why;
            const bool  switched = service.setNewSolver(use, why);
            UNSCOPED_INFO("switched: " + why);
            REQUIRE(switched);
            REQUIRE(service.newSolver() == use);
            UNSCOPED_INFO("the definitions with it");
            REQUIRE(service.hasDefinitions());
            ALLuauConfig config;
            config.mode = "strict";
            service.setConfig(config);
            ALScriptProblems problems = service.check("ll.Say(\"zero\", 0)\n");
            UNSCOPED_INFO(std::string(use ? "new" : "old") + ", strict, a wrong argument: " + said(problems));
            REQUIRE(errors(problems) > 0);
            // A method called with a dot is a missing self, whichever says it.
            problems      = service.check("local T = {}\nfunction T:m(a: number) end\nT.m(1)\n");
            bool missing_self = false;
            for (const ALScriptProblem& problem : problems)
            {
                missing_self |= problem.key == "LuauRequiresSelf" && problem.line == 2 && problem.column == 0;
            }
            UNSCOPED_INFO(std::string(use ? "new" : "old") + ": a missing self, at the call: " + said(problems));
            REQUIRE(missing_self);
        }
        // Nonstrict, as the grid compiles: the new solver says nothing of
        // ll.Say's argument, and what it does say comes keyed.
        ALLuauConfig config;
        config.mode = "nonstrict";
        service.setConfig(config);
        UNSCOPED_INFO("nothing of ll.Say: " + said(service.check("ll.Say(\"zero\", 0)\n")));
        CHECK(errors(service.check("ll.Say(\"zero\", 0)\n")) == 0);
        const ALScriptProblems problems = service.check("print(string.len(5))\n");
        {
            INFO("a checked function given the wrong type: " + said(problems));
            REQUIRE_FALSE(problems.empty());
            CHECK(problems.front().key == "LuauCheckedCall");
        }
        std::string why;
        service.setNewSolver(false, why);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a check past its time limit says so; a stopped one answers nothing", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        std::string script;
        for (int i = 0; i < 50; ++i)
        {
            script += llformat("local v%d: number = %d\nprint(v%d)\n", i, i, i);
        }
        for (const bool use : { false, true })
        {
            std::string why;
            service.setNewSolver(use, why);
            const std::string which = use ? "new: " : "old: ";
            // Far less than any check takes.
            service.setTimeLimit(1e-9);
            ALScriptProblems problems = service.check(script);
            {
                INFO(which + "said to have stopped: " + said(problems));
                REQUIRE_FALSE(problems.empty());
                REQUIRE(problems.front().key == "LuauCheckTimedOut");
                REQUIRE(problems.front().severity == ALScriptProblem::Severity::Warning);
            }
            service.setTimeLimit(0.0);
            problems = service.check(script);
            UNSCOPED_INFO(which + "unlimited, it runs its course: " + said(problems));
            REQUIRE(problems.empty());
            // Stopped before it began: nothing, and said to be stopped.
            ALLuauService::Stop stop = ALLuauService::newStop();
            ALLuauService::cancel(stop);
            service.setStop(stop);
            problems = service.check("local n: number = \"s\"\n");
            {
                INFO(which + "stopped, nothing: " + said(problems));
                REQUIRE(problems.empty());
                REQUIRE(service.stopped());
            }
            // And a question after it is answered from a check of its own,
            // not the part of one the stop left.
            service.setStop(nullptr);
            const ALScriptHover hover = service.hover("local n: number = \"s\"\n", 0, 6);
            UNSCOPED_INFO(which + "not stopped now");
            REQUIRE_FALSE(service.stopped());
            UNSCOPED_INFO(which + "answered: " + hover.label);
            REQUIRE(hover.label.find("n: number") != std::string::npos);
            problems = service.check("local n: number = \"s\"\n");
            UNSCOPED_INFO(which + "and checked in full: " + said(problems));
            REQUIRE((errors(problems) > 0 || use));

            // A module the script requires, out of time with it: not kept
            // as checked, so the next check runs it again and says again
            // that it stopped, and one given time enough finds what the
            // module's first check never reached.
            service.setDocument("timed");
            service.setConfig(ALLuauConfig());
            ALLuauService::Modules modules;
            std::string            module = "--!strict\n";
            for (int i = 0; i < 50; ++i)
            {
                module += llformat("local v%d: number = %d\nprint(v%d)\n", i, i, i);
            }
            module += "local bad: number = \"x\"\nreturn {}\n";
            modules.modules.push_back({ "disk:/lib/slow.luau", module });
            modules.reaches.push_back({ "", "slow", "disk:/lib/slow.luau" });
            service.setModules(modules);
            const std::string requiring = "--!strict\nlocal slow = require(\"slow\")\nprint(slow)\n";
            const auto        stopped   = [](const ALScriptProblems& found) {
                return std::any_of(found.begin(), found.end(), [](const ALScriptProblem& problem) { return problem.key == "LuauCheckTimedOut"; });
            };
            service.setTimeLimit(1e-9);
            for (const char* time : { "first", "second" })
            {
                const size_t before = service.modulesChecked();
                problems            = service.check(requiring);
                UNSCOPED_INFO(which + time + " check, the module and the script checked");
                CHECK(service.modulesChecked() - before == size_t(2));
                INFO(which + time + " check, said to have stopped: " + said(problems));
                REQUIRE(stopped(problems));
            }
            service.setTimeLimit(0.0);
            const size_t before = service.modulesChecked();
            problems            = service.check(requiring);
            UNSCOPED_INFO(which + "unlimited, the module checked again");
            CHECK(service.modulesChecked() - before == size_t(2));
            UNSCOPED_INFO(which + "not said to have stopped: " + said(problems));
            CHECK_FALSE(stopped(problems));
            UNSCOPED_INFO(which + "and the module's mistake at its end found: " + said(problems));
            CHECK(std::any_of(problems.begin(), problems.end(), [](const ALScriptProblem& problem) {
                return problem.file == "disk:/lib/slow.luau" && problem.line == 101 && problem.source == ALScriptProblem::Source::Types;
            }));
            service.setDocument("");
        }
        std::string why;
        service.setNewSolver(newSolver, why);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: selene's comments allow or deny Luau's lints for the file or one statement", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        const auto read = ALSeleneFilters::read("-- # selene: allow(unused_variable, multiple_statements))");
        {
            INFO("read leniently, for the whole file");
            REQUIRE(read);
            REQUIRE(read->file);
            REQUIRE(read->action == ALSeleneFilters::Action::Allow);
            REQUIRE(read->lints.size() == 2);
        }
        {
            INFO("a Luau name stands for itself, selene's for Luau's");
            CHECK(ALSeleneFilters::luauLints("LocalUnused") == ALLuauConfig::lintBit("LocalUnused"));
            CHECK((ALSeleneFilters::luauLints("unused_variable") & ALLuauConfig::lintBit("FunctionUnused")) != 0);
        }
        UNSCOPED_INFO("a check Luau does not make stands for none");
        CHECK(ALSeleneFilters::luauLints("almost_swapped") == 0);
        {
            INFO("not a directive");
            REQUIRE_FALSE(ALSeleneFilters::read("-- seleneous: allow(x)"));
            REQUIRE_FALSE(ALSeleneFilters::read("-- selene: permit(x)"));
        }

        const auto unused = [&](const std::string& script) {
            size_t count = 0;
            for (const ALScriptProblem& p : service.check(script))
            {
                count += p.key.compare(0, 19, "LuauLintLocalUnused") == 0 ? 1 : 0;
            }
            return count;
        };
        UNSCOPED_INFO("without, both said");
        CHECK(unused("local a = 1\nlocal b = 2\n") == size_t(2));
        UNSCOPED_INFO("the whole file");
        CHECK(unused("-- @file header\n-- # selene: allow(unused_variable)\nlocal a = 1\nlocal b = 2\n") == size_t(0));
        UNSCOPED_INFO("after code, nothing: selene refuses a whole file's there");
        CHECK(unused("local a = 1\n--# selene: allow(unused_variable)\nlocal b = 2\n") == size_t(2));
        UNSCOPED_INFO("the statement beside, not the next");
        CHECK(unused("-- selene: allow(unused_variable)\nlocal a = 1\nlocal b = 2\n") == size_t(1));
        UNSCOPED_INFO("a whole function beside it");
        CHECK(unused("-- selene: allow(unused_variable)\nlocal function f()\n    local a = 1\nend\nf()\nlocal b = 2\n") == size_t(1));
        UNSCOPED_INFO("at a line's end, its statement");
        CHECK(unused("local a = 1 -- selene: allow(unused_variable)\nlocal b = 2\n") == size_t(1));
        bool denied = false;
        for (const ALScriptProblem& p : service.check("-- selene: deny(unused_variable)\nlocal a = 1\n"))
        {
            denied |= p.key.compare(0, 19, "LuauLintLocalUnused") == 0 && p.severity == ALScriptProblem::Severity::Error;
        }
        UNSCOPED_INFO("denied: an error");
        CHECK(denied);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: an overloaded function's signature shows every form and the one that fits", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions load: " + error);
        CHECK(loaded);
        const std::string       script = "local f: ((n: number) -> ()) & ((a: string, b: string) -> ()) = nil :: any\nf(\"x\", \"y\")\n";
        const ALScriptSignature sig    = service.signature(script, 1, 7);
        REQUIRE(sig.found);
        REQUIRE(sig.overloads.size() == size_t(2));
        UNSCOPED_INFO("the second fits two arguments");
        CHECK(sig.overload == 1);
        CHECK(sig.label == sig.overloads[1].label);
        REQUIRE(sig.parameters.size() == 2);
        CHECK(sig.active == 1);
        UNSCOPED_INFO("one form: none listed");
        REQUIRE(service.signature("local function g(a: number) end\ng(1)\n", 1, 2).overloads.empty());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: type checked once for problems and once for questions; once if strict or new", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        ALLuauConfig config;
        service.setConfig(config);
        const bool   one_slot = service.newSolver();
        const size_t start    = service.typeChecks();
        // As the studio asks: a check with the outline, the names and the
        // hints; then questions and completions in turn, and the check again.
        const std::string loose = "local function f(a) return a + 1 end\nlocal y = f(2)\nprint(y)\n";
        const std::string first = said(service.check(loose));
        service.outline(loose);
        service.semanticTokens(loose);
        service.inlayHints(loose, true, true);
        service.complete(loose, 2, 2);
        service.hover(loose, 1, 6);
        service.complete(loose, 2, 2);
        service.signature(loose, 1, 12);
        service.references(loose, 1, 6);
        UNSCOPED_INFO("nonstrict: the check, and the questions' own");
        CHECK(service.typeChecks() - start == size_t(one_slot ? 1 : 2));
        UNSCOPED_INFO("the check again, from what it found");
        CHECK(said(service.check(loose)) == first);
        UNSCOPED_INFO("and not checked again");
        CHECK(service.typeChecks() - start == size_t(one_slot ? 1 : 2));

        // A script strict by its own comment: its check serves the questions.
        const std::string strict   = "--!strict\n" + loose;
        const size_t      before   = service.typeChecks();
        service.hover(strict, 2, 6);
        service.check(strict);
        service.outline(strict);
        service.inlayHints(strict, true, true);
        UNSCOPED_INFO("strict: one check, the question first");
        CHECK(service.typeChecks() - before == size_t(1));
        service.complete(strict, 3, 2);
        UNSCOPED_INFO("a completion reads autocomplete's own");
        CHECK(service.typeChecks() - before == size_t(one_slot ? 1 : 2));
        const std::string strict_first = said(service.check(strict));
        service.hover(strict, 2, 6);
        UNSCOPED_INFO("and nothing checked again");
        CHECK(service.typeChecks() - before == size_t(one_slot ? 1 : 2));
        // The types a question reads are the strict ones, whatever the
        // script's mode: a nonstrict check's module knows none of them.
        const std::string typed       = "local s = string.rep(\"a\", 2)\nlocal n = #s\nprint(n)\n";
        const auto        loose_hints = service.inlayHints(typed, false, true);
        const auto        strict_hints = service.inlayHints("--!strict\n" + typed, false, true);
        UNSCOPED_INFO("hints read nonstrict");
        REQUIRE(loose_hints.size() == size_t(2));
        UNSCOPED_INFO("and strict");
        REQUIRE(strict_hints.size() == size_t(2));
        CHECK(loose_hints[0].text == strict_hints[0].text);
        CHECK(loose_hints[1].text == strict_hints[1].text);
        {
            INFO("the problems of each told apart");
            CHECK(said(service.check(loose)) == first);
            CHECK(said(service.check(strict)) == strict_first);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the same definitions and docs again load nothing and keep what was checked", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local n: number = 1\nprint(n)\n";
        service.check(script);
        const size_t checked = service.typeChecks();
        std::string  why;
        {
            INFO("the same again: loaded");
            REQUIRE(service.loadDefinitions(definitions, why));
            REQUIRE(why.empty());
        }
        service.check(script);
        UNSCOPED_INFO("what was checked stands");
        CHECK(service.typeChecks() == checked);
        const std::string docs = "{\"@sl-slua/global/ll.Say\": {\"documentation\": \"Says it.\", \"learn_more_link\": \"x\"}}";
        CHECK(service.loadDocs(docs, why));
        CHECK(service.hasDocs());
        {
            INFO("and the same again");
            CHECK(service.loadDocs(docs, why));
            CHECK(service.hasDocs());
        }
        // Other definitions are loaded over, and the script checked again.
        UNSCOPED_INFO("others loaded");
        CHECK(service.loadDefinitions(definitions + "\ndeclare function extraThing(): number\n", why));
        service.check(script);
        UNSCOPED_INFO("checked again against them");
        CHECK(service.typeChecks() > checked);
        service.loadDefinitions(definitions, why);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a local's type is hinted at any depth and offered as a refactor at its line", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local a = 1\n"
                                   "local function f()\n"
                                   "    local b = \"x\"\n"
                                   "    if a > 0 then\n"
                                   "        local c = a + 1\n"
                                   "        print(b, c)\n"
                                   "    end\n"
                                   "end\n"
                                   "f()\n";
        const std::vector<ALScriptInlayHint> hints = service.inlayHints(script, false, true);
        std::string                          all;
        for (const ALScriptInlayHint& hint : hints)
        {
            all += std::to_string(hint.line) + hint.text + ";";
        }
        UNSCOPED_INFO("every local's, at every depth");
        CHECK(all == std::string("0: number;2: string;4: number;"));
        for (const auto& [line, column, type] : { std::tuple{ 0, 6, "number" }, std::tuple{ 2, 10, "string" }, std::tuple{ 4, 14, "number" } })
        {
            bool offered = false;
            for (const ALScriptFix& fix : service.actions(script, line, column, line, column))
            {
                offered |= fix.title == std::string("Declare it as '") + type + "'";
            }
            CAPTURE(line, type);
            REQUIRE(offered);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: each script its own module and configuration; the oldest of many let go", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string a = "local a: number = 1\nprint(a)\n";
        const std::string b = "local b: string = \"x\"\nprint(b)\n";
        ALLuauConfig      loose;
        ALLuauConfig      strict;
        strict.mode = "strict";
        service.setDocument("a");
        service.setConfig(strict);
        service.check(a);
        service.setDocument("b");
        service.setConfig(loose);
        service.check(b);
        const size_t both = service.typeChecks();
        for (int turn = 0; turn < 3; ++turn)
        {
            service.setDocument("a");
            service.setConfig(strict);
            service.check(a);
            service.setDocument("b");
            service.setConfig(loose);
            service.check(b);
        }
        UNSCOPED_INFO("turn and turn about, each as it was left");
        CHECK(service.typeChecks() == both);
        // Past a few, the one asked of longest ago is let go of.
        for (const char* other : { "c", "d", "e" })
        {
            service.setDocument(other);
            service.setConfig(loose);
            service.check(std::string("local ") + other + " = 1\n");
        }
        const size_t now = service.typeChecks();
        service.setDocument("b");
        service.setConfig(loose);
        service.check(b);
        UNSCOPED_INFO("one of the last few kept");
        CHECK(service.typeChecks() == now);
        service.setDocument("a");
        service.setConfig(strict);
        service.check(a);
        UNSCOPED_INFO("the one asked of longest ago let go of");
        CHECK(service.typeChecks() == now + 1);
        // The module of no name is apart from them all.
        service.setDocument("");
        service.setConfig(loose);
        service.check(a);
        UNSCOPED_INFO("unnamed: its own");
        CHECK(service.typeChecks() == now + 2);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module put ahead of the script is passed over by the names and the hints", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string text = "local __modules = {}\n"
                                 "__modules[\"m\"] = (function()\nlocal inner = 1\nreturn inner\nend)()\n"
                                 "local own = __modules[\"m\"]\nprint(own)\n";
        service.setPassedOver({ { 1, 4 } });
        bool in_module = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_module |= token.span.line >= 1 && token.span.line <= 4;
        }
        UNSCOPED_INFO("no names coloured in the module");
        CHECK_FALSE(in_module);
        bool hinted_module = false, hinted_own = false;
        for (const ALScriptInlayHint& hint : service.inlayHints(text, false, true))
        {
            hinted_module |= hint.line >= 1 && hint.line <= 4;
            hinted_own |= hint.line == 5;
        }
        UNSCOPED_INFO("no hints in the module");
        CHECK_FALSE(hinted_module);
        UNSCOPED_INFO("the script's own hinted");
        CHECK(hinted_own);
        service.setPassedOver({});
        in_module = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_module |= token.span.line >= 1 && token.span.line <= 4;
        }
        UNSCOPED_INFO("with nothing passed over, coloured");
        CHECK(in_module);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: required modules are checked on their own and not again for a script edit", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        ALLuauConfig config;
        service.setDocument("uses");
        service.setConfig(config);
        ALLuauService::Modules modules;
        modules.modules.push_back(
            { "disk:/lib/util.luau", "--!strict\nlocal M = {}\nfunction M.twice(n: number): number\n    return n * 2\nend\nlocal bad: number = \"x\"\nreturn M\n" });
        modules.reaches.push_back({ "", "util", "disk:/lib/util.luau" });
        service.setModules(modules);
        const std::string script = "--!strict\nlocal util = require(\"util\")\nlocal s: string = util.twice(2)\n";
        const ALScriptProblems problems = service.check(script);
        bool typed = false, module_said = false;
        for (const ALScriptProblem& problem : problems)
        {
            typed |= problem.file.empty() && problem.line == 2;
            module_said |= problem.file == "disk:/lib/util.luau" && problem.line == 5;
        }
        UNSCOPED_INFO("the module's types reach the script: " + said(problems));
        CHECK(typed);
        UNSCOPED_INFO("the module's own problem is the module's, in its lines: " + said(problems));
        CHECK(module_said);
        // The script typed in: only the script is checked again.
        const size_t modules_before = service.modulesChecked();
        service.check(script + "print(s)\n");
        UNSCOPED_INFO("the script alone");
        CHECK(service.modulesChecked() - modules_before == size_t(1));
        // The module changed: it, and the script that requires it.
        modules.modules[0].text = "local M = {}\nfunction M.twice(n: number): string\n    return tostring(n * 2)\nend\nreturn M\n";
        service.setModules(modules);
        const size_t again = service.modulesChecked();
        const ALScriptProblems now = service.check(script + "print(s)\n");
        UNSCOPED_INFO("both");
        CHECK(service.modulesChecked() - again == size_t(2));
        bool still = false;
        for (const ALScriptProblem& problem : now)
        {
            still |= problem.line == 2 && problem.file.empty() && problem.severity == ALScriptProblem::Severity::Error;
        }
        UNSCOPED_INFO("and what it gives now fits: " + said(now));
        CHECK_FALSE(still);
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the outline lists handlers as their events and timers with what is inside", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "LLEvents:on(\"touch_start\", function(events)\n"
            "    local who = events[1]\n"
            "    local function greet() end\n"
            "end)\n"
            "LLEvents:once(\"listen\", function(events) end)\n"
            "LLTimers:every(2.5, function() end)\n"
            "LLTimers:once(delay, function() end)\n"
            "LLEvents:off(\"touch_start\", nothing)\n";
        std::vector<ALScriptOutlineEntry> outline = service.outline(script);
        std::string names;
        for (const ALScriptOutlineEntry& e : outline) names += " " + e.name + llformat("@%d", e.depth);
        UNSCOPED_INFO("five entries:" + names);
        REQUIRE(outline.size() == size_t(5));
        {
            INFO("the touch as an event");
            CHECK(outline[0].name == "touch_start");
            CHECK(outline[0].kind == ALScriptSymbolKind::Event);
            CHECK(outline[0].depth == 0);
        }
        {
            INFO("named where its event is");
            CHECK(outline[0].nameSpan.line == 0);
            CHECK(outline[0].nameSpan.column == 12);
        }
        {
            INFO("spanning the call");
            CHECK(outline[0].span.line == 0);
            CHECK(outline[0].span.endLine == 3);
        }
        {
            INFO("what it holds, one deeper");
            CHECK(outline[1].name == "greet");
            CHECK(outline[1].depth == 1);
        }
        {
            INFO("once, too");
            CHECK(outline[2].name == "listen");
            CHECK(outline[2].kind == ALScriptSymbolKind::Event);
        }
        UNSCOPED_INFO("a timer, how often");
        CHECK(outline[3].name == std::string("timer every 2.5"));
        UNSCOPED_INFO("a timer whose delay the script works out");
        CHECK(outline[4].name == std::string("timer once"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: every SLua template Script Studio offers checks without an error", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        // Nonstrict, as the grid compiles: the new solver checking strict
        // cannot yet push an overloaded function's parameter types into a
        // function given to it, so every LLEvents:on there is an error it
        // is upstream's to put right.
        ALLuauConfig grid;
        grid.mode = "nonstrict";
        service.setConfig(grid);
        llifstream in(std::string(AL_SCRIPT_TEMPLATES_DIR) + "/slua.xml", std::ios::in | std::ios::binary);
        LLSD       templates;
        {
            INFO("read");
            CHECK(in.is_open());
            CHECK(LLSDSerialize::fromXML(templates, in) != LLSDParser::PARSE_FAILURE);
            REQUIRE(templates.isArray());
        }
        REQUIRE(templates.size() > 0);
        for (LLSD::array_const_iterator it = templates.beginArray(); it != templates.endArray(); ++it)
        {
            const std::string      name     = (*it)["name"].asString();
            const ALScriptProblems problems = service.check((*it)["body"].asString());
            std::string            wrong;
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.severity == ALScriptProblem::Severity::Error)
                {
                    wrong += llformat(" [%d:%d] %s", problem.line, problem.column, problem.message.c_str());
                }
            }
            CAPTURE(name, wrong);
            REQUIRE(wrong.empty());
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the studio's own lints answer to config and --!nolint and selene and NOLINT", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto found = [&](const std::string& text) {
            ALScriptProblems out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlCompoundAssign" || p.key.rfind("LuauLintDirective", 0) == 0)
                {
                    out.push_back(p);
                }
            }
            return out;
        };
        const std::string script = "local x = 1\nx = x + 1\nlocal t = { n = \"a\" }\nt.n = t.n .. \"b\"\nx = 1 + x\nprint(x, t.n)\n";
        ALScriptProblems  said   = found(script);
        UNSCOPED_INFO("two: x and t.n");
        REQUIRE(said.size() == size_t(2));
        {
            INFO("the first, a note in its words: " + said[0].message);
            CHECK(said[0].key == "LuauLintSlCompoundAssign");
            CHECK(said[0].severity == ALScriptProblem::Severity::Note);
            CHECK(said[0].line == 1);
            CHECK(said[0].args == std::vector<std::string>{ "x", "+" });
            CHECK(said[0].source == ALScriptProblem::Source::Lint);
        }
        {
            INFO("a field, joined: " + said[1].message);
            CHECK(said[1].line == 3);
            CHECK(said[1].args == std::vector<std::string>{ "t.n", ".." });
        }
        {
            INFO("a NOLINT answers to its name");
            CHECK(ALScriptFixes::suppressed(said[0], "x = x + 1 -- NOLINT(SlCompoundAssign)", "", true));
            CHECK_FALSE(ALScriptFixes::suppressed(said[0], "x = x + 1 -- NOLINT(LocalUnused)", "", true));
        }

        ALLuauConfig config;
        config.slLints = 0;
        service.setConfig(config);
        UNSCOPED_INFO("off");
        REQUIRE(found(script).empty());
        config.slLints      = ALScriptLintPass::defaults();
        config.slFatalLints = ALScriptLintPass::bit("SlCompoundAssign");
        service.setConfig(config);
        said = found(script);
        {
            INFO("an error");
            REQUIRE(said.size() == 2);
            CHECK(said[0].severity == ALScriptProblem::Severity::Error);
        }
        config.slFatalLints = 0;
        config.lintErrors   = true;
        service.setConfig(config);
        said = found(script);
        {
            INFO("every lint an error");
            REQUIRE(said.size() == 2);
            CHECK(said[0].severity == ALScriptProblem::Severity::Error);
        }
        service.setConfig(ALLuauConfig());

        said = found("--!nolint SlCompoundAssign\n" + script);
        UNSCOPED_INFO("--!nolint by its name, and no word that Luau does not know it");
        REQUIRE(said.empty());
        UNSCOPED_INFO("--!nolint alone");
        REQUIRE(found("--!nolint\n" + script).empty());
        said = found("--!nolint SlNope\n" + script);
        {
            INFO("a name that is none is still said");
            REQUIRE(said.size() == 3);
            CHECK(said[0].key.rfind("LuauLintDirective", 0) == 0);
        }

        said = found("local x = 1\nx = x + 1 -- selene: allow(SlCompoundAssign)\nprint(x)\n");
        UNSCOPED_INFO("selene's comment beside it");
        REQUIRE(said.empty());
        said = found("--# selene: deny(SlCompoundAssign)\nlocal x = 1\nx = x + 1\nprint(x)\n");
        {
            INFO("selene's for the whole file, an error");
            REQUIRE(said.size() == 1);
            CHECK(said[0].severity == ALScriptProblem::Severity::Error);
        }
        {
            INFO("selene's words for the studio's own");
            CHECK(ALSeleneFilters::slLints("SlCompoundAssign") == ALScriptLintPass::bit("SlCompoundAssign"));
            CHECK(ALSeleneFilters::slLints("unused_variable") == 0);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: SlNumberTruth: a number used as a condition; not a number? or a comparison", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto found = [&](const std::string& text) {
            std::vector<std::string> out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlNumberTruth")
                {
                    REQUIRE_FALSE(p.args.empty());
                    out.push_back(p.key + " " + p.args[0] + " " + std::to_string(p.line));
                }
            }
            return out;
        };
        const std::vector<std::string> said = found("local n = 0\n"
                                                    "if n then print(1) end\n"
                                                    "while (true and n) do break end\n"
                                                    "local b = not n\n"
                                                    "local t = if n then 1 else 2\n"
                                                    "local m: number? = nil\n"
                                                    "if m then print(m) end\n"
                                                    "if n ~= 0 or not (n > 1) then print(b, t) end\n");
        REQUIRE(said.size() == size_t(4));
        CHECK(said[0] == "LuauLintSlNumberTruth n 1");
        CHECK(said[1] == "LuauLintSlNumberTruth n 2");
        CHECK(said[2] == "LuauLintSlNumberTruthNot n 3");
        CHECK(said[3] == "LuauLintSlNumberTruth n 4");
        // A local by what it is given, where the old solver's nonstrict
        // mode says any: a copy of a number is one; one given a string, or
        // joined to one, or a parameter, is not.
        const std::vector<std::string> given = found("local n = llcompat.ListFindList({1}, {1})\n"
                                                     "local copy = n\n"
                                                     "local s = 0\n"
                                                     "s = \"x\"\n"
                                                     "local j = 0\n"
                                                     "j ..= \"y\"\n"
                                                     "local function f(p) if p then return 1 end return 0 end\n"
                                                     "if copy or s or j then print(f(n)) end\n");
        // The new solver types j a number still, and s the string it was
        // last given, which SLua counts true as it does every string, and
        // is taken at its word.
        std::string all;
        for (const std::string& one : given)
        {
            all += one + "|";
        }
        {
            INFO("a copy only: " + all);
            REQUIRE(given.size() == (newSolver ? 3u : 1u));
            CHECK(given[0] == "LuauLintSlNumberTruth copy 7");
            CHECK((!newSolver || (given[1] == "LuauLintSlStringTruth s 7" && given[2] == "LuauLintSlNumberTruth j 7")));
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: no mode means the solver's own default; nonstrict means nonstrict in either", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "local function f(): number? return nil end\nlocal x = f() + 1\nprint(x)\n";
        const auto        nil_said = [&] {
            for (const ALScriptProblem& p : service.check(script))
            {
                if (p.severity == ALScriptProblem::Severity::Error && p.line == 1)
                {
                    return true;
                }
            }
            return false;
        };
        service.setConfig(ALLuauConfig());
        const bool by_default = nil_said();
        ALLuauConfig nonstrict;
        nonstrict.mode = "nonstrict";
        service.setConfig(nonstrict);
        const bool asked_nonstrict = nil_said();
        if (newSolver)
        {
            UNSCOPED_INFO("strict where nothing says");
            CHECK(by_default);
            UNSCOPED_INFO("nonstrict where asked");
            CHECK_FALSE(asked_nonstrict);
        }
        else
        {
            UNSCOPED_INFO("nonstrict, whether asked or not");
            CHECK(by_default == asked_nonstrict);
        }
        service.setConfig(ALLuauConfig());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: SlNilSentinel: finds against -1 or 0 and llcompat's against nil only", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto found = [&](const std::string& text) {
            std::vector<std::string> out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlNilSentinel")
                {
                    REQUIRE(p.args.size() > 4);
                    out.push_back(p.key + " " + p.args[0] + " " + p.args[4] + " " + std::to_string(p.line) +
                                  (p.severity == ALScriptProblem::Severity::Error ? "" : " not an error"));
                }
            }
            std::sort(out.begin(), out.end());
            return out;
        };
        const std::vector<std::string> said = found("local l = {1, 2}\n"
                                                    "if ll.ListFindList(l, {2}) == -1 then print(1) end\n"
                                                    "if -1 ~= ll.SubStringIndex(\"ab\", \"b\") then print(2) end\n"
                                                    "local i = ll.ListFindList(l, {1})\n"
                                                    "if i < 0 or 0 <= i then print(3) end\n"
                                                    "if table.find(l, 2) ~= -1 then print(4) end\n"
                                                    "local s = \"abc\"\n"
                                                    "if s:find(\"b\") == -1 or string.find(s, \"c\") > -1 then print(5) end\n"
                                                    "local c = llcompat.ListFindList(l, {1})\n"
                                                    "if c == nil or (llcompat.SubStringIndex(s, \"a\")) ~= nil then print(6) end\n");
        const std::vector<std::string> expected = {
            "LuauLintSlNilSentinel c == -1 9",
            "LuauLintSlNilSentinel ll.ListFindList(...) == nil 1",
            "LuauLintSlNilSentinel s:find(...) == nil 7",
            "LuauLintSlNilSentinelAlways ll.SubStringIndex(...) ~= nil 2",
            "LuauLintSlNilSentinelAlways llcompat.SubStringIndex(...) ~= -1 9",
            "LuauLintSlNilSentinelAlways table.find(...) ~= nil 5",
            "LuauLintSlNilSentinelOrder i == nil 4",
            "LuauLintSlNilSentinelOrder i ~= nil 4",
            "LuauLintSlNilSentinelOrder string.find(...) ~= nil 7",
        };
        std::string listed;
        for (const std::string& each : said)
        {
            listed += each + "\n";
        }
        UNSCOPED_INFO("each, an error:\n" + listed);
        CHECK(said == expected);
        // What can hold: nil against SLua's, -1 and order against
        // llcompat's, an order that asks where, a local given a number
        // too, stepped, or nil before it is given llcompat's, a
        // parameter, and a -1 that is no find's.
        const std::vector<std::string> none = found("local l = {1}\n"
                                                    "if ll.ListFindList(l, {1}) == nil or ll.ListFindList(l, {1}) > 0 then print(1) end\n"
                                                    "if llcompat.ListFindList(l, {1}) == -1 or llcompat.ListFindList(l, {1}) < 0 then print(2) end\n"
                                                    "local j = ll.ListFindList(l, {1})\n"
                                                    "j = 5\n"
                                                    "local n = ll.ListFindList(l, {1})\n"
                                                    "n -= 2\n"
                                                    "local m\n"
                                                    "m = llcompat.SubStringIndex(\"a\", \"b\")\n"
                                                    "local function f(p) return p == -1 end\n"
                                                    "if j == -1 or n == -1 or m == nil or ll.GetInventoryType(\"x\") == -1 then print(f(3)) end\n");
        UNSCOPED_INFO("none: " + (none.empty() ? std::string() : none[0]));
        REQUIRE(none.empty());
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a shape is the untyped outline from a parse alone even of a broken text", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "local count = 1\n"
            "local function half(n: number)\n"
            "    local function quarter() return n / 4 end\n"
            "    return n / 2\n"
            "end\n"
            "function LLEvents.touch_start(n: number) end\n";
        const std::vector<ALScriptOutlineEntry> outline = service.outline(script);
        const std::vector<ALScriptOutlineEntry> shape   = ALLuauService::shape(script);
        REQUIRE(shape.size() == outline.size());
        for (size_t i = 0; i < shape.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(shape[i].name == outline[i].name);
            REQUIRE(shape[i].kind == outline[i].kind);
            REQUIRE(shape[i].span == outline[i].span);
            REQUIRE(shape[i].depth == outline[i].depth);
            UNSCOPED_INFO("no type");
            REQUIRE(shape[i].detail.empty());
        }
        const std::vector<ALScriptOutlineEntry> again = service.outline(script);
        REQUIRE_FALSE(again.empty());
        UNSCOPED_INFO("the outline still typed");
        CHECK(again[0].detail == "number");
        const std::vector<ALScriptOutlineEntry> broken = ALLuauService::shape("local function a()\n  x = \nend\nlocal function b()\nend\n");
        {
            INFO("broken in a: both still there");
            REQUIRE(broken.size() == 2);
            CHECK(broken[0].name == "a");
            CHECK(broken[1].name == "b");
            CHECK(broken[1].span.line == 3);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: completion says what fits and where brackets go; methods only after a colon", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto entry = [](const std::vector<ALScriptCompletion>& found, const std::string& text) -> const ALScriptCompletion* {
            for (const ALScriptCompletion& c : found)
            {
                if (c.text == text)
                {
                    return &c;
                }
            }
            return nullptr;
        };
        // What a number is wanted for. Asked as the studio asks, where the
        // word being typed begins.
        const std::string               typed = "local count: number = 1\nlocal name = \"x\"\nlocal n: number = c\n";
        std::vector<ALScriptCompletion> found = service.complete(typed, 2, 18);
        const ALScriptCompletion*       count = entry(found, "count");
        const ALScriptCompletion*       name  = entry(found, "name");
        {
            INFO("both offered");
            REQUIRE(count);
            REQUIRE(name);
        }
        UNSCOPED_INFO("the number fits");
        CHECK(count->fits);
        UNSCOPED_INFO("the string does not");
        CHECK_FALSE(name->fits);
        UNSCOPED_INFO("said where an expression goes");
        CHECK(count->context == ALScriptCompletion::Context::Expression);

        // Where a call's brackets go: between them for what takes
        // something, after them for what takes nothing, none for what is no
        // call or is wanted as the function it is.
        const std::string calls = "local function now(): number return 1 end\n"
                                  "local function twice(n: number): number return n * 2 end\n"
                                  "\n"
                                  "local f: (number) -> number = t\n";
        found                         = service.complete(calls, 2, 0);
        const ALScriptCompletion* say = entry(found, "print");
        const ALScriptCompletion* now = entry(found, "now");
        const ALScriptCompletion* lib = entry(found, "ll");
        {
            INFO("print, between its brackets");
            REQUIRE(say);
            CHECK(say->brackets == ALScriptCompletion::Brackets::Inside);
        }
        {
            INFO("now, after an empty pair");
            REQUIRE(now);
            CHECK(now->brackets == ALScriptCompletion::Brackets::After);
        }
        {
            INFO("ll, no call to say anything of");
            REQUIRE(lib);
            CHECK(lib->brackets == ALScriptCompletion::Brackets::Guess);
        }
        found                           = service.complete(calls, 3, 30);
        const ALScriptCompletion* twice = entry(found, "twice");
        {
            INFO("twice, wanted as it is");
            REQUIRE(twice);
            CHECK(twice->fits);
        }
        UNSCOPED_INFO("so no brackets");
        CHECK(twice->brackets == ALScriptCompletion::Brackets::None);

        // A method is called with a colon, and after a dot is no use. The
        // grid's own declare theirs as fields taking self, which are called
        // with a colon all the same.
        const std::string events = "local a = LLEvents.\nlocal b = LLEvents:\nlocal t = {}\nfunction t:greet(): number return 1 end\nlocal d = t:\n";
        found                    = service.complete(events, 0, 19);
        {
            INFO("none of LLEvents' methods after a dot");
            REQUIRE_FALSE(entry(found, "on"));
            REQUIRE_FALSE(entry(found, "off"));
            REQUIRE_FALSE(entry(found, "handlers"));
        }
        found = service.complete(events, 1, 19);
        {
            INFO("all of them after a colon");
            REQUIRE(entry(found, "on"));
            REQUIRE(entry(found, "off"));
            REQUIRE(entry(found, "handlers"));
        }
        UNSCOPED_INFO("a script's own method after a colon");
        REQUIRE(entry(service.complete(events, 4, 12), "greet") != nullptr);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a function an argument wants is offered as a stub to fill in", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto stub = [](const std::vector<ALScriptCompletion>& found) -> const ALScriptCompletion* {
            for (const ALScriptCompletion& c : found)
            {
                if (!c.snippet.empty())
                {
                    return &c;
                }
            }
            return nullptr;
        };
        // At `f`, where the second argument begins.
        const std::string                     each      = "local function each(n: number, f: (i: number) -> ()) end\neach(3, f)\n";
        const std::vector<ALScriptCompletion> for_each  = service.complete(each, 1, 8);
        const ALScriptCompletion*             own       = stub(for_each);
        UNSCOPED_INFO("Luau's stub offered");
        REQUIRE(own != nullptr);
        UNSCOPED_INFO("its head");
        CHECK(own->text == std::string("function(i: number)"));
        UNSCOPED_INFO("its body to fill, then its end");
        CHECK(own->snippet == std::string("function(i: number)\n    $0\nend"));
        UNSCOPED_INFO("of the type wanted");
        CHECK(own->fits);

        const std::string                     events    = "LLEvents:on(\"touch_start\", f)\n";
        const std::vector<ALScriptCompletion> for_event = service.complete(events, 0, 27);
        const ALScriptCompletion*             handler   = stub(for_event);
        UNSCOPED_INFO("the event's handler offered");
        REQUIRE(handler != nullptr);
        UNSCOPED_INFO("taking what the event gives");
        CHECK(handler->text == std::string("function(detected: {DetectedEvent})"));
        UNSCOPED_INFO("a body to fill: " + handler->snippet);
        REQUIRE(handler->snippet.find("\n    $0\nend") != std::string::npos);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: after --! only the hot comments the studio knows are offered", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::vector<ALScriptCompletion> found = service.complete("--!st\nlocal x = 1\n", 0, 3);
        std::string                           said;
        for (const ALScriptCompletion& c : found)
        {
            said += " " + c.text;
            CAPTURE(c.text);
            REQUIRE(c.kind == ALScriptSymbolKind::Keyword);
            REQUIRE(c.context == ALScriptCompletion::Context::HotComment);
            REQUIRE(ALScriptLexicon::isLuauHotComment(c.text));
        }
        UNSCOPED_INFO("each of them:" + said);
        REQUIRE(found.size() == std::size(ALScriptLexicon::LUAU_HOT_COMMENTS));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: fragments answer completion and signature help over the statement typed", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const auto offered = [](const std::vector<ALScriptCompletion>& found, const std::string& text) -> const ALScriptCompletion* {
            for (const ALScriptCompletion& c : found)
            {
                if (c.text == text)
                {
                    return &c;
                }
            }
            return nullptr;
        };
        std::string why;
        CHECK(service.loadDocs("{\"@sl-slua/global/ll.Say\": {\"documentation\": \"Says it.\", \"learn_more_link\": \"x\"},"
                               " \"@sl-slua/global/ll\": {\"documentation\": \"The library.\", \"learn_more_link\": \"x\"},"
                               " \"@sl-slua/global/vector\": {\"documentation\": \"Makes one.\", \"learn_more_link\": \"x\"},"
                               " \"@sl-slua/global/vector.magnitude\": {\"documentation\": \"Its length.\", \"learn_more_link\": \"x\"}}",
                               why));
        const std::string base = "local count: number = 1\n"
                                 "local function twice(n: number): number return n * 2 end\n"
                                 "local t = { a = 1, b = \"x\" }\n";
        // Off until told: an edit, and the whole script is checked again.
        service.check(base);
        service.complete(base, 0, 0);
        size_t checks = service.typeChecks();
        service.complete(base + "local n: number = co\n", 3, 18);
        UNSCOPED_INFO("off: checked whole");
        CHECK(service.typeChecks() > checks);
        UNSCOPED_INFO("a call of a field documented as the field, not what holds it");
        CHECK(service.signature(base + "ll.Say(0, )\n", 3, 10).documentation == std::string("Says it."));
        // `vector` is a table that is also called, whose fields Luau gives
        // no symbol of their own.
        UNSCOPED_INFO("and of a callable table's field");
        CHECK(service.signature(base + "local m = vector.magnitude()\n", 3, 27).documentation == std::string("Its length."));

        service.setFragments(true);
        CHECK(service.fragments());
        service.check(base);
        service.complete(base, 0, 0);
        checks                                = service.typeChecks();
        const size_t                    parts = service.fragmentsChecked();
        std::vector<ALScriptCompletion> found = service.complete(base + "local n: number = co\n", 3, 18);
        const ALScriptCompletion*       count = offered(found, "count");
        UNSCOPED_INFO("a local above offered");
        REQUIRE(count != nullptr);
        UNSCOPED_INFO("and fitting where a number is wanted");
        CHECK(count->fits);
        UNSCOPED_INFO("a function in scope offered");
        REQUIRE(offered(found, "twice") != nullptr);
        UNSCOPED_INFO("a table's fields");
        REQUIRE(offered(service.complete(base + "local y = t.\n", 3, 12), "b") != nullptr);
        found = service.complete(base + "LLEvents:\n", 3, 9);
        {
            INFO("an object's methods");
            REQUIRE(offered(found, "on"));
            REQUIRE(offered(found, "off"));
        }

        ALScriptSignature said = service.signature(base + "ll.Say(0, )\n", 3, 10);
        UNSCOPED_INFO("a call's signature");
        REQUIRE(said.found);
        CHECK(said.label == std::string("ll.Say(channel: number, msg: string): ()"));
        CHECK(said.active == 1);
        UNSCOPED_INFO("documented as the field it calls");
        CHECK(said.documentation == std::string("Says it."));
        said = service.signature(base + "if twice() then end\n", 3, 9);
        {
            INFO("a call in a condition");
            REQUIRE(said.found);
            CHECK(said.label.find("twice(n: number)") == 0);
        }

        UNSCOPED_INFO("nothing offered in a comment");
        REQUIRE(service.complete(base + "-- tw\n", 3, 5).empty());
        UNSCOPED_INFO("no signature outside a call");
        REQUIRE_FALSE(service.signature(base + "local q = 1\n", 3, 8).found);
        UNSCOPED_INFO("a callable table's field over the fragment too");
        CHECK(service.signature(base + "local m = vector.magnitude()\n", 3, 27).documentation == std::string("Its length."));
        UNSCOPED_INFO("none of it checked whole");
        CHECK(service.typeChecks() == checks);
        UNSCOPED_INFO("each answered over its statement");
        CHECK(service.fragmentsChecked() - parts == size_t(6));
        service.setFragments(false);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the whole script answers where a fragment cannot and requires survive one", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setFragments(true);
        // Nothing checked yet to patch: the whole script.
        service.setDocument("unchecked");
        size_t checks = service.typeChecks();
        size_t parts  = service.fragmentsChecked();
        const std::string base = "local count: number = 1\n";
        REQUIRE_FALSE(service.complete(base + "local x = co\n", 1, 10).empty());
        {
            INFO("checked whole");
            CHECK(service.typeChecks() > checks);
            CHECK(service.fragmentsChecked() == parts);
        }

        // Stopped part way: nothing, and said so; the next, unstopped,
        // answers over its statement.
        ALLuauService::Stop stop = ALLuauService::newStop();
        ALLuauService::cancel(stop);
        service.setStop(stop);
        REQUIRE(service.complete(base + "local y = co\n", 1, 10).empty());
        CHECK(service.stopped());
        service.setStop(nullptr);
        checks = service.typeChecks();
        {
            INFO("answered again");
            REQUIRE_FALSE(service.complete(base + "local y = co\n", 1, 10).empty());
            CHECK_FALSE(service.stopped());
        }
        UNSCOPED_INFO("over its statement");
        CHECK(service.typeChecks() == checks);

        // Something typed above since the last check, in another block
        // than the line typed: Luau sets beside the last check only the
        // block the line is in, so the scope the fragment would be checked
        // in is no longer so, and the whole script answers. In the same
        // block, the fragment starts at the edit and holds it.
        service.setDocument("edited above");
        const std::string two = "local function twice(n: number): number return n * 2 end\nlocal function main()\n    local count = 1\nend\n";
        service.check(two);
        service.warm(two);
        checks = service.typeChecks();
        parts  = service.fragmentsChecked();
        const std::string changed = "local function twice(n: string): string return n end\nlocal function main()\n    local count = 1\n    local x = tw\nend\n";
        const std::vector<ALScriptCompletion> found = service.complete(changed, 3, 14);
        CHECK(offers(found, "twice"));
        {
            INFO("whole");
            CHECK(service.typeChecks() > checks);
            CHECK(service.fragmentsChecked() == parts);
        }
        bool string_now = false;
        for (const ALScriptCompletion& c : found)
        {
            string_now |= c.text == "twice" && c.detail.find("string") != std::string::npos;
        }
        UNSCOPED_INFO("as it is now");
        CHECK(string_now);

        // A module the script requires, its requires traced when the text
        // was parsed: a fragment between that parse and the next check
        // leaves them, which Luau's own would forget, and that check finds
        // what the module gives.
        ALLuauConfig config;
        service.setDocument("requires");
        service.setConfig(config);
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/util.luau", "local M = {}\nfunction M.twice(n: number): number\n    return n * 2\nend\nreturn M\n" });
        modules.reaches.push_back({ "", "util", "disk:/lib/util.luau" });
        service.setModules(modules);
        const std::string script = "--!strict\nlocal util = require(\"util\")\n";
        service.check(script);
        service.complete(script, 1, 0);
        const std::string typed = script + "local n = util.\n";
        service.hover(typed, 1, 8);
        UNSCOPED_INFO("the module's function, over the fragment");
        CHECK(offers(service.complete(typed, 2, 15), "twice"));
        service.setFragments(false);
        UNSCOPED_INFO("and the whole check after it finds it too");
        CHECK(offers(service.complete(typed, 2, 15), "twice"));
        service.setModules({});
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: warming after a check lets the next keystroke check nothing whole", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setFragments(true);
        service.setDocument("warmed");
        const std::string base = "local count: number = 1\n";
        const std::string typed = base + "local n: number = co\n";
        // A check alone leaves the old solver no module to patch: the
        // keystroke after checks the whole script.
        service.check(base);
        size_t checks = service.typeChecks();
        service.complete(typed, 1, 18);
        UNSCOPED_INFO("unwarmed");
        CHECK(service.typeChecks() - checks == size_t(newSolver ? 0 : 1));

        service.check(base);
        checks = service.typeChecks();
        service.warm(base);
        UNSCOPED_INFO("the old solver's autocomplete module checked; the new solver's check is it");
        CHECK(service.typeChecks() - checks == size_t(newSolver ? 0 : 1));
        service.warm(base);
        UNSCOPED_INFO("and not again for the same text");
        CHECK(service.typeChecks() - checks == size_t(newSolver ? 0 : 1));
        checks             = service.typeChecks();
        const size_t parts = service.fragmentsChecked();
        CHECK(offers(service.complete(typed, 1, 18), "count"));
        UNSCOPED_INFO("over its statement");
        CHECK(service.fragmentsChecked() - parts == size_t(1));
        UNSCOPED_INFO("nothing whole");
        CHECK(service.typeChecks() == checks);
        service.setFragments(false);
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: several modules required are checked once each", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("several");
        service.setConfig(ALLuauConfig());
        ALLuauService::Modules modules;
        std::string            script = "--!strict\n";
        for (int i = 1; i <= 4; ++i)
        {
            const std::string key = llformat("disk:/lib/m%d.luau", i);
            // The last requires the first: one waits for another.
            const std::string needs = i == 4 ? "local first = require(\"m1\")\n" : "\n";
            modules.modules.push_back({ key, "--!strict\n" + needs + "local bad: number = \"x\"\nlocal M = {}\nfunction M.f(n: number): number\n    return n\nend\nreturn M\n" });
            modules.reaches.push_back({ "", llformat("m%d", i), key });
            script += llformat("local m%d = require(\"m%d\")\n", i, i);
        }
        modules.reaches.push_back({ "disk:/lib/m4.luau", "m1", "disk:/lib/m1.luau" });
        service.setModules(modules);
        script += "local s: string = m1.f(1) + m2.f(2) + m3.f(3) + m4.f(4)\n";
        const size_t           before   = service.modulesChecked();
        const ALScriptProblems problems = service.check(script);
        UNSCOPED_INFO("each module and the script checked once");
        CHECK(service.modulesChecked() - before == size_t(5));
        S32  in_modules = 0;
        bool own        = false;
        for (const ALScriptProblem& problem : problems)
        {
            in_modules += problem.file.rfind("disk:/lib/m", 0) == 0 && problem.line == 2 && problem.source == ALScriptProblem::Source::Types ? 1 : 0;
            own |= problem.file.empty() && problem.line == 5 && problem.severity == ALScriptProblem::Severity::Error;
        }
        UNSCOPED_INFO("each module's own type error, in its lines: " + said(problems));
        CHECK(in_modules == 4);
        UNSCOPED_INFO("and the script's: " + said(problems));
        CHECK(own);

        // The script typed in: only it, on its own.
        const size_t again = service.modulesChecked();
        const ALScriptProblems edited = service.check(script + "print(s)\n");
        UNSCOPED_INFO("the script alone");
        CHECK(service.modulesChecked() - again == size_t(1));
        UNSCOPED_INFO("its own problem still: " + said(edited));
        REQUIRE(std::any_of(edited.begin(), edited.end(), [](const ALScriptProblem& problem) {
                   return problem.file.empty() && problem.line == 5;
               }));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a required module's lints are told in its lines without fixes and only once", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("linted");
        service.setConfig(ALLuauConfig());
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/util.luau", "local unusedThing = 1\n"   // 0
                                                           "local n = 1\n"             // 1
                                                           "n = n + 1\n"               // 2
                                                           "local M = {}\n"            // 3
                                                           "M.n = n\n"                 // 4
                                                           "return M\n" });            // 5
        modules.reaches.push_back({ "", "util", "disk:/lib/util.luau" });
        service.setModules(modules);
        const std::string script = "local util = require(\"util\")\n"
                                   "local x = util.n\n"
                                   "x = x + 1\n"
                                   "print(x)\n";
        const ALScriptProblems problems = service.check(script);
        const auto in = [&](const ALScriptProblems& found, const std::string& file, const std::string& code, S32 line) -> const ALScriptProblem* {
            for (const ALScriptProblem& problem : found)
            {
                if (problem.file == file && problem.code == code && problem.line == line)
                {
                    return &problem;
                }
            }
            return nullptr;
        };
        const ALScriptProblem* unused = in(problems, "disk:/lib/util.luau", "LocalUnused", 0);
        UNSCOPED_INFO("Luau's lint of the module, in its lines: " + said(problems));
        REQUIRE(unused != nullptr);
        const ALScriptProblem* compound = in(problems, "disk:/lib/util.luau", "SlCompoundAssign", 2);
        UNSCOPED_INFO("the studio's own of the module: " + said(problems));
        REQUIRE(compound != nullptr);
        {
            INFO("neither with a fix, being another file's");
            REQUIRE(unused->fixes.empty());
            REQUIRE(compound->fixes.empty());
        }
        const ALScriptProblem* own = in(problems, "", "SlCompoundAssign", 2);
        {
            INFO("the script's own, with its fix: " + said(problems));
            REQUIRE(own);
            REQUIRE_FALSE(own->fixes.empty());
        }

        const ALScriptProblems edited = service.check(script + "print(util)\n");
        UNSCOPED_INFO("an edit of the script alone: not the module's again");
        REQUIRE_FALSE(in(edited, "disk:/lib/util.luau", "LocalUnused", 0));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a required module's exports are kept from its type in declaration order", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("exports");
        service.setConfig(ALLuauConfig());
        const std::string made = "--!strict\n"
                                 "local function make()\n"
                                 "    local t = {}\n"
                                 "    t.alpha = 1\n"
                                 "    t.beta = function() end\n"
                                 "    return t\n"
                                 "end\n"
                                 "return make()\n";
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/made.luau", made });
        modules.reaches.push_back({ "", "made", "disk:/lib/made.luau" });
        service.setModules(modules);
        service.check("local made = require(\"made\")\nprint(made.alpha)\n");
        const std::optional<std::vector<std::string>> kept = ALLuauExports::checkedOf("disk:/lib/made.luau", made);
        UNSCOPED_INFO("kept as its type says");
        REQUIRE(kept.has_value());
        std::string listed;
        for (const std::string& name : *kept)
        {
            listed += " " + name;
        }
        UNSCOPED_INFO("each field, in order:" + listed);
        CHECK(*kept == std::vector<std::string>({ "alpha", "beta" }));
        UNSCOPED_INFO("not for another text");
        REQUIRE_FALSE(ALLuauExports::checkedOf("disk:/lib/made.luau", made + "\n"));

        // Its type no table, as a nonstrict check may leave one: nothing
        // kept, and its text alone says what it exports.
        const std::string loose = "local M = {}\nfunction M.gamma() end\nreturn (M :: any)\n";
        modules.modules.push_back({ "disk:/lib/loose.luau", loose });
        modules.reaches.push_back({ "", "loose", "disk:/lib/loose.luau" });
        service.setModules(modules);
        service.check("local made = require(\"made\")\nlocal loose = require(\"loose\")\nprint(made.alpha, loose)\n");
        UNSCOPED_INFO("no table, nothing kept");
        REQUIRE_FALSE(ALLuauExports::checkedOf("disk:/lib/loose.luau", loose));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module returning other than one value is an error at the require of it", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        for (const char* mode : { "", "nonstrict", "strict" })
        {
            ALLuauConfig config;
            config.mode = mode;
            service.setDocument("uses");
            service.setConfig(config);
            ALLuauService::Modules modules;
            modules.modules.push_back({ "disk:/lib/two.luau", "return 1, 2\n" });
            modules.modules.push_back({ "disk:/lib/none.luau", "local x = 1\n" });
            modules.modules.push_back({ "disk:/lib/one.luau", "return { x = 1 }\n" });
            modules.modules.push_back({ "disk:/lib/mid.luau", "local t = require(\"two\")\nreturn { t = t }\n" });
            modules.reaches.push_back({ "", "two", "disk:/lib/two.luau" });
            modules.reaches.push_back({ "", "none", "disk:/lib/none.luau" });
            modules.reaches.push_back({ "", "one", "disk:/lib/one.luau" });
            modules.reaches.push_back({ "", "mid", "disk:/lib/mid.luau" });
            modules.reaches.push_back({ "disk:/lib/mid.luau", "two", "disk:/lib/two.luau" });
            service.setModules(modules);
            const ALScriptProblems problems =
                service.check("local two = require(\"two\")\nlocal none = require(\"none\")\nlocal one = require(\"one\")\nlocal mid = require(\"mid\")\nprint(two, none, one, mid)\n");
            bool two = false, none = false, one = false, in_mid = false;
            for (const ALScriptProblem& problem : problems)
            {
                // In the studio's words, with how many, and no module's path.
                const bool said = problem.file.empty() && problem.severity == ALScriptProblem::Severity::Error &&
                                  problem.key == "LuauModuleNotOneValue" && problem.message.find("disk:") == std::string::npos;
                two |= said && problem.line == 0 && problem.args == std::vector<std::string>{ "2" };
                none |= said && problem.line == 1 && problem.args == std::vector<std::string>{ "0" };
                one |= problem.line == 2 && problem.severity == ALScriptProblem::Severity::Error;
                in_mid |= problem.file == "disk:/lib/mid.luau" && problem.line == 0 && problem.key == "LuauModuleNotOneValue";
            }
            const std::string where = std::string(mode) + ": " + said(problems);
            UNSCOPED_INFO("two values: said at its require, " + where);
            REQUIRE(two);
            UNSCOPED_INFO("no value: said at its require, " + where);
            REQUIRE(none);
            UNSCOPED_INFO("one: nothing, " + where);
            REQUIRE_FALSE(one);
            // Told as the module is checked, as all of a module's are: the
            // first time, its text unchanged after.
            UNSCOPED_INFO("a module's require of it: said in that module, " + where);
            REQUIRE((in_mid || *mode));
        }
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module a question checked first is told at the next check once", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("asked first");
        service.setConfig(ALLuauConfig());
        const std::string util = "--!strict\n"                   // 0
                                 "local unusedThing = 1\n"       // 1
                                 "local two = require(\"two\")\n" // 2
                                 "local M = {}\n"                // 3
                                 "M.n = two\n"                   // 4
                                 "return M\n";                   // 5
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/asked.luau", util });
        modules.modules.push_back({ "disk:/lib/two.luau", "return 1, 2\n" });
        modules.reaches.push_back({ "", "util", "disk:/lib/asked.luau" });
        modules.reaches.push_back({ "disk:/lib/asked.luau", "two", "disk:/lib/two.luau" });
        service.setModules(modules);
        // Strict, so that a hover checks the script as a check does under
        // either solver, and its modules with it: the check after finds
        // nothing to check.
        const std::string script = "--!strict\nlocal util = require(\"util\")\nprint(util.n)\n";
        service.hover(script, 2, 7);
        const size_t checks = service.typeChecks();
        const auto   told   = [](const ALScriptProblems& found, const std::string& code_or_key, S32 line) {
            return std::any_of(found.begin(), found.end(), [&](const ALScriptProblem& problem) {
                return problem.file == "disk:/lib/asked.luau" && (problem.code == code_or_key || problem.key == code_or_key) && problem.line == line;
            });
        };
        const ALScriptProblems problems = service.check(script);
        UNSCOPED_INFO("the check found the script checked");
        CHECK(service.typeChecks() == checks);
        UNSCOPED_INFO("the module's lint: " + said(problems));
        CHECK(told(problems, "LocalUnused", 1));
        UNSCOPED_INFO("its require of what returns two values: " + said(problems));
        CHECK(told(problems, "LuauModuleNotOneValue", 2));
        const std::optional<std::vector<std::string>> kept = ALLuauExports::checkedOf("disk:/lib/asked.luau", util);
        {
            INFO("what it exports, as its type says");
            REQUIRE(kept);
            REQUIRE(std::find(kept->begin(), kept->end(), "n") != kept->end());
        }
        // Told, and so not told again for the same text.
        const ALScriptProblems again = service.check(script);
        UNSCOPED_INFO("once: " + said(again));
        CHECK_FALSE(told(again, "LocalUnused", 1));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: hover on a type says it; only the script's own has a declaration to go to", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "--!strict\n"                         // 0
                                   "local id: uuid = ll.GetOwner()\n"   // 1
                                   "local r: rotation = ll.GetRot()\n"  // 2
                                   "type Pair = { a: number }\n"        // 3
                                   "local p: Pair = { a = 1 }\n"        // 4
                                   "print(id, r, p)\n";                 // 5
        ALScriptHover hover = service.hover(script, 1, 11); // `uuid`, an extern type
        {
            INFO("uuid, said: " + hover.label);
            REQUIRE(hover.found);
            CHECK(hover.label.rfind("type uuid", 0) == 0);
        }
        UNSCOPED_INFO("nowhere to go");
        CHECK_FALSE(hover.hasDefinition);
        hover = service.hover(script, 2, 10); // `rotation`, an alias
        {
            INFO("rotation, said: " + hover.label);
            REQUIRE(hover.found);
            CHECK(hover.label.rfind("type rotation", 0) == 0);
        }
        UNSCOPED_INFO("nowhere to go either");
        CHECK_FALSE(hover.hasDefinition);
        hover = service.hover(script, 4, 10); // `Pair`
        {
            INFO("the script's own, where it declares it");
            REQUIRE(hover.found);
            CHECK(hover.hasDefinition);
            REQUIRE(hover.definitionFile.empty());
            CHECK(hover.definitionLine == 3);
        }
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module's exports leave out keyword-named fields by type as by text", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("keywords");
        service.setConfig(ALLuauConfig());
        const std::string util = "--!strict\nreturn { [\"end\"] = 1, ok = 2, [\"not\"] = 3, export = 4 }\n";
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/keywords.luau", util });
        modules.reaches.push_back({ "", "util", "disk:/lib/keywords.luau" });
        service.setModules(modules);
        service.check("local util = require(\"util\")\nprint(util.ok)\n");
        const std::optional<std::vector<std::string>> kept = ALLuauExports::checkedOf("disk:/lib/keywords.luau", util);
        UNSCOPED_INFO("kept as its type says");
        REQUIRE(kept.has_value());
        UNSCOPED_INFO("the names, a word only a statement begins with among them, and no keyword");
        CHECK(*kept == std::vector<std::string>({ "ok", "export" }));
        UNSCOPED_INFO("as its text alone says");
        CHECK(ALLuauExports::of(util) == std::vector<std::string>({ "ok", "export" }));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module's lints follow the configuration of the script requiring it", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string module = "local unusedThing = 1\n"       // 0
                                   "local function helper() end\n" // 1
                                   "local n = 1\n"                 // 2
                                   "n = n + 1\n"                   // 3
                                   "local M = {}\n"                // 4
                                   "M.n = n\n"                     // 5
                                   "return M\n";                   // 6
        // Each in a script of its own, with a module of its own, checked
        // the first time.
        const auto checked = [&](const char* id, const std::string& key, const ALLuauConfig& config) {
            service.setDocument(id);
            service.setConfig(config);
            ALLuauService::Modules modules;
            modules.modules.push_back({ key, module });
            modules.reaches.push_back({ "", "util", key });
            service.setModules(modules);
            return service.check("local util = require(\"util\")\nprint(util.n)\n");
        };
        const auto told = [](const ALScriptProblems& found, const std::string& code) -> const ALScriptProblem* {
            for (const ALScriptProblem& problem : found)
            {
                if (!problem.file.empty() && problem.code == code)
                {
                    return &problem;
                }
            }
            return nullptr;
        };

        ALLuauConfig some;
        some.lints &= ~ALLuauConfig::lintBit("LocalUnused");
        some.fatalLints |= ALLuauConfig::lintBit("FunctionUnused");
        const ALScriptProblems problems = checked("some", "disk:/lib/some.luau", some);
        UNSCOPED_INFO("one it turns off, not told: " + said(problems));
        REQUIRE_FALSE(told(problems, "LocalUnused"));
        const ALScriptProblem* fatal = told(problems, "FunctionUnused");
        {
            INFO("one it makes an error, an error: " + said(problems));
            REQUIRE(fatal);
            CHECK(fatal->severity == ALScriptProblem::Severity::Error);
        }
        const ALScriptProblem* compound = told(problems, "SlCompoundAssign");
        {
            INFO("the studio's own as it says, no error: " + said(problems));
            REQUIRE(compound);
            CHECK(compound->severity != ALScriptProblem::Severity::Error);
        }

        ALLuauConfig all;
        all.lintErrors = true;
        const ALScriptProblems errors = checked("all", "disk:/lib/all.luau", all);
        const ALScriptProblem* unused = told(errors, "LocalUnused");
        {
            INFO("every one an error, Luau's: " + said(errors));
            REQUIRE(unused);
            CHECK(unused->severity == ALScriptProblem::Severity::Error);
        }
        compound = told(errors, "SlCompoundAssign");
        {
            INFO("and the studio's own: " + said(errors));
            REQUIRE(compound);
            CHECK(compound->severity == ALScriptProblem::Severity::Error);
        }
        service.setDocument("");
    }

    // The constants the grid's VM sets from builtins.txt that the
    // definitions leave out, as lsl-definitions marks them private.
    const char* const UNDOCUMENTED_CONSTANTS[] = {
        "GCNP_GET_WALKABILITY",             "LEGACY_MASS_FACTOR",
        "NAVIGATE_TO_GOAL_REACHED_DIST",    "PARCEL_FLAG_LINDEN_HOMES",
        "PRIM_MATERIAL_DENSITY",            "PRIM_MATERIAL_FRICTION",
        "PRIM_MATERIAL_GRAVITY_MULTIPLIER", "PRIM_MATERIAL_RESTITUTION",
        "PSYS_SRC_OBJ_REL_MASK",            "REZ_TORQUE",
        "SKY_ABSORPTION_CONFIG",            "SKY_DENSITY_PROFILE_COUNTS",
        "SKY_MIE_CONFIG",                   "SKY_RAYLEIGH_CONFIG",
    };

    // Whether the checker or the lint called the name an unknown global,
    // by the mode the script is in.
    static bool unknownGlobal(const ALScriptProblems& problems, const std::string& name)
    {
        for (const ALScriptProblem& p : problems)
        {
            if ((p.key == "LuauUnknownGlobal" || p.key == "LuauUnknownGlobalAssign" || p.key == "LuauLintUnknownGlobal" || p.key == "LuauLintUnknownGlobalAssign") &&
                p.args.size() == 1 && p.args[0] == name)
            {
                return true;
            }
        }
        return false;
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: LLEvents:on shows the form its event name fits and the first with no name", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string script = "LLEvents:on(\"listen\", function(channel, name, id, msg) end)\n"     // 0
                                   "LLEvents:on(\"touch_start\", function(detected) end)\n"               // 1
                                   "LLEvents:once(\"timer\", function() end)\n"                           // 2
                                   "LLEvents:on(\n";                                                      // 3
        const auto shown = [&](S32 line, S32 column) {
            const ALScriptSignature sig = service.signature(script, line, column);
            return sig.found && sig.overloads.size() == 2 ? sig.overload : -1;
        };
        const ALScriptSignature listen = service.signature(script, 0, 24);
        REQUIRE(listen.found);
        REQUIRE(listen.overloads.size() == 2);
        {
            INFO("listen, not a detected event: " + listen.label);
            CHECK(listen.overload == 1);
            REQUIRE(listen.parameters.size() == 2);
            REQUIRE(listen.parameters[1].find("Detected") == std::string::npos);
        }
        UNSCOPED_INFO("touch_start, a detected event");
        CHECK(shown(1, 29) == 0);
        UNSCOPED_INFO("once too");
        CHECK(shown(2, 25) == 1);
        UNSCOPED_INFO("in the name, as it is written");
        CHECK(shown(0, 14) == 1);
        UNSCOPED_INFO("no name yet");
        CHECK(shown(3, 12) == 0);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the grid's undocumented constants are known as numbers in either mode", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        std::string read   = "print(";
        std::string summed = "--!strict\nlocal total: number = 0";
        for (const char* name : UNDOCUMENTED_CONSTANTS)
        {
            read   += std::string(read.size() > 6 ? ", " : "") + name;
            summed += std::string(" + ") + name;
        }
        read   += ")\n";
        summed += "\nprint(total)\n";
        ALScriptProblems problems = service.check(read);
        for (const char* name : UNDOCUMENTED_CONSTANTS)
        {
            UNSCOPED_INFO(std::string("known: ") + name + "\n" + said(problems));
            REQUIRE_FALSE(unknownGlobal(problems, name));
        }
        UNSCOPED_INFO("no errors: " + said(problems));
        CHECK(alluauservice_data::errors(problems) == size_t(0));
        problems = service.check(summed);
        UNSCOPED_INFO("each a number, strictly: " + said(problems));
        CHECK(alluauservice_data::errors(problems) == size_t(0));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: TRUE and FALSE are still unknown globals", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const ALScriptProblems problems = service.check("print(PRIM_MATERIAL_DENSITY, TRUE, FALSE)\n");
        UNSCOPED_INFO("TRUE unknown: " + said(problems));
        CHECK(unknownGlobal(problems, "TRUE"));
        UNSCOPED_INFO("FALSE unknown: " + said(problems));
        CHECK(unknownGlobal(problems, "FALSE"));
        UNSCOPED_INFO("the constant beside them known: " + said(problems));
        CHECK_FALSE(unknownGlobal(problems, "PRIM_MATERIAL_DENSITY"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: completion leaves out the undocumented constants but not a script's own", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        std::string script = "print(PRIM_MATERIAL_)\n";
        std::vector<ALScriptCompletion> found = service.complete(script, 0, 20);
        UNSCOPED_INFO("a documented one offered");
        CHECK(alluauservice_data::offers(found, "PRIM_MATERIAL_FLESH"));
        UNSCOPED_INFO("an undocumented one not");
        CHECK_FALSE(alluauservice_data::offers(found, "PRIM_MATERIAL_DENSITY"));
        script = "print(SKY_)\n";
        found  = service.complete(script, 0, 10);
        {
            INFO("nor its kin");
            CHECK_FALSE(alluauservice_data::offers(found, "SKY_MIE_CONFIG"));
            CHECK_FALSE(alluauservice_data::offers(found, "SKY_RAYLEIGH_CONFIG"));
        }
        script = "local REZ_TORQUE = 1\nprint(REZ_)\n";
        found  = service.complete(script, 1, 10);
        UNSCOPED_INFO("a script's own of the name offered");
        CHECK(alluauservice_data::offers(found, "REZ_TORQUE"));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: definitions declaring an undocumented constant win and the rest stay known", "[alluauservice]")
    {
        ALLuauService own;
        std::string   why;
        own.setNewSolver(newSolver, why);
        const bool    read = own.loadDefinitions(definitions + "\ndeclare REZ_TORQUE: string\n", why);
        UNSCOPED_INFO("loads: " + why);
        CHECK(read);
        const ALScriptProblems problems = own.check("--!strict\nlocal s: string = REZ_TORQUE\nlocal n: number = SKY_MIE_CONFIG\nprint(s, n)\n");
        UNSCOPED_INFO("theirs, a string; ours, a number: " + said(problems));
        CHECK(alluauservice_data::errors(problems) == size_t(0));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: the undocumented constants survive a solver change and hover as numbers", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        std::string why;
        const bool  other = service.setNewSolver(!newSolver, why);
        UNSCOPED_INFO("the other solver: " + why);
        REQUIRE(other);
        ALScriptProblems problems = service.check("print(REZ_TORQUE)\n");
        UNSCOPED_INFO("known there: " + said(problems));
        CHECK_FALSE(unknownGlobal(problems, "REZ_TORQUE"));
        const bool back = service.setNewSolver(newSolver, why);
        UNSCOPED_INFO("and back: " + why);
        CHECK(back);
        problems = service.check("print(REZ_TORQUE)\n");
        UNSCOPED_INFO("known again: " + said(problems));
        CHECK_FALSE(unknownGlobal(problems, "REZ_TORQUE"));
        const ALScriptHover hover = service.hover("print(REZ_TORQUE)\n", 0, 8);
        REQUIRE(hover.found);
        UNSCOPED_INFO("a number: " + hover.label);
        REQUIRE(hover.label.find("number") != std::string::npos);
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: an action asked over a stretch past the line is held to the line", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        // A stretch reaching past the line's end is cut to it; one wholly
        // past it, before its start, or on a line the text has not, is
        // nothing to act on. Each answered, and nothing read past the text.
        CHECK_NOTHROW(service.actions("local x = 1\n", 0, 0, 0, 40));
        CHECK_NOTHROW(service.actions("local x = 1\n", 0, 20, 0, 40));
        CHECK_NOTHROW(service.actions("local x = 1\n", 0, -3, 0, 2));
        CHECK_NOTHROW(service.actions("local x = 1\n", 5, 0, 5, 3));
        CHECK_NOTHROW(service.actions("local x = 1", 0, 6, 0, 400));
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: a module's require no longer traced reaches nothing once the modules are set again", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("retraced");
        service.setConfig(ALLuauConfig());
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/a.luau", "--!strict\nlocal b = require(\"b\")\nreturn { n = b.n }\n" });
        modules.modules.push_back({ "disk:/lib/b.luau", "--!strict\nreturn { n = 1 }\n" });
        modules.reaches.push_back({ "", "a", "disk:/lib/a.luau" });
        modules.reaches.push_back({ "disk:/lib/a.luau", "b", "disk:/lib/b.luau" });
        service.setModules(modules);
        const std::string script  = "--!strict\nlocal a = require(\"a\")\nprint(a.n)\n";
        const auto        unknown = [](const ALScriptProblems& problems) {
            return std::any_of(problems.begin(), problems.end(), [](const ALScriptProblem& p) {
                return p.file == "disk:/lib/a.luau" && p.message.find("Unknown require") != std::string::npos;
            });
        };
        ALScriptProblems problems = service.check(script);
        UNSCOPED_INFO("traced, the module's require reaches its module: " + said(problems));
        CHECK_FALSE(unknown(problems));
        // The same modules, the module's own require no longer traced: it
        // reaches nothing, as the script's own would.
        modules.reaches.pop_back();
        service.setModules(modules);
        problems = service.check(script);
        UNSCOPED_INFO("untraced, it reaches nothing: " + said(problems));
        CHECK(unknown(problems));
        service.setModules({});
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluauservice_data, "alluauservice: what the module check hook throws is a problem the check tells, from any thread", "[alluauservice]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        service.setDocument("hooked");
        service.setConfig(ALLuauConfig());
        // Two modules: a throw inside Luau's own check of either would
        // leave that module unrecorded, or end the lane.
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/one.luau", "return { n = 1 }\n" });
        modules.modules.push_back({ "disk:/lib/two.luau", "return { n = 2 }\n" });
        modules.reaches.push_back({ "", "one", "disk:/lib/one.luau" });
        modules.reaches.push_back({ "", "two", "disk:/lib/two.luau" });
        service.setModules(modules);
        service.setModuleCheckedHook([](const std::string& module) { throw std::runtime_error("hook failed in " + module); });
        const std::string script = "local one = require(\"one\")\nlocal two = require(\"two\")\nprint(one.n + two.n)\n";
        const auto        failed = [](const ALScriptProblems& problems) {
            return std::any_of(problems.begin(), problems.end(), [](const ALScriptProblem& p) {
                return p.key == "LuauInternalError" && p.message.find("hook failed") != std::string::npos;
            });
        };
        ALScriptProblems problems = service.check(script);
        UNSCOPED_INFO("told as a problem of the script's: " + said(problems));
        CHECK(failed(problems));
        // Without the hook, the script is checked again whole, and is fine.
        service.setModuleCheckedHook(nullptr);
        problems = service.check(script);
        UNSCOPED_INFO("and checked again, clean: " + said(problems));
        CHECK_FALSE(failed(problems));
        CHECK(errors(problems) == size_t(0));
        service.setModules({});
        service.setDocument("");
    }
}
