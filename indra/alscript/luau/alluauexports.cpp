/**
 * @file alluauexports.cpp
 * @brief What a SLua module gives whoever requires it, read off its text.
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

#include "alluauexports.h"

#include "Luau/Ast.h"
#include "Luau/Parser.h"
#include "alscriptlexicon.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>

namespace
{
    bool isName(std::string_view word)
    {
        return ALScriptLexicon::isName(word) && !ALScriptLexicon::isLuauKeyword(word);
    }

    struct Names
    {
        std::vector<std::string> out;

        void add(std::string_view name)
        {
            if (isName(name) && std::find(out.begin(), out.end(), name) == out.end())
            {
                out.emplace_back(name);
            }
        }

        // A constant string key, as `["name"]` or `name =` writes it.
        void addKey(Luau::AstExpr* key)
        {
            if (Luau::AstExprConstantString* string = key ? key->as<Luau::AstExprConstantString>() : nullptr)
            {
                add(std::string_view(string->value.data, string->value.size));
            }
        }

        void addTable(Luau::AstExprTable* table)
        {
            for (const Luau::AstExprTable::Item& item : table->items)
            {
                if (item.kind != Luau::AstExprTable::Item::Kind::List)
                {
                    addKey(item.key);
                }
            }
        }
    };

    // `setmetatable(t, meta)` returns `t`: what it is given first.
    Luau::AstExpr* unwrapped(Luau::AstExpr* expr)
    {
        while (expr)
        {
            if (Luau::AstExprGroup* group = expr->as<Luau::AstExprGroup>())
            {
                expr = group->expr;
                continue;
            }
            if (Luau::AstExprTypeAssertion* assertion = expr->as<Luau::AstExprTypeAssertion>())
            {
                expr = assertion->expr;
                continue;
            }
            Luau::AstExprCall*   call   = expr->as<Luau::AstExprCall>();
            Luau::AstExprGlobal* callee = call ? call->func->as<Luau::AstExprGlobal>() : nullptr;
            if (callee && strcmp(callee->name.value, "setmetatable") == 0 && call->args.size >= 1)
            {
                expr = call->args.data[0];
                continue;
            }
            break;
        }
        return expr;
    }

    // The field of the local a statement's target sets, `M.name` or
    // `M["name"]`, or null.
    Luau::AstExpr* fieldOf(Luau::AstExpr* target, Luau::AstLocal* of)
    {
        if (Luau::AstExprIndexName* index = target->as<Luau::AstExprIndexName>())
        {
            Luau::AstExprLocal* table = index->expr->as<Luau::AstExprLocal>();
            return table && table->local == of ? target : nullptr;
        }
        if (Luau::AstExprIndexExpr* index = target->as<Luau::AstExprIndexExpr>())
        {
            Luau::AstExprLocal* table = index->expr->as<Luau::AstExprLocal>();
            return table && table->local == of ? target : nullptr;
        }
        return nullptr;
    }

    void addField(Names& names, Luau::AstExpr* field)
    {
        if (Luau::AstExprIndexName* index = field->as<Luau::AstExprIndexName>())
        {
            names.add(index->index.value);
        }
        else if (Luau::AstExprIndexExpr* index = field->as<Luau::AstExprIndexExpr>())
        {
            names.addKey(index->index);
        }
    }
}

namespace ALLuauExports
{
    std::vector<std::string> of(std::string_view source)
    {
        Luau::Allocator     allocator;
        Luau::AstNameTable  table(allocator);
        Luau::ParseOptions  options;
        Luau::ParseResult   parsed = Luau::Parser::parse(source.data(), source.size(), table, allocator, options);
        Names               names;
        if (!parsed.root || !parsed.errors.empty() || parsed.root->body.size == 0)
        {
            return names.out;
        }
        // What the module gives is what its last statement returns: one
        // value, at the top.
        Luau::AstStatReturn* returned = parsed.root->body.data[parsed.root->body.size - 1]->as<Luau::AstStatReturn>();
        if (!returned || returned->list.size != 1)
        {
            return names.out;
        }
        Luau::AstExpr* value = unwrapped(returned->list.data[0]);
        if (Luau::AstExprTable* built = value ? value->as<Luau::AstExprTable>() : nullptr)
        {
            names.addTable(built);
            return names.out;
        }
        Luau::AstExprLocal* local = value ? value->as<Luau::AstExprLocal>() : nullptr;
        if (!local)
        {
            return names.out;
        }
        // A local table: what it was made with, and what the statements at
        // the top put in it, in the order they do.
        for (Luau::AstStat* stat : parsed.root->body)
        {
            if (Luau::AstStatLocal* declared = stat->as<Luau::AstStatLocal>())
            {
                for (size_t i = 0; i < declared->vars.size && i < declared->values.size; ++i)
                {
                    if (declared->vars.data[i] != local->local)
                    {
                        continue;
                    }
                    Luau::AstExpr* given = unwrapped(declared->values.data[i]);
                    if (Luau::AstExprTable* made = given ? given->as<Luau::AstExprTable>() : nullptr)
                    {
                        names.addTable(made);
                    }
                }
            }
            else if (Luau::AstStatFunction* function = stat->as<Luau::AstStatFunction>())
            {
                if (Luau::AstExpr* field = fieldOf(function->name, local->local))
                {
                    addField(names, field);
                }
            }
            else if (Luau::AstStatAssign* assign = stat->as<Luau::AstStatAssign>())
            {
                for (Luau::AstExpr* target : assign->vars)
                {
                    if (Luau::AstExpr* field = fieldOf(target, local->local))
                    {
                        addField(names, field);
                    }
                }
            }
        }
        return names.out;
    }

    namespace
    {
        // What each module checked was found to export, by its key and its
        // text's hash, the latest last.
        struct Checked
        {
            std::string              key;
            size_t                   hash = 0;
            std::vector<std::string> names;
        };
        constexpr size_t    CHECKED_KEPT = 256;
        std::mutex          sCheckedMutex;
        std::deque<Checked> sChecked;
    }

    void checked(const std::string& key, std::string_view text, std::vector<std::string> names)
    {
        const size_t                      hash = std::hash<std::string_view>()(text);
        const std::lock_guard<std::mutex> lock(sCheckedMutex);
        sChecked.erase(std::remove_if(sChecked.begin(), sChecked.end(), [&key](const Checked& one) { return one.key == key; }), sChecked.end());
        sChecked.push_back({ key, hash, std::move(names) });
        if (sChecked.size() > CHECKED_KEPT)
        {
            sChecked.pop_front();
        }
    }

    std::optional<std::vector<std::string>> checkedOf(const std::string& key, std::string_view text)
    {
        const size_t                      hash = std::hash<std::string_view>()(text);
        const std::lock_guard<std::mutex> lock(sCheckedMutex);
        for (const Checked& one : sChecked)
        {
            if (one.key == key && one.hash == hash)
            {
                return one.names;
            }
        }
        return std::nullopt;
    }
}
