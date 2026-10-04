/**
 * @file allslshapes.h
 * @brief The LSL optimizer's shapes made for size.
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

#include "allsloptimizerpass.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace ALLSLPasses
{
    // Which of Mono's list helpers a script calls, each by what Tailslide's
    // Mono compiler calls one for: a literal, an empty list, a cast to a
    // list and one of a list to a string, a list's literal among the
    // globals, something added to a list -- a helper for each type added
    // -- and something added before one.
    // The assembly references each once, which is what a list's shapes can
    // cost that they do not save at each place: a helper nothing in the
    // script called before.
    struct ListHelpers
    {
        enum : U32
        {
            LITERAL        = 1u << 8,
            EMPTY          = 2u << 8,
            GLOBAL_LITERAL = 3u << 8,
            CAST           = 4u << 8,
            PREPEND        = 5u << 8,
            APPEND         = 6u << 8,
            TO_STRING      = 7u << 8
        };

        boost::unordered_flat_set<U32> used;
        // Lists dumped with nothing between their elements, each a cast to
        // a string it may be.
        S32                            dumps = 0;

        bool castsToString() const { return used.contains(TO_STRING); }

        // How many of `after`'s helpers are not among these.
        S32 freshIn(const ListHelpers& after) const
        {
            return static_cast<S32>(std::count_if(after.used.begin(), after.used.end(), [this](U32 helper) { return !used.contains(helper); }));
        }
    };
    ListHelpers listHelpers(LSLScript* script);

    // Rewrites made for size alone, once the rounds are done: integers',
    // conditions' and increments' shapes (Values), and lists' (Lists),
    // which on Mono may bring in helpers the script did not call before
    // -- `had`, where that is weighed. How many changes they made.
    enum class ShapeStage : U8
    {
        Values,
        Lists
    };
    int shape(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script, ShapeStage stage, const ListHelpers* had = nullptr);
}
