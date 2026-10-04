/**
 * @file allslrepeatedcalls.cpp
 * @brief The LSL optimizer's repeated calls kept in a local.
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

#include "allslrepeatedcalls.h"

#include "allsleffects.h"
#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace ALLSLPasses
{
namespace
{
    // A pure library call made more than once in a straight run of a
    // block's statements, given only what nothing in the run changes: made
    // once, into a local declared before the run, which each place then
    // reads -- where the target's compiler says that is no larger, the call
    // at each place weighed against the local. The run holds no label, which
    // a jump could bring the code in by past the local; and its first place
    // is one its statement always comes to, so that the call runs no more
    // often than it did. It goes ahead of that statement, which changes
    // nothing it is given.
    class RepeatedCalls : public ASTVisitor, public Pass
    {
    public:
        RepeatedCalls(Ctx& c, Report& r, const ALLSLOptimizer::Options& o, LSLScript* script) : Pass(c, r, o)
        {
            // Every name the script writes, which a local's may be none of:
            // LSL has no shadowing.
            each(script, [this](LSLASTNode* n) {
                if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                {
                    mTaken.insert(static_cast<LSLIdentifier*>(n)->getName());
                }
            });
        }

        // The block's own runs first: a local before one of its statements
        // holds the places in the blocks that statement holds too.
        bool visit(LSLCompoundStatement* block) override
        {
            while (keepOne(block))
            {
            }
            visitChildren(block);
            return false;
        }

    private:
        // Each call that may be kept, by what it is: its text and the
        // variables it reads, in the order it reads them.
        struct Group
        {
            std::vector<LSLSymbol*>                                    reads;
            std::vector<std::pair<size_t, LSLFunctionExpression*>>     places;
            size_t                                                     length = 0;
        };

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

        static bool keepable(LSLFunctionExpression* call)
        {
            LSLSymbol* sym = call->getSymbol();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !isPure(sym->getName()) || !call->getArguments() || !call->getArguments()->hasChildren())
            {
                return false;
            }
            switch (call->getIType())
            {
                case LST_INTEGER:
                case LST_FLOATINGPOINT:
                case LST_STRING:
                case LST_KEY:
                case LST_VECTOR:
                case LST_QUATERNION:
                case LST_LIST:
                    return sideEffectFree(call);
                default:
                    return false;
            }
        }

        static const char* typeName(LSLIType type)
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

        // Whether a statement's own evaluation always comes to a node in
        // it: through expressions alone, from an expression statement, a
        // declaration's value, a return, or what an if, a while or a for
        // looks at first.
        static bool always(LSLASTNode* node, LSLASTNode* statement)
        {
            LSLASTNode* child = node;
            for (LSLASTNode* up = node->getParent(); up; child = up, up = up->getParent())
            {
                if (up == statement)
                {
                    switch (statement->getNodeSubType())
                    {
                        case NODE_EXPRESSION_STATEMENT:
                        case NODE_DECLARATION:
                        case NODE_RETURN_STATEMENT:
                            return true;
                        case NODE_IF_STATEMENT:
                        case NODE_WHILE_STATEMENT:
                            return child == statement->getChild(0);
                        case NODE_FOR_STATEMENT:
                            // Its first part, and its check, which runs once
                            // at the least.
                            return child == statement->getChild(0) || child == statement->getChild(1);
                        default:
                            return false;
                    }
                }
                if (up->getNodeType() != NODE_EXPRESSION && up->getNodeType() != NODE_AST_NODE_LIST)
                {
                    return false;
                }
            }
            return false;
        }

        static bool holdsLabel(LSLASTNode* root)
        {
            bool found = false;
            each(root, [&found](LSLASTNode* n) { found = found || (n->getNodeType() == NODE_STATEMENT && n->getNodeSubType() == NODE_LABEL); });
            return found;
        }

        // A name of the script's own for the local: the function's without
        // its ll, a length's `length`, numbered where it is taken.
        std::string fresh(const char* function)
        {
            const std::string_view called = function;
            std::string base = called == "llGetListLength" || called == "llStringLength" ? std::string("length") : std::string(called.substr(2));
            base[0]          = static_cast<char>(std::tolower(static_cast<unsigned char>(base[0])));
            const auto usable = [&](const std::string& name) {
                return !mTaken.contains(name) && ALScriptLexicon::lslWord(name) == ALScriptLexicon::LSL_NAME &&
                       !(ctx.context->builtins && ctx.context->builtins->lookup(name.c_str(), SYM_ANY));
            };
            std::string name = base;
            for (int n = 2; !usable(name); ++n)
            {
                name = base + std::to_string(n);
            }
            mTaken.insert(name);
            return name;
        }

        // Whether the local is no larger than the call at `places` places,
        // each compiled in a function of its own given what the call reads.
        bool smaller(LSLFunctionExpression* call, const std::vector<LSLSymbol*>& reads, size_t places)
        {
            const char* type = typeName(call->getIType());
            std::string given;
            std::vector<LSLSymbol*> seen;
            for (LSLSymbol* sym : reads)
            {
                if (sym->getSubType() == SYM_BUILTIN || std::find(seen.begin(), seen.end(), sym) != seen.end())
                {
                    continue;
                }
                const char* its = typeName(sym->getIType());
                if (!its)
                {
                    return false;
                }
                seen.push_back(sym);
                given += std::string(given.empty() ? "" : ", ") + its + " " + sym->getName();
            }
            const std::string text    = render(call);
            const std::string head    = "keptCall(" + given + ")\n{\n";
            const std::string tail    = "}\ndefault\n{\n    state_entry()\n    {\n    }\n}\n";
            std::string       asCalls = head;
            std::string       asLocal = head + "    " + type + " kept = " + text + ";\n";
            for (size_t i = 0; i < places; ++i)
            {
                asCalls += "    llOwnerSay((string)(" + text + "));\n";
                asLocal += "    llOwnerSay((string)kept);\n";
            }
            asCalls += tail;
            asLocal += tail;
            const std::string key = "kept\n" + asCalls;
            if (const auto found = ctx.answers.find(key); found != ctx.answers.end())
            {
                return found->second;
            }
            const ALScriptWeight byCalls = weigh(ctx.target, asCalls);
            const ALScriptWeight byLocal = weigh(ctx.target, asLocal);
            const bool           no      = byCalls.compiled && byLocal.compiled && byLocal.total <= byCalls.total;
            ctx.answers.emplace(key, no);
            return no;
        }

        // The first call worth keeping in the block kept; whether there was one.
        bool keepOne(LSLCompoundStatement* block)
        {
            std::vector<LSLASTNode*> statements;
            for (LSLASTNode* stmt : *block)
            {
                statements.push_back(stmt);
            }
            std::vector<Group>                                                                   groups;
            boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> index;
            for (size_t i = 0; i < statements.size(); ++i)
            {
                each(statements[i], [&](LSLASTNode* n) {
                    if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_FUNCTION_EXPRESSION ||
                        !keepable(static_cast<LSLFunctionExpression*>(n)))
                    {
                        return;
                    }
                    auto*                   call = static_cast<LSLFunctionExpression*>(n);
                    std::vector<LSLSymbol*> reads;
                    each(call, [&reads](LSLASTNode* m) {
                        if (m->getNodeType() == NODE_EXPRESSION && m->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                        {
                            reads.push_back(m->getSymbol());
                        }
                    });
                    const std::string text = render(call);
                    std::string       key  = text;
                    for (LSLSymbol* sym : reads)
                    {
                        key += "\n" + std::to_string(reinterpret_cast<uintptr_t>(sym));
                    }
                    auto [at, fresh] = index.try_emplace(key, groups.size());
                    if (fresh)
                    {
                        groups.push_back(Group{ std::move(reads), {}, text.size() });
                    }
                    groups[at->second].places.emplace_back(i, call);
                });
            }
            // The outermost first: a call kept takes with it the calls it is
            // given, which would otherwise be kept apart.
            std::vector<Group*> order;
            for (Group& g : groups)
            {
                if (g.places.size() > 1)
                {
                    order.push_back(&g);
                }
            }
            if (order.empty())
            {
                return false;
            }
            std::stable_sort(order.begin(), order.end(), [](const Group* a, const Group* b) { return a->length > b->length; });
            std::vector<ALLSLEffects::Writes> writes(statements.size());
            std::vector<bool>                 labelled(statements.size());
            for (size_t i = 0; i < statements.size(); ++i)
            {
                writes[i]   = ctx.effects->of(statements[i]);
                labelled[i] = holdsLabel(statements[i]);
            }
            for (Group* g : order)
            {
                // Runs of statements that change nothing the call reads, each
                // begun where a place is always come to.
                size_t                              open  = std::string::npos;
                LSLFunctionExpression*              first = nullptr;
                std::vector<LSLFunctionExpression*> held;
                const auto close = [&]() {
                    const bool kept = held.size() > 1 && smaller(first, g->reads, held.size());
                    if (kept)
                    {
                        keep(block, statements[open], first, held);
                    }
                    open = std::string::npos;
                    held.clear();
                    return kept;
                };
                size_t next = 0;
                for (size_t i = 0; i < statements.size(); ++i)
                {
                    const bool barred = labelled[i] || std::any_of(g->reads.begin(), g->reads.end(), [&](LSLSymbol* s) { return writes[i].writes(s); });
                    std::vector<LSLFunctionExpression*> here;
                    while (next < g->places.size() && g->places[next].first == i)
                    {
                        here.push_back(g->places[next++].second);
                    }
                    if (barred)
                    {
                        if (close())
                        {
                            return true;
                        }
                        continue;
                    }
                    if (here.empty())
                    {
                        continue;
                    }
                    if (open == std::string::npos)
                    {
                        const auto starts = std::find_if(here.begin(), here.end(), [&](LSLFunctionExpression* c) { return always(c, statements[i]); });
                        if (starts == here.end())
                        {
                            continue;
                        }
                        open  = i;
                        first = *starts;
                    }
                    held.insert(held.end(), here.begin(), here.end());
                }
                if (close())
                {
                    return true;
                }
            }
            return false;
        }

        // `first`'s call made the value of a local declared before `before`,
        // and each place held made a read of it.
        void keep(LSLCompoundStatement* block, LSLASTNode* before, LSLFunctionExpression* first, const std::vector<LSLFunctionExpression*>& held)
        {
            LSLType*          type  = first->getType();
            const std::string local = fresh(first->getSymbol()->getName());
            const std::string said  = report.wanted() ? render(first) : std::string();
            const char*       name  = ctx.allocator->copyStr(local.c_str());
            auto*             id    = ctx.allocator->newTracked<LSLIdentifier>(type, name);
            id->setLoc(first->getLoc());
            auto* decl = ctx.allocator->newTracked<LSLDeclaration>(id, static_cast<LSLExpression*>(nullptr));
            decl->setLoc(first->getLoc());
            auto* sym = ctx.allocator->newTracked<LSLSymbol>(name, type, SYM_VARIABLE, SYM_LOCAL, first->getLoc(), nullptr, decl);
            id->setSymbol(sym);
            for (LSLFunctionExpression* place : held)
            {
                auto* read = ctx.allocator->newTracked<LSLIdentifier>(type, name);
                read->setSymbol(sym);
                read->setLoc(place->getLoc());
                auto* lvalue = ctx.allocator->newTracked<LSLLValueExpression>(read, static_cast<LSLIdentifier*>(nullptr));
                lvalue->setType(type);
                lvalue->setLoc(place->getLoc());
                lvalue->setIsFoldable(true);
                LSLASTNode::replaceNode(place, lvalue);
            }
            decl->setChild(1, first);
            {
                // The statements are the ones they were: counted neither out
                // nor in again. The references are found again after the pass.
                const Uncounted          uncounted(*ctx.context);
                std::vector<LSLASTNode*> statements;
                for (LSLASTNode* stmt : *block)
                {
                    statements.push_back(stmt);
                }
                for (LSLASTNode* stmt : statements)
                {
                    block->removeChild(stmt);
                }
                for (LSLASTNode* stmt : statements)
                {
                    if (stmt == before)
                    {
                        block->pushChild(decl);
                    }
                    block->pushChild(stmt);
                }
            }
            if (LSLSymbolTable* table = block->getSymbolTable())
            {
                table->define(sym);
            }
            else
            {
                decl->defineSymbol(sym);
            }
            report.note(first->getLoc(), "OptimizerKeptCall", "kept [1] in the local [2], which its [3] places read",
                        { said, local, std::to_string(held.size()) });
            ++changes;
        }

        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> mTaken;
    };
}

    int keepRepeatedCalls(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        RepeatedCalls repeated(ctx, report, options, script);
        script->visit(&repeated);
        return repeated.changes;
    }
}
