/**
 * @file allsleffects_test.cpp
 * @brief Tests for ALLSLEffects.
 *
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
 */

#include "linden_common.h"

#include "../lsl/allsleffects.h"
#include "../lsl/allslservice.h"

#include <catch2/catch_test_macros.hpp>

#include <tailslide/tailslide.hh>

#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace Tailslide;

namespace
{
    // A script parsed and its symbols found, as the inliner has it.
    struct Parsed
    {
        std::unique_ptr<ScopedScriptParser> parser = std::make_unique<ScopedScriptParser>(nullptr);
        LSLScript*                          script = nullptr;

        explicit Parsed(const std::string& text)
        {
            script = parser->parseLSLBytes(text.data(), static_cast<int>(text.size()));
            if (script && !parser->logger.getErrors())
            {
                script->collectSymbols();
                script->determineTypes();
            }
        }

        LSLSymbol* function(const char* name) const { return script->getSymbolTable()->lookup(name, SYM_FUNCTION); }
        LSLSymbol* global(const char* name) const { return script->getSymbolTable()->lookup(name, SYM_VARIABLE); }

        // The calls of a function, in the order written.
        std::vector<LSLFunctionExpression*> calls(const char* name) const
        {
            std::vector<LSLFunctionExpression*> out;
            // Depth first, children in order.
            const std::function<void(LSLASTNode*)> walk = [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_FUNCTION_EXPRESSION &&
                    !strcmp(static_cast<LSLFunctionExpression*>(node)->getIdentifier()->getName(), name))
                {
                    out.push_back(static_cast<LSLFunctionExpression*>(node));
                }
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    walk(child);
                }
            };
            walk(script);
            return out;
        }
    };

    // The statement a node stands in.
    LSLASTNode* statementOf(LSLASTNode* node)
    {
        while (node && node->getNodeType() != NODE_STATEMENT)
        {
            node = node->getParent();
        }
        return node;
    }
}

namespace
{
    struct allsleffects_data
    {
        allsleffects_data()
        {
            static bool loaded = false;
            if (!loaded)
            {
                ALLSLService service;
                std::string  error;
                loaded = service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
                if (!loaded)
                {
                    FAIL("the builtins did not load: " + error);
                }
            }
        }
    };

    TEST_CASE_METHOD(allsleffects_data, "allsleffects: a function's writes and impurity take in its callees even round a loop", "[allsleffects]")
    {
        const Parsed p("integer g;\ninteger h;\ninteger k;\n"
                       "a() { g = 1; }\n"
                       "b() { a(); h++; }\n"
                       "c() { integer local; local = 3; }\n"
                       "d() { llOwnerSay(\"x\"); }\n"
                       "e() { k = llAbs(k); }\n"
                       "f() { b(); }\n"
                       "r() { k += 2; s(); }\n"
                       "s() { r(); }\n"
                       "default { state_entry() { f(); } }\n");
        REQUIRE(p.script != nullptr);
        const ALLSLEffects effects(p.script);
        const auto         is = [&](const char* name, std::initializer_list<const char*> globals, bool impure) {
            const ALLSLEffects::Writes& w = effects.ofFunction(p.function(name));
            bool                        same = w.variables.size() == globals.size() && w.impure == impure;
            for (const char* g : globals)
            {
                same = same && w.writes(p.global(g));
            }
            return same;
        };
        CHECK(is("a", { "g" }, false));
        UNSCOPED_INFO("b writes h, and g through a");
        CHECK(is("b", { "g", "h" }, false));
        UNSCOPED_INFO("c writes only its own local, which no caller sees");
        CHECK(is("c", {}, false));
        UNSCOPED_INFO("d says something: impure");
        CHECK(is("d", {}, true));
        UNSCOPED_INFO("e's library call is pure");
        CHECK(is("e", { "k" }, false));
        UNSCOPED_INFO("f through b through a");
        CHECK(is("f", { "g", "h" }, false));
        {
            INFO("r and s call each other, and both write k");
            CHECK(is("r", { "k" }, false));
            CHECK(is("s", { "k" }, false));
        }
        UNSCOPED_INFO("one the script does not define: impure");
        CHECK(effects.ofFunction(nullptr).impure);
    }

