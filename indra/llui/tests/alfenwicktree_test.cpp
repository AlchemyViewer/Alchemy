/**
 * @file tests/alfenwicktree_test.cpp
 * @brief Sums of a run of numbers that change one at a time.
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

#include "../alfenwicktree.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct alfenwicktree_data
    {
        // What the tree should say, added up the long way.
        static S32 before(const std::vector<S32>& values, size_t index)
        {
            S32 sum = 0;
            for (size_t i = 0; i < index; ++i)
            {
                sum += values[i];
            }
            return sum;
        }
        static size_t reach(const std::vector<S32>& values, S32 sum)
        {
            size_t at = 0;
            while (at < values.size() && before(values, at + 1) <= sum)
            {
                ++at;
            }
            return at;
        }

        static void same(const std::string& what, const ALFenwickTree<S32>& tree, const std::vector<S32>& values)
        {
            ensure_equals(what + ": size", tree.size(), values.size());
            for (size_t i = 0; i <= values.size(); ++i)
            {
                ensure_equals(what + ": before " + std::to_string(i), tree.before(i), before(values, i));
            }
            const S32 total = before(values, values.size());
            ensure_equals(what + ": total", tree.total(), total);
            for (S32 sum = -1; sum <= total + 2; ++sum)
            {
                ensure_equals(what + ": reach " + std::to_string(sum), tree.reach(sum), reach(values, sum));
            }
        }
    };

    typedef test_group<alfenwicktree_data> alfenwicktree_group;
    typedef alfenwicktree_group::object    alfenwicktree_object;
    alfenwicktree_group                    alfenwicktree_instance("alfenwicktree");

    template<> template<>
    void alfenwicktree_object::test<1>()
    {
        set_test_name("the sums before every place, and the place every sum reaches, are what adding up finds, through changes one at a time");
        std::vector<S32> values;
        for (S32 i = 0; i < 37; ++i)
        {
            values.push_back((i * 7) % 5);
        }
        ALFenwickTree<S32> tree;
        tree.assign(values);
        same("made", tree, values);
        U32 seed = 12345;
        for (S32 step = 0; step < 200; ++step)
        {
            seed               = seed * 1103515245 + 12345;
            const size_t index = (seed >> 8) % values.size();
            const S32    value = static_cast<S32>((seed >> 20) % 9);
            values[index]      = value;
            tree.set(index, value);
            ensure_equals("the value kept", tree.at(index), value);
        }
        same("changed", tree, values);
    }

    template<> template<>
    void alfenwicktree_object::test<2>()
    {
        set_test_name("a run that sums to nothing is passed whole, nothing and one are trees too");
        ALFenwickTree<S32> tree;
        same("none", tree, {});
        tree.assign({ 3, 0, 0, 2 });
        ensure_equals("past the run of nothing", tree.reach(3), size_t(3));
        ensure_equals("within the first", tree.reach(2), size_t(0));
        ensure_equals("past them all", tree.reach(5), size_t(4));
        tree.assign({ 4 });
        same("one", tree, { 4 });
    }
}
