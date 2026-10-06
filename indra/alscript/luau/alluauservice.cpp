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

#include "alluaucompletion.h"
#include "alluaufragment.h"
#include "alluaufrontend.h"
#include "alluautypes.h"

#include "alscriptfixes.h"
#include "alscriptlintpass.h"
#include "alselenefilters.h"

#include "almessagemap.h"

#include "llsdjson.h"
#include "llstl.h"

#include "Luau/AstQuery.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Cancellation.h"
#include "Luau/Error.h"
#include "Luau/Frontend.h"
#include "Luau/Linter.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Parser.h"
#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Luau/TypeArena.h"
#include "Luau/TypePack.h"
#include "alscriptlexicon.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <limits>
#include <mutex>
#include <set>

namespace
{
    // The package the definitions are loaded as. luau-lsp names it this
    // way, and secondlife.docs.json keys its entries "@sl-slua/global/...",
    // so the two line up when hover documentation arrives.
    const char* const DEFINITIONS_PACKAGE = "@sl-slua";

    using ALLuauTypes::functionOf;
    using ALLuauTypes::positionOf;
    using ALLuauTypes::typeText;

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

    // The property of a table or a class nearest a name that is not one,
    // as ALScriptFixes::nearest has it: none where several are as near.
    std::string nearestProperty(Luau::TypeId type, const std::string& key)
    {
        type = Luau::follow(type);
        std::vector<std::string> names;
        if (const Luau::TableType* table = Luau::getTableType(type))
        {
            for (const auto& [name, property] : table->props)
            {
                names.push_back(name);
            }
        }
        else if (const Luau::ExternType* cls = Luau::get<Luau::ExternType>(type))
        {
            for (const auto& [name, property] : cls->props)
            {
                names.push_back(name);
            }
        }
        return ALScriptFixes::nearest(key, names);
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
        // A handler put on an event -- `LLEvents:on("touch_start", function
        // ... end)`, or `:once` -- listed as the event, as LSL's are; and a
        // timer's, `LLTimers:every(5, function ... end)` or `:once`, as a
        // timer, with how often where the script says it plainly.
        bool visit(Luau::AstExprCall* call) override
        {
            Luau::AstExprIndexName* index = call->func->as<Luau::AstExprIndexName>();
            Luau::AstExprGlobal*    table = index ? index->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (!table || !call->self || call->args.size < 2)
            {
                return true;
            }
            const std::string    on       = index->index.value;
            const bool           events   = strcmp(table->name.value, "LLEvents") == 0 && (on == "on" || on == "once");
            const bool           timers   = strcmp(table->name.value, "LLTimers") == 0 && (on == "every" || on == "once");
            Luau::AstExprFunction* handler = call->args.data[call->args.size - 1]->as<Luau::AstExprFunction>();
            if ((!events && !timers) || !handler)
            {
                return true;
            }
            Luau::AstExpr* first = call->args.data[0];
            std::string    name;
            if (events)
            {
                Luau::AstExprConstantString* event = first->as<Luau::AstExprConstantString>();
                if (!event)
                {
                    return true;
                }
                name.assign(event->value.data, event->value.size);
            }
            else
            {
                name = "timer " + on;
                if (Luau::AstExprConstantNumber* every = first->as<Luau::AstExprConstantNumber>())
                {
                    name += " " + llformat("%g", every->value);
                }
            }
            entry(name, first->location, call->location, ALScriptSymbolKind::Event, typeAt(handler));
            inside(handler->body);
            return false;
        }
    };
}

namespace
{
    // selene's comments in a script (ALSeleneFilters), placed: for the
    // whole file, or for the statement each stands beside.
    struct SeleneFilters
    {
        struct Scoped
        {
            Luau::Location          where;
            ALSeleneFilters::Action action;
            // Luau's lints, and the studio's own (ALScriptLintPass's bits).
            uint64_t                lints;
            uint64_t                slLints;
        };
        struct File
        {
            ALSeleneFilters::Action action;
            uint64_t                lints;
            uint64_t                slLints;
        };
        std::vector<File>   file;
        std::vector<Scoped> scoped;

        // What they say of a lint at a place: the innermost statement's
        // about it, else the file's last.
        std::optional<ALSeleneFilters::Action> about(Luau::LintWarning::Code code, const Luau::Location& at) const
        {
            return about(1ull << code, 0, at);
        }
        // Of one of the studio's own, by its bit.
        std::optional<ALSeleneFilters::Action> aboutSl(uint64_t bit, const Luau::Location& at) const { return about(0, bit, at); }

        std::optional<ALSeleneFilters::Action> about(uint64_t bit, uint64_t sl_bit, const Luau::Location& at) const
        {
            const Scoped* best = nullptr;
            for (const Scoped& one : scoped)
            {
                if (((one.lints & bit) || (one.slLints & sl_bit)) && one.where.containsClosed(at.begin) &&
                    (!best || best->where.encloses(one.where)))
                {
                    best = &one;
                }
            }
            if (best)
            {
                return best->action;
            }
            std::optional<ALSeleneFilters::Action> out;
            for (const File& one : file)
            {
                if ((one.lints & bit) || (one.slLints & sl_bit))
                {
                    out = one.action;
                }
            }
            return out;
        }
    };

    // A script's statements, blocks aside, to find the one a comment
    // stands beside.
    struct StatementsOf final : public Luau::AstVisitor
    {
        std::vector<Luau::Location> found;
        bool                        visit(Luau::AstStat* node) override
        {
            if (!node->is<Luau::AstStatBlock>())
            {
                found.push_back(node->location);
            }
            return true;
        }
    };

    SeleneFilters seleneFiltersOf(std::string_view source, const Luau::SourceModule* module)
    {
        SeleneFilters out;
        if (!module || !module->root || module->commentLocations.empty() || source.find("selene") == std::string_view::npos)
        {
            return out;
        }
        std::vector<size_t> starts{ 0 };
        for (size_t at = source.find('\n'); at != std::string_view::npos; at = source.find('\n', at + 1))
        {
            starts.push_back(at + 1);
        }
        const auto offset = [&](const Luau::Position& p) {
            return p.line < starts.size() ? llmin(source.size(), starts[p.line] + p.column) : source.size();
        };
        // A whole file's must come before any code, as selene has it.
        const Luau::Position              first_code = module->root->body.size > 0 ? module->root->body.data[0]->location.begin
                                                                                    : Luau::Position(std::numeric_limits<unsigned int>::max(), 0);
        std::optional<std::vector<Luau::Location>> statements;
        for (const Luau::Comment& comment : module->commentLocations)
        {
            if (comment.type != Luau::Lexeme::Comment)
            {
                continue;
            }
            const size_t                                  from      = offset(comment.location.begin);
            const std::optional<ALSeleneFilters::Directive> directive = ALSeleneFilters::read(source.substr(from, offset(comment.location.end) - from));
            const uint64_t                                lints     = directive ? ALSeleneFilters::luauLints(*directive) : 0;
            const uint64_t                                sl_lints  = directive ? ALSeleneFilters::slLints(*directive) : 0;
            if (!lints && !sl_lints)
            {
                continue;
            }
            if (directive->file)
            {
                if (comment.location.end <= first_code)
                {
                    out.file.push_back({ directive->action, lints, sl_lints });
                }
                continue;
            }
            if (!statements)
            {
                StatementsOf visitor;
                module->root->visit(&visitor);
                statements = std::move(visitor.found);
            }
            // Beside it: the statement its line ends with, before it -- the
            // outermost -- else the first to begin after it, the outermost
            // of those that begin there.
            const Luau::Location* beside = nullptr;
            for (const Luau::Location& where : *statements)
            {
                if (where.end.line == comment.location.begin.line && where.end <= comment.location.begin &&
                    (!beside || where.begin < beside->begin))
                {
                    beside = &where;
                }
            }
            if (!beside)
            {
                for (const Luau::Location& where : *statements)
                {
                    if (comment.location.end <= where.begin &&
                        (!beside || where.begin < beside->begin || (where.begin == beside->begin && beside->end < where.end)))
                    {
                        beside = &where;
                    }
                }
            }
            if (beside)
            {
                out.scoped.push_back({ *beside, directive->action, lints, sl_lints });
            }
        }
        return out;
    }

    // Every name, by what the check found it to be. Types are visited too,
    // which the visitor does not do on its own.
    struct Semantics final : public Luau::AstVisitor
    {
        const Luau::Module&                module;
        const Luau::Scope*                 globals;
        std::vector<ALScriptSemanticToken> out;
        std::set<Luau::AstLocal*>          parameters;
        std::set<Luau::AstLocal*>          constants;
        std::set<Luau::AstLocal*>          functions;
        // The names of function statements, marked as they are met so
        // that the visit of the name as an expression knows it is bound
        // there.
        std::set<std::pair<unsigned, unsigned>> declared;

        Semantics(const Luau::Module& module_in, const Luau::Scope* globals_in)
        :   module(module_in),
            globals(globals_in)
        {
        }

        void add(const Luau::Location& where, ALScriptSymbolKind kind, U8 modifiers)
        {
            if (where.begin == where.end)
            {
                return;
            }
            ALScriptSemanticToken token;
            token.span      = spanOf(where);
            token.kind      = kind;
            token.modifiers = modifiers;
            out.push_back(std::move(token));
        }
        bool callable(Luau::AstExpr* expr) const
        {
            const Luau::TypeId* type = module.astTypes.find(expr);
            return type && functionOf(*type) != nullptr;
        }
        U8 declaredAt(const Luau::Location& where) const
        {
            return declared.count({ where.begin.line, where.begin.column }) ? ALScriptSemanticToken::Declaration : 0;
        }

