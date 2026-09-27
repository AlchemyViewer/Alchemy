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

#include <boost/unordered/unordered_flat_map.hpp>

#include <string_view>

namespace
{
    const ALLSLTraits::Trait TRAITS[] = {
#include "allsltraits.inc"
    };

    // The rows by name, made once: asked of every call the optimizer
    // folds, which a walk of five hundred names by strcmp made near a tenth
    // of a save.
    const boost::unordered_flat_map<std::string_view, const ALLSLTraits::Trait*>& byName()
    {
        static const boost::unordered_flat_map<std::string_view, const ALLSLTraits::Trait*> rows = [] {
            boost::unordered_flat_map<std::string_view, const ALLSLTraits::Trait*> made;
            made.reserve(std::size(TRAITS));
            for (const ALLSLTraits::Trait& t : TRAITS)
            {
                made.emplace(t.name, &t);
            }
            return made;
        }();
        return rows;
    }
}

// static
const ALLSLTraits::Trait* ALLSLTraits::of(const char* name)
{
    if (!name)
    {
        return nullptr;
    }
    const auto& rows  = byName();
    const auto  found = rows.find(std::string_view(name));
    return found == rows.end() ? nullptr : found->second;
}

// static
bool ALLSLTraits::pure(const char* name)
{
    const Trait* t = of(name);
    return t && t->pure;
}

namespace
{
    // What changes nothing: no assignment, no print, no call but to a
    // library function that changes nothing -- pure, or, where `reads`,
    // one whose result is the only point of calling it.
    bool quiet(Tailslide::LSLASTNode* node, bool reads)
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
                    if (!sym || sym->getSubType() != SYM_BUILTIN)
                    {
                        return false;
                    }
                    const ALLSLTraits::Trait* t = ALLSLTraits::of(sym->getName());
                    if (!t || !(t->pure || (reads && t->mustUse)))
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
            if (!quiet(child, reads))
            {
                return false;
            }
        }
        return true;
    }
}

// static
bool ALLSLTraits::sideEffectFree(Tailslide::LSLASTNode* node)
{
    return quiet(node, false);
}

// static
bool ALLSLTraits::changesNothing(Tailslide::LSLASTNode* node)
{
    return quiet(node, true);
}
