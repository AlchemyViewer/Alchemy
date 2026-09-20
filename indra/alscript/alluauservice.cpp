/**
 * @file alluauservice.cpp
 * @brief The SLua analyzer over Second Life's fork of Luau.
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

#include "alluauservice.h"

#include "llsdjson.h"

#include "Luau/AstQuery.h"
#include "Luau/Autocomplete.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/Linter.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Luau/TypeArena.h"
#include "Luau/TypePack.h"

#include <unordered_map>

namespace
{
    // The script under analysis is the only module there is.
    const char* const SCRIPT_MODULE = "script";

    // The package the definitions are loaded as. luau-lsp names it this
    // way, and secondlife.docs.json keys its entries "@sl-slua/global/...",
    // so the two line up when hover documentation arrives.
    const char* const DEFINITIONS_PACKAGE = "@sl-slua";

    // The one script, served from memory. Anything it might require does
    // not exist yet; that is phase 3's preprocessor.
    struct ScriptResolver final : public Luau::FileResolver
    {
        std::string text;

        std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override
        {
            if (name != SCRIPT_MODULE)
            {
                return std::nullopt;
            }
            return Luau::SourceCode{ text, Luau::SourceCode::Script };
        }
    };

    ALScriptProblem problemAt(const Luau::Location& where,
                              ALScriptProblem::Severity severity,
                              ALScriptProblem::Source source,
                              std::string code,
                              std::string message)
    {
        ALScriptProblem problem;
        problem.severity  = severity;
        problem.source    = source;
        problem.line      = static_cast<S32>(where.begin.line);
        problem.column    = static_cast<S32>(where.begin.column);
        problem.endLine   = static_cast<S32>(where.end.line);
        problem.endColumn = static_cast<S32>(where.end.column);
        problem.code      = std::move(code);
        problem.message   = std::move(message);
        return problem;
    }

    Luau::FrontendOptions frontendOptions()
    {
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        // The types of every term stay with the module, which is what a
        // hover or a signature reads.
        options.retainFullTypeGraphs = true;
        return options;
    }

    Luau::Position positionOf(S32 line, S32 column)
    {
        return Luau::Position(static_cast<unsigned>(std::max(0, line)), static_cast<unsigned>(std::max(0, column)));
    }

    // How a type prints beside a name.
    std::string typeText(Luau::TypeId type)
    {
        Luau::ToStringOptions options;
        options.functionTypeArguments = true;
        options.hideNamedFunctionTypeParameters = false;
        return Luau::toString(type, options);
    }

    // The name a call or an index reads as: `ll.Say`, `print`, `t.x`.
    std::string nameOf(Luau::AstExpr* expr)
    {
        if (!expr)
        {
            return std::string();
        }
        if (Luau::AstExprLocal* local = expr->as<Luau::AstExprLocal>())
        {
            return local->local->name.value;
        }
        if (Luau::AstExprGlobal* global = expr->as<Luau::AstExprGlobal>())
        {
            return global->name.value;
        }
        if (Luau::AstExprIndexName* index = expr->as<Luau::AstExprIndexName>())
        {
            const std::string head = nameOf(index->expr);
            return (head.empty() ? std::string() : head + std::string(1, index->op)) + index->index.value;
        }
        return std::string();
    }

    // The function type a call's callee has, or the first of an
    // overloaded one's.
    const Luau::FunctionType* functionOf(Luau::TypeId type)
    {
        type = Luau::follow(type);
        if (const Luau::FunctionType* function = Luau::get<Luau::FunctionType>(type))
        {
            return function;
        }
        if (const Luau::IntersectionType* overloads = Luau::get<Luau::IntersectionType>(type))
        {
            for (Luau::TypeId part : overloads->parts)
            {
                if (const Luau::FunctionType* function = Luau::get<Luau::FunctionType>(Luau::follow(part)))
                {
                    return function;
                }
            }
        }
        return nullptr;
    }

    std::string withoutBreaks(std::string text)
    {
        for (size_t at = text.find("<br>"); at != std::string::npos; at = text.find("<br>", at + 1))
        {
            text.replace(at, 4, "\n");
        }
        return text;
    }
}

namespace
{
    // One configuration for the one script, whose mode is set per query:
    // nonstrict to report what a script author would be told, strict
    // where the types of everything are wanted.
    struct ModeResolver final : public Luau::ConfigResolver
    {
        Luau::Config config;

        const Luau::Config& getConfig(const Luau::ModuleName&, const Luau::TypeCheckLimits&) const override { return config; }
    };
}

struct ALLuauService::Impl
{
    ScriptResolver                  files;
    ModeResolver                    configs;
    std::unique_ptr<Luau::Frontend> frontend;
    bool                            definitions = false;

    struct Doc
    {
        std::string documentation;
        std::string link;
    };
    std::unordered_map<std::string, Doc> docs;

    // The script checked, for a query; the text is what the resolver
    // serves, and the front end lays out the module again only when it
    // has been marked.
    void checked(std::string_view source, bool for_autocomplete)
    {
        if (files.text != source)
        {
            files.text.assign(source);
        }
        frontend->markDirty(SCRIPT_MODULE);
        Luau::FrontendOptions options = frontendOptions();
        options.runLintChecks         = false;
        if (for_autocomplete)
        {
            options.forAutocomplete = true;
        }
        // A query wants every local's type, which nonstrict mode does not
        // work out.
        configs.config.mode = Luau::Mode::Strict;
        frontend->check(SCRIPT_MODULE, options);
        configs.config.mode = Luau::Mode::Nonstrict;
    }

    const Doc* docFor(const std::optional<std::string>& symbol) const
    {
        if (!symbol)
        {
            return nullptr;
        }
        const auto it = docs.find(*symbol);
        return it == docs.end() ? nullptr : &it->second;
    }

    // A front end with Luau's own globals in it, ready for the definitions
    // or for a script. One is built for each set of definitions, because
    // the global type arena is frozen once they are in and a frozen arena
    // takes nothing more.
    // Autocomplete type-checks against a global scope of its own, so the
    // builtins and the definitions go into both.
    static std::unique_ptr<Luau::Frontend> plainFrontend(ScriptResolver& files, ModeResolver& configs)
    {
        auto frontend = std::make_unique<Luau::Frontend>(&files, &configs, frontendOptions());
        Luau::registerBuiltinGlobals(*frontend, frontend->globals);
        Luau::registerBuiltinGlobals(*frontend, frontend->globalsForAutocomplete, /*typeCheckForAutocomplete*/ true);
        return frontend;
    }
};