    TEST_CASE_METHOD(allsleffects_data, "allsleffects: a call may go first past what changes nothing nor reads its writes", "[allsleffects]")
    {
        const Parsed p("integer g;\ninteger h;\n"
                       "integer bump() { return ++g; }\n"
                       "integer same(integer v) { return v; }\n"
                       "integer pair(integer a, integer b) { return a + b; }\n"
                       "default { state_entry() {\n"
                       "    integer x; integer y; list l;\n"
                       "    x = g + bump();\n"                    // 0: bump is the right operand, run first
                       "    x = bump() + g;\n"                    // 1: g runs before, and bump writes it
                       "    x = bump() + h;\n"                    // 2: h runs before, unwritten
                       "    x = bump() + (integer)llFrand(2.0);\n" // 3: an impure call runs before
                       "    x = bump() + llAbs(h);\n"             // 4: a pure one
                       "    x = bump() + (h++);\n"                // 5: an increment runs before
                       "    x = pair(g, bump());\n"               // 6: an earlier argument reads g
                       "    x = pair(bump(), g);\n"               // 7: arguments left to right: nothing before
                       "    l = [h, bump()];\n"                   // 8: a list's element, unwritten
                       "    l = [bump(), g];\n"                   // 9: a list's element, written
                       "    x = same(y++) + y;\n"                 // 10: its argument writes what runs before
                       "    x = y + same(y++);\n"                 // 11: and here after
                       "    g = bump();\n"                        // 12: the store is after
                       "} }\n");
        REQUIRE(p.script != nullptr);
        const ALLSLEffects                        effects(p.script);
        const std::vector<LSLFunctionExpression*> bumps = p.calls("bump");
        const std::vector<LSLFunctionExpression*> sames = p.calls("same");
        REQUIRE(bumps.size() == size_t(11));
        REQUIRE(sames.size() == size_t(2));
        const auto first = [&](LSLFunctionExpression* call) { return effects.mayRunFirst(statementOf(call), call); };
        UNSCOPED_INFO("0: the right operand runs first");
        CHECK(first(bumps[0]));
        UNSCOPED_INFO("1: g before it, and it writes g");
        CHECK_FALSE(first(bumps[1]));
        UNSCOPED_INFO("2: h before it, unwritten");
        CHECK(first(bumps[2]));
        UNSCOPED_INFO("3: an impure call before it");
        CHECK_FALSE(first(bumps[3]));
        UNSCOPED_INFO("4: a pure call before it");
        CHECK(first(bumps[4]));
        UNSCOPED_INFO("5: an increment before it");
        CHECK_FALSE(first(bumps[5]));
        UNSCOPED_INFO("6: an earlier argument reads what it writes");
        CHECK_FALSE(first(bumps[6]));
        UNSCOPED_INFO("7: the first argument");
        CHECK(first(bumps[7]));
        UNSCOPED_INFO("8: a list's other element, unwritten");
        CHECK(first(bumps[8]));
        UNSCOPED_INFO("9: a list's other element it writes, the order not known");
        CHECK_FALSE(first(bumps[9]));
        UNSCOPED_INFO("10: its own argument writes what runs before it");
        CHECK_FALSE(first(sames[0]));
        UNSCOPED_INFO("11: what reads it runs after");
        CHECK(first(sames[1]));
        UNSCOPED_INFO("12: an assignment's store is after its value");
        CHECK(first(bumps[10]));
        UNSCOPED_INFO("before the right operand: nothing");
        REQUIRE(ALLSLEffects::before(statementOf(bumps[0]), bumps[0]).size() == size_t(0));
        UNSCOPED_INFO("before the left: the right");
        REQUIRE(ALLSLEffects::before(statementOf(bumps[1]), bumps[1]).size() == size_t(1));

        // Calls of the script's own that change nothing, either side.
        const Parsed q("integer g;\n"
                       "integer bump() { return ++g; }\n"
                       "integer look() { return g; }\n"
                       "integer same(integer v) { return v; }\n"
                       "default { state_entry() {\n"
                       "    list l;\n"
                       "    l = [look(), same(1)];\n"  // same changes nothing, and look nothing
                       "    l = [look(), bump()];\n"   // bump writes what look reads
                       "    l = [bump(), same(2)];\n"  // same changes nothing, and bump does
                       "} }\n");
        REQUIRE(q.script != nullptr);
        const ALLSLEffects                        quiet(q.script);
        const std::vector<LSLFunctionExpression*> quiet_bumps = q.calls("bump");
        const std::vector<LSLFunctionExpression*> quiet_sames = q.calls("same");
        REQUIRE(quiet_bumps.size() == size_t(2));
        REQUIRE(quiet_sames.size() == size_t(2));
        const auto firstQ = [&](LSLFunctionExpression* call) { return quiet.mayRunFirst(statementOf(call), call); };
        UNSCOPED_INFO("a quiet call before a quiet one");
        CHECK(firstQ(quiet_sames[0]));
        UNSCOPED_INFO("a call that reads before one that writes");
        CHECK_FALSE(firstQ(quiet_bumps[0]));
        UNSCOPED_INFO("a call that writes before a quiet one");
        CHECK_FALSE(firstQ(quiet_sames[1]));
    }
    TEST_CASE_METHOD(allsleffects_data, "allsleffects: a function's changes past the script and globals it reads through calls", "[allsleffects]")
    {
        const Parsed p("integer g;\n"
                       "integer h;\n"
                       "key me() { return llGetKey(); }\n"
                       "say() { llOwnerSay(\"x\"); }\n"
                       "loud() { say(); }\n"
                       "integer look() { return g; }\n"
                       "integer near() { return look(); }\n"
                       "integer bump() { return ++h; }\n"
                       "default { state_entry() { me(); loud(); near(); bump(); } }\n");
        REQUIRE(p.script != nullptr);
        const ALLSLEffects effects(p.script);
        UNSCOPED_INFO("reading the world is past the script");
        CHECK(effects.ofFunction(p.function("me")).impure);
        UNSCOPED_INFO("and changes nothing there");
        CHECK_FALSE(effects.ofFunction(p.function("me")).changes);
        UNSCOPED_INFO("saying something changes it");
        CHECK(effects.ofFunction(p.function("say")).changes);
        UNSCOPED_INFO("through what it calls too");
        CHECK(effects.ofFunction(p.function("loud")).changes);
        UNSCOPED_INFO("a read of a global");
        CHECK(effects.reads(p.function("look"), p.global("g")));
        UNSCOPED_INFO("through what it calls");
        CHECK(effects.reads(p.function("near"), p.global("g")));
        UNSCOPED_INFO("not one it does not name");
        CHECK_FALSE(effects.reads(p.function("near"), p.global("h")));
        UNSCOPED_INFO("nor one only another function names");
        CHECK_FALSE(effects.reads(p.function("me"), p.global("g")));
        UNSCOPED_INFO("one it steps");
        CHECK(effects.reads(p.function("bump"), p.global("h")));
        UNSCOPED_INFO("a function the script does not define reads any");
        CHECK(effects.reads(nullptr, p.global("g")));
        LSLASTNode* body = p.script->getStates()->getChild(0);
        {
            INFO("a statement's, its calls'");
            CHECK(effects.of(body).changes);
            CHECK(effects.of(body).impure);
        }
    }
}
