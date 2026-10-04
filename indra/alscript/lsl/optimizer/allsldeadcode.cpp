/**
 * @file allsldeadcode.cpp
 * @brief The LSL optimizer's dead code removal.
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

#include "allsldeadcode.h"

#include "allsleffects.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cstring>
#include <functional>

namespace ALLSLPasses
{
namespace
{
    // What can never run, and what is never used.
    class DeadCode : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION && global->getSymbol())
                {
                    mFunctions[global->getSymbol()] = static_cast<LSLGlobalFunction*>(global);
                }
            }
            script->visit(this);
            globals(script);
            states(script);
            unreached(script);
            writeOnly(script);
            emptyHandlers(script);
            return changes;
        }

        bool visit(LSLCompoundStatement* block) override
        {
            visitChildren(block);
            bool                     dead = false;
            std::vector<LSLASTNode*> going;
            // Removed for a reason, said; or silently, with nothing.
            const auto               go = [&](LSLASTNode* stmt, const char* key, const char* why) {
                if (key)
                {
                    // What went, as far as its first line and a few words
                    // of it: a note is read beside the line, not in place
                    // of the code.
                    std::string said = render(stmt);
                    const size_t cut = std::min(said.find('\n'), size_t(60));
                    if (cut < said.size())
                    {
                        said = said.substr(0, cut) + "\xE2\x80\xA6";
                    }
                    report.note(stmt->getLoc(), key, std::string("removed [1]") + why, { said });
                }
                going.push_back(stmt);
            };
            for (LSLASTNode* stmt : *block)
            {
                if (stmt->getNodeSubType() == NODE_LABEL)
                {
                    dead = false;
                    if (unusedLabel(stmt))
                    {
                        go(stmt, nullptr, "");
                    }
                    continue;
                }
                if (dead)
                {
                    // A label in it that a jump goes to makes it reachable,
                    // and what follows it: a jump in SL goes to the last
                    // label of its name in the function, whatever block
                    // that is in.
                    if (!holdsLiveLabel(stmt))
                    {
                        go(stmt, "OptimizerRemovedUnreachable", ", which can never run");
                        continue;
                    }
                    dead = false;
                }
                switch (stmt->getNodeSubType())
                {
                    case NODE_JUMP_STATEMENT:
                        if (jumpsToNext(stmt))
                        {
                            go(stmt, "OptimizerRemovedJump", ", which goes where running on goes");
                            break;
                        }
                        dead = true;
                        break;
                    case NODE_RETURN_STATEMENT:
                    case NODE_STATE_STATEMENT:
                        dead = true;
                        break;
                    case NODE_IF_STATEMENT:
                    case NODE_WHILE_STATEMENT:
                    case NODE_DO_STATEMENT:
                    case NODE_FOR_STATEMENT:
                        // An if none of whose ways goes on, a loop that
                        // never ends -- but for a jump out, whose label
                        // keeps what follows it (above).
                        dead = stops(stmt);
                        break;
                    case NODE_NOP_STATEMENT:
                        go(stmt, nullptr, "");
                        break;
                    case NODE_COMPOUND_STATEMENT:
                        if (!stmt->hasChildren())
                        {
                            go(stmt, nullptr, "");
                        }
                        break;
                    case NODE_EXPRESSION_STATEMENT:
                    {
                        LSLExpression* expr = static_cast<LSLExpressionStatement*>(stmt)->getExpr();
                        if (changesNothing(expr) || callsNothing(expr))
                        {
                            go(stmt, "OptimizerRemovedNoEffect", ", which does nothing");
                        }
                        dead = stops(stmt);
                        break;
                    }
                    case NODE_DECLARATION:
                        if (unusedLocal(static_cast<LSLDeclaration*>(stmt)))
                        {
                            go(stmt, nullptr, "");
                        }
                        break;
                    default:
                        break;
                }
            }
            for (LSLASTNode* stmt : going)
            {
                block->removeChild(stmt);
                ++changes;
            }
            flattenBlocks(block);
            sinkDeclarations(block);
            substituteLocals(block);
            deadStores(block);
            return false;
        }

        // How many identifiers of each name a function or event has,
        // counted once a run: what the pass takes away only makes a count
        // more than there are, never fewer.
        const boost::unordered_flat_map<std::string_view, int>& namesIn(LSLASTNode* node)
        {
            LSLASTNode* callable = node;
            while (callable && callable->getNodeType() != NODE_GLOBAL_FUNCTION && callable->getNodeType() != NODE_EVENT_HANDLER)
            {
                callable = callable->getParent();
            }
            auto& counted = mNames[callable];
            if (counted.empty() && callable)
            {
                std::vector<LSLASTNode*> stack{ callable };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                    {
                        ++counted[static_cast<LSLIdentifier*>(n)->getName()];
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
            }
            return counted;
        }

        // A block standing as a statement of a block, its statements put
        // in its place: where it declares nothing and holds no label, or
        // only names no identifier outside it has -- LSL has no shadowing,
        // and a name it declared would otherwise be one a later declaration
        // shadows, or a global a later read meant; and two labels of a name
        // may not stand in one block.
        void flattenBlocks(LSLCompoundStatement* block)
        {
            const auto flat = [&](LSLASTNode* inner) {
                if (inner->getNodeSubType() != NODE_COMPOUND_STATEMENT)
                {
                    return false;
                }
                boost::unordered_flat_map<std::string_view, int> own;
                std::vector<const char*>                        declared;
                std::vector<LSLASTNode*>                        stack{ inner };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                    {
                        ++own[static_cast<LSLIdentifier*>(n)->getName()];
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                // What it declares, and the labels it holds: two labels of a
                // name may not stand in one block.
                for (LSLASTNode* stmt = inner->getChild(0); stmt; stmt = stmt->getNext())
                {
                    if (stmt->getNodeSubType() == NODE_DECLARATION)
                    {
                        declared.push_back(static_cast<LSLDeclaration*>(stmt)->getIdentifier()->getName());
                    }
                    else if (stmt->getNodeSubType() == NODE_LABEL)
                    {
                        declared.push_back(static_cast<LSLLabel*>(stmt)->getIdentifier()->getName());
                    }
                }
                const auto& all = namesIn(block);
                for (const char* name : declared)
                {
                    const auto total = all.find(std::string_view(name));
                    if (!name || total == all.end() || total->second != own[std::string_view(name)])
                    {
                        return false;
                    }
                }
                return true;
            };
            std::vector<LSLASTNode*> statements;
            bool                     any = false;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
                any = any || flat(stmt);
            }
            if (!any)
            {
                return;
            }
            std::vector<bool> flatten(statements.size());
            for (size_t i = 0; i < statements.size(); ++i)
            {
                flatten[i] = flat(statements[i]);
            }
            // The statements are the ones they were, every reference in
            // them still in the script: counted neither out nor in again.
            const Uncounted uncounted(*ctx.context);
            for (LSLASTNode* stmt : statements)
            {
                block->removeChild(stmt);
            }
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (!flatten[i])
                {
                    block->pushChild(statements[i]);
                    continue;
                }
                std::vector<LSLASTNode*> inner;
                for (LSLASTNode* stmt = statements[i]->getChild(0); stmt; stmt = stmt->getNext())
                {
                    inner.push_back(stmt);
                }
                for (LSLASTNode* stmt : inner)
                {
                    statements[i]->removeChild(stmt);
                    block->pushChild(stmt);
                }
                ++changes;
            }
        }

        // `T x;` and, later in the block, `x = e;` with nothing between that
        // names x, jumps or is jumped to: the declaration where the
        // assignment was, `T x = e;` -- which the local's value may then
        // go further from.
        void sinkDeclarations(LSLCompoundStatement* block)
        {
            std::vector<LSLASTNode*> statements;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
            }
            const auto mentions = [](LSLASTNode* root, LSLSymbol* sym) {
                std::vector<LSLASTNode*> stack{ root };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym)
                    {
                        return true;
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                return false;
            };
            const auto jumpy = [](LSLASTNode* root) {
                std::vector<LSLASTNode*> stack{ root };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_STATEMENT && (n->getNodeSubType() == NODE_LABEL || n->getNodeSubType() == NODE_JUMP_STATEMENT))
                    {
                        return true;
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
                return false;
            };
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (statements[i]->getNodeSubType() != NODE_DECLARATION)
                {
                    continue;
                }
                auto*      decl = static_cast<LSLDeclaration*>(statements[i]);
                LSLSymbol* sym  = decl->getSymbol();
                LSLASTNode* init = decl->getChild(1);
                if (!sym || sym->getSubType() != SYM_LOCAL || (init && init->getNodeType() != NODE_NULL))
                {
                    continue;
                }
                for (size_t j = i + 1; j < statements.size(); ++j)
                {
                    LSLASTNode* stmt = statements[j];
                    if (!mentions(stmt, sym))
                    {
                        if (jumpy(stmt))
                        {
                            break;
                        }
                        continue;
                    }
                    auto* expr = stmt->getNodeSubType() == NODE_EXPRESSION_STATEMENT ? static_cast<LSLExpressionStatement*>(stmt)->getExpr() : nullptr;
                    if (!expr || expr->getNodeSubType() != NODE_BINARY_EXPRESSION || expr->getOperation() != '=' ||
                        expr->getChild(0)->getNodeSubType() != NODE_LVALUE_EXPRESSION || static_cast<LSLLValueExpression*>(expr->getChild(0))->getMember() ||
                        static_cast<LSLLValueExpression*>(expr->getChild(0))->getSymbol() != sym || mentions(expr->getChild(1), sym))
                    {
                        break;
                    }
                    decl->setChild(1, static_cast<LSLExpression*>(expr->takeChild(1)));
                    block->removeChild(decl);
                    LSLASTNode::replaceNode(stmt, decl);
                    statements[j] = decl;
                    ++changes;
                    break;
                }
            }
        }

        // Whether a jump goes where running on would go anyway: its label
        // is the next statement to run -- after the jump, or after the
        // blocks and the if branches it is the end of -- and the only
        // label of its name in the function, a jump going to the last of
        // a name.
        static bool jumpsToNext(LSLASTNode* jump)
        {
            LSLIdentifier* id     = static_cast<LSLJumpStatement*>(jump)->getIdentifier();
            const char*    target = id ? id->getName() : nullptr;
            if (!target)
            {
                return false;
            }
            const auto empty = [](LSLASTNode* n) {
                return n->getNodeSubType() == NODE_NOP_STATEMENT || (n->getNodeSubType() == NODE_COMPOUND_STATEMENT && !n->hasChildren());
            };
            LSLASTNode* at = jump;
            while (true)
            {
                LSLASTNode* parent = at->getParent();
                if (!parent || parent->getNodeType() != NODE_STATEMENT)
                {
                    return false;
                }
                if (parent->getNodeSubType() == NODE_IF_STATEMENT && at != static_cast<LSLIfStatement*>(parent)->getCheckExpr())
                {
                    at = parent;
                    continue;
                }
                if (parent->getNodeSubType() != NODE_COMPOUND_STATEMENT)
                {
                    return false;
                }
                LSLASTNode* next = at->getNext();
                while (next && empty(next))
                {
                    next = next->getNext();
                }
                if (!next)
                {
                    at = parent;
                    continue;
                }
                if (next->getNodeSubType() != NODE_LABEL || strcmp(static_cast<LSLLabel*>(next)->getIdentifier()->getName(), target))
                {
                    return false;
                }
                // The only label of the name in the function or event.
                return labelsNamed(next, target) == 1;
            }
        }

        // A value set and set again, in later statements of the same block,
        // before anything reads it: `x = 3; ... x = 4;`, `integer x = 3;
        // ... for (x = 0; ...)`. The first goes -- what it ran still run --
        // where nothing between names x, jumps or is jumped to. Of a local
        // or a parameter, which nothing outside the code here reads.
        void deadStores(LSLCompoundStatement* block)
        {
            std::vector<LSLASTNode*> statements;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
            }
            const auto names = [](LSLASTNode* root, LSLSymbol* sym) {
                bool found = false;
                each(root, [&](LSLASTNode* n) { found = found || (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getSymbol() == sym); });
                return found;
            };
            const auto jumpy = [](LSLASTNode* root) {
                bool found = false;
                each(root, [&](LSLASTNode* n) {
                    found = found || (n->getNodeType() == NODE_STATEMENT && (n->getNodeSubType() == NODE_LABEL || n->getNodeSubType() == NODE_JUMP_STATEMENT));
                });
                return found;
            };
            // `x = e`, whole, e naming nothing of x: what it sets.
            const auto setsWhole = [&](LSLASTNode* expr) -> LSLSymbol* {
                if (!expr || expr->getNodeType() != NODE_EXPRESSION || expr->getNodeSubType() != NODE_BINARY_EXPRESSION ||
                    static_cast<LSLExpression*>(expr)->getOperation() != OP_ASSIGN)
                {
                    return nullptr;
                }
                LSLASTNode* target = expr->getChild(0);
                LSLSymbol*  sym    = target->getNodeSubType() == NODE_LVALUE_EXPRESSION && !static_cast<LSLLValueExpression*>(target)->getMember()
                                         ? target->getSymbol()
                                         : nullptr;
                return sym && !names(expr->getChild(1), sym) ? sym : nullptr;
            };
            // Whether a statement sets x again before it reads it.
            const auto overwrites = [&](LSLASTNode* stmt, LSLSymbol* sym) {
                if (stmt->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
                {
                    return setsWhole(stmt->getChild(0)) == sym;
                }
                if (stmt->getNodeSubType() == NODE_FOR_STATEMENT)
                {
                    // Its first part, the first that names x setting it.
                    for (LSLASTNode* init = stmt->getChild(0)->getChild(0); init; init = init->getNext())
                    {
                        if (names(init, sym))
                        {
                            return setsWhole(init) == sym;
                        }
                    }
                }
                return false;
            };
            bool changed = false;
            for (size_t i = 0; i < statements.size(); ++i)
            {
                LSLASTNode* stmt  = statements[i];
                LSLASTNode* value = nullptr;
                LSLSymbol*  sym   = nullptr;
                if (stmt->getNodeSubType() == NODE_DECLARATION)
                {
                    value = stmt->getChild(1);
                    sym   = value && value->getNodeType() == NODE_EXPRESSION ? stmt->getSymbol() : nullptr;
                }
                else if (stmt->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
                {
                    sym   = setsWhole(stmt->getChild(0));
                    value = sym ? stmt->getChild(0)->getChild(1) : nullptr;
                }
                if (!sym || (sym->getSubType() != SYM_LOCAL && !parameter(sym)) || sym->getIType() == LST_LIST)
                {
                    continue;
                }
                bool dead = false;
                for (size_t j = i + 1; j < statements.size(); ++j)
                {
                    if (overwrites(statements[j], sym))
                    {
                        dead = true;
                        break;
                    }
                    if (names(statements[j], sym) || jumpy(statements[j]))
                    {
                        break;
                    }
                }
                if (!dead)
                {
                    continue;
                }
                report.note(stmt->getLoc(), "OptimizerDeadStore", "removed the value [1] is set to here, which it is set again before anything reads",
                            { sym->getName() });
                const Uncounted uncounted(*ctx.context);
                if (stmt->getNodeSubType() == NODE_DECLARATION)
                {
                    auto* was = static_cast<LSLExpression*>(stmt->takeChild(1));
                    if (!changesNothing(was))
                    {
                        auto* run = ctx.allocator->newTracked<LSLExpressionStatement>(was);
                        run->setLoc(stmt->getLoc());
                        statements.insert(statements.begin() + static_cast<std::ptrdiff_t>(i) + 1, run);
                    }
                }
                else if (changesNothing(value))
                {
                    statements.erase(statements.begin() + static_cast<std::ptrdiff_t>(i));
                    --i;
                }
                else
                {
                    LSLASTNode* expr = stmt->getChild(0);
                    stmt->setChild(0, expr->takeChild(1));
                }
                changed = true;
                ++changes;
            }
            if (changed)
            {
                setStatements(block, statements, *ctx.context);
            }
        }

        // A local set once from what changes nothing and is read the same
        // at any time, then read once in the expression of a later
        // statement of the same block: its value put where it is read, and
        // the local gone -- where no loop reads it again and no label
        // could bring the run in between, and nothing that runs between
        // writes what the value reads. A parameter a function was put in
        // place with, or the variable its value was set in, most often.
        void substituteLocals(LSLCompoundStatement* block)
        {
            // The block's locals that may go -- declared here from what
            // changes nothing, never written, read once -- and, in one walk
            // of what follows, where each is read.
            std::vector<LSLASTNode*>                                                          statements;
            boost::unordered_flat_map<LSLSymbol*, std::pair<size_t, LSLLValueExpression*>> reads;
            for (LSLASTNode* stmt : *block)
            {
                if (stmt->getNodeSubType() == NODE_DECLARATION)
                {
                    auto*      decl = static_cast<LSLDeclaration*>(stmt);
                    LSLSymbol* sym  = decl->getSymbol();
                    if (sym && sym->getSubType() == SYM_LOCAL && sym->getAssignments() == 0 && sym->getReferences() == 2 && decl->getInitializer() &&
                        decl->getInitializer()->getNodeType() == NODE_EXPRESSION)
                    {
                        reads.emplace(sym, std::make_pair(size_t(-1), nullptr));
                    }
                }
                statements.push_back(stmt);
            }
            if (reads.empty())
            {
                return;
            }
            std::vector<LSLASTNode*> stack;
            for (size_t k = 0; k < statements.size(); ++k)
            {
                stack.assign(1, statements[k]);
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        const auto found = reads.find(static_cast<LSLLValueExpression*>(n)->getSymbol());
                        if (found != reads.end() && !found->second.second)
                        {
                            found->second = { k, static_cast<LSLLValueExpression*>(n) };
                        }
                    }
                    // A statement's own expressions only: a read in one it
                    // holds is not one a value may be put in, and that
                    // statement's own block walks it.
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        if (child->getNodeType() != NODE_STATEMENT)
                        {
                            stack.push_back(child);
                        }
                    }
                }
            }
            const auto holdsLabel = [](LSLASTNode* root) {
                bool                                   found = false;
                const std::function<void(LSLASTNode*)> look  = [&](LSLASTNode* n) {
                    found = found || (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL);
                    for (LSLASTNode* child = n->getChild(0); child && !found; child = child->getNext())
                    {
                        look(child);
                    }
                };
                look(root);
                return found;
            };
            std::vector<LSLDeclaration*> gone;
            for (size_t i = 0; i < statements.size(); ++i)
            {
                if (statements[i]->getNodeSubType() != NODE_DECLARATION)
                {
                    continue;
                }
                auto*          decl  = static_cast<LSLDeclaration*>(statements[i]);
                LSLSymbol*     sym   = decl->getSymbol();
                LSLExpression* value = decl->getInitializer();
                const auto     read  = sym ? reads.find(sym) : reads.end();
                if (read == reads.end() || !value || !sideEffectFree(value) || value->getIType() != sym->getIType())
                {
                    continue;
                }
                // The read, and the statement it is in: one after this.
                const size_t         at  = read->second.first;
                LSLLValueExpression* use = read->second.second;
                if (!use || at <= i || use->getMember())
                {
                    continue;
                }
                // In the statement's own expression: no statement, and so
                // no loop or branch, between it and the read.
                bool direct = true;
                for (LSLASTNode* up = use->getParent(); up && up != statements[at]; up = up->getParent())
                {
                    direct = direct && up->getNodeType() != NODE_STATEMENT;
                }
                const LSLNodeSubType shape = statements[at]->getNodeSubType();
                if (!direct || shape == NODE_WHILE_STATEMENT || shape == NODE_DO_STATEMENT || shape == NODE_FOR_STATEMENT)
                {
                    continue;
                }
                bool                  labelled = false;
                ALLSLEffects::Writes  between;
                for (size_t k = i + 1; k < at; ++k)
                {
                    labelled = labelled || holdsLabel(statements[k]);
                    between.add(ctx.effects->of(statements[k]));
                }
                for (LSLASTNode* earlier : ALLSLEffects::before(statements[at], use))
                {
                    between.add(ctx.effects->of(earlier));
                }
                bool written = false;
                const std::function<void(LSLASTNode*)> reads = [&](LSLASTNode* n) {
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                    {
                        written = written || between.writes(static_cast<LSLLValueExpression*>(n)->getSymbol());
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        reads(child);
                    }
                };
                reads(value);
                if (labelled || written)
                {
                    continue;
                }
                report.note(decl->getLoc(), "OptimizerSubstitutedLocal", "put the value of [1] where it is read, and removed it", { sym->getName() });
                putInPlace(use, static_cast<LSLExpression*>(decl->takeChild(1)), ctx.allocator);
                gone.push_back(decl);
                ++changes;
            }
            for (LSLDeclaration* decl : gone)
            {
                block->removeChild(decl);
            }
        }

        bool visit(LSLIfStatement* stmt) override
        {
            visitChildren(stmt);
            LSLConstant* cv = stmt->getCheckExpr() ? stmt->getCheckExpr()->getConstantValue() : nullptr;
            if (!cv || cv->getNodeSubType() != NODE_INTEGER_CONSTANT)
            {
                return false;
            }
            const bool    taken  = static_cast<LSLIntegerConstant*>(cv)->getValue() != 0;
            LSLStatement* branch = taken ? stmt->getTrueBranch() : stmt->getFalseBranch();
            // A branch never taken is reached all the same by a jump to a
            // label in it.
            if (LSLStatement* other = taken ? stmt->getFalseBranch() : stmt->getTrueBranch(); other && holdsLiveLabel(other))
            {
                return false;
            }
            if (branch && lastIfOfStateChange(stmt, branch))
            {
                return false;
            }
            report.note(stmt->getLoc(), taken ? "OptimizerIfAlwaysTrue" : "OptimizerIfAlwaysFalse",
                        taken ? "the condition of this if is always true; kept only what runs" : "the condition of this if is always false; kept only what runs");
            replaceStatement(stmt, branch ? static_cast<LSLStatement*>(stmt->takeChild(taken ? 1 : 2)) : nullptr);
            return false;
        }

        bool visit(LSLWhileStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0) && !holdsLiveLabel(stmt))
            {
                report.note(stmt->getLoc(), "OptimizerRemovedWhile", "removed a while loop whose condition is always false");
                replaceStatement(stmt, nullptr);
            }
            return false;
        }

        bool visit(LSLDoStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0))
            {
                report.note(stmt->getLoc(), "OptimizerDoRunsOnce", "a do loop whose condition is always false runs once; kept its body");
                replaceStatement(stmt, static_cast<LSLStatement*>(stmt->takeChild(0)));
            }
            return false;
        }

        bool visit(LSLForStatement* stmt) override
        {
            visitChildren(stmt);
            if (isInteger(stmt->getCheckExpr(), 0) && !holdsLiveLabel(stmt))
            {
                // The initialisers still run, once.
                auto* block = ctx.allocator->newTracked<LSLCompoundStatement>(nullptr);
                block->setLoc(stmt->getLoc());
                LSLASTNode* init = stmt->getInitExprs();
                while (init && init->hasChildren())
                {
                    auto* expr = static_cast<LSLExpression*>(init->takeChild(0));
                    init->removeChild(init->getChild(0));
                    auto* es = ctx.allocator->newTracked<LSLExpressionStatement>(expr);
                    es->setLoc(expr->getLoc());
                    block->pushChild(es);
                }
                report.note(stmt->getLoc(), "OptimizerRemovedFor", "removed a for loop whose condition is always false; its initialisers stay");
                replaceStatement(stmt, block->hasChildren() ? block : nullptr);
            }
            return false;
        }

    private:
        // A statement becomes another, or nothing: nothing is an empty
        // statement where a branch or a body needs one.
        void replaceStatement(LSLStatement* old, LSLStatement* now)
        {
            LSLASTNode* parent = old->getParent();
            if (!now)
            {
                if (parent && parent->getNodeSubType() == NODE_COMPOUND_STATEMENT)
                {
                    parent->removeChild(old);
                    ++changes;
                    return;
                }
                now = ctx.allocator->newTracked<LSLNopStatement>();
                now->setLoc(old->getLoc());
            }
            LSLASTNode::replaceNode(old, now);
            ++changes;
        }

        // A call to a function of the script's own whose body is empty,
        // with arguments that do nothing on their own.
        bool callsNothing(LSLExpression* expr) const
        {
            if (!expr || expr->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
            {
                return false;
            }
            LSLSymbol* sym      = expr->getSymbol();
            auto       function = sym ? mFunctions.find(sym) : mFunctions.end();
            if (function == mFunctions.end())
            {
                return false;
            }
            LSLASTNode* body = function->second->getStatements();
            if (!body || body->getNodeSubType() != NODE_COMPOUND_STATEMENT || body->hasChildren())
            {
                return false;
            }
            for (LSLASTNode* arg : *static_cast<LSLFunctionExpression*>(expr)->getArguments())
            {
                if (!changesNothing(arg))
                {
                    return false;
                }
            }
            return true;
        }

        // Whether a label a jump from outside a statement goes to is in
        // it, however far down: the one way in to a statement nothing
        // before it reaches. A jump from inside to a label inside -- a
        // loop made of labels -- is no way in.
        static bool holdsLiveLabel(LSLASTNode* statement)
        {
            // Each label's references are its own name and every jump to
            // it; the jumps inside the statement are taken off.
            boost::unordered_flat_map<LSLSymbol*, int> inside;
            std::vector<LSLSymbol*>                    labels;
            const std::function<void(LSLASTNode*)> gather = [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_LABEL)
                {
                    if (LSLSymbol* sym = node->getSymbol())
                    {
                        labels.push_back(sym);
                    }
                }
                else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_JUMP_STATEMENT)
                {
                    if (LSLSymbol* sym = static_cast<LSLJumpStatement*>(node)->getIdentifier()->getSymbol())
                    {
                        ++inside[sym];
                    }
                }
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    gather(child);
                }
            };
            gather(statement);
            for (LSLSymbol* label : labels)
            {
                if (label->getReferences() - 1 > inside[label])
                {
                    return true;
                }
            }
            return false;
        }

        // Whether taking `stmt` away for its branch would leave a state
        // change in a function with no `if` around it: a function may
        // change state only under an `if`, which the compiler checks
        // (Tailslide's E_CHANGE_STATE_IN_FUNCTION), whatever the `if`
        // tests. An event may change state anywhere.
        static bool lastIfOfStateChange(LSLIfStatement* stmt, LSLStatement* branch)
        {
            for (LSLASTNode* up = stmt->getParent(); up; up = up->getParent())
            {
                if (up->getNodeType() == NODE_STATE || (up->getNodeType() == NODE_STATEMENT && up->getNodeSubType() == NODE_IF_STATEMENT))
                {
                    return false;
                }
                if (up->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    break;
                }
            }
            bool orphaned = false;
            const std::function<void(LSLASTNode*, bool)> look = [&](LSLASTNode* node, bool underIf) {
                if (orphaned || !node)
                {
                    return;
                }
                if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT && !underIf)
                {
                    orphaned = true;
                    return;
                }
                const bool isIf = node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_IF_STATEMENT;
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    look(child, underIf || isIf);
                }
            };
            look(branch, false);
            return orphaned;
        }

        bool unusedLabel(LSLASTNode* label)
        {
            LSLSymbol* sym = label->getSymbol();
            if (!sym || sym->getReferences() > 1)
            {
                return false;
            }
            // Another label of its name, and a jump the compiler finds this
            // one for, runs to the last: the count of this one's own jumps
            // does not say it is unused.
            if (labelsNamed(label, static_cast<LSLLabel*>(label)->getIdentifier()->getName()) > 1)
            {
                return false;
            }
            report.note(label->getLoc(), "OptimizerRemovedLabel", "removed the label [1], which nothing jumps to", { render(label) });
            return true;
        }

        bool unusedLocal(LSLDeclaration* decl)
        {
            LSLSymbol* sym = decl->getSymbol();
            if (!sym || sym->getReferences() != 1 || sym->getAssignments() != 0)
            {
                return false;
            }
            LSLASTNode* init = decl->getInitializer();
            if (init && !changesNothing(init))
            {
                return false;
            }
            forget(decl, sym);
            report.note(decl->getLoc(), "OptimizerRemovedLocal", "removed the unused local [1]", { sym->getName() });
            return true;
        }

        static void forget(LSLASTNode* node, LSLSymbol* sym)
        {
            for (LSLASTNode* up = node; up; up = up->getParent())
            {
                if (up->getSymbolTable() && up->getSymbolTable()->remove(sym))
                {
                    return;
                }
            }
        }

        void globals(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* global : *script->getGlobals())
            {
                LSLSymbol* sym = global->getSymbol();
                if (!sym || sym->getReferences() != 1)
                {
                    continue;
                }
                if (global->getNodeType() == NODE_GLOBAL_VARIABLE)
                {
                    report.note(global->getLoc(), "OptimizerRemovedGlobal", "removed the unused global [1]", { sym->getName() });
                    going.push_back(global);
                }
                else if (global->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    report.note(global->getLoc(), "OptimizerRemovedFunction", "removed the unused function [1]", { sym->getName() });
                    going.push_back(global);
                }
            }
            for (LSLASTNode* global : going)
            {
                script->getSymbolTable()->remove(global->getSymbol());
                script->getGlobals()->removeChild(global);
                ++changes;
            }
        }

        boost::unordered_flat_map<LSLSymbol*, LSLGlobalFunction*> mFunctions;
        boost::unordered_flat_map<LSLASTNode*, boost::unordered_flat_map<std::string_view, int>> mNames;

        // Every node of a subtree, the root first.
        template <class F> static void each(LSLASTNode* root, const F& f)
        {
            std::vector<LSLASTNode*> stack{ root };
            while (!stack.empty())
            {
                LSLASTNode* node = stack.back();
                stack.pop_back();
                if (!node)
                {
                    continue;
                }
                f(node);
                for (LSLASTNode* child = node->getChild(0); child; child = child->getNext())
                {
                    stack.push_back(child);
                }
            }
        }

        // What can run: the default state's handlers, each function they
        // call and each state they enter, and on through those. A function
        // or a state nothing that runs reaches goes, though others that do
        // not run reach it -- functions that call only each other.
        void unreached(LSLScript* script)
        {
            boost::unordered_flat_map<LSLSymbol*, LSLASTNode*> states;
            LSLASTNode*                                        start = nullptr;
            for (LSLASTNode* state : *script->getStates())
            {
                if (LSLSymbol* sym = state->getSymbol())
                {
                    states[sym] = state;
                    start       = !strcmp(sym->getName(), "default") ? state : start;
                }
            }
            if (!start)
            {
                return;
            }
            boost::unordered_flat_set<LSLASTNode*> reached{ start };
            std::vector<LSLASTNode*>               todo{ start };
            const auto                             reach = [&](LSLASTNode* node) {
                if (node && reached.insert(node).second)
                {
                    todo.push_back(node);
                }
            };
            while (!todo.empty())
            {
                LSLASTNode* from = todo.back();
                todo.pop_back();
                each(from, [&](LSLASTNode* node) {
                    if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
                    {
                        LSLSymbol* sym   = static_cast<LSLFunctionExpression*>(node)->getSymbol();
                        const auto found = sym ? mFunctions.find(sym) : mFunctions.end();
                        reach(found == mFunctions.end() ? nullptr : found->second);
                    }
                    else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT)
                    {
                        LSLIdentifier* id    = static_cast<LSLStateStatement*>(node)->getIdentifier();
                        const auto     found = id && id->getSymbol() ? states.find(id->getSymbol()) : states.end();
                        reach(found == states.end() ? nullptr : found->second);
                    }
                });
            }
            for (LSLASTNode* list : { static_cast<LSLASTNode*>(script->getGlobals()), static_cast<LSLASTNode*>(script->getStates()) })
            {
                std::vector<LSLASTNode*> going;
                for (LSLASTNode* node = list->getChild(0); node; node = node->getNext())
                {
                    const bool function = node->getNodeType() == NODE_GLOBAL_FUNCTION;
                    if ((function || node->getNodeType() == NODE_STATE) && node->getSymbol() && !reached.contains(node))
                    {
                        report.note(node->getLoc(), function ? "OptimizerRemovedUncalled" : "OptimizerRemovedUnentered",
                                    function ? "removed the function [1], which nothing that runs calls" : "removed the state [1], which nothing that runs enters",
                                    { node->getSymbol()->getName() });
                        going.push_back(node);
                    }
                }
                for (LSLASTNode* node : going)
                {
                    script->getSymbolTable()->remove(node->getSymbol());
                    list->removeChild(node);
                    ++changes;
                }
            }
        }

        // A handler with nothing in it goes where having it changes
        // nothing: not a touch's, which makes the object touchable, nor a
        // collision's, money's or control's, whose handler changes what the
        // object does. A state keeps one handler, which it must have.
        void emptyHandlers(LSLScript* script)
        {
            static constexpr std::string_view QUIET[] = { "state_entry", "state_exit", "on_rez", "attach", "changed", "timer", "listen", "link_message",
                                                          "dataserver", "http_response", "sensor", "no_sensor", "at_target", "not_at_target",
                                                          "at_rot_target", "not_at_rot_target", "moving_start", "moving_end", "object_rez",
                                                          "email", "remote_data", "run_time_permissions", "linkset_data", "transaction_result",
                                                          "path_update", "experience_permissions", "experience_permissions_denied" };
            for (LSLASTNode* state = script->getStates()->getChild(0); state; state = state->getNext())
            {
                LSLASTNode* handlers = state->getChild(1);
                if (!handlers)
                {
                    continue;
                }
                std::vector<LSLASTNode*> going;
                size_t                   count = 0;
                for (LSLASTNode* handler = handlers->getChild(0); handler; handler = handler->getNext())
                {
                    ++count;
                    auto*         event = static_cast<LSLEventHandler*>(handler);
                    LSLStatement* body  = event->getStatements();
                    LSLSymbol*    sym   = event->getSymbol();
                    if (sym && body && body->getNodeSubType() == NODE_COMPOUND_STATEMENT && !body->hasChildren() &&
                        std::find(std::begin(QUIET), std::end(QUIET), std::string_view(sym->getName())) != std::end(QUIET))
                    {
                        going.push_back(handler);
                    }
                }
                if (going.size() == count)
                {
                    going.pop_back();
                }
                for (LSLASTNode* handler : going)
                {
                    report.note(handler->getLoc(), "OptimizerRemovedEmptyHandler", "removed the empty handler of [1], which having changes nothing",
                                { handler->getSymbol()->getName() });
                    handlers->removeChild(handler);
                    ++changes;
                }
            }
        }

        // Whether an assignment's or an increment's value goes unread: it is
        // a statement of its own, or a for's first part or step.
        static bool unread(LSLASTNode* expr)
        {
            LSLASTNode* up = expr->getParent();
            if (up && up->getNodeType() == NODE_STATEMENT && up->getNodeSubType() == NODE_EXPRESSION_STATEMENT)
            {
                return true;
            }
            LSLASTNode* loop = up ? up->getParent() : nullptr;
            return up && up->getNodeType() == NODE_AST_NODE_LIST && loop && loop->getNodeType() == NODE_STATEMENT &&
                   loop->getNodeSubType() == NODE_FOR_STATEMENT &&
                   (up == static_cast<LSLForStatement*>(loop)->getInitExprs() || up == static_cast<LSLForStatement*>(loop)->getIncrExprs());
        }

        // A statement taken out: from its block or its for, or made nothing
        // where it is the whole of an if's or a loop's body.
        void takeOut(LSLASTNode* node)
        {
            LSLASTNode* up = node->getParent();
            if (up && (up->getNodeSubType() == NODE_COMPOUND_STATEMENT || up->getNodeType() == NODE_AST_NODE_LIST))
            {
                up->removeChild(node);
                return;
            }
            LSLASTNode::replaceNode(node, ctx.allocator->newTracked<LSLNopStatement>());
        }

        static bool parameter(LSLSymbol* sym)
        {
            return sym->getSubType() == SYM_FUNCTION_PARAMETER || sym->getSubType() == SYM_EVENT_PARAMETER;
        }

        // A variable -- a local, a global -- set and never read goes, and
        // each place it is set keeps what setting it ran: `x = f();` is
        // `f();`, `x++;` nothing, and `integer h = llListen(...);` the
        // call. Only where every place that names it but its declaration
        // sets it, and nothing reads what was set. A parameter so set
        // loses its writes the same way, and stays.
        void writeOnly(LSLScript* script)
        {
            struct Uses
            {
                LSLASTNode*              declaration = nullptr;
                std::vector<LSLASTNode*> writes;
                bool                     read = false;
            };
            boost::unordered_flat_map<LSLSymbol*, Uses> uses;
            std::vector<LSLSymbol*>                     order;
            const auto                                  of = [&](LSLSymbol* sym) -> Uses& {
                auto [it, fresh] = uses.try_emplace(sym);
                if (fresh)
                {
                    order.push_back(sym);
                }
                return it->second;
            };
            each(script, [&](LSLASTNode* node) {
                if (node->getNodeType() == NODE_GLOBAL_VARIABLE && node->getSymbol())
                {
                    of(node->getSymbol()).declaration = node;
                }
                else if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_DECLARATION && node->getSymbol())
                {
                    of(node->getSymbol()).declaration = node;
                }
                else if (node->getNodeType() == NODE_EXPRESSION && node->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    LSLSymbol* sym = node->getSymbol();
                    if (!sym || (sym->getSubType() != SYM_LOCAL && sym->getSubType() != SYM_GLOBAL && !parameter(sym)))
                    {
                        return;
                    }
                    LSLASTNode* parent = node->getParent();
                    const bool  write  = parent && parent->getNodeType() == NODE_EXPRESSION &&
                                        operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(0) == node;
                    if (write && unread(parent))
                    {
                        of(sym).writes.push_back(parent);
                    }
                    else
                    {
                        of(sym).read = true;
                    }
                }
            });
            for (LSLSymbol* sym : order)
            {
                // A global never used at all goes with the unused (globals()),
                // and a local set to nothing that changes anything goes with
                // those (unusedLocal); one set to what does is this pass's.
                // A parameter's writes go, and the parameter stays.
                Uses&      use    = uses[sym];
                const bool global = use.declaration && use.declaration->getNodeType() == NODE_GLOBAL_VARIABLE;
                const bool given  = parameter(sym);
                if (use.read || (!use.declaration && !given) || ((global || given) && use.writes.empty()))
                {
                    continue;
                }
                report.note(given ? use.writes.front()->getLoc() : use.declaration->getLoc(), "OptimizerRemovedWriteOnly",
                            "removed [1], which is set and never read", { sym->getName() });
                for (LSLASTNode* write : use.writes)
                {
                    LSLASTNode* value  = write->getNodeSubType() == NODE_BINARY_EXPRESSION ? write->getChild(1) : nullptr;
                    LSLASTNode* holder = write->getParent()->getNodeType() == NODE_AST_NODE_LIST ? write : write->getParent();
                    if (value && !changesNothing(value))
                    {
                        // What it was set to, still worked out.
                        LSLASTNode::replaceNode(write, write->takeChild(1));
                    }
                    else
                    {
                        takeOut(holder);
                    }
                }
                if (global)
                {
                    script->getSymbolTable()->remove(sym);
                    script->getGlobals()->removeChild(use.declaration);
                }
                else if (!given)
                {
                    auto*       decl = static_cast<LSLDeclaration*>(use.declaration);
                    LSLASTNode* init = decl->getInitializer();
                    forget(decl, sym);
                    if (init && !changesNothing(init))
                    {
                        LSLASTNode::replaceNode(decl, ctx.allocator->newTracked<LSLExpressionStatement>(static_cast<LSLExpression*>(decl->takeChild(1))));
                    }
                    else
                    {
                        takeOut(decl);
                    }
                }
                ++changes;
            }
        }

        void states(LSLScript* script)
        {
            std::vector<LSLASTNode*> going;
            for (LSLASTNode* state : *script->getStates())
            {
                LSLSymbol* sym = state->getSymbol();
                if (!sym || sym->getReferences() != 1 || !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                report.note(state->getLoc(), "OptimizerRemovedState", "removed the state [1], which nothing enters", { sym->getName() });
                going.push_back(state);
            }
            for (LSLASTNode* state : going)
            {
                script->getSymbolTable()->remove(state->getSymbol());
                script->getStates()->removeChild(state);
                ++changes;
            }
        }
    };
}

    int removeDead(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        DeadCode dead(ctx, report, options);
        return dead.run(script);
    }
}