        bool visit(Luau::AstType*) override { return true; }
        bool visit(Luau::AstTypePack*) override { return true; }

        bool visit(Luau::AstExprFunction* function) override
        {
            if (function->self)
            {
                parameters.insert(function->self);
                add(function->self->location, ALScriptSymbolKind::Parameter, ALScriptSemanticToken::Declaration);
            }
            for (Luau::AstLocal* arg : function->args)
            {
                parameters.insert(arg);
                add(arg->location, ALScriptSymbolKind::Parameter, ALScriptSemanticToken::Declaration);
            }
            return true;
        }
        bool visit(Luau::AstStatLocal* stat) override
        {
            for (size_t i = 0; i < stat->vars.size; ++i)
            {
                Luau::AstLocal* local = stat->vars.data[i];
                Luau::AstExpr*  value = i < stat->values.size ? stat->values.data[i] : nullptr;
                const bool      fn    = value && (value->is<Luau::AstExprFunction>() || callable(value));
                U8              mods  = ALScriptSemanticToken::Declaration;
                if (local->isConst)
                {
                    mods |= ALScriptSemanticToken::ReadOnly;
                    constants.insert(local);
                }
                if (fn)
                {
                    functions.insert(local);
                }
                add(local->location, fn ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Variable, mods);
            }
            return true;
        }
        bool visit(Luau::AstStatLocalFunction* stat) override
        {
            functions.insert(stat->name);
            U8 mods = ALScriptSemanticToken::Declaration;
            if (stat->isConst)
            {
                mods |= ALScriptSemanticToken::ReadOnly;
                constants.insert(stat->name);
            }
            add(stat->name->location, ALScriptSymbolKind::Function, mods);
            return true;
        }
        bool visit(Luau::AstStatFunction* stat) override
        {
            Luau::Location where = stat->name->location;
            if (Luau::AstExprIndexName* index = stat->name->as<Luau::AstExprIndexName>())
            {
                where = index->indexLocation;
            }
            declared.insert({ where.begin.line, where.begin.column });
            return true;
        }
        bool visit(Luau::AstStatFor* stat) override
        {
            add(stat->var->location, ALScriptSymbolKind::Variable, ALScriptSemanticToken::Declaration);
            return true;
        }
        bool visit(Luau::AstStatForIn* stat) override
        {
            for (Luau::AstLocal* local : stat->vars)
            {
                add(local->location, ALScriptSymbolKind::Variable, ALScriptSemanticToken::Declaration);
            }
            return true;
        }
        bool visit(Luau::AstExprLocal* expr) override
        {
            ALScriptSymbolKind kind = ALScriptSymbolKind::Variable;
            if (parameters.count(expr->local))
            {
                kind = ALScriptSymbolKind::Parameter;
            }
            else if (functions.count(expr->local) || callable(expr))
            {
                kind = ALScriptSymbolKind::Function;
            }
            add(expr->location, kind, constants.count(expr->local) ? ALScriptSemanticToken::ReadOnly : 0);
            return true;
        }
        bool visit(Luau::AstExprGlobal* expr) override
        {
            U8 mods = ALScriptSemanticToken::Global | declaredAt(expr->location);
            if (globals)
            {
                const auto bound = globals->bindings.find(Luau::Symbol(expr->name));
                if (bound != globals->bindings.end())
                {
                    mods |= ALScriptSemanticToken::Builtin;
                    if (bound->second.deprecated)
                    {
                        mods |= ALScriptSemanticToken::Deprecated;
                    }
                }
            }
            add(expr->location, callable(expr) ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Variable, mods);
            return true;
        }
        bool visit(Luau::AstExprIndexName* expr) override
        {
            U8 mods = declaredAt(expr->indexLocation);
            if (const Luau::TypeId* table = module.astTypes.find(expr->expr))
            {
                const Luau::TypeId     type = Luau::follow(*table);
                const Luau::Property*  prop = nullptr;
                if (const Luau::TableType* t = Luau::getTableType(type))
                {
                    const auto it = t->props.find(expr->index.value);
                    prop          = it == t->props.end() ? nullptr : &it->second;
                }
                else if (const Luau::ExternType* cls = Luau::get<Luau::ExternType>(type))
                {
                    const auto it = cls->props.find(expr->index.value);
                    prop          = it == cls->props.end() ? nullptr : &it->second;
                }
                if (type->documentationSymbol)
                {
                    mods |= ALScriptSemanticToken::Builtin;
                }
                if (prop && prop->deprecated)
                {
                    mods |= ALScriptSemanticToken::Deprecated;
                }
            }
            add(expr->indexLocation, callable(expr) ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Field, mods);
            return true;
        }
        bool visit(Luau::AstExprTable* table) override
        {
            for (const Luau::AstExprTable::Item& item : table->items)
            {
                if (item.kind == Luau::AstExprTable::Item::Kind::Record && item.key)
                {
                    const bool fn = item.value && (item.value->is<Luau::AstExprFunction>() || callable(item.value));
                    add(item.key->location, fn ? ALScriptSymbolKind::Function : ALScriptSymbolKind::Field, ALScriptSemanticToken::Declaration);
                }
            }
            return true;
        }
        bool visit(Luau::AstTypeReference* type) override
        {
            if (type->prefixLocation)
            {
                add(*type->prefixLocation, ALScriptSymbolKind::Module, 0);
            }
            add(type->nameLocation, ALScriptSymbolKind::Type, 0);
            return true;
        }
        bool visit(Luau::AstStatTypeAlias* stat) override
        {
            add(stat->nameLocation, ALScriptSymbolKind::Type, ALScriptSemanticToken::Declaration);
            return true;
        }
    };

    // What the editor may show beside the text.
    struct Hints final : public Luau::AstVisitor
    {
        const Luau::Module&            module;
        const bool                     parameters;
        const bool                     types;
        std::vector<ALScriptInlayHint> out;

        // Where only one line's are wanted: what the refactors at a place
        // read, which is not worth every line's.
        std::optional<unsigned>        onlyLine;
        // Each local's type, by the local: every scope's bindings read
        // once, where finding each local's scope from its place walked the
        // scopes again for every one.
        boost::unordered_flat_map<const Luau::AstLocal*, Luau::TypeId> localTypes;
        bool                                                           localTypesRead = false;

        Hints(const Luau::Module& module_in, bool parameters_in, bool types_in)
        :   module(module_in),
            parameters(parameters_in),
            types(types_in)
        {
        }

        std::optional<Luau::TypeId> typeOf(const Luau::AstLocal* local)
        {
            if (!localTypesRead)
            {
                localTypesRead = true;
                for (const auto& [where, scope] : module.scopes)
                {
                    for (const auto& [symbol, binding] : scope->bindings)
                    {
                        if (symbol.local)
                        {
                            localTypes.emplace(symbol.local, binding.typeId);
                        }
                    }
                }
            }
            const auto found = localTypes.find(local);
            return found != localTypes.end() ? std::optional<Luau::TypeId>(found->second) : std::nullopt;
        }

        // Only down into what holds the one line, where one is asked for.
        bool visit(Luau::AstNode* node) override
        {
            return !onlyLine || (node->location.begin.line <= *onlyLine && *onlyLine <= node->location.end.line);
        }

        void add(const Luau::Position& at, ALScriptInlayHint::Kind kind, std::string text, bool writable = false)
        {
            ALScriptInlayHint hint;
            hint.line     = static_cast<S32>(at.line);
            hint.column   = static_cast<S32>(at.column);
            hint.kind     = kind;
            hint.text     = std::move(text);
            hint.writable = writable;
            out.push_back(std::move(hint));
        }

        bool visit(Luau::AstExprCall* call) override
        {
            if (!parameters)
            {
                return true;
            }
            const Luau::TypeId*       callee   = module.astTypes.find(call->func);
            const Luau::FunctionType* function = callee ? functionOf(*callee) : nullptr;
            if (!function)
            {
                return true;
            }
            // A method's first parameter is the object before the colon.
            const size_t offset = call->self ? 1 : 0;
            for (size_t i = 0; i < call->args.size; ++i)
            {
                const size_t p = i + offset;
                if (p >= function->argNames.size())
                {
                    break;
                }
                Luau::AstExpr* arg = call->args.data[i];
                if (arg->is<Luau::AstExprVarargs>())
                {
                    break;
                }
                if (!function->argNames[p])
                {
                    continue;
                }
                const std::string& name = function->argNames[p]->name;
                // Nothing where the argument says it already, and nothing
                // for a name that says nothing.
                if (name.empty() || name == "_" || name == "self" || nameOf(arg) == name)
                {
                    continue;
                }
                add(arg->location.begin, ALScriptInlayHint::Kind::Parameter, name + ":");
            }
            return true;
        }
        bool visit(Luau::AstStatLocal* stat) override
        {
            if (!types)
            {
                return true;
            }
            for (size_t i = 0; i < stat->vars.size; ++i)
            {
                Luau::AstLocal* local = stat->vars.data[i];
                Luau::AstExpr*  value = i < stat->values.size ? stat->values.data[i] : nullptr;
                // A name annotated says its type; a function's is its
                // signature, which is the line itself.
                if (local->annotation || !value || value->is<Luau::AstExprFunction>() || (onlyLine && local->location.end.line != *onlyLine))
                {
                    continue;
                }
                std::optional<Luau::TypeId> type = typeOf(local);
                if (!type)
                {
                    if (const Luau::TypeId* of_value = module.astTypes.find(value))
                    {
                        type = *of_value;
                    }
                }
                if (!type)
                {
                    continue;
                }
                Luau::ToStringOptions       options;
                options.maxTableLength = 3;
                options.maxTypeLength  = 40;
                const Luau::ToStringResult said = Luau::toStringDetailed(*type, options);
                const std::string&         text = said.name;
                // A glance, not a listing; and nothing where there is
                // nothing to know.
                if (text.empty() || text == "any" || text == "nil" || text == "unknown" || text == "*error-type*"
                    || text.size() > 40 || text.find('\n') != std::string::npos)
                {
                    continue;
                }
                // Written in only as Luau would read it back: whole, and
                // with no name of Luau's own making -- a free type's
                // quote, a blocked or an error type's stars.
                const bool writable = !said.invalid && !said.error && !said.cycle && !said.truncated &&
                                      text.find_first_of("*'") == std::string::npos && text.find("...") == std::string::npos;
                add(local->location.end, ALScriptInlayHint::Kind::Type, ": " + text, writable);
            }
            return true;
        }
    };

