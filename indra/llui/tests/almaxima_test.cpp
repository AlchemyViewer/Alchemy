/**
 * @file almaxima_test.cpp
 * @brief Tests for ALMaxima: the greatest of a run kept through changes, insertions and removals.
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

#include "almaxima.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    struct almaxima_data
    {
        // Every place's value, or a few of them for `sampled`, and the
        // greatest as looking at them all finds it.
        static void require_same(const std::string& what, const ALMaxima<S32>& maxima, const std::vector<S32>& values, bool sampled = false)
        {
            INFO(what);
            REQUIRE(maxima.size() == values.size());
            const size_t step = sampled ? values.size() / 7 + 1 : 1;
            for (size_t i = 0; i < values.size(); i += step)
            {
                CAPTURE(i);
                REQUIRE(maxima.at(i) == values[i]);
            }
            REQUIRE(maxima.most() == (values.empty() ? 0 : *std::max_element(values.begin(), values.end())));
        }
    };

    TEST_CASE_METHOD(almaxima_data, "almaxima: the greatest of none is zero and the next takes over when it is cut down", "[almaxima]")
    {
        ALMaxima<S32> maxima;
        require_same("none", maxima, {});
        std::vector<S32> many;
        for (S32 i = 0; i < 1000; ++i)
        {
            many.push_back((i * 7) % 50);
        }
        many[613] = 90;
        maxima.assign(many);
        require_same("many blocks", maxima, many);
        many[613] = 10;
        maxima.set(613, 10);
        require_same("the greatest cut down", maxima, many);
        many[2] = 60;
        maxima.set(2, 60);
        require_same("another raised past it", maxima, many);
    }

    TEST_CASE_METHOD(almaxima_data, "almaxima: random sets and inserts and erases leave the greatest a full scan finds", "[almaxima]")
    {
        std::vector<S32> values(700, 1);
        ALMaxima<S32>    maxima;
        maxima.assign(values);
        U32 seed = 54321;
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
                // Now and then the greatest itself, cut down or raised.
                const size_t at    = next(4) == 0 ? static_cast<size_t>(std::max_element(values.begin(), values.end()) - values.begin()) : index;
                const S32    value = static_cast<S32>(next(200));
                values[at]         = value;
                maxima.set(at, value);
            }
            else if (kind < 7)
            {
                // A line or two typed in, now and then a paste of hundreds.
                std::vector<S32> made(next(10) == 0 ? next(600) : next(3) + 1);
                for (S32& value : made)
                {
                    value = static_cast<S32>(next(200));
                }
                values.insert(values.begin() + static_cast<std::ptrdiff_t>(index), made.begin(), made.end());
                maxima.insert(index, made);
            }
            else
            {
                const size_t count = next(10) == 0 ? next(500) : next(4);
                const size_t taken = std::min(count, values.size() - index);
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(index), values.begin() + static_cast<std::ptrdiff_t>(index + taken));
                maxima.erase(index, count);
            }
            CAPTURE(step);
            require_same("after a step", maxima, values, true);
        }
        require_same("at the end, every place", maxima, values);
        maxima.erase(0, maxima.size());
        require_same("all taken out", maxima, {});
        maxima.insert(0, { 2, 3 });
        require_same("put in again", maxima, { 2, 3 });
    }
}
