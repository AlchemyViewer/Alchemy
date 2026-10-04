/**
 * @file allslfolder.cpp
 * @brief The LSL optimizer's folder.
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

#include "allslfolder.h"

#include "allslcosts.h"
#include "allsllibraryfold.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <cstring>
#include <optional>

namespace ALLSLPasses
{
namespace
{
    // Constant expressions and never-assigned variables become their
    // values; a call to a pure library function with its arguments in
    // hand becomes its answer.
    class Folder : public ASTVisitor, public Pass
    {
    public:
        using Pass::Pass;

        bool visit(LSLExpression* expr) override
        {
            switch (expr->getNodeSubType())
            {
                case NODE_CONSTANT_EXPRESSION:
                    return false;
                case NODE_FUNCTION_EXPRESSION:
                    return !call(static_cast<LSLFunctionExpression*>(expr));
                default:
                    break;
            }
            LSLConstant* cv = expr->getConstantValue();
            if (cv && inlineable(cv))
            {
                fold(expr, cv, "OptimizerFolded", "folded");
                return false;
            }
            return true;
        }

        bool visit(LSLLValueExpression* lvalue) override
        {
            LSLSymbol* sym = lvalue->getSymbol();
            if (!sym || sym->getSubType() == SYM_BUILTIN || sym->getIType() == LST_LIST)
            {
                // A builtin constant is a token the compiler already has.
                return false;
            }
            if (sym->getIType() == LST_KEY && !keyMayInline(lvalue))
            {
                return false;
            }
            LSLConstant* cv = lvalue->getConstantValue();
            if (cv && inlineable(cv) && writeOut(sym, cv) && (sym->getAssignments() == 0 || sym->getSubType() == SYM_GLOBAL || allKnown(lvalue, sym)))
            {
                fold(lvalue, cv, "OptimizerInlinedConstant", "inlined");
            }
            return false;
        }

    private:
        // Of a variable set more than once, whose reads have values where
        // the code that runs to them set it to a constant (FlowValues):
        // whether every read has one, so that with all of them written out
        // the variable goes. Where one has none the variable stays, and a
        // value written at a read alone is a literal in place of a read --
        // larger, for most -- unless what it is read in folds with it,
        // which is that expression's fold, not this.
        bool allKnown(LSLLValueExpression* lvalue, LSLSymbol* sym)
        {
            if (!mCounted)
            {
                mCounted = true;
                std::vector<LSLASTNode*> stack{ lvalue->getRoot() };
                while (!stack.empty())
                {
                    LSLASTNode* n = stack.back();
                    stack.pop_back();
                    if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION && n->getSymbol() &&
                        n->getSymbol()->getAssignments() > 0)
                    {
                        auto*       read   = static_cast<LSLLValueExpression*>(n);
                        LSLASTNode* parent = n->getParent();
                        const bool  set    = parent && parent->getNodeType() == NODE_EXPRESSION &&
                                         static_cast<LSLExpression*>(parent)->getOperation() == OP_ASSIGN && parent->getChild(0) == n;
                        if (!set)
                        {
                            // A compound assignment or an increment reads it
                            // too, with nothing to write in its place.
                            const bool  mutated = parent && parent->getNodeType() == NODE_EXPRESSION &&
                                                 operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(0) == n;
                            LSLConstant* known  = mutated ? nullptr : read->getConstantValue();
                            auto&        count  = mReads[n->getSymbol()];
                            ++count.first;
                            if (known && inlineable(known) && (n->getSymbol()->getIType() != LST_KEY || keyMayInline(read)))
                            {
                                ++count.second;
                            }
                        }
                    }
                    for (LSLASTNode* child = n->getChild(0); child; child = child->getNext())
                    {
                        stack.push_back(child);
                    }
                }
            }
            const auto found = mReads.find(sym);
            return found != mReads.end() && found->second.first == found->second.second;
        }

        // Each such variable's reads, and those of them with a value.
        boost::unordered_flat_map<LSLSymbol*, std::pair<int, int>> mReads;
        bool                                                      mCounted = false;

        // A global's value goes where it is read only where writing it at
        // every place it is read costs less than the global does: a vector
        // read ten times is ten vectors. What goes where is the target's
        // (ALLSLCosts). A value a larger expression folds into is not a
        // read of it, and folds whatever this says.
        bool writeOut(LSLSymbol* sym, LSLConstant* cv) const
        {
            if (sym->getSubType() != SYM_GLOBAL)
            {
                return true;
            }
            const ALLSLCosts& costs = ALLSLCosts::of(ctx.target);
            const auto        whole = [](std::initializer_list<double> parts) {
                return std::all_of(parts.begin(), parts.end(), [](double v) { return integral(v); });
            };
            ALLSLCosts::Held held;
            switch (cv->getIType())
            {
                case LST_INTEGER:
                    held = costs.integer;
                    break;
                case LST_FLOATINGPOINT:
                    held = whole({ static_cast<LSLFloatConstant*>(cv)->getValue() }) ? costs.wholeFloating : costs.floating;
                    break;
                case LST_VECTOR:
                {
                    const Vector3* v = static_cast<LSLVectorConstant*>(cv)->getValue();
                    held             = whole({ v->x, v->y, v->z }) ? costs.wholeVector : costs.vector;
                    break;
                }
                case LST_QUATERNION:
                {
                    const Quaternion* q = static_cast<LSLQuaternionConstant*>(cv)->getValue();
                    held                = whole({ q->x, q->y, q->z, q->s }) ? costs.wholeRotation : costs.rotation;
                    break;
                }
                case LST_STRING:
                    held = costs.stringOf(static_cast<S32>(strlen(static_cast<LSLStringConstant*>(cv)->getValue())));
                    break;
                default:
                    return true;
            }
            return held.writeOut(sym->getReferences() - 1 - sym->getAssignments());
        }

        // Tailslide's rules: a key's key-ness must not be lost to a list,
        // a print, a condition or a boolean.
        static bool keyMayInline(LSLLValueExpression* lvalue)
        {
            LSLASTNode* ancestor = lvalue->getParent();
            LSLASTNode* top      = lvalue;
            while (ancestor && ancestor->getNodeType() == NODE_EXPRESSION)
            {
                switch (ancestor->getNodeSubType())
                {
                    case NODE_LIST_EXPRESSION:
                    case NODE_PRINT_EXPRESSION:
                    case NODE_BOOL_CONVERSION_EXPRESSION:
                        return false;
                    case NODE_TYPECAST_EXPRESSION:
                        if (ancestor->getIType() == LST_LIST)
                        {
                            return false;
                        }
                        break;
                    default:
                        break;
                }
                top      = ancestor;
                ancestor = ancestor->getParent();
            }
            if (top->getIType() == LST_KEY)
            {
                if (ancestor && ancestor->getNodeType() == NODE_AST_NODE_LIST)
                {
                    ancestor = ancestor->getParent();
                }
                if (ancestor && ancestor->getNodeType() == NODE_STATEMENT)
                {
                    switch (ancestor->getNodeSubType())
                    {
                        case NODE_WHILE_STATEMENT:
                        case NODE_IF_STATEMENT:
                        case NODE_DO_STATEMENT:
                        case NODE_FOR_STATEMENT:
                            return false;
                        default:
                            break;
                    }
                }
            }
            return true;
        }

        bool call(LSLFunctionExpression* expr)
        {
            LSLSymbol* sym = expr->getSymbol();
            if (!sym || sym->getSubType() != SYM_BUILTIN || !isPure(sym->getName()))
            {
                return false;
            }
            Args args;
            for (LSLASTNode* arg : *expr->getArguments())
            {
                LSLConstant* cv = arg->getConstantValue();
                if (!cv && arg->getNodeSubType() == NODE_LVALUE_EXPRESSION)
                {
                    // A list is never written into the script in place of
                    // its name, but a call may still be answered from it.
                    auto*      lvalue = static_cast<LSLLValueExpression*>(arg);
                    LSLSymbol* sym    = lvalue->getSymbol();
                    if (sym && !lvalue->getMember() && sym->getAssignments() == 0 && sym->getSubType() != SYM_BUILTIN)
                    {
                        cv = sym->getConstantValue();
                    }
                }
                if (!cv)
                {
                    return false;
                }
                args.push_back(cv);
            }
            LSLConstant* value = evaluate(ctx, sym->getName(), args);
            if (!value)
            {
                return false;
            }
            if (const std::string* name = ctx.nameOf(value))
            {
                // The builtin's name in place of the call: a token the
                // compiler already has.
                LSLSymbol* builtin = ctx.context->builtins->lookup(name->c_str(), SYM_VARIABLE);
                if (!builtin)
                {
                    return false;
                }
                // newTracked passes the context itself.
                auto* id = ctx.allocator->newTracked<LSLIdentifier>(ctx.allocator->copyStr(name->c_str()));
                id->setSymbol(builtin);
                auto* lvalue = ctx.allocator->newTracked<LSLLValueExpression>(id, static_cast<LSLIdentifier*>(nullptr));
                lvalue->setLoc(expr->getLoc());
                id->setLoc(expr->getLoc());
                report.note(expr->getLoc(), "OptimizerEvaluated", "evaluated [1] to [2]", { render(expr), *name });
                LSLASTNode::replaceNode(expr, lvalue);
                ++changes;
                return true;
            }
            if (!inlineable(value, true))
            {
                return false;
            }
            // An answer no longer than the strings the call was given takes
            // no more room than they did; a longer one can be larger than
            // the call, on a target that writes a string at every place it
            // is used, and on one that holds each string once where the
            // answer is new to it.
            if (value->getIType() == LST_STRING && strlen(static_cast<LSLStringConstant*>(value)->getValue()) > givenChars(expr) &&
                !smallerAnswered(expr, args, static_cast<LSLStringConstant*>(value)))
            {
                return false;
            }
            fold(expr, value, "OptimizerEvaluated", "evaluated");
            return true;
        }

        // Whether a string is smaller than the call it answers, as the
        // target's compiler counts: each compiled in a script of its own,
        // beside what stays either way -- the function called with its
        // strings emptied, for the reference to it a script may keep, and
        // each string of the call's and the answer that the code holds
        // elsewhere.
        bool smallerAnswered(LSLFunctionExpression* expr, const Args& args, LSLStringConstant* value)
        {
            const bool                       wide   = ctx.target == ALLSLOptimizer::Target::Luau;
            const std::optional<std::string> answer = ALLSLValues::literal(value, wide);
            if (!answer)
            {
                return false;
            }
            const std::string name = expr->getSymbol()->getName();
            std::string       call = name + "(";
            std::string       again = call;
            Counted           own;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::optional<std::string> arg   = ALLSLValues::literal(args[i], wide);
                const std::optional<std::string> empty = emptied(args[i], wide);
                if (!arg || !empty)
                {
                    return false;
                }
                call += (i ? ", " : "") + *arg;
                again += (i ? ", " : "") + *empty;
                count(args[i], own);
            }
            call += ")";
            again += ")";
            std::string   before    = "default { state_entry() { llOwnerSay(" + again + ");";
            const Counted held      = holds(expr);
            const auto    elsewhere = [&](const std::string& text) {
                const auto in   = held.find(text);
                const auto mine = own.find(text);
                return in != held.end() && in->second > (mine == own.end() ? 0 : mine->second);
            };
            for (const auto& [text, times] : own)
            {
                if (elsewhere(text))
                {
                    const std::optional<std::string> written = ALLSLValues::literal(ctx.allocator->newTracked<LSLStringConstant>(ctx.allocator->copyStr(text.c_str())), wide);
                    if (!written)
                    {
                        return false;
                    }
                    before += " llOwnerSay(" + *written + ");";
                }
            }
            if (elsewhere(value->getValue()))
            {
                before += " llOwnerSay(" + *answer + ");";
            }
            const std::string key = before + call;
            if (const auto found = ctx.answers.find(key); found != ctx.answers.end())
            {
                return found->second;
            }
            const ALScriptWeight asCall   = weigh(ctx.target, before + " llOwnerSay(" + call + "); } }");
            const ALScriptWeight asAnswer = weigh(ctx.target, before + " llOwnerSay(" + *answer + "); } }");
            const bool           smaller  = asCall.compiled && asAnswer.compiled && asAnswer.total <= asCall.total;
            ctx.answers.emplace(key, smaller);
            return smaller;
        }

        // The strings in a value, each as many times as it is there.
        static void count(LSLConstant* cv, Counted& out)
        {
            switch (cv->getIType())
            {
                case LST_STRING:
                    ++out[static_cast<LSLStringConstant*>(cv)->getValue()];
                    break;
                case LST_KEY:
                    ++out[static_cast<LSLKeyConstant*>(cv)->getValue()];
                    break;
                case LST_LIST:
                    for (LSLASTNode* item : *cv)
                    {
                        count(static_cast<LSLConstant*>(item), out);
                    }
                    break;
                default:
                    break;
            }
        }

        // A value as a literal of its shape, its strings empty.
        static std::optional<std::string> emptied(LSLConstant* cv, bool wide)
        {
            switch (cv->getIType())
            {
                case LST_STRING:
                case LST_KEY:
                    return "\"\"";
                case LST_LIST:
                {
                    std::string out = "[";
                    for (LSLASTNode* item : *cv)
                    {
                        const std::optional<std::string> element = emptied(static_cast<LSLConstant*>(item), wide);
                        if (!element)
                        {
                            return std::nullopt;
                        }
                        out += (out.size() > 1 ? ", " : "") + *element;
                    }
                    return out + "]";
                }
                default:
                    return ALLSLValues::literal(cv, wide);
            }
        }

        // The strings the code holds where a place would share them, each
        // as many times as it is written, as the code is now: Mono's are
        // the whole script's, Luau's each function's; LSO writes one
        // wherever it is used.
        Counted holds(LSLASTNode* at) const
        {
            if (ctx.target == ALLSLOptimizer::Target::LSO)
            {
                return {};
            }
            LSLASTNode* scope = at;
            while (scope->getParent() && (ctx.target == ALLSLOptimizer::Target::Mono || (scope->getNodeType() != NODE_GLOBAL_FUNCTION &&
                                                                                        scope->getNodeType() != NODE_EVENT_HANDLER)))
            {
                scope = scope->getParent();
            }
            Strings strings;
            scope->visit(&strings);
            return std::move(strings.found);
        }

        // The characters of the strings written in a call's arguments,
        // which go with it: a variable's value stays where it is.
        static size_t givenChars(LSLFunctionExpression* expr)
        {
            Counted given;
            for (LSLASTNode* arg : *expr->getArguments())
            {
                if (arg->getNodeSubType() != NODE_LVALUE_EXPRESSION && arg->getConstantValue())
                {
                    count(arg->getConstantValue(), given);
                }
            }
            size_t chars = 0;
            for (const auto& [text, times] : given)
            {
                chars += text.size() * times;
            }
            return chars;
        }
    };
}

    int fold(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        Folder folder(ctx, report, options);
        script->visit(&folder);
        return folder.changes;
    }

    int foldGlobalValues(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script)
    {
        Folder folder(ctx, report, options);
        for (LSLASTNode* global : *script->getGlobals())
        {
            if (global->getNodeType() == NODE_GLOBAL_VARIABLE)
            {
                global->visit(&folder);
            }
        }
        return folder.changes;
    }
}