    // The script's statements visited, those lying wholly within lines
    // nobody reads -- a module the preprocessor put ahead of it -- passed
    // over rather than gone down into.
    void visitRead(Luau::AstStatBlock* root, Luau::AstVisitor& visitor, const std::vector<std::pair<S32, S32>>& passed)
    {
        if (passed.empty())
        {
            root->visit(&visitor);
            return;
        }
        for (Luau::AstStat* stat : root->body)
        {
            if (!ALSourceMap::within(passed, static_cast<S32>(stat->location.begin.line), static_cast<S32>(stat->location.end.line)))
            {
                stat->visit(&visitor);
            }
        }
    }
}

ALLuauService::ALLuauService()
:   mFrontend(std::make_unique<ALLuauFrontend>())
,   mFragment(std::make_unique<ALLuauFragment>(*mFrontend))
,   mCompletion(std::make_unique<ALLuauCompletion>(*mFrontend, *mFragment))
{
    setUpProcess();
    mFrontend->frontend = ALLuauFrontend::plainFrontend(mFrontend->files, mFrontend->configs, mFrontend->solver);
    Luau::freeze(mFrontend->frontend->globals.globalTypes);
    Luau::freeze(mFrontend->frontend->globalsForAutocomplete.globalTypes);
}

ALLuauService::~ALLuauService() = default;

// static
void ALLuauService::setUpProcess()
{
    static std::once_flag once;
    std::call_once(once, []() {
        // The upstream flags SLua's semantics depend on, slua's
        // SLUA_REQUIRED_FFLAGS, which are the ones the grid runs with: its
        // integer type and library among them, which the grid's
        // definitions name.
        Luau::setRequiredSLuaFlags();
        // A type in a message is a glance, not a listing: `ll` has
        // hundreds of fields, and an error naming it must not print them
        // all.
        FInt::LuauTableTypeMaximumStringifierLength.value = 8;
    });
}

bool ALLuauService::loadDefinitions(std::string_view source, std::string& error)
{
    // The same definitions again -- a region change that changed none of
    // them -- are the ones in hand: nothing is checked twice over, and
    // what was checked of the scripts stands.
    if (mFrontend->definitions && source == mFrontend->definitionsSource)
    {
        error.clear();
        return true;
    }
    std::unique_ptr<Luau::Frontend> frontend = ALLuauFrontend::plainFrontend(mFrontend->files, mFrontend->configs, mFrontend->solver);
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
    mFrontend->frontend          = std::move(frontend);
    mFrontend->definitions       = true;
    mFrontend->definitionsSource = std::string(source);
    error.clear();
    return true;
}

bool ALLuauService::setNewSolver(bool use, std::string& error)
{
    error.clear();
    const Luau::SolverMode solver = use ? Luau::SolverMode::New : Luau::SolverMode::Old;
    if (solver == mFrontend->solver)
    {
        return true;
    }
    mFrontend->solver = solver;
    mFrontend->remode();
    const std::string source = mFrontend->definitionsSource;
    // Loaded again whatever they are: into a front end for this solver.
    mFrontend->definitions = false;
    if (!source.empty() && loadDefinitions(source, error))
    {
        return true;
    }
    // Nothing to load, or it did not load for this solver: Luau's own
    // globals alone, in a front end for the solver asked for.
    mFrontend->frontend    = ALLuauFrontend::plainFrontend(mFrontend->files, mFrontend->configs, mFrontend->solver);
    Luau::freeze(mFrontend->frontend->globals.globalTypes);
    Luau::freeze(mFrontend->frontend->globalsForAutocomplete.globalTypes);
    mFrontend->definitions = false;
    return source.empty();
}

bool ALLuauService::newSolver() const
{
    return mFrontend->solver == Luau::SolverMode::New;
}

void ALLuauService::setTimeLimit(double seconds)
{
    mFrontend->timeLimit = std::max(0.0, seconds);
}

// static
ALLuauService::Stop ALLuauService::newStop()
{
    return std::make_shared<Luau::FrontendCancellationToken>();
}

// static
void ALLuauService::cancel(const Stop& stop)
{
    if (stop)
    {
        stop->cancel();
    }
}

void ALLuauService::setStop(Stop stop)
{
    mFrontend->stop = std::move(stop);
}

bool ALLuauService::stopped() const
{
    return mFrontend->wasStopped;
}

void ALLuauService::setFragments(bool use)
{
    mFrontend->useFragments = use;
}

bool ALLuauService::fragments() const
{
    return mFrontend->useFragments;
}

size_t ALLuauService::typeChecks() const
{
    return mFrontend->checks;
}

size_t ALLuauService::fragmentsChecked() const
{
    return mFrontend->fragments;
}

size_t ALLuauService::modulesChecked() const
{
    return mFrontend->frontend->stats.filesStrict + mFrontend->frontend->stats.filesNonstrict;
}

bool ALLuauService::hasDefinitions() const
{
    return mFrontend->definitions;
}

bool ALLuauService::loadDocs(std::string_view json, std::string& error)
{
    // The same documentation again is not parsed again: by its length and
    // its hash, rather than half a megabyte kept to compare with.
    const size_t hash = std::hash<std::string_view>()(json);
    if (!mFrontend->docs.empty() && mFrontend->docsHash == std::make_pair(json.size(), hash))
    {
        error.clear();
        return true;
    }
    LLSD parsed;
    if (!LlsdFromJsonString(json, parsed, &error) || !parsed.isMap())
    {
        if (error.empty())
        {
            error = "the docs are not a JSON object";
        }
        return false;
    }
    mFrontend->docs.clear();
    for (LLSD::map_const_iterator it = parsed.beginMap(); it != parsed.endMap(); ++it)
    {
        ALLuauFrontend::Doc doc;
        doc.documentation = withoutBreaks(it->second.get("documentation").asString());
        doc.link          = it->second.get("learn_more_link").asString();
        mFrontend->docs.emplace(it->first, std::move(doc));
    }
    mFrontend->docsHash = std::make_pair(json.size(), hash);
    error.clear();
    return true;
}

bool ALLuauService::hasDocs() const
{
    return !mFrontend->docs.empty();
}

void ALLuauService::setConfig(const ALLuauConfig& config)
{
    ALLuauFrontend&           front = *mFrontend;
    std::optional<Luau::Mode> asked;
    if (!config.mode.empty())
    {
        asked = config.mode == "strict" ? Luau::Mode::Strict : config.mode == "nocheck" ? Luau::Mode::NoCheck : Luau::Mode::Nonstrict;
    }
    const Luau::Mode mode              = front.checkedIn(asked);
    front.askedModes[front.moduleName] = asked;
    // The script asked about now's own, told before every question whether
    // or not anything changed: the same configuration leaves what was
    // checked as it is, and another script's is its own.
    front.slLints      = config.slLints;
    front.slFatalLints = config.slFatalLints;
    Luau::Config& own = front.configs.configs[front.moduleName];
    if (own.mode == mode && own.enabledLint.warningMask == config.lints && own.fatalLint.warningMask == config.fatalLints &&
        own.lintErrors == config.lintErrors && own.globals == config.globals)
    {
        return;
    }
    own.mode                    = mode;
    own.enabledLint.warningMask = config.lints;
    own.fatalLint.warningMask   = config.fatalLints;
    own.lintErrors              = config.lintErrors;
    own.globals                 = config.globals;
    // The globals are bound into the environment as the script is
    // checked; a change to them is a change to the script.
    front.frontend->markDirty(front.moduleName);
}

void ALLuauService::setPassedOver(std::vector<std::pair<S32, S32>> lines)
{
    mFrontend->passedOver = std::move(lines);
}

void ALLuauService::setModules(const Modules& modules)
{
    ALLuauFrontend& front = *mFrontend;
    // Each module's text, told as it changes: a module unchanged stays
    // checked.
    std::vector<std::string> names;
    names.reserve(modules.modules.size());
    for (const Module& module : modules.modules)
    {
        const std::string name = ALLuauFrontend::moduleOf(module.key);
        std::string&      text = front.files.texts[name];
        if (text != module.text)
        {
            text = module.text;
            front.frontend->markDirty(name);
        }
        names.push_back(name);
    }
    // Which require is which, as this script and its modules say now; a
    // change of it is a change to the one that requires.
    const auto requirer = [&front](const std::string& from) { return from.empty() ? front.moduleName : ALLuauFrontend::moduleOf(from); };
    boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> now;
    for (const Require& require : modules.reaches)
    {
        std::string key = requirer(require.from);
        key += '\x1f';
        key += require.name;
        now[key] = ALLuauFrontend::moduleOf(require.key);
    }
    for (const auto& [key, module] : now)
    {
        const auto was = front.files.leadsTo.find(key);
        if (was == front.files.leadsTo.end() || was->second != module)
        {
            front.files.leadsTo[key] = module;
            front.frontend->markDirty(key.substr(0, key.find('\x1f')));
        }
    }
    // The script's own that it no longer says.
    const std::string own = front.moduleName + '\x1f';
    for (auto it = front.files.leadsTo.begin(); it != front.files.leadsTo.end();)
    {
        if (it->first.compare(0, own.size(), own) == 0 && !now.count(it->first))
        {
            front.frontend->markDirty(front.moduleName);
            it = front.files.leadsTo.erase(it);
        }
        else
        {
            ++it;
        }
    }
    front.requiredBy[front.moduleName] = std::move(names);
}

