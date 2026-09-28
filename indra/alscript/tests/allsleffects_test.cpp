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

#include "linden_common.h"

#include "../allsleffects.h"
#include "../allslservice.h"

#include "../test/lltut.h"

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

namespace tut
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
                    fail("the builtins did not load: " + error);
                }
            }
        }
    };

    typedef test_group<allsleffects_data> allsleffects_group;
    typedef allsleffects_group::object    allsleffects_object;
    allsleffects_group                    allsleffects_instance("allsleffects");

    template<> template<>
    void allsleffects_object::test<1>()
    {
        set_test_name("a function's writes are the globals it assigns, itself and through what it calls, round a loop of calls too; a library call that is not pure makes it impure");
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
        ensure("parsed", p.script != nullptr);
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
        ensure("a writes g", is("a", { "g" }, false));
        ensure("b writes h, and g through a", is("b", { "g", "h" }, false));
        ensure("c writes only its own local, which no caller sees", is("c", {}, false));
        ensure("d says something: impure", is("d", {}, true));
        ensure("e's library call is pure", is("e", { "k" }, false));
        ensure("f through b through a", is("f", { "g", "h" }, false));
        ensure("r and s call each other, and both write k", is("r", { "k" }, false) && is("s", { "k" }, false));
        ensure("one the script does not define: impure", effects.ofFunction(nullptr).impure);
    }

    template<> template<>
    void allsleffects_object::test<2>()
    {
        set_test_name("a call may run first where what runs before it -- a right operand, earlier arguments, a list's other elements -- changes nothing and reads nothing it changes");
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
        ensure("parsed", p.script != nullptr);
        const ALLSLEffects                        effects(p.script);
        const std::vector<LSLFunctionExpression*> bumps = p.calls("bump");
        const std::vector<LSLFunctionExpression*> sames = p.calls("same");
        ensure_equals("the calls", bumps.size(), size_t(11));
        const auto first = [&](LSLFunctionExpression* call) { return effects.mayRunFirst(statementOf(call), call); };
        ensure("0: the right operand runs first", first(bumps[0]));
        ensure("1: g before it, and it writes g", !first(bumps[1]));
        ensure("2: h before it, unwritten", first(bumps[2]));
        ensure("3: an impure call before it", !first(bumps[3]));
        ensure("4: a pure call before it", first(bumps[4]));
        ensure("5: an increment before it", !first(bumps[5]));
        ensure("6: an earlier argument reads what it writes", !first(bumps[6]));
        ensure("7: the first argument", first(bumps[7]));
        ensure("8: a list's other element, unwritten", first(bumps[8]));
        ensure("9: a list's other element it writes, the order not known", !first(bumps[9]));
        ensure("10: its own argument writes what runs before it", !first(sames[0]));
        ensure("11: what reads it runs after", first(sames[1]));
        ensure("12: an assignment's store is after its value", first(bumps[10]));
        ensure_equals("before the right operand: nothing", ALLSLEffects::before(statementOf(bumps[0]), bumps[0]).size(), size_t(0));
        ensure_equals("before the left: the right", ALLSLEffects::before(statementOf(bumps[1]), bumps[1]).size(), size_t(1));

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
        ensure("parsed", q.script != nullptr);
        const ALLSLEffects quiet(q.script);
        const auto         firstQ = [&](LSLFunctionExpression* call) { return quiet.mayRunFirst(statementOf(call), call); };
        ensure("a quiet call before a quiet one", firstQ(q.calls("same")[0]));
        ensure("a call that reads before one that writes", !firstQ(q.calls("bump")[0]));
        ensure("a call that writes before a quiet one", !firstQ(q.calls("same")[1]));
    }
}
