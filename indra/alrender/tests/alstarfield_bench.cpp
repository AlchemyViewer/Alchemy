/**
 * @file alstarfield_bench.cpp
 * @brief What laying the star field out costs, on the main thread at login and at every RenderStarCount change: the
 *        default count and the most a count may ask for.
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

#include "../alstarfield.h"

#include "altestbenchmark.h"

#include <fmt/format.h>

#include <vector>

namespace
{
    // LLSettingsSky's DOME_RADIUS.
    constexpr F32 DOME_RADIUS = 15000.f;

    TEST_CASE("alstarfield_bench: the field laid out at the default count and the most", "[alstarfield_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        for (const U32 count : { ALStarField::DEFAULT_COUNT, ALStarField::MAX_COUNT })
        {
            const std::vector<ALStarField::Star> field = ALStarField::generate(count, DOME_RADIUS);
            // Each run lays out a field of its own, as generated: layOut orders the stars it is given in place.
            BENCHMARK_ADVANCED(fmt::format("layOut, {} stars", field.size()))(Catch::Benchmark::Chronometer meter)
            {
                std::vector<std::vector<ALStarField::Star>> fields(meter.runs(), field);
                meter.measure([&](int i) { return ALStarField::layOut(fields[i]).mQuads; });
            };
            BENCHMARK(fmt::format("generate, {} stars", field.size()))
            {
                return ALStarField::generate(count, DOME_RADIUS).size();
            };
        }
    }
}
