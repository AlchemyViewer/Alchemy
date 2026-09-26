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

#include "../allinetable.h"

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
               changed.rows() == std::vector<int>({ 0, 0, 0, 1, 1, 0 }));
        ALLineTable<int> part(2, 7);
        part.apply(1, 4, 1, 3, 1, 0);
        ensure("an edit past the table's end replaces what it had of it", part.rows() == std::vector<int>({ 7, 1, 0 }));
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
}
