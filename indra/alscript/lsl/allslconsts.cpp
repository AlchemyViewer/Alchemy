/**
 * @file allslconsts.cpp
 * @brief The const keyword: what a script declares constant, checked and folded.
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

#include "allslconsts.h"

#include "allsleffects.h"
#include "allslservice.h"
#include "allsltraits.h"
#include "allslvalues.h"
#include "alscriptengine.h"

#include <tailslide/tailslide.hh>
#include <tailslide/passes/globalexpr_validator.hh>
#include <tailslide/passes/values.hh>

#include <boost/unordered/unordered_flat_map.hpp>

using namespace Tailslide;

namespace
{
    S32 zeroBased(int one) { return std::max(0, one - 1); }

    // Every node of a subtree, the root first.
    template <class F> void each(LSLASTNode* root, const F& f)
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

    // The variable an assignment or an increment writes, or none.
    LSLSymbol* written(LSLASTNode* node)
    {
        if (node->getNodeType() != NODE_EXPRESSION || !operation_mutates(static_cast<LSLExpression*>(node)->getOperation()))
        {
            return nullptr;
        }
        LSLASTNode* target = node->getChild(0);
        if (!target || target->getNodeSubType() != NODE_LVALUE_EXPRESSION)
        {
            return nullptr;
        }
        LSLIdentifier* id = static_cast<LSLLValueExpression*>(target)->getIdentifier();
        return id ? id->getSymbol() : nullptr;
    }

    ALScriptProblem error(LSLASTNode* at, const char* key, std::string_view text, std::vector<std::string> args)
    {
        ALScriptProblem p;
        p.severity = ALScriptProblem::Severity::Error;
        p.source   = ALScriptProblem::Source::Preprocessor;
        p.key      = key;
        if (const Tailslide::YYLTYPE* loc = at->getLoc(); loc && loc->first_line > 0)
        {
            p.line      = zeroBased(loc->first_line);
            p.column    = zeroBased(loc->first_column);
            p.endLine   = zeroBased(loc->last_line);
            p.endColumn = zeroBased(loc->last_column);
        }
        p.message = ALScriptProblem::fill(text, args);
        p.args    = std::move(args);
        return p;
    }

    S64 placeOf(S32 line, S32 column) { return (S64(line) << 32) | U32(column); }

    // What a const function does that it may not: sets a global, reads one
    // the script changes, calls what does more than work out a value, or
    // changes state. Each said where it is.
    void checkFunction(LSLGlobalFunction* function, const ALLSLEffects& effects, ALScriptProblems& out)
    {
        const std::string name = function->getSymbol() ? function->getSymbol()->getName() : std::string();
        each(function->getStatements(), [&](LSLASTNode* node) {
            if (LSLSymbol* sym = written(node))
            {
                if (sym->getSubType() == SYM_GLOBAL)
                {
                    out.push_back(error(node, "ConstFunctionSets", "the const function [1] sets [2]: what it gives back must come of its arguments alone",
                                        { name, sym->getName() }));
                }
                return;
            }
            if (node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT)
            {
                out.push_back(error(node, "ConstFunctionChangesState", "the const function [1] changes state", { name }));
                return;
            }
            if (node->getNodeType() != NODE_EXPRESSION)
            {
                return;
            }
            if (node->getNodeSubType() == NODE_FUNCTION_EXPRESSION)
            {
                LSLIdentifier* id     = static_cast<LSLFunctionExpression*>(node)->getIdentifier();
                LSLSymbol*     callee = id ? id->getSymbol() : nullptr;
                if (!callee)
                {
                    return;
                }
                const bool pure = callee->getSubType() == SYM_BUILTIN ? ALLSLTraits::pure(callee->getName())
                                                                      : !effects.ofFunction(callee).impure && effects.ofFunction(callee).variables.empty();
                if (!pure)
                {
                    out.push_back(error(node, "ConstFunctionCalls", "the const function [1] calls [2], which does more than work out a value",
                                        { name, callee->getName() }));
                }
                return;
            }
            if (node->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                // A read: the written side of an assignment is said above.
                LSLASTNode* parent = node->getParent();
                if (parent && written(parent) && parent->getChild(0) == node)
                {
                    return;
                }
                LSLSymbol* sym = static_cast<LSLLValueExpression*>(node)->getSymbol();
                if (sym && sym->getSubType() == SYM_GLOBAL && sym->getAssignments() > 0)
                {
                    out.push_back(error(node, "ConstFunctionReads", "the const function [1] reads [2], which the script changes", { name, sym->getName() }));
                }
            }
        });
    }
}

// static
ALLSLConsts::Result ALLSLConsts::run(std::string_view text, const std::vector<Declared>& declared, ALLSLOptimizer::Target target)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Result result;
    if (declared.empty() || !ALLSLService::builtinsLoaded())
    {
        return result;
    }
    AL_SCRIPT_ENGINE_HELD;
    ScopedScriptParser parser(nullptr);
    const std::string  copy(text);
    LSLScript*         script = parser.parseLSLBytes(copy.data(), static_cast<int>(copy.size()));
    if (!script || parser.logger.getErrors())
    {
        return result;
    }
    script->collectSymbols();
    script->determineTypes();
    if (parser.logger.getErrors())
    {
        return result;
    }
    // What is assigned where, which a const function's reads are told by.
    script->recalculateReferenceData();

    // Each declared name found by where its declaration names it: the
    // identifier a global, a local, a parameter or a function begins with.
    boost::unordered_flat_map<S64, const Declared*> places;
    for (const Declared& d : declared)
    {
        places[placeOf(d.line, d.column)] = &d;
    }
    boost::unordered_flat_map<LSLSymbol*, LSLASTNode*> variables;
    std::vector<LSLGlobalFunction*>                    functions;
    each(script, [&](LSLASTNode* node) {
        const Tailslide::YYLTYPE* loc = node->getLoc();
        if (node->getNodeType() != NODE_IDENTIFIER || !loc || loc->first_line <= 0)
        {
            return;
        }
        const auto found = places.find(placeOf(zeroBased(loc->first_line), zeroBased(loc->first_column)));
        LSLASTNode* declaration = node->getParent();
        if (found == places.end() || !declaration || static_cast<LSLIdentifier*>(node)->getName() != found->second->name)
        {
            return;
        }
        LSLSymbol* sym = static_cast<LSLIdentifier*>(node)->getSymbol();
        switch (declaration->getNodeType())
        {
            case NODE_GLOBAL_FUNCTION:
                if (found->second->function)
                {
                    functions.push_back(static_cast<LSLGlobalFunction*>(declaration));
                }
                break;
            case NODE_GLOBAL_VARIABLE:
            case NODE_FUNCTION_DEC:
            case NODE_EVENT_DEC:
                if (sym && !found->second->function)
                {
                    variables[sym] = declaration;
                }
                break;
            case NODE_STATEMENT:
                if (sym && !found->second->function && declaration->getNodeSubType() == NODE_DECLARATION)
                {
                    variables[sym] = declaration;
                }
                break;
            default:
                break;
        }
    });

    // A global or a local given its value where it is declared, and set
    // nowhere else.
    for (const auto& [sym, declaration] : variables)
    {
        LSLASTNode* init = declaration->getNodeType() == NODE_GLOBAL_VARIABLE ? static_cast<LSLGlobalVariable*>(declaration)->getInitializer()
                           : declaration->getNodeType() == NODE_STATEMENT     ? static_cast<LSLDeclaration*>(declaration)->getInitializer()
                                                                              : declaration;
        if (!init || init->getNodeType() == NODE_NULL)
        {
            result.problems.push_back(error(declaration->getChild(0), "ConstWithoutValue", "the const [1] needs a value where it is declared", { sym->getName() }));
        }
    }
    each(script, [&](LSLASTNode* node) {
        if (LSLSymbol* sym = written(node); sym && variables.contains(sym))
        {
            result.problems.push_back(error(node, "ConstAssigned", "[1] is const: its value is set where it is declared, and nowhere else", { sym->getName() }));
        }
    });

    // A function that works out what it gives back from its arguments alone.
    if (!functions.empty())
    {
        const ALLSLEffects effects(script);
        for (LSLGlobalFunction* function : functions)
        {
            checkFunction(function, effects, result.problems);
        }
    }
    // A global's value worked out where LSL would not take it as written.
    std::vector<LSLGlobalVariable*> globals;
    for (const auto& [sym, declaration] : variables)
    {
        if (declaration->getNodeType() == NODE_GLOBAL_VARIABLE && static_cast<LSLGlobalVariable*>(declaration)->getInitializer())
        {
            globals.push_back(static_cast<LSLGlobalVariable*>(declaration));
        }
    }
    if (!globals.empty())
    {
        // Which values LSL takes as written -- a literal, a name -- told
        // before anything is folded, so that those stay as they are.
        ALLSLArithmetic            arithmetic(&parser.allocator, true, target);
        ConstantDeterminingVisitor values(&arithmetic, &parser.allocator);
        script->visit(&values);
        std::vector<LSLGlobalVariable*> folding;
        for (LSLGlobalVariable* global : globals)
        {
            SimpleAssignableValidatingVisitor simple(target != ALLSLOptimizer::Target::LSO);
            const int                         errors = parser.logger.getErrors();
            global->visit(&simple);
            LSLASTNode* init = global->getInitializer();
            const bool  name = init->getNodeType() == NODE_EXPRESSION && init->getNodeSubType() == NODE_LVALUE_EXPRESSION;
            // Tailslide says nothing of a value that is no constant for
            // what it reads -- a global the script changes -- which, as
            // anything but that global's own name, is none a const takes.
            if (parser.logger.getErrors() != errors || (!init->getConstantValue() && !name))
            {
                folding.push_back(global);
            }
        }
        if (!folding.empty())
        {
            ALLSLOptimizer::foldGlobals(script, &parser.allocator, &parser.context, target);
        }
        for (LSLGlobalVariable* global : folding)
        {
            LSLASTNode*  init  = global->getInitializer();
            LSLConstant* value = init ? init->getConstantValue() : nullptr;
            // An integer a float is given reads as the float it becomes.
            if (value && value->getIType() == LST_INTEGER && global->getSymbol() && global->getSymbol()->getIType() == LST_FLOATINGPOINT)
            {
                value = parser.allocator.newTracked<LSLFloatConstant>(static_cast<double>(static_cast<LSLIntegerConstant*>(value)->getValue()));
            }
            const std::optional<std::string> written = ALLSLValues::literal(value, target == ALLSLOptimizer::Target::Luau);
            const Tailslide::YYLTYPE*        loc     = init ? init->getLoc() : nullptr;
            const std::string                name    = global->getSymbol() ? global->getSymbol()->getName() : std::string();
            if (!written || !loc || loc->first_line <= 0)
            {
                result.problems.push_back(error(init ? init : global, "ConstNotKnown", "the value of the const [1] cannot be worked out before the script runs", { name }));
                continue;
            }
            result.values.push_back({ zeroBased(loc->first_line), zeroBased(loc->first_column), zeroBased(loc->last_line), zeroBased(loc->last_column), *written });
        }
        std::sort(result.values.begin(), result.values.end(), [](const Value& a, const Value& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
    }
    std::stable_sort(result.problems.begin(), result.problems.end(), [](const ALScriptProblem& a, const ALScriptProblem& b) {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    return result;
}
