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

#include "allslcosts.h"
#include "allsleffects.h"
#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>

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
    // A global's read so too, where the run sets nothing of it; and, once
    // nothing more will fold, a constant written again and again: each read
    // or written at any time alike, its first place anywhere in the run.
    class RepeatedCalls : public ASTVisitor, public Pass
    {
    public:
        RepeatedCalls(Ctx& c, Report& r, const ALLSLOptimizer::Options& o, LSLScript* script, Keeping keeping)
            : Pass(c, r, o), mCosts(ALLSLCosts::of(o.target)), mKeeping(keeping)
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
            std::vector<LSLSymbol*>                            reads;
            std::vector<std::pair<size_t, LSLExpression*>>     places;
            size_t                                             length = 0;
        };

        const ALLSLCosts& mCosts;
        const Keeping     mKeeping;

        // A read of a global the script sets, whole, that is no write of it.
        static LSLSymbol* globalRead(LSLASTNode* n)
        {
            if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_LVALUE_EXPRESSION || static_cast<LSLLValueExpression*>(n)->getMember())
            {
                return nullptr;
            }
            LSLSymbol*  sym    = n->getSymbol();
            LSLASTNode* parent = n->getParent();
            const bool  write  = parent && parent->getNodeType() == NODE_EXPRESSION && operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) &&
                                parent->getChild(0) == n;
            // One never set is the folder's, which writes its value out or not.
            return sym && sym->getSymbolType() == SYM_VARIABLE && sym->getSubType() == SYM_GLOBAL && sym->getAssignments() > 0 && !write &&
                           typeName(sym->getIType())
                       ? sym
                       : nullptr;
        }

        // A constant of a type a local may hold, but a list.
        static bool keptConstant(LSLASTNode* n)
        {
            if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_CONSTANT_EXPRESSION || !n->getConstantValue())
            {
                return false;
            }
            const LSLIType type = n->getIType();
            return type != LST_LIST && typeName(type) && !n->getConstantValue()->containsNaN();
        }

        // A number as the printer writes it for the target: whole as an
        // integer where that is smaller, negative as its cast where that is.
        std::string printedNumber(double v, bool asInteger, bool alone) const
        {
            const bool  wide = ctx.target == ALLSLOptimizer::Target::Luau;
            std::string text = asInteger && integral(v) && mCosts.integerForFloat ? std::to_string(static_cast<long long>(v)) : floatText(v, wide);
            if (alone && !asInteger && mCosts.castForWholeFloat && integral(v) && std::fabs(v) < 2147483648.0 && !(v == 0.0 && std::signbit(v)))
            {
                return "((float)" + std::to_string(static_cast<long long>(v)) + ")";
            }
            if (v < 0.0 && mCosts.castForNegative)
            {
                const bool whole = text.find_first_not_of("-0123456789") == std::string::npos;
                return std::string(whole ? "((integer)" : "((float)") + text + ")";
            }
            return text;
        }

        // A constant as the printer writes it, for a weighing.
        std::optional<std::string> printed(LSLConstant* cv) const
        {
            switch (cv->getNodeSubType())
            {
                case NODE_INTEGER_CONSTANT:
                {
                    const S32 v = static_cast<LSLIntegerConstant*>(cv)->getValue();
                    return v < 0 && mCosts.castForNegative ? "((integer)" + std::to_string(v) + ")" : std::to_string(v);
                }
                case NODE_FLOAT_CONSTANT:
                    return printedNumber(static_cast<LSLFloatConstant*>(cv)->getValue(), false, true);
                case NODE_VECTOR_CONSTANT:
                {
                    const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                    return "<" + printedNumber(v->x, true, false) + ", " + printedNumber(v->y, true, false) + ", " + printedNumber(v->z, true, false) + ">";
                }
                case NODE_QUATERNION_CONSTANT:
                {
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                    return "<" + printedNumber(q->x, true, false) + ", " + printedNumber(q->y, true, false) + ", " + printedNumber(q->z, true, false) + ", " +
                           printedNumber(q->s, true, false) + ">";
                }
                default:
                    return ALLSLValues::literal(cv, ctx.target == ALLSLOptimizer::Target::Luau);
            }
        }

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

        // What the local is named for: a call's function without its ll, a
        // length's `length`; a global's `local` and its name; a constant's
        // type, by its first letter.
        static std::string nameFor(LSLExpression* kept)
        {
            if (kept->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                const std::string_view called = kept->getSymbol()->getName();
                std::string base = called == "llGetListLength" || called == "llStringLength" ? std::string("length") : std::string(called.substr(2));
                base[0]          = static_cast<char>(std::tolower(static_cast<unsigned char>(base[0])));
                return base;
            }
            if (kept->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                std::string name = kept->getSymbol()->getName();
                name[0]          = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                return "local" + name;
            }
            switch (kept->getIType())
            {
                case LST_INTEGER: return "n";
                case LST_FLOATINGPOINT: return "f";
                case LST_STRING: return "s";
                case LST_KEY: return "k";
                case LST_VECTOR: return "v";
                default: return "r";
            }
        }

        // A name of the script's own for the local, numbered where it is taken.
        std::string fresh(const std::string& base)
        {
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

        // Whether the local is no larger than what it keeps at `places`
        // places, each compiled in a function of its own: a call given what
        // it reads, a global's read with the global declared, a constant as
        // the printer writes it.
        bool smaller(LSLExpression* first, const std::vector<LSLSymbol*>& reads, size_t places)
        {
            const char* type = typeName(first->getIType());
            std::string globals;
            std::string given;
            std::string text;
            if (first->getNodeSubType() == NODE_CONSTANT_EXPRESSION)
            {
                const std::optional<std::string> literal = printed(first->getConstantValue());
                if (!literal)
                {
                    return false;
                }
                text = *literal;
            }
            else if (first->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                LSLSymbol* sym = first->getSymbol();
                globals        = std::string(typeName(sym->getIType())) + " " + sym->getName() + ";\n";
                text           = sym->getName();
            }
            else
            {
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
                text = render(first);
            }
            const std::string head    = globals + "keptCall(" + given + ")\n{\n";
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
            // The local's frame too, on a target that keeps one for it.
            const bool           no      = byCalls.compiled && byLocal.compiled && byLocal.total + mCosts.localFrame <= byCalls.total;
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
                    const bool call     = mKeeping == Keeping::Calls && n->getNodeType() == NODE_EXPRESSION &&
                                      n->getNodeSubType() == NODE_FUNCTION_EXPRESSION && keepable(static_cast<LSLFunctionExpression*>(n));
                    const bool global   = mKeeping == Keeping::Calls && globalRead(n);
                    const bool constant = mKeeping == Keeping::Constants && keptConstant(n);
                    if (!call && !global && !constant)
                    {
                        return;
                    }
                    auto*                   kept = static_cast<LSLExpression*>(n);
                    std::vector<LSLSymbol*> reads;
                    each(kept, [&reads](LSLASTNode* m) {
                        if (m->getNodeType() == NODE_EXPRESSION && m->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                        {
                            reads.push_back(m->getSymbol());
                        }
                    });
                    // A string's and a key's literals are written alike.
                    const std::string text = render(kept);
                    std::string       key  = std::string(typeName(kept->getIType())) + "\n" + text;
                    for (LSLSymbol* sym : reads)
                    {
                        key += "\n" + std::to_string(reinterpret_cast<uintptr_t>(sym));
                    }
                    auto [at, fresh] = index.try_emplace(key, groups.size());
                    if (fresh)
                    {
                        groups.push_back(Group{ std::move(reads), {}, text.size() });
                    }
                    groups[at->second].places.emplace_back(i, kept);
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
                size_t                      open  = std::string::npos;
                LSLExpression*              first = nullptr;
                std::vector<LSLExpression*> held;
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
                    std::vector<LSLExpression*> here;
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
                        // A call only where its statement always comes to it; what
                        // reads the same at any time anywhere.
                        const auto starts = std::find_if(here.begin(), here.end(), [&](LSLExpression* c) {
                            return c->getNodeSubType() != NODE_FUNCTION_EXPRESSION || always(c, statements[i]);
                        });
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
        void keep(LSLCompoundStatement* block, LSLASTNode* before, LSLExpression* first, const std::vector<LSLExpression*>& held)
        {
            LSLType*          type  = first->getType();
            const std::string local = fresh(nameFor(first));
            const std::string said  = report.wanted() ? render(first) : std::string();
            const char*       name  = ctx.allocator->copyStr(local.c_str());
            auto*             id    = ctx.allocator->newTracked<LSLIdentifier>(type, name);
            id->setLoc(first->getLoc());
            auto* decl = ctx.allocator->newTracked<LSLDeclaration>(id, static_cast<LSLExpression*>(nullptr));
            decl->setLoc(first->getLoc());
            auto* sym = ctx.allocator->newTracked<LSLSymbol>(name, type, SYM_VARIABLE, SYM_LOCAL, first->getLoc(), nullptr, decl);
            id->setSymbol(sym);
            for (LSLExpression* place : held)
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

    int keepRepeatedCalls(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script, Keeping keeping)
    {
        RepeatedCalls repeated(ctx, report, options, script, keeping);
        script->visit(&repeated);
        return repeated.changes;
    }
}
