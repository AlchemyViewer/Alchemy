/**
 * @file allsloptimizerpass.cpp
 * @brief What the LSL optimizer's passes share.
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

#include "allsloptimizerpass.h"

#include <tailslide/passes/pretty_print.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

namespace ALLSLPasses
{
    std::string render(LSLASTNode* node)
    {
        PrettyPrintOpts    opts{};
        PrettyPrintVisitor printer(opts);
        node->visit(&printer);
        std::string text = printer.mStream.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        {
            text.pop_back();
        }
        // A statement's rendering starts with its indentation.
        const size_t start = text.find_first_not_of(" \t");
        return start == std::string::npos ? text : text.substr(start);
    }
    bool wantsParens(LSLASTNode* parent, LSLASTNode* old)
    {
        if (!parent || parent->getNodeType() != NODE_EXPRESSION)
        {
            return false;
        }
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                if (operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(1) == old)
                {
                    return false;
                }
                return true;
            // A cast's own printing puts its operand in parentheses where
            // it needs them, and around parentheses too.
            case NODE_TYPECAST_EXPRESSION:
                return false;
            case NODE_UNARY_EXPRESSION:
            case NODE_PRINT_EXPRESSION:
            case NODE_BOOL_CONVERSION_EXPRESSION:
                return true;
            default:
                return false;
        }
    }

    void putInPlace(LSLASTNode* old, LSLExpression* replacement, ScriptAllocator* allocator)
    {
        LSLASTNode* parent = old->getParent();
        replacement->setLoc(old->getLoc());
        if (wantsParens(parent, old) && replacement->getNodeSubType() != NODE_CONSTANT_EXPRESSION &&
            replacement->getNodeSubType() != NODE_LVALUE_EXPRESSION && replacement->getNodeSubType() != NODE_PARENTHESIS_EXPRESSION &&
            replacement->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            auto* parens = allocator->newTracked<LSLParenthesisExpression>(replacement);
            parens->setType(replacement->getType());
            parens->setLoc(old->getLoc());
            LSLASTNode::replaceNode(old, parens);
            return;
        }
        LSLASTNode::replaceNode(old, replacement);
    }
    ALScriptWeight::Target weightTarget(ALLSLOptimizer::Target target)
    {
        return target == ALLSLOptimizer::Target::LSO    ? ALScriptWeight::Target::LSO
               : target == ALLSLOptimizer::Target::Luau ? ALScriptWeight::Target::LSLLuau
                                                        : ALScriptWeight::Target::Mono;
    }

    ALScriptWeight weigh(ALLSLOptimizer::Target target, std::string_view text)
    {
        switch (weightTarget(target))
        {
            case ALScriptWeight::Target::LSO:
                return ALScriptWeigh::lso(text);
            case ALScriptWeight::Target::LSLLuau:
                return ALScriptWeigh::lslLuau(text);
            default:
                return ALScriptWeigh::mono(text);
        }
    }

std::optional<Range> rangeOf(LSLExpression* e, int depth)
    {
        e = bare(e);
        if (!e || depth > 8)
        {
            return std::nullopt;
        }
        if (LSLConstant* cv = e->getConstantValue())
        {
            double v = 0.0;
            if (cv->getNodeSubType() == NODE_INTEGER_CONSTANT)
            {
                v = static_cast<LSLIntegerConstant*>(cv)->getValue();
            }
            else if (cv->getNodeSubType() == NODE_FLOAT_CONSTANT && std::isfinite(static_cast<LSLFloatConstant*>(cv)->getValue()))
            {
                v = static_cast<LSLFloatConstant*>(cv)->getValue();
            }
            else
            {
                return std::nullopt;
            }
            return Range{ v, v };
        }
        const auto number = [](LSLExpression* x) { return x && (x->getIType() == LST_INTEGER || x->getIType() == LST_FLOATINGPOINT); };
        switch (e->getNodeSubType())
        {
            case NODE_LVALUE_EXPRESSION:
            {
                auto*       read = static_cast<LSLLValueExpression*>(e);
                LSLSymbol*  sym  = read->getSymbol();
                LSLASTNode* decl = sym && !read->getMember() && read->getIsFoldable() && sym->getSubType() == SYM_LOCAL && sym->getAssignments() == 0
                                       ? sym->getVarDecl()
                                       : nullptr;
                LSLASTNode* init = decl ? decl->getChild(1) : nullptr;
                return init && init->getNodeType() == NODE_EXPRESSION ? rangeOf(static_cast<LSLExpression*>(init), depth + 1) : std::nullopt;
            }
            case NODE_FUNCTION_EXPRESSION:
            {
                LSLSymbol* sym = e->getSymbol();
                if (!sym || sym->getSubType() != SYM_BUILTIN)
                {
                    return std::nullopt;
                }
                if (!strcmp(sym->getName(), "llFrand"))
                {
                    // From nought towards what it is given, whose sign it
                    // has; the bound itself let in, which rounding can give.
                    LSLASTNode*                arg = static_cast<LSLFunctionExpression*>(e)->getArguments()->getChild(0);
                    const std::optional<Range> mag = arg && arg->getNodeType() == NODE_EXPRESSION ? rangeOf(static_cast<LSLExpression*>(arg), depth + 1)
                                                                                                  : std::nullopt;
                    if (!mag)
                    {
                        return std::nullopt;
                    }
                    return Range{ std::min(0.0, mag->least), std::max(0.0, mag->most) };
                }
                S32 least = 0, most = 0;
                if (e->getIType() == LST_INTEGER && ALLSLTraits::bounds(sym->getName(), least, most))
                {
                    return Range{ static_cast<double>(least), static_cast<double>(most) };
                }
                return std::nullopt;
            }
            case NODE_UNARY_EXPRESSION:
                return e->getOperation() == OP_BOOLEAN_NOT ? std::optional<Range>(Range{ 0.0, 1.0 }) : std::nullopt;
            case NODE_BINARY_EXPRESSION:
            {
                auto* b = static_cast<LSLBinaryExpression*>(e);
                switch (b->getOperation())
                {
                    case OP_LESS:
                    case OP_GREATER:
                    case OP_LEQ:
                    case OP_GEQ:
                    case OP_EQ:
                    case OP_BOOLEAN_AND:
                    case OP_BOOLEAN_OR:
                        return Range{ 0.0, 1.0 };
                    case OP_NEQ:
                        // A list's != is how much longer it is; LSO's of
                        // a string not only 1 or 0.
                        if (b->getLHS()->getIType() == LST_LIST && isEmptyList(b->getRHS()))
                        {
                            return Range{ 0.0, static_cast<double>(INT32_MAX) };
                        }
                        return number(b->getLHS()) && number(b->getRHS()) ? std::optional<Range>(Range{ 0.0, 1.0 }) : std::nullopt;
                    case OP_BIT_AND:
                        for (LSLExpression* side : { b->getLHS(), b->getRHS() })
                        {
                            const std::optional<Range> r = side && side->getIType() == LST_INTEGER ? rangeOf(side, depth + 1) : std::nullopt;
                            if (r && r->least >= 0.0)
                            {
                                return Range{ 0.0, r->most };
                            }
                        }
                        return std::nullopt;
                    default:
                        return std::nullopt;
                }
            }
            default:
                return std::nullopt;
        }
    }

    // How many labels of a name the function or event a node is in has.
    // A jump goes to the last of them; the compiler finds it among
    // those in scope.
    int labelsNamed(LSLASTNode* node, const char* name)
    {
        LSLASTNode* callable = node;
        while (callable && callable->getNodeType() != NODE_GLOBAL_FUNCTION && callable->getNodeType() != NODE_EVENT_HANDLER)
        {
            callable = callable->getParent();
        }
        int                                    named = 0;
        const std::function<void(LSLASTNode*)> count = [&](LSLASTNode* n) {
            if (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL && !strcmp(static_cast<LSLLabel*>(n)->getIdentifier()->getName(), name))
            {
                ++named;
            }
            for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
            {
                count(child);
            }
        };
        if (callable)
        {
            count(callable);
        }
        return named;
    }

    // Whether what follows a statement never runs by running on from it:
    // it returns, changes state or jumps; resets the script; is an if
    // every way of which does so, or a loop that never ends. A label
    // jumped to after it is the dead code's own business (above).
    bool stops(LSLASTNode* stmt)
    {
        if (!stmt || stmt->getNodeType() != NODE_STATEMENT)
        {
            return false;
        }
        const auto forever = [](LSLExpression* check) {
            LSLConstant* cv = check ? check->getConstantValue() : nullptr;
            return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && static_cast<LSLIntegerConstant*>(cv)->getValue() != 0;
        };
        switch (stmt->getNodeSubType())
        {
            case NODE_RETURN_STATEMENT:
            case NODE_STATE_STATEMENT:
            case NODE_JUMP_STATEMENT:
                return true;
            case NODE_EXPRESSION_STATEMENT:
            {
                LSLExpression* expr = bare(static_cast<LSLExpressionStatement*>(stmt)->getExpr());
                LSLSymbol*     sym  = expr && expr->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? expr->getSymbol() : nullptr;
                return sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llResetScript");
            }
            case NODE_COMPOUND_STATEMENT:
            {
                LSLASTNode* last = nullptr;
                for (LSLASTNode* child = stmt->getChild(0); child; child = child->getNext())
                {
                    last = child;
                }
                return stops(last);
            }
            case NODE_IF_STATEMENT:
            {
                auto* branch = static_cast<LSLIfStatement*>(stmt);
                return branch->getFalseBranch() && stops(branch->getTrueBranch()) && stops(branch->getFalseBranch());
            }
            case NODE_WHILE_STATEMENT:
                return forever(static_cast<LSLWhileStatement*>(stmt)->getCheckExpr());
            case NODE_DO_STATEMENT:
                return forever(static_cast<LSLDoStatement*>(stmt)->getCheckExpr());
            case NODE_FOR_STATEMENT:
                return forever(static_cast<LSLForStatement*>(stmt)->getCheckExpr());
            default:
                return false;
        }
    }

    bool holdsLabel(LSLASTNode* root)
    {
        bool found = false;
        eachNode(root, [&found](LSLASTNode* n) { found = found || (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL); });
        return found;
    }

    void setStatements(LSLASTNode* block, const std::vector<LSLASTNode*>& statements, ScriptContext& context)
    {
        // The statements are the ones they were, every reference in them
        // still in the script: counted neither out nor in again.
        const Uncounted          uncounted(context);
        std::vector<LSLASTNode*> had;
        for (LSLASTNode* stmt = block->getChild(0); stmt; stmt = stmt->getNext())
        {
            had.push_back(stmt);
        }
        for (LSLASTNode* stmt : had)
        {
            block->removeChild(stmt);
        }
        for (LSLASTNode* stmt : statements)
        {
            block->pushChild(stmt);
        }
    }

    std::optional<ALLSLCosts::Held> heldFor(ALLSLOptimizer::Target target, LSLConstant* cv)
    {
        const ALLSLCosts& costs = ALLSLCosts::of(target);
        const auto        whole = [](std::initializer_list<double> parts) {
            return std::all_of(parts.begin(), parts.end(), [](double v) { return integral(v); });
        };
        switch (cv->getIType())
        {
            case LST_INTEGER:
                return costs.integer;
            case LST_FLOATINGPOINT:
                return whole({ static_cast<LSLFloatConstant*>(cv)->getValue() }) ? costs.wholeFloating : costs.floating;
            case LST_VECTOR:
            {
                const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                return whole({ v->x, v->y, v->z }) ? costs.wholeVector : costs.vector;
            }
            case LST_QUATERNION:
            {
                const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                return whole({ q->x, q->y, q->z, q->s }) ? costs.wholeRotation : costs.rotation;
            }
            case LST_STRING:
                return costs.stringOf(static_cast<S32>(strlen(static_cast<LSLStringConstant*>(cv)->getValue())));
            case LST_KEY:
                return costs.stringOf(static_cast<S32>(strlen(static_cast<LSLKeyConstant*>(cv)->getValue())));
            default:
                return std::nullopt;
        }
    }
}
