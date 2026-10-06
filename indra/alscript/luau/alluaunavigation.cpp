/**
 * @file alluaunavigation.cpp
 * @brief Where a name in an SLua script is bound and used, across the modules it requires.
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

#include "alluaunavigation.h"

#include "alluaufrontend.h"
#include "alluautypes.h"

#include "Luau/AstQuery.h"
#include "Luau/Frontend.h"
#include "Luau/Module.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"

#include <algorithm>
#include <vector>

namespace
{
    using ALLuauTypes::functionOf;
    using ALLuauTypes::spanOf;

    // What a module the script requires is called to Luau, ahead of its
    // key (ALLuauFrontend::moduleOf).
    constexpr std::string_view MODULE = "module:";

    // A table or a type by where it was made: the module, and the place in
    // it. A copy keeps it -- the one a script requiring the module sees --
    // where the type itself is another.
    struct Identity
    {
        std::string    module;
        Luau::Location where;

        friend bool operator==(const Identity& a, const Identity& b) { return a.module == b.module && a.where == b.where; }
    };

    // Where the table a type is was made, where Luau says: a metatable's
    // own table's.
    std::optional<Identity> identityOf(Luau::TypeId type)
    {
        type = Luau::follow(type);
        if (const Luau::MetatableType* meta = Luau::get<Luau::MetatableType>(type))
        {
            type = Luau::follow(meta->table);
        }
        const Luau::TableType* table = Luau::get<Luau::TableType>(type);
        if (!table || table->definitionModuleName.empty())
        {
            return std::nullopt;
        }
        return Identity{ table->definitionModuleName, table->definitionLocation };
    }

    // A module the front end holds, as a search reads it: by its name to
    // Luau, its parse and its types.
    struct Held
    {
        std::string               name;
        const Luau::SourceModule* source = nullptr;
        const Luau::Module*       module = nullptr;
    };

    // Where the type a reference names was declared: one a module exports,
    // through the local the script required the module as; else one of
    // the module's own, in the scope it is used in or one around it --
    // not the definitions', in the scope around the module's.
    std::optional<Identity> typeIdentity(const Luau::FrontendModuleResolver& resolver, const Held& in, const Luau::AstTypeReference& ref)
    {
        const Luau::ScopePtr scope = Luau::findScopeAtPosition(*in.module, ref.location.begin);
        const std::string    name  = ref.name.value;
        if (ref.prefix)
        {
            for (const Luau::Scope* each = scope.get(); each; each = each->parent.get())
            {
                const auto imported = each->importedModules.find(ref.prefix->value);
                if (imported == each->importedModules.end())
                {
                    continue;
                }
                const Luau::ModulePtr required = resolver.getModule(imported->second);
                if (!required)
                {
                    return std::nullopt;
                }
                const auto exported = required->exportedTypeBindings.find(name);
                if (exported == required->exportedTypeBindings.end() || !exported->second.definitionLocation)
                {
                    return std::nullopt;
                }
                return Identity{ imported->second, *exported->second.definitionLocation };
            }
            return std::nullopt;
        }
        for (const Luau::Scope* each = scope.get(); each && each->parent; each = each->parent.get())
        {
            for (const auto* bindings : { &each->privateTypeBindings, &each->exportedTypeBindings })
            {
                if (const auto found = bindings->find(name); found != bindings->end())
                {
                    if (!found->second.definitionLocation)
                    {
                        return std::nullopt;
                    }
                    return Identity{ in.name, *found->second.definitionLocation };
                }
            }
        }
        return std::nullopt;
    }

    // What a position names: a local, by the binding itself; a global, by
    // its name; a field, by its name and the table it is a field of; or a
    // type, by its name and where it was declared.
    struct Target
    {
        enum class Kind : U8
        {
            None,
            Local,
            Global,
            Field,
            Type
        };
        Kind            kind  = Kind::None;
        Luau::AstLocal* local = nullptr;
        Luau::AstName   global;
        // A field's or a type's.
        std::string     name;
        // The table a field belongs to, followed, or null for one whose
        // table has no type; fields of the same name on other tables are
        // other fields.
        Luau::TypeId    table = nullptr;
        // Where a field's table, or a type, was made: what finds it in
        // another module, which sees a copy of the table.
        std::optional<Identity> identity;
    };

    Luau::TypeId tableTypeOf(const Luau::Module& module, Luau::AstExpr* expr)
    {
        const Luau::TypeId* type = module.astTypes.find(expr);
        return type ? Luau::follow(*type) : nullptr;
    }

    Target targetAt(const Luau::FrontendModuleResolver& resolver, const Held& in, Luau::Position at)
    {
        const Luau::Module&       module = *in.module;
        const Luau::SourceModule& source = *in.source;
        Target                    target;
        // A type, where it is used or declared: nothing an expression is.
        const std::vector<Luau::AstNode*> typed = Luau::findAstAncestryOfPosition(source, at, /*includeTypes*/ true);
        for (auto it = typed.rbegin(); it != typed.rend(); ++it)
        {
            if (const Luau::AstTypeReference* ref = (*it)->as<Luau::AstTypeReference>(); ref && ref->nameLocation.containsClosed(at))
            {
                if (std::optional<Identity> declared = typeIdentity(resolver, in, *ref))
                {
                    target.kind     = Target::Kind::Type;
                    target.name     = ref->name.value;
                    target.identity = std::move(declared);
                }
                return target;
            }
            if (const Luau::AstStatTypeAlias* alias = (*it)->as<Luau::AstStatTypeAlias>(); alias && alias->nameLocation.containsClosed(at))
            {
                target.kind     = Target::Kind::Type;
                target.name     = alias->name.value;
                target.identity = Identity{ in.name, alias->location };
                return target;
            }
        }
        Luau::ExprOrLocal found = Luau::findExprOrLocalAtPosition(source, at);
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
                target.name  = index->index.value;
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
                        target.name  = std::string(key->value.data, key->value.size);
                        target.table = tableTypeOf(module, table);
                    }
                }
            }
        }
        if (target.kind == Target::Kind::Field && target.table)
        {
            target.identity = identityOf(target.table);
        }
        return target;
    }

    // Every place a target stands in one module, and where the module
    // binds it.
    struct Uses final : public Luau::AstVisitor
    {
        const Target&                       target;
        const Held&                         in;
        const Luau::FrontendModuleResolver& resolver;
        // Whether this is the module the target was found in: there an
        // expression with no type at all is taken to be the target's.
        bool                                home = true;
        std::vector<ALScriptSpan>           spans;
        std::optional<Luau::Location>       definition;
        bool                                parameter = false;
        bool                                function  = false;

        Uses(const Target& target_in, const Held& in_in, const Luau::FrontendModuleResolver& resolver_in, bool home_in)
        :   target(target_in),
            in(in_in),
            resolver(resolver_in),
            home(home_in)
        {
        }

        bool sameTable(Luau::AstExpr* expr) const
        {
            if (!target.table)
            {
                return true;
            }
            const Luau::TypeId type = tableTypeOf(*in.module, expr);
            if (!type)
            {
                return home;
            }
            return type == target.table || (target.identity && identityOf(type) == target.identity);
        }
        bool isField(Luau::AstExprIndexName* index) const
        {
            return target.kind == Target::Kind::Field && target.name == index->index.value && sameTable(index->expr);
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

        // Types are read too, and lists of them -- what a function
        // returns: a type is named in them, and a local or a field may be,
        // inside a typeof.
        bool visit(Luau::AstType*) override { return true; }
        bool visit(Luau::AstTypePack*) override { return true; }
        bool visit(Luau::AstTypeReference* ref) override
        {
            if (target.kind == Target::Kind::Type && target.name == ref->name.value && typeIdentity(resolver, in, *ref) == target.identity)
            {
                add(ref->nameLocation);
            }
            else if (target.kind == Target::Kind::Local && ref->prefix && ref->prefixLocation && *ref->prefix == target.local->name &&
                     importedAs(*ref))
            {
                add(*ref->prefixLocation);
            }
            return true;
        }
        // Whether a type's prefix is the target, a local a module was
        // required as: the scope the prefix was imported into, the nearest
        // around the type, is the one the local was declared in.
        bool importedAs(const Luau::AstTypeReference& ref) const
        {
            const Luau::ScopePtr declared = Luau::findScopeAtPosition(*in.module, target.local->location.begin);
            for (Luau::ScopePtr each = Luau::findScopeAtPosition(*in.module, ref.location.begin); each; each = each->parent)
            {
                if (each->importedModules.count(ref.prefix->value))
                {
                    return each == declared;
                }
            }
            return false;
        }
        bool visit(Luau::AstStatTypeAlias* alias) override
        {
            if (target.kind == Target::Kind::Type && target.name == alias->name.value && target.identity &&
                Identity{ in.name, alias->location } == *target.identity)
            {
                declare(alias->nameLocation);
            }
            return true;
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
                        && std::string_view(key->value.data, key->value.size) == target.name)
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

        // In order, each place once.
        void sort()
        {
            std::sort(spans.begin(), spans.end());
            spans.erase(std::unique(spans.begin(), spans.end()), spans.end());
        }
    };

    // A module's key, as a problem's file has it: its name to Luau less
    // what marks it a module.
    std::string keyOf(const std::string& name)
    {
        return name.rfind(MODULE.data(), 0) == 0 ? name.substr(MODULE.size()) : name;
    }
}