void ALLuauService::setDocument(std::string_view id)
{
    ALLuauFrontend&   front = *mFrontend;
    const std::string name  = id.empty() ? std::string(ALLuauFrontend::SCRIPT_MODULE) : std::string(ALLuauFrontend::SCRIPT_MODULE) + ":" + std::string(id);
    front.moduleName        = name;
    if (id.empty())
    {
        return;
    }
    // The one asked of last first; past a few, the one asked of longest
    // ago let go of, its module, text and configuration with it.
    auto& kept = front.kept;
    if (const auto at = std::find(kept.begin(), kept.end(), name); at != kept.end())
    {
        std::rotate(kept.begin(), at, at + 1);
        return;
    }
    kept.insert(kept.begin(), name);
    while (kept.size() > ALLuauFrontend::KEPT)
    {
        const std::string gone = kept.back();
        kept.pop_back();
        std::vector<std::string> clear{ gone };
        front.files.texts.erase(gone);
        front.baseTexts.erase(gone);
        front.configs.configs.erase(gone);
        front.askedModes.erase(gone);
        // Its modules too, where no script kept requires them.
        if (const auto used = front.requiredBy.find(gone); used != front.requiredBy.end())
        {
            const std::vector<std::string> modules = std::move(used->second);
            front.requiredBy.erase(used);
            for (const std::string& module : modules)
            {
                const bool still = std::any_of(front.requiredBy.begin(), front.requiredBy.end(), [&module](const auto& other) {
                    return std::find(other.second.begin(), other.second.end(), module) != other.second.end();
                });
                if (!still)
                {
                    clear.push_back(module);
                    front.files.texts.erase(module);
                }
            }
        }
        front.frontend->clearModules(clear);
    }
}

ALScriptProblems ALLuauService::check(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    front.wasStopped = false;
    front.sync(source);
    // What was found the first time, where the text has not changed since.
    Luau::CheckResult result;
    if (front.stoppedIn(front.checkScript(&result)))
    {
        return ALScriptProblems();
    }

    ALScriptProblems problems;
    if (!result.timeoutHits.empty())
    {
        // Past the time limit: what was found before it, and that there
        // may be more.
        const std::vector<std::string> args{ llformat("%g", front.timeLimit) };
        problems.push_back(problemAt(Luau::Location(Luau::Position(0, 0), Luau::Position(0, 0)), ALScriptProblem::Severity::Warning,
                                     ALScriptProblem::Source::Types, std::string(),
                                     ALScriptProblem::fill("Type checking stopped after [1] seconds; what it found before then is shown", args)));
        problems.back().key  = "LuauCheckTimedOut";
        problems.back().args = args;
    }
    problems.reserve(result.errors.size() + result.lintResult.errors.size() + result.lintResult.warnings.size());
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    const Luau::ModulePtr     module        = front.frontend->moduleResolver.getModule(front.moduleName);
    // The call a count of its arguments is about.
    const auto call_at = [&module_source](const Luau::Location& where) -> Luau::AstExprCall* {
        if (!module_source || !module_source->root)
        {
            return nullptr;
        }
        const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*module_source, where.begin);
        for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
        {
            if (Luau::AstExprCall* call = (*it)->as<Luau::AstExprCall>(); call && call->location.encloses(where))
            {
                return call;
            }
        }
        return nullptr;
    };
    for (const Luau::TypeError& error : result.errors)
    {
        const bool               syntax  = Luau::get_if<Luau::SyntaxError>(&error.data) != nullptr;
        std::string              message = Luau::toString(error);
        std::string              key;
        std::vector<std::string> args;
        Luau::Location           where = error.location;
        // One in a module the script requires is the module's, in its
        // lines: said with its key as the problem's file.
        const bool                 elsewhere = error.moduleName != front.moduleName && error.moduleName.rfind("module:", 0) == 0;
        const Luau::CountMismatch* count     = Luau::get_if<Luau::CountMismatch>(&error.data);
        Luau::AstExprCall* counted = count && count->context == Luau::CountMismatch::Arg && !elsewhere ? call_at(error.location) : nullptr;
        const Luau::TypeId* callee = counted && module ? module->astTypes.find(counted->func) : nullptr;
        const Luau::FunctionType* function = callee ? functionOf(*callee) : nullptr;
        if (counted && function && !counted->self && counted->func->is<Luau::AstExprIndexName>() && count->actual < count->expected &&
            !function->argNames.empty() && function->argNames[0] && function->argNames[0]->name == "self")
        {
            // A method called with a dot, one short: the new solver counts
            // what the old says is a missing self, and it is the same
            // mistake, with the same fix.
            key     = "LuauRequiresSelf";
            message = "This function must be called with self. Did you mean to use a colon instead of a dot?";
            where   = counted->location;
        }
        else if (counted && count->function.empty() && !nameOf(counted->func).empty())
        {
            // A count with no function named, which the new solver says of
            // a function it has no name for: named by what the script
            // calls it, and so said in the words the map has.
            Luau::CountMismatch named = *count;
            named.function            = nameOf(counted->func);
            message                   = Luau::toString(Luau::TypeError(error.location, error.moduleName, named));
            ALMessageMap::Match known;
            if (ALMessageMap::luauError(message, known))
            {
                key  = std::move(known.key);
                args = std::move(known.args);
            }
        }
        else if (const Luau::UnknownProperty* unknown = Luau::get_if<Luau::UnknownProperty>(&error.data))
        {
            // Named by what was written -- `ll`, not the table's fields --
            // with the nearest key there is, which is usually the one meant.
            std::string head;
            if (!elsewhere && module_source && module_source->root)
            {
                const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*module_source, error.location.begin);
                for (auto it = ancestry.rbegin(); it != ancestry.rend() && head.empty(); ++it)
                {
                    if (Luau::AstExprIndexName* index = (*it)->as<Luau::AstExprIndexName>(); index && index->index.value == unknown->key)
                    {
                        head = nameOf(index->expr);
                    }
                }
            }
            if (head.empty())
            {
                head = typeText(unknown->table);
            }
            // Said in this library's own words, so the studio may put
            // them in another language.
            const std::string nearest = nearestProperty(unknown->table, unknown->key);
            key                       = nearest.empty() ? "LuauKeyNotFound" : "LuauKeyNotFoundDidYouMean";
            args                      = { unknown->key, head, nearest };
            message                   = ALScriptProblem::fill(nearest.empty() ? "Key '[1]' not found in [2]" : "Key '[1]' not found in [2]; did you mean '[3]'?", args);
        }
        else
        {
            // One of the commonest shapes, taken apart by its words: a type
            // error's, or the parser's where it says what was meant.
            ALMessageMap::Match known;
            if (ALMessageMap::luauError(message, known))
            {
                key  = std::move(known.key);
                args = std::move(known.args);
            }
        }
        problems.push_back(problemAt(where,
                                     ALScriptProblem::Severity::Error,
                                     syntax ? ALScriptProblem::Source::Parser : ALScriptProblem::Source::Types,
                                     std::string(),
                                     std::move(message)));
        problems.back().key  = key;
        problems.back().args = std::move(args);
        if (elsewhere)
        {
            problems.back().file = error.moduleName.substr(7);
        }
    }
    // What selene's comments say of the lints, where the script was
    // written for selene too: allowed, gone; denied, an error.
    const SeleneFilters selene = seleneFiltersOf(source, module_source);
    // A lint taken apart by its name, where the map knows its words.
    auto lint = [&problems, &selene](const Luau::LintWarning& warning, ALScriptProblem::Severity severity) {
        if (const std::optional<ALSeleneFilters::Action> said = selene.about(warning.code, warning.location))
        {
            if (*said == ALSeleneFilters::Action::Allow)
            {
                return;
            }
            severity = *said == ALSeleneFilters::Action::Deny ? ALScriptProblem::Severity::Error : ALScriptProblem::Severity::Warning;
        }
        const char* name = Luau::LintWarning::getName(warning.code);
        // A --!nolint of one of the studio's own lints, which Luau does not
        // know, is no mistake.
        if (warning.code == Luau::LintWarning::Code_CommentDirective)
        {
            const std::string_view text   = warning.text;
            const size_t           quoted = text.find("unknown lint rule '");
            const size_t           from   = quoted == std::string_view::npos ? quoted : quoted + 19;
            const size_t           to     = from == std::string_view::npos ? from : text.find('\'', from);
            if (to != std::string_view::npos && ALScriptLintPass::bit(text.substr(from, to - from)))
            {
                return;
            }
        }
        problems.push_back(problemAt(warning.location, severity, ALScriptProblem::Source::Lint, name, warning.text));
        ALMessageMap::Match known;
        if (ALMessageMap::luauLint(name, warning.text, known))
        {
            problems.back().key  = std::move(known.key);
            problems.back().args = std::move(known.args);
        }
    };
    for (const Luau::LintWarning& warning : result.lintResult.errors)
    {
        lint(warning, ALScriptProblem::Severity::Error);
    }
    for (const Luau::LintWarning& warning : result.lintResult.warnings)
    {
        lint(warning, ALScriptProblem::Severity::Warning);
    }
    // The studio's own, beside Luau's, as selene's comments say of them too.
    if (module_source)
    {
        ALScriptProblems own;
        const Luau::Config& config = front.configs.getConfig(front.moduleName, {});
        ALScriptLintPass::check(source, *module_source, module.get(), front.slLints, front.slFatalLints, config.lintErrors, own);
        for (ALScriptProblem& problem : own)
        {
            const Luau::Location where(Luau::Position(problem.line, problem.column), Luau::Position(problem.endLine, problem.endColumn));
            if (const std::optional<ALSeleneFilters::Action> said = selene.aboutSl(ALScriptLintPass::bit(problem.code), where))
            {
                if (*said == ALSeleneFilters::Action::Allow)
                {
                    continue;
                }
                problem.severity = *said == ALSeleneFilters::Action::Deny ? ALScriptProblem::Severity::Error : ALScriptProblem::Severity::Warning;
            }
            // Its fixes, made as it read the script, offered only where
            // they will be read.
            if (ALSourceMap::within(front.passedOver, problem.line, std::max(problem.line, problem.endLine)))
            {
                problem.fixes.clear();
            }
            problems.push_back(std::move(problem));
        }
    }
    // A global it does not know changed to the nearest name that is in
    // scope there, where one is near: the script's own locals, its globals
    // and the definitions', up the scopes from the place.
    // The text's lines found once, for every fix offered over it; and a
    // problem wholly in lines nobody reads offered none.
    const ALScriptFixes::Lines lines(source);
    if (const Luau::ModulePtr module = front.frontend->moduleResolver.getModule(front.moduleName))
    {
        for (ALScriptProblem& problem : problems)
        {
            const std::string& key = problem.key;
            if ((key == "LuauUnknownGlobal" || key == "LuauUnknownGlobalAssign" || key == "LuauLintUnknownGlobal" || key == "LuauLintUnknownGlobalAssign") &&
                problem.args.size() == 1 && !ALSourceMap::within(front.passedOver, problem.line, std::max(problem.line, problem.endLine)))
            {
                std::vector<std::string> names;
                const Luau::Position     at(static_cast<unsigned>(problem.line), static_cast<unsigned>(problem.column));
                for (Luau::ScopePtr scope = Luau::findScopeAtPosition(*module, at); scope; scope = scope->parent)
                {
                    for (const auto& [symbol, binding] : scope->bindings)
                    {
                        names.emplace_back(symbol.c_str());
                    }
                }
                ALScriptFixes::offerNames(problem, lines, problem.args[0], names);
            }
        }
    }
    // What would put each right, where its words and its place say.
    ALScriptFixes::attach(problems, lines, true, front.passedOver);
    return problems;
}

