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
#include "../alluauconfig.h"
#include "../alscriptfixes.h"
#include "../alscriptlintpass.h"
#include "../alselenefilters.h"

#include "../test/lltut.h"
#include "llsdserialize.h"

#include <cstring>
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

        // The new solver's nonstrict mode says only what is sure to fail
        // as the script runs, which a wrong argument to ll.Say is not: a
        // test of what the checker says of a type asks it in strict mode
        // there.
        void strictUnderNewSolver()
        {
            if (newSolver)
            {
                ALLuauConfig config;
                config.mode = "strict";
                service.setConfig(config);
            }
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
        strictUnderNewSolver();
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
        ensure("the unused local's lint, keyed with its name: " + said(problems), keyed_lint);
        ensure("the unknown global, keyed with its name: " + said(problems), keyed_error);
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
        llifstream in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.docs.json", std::ios::binary);
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
        hover = service.hover(script, 1, 0);  // on `ll` itself
        ensure("the table is found", hover.found);
        ensure(llformat("and said in a glance, not %d characters", (int)hover.label.size()), hover.label.size() < 1200);
        ensure("with how many more there are: " + hover.label, hover.label.find("more") != std::string::npos);
        ensure("and the whole of it apart, a field to a line", hover.typeDetail.size() > hover.label.size() && hover.typeDetail.find('\n') != std::string::npos);
    }

    template<> template<>
    void alluauservice_object::test<17>()
    {
        set_test_name("hover says what kind of name it is, a function by its signature, and what is wanted where a type is wrong");
        ensure("definitions loaded: " + error, loaded);
        const std::string script =
            "local function half(n: number): number\n"
            "    return n / 2\n"
            "end\n"
            "const limit = 3\n"
            "local p = { a = 1 }\n"
            "ll.Say(0, half(\"x\"))\n"
            "print(limit, p.a)\n";
        ALScriptHover hover = service.hover(script, 0, 16);  // half, declared
        ensure("a function by its signature: " + hover.label, hover.label.rfind("function half(n: number): number", 0) == 0);
        hover = service.hover(script, 0, 20);  // n, the parameter
        ensure("a parameter: " + hover.label, hover.label == "(parameter) n: number");
        hover = service.hover(script, 1, 11);  // n, used
        ensure("still a parameter where used: " + hover.label, hover.label == "(parameter) n: number");
        hover = service.hover(script, 3, 7);  // limit
        ensure("a const: " + hover.label, hover.label == "const limit: number");
        hover = service.hover(script, 4, 6);  // p
        ensure("a local: " + hover.label, hover.label.rfind("local p: ", 0) == 0);
        hover = service.hover(script, 6, 15);  // p.a
        ensure("a field: " + hover.label, hover.label == "(field) p.a: number");
        hover = service.hover(script, 5, 16);  // "x" where a number is wanted
        ensure("what is wanted there: " + hover.expected, hover.expected == "number");
        hover = service.hover(script, 6, 0);  // print
        // The new solver gives print its generic pack: print<T...>.
        ensure("a builtin by its signature: " + hover.label, hover.label.rfind(newSolver ? "function print<" : "function print(", 0) == 0);
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

    template<> template<>
    void alluauservice_object::test<14>()
    {
        set_test_name("a key that is not there is named by what was written, with the nearest key there is");
        ensure("definitions loaded: " + error, loaded);
        strictUnderNewSolver();
        ALScriptProblems problems = service.check("ll.ay(0, \"hi\")\n");
        ensure("one problem", !problems.empty());
        const std::string& message = problems.front().message;
        ensure("names ll, not its fields: " + message, message.find("not found in ll") != std::string::npos && message.find("Abs") == std::string::npos);
        ensure("and suggests Say: " + message, message.find("'Say'") != std::string::npos);
        ensure(llformat("in a glance, not %d characters", (int)message.size()), message.size() < 100);
    }

    template<> template<>
    void alluauservice_object::test<15>()
    {
        set_test_name("every name is told by what it is: parameter, local, global, field, function, type, builtin, deprecated");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("count declared, a variable:" + listed, count_decl && count_decl->kind == ALScriptSymbolKind::Variable && (count_decl->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* half_decl = at(1, 15);
        ensure("half declared, a function:" + listed, half_decl && half_decl->kind == ALScriptSymbolKind::Function && (half_decl->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* n_param = at(1, 20);
        ensure("n, a parameter:" + listed, n_param && n_param->kind == ALScriptSymbolKind::Parameter);
        const ALScriptSemanticToken* n_type = at(1, 23);
        ensure("number, a type:" + listed, n_type && n_type->kind == ALScriptSymbolKind::Type);
        const ALScriptSemanticToken* n_use = at(2, 11);
        ensure("n used, still a parameter:" + listed, n_use && n_use->kind == ALScriptSymbolKind::Parameter && !(n_use->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* pair = at(4, 5);
        ensure("Pair declared, a type:" + listed, pair && pair->kind == ALScriptSymbolKind::Type && (pair->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* a_key = at(5, 18);
        ensure("a in the table, a field declared:" + listed, a_key && a_key->kind == ALScriptSymbolKind::Field && (a_key->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* ll = at(6, 0);
        ensure("ll, a builtin global:" + listed, ll && (ll->modifiers & ALScriptSemanticToken::Builtin) && (ll->modifiers & ALScriptSemanticToken::Global));
        const ALScriptSemanticToken* say = at(6, 3);
        ensure("Say, a builtin function:" + listed, say && say->kind == ALScriptSymbolKind::Function && (say->modifiers & ALScriptSemanticToken::Builtin));
        const ALScriptSemanticToken* p_a = at(6, 21);
        ensure("p.a, a field:" + listed, p_a && p_a->kind == ALScriptSymbolKind::Field);
        const ALScriptSemanticToken* half_call = at(6, 25);
        ensure("half called, a function:" + listed, half_call && half_call->kind == ALScriptSymbolKind::Function);
        const ALScriptSemanticToken* handlers = at(7, 0);
        ensure("handlers, a global the script binds:" + listed, handlers && (handlers->modifiers & ALScriptSemanticToken::Global) && !(handlers->modifiers & ALScriptSemanticToken::Builtin));
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            ensure("in order, each once", tokens[i - 1].span < tokens[i].span);
        }
    }

    template<> template<>
    void alluauservice_object::test<16>()
    {
        set_test_name("inlay hints name each argument's parameter and the type a local was given without saying");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("the local's type after its name:" + listed, has(1, 11, ": number"));
        for (const ALScriptInlayHint& h : hints)
        {
            ensure("a type may be written in, a parameter's name never: " + h.text, h.writable == (h.kind == ALScriptInlayHint::Kind::Type));
        }
        // A type cut short says less than the text would have to.
        const std::vector<ALScriptInlayHint> wide = service.inlayHints("local t = { a = 1, b = 2, c = 3, d = 4, e = 5 }\n", false, true);
        ensure("not a table cut short", wide.empty() || !wide.front().writable || wide.front().text.find("...") == std::string::npos);
        ensure("not one that says its type:" + listed, !has(2, 11, ": number"));
        ensure("name: before the string:" + listed, has(3, 6, "name:"));
        ensure("nothing before an argument that is the name:" + listed, !has(3, 12, "times:"));
        ensure("the channel's name before the 0:" + listed, has(4, 7, "Channel:") || has(4, 7, "channel:"));
        ensure("and the text's:" + listed, has(4, 10, "Text:") || has(4, 10, "text:"));
        std::vector<ALScriptInlayHint> only_types = service.inlayHints(script, false, true);
        ensure("types alone, when asked", only_types.size() == 1 && only_types.front().kind == ALScriptInlayHint::Kind::Type);
        ensure("nothing when neither is asked", service.inlayHints(script, false, false).empty());
    }
    template<> template<>
    void alluauservice_object::test<18>()
    {
        set_test_name("the configuration names the globals a script may use and turns lints off, until it is replaced");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "local unused = 1\nAlchemy.log(\"hi\")\n";
        ALScriptProblems  plain  = service.check(script);
        ensure("Alchemy unknown without a configuration: " + said(plain), mentions(plain, "Alchemy"));
        ensure("the unused local warned of: " + said(plain), mentions(plain, "unused"));
        ALLuauConfig config;
        std::string  bad;
        ensure("parses: " + bad, ALLuauConfig::parse("{ \"globals\": [\"Alchemy\"], \"lint\": { \"LocalUnused\": false } }", config, bad));
        service.setConfig(config);
        ALScriptProblems configured = service.check(script);
        ensure("Alchemy known: " + said(configured), !mentions(configured, "Alchemy"));
        ensure("the unused local let be: " + said(configured), !mentions(configured, "unused"));
        service.setConfig(ALLuauConfig());
        ALScriptProblems again = service.check(script);
        ensure("unknown again: " + said(again), mentions(again, "Alchemy"));
    }

    template<> template<>
    void alluauservice_object::test<19>()
    {
        set_test_name("the engine's commonest messages come back keyed, which an upgrade that rewords them would end");
        ensure("definitions loaded: " + error, loaded);
        strictUnderNewSolver();
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
            ensure(std::string("keyed ") + c.key + " for " + c.script + ": " + said(problems), keyed);
        }
    }
    template<> template<>
    void alluauservice_object::test<20>()
    {
        set_test_name("a question asked again of the same text is answered from the check made, and what goes beside the text is the same whatever came first");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("the queries share the one check", service.typeChecks(), checked);
        service.setConfig(config);
        service.hover(script, 1, 6);
        ensure_equals("the same configuration again changes nothing", service.typeChecks(), checked);
        // A text asked about before a check has what a hover does.
        const std::vector<ALScriptInlayHint> after_hover = service.inlayHints(script, true, true);
        ensure_equals("as many hints", after_hover.size(), after_check.size());
        for (size_t i = 0; i < after_hover.size(); ++i)
        {
            ensure("the same hint: " + after_hover[i].text + " / " + after_check[i].text,
                   after_hover[i].text == after_check[i].text && after_hover[i].line == after_check[i].line);
        }
        // Another configuration, or another text, is checked again.
        ALLuauConfig strict;
        strict.mode = "strict";
        service.setConfig(strict);
        service.hover(script, 1, 6);
        ensure("another configuration, checked again", service.typeChecks() > checked);
        const size_t now = service.typeChecks();
        service.hover(script + "print(y)\n", 1, 6);
        ensure("another text, checked again", service.typeChecks() > now);
        service.setConfig(config);
    }

    template<> template<>
    void alluauservice_object::test<21>()
    {
        set_test_name("a method called with a dot is given its object as its first argument, and the signature counts it");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "local T = {}\nfunction T:m(a: number, b: string) end\nT.m(T, 1, \"x\")\nT:m(1, \"x\")\n";
        ALScriptSignature dotted = service.signature(script, 2, 12);  // in "x"
        ensure("found", dotted.found);
        ensure_equals("self, a and b: " + dotted.label, dotted.parameters.size(), size_t(3));
        ensure_equals("at the third", dotted.active, 2);
        ensure("which is b: " + dotted.parameters[2], dotted.parameters[2].find("b") == 0);
        ALScriptSignature colon = service.signature(script, 3, 9);  // in "x"
        ensure_equals("with a colon, a and b", colon.parameters.size(), size_t(2));
        ensure("at b: " + colon.parameters[static_cast<size_t>(std::max(0, colon.active))], colon.active == 1 && colon.parameters[1].find("b") == 0);
    }

    template<> template<>
    void alluauservice_object::test<22>()
    {
        set_test_name("either solver checks with the definitions, the new one in nonstrict mode saying only what is sure to fail as the script runs");
        ensure("definitions loaded: " + error, loaded);
        for (const bool use : { true, false, true })
        {
            std::string why;
            ensure("switched: " + why, service.setNewSolver(use, why));
            ensure_equals("the one asked for", service.newSolver(), use);
            ensure("the definitions with it", service.hasDefinitions());
            ALLuauConfig config;
            config.mode = "strict";
            service.setConfig(config);
            ALScriptProblems problems = service.check("ll.Say(\"zero\", 0)\n");
            ensure(std::string(use ? "new" : "old") + ", strict, a wrong argument: " + said(problems), errors(problems) > 0);
            // A method called with a dot is a missing self, whichever says it.
            problems      = service.check("local T = {}\nfunction T:m(a: number) end\nT.m(1)\n");
            bool missing_self = false;
            for (const ALScriptProblem& problem : problems)
            {
                missing_self |= problem.key == "LuauRequiresSelf" && problem.line == 2 && problem.column == 0;
            }
            ensure(std::string(use ? "new" : "old") + ": a missing self, at the call: " + said(problems), missing_self);
        }
        // Nonstrict, as the grid compiles: the new solver says nothing of
        // ll.Say's argument, and what it does say comes keyed.
        ALLuauConfig config;
        config.mode = "nonstrict";
        service.setConfig(config);
        ensure("nothing of ll.Say: " + said(service.check("ll.Say(\"zero\", 0)\n")), errors(service.check("ll.Say(\"zero\", 0)\n")) == 0);
        const ALScriptProblems problems = service.check("print(string.len(5))\n");
        ensure("a checked function given the wrong type: " + said(problems), !problems.empty() && problems.front().key == "LuauCheckedCall");
        std::string why;
        service.setNewSolver(false, why);
    }

    template<> template<>
    void alluauservice_object::test<23>()
    {
        set_test_name("a check past its time limit stops, says so, and answers what it found by then; a stopped one answers nothing");
        ensure("definitions loaded: " + error, loaded);
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
            ensure(which + "said to have stopped: " + said(problems), !problems.empty() && problems.front().key == "LuauCheckTimedOut" &&
                                                                        problems.front().severity == ALScriptProblem::Severity::Warning);
            service.setTimeLimit(0.0);
            problems = service.check(script);
            ensure(which + "unlimited, it runs its course: " + said(problems), problems.empty());
            // Stopped before it began: nothing, and said to be stopped.
            ALLuauService::Stop stop = ALLuauService::newStop();
            ALLuauService::cancel(stop);
            service.setStop(stop);
            problems = service.check("local n: number = \"s\"\n");
            ensure(which + "stopped, nothing: " + said(problems), problems.empty() && service.stopped());
            // And a question after it is answered from a check of its own,
            // not the part of one the stop left.
            service.setStop(nullptr);
            const ALScriptHover hover = service.hover("local n: number = \"s\"\n", 0, 6);
            ensure(which + "not stopped now", !service.stopped());
            ensure(which + "answered: " + hover.label, hover.label.find("n: number") != std::string::npos);
            problems = service.check("local n: number = \"s\"\n");
            ensure(which + "and checked in full: " + said(problems), errors(problems) > 0 || use);
        }
        std::string why;
        service.setNewSolver(newSolver, why);
    }

    template<> template<>
    void alluauservice_object::test<24>()
    {
        set_test_name("selene's comments: its lints named as Luau's, allowed or denied for the whole file before any code, or for the statement beside");
        ensure("definitions load: " + error, loaded);
        const auto read = ALSeleneFilters::read("-- # selene: allow(unused_variable, multiple_statements))");
        ensure("read leniently, for the whole file", read && read->file && read->action == ALSeleneFilters::Action::Allow && read->lints.size() == 2);
        ensure("a Luau name stands for itself, selene's for Luau's",
               ALSeleneFilters::luauLints("LocalUnused") == ALLuauConfig::lintBit("LocalUnused") &&
                   (ALSeleneFilters::luauLints("unused_variable") & ALLuauConfig::lintBit("FunctionUnused")) != 0);
        ensure("a check Luau does not make stands for none", ALSeleneFilters::luauLints("almost_swapped") == 0);
        ensure("not a directive", !ALSeleneFilters::read("-- seleneous: allow(x)") && !ALSeleneFilters::read("-- selene: permit(x)"));

        const auto unused = [&](const std::string& script) {
            size_t count = 0;
            for (const ALScriptProblem& p : service.check(script))
            {
                count += p.key.compare(0, 19, "LuauLintLocalUnused") == 0 ? 1 : 0;
            }
            return count;
        };
        ensure_equals("without, both said", unused("local a = 1\nlocal b = 2\n"), size_t(2));
        ensure_equals("the whole file", unused("-- @file header\n-- # selene: allow(unused_variable)\nlocal a = 1\nlocal b = 2\n"), size_t(0));
        ensure_equals("after code, nothing: selene refuses a whole file's there", unused("local a = 1\n--# selene: allow(unused_variable)\nlocal b = 2\n"), size_t(2));
        ensure_equals("the statement beside, not the next", unused("-- selene: allow(unused_variable)\nlocal a = 1\nlocal b = 2\n"), size_t(1));
        ensure_equals("a whole function beside it",
                      unused("-- selene: allow(unused_variable)\nlocal function f()\n    local a = 1\nend\nf()\nlocal b = 2\n"), size_t(1));
        ensure_equals("at a line's end, its statement", unused("local a = 1 -- selene: allow(unused_variable)\nlocal b = 2\n"), size_t(1));
        bool denied = false;
        for (const ALScriptProblem& p : service.check("-- selene: deny(unused_variable)\nlocal a = 1\n"))
        {
            denied |= p.key.compare(0, 19, "LuauLintLocalUnused") == 0 && p.severity == ALScriptProblem::Severity::Error;
        }
        ensure("denied: an error", denied);
    }

    template<> template<>
    void alluauservice_object::test<25>()
    {
        set_test_name("an overloaded function's signature: every form, the one the arguments fit first shown");
        ensure("definitions load: " + error, loaded);
        const std::string       script = "local f: ((n: number) -> ()) & ((a: string, b: string) -> ()) = nil :: any\nf(\"x\", \"y\")\n";
        const ALScriptSignature sig    = service.signature(script, 1, 7);
        ensure("found", sig.found);
        ensure_equals("both forms", sig.overloads.size(), size_t(2));
        ensure_equals("the second fits two arguments", sig.overload, 1);
        ensure("shown", sig.label == sig.overloads[1].label && sig.parameters.size() == 2);
        ensure_equals("at the second argument", sig.active, 1);
        ensure("one form: none listed", service.signature("local function g(a: number) end\ng(1)\n", 1, 2).overloads.empty());
    }

    template<> template<>
    void alluauservice_object::test<26>()
    {
        set_test_name("a text is type checked once for its check and once for the questions, in whatever order they come; once for both where it is strict or the solver is the new one");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("nonstrict: the check, and the questions' own", service.typeChecks() - start, size_t(one_slot ? 1 : 2));
        ensure_equals("the check again, from what it found", said(service.check(loose)), first);
        ensure_equals("and not checked again", service.typeChecks() - start, size_t(one_slot ? 1 : 2));

        // A script strict by its own comment: its check serves the questions.
        const std::string strict   = "--!strict\n" + loose;
        const size_t      before   = service.typeChecks();
        service.hover(strict, 2, 6);
        service.check(strict);
        service.outline(strict);
        service.inlayHints(strict, true, true);
        ensure_equals("strict: one check, the question first", service.typeChecks() - before, size_t(1));
        service.complete(strict, 3, 2);
        ensure_equals("a completion reads autocomplete's own", service.typeChecks() - before, size_t(one_slot ? 1 : 2));
        const std::string strict_first = said(service.check(strict));
        service.hover(strict, 2, 6);
        ensure_equals("and nothing checked again", service.typeChecks() - before, size_t(one_slot ? 1 : 2));
        // The hints the questions read are the strict ones either way.
        // The types a question reads are the strict ones, whatever the
        // script's mode: a nonstrict check's module knows none of them.
        const std::string typed       = "local s = string.rep(\"a\", 2)\nlocal n = #s\nprint(n)\n";
        const auto        loose_hints = service.inlayHints(typed, false, true);
        const auto        strict_hints = service.inlayHints("--!strict\n" + typed, false, true);
        ensure_equals("hints read nonstrict", loose_hints.size(), size_t(2));
        ensure_equals("and strict", strict_hints.size(), size_t(2));
        ensure("the same", loose_hints[0].text == strict_hints[0].text && loose_hints[1].text == strict_hints[1].text);
        ensure("the problems of each told apart", said(service.check(loose)) == first && said(service.check(strict)) == strict_first);
    }

    template<> template<>
    void alluauservice_object::test<27>()
    {
        set_test_name("the same definitions and docs again are the ones in hand: nothing loaded over, and what was checked stands");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "local n: number = 1\nprint(n)\n";
        service.check(script);
        const size_t checked = service.typeChecks();
        std::string  why;
        ensure("the same again: loaded", service.loadDefinitions(definitions, why) && why.empty());
        service.check(script);
        ensure_equals("what was checked stands", service.typeChecks(), checked);
        const std::string docs = "{\"@sl-slua/global/ll.Say\": {\"documentation\": \"Says it.\", \"learn_more_link\": \"x\"}}";
        ensure("docs loaded", service.loadDocs(docs, why) && service.hasDocs());
        ensure("and the same again", service.loadDocs(docs, why) && service.hasDocs());
        // Other definitions are loaded over, and the script checked again.
        ensure("others loaded", service.loadDefinitions(definitions + "\ndeclare function extraThing(): number\n", why));
        service.check(script);
        ensure("checked again against them", service.typeChecks() > checked);
        service.loadDefinitions(definitions, why);
    }

    template<> template<>
    void alluauservice_object::test<28>()
    {
        set_test_name("a local's type is found however deep its scope, and the refactor at a line reads that line's hint as the whole text's has it");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("every local's, at every depth", all, std::string("0: number;2: string;4: number;"));
        for (const auto& [line, column, type] : { std::tuple{ 0, 6, "number" }, std::tuple{ 2, 10, "string" }, std::tuple{ 4, 14, "number" } })
        {
            bool offered = false;
            for (const ALScriptFix& fix : service.actions(script, line, column, line, column))
            {
                offered |= fix.title == std::string("Declare it as '") + type + "'";
            }
            ensure("line " + std::to_string(line) + ": declared as " + type, offered);
        }
    }

    template<> template<>
    void alluauservice_object::test<29>()
    {
        set_test_name("each script its own module: moving between a few finds each checked as it was left, each with its own configuration");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("turn and turn about, each as it was left", service.typeChecks(), both);
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
        ensure_equals("one of the last few kept", service.typeChecks(), now);
        service.setDocument("a");
        service.setConfig(strict);
        service.check(a);
        ensure_equals("the one asked of longest ago let go of", service.typeChecks(), now + 1);
        // The module of no name is apart from them all.
        service.setDocument("");
        service.setConfig(loose);
        service.check(a);
        ensure_equals("unnamed: its own", service.typeChecks(), now + 2);
    }

    template<> template<>
    void alluauservice_object::test<30>()
    {
        set_test_name("lines nobody reads -- a module put ahead of the script -- are passed over by the names and the hints");
        ensure("definitions loaded: " + error, loaded);
        const std::string text = "local __modules = {}\n"
                                 "__modules[\"m\"] = (function()\nlocal inner = 1\nreturn inner\nend)()\n"
                                 "local own = __modules[\"m\"]\nprint(own)\n";
        service.setPassedOver({ { 1, 4 } });
        bool in_module = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_module |= token.span.line >= 1 && token.span.line <= 4;
        }
        ensure("no names coloured in the module", !in_module);
        bool hinted_module = false, hinted_own = false;
        for (const ALScriptInlayHint& hint : service.inlayHints(text, false, true))
        {
            hinted_module |= hint.line >= 1 && hint.line <= 4;
            hinted_own |= hint.line == 5;
        }
        ensure("no hints in the module", !hinted_module);
        ensure("the script's own hinted", hinted_own);
        service.setPassedOver({});
        in_module = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_module |= token.span.line >= 1 && token.span.line <= 4;
        }
        ensure("with nothing passed over, coloured", in_module);
    }

    template<> template<>
    void alluauservice_object::test<31>()
    {
        set_test_name("a script's requires kept as calls reach its modules, each checked on its own and not again for an edit of the script alone");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("the module's types reach the script: " + said(problems), typed);
        ensure("the module's own problem is the module's, in its lines: " + said(problems), module_said);
        // The script typed in: only the script is checked again.
        const size_t modules_before = service.modulesChecked();
        service.check(script + "print(s)\n");
        ensure_equals("the script alone", service.modulesChecked() - modules_before, size_t(1));
        // The module changed: it, and the script that requires it.
        modules.modules[0].text = "local M = {}\nfunction M.twice(n: number): string\n    return tostring(n * 2)\nend\nreturn M\n";
        service.setModules(modules);
        const size_t again = service.modulesChecked();
        const ALScriptProblems now = service.check(script + "print(s)\n");
        ensure_equals("both", service.modulesChecked() - again, size_t(2));
        bool still = false;
        for (const ALScriptProblem& problem : now)
        {
            still |= problem.line == 2 && problem.file.empty() && problem.severity == ALScriptProblem::Severity::Error;
        }
        ensure("and what it gives now fits: " + said(now), !still);
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<32>()
    {
        set_test_name("the outline lists a handler put on an event as the event, a timer's as a timer, each with what is inside it");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("five entries:" + names, outline.size(), size_t(5));
        ensure("the touch as an event", outline[0].name == "touch_start" && outline[0].kind == ALScriptSymbolKind::Event && outline[0].depth == 0);
        ensure("named where its event is", outline[0].nameSpan.line == 0 && outline[0].nameSpan.column == 12);
        ensure("spanning the call", outline[0].span.line == 0 && outline[0].span.endLine == 3);
        ensure("what it holds, one deeper", outline[1].name == "greet" && outline[1].depth == 1);
        ensure("once, too", outline[2].name == "listen" && outline[2].kind == ALScriptSymbolKind::Event);
        ensure_equals("a timer, how often", outline[3].name, std::string("timer every 2.5"));
        ensure_equals("a timer whose delay the script works out", outline[4].name, std::string("timer once"));
    }

    template<> template<>
    void alluauservice_object::test<33>()
    {
        set_test_name("every template Script Studio offers for a new SLua script checks without an error");
        ensure("definitions loaded: " + error, loaded);
        llifstream in(std::string(AL_SCRIPT_TEMPLATES_DIR) + "/slua.xml", std::ios::in | std::ios::binary);
        LLSD       templates;
        ensure("read", in.is_open() && LLSDSerialize::fromXML(templates, in) != LLSDParser::PARSE_FAILURE && templates.isArray());
        ensure("some", templates.size() > 0);
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
            ensure(name + ":" + wrong, wrong.empty());
        }
    }

    template<> template<>
    void alluauservice_object::test<34>()
    {
        set_test_name("the studio's own lints beside Luau's: found, off or an error as configured, turned off by --!nolint and selene's comments, and a NOLINT answers to its name");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("two: x and t.n", said.size(), size_t(2));
        ensure("the first, a note in its words: " + said[0].message,
               said[0].key == "LuauLintSlCompoundAssign" && said[0].severity == ALScriptProblem::Severity::Note && said[0].line == 1 &&
                   said[0].args == std::vector<std::string>{ "x", "+" } && said[0].source == ALScriptProblem::Source::Lint);
        ensure("a field, joined: " + said[1].message, said[1].line == 3 && said[1].args == std::vector<std::string>{ "t.n", ".." });
        ensure("a NOLINT answers to its name", ALScriptFixes::suppressed(said[0], "x = x + 1 -- NOLINT(SlCompoundAssign)", "", true) &&
                                                   !ALScriptFixes::suppressed(said[0], "x = x + 1 -- NOLINT(LocalUnused)", "", true));

        ALLuauConfig config;
        config.slLints = 0;
        service.setConfig(config);
        ensure("off", found(script).empty());
        config.slLints      = ALScriptLintPass::defaults();
        config.slFatalLints = ALScriptLintPass::bit("SlCompoundAssign");
        service.setConfig(config);
        said = found(script);
        ensure("an error", said.size() == 2 && said[0].severity == ALScriptProblem::Severity::Error);
        config.slFatalLints = 0;
        config.lintErrors   = true;
        service.setConfig(config);
        said = found(script);
        ensure("every lint an error", said.size() == 2 && said[0].severity == ALScriptProblem::Severity::Error);
        service.setConfig(ALLuauConfig());

        said = found("--!nolint SlCompoundAssign\n" + script);
        ensure("--!nolint by its name, and no word that Luau does not know it", said.empty());
        ensure("--!nolint alone", found("--!nolint\n" + script).empty());
        said = found("--!nolint SlNope\n" + script);
        ensure("a name that is none is still said", said.size() == 3 && said[0].key.rfind("LuauLintDirective", 0) == 0);

        said = found("local x = 1\nx = x + 1 -- selene: allow(SlCompoundAssign)\nprint(x)\n");
        ensure("selene's comment beside it", said.empty());
        said = found("--# selene: deny(SlCompoundAssign)\nlocal x = 1\nx = x + 1\nprint(x)\n");
        ensure("selene's for the whole file, an error", said.size() == 1 && said[0].severity == ALScriptProblem::Severity::Error);
        ensure("selene's words for the studio's own", ALSeleneFilters::slLints("SlCompoundAssign") == ALScriptLintPass::bit("SlCompoundAssign") &&
                                                          ALSeleneFilters::slLints("unused_variable") == 0);
    }
}
