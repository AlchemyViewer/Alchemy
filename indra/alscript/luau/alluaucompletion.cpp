/**
 * @file alluaucompletion.cpp
 * @brief What could go at a position of an SLua script, as Luau's autocomplete says.
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

#include "alluaucompletion.h"

#include "alluaufrontend.h"
#include "alluautypes.h"

#include "Luau/Ast.h"
#include "Luau/Autocomplete.h"
#include "Luau/Type.h"

namespace
{
    ALScriptCompletion::Context contextOf(Luau::AutocompleteContext context)
    {
        switch (context)
        {
            case Luau::AutocompleteContext::Expression: return ALScriptCompletion::Context::Expression;
            case Luau::AutocompleteContext::Statement:  return ALScriptCompletion::Context::Statement;
            case Luau::AutocompleteContext::Property:   return ALScriptCompletion::Context::Property;
            case Luau::AutocompleteContext::Type:       return ALScriptCompletion::Context::Type;
            case Luau::AutocompleteContext::Keyword:    return ALScriptCompletion::Context::Keyword;
            case Luau::AutocompleteContext::String:     return ALScriptCompletion::Context::String;
            case Luau::AutocompleteContext::HotComment: return ALScriptCompletion::Context::HotComment;
            default:                                    return ALScriptCompletion::Context::Unknown;
        }
    }

    // Whether a member is no use indexed as it is, `op` being the dot or
    // the colon before it. Luau's mark, but for an extern type's: there it
    // asks for a method to be called as it was declared, and the grid's
    // definitions declare `LLEvents:on` and its kind as fields whose first
    // parameter is self, which Luau then marks as wrong after a colon and
    // right after a dot. A member is a method where it takes self first,
    // however it was declared.
    bool wronglyIndexed(const Luau::AutocompleteEntry& entry, char op)
    {
        const Luau::FunctionType* function = entry.type ? ALLuauTypes::functionOf(*entry.type) : nullptr;
        if (!entry.containingExternType || !function || (op != '.' && op != ':'))
        {
            return entry.wrongIndexType;
        }
        const bool method = function->hasSelf || (!function->argNames.empty() && function->argNames[0] && function->argNames[0]->name == "self");
        return method != (op == ':');
    }

    ALScriptCompletion::Brackets bracketsOf(Luau::ParenthesesRecommendation parens)
    {
        switch (parens)
        {
            case Luau::ParenthesesRecommendation::CursorAfter:  return ALScriptCompletion::Brackets::After;
            case Luau::ParenthesesRecommendation::CursorInside: return ALScriptCompletion::Brackets::Inside;
            default:                                            return ALScriptCompletion::Brackets::None;
        }
    }
}

ALLuauCompletion::ALLuauCompletion(ALLuauFrontend& front)
:   mFront(front)
{
}

std::vector<ALScriptCompletion> ALLuauCompletion::complete(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // Nothing offered from a check stopped part way.
    if (!mFront.queried(source, /*completion*/ true))
    {
        return {};
    }
    Luau::AutocompleteResult found = Luau::autocomplete(
        *mFront.frontend, mFront.moduleName, ALLuauTypes::positionOf(line, column),
        [](std::string, std::optional<const Luau::ExternType*>, std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> {
            return std::nullopt;
        });

    const ALScriptCompletion::Context context = contextOf(found.context);
    // The dot or the colon a member's name follows, where it follows one.
    const Luau::AstExprIndexName* index = found.ancestry.empty() ? nullptr : found.ancestry.back()->as<Luau::AstExprIndexName>();
    const char                    op    = index ? index->op : '\0';
    std::vector<ALScriptCompletion> out;
    out.reserve(found.entryMap.size());
    for (const auto& [name, entry] : found.entryMap)
    {
        // A method after a dot, a field after a colon: listed, and no use
        // there.
        if (wronglyIndexed(entry, op))
        {
            continue;
        }
        ALScriptCompletion completion;
        completion.text       = name;
        completion.deprecated = entry.deprecated;
        completion.context    = context;
        completion.fits       = entry.typeCorrect != Luau::TypeCorrectKind::None;
        const bool callable   = entry.type && ALLuauTypes::functionOf(*entry.type) != nullptr;
        switch (entry.kind)
        {
            case Luau::AutocompleteEntryKind::Keyword:
                completion.kind = ALScriptSymbolKind::Keyword;
                break;
            case Luau::AutocompleteEntryKind::Property:
                completion.kind = callable ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Field;
                break;
            case Luau::AutocompleteEntryKind::Binding:
                completion.kind = callable ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Variable;
                break;
            case Luau::AutocompleteEntryKind::Type:
                completion.kind = ALScriptSymbolKind::Type;
                break;
            case Luau::AutocompleteEntryKind::Module:
                completion.kind = ALScriptSymbolKind::Module;
                break;
            case Luau::AutocompleteEntryKind::String:
                completion.kind = ALScriptSymbolKind::Constant;
                break;
            default:
                // Generated functions, require paths and hot comments:
                // nothing the studio offers yet.
                continue;
        }
        // Where a call's brackets go, for what Luau says may be called:
        // none where it is called already. Nor where a function is wanted
        // as it is -- Luau's own rule, which it applies to a field but works
        // out for a binding before it knows the binding is what is wanted.
        if (completion.kind == ALScriptSymbolKind::Function)
        {
            completion.brackets = entry.typeCorrect == Luau::TypeCorrectKind::Correct ? ALScriptCompletion::Brackets::None
                                                                                       : bracketsOf(entry.parens);
        }
        if (entry.type)
        {
            completion.detail = ALLuauTypes::typeText(*entry.type);
        }
        std::optional<std::string> symbol = entry.documentationSymbol;
        if (!symbol && entry.type)
        {
            symbol = Luau::follow(*entry.type)->documentationSymbol;
        }
        if (const ALLuauFrontend::Doc* doc = mFront.docFor(symbol))
        {
            completion.documentation = doc->documentation;
        }
        out.push_back(std::move(completion));
    }
    return out;
}