void ALLuauService::warm(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (mFrontend->solver == Luau::SolverMode::Old)
    {
        mFrontend->queried(source, /*completion*/ true);
    }
}

// --- what could go here ---------------------------------------------------------

std::vector<ALScriptCompletion> ALLuauService::complete(std::string_view source, S32 line, S32 column)
{
    return mCompletion->complete(source, line, column);
}

// --- what is here --------------------------------------------------------------------

ALScriptHover ALLuauService::hover(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    const Luau::ModulePtr     module        = front.queried(source);
    ALScriptHover           answer;
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module)
    {
        return answer;
    }
    const Luau::Position        at   = positionOf(line, column);
    std::optional<Luau::TypeId> type = Luau::findTypeAtPosition(*module, *module_source, at);

    // What the name is, said before it as an editor would: a local, a
    // constant, a parameter, a field, a global; a function by its
    // signature under its name.
    Luau::ExprOrLocal            found = Luau::findExprOrLocalAtPosition(*module_source, at);
    Luau::AstExpr*               expr  = found.getExpr();
    Luau::AstLocal*              local = found.getLocal();
    if (!local && expr)
    {
        if (Luau::AstExprLocal* use = expr->as<Luau::AstExprLocal>())
        {
            local = use->local;
        }
    }
    if (local)
    {
        // The local's own type, which at its declaration the position
        // does not give: there the innermost expression is the function
        // whose parameter it is.
        if (const Luau::ScopePtr scope = Luau::findScopeAtPosition(*module, at))
        {
            if (const std::optional<Luau::TypeId> bound = scope->lookup(Luau::Symbol(local)))
            {
                type = bound;
            }
        }
    }
    if (!type)
    {
        return answer;
    }
    answer.found = true;
    std::string name = nameOf(expr);
    if (name.empty() && local)
    {
        name = local->name.value;
    }
    std::string kind;
    if (local)
    {
        kind = local->isConst ? "const" : "local";
        for (Luau::AstNode* node : Luau::findAstAncestryOfPosition(*module_source, at))
        {
            if (Luau::AstExprFunction* function = node->as<Luau::AstExprFunction>())
            {
                if (function->self == local)
                {
                    kind = "self";
                }
                for (Luau::AstLocal* arg : function->args)
                {
                    if (arg == local)
                    {
                        kind = "parameter";
                    }
                }
            }
        }
    }
    else if (expr && expr->is<Luau::AstExprGlobal>())
    {
        kind = "global";
    }
    else if (expr && expr->is<Luau::AstExprIndexName>())
    {
        kind = expr->as<Luau::AstExprIndexName>()->op == ':' ? "method" : "field";
    }
    const Luau::TypeId        followed = Luau::follow(*type);
    const Luau::FunctionType* function = functionOf(followed);
    if (function && !name.empty() && Luau::get<Luau::FunctionType>(followed))
    {
        Luau::ToStringOptions options;
        options.functionTypeArguments = true;
        options.maxTableLength        = 8;
        options.maxTypeLength         = 1000;
        answer.label                  = "function " + Luau::toStringNamedFunction(name, *function, options);
    }
    else if (name.empty())
    {
        answer.label = typeText(*type);
    }
    else
    {
        const std::string prefix = kind.empty() ? std::string() : (kind == "local" || kind == "const") ? kind + " " : "(" + kind + ") ";
        answer.label             = prefix + name + ": " + typeText(*type);
    }

    // The whole of a type the label only glances at.
    {
        Luau::ToStringOptions whole;
        whole.functionTypeArguments = true;
        whole.useLineBreaks         = true;
        whole.maxTableLength        = 200;
        whole.maxTypeLength         = 20000;
        const std::string full = Luau::toString(*type, whole);
        Luau::ToStringOptions glance;
        glance.functionTypeArguments = true;
        glance.maxTableLength        = 8;
        glance.maxTypeLength         = 1000;
        if (full != Luau::toString(*type, glance) || full.find('\n') != std::string::npos)
        {
            answer.typeDetail = full;
        }
    }

    // What is wanted here, where it is known and is not what is here.
    if (const std::optional<Luau::TypeId> expected = Luau::findExpectedTypeAtPosition(*module, *module_source, at))
    {
        const std::string text = typeText(*expected);
        if (Luau::follow(*expected) != followed && text != "*error-type*" && text != "unknown" && text != "any" && text != typeText(*type))
        {
            answer.expected = text;
        }
    }

    std::optional<std::string> symbol = Luau::getDocumentationSymbolAtPosition(*module_source, *module, at);
    if (!symbol)
    {
        symbol = followed->documentationSymbol;
    }
    if (const ALLuauFrontend::Doc* doc = front.docFor(symbol))
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

namespace
{
    // The documentation symbol of a field `name` of a type, as the
    // definitions gave it: a table's or an extern type's own, or its
    // parent's; through each part of an intersection -- `vector`, a table
    // that is also called -- and a primitive's metatable's __index, where a
    // string's methods are. Where the definitions gave the field none,
    // what holds it and its name: Luau marks an intersection, and not its
    // table's fields.
    std::optional<std::string> fieldSymbolOf(Luau::TypeId of, const std::string& name, std::optional<std::string> holder = std::nullopt,
                                             int depth = 0)
    {
        of = Luau::follow(of);
        if (depth > 4)
        {
            return std::nullopt;
        }
        if (!holder)
        {
            holder = of->documentationSymbol;
        }
        const auto own = [&](const Luau::Property& field) -> std::optional<std::string> {
            if (field.documentationSymbol)
            {
                return field.documentationSymbol;
            }
            return holder ? std::optional<std::string>(*holder + "." + name) : std::nullopt;
        };
        if (const Luau::TableType* table = Luau::get<Luau::TableType>(of))
        {
            const auto it = table->props.find(name);
            return it != table->props.end() ? own(it->second) : std::nullopt;
        }
        for (const Luau::ExternType* type = Luau::get<Luau::ExternType>(of); type;
             type = type->parent ? Luau::get<Luau::ExternType>(Luau::follow(*type->parent)) : nullptr)
        {
            if (const auto it = type->props.find(name); it != type->props.end())
            {
                return own(it->second);
            }
        }
        if (const Luau::IntersectionType* parts = Luau::get<Luau::IntersectionType>(of))
        {
            for (Luau::TypeId part : parts->parts)
            {
                if (std::optional<std::string> symbol = fieldSymbolOf(part, name, holder, depth + 1))
                {
                    return symbol;
                }
            }
        }
        if (const Luau::PrimitiveType* primitive = Luau::get<Luau::PrimitiveType>(of); primitive && primitive->metatable)
        {
            if (const Luau::TableType* meta = Luau::get<Luau::TableType>(Luau::follow(*primitive->metatable)))
            {
                if (const auto index = meta->props.find("__index"); index != meta->props.end() && index->second.readTy)
                {
                    return fieldSymbolOf(*index->second.readTy, name, std::nullopt, depth + 1);
                }
            }
        }
        return std::nullopt;
    }

