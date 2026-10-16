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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace
{
    // The table's SLua flags by their own names.
    using enum ALLSLTraits::Slua;
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

// static
bool ALLSLTraits::eventTextParam(std::string_view event, int index)
{
    static constexpr std::pair<std::string_view, int> PARAMS[] = {
#include "allsleventtext.inc"
    };
    return std::find(std::begin(PARAMS), std::end(PARAMS), std::pair<std::string_view, int>(event, index)) != std::end(PARAMS);
}

// static
ALLSLTraits::Item ALLSLTraits::itemArg(std::string_view function, int index)
{
    struct Row
    {
        std::string_view function;
        int              index;
        Item             item;
    };
    static constexpr Row ROWS[] = {
#include "allslitemargs.inc"
    };
    // SLua's names are LSL's with the table's dot for the prefix.
    std::string lsl;
    for (const std::string_view table : { std::string_view("ll."), std::string_view("llcompat.") })
    {
        if (function.substr(0, table.size()) == table)
        {
            lsl = "ll" + std::string(function.substr(table.size()));
            function = lsl;
            break;
        }
    }
    for (const Row& row : ROWS)
    {
        if (row.function == function && row.index == index)
        {
            return row.item;
        }
    }
    return Item::None;
}

// static
bool ALLSLTraits::isUuid(std::string_view text)
{
    if (text.size() != 36)
    {
        return false;
    }
    for (size_t i = 0; i < text.size(); ++i)
    {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !std::isxdigit(static_cast<unsigned char>(text[i])))
        {
            return false;
        }
    }
    return true;
}

// static
bool ALLSLTraits::uuidConstant(std::string_view name)
{
    static constexpr std::string_view UUIDS[] = {
#include "allsluuids.inc"
    };
    return std::find(std::begin(UUIDS), std::end(UUIDS), name) != std::end(UUIDS);
}

namespace
{
    // Whether a division or a remainder may stop the script: an integer's
    // or a float's by nought is a Math Error, so one by anything but a
    // constant other than nought may be. A vector's remainder is a cross
    // product and a rotation's quotient a product, which never fail.
    bool mayFault(Tailslide::LSLExpression* expr)
    {
        using namespace Tailslide;
        if (expr->getNodeSubType() != NODE_BINARY_EXPRESSION || (expr->getOperation() != OP_DIV && expr->getOperation() != OP_MOD))
        {
            return false;
        }
        LSLASTNode* divisor = expr->getChild(1);
        if (!divisor)
        {
            return true;
        }
        const LSLIType type = divisor->getIType();
        if (type != LST_INTEGER && type != LST_FLOATINGPOINT)
        {
            return false;
        }
        LSLConstant* cv = divisor->getConstantValue();
        if (!cv)
        {
            return true;
        }
        switch (cv->getNodeSubType())
        {
            case NODE_INTEGER_CONSTANT: return static_cast<LSLIntegerConstant*>(cv)->getValue() == 0;
            case NODE_FLOAT_CONSTANT: return static_cast<LSLFloatConstant*>(cv)->getValue() == 0.0f;
            default: return true;
        }
    }

    // What changes nothing: no assignment, no print, no call but to a
    // library function that changes nothing -- pure, or, where `reads`,
    // one whose result is the only point of calling it -- and no division
    // that may stop the script, whose error is as much an effect as a
    // print.
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
            if (operation_mutates(expr->getOperation()) || mayFault(expr))
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

// static
bool ALLSLTraits::bounds(const char* name, S32& least, S32& most)
{
    if (!name)
    {
        return false;
    }
    if (atLeastMinusOne(name))
    {
        least = -1;
        most  = INT32_MAX;
        return true;
    }
    struct Row
    {
        std::string_view name;
        S32              least;
        S32              most;
    };
    // Not llGetUnixTime, whose answer wraps below nought in 2038.
    static constexpr Row ROWS[] = {
        { "llGetAttached", 0, INT32_MAX },        { "llGetFreeMemory", 0, INT32_MAX },     { "llGetFreeURLs", 0, INT32_MAX },
        { "llGetInventoryNumber", 0, INT32_MAX }, { "llGetLinkNumber", 0, INT32_MAX },      { "llGetLinkNumberOfSides", 0, INT32_MAX },
        { "llGetListEntryType", 0, 6 },           { "llGetListLength", 0, INT32_MAX },      { "llGetMemoryLimit", 0, INT32_MAX },
        { "llGetNumberOfPrims", 0, INT32_MAX },   { "llGetNumberOfSides", 0, INT32_MAX },   { "llGetObjectPrimCount", 0, INT32_MAX },
        { "llGetRegionAgentCount", 0, INT32_MAX }, { "llGetSPMaxMemory", 0, INT32_MAX },    { "llGetUsedMemory", 0, INT32_MAX },
        { "llStringLength", 0, INT32_MAX },
    };
    for (const Row& row : ROWS)
    {
        if (row.name == name)
        {
            least = row.least;
            most  = row.most;
            return true;
        }
    }
    const Trait* t = of(name);
    if (t && (t->slua & SluaBool))
    {
        least = 0;
        most  = 1;
        return true;
    }
    return false;
}

// static
bool ALLSLTraits::atLeastMinusOne(const char* name)
{
    static constexpr std::string_view NAMES[] = { "llGetInventoryType", "llListFindList", "llListFindListNext", "llListFindStrided",
                                                  "llSubStringIndex" };
    return name && std::find(std::begin(NAMES), std::end(NAMES), std::string_view(name)) != std::end(NAMES);
}
