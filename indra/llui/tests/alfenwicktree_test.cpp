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

#include "alfenwicktree.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
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

        static void require_same(const std::string& what, const ALFenwickTree<S32>& tree, const std::vector<S32>& values)
        {
            INFO(what);
            REQUIRE(tree.size() == values.size());
            for (size_t i = 0; i <= values.size(); ++i)
            {
                CAPTURE(i);
                REQUIRE(tree.before(i) == before(values, i));
            }
            const S32 total = before(values, values.size());
            REQUIRE(tree.total() == total);
            for (S32 sum = -1; sum <= total + 2; ++sum)
            {
                CAPTURE(sum);
                REQUIRE(tree.reach(sum) == reach(values, sum));
            }
        }
    };

    TEST_CASE_METHOD(alfenwicktree_data, "alfenwicktree: before and reach agree with plain sums through single changes", "[alfenwicktree]")
    {
        std::vector<S32> values;
        for (S32 i = 0; i < 37; ++i)
        {
            values.push_back((i * 7) % 5);
        }
        ALFenwickTree<S32> tree;
        tree.assign(values);
        require_same("made", tree, values);
        U32 seed = 12345;
        for (S32 step = 0; step < 200; ++step)
        {
            seed               = seed * 1103515245 + 12345;
            const size_t index = (seed >> 8) % values.size();
            const S32    value = static_cast<S32>((seed >> 20) % 9);
            values[index]      = value;
            tree.set(index, value);
            REQUIRE(tree.at(index) == value);
        }
        require_same("changed", tree, values);
    }

    TEST_CASE_METHOD(alfenwicktree_data, "alfenwicktree: reach passes a run of zeros whole and empty or one-value trees work", "[alfenwicktree]")
    {
        ALFenwickTree<S32> tree;
        require_same("none", tree, {});
        tree.assign({ 3, 0, 0, 2 });
        UNSCOPED_INFO("past the run of nothing");
        CHECK(tree.reach(3) == size_t(3));
        UNSCOPED_INFO("within the first");
        CHECK(tree.reach(2) == size_t(0));
        UNSCOPED_INFO("past them all");
        CHECK(tree.reach(5) == size_t(4));
        tree.assign({ 4 });
        require_same("one", tree, { 4 });
    }
}