    // The same of a field a call calls: `ll.Say`'s, which is the field's and
    // not its function's. The module's types alone, which a fragment has,
    // where Luau's own (getDocumentationSymbolAtPosition) wants the source
    // the module was checked from, and at the callee's start finds what
    // holds the field.
    std::optional<std::string> fieldSymbolOf(const Luau::Module& module, const Luau::AstExpr* callee)
    {
        const Luau::AstExprIndexName* index  = callee->as<Luau::AstExprIndexName>();
        const Luau::TypeId*           parent = index ? module.astTypes.find(index->expr) : nullptr;
        return parent ? fieldSymbolOf(*parent, std::string(index->index.value)) : std::nullopt;
    }

    // What `call` takes, `at` in its brackets, read from `module`'s types:
    // the whole script's, or a fragment's, whose source is none.
    ALScriptSignature signatureOf(const ALLuauFrontend& front, const Luau::Module& module, const Luau::AstExprCall* call,
                                  const Luau::SourceModule* source, Luau::Position at)
    {
        ALScriptSignature   answer;
        const Luau::TypeId* callee = module.astTypes.find(call->func);
        if (!callee)
        {
            return answer;
        }
        // What is called: a function that may be nil -- a field read after
        // `if t.f then` -- as the function it is where it is one.
        Luau::TypeId called = Luau::follow(*callee);
        if (const Luau::UnionType* options = Luau::get<Luau::UnionType>(called))
        {
            std::vector<Luau::TypeId> some;
            for (Luau::TypeId option : options->options)
            {
                if (!Luau::isNil(Luau::follow(option)))
                {
                    some.push_back(Luau::follow(option));
                }
            }
            if (some.size() == 1)
            {
                called = some.front();
            }
        }
        // Each form it has: one function, or an overloaded one's every part.
        std::vector<const Luau::FunctionType*> forms;
        if (const Luau::IntersectionType* overloads = Luau::get<Luau::IntersectionType>(called))
        {
            for (Luau::TypeId part : overloads->parts)
            {
                if (const Luau::FunctionType* one = Luau::get<Luau::FunctionType>(Luau::follow(part)))
                {
                    forms.push_back(one);
                }
            }
        }
        else if (const Luau::FunctionType* function = functionOf(called))
        {
            forms.push_back(function);
        }
        if (forms.empty())
        {
            return answer;
        }
        answer.found           = true;
        const std::string name = nameOf(call->func);
        Luau::ToStringOptions options;
        options.functionTypeArguments = true;
        // A form's label, and its parameters as they print, the first dropped
        // when the call passes it as self; and whether it takes as many
        // arguments as the call has.
        const auto describe = [&](const Luau::FunctionType& function, ALScriptSignature::Overload& out) {
            out.label = Luau::toStringNamedFunction(name.empty() ? "function" : name, function, options);
            const auto [arg_types, tail] = Luau::flatten(function.argTypes);
            // Only where the call itself passes it: a method called with a dot
            // is given its object as its first argument, which is a parameter
            // like any other there.
            const size_t skip = call->self && !arg_types.empty() ? 1 : 0;
            for (size_t i = skip; i < arg_types.size(); ++i)
            {
                std::string parameter;
                if (i < function.argNames.size() && function.argNames[i])
                {
                    parameter = function.argNames[i]->name + ": ";
                }
                parameter += typeText(arg_types[i]);
                out.parameters.push_back(std::move(parameter));
            }
            // A tail that takes more, where the function does: not a hidden one,
            // which the new solver gives a function the script wrote, nor none.
            const Luau::VariadicTypePack* variadic = tail ? Luau::get<Luau::VariadicTypePack>(Luau::follow(*tail)) : nullptr;
            const bool                    more     = tail && !Luau::isEmpty(*tail) && !(variadic && variadic->hidden);
            if (more)
            {
                out.parameters.push_back("..." + Luau::toString(*tail));
            }
            return more || out.parameters.size() >= call->args.size;
        };
        S32 fits = -1;
        for (const Luau::FunctionType* form : forms)
        {
            ALScriptSignature::Overload one;
            if (describe(*form, one) && fits < 0)
            {
                fits = static_cast<S32>(answer.overloads.size());
            }
            answer.overloads.push_back(std::move(one));
        }
        answer.overload   = llmax(0, fits);
        answer.label      = answer.overloads[static_cast<size_t>(answer.overload)].label;
        answer.parameters = answer.overloads[static_cast<size_t>(answer.overload)].parameters;
        if (answer.overloads.size() < 2)
        {
            answer.overloads.clear();
            answer.overload = 0;
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
        std::optional<std::string> symbol = called->documentationSymbol;
        if (!symbol)
        {
            symbol = fieldSymbolOf(module, call->func);
        }
        if (!symbol && source)
        {
            symbol = Luau::getDocumentationSymbolAtPosition(*source, module, call->func->location.begin);
        }
        if (const ALLuauFrontend::Doc* doc = front.docFor(symbol))
        {
            answer.documentation = doc->documentation;
        }
        return answer;
    }
}

ALScriptSignature ALLuauService::signature(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend&      front = *mFrontend;
    const Luau::Position at    = positionOf(line, column);
    // As the script is typed: the call's statement checked alone against
    // the last check, where that answers; else the script checked whole.
    const ALLuauFragment::Typed typed = mFragment->typecheck(source, at);
    if (typed.outcome == ALLuauFragment::Outcome::Nothing)
    {
        return {};
    }
    if (typed.outcome == ALLuauFragment::Outcome::Answered)
    {
        // Where the fragment's callee is no function -- one that may be nil,
        // read under an `if` that says it is not, which a fragment does not
        // see -- the whole script says what it is.
        if (ALScriptSignature answer = signatureOf(front, *typed.module, typed.call, nullptr, at); answer.found)
        {
            return answer;
        }
    }
    const Luau::ModulePtr     module        = front.queried(source);
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module)
    {
        return {};
    }
    // The innermost call whose parentheses hold the position.
    const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*module_source, at);
    for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
    {
        if (const Luau::AstExprCall* call = (*it)->as<Luau::AstExprCall>(); call && call->argLocation.containsClosed(at))
        {
            return signatureOf(front, *module, call, module_source, at);
        }
    }
    return {};
}

// --- where a name lives -------------------------------------------------------------

ALScriptReferences ALLuauService::references(std::string_view source, S32 line, S32 column)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    const Luau::ModulePtr     module        = front.queried(source);
    ALScriptReferences        answer;
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
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
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    // The types beside the names come from a query's check, as a hover's
    // do, whatever came before: a check in the script's own mode works out
    // fewer of them.
    const Luau::ModulePtr     module        = front.queried(source);
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module_source->root)
    {
        return {};
    }
    Outliner outliner(module.get());
    module_source->root->visit(&outliner);
    return std::move(outliner.out);
}

std::vector<ALScriptOutlineEntry> ALLuauService::shape(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    Luau::Allocator    allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;
    Luau::ParseResult  parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
    if (!parsed.root)
    {
        return {};
    }
    Outliner outliner(nullptr);
    parsed.root->visit(&outliner);
    return std::move(outliner.out);
}

// --- what every name is ------------------------------------------------------------

std::vector<ALScriptSemanticToken> ALLuauService::semanticTokens(std::string_view source)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    const Luau::ModulePtr     module        = front.queried(source);
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module || !module_source->root)
    {
        return {};
    }
    Semantics semantics(*module, front.frontend->globals.globalScope.get());
    visitRead(module_source->root, semantics, front.passedOver);
    std::vector<ALScriptSemanticToken>& out = semantics.out;
    std::stable_sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](const ALScriptSemanticToken& a, const ALScriptSemanticToken& b) { return a.span == b.span; }),
              out.end());
    return std::move(out);
}

// --- what goes beside the text ----------------------------------------------------

std::vector<ALScriptInlayHint> ALLuauService::inlayHints(std::string_view source, bool parameters, bool types)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    ALLuauFrontend& front = *mFrontend;
    if (!parameters && !types)
    {
        return {};
    }
    const Luau::ModulePtr     module        = front.queried(source);
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    if (!module_source || !module || !module_source->root)
    {
        return {};
    }
    Hints hints(*module, parameters, types);
    visitRead(module_source->root, hints, front.passedOver);
    std::stable_sort(hints.out.begin(), hints.out.end());
    return std::move(hints.out);
}

// --- what could be done here ----------------------------------------------------------

namespace
{
    // Where a place of Luau's is in the text, or npos past it.
    size_t offsetAt(std::string_view source, const Luau::Position& at)
    {
        size_t offset = 0;
        for (unsigned line = 0; line < at.line; ++line)
        {
            offset = source.find('\n', offset);
            if (offset == std::string_view::npos)
            {
                return offset;
            }
            ++offset;
        }
        const size_t end = source.find('\n', offset);
        const size_t length = (end == std::string_view::npos ? source.size() : end) - offset;
        return at.column <= length ? offset + at.column : std::string_view::npos;
    }

    std::string_view sliceOf(std::string_view source, const Luau::Position& begin, const Luau::Position& end)
    {
        const size_t from = offsetAt(source, begin);
        const size_t to   = offsetAt(source, end);
        return from == std::string_view::npos || to == std::string_view::npos || to < from ? std::string_view() : source.substr(from, to - from);
    }

