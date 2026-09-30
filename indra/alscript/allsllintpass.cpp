/**
 * @file allsllintpass.cpp
 * @brief The studio's own LSL lints, beside Tailslide's, from ALScriptLintPass's table.
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

#include "allsllintpass.h"

#include "allsleffects.h"
#include "allsltraits.h"
#include "alscriptlintpass.h"

#include <tailslide/tailslide.hh>

#include <functional>

using namespace Tailslide;

namespace
{
    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    bool isNull(LSLASTNode* node)
    {
        return !node || node->getNodeType() == NODE_NULL;
    }

    // Every node under one, and it, in the tree's order.
    void walk(LSLASTNode* node, const std::function<void(LSLASTNode*)>& each)
    {
        if (isNull(node))
        {
            return;
        }
        each(node);
        for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
        {
            walk(child, each);
        }
    }

    const char* typeName(LSLIType type)
    {
        switch (type)
        {
            case LST_INTEGER: return "integer";
            case LST_FLOATINGPOINT: return "float";
            case LST_STRING: return "string";
            case LST_KEY: return "key";
            case LST_VECTOR: return "vector";
            case LST_QUATERNION: return "rotation";
            case LST_LIST: return "list";
            default: return nullptr;
        }
    }

    void problem(LSLASTNode* at, const char* key, const char* english, std::vector<std::string> args, const char* name, ALScriptProblems& out)
    {
        const ALScriptLintPass::Rule* rule = ALScriptLintPass::rule(name);
        const YYLTYPE*                loc  = at->getLoc();
        ALScriptProblem               p;
        p.severity  = rule->severity;
        p.source    = ALScriptProblem::Source::Lint;
        p.line      = zeroBased(loc->first_line);
        p.column    = zeroBased(loc->first_column);
        p.endLine   = zeroBased(loc->last_line);
        p.endColumn = zeroBased(loc->last_column);
        p.code      = name;
        p.key       = key;
        p.message   = ALScriptProblem::fill(english, args);
        p.args      = std::move(args);
        out.push_back(std::move(p));
    }

    // SlLoopInvariantCall: a call in a loop's check to a function the
    // definitions call pure, whose arguments nothing in the loop changes --
    // llGetListLength(l) where the loop leaves l be -- worked out again on
    // every turn when once before it would do.
    void loopInvariantCalls(LSLScript* script, const ALLSLEffects& effects, ALScriptProblems& out)
    {
        walk(script, [&](LSLASTNode* node) {
            LSLExpression* check = nullptr;
            switch (node->getNodeSubType())
            {
                case NODE_FOR_STATEMENT: check = static_cast<LSLForStatement*>(node)->getCheckExpr(); break;
                case NODE_WHILE_STATEMENT: check = static_cast<LSLWhileStatement*>(node)->getCheckExpr(); break;
                case NODE_DO_STATEMENT: check = static_cast<LSLDoStatement*>(node)->getCheckExpr(); break;
                default: return;
            }
            if (isNull(check))
            {
                return;
            }
            const ALLSLEffects::Writes changed = effects.of(node);
            walk(check, [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    return;
                }
                LSLSymbol* symbol = static_cast<LSLFunctionExpression*>(inner)->getIdentifier()->getSymbol();
                if (!symbol || symbol->getSubType() != SYM_BUILTIN || !ALLSLTraits::pure(symbol->getName()) ||
                    !ALLSLTraits::sideEffectFree(inner))
                {
                    return;
                }
                bool steady = true;
                walk(inner, [&](LSLASTNode* read) {
                    if (read->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        steady = steady && !changed.writes(static_cast<LSLLValueExpression*>(read)->getIdentifier()->getSymbol());
                    }
                });
                // One inside another is said with the outer.
                LSLASTNode* up = inner->getParent();
                while (up != check && up && up->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
                {
                    up = up->getParent();
                }
                if (!steady || up != check)
                {
                    return;
                }
                // For the fix, where the loop stands in a block: the local's
                // type, and where the loop begins, before which it goes.
                std::vector<std::string> args{ symbol->getName() };
                const char*              type = typeName(inner->getIType());
                if (type && node->getParent() && node->getParent()->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                {
                    const YYLTYPE* loop = node->getLoc();
                    args.emplace_back(type);
                    args.push_back(std::to_string(zeroBased(loop->first_line)));
                    args.push_back(std::to_string(zeroBased(loop->first_column)));
                }
                problem(inner, "LSLSlLoopInvariantCall",
                        "[1] is worked out again on every turn of the loop, though nothing in the loop changes what it is given: a "
                        "local set before the loop works it out once",
                        std::move(args), "SlLoopInvariantCall", out);
            });
        });
    }
}

// static
void ALLSLLintPass::check(LSLScript* script, ALScriptProblems& out)
{
    if (!script)
    {
        return;
    }
    const ALLSLEffects effects(script);
    loopInvariantCalls(script, effects, out);
}
