/**
 * @file alluaushadowed.h
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

#pragma once

#include "Luau/AutocompleteTypes.h"
#include "Luau/Location.h"
#include "Luau/Scope.h"

#include <utility>
#include <vector>

namespace Luau
{
    struct Module;
}

// A module's scopes as Lua has them at a place, while this lives: of the
// locals a scope declares more than once under one name, only the one in
// effect there -- the last declared before it, but for one whose statement
// the place is in -- and none where none is. The others are put back as
// this goes.
//
// Luau keeps each declaration a scope makes among its bindings, a table
// ordered by where the parser put each. Where it looks a local up by its
// name -- autocomplete offering what is in scope, a fragment's check
// bringing the last check's type of each name it uses -- it takes the first
// the table gives: for the same script at the same place, one run offers
// `local function test` and the next the `local test = 1` declared after
// it. Where it looks one up by its declaration, as a check does each use,
// there is no such choice.
//
// Over a module of one thread's front end, as the questions are.
class ALLuauShadowed
{
public:
    ALLuauShadowed(const Luau::Module& module, Luau::Position at);
    ~ALLuauShadowed();
    ALLuauShadowed(const ALLuauShadowed&)            = delete;
    ALLuauShadowed& operator=(const ALLuauShadowed&) = delete;

    // Whether what autocomplete `found` at `at` took of `scope` by the
    // table's order: two of its locals of one name it would offer there.
    // For a scope Luau makes and reads in the one call, which nothing can
    // set right before it is read: a fragment's own.
    static bool ambiguous(const Luau::Scope& scope, Luau::Position at, const Luau::AutocompleteResult& found);

private:
    using Bindings = decltype(Luau::Scope::bindings);
    std::vector<std::pair<Luau::Scope*, Bindings::node_type>> mAside;
};