    ALScriptFix refactor(ALScriptFix fix)
    {
        fix.kind = ALScriptFix::Kind::Refactor;
        return fix;
    }

    void replace(ALScriptFix& fix, const Luau::Position& begin, const Luau::Position& end, std::string text)
    {
        fix.edits.push_back({ static_cast<S32>(begin.line), static_cast<S32>(begin.column), static_cast<S32>(end.line), static_cast<S32>(end.column),
                              std::move(text) });
    }

    // A condition the other way round: `not x` as `x`, `a == b` as
    // `a ~= b`, anything else under a `not`. An order is not turned round,
    // `a < b` as `a >= b`: NaN is neither.
    std::string negated(std::string_view source, Luau::AstExpr* condition)
    {
        const std::string text(sliceOf(source, condition->location.begin, condition->location.end));
        if (Luau::AstExprUnary* unary = condition->as<Luau::AstExprUnary>(); unary && unary->op == Luau::AstExprUnary::Op::Not)
        {
            Luau::AstExpr* inner = unary->expr;
            if (Luau::AstExprGroup* group = inner->as<Luau::AstExprGroup>())
            {
                inner = group->expr;
            }
            return std::string(sliceOf(source, inner->location.begin, inner->location.end));
        }
        if (Luau::AstExprBinary* binary = condition->as<Luau::AstExprBinary>();
            binary && (binary->op == Luau::AstExprBinary::CompareEq || binary->op == Luau::AstExprBinary::CompareNe))
        {
            std::string between(sliceOf(source, binary->left->location.end, binary->right->location.begin));
            const size_t at = between.find(binary->op == Luau::AstExprBinary::CompareEq ? "==" : "~=");
            if (at != std::string::npos)
            {
                between.replace(at, 2, binary->op == Luau::AstExprBinary::CompareEq ? "~=" : "==");
                return std::string(sliceOf(source, binary->left->location.begin, binary->left->location.end)) + between +
                       std::string(sliceOf(source, binary->right->location.begin, binary->right->location.end));
            }
        }
        const bool simple = condition->is<Luau::AstExprLocal>() || condition->is<Luau::AstExprGlobal>() || condition->is<Luau::AstExprCall>() ||
                            condition->is<Luau::AstExprIndexName>() || condition->is<Luau::AstExprIndexExpr>() || condition->is<Luau::AstExprGroup>();
        return simple ? "not " + text : "not (" + text + ")";
    }

    // Every operand of a chain of `..`, in order.
    void concatenated(Luau::AstExpr* expr, std::vector<Luau::AstExpr*>& out)
    {
        if (Luau::AstExprBinary* binary = expr->as<Luau::AstExprBinary>(); binary && binary->op == Luau::AstExprBinary::Concat)
        {
            concatenated(binary->left, out);
            concatenated(binary->right, out);
            return;
        }
        out.push_back(expr);
    }

    // What the script calls in `ll`, and which events it hears.
    struct Asking : Luau::AstVisitor
    {
        std::set<std::string> calls;
        std::set<std::string> heard;

        bool visit(Luau::AstExprCall* call) override
        {
            Luau::AstExprIndexName* index = call->func->as<Luau::AstExprIndexName>();
            Luau::AstExprGlobal*    table = index ? index->expr->as<Luau::AstExprGlobal>() : nullptr;
            if (!table)
            {
                return true;
            }
            if (strcmp(table->name.value, "ll") == 0)
            {
                calls.insert(index->index.value);
            }
            // Heard where a handler is put on it -- `LLEvents:on` and
            // `:once` -- not where one is taken off, nor asked about.
            else if (strcmp(table->name.value, "LLEvents") == 0 && call->args.size >= 1 &&
                     (strcmp(index->index.value, "on") == 0 || strcmp(index->index.value, "once") == 0))
            {
                if (Luau::AstExprConstantString* name = call->args.data[0]->as<Luau::AstExprConstantString>())
                {
                    heard.insert(std::string(name->value.data, name->value.size));
                }
            }
            return true;
        }

        bool visit(Luau::AstStatAssign* assign) override
        {
            for (Luau::AstExpr* target : assign->vars)
            {
                Luau::AstExprIndexName* index = target->as<Luau::AstExprIndexName>();
                Luau::AstExprGlobal*    table = index ? index->expr->as<Luau::AstExprGlobal>() : nullptr;
                if (table && strcmp(table->name.value, "LLEvents") == 0)
                {
                    heard.insert(index->index.value);
                }
            }
            return true;
        }
    };
}

