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

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct alprefixsums_data
    {
        // What the sums should say, added up the long way.
        static S32 before(const std::vector<S32>& values, size_t index)
        {
            S32 sum = 0;
            for (size_t i = 0; i < index && i < values.size(); ++i)
            {
                sum += values[i];
            }
            return sum;
        }
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
        static void same(const std::string& what, const ALPrefixSums<S32>& sums, const std::vector<S32>& values, bool sampled = false)
        {
            ensure_equals(what + ": size", sums.size(), values.size());
            const size_t step = sampled ? values.size() / 7 + 1 : 1;
            S32          sum  = 0;
            for (size_t i = 0; i <= values.size(); ++i)
            {
                if (i % step == 0 || i == values.size())
                {
                    ensure_equals(what + ": before " + std::to_string(i), sums.before(i), sum);
                    if (i < values.size())
                    {
                        ensure_equals(what + ": at " + std::to_string(i), sums.at(i), values[i]);
                    }
                }
                if (i < values.size())
                {
                    sum += values[i];
                }
            }
            ensure_equals(what + ": total", sums.total(), sum);
            for (S32 s = -1; s <= sum + 2; s += sampled ? sum / 11 + 1 : 1)
            {
                ensure_equals(what + ": reach " + std::to_string(s), sums.reach(s), reach(values, s));
            }
        }
    };

    typedef test_group<alprefixsums_data> alprefixsums_group;
    typedef alprefixsums_group::object    alprefixsums_object;
    alprefixsums_group                    alprefixsums_instance("alprefixsums");

    template<> template<>
    void alprefixsums_object::test<1>()
    {
        set_test_name("the sums before every place, and the place every sum reaches, are what adding up finds; nothing and one are runs too");
        ALPrefixSums<S32> sums;
        same("none", sums, {});
        sums.assign({ 3, 0, 0, 2 });
        ensure_equals("past the run of nothing", sums.reach(3), size_t(3));
        ensure_equals("within the first", sums.reach(2), size_t(0));
        ensure_equals("past them all", sums.reach(5), size_t(4));
        sums.assign({ 4 });
        same("one", sums, { 4 });
        std::vector<S32> many;
        for (S32 i = 0; i < 1000; ++i)
        {
            many.push_back((i * 7) % 5);
        }
        sums.assign(many);
        same("many blocks", sums, many);
    }

    template<> template<>
    void alprefixsums_object::test<2>()
    {
        set_test_name("numbers changed, put in and taken out anywhere, a few or many, leave the sums adding up finds");
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
            same("step " + std::to_string(step), sums, values, true);
        }
        same("at the end, every place", sums, values);
        sums.erase(0, sums.size());
        same("all taken out", sums, {});
        sums.insert(0, { 2, 3 });
        same("put in again", sums, { 2, 3 });
    }
}
