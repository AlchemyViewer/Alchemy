/**
 * @file allslglobals.cpp
 * @brief The LSL optimizer's globals that are only scratch made locals.
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

#include "allslglobals.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <cstring>
#include <vector>

namespace ALLSLPasses
{
namespace
{
    class ScratchGlobals : public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            // Each function and handler, and the script's functions each
            // calls.
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION && global->getSymbol())
                {
                    mFunctions[global->getSymbol()] = global;
                    mOwners.push_back(global);
                }
            }
            for (LSLASTNode* state : *script->getStates())
            {
                for (LSLASTNode* handler = state->getChild(1) ? state->getChild(1)->getChild(0) : nullptr; handler; handler = handler->getNext())
                {
                    mOwners.push_back(handler);
                }
            }
            for (LSLASTNode* owner : mOwners)
            {
                auto& calls = mCalls[owner];
                eachNode(owner, [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
                    {
                        const auto found = mFunctions.find(n->getSymbol());
                        if (found != mFunctions.end())
                        {
                            calls.insert(found->second);
                        }
                    }
                });
            }
            std::vector<LSLASTNode*> globals;
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_VARIABLE && global->getSymbol())
                {
                    globals.push_back(global);
                }
            }
            for (LSLASTNode* global : globals)
            {
                localize(script, global);
            }
            return changes;
        }

    private:
        boost::unordered_flat_map<LSLSymbol*, LSLASTNode*>                              mFunctions;
        std::vector<LSLASTNode*>                                                        mOwners;
        boost::unordered_flat_map<LSLASTNode*, boost::unordered_flat_set<LSLASTNode*>> mCalls;

        static bool isStatement(LSLASTNode* n, LSLNodeSubType type)
        {
            return n && n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == type;
        }

        static bool mentions(LSLASTNode* root, LSLSymbol* sym)
        {
            bool found = false;
            eachNode(root, [&](LSLASTNode* n) { found = found || (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym); });
            return found;
        }

        // The functions a function or handler calls, however far on.
        boost::unordered_flat_set<LSLASTNode*> reached(LSLASTNode* from)
        {
            boost::unordered_flat_set<LSLASTNode*> seen;
            std::vector<LSLASTNode*>               todo(mCalls[from].begin(), mCalls[from].end());
            while (!todo.empty())
            {
                LSLASTNode* f = todo.back();
                todo.pop_back();
                if (seen.insert(f).second)
                {
                    todo.insert(todo.end(), mCalls[f].begin(), mCalls[f].end());
                }
            }
            return seen;
        }

        // `G = e`, whole, e naming nothing of G.
        static bool setsWhole(LSLASTNode* expr, LSLSymbol* sym)
        {
            if (!expr || expr->getNodeType() != NODE_EXPRESSION || static_cast<LSLExpression*>(expr)->getOperation() != OP_ASSIGN ||
                expr->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return false;
            }
            LSLASTNode* target = expr->getChild(0);
            return target->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(target)->getMember() &&
                   target->getSymbol() == sym && !mentions(expr->getChild(1), sym);
        }

        // Whether every read of G in a statement comes after G is set, on
        // every way there, from `set` (whether it is set as the statement
        // begins); where it does, `set` as it ends.
        static bool setBeforeRead(LSLASTNode* stmt, LSLSymbol* sym, bool& set)
        {
            if (!stmt || stmt->getNodeType() == NODE_NULL)
            {
                return true;
            }
            // An expression: set whole, or read only once set.
            const auto expression = [&](LSLASTNode* expr) {
                if (!expr || expr->getNodeType() == NODE_NULL)
                {
                    return true;
                }
                if (setsWhole(expr, sym))
                {
                    set = true;
                    return true;
                }
                return set || !mentions(expr, sym);
            };
            if (stmt->getNodeType() != NODE_STATEMENT)
            {
                return expression(stmt);
            }
            switch (stmt->getNodeSubType())
            {
                case NODE_COMPOUND_STATEMENT:
                    for (LSLASTNode* s = stmt->getChild(0); s; s = s->getNext())
                    {
                        if (!setBeforeRead(s, sym, set))
                        {
                            return false;
                        }
                    }
                    return true;
                case NODE_EXPRESSION_STATEMENT:
                case NODE_RETURN_STATEMENT:
                    return expression(stmt->getChild(0));
                case NODE_DECLARATION:
                    return expression(stmt->getChild(1));
                case NODE_LABEL:
                    // A jump may come in from anywhere.
                    set = false;
                    return true;
                case NODE_IF_STATEMENT:
                {
                    if (!expression(stmt->getChild(0)))
                    {
                        return false;
                    }
                    bool yes = set;
                    bool no  = set;
                    if (!setBeforeRead(stmt->getChild(1), sym, yes) || !setBeforeRead(stmt->getChild(2), sym, no))
                    {
                        return false;
                    }
                    set = yes && no;
                    return true;
                }
                case NODE_WHILE_STATEMENT:
                {
                    // The body may not run, and runs again as set as it was.
                    if (!expression(stmt->getChild(0)))
                    {
                        return false;
                    }
                    bool body = set;
                    return setBeforeRead(stmt->getChild(1), sym, body);
                }
                case NODE_DO_STATEMENT:
                    return setBeforeRead(stmt->getChild(0), sym, set) && expression(stmt->getChild(1));
                case NODE_FOR_STATEMENT:
                {
                    for (LSLASTNode* init = stmt->getChild(0)->getChild(0); init; init = init->getNext())
                    {
                        if (!expression(init))
                        {
                            return false;
                        }
                    }
                    if (!expression(stmt->getChild(1)))
                    {
                        return false;
                    }
                    bool body = set;
                    if (!setBeforeRead(stmt->getChild(3), sym, body))
                    {
                        return false;
                    }
                    for (LSLASTNode* step = stmt->getChild(2)->getChild(0); step; step = step->getNext())
                    {
                        if (!(body || !mentions(step, sym)))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                default:
                    return !mentions(stmt, sym) || set;
            }
        }

        // The global made a local of each function and handler that names it.
        void localize(LSLScript* script, LSLASTNode* global)
        {
            LSLSymbol*  sym  = global->getSymbol();
            LSLASTNode* init = global->getChild(1);
            if (init && init->getNodeType() == NODE_EXPRESSION && !changesNothing(init))
            {
                return;
            }
            std::vector<LSLASTNode*> owners;
            for (LSLASTNode* owner : mOwners)
            {
                if (mentions(owner, sym))
                {
                    owners.push_back(owner);
                }
            }
            if (owners.empty())
            {
                return;
            }
            const boost::unordered_flat_set<LSLASTNode*> named(owners.begin(), owners.end());
            for (LSLASTNode* owner : owners)
            {
                // Set before read in its body; no other name the same; and no
                // call on from it into one that names the global.
                LSLStatement* body = owner->getNodeType() == NODE_GLOBAL_FUNCTION ? static_cast<LSLGlobalFunction*>(owner)->getStatements()
                                                                                    : static_cast<LSLEventHandler*>(owner)->getStatements();
                bool set = false;
                if (!isStatement(body, NODE_COMPOUND_STATEMENT) || !setBeforeRead(body, sym, set))
                {
                    return;
                }
                bool clash = false;
                eachNode(owner, [&](LSLASTNode* n) {
                    clash = clash || (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() != sym &&
                                      static_cast<LSLIdentifier*>(n)->getName() && !strcmp(static_cast<LSLIdentifier*>(n)->getName(), sym->getName()));
                });
                if (clash)
                {
                    return;
                }
                for (LSLASTNode* f : reached(owner))
                {
                    if (named.contains(f))
                    {
                        return;
                    }
                }
            }
            report.note(global->getLoc(), "OptimizerLocalizedGlobal", "made the global [1] a local where it is used, each setting it before it reads it",
                        { sym->getName() });
            const Uncounted uncounted(*ctx.context);
            for (LSLASTNode* owner : owners)
            {
                LSLStatement* body  = owner->getNodeType() == NODE_GLOBAL_FUNCTION ? static_cast<LSLGlobalFunction*>(owner)->getStatements()
                                                                                    : static_cast<LSLEventHandler*>(owner)->getStatements();
                LSLType*      type  = sym->getType();
                auto*         id    = ctx.allocator->newTracked<LSLIdentifier>(type, sym->getName());
                id->setLoc(body->getLoc());
                auto*         decl  = ctx.allocator->newTracked<LSLDeclaration>(id, static_cast<LSLExpression*>(nullptr));
                decl->setLoc(body->getLoc());
                auto*         local = ctx.allocator->newTracked<LSLSymbol>(sym->getName(), type, SYM_VARIABLE, SYM_LOCAL, body->getLoc(), nullptr, decl);
                id->setSymbol(local);
                eachNode(body, [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym)
                    {
                        static_cast<LSLIdentifier*>(n)->setSymbol(local);
                    }
                });
                std::vector<LSLASTNode*> stmts{ decl };
                for (LSLASTNode* stmt = body->getChild(0); stmt; stmt = stmt->getNext())
                {
                    stmts.push_back(stmt);
                }
                setStatements(body, stmts, *ctx.context);
                if (LSLSymbolTable* table = body->getSymbolTable())
                {
                    table->define(local);
                }
                else
                {
                    decl->defineSymbol(local);
                }
            }
            script->getSymbolTable()->remove(sym);
            script->getGlobals()->removeChild(global);
            ++changes;
        }
    };
}

    int localizeGlobals(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        ScratchGlobals scratch(ctx, report, options);
        return scratch.run(script);
    }
}
