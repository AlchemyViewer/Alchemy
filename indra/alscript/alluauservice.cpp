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

#include <set>
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

    ALScriptSpan spanOf(const Luau::Location& where)
    {
        ALScriptSpan span;
        span.line      = static_cast<S32>(where.begin.line);
        span.column    = static_cast<S32>(where.begin.column);
        span.endLine   = static_cast<S32>(where.end.line);
        span.endColumn = static_cast<S32>(where.end.column);
        return span;
    }

    // What a position names: a local, by the binding itself; a global, by
    // its name; or a field, by its name and the table it is a field of.
    struct Target
    {
        enum class Kind : U8
        {
            None,
            Local,
            Global,
            Field
        };
        Kind            kind  = Kind::None;
        Luau::AstLocal* local = nullptr;
        Luau::AstName   global;
        std::string     field;
        // The table a field belongs to, followed, or null for one whose
        // table has no type; fields of the same name on other tables are
        // other fields.
        Luau::TypeId    table = nullptr;
    };

    Luau::TypeId tableTypeOf(const Luau::Module& module, Luau::AstExpr* expr)
    {
        const Luau::TypeId* type = module.astTypes.find(expr);
        return type ? Luau::follow(*type) : nullptr;
    }

    Target targetAt(const Luau::Module& module, const Luau::SourceModule& source, Luau::Position at)
    {
        Target             target;
        Luau::ExprOrLocal  found = Luau::findExprOrLocalAtPosition(source, at);
        if (Luau::AstLocal* local = found.getLocal())
        {
            target.kind  = Target::Kind::Local;
            target.local = local;
            return target;
        }
        Luau::AstExpr* expr = found.getExpr();
        if (!expr)
        {
            return target;
        }
        if (Luau::AstExprLocal* local = expr->as<Luau::AstExprLocal>())
        {
            target.kind  = Target::Kind::Local;
            target.local = local->local;
        }
        else if (Luau::AstExprGlobal* global = expr->as<Luau::AstExprGlobal>())
        {
            target.kind   = Target::Kind::Global;
            target.global = global->name;
        }
        else if (Luau::AstExprIndexName* index = expr->as<Luau::AstExprIndexName>())
        {
            if (index->indexLocation.containsClosed(at))
            {
                target.kind  = Target::Kind::Field;
                target.field = index->index.value;
                target.table = tableTypeOf(module, index->expr);
            }
        }
        else if (Luau::AstExprConstantString* key = expr->as<Luau::AstExprConstantString>())
        {
            // A record's key in a table constructor: a name to a person,
            // a string to the parser.
            for (Luau::AstNode* node : Luau::findAstAncestryOfPosition(source, at))
            {
                Luau::AstExprTable* table = node->as<Luau::AstExprTable>();
                if (!table)
                {
                    continue;
                }
                for (const Luau::AstExprTable::Item& item : table->items)
                {
                    if (item.kind == Luau::AstExprTable::Item::Kind::Record && item.key == key)
                    {
                        target.kind  = Target::Kind::Field;
                        target.field = std::string(key->value.data, key->value.size);
                        target.table = tableTypeOf(module, table);
                    }
                }
            }
        }
        return target;
    }

    // Every place a target stands, and where it is bound.
    struct Uses final : public Luau::AstVisitor
    {
        const Target&                 target;
        const Luau::Module&           module;
        std::vector<ALScriptSpan>     spans;
        std::optional<Luau::Location> definition;
        bool                          parameter = false;
        bool                          function  = false;

        Uses(const Target& target_in, const Luau::Module& module_in)
        :   target(target_in),
            module(module_in)
        {
        }

        bool sameTable(Luau::AstExpr* expr) const
        {
            if (!target.table)
            {
                return true;
            }
            const Luau::TypeId type = tableTypeOf(module, expr);
            return !type || type == target.table;
        }
        bool isField(Luau::AstExprIndexName* index) const
        {
            return target.kind == Target::Kind::Field && target.field == index->index.value && sameTable(index->expr);
        }
        bool isGlobal(Luau::AstExprGlobal* global) const { return target.kind == Target::Kind::Global && global->name == target.global; }
        bool isLocal(Luau::AstLocal* local) const { return target.kind == Target::Kind::Local && local == target.local; }

        void add(const Luau::Location& where) { spans.push_back(spanOf(where)); }
        void declare(const Luau::Location& where)
        {
            if (!definition)
            {
                definition = where;
            }
            add(where);
        }

        bool visit(Luau::AstExprLocal* expr) override
        {
            if (isLocal(expr->local))
            {
                add(expr->location);
            }
            return true;
        }
        bool visit(Luau::AstExprGlobal* expr) override
        {
            if (isGlobal(expr))
            {
                add(expr->location);
            }
            return true;
        }
        bool visit(Luau::AstExprIndexName* expr) override
        {
            if (isField(expr))
            {
                add(expr->indexLocation);
            }
            return true;
        }
        bool visit(Luau::AstExprTable* table) override
        {
            if (target.kind == Target::Kind::Field && sameTable(table))
            {
                for (const Luau::AstExprTable::Item& item : table->items)
                {
                    Luau::AstExprConstantString* key = item.key ? item.key->as<Luau::AstExprConstantString>() : nullptr;
                    if (item.kind == Luau::AstExprTable::Item::Kind::Record && key
                        && std::string_view(key->value.data, key->value.size) == target.field)
                    {
                        declare(key->location);
                    }
                }
            }
            return true;
        }
        bool visit(Luau::AstStatLocal* stat) override
        {
            for (Luau::AstLocal* local : stat->vars)
            {
                if (isLocal(local))
                {
                    declare(local->location);
                }
            }
            return true;
        }
        bool visit(Luau::AstStatLocalFunction* stat) override
        {
            if (isLocal(stat->name))
            {
                declare(stat->name->location);
                function = true;
            }
            return true;
        }
        bool visit(Luau::AstExprFunction* expr) override
        {
            if (expr->self && isLocal(expr->self))
            {
                declare(expr->self->location);
                parameter = true;
            }
            for (Luau::AstLocal* arg : expr->args)
            {
                if (isLocal(arg))
                {
                    declare(arg->location);
                    parameter = true;
                }
            }
            return true;
        }
        bool visit(Luau::AstStatFor* stat) override
        {
            if (isLocal(stat->var))
            {
                declare(stat->var->location);
            }
            return true;
        }
        bool visit(Luau::AstStatForIn* stat) override
        {
            for (Luau::AstLocal* local : stat->vars)
            {
                if (isLocal(local))
                {
                    declare(local->location);
                }
            }
            return true;
        }
        // The name of a function statement, or the first assignment, is
        // where a global or a field is bound; the name itself is added
        // when it is visited as the expression it is.
        bool visit(Luau::AstStatFunction* stat) override
        {
            if (!definition && bind(stat->name))
            {
                function = true;
            }
            return true;
        }
        bool visit(Luau::AstStatAssign* stat) override
        {
            for (Luau::AstExpr* var : stat->vars)
            {
                bind(var);
            }
            return true;
        }
        // Whether this is the target's first binding.
        bool bind(Luau::AstExpr* name)
        {
            if (definition)
            {
                return false;
            }
            if (Luau::AstExprGlobal* global = name->as<Luau::AstExprGlobal>(); global && isGlobal(global))
            {
                definition = global->location;
            }
            else if (Luau::AstExprIndexName* index = name->as<Luau::AstExprIndexName>(); index && isField(index))
            {
                definition = index->indexLocation;
            }
            return definition.has_value();
        }
    };

    // The outline: what the top of the script binds, and every function,
    // each function's own one deeper. The locals inside a function are
    // its business.
    struct Outliner final : public Luau::AstVisitor
    {
        const Luau::Module*               module;
        std::vector<ALScriptOutlineEntry> out;
        S32                               depth = 0;
        std::set<std::string>             bound;

        explicit Outliner(const Luau::Module* module_in)
        :   module(module_in)
        {
        }

        std::string typeAt(Luau::AstExpr* expr) const
        {
            const Luau::TypeId* type = expr && module ? module->astTypes.find(expr) : nullptr;
            return type ? typeText(*type) : std::string();
        }
        static bool isEvent(const std::string& name) { return name.rfind("LLEvents.", 0) == 0; }
        void entry(std::string name, const Luau::Location& name_where, const Luau::Location& where, ALScriptSymbolKind kind, std::string detail)
        {
            ALScriptOutlineEntry one;
            one.name     = std::move(name);
            one.detail   = std::move(detail);
            one.kind     = kind;
            one.nameSpan = spanOf(name_where);
            one.span     = spanOf(where);
            one.depth    = depth;
            out.push_back(std::move(one));
        }
        void inside(Luau::AstStatBlock* body)
        {
            ++depth;
            body->visit(this);
            --depth;
        }

        bool visit(Luau::AstStatLocal* stat) override
        {
            if (depth == 0)
            {
                for (size_t i = 0; i < stat->vars.size; ++i)
                {
                    Luau::AstExpr* value = i < stat->values.size ? stat->values.data[i] : nullptr;
                    const bool     function = value && value->is<Luau::AstExprFunction>();
                    entry(stat->vars.data[i]->name.value, stat->vars.data[i]->location, stat->location,
                          function ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Variable, typeAt(value));
                }
            }
            return true;
        }
        bool visit(Luau::AstStatLocalFunction* stat) override
        {
            entry(stat->name->name.value, stat->name->location, stat->location, ALScriptSymbolKind::Function, typeAt(stat->func));
            inside(stat->func->body);
            return false;
        }
        bool visit(Luau::AstStatFunction* stat) override
        {
            const std::string name = nameOf(stat->name);
            Luau::Location    name_where = stat->name->location;
            if (Luau::AstExprIndexName* index = stat->name->as<Luau::AstExprIndexName>())
            {
                name_where = index->indexLocation;
            }
            entry(name.empty() ? "function" : name, name_where, stat->location,
                  isEvent(name) ? ALScriptSymbolKind::Event : ALScriptSymbolKind::Function, typeAt(stat->func));
            inside(stat->func->body);
            return false;
        }
        bool visit(Luau::AstStatAssign* stat) override
        {
            if (depth == 0)
            {
                for (size_t i = 0; i < stat->vars.size; ++i)
                {
                    Luau::AstExpr*    var  = stat->vars.data[i];
                    const std::string name = nameOf(var);
                    if (name.empty() || !bound.insert(name).second)
                    {
                        continue;
                    }
                    Luau::AstExpr* value    = i < stat->values.size ? stat->values.data[i] : nullptr;
                    const bool     function = value && value->is<Luau::AstExprFunction>();
                    Luau::Location name_where = var->location;
                    ALScriptSymbolKind kind = var->is<Luau::AstExprGlobal>() ? ALScriptSymbolKind::Variable : ALScriptSymbolKind::Field;
                    if (Luau::AstExprIndexName* index = var->as<Luau::AstExprIndexName>())
                    {
                        name_where = index->indexLocation;
                    }
                    if (function)
                    {
                        kind = isEvent(name) ? ALScriptSymbolKind::Event : ALScriptSymbolKind::Function;
                    }
                    entry(name, name_where, stat->location, kind, typeAt(value));
                }
            }
            return true;
        }
        bool visit(Luau::AstStatTypeAlias* stat) override
        {
            if (depth == 0)
            {
                entry(stat->name.value, stat->nameLocation, stat->location, ALScriptSymbolKind::Type, std::string());
            }
            return false;
        }
        bool visit(Luau::AstExprFunction* expr) override
        {
            inside(expr->body);
            return false;
        }
    };
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