ALLuauService::ALLuauService()
:   mImpl(std::make_unique<Impl>())
{
    mImpl->frontend = Impl::plainFrontend(mImpl->files, mImpl->configs);
    Luau::freeze(mImpl->frontend->globals.globalTypes);
    Luau::freeze(mImpl->frontend->globalsForAutocomplete.globalTypes);
}

ALLuauService::~ALLuauService() = default;

bool ALLuauService::loadDefinitions(std::string_view source, std::string& error)
{
    std::unique_ptr<Luau::Frontend> frontend = Impl::plainFrontend(mImpl->files, mImpl->configs);
    Luau::LoadDefinitionFileResult loaded = frontend->loadDefinitionFile(
        frontend->globals, frontend->globals.globalScope, source, DEFINITIONS_PACKAGE, /*captureComments*/ false);
    if (!loaded.success)
    {
        // The first thing that went wrong, which is the one worth reading.
        if (!loaded.parseResult.errors.empty())
        {
            const Luau::ParseError& first = loaded.parseResult.errors.front();
            error = llformat("%u:%u: %s",
                             first.getLocation().begin.line + 1,
                             first.getLocation().begin.column + 1,
                             first.getMessage().c_str());
        }
        else if (loaded.module && !loaded.module->errors.empty())
        {
            error = Luau::toString(loaded.module->errors.front());
        }
        else
        {
            error = "the definitions did not load";
        }
        return false;
    }
    Luau::LoadDefinitionFileResult for_autocomplete = frontend->loadDefinitionFile(
        frontend->globalsForAutocomplete, frontend->globalsForAutocomplete.globalScope, source, DEFINITIONS_PACKAGE,
        /*captureComments*/ false, /*typeCheckForAutocomplete*/ true);
    if (!for_autocomplete.success)
    {
        error = "the definitions did not load for autocomplete";
        return false;
    }
    Luau::freeze(frontend->globals.globalTypes);
    Luau::freeze(frontend->globalsForAutocomplete.globalTypes);
    mImpl->frontend    = std::move(frontend);
    mImpl->definitions = true;
    error.clear();
    return true;
}

bool ALLuauService::hasDefinitions() const
{
    return mImpl->definitions;
}

