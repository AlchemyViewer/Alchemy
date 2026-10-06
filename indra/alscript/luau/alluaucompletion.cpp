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

#include "alluaufragment.h"
#include "alluaufrontend.h"
#include "alluautypes.h"
#include "alscriptlexicon.h"

#include "Luau/Ast.h"
#include "Luau/Autocomplete.h"
#include "Luau/Type.h"
#include "Luau/TypePack.h"

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

    // A function written out to be filled in, as a snippet: `head` -- its
    // `function(...)` and what it returns -- then a line for its body, where
    // the caret lands, then its `end`. Luau's own stub ends on the line it
    // starts, `function(...)  end`; one without an `end` is a parameter list
    // alone, put in as it is.
    struct Stub
    {
        std::string head;
        std::string snippet;
    };

    Stub stubOf(std::string written)
    {
        while (!written.empty() && written.back() == ' ')
        {
            written.pop_back();
        }
        const bool body = written.size() >= 3 && written.compare(written.size() - 3, 3, "end") == 0;
        if (body)
        {
            written.erase(written.size() - 3);
            while (!written.empty() && written.back() == ' ')
            {
                written.pop_back();
            }
        }
        // A dollar is the snippet's own mark; nothing else of a type is.
        std::string escaped;
        for (char c : written)
        {
            if (c == '$')
            {
                escaped += '\\';
            }
            escaped += c;
        }
        return Stub{ written, body ? escaped + "\n    $0\nend" : escaped };
    }

    // The same written from a function type, as Luau writes its own: each
    // parameter by its name, else `a` and its place, with its type; a
    // variadic tail as `...`, typed where it is not `any`; and what it
    // returns, where it returns anything.
    std::string writtenOf(const Luau::FunctionType& type)
    {
        auto [args, tail] = Luau::flatten(type.argTypes);
        std::string out   = "function(";
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
            {
                out += ", ";
            }
            out += i < type.argNames.size() && type.argNames[i] ? type.argNames[i]->name : "a" + std::to_string(i);
            out += ": " + ALLuauTypes::typeText(args[i]);
        }
        if (tail)
        {
            if (const Luau::VariadicTypePack* rest = Luau::get<Luau::VariadicTypePack>(Luau::follow(*tail)))
            {
                out += args.empty() ? "..." : ", ...";
                if (!Luau::get<Luau::AnyType>(Luau::follow(rest->ty)))
                {
                    out += ": " + ALLuauTypes::typeText(rest->ty);
                }
            }
        }
        out += ")";
        auto [rets, rest] = Luau::flatten(type.retTypes);
        if (!rets.empty() && !rest)
        {
            out += ": ";
            if (rets.size() > 1)
            {
                out += "(";
            }
            for (size_t i = 0; i < rets.size(); ++i)
            {
                out += (i > 0 ? ", " : "") + ALLuauTypes::typeText(rets[i]);
            }
            if (rets.size() > 1)
            {
                out += ")";
            }
        }
        return out + "  end";
    }

    // The function an argument at `at` is to be, where the callee is
    // overloaded: Luau writes no stub for one, so the overload the checker
    // chose for the call says which. None where `at` is in no call's
    // arguments, the callee is not overloaded, nothing was chosen, or the
    // argument is no function.
    std::optional<Luau::TypeId> overloadedCallbackAt(const Luau::Module& module, const std::vector<Luau::AstNode*>& ancestry, Luau::Position at)
    {
        for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
        {
            const Luau::AstExprCall* call = (*it)->as<Luau::AstExprCall>();
            if (!call)
            {
                continue;
            }
            if (!call->argLocation.containsClosed(at) || call->func->location.containsClosed(at))
            {
                return std::nullopt;
            }
            const Luau::TypeId* callee = module.astTypes.find(call->func);
            if (!callee || !Luau::get<Luau::IntersectionType>(Luau::follow(*callee)))
            {
                return std::nullopt;
            }
            const Luau::TypeId*       chosen   = module.astOverloadResolvedTypes.find(call);
            const Luau::FunctionType* overload = chosen ? Luau::get<Luau::FunctionType>(Luau::follow(*chosen)) : nullptr;
            if (!overload)
            {
                return std::nullopt;
            }
            // The argument the position is in; past the last where it is
            // in none yet, after a comma.
            size_t argument = call->args.size;
            for (size_t i = 0; i < call->args.size; ++i)
            {
                if (call->args.data[i]->location.containsClosed(at))
                {
                    argument = i;
                    break;
                }
            }
            if (call->self)
            {
                ++argument;
            }
            auto [args, tail] = Luau::flatten(overload->argTypes);
            if (argument >= args.size())
            {
                return std::nullopt;
            }
            const Luau::TypeId wanted = Luau::follow(args[argument]);
            if (Luau::get<Luau::FunctionType>(wanted))
            {
                return wanted;
            }
            // An optional one: the function it is where it is given.
            if (const Luau::UnionType* options = Luau::get<Luau::UnionType>(wanted))
            {
                for (Luau::TypeId option : options->options)
                {
                    if (Luau::get<Luau::FunctionType>(Luau::follow(option)))
                    {
                        return Luau::follow(option);
                    }
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    ALScriptCompletion stubCompletion(const Stub& stub, std::optional<Luau::TypeId> type, ALScriptCompletion::Context context)
    {
        ALScriptCompletion completion;
        completion.text    = stub.head;
        completion.snippet = stub.snippet;
        completion.kind    = ALScriptSymbolKind::Function;
        completion.fits    = true;
        completion.context = context;
        if (type)
        {
            completion.detail = ALLuauTypes::typeText(*type);
        }
        return completion;
    }
}

ALLuauCompletion::ALLuauCompletion(ALLuauFrontend& front, ALLuauFragment& fragment)
:   mFront(front)
,   mFragment(fragment)
{
}

std::vector<ALScriptCompletion> ALLuauCompletion::complete(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const Luau::Position                 at       = ALLuauTypes::positionOf(line, column);
    const Luau::StringCompletionCallback strings  = [](std::string, std::optional<const Luau::ExternType*>,
                                                      std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> {
        return std::nullopt;
    };
    // As the script is typed: the statement being typed checked alone
    // against the last check, where that answers.
    ALLuauFragment::Completion fragment = mFragment.complete(source, at, strings);
    if (fragment.outcome == ALLuauFragment::Outcome::Nothing)
    {
        return {};
    }
    if (fragment.outcome == ALLuauFragment::Outcome::Answered)
    {
        return answer(fragment.found, *fragment.module, at);
    }
    // Nothing offered from a check stopped part way.
    const Luau::ModulePtr module = mFront.queried(source, /*completion*/ true);
    if (!module)
    {
        return {};
    }
    return answer(Luau::autocomplete(*mFront.frontend, mFront.moduleName, at, strings), *module, at);
}

std::vector<ALScriptCompletion> ALLuauCompletion::answer(const Luau::AutocompleteResult& found, const Luau::Module& module, Luau::Position at) const
{

    const ALScriptCompletion::Context context = contextOf(found.context);
    // The dot or the colon a member's name follows, where it follows one.
    const Luau::AstExprIndexName* index = found.ancestry.empty() ? nullptr : found.ancestry.back()->as<Luau::AstExprIndexName>();
    const char                    op    = index ? index->op : '\0';
    std::vector<ALScriptCompletion> out;
    out.reserve(found.entryMap.size() + 1);
    bool stubbed = false;
    for (const auto& [name, entry] : found.entryMap)
    {
        // A function for an argument that wants one, written out: first,
        // since it is of the type wanted.
        if (entry.kind == Luau::AutocompleteEntryKind::GeneratedFunction)
        {
            if (entry.insertText)
            {
                out.push_back(stubCompletion(stubOf(*entry.insertText), entry.type, context));
                stubbed = true;
            }
            continue;
        }
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
            case Luau::AutocompleteEntryKind::HotComment:
                // Those the studio offers, as words of the language.
                if (!ALScriptLexicon::isLuauHotComment(name))
                {
                    continue;
                }
                completion.kind = ALScriptSymbolKind::Keyword;
                break;
            default:
                // Require paths: nothing the studio offers yet.
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
    // The same for an overloaded callee's argument, which Luau leaves
    // alone: `LLEvents:on("touch_start", ...)` wants the handler its
    // event takes.
    if (!stubbed)
    {
        if (const std::optional<Luau::TypeId> wanted = overloadedCallbackAt(module, found.ancestry, at))
        {
            out.push_back(stubCompletion(stubOf(writtenOf(*Luau::get<Luau::FunctionType>(*wanted))), wanted, context));
        }
    }
    return out;
}
