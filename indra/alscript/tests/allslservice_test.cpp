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

#include "../lsl/allslservice.h"
#include "../lint/alscriptfixes.h"

#include "alteststateguard.h"

#include <catch2/catch_test_macros.hpp>

#include "llsdserialize.h"
#include "lluuid.h"

#include <filesystem>
#include <fstream>

namespace
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

    TEST_CASE_METHOD(allslservice_data, "allslservice: the grid's builtins load", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
        CHECK(service.hasBuiltins());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a second service loading the same builtins has them too", "[allslservice]")
    {
        // Every name it declares is in Tailslide's table already, so none
        // is loaded again; the service has them all the same.
        REQUIRE(loaded);
        ALLSLService again;
        std::string  why;
        const bool   read = again.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", why);
        CAPTURE(why);
        CHECK(read);
        CHECK(again.hasBuiltins());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a script that is right checks clean", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check(
            "default\n"
            "{\n"
            "    state_entry()\n"
            "    {\n"
            "        llSay(0, \"Hello, \" + (string)llGetOwner());\n"
            "    }\n"
            "}\n");
        UNSCOPED_INFO("nothing said: " + said(problems));
        REQUIRE(problems.size() == 0);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a wrong argument type is an error on its line", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("the wrong-type error: " + said(problems));
        REQUIRE(problem != nullptr);
        CHECK(problem->severity == ALScriptProblem::Severity::Error);
        CHECK(problem->source == ALScriptProblem::Source::Types);
        UNSCOPED_INFO("on the call's line, zero-based");
        CHECK(problem->line == 4);
        // Taken apart by its code, for another language to say: the
        // words in the order the marks have them.
        CHECK(problem->key == std::string("LSLArgumentWrongType"));
        REQUIRE(problem->args.size() == size_t(5));
        UNSCOPED_INFO("the type passed");
        CHECK(problem->args[0] == std::string("string"));
        UNSCOPED_INFO("the argument's number");
        CHECK(problem->args[1] == std::string("1"));
        UNSCOPED_INFO("the function");
        CHECK(problem->args[2] == std::string("llSay"));
        UNSCOPED_INFO("put back, the same words");
        CHECK(ALScriptProblem::fill("Passing [1] as argument [2] of `[3]' which is declared as `[4] [5]'.", problem->args) == problem->message);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: an unused global is a warning", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("the unused warning: " + said(problems));
        REQUIRE(problem != nullptr);
        CHECK(problem->severity == ALScriptProblem::Severity::Warning);
        CHECK(problem->source == ALScriptProblem::Source::Lint);
        UNSCOPED_INFO("on the declaration's line");
        CHECK(problem->line == 0);
        CHECK(errors(problems) == 0);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a syntax error is the parser's", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
        ALScriptProblems problems = service.check(
            "default\n"
            "{\n"
            "    state_entry(\n"
            "    {\n"
            "    }\n"
            "}\n");
        UNSCOPED_INFO("an error: " + said(problems));
        REQUIRE(errors(problems) > 0);
        CHECK(problems.front().source == ALScriptProblem::Source::Parser);
        CHECK(problems.front().line > 0);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a builtins file that cannot be opened is refused and is not fatal", "[allslservice]")
    {
        ALLSLService bare;
        std::string  why;
        REQUIRE_FALSE(bare.loadBuiltins("/nonexistent/builtins.txt", why));
        REQUIRE_FALSE(why.empty());
        CHECK_FALSE(bare.hasBuiltins());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: symbols in scope are the script's own and only locals declared before", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        CHECK(has("count", ALScriptSymbolKind::Variable, "integer count"));
        CHECK(has("half", ALScriptSymbolKind::Function, "float half(integer n)"));
        CHECK(has("other", ALScriptSymbolKind::State, "state other"));
        CHECK(has("total", ALScriptSymbolKind::Parameter, "integer total"));
        CHECK(has("before", ALScriptSymbolKind::Variable, "string before"));
        bool after = false, builtin = false;
        for (const ALScriptCompletion& c : found)
        {
            after   = after || c.text == "after";
            builtin = builtin || c.text == "llSay";
        }
        CHECK_FALSE(after);
        CHECK_FALSE(builtin);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: hover shows a declaration and where it is; signature says which argument", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        REQUIRE(hover.found);
        CHECK(hover.label == std::string("float half(integer n)"));
        CHECK(hover.hasDefinition);
        CHECK(hover.definitionLine == 0);
        hover = service.hover(script, 5, 10);  // on `llSay`
        REQUIRE(hover.found);
        UNSCOPED_INFO("as declared: " + hover.label);
        REQUIRE(hover.label.find("llSay(") != std::string::npos);
        UNSCOPED_INFO("but not in the script");
        CHECK_FALSE(hover.hasDefinition);

        ALScriptSignature sig = service.signature(script, 5, 15);  // in llSay's first argument
        REQUIRE(sig.found);
        UNSCOPED_INFO("the builtin's signature: " + sig.label);
        REQUIRE((sig.label.find("llSay(") == 0 || sig.label.find(" llSay(") != std::string::npos));
        REQUIRE(sig.parameters.size() == size_t(2));
        CHECK(sig.active == 0);
        sig = service.signature(script, 5, 30);  // just inside half(
        REQUIRE(sig.found);
        CHECK(sig.label == "float half(integer n)");
        CHECK(sig.active == 0);
        UNSCOPED_INFO("none at the top");
        REQUIRE_FALSE(service.signature(script, 0, 0).found);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: references find a declaration and its uses; only the script's own rename", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        REQUIRE(refs.found);
        CHECK(refs.name == "count");
        CHECK(refs.kind == ALScriptSymbolKind::Variable);
        CHECK(refs.hasDefinition);
        CHECK(refs.definition.line == 0);
        CHECK(refs.definition.column == 8);
        CHECK(refs.definition.endColumn == 13);
        REQUIRE(refs.references.size() == size_t(4));
        CHECK(refs.references[0] == refs.definition);
        {
            INFO("the cast's operand last");
            CHECK(refs.references[3].line == 6);
            CHECK(refs.references[3].column == 25);
        }
        CHECK(refs.renamable);

        refs = service.references(script, 6, 10);  // on llSay
        REQUIRE(refs.found);
        CHECK(refs.name == "llSay");
        {
            INFO("used once, declared nowhere here");
            REQUIRE(refs.references.size() == 1);
            CHECK_FALSE(refs.hasDefinition);
            CHECK_FALSE(refs.renamable);
        }

        refs = service.references(script, 7, 15);  // on `other` in `state other;`
        REQUIRE(refs.found);
        CHECK(refs.kind == ALScriptSymbolKind::State);
        CHECK(refs.hasDefinition);
        CHECK(refs.definition.line == 10);
        CHECK(refs.definition.column == 6);
        UNSCOPED_INFO("the declaration and the change");
        REQUIRE(refs.references.size() == size_t(2));
        CHECK(refs.renamable);

        refs = service.references(script, 12, 26);  // on `default` in `state default;`
        REQUIRE(refs.found);
        CHECK(refs.name == "default");
        UNSCOPED_INFO("but is not the script's to rename");
        CHECK_FALSE(refs.renamable);

        refs = service.references(script, 3, 6);  // on state_entry
        REQUIRE(refs.found);
        CHECK(refs.kind == ALScriptSymbolKind::Event);
        UNSCOPED_INFO("in both states");
        REQUIRE(refs.references.size() == 2);
        CHECK_FALSE(refs.renamable);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: the outline lists globals and functions and states; events one deeper", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("six entries:" + names);
        REQUIRE(outline.size() == size_t(6));
        CHECK(outline[0].name == "count");
        CHECK(outline[0].kind == ALScriptSymbolKind::Variable);
        CHECK(outline[0].detail == "integer count");
        CHECK(outline[0].depth == 0);
        CHECK(outline[1].name == "half");
        CHECK(outline[1].kind == ALScriptSymbolKind::Function);
        CHECK(outline[1].detail == "float half(integer n)");
        CHECK(outline[1].nameSpan.line == 1);
        CHECK(outline[1].nameSpan.column == 6);
        CHECK(outline[1].nameSpan.endColumn == 10);
        CHECK(outline[2].name == "default");
        CHECK(outline[2].kind == ALScriptSymbolKind::State);
        CHECK(outline[2].depth == 0);
        CHECK(outline[2].span.line == 2);
        CHECK(outline[2].span.endLine == 8);
        CHECK(outline[3].name == "state_entry");
        CHECK(outline[3].kind == ALScriptSymbolKind::Event);
        CHECK(outline[3].depth == 1);
        CHECK(outline[4].name == "touch_start");
        CHECK(outline[4].detail == "touch_start(integer total)");
        CHECK(outline[4].span.line == 5);
        CHECK(outline[4].span.endLine == 7);
        CHECK(outline[5].name == "other");
        CHECK(outline[5].kind == ALScriptSymbolKind::State);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: semantic tokens tell every name by what it is", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        {
            INFO("count, a global declared:" + listed);
            REQUIRE(count);
            REQUIRE(count->kind == ALScriptSymbolKind::Variable);
            REQUIRE(count->modifiers & ALScriptSemanticToken::Global);
            CHECK(count->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* half = at(1, 6);
        {
            INFO("half, a function declared:" + listed);
            REQUIRE(half);
            REQUIRE(half->kind == ALScriptSymbolKind::Function);
            CHECK(half->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* n = at(1, 19);
        {
            INFO("n, a parameter declared:" + listed);
            REQUIRE(n);
            REQUIRE(n->kind == ALScriptSymbolKind::Parameter);
            CHECK(n->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* touch = at(4, 4);
        {
            INFO("touch_start, an event:" + listed);
            REQUIRE(touch);
            CHECK(touch->kind == ALScriptSymbolKind::Event);
        }
        const ALScriptSemanticToken* h = at(6, 14);
        {
            INFO("h, a local declared:" + listed);
            REQUIRE(h);
            REQUIRE(h->kind == ALScriptSymbolKind::Variable);
            REQUIRE_FALSE(h->modifiers & ALScriptSemanticToken::Global);
            CHECK(h->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* total = at(6, 23);
        {
            INFO("total used, a parameter:" + listed);
            REQUIRE(total);
            REQUIRE(total->kind == ALScriptSymbolKind::Parameter);
            CHECK_FALSE(total->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* say = at(7, 8);
        {
            INFO("llSay, a builtin function:" + listed);
            REQUIRE(say);
            REQUIRE(say->kind == ALScriptSymbolKind::Function);
            CHECK(say->modifiers & ALScriptSemanticToken::Builtin);
        }
        const ALScriptSemanticToken* channel = at(7, 14);
        {
            INFO("PUBLIC_CHANNEL, a builtin constant:" + listed);
            REQUIRE(channel);
            REQUIRE(channel->kind == ALScriptSymbolKind::Constant);
            CHECK(channel->modifiers & ALScriptSemanticToken::ReadOnly);
        }
        const ALScriptSemanticToken* label = at(8, 9);
        {
            INFO("again, a label declared:" + listed);
            REQUIRE(label);
            REQUIRE(label->kind == ALScriptSymbolKind::Label);
            CHECK(label->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* jump = at(9, 24);
        {
            INFO("jumped to, not declared:" + listed);
            REQUIRE(jump);
            REQUIRE(jump->kind == ALScriptSymbolKind::Label);
            CHECK_FALSE(jump->modifiers & ALScriptSemanticToken::Declaration);
        }
        const ALScriptSemanticToken* other = at(10, 14);
        {
            INFO("other, a state:" + listed);
            REQUIRE(other);
            CHECK(other->kind == ALScriptSymbolKind::State);
        }
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            UNSCOPED_INFO("in order, each once");
            REQUIRE(tokens[i - 1].span < tokens[i].span);
        }
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: inlay hints name each argument's parameter in builtin and own calls", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("the channel's name before the 0:" + listed);
        CHECK(has(6, 14, "channel:"));
        UNSCOPED_INFO("the message's before the cast:" + listed);
        CHECK(has(6, 17, "msg:"));
        UNSCOPED_INFO("nothing before n, which is the name:" + listed);
        CHECK_FALSE(has(6, 30, "n:"));
        UNSCOPED_INFO("n: before the 2:" + listed);
        CHECK(has(7, 29, "n:"));
        UNSCOPED_INFO("nothing when not asked");
        REQUIRE(service.inlayHints(script, false).empty());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a script mid-edit is answered about the rest of it", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        REQUIRE(sig.found);
        REQUIRE(sig.label.find("llSay(") != std::string::npos);
        CHECK(sig.active == 1);
        CHECK_FALSE(service.parsed());
        CHECK(service.understood());
        const std::string in_scope = names(script, 7, 17);
        UNSCOPED_INFO("the local before, in scope:" + in_scope);
        REQUIRE(in_scope.find(" before ") != std::string::npos);
        UNSCOPED_INFO("the parameter:" + in_scope);
        REQUIRE(in_scope.find(" total ") != std::string::npos);
        UNSCOPED_INFO("the global:" + in_scope);
        REQUIRE(in_scope.find(" count ") != std::string::npos);

        // The bracket just typed, and a comma just typed.
        sig = service.signature(head + "        llSay(" + tail, 7, 14);
        REQUIRE(sig.found);
        CHECK(sig.active == 0);
        sig = service.signature(head + "        llSay(0," + tail, 7, 16);
        REQUIRE(sig.found);
        CHECK(sig.active == 1);
        sig = service.signature(head + "        llSay(0, \"hel" + tail, 7, 20);
        {
            INFO("inside a string not yet closed");
            REQUIRE(sig.found);
            CHECK(sig.active == 1);
        }
        sig = service.signature(head + "        half(" + tail, 7, 13);
        REQUIRE(sig.found);
        CHECK(sig.label == "float half(integer n)");

        // A name alone, and a statement without its semicolon.
        REQUIRE(names(head + "        bef" + tail, 7, 8).find(" before ") != std::string::npos);
        REQUIRE(names(head + "        llSay(0, before)" + tail, 7, 17).find(" before ") != std::string::npos);

        // What the check's other questions answer of a text that does not
        // parse: the rest of it, rather than nothing.
        script = head + "        llSay(0, be" + tail;
        REQUIRE(service.outline(script).size() == 4);
        const std::vector<ALScriptSemanticToken> tokens = service.semanticTokens(script);
        bool before_named = false;
        for (const ALScriptSemanticToken& token : tokens)
        {
            before_named = before_named || (token.span.line == 6 && token.kind == ALScriptSymbolKind::Variable);
        }
        UNSCOPED_INFO("the names before the break");
        CHECK(before_named);
        REQUIRE_FALSE(service.inlayHints(head + "        llSay(0, \"x\");\n        llOwnerSay(" + tail, true).empty());

        // A function being declared at the top, before the states.
        script = "integer count = 0;\nfoo(\ndefault\n{\n    state_entry()\n    {\n        llSay(0, (string)count);\n    }\n}\n";
        const ALScriptHover hover = service.hover(script, 6, 26);
        {
            INFO("a name below a broken declaration");
            REQUIRE(hover.found);
            CHECK(hover.label == "integer count");
        }
        REQUIRE(service.references(script, 6, 26).references.size() == 2);

        // A block left open at the end.
        script = head + "        llSay(0, before);\n";
        UNSCOPED_INFO("closed at the end");
        REQUIRE(names(script, 7, 17).find(" before ") != std::string::npos);

        // Nothing to mend from.
        {
            INFO("nonsense is not understood");
            REQUIRE(service.outline("}}} ((( ;;; \"").empty());
            CHECK_FALSE(service.understood());
        }
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a syntax error says what is missing and where or what was unexpected", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        CHECK(p.message == std::string("Missing ')'."));
        CHECK(p.key == "LSLSyntaxMissing");
        REQUIRE(p.args.size() == 1);
        CHECK(p.args[0] == "')'");
        {
            INFO("on the string's closing quote, not the next line's brace");
            CHECK(p.line == 4);
            CHECK(p.column == 20);
            CHECK(p.endColumn == 21);
        }
        // A statement not ended.
        p = first_error(head + "        llSay(0, \"hi\")\n        llOwnerSay(\"x\");\n    }\n}\n");
        CHECK(p.message == std::string("Missing ';'."));
        {
            INFO("after the call");
            CHECK(p.line == 4);
            CHECK(p.column == 21);
        }
        p = first_error(head + "        integer x = 5 // five\n    }\n}\n");
        UNSCOPED_INFO("past a comment");
        CHECK(p.message == std::string("Missing ';'."));
        {
            INFO("on the number");
            CHECK(p.line == 4);
            CHECK(p.column == 20);
        }
        // Something that is no statement's next word.
        p = first_error("integer x = 1;\nfoo(\ndefault\n{\n    state_entry() { }\n}\n");
        UNSCOPED_INFO("unexpected, by what it is: " + p.message);
        CHECK((p.key == "LSLSyntaxUnexpected" || p.key == "LSLSyntaxUnexpectedWanted"));
        UNSCOPED_INFO("said as written: " + p.message);
        REQUIRE(p.message.find("'default'") != std::string::npos);
        {
            INFO("no bison left in it: " + p.message);
            REQUIRE(p.message.find("syntax error") == std::string::npos);
            REQUIRE(p.message.find("STATE_DEFAULT") == std::string::npos);
        }
    }
    TEST_CASE_METHOD(allslservice_data, "allslservice: a text checked again says the same after other questions or targets", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
        const std::string script = "integer unused;\nlist g = [1, 2];\ninteger h = g;\ndefault\n{\n    state_entry()\n    {\n        integer never;\n    }\n}\n";
        const ALScriptProblems first = service.check(script, true);
        REQUIRE_FALSE(first.empty());
        service.outline(script);
        service.hover(script, 0, 9);
        const ALScriptProblems again = service.check(script, true);
        UNSCOPED_INFO("said once, the second time too");
        CHECK(said(again) == said(first));
        const ALScriptProblems lso = service.check(script, false);
        const ALScriptProblems back = service.check(script, true);
        UNSCOPED_INFO("and after the other target, nothing of it left");
        CHECK(said(back) == said(first));
        UNSCOPED_INFO("the other target, asked twice, the same");
        CHECK(said(service.check(script, false)) == said(lso));
    }
    TEST_CASE_METHOD(allslservice_data, "allslservice: a parameter without its type is named; the unexpected is said plainly", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        CHECK(p.message ==
                      std::string("The parameter 'object' needs its type before it: integer, float, string, key, vector, rotation or list."));
        CHECK(p.key == "LSLParameterUntyped");
        REQUIRE(p.args.size() == 1);
        CHECK(p.args[0] == "object");
        {
            INFO("on the name, whole");
            CHECK(p.line == 0);
            CHECK(p.column == 33);
            CHECK(p.endLine == 0);
            CHECK(p.endColumn == 39);
        }
        p = first_error("f(integer a, b) { }\n" + tail);
        {
            INFO("after a comma: " + p.message);
            REQUIRE(p.key == "LSLParameterUntyped");
            CHECK(p.args[0] == "b");
            CHECK(p.column == 13);
        }
        p = first_error("default\n{\n    touch_start(total)\n    {\n    }\n}\n");
        {
            INFO("an event's: " + p.message);
            REQUIRE(p.key == "LSLParameterUntyped");
            CHECK(p.args[0] == "total");
            CHECK(p.line == 2);
        }
        // A name where none can go, not after a bracket.
        p = first_error("default\n{\n    state_entry()\n    {\n        llSay(0, \"x\") y;\n    }\n}\n");
        UNSCOPED_INFO("not a parameter: " + p.message);
        CHECK(p.key != "LSLParameterUntyped");
        {
            INFO("said plainly: " + p.message);
            REQUIRE(p.message.find("Unexpected a") == std::string::npos);
            REQUIRE(p.message.find("syntax error") == std::string::npos);
        }
    }
    TEST_CASE_METHOD(allslservice_data, "allslservice: a definitions file with lines the engine cannot read loads the rest", "[allslservice]")
    {
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
        // Tailslide's table is the process's and only ever added to: what
        // the file declares stays for every case after this one.
        ll_test::StateGuard::accept("Tailslide's builtins");
        std::error_code gone;
        std::filesystem::remove(fsyspath(path), gone);
        UNSCOPED_INFO("loaded: " + why);
        CHECK(read);
        const std::string head = "default\n{\n    state_entry()\n    {\n";
        const std::string tail = "    }\n}\n";
        const ALScriptProblems kept = service.check(head + "        llOwnerSay((string)(llAlchemyProbe(1) + llAlchemyAfter(\"x\")));\n" + tail, true);
        UNSCOPED_INFO("the lines it could read are known: " + said(kept));
        CHECK(errors(kept) == size_t(0));
        const ALScriptProblems left = service.check(head + "        llAlchemyBadReturn();\n" + tail, true);
        UNSCOPED_INFO("a line it could not read is not: " + said(left));
        CHECK(errors(left) > 0);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a definitions file under a folder named outside the ANSI code page loads", "[allslservice]")
    {
        // The grid's definitions are kept in the cache, under the user's
        // profile, whose name may be in any script: the path is UTF-8, and
        // on Windows the engine opened it as the ANSI code page read it.
        const fsyspath dir = std::filesystem::temp_directory_path() / fsyspath("al_builtins_\xc3\xa9\xe6\x97\xa5_" + LLUUID::generateNewID().asString());
        std::error_code made;
        std::filesystem::create_directories(dir, made);
        UNSCOPED_INFO("folder made: " + made.message());
        REQUIRE_FALSE(made);
        const std::string path = fsyspath(dir / "builtins.txt").string();
        {
            llofstream out(path, std::ios::binary);
            out << "integer llAlchemyWide(integer a)\n";
        }
        std::string why;
        const bool  read = service.loadBuiltins(path, why);
        // Tailslide's table is the process's and only ever added to: what
        // the file declares stays for every case after this one.
        ll_test::StateGuard::accept("Tailslide's builtins");
        std::error_code gone;
        std::filesystem::remove_all(dir, gone);
        UNSCOPED_INFO("loaded: " + why);
        CHECK(read);
        const ALScriptProblems kept = service.check("default\n{\n    state_entry()\n    {\n        llOwnerSay((string)llAlchemyWide(1));\n    }\n}\n", true);
        UNSCOPED_INFO("what it defines is known: " + said(kept));
        CHECK(errors(kept) == size_t(0));
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a broken text is mended once whole and once at the caret and no more", "[allslservice]")
    {
        const std::string broken = "integer count;\ndefault {\n    state_entry() {\n        llSay(0, (string)count\n    }\n}\n";
        const size_t      before = service.mendings();
        // What the check asks after it, whole; a call's parameters, at the
        // caret.
        service.outline(broken);
        service.signature(broken, 3, 20);
        const size_t twice = service.mendings();
        UNSCOPED_INFO("one each");
        CHECK(twice - before == size_t(2));
        for (int turn = 0; turn < 3; ++turn)
        {
            service.outline(broken);
            service.signature(broken, 3, 20);
            service.hover(broken, 3, 20);
            service.symbols(broken, 3, 20);
        }
        UNSCOPED_INFO("and no more, turn and turn about");
        CHECK(service.mendings() == twice);
        service.signature(broken, 3, 12);
        UNSCOPED_INFO("another place is mended again");
        CHECK(service.mendings() == twice + 1);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: lines passed over get no semantic tokens and their problems no fixes", "[allslservice]")
    {
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
        UNSCOPED_INFO("the include's unused function said");
        CHECK(helper_said);
        UNSCOPED_INFO("but offered nothing");
        CHECK_FALSE(helper_fixed);
        UNSCOPED_INFO("the script's own offered its fix");
        CHECK(own_fixed);
        bool in_include = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            in_include |= token.span.line <= 3;
        }
        UNSCOPED_INFO("no names coloured in the include");
        CHECK_FALSE(in_include);
        service.setPassedOver({});
        bool coloured = false;
        for (const ALScriptSemanticToken& token : service.semanticTokens(text))
        {
            coloured |= token.span.line <= 3;
        }
        UNSCOPED_INFO("with nothing passed over, coloured");
        CHECK(coloured);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: another region's builtins: new ones known after and none declared twice", "[allslservice]")
    {
        const std::string base = std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt";
        std::string       why;
        const bool        first = service.loadBuiltins(base, why);
        UNSCOPED_INFO("the first region's: " + why);
        REQUIRE(first);
        const std::string script = "default\n{\n    state_entry()\n    {\n        integer n = llBrandNewThing(1);\n        llOwnerSay((string)n);\n    }\n}\n";
        bool unknown = false;
        for (const ALScriptProblem& problem : service.check(script))
        {
            unknown |= problem.message.find("llBrandNewThing") != std::string::npos;
        }
        UNSCOPED_INFO("not yet known");
        CHECK(unknown);
        // A region with one function more.
        std::string text;
        {
            llifstream in(base, std::ios::in | std::ios::binary);
            std::ostringstream all;
            all << in.rdbuf();
            text = all.str();
        }
        // a name of its own, so two runs at once never write one file
        const std::string path =
            (std::filesystem::temp_directory_path() / ("alscript-test-builtins-newer-" + LLUUID::generateNewID().asString() + ".txt")).string();
        {
            llofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
            out << text << "integer llBrandNewThing( integer Value )\n";
        }
        const bool newer = service.loadBuiltins(path, why);
        // Tailslide's table is the process's and only ever added to: what
        // the file declares stays for every case after this one.
        ll_test::StateGuard::accept("Tailslide's builtins");
        LLFile::remove(path);
        UNSCOPED_INFO("loaded: " + why);
        CHECK(newer);
        unknown = false;
        for (const ALScriptProblem& problem : service.check(script))
        {
            unknown |= problem.message.find("llBrandNewThing") != std::string::npos;
        }
        UNSCOPED_INFO("known now");
        CHECK_FALSE(unknown);
        // Nothing twice: an old one still one declaration.
        const ALScriptSignature said = service.signature("default { state_entry() { llAbs( } }\n", 0, 32);
        {
            INFO("an old function as it was");
            REQUIRE(said.found);
            REQUIRE(said.label.find("llAbs") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: every template for a new LSL script checks without an error", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
        llifstream in(std::string(AL_SCRIPT_TEMPLATES_DIR) + "/lsl.xml", std::ios::in | std::ios::binary);
        LLSD       templates;
        CHECK(in.is_open());
        CHECK(LLSDSerialize::fromXML(templates, in) != LLSDParser::PARSE_FAILURE);
        REQUIRE(templates.isArray());
        REQUIRE(templates.size() > 0);
        for (LLSD::array_const_iterator it = templates.beginArray(); it != templates.endArray(); ++it)
        {
            const std::string      name     = (*it)["name"].asString();
            const ALScriptProblems problems = service.check((*it)["body"].asString());
            CAPTURE(name);
            REQUIRE(service.parsed());
            INFO(said(problems));
            REQUIRE(errors(problems) == size_t(0));
        }
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a handler or state begun in an open block is a missing brace before it", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
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
        CHECK(p.message == std::string("Missing '}'."));
        CHECK(p.key == "LSLSyntaxMissing");
        REQUIRE(p.args.size() == 1);
        CHECK(p.args[0] == "'}'");
        {
            INFO("on the semicolon before it");
            CHECK(p.line == 4);
            CHECK(p.column == 34);
            CHECK(p.endColumn == 35);
        }
        // One without, its brace on the next line.
        p = first_error(open + "    touch_start()\n    {\n    }\n}\n");
        UNSCOPED_INFO("a head without parameters");
        CHECK(p.message == std::string("Missing '}'."));
        {
            INFO("on the same semicolon");
            CHECK(p.line == 4);
            CHECK(p.column == 34);
        }
        // A state after one not closed.
        p = first_error("default\n{\n    state_entry()\n    {\n    }\n\nstate other\n{\n    state_entry()\n    {\n    }\n}\n");
        UNSCOPED_INFO("a state after one not closed");
        CHECK(p.message == std::string("Missing '}'."));
        {
            INFO("after the handler's");
            CHECK(p.line == 4);
            CHECK(p.column == 4);
        }
        // A handler's own head wrong where handlers go is not one.
        p = first_error("default\n{\n    touch_start(integer)\n    {\n    }\n}\n");
        {
            INFO("a head wrong in its state's braces: not a brace");
            CHECK(p.severity == ALScriptProblem::Severity::Error);
            CHECK(p.message != "Missing '}'.");
        }
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: SlLoopInvariantCall notes a loop check's invariant call; NOLINT by name", "[allslservice]")
    {
        UNSCOPED_INFO("builtins load: " + error);
        CHECK(loaded);
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
        UNSCOPED_INFO("three: each loop's once");
        REQUIRE(said.size() == size_t(3));
        {
            INFO("a note keyed as Tailslide's, by the function: " + said[0].message);
            CHECK(said[0].key == "LSLSlLoopInvariantCall");
            CHECK(said[0].severity == ALScriptProblem::Severity::Note);
            CHECK(said[0].line == 3);
            CHECK(said[0].args == std::vector<std::string>{ "llGetListLength", "integer", "3", "4" });
            CHECK(said[0].source == ALScriptProblem::Source::Lint);
        }
        {
            INFO("the outer call of two: " + said[2].message);
            CHECK(said[2].line == 5);
            REQUIRE_FALSE(said[2].args.empty());
            CHECK(said[2].args[0] == "llGetListLength");
        }
        UNSCOPED_INFO("a NOLINT answers to the rule's name");
        CHECK(ALScriptFixes::suppressed(said[0], "    for (i = 0; i < llGetListLength(gL); ++i) // NOLINT(SlLoopInvariantCall)", "", false));

        said = found("list gL = [1, 2];\n"
                     "grow() { gL += [0]; }\n"
                     "default { state_entry() {\n"
                     "    integer i; list l;\n"
                     "    for (i = 0; i < llGetListLength(l); ++i) l += [i];\n"
                     "    for (i = 0; i < llGetListLength(gL) && i < 10; ++i) grow();\n"
                     "    while (llGetUnixTime() < 5) ++i;\n"
                     "} }\n");
        UNSCOPED_INFO("not where the loop changes what it is given, itself or by a function, nor for a call that reads the world");
        REQUIRE(said.empty());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a shape-only outline drops details and mends apart from the kept tree", "[allslservice]")
    {
        UNSCOPED_INFO("builtins loaded: " + error);
        CHECK(loaded);
        const std::string script =
            "integer count = 0;\n"
            "float half(integer n) { return n / 2.0; }\n"
            "default\n"
            "{\n"
            "    touch_start(integer total)\n"
            "    {\n"
            "    }\n"
            "}\n";
        const std::vector<ALScriptOutlineEntry> whole = service.outline(script);
        const std::vector<ALScriptOutlineEntry> shape = service.outline(script, false);
        REQUIRE(shape.size() == whole.size());
        for (size_t i = 0; i < shape.size(); ++i)
        {
            CAPTURE(whole[i].name);
            REQUIRE(shape[i].name == whole[i].name);
            REQUIRE(shape[i].kind == whole[i].kind);
            REQUIRE(shape[i].depth == whole[i].depth);
            REQUIRE(shape[i].span == whole[i].span);
            REQUIRE(shape[i].nameSpan == whole[i].nameSpan);
            UNSCOPED_INFO("no words of its declaration: " + shape[i].detail);
            REQUIRE(shape[i].detail.empty());
        }
        // Another text's shape, broken: mended apart from the tree kept.
        const std::string broken = "integer count;\ndefault {\n    state_entry() {\n        llSay(0, (string)count\n    }\n}\n";
        const size_t      before = service.mendings();
        const std::vector<ALScriptOutlineEntry> mended = service.outline(broken, false);
        {
            INFO("mended to its shape");
            REQUIRE(mended.size() == 3);
            CHECK(mended[0].name == "count");
            CHECK(mended[2].name == "state_entry");
            CHECK(mended[2].depth == 1);
        }
        UNSCOPED_INFO("the tree kept mended nothing");
        CHECK(service.mendings() == before);
        UNSCOPED_INFO("and still answers for its own text");
        CHECK(service.parsed());
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: an action asked before a line's start or past its end is held to the line", "[allslservice]")
    {
        CHECK(loaded);
        const std::string script = "default\n{\n    state_entry()\n    {\n        llSay(0, (string)(1 + 2));\n    }\n}\n";
        CHECK_NOTHROW(service.actions(script, 4, -1, 4, 5));
        CHECK_NOTHROW(service.actions(script, 4, 0, 4, 400));
        CHECK_NOTHROW(service.actions(script, 40, 0, 40, 4));
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: an undeclared name is changed only to one of the kind its place looks up", "[allslservice]")
    {
        CHECK(loaded);
        const std::string script = "integer helper(integer n) { return n; }\n"
                                   "integer count;\n"
                                   "default\n{\n    touch_start(integer total)\n    {\n"
                                   "        touch_strt(1);\n"
                                   "        helpr(2);\n"
                                   "        cont = 3;\n"
                                   "    }\n}\n";
        const ALScriptProblems problems = service.check(script);
        const auto             fixesOf  = [&problems](const std::string& name) {
            std::string out;
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.key == "LSLUndeclared" && problem.args.size() == 1 && problem.args[0] == name)
                {
                    for (const ALScriptFix& fix : problem.fixes)
                    {
                        out += "'" + fix.title + "' ";
                    }
                }
            }
            return out;
        };
        UNSCOPED_INFO("said: " + said(problems));
        UNSCOPED_INFO("a call is not changed to the handler it is in: " + fixesOf("touch_strt"));
        CHECK(fixesOf("touch_strt").find("touch_start") == std::string::npos);
        UNSCOPED_INFO("a call is changed to a function: " + fixesOf("helpr"));
        CHECK(fixesOf("helpr").find("to 'helper'") != std::string::npos);
        UNSCOPED_INFO("a variable to a variable: " + fixesOf("cont"));
        CHECK(fixesOf("cont").find("to 'count'") != std::string::npos);
    }

    TEST_CASE_METHOD(allslservice_data, "allslservice: a label declared after a jump is offered at the jump", "[allslservice]")
    {
        CHECK(loaded);
        // The caret in the jump's name, the label on a later line. (Without
        // the jump's semicolon Tailslide's own recovery takes the label's
        // line with the jump's, and no scope rule can offer what is not in
        // the tree.)
        const std::string script = "default\n{\n    state_entry()\n    {\n        integer i;\n        jump don;\n        @done;\n        integer later;\n        i = later;\n    }\n}\n";
        std::string       names;
        for (const ALScriptCompletion& c : service.symbols(script, 5, 16))
        {
            names += " " + c.text;
        }
        names += " ";
        UNSCOPED_INFO("offered:" + names);
        CHECK(names.find(" done ") != std::string::npos);
        UNSCOPED_INFO("a local declared after the caret is not");
        CHECK(names.find(" later ") == std::string::npos);
        CHECK(names.find(" i ") != std::string::npos);
    }
}
