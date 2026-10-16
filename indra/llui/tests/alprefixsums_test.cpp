/**
 * @file alprefixsums_test.cpp
 * @brief Tests for ALPrefixSums: sums before every place and places sums reach, through changes, insertions and removals.
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

#include "alprefixsums.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alprefixsums_data
    {
        // The place a sum reaches, found the long way.
        static size_t reach(const std::vector<S32>& values, S32 sum)
        {
            size_t at  = 0;
            S32    ran = 0;
            while (at < values.size() && ran + values[at] <= sum)
            {
                ran += values[at];
                ++at;
            }
            return at;
        }

        // Every place's sum and value, and the place each sum reaches; or,
        // `sampled`, a few of each, for a long run.
        static void require_same(const std::string& what, const ALPrefixSums<S32>& sums, const std::vector<S32>& values, bool sampled = false)
        {
            INFO(what);
            REQUIRE(sums.size() == values.size());
            const size_t step = sampled ? values.size() / 7 + 1 : 1;
            S32          sum  = 0;
            for (size_t i = 0; i <= values.size(); ++i)
            {
                if (i % step == 0 || i == values.size())
                {
                    CAPTURE(i);
                    REQUIRE(sums.before(i) == sum);
                    if (i < values.size())
                    {
                        REQUIRE(sums.at(i) == values[i]);
                    }
                }
                if (i < values.size())
                {
                    sum += values[i];
                }
            }
            REQUIRE(sums.total() == sum);
            for (S32 s = -1; s <= sum + 2; s += sampled ? sum / 11 + 1 : 1)
            {
                CAPTURE(s);
                REQUIRE(sums.reach(s) == reach(values, s));
            }
        }
    };

    TEST_CASE_METHOD(alprefixsums_data, "alprefixsums: before and reach match a plain sum for none and one and many values", "[alprefixsums]")
    {
        ALPrefixSums<S32> sums;
        require_same("none", sums, {});
        sums.assign({ 3, 0, 0, 2 });
        UNSCOPED_INFO("past the run of nothing");
        CHECK(sums.reach(3) == size_t(3));
        UNSCOPED_INFO("within the first");
        CHECK(sums.reach(2) == size_t(0));
        UNSCOPED_INFO("past them all");
        CHECK(sums.reach(5) == size_t(4));
        sums.assign({ 4 });
        require_same("one", sums, { 4 });
        std::vector<S32> many;
        for (S32 i = 0; i < 1000; ++i)
        {
            many.push_back((i * 7) % 5);
        }
        sums.assign(many);
        require_same("many blocks", sums, many);
    }

    TEST_CASE_METHOD(alprefixsums_data, "alprefixsums: sums stay right through 4000 random edits of any size", "[alprefixsums]")
    {
        std::vector<S32>  values(700, 1);
        ALPrefixSums<S32> sums;
        sums.assign(values);
        U32 seed = 12345;
        const auto next = [&seed](U32 below) {
            seed = seed * 1103515245u + 12345u;
            return below == 0 ? 0 : (seed >> 8) % below;
        };
        for (S32 step = 0; step < 4000; ++step)
        {
            const U32    kind  = next(10);
            const size_t index = next(static_cast<U32>(values.size() + 1));
            if (kind < 4 && index < values.size())
            {
                const S32 value = static_cast<S32>(next(9));
                values[index]   = value;
                sums.set(index, value);
            }
            else if (kind < 7)
            {
                // A line or two typed in, now and then a paste of hundreds.
                std::vector<S32> made(next(10) == 0 ? next(600) : next(3) + 1);
                for (S32& value : made)
                {
                    value = static_cast<S32>(next(4));
                }
                values.insert(values.begin() + static_cast<std::ptrdiff_t>(index), made.begin(), made.end());
                sums.insert(index, made);
            }
            else
            {
                const size_t count = next(10) == 0 ? next(500) : next(4);
                const size_t taken = std::min(count, values.size() - index);
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(index), values.begin() + static_cast<std::ptrdiff_t>(index + taken));
                sums.erase(index, count);
            }
            CAPTURE(step);
            require_same("after a step", sums, values, true);
        }
        require_same("at the end, every place", sums, values);
        sums.erase(0, sums.size());
        require_same("all taken out", sums, {});
        sums.insert(0, { 2, 3 });
        require_same("put in again", sums, { 2, 3 });
    }
}
