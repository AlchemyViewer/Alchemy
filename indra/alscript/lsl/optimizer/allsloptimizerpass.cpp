/**
 * @file allsloptimizerpass.cpp
 * @brief What the LSL optimizer's passes share.
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

#include "allsloptimizerpass.h"

#include <tailslide/passes/pretty_print.hh>

namespace ALLSLPasses
{
    std::string render(LSLASTNode* node)
    {
        PrettyPrintOpts    opts{};
        PrettyPrintVisitor printer(opts);
        node->visit(&printer);
        std::string text = printer.mStream.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        {
            text.pop_back();
        }
        // A statement's rendering starts with its indentation.
        const size_t start = text.find_first_not_of(" \t");
        return start == std::string::npos ? text : text.substr(start);
    }
    bool wantsParens(LSLASTNode* parent, LSLASTNode* old)
    {
        if (!parent || parent->getNodeType() != NODE_EXPRESSION)
        {
            return false;
        }
        switch (parent->getNodeSubType())
        {
            case NODE_BINARY_EXPRESSION:
                if (operation_mutates(static_cast<LSLExpression*>(parent)->getOperation()) && parent->getChild(1) == old)
                {
                    return false;
                }
                return true;
            // A cast's own printing puts its operand in parentheses where
            // it needs them, and around parentheses too.
            case NODE_TYPECAST_EXPRESSION:
                return false;
            case NODE_UNARY_EXPRESSION:
            case NODE_PRINT_EXPRESSION:
            case NODE_BOOL_CONVERSION_EXPRESSION:
                return true;
            default:
                return false;
        }
    }

    void putInPlace(LSLASTNode* old, LSLExpression* replacement, ScriptAllocator* allocator)
    {
        LSLASTNode* parent = old->getParent();
        replacement->setLoc(old->getLoc());
        if (wantsParens(parent, old) && replacement->getNodeSubType() != NODE_CONSTANT_EXPRESSION &&
            replacement->getNodeSubType() != NODE_LVALUE_EXPRESSION && replacement->getNodeSubType() != NODE_PARENTHESIS_EXPRESSION &&
            replacement->getNodeSubType() != NODE_FUNCTION_EXPRESSION)
        {
            auto* parens = allocator->newTracked<LSLParenthesisExpression>(replacement);
            parens->setType(replacement->getType());
            parens->setLoc(old->getLoc());
            LSLASTNode::replaceNode(old, parens);
            return;
        }
        LSLASTNode::replaceNode(old, replacement);
    }
    ALScriptWeight::Target weightTarget(ALLSLOptimizer::Target target)
    {
        return target == ALLSLOptimizer::Target::LSO    ? ALScriptWeight::Target::LSO
               : target == ALLSLOptimizer::Target::Luau ? ALScriptWeight::Target::LSLLuau
                                                        : ALScriptWeight::Target::Mono;
    }

    ALScriptWeight weigh(ALLSLOptimizer::Target target, std::string_view text)
    {
        switch (weightTarget(target))
        {
            case ALScriptWeight::Target::LSO:
                return ALScriptWeigh::lso(text);
            case ALScriptWeight::Target::LSLLuau:
                return ALScriptWeigh::lslLuau(text);
            default:
                return ALScriptWeigh::mono(text);
        }
    }
}