std::vector<ALScriptFix> ALLuauService::actions(std::string_view source, S32 line, S32 column, S32 endLine, S32 endColumn)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    std::vector<ALScriptFix> out;
    ALLuauFrontend&           front         = *mFrontend;
    const Luau::ModulePtr     module        = front.queried(source);
    const Luau::SourceModule* module_source = front.frontend->getSourceModule(front.moduleName);
    // The type a local was given without saying, where the caret is on its
    // name: the hint the editor shows beside it, written in. That line's
    // hints alone.
    std::vector<ALScriptInlayHint> line_hints;
    if (module && module_source && module_source->root)
    {
        Hints on_line(*module, false, true);
        on_line.onlyLine = static_cast<unsigned>(std::max(0, line));
        module_source->root->visit(&on_line);
        std::stable_sort(on_line.out.begin(), on_line.out.end());
        line_hints = std::move(on_line.out);
    }
    for (const ALScriptInlayHint& hint : line_hints)
    {
        if (hint.kind != ALScriptInlayHint::Kind::Type || hint.line != line || column > hint.column)
        {
            continue;
        }
        const std::string_view text = sliceOf(source, Luau::Position(hint.line, 0), Luau::Position(hint.line, hint.column));
        S32                    from = hint.column;
        while (from > 0 && ALScriptLexicon::isNameByte(text[from - 1]))
        {
            --from;
        }
        if (column >= from && hint.writable && hint.text.size() > 2)
        {
            ALScriptFix fix = refactor(ALScriptFixes::titled("ScriptActionAnnotate", "Declare it as '[1]'", { hint.text.substr(2) }));
            fix.edits.push_back({ hint.line, hint.column, hint.line, hint.column, hint.text });
            out.push_back(std::move(fix));
        }
    }

    if (!module_source || !module || !module_source->root || !module_source->parseErrors.empty())
    {
        return out;
    }
    const Luau::Position              at       = positionOf(line, column);
    const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(*module_source, at);

    // A stretch chosen that is one expression, into a local declared just
    // before the statement it is in, named as nothing in the script is.
    // Only where that runs it as often, and as surely, as it ran: not out
    // of a loop's condition, an `elseif`'s, nor what `and`, `or` or an `if`
    // expression may skip.
    if (endLine == line && endColumn > column)
    {
        S32                    from = column, to = endColumn;
        const std::string_view text = sliceOf(source, Luau::Position(line, 0), Luau::Position(line, endColumn));
        while (from < to && isspace(static_cast<unsigned char>(text[from])))
        {
            ++from;
        }
        while (to > from && isspace(static_cast<unsigned char>(text[to - 1])))
        {
            --to;
        }
        const std::vector<Luau::AstNode*> around = Luau::findAstAncestryOfPosition(*module_source, positionOf(line, from));
        Luau::AstExpr*                    chosen    = nullptr;
        Luau::AstNode*                    holder    = nullptr;
        Luau::AstStat*                    statement = nullptr;
        bool                              certain   = true;
        for (size_t i = around.size(); i-- > 0;)
        {
            Luau::AstNode* node = around[i];
            if (!chosen)
            {
                Luau::AstExpr* expr = node->asExpr();
                if (expr && expr->location.begin == positionOf(line, from) && expr->location.end == positionOf(line, to))
                {
                    chosen = expr;
                    holder = i > 0 ? around[i - 1] : nullptr;
                }
                continue;
            }
            const Luau::AstNode* below = around[i + 1];
            if (Luau::AstExprBinary* binary = node->as<Luau::AstExprBinary>();
                binary && (binary->op == Luau::AstExprBinary::And || binary->op == Luau::AstExprBinary::Or) && below == binary->right)
            {
                certain = false;
            }
            if (Luau::AstExprIfElse* either = node->as<Luau::AstExprIfElse>(); either && below != either->condition)
            {
                certain = false;
            }
            Luau::AstStat* stat = node->asStat();
            if (!stat)
            {
                continue;
            }
            // The statement it is in, where that stands in a block: one that
            // stands in another statement -- an `elseif` in its `if` -- runs
            // only when that one gets to it.
            if (i == 0 || !around[i - 1]->is<Luau::AstStatBlock>())
            {
                certain = false;
                continue;
            }
            statement = stat;
            break;
        }
        // Nor what is assigned to, which a local of its own would take in
        // its place.
        if (Luau::AstStatAssign* assign = statement ? statement->as<Luau::AstStatAssign>() : nullptr)
        {
            for (Luau::AstExpr* target : assign->vars)
            {
                certain = certain && target != chosen;
            }
        }
        if (Luau::AstStatCompoundAssign* assign = statement ? statement->as<Luau::AstStatCompoundAssign>() : nullptr)
        {
            certain = certain && assign->var != chosen;
        }
        if (Luau::AstStatFunction* function = statement ? statement->as<Luau::AstStatFunction>() : nullptr)
        {
            certain = certain && function->name != chosen;
        }
        // Nor a call that is the whole statement, which would leave the
        // local's name standing alone as one, and that is no statement.
        if (Luau::AstStatExpr* alone = statement ? statement->as<Luau::AstStatExpr>() : nullptr)
        {
            certain = certain && alone->expr != chosen;
        }
        // Nor a call where every value it gives is kept -- the last of a
        // call's arguments, of what is returned, of a table's items, of
        // what a for-in reads, or of the values of an assignment with more
        // names than values -- since a local keeps only the first; unless
        // the checker says it gives just the one.
        const auto one_value = [&module](Luau::AstExpr* expr) {
            // What the call gave, where the checker kept it; else what the
            // function called returns -- the overload chosen, or the
            // function as it was called.
            std::optional<Luau::TypePackId> pack;
            if (const Luau::TypePackId* given = module->astTypePacks.find(expr))
            {
                pack = *given;
            }
            else if (Luau::AstExprCall* call = expr->as<Luau::AstExprCall>())
            {
                const Luau::TypeId* callee = module->astOverloadResolvedTypes.find(call);
                callee                     = callee ? callee : module->astTypes.find(call->func);
                if (const Luau::FunctionType* function = callee ? Luau::get<Luau::FunctionType>(Luau::follow(*callee)) : nullptr)
                {
                    pack = function->retTypes;
                }
            }
            if (!pack)
            {
                return false;
            }
            const auto [head, tail] = Luau::flatten(Luau::follow(*pack));
            return head.size() == 1 && (!tail || Luau::isEmpty(*tail));
        };
        if (chosen && chosen->is<Luau::AstExprCall>() && holder && !one_value(chosen))
        {
            const auto last = [chosen](const Luau::AstArray<Luau::AstExpr*>& list) { return list.size > 0 && list.data[list.size - 1] == chosen; };
            if (Luau::AstExprCall* call = holder->as<Luau::AstExprCall>())
            {
                certain = certain && !last(call->args);
            }
            else if (Luau::AstStatReturn* ret = holder->as<Luau::AstStatReturn>())
            {
                certain = certain && !last(ret->list);
            }
            else if (Luau::AstExprTable* table = holder->as<Luau::AstExprTable>())
            {
                certain = certain && !(table->items.size > 0 && table->items.data[table->items.size - 1].kind == Luau::AstExprTable::Item::Kind::List &&
                                       table->items.data[table->items.size - 1].value == chosen);
            }
            else if (Luau::AstStatForIn* each = holder->as<Luau::AstStatForIn>())
            {
                certain = certain && !last(each->values);
            }
            else if (Luau::AstStatLocal* local = holder->as<Luau::AstStatLocal>())
            {
                certain = certain && !(last(local->values) && local->vars.size > local->values.size);
            }
            else if (Luau::AstStatAssign* assign = holder->as<Luau::AstStatAssign>())
            {
                certain = certain && !(last(assign->values) && assign->vars.size > assign->values.size);
            }
        }
        if (chosen && statement && certain && !chosen->is<Luau::AstExprFunction>() && !chosen->is<Luau::AstExprVarargs>() &&
            !statement->is<Luau::AstStatWhile>() && !statement->is<Luau::AstStatRepeat>())
        {
            const std::string      name   = ALScriptFixes::freshName(source, "value");
            const Luau::Position   start(statement->location.begin.line, 0);
            const std::string_view first  = sliceOf(source, start, statement->location.begin);
            const bool             alone  = first.find_first_not_of(" \t") == std::string_view::npos;
            const std::string      value(sliceOf(source, chosen->location.begin, chosen->location.end));
            ALScriptFix            fix = refactor(ALScriptFixes::titled("ScriptActionExtract", "Put it in a local, '[1]'", { name }));
            // On a line of its own, where the statement starts its line;
            // else just before it, where another stands ahead of it.
            if (alone)
            {
                replace(fix, start, start, std::string(first) + "local " + name + " = " + value + "\n");
            }
            else
            {
                replace(fix, statement->location.begin, statement->location.begin, "local " + name + " = " + value + "; ");
            }
            replace(fix, chosen->location.begin, chosen->location.end, name);
            out.push_back(std::move(fix));
        }
    }

    for (Luau::AstNode* node : ancestry)
    {
        // A concatenation the caret is in, with a string in it: one
        // interpolated string, each operand not a string in braces.
        Luau::AstExprBinary* binary = node->as<Luau::AstExprBinary>();
        if (binary && binary->op == Luau::AstExprBinary::Concat)
        {
            std::vector<Luau::AstExpr*> parts;
            concatenated(binary, parts);
            std::string written = "`";
            bool        strings = false;
            bool        fits    = true;
            for (Luau::AstExpr* part : parts)
            {
                if (Luau::AstExprConstantString* string = part->as<Luau::AstExprConstantString>())
                {
                    using Quote = Luau::AstExprConstantString::QuoteStyle;
                    if (string->quoteStyle != Quote::QuotedSimple && string->quoteStyle != Quote::QuotedSingle)
                    {
                        fits = false;
                        break;
                    }
                    const std::string_view quoted = sliceOf(source, string->location.begin, string->location.end);
                    if (quoted.size() < 2 || quoted.front() == '`')
                    {
                        fits = false;
                        break;
                    }
                    for (char c : quoted.substr(1, quoted.size() - 2))
                    {
                        if (c == '`' || c == '{')
                        {
                            written += '\\';
                        }
                        written += c;
                    }
                    strings = true;
                }
                else if (part->is<Luau::AstExprInterpString>())
                {
                    fits = false;
                    break;
                }
                else
                {
                    written += "{" + std::string(sliceOf(source, part->location.begin, part->location.end)) + "}";
                }
            }
            if (fits && strings && parts.size() > 1)
            {
                ALScriptFix fix = refactor(ALScriptFixes::titled("ScriptActionInterpolate", "Write it as an interpolated string", {}));
                replace(fix, binary->location.begin, binary->location.end, written + "`");
                out.push_back(std::move(fix));
            }
            break;
        }
    }

    // An `if` with an `else`, the caret on its first line: the condition
    // turned round, the branches swapped.
    for (size_t i = ancestry.size(); i-- > 0;)
    {
        Luau::AstStatIf* branch = ancestry[i]->as<Luau::AstStatIf>();
        if (!branch || branch->location.begin.line != static_cast<unsigned>(line))
        {
            continue;
        }
        if (branch->thenLocation && branch->elseLocation && branch->elsebody && branch->elsebody->is<Luau::AstStatBlock>() &&
            branch->location.end.column >= 3)
        {
            const Luau::Position ending(branch->location.end.line, branch->location.end.column - 3);
            if (sliceOf(source, ending, branch->location.end) == "end")
            {
                const std::string then_text(sliceOf(source, branch->thenLocation->end, branch->elseLocation->begin));
                const std::string else_text(sliceOf(source, branch->elseLocation->end, ending));
                ALScriptFix       fix = refactor(ALScriptFixes::titled("ScriptActionInvertIf", "Invert the if", {}));
                replace(fix, branch->condition->location.begin, branch->condition->location.end, negated(source, branch->condition));
                replace(fix, branch->thenLocation->end, branch->elseLocation->begin, else_text);
                replace(fix, branch->elseLocation->end, ending, then_text);
                out.push_back(std::move(fix));
            }
        }
        break;
    }

    // A handler for each event the script asks for and does not hear, at
    // its end, with the parameters the definitions give the event.
    Asking asking;
    module_source->root->visit(&asking);
    std::set<std::string> offered;
    for (const std::string& call : asking.calls)
    {
        const char* event = ALScriptFixes::eventAnswering("ll" + call);
        if (!event || asking.heard.count(event) || !offered.insert(event).second)
        {
            continue;
        }
        std::string parameters;
        for (const auto& [symbol, binding] : front.frontend->globals.globalScope->bindings)
        {
            if (strcmp(symbol.c_str(), "LLEvents") != 0)
            {
                continue;
            }
            const Luau::ExternType* events = Luau::get<Luau::ExternType>(Luau::follow(binding.typeId));
            if (!events)
            {
                break;
            }
            const auto prop = events->props.find(event);
            if (prop == events->props.end() || !prop->second.readTy)
            {
                break;
            }
            Luau::TypeId handler = Luau::follow(*prop->second.readTy);
            if (const Luau::UnionType* either = Luau::get<Luau::UnionType>(handler))
            {
                for (Luau::TypeId option : either->options)
                {
                    if (Luau::get<Luau::FunctionType>(Luau::follow(option)))
                    {
                        handler = Luau::follow(option);
                    }
                }
            }
            if (const Luau::FunctionType* function = Luau::get<Luau::FunctionType>(handler))
            {
                const auto [types, tail] = Luau::flatten(function->argTypes);
                for (size_t n = 0; n < types.size(); ++n)
                {
                    const std::string name = n < function->argNames.size() && function->argNames[n] ? function->argNames[n]->name : "arg" + std::to_string(n + 1);
                    parameters += (n ? ", " : "") + name + ": " + Luau::toString(types[n]);
                }
            }
            break;
        }
        // At the end, after a blank line: on the empty line a final break
        // leaves, or after the last line's text.
        const S32    last   = static_cast<S32>(std::count(source.begin(), source.end(), '\n'));
        const bool   broken = !source.empty() && source.back() == '\n';
        const size_t start  = source.rfind('\n');
        const S32    end    = broken ? 0 : static_cast<S32>(source.size() - (start == std::string_view::npos ? 0 : start + 1));
        ALScriptFix  fix    = refactor(ALScriptFixes::titled("ScriptActionHandler", "Add a handler for '[1]'", { event }));
        fix.edits.push_back({ last, end, last, end,
                              std::string(broken ? "\n" : "\n\n") + "LLEvents:on(\"" + event + "\", function(" + parameters + ")\nend)\n" });
        out.push_back(std::move(fix));
    }
    return out;
}
