/**
 * @file allslnames.cpp
 * @brief The LSL optimizer's shortened names.
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

#include "allslnames.h"

#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <set>

namespace ALLSLPasses
{
namespace
{
    class TableCollector : public ASTVisitor
    {
    public:
        bool visit(LSLASTNode* node) override
        {
            if (LSLSymbolTable* table = node->getSymbolTable())
            {
                if (mSeen.insert(table).second)
                {
                    tables.push_back(table);
                }
            }
            return true;
        }
        std::vector<LSLSymbolTable*> tables;

    private:
        std::set<LSLSymbolTable*> mSeen;
    };

    // Each label's function or handler. LSL takes a jump to whichever
    // label of its name the function declares last, seen from the jump or
    // not, so labels of one name in one function are one name still.
    class LabelScopes : public ASTVisitor
    {
    public:
        bool visit(LSLLabel* label) override
        {
            LSLASTNode* scope = label;
            while (scope && scope->getNodeType() != NODE_GLOBAL_FUNCTION && scope->getNodeType() != NODE_EVENT_HANDLER)
            {
                scope = scope->getParent();
            }
            if (LSLSymbol* sym = label->getSymbol())
            {
                of[sym] = scope;
            }
            return false;
        }
        std::map<LSLSymbol*, LSLASTNode*> of;
    };

    // What each character of a name costs the target's code, where the
    // compiled script keeps it. Mono keeps a global's as its field's, and a
    // function's as its method's, and each once more where the code
    // reaches it; and a state's in the name of each of its handlers'
    // methods, and in the string a change to it loads, whose characters
    // are two bytes. Luau keeps a global's and a function's once. Nothing
    // else keeps a name: LSO none, and a local's, a parameter's or a
    // label's no target.
    S32 nameCost(LSLSymbol* sym, ALLSLOptimizer::Target target, const std::map<LSLSymbol*, S32>& handlers)
    {
        const bool global = sym->getSymbolType() == SYM_VARIABLE && sym->getSubType() == SYM_GLOBAL;
        const bool used   = sym->getReferences() > 1;
        switch (target)
        {
            case ALLSLOptimizer::Target::Mono:
                if (global || sym->getSymbolType() == SYM_FUNCTION)
                {
                    return used ? 2 : 1;
                }
                if (sym->getSymbolType() == SYM_STATE)
                {
                    const auto found = handlers.find(sym);
                    return (found == handlers.end() ? 0 : found->second) + (used ? 2 : 0);
                }
                return 0;
            case ALLSLOptimizer::Target::Luau:
                return global || sym->getSymbolType() == SYM_FUNCTION ? 1 : 0;
            default:
                return 0;
        }
    }

    // Every name the script owns, shortest first for those whose characters
    // cost the compiled script the most, then for the most used.
    void shrink(LSLScript* script, ScriptContext& context, ScriptAllocator& allocator, Report& report, ALLSLOptimizer::Result& result,
                ALLSLOptimizer::Target target)
    {
        TableCollector collector;
        script->visit(&collector);
        std::vector<LSLSymbol*> symbols;
        for (LSLSymbolTable* table : collector.tables)
        {
            if (table->getTableType() == SYMTAB_BUILTINS)
            {
                continue;
            }
            for (auto& entry : table->getMap())
            {
                LSLSymbol* sym = entry.second;
                if (sym->getSymbolType() == SYM_EVENT || sym->getSubType() == SYM_BUILTIN)
                {
                    continue;
                }
                if (sym->getSymbolType() == SYM_STATE && !strcmp(sym->getName(), "default"))
                {
                    continue;
                }
                symbols.push_back(sym);
            }
        }
        std::map<LSLSymbol*, S32> handlers;
        for (LSLState* state : *script->getStates())
        {
            if (LSLSymbol* sym = state->getSymbol())
            {
                handlers[sym] = static_cast<S32>(state->getEventHandlers()->getNumChildren());
            }
        }
        std::map<LSLSymbol*, S32> costs;
        for (LSLSymbol* sym : symbols)
        {
            costs[sym] = nameCost(sym, target, handlers);
        }
        // Equally used, the first declared first, then by name: the tables
        // are hash maps, whose order is the standard library's own.
        std::stable_sort(symbols.begin(), symbols.end(), [&costs](LSLSymbol* a, LSLSymbol* b) {
            if (costs[a] != costs[b])
            {
                return costs[a] > costs[b];
            }
            if (a->getReferences() != b->getReferences())
            {
                return a->getReferences() > b->getReferences();
            }
            const Tailslide::YYLTYPE& at = *a->getLoc();
            const Tailslide::YYLTYPE& bt = *b->getLoc();
            if (at < bt || bt < at)
            {
                return at < bt;
            }
            return strcmp(a->getName(), b->getName()) < 0;
        });
        const std::string first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
        const std::string rest  = first + "0123456789";
        // Every name given so far, which none is given again but a label's.
        boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> given;
        const auto usable = [&](const std::string& name) {
            // No word of the language's, the preprocessor's extensions
            // included, whether or not they are on.
            if (ALScriptLexicon::lslWord(name) != ALScriptLexicon::LSL_NAME)
            {
                return false;
            }
            return !given.contains(name) && !(context.builtins && context.builtins->lookup(name.c_str(), SYM_ANY));
        };
        std::vector<int> digits;
        // The next name, counting up in a mixed radix: the first digit over
        // `first`, the rest over `rest`. Looked at, or taken.
        const auto next = [&](bool take) {
            std::vector<int>  looked = digits;
            std::vector<int>& at     = take ? digits : looked;
            std::string       name;
            while (true)
            {
                if (at.empty())
                {
                    at.push_back(0);
                }
                else
                {
                    size_t i = at.size();
                    while (i > 0)
                    {
                        --i;
                        const int radix = i == 0 ? static_cast<int>(first.size()) : static_cast<int>(rest.size());
                        if (++at[i] < radix)
                        {
                            break;
                        }
                        at[i] = 0;
                        if (i == 0)
                        {
                            at.insert(at.begin(), 0);
                            break;
                        }
                    }
                }
                name.clear();
                for (size_t i = 0; i < at.size(); ++i)
                {
                    name += (i == 0 ? first : rest)[at[i]];
                }
                if (usable(name))
                {
                    return name;
                }
            }
        };
        // Mono loads a state's name as a string where the script changes to
        // it, and holds each string once: such a state is named for a string
        // the script holds already, where its characters in each handler's
        // name cost less than the next name's and a string of that.
        Strings strings;
        std::vector<std::string> held;
        if (target == ALLSLOptimizer::Target::Mono)
        {
            script->visit(&strings);
            for (const auto& [text, times] : strings.found)
            {
                const bool shaped = !text.empty() && first.find(text[0]) != std::string::npos &&
                                    text.find_first_not_of(rest) == std::string::npos;
                if (shaped)
                {
                    held.push_back(text);
                }
            }
            std::sort(held.begin(), held.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
        }
        const auto heldName = [&](LSLSymbol* sym) -> std::optional<std::string> {
            const auto count = handlers.find(sym);
            if (held.empty() || sym->getSymbolType() != SYM_STATE || sym->getReferences() < 2 || count == handlers.end())
            {
                return std::nullopt;
            }
            const std::string ours = next(false);
            const S32         each = count->second;
            const S32         cost = each * S32(ours.size()) + (strings.found.contains(ours) ? 0 : 2 * S32(ours.size()) + 2);
            for (const std::string& text : held)
            {
                if (each * S32(text.size()) >= cost)
                {
                    break;
                }
                if (usable(text))
                {
                    return text;
                }
            }
            return std::nullopt;
        };
        LabelScopes labels;
        script->visit(&labels);
        std::map<std::pair<LSLASTNode*, std::string>, std::string> labelNames;
        for (LSLSymbol* sym : symbols)
        {
            std::string name;
            const auto  scope = labels.of.find(sym);
            if (scope != labels.of.end())
            {
                auto [named, fresh] = labelNames.try_emplace({ scope->second, sym->getName() });
                if (fresh)
                {
                    named->second = next(true);
                }
                name = named->second;
            }
            else if (const std::optional<std::string> text = heldName(sym))
            {
                name = *text;
            }
            else
            {
                name = next(true);
            }
            given.insert(name);
            sym->setMangledName(allocator.copyStr(name.c_str()));
            result.renamed.emplace(sym->getName(), name);
            report.note(sym->getLoc(), "OptimizerRenamed", "renamed the [1] [2] to [3]", { LSLSymbol::getTypeName(sym->getSymbolType()), sym->getName(), name });
        }
    }
}

    void shrinkNames(LSLScript* script, ScriptContext& context, ScriptAllocator& allocator, Report& report, ALLSLOptimizer::Result& result,
                     ALLSLOptimizer::Target target)
    {
        shrink(script, context, allocator, report, result, target);
    }
}
