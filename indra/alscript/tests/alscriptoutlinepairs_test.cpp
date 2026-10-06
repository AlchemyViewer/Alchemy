/**
 * @file alscriptoutlinepairs_test.cpp
 * @brief Two outlines' functions, events and states paired by what they are.
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

#include "../core/alscriptoutlinepairs.h"

#include "../test/lltut.h"

namespace tut
{
    struct alscriptoutlinepairs_data
    {
        typedef ALScriptOutlinePairs::Pair Pair;

        // A symbol from its first line to its last, its end past the last
        // line's first character.
        static ALScriptOutlineEntry symbol(const char* name, ALScriptSymbolKind kind, S32 first, S32 last, S32 depth = 0)
        {
            ALScriptOutlineEntry entry;
            entry.name           = name;
            entry.kind           = kind;
            entry.depth          = depth;
            entry.span.line      = first;
            entry.span.endLine   = last;
            entry.span.endColumn = 1;
            entry.nameSpan       = entry.span;
            return entry;
        }
    };
    typedef test_group<alscriptoutlinepairs_data> alscriptoutlinepairs_group;
    typedef alscriptoutlinepairs_group::object    alscriptoutlinepairs_object;
    alscriptoutlinepairs_group                    alscriptoutlinepairs_instance("alscriptoutlinepairs");

    template<> template<>
    void alscriptoutlinepairs_object::test<1>()
    {
        set_test_name("functions paired by name, wherever each now is; one on a side alone, and a global, not paired");
        typedef ALScriptSymbolKind K;
        const std::vector<ALScriptOutlineEntry> left  = { symbol("count", K::Variable, 0, 0), symbol("a", K::Function, 1, 4), symbol("b", K::Function, 5, 9),
                                                          symbol("gone", K::Function, 10, 12) };
        const std::vector<ALScriptOutlineEntry> right = { symbol("count", K::Variable, 0, 0), symbol("b", K::Function, 1, 5), symbol("made", K::Function, 6, 7),
                                                          symbol("a", K::Function, 8, 12) };
        const std::vector<Pair> pairs = ALScriptOutlinePairs::pair(left, right);
        ensure_equals("two", pairs.size(), size_t(2));
        ensure("a with a, in the left's order", pairs[0] == Pair{ 1, 4, 8, 12 });
        ensure("b with b", pairs[1] == Pair{ 5, 9, 1, 5 });
    }

    template<> template<>
    void alscriptoutlinepairs_object::test<2>()
    {
        set_test_name("an event by its state, a function by the function it is in; of a name twice, the first with the first; a function not an event of the name");
        typedef ALScriptSymbolKind K;
        const std::vector<ALScriptOutlineEntry> left = {
            symbol("default", K::State, 0, 9),       symbol("state_entry", K::Event, 1, 3, 1), symbol("touch_start", K::Event, 4, 8, 1),
            symbol("running", K::State, 10, 14),     symbol("state_entry", K::Event, 11, 13, 1), symbol("helper", K::Function, 15, 20),
            symbol("inner", K::Function, 16, 18, 1), symbol("helper", K::Function, 21, 22),      symbol("touch_start", K::Function, 23, 24),
        };
        const std::vector<ALScriptOutlineEntry> right = {
            symbol("running", K::State, 0, 4),       symbol("state_entry", K::Event, 1, 3, 1), symbol("default", K::State, 5, 12),
            symbol("state_entry", K::Event, 6, 8, 1), symbol("helper", K::Function, 13, 14),     symbol("helper", K::Function, 15, 19),
            symbol("inner", K::Function, 16, 18, 1), symbol("touch_start", K::Event, 20, 21),
        };
        const std::vector<Pair> pairs = ALScriptOutlinePairs::pair(left, right);
        ensure_equals("six", pairs.size(), size_t(6));
        ensure("default with default", pairs[0] == Pair{ 0, 9, 5, 12 });
        ensure("its state_entry with its own, not running's", pairs[1] == Pair{ 1, 3, 6, 8 });
        ensure("running with running, and its state_entry", pairs[2] == Pair{ 10, 14, 0, 4 } && pairs[3] == Pair{ 11, 13, 1, 3 });
        ensure("the first helper with the first", pairs[4] == Pair{ 15, 20, 13, 14 });
        ensure("inner by the helper it is in, the first; the second helper with the second", pairs[5] == Pair{ 21, 22, 15, 19 });
    }

    template<> template<>
    void alscriptoutlinepairs_object::test<3>()
    {
        set_test_name("a span ending at the start of a line ends on the line before");
        ALScriptOutlineEntry left  = symbol("a", ALScriptSymbolKind::Function, 2, 6);
        ALScriptOutlineEntry right = symbol("a", ALScriptSymbolKind::Function, 3, 8);
        left.span.endColumn        = 0;
        const std::vector<Pair> pairs = ALScriptOutlinePairs::pair({ left }, { right });
        ensure("its last line the one before", pairs.size() == 1 && pairs[0] == Pair{ 2, 5, 3, 8 });
    }
}