bool ALLuauService::loadDocs(std::string_view json, std::string& error)
{
    LLSD parsed;
    if (!LlsdFromJsonString(json, parsed, &error) || !parsed.isMap())
    {
        if (error.empty())
        {
            error = "the docs are not a JSON object";
        }
        return false;
    }
    mImpl->docs.clear();
    for (LLSD::map_const_iterator it = parsed.beginMap(); it != parsed.endMap(); ++it)
    {
        Impl::Doc doc;
        doc.documentation = withoutBreaks(it->second.get("documentation").asString());
        doc.link          = it->second.get("learn_more_link").asString();
        mImpl->docs.emplace(it->first, std::move(doc));
    }
    error.clear();
    return true;
}

bool ALLuauService::hasDocs() const
{
    return !mImpl->docs.empty();
}

ALScriptProblems ALLuauService::check(std::string_view source)
{
    Impl& impl = *mImpl;
    impl.files.text.assign(source);
    impl.frontend->markDirty(SCRIPT_MODULE);
    Luau::CheckResult result = impl.frontend->check(SCRIPT_MODULE);

    ALScriptProblems problems;
    problems.reserve(result.errors.size() + result.lintResult.errors.size() + result.lintResult.warnings.size());
    for (const Luau::TypeError& error : result.errors)
    {
        const bool syntax = Luau::get_if<Luau::SyntaxError>(&error.data) != nullptr;
        problems.push_back(problemAt(error.location,
                                     ALScriptProblem::Severity::Error,
                                     syntax ? ALScriptProblem::Source::Parser : ALScriptProblem::Source::Types,
                                     std::string(),
                                     Luau::toString(error)));
    }
    for (const Luau::LintWarning& warning : result.lintResult.errors)
    {
        problems.push_back(problemAt(warning.location,
                                     ALScriptProblem::Severity::Error,
                                     ALScriptProblem::Source::Lint,
                                     Luau::LintWarning::getName(warning.code),
                                     warning.text));
    }
    for (const Luau::LintWarning& warning : result.lintResult.warnings)
    {
        problems.push_back(problemAt(warning.location,
                                     ALScriptProblem::Severity::Warning,
                                     ALScriptProblem::Source::Lint,
                                     Luau::LintWarning::getName(warning.code),
                                     warning.text));
    }
    return problems;
}

// --- what could go here ---------------------------------------------------------

