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

#include "../test/lltut.h"
#include "llsdserialize.h"

#include <algorithm>
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
        ensure("the channel's name before the 0:" + listed, has(4, 7, "channel:"));
        ensure("and the message's:" + listed, has(4, 10, "msg:"));
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
        // Another configuration, or another text, is checked again: one
        // unlike either solver's default.
        ALLuauConfig strict;
        strict.mode = "nocheck";
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
        // Nonstrict, as the grid compiles: the new solver checking strict
        // cannot yet push an overloaded function's parameter types into a
        // function given to it, so every LLEvents:on there is an error it
        // is upstream's to put right.
        ALLuauConfig grid;
        grid.mode = "nonstrict";
        service.setConfig(grid);
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

    template<> template<>
    void alluauservice_object::test<35>()
    {
        set_test_name("SlNumberTruth: a number read as a condition, through and, or and brackets, and under not; not number?, nor a comparison");
        ensure("definitions loaded: " + error, loaded);
        const auto found = [&](const std::string& text) {
            std::vector<std::string> out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlNumberTruth")
                {
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
        ensure_equals("four: " + llformat("%zu", said.size()), said.size(), size_t(4));
        ensure("each: " + said[0] + "|" + said[1] + "|" + said[2] + "|" + said[3],
               said[0] == "LuauLintSlNumberTruth n 1" && said[1] == "LuauLintSlNumberTruth n 2" && said[2] == "LuauLintSlNumberTruthNot n 3" &&
                   said[3] == "LuauLintSlNumberTruth n 4");
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
        // The new solver types j a number still, and is taken at its word.
        ensure("a copy only: " + (given.empty() ? std::string() : given[0]),
               given.size() == (newSolver ? 2u : 1u) && given[0] == "LuauLintSlNumberTruth copy 7");
    }

    template<> template<>
    void alluauservice_object::test<36>()
    {
        set_test_name("a configuration that says no mode is checked in the solver's own: the new one's strict, as strict as the old one's nonstrict; one that says nonstrict is nonstrict under either");
        ensure("definitions loaded: " + error, loaded);
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
            ensure("strict where nothing says", by_default);
            ensure("nonstrict where asked", !asked_nonstrict);
        }
        else
        {
            ensure_equals("nonstrict, whether asked or not", by_default, asked_nonstrict);
        }
        service.setConfig(ALLuauConfig());
    }

    template<> template<>
    void alluauservice_object::test<37>()
    {
        set_test_name("SlNilSentinel: a find against -1, or ordered against 0 or -1, on either side, a local given one, table's and string's; llcompat's against nil; not what can hold");
        ensure("definitions loaded: " + error, loaded);
        const auto found = [&](const std::string& text) {
            std::vector<std::string> out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlNilSentinel")
                {
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
        ensure("each, an error:\n" + listed, said == expected);
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
        ensure("none: " + (none.empty() ? std::string() : none[0]), none.empty());
    }

    template<> template<>
    void alluauservice_object::test<38>()
    {
        set_test_name("a shape is the outline from a parse alone: the same symbols and spans, no types; a text broken in places as far as it parses; the service as it was");
        ensure("definitions loaded: " + error, loaded);
        const std::string script =
            "local count = 1\n"
            "local function half(n: number)\n"
            "    local function quarter() return n / 4 end\n"
            "    return n / 2\n"
            "end\n"
            "function LLEvents.touch_start(n: number) end\n";
        const std::vector<ALScriptOutlineEntry> outline = service.outline(script);
        const std::vector<ALScriptOutlineEntry> shape   = ALLuauService::shape(script);
        ensure_equals("as many", shape.size(), outline.size());
        for (size_t i = 0; i < shape.size(); ++i)
        {
            ensure("the same symbol, where it was", shape[i].name == outline[i].name && shape[i].kind == outline[i].kind && shape[i].span == outline[i].span &&
                                                         shape[i].depth == outline[i].depth);
            ensure("no type", shape[i].detail.empty());
        }
        ensure("the outline still typed", service.outline(script)[0].detail == "number");
        const std::vector<ALScriptOutlineEntry> broken = ALLuauService::shape("local function a()\n  x = \nend\nlocal function b()\nend\n");
        ensure("broken in a: both still there", broken.size() == 2 && broken[0].name == "a" && broken[1].name == "b" && broken[1].span.line == 3);
    }

    template<> template<>
    void alluauservice_object::test<39>()
    {
        set_test_name("completion says what fits where it goes and where a call's brackets go, and offers a method only after a colon");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("both offered", count && name);
        ensure("the number fits", count->fits);
        ensure("the string does not", !name->fits);
        ensure("said where an expression goes", count->context == ALScriptCompletion::Context::Expression);

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
        ensure("print, between its brackets", say && say->brackets == ALScriptCompletion::Brackets::Inside);
        ensure("now, after an empty pair", now && now->brackets == ALScriptCompletion::Brackets::After);
        ensure("ll, no call to say anything of", lib && lib->brackets == ALScriptCompletion::Brackets::Guess);
        found                           = service.complete(calls, 3, 30);
        const ALScriptCompletion* twice = entry(found, "twice");
        ensure("twice, wanted as it is", twice && twice->fits);
        ensure("so no brackets", twice->brackets == ALScriptCompletion::Brackets::None);

        // A method is called with a colon, and after a dot is no use. The
        // grid's own declare theirs as fields taking self, which are called
        // with a colon all the same.
        const std::string events = "local a = LLEvents.\nlocal b = LLEvents:\nlocal t = {}\nfunction t:greet(): number return 1 end\nlocal d = t:\n";
        found                    = service.complete(events, 0, 19);
        ensure("none of LLEvents' methods after a dot", !entry(found, "on") && !entry(found, "off") && !entry(found, "handlers"));
        found = service.complete(events, 1, 19);
        ensure("all of them after a colon", entry(found, "on") && entry(found, "off") && entry(found, "handlers"));
        ensure("a script's own method after a colon", entry(service.complete(events, 4, 12), "greet") != nullptr);
    }

    template<> template<>
    void alluauservice_object::test<40>()
    {
        set_test_name("a function an argument wants is offered written out, to fill in: Luau's own stub, and an overloaded callee's from the overload chosen");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("Luau's stub offered", own != nullptr);
        ensure_equals("its head", own->text, std::string("function(i: number)"));
        ensure_equals("its body to fill, then its end", own->snippet, std::string("function(i: number)\n    $0\nend"));
        ensure("of the type wanted", own->fits);

        const std::string                     events    = "LLEvents:on(\"touch_start\", f)\n";
        const std::vector<ALScriptCompletion> for_event = service.complete(events, 0, 27);
        const ALScriptCompletion*             handler   = stub(for_event);
        ensure("the event's handler offered", handler != nullptr);
        ensure_equals("taking what the event gives", handler->text, std::string("function(detected: {DetectedEvent})"));
        ensure("a body to fill: " + handler->snippet, handler->snippet.find("\n    $0\nend") != std::string::npos);
    }

    template<> template<>
    void alluauservice_object::test<41>()
    {
        set_test_name("after --! the comments that say how a script is checked are offered, those the studio offers alone");
        ensure("definitions loaded: " + error, loaded);
        const std::vector<ALScriptCompletion> found = service.complete("--!st\nlocal x = 1\n", 0, 3);
        std::string                           said;
        for (const ALScriptCompletion& c : found)
        {
            said += " " + c.text;
            ensure("a word of the language: " + c.text, c.kind == ALScriptSymbolKind::Keyword);
            ensure("in a hot comment: " + c.text, c.context == ALScriptCompletion::Context::HotComment);
            ensure("one the studio offers: " + c.text, ALScriptLexicon::isLuauHotComment(c.text));
        }
        ensure_equals("each of them:" + said, found.size(), std::size(ALScriptLexicon::LUAU_HOT_COMMENTS));
    }

    template<> template<>
    void alluauservice_object::test<42>()
    {
        set_test_name("as a script is typed, a completion and signature help are answered over the statement typed, and nothing is checked whole");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("docs loaded", service.loadDocs("{\"@sl-slua/global/ll.Say\": {\"documentation\": \"Says it.\", \"learn_more_link\": \"x\"},"
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
        ensure("off: checked whole", service.typeChecks() > checks);
        ensure_equals("a call of a field documented as the field, not what holds it", service.signature(base + "ll.Say(0, )\n", 3, 10).documentation,
                      std::string("Says it."));
        // `vector` is a table that is also called, whose fields Luau gives
        // no symbol of their own.
        ensure_equals("and of a callable table's field", service.signature(base + "local m = vector.magnitude()\n", 3, 27).documentation,
                      std::string("Its length."));

        service.setFragments(true);
        ensure("on", service.fragments());
        service.check(base);
        service.complete(base, 0, 0);
        checks                                = service.typeChecks();
        const size_t                    parts = service.fragmentsChecked();
        std::vector<ALScriptCompletion> found = service.complete(base + "local n: number = co\n", 3, 18);
        const ALScriptCompletion*       count = offered(found, "count");
        ensure("a local above offered", count != nullptr);
        ensure("and fitting where a number is wanted", count->fits);
        ensure("a function in scope offered", offered(found, "twice") != nullptr);
        ensure("a table's fields", offered(service.complete(base + "local y = t.\n", 3, 12), "b") != nullptr);
        found = service.complete(base + "LLEvents:\n", 3, 9);
        ensure("an object's methods", offered(found, "on") && offered(found, "off"));

        ALScriptSignature said = service.signature(base + "ll.Say(0, )\n", 3, 10);
        ensure("a call's signature", said.found);
        ensure_equals("its label", said.label, std::string("ll.Say(channel: number, msg: string): ()"));
        ensure_equals("at its second argument", said.active, 1);
        ensure_equals("documented as the field it calls", said.documentation, std::string("Says it."));
        said = service.signature(base + "if twice() then end\n", 3, 9);
        ensure("a call in a condition", said.found && said.label.find("twice(n: number)") == 0);

        ensure("nothing offered in a comment", service.complete(base + "-- tw\n", 3, 5).empty());
        ensure("no signature outside a call", !service.signature(base + "local q = 1\n", 3, 8).found);
        ensure_equals("a callable table's field over the fragment too", service.signature(base + "local m = vector.magnitude()\n", 3, 27).documentation,
                      std::string("Its length."));
        ensure_equals("none of it checked whole", service.typeChecks(), checks);
        ensure_equals("each answered over its statement", service.fragmentsChecked() - parts, size_t(6));
        service.setFragments(false);
    }

    template<> template<>
    void alluauservice_object::test<43>()
    {
        set_test_name("where a fragment cannot answer the whole script does; one stopped answers nothing; and the script's requires survive one");
        ensure("definitions loaded: " + error, loaded);
        service.setFragments(true);
        // Nothing checked yet to patch: the whole script.
        service.setDocument("unchecked");
        size_t checks = service.typeChecks();
        size_t parts  = service.fragmentsChecked();
        const std::string base = "local count: number = 1\n";
        ensure("answered", !service.complete(base + "local x = co\n", 1, 10).empty());
        ensure("checked whole", service.typeChecks() > checks && service.fragmentsChecked() == parts);

        // Stopped part way: nothing, and said so; the next, unstopped,
        // answers over its statement.
        ALLuauService::Stop stop = ALLuauService::newStop();
        ALLuauService::cancel(stop);
        service.setStop(stop);
        ensure("nothing", service.complete(base + "local y = co\n", 1, 10).empty());
        ensure("stopped", service.stopped());
        service.setStop(nullptr);
        checks = service.typeChecks();
        ensure("answered again", !service.complete(base + "local y = co\n", 1, 10).empty() && !service.stopped());
        ensure_equals("over its statement", service.typeChecks(), checks);

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
        ensure("answered", offers(found, "twice"));
        ensure("whole", service.typeChecks() > checks && service.fragmentsChecked() == parts);
        bool string_now = false;
        for (const ALScriptCompletion& c : found)
        {
            string_now |= c.text == "twice" && c.detail.find("string") != std::string::npos;
        }
        ensure("as it is now", string_now);

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
        ensure("the module's function, over the fragment", offers(service.complete(typed, 2, 15), "twice"));
        service.setFragments(false);
        ensure("and the whole check after it finds it too", offers(service.complete(typed, 2, 15), "twice"));
        service.setModules({});
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<44>()
    {
        set_test_name("warming after a check makes what a fragment is checked against the text's, so the next keystroke checks nothing whole");
        ensure("definitions loaded: " + error, loaded);
        service.setFragments(true);
        service.setDocument("warmed");
        const std::string base = "local count: number = 1\n";
        const std::string typed = base + "local n: number = co\n";
        // A check alone leaves the old solver no module to patch: the
        // keystroke after checks the whole script.
        service.check(base);
        size_t checks = service.typeChecks();
        service.complete(typed, 1, 18);
        ensure_equals("unwarmed", service.typeChecks() - checks, size_t(newSolver ? 0 : 1));

        service.check(base);
        checks = service.typeChecks();
        service.warm(base);
        ensure_equals("the old solver's autocomplete module checked; the new solver's check is it", service.typeChecks() - checks,
                      size_t(newSolver ? 0 : 1));
        service.warm(base);
        ensure_equals("and not again for the same text", service.typeChecks() - checks, size_t(newSolver ? 0 : 1));
        checks             = service.typeChecks();
        const size_t parts = service.fragmentsChecked();
        ensure("answered", offers(service.complete(typed, 1, 18), "count"));
        ensure_equals("over its statement", service.fragmentsChecked() - parts, size_t(1));
        ensure_equals("nothing whole", service.typeChecks(), checks);
        service.setFragments(false);
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<45>()
    {
        set_test_name("a script requiring several modules has them checked together, each on a thread of its own as what it requires is checked: the same problems, each module checked once, and not again for an edit of the script alone");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("each module and the script checked once", service.modulesChecked() - before, size_t(5));
        S32  in_modules = 0;
        bool own        = false;
        for (const ALScriptProblem& problem : problems)
        {
            in_modules += problem.file.rfind("disk:/lib/m", 0) == 0 && problem.line == 2 && problem.source == ALScriptProblem::Source::Types ? 1 : 0;
            own |= problem.file.empty() && problem.line == 5 && problem.severity == ALScriptProblem::Severity::Error;
        }
        ensure_equals("each module's own type error, in its lines: " + said(problems), in_modules, 4);
        ensure("and the script's: " + said(problems), own);

        // The script typed in: only it, on its own.
        const size_t again = service.modulesChecked();
        const ALScriptProblems edited = service.check(script + "print(s)\n");
        ensure_equals("the script alone", service.modulesChecked() - again, size_t(1));
        ensure("its own problem still: " + said(edited), std::any_of(edited.begin(), edited.end(), [](const ALScriptProblem& problem) {
                   return problem.file.empty() && problem.line == 5;
               }));
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<46>()
    {
        set_test_name("a module checked with the script has its lints told too, Luau's and the studio's own, in its lines and with no fix, while the script's keep theirs; not again for an edit of the script alone");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("Luau's lint of the module, in its lines: " + said(problems), unused != nullptr);
        const ALScriptProblem* compound = in(problems, "disk:/lib/util.luau", "SlCompoundAssign", 2);
        ensure("the studio's own of the module: " + said(problems), compound != nullptr);
        ensure("neither with a fix, being another file's", unused->fixes.empty() && compound->fixes.empty());
        const ALScriptProblem* own = in(problems, "", "SlCompoundAssign", 2);
        ensure("the script's own, with its fix: " + said(problems), own && !own->fixes.empty());

        const ALScriptProblems edited = service.check(script + "print(util)\n");
        ensure("an edit of the script alone: not the module's again", !in(edited, "disk:/lib/util.luau", "LocalUnused", 0));
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<47>()
    {
        set_test_name("a module checked with the script has what it exports kept as its type says, in the order its fields were declared, for what is offered from it");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("kept as its type says", kept.has_value());
        std::string listed;
        for (const std::string& name : *kept)
        {
            listed += " " + name;
        }
        ensure("each field, in order:" + listed, *kept == std::vector<std::string>({ "alpha", "beta" }));
        ensure("not for another text", !ALLuauExports::checkedOf("disk:/lib/made.luau", made + "\n"));

        // Its type no table, as a nonstrict check may leave one: nothing
        // kept, and its text alone says what it exports.
        const std::string loose = "local M = {}\nfunction M.gamma() end\nreturn (M :: any)\n";
        modules.modules.push_back({ "disk:/lib/loose.luau", loose });
        modules.reaches.push_back({ "", "loose", "disk:/lib/loose.luau" });
        service.setModules(modules);
        service.check("local made = require(\"made\")\nlocal loose = require(\"loose\")\nprint(made.alpha, loose)\n");
        ensure("no table, nothing kept", !ALLuauExports::checkedOf("disk:/lib/loose.luau", loose));
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<48>()
    {
        set_test_name("a module that returns other than one value is said at the require that asks for it, as Luau's require would stop there: what the bundle need not check as it runs");
        ensure("definitions loaded: " + error, loaded);
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
            ensure("two values: said at its require, " + where, two);
            ensure("no value: said at its require, " + where, none);
            ensure("one: nothing, " + where, !one);
            // Told as the module is checked, as all of a module's are: the
            // first time, its text unchanged after.
            ensure("a module's require of it: said in that module, " + where, in_mid || *mode);
        }
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<49>()
    {
        set_test_name("a module a question checked before the script's check is told at that check all the same -- its lints, a require in it of what returns two values, what it exports -- and once");
        ensure("definitions loaded: " + error, loaded);
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
        ensure_equals("the check found the script checked", service.typeChecks(), checks);
        ensure("the module's lint: " + said(problems), told(problems, "LocalUnused", 1));
        ensure("its require of what returns two values: " + said(problems), told(problems, "LuauModuleNotOneValue", 2));
        const std::optional<std::vector<std::string>> kept = ALLuauExports::checkedOf("disk:/lib/asked.luau", util);
        ensure("what it exports, as its type says", kept && std::find(kept->begin(), kept->end(), "n") != kept->end());
        // Told, and so not told again for the same text.
        const ALScriptProblems again = service.check(script);
        ensure("once: " + said(again), !told(again, "LocalUnused", 1));
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<50>()
    {
        set_test_name("a hover on the name of a type the definitions declare says it, with nowhere in the script to go -- its place is a line of their file -- while the script's own goes where it declares it");
        ensure("definitions loaded: " + error, loaded);
        const std::string script = "--!strict\n"                         // 0
                                   "local id: uuid = ll.GetOwner()\n"   // 1
                                   "local r: rotation = ll.GetRot()\n"  // 2
                                   "type Pair = { a: number }\n"        // 3
                                   "local p: Pair = { a = 1 }\n"        // 4
                                   "print(id, r, p)\n";                 // 5
        ALScriptHover hover = service.hover(script, 1, 11); // `uuid`, an extern type
        ensure("uuid, said: " + hover.label, hover.found && hover.label.rfind("type uuid", 0) == 0);
        ensure("nowhere to go", !hover.hasDefinition);
        hover = service.hover(script, 2, 10); // `rotation`, an alias
        ensure("rotation, said: " + hover.label, hover.found && hover.label.rfind("type rotation", 0) == 0);
        ensure("nowhere to go either", !hover.hasDefinition);
        hover = service.hover(script, 4, 10); // `Pair`
        ensure("the script's own, where it declares it", hover.found && hover.hasDefinition && hover.definitionFile.empty() && hover.definitionLine == 3);
    }

    template<> template<>
    void alluauservice_object::test<51>()
    {
        set_test_name("what a module checked exports, as its type says, leaves out a field no script could write after a dot, as what its text says does");
        ensure("definitions loaded: " + error, loaded);
        service.setDocument("keywords");
        service.setConfig(ALLuauConfig());
        const std::string util = "--!strict\nreturn { [\"end\"] = 1, ok = 2, [\"not\"] = 3, export = 4 }\n";
        ALLuauService::Modules modules;
        modules.modules.push_back({ "disk:/lib/keywords.luau", util });
        modules.reaches.push_back({ "", "util", "disk:/lib/keywords.luau" });
        service.setModules(modules);
        service.check("local util = require(\"util\")\nprint(util.ok)\n");
        const std::optional<std::vector<std::string>> kept = ALLuauExports::checkedOf("disk:/lib/keywords.luau", util);
        ensure("kept as its type says", kept.has_value());
        ensure("the names, a word only a statement begins with among them, and no keyword", *kept == std::vector<std::string>({ "ok", "export" }));
        ensure("as its text alone says", ALLuauExports::of(util) == std::vector<std::string>({ "ok", "export" }));
        service.setDocument("");
    }

    template<> template<>
    void alluauservice_object::test<52>()
    {
        set_test_name("a module's lints are told as the configuration of the script that requires it says, a module having none of its own: one it turns off not told, one it makes an error an error, every one where all are, Luau's and the studio's own alike");
        ensure("definitions loaded: " + error, loaded);
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
        ensure("one it turns off, not told: " + said(problems), !told(problems, "LocalUnused"));
        const ALScriptProblem* fatal = told(problems, "FunctionUnused");
        ensure("one it makes an error, an error: " + said(problems), fatal && fatal->severity == ALScriptProblem::Severity::Error);
        const ALScriptProblem* compound = told(problems, "SlCompoundAssign");
        ensure("the studio's own as it says, no error: " + said(problems), compound && compound->severity != ALScriptProblem::Severity::Error);

        ALLuauConfig all;
        all.lintErrors = true;
        const ALScriptProblems errors = checked("all", "disk:/lib/all.luau", all);
        const ALScriptProblem* unused = told(errors, "LocalUnused");
        ensure("every one an error, Luau's: " + said(errors), unused && unused->severity == ALScriptProblem::Severity::Error);
        compound = told(errors, "SlCompoundAssign");
        ensure("and the studio's own: " + said(errors), compound && compound->severity == ALScriptProblem::Severity::Error);
        service.setDocument("");
    }
}
