/**
 * @file allinetable_test.cpp
 * @brief One row per line of a text, kept in step with its edits.
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

#include "allinetable.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct allinetable_data
    {
        typedef ALLineTable<std::string> table_t;

        // Rows named for their lines: "0", "1", ...
        static table_t numbered(S32 count)
        {
            table_t table;
            for (S32 i = 0; i < count; ++i)
            {
                table.push_back(std::to_string(i));
            }
            return table;
        }

        static std::string joined(const table_t& table)
        {
            std::string out;
            for (const std::string& row : table)
            {
                out += (out.empty() ? "" : ",") + row;
            }
            return out;
        }
    };
    typedef test_group<allinetable_data> allinetable_group;
    typedef allinetable_group::object    allinetable_object;
    allinetable_group                    allinetable_instance("allinetable");

    template<> template<>
    void allinetable_object::test<1>()
    {
        set_test_name("an edit that makes as many lines as it replaced assigns them in place, and the rest stay where they are");
        table_t          table = numbered(6);
        const std::string* data = &table[0];
        const auto       done  = table.apply(2, 3, 2, 6, "x");
        ensure_equals("in place", joined(table), std::string("0,1,x,x,4,5"));
        ensure("nothing moved", &table[0] == data);
        ensure("said", done.first == 2 && done.count == 2 && done.made == 2);
        table.apply(4, 4, 1, 6, "t");
        ensure_equals("a line typed in", joined(table), std::string("0,1,x,x,t,5"));
    }

    template<> template<>
    void allinetable_object::test<2>()
    {
        set_test_name("more lines made moves the rows below down once; fewer, up once; the text's count the table's");
        table_t table = numbered(5);
        table.apply(1, 1, 3, 7, "n");
        ensure_equals("a line broken twice", joined(table), std::string("0,n,n,n,2,3,4"));
        table.apply(1, 3, 1, 5, "j");
        ensure_equals("joined again", joined(table), std::string("0,j,2,3,4"));
        table.apply(0, 4, 1, 1, "all");
        ensure_equals("everything replaced by a line", joined(table), std::string("all"));
        table.apply(0, 0, 3, -1, "k");
        ensure_equals("as long as the edit makes it, where no count is given", joined(table), std::string("k,k,k"));
    }

    template<> template<>
    void allinetable_object::test<3>()
    {
        set_test_name("a table kept only as far as it was told of lines: what it lacked is the other row, before and after the edit");
        ALLineTable<int> changed;
        changed.push_back(0);
        changed.apply(3, 3, 2, 6, 1, 0);
        ensure("the rows it lacked up to the edit are other, the lines made the fill, and on to the text's end",
               std::vector<int>(changed.begin(), changed.end()) == std::vector<int>({ 0, 0, 0, 1, 1, 0 }));
        ALLineTable<int> part(2, 7);
        part.apply(1, 4, 1, 3, 1, 0);
        ensure("an edit past the table's end replaces what it had of it", std::vector<int>(part.begin(), part.end()) == std::vector<int>({ 7, 1, 0 }));
    }

    template<> template<>
    void allinetable_object::test<4>()
    {
        set_test_name("rows moved in from elsewhere, as the text's own lines are");
        table_t                  table = numbered(4);
        std::vector<std::string> made  = { "a", "b", "c" };
        table.replace(1, 2, std::make_move_iterator(made.begin()), std::make_move_iterator(made.end()));
        ensure_equals("in place and one more", joined(table), std::string("0,a,b,c,3"));
        std::vector<std::string> one = { "z" };
        table.replace(0, 3, one.begin(), one.end());
        ensure_equals("three for one", joined(table), std::string("z,c,3"));
    }

    template<> template<>
    void allinetable_object::test<5>()
    {
        set_test_name("a line broken or joined where the last one was moves no row below it; one further down only the rows between");
        table_t table = numbered(1000);
        table.apply(10, 10, 2, 1001, "a");
        const std::string* below = &table[600];
        table.apply(11, 11, 2, 1002, "b");
        ensure_equals("broken again, just under", table[601], std::string("599"));
        ensure("nothing below moved", &table[601] == below);
        table.apply(11, 12, 1, 1001, "c");
        ensure("joined there, nothing below moved", &table[600] == below);
        table.apply(700, 700, 2, 1002, "d");
        ensure_equals("broken further down", table[701], std::string("d"));
        ensure_equals("the rows between in their places", table[600], std::string("599"));
        ensure_equals("and those after", table[702], std::string("700"));
        ensure_equals("the count", table.size(), size_t(1002));
    }

    template<> template<>
    void allinetable_object::test<6>()
    {
        set_test_name("edits anywhere, of every size, leave the rows a plain vector would hold");
        table_t                  table = numbered(50);
        std::vector<std::string> plain;
        for (S32 i = 0; i < 50; ++i)
        {
            plain.push_back(std::to_string(i));
        }
        U32 seed = 12345;
        const auto next = [&seed](U32 below) {
            seed = seed * 1664525u + 1013904223u;
            return below == 0 ? 0 : (seed >> 8) % below;
        };
        for (S32 step = 0; step < 3000; ++step)
        {
            const std::string fill = "s" + std::to_string(step);
            const U32         kind = next(10);
            const S32         size = static_cast<S32>(plain.size());
            if (kind < 7 && size > 0)
            {
                // An edit over some lines making as many, more or fewer; now
                // and then a paste of more than the gap would take, or most
                // of the text taken out.
                const S32 first = static_cast<S32>(next(static_cast<U32>(size)));
                const S32 last  = first + static_cast<S32>(next(static_cast<U32>(next(40) == 0 ? size - first : llmin(size - first, 6))));
                const S32 made  = static_cast<S32>(next(30) == 0 ? 64 + next(300) : next(8));
                const S32 lines = size - (last - first + 1) + made;
                table.apply(first, last, made, lines, fill);
                plain.erase(plain.begin() + first, plain.begin() + last + 1);
                plain.insert(plain.begin() + first, static_cast<size_t>(made), fill);
                plain.resize(static_cast<size_t>(lines));
            }
            else if (kind == 7)
            {
                // A batch's runs, apart.
                struct Span
                {
                    S32 first;
                    S32 last;
                    S32 made;
                };
                std::vector<Span> spans;
                S32               at = 0;
                while (at < size && spans.size() < 4)
                {
                    const S32 first = at + static_cast<S32>(next(10));
                    if (first >= size)
                    {
                        break;
                    }
                    const S32 last = first + static_cast<S32>(next(static_cast<U32>(llmin(size - first, 3))));
                    spans.push_back(Span{ first, last, static_cast<S32>(next(4)) });
                    at = last + 2;
                }
                S32 lines = size;
                for (const Span& s : spans)
                {
                    lines += s.made - (s.last - s.first + 1);
                }
                table.applySpans(spans, lines, fill);
                for (auto s = spans.rbegin(); s != spans.rend(); ++s)
                {
                    plain.erase(plain.begin() + s->first, plain.begin() + s->last + 1);
                    plain.insert(plain.begin() + s->first, static_cast<size_t>(s->made), fill);
                }
            }
            else if (kind == 8)
            {
                const size_t n = next(static_cast<U32>(size + 10));
                table.resize(n, fill);
                plain.resize(n, fill);
            }
            else
            {
                table.push_back(fill);
                plain.push_back(fill);
            }
            ensure_equals("as many", table.size(), plain.size());
            if (std::vector<std::string>(table.begin(), table.end()) != plain)
            {
                ensure(("the same at step " + std::to_string(step)).c_str(), false);
            }
        }
        std::vector<std::string> swapped;
        table.swap(swapped);
        ensure("swapped out whole, in order", swapped == plain);
    }
}