std::vector<ALScriptCompletion> ALLuauService::complete(std::string_view source, S32 line, S32 column)
{
    Impl& impl = *mImpl;
    impl.checked(source, /*for_autocomplete*/ true);
    Luau::AutocompleteResult found = Luau::autocomplete(
        *impl.frontend, SCRIPT_MODULE, positionOf(line, column),
        [](std::string, std::optional<const Luau::ExternType*>, std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> {
            return std::nullopt;
        });

    std::vector<ALScriptCompletion> out;
    out.reserve(found.entryMap.size());
    for (const auto& [name, entry] : found.entryMap)
    {
        ALScriptCompletion completion;
        completion.text       = name;
        completion.deprecated = entry.deprecated;
        const bool callable   = entry.type && functionOf(*entry.type) != nullptr;
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
        if (entry.type)
        {
            completion.detail = typeText(*entry.type);
        }
        std::optional<std::string> symbol = entry.documentationSymbol;
        if (!symbol && entry.type)
        {
            symbol = Luau::follow(*entry.type)->documentationSymbol;
        }
        if (const Impl::Doc* doc = impl.docFor(symbol))
        {
            completion.documentation = doc->documentation;
        }
        out.push_back(std::move(completion));
    }
    std::sort(out.begin(), out.end(), [](const ALScriptCompletion& a, const ALScriptCompletion& b) { return a.text < b.text; });
    return out;
}

// --- what is here --------------------------------------------------------------------

ALScriptHover ALLuauService::hover(std::string_view source, S32 line, S32 column)
{
    Impl& impl = *mImpl;
    impl.checked(source, /*for_autocomplete*/ false);
    ALScriptHover           answer;
    const Luau::SourceModule* module_source = impl.frontend->getSourceModule(SCRIPT_MODULE);
    const Luau::ModulePtr     module        = impl.frontend->moduleResolver.getModule(SCRIPT_MODULE);
    if (!module_source || !module)
    {
        return answer;
    }
    const Luau::Position         at   = positionOf(line, column);
    const std::optional<Luau::TypeId> type = Luau::findTypeAtPosition(*module, *module_source, at);
    if (!type)
    {
        return answer;
    }
    answer.found = true;
    std::string name = nameOf(Luau::findExprAtPosition(*module_source, at));
    if (Luau::AstExpr* expr = Luau::findExprAtPosition(*module_source, at); expr && name.empty())
    {
        // Under a binding's own name in a `local` or a function statement.
        if (std::optional<Luau::Binding> binding = Luau::findBindingAtPosition(*module, *module_source, at))
        {
            (void)binding;
        }
    }
    answer.label = name.empty() ? typeText(*type) : name + ": " + typeText(*type);
    std::optional<std::string> symbol = Luau::getDocumentationSymbolAtPosition(*module_source, *module, at);
    if (!symbol)
    {
        symbol = Luau::follow(*type)->documentationSymbol;
    }
    if (const Impl::Doc* doc = impl.docFor(symbol))
    {
        answer.documentation = doc->documentation;
        answer.link          = doc->link;
    }
    if (std::optional<Luau::Binding> binding = Luau::findBindingAtPosition(*module, *module_source, at))
    {
        // A binding the script made has a location in it; one from the
        // definitions has none worth going to.
        if (binding->location.begin != binding->location.end && !binding->documentationSymbol)
        {
            answer.hasDefinition    = true;
            answer.definitionLine   = static_cast<S32>(binding->location.begin.line);
            answer.definitionColumn = static_cast<S32>(binding->location.begin.column);
        }
    }
    return answer;
}

// --- what a call here takes ----------------------------------------------------------

ALScriptSignature ALLuauService::signature(std::string_view source, S32 line, S32 column)
{
    Impl& impl = *mImpl;
    impl.checked(source, /*for_autocomplete*/ false);
    ALScriptSignature         answer;
    const Luau::SourceModule* module_source = impl.frontend->getSourceModule(SCRIPT_MODULE);
    const Luau::ModulePtr     module        = impl.frontend->moduleResolver.getModule(SCRIPT_MODULE);
    if (!module_source || !module)
    {
        return answer;
    }
    const Luau::Position at = positionOf(line, column);
    // The innermost call whose parentheses hold the position.
    std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*module_source, at);
    Luau::AstExprCall*          call     = nullptr;
    for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
    {
        if (Luau::AstExprCall* candidate = (*it)->as<Luau::AstExprCall>(); candidate && candidate->argLocation.containsClosed(at))
        {
            call = candidate;
            break;
        }
    }
    if (!call)
    {
        return answer;
    }
    const Luau::TypeId* callee = module->astTypes.find(call->func);
    if (!callee)
    {
        return answer;
    }
    const Luau::FunctionType* function = functionOf(*callee);
    if (!function)
    {
        return answer;
    }
    answer.found = true;
    const std::string name = nameOf(call->func);
    Luau::ToStringOptions options;
    options.functionTypeArguments = true;
    answer.label = Luau::toStringNamedFunction(name.empty() ? "function" : name, *function, options);

    // The parameters as they print, the first dropped when the call
    // passes it as self.
    const auto [arg_types, tail] = Luau::flatten(function->argTypes);
    const size_t skip = (call->self || function->hasSelf) && !arg_types.empty() ? 1 : 0;
    for (size_t i = skip; i < arg_types.size(); ++i)
    {
        std::string parameter;
        if (i < function->argNames.size() && function->argNames[i])
        {
            parameter = function->argNames[i]->name + ": ";
        }
        parameter += typeText(arg_types[i]);
        answer.parameters.push_back(std::move(parameter));
    }
    if (tail)
    {
        answer.parameters.push_back("..." + Luau::toString(*tail));
    }
    // Which one the position is at: the argument that holds it, or the
    // one after the last that ends before it.
    S32 active = 0;
    for (size_t i = 0; i < call->args.size; ++i)
    {
        const Luau::Location& where = call->args.data[i]->location;
        if (where.containsClosed(at))
        {
            active = static_cast<S32>(i);
            break;
        }
        if (where.end < at || where.end == at)
        {
            active = static_cast<S32>(i) + 1;
        }
    }
    answer.active = active;
    std::optional<std::string> symbol = Luau::follow(*callee)->documentationSymbol;
    if (!symbol)
    {
        symbol = Luau::getDocumentationSymbolAtPosition(*module_source, *module, call->func->location.begin);
    }
    if (const Impl::Doc* doc = impl.docFor(symbol))
    {
        answer.documentation = doc->documentation;
    }
    return answer;
}
