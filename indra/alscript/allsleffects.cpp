/**
 * @file allsleffects.cpp
 * @brief What LSL code may change, and the order it runs in.
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

#include "allsleffects.h"

#include "allsltraits.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>

using namespace Tailslide;

namespace
{
    // The variable an assignment or an increment writes, or none.
    LSLSymbol* assigned(LSLASTNode* node)
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

    // The function a call calls, or none.
    LSLSymbol* called(LSLASTNode* node)
    {
        if (node->getNodeType() != NODE_EXPRESSION || node->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            return nullptr;
        }
        LSLIdentifier* id = static_cast<LSLFunctionExpression*>(node)->getIdentifier();
        return id ? id->getSymbol() : nullptr;
    }

    bool isState(LSLASTNode* node)
    {
        return node->getNodeType() == NODE_STATEMENT && node->getNodeSubType() == NODE_STATE_STATEMENT;
    }

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
}

void ALLSLEffects::Writes::add(const Writes& more)
{
    variables.insert(more.variables.begin(), more.variables.end());
    impure = impure || more.impure;
}

ALLSLEffects::ALLSLEffects(LSLScript* script)
{
    mUnknown.impure = true;
    if (!script || !script->getGlobals())
    {
        return;
    }
    // Each function's own writes, and whom it calls; then each takes in
    // its callees' until nothing more comes in.
    boost::unordered_flat_map<LSLSymbol*, std::vector<LSLSymbol*>> callees;
    for (LSLASTNode* global : *script->getGlobals())
    {
        if (global->getNodeType() != NODE_GLOBAL_FUNCTION || !global->getSymbol())
        {
            continue;
        }
        LSLSymbol* function = global->getSymbol();
        Writes&    own      = mFunctions[function];
        each(global, [&](LSLASTNode* node) {
            if (LSLSymbol* written = assigned(node); written && written->getSubType() == SYM_GLOBAL)
            {
                own.variables.insert(written);
            }
            else if (LSLSymbol* callee = called(node))
            {
                if (callee->getSubType() == SYM_BUILTIN)
                {
                    own.impure = own.impure || !ALLSLTraits::pure(callee->getName());
                }
                else
                {
                    callees[function].push_back(callee);
                }
            }
            else if (isState(node))
            {
                own.impure = true;
            }
        });
    }
    for (bool grew = true; grew;)
    {
        grew = false;
        for (auto& [function, calls] : callees)
        {
            Writes& own = mFunctions[function];
            for (LSLSymbol* callee : calls)
            {
                const Writes& theirs = ofFunction(callee);
                const size_t  had    = own.variables.size();
                const bool    was    = own.impure;
                if (&theirs != &own)
                {
                    own.add(theirs);
                }
                grew = grew || own.variables.size() != had || own.impure != was;
            }
        }
    }
}

const ALLSLEffects::Writes& ALLSLEffects::ofFunction(LSLSymbol* function) const
{
    const auto found = mFunctions.find(function);
    return found == mFunctions.end() ? mUnknown : found->second;
}

ALLSLEffects::Writes ALLSLEffects::of(LSLASTNode* node) const
{
    Writes out;
    if (!node)
    {
        return out;
    }
    each(node, [&](LSLASTNode* n) {
        if (LSLSymbol* written = assigned(n))
        {
            out.variables.insert(written);
        }
        else if (LSLSymbol* callee = called(n))
        {
            if (callee->getSubType() == SYM_BUILTIN)
            {
                out.impure = out.impure || !ALLSLTraits::pure(callee->getName());
            }
            else
            {
                out.add(ofFunction(callee));
            }
        }
        else if (isState(n))
        {
            out.impure = true;
        }
    });
    return out;
}

// static
std::vector<LSLASTNode*> ALLSLEffects::before(LSLASTNode* root, LSLASTNode* node)
{
    std::vector<LSLASTNode*> out;
    const auto siblings = [&out](LSLASTNode* parent, LSLASTNode* child, bool all) {
        for (LSLASTNode* sibling = parent->getChild(0); sibling; sibling = sibling->getNext())
        {
            if (sibling == child)
            {
                if (!all)
                {
                    return;
                }
                continue;
            }
            out.push_back(sibling);
        }
    };
    for (LSLASTNode* child = node; child && child != root; child = child->getParent())
    {
        LSLASTNode* parent = child->getParent();
        if (!parent)
        {
            break;
        }
        if (parent->getNodeType() == NODE_AST_NODE_LIST)
        {
            // A call's arguments, and a `for`'s parts, left to right.
            siblings(parent, child, false);
            continue;
        }
        if (parent->getNodeType() != NODE_EXPRESSION)
        {
            continue;
        }
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                // The right runs first: a left operand has it before it.
                if (child == parent->getChild(0) && parent->getChild(1))
                {
                    out.push_back(parent->getChild(1));
                }
                break;
            case NODE_LIST_EXPRESSION:
            case NODE_VECTOR_EXPRESSION:
            case NODE_QUATERNION_EXPRESSION:
                siblings(parent, child, true);
                break;
            default:
                break;
        }
    }
    return out;
}

bool ALLSLEffects::mayRunFirst(LSLASTNode* root, LSLASTNode* node) const
{
    const Writes moved = of(node);
    for (LSLASTNode* earlier : before(root, node))
    {
        bool fine = true;
        each(earlier, [&](LSLASTNode* n) {
            if (!fine)
            {
                return;
            }
            if (assigned(n) || isState(n))
            {
                fine = false;
            }
            else if (LSLSymbol* callee = called(n))
            {
                fine = callee->getSubType() == SYM_BUILTIN && ALLSLTraits::pure(callee->getName());
            }
            else if (n->getNodeType() == NODE_EXPRESSION && n->getNodeSubType() == NODE_LVALUE_EXPRESSION)
            {
                LSLIdentifier* id = static_cast<LSLLValueExpression*>(n)->getIdentifier();
                fine              = !(id && id->getSymbol() && moved.writes(id->getSymbol()));
            }
        });
        if (!fine)
        {
            return false;
        }
    }
    return true;
}
