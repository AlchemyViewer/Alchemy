/**
 * @file allslshapes.cpp
 * @brief The LSL optimizer's shapes made for size.
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

#include "allslshapes.h"

#include "allslcosts.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>

namespace ALLSLPasses
{
namespace
{
    class ListHelpersFinder : public ASTVisitor
    {
    public:
        ListHelpers found;

        bool visit(LSLFunctionExpression* expr) override
        {
            LSLSymbol*                     sym  = expr->getSymbol();
            LSLASTNodeList<LSLExpression>* args = expr->getArguments();
            if (sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llDumpList2String") && args && args->getNumChildren() == 2)
            {
                LSLConstant* cv = args->getChild(1)->getConstantValue();
                found.dumps += cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue() ? 1 : 0;
            }
            return true;
        }

        bool visit(LSLGlobalVariable* global) override
        {
            mGlobal = true;
            visitChildren(global);
            mGlobal = false;
            return false;
        }
        bool visit(LSLListExpression* expr) override
        {
            found.used.insert(mGlobal ? ListHelpers::GLOBAL_LITERAL : expr->hasChildren() ? ListHelpers::LITERAL : ListHelpers::EMPTY);
            return true;
        }
        bool visit(LSLTypecastExpression* expr) override
        {
            if (expr->getIType() == LST_LIST)
            {
                found.used.insert(ListHelpers::CAST);
            }
            else if (expr->getIType() == LST_STRING && expr->getChildExpr() && expr->getChildExpr()->getIType() == LST_LIST)
            {
                found.used.insert(ListHelpers::TO_STRING);
            }
            return true;
        }
        bool visit(LSLBinaryExpression* expr) override
        {
            const LSLOperator op    = expr->getOperation();
            LSLExpression*    left  = expr->getLHS();
            LSLExpression*    right = expr->getRHS();
            if ((op == OP_PLUS || op == OP_ADD_ASSIGN) && expr->getIType() == LST_LIST && left && right)
            {
                found.used.insert(left->getIType() == LST_LIST ? ListHelpers::APPEND | static_cast<U32>(right->getIType()) : ListHelpers::PREPEND);
            }
            return true;
        }

    private:
        bool mGlobal = false;
    };

    // Rewrites made for size alone, once the rounds are done: each hides a
    // value the folder could have used -- a list's literal made a sum is
    // no longer a constant -- so they wait until nothing more will fold.
    class Shapes : public ASTVisitor, public Pass
    {
    public:
        // Integers' and conditions' shapes, and increments', which the
        // code makes of plain instructions; and lists', which Mono makes
        // of helpers its assembly references once each, so that what they
        // save at each place a helper new to the script can cost more than
        // -- weighed, where it can (once()).
        enum class Stage : U8
        {
            Values,
            Lists
        };
        Shapes(Ctx& c, Report& r, const ALLSLOptimizer::Options& o, Stage stage) : Pass(c, r, o), mCosts(ALLSLCosts::of(o.target)), mStage(stage) {}

        // An increment whose value nothing reads, before rather than after.
        bool visit(LSLExpressionStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                pre(stmt->getExpr());
            }
            return false;
        }

        // Where only whether it is true counts; and an if whose condition
        // is a == with an else, turned around over a ^.
        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage != Stage::Values)
            {
                return false;
            }
            truth(stmt, 0);
            LSLExpression* cond = bare(stmt->getCheckExpr());
            LSLStatement*  yes  = stmt->getTrueBranch();
            LSLStatement*  no   = stmt->getFalseBranch();
            if (mCosts.xorForNotEqual && no && yes && cond && cond->getNodeSubType() == NODE_BINARY_EXPRESSION && cond->getOperation() == OP_EQ &&
                integerOperands(static_cast<LSLBinaryExpression*>(cond)))
            {
                const std::string before = report.wanted() ? render(cond) : std::string();
                cond->setOperation(OP_BIT_XOR);
                stmt->setTrueBranch(nullptr);
                stmt->setFalseBranch(nullptr);
                stmt->setTrueBranch(no);
                stmt->setFalseBranch(yes);
                if (report.wanted())
                {
                    report.note(stmt->getLoc(), "OptimizerSwappedOnXor", "wrote if ([1]) as if ([2]), its branches swapped", { before, render(cond) });
                }
                ++changes;
            }
            return false;
        }

        // The ! of a comparison as the comparison the other way.
        bool visit(LSLUnaryExpression* expr) override
        {
            visitChildren(expr);
            if (mStage == Stage::Values && !inGlobal(expr))
            {
                notComparison(expr);
            }
            return false;
        }
        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                truth(stmt, 0);
            }
            return false;
        }
        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage == Stage::Values)
            {
                truth(stmt, 1);
            }
            return false;
        }
        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            if (mStage != Stage::Values)
            {
                return false;
            }
            truth(stmt, 1);
            if (LSLASTNode* steps = stmt->getIncrExprs())
            {
                for (LSLASTNode* step = steps->getChild(0); step; step = step->getNext())
                {
                    pre(step);
                }
            }
            return false;
        }

        bool visit(LSLFunctionExpression* expr) override
        {
            visitChildren(expr);
            if (!inGlobal(expr))
            {
                if (mStage == Stage::Values)
                {
                    nullKeys(expr);
                }
                else
                {
                    // A list cast to a string, a list's helper on Mono.
                    libraryCast(expr);
                }
            }
            return false;
        }

        bool visit(LSLBinaryExpression* expr) override
        {
            if (inGlobal(expr))
            {
                return false;
            }
            // x = x + 1 as ++x, before its x + 1 is seen and made -~x.
            if (mStage == Stage::Values && increment(expr))
            {
                return false;
            }
            // A list's elements bare, seen as the author wrote them, before
            // what is left of a literal is made a sum below.
            if (mStage == Stage::Lists)
            {
                if (LSLExpression* made = elements(expr))
                {
                    made->visit(this);
                    return false;
                }
                visitChildren(expr);
                return false;
            }
            visitChildren(expr);
            if (bitTests(expr))
            {
                return false;
            }
            integers(expr);
            return false;
        }

        bool visit(LSLDeclaration* decl) override
        {
            visitChildren(decl);
            if (mStage == Stage::Values)
            {
                dropDefault(decl, decl->getSymbol(), false);
            }
            return false;
        }

        bool visit(LSLGlobalVariable* global) override
        {
            visitChildren(global);
            if (mStage == Stage::Values)
            {
                dropDefault(global, global->getSymbol(), true);
            }
            return false;
        }

        bool visit(LSLListExpression* expr) override
        {
            visitChildren(expr);
            if (mStage != Stage::Lists || !options.listadd || !mCosts.listAsSum || expr->getNumChildren() == 0)
            {
                return false;
            }
            // Not in a global's initializer, which must stay simple.
            if (inGlobal(expr))
            {
                return false;
            }
            // A sum takes its right side first, so its elements are taken
            // last to first, where a literal's are taken first to last:
            // where one of them changes something, and another is anything
            // but a constant, that one could see the change the other way
            // round.
            size_t changing = 0;
            size_t varying  = 0;
            for (LSLASTNode* child : *expr)
            {
                if (child->getIType() == LST_LIST || child->getIType() == LST_ERROR)
                {
                    return false;
                }
                // Read at another time than it was: a read of the clock
                // counts as a change here.
                changing += sideEffectFree(child) ? 0 : 1;
                varying += child->getConstantValue() ? 0 : 1;
            }
            if (changing > 0 && varying > 1)
            {
                return false;
            }
            // [a, b, c] as (list)a + b + c.
            const std::string           was = render(expr);
            std::vector<LSLExpression*> terms;
            terms.reserve(expr->getNumChildren());
            while (expr->hasChildren())
            {
                // takeChild leaves a null in the slot, dropped each time.
                terms.push_back(static_cast<LSLExpression*>(expr->takeChild(0)));
                expr->removeChild(expr->getChild(0));
            }
            LSLExpression* sum = nullptr;
            {
                const Uncounted uncounted(*ctx.context);
                sum = uncounted.made(ctx.allocator->newTracked<LSLTypecastExpression>(TYPE(LST_LIST), terms.front()));
                sum->setLoc(terms.front()->getLoc());
                for (size_t i = 1; i < terms.size(); ++i)
                {
                    sum = uncounted.made(ctx.allocator->newTracked<LSLBinaryExpression>(sum, OP_PLUS, uncounted.made(bracketed(terms[i]))));
                    sum->setType(TYPE(LST_LIST));
                    sum->setLoc(terms[i]->getLoc());
                }
            }
            report.note(expr->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { was, render(sum) });
            putInPlace(expr, sum, ctx.allocator);
            ++changes;
            return false;
        }

    public:
        // What list helpers the script called before a list's shapes, where
        // they are held once for the script (Mono): a list dumped with
        // nothing between is cast to a string only where the script casts
        // one already, or the dumps are enough to pay for the helper.
        void setHelpers(const ListHelpers* had) { mHad = had; }

    private:
        const ALLSLCosts&  mCosts;
        const Stage        mStage;
        const ListHelpers* mHad = nullptr;

        static bool inGlobal(LSLASTNode* node)
        {
            for (LSLASTNode* up = node->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    return true;
                }
            }
            return false;
        }

        void wrote(LSLASTNode* was, LSLExpression* now, const std::string& before)
        {
            if (report.wanted())
            {
                report.note(was->getLoc(), "OptimizerWroteAs", "wrote [1] as [2]", { before, render(now) });
            }
            ++changes;
        }

        // `op` over `x`, which is bracketed where what it is could read
        // otherwise after the operator: a sum, or -- after a minus -- a
        // minus of its own, which would read as a decrement.
        LSLExpression* unary(const Uncounted& uncounted, LSLOperator op, LSLExpression* x)
        {
            bool tight = false;
            switch (x->getNodeSubType())
            {
                case NODE_LVALUE_EXPRESSION:
                case NODE_FUNCTION_EXPRESSION:
                case NODE_PARENTHESIS_EXPRESSION:
                case NODE_TYPECAST_EXPRESSION:
                    tight = true;
                    break;
                case NODE_UNARY_EXPRESSION:
                    tight = op != OP_MINUS || (x->getOperation() != OP_MINUS && x->getOperation() != OP_PRE_DECR);
                    break;
                default:
                    break;
            }
            if (!tight)
            {
                auto* parens = uncounted.made(ctx.allocator->newTracked<LSLParenthesisExpression>(x));
                parens->setType(x->getType());
                parens->setLoc(x->getLoc());
                x = parens;
            }
            auto* made = uncounted.made(ctx.allocator->newTracked<LSLUnaryExpression>(x, op));
            made->setType(TYPE(LST_INTEGER));
            made->setLoc(x->getLoc());
            return made;
        }

        // `expr`, a binary expression, made `ops` over its operand in
        // `slot`, the last of them outermost: `-~x` is { OP_BIT_NOT,
        // OP_MINUS }.
        void over(LSLBinaryExpression* expr, int slot, std::initializer_list<LSLOperator> ops)
        {
            const std::string before = report.wanted() ? render(expr) : std::string();
            auto*             x      = static_cast<LSLExpression*>(expr->takeChild(slot));
            LSLExpression*    made   = x;
            {
                const Uncounted uncounted(*ctx.context);
                for (LSLOperator op : ops)
                {
                    made = unary(uncounted, op, made);
                }
            }
            made->setLoc(expr->getLoc());
            // Unary binds tighter than anything around it.
            LSLASTNode::replaceNode(expr, made);
            wrote(expr, made, before);
        }

        // Whether a value is never below -1: a find's, a count's, or a
        // local's that is set to one where it is declared and never after.
        static bool atLeastMinusOne(LSLExpression* x)
        {
            x = bare(x);
            if (x && x->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(x)->getMember())
            {
                LSLSymbol*  sym  = x->getSymbol();
                LSLASTNode* decl = sym && sym->getSubType() == SYM_LOCAL && sym->getAssignments() == 0 ? sym->getVarDecl() : nullptr;
                LSLASTNode* init = decl ? decl->getChild(1) : nullptr;
                return init && init->getNodeType() == NODE_EXPRESSION && atLeastMinusOne(static_cast<LSLExpression*>(init));
            }
            if (!x || x->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym   = static_cast<LSLFunctionExpression*>(x)->getSymbol();
            S32        least = 0, most = 0;
            return sym && sym->getSubType() == SYM_BUILTIN && ALLSLTraits::bounds(sym->getName(), least, most) && least >= -1;
        }

        static bool integerOperands(LSLBinaryExpression* expr)
        {
            return expr->getLHS() && expr->getRHS() && expr->getLHS()->getIType() == LST_INTEGER && expr->getRHS()->getIType() == LST_INTEGER;
        }

        // !(a < b) as a >= b, and each comparison's so: of integers, where
        // the order of any two is known; and of anything but lists for ==
        // and !=, which are each other's opposite whatever the values --
        // a list's != is how much longer one is.
        void notComparison(LSLUnaryExpression* expr)
        {
            LSLExpression* inner = bare(expr->getChildExpr());
            if (!mCosts.comparisonForNot || expr->getOperation() != OP_BOOLEAN_NOT || !inner || inner->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return;
            }
            auto*             cmp = static_cast<LSLBinaryExpression*>(inner);
            LSLOperator       to  = OP_NONE;
            const LSLOperator op  = cmp->getOperation();
            switch (op)
            {
                case OP_LESS: to = OP_GEQ; break;
                case OP_GEQ: to = OP_LESS; break;
                case OP_GREATER: to = OP_LEQ; break;
                case OP_LEQ: to = OP_GREATER; break;
                case OP_EQ: to = OP_NEQ; break;
                case OP_NEQ: to = OP_EQ; break;
                default: return;
            }
            LSLExpression* left  = cmp->getLHS();
            LSLExpression* right = cmp->getRHS();
            if (!left || !right)
            {
                return;
            }
            const bool equality = op == OP_EQ || op == OP_NEQ;
            const bool fits     = integerOperands(cmp) || (equality && left->getIType() != LST_LIST && right->getIType() != LST_LIST &&
                                                       left->getIType() != LST_ERROR && right->getIType() != LST_ERROR);
            if (!fits)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            cmp->getParent()->takeChild(cmp->getParentSlot());
            cmp->setOperation(to);
            putInPlace(expr, cmp, ctx.allocator);
            wrote(expr, cmp, before);
        }

        // a >= 5 as a > 4, and a <= 5 as a < 6, either way round, where the
        // constant one either side of it is an integer too.
        void strict(LSLBinaryExpression* expr)
        {
            const LSLOperator op = expr->getOperation();
            if (!mCosts.strictForInclusive || (op != OP_GEQ && op != OP_LEQ) || !integerOperands(expr))
            {
                return;
            }
            for (int slot = 0; slot < 2; ++slot)
            {
                LSLASTNode*  side = expr->getChild(slot);
                LSLConstant* cv   = side->getConstantValue();
                if (!cv || cv->getNodeSubType() != NODE_INTEGER_CONSTANT || expr->getChild(1 - slot)->getConstantValue())
                {
                    continue;
                }
                // Whether the constant is on the greater side: a >= C, C <= a.
                const bool    below = (op == OP_GEQ) == (slot == 1);
                const int32_t c     = static_cast<LSLIntegerConstant*>(cv)->getValue();
                if ((below && c == INT32_MIN) || (!below && c == INT32_MAX))
                {
                    return;
                }
                const std::string before = report.wanted() ? render(expr) : std::string();
                LSLASTNode::replaceNode(side, constant(ctx.integer(below ? c - 1 : c + 1), side));
                expr->setOperation(op == OP_GEQ ? OP_GREATER : OP_LESS);
                wrote(expr, expr, before);
                return;
            }
        }

        // What an integer comparison or sum comes to in fewer bytes, as a
        // value: x == -1 as !~x, x < 0 as !~x of a find, x + 1 as -~x,
        // x - 1 as ~-x, and two either way.
        // Bit tests of one value merged into one (L18), each form smaller on
        // every target (ALLSLCosts::bitTestsMerged): a chain of `x & c` one
        // bit each, all asked, as `!~(x | ~mask)`; of `!(x & c)` as
        // `!(x & mask)`; and an | of `x & c` as `x & mask` -- and, where only
        // whether it is true counts, an || of them (anyBit). Each form is
        // one this makes, so that a chain merges a link at a time. x is read
        // once where it was read at each test: only what is the same at any
        // time, an integer.
        enum class Test : U8
        {
            Set,     // x & c, or a !~(x | ~mask) this made: the bits all set
            Clear,   // !(x & c): the bits all clear
            Masked,  // x & c, for an |: the bits as they are
        };
        std::optional<std::pair<LSLExpression*, U32>> bitTest(LSLExpression* e, Test kind) const
        {
            e = bare(e);
            if (!e)
            {
                return std::nullopt;
            }
            if (kind == Test::Clear || kind == Test::Set)
            {
                // A ! over the rest: !(x & c), or !~(x | k).
                if (e->getNodeSubType() != NODE_UNARY_EXPRESSION || e->getOperation() != OP_BOOLEAN_NOT)
                {
                    if (kind == Test::Clear)
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    LSLExpression* inner = bare(static_cast<LSLUnaryExpression*>(e)->getChildExpr());
                    if (kind == Test::Clear)
                    {
                        return bitTest(inner, Test::Masked);
                    }
                    if (!inner || inner->getNodeSubType() != NODE_UNARY_EXPRESSION || inner->getOperation() != OP_BIT_NOT)
                    {
                        return std::nullopt;
                    }
                    const auto set = operand(bare(static_cast<LSLUnaryExpression*>(inner)->getChildExpr()), OP_BIT_OR);
                    return set ? std::optional<std::pair<LSLExpression*, U32>>({ set->first, ~set->second }) : std::nullopt;
                }
            }
            const auto masked = operand(e, OP_BIT_AND);
            // One bit each, for a test that asks it is set.
            if (masked && kind == Test::Set && (masked->second == 0 || (masked->second & (masked->second - 1)) != 0))
            {
                return std::nullopt;
            }
            return masked;
        }

        // `x op c` or `c op x`: x, an integer that is the same at any time,
        // and the constant.
        static std::optional<std::pair<LSLExpression*, U32>> operand(LSLExpression* e, LSLOperator op)
        {
            if (!e || e->getNodeSubType() != NODE_BINARY_EXPRESSION || e->getOperation() != op || e->getIType() != LST_INTEGER)
            {
                return std::nullopt;
            }
            for (int slot = 0; slot < 2; ++slot)
            {
                LSLConstant*   cv = e->getChild(slot)->getConstantValue();
                LSLExpression* x  = static_cast<LSLExpression*>(e->getChild(1 - slot));
                if (cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT && !x->getConstantValue() && x->getIType() == LST_INTEGER && sideEffectFree(x))
                {
                    return std::make_pair(x, static_cast<U32>(static_cast<LSLIntegerConstant*>(cv)->getValue()));
                }
            }
            return std::nullopt;
        }

        // The tests a chain of `op` is made of, all of one x; their x and
        // their bits together.
        std::optional<std::pair<LSLExpression*, U32>> chain(LSLBinaryExpression* expr, LSLOperator op, Test kind) const
        {
            std::vector<LSLExpression*>                 leaves;
            const std::function<void(LSLExpression*)> gather = [&](LSLExpression* e) {
                LSLExpression* inner = bare(e);
                if (inner && inner->getNodeSubType() == NODE_BINARY_EXPRESSION && inner->getOperation() == op)
                {
                    gather(static_cast<LSLBinaryExpression*>(inner)->getLHS());
                    gather(static_cast<LSLBinaryExpression*>(inner)->getRHS());
                    return;
                }
                leaves.push_back(e);
            };
            gather(expr->getLHS());
            gather(expr->getRHS());
            LSLExpression* x    = nullptr;
            std::string    said;
            U32            bits = 0;
            for (LSLExpression* leaf : leaves)
            {
                const auto test = bitTest(leaf, kind);
                if (!test)
                {
                    return std::nullopt;
                }
                const std::string text = render(test->first);
                if (x && text != said)
                {
                    return std::nullopt;
                }
                x    = test->first;
                said = text;
                bits |= test->second;
            }
            return x ? std::optional<std::pair<LSLExpression*, U32>>({ x, bits }) : std::nullopt;
        }

        // `x op mask`, x taken from where it stands.
        LSLExpression* withMask(const Uncounted& uncounted, LSLExpression* x, LSLOperator op, U32 mask, LSLASTNode* at)
        {
            LSLASTNode* parent = x->getParent();
            parent->takeChild(x->getParentSlot());
            auto* made = uncounted.made(ctx.allocator->newTracked<LSLBinaryExpression>(x, op, constant(ctx.integer(static_cast<int32_t>(mask)), at)));
            made->setType(TYPE(LST_INTEGER));
            made->setLoc(at->getLoc());
            return made;
        }

        bool bitTests(LSLBinaryExpression* expr)
        {
            if (!mCosts.bitTestsMerged || inGlobal(expr))
            {
                return false;
            }
            const LSLOperator op = expr->getOperation();
            std::optional<std::pair<LSLExpression*, U32>> set, clear, masked;
            if (op == OP_BOOLEAN_AND)
            {
                set   = chain(expr, op, Test::Set);
                clear = set ? std::nullopt : chain(expr, op, Test::Clear);
            }
            else if (op == OP_BIT_OR)
            {
                masked = chain(expr, op, Test::Masked);
            }
            if (!set && !clear && !masked)
            {
                return false;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            LSLExpression*    made   = nullptr;
            {
                const Uncounted uncounted(*ctx.context);
                if (set)
                {
                    made = unary(uncounted, OP_BOOLEAN_NOT, unary(uncounted, OP_BIT_NOT, withMask(uncounted, set->first, OP_BIT_OR, ~set->second, expr)));
                }
                else if (clear)
                {
                    made = unary(uncounted, OP_BOOLEAN_NOT, withMask(uncounted, clear->first, OP_BIT_AND, clear->second, expr));
                }
                else
                {
                    made = withMask(uncounted, masked->first, OP_BIT_AND, masked->second, expr);
                }
            }
            made->setLoc(expr->getLoc());
            putInPlace(expr, made, ctx.allocator);
            wrote(expr, made, before);
            return true;
        }

        // An || of `x & c` where only whether it is true counts: `x & mask`.
        bool anyBit(LSLBinaryExpression* expr)
        {
            const auto any = mCosts.bitTestsMerged ? chain(expr, OP_BOOLEAN_OR, Test::Masked) : std::nullopt;
            if (!any)
            {
                return false;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            LSLExpression*    made   = nullptr;
            {
                const Uncounted uncounted(*ctx.context);
                made = withMask(uncounted, any->first, OP_BIT_AND, any->second, expr);
            }
            putInPlace(expr, made, ctx.allocator);
            wrote(expr, made, before);
            return true;
        }

        void integers(LSLBinaryExpression* expr)
        {
            strict(expr);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (!left || !right || expr->getIType() != LST_INTEGER || left->getIType() != LST_INTEGER || right->getIType() != LST_INTEGER)
            {
                return;
            }
            // Two numbers are the folder's, whose one number is smaller than
            // any shape of them: 1 + 2 is 3, never -~-~1 -- though a run that
            // stops before it folds leaves them as they are.
            if (left->getNodeSubType() == NODE_CONSTANT_EXPRESSION && right->getNodeSubType() == NODE_CONSTANT_EXPRESSION)
            {
                return;
            }
            switch (expr->getOperation())
            {
                case OP_EQ:
                    if (mCosts.notComplementForMinusOne && (isInteger(right, -1) || isInteger(left, -1)))
                    {
                        over(expr, isInteger(right, -1) ? 0 : 1, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_LESS:
                    if (mCosts.notComplementForMinusOne && isInteger(right, 0) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_GREATER:
                    if (mCosts.notComplementForMinusOne && isInteger(left, 0) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT, OP_BOOLEAN_NOT });
                    }
                    return;
                case OP_PLUS:
                    for (int slot = 0; slot < 2 && mCosts.negateComplementForIncrement; ++slot)
                    {
                        LSLExpression* other = slot == 0 ? right : left;
                        if (isInteger(other, 1))
                        {
                            over(expr, slot, { OP_BIT_NOT, OP_MINUS });
                            return;
                        }
                        if (isInteger(other, 2))
                        {
                            over(expr, slot, { OP_BIT_NOT, OP_MINUS, OP_BIT_NOT, OP_MINUS });
                            return;
                        }
                    }
                    return;
                case OP_MINUS:
                    if (mCosts.complementNegateForDecrement && isInteger(right, 1))
                    {
                        over(expr, 0, { OP_MINUS, OP_BIT_NOT });
                    }
                    else if (mCosts.complementNegateForDecrement && isInteger(right, 2))
                    {
                        over(expr, 0, { OP_MINUS, OP_BIT_NOT, OP_MINUS, OP_BIT_NOT });
                    }
                    return;
                default:
                    return;
            }
        }

        // What only counts as true or false -- a condition, an operand of
        // !, && or || in one -- in fewer bytes: x != -1 as ~x, and x > -1
        // and x >= 0 as ~x of a find.
        void truth(LSLASTNode* parent, int slot)
        {
            LSLASTNode* child = parent->getChild(slot);
            if (!child || child->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            LSLExpression* inner = bare(static_cast<LSLExpression*>(child));
            if (!inner || inGlobal(inner))
            {
                return;
            }
            if (inner->getNodeSubType() == NODE_UNARY_EXPRESSION && inner->getOperation() == OP_BOOLEAN_NOT)
            {
                truth(inner, 0);
                return;
            }
            // A string's length is nothing where the string is.
            if (inner->getNodeSubType() == NODE_FUNCTION_EXPRESSION && mCosts.emptyForLength)
            {
                auto*                          call = static_cast<LSLFunctionExpression*>(inner);
                LSLSymbol*                     sym  = call->getSymbol();
                LSLASTNodeList<LSLExpression>* args = call->getArguments();
                if (sym && sym->getSubType() == SYM_BUILTIN && !strcmp(sym->getName(), "llStringLength") && args && args->getNumChildren() == 1)
                {
                    const std::string before = report.wanted() ? render(call) : std::string();
                    auto*             text   = static_cast<LSLExpression*>(args->takeChild(0));
                    auto*             made   = ctx.allocator->newTracked<LSLBinaryExpression>(text, OP_NEQ, constant(ctx.string(std::string()), call));
                    made->setType(TYPE(LST_INTEGER));
                    made->setLoc(call->getLoc());
                    putInPlace(call, made, ctx.allocator);
                    wrote(call, made, before);
                }
                return;
            }
            if (inner->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return;
            }
            auto*          expr  = static_cast<LSLBinaryExpression*>(inner);
            LSLExpression* left  = expr->getLHS();
            LSLExpression* right = expr->getRHS();
            if (expr->getOperation() == OP_BOOLEAN_OR && anyBit(expr))
            {
                return;
            }
            if (expr->getOperation() == OP_BOOLEAN_AND || expr->getOperation() == OP_BOOLEAN_OR)
            {
                truth(expr, 0);
                truth(expr, 1);
                // Either true is the two bits or'd true; LSL runs both
                // sides of || either way.
                if (expr->getOperation() == OP_BOOLEAN_OR && mCosts.bitOrForOr && integerOperands(expr))
                {
                    const std::string before = report.wanted() ? render(expr) : std::string();
                    expr->setOperation(OP_BIT_OR);
                    wrote(expr, expr, before);
                }
                return;
            }
            if (!left || !right || left->getIType() != LST_INTEGER || right->getIType() != LST_INTEGER)
            {
                return;
            }
            if (expr->getOperation() == OP_NEQ && !isInteger(right, -1) && !isInteger(left, -1))
            {
                // Different is the bits differing.
                if (mCosts.xorForNotEqual)
                {
                    const std::string before = report.wanted() ? render(expr) : std::string();
                    expr->setOperation(OP_BIT_XOR);
                    wrote(expr, expr, before);
                }
                return;
            }
            if (!mCosts.complementForNotMinusOne)
            {
                return;
            }
            switch (expr->getOperation())
            {
                case OP_NEQ:
                    if (isInteger(right, -1) || isInteger(left, -1))
                    {
                        over(expr, isInteger(right, -1) ? 0 : 1, { OP_BIT_NOT });
                    }
                    return;
                case OP_GREATER:
                    if (isInteger(right, -1) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT });
                    }
                    return;
                case OP_GEQ:
                    if (isInteger(right, 0) && atLeastMinusOne(left))
                    {
                        over(expr, 0, { OP_BIT_NOT });
                    }
                    return;
                case OP_LESS:
                    if (isInteger(left, -1) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT });
                    }
                    return;
                case OP_LEQ:
                    if (isInteger(left, 0) && atLeastMinusOne(right))
                    {
                        over(expr, 1, { OP_BIT_NOT });
                    }
                    return;
                default:
                    return;
            }
        }

        // A library call that a cast to a string says the same as:
        // llDumpList2String(l, "") as (string)l, and the one detail
        // llGetObjectDetails gives as a string -- or "", as both say where
        // there is no such object.
        void libraryCast(LSLFunctionExpression* expr)
        {
            LSLSymbol*                     sym  = expr->getSymbol();
            LSLASTNodeList<LSLExpression>* args = expr->getArguments();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !args || args->getNumChildren() != 2)
            {
                return;
            }
            auto*       first  = static_cast<LSLExpression*>(args->getChild(0));
            LSLASTNode* second = args->getChild(1);
            bool        cast   = false;
            if (mCosts.castForDump && !strcmp(sym->getName(), "llDumpList2String"))
            {
                LSLConstant* cv   = second->getConstantValue();
                const bool   pays = !mHad || mHad->castsToString() || mHad->dumps * mCosts.listShapeLeast > mCosts.listHelperMost;
                cast              = pays && cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue();
            }
            else if (mCosts.castForDetail && !strcmp(sym->getName(), "llList2String") && isInteger(second, 0))
            {
                LSLExpression* details = bare(first);
                LSLSymbol*     inner   = details && details->getNodeSubType() == NODE_FUNCTION_EXPRESSION ? details->getSymbol() : nullptr;
                LSLASTNodeList<LSLExpression>* asked = inner ? static_cast<LSLFunctionExpression*>(details)->getArguments() : nullptr;
                LSLASTNode*                    which = asked && asked->getNumChildren() == 2 ? asked->getChild(1) : nullptr;
                LSLExpression*                 named = which && which->getNodeType() == NODE_EXPRESSION ? bare(static_cast<LSLExpression*>(which)) : nullptr;
                // One detail asked for: a literal of one, or that literal as
                // the cast listadd makes of it.
                const bool one = named && ((named->getNodeSubType() == NODE_LIST_EXPRESSION && named->getNumChildren() == 1) ||
                                           (named->getNodeSubType() == NODE_TYPECAST_EXPRESSION && named->getIType() == LST_LIST &&
                                            static_cast<LSLTypecastExpression*>(named)->getChildExpr() &&
                                            static_cast<LSLTypecastExpression*>(named)->getChildExpr()->getIType() != LST_LIST));
                cast = inner && inner->getSubType() == SYM_BUILTIN && !strcmp(inner->getName(), "llGetObjectDetails") && one;
            }
            if (!cast)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            args->takeChild(0);
            auto* made = ctx.allocator->newTracked<LSLTypecastExpression>(TYPE(LST_STRING), first);
            made->setType(TYPE(LST_STRING));
            made->setLoc(expr->getLoc());
            putInPlace(expr, made, ctx.allocator);
            wrote(expr, made, before);
        }

        // Whether a constant is no key a library function would find
        // anything by: NULL_KEY, or anything that is not a key's form.
        static bool nullKey(LSLConstant* cv)
        {
            if (!cv || (cv->getIType() != LST_STRING && cv->getIType() != LST_KEY))
            {
                return false;
            }
            const std::string_view text = cv->getIType() == LST_STRING ? static_cast<LSLStringConstant*>(cv)->getValue()
                                                                        : static_cast<LSLKeyConstant*>(cv)->getValue();
            if (text.size() != 36)
            {
                return true;
            }
            bool zero = true;
            for (size_t i = 0; i < text.size(); ++i)
            {
                const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
                if (dash ? text[i] != '-' : !isxdigit(static_cast<unsigned char>(text[i])))
                {
                    return true;
                }
                zero = zero && (dash || text[i] == '0');
            }
            return zero;
        }

        // A library function given NULL_KEY, or no key at all, for a key
        // it looks something up by, given "" instead, which it takes the
        // same way; not the two that pass a key on as it came.
        void nullKeys(LSLFunctionExpression* expr)
        {
            LSLSymbol* sym = expr->getSymbol();
            if (!mCosts.emptyForNullKey || !sym || sym->getSubType() != SYM_BUILTIN || !sym->getFunctionDecl() || !expr->getArguments() ||
                !strcmp(sym->getName(), "llMessageLinked") || !strcmp(sym->getName(), "llRemoteDataReply"))
            {
                return;
            }
            std::vector<LSLASTNode*> nulls;
            LSLASTNode*              param = sym->getFunctionDecl()->getChild(0);
            for (LSLASTNode* arg = expr->getArguments()->getChild(0); arg && param; arg = arg->getNext(), param = param->getNext())
            {
                LSLConstant* cv = arg->getConstantValue();
                const bool   empty = cv && cv->getIType() == LST_STRING && !*static_cast<LSLStringConstant*>(cv)->getValue();
                if (param->getIType() == LST_KEY && !empty && nullKey(cv))
                {
                    nulls.push_back(arg);
                }
            }
            for (LSLASTNode* arg : nulls)
            {
                const std::string before = report.wanted() ? render(arg) : std::string();
                auto*             made   = constant(ctx.string(std::string()), arg);
                LSLASTNode::replaceNode(arg, made);
                wrote(arg, made, before);
            }
        }

        // x = x + 1, x = 1 + x and x += 1 as ++x, and x = x - 1 and x -= 1
        // as --x, of an integer variable (ALLSLCosts::incrementForAssign).
        // ++x gives what the assignment gave, x after it, so a value read
        // of it reads the same. True where it was written.
        bool increment(LSLBinaryExpression* expr)
        {
            if (!mCosts.incrementForAssign || expr->getIType() != LST_INTEGER)
            {
                return false;
            }
            const LSLOperator op     = expr->getOperation();
            LSLExpression*    target = expr->getLHS();
            LSLExpression*    value  = expr->getRHS();
            if (!target || !value || target->getNodeSubType() != NODE_LVALUE_EXPRESSION || static_cast<LSLLValueExpression*>(target)->getMember() ||
                target->getIType() != LST_INTEGER || !target->getSymbol())
            {
                return false;
            }
            LSLSymbol* sym   = target->getSymbol();
            const auto reads = [sym](LSLExpression* x) {
                x = bare(x);
                return x && x->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(x)->getMember() && x->getSymbol() == sym;
            };
            int step = 0;
            if ((op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN) && isInteger(value, 1))
            {
                step = op == OP_ADD_ASSIGN ? 1 : -1;
            }
            else if (op == OP_ASSIGN && bare(value)->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                auto* sum = static_cast<LSLBinaryExpression*>(bare(value));
                if (sum->getOperation() == OP_PLUS && ((reads(sum->getLHS()) && isInteger(sum->getRHS(), 1)) || (isInteger(sum->getLHS(), 1) && reads(sum->getRHS()))))
                {
                    step = 1;
                }
                else if (sum->getOperation() == OP_MINUS && reads(sum->getLHS()) && isInteger(sum->getRHS(), 1))
                {
                    step = -1;
                }
            }
            if (step == 0)
            {
                return false;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            auto*             x      = static_cast<LSLExpression*>(expr->takeChild(0));
            LSLExpression*    made   = nullptr;
            {
                const Uncounted uncounted(*ctx.context);
                made = uncounted.made(ctx.allocator->newTracked<LSLUnaryExpression>(x, step > 0 ? OP_PRE_INCR : OP_PRE_DECR));
            }
            made->setType(TYPE(LST_INTEGER));
            made->setLoc(expr->getLoc());
            LSLASTNode::replaceNode(expr, made);
            wrote(expr, made, before);
            return true;
        }

        // A declaration's initializer that is its type's default, left out,
        // where that loads smaller: `integer x = 0;` as `integer x;`. Its
        // value exactly -- a float's +0, not -0; a key's "", not NULL_KEY; a
        // rotation's <0, 0, 0, 1> -- since what is left out is what LSL
        // gives it.
        void dropDefault(LSLASTNode* decl, LSLSymbol* sym, bool global)
        {
            LSLASTNode* init = decl->getChild(1);
            if (!sym || !init || init->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            const LSLIType type = sym->getIType();
            const bool     drop = global ? type == LST_INTEGER && mCosts.dropIntegerGlobalDefault
                                         : (type == LST_INTEGER && mCosts.dropIntegerDefault) || (type == LST_FLOATINGPOINT && mCosts.dropFloatDefault) ||
                                               (type == LST_KEY && mCosts.dropKeyDefault) || (type == LST_VECTOR && mCosts.dropVectorDefault) ||
                                               (type == LST_QUATERNION && mCosts.dropRotationDefault);
            LSLConstant* cv = drop ? init->getConstantValue() : nullptr;
            if (!cv)
            {
                return;
            }
            const auto zero = [](double v) { return v == 0.0 && !std::signbit(v); };
            bool       isDefault = false;
            switch (cv->getIType())
            {
                case LST_INTEGER: isDefault = type == LST_INTEGER && static_cast<LSLIntegerConstant*>(cv)->getValue() == 0; break;
                case LST_FLOATINGPOINT: isDefault = type == LST_FLOATINGPOINT && zero(static_cast<LSLFloatConstant*>(cv)->getValue()); break;
                case LST_STRING: isDefault = type == LST_KEY && !*static_cast<LSLStringConstant*>(cv)->getValue(); break;
                case LST_KEY: isDefault = type == LST_KEY && !*static_cast<LSLKeyConstant*>(cv)->getValue(); break;
                case LST_VECTOR:
                {
                    const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                    isDefault        = type == LST_VECTOR && v && zero(v->x) && zero(v->y) && zero(v->z);
                    break;
                }
                case LST_QUATERNION:
                {
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                    isDefault           = type == LST_QUATERNION && q && zero(q->x) && zero(q->y) && zero(q->z) && q->s == 1.0f;
                    break;
                }
                default: break;
            }
            // An integer 0 a float is set to is its default too.
            if (cv->getIType() == LST_INTEGER && type == LST_FLOATINGPOINT)
            {
                isDefault = static_cast<LSLIntegerConstant*>(cv)->getValue() == 0;
            }
            if (!isDefault)
            {
                return;
            }
            decl->setChild(1, nullptr);
            report.note(decl->getLoc(), "OptimizerDroppedDefault", "left out [1]'s initializer, which is its default", { sym->getName() });
            ++changes;
        }

        // x++ and x-- whose value nothing reads, as ++x and --x.
        void pre(LSLASTNode* node)
        {
            if (!mCosts.preForPost || !node || node->getNodeType() != NODE_EXPRESSION || node->getNodeSubType() != NODE_UNARY_EXPRESSION)
            {
                return;
            }
            auto*             expr = static_cast<LSLExpression*>(node);
            const LSLOperator op   = expr->getOperation();
            if (op != OP_POST_INCR && op != OP_POST_DECR)
            {
                return;
            }
            const std::string before = report.wanted() ? render(expr) : std::string();
            expr->setOperation(op == OP_POST_INCR ? OP_PRE_INCR : OP_PRE_DECR);
            wrote(expr, expr, before);
        }

        // A list's element added bare: l + [a, b] as l + a + b, [a] + l as
        // a + l, l + (list)x as l + x, and l += [a] as l += a. What was
        // made, where anything was.
        LSLExpression* elements(LSLBinaryExpression* expr)
        {
            if (!mCosts.elementForList || expr->getIType() != LST_LIST)
            {
                return nullptr;
            }
            LSLExpression*    left  = expr->getLHS();
            LSLExpression*    right = expr->getRHS();
            const LSLOperator op    = expr->getOperation();
            if (!left || !right || (op != OP_PLUS && op != OP_ADD_ASSIGN) || left->getIType() != LST_LIST)
            {
                return nullptr;
            }
            if (LSLExpression* made = appended(expr, left, right, op))
            {
                return made;
            }
            // [a] + l as a + l.
            LSLExpression* literal = bare(left);
            if (op == OP_PLUS && right->getIType() == LST_LIST && literal->getNodeSubType() == NODE_LIST_EXPRESSION &&
                literal->getNumChildren() == 1 && literal->getChild(0)->getIType() != LST_LIST)
            {
                const std::string before = report.wanted() ? render(expr) : std::string();
                LSLASTNode*       a      = literal->takeChild(0);
                literal->removeChild(literal->getChild(0));
                LSLASTNode::replaceNode(left, a);
                wrote(expr, expr, before);
                return expr;
            }
            return nullptr;
        }

        // A list with elements added to it: those elements bare.
        LSLExpression* appended(LSLBinaryExpression* expr, LSLExpression* left, LSLExpression* right, LSLOperator op)
        {
            LSLExpression* added = bare(right);
            // l + (list)x as l + x.
            if (op == OP_PLUS && added->getNodeSubType() == NODE_TYPECAST_EXPRESSION && added->getIType() == LST_LIST)
            {
                LSLExpression* x = static_cast<LSLTypecastExpression*>(added)->getChildExpr();
                if (!x || x->getIType() == LST_LIST)
                {
                    return nullptr;
                }
                const std::string before = report.wanted() ? render(expr) : std::string();
                added->takeChild(0);
                LSLASTNode::replaceNode(right, x);
                wrote(expr, expr, before);
                return expr;
            }
            if (added->getNodeSubType() != NODE_LIST_EXPRESSION || added->getNumChildren() == 0)
            {
                return nullptr;
            }
            const size_t count = added->getNumChildren();
            if ((op == OP_ADD_ASSIGN && count != 1) || (mCosts.elementsForListMost > 0 && count > size_t(mCosts.elementsForListMost)))
            {
                return nullptr;
            }
            // Each element's order as a sum's -- last first -- against a
            // literal's, which is known to nobody: as for listadd.
            size_t changing = 0;
            size_t varying  = 0;
            for (LSLASTNode* child : *added)
            {
                if (child->getIType() == LST_LIST || child->getIType() == LST_ERROR)
                {
                    return nullptr;
                }
                changing += sideEffectFree(child) ? 0 : 1;
                varying += child->getConstantValue() ? 0 : 1;
            }
            if (changing > 0 && varying > 1)
            {
                return nullptr;
            }
            const std::string           before = report.wanted() ? render(expr) : std::string();
            std::vector<LSLExpression*> terms;
            while (added->hasChildren())
            {
                terms.push_back(static_cast<LSLExpression*>(added->takeChild(0)));
                added->removeChild(added->getChild(0));
            }
            if (op == OP_ADD_ASSIGN)
            {
                LSLASTNode::replaceNode(right, terms.front());
                wrote(expr, expr, before);
                return expr;
            }
            // l + a + b, each on the right of its own +.
            auto*          l   = static_cast<LSLExpression*>(expr->takeChild(0));
            LSLExpression* sum = l;
            {
                const Uncounted uncounted(*ctx.context);
                for (LSLExpression* term : terms)
                {
                    sum = uncounted.made(ctx.allocator->newTracked<LSLBinaryExpression>(sum, OP_PLUS, uncounted.made(bracketed(term))));
                    sum->setType(TYPE(LST_LIST));
                    sum->setLoc(term->getLoc());
                }
            }
            sum->setLoc(expr->getLoc());
            putInPlace(expr, sum, ctx.allocator);
            wrote(expr, sum, before);
            return sum;
        }

        // An element of a list as a sum has it, after the first: on the
        // right of a +, which binds tighter than every operator but a
        // product and what is unary, and so bracketed where it is any
        // other. (The first goes under the cast, which the printer
        // brackets for itself.)
        LSLExpression* bracketed(LSLExpression* element)
        {
            bool bare = false;
            switch (element->getNodeSubType())
            {
                case NODE_CONSTANT_EXPRESSION:
                case NODE_LVALUE_EXPRESSION:
                case NODE_PARENTHESIS_EXPRESSION:
                case NODE_FUNCTION_EXPRESSION:
                case NODE_PRINT_EXPRESSION:
                case NODE_VECTOR_EXPRESSION:
                case NODE_QUATERNION_EXPRESSION:
                case NODE_LIST_EXPRESSION:
                    bare = true;
                    break;
                case NODE_UNARY_EXPRESSION:
                case NODE_TYPECAST_EXPRESSION:
                    bare = true;
                    break;
                case NODE_BINARY_EXPRESSION:
                    bare = element->getOperation() == OP_MUL || element->getOperation() == OP_DIV || element->getOperation() == OP_MOD;
                    break;
                default:
                    break;
            }
            if (bare)
            {
                return element;
            }
            auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(element);
            parens->setType(element->getType());
            parens->setLoc(element->getLoc());
            return parens;
        }
    };
}

    ListHelpers listHelpers(LSLScript* script)
    {
        ListHelpersFinder finder;
        script->visit(&finder);
        return std::move(finder.found);
    }

    int shape(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script, ShapeStage stage, const ListHelpers* had)
    {
        Shapes shapes(ctx, report, options, stage == ShapeStage::Values ? Shapes::Stage::Values : Shapes::Stage::Lists);
        shapes.setHelpers(had);
        script->visit(&shapes);
        return shapes.changes;
    }
}
