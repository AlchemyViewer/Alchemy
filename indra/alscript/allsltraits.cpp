/**
 * @file allsltraits.cpp
 * @brief What the definitions say of each library function.
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

#include "allsltraits.h"

#include <tailslide/tailslide.hh>
#include <tailslide/operations.hh>

#include <cstring>

namespace
{
    const ALLSLTraits::Trait TRAITS[] = {
#include "allsltraits.inc"
    };
}

// static
const ALLSLTraits::Trait* ALLSLTraits::of(const char* name)
{
    for (const Trait& t : TRAITS)
    {
        if (!strcmp(t.name, name))
        {
            return &t;
        }
    }
    return nullptr;
}

// static
bool ALLSLTraits::pure(const char* name)
{
    const Trait* t = of(name);
    return t && t->pure;
}

// static
bool ALLSLTraits::sideEffectFree(Tailslide::LSLASTNode* node)
{
    using namespace Tailslide;
    if (!node)
    {
        return true;
    }
    switch (node->getNodeType())
    {
        case NODE_NULL:
        case NODE_CONSTANT:
        case NODE_IDENTIFIER:
        case NODE_TYPE:
            return true;
        case NODE_EXPRESSION:
        case NODE_AST_NODE_LIST:
            break;
        default:
            return false;
    }
    if (node->getNodeType() == NODE_EXPRESSION)
    {
        auto* expr = static_cast<LSLExpression*>(node);
        if (operation_mutates(expr->getOperation()))
        {
            return false;
        }
        switch (expr->getNodeSubType())
        {
            case NODE_PRINT_EXPRESSION:
                return false;
            case NODE_FUNCTION_EXPRESSION:
            {
                LSLSymbol* sym = expr->getSymbol();
                if (!sym || sym->getSubType() != SYM_BUILTIN || !pure(sym->getName()))
                {
                    return false;
                }
                break;
            }
            default:
                break;
        }
    }
    for (LSLASTNode* child : *node)
    {
        if (!sideEffectFree(child))
        {
            return false;
        }
    }
    return true;
}