ALLuauNavigation::ALLuauNavigation(ALLuauFrontend& front)
:   mFront(front)
{
}

const Luau::FrontendModuleResolver& ALLuauNavigation::resolverOf(const Luau::Module& module) const
{
    const Luau::FrontendModuleResolver& complete = mFront.frontend->moduleResolverForAutocomplete;
    return complete.getModule(mFront.moduleName).get() == &module ? complete : mFront.frontend->moduleResolver;
}

ALScriptReferences ALLuauNavigation::references(std::string_view source, Luau::Position at)
{
    ALLuauFrontend&           front         = mFront;
    const Luau::ModulePtr     module        = front.queried(source);
    ALScriptReferences        answer;
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module || !module_source->root)
    {
        return answer;
    }
    const Luau::FrontendModuleResolver& resolver = resolverOf(*module);
    const Held                          home{ front.moduleName, module_source, module.get() };
    const Target                        target = targetAt(resolver, home, at);
    if (target.kind == Target::Kind::None)
    {
        return answer;
    }
    // Where it may stand: a local or a global in the script alone; a
    // field of a table made somewhere, or a type, in every module the
    // script requires too, as checked for the script.
    std::vector<Held> searched{ home };
    if (target.identity)
    {
        if (const auto required = front.requiredBy.find(front.moduleName); required != front.requiredBy.end())
        {
            for (const std::string& name : required->second)
            {
                const Luau::SourceModule* parsed  = front.frontend->getSourceModule(name);
                const Luau::ModulePtr     checked = resolver.getModule(name);
                if (parsed && parsed->root && checked)
                {
                    searched.push_back({ name, parsed, checked.get() });
                }
            }
        }
    }
    std::vector<Uses> found;
    found.reserve(searched.size());
    for (const Held& in : searched)
    {
        found.emplace_back(target, in, resolver, &in == &searched.front());
        in.source->root->visit(&found.back());
        found.back().sort();
    }

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
            answer.name = target.name;
            break;
    }
    // Declared where its table or type was made, where that binds it; else
    // where the script does; else in the first module that does.
    const Uses* declaring = nullptr;
    for (const Uses& uses : found)
    {
        if (uses.definition && target.identity && uses.in.name == target.identity->module)
        {
            declaring = &uses;
        }
    }
    for (const Uses& uses : found)
    {
        declaring = declaring ? declaring : uses.definition ? &uses : nullptr;
    }
    bool parameter = false;
    bool function  = false;
    for (const Uses& uses : found)
    {
        parameter |= uses.parameter;
        function |= uses.function;
    }
    const std::optional<Luau::TypeId> type = Luau::findTypeAtPosition(*module, *module_source, at);
    if (target.kind == Target::Kind::Type)
    {
        answer.kind = ALScriptSymbolKind::Type;
    }
    else if (parameter)
    {
        answer.kind = ALScriptSymbolKind::Parameter;
    }
    else if (function || (type && functionOf(*type)))
    {
        answer.kind = ALScriptSymbolKind::Function;
    }
    else
    {
        answer.kind = target.kind == Target::Kind::Field ? ALScriptSymbolKind::Field : ALScriptSymbolKind::Variable;
    }
    if (declaring)
    {
        answer.hasDefinition  = true;
        answer.definition     = spanOf(*declaring->definition);
        answer.definitionFile = &declaring->in == &searched.front() ? std::string() : keyOf(declaring->in.name);
        answer.renamable      = true;
    }
    answer.references = std::move(found.front().spans);
    for (size_t i = 1; i < found.size(); ++i)
    {
        const std::string file = keyOf(found[i].in.name);
        for (const ALScriptSpan& span : found[i].spans)
        {
            answer.elsewhere.push_back({ file, span });
        }
    }
    return answer;
}

