/**
 * @file allslconstantglobals.cpp
 * @brief The LSL optimizer's constants written at many places kept once in a global.
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

#include "allslconstantglobals.h"

#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <vector>

namespace ALLSLPasses
{
namespace
{
    class ConstantGlobals : public Pass
    {
    public:
        using Pass::Pass;

        int run(LSLScript* script)
        {
            boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> taken;
            eachNode(script, [&](LSLASTNode* n) {
                if (n->getNodeType() == NODE_IDENTIFIER && static_cast<LSLIdentifier*>(n)->getName())
                {
                    taken.insert(static_cast<LSLIdentifier*>(n)->getName());
                }
            });
            // Each constant's places in the functions and handlers, by its
            // type and value, in the order they come.
            struct Places
            {
                std::vector<LSLExpression*> at;
            };
            std::vector<std::string>                                                            order;
            boost::unordered_flat_map<std::string, Places, ll::string_hash, std::equal_to<>> places;
            const auto gather = [&](LSLASTNode* body) {
                eachNode(body, [&](LSLASTNode* n) {
                    if (n->getNodeType() != NODE_EXPRESSION || n->getNodeSubType() != NODE_CONSTANT_EXPRESSION || !n->getConstantValue() ||
                        n->getIType() == LST_LIST || n->getConstantValue()->containsNaN())
                    {
                        return;
                    }
                    const std::string key = std::to_string(static_cast<int>(n->getIType())) + "\n" + render(n);
                    auto [it, fresh]      = places.try_emplace(key);
                    if (fresh)
                    {
                        order.push_back(key);
                    }
                    it->second.at.push_back(static_cast<LSLExpression*>(n));
                });
            };
            for (LSLASTNode* global : *script->getGlobals())
            {
                if (global->getNodeType() == NODE_GLOBAL_FUNCTION)
                {
                    gather(global);
                }
            }
            for (LSLASTNode* state : *script->getStates())
            {
                gather(state);
            }
            std::vector<LSLASTNode*> made;
            for (const std::string& key : order)
            {
                const std::vector<LSLExpression*>& at = places[key].at;
                LSLConstant*                       cv = at.front()->getConstantValue();
                const std::optional<ALLSLCosts::Held> held = heldFor(ctx.target, cv);
                const S32                             n    = static_cast<S32>(at.size());
                // Kept only where the global is the smaller, not where it is
                // only as large.
                if (n < 2 || !held || held->first + (n - 1) * held->each <= held->global)
                {
                    continue;
                }
                const std::string name = fresh(cv->getIType(), taken);
                LSLType*          type = TYPE(cv->getIType());
                const char*       id   = ctx.allocator->copyStr(name.c_str());
                auto*             ident = ctx.allocator->newTracked<LSLIdentifier>(type, id);
                ident->setLoc(at.front()->getLoc());
                auto* global = ctx.allocator->newTracked<LSLGlobalVariable>(ident, static_cast<LSLExpression*>(constant(cv->copy(ctx.allocator), at.front())));
                global->setLoc(at.front()->getLoc());
                auto* sym = ctx.allocator->newTracked<LSLSymbol>(id, type, SYM_VARIABLE, SYM_GLOBAL, at.front()->getLoc(), nullptr, global);
                ident->setSymbol(sym);
                script->getSymbolTable()->define(sym);
                for (LSLExpression* place : at)
                {
                    auto* read = ctx.allocator->newTracked<LSLIdentifier>(type, id);
                    read->setSymbol(sym);
                    read->setLoc(place->getLoc());
                    auto* lvalue = ctx.allocator->newTracked<LSLLValueExpression>(read, static_cast<LSLIdentifier*>(nullptr));
                    lvalue->setType(type);
                    lvalue->setLoc(place->getLoc());
                    LSLASTNode::replaceNode(place, lvalue);
                }
                report.note(at.front()->getLoc(), "OptimizerKeptConstant", "kept [1] in the global [2], which its [3] places read",
                            { render(global->getChild(1)), name, std::to_string(n) });
                made.push_back(global);
                ++changes;
            }
            if (!made.empty())
            {
                // Ahead of the rest, in the order they were made.
                std::vector<LSLASTNode*> globals = made;
                for (LSLASTNode* g = script->getGlobals()->getChild(0); g; g = g->getNext())
                {
                    globals.push_back(g);
                }
                setStatements(script->getGlobals(), globals, *ctx.context);
            }
            return changes;
        }

    private:
        // A short name of the script's own, by the type's first letter.
        std::string fresh(LSLIType type, boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>& taken) const
        {
            const char* base = type == LST_INTEGER ? "n" : type == LST_FLOATINGPOINT ? "f" : type == LST_STRING ? "s" : type == LST_KEY ? "k" : type == LST_VECTOR ? "v" : "r";
            const auto  usable = [&](const std::string& name) {
                return !taken.contains(name) && ALScriptLexicon::lslWord(name) == ALScriptLexicon::LSL_NAME &&
                       !(ctx.context->builtins && ctx.context->builtins->lookup(name.c_str(), SYM_ANY));
            };
            std::string name = base;
            for (int k = 2; !usable(name); ++k)
            {
                name = base + std::to_string(k);
            }
            taken.insert(name);
            return name;
        }
    };
}

    int poolConstants(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        ConstantGlobals pooled(ctx, report, options);
        return pooled.run(script);
    }
}