// --- where a name lives -------------------------------------------------------------

ALScriptReferences ALLuauService::references(std::string_view source, S32 line, S32 column)
{
    Impl& impl = *mImpl;
    impl.checked(source, /*for_autocomplete*/ false);
    ALScriptReferences        answer;
    const Luau::SourceModule* module_source = impl.frontend->getSourceModule(SCRIPT_MODULE);
    const Luau::ModulePtr     module        = impl.frontend->moduleResolver.getModule(SCRIPT_MODULE);
    if (!module_source || !module || !module_source->root)
    {
        return answer;
    }
    const Luau::Position at     = positionOf(line, column);
    const Target         target = targetAt(*module, *module_source, at);
    if (target.kind == Target::Kind::None)
    {
        return answer;
    }
    Uses uses(target, *module);
    module_source->root->visit(&uses);
    std::sort(uses.spans.begin(), uses.spans.end());
    uses.spans.erase(std::unique(uses.spans.begin(), uses.spans.end()), uses.spans.end());

    answer.found = true;
    switch (target.kind)
    {
        case Target::Kind::Local:
            answer.name = target.local->name.value;
            break;
        case Target::Kind::Global:
            answer.name = target.global.value;
            break;
        default:
            answer.name = target.field;
            break;
    }
    const std::optional<Luau::TypeId> type     = Luau::findTypeAtPosition(*module, *module_source, at);
    const bool                        callable = uses.function || (type && functionOf(*type));
    if (uses.parameter)
    {
        answer.kind = ALScriptSymbolKind::Parameter;
    }
    else if (callable)
    {
        answer.kind = ALScriptSymbolKind::Function;
    }
    else
    {
        answer.kind = target.kind == Target::Kind::Field ? ALScriptSymbolKind::Field : ALScriptSymbolKind::Variable;
    }
    if (uses.definition)
    {
        answer.hasDefinition = true;
        answer.definition    = spanOf(*uses.definition);
        answer.renamable     = true;
    }
    answer.references = std::move(uses.spans);
    return answer;
}

// --- what the script declares ------------------------------------------------------

std::vector<ALScriptOutlineEntry> ALLuauService::outline(std::string_view source)
{
    Impl& impl = *mImpl;
    // The text just checked is laid out already; anything else is checked
    // now, since the types beside the names come from the check.
    if (impl.files.text != source || !impl.frontend->getSourceModule(SCRIPT_MODULE))
    {
        impl.checked(source, /*for_autocomplete*/ false);
    }
    const Luau::SourceModule* module_source = impl.frontend->getSourceModule(SCRIPT_MODULE);
    const Luau::ModulePtr     module        = impl.frontend->moduleResolver.getModule(SCRIPT_MODULE);
    if (!module_source || !module_source->root)
    {
        return {};
    }
    Outliner outliner(module.get());
    module_source->root->visit(&outliner);
    return std::move(outliner.out);
}