std::optional<ALLuauNavigation::Declared> ALLuauNavigation::declaredAt(const Luau::Module& module, const Luau::SourceModule& source,
                                                                         Luau::Position at) const
{
    if (!source.root)
    {
        return std::nullopt;
    }
    const Luau::FrontendModuleResolver& resolver = resolverOf(module);
    const Held                          home{ mFront.moduleName, &source, &module };
    const Target                        target = targetAt(resolver, home, at);
    if (target.kind != Target::Kind::Field && target.kind != Target::Kind::Type)
    {
        return std::nullopt;
    }
    // Looked for where its table or type was made, where the front end
    // holds that module -- the script, or one it requires -- and then in
    // the script; one made where it holds none, the definitions, has
    // nothing to go to.
    std::vector<Held> searched;
    if (target.identity && target.identity->module != home.name)
    {
        const std::string& name     = target.identity->module;
        const auto         required = mFront.requiredBy.find(mFront.moduleName);
        if (required == mFront.requiredBy.end() || std::find(required->second.begin(), required->second.end(), name) == required->second.end())
        {
            return std::nullopt;
        }
        const Luau::SourceModule* parsed  = mFront.frontend->getSourceModule(name);
        const Luau::ModulePtr     checked = resolver.getModule(name);
        if (parsed && parsed->root && checked)
        {
            searched.push_back({ name, parsed, checked.get() });
        }
    }
    searched.push_back(home);
    for (const Held& in : searched)
    {
        Uses uses(target, in, resolver, &in == &searched.back());
        in.source->root->visit(&uses);
        if (uses.definition)
        {
            return Declared{ &in == &searched.back() ? std::string() : keyOf(in.name), *uses.definition };
        }
    }
    return std::nullopt;
}
