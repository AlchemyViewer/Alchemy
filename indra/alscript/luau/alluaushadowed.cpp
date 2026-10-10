/**
 * @file alluaushadowed.cpp
 * @brief Of a name a scope declares more than once, only the declaration in effect at a place, while Luau is asked about it.
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

#include "alluaushadowed.h"

#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Module.h"

#include <algorithm>
#include <string_view>

namespace
{
    // A local a scope declares.
    struct Declared
    {
        std::string_view name;
        Luau::Symbol     symbol;
        Luau::Location   location;
    };

    // Each run of one name among locals in order of their names, `each`
    // given its first and last.
    template<typename Each>
    void eachName(const std::vector<Declared>& locals, Each each)
    {
        for (auto first = locals.begin(); first != locals.end();)
        {
            const auto last = std::find_if(first, locals.end(), [&first](const Declared& local) { return local.name != first->name; });
            each(first, last);
            first = last;
        }
    }

    // The locals of a scope that another of its locals shares a name with,
    // each name's together in the order they were declared. Most scopes
    // declare no name twice, and have none.
    void sharedNames(const Luau::Scope& scope, std::vector<Declared>& out)
    {
        out.clear();
        if (scope.bindings.size() < 2)
        {
            return;
        }
        for (const auto& [symbol, binding] : scope.bindings)
        {
            if (symbol.local)
            {
                out.push_back({ symbol.local->name.value, symbol, binding.location });
            }
        }
        std::sort(out.begin(), out.end(), [](const Declared& a, const Declared& b) {
            return a.name != b.name ? a.name < b.name : a.location.begin < b.location.begin;
        });
        size_t shared = 0;
        for (size_t first = 0; first < out.size();)
        {
            size_t last = first + 1;
            while (last < out.size() && out[last].name == out[first].name)
            {
                ++last;
            }
            for (size_t i = first; last - first > 1 && i < last; ++i)
            {
                out[shared++] = out[i];
            }
            first = last;
        }
        out.resize(shared);
    }

    // Whether the place `ancestry` was found at is in the statement that
    // declares `local`, as Luau's autocomplete asks (isBeingDefined): the
    // `local` the name is in, its value being written.
    bool declaring(const Luau::AstLocal* local, const std::vector<Luau::AstNode*>& ancestry)
    {
        for (const Luau::AstNode* node : ancestry)
        {
            if (const Luau::AstStatLocal* statement = node->as<Luau::AstStatLocal>())
            {
                if (std::find(statement->vars.begin(), statement->vars.end(), local) != statement->vars.end())
                {
                    return true;
                }
            }
        }
        return false;
    }
}

ALLuauShadowed::ALLuauShadowed(const Luau::Module& module, Luau::Position at)
{
    std::vector<Declared>       shared;
    std::vector<Luau::AstNode*> ancestry;
    bool                        found = false;
    for (const auto& [where, scope] : module.scopes)
    {
        sharedNames(*scope, shared);
        if (shared.empty())
        {
            continue;
        }
        // What `at` is in, found as Luau's autocomplete finds it: only
        // where a name is declared again at all.
        if (!found && module.root)
        {
            found    = true;
            ancestry = Luau::findAncestryAtPositionForAutocomplete(module.root, at);
        }
        eachName(shared, [&](auto first, auto last) {
            // The one in effect: declared before `at`, as Luau's own test
            // has it (isBindingLegalAtCurrentPosition), the last of those
            // but one whose statement `at` is in.
            auto kept = last;
            for (auto it = first; it != last; ++it)
            {
                if (it->location.end < at && !declaring(it->symbol.local, ancestry))
                {
                    kept = it;
                }
            }
            for (auto it = first; it != last; ++it)
            {
                if (it == kept)
                {
                    continue;
                }
                // Taken out by where it is: by its key is ambiguous to
                // libc++, a Symbol being constructible, if deleted, from
                // anything.
                const Bindings::const_iterator bound = scope->bindings.find(it->symbol);
                if (bound != scope->bindings.end())
                {
                    mAside.emplace_back(scope.get(), scope->bindings.extract(bound));
                }
            }
        });
    }
}

ALLuauShadowed::~ALLuauShadowed()
{
    for (auto it = mAside.rbegin(); it != mAside.rend(); ++it)
    {
        it->first->bindings.insert(std::move(it->second));
    }
}

// static
bool ALLuauShadowed::ambiguous(const Luau::Scope& scope, Luau::Position at, const Luau::AutocompleteResult& found)
{
    // Autocomplete reads a scope's bindings by name for an expression,
    // passing over a local whose statement the place is in, and for a
    // statement, not.
    const bool expression = found.context == Luau::AutocompleteContext::Expression;
    if (!expression && found.context != Luau::AutocompleteContext::Statement)
    {
        return false;
    }
    std::vector<Declared> shared;
    sharedNames(scope, shared);
    bool told = false;
    eachName(shared, [&](auto first, auto last) {
        const auto offered = std::count_if(first, last, [&](const Declared& local) {
            return local.location.end < at && !(expression && declaring(local.symbol.local, found.ancestry));
        });
        told = told || offered > 1;
    });
    return told;
}
