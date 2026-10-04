/**
 * @file allslsimplifier.cpp
 * @brief The LSL optimizer's simplifier.
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

#include "allslsimplifier.h"

#include "allslcosts.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace ALLSLPasses
{
namespace
{
    // The identities, the shorter spellings, and the reshaped conditions.
    class Simplifier : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLBinaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (!left || !right || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            if (settled(expr))
            {
                return false;
            }
            const LSLOperator op   = expr->getOperation();
            const LSLIType    type = expr->getIType();
            const bool        keepLeft  = left->getIType() == type;
            const bool        keepRight = right->getIType() == type;
            const bool        numeric   = type == LST_INTEGER || type == LST_FLOATINGPOINT;
            const auto        zero      = [](LSLASTNode* n) { return isInteger(n, 0) || isFloat(n, 0.0); };
            const auto        one       = [](LSLASTNode* n) { return isInteger(n, 1) || isFloat(n, 1.0); };
            switch (op)
            {
                case OP_PLUS:
                    // What adds nothing, by what the sum is: an empty
                    // string to a string, an empty list to a list, and a
                    // zero to an integer. A list plus anything else is that
                    // list with one more element -- `l + 0` appends a zero,
                    // and so does `(list)a + 0`, which is what a list's
                    // literal is written as -- and a float plus zero is not
                    // the float where the float is -0.0.
                    if (keepLeft && ((type == LST_INTEGER && zero(right)) || (type == LST_STRING && isEmptyString(right)) ||
                                     (type == LST_LIST && isEmptyList(right))))
                    {
                        return keep(expr, 0);
                    }
                    if (keepRight && ((type == LST_INTEGER && zero(left)) || (type == LST_STRING && isEmptyString(left)) ||
                                      (type == LST_LIST && isEmptyList(left))))
                    {
                        return keep(expr, 1);
                    }
                    if (numeric && keepLeft && negative(right)) return resign(expr, OP_MINUS);
                    break;
                case OP_MINUS:
                    // Taking zero away leaves a number as it was, -0.0 too.
                    if (numeric && keepLeft && zero(right)) return keep(expr, 0);
                    if (numeric && keepLeft && negative(right)) return resign(expr, OP_PLUS);
                    break;
                case OP_MUL:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    if (keepRight && one(left)) return keep(expr, 1);
                    if (type == LST_INTEGER && isInteger(right, 0) && changesNothing(left)) return become(expr, ctx.integer(0));
                    if (type == LST_INTEGER && isInteger(left, 0) && changesNothing(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_DIV:
                    if (keepLeft && one(right)) return keep(expr, 0);
                    break;
                case OP_BIT_OR:
                case OP_BIT_XOR:
                case OP_SHIFT_LEFT:
                case OP_SHIFT_RIGHT:
                    if (isInteger(right, 0)) return keep(expr, 0);
                    if ((op == OP_BIT_OR || op == OP_BIT_XOR) && isInteger(left, 0)) return keep(expr, 1);
                    break;
                case OP_BIT_AND:
                    if (isInteger(right, -1)) return keep(expr, 0);
                    if (isInteger(left, -1)) return keep(expr, 1);
                    if (isInteger(right, 0) && changesNothing(left)) return become(expr, ctx.integer(0));
                    if (isInteger(left, 0) && changesNothing(right)) return become(expr, ctx.integer(0));
                    break;
                case OP_EQ:
                    // `x == 0` is `!x`, and one token shorter.
                    if (left->getIType() == LST_INTEGER && isInteger(right, 0)) return negate(expr, 0);
                    if (right->getIType() == LST_INTEGER && isInteger(left, 0)) return negate(expr, 1);
                    break;
                case OP_BOOLEAN_AND:
                case OP_BOOLEAN_OR:
                    condition(expr, 0);
                    condition(expr, 1);
                    break;
                default:
                    break;
            }
            return false;
        }

        bool visit(LSLUnaryExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (!child || expr->getIType() == LST_ERROR)
            {
                return false;
            }
            LSLExpression* inner = bare(child);
            if (expr->getOperation() == OP_MINUS && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_MINUS)
            {
                // -(-x)
                LSLExpression* x = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (x && x->getIType() == expr->getIType())
                {
                    report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { render(expr), render(x) });
                    inner->setChild(0, nullptr);
                    putInPlace(expr, x, ctx.allocator);
                    ++changes;
                }
                return false;
            }
            if (expr->getOperation() == OP_BOOLEAN_NOT)
            {
                condition(expr, 0);
                child = expr->getChildExpr();
                inner = bare(child);
                if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getIType() == LST_INTEGER)
                {
                    // !(a != b) is a == b, whatever they are: both are
                    // true or false. !(a == b) is a != b only for numbers --
                    // a list's != is the difference of the lengths, and
                    // LSO's for a string is not only one or zero -- and the
                    // orderings only for integers, which have no NaN.
                    auto*       bin      = static_cast<LSLBinaryExpression*>(inner);
                    LSLOperator opposite = OP_NONE;
                    const auto  number   = [](LSLExpression* e) { return e->getIType() == LST_INTEGER || e->getIType() == LST_FLOATINGPOINT; };
                    const bool  ints     = bin->getLHS()->getIType() == LST_INTEGER && bin->getRHS()->getIType() == LST_INTEGER;
                    const bool  numbers  = number(bin->getLHS()) && number(bin->getRHS());
                    switch (bin->getOperation())
                    {
                        case OP_EQ: opposite = numbers ? OP_NEQ : OP_NONE; break;
                        case OP_NEQ: opposite = OP_EQ; break;
                        case OP_LESS: opposite = ints ? OP_GEQ : OP_NONE; break;
                        case OP_GREATER: opposite = ints ? OP_LEQ : OP_NONE; break;
                        case OP_LEQ: opposite = ints ? OP_GREATER : OP_NONE; break;
                        case OP_GEQ: opposite = ints ? OP_LESS : OP_NONE; break;
                        default: break;
                    }
                    if (opposite != OP_NONE)
                    {
                        const std::string was = render(expr);
                        bin->setOperation(opposite);
                        inner->getParent()->takeChild(inner->getParentSlot());
                        report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(bin) });
                        putInPlace(expr, bin, ctx.allocator);
                        ++changes;
                    }
                }
            }
            return false;
        }

        bool visit(LSLTypecastExpression* expr) override
        {
            visitChildren(expr);
            LSLExpression* child = expr->getChildExpr();
            if (child && child->getIType() == expr->getIType() && expr->getIType() != LST_ERROR)
            {
                report.note(expr->getLoc(), "OptimizerDroppedCast", "dropped the cast in [1]", { render(expr) });
                expr->setChild(0, nullptr);
                putInPlace(expr, child, ctx.allocator);
                ++changes;
                return false;
            }
            return false;
        }

        bool visit(LSLFunctionExpression* expr) override
        {
            visitChildren(expr);
            LSLSymbol* sym = expr->getSymbol();
            if (options.listlength && ALLSLCosts::of(options.target).lengthAsNotEqual && sym && sym->getSubType() == SYM_BUILTIN &&
                !strcmp(sym->getName(), "llGetListLength") &&
                expr->getArguments()->getNumChildren() == 1)
            {
                // llGetListLength(x) is x != [], which is the length.
                auto* arg = static_cast<LSLExpression*>(expr->getArguments()->takeChild(0));
                auto* empty = ctx.allocator->newTracked<LSLConstantExpression>(ctx.allocator->newTracked<LSLListConstant>(nullptr));
                auto* test  = ctx.allocator->newTracked<LSLBinaryExpression>(arg, OP_NEQ, static_cast<LSLExpression*>(empty));
                test->setType(TYPE(LST_INTEGER));
                report.note(expr->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { render(expr), render(test) });
                putInPlace(expr, test, ctx.allocator);
                ++changes;
            }
            return false;
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            if (!options.ifelseswap)
            {
                return false;
            }
            LSLExpression* cond  = stmt->getCheckExpr();
            LSLStatement*  yes   = stmt->getTrueBranch();
            LSLStatement*  no    = stmt->getFalseBranch();
            LSLExpression* inner = bare(cond);
            if (no && inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                // if (!c) A else B is if (c) B else A.
                LSLExpression* c = static_cast<LSLUnaryExpression*>(inner)->getChildExpr();
                if (c)
                {
                    inner->setChild(0, nullptr);
                    stmt->setCheckExpr(c);
                    swapBranches(stmt);
                    report.note(stmt->getLoc(), "OptimizerSwappedBranches", "swapped the branches of if ([1]) and dropped the !", { render(c) });
                    ++changes;
                }
                return false;
            }
            if (no && empty(yes) && cond->getIType() == LST_INTEGER)
            {
                // if (c) ; else B is if (!c) B: !(c) where c is an
                // operation, which the ! would take only the first operand
                // of otherwise.
                stmt->setCheckExpr(nullptr);
                LSLExpression* operand = cond;
                if (cond->getNodeSubType() == NODE_BINARY_EXPRESSION)
                {
                    auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(cond);
                    parens->setType(cond->getType());
                    parens->setLoc(cond->getLoc());
                    operand = parens;
                }
                auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
                negated->setType(TYPE(LST_INTEGER));
                negated->setLoc(cond->getLoc());
                stmt->setCheckExpr(negated);
                stmt->setTrueBranch(nullptr);
                stmt->setFalseBranch(nullptr);
                stmt->setTrueBranch(no);
                report.note(stmt->getLoc(), "OptimizerTurnedEmptyBranch", "turned an empty if branch around");
                ++changes;
            }
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 0);
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            condition(stmt, 1);
            return false;
        }

    private:
        // What a number can be, the least and the most, where something
        // says: a constant; a library function's answer (ALLSLTraits::bounds),
        // llFrand's of a magnitude whose sign is known; a truth; an & with a
        // number not below nought; a list's length as l != []; a local set
        // to one of these where it is declared and never after.
        struct Range
        {
            double least = 0.0;
            double most  = 0.0;
        };
        std::optional<Range> range(LSLExpression* e, int depth = 0) const
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
                    return init && init->getNodeType() == NODE_EXPRESSION ? range(static_cast<LSLExpression*>(init), depth + 1) : std::nullopt;
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
                        const std::optional<Range> mag = arg && arg->getNodeType() == NODE_EXPRESSION ? range(static_cast<LSLExpression*>(arg), depth + 1)
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
                                const std::optional<Range> r = side && side->getIType() == LST_INTEGER ? range(side, depth + 1) : std::nullopt;
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

        // A comparison of numbers settled by what each side can be: never
        // below nought is never below -1, and a random float of a positive
        // magnitude never below nought. Only where neither side changes
        // anything, since neither is run any more.
        bool settled(LSLBinaryExpression* expr)
        {
            const LSLOperator op = expr->getOperation();
            if (op != OP_LESS && op != OP_GREATER && op != OP_LEQ && op != OP_GEQ && op != OP_EQ && op != OP_NEQ)
            {
                return false;
            }
            LSLExpression* left   = expr->getLHS();
            LSLExpression* right  = expr->getRHS();
            const auto     number = [](LSLExpression* x) { return x->getIType() == LST_INTEGER || x->getIType() == LST_FLOATINGPOINT; };
            if (!number(left) || !number(right) || expr->getConstantValue() || !changesNothing(left) || !changesNothing(right))
            {
                return false;
            }
            const std::optional<Range> a = range(left);
            const std::optional<Range> b = range(right);
            if (!a || !b)
            {
                return false;
            }
            std::optional<bool> is;
            const bool          apart = a->most < b->least || b->most < a->least;
            switch (op)
            {
                case OP_LESS: is = a->most < b->least ? std::optional<bool>(true) : a->least >= b->most ? std::optional<bool>(false) : std::nullopt; break;
                case OP_LEQ: is = a->most <= b->least ? std::optional<bool>(true) : a->least > b->most ? std::optional<bool>(false) : std::nullopt; break;
                case OP_GREATER: is = a->least > b->most ? std::optional<bool>(true) : a->most <= b->least ? std::optional<bool>(false) : std::nullopt; break;
                case OP_GEQ: is = a->least >= b->most ? std::optional<bool>(true) : a->most < b->least ? std::optional<bool>(false) : std::nullopt; break;
                case OP_EQ: is = apart ? std::optional<bool>(false) : std::nullopt; break;
                default: is = apart ? std::optional<bool>(true) : std::nullopt; break;
            }
            if (!is)
            {
                return false;
            }
            fold(expr, ctx.integer(*is ? 1 : 0), "OptimizerSettled", "settled");
            return true;
        }

        static bool empty(LSLStatement* s)
        {
            if (!s)
            {
                return true;
            }
            if (s->getNodeSubType() == NODE_NOP_STATEMENT)
            {
                return true;
            }
            return s->getNodeSubType() == NODE_COMPOUND_STATEMENT && !s->hasChildren();
        }

        static void swapBranches(LSLIfStatement* stmt)
        {
            LSLStatement* yes = stmt->getTrueBranch();
            LSLStatement* no  = stmt->getFalseBranch();
            stmt->setTrueBranch(nullptr);
            stmt->setFalseBranch(nullptr);
            stmt->setTrueBranch(no);
            stmt->setFalseBranch(yes);
        }

        // A constant below zero, whose sign can move onto the operator.
        static bool negative(LSLASTNode* n)
        {
            LSLConstant* cv = n->getConstantValue();
            if (!cv)
            {
                return false;
            }
            if (cv->getNodeSubType() == NODE_INTEGER_CONSTANT)
            {
                const int v = static_cast<LSLIntegerConstant*>(cv)->getValue();
                return v < 0 && v != INT32_MIN;
            }
            if (cv->getNodeSubType() == NODE_FLOAT_CONSTANT)
            {
                return static_cast<LSLFloatConstant*>(cv)->getValue() < 0.0;
            }
            return false;
        }

        // The expression becomes one of its operands.
        bool keep(LSLBinaryExpression* expr, int slot)
        {
            const std::string was  = render(expr);
            auto*             kept = static_cast<LSLExpression*>(expr->takeChild(slot));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(kept) });
            putInPlace(expr, kept, ctx.allocator);
            ++changes;
            return false;
        }

        bool become(LSLBinaryExpression* expr, LSLConstant* cv)
        {
            fold(expr, cv, "OptimizerSimplified", "simplified");
            return false;
        }

        // x + -c as x - c, x - -c as x + c.
        bool resign(LSLBinaryExpression* expr, LSLOperator op)
        {
            const std::string was = render(expr);
            LSLConstant*      cv  = expr->getRHS()->getConstantValue();
            LSLConstant*      pos = cv->getNodeSubType() == NODE_INTEGER_CONSTANT
                                        ? ctx.integer(-static_cast<LSLIntegerConstant*>(cv)->getValue())
                                        : ctx.allocator->newTracked<LSLFloatConstant>(-static_cast<LSLFloatConstant*>(cv)->getValue());
            expr->setOperation(op);
            expr->setRHS(constant(pos, expr->getRHS()));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(expr) });
            ++changes;
            return false;
        }

        // x == 0 as !x.
        bool negate(LSLBinaryExpression* expr, int slot)
        {
            const std::string was = render(expr);
            auto*             x   = static_cast<LSLExpression*>(expr->takeChild(slot));
            LSLExpression*    operand = x;
            if (x->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                operand = ctx.allocator->newTracked<LSLParenthesisExpression>(x);
                operand->setType(x->getType());
            }
            auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
            negated->setType(TYPE(LST_INTEGER));
            report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(negated) });
            putInPlace(expr, negated, ctx.allocator);
            ++changes;
            return false;
        }

        // A child that is only ever true or false: `x != 0` is `x`, and
        // `!!x` is `x`.
        void condition(LSLASTNode* parent, int slot)
        {
            LSLASTNode* child = parent->getChild(slot);
            if (!child || child->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            auto*          expr  = static_cast<LSLExpression*>(child);
            LSLExpression* inner = bare(expr);
            if (inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getOperation() == OP_NEQ)
            {
                auto* bin = static_cast<LSLBinaryExpression*>(inner);
                int   keepSlot = -1;
                if (bin->getLHS()->getIType() == LST_INTEGER && isInteger(bin->getRHS(), 0)) keepSlot = 0;
                else if (bin->getRHS()->getIType() == LST_INTEGER && isInteger(bin->getLHS(), 0)) keepSlot = 1;
                if (keepSlot >= 0)
                {
                    const std::string was  = render(expr);
                    auto*             kept = static_cast<LSLExpression*>(bin->takeChild(keepSlot));
                    report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(kept) });
                    LSLASTNode::replaceNode(expr, kept);
                    ++changes;
                    return;
                }
            }
            if (inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                LSLExpression* once = bare(static_cast<LSLUnaryExpression*>(inner)->getChildExpr());
                if (once && once->getNodeSubType() == NODE_UNARY_EXPRESSION && once->getOperation() == OP_BOOLEAN_NOT)
                {
                    LSLExpression* x = static_cast<LSLUnaryExpression*>(once)->getChildExpr();
                    if (x && x->getIType() == LST_INTEGER)
                    {
                        const std::string was = render(expr);
                        once->setChild(0, nullptr);
                        report.note(expr->getLoc(), "OptimizerSimplified", "simplified [1] to [2]", { was, render(x) });
                        LSLASTNode::replaceNode(expr, x);
                        ++changes;
                    }
                }
            }
        }
    };
}

    int simplify(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        Simplifier simplifier(ctx, report, options);
        script->visit(&simplifier);
        return simplifier.changes;
    }
}
