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
        ensure("a builtin by its signature: " + hover.label, hover.label.rfind("function print(", 0) == 0);
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
}
