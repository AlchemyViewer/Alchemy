/**
 * @file allslsignatures.cpp
 * @brief The LSL optimizer's functions given and giving no more than they need.
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

#include "allslsignatures.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <cmath>
#include <cstring>
#include <vector>

namespace ALLSLPasses
{
namespace
{
    // Each change is one function's, its calls found again for the next:
    // a parameter taken away moves every argument after it.
    class Signatures : public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION && global->getSymbol())
                {
                    auto* function = static_cast<LSLGlobalFunction*>(global);
                    while (unusedParameter(script, function) || constantParameter(script, function))
                    {
                    }
                    unreadResult(script, function);
                }
            }
            return changes;
        }

    private:
        static bool isStatement(LSLASTNode* n, LSLNodeSubType type)
        {
            return n && n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == type;
        }

        // Every call of a function in the script.
        static std::vector<LSLFunctionExpression*> callsOf(LSLScript* script, LSLSymbol* function)
        {
            std::vector<LSLFunctionExpression*> calls;
            eachNode(script, [&](LSLASTNode* n) {
                if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_FUNCTION_EXPRESSION && n->getSymbol() == function)
                {
                    calls.push_back(static_cast<LSLFunctionExpression*>(n));
                }
            });
            return calls;
        }

        static std::vector<LSLIdentifier*> parametersOf(LSLGlobalFunction* function)
        {
            std::vector<LSLIdentifier*> params;
            if (LSLFunctionDec* dec = function->getArguments())
            {
                for (LSLASTNode* p = dec->getChild(0); p; p = p->getNext())
                {
                    params.push_back(static_cast<LSLIdentifier*>(p));
                }
            }
            return params;
        }

        static LSLASTNode* argumentAt(LSLFunctionExpression* call, size_t at)
        {
            LSLASTNode* arg = call->getArguments() ? call->getArguments()->getChild(0) : nullptr;
            for (size_t k = 0; arg && k < at; ++k)
            {
                arg = arg->getNext();
            }
            return arg;
        }

        // How a body names a symbol: every time, and the times it writes it.
        static std::pair<int, int> namings(LSLASTNode* body, LSLSymbol* sym)
        {
            int named = 0, written = 0;
            eachNode(body, [&](LSLASTNode* n) {
                if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_LVALUE_EXPRESSION || n->getSymbol() != sym)
                {
                    return;
                }
                ++named;
                LSLASTNode* parent = n->getParent();
                written += parent && parent->getNodeType() == NODE_EXPRESSION && operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) &&
                                   parent->getChild(0) == n
                               ? 1
                               : 0;
            });
            return { named, written };
        }

        // A parameter and each call's argument for it taken away.
        void takeAway(LSLGlobalFunction* function, const std::vector<LSLFunctionExpression*>& calls, size_t at, LSLIdentifier* param)
        {
            const Uncounted uncounted(*ctx.context);
            for (LSLFunctionExpression* call : calls)
            {
                if (LSLASTNode* arg = argumentAt(call, at))
                {
                    call->getArguments()->removeChild(arg);
                }
            }
            LSLSymbol* sym = param->getSymbol();
            for (LSLASTNode* up = function; up && sym; up = up->getParent())
            {
                if (up->getSymbolTable() && up->getSymbolTable()->remove(sym))
                {
                    break;
                }
            }
            function->getArguments()->removeChild(param);
        }

        // A parameter the body never names, where what each call gives it
        // does nothing.
        bool unusedParameter(LSLScript* script, LSLGlobalFunction* function)
        {
            const std::vector<LSLIdentifier*> params = parametersOf(function);
            const std::vector<LSLFunctionExpression*> calls = callsOf(script, function->getSymbol());
            for (size_t at = 0; at < params.size(); ++at)
            {
                LSLSymbol* sym = params[at]->getSymbol();
                if (!sym || namings(function->getStatements(), sym).first != 0)
                {
                    continue;
                }
                bool quiet = true;
                for (LSLFunctionExpression* call : calls)
                {
                    LSLASTNode* arg = argumentAt(call, at);
                    quiet           = quiet && arg && changesNothing(arg);
                }
                if (!quiet)
                {
                    continue;
                }
                report.note(params[at]->getLoc(), "OptimizerRemovedParameter", "removed the parameter [1] of [2], which it never reads, and what its calls gave it",
                            { sym->getName(), function->getSymbol()->getName() });
                takeAway(function, calls, at, params[at]);
                ++changes;
                return true;
            }
            return false;
        }

        // Whether two constants are the one value, bit for bit.
        static bool same(LSLConstant* a, LSLConstant* b)
        {
            if (!a || !b || a->getNodeSubType() != b->getNodeSubType())
            {
                return false;
            }
            const auto bits = [](double x, double y) { return x == y && std::signbit(x) == std::signbit(y); };
            switch (a->getNodeSubType())
            {
                case NODE_INTEGER_CONSTANT:
                    return static_cast<LSLIntegerConstant*>(a)->getValue() == static_cast<LSLIntegerConstant*>(b)->getValue();
                case NODE_FLOAT_CONSTANT:
                    return bits(static_cast<LSLFloatConstant*>(a)->getValue(), static_cast<LSLFloatConstant*>(b)->getValue());
                case NODE_STRING_CONSTANT:
                    return !strcmp(static_cast<LSLStringConstant*>(a)->getValue(), static_cast<LSLStringConstant*>(b)->getValue());
                case NODE_KEY_CONSTANT:
                    return !strcmp(static_cast<LSLKeyConstant*>(a)->getValue(), static_cast<LSLKeyConstant*>(b)->getValue());
                case NODE_VECTOR_CONSTANT:
                {
                    const Vector3* u = static_cast<LSLVectorConstant*>(a)->getValue();
                    const Vector3* v = static_cast<LSLVectorConstant*>(b)->getValue();
                    return bits(u->x, v->x) && bits(u->y, v->y) && bits(u->z, v->z);
                }
                case NODE_QUATERNION_CONSTANT:
                {
                    const Quaternion* p = static_cast<LSLQuaternionConstant*>(a)->getValue();
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(b)->getValue();
                    return bits(p->x, q->x) && bits(p->y, q->y) && bits(p->z, q->z) && bits(p->s, q->s);
                }
                default:
                    return false;
            }
        }

        // A parameter every call gives the same constant, never set in the
        // body: a local of that value instead, `T p = C;` ahead of the rest,
        // which the folder then writes where it is read.
        bool constantParameter(LSLScript* script, LSLGlobalFunction* function)
        {
            const std::vector<LSLIdentifier*>         params = parametersOf(function);
            const std::vector<LSLFunctionExpression*> calls  = callsOf(script, function->getSymbol());
            LSLStatement*                             body   = function->getStatements();
            if (calls.empty() || !isStatement(body, NODE_COMPOUND_STATEMENT))
            {
                return false;
            }
            for (size_t at = 0; at < params.size(); ++at)
            {
                LSLSymbol* sym = params[at]->getSymbol();
                if (!sym || sym->getIType() == LST_LIST || namings(body, sym).second != 0)
                {
                    continue;
                }
                LSLASTNode*  first = argumentAt(calls.front(), at);
                LSLConstant* value = first ? first->getConstantValue() : nullptr;
                bool         one   = value && inlineable(value) && first->getIType() == sym->getIType();
                for (LSLFunctionExpression* call : calls)
                {
                    LSLASTNode* arg = argumentAt(call, at);
                    one             = one && arg && same(arg->getConstantValue(), value);
                }
                if (!one)
                {
                    continue;
                }
                report.note(params[at]->getLoc(), "OptimizerConstantParameter", "made the parameter [1] of [2] a local, every call giving it the same value",
                            { sym->getName(), function->getSymbol()->getName() });
                // The local, a symbol of its own, named where the parameter was.
                LSLType*    type = sym->getType();
                const char* name = sym->getName();
                auto*       id   = ctx.allocator->newTracked<LSLIdentifier>(type, name);
                id->setLoc(params[at]->getLoc());
                auto* decl  = ctx.allocator->newTracked<LSLDeclaration>(id, static_cast<LSLExpression*>(constant(value->copy(ctx.allocator), params[at])));
                decl->setLoc(params[at]->getLoc());
                auto* local = ctx.allocator->newTracked<LSLSymbol>(name, type, SYM_VARIABLE, SYM_LOCAL, params[at]->getLoc(), nullptr, decl);
                id->setSymbol(local);
                eachNode(body, [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym)
                    {
                        static_cast<LSLIdentifier*>(n)->setSymbol(local);
                    }
                });
                takeAway(function, calls, at, params[at]);
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
                ++changes;
                return true;
            }
            return false;
        }

        // A function whose value no call reads, made one that returns
        // nothing: each `return e;` as `e; return;`, or `return;` where e
        // does nothing.
        void unreadResult(LSLScript* script, LSLGlobalFunction* function)
        {
            LSLIdentifier* id = function->getIdentifier();
            if (!id || id->getIType() == LST_NULL)
            {
                return;
            }
            for (LSLFunctionExpression* call : callsOf(script, function->getSymbol()))
            {
                LSLASTNode* up   = call->getParent();
                LSLASTNode* loop = up ? up->getParent() : nullptr;
                const bool  statement = isStatement(up, NODE_EXPRESSION_STATEMENT);
                const bool  step      = up && up->getNodeType() == NODE_AST_NODE_LIST && isStatement(loop, NODE_FOR_STATEMENT) &&
                                  (up == static_cast<LSLForStatement*>(loop)->getInitExprs() || up == static_cast<LSLForStatement*>(loop)->getIncrExprs());
                if (!statement && !step)
                {
                    return;
                }
            }
            std::vector<LSLASTNode*> returns;
            eachNode(function->getStatements(), [&](LSLASTNode* n) {
                if (isStatement(n, NODE_RETURN_STATEMENT))
                {
                    returns.push_back(n);
                }
            });
            report.note(id->getLoc(), "OptimizerUnreadResult", "made [1] return nothing, as no call reads what it returns", { function->getSymbol()->getName() });
            const Uncounted uncounted(*ctx.context);
            id->setType(TYPE(LST_NULL));
            for (LSLASTNode* ret : returns)
            {
                LSLASTNode* value = ret->getChild(0);
                if (!value || value->getNodeType() != NODE_EXPRESSION)
                {
                    continue;
                }
                if (changesNothing(value))
                {
                    ret->setChild(0, ret->newNullNode());
                    continue;
                }
                // What it worked out, still worked out, then the return.
                auto* run = ctx.allocator->newTracked<LSLExpressionStatement>(static_cast<LSLExpression*>(ret->takeChild(0)));
                run->setLoc(ret->getLoc());
                LSLASTNode* parent = ret->getParent();
                if (isStatement(parent, NODE_COMPOUND_STATEMENT))
                {
                    std::vector<LSLASTNode*> stmts;
                    for (LSLASTNode* stmt = parent->getChild(0); stmt; stmt = stmt->getNext())
                    {
                        if (stmt == ret)
                        {
                            stmts.push_back(run);
                        }
                        stmts.push_back(stmt);
                    }
                    setStatements(parent, stmts, *ctx.context);
                }
                else
                {
                    const int slot  = ret->getParentSlot();
                    auto*     block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
                    block->setLoc(ret->getLoc());
                    parent->takeChild(slot);
                    block->pushChild(run);
                    block->pushChild(ret);
                    parent->setChild(slot, block);
                }
            }
            ++changes;
        }
    };
}

    int trimSignatures(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        Signatures signatures(ctx, report, options);
        return signatures.run(script);
    }
}
