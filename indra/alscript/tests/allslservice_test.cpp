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
#include "../alscriptfixes.h"

#include "../test/lltut.h"

#include "llsdserialize.h"
#include "lluuid.h"

#include <filesystem>
#include <fstream>

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
        // Taken apart by its code, for another language to say: the
        // words in the order the marks have them.
        ensure_equals("the key", problem->key, std::string("LSLArgumentWrongType"));
        ensure_equals("five words", problem->args.size(), size_t(5));
        ensure_equals("the type passed", problem->args[0], std::string("string"));
        ensure_equals("the argument's number", problem->args[1], std::string("1"));
        ensure_equals("the function", problem->args[2], std::string("llSay"));
        ensure_equals("put back, the same words", ALScriptProblem::fill("Passing [1] as argument [2] of `[3]' which is declared as `[4] [5]'.", problem->args), problem->message);
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

    template<> template<>
    void allslservice_object::test<11>()
    {
        set_test_name("every name is told by what it is: global, parameter, local, function, builtin, state, event, label");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "integer count = 0;\n"
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    touch_start(integer total)\n"
            "    {\n"
            "        float h = half(total);\n"
            "        llSay(PUBLIC_CHANNEL, (string)h);\n"
            "        @again;\n"
            "        if (count) jump again;\n"
            "        state other;\n"
            "    }\n"
            "}\n"
            "state other { }\n";
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
        const ALScriptSemanticToken* count = at(0, 8);
        ensure("count, a global declared:" + listed, count && count->kind == ALScriptSymbolKind::Variable && (count->modifiers & ALScriptSemanticToken::Global) && (count->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* half = at(1, 6);
        ensure("half, a function declared:" + listed, half && half->kind == ALScriptSymbolKind::Function && (half->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* n = at(1, 19);
        ensure("n, a parameter declared:" + listed, n && n->kind == ALScriptSymbolKind::Parameter && (n->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* touch = at(4, 4);
        ensure("touch_start, an event:" + listed, touch && touch->kind == ALScriptSymbolKind::Event);
        const ALScriptSemanticToken* h = at(6, 14);
        ensure("h, a local declared:" + listed, h && h->kind == ALScriptSymbolKind::Variable && !(h->modifiers & ALScriptSemanticToken::Global) && (h->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* total = at(6, 23);
        ensure("total used, a parameter:" + listed, total && total->kind == ALScriptSymbolKind::Parameter && !(total->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* say = at(7, 8);
        ensure("llSay, a builtin function:" + listed, say && say->kind == ALScriptSymbolKind::Function && (say->modifiers & ALScriptSemanticToken::Builtin));
        const ALScriptSemanticToken* channel = at(7, 14);
        ensure("PUBLIC_CHANNEL, a builtin constant:" + listed, channel && channel->kind == ALScriptSymbolKind::Constant && (channel->modifiers & ALScriptSemanticToken::ReadOnly));
        const ALScriptSemanticToken* label = at(8, 9);
        ensure("again, a label declared:" + listed, label && label->kind == ALScriptSymbolKind::Label && (label->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* jump = at(9, 24);
        ensure("jumped to, not declared:" + listed, jump && jump->kind == ALScriptSymbolKind::Label && !(jump->modifiers & ALScriptSemanticToken::Declaration));
        const ALScriptSemanticToken* other = at(10, 14);
        ensure("other, a state:" + listed, other && other->kind == ALScriptSymbolKind::State);
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            ensure("in order, each once", tokens[i - 1].span < tokens[i].span);
        }
    }

    template<> template<>
    void allslservice_object::test<12>()
    {
        set_test_name("inlay hints name each argument's parameter, builtin or the script's own");
        ensure("builtins loaded: " + error, loaded);
        const std::string script =
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        integer n = 4;\n"
            "        llSay(0, (string)half(n));\n"
            "        llSetTimerEvent(half(2));\n"
            "    }\n"
            "}\n";
        std::vector<ALScriptInlayHint> hints = service.inlayHints(script, true);
        std::string listed;
        for (const ALScriptInlayHint& h : hints) listed += llformat(" %d:%d[%s]", h.line, h.column, h.text.c_str());
        auto has = [&](S32 line, S32 column, const char* text) {
            for (const ALScriptInlayHint& h : hints)
            {
                if (h.line == line && h.column == column && h.text == text) return true;
            }
            return false;
        };
        ensure("the channel's name before the 0:" + listed, has(6, 14, "Channel:") || has(6, 14, "channel:"));
        ensure("the text's before the cast:" + listed, has(6, 17, "Text:") || has(6, 17, "text:"));
        ensure("nothing before n, which is the name:" + listed, !has(6, 30, "n:"));
        ensure("n: before the 2:" + listed, has(7, 29, "n:"));
        ensure("nothing when not asked", service.inlayHints(script, false).empty());
    }

    template<> template<>
    void allslservice_object::test<13>()
    {
        set_test_name("a script mid-edit is answered about the rest of it: the scope, the call being typed, what every name is");
        ensure("builtins loaded: " + error, loaded);
        const std::string head =
            "integer count = 0;\n"
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    touch_start(integer total)\n"
            "    {\n"
            "        string before = \"a\";\n";
        const std::string tail = "\n    }\n}\n";
        auto names = [&](const std::string& script, S32 line, S32 column) {
            std::string out;
            for (const ALScriptCompletion& c : service.symbols(script, line, column))
            {
                out += " " + c.text;
            }
            return out + " ";
        };

        // A call not yet closed, the caret after an argument half typed.
        std::string script = head + "        llSay(0, be" + tail;
        ALScriptSignature sig = service.signature(script, 7, 19);
        ensure("the call is found", sig.found && sig.label.find("llSay(") != std::string::npos);
        ensure_equals("at its second parameter", sig.active, 1);
        ensure("the text does not parse", !service.parsed());
        ensure("but was understood", service.understood());
        const std::string in_scope = names(script, 7, 17);
        ensure("the local before, in scope:" + in_scope, in_scope.find(" before ") != std::string::npos);
        ensure("the parameter:" + in_scope, in_scope.find(" total ") != std::string::npos);
        ensure("the global:" + in_scope, in_scope.find(" count ") != std::string::npos);

        // The bracket just typed, and a comma just typed.
        sig = service.signature(head + "        llSay(" + tail, 7, 14);
        ensure("just opened", sig.found);
        ensure_equals("at the first parameter", sig.active, 0);
        sig = service.signature(head + "        llSay(0," + tail, 7, 16);
        ensure("after the comma", sig.found);
        ensure_equals("at the second parameter", sig.active, 1);
        sig = service.signature(head + "        llSay(0, \"hel" + tail, 7, 20);
        ensure("inside a string not yet closed", sig.found && sig.active == 1);
        sig = service.signature(head + "        half(" + tail, 7, 13);
        ensure("the script's own function", sig.found && sig.label == "float half(integer n)");

        // A name alone, and a statement without its semicolon.
        ensure("a word alone", names(head + "        bef" + tail, 7, 8).find(" before ") != std::string::npos);
        ensure("no semicolon", names(head + "        llSay(0, before)" + tail, 7, 17).find(" before ") != std::string::npos);

        // What the check's other questions answer of a text that does not
        // parse: the rest of it, rather than nothing.
        script = head + "        llSay(0, be" + tail;
        ensure("the outline", service.outline(script).size() == 4);
        const std::vector<ALScriptSemanticToken> tokens = service.semanticTokens(script);
        bool before_named = false;
        for (const ALScriptSemanticToken& token : tokens)
        {
            before_named = before_named || (token.span.line == 6 && token.kind == ALScriptSymbolKind::Variable);
        }
        ensure("the names before the break", before_named);
        ensure("the hints", !service.inlayHints(head + "        llSay(0, \"x\");\n        llOwnerSay(" + tail, true).empty());

        // A function being declared at the top, before the states.
        script = "integer count = 0;\nfoo(\ndefault\n{\n    state_entry()\n    {\n        llSay(0, (string)count);\n    }\n}\n";
        const ALScriptHover hover = service.hover(script, 6, 26);
        ensure("a name below a broken declaration", hover.found && hover.label == "integer count");
        ensure("its references", service.references(script, 6, 26).references.size() == 2);

        // A block left open at the end.
        script = head + "        llSay(0, before);\n";
        ensure("closed at the end", names(script, 7, 17).find(" before ") != std::string::npos);

        // Nothing to mend from.
        ensure("nonsense is not understood", service.outline("}}} ((( ;;; \"").empty() && !service.understood());
    }

    template<> template<>
    void allslservice_object::test<14>()
    {
        set_test_name("a syntax error says what is missing where it is missing, or what was unexpected, keyed");
        ensure("builtins loaded: " + error, loaded);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        auto first_error = [&](const std::string& script) {
            for (const ALScriptProblem& problem : service.check(script))
            {
                if (problem.severity == ALScriptProblem::Severity::Error)
                {
                    return problem;
                }
            }
            return ALScriptProblem();
        };
        // A call not closed, the block closed on the next line.
        ALScriptProblem p = first_error(head + "        llSay(0, \"hi\"\n    }\n}\n");
        ensure_equals("missing the bracket: " + p.message, p.message, std::string("Missing ')'."));
        ensure("keyed", p.key == "LSLSyntaxMissing" && p.args.size() == 1 && p.args[0] == "')'");
        ensure("on the string's closing quote, not the next line's brace", p.line == 4 && p.column == 20 && p.endColumn == 21);
        // A statement not ended.
        p = first_error(head + "        llSay(0, \"hi\")\n        llOwnerSay(\"x\");\n    }\n}\n");
        ensure_equals("missing the semicolon: " + p.message, p.message, std::string("Missing ';'."));
        ensure("after the call", p.line == 4 && p.column == 21);
        p = first_error(head + "        integer x = 5 // five\n    }\n}\n");
        ensure_equals("past a comment: " + p.message, p.message, std::string("Missing ';'."));
        ensure("on the number", p.line == 4 && p.column == 20);
        // Something that is no statement's next word.
        p = first_error("integer x = 1;\nfoo(\ndefault\n{\n    state_entry() { }\n}\n");
        ensure("unexpected, by what it is: " + p.message, p.key == "LSLSyntaxUnexpected" || p.key == "LSLSyntaxUnexpectedWanted");
        ensure("said as written: " + p.message, p.message.find("'default'") != std::string::npos);
        ensure("no bison left in it: " + p.message, p.message.find("syntax error") == std::string::npos && p.message.find("STATE_DEFAULT") == std::string::npos);
    }
    template<> template<>
    void allslservice_object::test<15>()
    {
        set_test_name("the same text checked again says what it says once, whatever was asked between, for either target");
        ensure("builtins loaded: " + error, loaded);
        const std::string script = "integer unused;\nlist g = [1, 2];\ninteger h = g;\ndefault\n{\n    state_entry()\n    {\n        integer never;\n    }\n}\n";
        const ALScriptProblems first = service.check(script, true);
        ensure("something to say: " + said(first), !first.empty());
        service.outline(script);
        service.hover(script, 0, 9);
        const ALScriptProblems again = service.check(script, true);
        ensure_equals("said once, the second time too", said(again), said(first));
        const ALScriptProblems lso = service.check(script, false);
        const ALScriptProblems back = service.check(script, true);
        ensure_equals("and after the other target, nothing of it left", said(back), said(first));
        ensure_equals("the other target, asked twice, the same", said(service.check(script, false)), said(lso));
    }
    template<> template<>
    void allslservice_object::test<16>()
    {
        set_test_name("a parameter written without its type says so by name, a function's or an event's, and what was not expected is said plainly");
        ensure("builtins loaded: " + error, loaded);
        auto first_error = [&](const std::string& script) {
            for (const ALScriptProblem& problem : service.check(script))
            {
                if (problem.severity == ALScriptProblem::Severity::Error)
                {
                    return problem;
                }
            }
            return ALScriptProblem();
        };
        const std::string tail = "default\n{\n    state_entry() { }\n}\n";
        // As Nexii's linkset library has it.
        ALScriptProblem p = first_error("list ObjectLinksetSittingAvatars(object) {\n    return [];\n}\n" + tail);
        ensure_equals("the first: " + p.message, p.message,
                      std::string("The parameter 'object' needs its type before it: integer, float, string, key, vector, rotation or list."));
        ensure("keyed", p.key == "LSLParameterUntyped" && p.args.size() == 1 && p.args[0] == "object");
        ensure("on the name, whole", p.line == 0 && p.column == 33 && p.endLine == 0 && p.endColumn == 39);
        p = first_error("f(integer a, b) { }\n" + tail);
        ensure("after a comma: " + p.message, p.key == "LSLParameterUntyped" && p.args[0] == "b" && p.column == 13);
        p = first_error("default\n{\n    touch_start(total)\n    {\n    }\n}\n");
        ensure("an event's: " + p.message, p.key == "LSLParameterUntyped" && p.args[0] == "total" && p.line == 2);
        // A name where none can go, not after a bracket.
        p = first_error("default\n{\n    state_entry()\n    {\n        llSay(0, \"x\") y;\n    }\n}\n");
        ensure("not a parameter: " + p.message, p.key != "LSLParameterUntyped");
        ensure("said plainly: " + p.message, p.message.find("Unexpected a") == std::string::npos && p.message.find("syntax error") == std::string::npos);
    }
    template<> template<>
    void allslservice_object::test<17>()
    {
        set_test_name("a definitions file with lines the engine cannot read loads the rest, and the process goes on");
        // What a grid newer than this viewer may send: a type the engine does
        // not know, a constant it cannot read, blank lines of CR LF and of
        // blanks -- each of which ended the process before.
        const std::string path = fsyspath(std::filesystem::temp_directory_path() / ("al_builtins_" + LLUUID::generateNewID().asString() + ".txt")).string();
        {
            llofstream out(path, std::ios::binary);
            out << "// the grid's definitions, as a newer grid might send them\n"
                   "integer llAlchemyProbe(integer a)\n"
                   "\r\n"
                   "   \n"
                   "const uuid ALCHEMY_UUID = \"00000000-0000-0000-0000-000000000000\"\n"
                   "uuid llAlchemyBadReturn()\n"
                   "integer llAlchemyBadParam(uuid a)\n"
                   "const integer ALCHEMY_BAD = abc\n"
                   "integer llAlchemyAfter(string s)\n";
        }
        std::string   why;
        const bool    read = service.loadBuiltins(path, why);
        std::error_code gone;
        std::filesystem::remove(fsyspath(path), gone);
        ensure("loaded: " + why, read);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        const ALScriptProblems kept = service.check(head + "        llOwnerSay((string)(llAlchemyProbe(1) + llAlchemyAfter(\"x\")));\n" + tail, true);
        ensure_equals("the lines it could read are known: " + said(kept), errors(kept), size_t(0));
        const ALScriptProblems left = service.check(head + "        llAlchemyBadReturn();\n" + tail, true);
        ensure("a line it could not read is not: " + said(left), errors(left) > 0);
    }

    template<> template<>
    void allslservice_object::test<18>()
    {
        set_test_name("a definitions file under a folder named outside the ANSI code page loads");
        // The grid's definitions are kept in the cache, under the user's
        // profile, whose name may be in any script: the path is UTF-8, and
        // on Windows the engine opened it as the ANSI code page read it.
        const fsyspath dir = std::filesystem::temp_directory_path() / fsyspath("al_builtins_\xc3\xa9\xe6\x97\xa5_" + LLUUID::generateNewID().asString());
        std::error_code made;
        std::filesystem::create_directories(dir, made);
        ensure("folder made: " + made.message(), !made);
        const std::string path = fsyspath(dir / "builtins.txt").string();
        {
            llofstream out(path, std::ios::binary);
            out << "integer llAlchemyWide(integer a)\n";
        }
        std::string why;
        const bool  read = service.loadBuiltins(path, why);
        std::error_code gone;
        std::filesystem::remove_all(dir, gone);
        ensure("loaded: " + why, read);
        const ALScriptProblems kept = service.check("default\n{\n    state_entry()\n    {\n        llOwnerSay((string)llAlchemyWide(1));\n    }\n}\n", true);
        ensure_equals("what it defines is known: " + said(kept), errors(kept), size_t(0));
    }

    template<> template<>
    void allslservice_object::test<19>()
    {
        set_test_name("a text that does not parse is mended once whole and once at the caret, however the questions take turns");
        const std::string broken = "integer count;\ndefault {\n    state_entry() {\n        llSay(0, (string)count\n    }\n}\n";
        const size_t      before = service.mendings();
        // What the check asks after it, whole; a call's parameters, at the
        // caret.
        service.outline(broken);
        service.signature(broken, 3, 20);
        const size_t twice = service.mendings();
        ensure_equals("one each", twice - before, size_t(2));
        for (int turn = 0; turn < 3; ++turn)
        {
            service.outline(broken);
            service.signature(broken, 3, 20);
            service.hover(broken, 3, 20);
            service.symbols(broken, 3, 20);
        }
        ensure_equals("and no more, turn and turn about", service.mendings(), twice);
        service.signature(broken, 3, 12);
        ensure_equals("another place is mended again", service.mendings(), twice + 1);
    }

    template<> template<>
    void allslservice_object::test<20>()
    {
        set_test_name("lines nobody reads -- an include's -- are passed over by the names and the hints, and their problems offered no fixes");
        // An include's function, unused, then the script's own.
        const std::string text = "helper(integer count)\n{\n    llOwnerSay((string)count);\n}\n"
                                 "default\n{\n    state_entry()\n    {\n        integer unused = 1;\n        llOwnerSay(\"x\");\n    }\n}\n";
        service.setPassedOver({ { 0, 3 } });
        const ALScriptProblems problems = service.check(text, true);
        bool helper_said = false, helper_fixed = false, own_fixed = false;
        for (const ALScriptProblem& problem : problems)
        {
            if (problem.line <= 3 && problem.key == "LSLDeclaredButNotUsed")
            {
                helper_said  = true;
                helper_fixed = !problem.fixes.empty();
            }
            if (problem.line == 8 && problem.key == "LSLDeclaredButNotUsed")
            {
                own_fixed = !problem.fixes.empty();
            }
        }
        ensure("the include's unused function said", helper_said);
        ensure("but offered nothing", !helper_fixed);
        ensure("the script's own offered its fix", own_fixed);
        bool in_include = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_include |= token.span.line <= 3;
        }
        ensure("no names coloured in the include", !in_include);
        service.setPassedOver({});
        bool coloured = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            coloured |= token.span.line <= 3;
        }
        ensure("with nothing passed over, coloured", coloured);
    }

    template<> template<>
    void allslservice_object::test<21>()
    {
        set_test_name("another region's builtins: what it adds is known after, what both have stays as first loaded, and nothing is said twice");
        const std::string base = std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt";
        std::string       error;
        ensure("the first region's: " + error, service.loadBuiltins(base, error));
        const std::string script = "default\n{\n    state_entry()\n    {\n        integer n = llBrandNewThing(1);\n        llOwnerSay((string)n);\n    }\n}\n";
        bool unknown = false;
        for (const ALScriptProblem& problem : service.check(script))
        {
            unknown |= problem.message.find("llBrandNewThing") != std::string::npos;
        }
        ensure("not yet known", unknown);
        // A region with one function more.
        std::string text;
        {
            llifstream in(base, std::ios::in | std::ios::binary);
            std::ostringstream all;
            all << in.rdbuf();
            text = all.str();
        }
        const std::string path = (std::filesystem::temp_directory_path() / "alscript-test-builtins-newer.txt").string();
        {
            llofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
            out << text << "integer llBrandNewThing( integer Value )\n";
        }
        const bool loaded = service.loadBuiltins(path, error);
        LLFile::remove(path);
        ensure("loaded: " + error, loaded);
        unknown = false;
        for (const ALScriptProblem& problem : service.check(script))
        {
            unknown |= problem.message.find("llBrandNewThing") != std::string::npos;
        }
        ensure("known now", !unknown);
        // Nothing twice: an old one still one declaration.
        const ALScriptSignature said = service.signature("default { state_entry() { llAbs( } }\n", 0, 32);
        ensure("an old function as it was", said.found && said.label.find("llAbs") != std::string::npos);
    }

    template<> template<>
    void allslservice_object::test<22>()
    {
        set_test_name("every template Script Studio offers for a new LSL script checks without an error");
        ensure("builtins loaded: " + error, loaded);
        llifstream in(std::string(AL_SCRIPT_TEMPLATES_DIR) + "/lsl.xml", std::ios::in | std::ios::binary);
        LLSD       templates;
        ensure("read", in.is_open() && LLSDSerialize::fromXML(templates, in) != LLSDParser::PARSE_FAILURE && templates.isArray());
        ensure("some", templates.size() > 0);
        for (LLSD::array_const_iterator it = templates.beginArray(); it != templates.endArray(); ++it)
        {
            const std::string      name     = (*it)["name"].asString();
            const ALScriptProblems problems = service.check((*it)["body"].asString());
            ensure(name + " parses", service.parsed());
            ensure_equals(name + ": " + said(problems), errors(problems), size_t(0));
        }
    }

    template<> template<>
    void allslservice_object::test<23>()
    {
        set_test_name("a handler or a state begun inside a block left open is a missing brace, after the last code before it, not what the "
                      "parser wanted of the head it misread");
        ensure("builtins loaded: " + error, loaded);
        auto first_error = [&](const std::string& script) {
            for (const ALScriptProblem& problem : service.check(script))
            {
                if (problem.severity == ALScriptProblem::Severity::Error)
                {
                    return problem;
                }
            }
            return ALScriptProblem();
        };
        const std::string open = "default\n{\n    state_entry()\n    {\n        llSay(0, \"Hello, Avatar!\");\n\n";
        // A handler with parameters, read as a call.
        ALScriptProblem p = first_error(open + "    touch_start(integer total_number)\n    {\n        llSay(0, \"Touched.\");\n    }\n}\n");
        ensure_equals("the brace: " + p.message, p.message, std::string("Missing '}'."));
        ensure("keyed", p.key == "LSLSyntaxMissing" && p.args.size() == 1 && p.args[0] == "'}'");
        ensure("on the semicolon before it", p.line == 4 && p.column == 34 && p.endColumn == 35);
        // One without, its brace on the next line.
        p = first_error(open + "    touch_start()\n    {\n    }\n}\n");
        ensure_equals("the brace, a head without parameters: " + p.message, p.message, std::string("Missing '}'."));
        ensure("on the same semicolon", p.line == 4 && p.column == 34);
        // A state after one not closed.
        p = first_error("default\n{\n    state_entry()\n    {\n    }\n\nstate other\n{\n    state_entry()\n    {\n    }\n}\n");
        ensure_equals("the state's brace: " + p.message, p.message, std::string("Missing '}'."));
        ensure("after the handler's", p.line == 4 && p.column == 4);
        // A handler's own head wrong where handlers go is not one.
        p = first_error("default\n{\n    touch_start(integer)\n    {\n    }\n}\n");
        ensure("a head wrong in its state's braces: not a brace", p.severity == ALScriptProblem::Severity::Error && p.message != "Missing '}'.");
    }

    template<> template<>
    void allslservice_object::test<24>()
    {
        set_test_name("the studio's own LSL lints beside Tailslide's: a pure call in a loop's check whose arguments the loop leaves be, noted once, and answering a NOLINT by its name");
        ensure("builtins load: " + error, loaded);
        const auto found = [&](const std::string& text) {
            ALScriptProblems out;
            for (const ALScriptProblem& p : service.check(text))
            {
                if (p.code == "SlLoopInvariantCall")
                {
                    out.push_back(p);
                }
            }
            return out;
        };
        ALScriptProblems said = found("list gL = [1, 2];\n"
                                      "default { state_entry() {\n"
                                      "    integer i; string s = \"abc\";\n"
                                      "    for (i = 0; i < llGetListLength(gL); ++i) llOwnerSay((string)i);\n"
                                      "    while (i < llStringLength(s)) ++i;\n"
                                      "    do ++i; while (i < llGetListLength(llParseString2List(s, [\",\"], [])));\n"
                                      "} }\n");
        ensure_equals("three: each loop's once", said.size(), size_t(3));
        ensure("a note keyed as Tailslide's, by the function: " + said[0].message,
               said[0].key == "LSLSlLoopInvariantCall" && said[0].severity == ALScriptProblem::Severity::Note && said[0].line == 3 &&
                   said[0].args == std::vector<std::string>{ "llGetListLength" } && said[0].source == ALScriptProblem::Source::Lint);
        ensure("the outer call of two: " + said[2].message, said[2].line == 5 && said[2].args[0] == "llGetListLength");
        ensure("a NOLINT answers to the rule's name",
               ALScriptFixes::suppressed(said[0], "    for (i = 0; i < llGetListLength(gL); ++i) // NOLINT(SlLoopInvariantCall)", "", false));

        said = found("list gL = [1, 2];\n"
                     "grow() { gL += [0]; }\n"
                     "default { state_entry() {\n"
                     "    integer i; list l;\n"
                     "    for (i = 0; i < llGetListLength(l); ++i) l += [i];\n"
                     "    for (i = 0; i < llGetListLength(gL) && i < 10; ++i) grow();\n"
                     "    while (llGetUnixTime() < 5) ++i;\n"
                     "} }\n");
        ensure("not where the loop changes what it is given, itself or by a function, nor for a call that reads the world", said.empty());
    }
}
