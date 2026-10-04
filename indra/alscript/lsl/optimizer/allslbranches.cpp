/**
 * @file allslbranches.cpp
 * @brief The LSL optimizer's branches and loops in fewer jumps.
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

#include "allslbranches.h"

#include "allslcosts.h"
#include "allsleffects.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>

namespace ALLSLPasses
{
namespace
{
    // Each rewrite is made a block at a time, the block's statements read
    // into a list, changed there and put back: a statement moved is the
    // one it was, counted neither out nor in (setStatements), so that what
    // the references say holds until the run finds them again after the
    // pass. Each rewrite takes away a jump, an else or a statement, so the
    // block's rewrites come to an end.
    class Branches : public ASTVisitor, public Pass
    {
    public:
        Branches(Ctx& c, Report& r, const ALLSLOptimizer::Options& o) : Pass(c, r, o), mCosts(ALLSLCosts::of(o.target)) {}

        bool visit(LSLCompoundStatement* block) override
        {
            visitChildren(block);
            while (rewrite(block))
            {
            }
            return false;
        }

    private:
        typedef std::vector<LSLASTNode*> Statements;

        const ALLSLCosts& mCosts;

        static bool present(LSLASTNode* n) { return n && n->getNodeType() != NODE_NULL; }

        static bool isStatement(LSLASTNode* n, LSLNodeSubType type)
        {
            return n && n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == type;
        }

        // A branch's statements: a block's, or the one it is; none of nothing.
        static Statements statementsOf(LSLASTNode* branch)
        {
            Statements out;
            if (!present(branch) || isStatement(branch, NODE_NOP_STATEMENT))
            {
                return out;
            }
            if (isStatement(branch, NODE_COMPOUND_STATEMENT))
            {
                for (LSLASTNode* stmt = branch->getChild(0); stmt; stmt = stmt->getNext())
                {
                    out.push_back(stmt);
                }
                return out;
            }
            out.push_back(branch);
            return out;
        }

        // The one statement a branch is, a block of one included.
        static LSLASTNode* single(LSLASTNode* branch)
        {
            const Statements stmts = statementsOf(branch);
            return stmts.size() == 1 ? stmts.front() : nullptr;
        }

        // `x = value;`, of a variable whole: x and the value.
        static std::optional<std::pair<LSLLValueExpression*, LSLExpression*>> assignment(LSLASTNode* stmt)
        {
            if (!isStatement(stmt, NODE_EXPRESSION_STATEMENT))
            {
                return std::nullopt;
            }
            LSLExpression* expr = static_cast<LSLExpressionStatement*>(stmt)->getExpr();
            if (!expr || expr->getNodeSubType() != NODE_BINARY_EXPRESSION || expr->getOperation() != OP_ASSIGN)
            {
                return std::nullopt;
            }
            auto* target = static_cast<LSLBinaryExpression*>(expr)->getLHS();
            if (!target || target->getNodeSubType() != NODE_LVALUE_EXPRESSION || static_cast<LSLLValueExpression*>(target)->getMember() ||
                !target->getSymbol())
            {
                return std::nullopt;
            }
            return std::make_pair(static_cast<LSLLValueExpression*>(target), static_cast<LSLBinaryExpression*>(expr)->getRHS());
        }

        // `return value;`: the value.
        static LSLExpression* returned(LSLASTNode* stmt)
        {
            if (!isStatement(stmt, NODE_RETURN_STATEMENT) || !present(stmt->getChild(0)) || stmt->getChild(0)->getNodeType() != NODE_EXPRESSION)
            {
                return nullptr;
            }
            return static_cast<LSLExpression*>(stmt->getChild(0));
        }

        // Whether a subtree names a symbol.
        static bool mentions(LSLASTNode* root, LSLSymbol* sym)
        {
            bool found = false;
            eachNode(root, [&](LSLASTNode* n) { found = found || (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym); });
            return found;
        }

        // How many times a subtree names a symbol.
        static int mentionsOf(LSLASTNode* root, LSLSymbol* sym)
        {
            int count = 0;
            eachNode(root, [&](LSLASTNode* n) { count += n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym ? 1 : 0; });
            return count;
        }

        static LSLASTNode* callable(LSLASTNode* node)
        {
            while (node && node->getNodeType() != NODE_GLOBAL_FUNCTION && node->getNodeType() != NODE_EVENT_HANDLER)
            {
                node = node->getParent();
            }
            return node;
        }

        // Whether every name a statement reads or writes is declared outside
        // `outside`: what may move out of it and mean the same.
        static bool namesFromOutside(LSLASTNode* stmt, LSLASTNode* outside)
        {
            bool inside = false;
            eachNode(stmt, [&](LSLASTNode* n) {
                if (inside || n->getNodeType() != NODE_IDENTIFIER)
                {
                    return;
                }
                LSLSymbol* sym = static_cast<LSLIdentifier*>(n)->getSymbol();
                if (!sym || sym->getSymbolType() != SYM_VARIABLE || sym->getSubType() != SYM_LOCAL)
                {
                    return;
                }
                for (LSLASTNode* up = sym->getVarDecl(); up; up = up->getParent())
                {
                    if (up == outside)
                    {
                        inside = true;
                        return;
                    }
                }
            });
            return !inside;
        }

        // Whether a state change in `branch` stands under no if of its own:
        // in a function, which may change state only under an if, it must
        // stay under the one it is in.
        static bool bareStateChange(LSLASTNode* branch)
        {
            bool bare = false;
            const std::function<void(LSLASTNode*)> look = [&](LSLASTNode* n) {
                if (bare || !n)
                {
                    return;
                }
                if (isStatement(n, NODE_STATE_STATEMENT))
                {
                    bare = true;
                    return;
                }
                if (isStatement(n, NODE_IF_STATEMENT))
                {
                    return;
                }
                for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                {
                    look(child);
                }
            };
            look(branch);
            return bare;
        }

        bool inFunction(LSLASTNode* node) const
        {
            LSLASTNode* owner = callable(node);
            return owner && owner->getNodeType() == NODE_GLOBAL_FUNCTION;
        }

        // A branch made of statements: nothing, the one, or a block of them.
        LSLStatement* branchOf(const Statements& stmts, LSLASTNode* at)
        {
            if (stmts.empty())
            {
                auto* nop = ctx.allocator->newTracked<LSLNopStatement>();
                nop->setLoc(at->getLoc());
                return nop;
            }
            if (stmts.size() == 1 && !isStatement(stmts.front(), NODE_DECLARATION))
            {
                return static_cast<LSLStatement*>(stmts.front());
            }
            auto* block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
            block->setLoc(stmts.front()->getLoc());
            for (LSLASTNode* stmt : stmts)
            {
                block->pushChild(stmt);
            }
            return block;
        }

        // A statement's place left, as its parent sees it: taken from a
        // block, or from an if's or a loop's slot, where a null is put.
        static void detach(LSLASTNode* stmt)
        {
            LSLASTNode* parent = stmt->getParent();
            if (!parent)
            {
                return;
            }
            if (isStatement(parent, NODE_COMPOUND_STATEMENT))
            {
                parent->removeChild(stmt);
                return;
            }
            parent->takeChild(stmt->getParentSlot());
        }

        // A branch's statements taken out of it, in order.
        static Statements takeStatements(LSLASTNode* branch)
        {
            Statements stmts = statementsOf(branch);
            for (LSLASTNode* stmt : stmts)
            {
                detach(stmt);
            }
            return stmts;
        }

        // !(c), bracketed where c is an operation.
        LSLExpression* negation(LSLExpression* c)
        {
            LSLExpression* operand = c;
            if (c->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(c);
                parens->setType(c->getType());
                parens->setLoc(c->getLoc());
                operand = parens;
            }
            auto* negated = ctx.allocator->newTracked<LSLUnaryExpression>(operand, OP_BOOLEAN_NOT);
            negated->setType(TYPE(LST_INTEGER));
            negated->setLoc(c->getLoc());
            return negated;
        }

        // Whether a value is only ever 1 or 0.
        static bool truthValue(LSLExpression* x)
        {
            const std::optional<Range> r = x && x->getIType() == LST_INTEGER ? rangeOf(x) : std::nullopt;
            return r && r->least >= 0.0 && r->most <= 1.0;
        }

        static std::optional<S32> integerValue(LSLASTNode* n)
        {
            LSLConstant* cv = n ? n->getConstantValue() : nullptr;
            return cv && cv->getNodeSubType() == NODE_INTEGER_CONSTANT ? std::optional<S32>(static_cast<LSLIntegerConstant*>(cv)->getValue())
                                                                        : std::nullopt;
        }

        // c as the truth of itself, 1 or 0: as it is where it is one; !c,
        // or !!c, where it is not.
        LSLExpression* truthOf(LSLExpression* c, bool asTrue)
        {
            if (asTrue)
            {
                return truthValue(c) ? c : negation(negation(c));
            }
            return negation(c);
        }

        void noteAt(LSLASTNode* at, const char* key, const char* text, const std::string& before)
        {
            if (report.wanted())
            {
                report.note(at->getLoc(), key, text, { before });
            }
            ++changes;
        }

        // ---- one rewrite of a block, the first there is ---------------------------------

        bool rewrite(LSLCompoundStatement* block)
        {
            Statements stmts;
            for (LSLASTNode* stmt = block->getChild(0); stmt; stmt = stmt->getNext())
            {
                stmts.push_back(stmt);
            }
            for (size_t i = 0; i < stmts.size(); ++i)
            {
                LSLASTNode* stmt = stmts[i];
                if (isStatement(stmt, NODE_IF_STATEMENT))
                {
                    auto* branch = static_cast<LSLIfStatement*>(stmt);
                    if (emptyIf(block, stmts, i, branch) || truthChosen(block, stmts, i, branch) || sameWays(block, stmts, i, branch) ||
                        elseAfterStop(block, stmts, i, branch) || jumpOver(block, stmts, i, branch) || setBeforeIf(block, stmts, i, branch))
                    {
                        return true;
                    }
                }
                else if (isStatement(stmt, NODE_FOR_STATEMENT))
                {
                    auto* loop = static_cast<LSLForStatement*>(stmt);
                    if (countDown(block, stmts, i, loop) || forAsDo(block, stmts, i, loop))
                    {
                        return true;
                    }
                }
                else if (isStatement(stmt, NODE_WHILE_STATEMENT))
                {
                    if (whileAsDo(block, stmts, i, static_cast<LSLWhileStatement*>(stmt)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // ---- an if with nothing in it ----------------------------------------------------

        // An if with nothing to do either way: its check, where that does
        // something, or nothing.
        bool emptyIf(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            LSLExpression* c = stmt->getCheckExpr();
            if (!c || !statementsOf(stmt->getTrueBranch()).empty() || !statementsOf(stmt->getFalseBranch()).empty() || holdsLabel(stmt))
            {
                return false;
            }
            const std::string before = report.wanted() ? render(c) : std::string();
            const Uncounted   uncounted(*ctx.context);
            if (changesNothing(c))
            {
                stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
            {
                auto* run = ctx.allocator->newTracked<LSLExpressionStatement>(static_cast<LSLExpression*>(stmt->takeChild(0)));
                run->setLoc(stmt->getLoc());
                stmts[i] = run;
            }
            setStatements(block, stmts, *ctx.context);
            noteAt(stmt, "OptimizerRemovedEmptyIf", "removed if ([1]), which has nothing to do either way", before);
            return true;
        }

        // ---- a truth chosen by an if ------------------------------------------------------

        // `if (c) return 1; return 0;` as `return c;`, and `if (c) x = 1;
        // else x = 0;` as `x = c;` -- the other way round, `!c`; c as its
        // truth, `!!c`, where it is not only ever 1 or 0.
        bool truthChosen(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            LSLExpression* c = stmt->getCheckExpr();
            if (!c || c->getIType() != LST_INTEGER)
            {
                return false;
            }
            LSLASTNode* yes = single(stmt->getTrueBranch());
            LSLASTNode* no  = present(stmt->getFalseBranch()) ? single(stmt->getFalseBranch()) : nullptr;
            // Returns: the else's, or the statement after a lone if.
            const bool  lone  = !present(stmt->getFalseBranch()) && i + 1 < stmts.size();
            LSLASTNode* other = no ? no : lone ? stmts[i + 1] : nullptr;
            LSLASTNode* owner = callable(stmt);
            const bool  ints  = owner && owner->getNodeType() == NODE_GLOBAL_FUNCTION &&
                               static_cast<LSLGlobalFunction*>(owner)->getIdentifier()->getIType() == LST_INTEGER;
            if (LSLExpression* a = returned(yes); a && other && ints)
            {
                LSLExpression*           b  = returned(other);
                const std::optional<S32> va = integerValue(a);
                const std::optional<S32> vb = b ? integerValue(b) : std::nullopt;
                if (!va || !vb || *va == *vb || (*va != 0 && *va != 1) || (*vb != 0 && *vb != 1) || a->getIType() != LST_INTEGER)
                {
                    return false;
                }
                const std::string before = report.wanted() ? render(stmt) : std::string();
                const Uncounted   uncounted(*ctx.context);
                stmt->takeChild(0);
                auto* made = ctx.allocator->newTracked<LSLReturnStatement>(truthOf(c, *va == 1));
                made->setLoc(stmt->getLoc());
                stmts[i] = made;
                if (!no)
                {
                    stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1);
                }
                setStatements(block, stmts, *ctx.context);
                noteAt(stmt, "OptimizerReturnedTruth", "returned the truth of what [1] chose", before);
                return true;
            }
            const auto set   = assignment(yes);
            const auto unset = no ? assignment(no) : std::nullopt;
            if (!set || !unset || set->first->getSymbol() != unset->first->getSymbol() || set->first->getIType() != LST_INTEGER)
            {
                return false;
            }
            const std::optional<S32> va = integerValue(set->second);
            const std::optional<S32> vb = integerValue(unset->second);
            if (!va || !vb || *va == *vb || (*va != 0 && *va != 1) || (*vb != 0 && *vb != 1))
            {
                return false;
            }
            const std::string before = report.wanted() ? render(stmt) : std::string();
            const Uncounted   uncounted(*ctx.context);
            stmt->takeChild(0);
            auto* target = static_cast<LSLExpression*>(set->first->getParent()->takeChild(0));
            auto* value  = truthOf(c, *va == 1);
            auto* made   = ctx.allocator->newTracked<LSLBinaryExpression>(target, OP_ASSIGN, value);
            made->setType(target->getType());
            made->setLoc(stmt->getLoc());
            auto* line = ctx.allocator->newTracked<LSLExpressionStatement>(made);
            line->setLoc(stmt->getLoc());
            stmts[i] = line;
            setStatements(block, stmts, *ctx.context);
            noteAt(stmt, "OptimizerReturnedTruth", "returned the truth of what [1] chose", before);
            return true;
        }

        // ---- ways the same ---------------------------------------------------------------

        // An if whose ways are the same as one way, its check run first where
        // it does something; and the statements both ways end or begin
        // with, once after or before it -- before only where the check reads
        // nothing they change and could be read at another time.
        bool sameWays(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            LSLExpression* c = stmt->getCheckExpr();
            if (!c || !present(stmt->getFalseBranch()) || holdsLabel(stmt))
            {
                return false;
            }
            LSLStatement*     yes  = stmt->getTrueBranch();
            LSLStatement*     no   = stmt->getFalseBranch();
            const std::string was  = report.wanted() ? render(stmt) : std::string();
            if (render(yes) == render(no))
            {
                const Uncounted uncounted(*ctx.context);
                Statements      made;
                if (!changesNothing(c))
                {
                    auto* run = ctx.allocator->newTracked<LSLExpressionStatement>(static_cast<LSLExpression*>(stmt->takeChild(0)));
                    run->setLoc(stmt->getLoc());
                    made.push_back(run);
                }
                // A block stays one, its locals its own.
                made.push_back(stmt->takeChild(1));
                stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), made.begin(), made.end());
                setStatements(block, stmts, *ctx.context);
                noteAt(stmt, "OptimizerMergedWays", "wrote the if [1] once, its ways being the same", was);
                return true;
            }
            Statements a = statementsOf(yes);
            Statements b = statementsOf(no);
            if (a.empty() || b.empty())
            {
                return false;
            }
            const auto movable = [&](LSLASTNode* x) {
                return !isStatement(x, NODE_DECLARATION) && !isStatement(x, NODE_LABEL) && namesFromOutside(x, stmt);
            };
            // The last of each, after the if.
            if (render(a.back()) == render(b.back()) && movable(a.back()))
            {
                const Uncounted uncounted(*ctx.context);
                LSLASTNode*     kept = a.back();
                detach(kept);
                detach(b.back());
                tidy(stmt);
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1, kept);
                setStatements(block, stmts, *ctx.context);
                noteAt(stmt, "OptimizerMergedEnds", "wrote what both ways of the if [1] end with once, after it", was);
                return true;
            }
            // The first of each, before the if.
            if (render(a.front()) == render(b.front()) && movable(a.front()) && sideEffectFree(c))
            {
                const ALLSLEffects::Writes writes = ctx.effects->of(a.front());
                bool                       read   = false;
                eachNode(c, [&](LSLASTNode* n) {
                    read = read || (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION && writes.writes(n->getSymbol()));
                });
                if (read)
                {
                    return false;
                }
                const Uncounted uncounted(*ctx.context);
                LSLASTNode*     kept = a.front();
                detach(kept);
                detach(b.front());
                tidy(stmt);
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), kept);
                setStatements(block, stmts, *ctx.context);
                noteAt(stmt, "OptimizerMergedStarts", "wrote what both ways of the if [1] begin with once, before it", was);
                return true;
            }
            return false;
        }

        // An if's ways after statements were taken from them: a true way
        // left null made an empty statement, and an else left empty gone.
        void tidy(LSLIfStatement* stmt)
        {
            if (!present(stmt->getChild(1)))
            {
                auto* nop = ctx.allocator->newTracked<LSLNopStatement>();
                nop->setLoc(stmt->getLoc());
                stmt->setChild(1, nop);
            }
            if (present(stmt->getChild(2)) && statementsOf(stmt->getChild(2)).empty())
            {
                stmt->setChild(2, stmt->newNullNode());
            }
        }

        // ---- an else after a way that never goes on --------------------------------------

        // `if (c) { ...; return; } else B` as `if (c) { ...; return; } B`:
        // B runs where c is false either way, without the jump over it.
        bool elseAfterStop(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            LSLStatement* no = stmt->getFalseBranch();
            if (!present(no) || !stops(stmt->getTrueBranch()) || (inFunction(stmt) && bareStateChange(no)))
            {
                return false;
            }
            const std::string before = report.wanted() ? render(stmt->getCheckExpr()) : std::string();
            const Uncounted   uncounted(*ctx.context);
            LSLASTNode*       otherwise = stmt->takeChild(2);
            stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1, otherwise);
            setStatements(block, stmts, *ctx.context);
            noteAt(stmt, "OptimizerDroppedElse", "dropped the else of if ([1]), whose way never goes on", before);
            return true;
        }

        // ---- a jump over statements ------------------------------------------------------

        // `if (c) { A; jump L; } S; @L;` as `if (c) A; else S;`: where L
        // is jumped to from there alone, the only label of its name, and
        // nothing S declares is named after it.
        bool jumpOver(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            if (present(stmt->getFalseBranch()))
            {
                return false;
            }
            Statements way = statementsOf(stmt->getTrueBranch());
            if (way.empty() || !isStatement(way.back(), NODE_JUMP_STATEMENT))
            {
                return false;
            }
            auto*          jump   = static_cast<LSLJumpStatement*>(way.back());
            LSLIdentifier* id     = jump->getIdentifier();
            LSLSymbol*     target = id ? id->getSymbol() : nullptr;
            if (!target || !id->getName())
            {
                return false;
            }
            size_t at = i + 1;
            while (at < stmts.size() && !(isStatement(stmts[at], NODE_LABEL) && stmts[at]->getSymbol() == target))
            {
                ++at;
            }
            if (at >= stmts.size() || at == i + 1 || labelsNamed(stmts[at], id->getName()) != 1)
            {
                return false;
            }
            // Named by the label and this jump alone.
            if (mentionsOf(callable(stmt), target) != 2)
            {
                return false;
            }
            Statements over(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1, stmts.begin() + static_cast<std::ptrdiff_t>(at));
            for (LSLASTNode* s : over)
            {
                if (holdsLabel(s) || (inFunction(stmt) && bareStateChange(s)))
                {
                    return false;
                }
            }
            // What S declares, named only in S.
            for (LSLASTNode* s : over)
            {
                if (!isStatement(s, NODE_DECLARATION))
                {
                    continue;
                }
                LSLSymbol* sym   = s->getSymbol();
                int        inside = 0;
                for (LSLASTNode* t : over)
                {
                    inside += mentionsOf(t, sym);
                }
                if (!sym || inside != mentionsOf(callable(stmt), sym))
                {
                    return false;
                }
            }
            const std::string before = report.wanted() ? render(id) : std::string();
            const Uncounted   uncounted(*ctx.context);
            LSLASTNode*       label = stmts[at];
            way.pop_back();
            takeStatements(stmt->getTrueBranch());
            for (LSLASTNode* s : over)
            {
                detach(s);
            }
            detach(label);
            stmt->setChild(1, branchOf(way, stmt));
            stmt->setChild(2, branchOf(over, stmt));
            stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i) + 1, stmts.begin() + static_cast<std::ptrdiff_t>(at) + 1);
            setStatements(block, stmts, *ctx.context);
            noteAt(stmt, "OptimizerJumpAsIf", "wrote the jump to [1] over what follows as an else", before);
            return true;
        }

        // ---- a value set before an if ----------------------------------------------------

        // `if (c) x = a; else x = B;` as `x = B; if (c) x = a;`, B a
        // constant, of a local or a parameter neither c nor a reads; or,
        // both constant integers, as `x = (c) * (A - B) + B`, c as its truth.
        bool setBeforeIf(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLIfStatement* stmt)
        {
            LSLExpression* c     = stmt->getCheckExpr();
            const auto     set   = assignment(single(stmt->getTrueBranch()));
            const auto     unset = present(stmt->getFalseBranch()) ? assignment(single(stmt->getFalseBranch())) : std::nullopt;
            if (!c || !set || !unset || set->first->getSymbol() != unset->first->getSymbol())
            {
                return false;
            }
            LSLSymbol* sym = set->first->getSymbol();
            if (!unset->second->getConstantValue())
            {
                return false;
            }
            const std::string        before = report.wanted() ? render(stmt) : std::string();
            const std::optional<S32> va     = integerValue(set->second);
            const std::optional<S32> vb     = integerValue(unset->second);
            const Uncounted          uncounted(*ctx.context);
            // (Both ways set what c and a read nothing of, below; the arithmetic
            // sets it once, after c runs, as the if did.)
            // Of a truth smaller on every target; of anything else, as !!c,
            // where the target has it so.
            if (va && vb && c->getIType() == LST_INTEGER && sym->getIType() == LST_INTEGER && (truthValue(c) || mCosts.arithmeticForSelect))
            {
                // truth * (A - B) + B, in 32 bits as the VM has it.
                const S32      step  = static_cast<S32>(static_cast<U32>(*va) - static_cast<U32>(*vb));
                LSLExpression* value = truthOf(static_cast<LSLExpression*>(stmt->takeChild(0)), true);
                if (value->getNodeSubType() == NODE_BINARY_EXPRESSION)
                {
                    auto* parens = ctx.allocator->newTracked<LSLParenthesisExpression>(value);
                    parens->setType(value->getType());
                    value = parens;
                }
                if (step != 1)
                {
                    auto* product = ctx.allocator->newTracked<LSLBinaryExpression>(value, OP_MUL, constant(ctx.integer(step), stmt));
                    product->setType(TYPE(LST_INTEGER));
                    value = product;
                }
                if (*vb != 0)
                {
                    auto* sum = ctx.allocator->newTracked<LSLBinaryExpression>(value, OP_PLUS, constant(ctx.integer(*vb), stmt));
                    sum->setType(TYPE(LST_INTEGER));
                    value = sum;
                }
                auto* target = static_cast<LSLExpression*>(set->first->getParent()->takeChild(0));
                auto* made   = ctx.allocator->newTracked<LSLBinaryExpression>(target, OP_ASSIGN, value);
                made->setType(target->getType());
                made->setLoc(stmt->getLoc());
                auto* line = ctx.allocator->newTracked<LSLExpressionStatement>(made);
                line->setLoc(stmt->getLoc());
                stmts[i] = line;
                setStatements(block, stmts, *ctx.context);
                noteAt(stmt, "OptimizerSelectAsArithmetic", "wrote [1] as arithmetic", before);
                return true;
            }
            // Set before the check, only where nothing could see it early.
            if ((sym->getSubType() != SYM_LOCAL && sym->getSubType() != SYM_FUNCTION_PARAMETER && sym->getSubType() != SYM_EVENT_PARAMETER) ||
                mentions(c, sym) || mentions(set->second, sym))
            {
                return false;
            }
            LSLASTNode* first = single(stmt->getFalseBranch());
            detach(first);
            stmt->setChild(2, stmt->newNullNode());
            stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), first);
            setStatements(block, stmts, *ctx.context);
            noteAt(stmt, "OptimizerSetBeforeIf", "set the value of [1]'s else before it", before);
            return true;
        }

        // ---- loops -----------------------------------------------------------------------

        typedef boost::unordered_flat_map<LSLSymbol*, S32> Known;

        // An integer expression worked out from what is known of the locals
        // it reads, where everything it reads is known.
        static std::optional<S32> worked(LSLExpression* e, const Known& known)
        {
            e = bare(e);
            if (!e || e->getIType() != LST_INTEGER)
            {
                return std::nullopt;
            }
            if (const std::optional<S32> v = integerValue(e))
            {
                return v;
            }
            switch (e->getNodeSubType())
            {
                case NODE_LVALUE_EXPRESSION:
                {
                    const auto found = static_cast<LSLLValueExpression*>(e)->getMember() ? known.end() : known.find(e->getSymbol());
                    return found == known.end() ? std::nullopt : std::optional<S32>(found->second);
                }
                case NODE_UNARY_EXPRESSION:
                {
                    const std::optional<S32> x = worked(static_cast<LSLUnaryExpression*>(e)->getChildExpr(), known);
                    if (!x)
                    {
                        return std::nullopt;
                    }
                    switch (e->getOperation())
                    {
                        case OP_BOOLEAN_NOT: return *x ? 0 : 1;
                        case OP_BIT_NOT: return ~*x;
                        case OP_MINUS: return static_cast<S32>(0u - static_cast<U32>(*x));
                        default: return std::nullopt;
                    }
                }
                case NODE_BINARY_EXPRESSION:
                {
                    auto* b = static_cast<LSLBinaryExpression*>(e);
                    if (!b->getLHS() || !b->getRHS() || b->getLHS()->getIType() != LST_INTEGER || b->getRHS()->getIType() != LST_INTEGER)
                    {
                        return std::nullopt;
                    }
                    const std::optional<S32> l = worked(b->getLHS(), known);
                    const std::optional<S32> r = worked(b->getRHS(), known);
                    if (!l || !r)
                    {
                        return std::nullopt;
                    }
                    switch (b->getOperation())
                    {
                        case OP_LESS: return *l < *r;
                        case OP_LEQ: return *l <= *r;
                        case OP_GREATER: return *l > *r;
                        case OP_GEQ: return *l >= *r;
                        case OP_EQ: return *l == *r;
                        case OP_NEQ: return *l != *r;
                        case OP_BOOLEAN_AND: return *l && *r;
                        case OP_BOOLEAN_OR: return *l || *r;
                        case OP_BIT_AND: return *l & *r;
                        case OP_BIT_OR: return *l | *r;
                        case OP_BIT_XOR: return *l ^ *r;
                        case OP_PLUS: return static_cast<S32>(static_cast<U32>(*l) + static_cast<U32>(*r));
                        case OP_MINUS: return static_cast<S32>(static_cast<U32>(*l) - static_cast<U32>(*r));
                        default: return std::nullopt;
                    }
                }
                default:
                    return std::nullopt;
            }
        }

        // What a local integer is set to by `x = C`, the constant.
        static std::optional<std::pair<LSLSymbol*, S32>> setTo(LSLASTNode* expr)
        {
            if (!expr || expr->getNodeType() != NODE_EXPRESSION || expr->getNodeSubType() != NODE_BINARY_EXPRESSION ||
                static_cast<LSLExpression*>(expr)->getOperation() != OP_ASSIGN)
            {
                return std::nullopt;
            }
            auto*      b   = static_cast<LSLBinaryExpression*>(expr);
            LSLSymbol* sym = b->getLHS() && b->getLHS()->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(b->getLHS())->getMember()
                                 ? b->getLHS()->getSymbol()
                                 : nullptr;
            const std::optional<S32> v = integerValue(b->getRHS());
            if (!sym || !v || sym->getIType() != LST_INTEGER || sym->getSubType() != SYM_LOCAL)
            {
                return std::nullopt;
            }
            return std::make_pair(sym, *v);
        }

        // What a for's first part leaves known: each `x = C` in turn, and
        // anything else it writes forgotten.
        Known knownAfter(LSLASTNode* inits) const
        {
            Known known;
            for (LSLASTNode* init = inits ? inits->getChild(0) : nullptr; init; init = init->getNext())
            {
                if (const auto set = setTo(init))
                {
                    known[set->first] = set->second;
                    continue;
                }
                for (LSLSymbol* sym : ctx.effects->of(init).variables)
                {
                    known.erase(sym);
                }
            }
            return known;
        }

        // The one step of a for that adds one to, or takes one from, a
        // local integer: the local and which way.
        static std::optional<std::pair<LSLSymbol*, int>> stepOf(LSLASTNode* steps)
        {
            LSLASTNode* step = steps ? steps->getChild(0) : nullptr;
            if (!step || step->getNext() || step->getNodeType() != NODE_EXPRESSION)
            {
                return std::nullopt;
            }
            auto*      e   = static_cast<LSLExpression*>(step);
            LSLSymbol* sym = nullptr;
            int        way = 0;
            const auto var = [](LSLExpression* x) -> LSLSymbol* {
                x = bare(x);
                return x && x->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(x)->getMember() ? x->getSymbol() : nullptr;
            };
            if (e->getNodeSubType() == NODE_UNARY_EXPRESSION)
            {
                const LSLOperator op = e->getOperation();
                way                  = op == OP_PRE_INCR || op == OP_POST_INCR ? 1 : op == OP_PRE_DECR || op == OP_POST_DECR ? -1 : 0;
                sym                  = var(static_cast<LSLUnaryExpression*>(e)->getChildExpr());
            }
            else if (e->getNodeSubType() == NODE_BINARY_EXPRESSION)
            {
                auto*             b  = static_cast<LSLBinaryExpression*>(e);
                const LSLOperator op = b->getOperation();
                sym                  = var(b->getLHS());
                if ((op == OP_ADD_ASSIGN || op == OP_SUB_ASSIGN) && isInteger(b->getRHS(), 1))
                {
                    way = op == OP_ADD_ASSIGN ? 1 : -1;
                }
                else if (op == OP_ASSIGN && bare(b->getRHS())->getNodeSubType() == NODE_BINARY_EXPRESSION)
                {
                    auto* sum = static_cast<LSLBinaryExpression*>(bare(b->getRHS()));
                    if (sum->getOperation() == OP_PLUS && var(sum->getLHS()) == sym && isInteger(sum->getRHS(), 1))
                    {
                        way = 1;
                    }
                    else if (sum->getOperation() == OP_MINUS && var(sum->getLHS()) == sym && isInteger(sum->getRHS(), 1))
                    {
                        way = -1;
                    }
                }
            }
            if (!sym || way == 0 || sym->getIType() != LST_INTEGER || sym->getSubType() != SYM_LOCAL)
            {
                return std::nullopt;
            }
            return std::make_pair(sym, way);
        }

        // The for's parts taken apart: its first part as statements, its
        // body and its check.
        struct Parts
        {
            Statements     inits;
            LSLStatement*  body  = nullptr;
            LSLExpression* check = nullptr;
        };
        Parts takeApart(LSLForStatement* loop)
        {
            Parts       parts;
            LSLASTNode* inits = loop->getInitExprs();
            while (inits && inits->hasChildren())
            {
                auto* expr = static_cast<LSLExpression*>(inits->takeChild(0));
                inits->removeChild(inits->getChild(0));
                auto* line = ctx.allocator->newTracked<LSLExpressionStatement>(expr);
                line->setLoc(expr->getLoc());
                parts.inits.push_back(line);
            }
            parts.body  = static_cast<LSLStatement*>(loop->takeChild(3));
            parts.check = static_cast<LSLExpression*>(loop->takeChild(1));
            return parts;
        }

        // A statement made a body's last: the body a block where it was not.
        LSLStatement* ending(LSLStatement* body, LSLStatement* last)
        {
            if (!isStatement(body, NODE_COMPOUND_STATEMENT))
            {
                auto* block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
                block->setLoc(body->getLoc());
                if (!isStatement(body, NODE_NOP_STATEMENT))
                {
                    block->pushChild(body);
                }
                body = block;
            }
            body->pushChild(last);
            return body;
        }

        LSLDoStatement* doOf(LSLStatement* body, LSLExpression* check, LSLASTNode* at)
        {
            auto* made = ctx.allocator->newTracked<LSLDoStatement>(body, check);
            made->setLoc(at->getLoc());
            return made;
        }

        // `--x` or `++x` of a local, new.
        LSLExpression* stepped(LSLSymbol* sym, int way, LSLASTNode* at)
        {
            auto* id = ctx.allocator->newTracked<LSLIdentifier>(sym->getType(), sym->getName());
            id->setSymbol(sym);
            id->setLoc(at->getLoc());
            auto* read = ctx.allocator->newTracked<LSLLValueExpression>(id, static_cast<LSLIdentifier*>(nullptr));
            read->setType(sym->getType());
            read->setLoc(at->getLoc());
            auto* made = ctx.allocator->newTracked<LSLUnaryExpression>(read, way > 0 ? OP_PRE_INCR : OP_PRE_DECR);
            made->setType(TYPE(LST_INTEGER));
            made->setLoc(at->getLoc());
            return made;
        }

        // `for (i = A; i < B; ++i) S`, S never naming i, nor anything after
        // the loop: S run B - A times by `i = B - A; do S while (--i);`;
        // or, B not known but never below nought and A nought, `i = B;
        // while (i--) S` -- B read once, where nothing S does changes it.
        bool countDown(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLForStatement* loop)
        {
            LSLASTNode* inits = loop->getInitExprs();
            LSLASTNode* init  = inits ? inits->getChild(0) : nullptr;
            const auto  set   = init && !init->getNext() ? setTo(init) : std::nullopt;
            const auto  step  = stepOf(loop->getIncrExprs());
            LSLExpression* check = bare(loop->getCheckExpr());
            if (!set || !step || set->first != step->first || !check || check->getNodeSubType() != NODE_BINARY_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym   = set->first;
            auto*      cmp   = static_cast<LSLBinaryExpression*>(check);
            const auto isSym = [sym](LSLExpression* x) {
                x = bare(x);
                return x && x->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(x)->getMember() && x->getSymbol() == sym;
            };
            // i < B, i <= B, i > B, i >= B, either way round.
            LSLOperator    op    = cmp->getOperation();
            LSLExpression* bound = nullptr;
            if (isSym(cmp->getLHS()))
            {
                bound = cmp->getRHS();
            }
            else if (isSym(cmp->getRHS()))
            {
                bound = cmp->getLHS();
                op    = op == OP_LESS ? OP_GREATER : op == OP_GREATER ? OP_LESS : op == OP_LEQ ? OP_GEQ : op == OP_GEQ ? OP_LEQ : op;
            }
            const bool up = step->second > 0;
            if (!bound || mentions(bound, sym) || !((up && (op == OP_LESS || op == OP_LEQ)) || (!up && (op == OP_GREATER || op == OP_GEQ))))
            {
                return false;
            }
            // i named in the loop's head and its declaration, nowhere else.
            LSLASTNode* body = loop->getBody();
            if (mentions(body, sym) || mentionsOf(callable(loop), sym) != mentionsOf(loop, sym) + 1)
            {
                return false;
            }
            const std::optional<S32> b = integerValue(bound);
            const std::string        before = report.wanted() ? render(loop->getCheckExpr()) : std::string();
            if (b)
            {
                const int64_t count = (up ? int64_t(*b) - int64_t(set->second) : int64_t(set->second) - int64_t(*b)) +
                                      (op == OP_LEQ || op == OP_GEQ ? 1 : 0);
                if (count < 1 || count > INT32_MAX)
                {
                    return false;
                }
                const Uncounted uncounted(*ctx.context);
                Parts           parts = takeApart(loop);
                // i = count, where the first part set i = A.
                auto* first = static_cast<LSLExpressionStatement*>(parts.inits.front());
                renumber(static_cast<LSLBinaryExpression*>(first->getExpr()), ctx.integer(static_cast<S32>(count)));
                Statements made = parts.inits;
                made.push_back(doOf(parts.body, stepped(sym, -1, loop), loop));
                stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), made.begin(), made.end());
                setStatements(block, stmts, *ctx.context);
                noteAt(loop, "OptimizerCountedDown", "counted the loop while ([1]) down to nought", before);
                return true;
            }
            const std::optional<Range> r = rangeOf(bound);
            bool                       moved = false;
            for (LSLSymbol* written : ctx.effects->of(body).variables)
            {
                moved = moved || mentions(bound, written);
            }
            if (op != OP_LESS || set->second != 0 || !r || r->least < 0.0 || !sideEffectFree(bound) || moved)
            {
                return false;
            }
            const Uncounted uncounted(*ctx.context);
            Parts           parts = takeApart(loop);
            auto*           first = static_cast<LSLExpressionStatement*>(parts.inits.front());
            auto*           sets  = static_cast<LSLBinaryExpression*>(first->getExpr());
            // i = B, B taken from the check.
            LSLASTNode* boundParent = bound->getParent();
            boundParent->takeChild(bound->getParentSlot());
            sets->setChild(1, bound);
            auto* id = ctx.allocator->newTracked<LSLIdentifier>(sym->getType(), sym->getName());
            id->setSymbol(sym);
            id->setLoc(loop->getLoc());
            auto* read = ctx.allocator->newTracked<LSLLValueExpression>(id, static_cast<LSLIdentifier*>(nullptr));
            read->setType(sym->getType());
            read->setLoc(loop->getLoc());
            auto* down = ctx.allocator->newTracked<LSLUnaryExpression>(read, OP_POST_DECR);
            down->setType(TYPE(LST_INTEGER));
            down->setLoc(loop->getLoc());
            auto* made = ctx.allocator->newTracked<LSLWhileStatement>(down, parts.body);
            made->setLoc(loop->getLoc());
            Statements all = parts.inits;
            all.push_back(made);
            stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
            stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), all.begin(), all.end());
            setStatements(block, stmts, *ctx.context);
            noteAt(loop, "OptimizerCountedDown", "counted the loop while ([1]) down to nought", before);
            return true;
        }

        void renumber(LSLBinaryExpression* assignment, LSLConstant* cv)
        {
            LSLASTNode* value = assignment->getChild(1);
            LSLASTNode::replaceNode(value, constant(cv, value));
        }

        // A for whose first check is known to fail, from what its first part
        // sets, gone but for that part; and one known to pass run as a do: its step made the check's read of what it steps
        // where that is the check's one read of it -- `while (++i < 10)` --
        // or put at the body's end. Where the target has a do the smaller.
        bool forAsDo(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLForStatement* loop)
        {
            LSLExpression* check = loop->getCheckExpr();
            if (!check || holdsLabel(loop))
            {
                return false;
            }
            const std::optional<S32> first = worked(check, knownAfter(loop->getInitExprs()));
            if (!first)
            {
                return false;
            }
            if (!*first)
            {
                // Never run: its first part alone.
                const std::string before = report.wanted() ? render(check) : std::string();
                const Uncounted   uncounted(*ctx.context);
                Parts             parts = takeApart(loop);
                stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), parts.inits.begin(), parts.inits.end());
                setStatements(block, stmts, *ctx.context);
                noteAt(loop, "OptimizerLoopNeverRuns", "removed the loop on ([1]), its first check known to fail", before);
                return true;
            }
            if (!mCosts.doForKnownFirst)
            {
                return false;
            }
            const std::string before = report.wanted() ? render(check) : std::string();
            const auto        step   = stepOf(loop->getIncrExprs());
            // The check's one read of the stepped local.
            LSLLValueExpression* read  = nullptr;
            int                  reads = 0;
            if (step)
            {
                eachNode(check, [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == step->first)
                    {
                        ++reads;
                        LSLASTNode* parent = n->getParent();
                        read = parent && parent->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(parent)->getMember()
                                   ? static_cast<LSLLValueExpression*>(parent)
                                   : nullptr;
                    }
                });
            }
            const Uncounted uncounted(*ctx.context);
            Parts           parts = takeApart(loop);
            LSLStatement*   body  = parts.body;
            if (step && reads == 1 && read)
            {
                LSLASTNode* parent = read == parts.check ? nullptr : read->getParent();
                const int   slot   = read->getParentSlot();
                if (parent)
                {
                    parent->takeChild(slot);
                }
                auto* made = ctx.allocator->newTracked<LSLUnaryExpression>(read, step->second > 0 ? OP_PRE_INCR : OP_PRE_DECR);
                made->setType(TYPE(LST_INTEGER));
                made->setLoc(read->getLoc());
                if (parent)
                {
                    parent->setChild(slot, made);
                }
                else
                {
                    parts.check = made;
                }
            }
            else
            {
                LSLASTNode* steps = loop->getIncrExprs();
                while (steps && steps->hasChildren())
                {
                    auto* expr = static_cast<LSLExpression*>(steps->takeChild(0));
                    steps->removeChild(steps->getChild(0));
                    auto* line = ctx.allocator->newTracked<LSLExpressionStatement>(expr);
                    line->setLoc(expr->getLoc());
                    body = ending(body, line);
                }
            }
            Statements made = parts.inits;
            made.push_back(doOf(body, parts.check, loop));
            stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
            stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i), made.begin(), made.end());
            setStatements(block, stmts, *ctx.context);
            noteAt(loop, "OptimizerLoopAsDo", "ran the loop on ([1]) as a do, its first check known to pass", before);
            return true;
        }

        // A while whose first check is known, from the constants the
        // statements just before it set: gone where it fails, run as a do
        // where it passes.
        bool whileAsDo(LSLCompoundStatement* block, Statements& stmts, size_t i, LSLWhileStatement* loop)
        {
            LSLExpression* check = loop->getCheckExpr();
            if (!check || holdsLabel(loop))
            {
                return false;
            }
            Known known;
            for (size_t k = i; k-- > 0;)
            {
                LSLASTNode* stmt = stmts[k];
                if (isStatement(stmt, NODE_EXPRESSION_STATEMENT))
                {
                    const auto set = setTo(static_cast<LSLExpressionStatement*>(stmt)->getExpr());
                    if (!set)
                    {
                        break;
                    }
                    known.try_emplace(set->first, set->second);
                    continue;
                }
                if (isStatement(stmt, NODE_DECLARATION))
                {
                    LSLSymbol*               sym  = stmt->getSymbol();
                    LSLASTNode*              init = static_cast<LSLDeclaration*>(stmt)->getInitializer();
                    const std::optional<S32> v    = present(init) ? integerValue(init) : std::optional<S32>(0);
                    if (!sym || !v || sym->getIType() != LST_INTEGER)
                    {
                        break;
                    }
                    known.try_emplace(sym, *v);
                    continue;
                }
                break;
            }
            const std::optional<S32> first = worked(check, known);
            if (!first)
            {
                return false;
            }
            const std::string before    = report.wanted() ? render(check) : std::string();
            if (!*first)
            {
                const Uncounted uncounted(*ctx.context);
                stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i));
                setStatements(block, stmts, *ctx.context);
                noteAt(loop, "OptimizerLoopNeverRuns", "removed the loop on ([1]), its first check known to fail", before);
                return true;
            }
            if (!mCosts.doForKnownFirst)
            {
                return false;
            }
            const Uncounted   uncounted(*ctx.context);
            auto*             body      = static_cast<LSLStatement*>(loop->takeChild(1));
            auto*             condition = static_cast<LSLExpression*>(loop->takeChild(0));
            stmts[i]                    = doOf(body, condition, loop);
            setStatements(block, stmts, *ctx.context);
            noteAt(loop, "OptimizerLoopAsDo", "ran the loop on ([1]) as a do, its first check known to pass", before);
            return true;
        }
    };
}

    int restructure(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        Branches branches(ctx, report, options);
        script->visit(&branches);
        return branches.changes;
    }
}
